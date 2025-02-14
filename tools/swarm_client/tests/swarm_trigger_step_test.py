#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hashlib
import json
import os
import StringIO
import sys
import unittest

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT_DIR)
import swarm_trigger_step

FILE_NAME = "test.isolated"
FILE_HASH = hashlib.sha1(FILE_NAME).hexdigest()
TEST_NAME = "unit_tests"
TEST_FILTER = '*'
CLEANUP_SCRIPT_NAME = 'swarm_cleanup.py'

ENV_VARS = {
    'GTEST_FILTER': TEST_FILTER,
    'GTEST_SHARD_INDEX': '%(instance_index)s',
    'GTEST_TOTAL_SHARDS': '%(num_instances)s'
}

class Options(object):
  def __init__(self, working_dir="swarm_tests", shards=1,
               os_image='win32', swarm_url='http://localhost:8080',
               data_server='http://localhost:8081'):
    self.working_dir = working_dir
    self.shards = shards
    self.os_image = os_image
    self.swarm_url = swarm_url
    self.data_server = data_server


def GenerateExpectedJSON(options):
  platform_mapping =  {
    'cygwin': 'Windows',
    'darwin': 'Mac',
    'linux2': 'Linux',
    'win32': 'Windows'
  }

  retrieval_url = options.data_server + '/content/retrieve/default/'

  expected = {
    'test_case_name': TEST_NAME,
    'data': [[retrieval_url, 'swarm_data.zip']],
    'tests' : [
      {
        'action': [
          'python', swarm_trigger_step.RUN_TEST_NAME,
          '--hash', FILE_HASH,
          '--remote', retrieval_url,
          '-v'
        ],
        'test_name': 'Run Test',
        'time_out': 600
      },
      {
        'action' : [
            'python', CLEANUP_SCRIPT_NAME
        ],
        'test_name': 'Clean Up',
        'time_out': 600
      }
    ],
    'env_vars': ENV_VARS,
    'configurations': [
      {
        'min_instances': options.shards,
        'config_name': platform_mapping[options.os_image],
        'dimensions': {
          'os': platform_mapping[options.os_image],
          'vlan': 'm4'
        },
      },
    ],
    'working_dir': options.working_dir,
    'restart_on_failure': True,
    'cleanup': 'root'
  }

  if options.os_image == 'win32':
    expected['tests'].append(
      {
        'action': [
          'python',
          'kill_processes.py',
          '--handle_exe',
          'handle.exe'
        ],
        'test_name': 'Kill Processes',
        'time_out': 600
      }
    )

  return expected


class MockZipFile(object):
  def __init__(self, filename, mode):
    pass

  def write(self, source, dest=None):
    pass

  def close(self):
    pass


def MockUrlOpen(url_or_request, _data, has_return_value):
  if (isinstance(url_or_request, basestring) and
      url_or_request.endswith('/content/contains')):
    return StringIO.StringIO(has_return_value)
  return StringIO.StringIO('{}')


def MockUrlOpenHasZip(url_or_request, data=None):
  return MockUrlOpen(url_or_request, data, has_return_value=chr(1))


def MockUrlOpenNoZip(url_or_request, data=None):
  return MockUrlOpen(url_or_request, data, has_return_value=chr(0))


class ManifestTest(unittest.TestCase):
  def setUp(self):
    self.old_ZipFile = swarm_trigger_step.zipfile.ZipFile
    self.old_urlopen = swarm_trigger_step.urllib2.urlopen
    swarm_trigger_step.zipfile.ZipFile = MockZipFile

  def tearDown(self):
    swarm_trigger_step.zipfile.ZipFile = self.old_ZipFile
    swarm_trigger_step.urllib2.urlopen = self.old_urlopen

  def test_basic_manifest(self):
    options = Options(shards=2)
    manifest = swarm_trigger_step.Manifest(
        FILE_HASH, TEST_NAME, options.shards, '*', options)

    manifest_json = json.loads(manifest.to_json())

    expected = GenerateExpectedJSON(options)
    self.assertEqual(expected, manifest_json)

  def test_basic_linux(self):
    """A basic linux manifest test to ensure that windows specific values
       aren't used.
    """
    options = Options(os_image='linux2')
    manifest = swarm_trigger_step.Manifest(
        FILE_HASH, TEST_NAME, options.shards, '*', options)

    manifest_json = json.loads(manifest.to_json())

    expected = GenerateExpectedJSON(options)
    self.assertEqual(expected, manifest_json)

  def test_process_manifest_success(self):
    swarm_trigger_step.urllib2.urlopen = MockUrlOpenNoZip
    options = Options()
    self.assertEqual(
        0,
        swarm_trigger_step.ProcessManifest(
            FILE_HASH, TEST_NAME, options.shards, '*', options))

  def test_process_manifest_success_zip_already_uploaded(self):
    swarm_trigger_step.urllib2.urlopen = MockUrlOpenHasZip
    options = Options()
    self.assertEqual(
        0,
        swarm_trigger_step.ProcessManifest(
            FILE_HASH, TEST_NAME, options.shards, '*', options))


if __name__ == '__main__':
  unittest.main()
