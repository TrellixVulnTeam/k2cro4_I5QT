#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Entry point for both build and try bots

This script is invoked from XXX, usually without arguments
to package an SDK. It automatically determines whether
this SDK is for mac, win, linux.

The script inspects the following environment variables:

BUILDBOT_BUILDERNAME to determine whether the script is run locally
and whether it should upload an SDK to file storage (GSTORE)
"""

# pylint: disable=W0621

# std python includes
import copy
import datetime
import optparse
import os
import re
import subprocess
import sys

# local includes
import buildbot_common
import build_updater
import build_utils
import generate_make
import generate_notice
import manifest_util
import test_sdk

# Create the various paths of interest
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SDK_SRC_DIR = os.path.dirname(SCRIPT_DIR)
SDK_EXAMPLE_DIR = os.path.join(SDK_SRC_DIR, 'examples')
SDK_LIBRARY_DIR = os.path.join(SDK_SRC_DIR, 'libraries')
SDK_DIR = os.path.dirname(SDK_SRC_DIR)
SRC_DIR = os.path.dirname(SDK_DIR)
NACL_DIR = os.path.join(SRC_DIR, 'native_client')
OUT_DIR = os.path.join(SRC_DIR, 'out')
PPAPI_DIR = os.path.join(SRC_DIR, 'ppapi')


# Add SDK make tools scripts to the python path.
sys.path.append(os.path.join(SDK_SRC_DIR, 'tools'))
sys.path.append(os.path.join(NACL_DIR, 'build'))

import getos
import http_download
import oshelpers

GSTORE = 'https://commondatastorage.googleapis.com/nativeclient-mirror/nacl/'
MAKE = 'nacl_sdk/make_3_81/make.exe'
CYGTAR = os.path.join(NACL_DIR, 'build', 'cygtar.py')


options = None


def GetGlibcToolchain(platform, arch):
  tcdir = os.path.join(NACL_DIR, 'toolchain', '.tars')
  tcname = 'toolchain_%s_%s.tar.bz2' % (platform, arch)
  return os.path.join(tcdir, tcname)


def GetNewlibToolchain(platform, arch):
  tcdir = os.path.join(NACL_DIR, 'toolchain', '.tars')
  tcname = 'naclsdk_%s_%s.tgz' % (platform, arch)
  return os.path.join(tcdir, tcname)


def GetPNaClToolchain(os_platform, arch):
  tcdir = os.path.join(NACL_DIR, 'toolchain', '.tars')
  tcname = 'naclsdk_pnacl_%s_%s.tgz' % (os_platform, arch)
  return os.path.join(tcdir, tcname)


def GetScons():
  if sys.platform in ['cygwin', 'win32']:
    return 'scons.bat'
  return './scons'


def GetArchName(arch, xarch=None):
  if xarch:
    return arch + '-' + str(xarch)
  return arch


def GetToolchainNaClInclude(tcname, tcpath, arch, xarch=None):
  if arch == 'x86':
    if tcname == 'pnacl':
      return os.path.join(tcpath, 'newlib', 'sdk', 'include')
    return os.path.join(tcpath, 'x86_64-nacl', 'include')
  else:
    buildbot_common.ErrorExit('Unknown architecture.')


def GetToolchainNaClLib(tcname, tcpath, arch, xarch):
  if arch == 'x86':
    if tcname == 'pnacl':
      return os.path.join(tcpath, 'newlib', 'sdk', 'lib')
    if str(xarch) == '32':
      return os.path.join(tcpath, 'x86_64-nacl', 'lib32')
    if str(xarch) == '64':
      return os.path.join(tcpath, 'x86_64-nacl', 'lib')
  buildbot_common.ErrorExit('Unknown architecture.')


def GetPNaClNativeLib(tcpath, arch):
  if arch not in ['arm', 'x86-32', 'x86-64']:
    buildbot_common.ErrorExit('Unknown architecture %s.' % arch)
  return os.path.join(tcpath, 'lib-' + arch)


def GetBuildArgs(tcname, tcpath, outdir, arch, xarch=None):
  """Return list of scons build arguments to generate user libraries."""
  scons = GetScons()
  mode = '--mode=opt-host,nacl'
  arch_name = GetArchName(arch, xarch)
  plat = 'platform=' + arch_name
  binarg = 'bindir=' + os.path.join(outdir, 'tools')
  lib = 'libdir=' + GetToolchainNaClLib(tcname, tcpath, arch, xarch)
  args = [scons, mode, plat, binarg, lib, '-j10',
          'install_bin', 'install_lib']
  if tcname == 'glibc':
    args.append('--nacl_glibc')

  if tcname == 'pnacl':
    args.append('bitcode=1')

  print "Building %s (%s): %s" % (tcname, arch, ' '.join(args))
  return args


def BuildStepDownloadToolchains(platform):
  buildbot_common.BuildStep('Rerun hooks to get toolchains')
  buildbot_common.Run(['gclient', 'runhooks'],
                      cwd=SRC_DIR, shell=(platform == 'win'))


def BuildStepCleanPepperDirs(pepperdir, pepperdir_old):
  buildbot_common.BuildStep('Clean Pepper Dirs')
  buildbot_common.RemoveDir(pepperdir_old)
  buildbot_common.RemoveDir(pepperdir)
  buildbot_common.MakeDir(pepperdir)


def BuildStepMakePepperDirs(pepperdir, subdirs):
  for subdir in subdirs:
    buildbot_common.MakeDir(os.path.join(pepperdir, subdir))


def BuildStepCopyTextFiles(pepperdir, pepper_ver, revision):
  buildbot_common.BuildStep('Add Text Files')
  files = ['AUTHORS', 'COPYING', 'LICENSE']
  files = [os.path.join(SDK_SRC_DIR, filename) for filename in files]
  oshelpers.Copy(['-v'] + files + [pepperdir])

  # Replace a few placeholders in README
  readme_text = open(os.path.join(SDK_SRC_DIR, 'README'), 'rt').read()
  readme_text = readme_text.replace('${VERSION}', pepper_ver)
  readme_text = readme_text.replace('${REVISION}', revision)

  # Year/Month/Day Hour:Minute:Second
  time_format = '%Y/%m/%d %H:%M:%S'
  readme_text = readme_text.replace('${DATE}',
      datetime.datetime.now().strftime(time_format))

  open(os.path.join(pepperdir, 'README'), 'wt').write(readme_text)


def BuildStepUntarToolchains(pepperdir, platform, arch, toolchains):
  buildbot_common.BuildStep('Untar Toolchains')
  tcname = platform + '_' + arch
  tmpdir = os.path.join(SRC_DIR, 'out', 'tc_temp')
  buildbot_common.RemoveDir(tmpdir)
  buildbot_common.MakeDir(tmpdir)

  if 'newlib' in toolchains:
    # Untar the newlib toolchains
    tarfile = GetNewlibToolchain(platform, arch)
    buildbot_common.Run([sys.executable, CYGTAR, '-C', tmpdir, '-xf', tarfile],
                        cwd=NACL_DIR)

    # Then rename/move it to the pepper toolchain directory
    srcdir = os.path.join(tmpdir, 'sdk', 'nacl-sdk')
    newlibdir = os.path.join(pepperdir, 'toolchain', tcname + '_newlib')
    buildbot_common.Move(srcdir, newlibdir)

  if 'glibc' in toolchains:
    # Untar the glibc toolchains
    tarfile = GetGlibcToolchain(platform, arch)
    buildbot_common.Run([sys.executable, CYGTAR, '-C', tmpdir, '-xf', tarfile],
                        cwd=NACL_DIR)

    # Then rename/move it to the pepper toolchain directory
    srcdir = os.path.join(tmpdir, 'toolchain', tcname)
    glibcdir = os.path.join(pepperdir, 'toolchain', tcname + '_glibc')
    buildbot_common.Move(srcdir, glibcdir)

  # Untar the pnacl toolchains
  if 'pnacl' in toolchains:
    tmpdir = os.path.join(tmpdir, 'pnacl')
    buildbot_common.RemoveDir(tmpdir)
    buildbot_common.MakeDir(tmpdir)
    tarfile = GetPNaClToolchain(platform, arch)
    buildbot_common.Run([sys.executable, CYGTAR, '-C', tmpdir, '-xf', tarfile],
                        cwd=NACL_DIR)

    # Then rename/move it to the pepper toolchain directory
    pnacldir = os.path.join(pepperdir, 'toolchain', tcname + '_pnacl')
    buildbot_common.Move(tmpdir, pnacldir)

  if options.gyp and sys.platform not in ['cygwin', 'win32']:
    # If the gyp options is specified we install a toolchain
    # wrapper so that gyp can switch toolchains via a commandline
    # option.
    bindir = os.path.join(pepperdir, 'toolchain', tcname, 'bin')
    wrapper = os.path.join(SDK_SRC_DIR, 'tools', 'compiler-wrapper.py')
    buildbot_common.MakeDir(bindir)
    buildbot_common.CopyFile(wrapper, bindir)

    # Module 'os' has no 'symlink' member (on Windows).
    # pylint: disable=E1101

    os.symlink('compiler-wrapper.py', os.path.join(bindir, 'i686-nacl-g++'))
    os.symlink('compiler-wrapper.py', os.path.join(bindir, 'i686-nacl-gcc'))
    os.symlink('compiler-wrapper.py', os.path.join(bindir, 'i686-nacl-ar'))


HEADER_MAP = {
  'newlib': {
      'pthread.h': 'src/untrusted/pthread/pthread.h',
      'semaphore.h': 'src/untrusted/pthread/semaphore.h',
      'nacl/dynamic_annotations.h':
          'src/untrusted/valgrind/dynamic_annotations.h',
      'nacl/nacl_dyncode.h': 'src/untrusted/nacl/nacl_dyncode.h',
      'nacl/nacl_startup.h': 'src/untrusted/nacl/nacl_startup.h',
      'nacl/nacl_thread.h': 'src/untrusted/nacl/nacl_thread.h',
      'pnacl.h': 'src/untrusted/nacl/pnacl.h',
      'irt.h': 'src/untrusted/irt/irt.h',
      'irt_ppapi.h': 'src/untrusted/irt/irt_ppapi.h',
  },
  'glibc': {
      'nacl/dynamic_annotations.h':
          'src/untrusted/valgrind/dynamic_annotations.h',
      'nacl/nacl_dyncode.h': 'src/untrusted/nacl/nacl_dyncode.h',
      'nacl/nacl_startup.h': 'src/untrusted/nacl/nacl_startup.h',
      'nacl/nacl_thread.h': 'src/untrusted/nacl/nacl_thread.h',
      'pnacl.h': 'src/untrusted/nacl/pnacl.h',
      'irt.h': 'src/untrusted/irt/irt.h',
      'irt_ppapi.h': 'src/untrusted/irt/irt_ppapi.h',
  },
  'libs': {
  },
}


def InstallHeaders(tc_dst_inc, pepper_ver, tc_name):
  """Copies NaCl headers to expected locations in the toolchain."""
  tc_map = HEADER_MAP[tc_name]
  for filename in tc_map:
    src = os.path.join(NACL_DIR, tc_map[filename])
    dst = os.path.join(tc_dst_inc, filename)
    buildbot_common.MakeDir(os.path.dirname(dst))
    buildbot_common.CopyFile(src, dst)

  # Clean out per toolchain ppapi directory
  ppapi = os.path.join(tc_dst_inc, 'ppapi')
  buildbot_common.RemoveDir(ppapi)

  # Copy in c and c/dev headers
  buildbot_common.MakeDir(os.path.join(ppapi, 'c', 'dev'))
  buildbot_common.CopyDir(os.path.join(PPAPI_DIR, 'c', '*.h'),
          os.path.join(ppapi, 'c'))
  buildbot_common.CopyDir(os.path.join(PPAPI_DIR, 'c', 'dev', '*.h'),
          os.path.join(ppapi, 'c', 'dev'))

  # Run the generator to overwrite IDL files
  generator_args = [sys.executable, 'generator.py', '--wnone', '--cgen',
      '--verbose', '--dstroot=%s/c' % ppapi]
  if pepper_ver:
    generator_args.append('--release=M' + pepper_ver)
  buildbot_common.Run(generator_args,
                      cwd=os.path.join(PPAPI_DIR, 'generators'))

  # Remove private and trusted interfaces
  buildbot_common.RemoveDir(os.path.join(ppapi, 'c', 'private'))
  buildbot_common.RemoveDir(os.path.join(ppapi, 'c', 'trusted'))

  # Copy in the C++ headers
  buildbot_common.MakeDir(os.path.join(ppapi, 'cpp', 'dev'))
  buildbot_common.CopyDir(os.path.join(PPAPI_DIR, 'cpp', '*.h'),
          os.path.join(ppapi, 'cpp'))
  buildbot_common.CopyDir(os.path.join(PPAPI_DIR, 'cpp', 'dev', '*.h'),
          os.path.join(ppapi, 'cpp', 'dev'))
  buildbot_common.MakeDir(os.path.join(ppapi, 'utility', 'graphics'))
  buildbot_common.MakeDir(os.path.join(ppapi, 'utility', 'threading'))
  buildbot_common.MakeDir(os.path.join(ppapi, 'utility', 'websocket'))
  buildbot_common.CopyDir(os.path.join(PPAPI_DIR, 'utility', '*.h'),
          os.path.join(ppapi, 'utility'))
  buildbot_common.CopyDir(os.path.join(PPAPI_DIR, 'utility', 'graphics', '*.h'),
          os.path.join(ppapi, 'utility', 'graphics'))
  buildbot_common.CopyDir(
          os.path.join(PPAPI_DIR, 'utility', 'threading', '*.h'),
          os.path.join(ppapi, 'utility', 'threading'))
  buildbot_common.CopyDir(
          os.path.join(PPAPI_DIR, 'utility', 'websocket', '*.h'),
          os.path.join(ppapi, 'utility', 'websocket'))

  # Copy in the gles2 headers
  buildbot_common.MakeDir(os.path.join(ppapi, 'gles2'))
  buildbot_common.CopyDir(os.path.join(PPAPI_DIR, 'lib', 'gl', 'gles2', '*.h'),
          os.path.join(ppapi, 'gles2'))

  # Copy the EGL headers
  buildbot_common.MakeDir(os.path.join(tc_dst_inc, 'EGL'))
  buildbot_common.CopyDir(
          os.path.join(PPAPI_DIR, 'lib', 'gl', 'include', 'EGL', '*.h'),
          os.path.join(tc_dst_inc, 'EGL'))

  # Copy the GLES2 headers
  buildbot_common.MakeDir(os.path.join(tc_dst_inc, 'GLES2'))
  buildbot_common.CopyDir(
          os.path.join(PPAPI_DIR, 'lib', 'gl', 'include', 'GLES2', '*.h'),
          os.path.join(tc_dst_inc, 'GLES2'))

  # Copy the KHR headers
  buildbot_common.MakeDir(os.path.join(tc_dst_inc, 'KHR'))
  buildbot_common.CopyDir(
          os.path.join(PPAPI_DIR, 'lib', 'gl', 'include', 'KHR', '*.h'),
          os.path.join(tc_dst_inc, 'KHR'))

  # Copy the lib files
  buildbot_common.CopyDir(os.path.join(PPAPI_DIR, 'lib'),
          os.path.join(tc_dst_inc, 'ppapi'))


def MakeNinjaRelPath(path):
  return os.path.join(os.path.relpath(OUT_DIR, SRC_DIR), path)


def GypNinjaBuild_X86(pepperdir, platform, toolchains):
  build_dir = 'gypbuild'
  GypNinjaBuild_X86_Nacl(platform, build_dir)
  GypNinjaBuild_X86_Chrome(build_dir)

  ninja_out_dir = os.path.join(OUT_DIR, build_dir, 'Release')
  # src_file, dst_file, is_host_exe?
  tools_files = [
    ('sel_ldr', 'sel_ldr_x86_32', True),
    ('ncval_x86_32', 'ncval_x86_32', True),
    ('ncval_arm', 'ncval_arm', True),
    ('irt_core_newlib_x32.nexe', 'irt_core_newlib_x32.nexe', False),
    ('irt_core_newlib_x64.nexe', 'irt_core_newlib_x64.nexe', False),
  ]
  if platform != 'mac':
    # Mac doesn't build 64-bit binaries.
    tools_files.append(('sel_ldr64', 'sel_ldr_x86_64', True))
    tools_files.append(('ncval_x86_64', 'ncval_x86_64', True))

  if platform == 'linux':
    tools_files.append(('nacl_helper_bootstrap',
                        'nacl_helper_bootstrap_x86_32', True))
    tools_files.append(('nacl_helper_bootstrap64',
                        'nacl_helper_bootstrap_x86_64', True))

  buildbot_common.MakeDir(os.path.join(pepperdir, 'tools'))
  for src, dst, host_exe in tools_files:
    if platform == 'win' and host_exe:
      src += '.exe'
      dst += '.exe'

    buildbot_common.CopyFile(
        os.path.join(ninja_out_dir, src),
        os.path.join(pepperdir, 'tools', dst))

  for tc in set(toolchains) & set(['newlib', 'glibc']):
    for bits in '32', '64':
      tc_dir = 'tc_' + tc
      lib_dir = 'lib' + bits
      src_dir = os.path.join(ninja_out_dir, 'gen', tc_dir, lib_dir)
      tcpath = os.path.join(pepperdir, 'toolchain',
                            '%s_x86_%s' % (platform, tc))
      dst_dir = GetToolchainNaClLib(tc, tcpath, 'x86', bits)

      buildbot_common.MakeDir(dst_dir)
      buildbot_common.CopyDir(os.path.join(src_dir, '*.a'), dst_dir)
      if tc == 'newlib':
        buildbot_common.CopyDir(os.path.join(src_dir, '*.o'), dst_dir)

      if tc == 'glibc':
        buildbot_common.CopyDir(os.path.join(src_dir, '*.so'), dst_dir)

      # TODO(binji): temporary hack; copy crt1.o from sdk toolchain directory.
      lib_dir = os.path.join(ninja_out_dir, 'gen', 'sdk', 'toolchain',
                     '%s_x86_%s' % (platform, tc), 'x86_64-nacl', 'lib')
      if bits == '32':
        lib_dir += '32'
      buildbot_common.CopyFile(os.path.join(lib_dir, 'crt1.o'), dst_dir)


def GypNinjaBuild_X86_Nacl(platform, rel_out_dir):
  gyp_py = os.path.join(NACL_DIR, 'build', 'gyp_nacl')
  nacl_core_sdk_gyp = os.path.join(NACL_DIR, 'build', 'nacl_core_sdk.gyp')
  all_gyp = os.path.join(NACL_DIR, 'build', 'all.gyp')

  out_dir = MakeNinjaRelPath(rel_out_dir)
  GypNinjaBuild('ia32', gyp_py, nacl_core_sdk_gyp, 'nacl_core_sdk', out_dir)
  GypNinjaBuild('ia32', gyp_py, all_gyp, 'ncval_x86_32', out_dir)
  GypNinjaBuild(None, gyp_py, all_gyp, 'ncval_arm', out_dir)

  if platform == 'win':
    NinjaBuild('sel_ldr64', out_dir)
    NinjaBuild('ncval_x86_64', out_dir)
  elif platform == 'linux':
    out_dir_64 = MakeNinjaRelPath(rel_out_dir + '_64')
    GypNinjaBuild('x64', gyp_py, nacl_core_sdk_gyp, 'sel_ldr', out_dir_64)
    GypNinjaBuild('x64', gyp_py, all_gyp, 'ncval_x86_64', out_dir_64)

    # We only need sel_ldr and ncval_x86_64 from the 64-bit out directory.
    # sel_ldr needs to be renamed, so we'll call it sel_ldr64.
    files_to_copy = [
      ('sel_ldr', 'sel_ldr64'),
      ('ncval_x86_64', 'ncval_x86_64'),
    ]
    files_to_copy.append(('nacl_helper_bootstrap', 'nacl_helper_bootstrap64'))

    for src, dst in files_to_copy:
      buildbot_common.CopyFile(
          os.path.join(SRC_DIR, out_dir_64, 'Release', src),
          os.path.join(SRC_DIR, out_dir, 'Release', dst))


def GypNinjaBuild_X86_Chrome(rel_out_dir):
  gyp_py = os.path.join(SRC_DIR, 'build', 'gyp_chromium')

  out_dir = MakeNinjaRelPath(rel_out_dir)
  gyp_file = os.path.join(SRC_DIR, 'ppapi', 'ppapi_untrusted.gyp')
  targets = ['ppapi_cpp_lib', 'ppapi_gles2_lib']
  GypNinjaBuild('ia32', gyp_py, gyp_file, targets, out_dir)

  gyp_file = os.path.join(SRC_DIR, 'ppapi', 'native_client',
                          'native_client.gyp')
  GypNinjaBuild('ia32', gyp_py, gyp_file, 'ppapi_lib', out_dir)


def GypNinjaBuild_Pnacl(rel_out_dir, target_arch):
  # TODO(binji): This will build the pnacl_irt_shim twice; once as part of the
  # Chromium build, and once here. When we move more of the SDK build process
  # to gyp, we can remove this.
  gyp_py = os.path.join(SRC_DIR, 'build', 'gyp_chromium')

  out_dir = MakeNinjaRelPath(rel_out_dir)
  gyp_file = os.path.join(SRC_DIR, 'ppapi', 'native_client', 'src',
                          'untrusted', 'pnacl_irt_shim', 'pnacl_irt_shim.gyp')
  targets = ['pnacl_irt_shim']
  GypNinjaBuild(target_arch, gyp_py, gyp_file, targets, out_dir)


def GypNinjaBuild(arch, gyp_py_script, gyp_file, targets, out_dir):
  gyp_env = copy.copy(os.environ)
  gyp_env['GYP_GENERATORS'] = 'ninja'
  gyp_defines = []
  if options.mac_sdk:
    gyp_defines.append('mac_sdk=%s' % options.mac_sdk)
  if arch:
    gyp_defines.append('target_arch=%s' % arch)

  gyp_env['GYP_DEFINES'] = ' '.join(gyp_defines)
  gyp_generator_flags = ['-G', 'output_dir=%s' % (out_dir,)]
  gyp_depth = '--depth=.'
  buildbot_common.Run(
      [sys.executable, gyp_py_script, gyp_file, gyp_depth] + \
          gyp_generator_flags,
      cwd=SRC_DIR,
      env=gyp_env)
  NinjaBuild(targets, out_dir)


def NinjaBuild(targets, out_dir):
  if type(targets) is not list:
    targets = [targets]
  out_config_dir = os.path.join(out_dir, 'Release')
  buildbot_common.Run(['ninja', '-C', out_config_dir] + targets, cwd=SRC_DIR)


def BuildStepBuildToolchains(pepperdir, platform, arch, pepper_ver,
                             toolchains):
  buildbot_common.BuildStep('SDK Items')

  tcname = platform + '_' + arch
  newlibdir = os.path.join(pepperdir, 'toolchain', tcname + '_newlib')
  glibcdir = os.path.join(pepperdir, 'toolchain', tcname + '_glibc')
  pnacldir = os.path.join(pepperdir, 'toolchain', tcname + '_pnacl')

  # Run scons TC build steps
  if arch == 'x86':
    if set(toolchains) & set(['newlib', 'glibc']):
      GypNinjaBuild_X86(pepperdir, platform, toolchains)

    if 'newlib' in toolchains:
      InstallHeaders(GetToolchainNaClInclude('newlib', newlibdir, 'x86'),
                     pepper_ver,
                     'newlib')

    if 'glibc' in toolchains:
      InstallHeaders(GetToolchainNaClInclude('glibc', glibcdir, 'x86'),
                     pepper_ver,
                     'glibc')

    if 'pnacl' in toolchains:
      shell = platform == 'win'
      buildbot_common.Run(
          GetBuildArgs('pnacl', pnacldir, pepperdir, 'x86', '32'),
          cwd=NACL_DIR, shell=shell)
      buildbot_common.Run(
          GetBuildArgs('pnacl', pnacldir, pepperdir, 'x86', '64'),
          cwd=NACL_DIR, shell=shell)

      for arch in ('ia32', 'arm'):
        # Fill in the latest native pnacl shim library from the chrome build.
        GypNinjaBuild_Pnacl('gypbuild-' + arch, arch)
        release_build_dir = os.path.join(OUT_DIR, 'gypbuild-' + arch,
                                         'Release')

        pnacl_libdir_map = { 'ia32': 'x86-64', 'arm': 'arm' }
        buildbot_common.CopyFile(
            os.path.join(release_build_dir, 'libpnacl_irt_shim.a'),
            GetPNaClNativeLib(pnacldir, pnacl_libdir_map[arch]))

      InstallHeaders(GetToolchainNaClInclude('pnacl', pnacldir, 'x86'),
                     pepper_ver,
                     'newlib')
  else:
    buildbot_common.ErrorExit('Missing arch %s' % arch)


def BuildStepCopyBuildHelpers(pepperdir, platform):
  buildbot_common.BuildStep('Copy build helpers')
  buildbot_common.CopyDir(os.path.join(SDK_SRC_DIR, 'tools', '*.py'),
      os.path.join(pepperdir, 'tools'))
  if platform == 'win':
    buildbot_common.BuildStep('Add MAKE')
    http_download.HttpDownload(GSTORE + MAKE,
                               os.path.join(pepperdir, 'tools', 'make.exe'))


EXAMPLE_LIST = [
  'debugging',
  'dlopen',
  'file_histogram',
  'file_io',
  'gamepad',
  'geturl',
  'hello_nacl_mounts',
  'hello_world',
  'hello_world_gles',
  'hello_world_interactive',
  'input_events',
  'load_progress',
  'mouselock',
  'pi_generator',
  'sine_synth',
  'websocket',
]

LIBRARY_LIST = [
  'nacl_mounts',
  'ppapi',
  'ppapi_cpp',
  'ppapi_gles2',
  'pthread',
]

LIB_DICT = {
  'linux': [],
  'mac': [],
  'win': ['x86_32']
}


def MakeDirectoryOrClobber(pepperdir, dirname, clobber):
  dirpath = os.path.join(pepperdir, dirname)
  if clobber:
    buildbot_common.RemoveDir(dirpath)
  buildbot_common.MakeDir(dirpath)

  return dirpath


def BuildStepCopyExamples(pepperdir, toolchains, build_experimental, clobber):
  buildbot_common.BuildStep('Copy examples')

  if not os.path.exists(os.path.join(pepperdir, 'tools')):
    buildbot_common.ErrorExit('Examples depend on missing tools.')
  if not os.path.exists(os.path.join(pepperdir, 'toolchain')):
    buildbot_common.ErrorExit('Examples depend on missing toolchains.')

  exampledir = MakeDirectoryOrClobber(pepperdir, 'examples', clobber)
  libdir = MakeDirectoryOrClobber(pepperdir, 'lib', clobber)

  plat = getos.GetPlatform()
  for arch in LIB_DICT[plat]:
    buildbot_common.MakeDir(os.path.join(libdir, '%s_%s_host' % (plat, arch)))
    if options.gyp and plat != 'win':
      configs = ['debug', 'release']
    else:
      configs = ['Debug', 'Release']
    for config in configs:
      buildbot_common.MakeDir(os.path.join(libdir, '%s_%s_host' % (plat, arch),
                              config))

  MakeDirectoryOrClobber(pepperdir, 'src', clobber)

  # Copy individual files
  files = ['favicon.ico', 'httpd.cmd']
  for filename in files:
    oshelpers.Copy(['-v', os.path.join(SDK_EXAMPLE_DIR, filename), exampledir])

  args = ['--dstroot=%s' % pepperdir, '--master']
  for toolchain in toolchains:
    args.append('--' + toolchain)

  for example in EXAMPLE_LIST:
    dsc = os.path.join(SDK_EXAMPLE_DIR, example, 'example.dsc')
    args.append(dsc)

  for library in LIBRARY_LIST:
    dsc = os.path.join(SDK_LIBRARY_DIR, library, 'library.dsc')
    args.append(dsc)

  if build_experimental:
    args.append('--experimental')

  if generate_make.main(args):
    buildbot_common.ErrorExit('Failed to build examples.')


def GetWindowsEnvironment():
  sys.path.append(os.path.join(NACL_DIR, 'buildbot'))
  import buildbot_standard

  # buildbot_standard.SetupWindowsEnvironment expects a "context" object. We'll
  # fake enough of that here to work.
  class FakeContext(object):
    def __init__(self):
      self.env = os.environ

    def GetEnv(self, key):
      return self.env[key]

    def __getitem__(self, key):
      return self.env[key]

    def SetEnv(self, key, value):
      self.env[key] = value

    def __setitem__(self, key, value):
      self.env[key] = value

  context = FakeContext()
  buildbot_standard.SetupWindowsEnvironment(context)

  # buildbot_standard.SetupWindowsEnvironment adds the directory which contains
  # vcvarsall.bat to the path, but not the directory which contains cl.exe,
  # link.exe, etc.
  # Running vcvarsall.bat adds the correct directories to the path, which we
  # extract below.
  process = subprocess.Popen('vcvarsall.bat x86 > NUL && set',
      stdout=subprocess.PIPE, env=context.env, shell=True)
  stdout, _ = process.communicate()

  # Parse environment from "set" command above.
  # It looks like this:
  # KEY1=VALUE1\r\n
  # KEY2=VALUE2\r\n
  # ...
  return dict(line.split('=') for line in stdout.split('\r\n')[:-1])


def BuildStepMakeAll(pepperdir, platform, directory, step_name, clean=False):
  buildbot_common.BuildStep(step_name)
  make_dir = os.path.join(pepperdir, directory)
  makefile = os.path.join(make_dir, 'Makefile')
  if os.path.isfile(makefile):
    print "\n\nMake: " + make_dir
    if platform == 'win':
      # We need to modify the environment to build host on Windows.
      env = GetWindowsEnvironment()
      make = os.path.join(make_dir, 'make.bat')
    else:
      env = os.environ
      make = 'make'

    buildbot_common.Run([make, '-j8'],
                        cwd=os.path.abspath(make_dir), env=env)
    if clean:
      # Clean to remove temporary files but keep the built libraries.
      buildbot_common.Run([make, '-j8', 'clean'],
                          cwd=os.path.abspath(make_dir))


def BuildStepBuildLibraries(pepperdir, platform, directory, clean=True):
  BuildStepMakeAll(pepperdir, platform, directory, 'Build Libraries',
      clean=clean)


def BuildStepGenerateNotice(pepperdir):
  # Look for LICENSE files
  license_filenames_re = re.compile('LICENSE|COPYING')

  license_files = []
  for root, _, files in os.walk(pepperdir):
    for filename in files:
      if license_filenames_re.match(filename):
        path = os.path.join(root, filename)
        license_files.append(path)
  print '\n'.join(license_files)

  notice_filename = os.path.join(pepperdir, 'NOTICE')
  generate_notice.Generate(notice_filename, pepperdir, license_files)


def BuildStepTarBundle(pepper_ver, tarfile):
  buildbot_common.BuildStep('Tar Pepper Bundle')
  buildbot_common.MakeDir(os.path.dirname(tarfile))
  buildbot_common.Run([sys.executable, CYGTAR, '-C', OUT_DIR, '-cjf', tarfile,
       'pepper_' + pepper_ver], cwd=NACL_DIR)


def BuildStepRunTests():
  args = []
  if options.build_experimental:
    args.append('--experimental')
  if options.run_pyauto_tests:
    args.append('--pyauto')
  test_sdk.main(args)


def GetManifestBundle(pepper_ver, revision, tarfile, archive_url):
  with open(tarfile, 'rb') as tarfile_stream:
    archive_sha1, archive_size = manifest_util.DownloadAndComputeHash(
        tarfile_stream)

  archive = manifest_util.Archive(manifest_util.GetHostOS())
  archive.url = archive_url
  archive.size = archive_size
  archive.checksum = archive_sha1

  bundle = manifest_util.Bundle('pepper_' + pepper_ver)
  bundle.revision = int(revision)
  bundle.repath = 'pepper_' + pepper_ver
  bundle.version = int(pepper_ver)
  bundle.description = 'Chrome %s bundle, revision %s' % (pepper_ver, revision)
  bundle.stability = 'dev'
  bundle.recommended = 'no'
  bundle.archives = [archive]
  return bundle


def BuildStepArchiveBundle(pepper_ver, revision, tarfile):
  buildbot_common.BuildStep('Archive build')
  bucket_path = 'nativeclient-mirror/nacl/nacl_sdk/%s' % (
      build_utils.ChromeVersion(),)
  tarname = os.path.basename(tarfile)
  tarfile_dir = os.path.dirname(tarfile)
  buildbot_common.Archive(tarname, bucket_path, tarfile_dir)

  # generate "manifest snippet" for this archive.
  archive_url = GSTORE + 'nacl_sdk/%s/%s' % (
      build_utils.ChromeVersion(), tarname)
  bundle = GetManifestBundle(pepper_ver, revision, tarfile, archive_url)

  manifest_snippet_file = os.path.join(OUT_DIR, tarname + '.json')
  with open(manifest_snippet_file, 'wb') as manifest_snippet_stream:
    manifest_snippet_stream.write(bundle.GetDataAsString())

  buildbot_common.Archive(tarname + '.json', bucket_path, OUT_DIR,
                          step_link=False)


def BuildStepArchiveSDKTools():
  # Only push up sdk_tools.tgz and nacl_sdk.zip on the linux buildbot.
  builder_name = os.getenv('BUILDBOT_BUILDERNAME', '')
  if builder_name == 'linux-sdk-multi':
    buildbot_common.BuildStep('Build SDK Tools')
    build_updater.BuildUpdater(OUT_DIR)

    buildbot_common.BuildStep('Archive SDK Tools')
    bucket_path = 'nativeclient-mirror/nacl/nacl_sdk/%s' % (
        build_utils.ChromeVersion(),)
    buildbot_common.Archive('sdk_tools.tgz', bucket_path, OUT_DIR,
                            step_link=False)
    buildbot_common.Archive('nacl_sdk.zip', bucket_path, OUT_DIR,
                            step_link=False)


def main(args):
  parser = optparse.OptionParser()
  parser.add_option('--run-tests',
      help='Run tests. This includes building examples.', action='store_true')
  parser.add_option('--run-pyauto-tests',
      help='Run the pyauto tests for examples.', action='store_true')
  parser.add_option('--skip-tar', help='Skip generating a tarball.',
      action='store_true')
  parser.add_option('--archive', help='Force the archive step.',
      action='store_true')
  parser.add_option('--gyp',
      help='Use gyp to build examples/libraries/Makefiles.',
      action='store_true')
  parser.add_option('--release', help='PPAPI release version.',
      dest='release', default=None)
  parser.add_option('--experimental',
      help='build experimental examples and libraries', action='store_true',
      dest='build_experimental')
  parser.add_option('--skip-toolchain', help='Skip toolchain download/untar',
      action='store_true')
  parser.add_option('--mac_sdk',
      help='Set the mac_sdk (e.g. 10.6) to use when building with ninja.',
      dest='mac_sdk')

  global options
  options, args = parser.parse_args(args[1:])
  platform = getos.GetPlatform()
  arch = 'x86'

  generate_make.use_gyp = options.gyp

  # TODO(binji) for now, only test examples on non-trybots. Trybots don't build
  # pyauto Chrome.
  if buildbot_common.IsSDKBuilder():
    options.run_tests = True
    options.run_pyauto_tests = True
    options.archive = True

  if buildbot_common.IsSDKTrybot():
    options.run_tests = True

  toolchains = ['newlib', 'glibc', 'pnacl', 'host']
  print 'Building: ' + ' '.join(toolchains)

  if options.archive and options.skip_tar:
    parser.error('Incompatible arguments with archive.')

  pepper_ver = str(int(build_utils.ChromeMajorVersion()))
  pepper_old = str(int(build_utils.ChromeMajorVersion()) - 1)
  pepperdir = os.path.join(SRC_DIR, 'out', 'pepper_' + pepper_ver)
  pepperdir_old = os.path.join(SRC_DIR, 'out', 'pepper_' + pepper_old)
  clnumber = build_utils.ChromeRevision()
  tarname = 'naclsdk_' + platform + '.tar.bz2'
  tarfile = os.path.join(OUT_DIR, tarname)

  if options.release:
    pepper_ver = options.release
  print 'Building PEPPER %s at %s' % (pepper_ver, clnumber)

  if 'NACL_SDK_ROOT' in os.environ:
    # We don't want the currently configured NACL_SDK_ROOT to have any effect
    # of the build.
    del os.environ['NACL_SDK_ROOT']

  if not options.skip_toolchain:
    BuildStepDownloadToolchains(platform)
  BuildStepCleanPepperDirs(pepperdir, pepperdir_old)
  BuildStepMakePepperDirs(pepperdir, ['include', 'toolchain', 'tools'])
  BuildStepCopyTextFiles(pepperdir, pepper_ver, clnumber)
  if not options.skip_toolchain:
    BuildStepUntarToolchains(pepperdir, platform, arch, toolchains)
  BuildStepBuildToolchains(pepperdir, platform, arch, pepper_ver, toolchains)
  InstallHeaders(os.path.join(pepperdir, 'include'), None, 'libs')
  BuildStepCopyBuildHelpers(pepperdir, platform)
  BuildStepCopyExamples(pepperdir, toolchains, options.build_experimental, True)

  # Ship with libraries prebuilt, so run that first.
  BuildStepBuildLibraries(pepperdir, platform, 'src')
  BuildStepGenerateNotice(pepperdir)

  if not options.skip_tar:
    BuildStepTarBundle(pepper_ver, tarfile)

  if options.run_tests:
    BuildStepRunTests()

  # Archive on non-trybots.
  if options.archive:
    BuildStepArchiveBundle(pepper_ver, clnumber, tarfile)
    BuildStepArchiveSDKTools()

  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv))
