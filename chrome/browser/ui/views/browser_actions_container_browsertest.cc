// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/browser_actions_container.h"

#include "chrome/browser/extensions/browser_action_test_util.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/extension_resource.h"
#include "content/public/test/test_utils.h"

using extensions::Extension;

class BrowserActionsContainerTest : public ExtensionBrowserTest {
 public:
  BrowserActionsContainerTest() {
  }
  virtual ~BrowserActionsContainerTest() {}

  virtual void SetUpOnMainThread() OVERRIDE {
    browser_actions_bar_.reset(new BrowserActionTestUtil(browser()));
  }

  BrowserActionTestUtil* browser_actions_bar() {
    return browser_actions_bar_.get();
  }

  // Make sure extension with index |extension_index| has an icon.
  void EnsureExtensionHasIcon(int extension_index) {
    if (!browser_actions_bar_->HasIcon(extension_index)) {
      // The icon is loaded asynchronously and a notification is then sent to
      // observers. So we wait on it.
      ExtensionAction* browser_action =
          browser_actions_bar_->GetExtensionAction(extension_index);

      content::WindowedNotificationObserver observer(
          chrome::NOTIFICATION_EXTENSION_BROWSER_ACTION_UPDATED,
          content::Source<ExtensionAction>(browser_action));
      observer.Wait();
    }
    EXPECT_TRUE(browser_actions_bar()->HasIcon(extension_index));
  }

 private:
  scoped_ptr<BrowserActionTestUtil> browser_actions_bar_;
};

// Test the basic functionality.
// http://crbug.com/120770
#if defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(BrowserActionsContainerTest, DISABLED_Basic) {
#else
IN_PROC_BROWSER_TEST_F(BrowserActionsContainerTest, Basic) {
#endif
  BrowserActionsContainer::disable_animations_during_testing_ = true;

  // Load an extension with no browser action.
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("none")));
  // This extension should not be in the model (has no browser action).
  EXPECT_EQ(0, browser_actions_bar()->NumberOfBrowserActions());

  // Load an extension with a browser action.
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("basics")));
  EXPECT_EQ(1, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(0);

  // Unload the extension.
  std::string id = browser_actions_bar()->GetExtensionId(0);
  UnloadExtension(id);
  EXPECT_EQ(0, browser_actions_bar()->NumberOfBrowserActions());
}

