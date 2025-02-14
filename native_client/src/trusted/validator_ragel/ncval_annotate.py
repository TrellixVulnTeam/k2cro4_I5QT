#!/usr/bin/python

# Copyright (c) 2011 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import re
import subprocess
import sys


BUNDLE_SIZE = 32


Error = collections.namedtuple('Error', 'offset message')


def GetLocation(binary, offset):
  proc = subprocess.Popen(
      ['x86_64-nacl-addr2line', '--functions', '--basenames',
       '-e', binary, hex(offset)],
      stdout=subprocess.PIPE)
  function, file_loc = list(proc.stdout)
  function = function.rstrip()

  m = re.match(r'(.*):(\d+)$', file_loc)
  file_name = m.group(1)
  line_number = int(m.group(2))

  proc.wait()
  assert proc.returncode == 0, 'addr2line failed'

  return '%s:%s, function %s' % (file_name, line_number, function)


def PrintDisassembly(binary, start_address, stop_address, erroneous_offsets):
  proc = subprocess.Popen(
      ['x86_64-nacl-objdump', '-D', binary,
       '--start-address', hex(start_address),
       '--stop-address', hex(stop_address)],
      stdout=subprocess.PIPE)

  disassembly = []
  for line in proc.stdout:
    # Parse disassembler output of the form
    #    0: 66 0f be 04 10        movsbw (%eax,%edx,1),%ax
    # and extract offset (0 in this case).
    m = re.match(r'\s*([0-9a-f]+):\s([0-9a-f]{2}\s)+\s*(.*)$',
                 line,
                 re.IGNORECASE)
    if m is not None:
      disassembly.append((int(m.group(1), 16), m.group()))

  proc.wait()
  assert proc.returncode == 0, 'objdump failed'

  width = max(len(line) for _, line in disassembly)

  # Print disassembly with erroneous lines marked with '<<<<'.
  for i, (offset, line) in enumerate(disassembly):
    if i < len(disassembly) - 1:
      next_offset, _ = disassembly[i + 1]
    else:
      next_offset = stop_address
    if any(offset <= off < next_offset for off in erroneous_offsets):
      line += ' ' * (width - len(line)) + ' <<<<'
    print line


def PrintErrors(binary, errors):
  errors = sorted(errors, key=lambda e: e.offset)
  errors_by_bundle = collections.defaultdict(list)
  for e in errors:
    errors_by_bundle[e.offset // BUNDLE_SIZE].append(e)

  for bundle, bundle_errors in sorted(errors_by_bundle.items()):
    for offset, message in bundle_errors:
      print '%x (%s): %s' % (offset, GetLocation(binary, offset), message)

    PrintDisassembly(binary,
                     start_address=bundle * BUNDLE_SIZE,
                     stop_address=(bundle + 1) * BUNDLE_SIZE,
                     erroneous_offsets=[offset for offset, _ in bundle_errors])
    print


def main():
  # TODO(shcherbina): add option to pass path to the toolchain.
  if len(sys.argv) != 2:
    print 'Usage:'
    print '    ncval <nexe> | ncval_annotate.py <nexe>'
    sys.exit(1)

  binary = sys.argv[1]

  # Ncval output has the following structure:
  #   HEADER (information about nexe it is validating, etc.)
  #   offset: error message
  #   offset: error message
  #   ...
  #   FOOTER (validation verdict, for instance)
  #
  # List of error messages can be empty (if nexe is valid or if ELF file itself
  # can not be loaded).
  # We want to process all errors as a batch (because we sort them and group
  # by bundles).

  errors = []
  for line in sys.stdin:
    # Collect error messages of the form
    #    201ef: unrecognized instruction
    m = re.match(r'\s*([0-9a-f]+): (.*)$', line, re.IGNORECASE)
    if m is not None:
      errors.append(Error(offset=int(m.group(1), 16), message=m.group(2)))
    else:
      if errors:
        PrintErrors(binary, errors)
        errors = []
      print line.rstrip()

  if errors:
    PrintErrors(binary, errors)


if __name__ == '__main__':
  main()
