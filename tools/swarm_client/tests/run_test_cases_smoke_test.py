#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import logging
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from xml.dom import minidom

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT_DIR)
sys.path.append(os.path.join(ROOT_DIR, 'tests', 'gtest_fake'))

import gtest_fake_base


def RunTest(arguments):
  cmd = [
      sys.executable,
      os.path.join(ROOT_DIR, 'run_test_cases.py'),
  ] + arguments

  logging.debug(' '.join(cmd))
  proc = subprocess.Popen(
      cmd,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      universal_newlines=True)
    # pylint is confused.
  out, err = proc.communicate() or ('', '')

  return (out, err, proc.returncode)


def trim_xml_whitespace(data):
  """Recursively remove non-important elements."""
  children_to_remove = []
  for child in data.childNodes:
    if child.nodeType == minidom.Node.TEXT_NODE and not child.data.strip():
      children_to_remove.append(child)
    elif child.nodeType == minidom.Node.COMMENT_NODE:
      children_to_remove.append(child)
    elif child.nodeType == minidom.Node.ELEMENT_NODE:
      trim_xml_whitespace(child)
  for child in children_to_remove:
    data.removeChild(child)


def load_xml_as_string_and_filter(filepath):
  """Serializes XML to a list of strings that is consistently formatted
  (ignoring whitespace between elements) so that it may be compared.
  """
  with open(filepath, 'r') as f:
    xml = minidom.parse(f)

  trim_xml_whitespace(xml)
  return xml.toprettyxml(indent='  ').splitlines()


