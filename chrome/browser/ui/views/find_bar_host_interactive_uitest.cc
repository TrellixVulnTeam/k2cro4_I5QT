// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process_util.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/find_bar/find_notification_details.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/find_bar_host.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "net/test/test_server.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/views_delegate.h"

using content::WebContents;

namespace {

// The delay waited after sending an OS simulated event.
static const int kActionDelayMs = 500;
static const char kSimplePage[] = "files/find_in_page/simple.html";

void Checkpoint(const char* message, const base::TimeTicks& start_time) {
  LOG(INFO) << message << " : "
    << (base::TimeTicks::Now() - start_time).InMilliseconds()
    << " ms" << std::flush;
}

class FindInPageTest : public InProcessBrowserTest {
 public:
  FindInPageTest() :
#if defined(USE_AURA)
      location_bar_focus_view_id_(VIEW_ID_OMNIBOX)
#else
      location_bar_focus_view_id_(VIEW_ID_LOCATION_BAR)
#endif
  {
    FindBarHost::disable_animations_during_testing_ = true;
  }

  string16 GetFindBarText() {
    FindBarTesting* find_bar =
        browser()->GetFindBarController()->find_bar()->GetFindBarTesting();
    return find_bar->GetFindText();
  }

  string16 GetFindBarSelectedText() {
    FindBarTesting* find_bar =
        browser()->GetFindBarController()->find_bar()->GetFindBarTesting();
    return find_bar->GetFindSelectedText();
  }

  ViewID location_bar_focus_view_id_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FindInPageTest);
};

}  // namespace

#if defined(OS_WIN)
#define MAYBE_CrashEscHandlers DISABLED_CrashEscHandlers
#else
#define MAYBE_CrashEscHandlers CrashEscHandlers
#endif
IN_PROC_BROWSER_TEST_F(FindInPageTest, MAYBE_CrashEscHandlers) {
  ASSERT_TRUE(test_server()->Start());

  // First we navigate to our test page (tab A).
  GURL url = test_server()->GetURL(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  chrome::Find(browser());

  // Open another tab (tab B).
  chrome::AddSelectedTabWithURL(browser(), url, content::PAGE_TRANSITION_TYPED);

  chrome::Find(browser());
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));

  // Select tab A.
  chrome::ActivateTabAt(browser(), 0, true);

  // Close tab B.
  chrome::CloseWebContents(browser(), chrome::GetWebContentsAt(browser(), 1));

  // Click on the location bar so that Find box loses focus.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::ClickOnView(browser(),
                                                     VIEW_ID_LOCATION_BAR));
  // Check the location bar is focused.
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           location_bar_focus_view_id_));

  // This used to crash until bug 1303709 was fixed.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_ESCAPE, false, false, false, false));
}

IN_PROC_BROWSER_TEST_F(FindInPageTest, FocusRestore) {
  ASSERT_TRUE(test_server()->Start());

  GURL url = test_server()->GetURL("title1.html");
  ui_test_utils::NavigateToURL(browser(), url);

  // Focus the location bar, open and close the find-in-page, focus should
  // return to the location bar.
  chrome::FocusLocationBar(browser());
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           location_bar_focus_view_id_));
  // Ensure the creation of the find bar controller.
  browser()->GetFindBarController()->Show();
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelectionOnPage,
      FindBarController::kKeepResultsInFindBox);
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           location_bar_focus_view_id_));

  // Focus the location bar, find something on the page, close the find box,
  // focus should go to the page.
  chrome::FocusLocationBar(browser());
  chrome::Find(browser());
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));
  ui_test_utils::FindInPage(chrome::GetActiveWebContents(browser()),
                            ASCIIToUTF16("a"), true, false, NULL, NULL);
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelectionOnPage,
      FindBarController::kKeepResultsInFindBox);
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_TAB_CONTAINER));

  // Focus the location bar, open and close the find box, focus should return to
  // the location bar (same as before, just checking that http://crbug.com/23599
  // is fixed).
  chrome::FocusLocationBar(browser());
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           location_bar_focus_view_id_));
  browser()->GetFindBarController()->Show();
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelectionOnPage,
      FindBarController::kKeepResultsInFindBox);
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           location_bar_focus_view_id_));
}

#if defined(OS_WIN)
// crbug.com/128724
#define MAYBE_FocusRestoreOnTabSwitch DISABLED_FocusRestoreOnTabSwitch
#else
#define MAYBE_FocusRestoreOnTabSwitch FocusRestoreOnTabSwitch
#endif
IN_PROC_BROWSER_TEST_F(FindInPageTest, MAYBE_FocusRestoreOnTabSwitch) {
  ASSERT_TRUE(test_server()->Start());

  // First we navigate to our test page (tab A).
  GURL url = test_server()->GetURL(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  chrome::Find(browser());
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));

  FindBarTesting* find_bar =
      browser()->GetFindBarController()->find_bar()->GetFindBarTesting();

  // Search for 'a'.
  ui_test_utils::FindInPage(chrome::GetActiveWebContents(browser()),
                            ASCIIToUTF16("a"), true, false, NULL, NULL);
  EXPECT_TRUE(ASCIIToUTF16("a") == find_bar->GetFindSelectedText());

  // Open another tab (tab B).
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_STOP,
      content::NotificationService::AllSources());
  chrome::AddSelectedTabWithURL(browser(), url, content::PAGE_TRANSITION_TYPED);
  observer.Wait();

  // Make sure Find box is open.
  chrome::Find(browser());
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));

  // Search for 'b'.
  ui_test_utils::FindInPage(chrome::GetActiveWebContents(browser()),
                            ASCIIToUTF16("b"), true, false, NULL, NULL);
  EXPECT_TRUE(ASCIIToUTF16("b") == find_bar->GetFindSelectedText());

  // Set focus away from the Find bar (to the Location bar).
  chrome::FocusLocationBar(browser());
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           location_bar_focus_view_id_));

  // Select tab A. Find bar should get focus.
  chrome::ActivateTabAt(browser(), 0, true);
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));
  EXPECT_TRUE(ASCIIToUTF16("a") == find_bar->GetFindSelectedText());

  // Select tab B. Location bar should get focus.
  chrome::ActivateTabAt(browser(), 1, true);
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           location_bar_focus_view_id_));
}

