// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/search/search.h"
#include "chrome/browser/ui/search/search_model.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/browser_with_test_window_test.h"

namespace chrome {
namespace search {

typedef BrowserWithTestWindowTest SearchDelegateTest;

// Test the propagation of search "mode" changes from the tab's search model to
// the browser's search model.
TEST_F(SearchDelegateTest, SearchModel) {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  command_line->AppendSwitch(switches::kEnableInstantExtendedAPI);

  // Avoid these tests on branded Chrome where channel is set to CHANNEL_STABLE.
  if (!chrome::search::IsInstantExtendedAPIEnabled(profile()))
    return;

  // Initial state.
  EXPECT_TRUE(browser()->search_model()->mode().is_default());

  // Propagate change from tab's search model to browser's search model.
  AddTab(browser(), GURL("http://foo/0"));
  content::WebContents* web_contents = chrome::GetWebContentsAt(browser(), 0);
  chrome::search::SearchTabHelper::FromWebContents(web_contents)->model()->
      SetMode(Mode(Mode::MODE_NTP, Mode::ORIGIN_NTP, false));
  EXPECT_TRUE(browser()->search_model()->mode().is_ntp());

  // Add second tab, make it active, and make sure its mode changes
  // propagate to the browser's search model.
  AddTab(browser(), GURL("http://foo/1"));
  chrome::ActivateTabAt(browser(), 1, true);
  web_contents = chrome::GetWebContentsAt(browser(), 1);
  chrome::search::SearchTabHelper::FromWebContents(web_contents)->model()->
      SetMode(Mode(Mode::MODE_SEARCH_RESULTS, Mode::ORIGIN_DEFAULT, false));
  EXPECT_TRUE(browser()->search_model()->mode().is_search());

  // The first tab is not active so changes should not propagate.
  web_contents = chrome::GetWebContentsAt(browser(), 0);
  chrome::search::SearchTabHelper::FromWebContents(web_contents)->model()->
      SetMode(Mode(Mode::MODE_NTP, Mode::ORIGIN_NTP, false));
  EXPECT_TRUE(browser()->search_model()->mode().is_search());
}

}  // namespace search
}  // namespace chrome
