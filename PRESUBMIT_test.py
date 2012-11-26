#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import re
import unittest

import PRESUBMIT


class MockInputApi(object):
  def __init__(self):
    self.re = re
    self.os_path = os.path


class MockFile(object):
  def __init__(self, local_path, new_contents):
    self._local_path = local_path
    self._new_contents = new_contents
    self._changed_contents = [(i + 1, l) for i, l in enumerate(new_contents)]

  def ChangedContents(self):
    return self._changed_contents

  def NewContents(self):
    return self._new_contents

  def LocalPath(self):
    return self._local_path


class IncludeOrderTest(unittest.TestCase):
  def testSystemHeaderOrder(self):
    scope = [(1, '#include <csystem.h>'),
             (2, '#include <cppsystem>'),
             (3, '#include "acustom.h"')]
    all_linenums = [linenum for (linenum, _) in scope]
    mock_input_api = MockInputApi()
    warnings = PRESUBMIT._CheckIncludeOrderForScope(scope, mock_input_api,
                                                    '', all_linenums)
    self.assertEqual(0, len(warnings))

  def testSystemHeaderOrderMismatch1(self):
    scope = [(10, '#include <cppsystem>'),
             (20, '#include <csystem.h>'),
             (30, '#include "acustom.h"')]
    all_linenums = [linenum for (linenum, _) in scope]
    mock_input_api = MockInputApi()
    warnings = PRESUBMIT._CheckIncludeOrderForScope(scope, mock_input_api,
                                                    '', all_linenums)
    self.assertEqual(1, len(warnings))
    self.assertTrue('20' in warnings[0])

  def testSystemHeaderOrderMismatch2(self):
    scope = [(10, '#include <cppsystem>'),
             (20, '#include "acustom.h"'),
             (30, '#include <csystem.h>')]
    all_linenums = [linenum for (linenum, _) in scope]
    mock_input_api = MockInputApi()
    warnings = PRESUBMIT._CheckIncludeOrderForScope(scope, mock_input_api,
                                                    '', all_linenums)
    self.assertEqual(1, len(warnings))
    self.assertTrue('30' in warnings[0])

  def testSystemHeaderOrderMismatch3(self):
    scope = [(10, '#include "acustom.h"'),
             (20, '#include <csystem.h>'),
             (30, '#include <cppsystem>')]
    all_linenums = [linenum for (linenum, _) in scope]
    mock_input_api = MockInputApi()
    warnings = PRESUBMIT._CheckIncludeOrderForScope(scope, mock_input_api,
                                                    '', all_linenums)
    self.assertEqual(2, len(warnings))
    self.assertTrue('20' in warnings[0])
    self.assertTrue('30' in warnings[1])

  def testAlphabeticalOrderMismatch(self):
    scope = [(10, '#include <csystem.h>'),
             (15, '#include <bsystem.h>'),
             (20, '#include <cppsystem>'),
             (25, '#include <bppsystem>'),
             (30, '#include "bcustom.h"'),
             (35, '#include "acustom.h"')]
    all_linenums = [linenum for (linenum, _) in scope]
    mock_input_api = MockInputApi()
    warnings = PRESUBMIT._CheckIncludeOrderForScope(scope, mock_input_api,
                                                    '', all_linenums)
    self.assertEqual(3, len(warnings))
    self.assertTrue('15' in warnings[0])
    self.assertTrue('25' in warnings[1])
    self.assertTrue('35' in warnings[2])

  def testSpecialFirstInclude1(self):
    mock_input_api = MockInputApi()
    contents = ['#include "some/path/foo.h"',
                '#include "a/header.h"']
    mock_file = MockFile('some/path/foo.cc', contents)
    warnings = PRESUBMIT._CheckIncludeOrderInFile(
        mock_input_api, mock_file, True, range(1, len(contents) + 1))
    self.assertEqual(0, len(warnings))

  def testSpecialFirstInclude2(self):
    mock_input_api = MockInputApi()
    contents = ['#include "some/other/path/foo.h"',
                '#include "a/header.h"']
    mock_file = MockFile('some/path/foo.cc', contents)
    warnings = PRESUBMIT._CheckIncludeOrderInFile(
        mock_input_api, mock_file, True, range(1, len(contents) + 1))
    self.assertEqual(0, len(warnings))

  def testSpecialFirstInclude3(self):
    mock_input_api = MockInputApi()
    contents = ['#include "some/path/foo.h"',
                '#include "a/header.h"']
    mock_file = MockFile('some/path/foo_platform.cc', contents)
    warnings = PRESUBMIT._CheckIncludeOrderInFile(
        mock_input_api, mock_file, True, range(1, len(contents) + 1))
    self.assertEqual(0, len(warnings))

  def testSpecialFirstInclude4(self):
    mock_input_api = MockInputApi()
    contents = ['#include "some/path/bar.h"',
                '#include "a/header.h"']
    mock_file = MockFile('some/path/foo_platform.cc', contents)
    warnings = PRESUBMIT._CheckIncludeOrderInFile(
        mock_input_api, mock_file, True, range(1, len(contents) + 1))
    self.assertEqual(1, len(warnings))
    self.assertTrue('2' in warnings[0])

  def testOrderAlreadyWrong(self):
    scope = [(1, '#include "b.h"'),
             (2, '#include "a.h"'),
             (3, '#include "c.h"')]
    mock_input_api = MockInputApi()
    warnings = PRESUBMIT._CheckIncludeOrderForScope(scope, mock_input_api,
                                                    '', [3])
    self.assertEqual(0, len(warnings))

  def testConflictAdded1(self):
    scope = [(1, '#include "a.h"'),
             (2, '#include "c.h"'),
             (3, '#include "b.h"')]
    mock_input_api = MockInputApi()
    warnings = PRESUBMIT._CheckIncludeOrderForScope(scope, mock_input_api,
                                                    '', [2])
    self.assertEqual(1, len(warnings))
    self.assertTrue('3' in warnings[0])

  def testConflictAdded2(self):
    scope = [(1, '#include "c.h"'),
             (2, '#include "b.h"'),
             (3, '#include "d.h"')]
    mock_input_api = MockInputApi()
    warnings = PRESUBMIT._CheckIncludeOrderForScope(scope, mock_input_api,
                                                    '', [2])
    self.assertEqual(1, len(warnings))
    self.assertTrue('2' in warnings[0])

  def testIfElifElseEndif(self):
    mock_input_api = MockInputApi()
    contents = ['#include "e.h"',
                '#if foo',
                '#include "d.h"',
                '#elif bar',
                '#include "c.h"',
                '#else',
                '#include "b.h"',
                '#endif',
                '#include "a.h"']
    mock_file = MockFile('', contents)
    warnings = PRESUBMIT._CheckIncludeOrderInFile(
        mock_input_api, mock_file, True, range(1, len(contents) + 1))
    self.assertEqual(0, len(warnings))

  def testSysIncludes(self):
    # #include <sys/...>'s can appear in any order.
    mock_input_api = MockInputApi()
    contents = ['#include <sys/b.h>',
                '#include <sys/a.h>']
    mock_file = MockFile('', contents)
    warnings = PRESUBMIT._CheckIncludeOrderInFile(
        mock_input_api, mock_file, True, range(1, len(contents) + 1))
    self.assertEqual(0, len(warnings))


class VersionControlerConflictsTest(unittest.TestCase):
  def testTypicalConflict(self):
    lines = ['<<<<<<< HEAD',
             '  base::ScopedTempDir temp_dir_;',
             '=======',
             '  ScopedTempDir temp_dir_;',
             '>>>>>>> master']
    errors = PRESUBMIT._CheckForVersionControlConflictsInFile(
        MockInputApi(), MockFile('some/path/foo_platform.cc', lines))
    self.assertEqual(3, len(errors))
    self.assertTrue('1' in errors[0])
    self.assertTrue('3' in errors[1])
    self.assertTrue('5' in errors[2])


if __name__ == '__main__':
  unittest.main()
