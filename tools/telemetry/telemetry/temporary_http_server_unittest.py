# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import unittest

from telemetry import browser_finder
from telemetry import options_for_unittests

class TemporaryHTTPServerTest(unittest.TestCase):
  def testBasicHosting(self):
    unittest_data_dir = os.path.join(os.path.dirname(__file__),
                                     '..', 'unittest_data')
    options = options_for_unittests.GetCopy()
    browser_to_create = browser_finder.FindBrowser(options)
    with browser_to_create.Create() as b:
      b.SetHTTPServerDirectory(unittest_data_dir)
      with b.ConnectToNthTab(0) as t:
        t.page.Navigate(b.http_server.UrlOf('/blank.html'))
        t.WaitForDocumentReadyStateToBeComplete()
        x = t.runtime.Evaluate('document.body.innerHTML')
        x = x.strip()

        self.assertEquals(x, 'Hello world')