IN_PROC_BROWSER_TEST_F(BrowserActionsContainerTest, Visibility) {
  BrowserActionsContainer::disable_animations_during_testing_ = true;

  base::TimeTicks start_time = base::TimeTicks::Now();

  // Load extension A (contains browser action).
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("basics")));
  EXPECT_EQ(1, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(0);
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  std::string idA = browser_actions_bar()->GetExtensionId(0);

  LOG(INFO) << "Load extension A done  : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Load extension B (contains browser action).
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("add_popup")));
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(0);
  EXPECT_EQ(2, browser_actions_bar()->VisibleBrowserActions());
  std::string idB = browser_actions_bar()->GetExtensionId(1);

  LOG(INFO) << "Load extension B done  : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  EXPECT_NE(idA, idB);

  // Load extension C (contains browser action).
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("remove_popup")));
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(2);
  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());
  std::string idC = browser_actions_bar()->GetExtensionId(2);

  LOG(INFO) << "Load extension C done  : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Change container to show only one action, rest in overflow: A, [B, C].
  browser_actions_bar()->SetIconVisibilityCount(1);
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());

  LOG(INFO) << "Icon visibility count 1: "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Disable extension A (should disappear). State becomes: B [C].
  DisableExtension(idA);
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idB, browser_actions_bar()->GetExtensionId(0));

  LOG(INFO) << "Disable extension A    : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Enable A again. A should get its spot in the same location and the bar
  // should not grow (chevron is showing). For details: http://crbug.com/35349.
  // State becomes: A, [B, C].
  EnableExtension(idA);
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idA, browser_actions_bar()->GetExtensionId(0));

  LOG(INFO) << "Enable extension A     : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Disable C (in overflow). State becomes: A, [B].
  DisableExtension(idC);
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idA, browser_actions_bar()->GetExtensionId(0));

  LOG(INFO) << "Disable extension C    : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Enable C again. State becomes: A, [B, C].
  EnableExtension(idC);
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idA, browser_actions_bar()->GetExtensionId(0));

  LOG(INFO) << "Enable extension C     : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Now we have 3 extensions. Make sure they are all visible. State: A, B, C.
  browser_actions_bar()->SetIconVisibilityCount(3);
  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());

  LOG(INFO) << "Checkpoint             : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Disable extension A (should disappear). State becomes: B, C.
  DisableExtension(idA);
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(2, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idB, browser_actions_bar()->GetExtensionId(0));

  LOG(INFO) << "Disable extension A    : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Disable extension B (should disappear). State becomes: C.
  DisableExtension(idB);
  EXPECT_EQ(1, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idC, browser_actions_bar()->GetExtensionId(0));

  LOG(INFO) << "Disable extension B    : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Enable B (makes B and C showing now). State becomes: B, C.
  EnableExtension(idB);
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(2, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idB, browser_actions_bar()->GetExtensionId(0));

  LOG(INFO) << "Enable extension B     : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;

  // Enable A (makes A, B and C showing now). State becomes: B, C, A.
  EnableExtension(idA);
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idA, browser_actions_bar()->GetExtensionId(2));

  LOG(INFO) << "Test complete          : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;
}

IN_PROC_BROWSER_TEST_F(BrowserActionsContainerTest, ForceHide) {
  BrowserActionsContainer::disable_animations_during_testing_ = true;

  // Load extension A (contains browser action).
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("basics")));
  EXPECT_EQ(1, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(0);
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  std::string idA = browser_actions_bar()->GetExtensionId(0);

  // Force hide this browser action.
  ExtensionService* service = browser()->profile()->GetExtensionService();
  service->extension_prefs()->SetBrowserActionVisibility(
      service->GetExtensionById(idA, false), false);
  EXPECT_EQ(0, browser_actions_bar()->VisibleBrowserActions());
}

IN_PROC_BROWSER_TEST_F(BrowserActionsContainerTest, TestCrash57536) {
  LOG(INFO) << "Test starting\n" << std::flush;

  LOG(INFO) << "Loading extension\n" << std::flush;

  // Load extension A (contains browser action).
  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("api_test")
      .AppendASCII("browser_action")
      .AppendASCII("crash_57536"));
  ASSERT_TRUE(extension);

  LOG(INFO) << "Creating bitmap\n" << std::flush;

  // Create and cache and empty bitmap.
  SkBitmap bitmap;
  bitmap.setConfig(SkBitmap::kARGB_8888_Config,
                    Extension::kBrowserActionIconMaxSize,
                    Extension::kBrowserActionIconMaxSize);
  bitmap.allocPixels();

  LOG(INFO) << "Set as cached image\n" << std::flush;

  gfx::Size size(Extension::kBrowserActionIconMaxSize,
                 Extension::kBrowserActionIconMaxSize);
  const ExtensionIconSet* default_icon =
      extensions::ExtensionActionManager::Get(browser()->profile())->
      GetBrowserAction(*extension)->default_icon();
  const std::string path =
      default_icon->Get(extension_misc::EXTENSION_ICON_ACTION,
                        ExtensionIconSet::MATCH_EXACTLY);

  extension->SetCachedImage(extension->GetResource(path), bitmap, size);

  LOG(INFO) << "Disabling extension\n" << std::flush;
  DisableExtension(extension->id());
  LOG(INFO) << "Enabling extension\n" << std::flush;
  EnableExtension(extension->id());
  LOG(INFO) << "Test ending\n" << std::flush;
}
