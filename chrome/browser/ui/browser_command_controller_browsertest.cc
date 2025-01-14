// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser_command_controller.h"

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog_browsertest.h"
#include "chrome/test/base/in_process_browser_test.h"

typedef InProcessBrowserTest BrowserCommandControllerBrowserTest;

// Verify that showing a constrained window disables find.
IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest, DisableFind) {
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FIND));

  // Showing constrained window should disable find.
  content::WebContents* web_contents = chrome::GetActiveWebContents(browser());
  MockTabModalConfirmDialogDelegate* delegate =
      new MockTabModalConfirmDialogDelegate(web_contents, NULL);
  TabModalConfirmDialog::Create(delegate, web_contents);
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_FIND));

  // Switching to a new (unblocked) tab should reenable it.
  AddBlankTabAndShow(browser());
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FIND));

  // Switching back to the blocked tab should disable it again.
  chrome::ActivateTabAt(browser(), 0, false);
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_FIND));

  // Closing the constrained window should reenable it.
  delegate->Cancel();
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FIND));
}
