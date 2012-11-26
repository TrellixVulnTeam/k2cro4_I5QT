#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import cStringIO
import hashlib
import logging
import os
import sys
import tempfile
import unittest

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT_DIR)

import isolate
# Create shortcuts.
from isolate import KEY_TOUCHED, KEY_TRACKED, KEY_UNTRACKED


def _size(*args):
  return os.stat(os.path.join(ROOT_DIR, *args)).st_size


def _sha1(*args):
  with open(os.path.join(ROOT_DIR, *args), 'rb') as f:
    return hashlib.sha1(f.read()).hexdigest()


class IsolateTest(unittest.TestCase):
  def setUp(self):
    super(IsolateTest, self).setUp()
    # Everything should work even from another directory.
    os.chdir(os.path.dirname(ROOT_DIR))

  def test_load_isolate_for_flavor_empty(self):
    content = "{}"
    command, infiles, touched, read_only = isolate.load_isolate_for_flavor(
        content, isolate.get_flavor())
    self.assertEqual([], command)
    self.assertEqual([], infiles)
    self.assertEqual([], touched)
    self.assertEqual(None, read_only)

  def test_isolated_load_empty(self):
    values = {
    }
    expected = {
      'command': [],
      'files': {},
      'os': isolate.get_flavor(),
    }
    self.assertEqual(expected, isolate.IsolatedFile.load(values).flatten())

  def test_isolated_load(self):
    values = {
      'command': 'maybe',
      'files': {'foo': 42},
      'read_only': 2,
    }
    expected = {
      'command': 'maybe',
      'files': {'foo': 42},
      'os': isolate.get_flavor(),
      'read_only': 2,
    }
    self.assertEqual(expected, isolate.IsolatedFile.load(values).flatten())

  def test_isolated_load_unexpected(self):
    values = {
      'foo': 'bar',
    }
    expected = (
      ("Found unexpected entry {'foo': 'bar'} while constructing an "
          "object IsolatedFile"),
      {'foo': 'bar'},
      'IsolatedFile')
    try:
      isolate.IsolatedFile.load(values)
      self.fail()
    except ValueError, e:
      self.assertEqual(expected, e.args)

  def test_savedstate_load_empty(self):
    values = {
    }
    expected = {
      'variables': {},
    }
    self.assertEqual(expected, isolate.SavedState.load(values).flatten())

  def test_savedstate_load(self):
    values = {
      'isolate_file': os.path.join(ROOT_DIR, 'maybe'),
      'variables': {'foo': 42},
    }
    expected = {
      'isolate_file': os.path.join(ROOT_DIR, 'maybe'),
      'variables': {'foo': 42},
    }
    self.assertEqual(expected, isolate.SavedState.load(values).flatten())

  def test_unknown_key(self):
    try:
      isolate.verify_variables({'foo': [],})
      self.fail()
    except AssertionError:
      pass

  def test_unknown_var(self):
    try:
      isolate.verify_condition({'variables': {'foo': [],}})
      self.fail()
    except AssertionError:
      pass

  def test_union(self):
    value1 = {
      'a': set(['A']),
      'b': ['B', 'C'],
      'c': 'C',
    }
    value2 = {
      'a': set(['B', 'C']),
      'b': [],
      'd': set(),
    }
    expected = {
      'a': set(['A', 'B', 'C']),
      'b': ['B', 'C'],
      'c': 'C',
      'd': set(),
    }
    self.assertEqual(expected, isolate.union(value1, value2))

  def test_eval_content(self):
    try:
      # Intrinsics are not available.
      isolate.eval_content('map(str, [1, 2])')
      self.fail()
    except NameError:
      pass

  def test_load_isolate_as_config_empty(self):
    self.assertEqual({}, isolate.load_isolate_as_config(
      {}, None, []).flatten())

  def test_load_isolate_as_config(self):
    value = {
      'variables': {
        KEY_TRACKED: ['a'],
        KEY_UNTRACKED: ['b'],
        KEY_TOUCHED: ['touched'],
      },
      'conditions': [
        ['OS=="atari"', {
          'variables': {
            KEY_TRACKED: ['c', 'x'],
            KEY_UNTRACKED: ['d'],
            KEY_TOUCHED: ['touched_a'],
            'command': ['echo', 'Hello World'],
            'read_only': True,
          },
        }, {  # else
          'variables': {
            KEY_TRACKED: ['e', 'x'],
            KEY_UNTRACKED: ['f'],
            KEY_TOUCHED: ['touched_e'],
            'command': ['echo', 'You should get an Atari'],
          },
        }],
        ['OS=="amiga"', {
          'variables': {
            KEY_TRACKED: ['g'],
            'read_only': False,
          },
        }],
        ['OS=="dendy"', {
        }],
        ['OS=="coleco"', {
        }, {  # else
          'variables': {
            KEY_UNTRACKED: ['h'],
            'read_only': None,
          },
        }],
      ],
    }
    expected = {
      'amiga': {
        'command': ['echo', 'You should get an Atari'],
        KEY_TOUCHED: ['touched', 'touched_e'],
        KEY_TRACKED: ['a', 'e', 'g', 'x'],
        KEY_UNTRACKED: ['b', 'f', 'h'],
        'read_only': False,
      },
      'atari': {
        'command': ['echo', 'Hello World'],
        KEY_TOUCHED: ['touched', 'touched_a'],
        KEY_TRACKED: ['a', 'c', 'x'],
        KEY_UNTRACKED: ['b', 'd', 'h'],
        'read_only': True,
      },
      'coleco': {
        'command': ['echo', 'You should get an Atari'],
        KEY_TOUCHED: ['touched', 'touched_e'],
        KEY_TRACKED: ['a', 'e', 'x'],
        KEY_UNTRACKED: ['b', 'f'],
      },
      'dendy': {
        'command': ['echo', 'You should get an Atari'],
        KEY_TOUCHED: ['touched', 'touched_e'],
        KEY_TRACKED: ['a', 'e', 'x'],
        KEY_UNTRACKED: ['b', 'f', 'h'],
      },
    }
    self.assertEqual(
        expected, isolate.load_isolate_as_config(value, None, []).flatten())

  def test_load_isolate_as_config_duplicate_command(self):
    value = {
      'variables': {
        'command': ['rm', '-rf', '/'],
      },
      'conditions': [
        ['OS=="atari"', {
          'variables': {
            'command': ['echo', 'Hello World'],
          },
        }],
      ],
    }
    try:
      isolate.load_isolate_as_config(value, None, [])
      self.fail()
    except AssertionError:
      pass

  def test_load_isolate_as_config_no_condition(self):
    value = {
      'variables': {
        KEY_TRACKED: ['a'],
        KEY_UNTRACKED: ['b'],
      },
    }
    expected = {
      KEY_TRACKED: ['a'],
      KEY_UNTRACKED: ['b'],
    }
    actual = isolate.load_isolate_as_config(value, None, [])
    # Flattening the whole config will discard 'None'.
    self.assertEqual({}, actual.flatten())
    self.assertEqual([None], actual.per_os.keys())
    # But the 'None' value is still available as a backup.
    self.assertEqual(expected, actual.per_os[None].flatten())

  def test_invert_map(self):
    value = {
      'amiga': {
        'command': ['echo', 'You should get an Atari'],
        KEY_TOUCHED: ['touched', 'touched_e'],
        KEY_TRACKED: ['a', 'e', 'g', 'x'],
        KEY_UNTRACKED: ['b', 'f', 'h'],
        'read_only': False,
      },
      'atari': {
        'command': ['echo', 'Hello World'],
        KEY_TOUCHED: ['touched', 'touched_a'],
        KEY_TRACKED: ['a', 'c', 'x'],
        KEY_UNTRACKED: ['b', 'd', 'h'],
        'read_only': True,
      },
      'coleco': {
        'command': ['echo', 'You should get an Atari'],
        KEY_TOUCHED: ['touched', 'touched_e'],
        KEY_TRACKED: ['a', 'e', 'x'],
        KEY_UNTRACKED: ['b', 'f'],
      },
      'dendy': {
        'command': ['echo', 'You should get an Atari'],
        KEY_TOUCHED: ['touched', 'touched_e'],
        KEY_TRACKED: ['a', 'e', 'x'],
        KEY_UNTRACKED: ['b', 'f', 'h'],
      },
    }
    expected_values = {
      'command': {
        ('echo', 'Hello World'): set(['atari']),
        ('echo', 'You should get an Atari'): set(['amiga', 'coleco', 'dendy']),
      },
      KEY_TRACKED: {
        'a': set(['amiga', 'atari', 'coleco', 'dendy']),
        'c': set(['atari']),
        'e': set(['amiga', 'coleco', 'dendy']),
        'g': set(['amiga']),
        'x': set(['amiga', 'atari', 'coleco', 'dendy']),
      },
      KEY_UNTRACKED: {
        'b': set(['amiga', 'atari', 'coleco', 'dendy']),
        'd': set(['atari']),
        'f': set(['amiga', 'coleco', 'dendy']),
        'h': set(['amiga', 'atari', 'dendy']),
      },
      KEY_TOUCHED: {
        'touched': set(['amiga', 'atari', 'coleco', 'dendy']),
        'touched_a': set(['atari']),
        'touched_e': set(['amiga', 'coleco', 'dendy']),
      },
      'read_only': {
        None: set(['coleco', 'dendy']),
        False: set(['amiga']),
        True: set(['atari']),
      },
    }
    expected_oses = set(['amiga', 'atari', 'coleco', 'dendy'])
    actual_values, actual_oses = isolate.invert_map(value)
    self.assertEqual(expected_values, actual_values)
    self.assertEqual(expected_oses, actual_oses)

  def test_reduce_inputs(self):
    values = {
      'command': {
        ('echo', 'Hello World'): set(['atari']),
        ('echo', 'You should get an Atari'): set(['amiga', 'coleco', 'dendy']),
      },
      KEY_TRACKED: {
        'a': set(['amiga', 'atari', 'coleco', 'dendy']),
        'c': set(['atari']),
        'e': set(['amiga', 'coleco', 'dendy']),
        'g': set(['amiga']),
        'x': set(['amiga', 'atari', 'coleco', 'dendy']),
      },
      KEY_UNTRACKED: {
        'b': set(['amiga', 'atari', 'coleco', 'dendy']),
        'd': set(['atari']),
        'f': set(['amiga', 'coleco', 'dendy']),
        'h': set(['amiga', 'atari', 'dendy']),
      },
      KEY_TOUCHED: {
        'touched': set(['amiga', 'atari', 'coleco', 'dendy']),
        'touched_a': set(['atari']),
        'touched_e': set(['amiga', 'coleco', 'dendy']),
      },
      'read_only': {
        None: set(['coleco', 'dendy']),
        False: set(['amiga']),
        True: set(['atari']),
      },
    }
    oses = set(['amiga', 'atari', 'coleco', 'dendy'])
    expected_values = {
      'command': {
        ('echo', 'Hello World'): set(['atari']),
        ('echo', 'You should get an Atari'): set(['!atari']),
      },
      KEY_TRACKED: {
        'a': set([None]),
        'c': set(['atari']),
        'e': set(['!atari']),
        'g': set(['amiga']),
        'x': set([None]),
      },
      KEY_UNTRACKED: {
        'b': set([None]),
        'd': set(['atari']),
        'f': set(['!atari']),
        'h': set(['!coleco']),
      },
      KEY_TOUCHED: {
        'touched': set([None]),
        'touched_a': set(['atari']),
        'touched_e': set(['!atari']),
      },
      'read_only': {
        None: set(['coleco', 'dendy']),
        False: set(['amiga']),
        True: set(['atari']),
      },
    }
    actual_values, actual_oses = isolate.reduce_inputs(values, oses)
    self.assertEqual(expected_values, actual_values)
    self.assertEqual(oses, actual_oses)

  def test_reduce_inputs_merge_subfolders_and_files(self):
    values = {
      'command': {},
      KEY_TRACKED: {
        'folder/tracked_file': set(['win']),
        'folder_helper/tracked_file': set(['win']),
      },
      KEY_UNTRACKED: {
        'folder/': set(['linux', 'mac', 'win']),
        'folder/subfolder/': set(['win']),
        'folder/untracked_file': set(['linux', 'mac', 'win']),
        'folder_helper/': set(['linux']),
      },
      KEY_TOUCHED: {
        'folder/touched_file': set (['win']),
        'folder/helper_folder/deep_file': set(['win']),
        'folder_helper/touched_file1': set (['mac', 'win']),
        'folder_helper/touched_file2': set (['linux']),
      },
    }
    oses = set(['linux', 'mac', 'win'])
    expected_values = {
      'command': {},
      KEY_TRACKED: {
        'folder_helper/tracked_file': set(['win']),
      },
      KEY_UNTRACKED: {
        'folder/': set([None]),
        'folder_helper/': set(['linux']),
      },
      KEY_TOUCHED: {
        'folder_helper/touched_file1': set (['!linux']),
      },
      'read_only': {},
    }
    actual_values, actual_oses = isolate.reduce_inputs(values, oses)
    self.assertEqual(expected_values, actual_values)
    self.assertEqual(oses, actual_oses)

  def test_reduce_inputs_take_strongest_dependency(self):
    values = {
      'command': {
        ('echo', 'Hello World'): set(['atari']),
        ('echo', 'You should get an Atari'): set(['amiga', 'coleco', 'dendy']),
      },
      KEY_TRACKED: {
        'a': set(['amiga', 'atari', 'coleco', 'dendy']),
        'b': set(['amiga', 'atari', 'coleco']),
      },
      KEY_UNTRACKED: {
        'c': set(['amiga', 'atari', 'coleco', 'dendy']),
        'd': set(['amiga', 'coleco', 'dendy']),
      },
      KEY_TOUCHED: {
        'a': set(['amiga', 'atari', 'coleco', 'dendy']),
        'b': set(['atari', 'coleco', 'dendy']),
        'c': set(['amiga', 'atari', 'coleco', 'dendy']),
        'd': set(['atari', 'coleco', 'dendy']),
      },
    }
    oses = set(['amiga', 'atari', 'coleco', 'dendy'])
    expected_values = {
      'command': {
        ('echo', 'Hello World'): set(['atari']),
        ('echo', 'You should get an Atari'): set(['!atari']),
      },
      KEY_TRACKED: {
        'a': set([None]),
        'b': set(['!dendy']),
      },
      KEY_UNTRACKED: {
        'c': set([None]),
        'd': set(['!atari']),
      },
      KEY_TOUCHED: {
        'b': set(['dendy']),
        'd': set(['atari']),
      },
      'read_only': {},
    }
    actual_values, actual_oses = isolate.reduce_inputs(values, oses)
    self.assertEqual(expected_values, actual_values)
    self.assertEqual(oses, actual_oses)

  def test_convert_map_to_isolate_dict(self):
    values = {
      'command': {
        ('echo', 'Hello World'): set(['atari']),
        ('echo', 'You should get an Atari'): set(['!atari']),
      },
      KEY_TRACKED: {
        'a': set([None]),
        'c': set(['atari']),
        'e': set(['!atari']),
        'g': set(['amiga']),
        'x': set([None]),
      },
      KEY_UNTRACKED: {
        'b': set([None]),
        'd': set(['atari']),
        'f': set(['!atari']),
        'h': set(['!coleco']),
      },
      KEY_TOUCHED: {
        'touched': set([None]),
        'touched_a': set(['atari']),
        'touched_e': set(['!atari']),
      },
      'read_only': {
        None: set(['coleco', 'dendy']),
        False: set(['amiga']),
        True: set(['atari']),
      },
    }
    oses = set(['amiga', 'atari', 'coleco', 'dendy'])
    expected = {
      'variables': {
        KEY_TRACKED: ['a', 'x'],
        KEY_UNTRACKED: ['b'],
        KEY_TOUCHED: ['touched'],
      },
      'conditions': [
        ['OS=="amiga"', {
          'variables': {
            KEY_TRACKED: ['g'],
            'read_only': False,
          },
        }],
        ['OS=="atari"', {
          'variables': {
            'command': ['echo', 'Hello World'],
            KEY_TRACKED: ['c'],
            KEY_UNTRACKED: ['d'],
            KEY_TOUCHED: ['touched_a'],
            'read_only': True,
          },
        }, {
          'variables': {
            'command': ['echo', 'You should get an Atari'],
            KEY_TRACKED: ['e'],
            KEY_UNTRACKED: ['f'],
            KEY_TOUCHED: ['touched_e'],
          },
        }],
        ['OS=="coleco"', {
        }, {
          'variables': {
            KEY_UNTRACKED: ['h'],
          },
        }],
      ],
    }
    self.assertEqual(
        expected, isolate.convert_map_to_isolate_dict(values, oses))

  def test_merge_two_empty(self):
    # Flat stay flat. Pylint is confused about union() return type.
    # pylint: disable=E1103
    actual = isolate.union(
        isolate.union(
          isolate.Configs([], None),
          isolate.load_isolate_as_config({}, None, [])),
        isolate.load_isolate_as_config({}, None, [])).flatten()
    self.assertEqual({}, actual)

  def test_merge_empty(self):
    actual = isolate.convert_map_to_isolate_dict(
        *isolate.reduce_inputs(*isolate.invert_map({})))
    self.assertEqual({}, actual)

  def test_load_two_conditions(self):
    linux = {
      'conditions': [
        ['OS=="linux"', {
          'variables': {
            'isolate_dependency_tracked': [
              'file_linux',
              'file_common',
            ],
          },
        }],
      ],
    }
    mac = {
      'conditions': [
        ['OS=="mac"', {
          'variables': {
            'isolate_dependency_tracked': [
              'file_mac',
              'file_common',
            ],
          },
        }],
      ],
    }
    expected = {
      'linux': {
        'isolate_dependency_tracked': ['file_common', 'file_linux'],
      },
      'mac': {
        'isolate_dependency_tracked': ['file_common', 'file_mac'],
      },
    }
    # Pylint is confused about union() return type.
    # pylint: disable=E1103
    configs = isolate.union(
        isolate.union(
          isolate.Configs([], None),
          isolate.load_isolate_as_config(linux, None, [])),
        isolate.load_isolate_as_config(mac, None, [])).flatten()
    self.assertEqual(expected, configs)

  def test_load_three_conditions(self):
    linux = {
      'conditions': [
        ['OS=="linux"', {
          'variables': {
            'isolate_dependency_tracked': [
              'file_linux',
              'file_common',
            ],
          },
        }],
      ],
    }
    mac = {
      'conditions': [
        ['OS=="mac"', {
          'variables': {
            'isolate_dependency_tracked': [
              'file_mac',
              'file_common',
            ],
          },
        }],
      ],
    }
    win = {
      'conditions': [
        ['OS=="win"', {
          'variables': {
            'isolate_dependency_tracked': [
              'file_win',
              'file_common',
            ],
          },
        }],
      ],
    }
    expected = {
      'linux': {
        'isolate_dependency_tracked': ['file_common', 'file_linux'],
      },
      'mac': {
        'isolate_dependency_tracked': ['file_common', 'file_mac'],
      },
      'win': {
        'isolate_dependency_tracked': ['file_common', 'file_win'],
      },
    }
    # Pylint is confused about union() return type.
    # pylint: disable=E1103
    configs = isolate.union(
        isolate.union(
          isolate.union(
            isolate.Configs([], None),
            isolate.load_isolate_as_config(linux, None, [])),
          isolate.load_isolate_as_config(mac, None, [])),
        isolate.load_isolate_as_config(win, None, [])).flatten()
    self.assertEqual(expected, configs)

  def test_merge_three_conditions(self):
    values = {
      'linux': {
        'isolate_dependency_tracked': ['file_common', 'file_linux'],
      },
      'mac': {
        'isolate_dependency_tracked': ['file_common', 'file_mac'],
      },
      'win': {
        'isolate_dependency_tracked': ['file_common', 'file_win'],
      },
    }
    expected = {
      'variables': {
        'isolate_dependency_tracked': [
          'file_common',
        ],
      },
      'conditions': [
        ['OS=="linux"', {
          'variables': {
            'isolate_dependency_tracked': [
              'file_linux',
            ],
          },
        }],
        ['OS=="mac"', {
          'variables': {
            'isolate_dependency_tracked': [
              'file_mac',
            ],
          },
        }],
        ['OS=="win"', {
          'variables': {
            'isolate_dependency_tracked': [
              'file_win',
            ],
          },
        }],
      ],
    }
    actual = isolate.convert_map_to_isolate_dict(
        *isolate.reduce_inputs(*isolate.invert_map(values)))
    self.assertEqual(expected, actual)

  def test_configs_comment(self):
    # Pylint is confused with isolate.union() return type.
    # pylint: disable=E1103
    configs = isolate.union(
        isolate.load_isolate_as_config({}, '# Yo dawg!\n# Chill out.\n', []),
        isolate.load_isolate_as_config({}, None, []))
    self.assertEqual('# Yo dawg!\n# Chill out.\n', configs.file_comment)

    configs = isolate.union(
        isolate.load_isolate_as_config({}, None, []),
        isolate.load_isolate_as_config({}, '# Yo dawg!\n# Chill out.\n', []))
    self.assertEqual('# Yo dawg!\n# Chill out.\n', configs.file_comment)

    # Only keep the first one.
    configs = isolate.union(
        isolate.load_isolate_as_config({}, '# Yo dawg!\n', []),
        isolate.load_isolate_as_config({}, '# Chill out.\n', []))
    self.assertEqual('# Yo dawg!\n', configs.file_comment)

  def test_extract_comment(self):
    self.assertEqual(
        '# Foo\n# Bar\n', isolate.extract_comment('# Foo\n# Bar\n{}'))
    self.assertEqual('', isolate.extract_comment('{}'))

  def _test_pretty_print_impl(self, value, expected):
    actual = cStringIO.StringIO()
    isolate.pretty_print(value, actual)
    self.assertEqual(expected, actual.getvalue())

  def test_pretty_print_empty(self):
    self._test_pretty_print_impl({}, '{\n}\n')

  def test_pretty_print_mid_size(self):
    value = {
      'variables': {
        'bar': [
          'file1',
          'file2',
        ],
      },
      'conditions': [
        ['OS=\"foo\"', {
          'variables': {
            isolate.KEY_UNTRACKED: [
              'dir1',
              'dir2',
            ],
            isolate.KEY_TRACKED: [
              'file4',
              'file3',
            ],
            'command': ['python', '-c', 'print "H\\i\'"'],
            'read_only': True,
            'relative_cwd': 'isol\'at\\e',
          },
        }],
        ['OS=\"bar\"', {
          'variables': {},
        }, {
          'variables': {},
        }],
      ],
    }
    expected = (
        "{\n"
        "  'variables': {\n"
        "    'bar': [\n"
        "      'file1',\n"
        "      'file2',\n"
        "    ],\n"
        "  },\n"
        "  'conditions': [\n"
        "    ['OS=\"foo\"', {\n"
        "      'variables': {\n"
        "        'command': [\n"
        "          'python',\n"
        "          '-c',\n"
        "          'print \"H\\i\'\"',\n"
        "        ],\n"
        "        'relative_cwd': 'isol\\'at\\\\e',\n"
        "        'read_only': True\n"
        "        'isolate_dependency_tracked': [\n"
        "          'file4',\n"
        "          'file3',\n"
        "        ],\n"
        "        'isolate_dependency_untracked': [\n"
        "          'dir1',\n"
        "          'dir2',\n"
        "        ],\n"
        "      },\n"
        "    }],\n"
        "    ['OS=\"bar\"', {\n"
        "      'variables': {\n"
        "      },\n"
        "    }, {\n"
        "      'variables': {\n"
        "      },\n"
        "    }],\n"
        "  ],\n"
        "}\n")
    self._test_pretty_print_impl(value, expected)