// Flaky on XP: http://crbug.com/152100
#if defined(OS_WIN)
#define MAYBE_PrepopulateRespectBlank DISABLED_PrepopulateRespectBlank
#else
#define MAYBE_PrepopulateRespectBlank PrepopulateRespectBlank
#endif
// This tests that whenever you clear values from the Find box and close it that
// it respects that and doesn't show you the last search, as reported in bug:
// http://crbug.com/40121.
IN_PROC_BROWSER_TEST_F(FindInPageTest, MAYBE_PrepopulateRespectBlank) {
#if defined(OS_MACOSX)
  // FindInPage on Mac doesn't use prepopulated values. Search there is global.
  return;
#endif
  base::TimeTicks start_time = base::TimeTicks::Now();
  Checkpoint("Test starting", start_time);

  ASSERT_TRUE(test_server()->Start());

  // Make sure Chrome is in the foreground, otherwise sending input
  // won't do anything and the test will hang.
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  Checkpoint("Navigate", start_time);

  // First we navigate to any page.
  GURL url = test_server()->GetURL(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  Checkpoint("Show Find bar", start_time);

  // Show the Find bar.
  browser()->GetFindBarController()->Show();

  Checkpoint("Search for 'a'", start_time);

  // Search for "a".
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_A, false, false, false, false));

  // We should find "a" here.
  EXPECT_EQ(ASCIIToUTF16("a"), GetFindBarText());

  Checkpoint("Delete 'a'", start_time);

  // Delete "a".
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_BACK, false, false, false, false));

  // Validate we have cleared the text.
  EXPECT_EQ(string16(), GetFindBarText());

  Checkpoint("Close find bar", start_time);

  // Close the Find box.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_ESCAPE, false, false, false, false));

  Checkpoint("Show Find bar", start_time);

  // Show the Find bar.
  browser()->GetFindBarController()->Show();

  Checkpoint("Validate text", start_time);

  // After the Find box has been reopened, it should not have been prepopulated
  // with "a" again.
  EXPECT_EQ(string16(), GetFindBarText());

  Checkpoint("Close Find bar", start_time);

  // Close the Find box.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_ESCAPE, false, false, false, false));

  Checkpoint("FindNext", start_time);

  // Press F3 to trigger FindNext.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_F3, false, false, false, false));

  Checkpoint("Validate", start_time);

  // After the Find box has been reopened, it should still have no prepopulate
  // value.
  EXPECT_EQ(string16(), GetFindBarText());

  Checkpoint("Test done", start_time);
}

// Flaky on Win. http://crbug.com/92467
// Flaky on ChromeOS. http://crbug.com/118216
#if defined(OS_WIN) || defined(OS_CHROMEOS)
#define MAYBE_PasteWithoutTextChange DISABLED_PasteWithoutTextChange
#else
#define MAYBE_PasteWithoutTextChange PasteWithoutTextChange
#endif

IN_PROC_BROWSER_TEST_F(FindInPageTest, MAYBE_PasteWithoutTextChange) {
  ASSERT_TRUE(test_server()->Start());

  // Make sure Chrome is in the foreground, otherwise sending input
  // won't do anything and the test will hang.
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  // First we navigate to any page.
  GURL url = test_server()->GetURL(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  // Show the Find bar.
  browser()->GetFindBarController()->Show();

  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));

  // Search for "a".
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_A, false, false, false, false));

  // We should find "a" here.
  EXPECT_EQ(ASCIIToUTF16("a"), GetFindBarText());

  // Reload the page to clear the matching result.
  chrome::Reload(browser(), CURRENT_TAB);

  // Focus the Find bar again to make sure the text is selected.
  browser()->GetFindBarController()->Show();

  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(),
                                           VIEW_ID_FIND_IN_PAGE_TEXT_FIELD));

  // "a" should be selected.
  EXPECT_EQ(ASCIIToUTF16("a"), GetFindBarSelectedText());

  // Press Ctrl-C to copy the content.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_C, true, false, false, false));

  string16 str;
  ui::Clipboard::GetForCurrentThread()->
      ReadText(ui::Clipboard::BUFFER_STANDARD, &str);

  // Make sure the text is copied successfully.
  EXPECT_EQ(ASCIIToUTF16("a"), str);

  // Press Ctrl-V to paste the content back, it should start finding even if the
  // content is not changed.
  content::Source<WebContents> notification_source(
      chrome::GetActiveWebContents(browser()));
  ui_test_utils::WindowedNotificationObserverWithDetails
      <FindNotificationDetails> observer(
          chrome::NOTIFICATION_FIND_RESULT_AVAILABLE, notification_source);

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_V, true, false, false, false));

  ASSERT_NO_FATAL_FAILURE(observer.Wait());
  FindNotificationDetails details;
  ASSERT_TRUE(observer.GetDetailsFor(notification_source.map_key(), &details));
  EXPECT_TRUE(details.number_of_matches() > 0);
}
