// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "googleurl/src/gurl.h"

namespace content {

class ChromeContentBrowserClientBrowserTest : public InProcessBrowserTest {
 public:
  // Returns the last committed navigation entry of the first tab. May be NULL
  // if there is no such entry.
  NavigationEntry* GetLastCommittedEntry() {
    return browser()->tab_strip_model()->GetWebContentsAt(0)->
        GetController().GetLastCommittedEntry();
  }
};

IN_PROC_BROWSER_TEST_F(ChromeContentBrowserClientBrowserTest,
                       UberURLHandler_SettingsPage) {
  const GURL url_short(std::string("chrome://settings/"));
  const GURL url_long(std::string("chrome://chrome/settings/"));

  ui_test_utils::NavigateToURL(browser(), url_short);
  NavigationEntry* entry = GetLastCommittedEntry();

  ASSERT_TRUE(entry != NULL);
  EXPECT_EQ(url_long, entry->GetURL());
  EXPECT_EQ(url_short, entry->GetVirtualURL());
}

IN_PROC_BROWSER_TEST_F(ChromeContentBrowserClientBrowserTest,
                       UberURLHandler_ContentSettingsPage) {
  const GURL url_short(std::string("chrome://settings/content"));
  const GURL url_long(std::string("chrome://chrome/settings/content"));

  ui_test_utils::NavigateToURL(browser(), url_short);
  NavigationEntry* entry = GetLastCommittedEntry();

  ASSERT_TRUE(entry != NULL);
  EXPECT_EQ(url_long, entry->GetURL());
  EXPECT_EQ(url_short, entry->GetVirtualURL());
}

IN_PROC_BROWSER_TEST_F(ChromeContentBrowserClientBrowserTest,
                       UberURLHandler_AboutPage) {
  const GURL url(std::string("chrome://chrome/"));

  ui_test_utils::NavigateToURL(browser(), url);
  NavigationEntry* entry = GetLastCommittedEntry();

  ASSERT_TRUE(entry != NULL);
  EXPECT_EQ(url, entry->GetURL());
  EXPECT_EQ(url, entry->GetVirtualURL());
}

}  // namespace content
