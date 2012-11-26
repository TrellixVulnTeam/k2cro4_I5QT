#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys
import unittest

from file_system import FileNotFoundError
from in_memory_object_store import InMemoryObjectStore
from reference_resolver import ReferenceResolver

class FakeAPIDataSource(object):
  def __init__(self, json_data):
    self._json = json_data

  def get(self, key):
    if key not in self._json:
      raise FileNotFoundError(key)
    return self._json[key]

  def GetAllNames(self):
    return self._json.keys()

class APIDataSourceTest(unittest.TestCase):
  def setUp(self):
    self._base_path = os.path.join(sys.path[0], 'test_data', 'test_json')

  def _ReadLocalFile(self, filename):
    with open(os.path.join(self._base_path, filename), 'r') as f:
      return f.read()

  def testGetLink(self):
    data_source = FakeAPIDataSource(
        json.loads(self._ReadLocalFile('fake_data_source.json')))
    resolver = ReferenceResolver(data_source,
                                 data_source,
                                 InMemoryObjectStore(''))
    self.assertEqual({
      'href': 'foo.html#type-foo_t1',
      'text': 'foo.foo_t1'
    }, resolver.GetLink('baz', 'foo.foo_t1'))
    self.assertEqual({
      'href': 'baz.html#event-baz_e1',
      'text': 'baz_e1'
    }, resolver.GetLink('baz', 'baz.baz_e1'))
    self.assertEqual({
      'href': '#event-baz_e1',
      'text': 'baz_e1'
    }, resolver.GetLink('baz', 'baz_e1'))
    self.assertEqual({
      'href': 'foo.html#method-foo_f1',
      'text': 'foo.foo_f1'
    }, resolver.GetLink('baz', 'foo.foo_f1'))
    self.assertEqual({
      'href': 'foo.html#property-foo_p3',
      'text': 'foo.foo_p3'
    }, resolver.GetLink('baz', 'foo.foo_p3'))
    self.assertEqual({
      'href': 'bar.bon.html#type-bar_bon_t3',
      'text': 'bar.bon.bar_bon_t3'
    }, resolver.GetLink('baz', 'bar.bon.bar_bon_t3'))
    self.assertEqual({
      'href': '#property-bar_bon_p3',
      'text': 'bar_bon_p3'
    }, resolver.GetLink('bar.bon', 'bar_bon_p3'))
    self.assertEqual({
      'href': 'bar.bon.html#property-bar_bon_p3',
      'text': 'bar_bon_p3'
    }, resolver.GetLink('bar.bon', 'bar.bon.bar_bon_p3'))
    self.assertEqual({
      'href': 'bar.html#event-bar_e2',
      'text': 'bar_e2'
    }, resolver.GetLink('bar', 'bar.bar_e2'))
    self.assertEqual({
      'href': 'bar.html#type-bon',
      'text': 'bon'
    }, resolver.GetLink('bar', 'bar.bon'))
    self.assertEqual({
      'href': '#event-foo_t3-foo_t3_e1',
      'text': 'foo_t3.foo_t3_e1'
    }, resolver.GetLink('foo', 'foo_t3.foo_t3_e1'))
    self.assertEqual({
      'href': 'foo.html#event-foo_t3-foo_t3_e1',
      'text': 'foo_t3.foo_t3_e1'
    }, resolver.GetLink('foo', 'foo.foo_t3.foo_t3_e1'))
    self.assertEqual(
        None,
        resolver.GetLink('bar', 'bar.bon.bar_e3'))
    self.assertEqual(
        None,
        resolver.GetLink('baz.bon', 'bar_p3'))
    self.assertEqual(
        None,
        resolver.GetLink('a', 'falafel.faf'))
    self.assertEqual(
        None,
        resolver.GetLink('foo', 'bar_p3'))

if __name__ == '__main__':
  unittest.main()