class IsolateLoad(unittest.TestCase):
  def setUp(self):
    super(IsolateLoad, self).setUp()
    self.directory = tempfile.mkdtemp(prefix='isolate_')

  def tearDown(self):
    isolate.run_isolated.rmtree(self.directory)
    super(IsolateLoad, self).tearDown()

  def _get_option(self, isolate_file):
    class Options(object):
      isolated = os.path.join(self.directory, 'isolated')
      outdir = os.path.join(self.directory, 'outdir')
      isolate = isolate_file
      variables = {'foo': 'bar'}
      ignore_broken_items = False
    return Options()

  def _cleanup_isolated(self, expected_isolated, actual_isolated):
    """Modifies isolated to remove the non-deterministic parts."""
    if sys.platform == 'win32':
      # 'm' are not saved in windows.
      for values in expected_isolated['files'].itervalues():
        del values['m']

    for item in actual_isolated['files'].itervalues():
      if 't' in item:
        self.assertTrue(item.pop('t'))

  def test_load_stale_isolated(self):
    isolate_file = os.path.join(
        ROOT_DIR, 'tests', 'isolate', 'touch_root.isolate')

    # Data to be loaded in the .isolated file. Do not create a .state file.
    input_data = {
      'command': ['python'],
      'files': {
        'foo': {
          "m": 416,
          "h": "invalid",
          "s": 538,
          "t": 1335146921,
        },
        os.path.join('tests', 'isolate', 'touch_root.py'): {
          "m": 488,
          "h": "invalid",
          "s": 538,
          "t": 1335146921,
        },
      },
    }
    options = self._get_option(isolate_file)
    isolate.trace_inputs.write_json(options.isolated, input_data, False)

    # A CompleteState object contains two parts:
    # - Result instance stored in complete_state.isolated, corresponding to the
    #   .isolated file, is what is read by run_test_from_archive.py.
    # - SavedState instance stored in compelte_state.saved_state,
    #   corresponding to the .state file, which is simply to aid the developer
    #   when re-running the same command multiple times and contain
    #   discardable information.
    complete_state = isolate.load_complete_state(options, None)
    actual_isolated = complete_state.isolated.flatten()
    actual_saved_state = complete_state.saved_state.flatten()

    expected_isolated = {
      'command': ['python', 'touch_root.py'],
      'files': {
        os.path.join(u'tests', 'isolate', 'touch_root.py'): {
          'm': 488,
          'h': _sha1('tests', 'isolate', 'touch_root.py'),
          's': _size('tests', 'isolate', 'touch_root.py'),
        },
        'isolate.py': {
          'm': 488,
          'h': _sha1('isolate.py'),
          's': _size('isolate.py'),
        },
      },
      'os': isolate.get_flavor(),
      'relative_cwd': os.path.join('tests', 'isolate'),
    }
    self._cleanup_isolated(expected_isolated, actual_isolated)
    self.assertEqual(expected_isolated, actual_isolated)

    expected_saved_state = {
      'isolate_file': isolate_file,
      'variables': {'foo': 'bar'},
    }
    self.assertEqual(expected_saved_state, actual_saved_state)

  def test_subdir(self):
    # The resulting .isolated file will be missing ../../isolate.py. It is
    # because this file is outside the --subdir parameter.
    isolate_file = os.path.join(
        ROOT_DIR, 'tests', 'isolate', 'touch_root.isolate')
    options = self._get_option(isolate_file)
    complete_state = isolate.load_complete_state(
        options, os.path.join('tests', 'isolate'))
    actual_isolated = complete_state.isolated.flatten()
    actual_saved_state = complete_state.saved_state.flatten()

    expected_isolated =  {
      'command': ['python', 'touch_root.py'],
      'files': {
        os.path.join('tests', 'isolate', 'touch_root.py'): {
          'm': 488,
          'h': _sha1('tests', 'isolate', 'touch_root.py'),
          's': _size('tests', 'isolate', 'touch_root.py'),
        },
      },
      'os': isolate.get_flavor(),
      'relative_cwd': os.path.join('tests', 'isolate'),
    }
    self._cleanup_isolated(expected_isolated, actual_isolated)
    self.assertEqual(expected_isolated, actual_isolated)

    expected_saved_state = {
      'isolate_file': isolate_file,
      'variables': {'foo': 'bar'},
    }
    self.assertEqual(expected_saved_state, actual_saved_state)

  def test_subdir_variable(self):
    # The resulting .isolated file will be missing ../../isolate.py. It is
    # because this file is outside the --subdir parameter.
    isolate_file = os.path.join(
        ROOT_DIR, 'tests', 'isolate', 'touch_root.isolate')
    options = self._get_option(isolate_file)
    options.variables['BAZ'] = os.path.join('tests', 'isolate')
    complete_state = isolate.load_complete_state(options, '<(BAZ)')
    actual_isolated = complete_state.isolated.flatten()
    actual_saved_state = complete_state.saved_state.flatten()

    expected_isolated =  {
      'command': ['python', 'touch_root.py'],
      'files': {
        os.path.join('tests', 'isolate', 'touch_root.py'): {
          'm': 488,
          'h': _sha1('tests', 'isolate', 'touch_root.py'),
          's': _size('tests', 'isolate', 'touch_root.py'),
        },
      },
      'os': isolate.get_flavor(),
      'relative_cwd': os.path.join('tests', 'isolate'),
    }
    self._cleanup_isolated(expected_isolated, actual_isolated)
    self.assertEqual(expected_isolated, actual_isolated)

    expected_saved_state = {
      'isolate_file': isolate_file,
      'variables': {
        'foo': 'bar',
        'BAZ': os.path.join('tests', 'isolate'),
      },
    }
    self.assertEqual(expected_saved_state, actual_saved_state)


if __name__ == '__main__':
  logging.basicConfig(
      level=logging.DEBUG if '-v' in sys.argv else logging.ERROR,
      format='%(levelname)5s %(filename)15s(%(lineno)3d): %(message)s')
  unittest.main()