class RunTestCases(unittest.TestCase):
  def setUp(self):
    # Make sure there's no environment variable that could do side effects.
    os.environ.pop('GTEST_SHARD_INDEX', '')
    os.environ.pop('GTEST_TOTAL_SHARDS', '')
    self._tempdirpath = None

  def tearDown(self):
    if self._tempdirpath and os.path.exists(self._tempdirpath):
      shutil.rmtree(self._tempdirpath)

  @property
  def tempdirpath(self):
    if not self._tempdirpath:
      self._tempdirpath = tempfile.mkdtemp(prefix='run_test_cases')
    return self._tempdirpath

  @property
  def filename(self):
    return os.path.join(self.tempdirpath, 'foo', 'bar.run_test_cases')

  def _check_results(self, expected_out_re, out, err):
    lines = out.splitlines()

    for index in range(len(expected_out_re)):
      if not lines:
        self.fail((expected_out_re[index:], err))
      line = lines.pop(0)
      self.assertTrue(
          re.match('^%s$' % expected_out_re[index], line),
          (index, repr(expected_out_re[index]), repr(line)))
    self.assertEqual([], lines)
    self.assertEqual('', err)

  def _check_results_file(self, fail, flaky, success, test_cases):
    self.assertTrue(os.path.exists(self.filename))

    with open(self.filename) as f:
      actual = json.load(f)

    self.assertEqual(
        [u'duration', u'fail', u'flaky', u'success', u'test_cases'],
        sorted(actual))

    self.assertTrue(actual['duration'] > 0.0000001)
    self.assertEqual(fail, actual['fail'])
    self.assertEqual(flaky, actual['flaky'])
    self.assertEqual(success, actual['success'])
    self.assertEqual(len(test_cases), len(actual['test_cases']))
    for (entry_name, entry_count) in test_cases:
      self.assertTrue(entry_name in actual['test_cases'])
      self.assertEqual(
          entry_count, len(actual['test_cases'][entry_name]), entry_name)

  def test_simple_pass(self):
    out, err, return_code = RunTest(
        [
          '--result', self.filename,
          os.path.join(ROOT_DIR, 'tests', 'gtest_fake', 'gtest_fake_pass.py'),
        ])

    self.assertEqual(0, return_code)

    expected_out_re = [
      r'\[\d/\d\]   \d\.\d\ds .+',
      r'\[\d/\d\]   \d\.\d\ds .+',
      r'\[\d/\d\]   \d\.\d\ds .+',
      re.escape('Summary:'),
      re.escape('  Success:    3 100.00% ') + r' +\d+\.\d\ds',
      re.escape('    Flaky:    0   0.00% ') + r' +\d+\.\d\ds',
      re.escape('     Fail:    0   0.00% ') + r' +\d+\.\d\ds',
      r'  \d+\.\d\ds Done running 3 tests with 3 executions. \d+\.\d\d test/s',
    ]
    self._check_results(expected_out_re, out, err)

    test_cases = [
        ('Foo.Bar1', 1),
        ('Foo.Bar2', 1),
        ('Foo.Bar/3', 1)
    ]
    self._check_results_file(
        fail=[],
        flaky=[],
        success=sorted([u'Foo.Bar1', u'Foo.Bar2', u'Foo.Bar/3']),
        test_cases=test_cases)

  def test_simple_fail(self):
    out, err, return_code = RunTest(
        [
          '--result', self.filename,
          os.path.join(ROOT_DIR, 'tests', 'gtest_fake', 'gtest_fake_fail.py'),
        ])

    self.assertEqual(1, return_code)

    expected_out_re = [
      r'\[\d/\d\]   \d\.\d\ds .+',
      r'\[\d/\d\]   \d\.\d\ds .+',
      r'\[\d/\d\]   \d\.\d\ds .+',
      r'\[\d/\d\]   \d\.\d\ds .+',
      r'\[\d/\d\]   \d\.\d\ds .+',
      r'\[\d/\d\]   \d\.\d\ds .+',
      re.escape('Note: Google Test filter = Baz.Fail'),
      r'',
    ] + [
      re.escape(l) for l in
      gtest_fake_base.get_test_output('Baz.Fail').splitlines()
    ] + [
      '',
    ] + [
      re.escape(l) for l in gtest_fake_base.get_footer(1, 1).splitlines()
    ] + [
      '',
      re.escape('Failed tests:'),
      re.escape('  Baz.Fail'),
      re.escape('Summary:'),
      re.escape('  Success:    3  75.00%') + r' +\d+\.\d\ds',
      re.escape('    Flaky:    0   0.00%') + r' +\d+\.\d\ds',
      re.escape('     Fail:    1  25.00%') + r' +\d+\.\d\ds',
      r'  \d+\.\d\ds Done running 4 tests with 6 executions. \d+\.\d\d test/s',
    ]
    self._check_results(expected_out_re, out, err)

    test_cases = [
        ('Foo.Bar1', 1),
        ('Foo.Bar2', 1),
        ('Foo.Bar3', 1),
        ('Baz.Fail', 3)
    ]
    self._check_results_file(
        fail=['Baz.Fail'],
        flaky=[],
        success=[u'Foo.Bar1', u'Foo.Bar2', u'Foo.Bar3'],
        test_cases=test_cases)

  def test_simple_gtest_list_error(self):
    out, err, return_code = RunTest(
        [
          '--no-dump',
          os.path.join(ROOT_DIR, 'tests', 'gtest_fake', 'gtest_fake_error.py'),
        ])

    expected_out_re = [
        'Failed to list test cases',
        'Failed to run %s %s --gtest_list_tests' % (
          re.escape(sys.executable),
          re.escape(
            os.path.join(
              ROOT_DIR, 'tests', 'gtest_fake', 'gtest_fake_error.py'))),
        'stdout:',
        '',
        'stderr:',
        'Unable to list tests'
    ]

    self.assertEqual(1, return_code)
    self._check_results(expected_out_re, out, err)

  def test_gtest_list_tests(self):
    out, err, return_code = RunTest(
        [
          '--gtest_list_tests',
          os.path.join(ROOT_DIR, 'tests', 'gtest_fake', 'gtest_fake_fail.py'),
        ])

    expected_out = (
      'Foo.\n  Bar1\n  Bar2\n  Bar3\nBaz.\n  Fail\n'
      '  YOU HAVE 2 tests with ignored failures (FAILS prefix)\n\n')
    self.assertEqual('', err)
    self.assertEqual(expected_out, out)
    self.assertEqual(0, return_code)

  def test_flaky_stop_early(self):
    tempdir = tempfile.mkdtemp(prefix='run_test_cases')
    try:
      out, err, return_code = RunTest(
          [
            '--result', self.filename,
            # Make it determinist.
            '--jobs', '1',
            '--retries', '1',
            '--max-failures', '2',
            os.path.join(
              ROOT_DIR, 'tests', 'gtest_fake', 'gtest_fake_flaky.py'),
            tempdir,
          ])
      self.assertEqual(1, return_code)
      # Give up on checking the stdout.
      self.assertTrue('STOPPED EARLY' in out, out)
      self.assertEqual('', err)
      # The order is determined by the test shuffling.
      test_cases = [
          ('Foo.Bar1', 2),
          ('Foo.Bar4', 1),
          ('Foo.Bar5', 2),
      ]
      self._check_results_file(
          fail=[u'Foo.Bar4'],
          flaky=[u'Foo.Bar1', u'Foo.Bar5'],
          success=[],
          test_cases=test_cases)
    finally:
      shutil.rmtree(tempdir)

  def test_xml(self):
    out, err, return_code = RunTest(
        [
          # In that case, it's an XML file even if it has the wrong extension.
          '--gtest_output=xml:' + self.filename,
          '--no-dump',
          os.path.join(ROOT_DIR, 'tests', 'gtest_fake', 'gtest_fake_xml.py'),
        ])
    self.assertEqual(0, return_code, out)
    self.assertEqual('', err)
    actual_xml = load_xml_as_string_and_filter(self.filename)
    expected_xml = load_xml_as_string_and_filter(
        os.path.join(ROOT_DIR, 'tests', 'gtest_fake', 'expected.xml'))
    self.assertEqual(expected_xml, actual_xml)


if __name__ == '__main__':
  VERBOSE = '-v' in sys.argv
  logging.basicConfig(level=logging.DEBUG if VERBOSE else logging.ERROR)
  unittest.main()
