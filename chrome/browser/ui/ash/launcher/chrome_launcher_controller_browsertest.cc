// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"

#include "ash/launcher/launcher.h"
#include "ash/launcher/launcher_model.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/automation/automation_util.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/extensions/platform_app_browsertest_util.h"
#include "chrome/browser/extensions/shell_window_registry.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/extensions/shell_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/window.h"

using extensions::Extension;
using content::WebContents;

class LauncherPlatformAppBrowserTest
    : public extensions::PlatformAppBrowserTest {
 protected:
  LauncherPlatformAppBrowserTest()
      : launcher_(NULL),
        controller_(NULL) {
  }

  virtual ~LauncherPlatformAppBrowserTest() {}

  virtual void RunTestOnMainThreadLoop() {
    launcher_ = ash::Launcher::ForPrimaryDisplay();
    controller_ = static_cast<ChromeLauncherController*>(launcher_->delegate());
    return extensions::PlatformAppBrowserTest::RunTestOnMainThreadLoop();
  }

  ash::LauncherID CreateAppShortcutLauncherItem(const std::string& name) {
    return controller_->CreateAppShortcutLauncherItem(
        name, controller_->model()->item_count());
  }

  ash::Launcher* launcher_;
  ChromeLauncherController* controller_;
};

class LauncherAppBrowserTest : public ExtensionBrowserTest {
 protected:
  LauncherAppBrowserTest()
      : launcher_(NULL),
        model_(NULL) {
  }

  virtual ~LauncherAppBrowserTest() {}

  virtual void RunTestOnMainThreadLoop() {
    launcher_ = ash::Launcher::ForPrimaryDisplay();
    model_ = launcher_->model();
    return ExtensionBrowserTest::RunTestOnMainThreadLoop();
  }

  const Extension* LoadAndLaunchExtension(
      const char* name,
      extension_misc::LaunchContainer container,
      WindowOpenDisposition disposition) {
    EXPECT_TRUE(LoadExtension(test_data_dir_.AppendASCII(name)));

    ExtensionService* service = browser()->profile()->GetExtensionService();
    const Extension* extension =
        service->GetExtensionById(last_loaded_extension_id_, false);
    EXPECT_TRUE(extension);

    application_launch::OpenApplication(application_launch::LaunchParams(
            browser()->profile(), extension, container, disposition));
    return extension;
  }

  ash::LauncherID CreateShortcut(const char* name) {
    ExtensionService* service = browser()->profile()->GetExtensionService();
    LoadExtension(test_data_dir_.AppendASCII(name));

    // First get app_id.
    const Extension* extension =
        service->GetExtensionById(last_loaded_extension_id_, false);
    const std::string app_id = extension->id();

    // Then create a shortcut.
    ChromeLauncherController* controller =
        static_cast<ChromeLauncherController*>(launcher_->delegate());
    int item_count = model_->item_count();
    ash::LauncherID shortcut_id = controller->CreateAppShortcutLauncherItem(
        app_id, item_count);
    controller->PersistPinnedState();
    EXPECT_EQ(++item_count, model_->item_count());
    ash::LauncherItem item = *model_->ItemByID(shortcut_id);
    EXPECT_EQ(ash::TYPE_APP_SHORTCUT, item.type);
    return item.id;
  }

  ash::Launcher* launcher_;
  ash::LauncherModel* model_;
};

// Test that we can launch a platform app and get a running item.
IN_PROC_BROWSER_TEST_F(LauncherPlatformAppBrowserTest, LaunchUnpinned) {
  ash::Launcher* launcher = ash::Launcher::ForPrimaryDisplay();
  int item_count = launcher->model()->item_count();
  const Extension* extension = LoadAndLaunchPlatformApp("launch");
  ShellWindow* window = CreateShellWindow(extension);
  ++item_count;
  ASSERT_EQ(item_count, launcher->model()->item_count());
  ash::LauncherItem item =
      launcher->model()->items()[launcher->model()->item_count() - 2];
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);
  CloseShellWindow(window);
  --item_count;
  EXPECT_EQ(item_count, launcher->model()->item_count());
}

// Test that we can launch a platform app that already has a shortcut.
IN_PROC_BROWSER_TEST_F(LauncherPlatformAppBrowserTest, LaunchPinned) {
  int item_count = launcher_->model()->item_count();

  // First get app_id.
  const Extension* extension = LoadAndLaunchPlatformApp("launch");
  const std::string app_id = extension->id();

  // Then create a shortcut.
  ash::LauncherID shortcut_id = CreateAppShortcutLauncherItem(app_id);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item = *launcher_->model()->ItemByID(shortcut_id);
  EXPECT_EQ(ash::TYPE_APP_SHORTCUT, item.type);
  EXPECT_EQ(ash::STATUS_CLOSED, item.status);

  // Open a window. Confirm the item is now running.
  ShellWindow* window = CreateShellWindow(extension);
  ash::wm::ActivateWindow(window->GetNativeWindow());
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  item = *launcher_->model()->ItemByID(shortcut_id);
  EXPECT_EQ(ash::TYPE_APP_SHORTCUT, item.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);

  // Then close it, make sure there's still an item.
  CloseShellWindow(window);
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  item = *launcher_->model()->ItemByID(shortcut_id);
  EXPECT_EQ(ash::TYPE_APP_SHORTCUT, item.type);
  EXPECT_EQ(ash::STATUS_CLOSED, item.status);
}

IN_PROC_BROWSER_TEST_F(LauncherPlatformAppBrowserTest, PinRunning) {
  // Run.
  int item_count = launcher_->model()->item_count();
  const Extension* extension = LoadAndLaunchPlatformApp("launch");
  ShellWindow* window = CreateShellWindow(extension);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item =
      launcher_->model()->items()[launcher_->model()->item_count() - 2];
  ash::LauncherID id = item.id;
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);

  // Create a shortcut. The app item should be after it.
  ash::LauncherID foo_id = CreateAppShortcutLauncherItem("foo");
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  EXPECT_LT(launcher_->model()->ItemIndexByID(foo_id),
            launcher_->model()->ItemIndexByID(id));

  // Pin the app. The item should remain.
  controller_->Pin(id);
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  item = *launcher_->model()->ItemByID(id);
  EXPECT_EQ(ash::TYPE_APP_SHORTCUT, item.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);

  // New shortcuts should come after the item.
  ash::LauncherID bar_id = CreateAppShortcutLauncherItem("bar");
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  EXPECT_LT(launcher_->model()->ItemIndexByID(id),
            launcher_->model()->ItemIndexByID(bar_id));

  // Then close it, make sure the item remains.
  CloseShellWindow(window);
  ASSERT_EQ(item_count, launcher_->model()->item_count());
}

IN_PROC_BROWSER_TEST_F(LauncherPlatformAppBrowserTest, UnpinRunning) {
  int item_count = launcher_->model()->item_count();

  // First get app_id.
  const Extension* extension = LoadAndLaunchPlatformApp("launch");
  const std::string app_id = extension->id();

  // Then create a shortcut.
  ash::LauncherID shortcut_id = CreateAppShortcutLauncherItem(app_id);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item = *launcher_->model()->ItemByID(shortcut_id);
  EXPECT_EQ(ash::TYPE_APP_SHORTCUT, item.type);
  EXPECT_EQ(ash::STATUS_CLOSED, item.status);

  // Create a second shortcut. This will be needed to force the first one to
  // move once it gets unpinned.
  ash::LauncherID foo_id = CreateAppShortcutLauncherItem("foo");
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  EXPECT_LT(launcher_->model()->ItemIndexByID(shortcut_id),
            launcher_->model()->ItemIndexByID(foo_id));

  // Open a window. Confirm the item is now running.
  ShellWindow* window = CreateShellWindow(extension);
  ash::wm::ActivateWindow(window->GetNativeWindow());
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  item = *launcher_->model()->ItemByID(shortcut_id);
  EXPECT_EQ(ash::TYPE_APP_SHORTCUT, item.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);

  // Unpin the app. The item should remain.
  controller_->Unpin(shortcut_id);
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  item = *launcher_->model()->ItemByID(shortcut_id);
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);
  // The item should have moved after the other shortcuts.
  EXPECT_GT(launcher_->model()->ItemIndexByID(shortcut_id),
            launcher_->model()->ItemIndexByID(foo_id));

  // Then close it, make sure the item's gone.
  CloseShellWindow(window);
  --item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
}

// Test that we can launch a platform app with more than one window.
IN_PROC_BROWSER_TEST_F(LauncherPlatformAppBrowserTest, MultipleWindows) {
  int item_count = launcher_->model()->item_count();

  // First run app.
  const Extension* extension = LoadAndLaunchPlatformApp("launch");
  ShellWindow* window1 = CreateShellWindow(extension);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item =
      launcher_->model()->items()[launcher_->model()->item_count() - 2];
  ash::LauncherID item_id = item.id;
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);

  // Add second window.
  ShellWindow* window2 = CreateShellWindow(extension);
  // Confirm item stays.
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  item = *launcher_->model()->ItemByID(item_id);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);

  // Close second window.
  CloseShellWindow(window2);
  // Confirm item stays.
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  item = *launcher_->model()->ItemByID(item_id);
  EXPECT_EQ(ash::STATUS_ACTIVE, item.status);

  // Close first window.
  CloseShellWindow(window1);
  // Confirm item is removed.
  --item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
}

// Times out on ChromeOS: http://crbug.com/159394
#if defined(OS_CHROMEOS)
#define MAYBE_MultipleApps DISABLED_MultipleApps
#else
#define MAYBE_MultipleApps MultipleApps
#endif
IN_PROC_BROWSER_TEST_F(LauncherPlatformAppBrowserTest, MAYBE_MultipleApps) {
  int item_count = launcher_->model()->item_count();

  // First run app.
  const Extension* extension1 = LoadAndLaunchPlatformApp("launch");
  ShellWindow* window1 = CreateShellWindow(extension1);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item1 =
      launcher_->model()->items()[launcher_->model()->item_count() - 2];
  ash::LauncherID item_id1 = item1.id;
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item1.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item1.status);

  // Then run second app.
  const Extension* extension2 = LoadAndLaunchPlatformApp("launch_2");
  ShellWindow* window2 = CreateShellWindow(extension2);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item2 =
      launcher_->model()->items()[launcher_->model()->item_count() - 2];
  ash::LauncherID item_id2 = item2.id;
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item2.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item2.status);

  EXPECT_NE(item_id1, item_id2);
  EXPECT_EQ(ash::STATUS_RUNNING,
            launcher_->model()->ItemByID(item_id1)->status);

  // Close second app.
  CloseShellWindow(window2);
  --item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  // First app should be active again.
  EXPECT_EQ(ash::STATUS_ACTIVE,
            launcher_->model()->ItemByID(item_id1)->status);

  // Close first app.
  CloseShellWindow(window1);
  --item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());

}

// Confirm that app windows can be reactivated by clicking their icons and that
// the correct activation order is maintained.
// Times out on ChromeOS: http://crbug.com/159394
#if defined(OS_CHROMEOS)
#define MAYBE_WindowActivation DISABLED_WindowActivation
#else
#define MAYBE_WindowActivation WindowActivation
#endif
IN_PROC_BROWSER_TEST_F(LauncherPlatformAppBrowserTest, MAYBE_WindowActivation) {
  int item_count = launcher_->model()->item_count();

  // First run app.
  const Extension* extension1 = LoadAndLaunchPlatformApp("launch");
  ShellWindow* window1 = CreateShellWindow(extension1);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item1 =
      launcher_->model()->items()[launcher_->model()->item_count() - 2];
  ash::LauncherID item_id1 = item1.id;
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item1.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item1.status);

  // Then run second app.
  const Extension* extension2 = LoadAndLaunchPlatformApp("launch_2");
  ShellWindow* window2 = CreateShellWindow(extension2);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item2 =
      launcher_->model()->items()[launcher_->model()->item_count() - 2];
  ash::LauncherID item_id2 = item2.id;
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item2.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item2.status);

  EXPECT_NE(item_id1, item_id2);
  EXPECT_EQ(ash::STATUS_RUNNING,
            launcher_->model()->ItemByID(item_id1)->status);

  // Activate first one.
  launcher_->ActivateLauncherItem(launcher_->model()->ItemIndexByID(item_id1));
  EXPECT_EQ(ash::STATUS_ACTIVE, launcher_->model()->ItemByID(item_id1)->status);
  EXPECT_EQ(ash::STATUS_RUNNING,
            launcher_->model()->ItemByID(item_id2)->status);
  EXPECT_TRUE(ash::wm::IsActiveWindow(window1->GetNativeWindow()));
  EXPECT_FALSE(ash::wm::IsActiveWindow(window2->GetNativeWindow()));

  // Activate second one.
  launcher_->ActivateLauncherItem(launcher_->model()->ItemIndexByID(item_id2));
  EXPECT_EQ(ash::STATUS_RUNNING,
            launcher_->model()->ItemByID(item_id1)->status);
  EXPECT_EQ(ash::STATUS_ACTIVE, launcher_->model()->ItemByID(item_id2)->status);
  EXPECT_FALSE(ash::wm::IsActiveWindow(window1->GetNativeWindow()));
  EXPECT_TRUE(ash::wm::IsActiveWindow(window2->GetNativeWindow()));

  // Add window for app1. This will activate it.
  ShellWindow* window1b = CreateShellWindow(extension1);
  ash::wm::ActivateWindow(window1b->GetNativeWindow());
  EXPECT_FALSE(ash::wm::IsActiveWindow(window1->GetNativeWindow()));
  EXPECT_FALSE(ash::wm::IsActiveWindow(window2->GetNativeWindow()));
  EXPECT_TRUE(ash::wm::IsActiveWindow(window1b->GetNativeWindow()));

  // Activate launcher item for app1, this will cycle the active window.
  launcher_->ActivateLauncherItem(launcher_->model()->ItemIndexByID(item_id1));
  EXPECT_FALSE(ash::wm::IsActiveWindow(window1b->GetNativeWindow()));
  EXPECT_TRUE(ash::wm::IsActiveWindow(window1->GetNativeWindow()));
  launcher_->ActivateLauncherItem(launcher_->model()->ItemIndexByID(item_id1));
  EXPECT_TRUE(ash::wm::IsActiveWindow(window1b->GetNativeWindow()));
  EXPECT_FALSE(ash::wm::IsActiveWindow(window1->GetNativeWindow()));

  // Activate the second app again
  launcher_->ActivateLauncherItem(launcher_->model()->ItemIndexByID(item_id2));
  EXPECT_FALSE(ash::wm::IsActiveWindow(window1->GetNativeWindow()));
  EXPECT_TRUE(ash::wm::IsActiveWindow(window2->GetNativeWindow()));
  EXPECT_FALSE(ash::wm::IsActiveWindow(window1b->GetNativeWindow()));

  // Activate the first app again
  launcher_->ActivateLauncherItem(launcher_->model()->ItemIndexByID(item_id1));
  EXPECT_FALSE(ash::wm::IsActiveWindow(window1->GetNativeWindow()));
  EXPECT_FALSE(ash::wm::IsActiveWindow(window2->GetNativeWindow()));
  EXPECT_TRUE(ash::wm::IsActiveWindow(window1b->GetNativeWindow()));

  // Close second app.
  CloseShellWindow(window2);
  --item_count;
  EXPECT_EQ(item_count, launcher_->model()->item_count());
  // First app should be active again.
  EXPECT_EQ(ash::STATUS_ACTIVE, launcher_->model()->ItemByID(item_id1)->status);

  // Close first app.
  CloseShellWindow(window1b);
  CloseShellWindow(window1);
  --item_count;
  EXPECT_EQ(item_count, launcher_->model()->item_count());
}

IN_PROC_BROWSER_TEST_F(LauncherPlatformAppBrowserTest, BrowserActivation) {
  int item_count = launcher_->model()->item_count();

  // First run app.
  const Extension* extension1 = LoadAndLaunchPlatformApp("launch");
  CreateShellWindow(extension1);
  ++item_count;
  ASSERT_EQ(item_count, launcher_->model()->item_count());
  ash::LauncherItem item1 =
      launcher_->model()->items()[launcher_->model()->item_count() - 2];
  ash::LauncherID item_id1 = item1.id;
  EXPECT_EQ(ash::TYPE_PLATFORM_APP, item1.type);
  EXPECT_EQ(ash::STATUS_ACTIVE, item1.status);

  ash::wm::ActivateWindow(browser()->window()->GetNativeWindow());
  EXPECT_EQ(ash::STATUS_RUNNING,
            launcher_->model()->ItemByID(item_id1)->status);
}

// Test that we can launch an app with a shortcut.
IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, LaunchPinned) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  ash::LauncherID shortcut_id = CreateShortcut("app1");
  EXPECT_EQ(ash::STATUS_CLOSED, (*model_->ItemByID(shortcut_id)).status);
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(++tab_count, tab_strip->count());
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut_id)).status);
  TabContents* tab = tab_strip->GetActiveTabContents();
  content::WindowedNotificationObserver close_observer(
      content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
      content::Source<WebContents>(tab->web_contents()));
  browser()->tab_strip_model()->CloseSelectedTabs();
  close_observer.Wait();
  EXPECT_EQ(--tab_count, tab_strip->count());
  EXPECT_EQ(ash::STATUS_CLOSED, (*model_->ItemByID(shortcut_id)).status);
}

// Launch the app first and then create the shortcut.
IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, LaunchUnpinned) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  LoadAndLaunchExtension("app1", extension_misc::LAUNCH_TAB,
                         NEW_FOREGROUND_TAB);
  EXPECT_EQ(++tab_count, tab_strip->count());
  ash::LauncherID shortcut_id = CreateShortcut("app1");
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut_id)).status);
  TabContents* tab = tab_strip->GetActiveTabContents();
  content::WindowedNotificationObserver close_observer(
      content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
      content::Source<WebContents>(tab->web_contents()));
  browser()->tab_strip_model()->CloseSelectedTabs();
  close_observer.Wait();
  EXPECT_EQ(--tab_count, tab_strip->count());
  EXPECT_EQ(ash::STATUS_CLOSED, (*model_->ItemByID(shortcut_id)).status);
}

// Launches an app in the background and then tries to open it. This is test for
// a crash we had.
IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, LaunchInBackground) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  LoadAndLaunchExtension("app1", extension_misc::LAUNCH_TAB,
                         NEW_BACKGROUND_TAB);
  EXPECT_EQ(++tab_count, tab_strip->count());
  ChromeLauncherController::instance()->LaunchApp(last_loaded_extension_id_, 0);
}

// Confirm that clicking a icon for an app running in one of 2 maxmized windows
// activates the right window.
IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, LaunchMaximized) {
  aura::Window* window1 = browser()->window()->GetNativeWindow();
  ash::wm::MaximizeWindow(window1);
  content::WindowedNotificationObserver open_observer(
      chrome::NOTIFICATION_BROWSER_WINDOW_READY,
      content::NotificationService::AllSources());
  chrome::NewEmptyWindow(browser()->profile());
  open_observer.Wait();
  Browser* browser2 = content::Source<Browser>(open_observer.source()).ptr();
  aura::Window* window2 = browser2->window()->GetNativeWindow();
  TabStripModel* tab_strip = browser2->tab_strip_model();
  int tab_count = tab_strip->count();
  ash::wm::MaximizeWindow(window2);

  ash::LauncherID shortcut_id = CreateShortcut("app1");
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(++tab_count, tab_strip->count());
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut_id)).status);

  window1->Show();
  ash::wm::ActivateWindow(window1);
  EXPECT_EQ(ash::STATUS_RUNNING, (*model_->ItemByID(shortcut_id)).status);

  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut_id)).status);
}

// Activating the same app multiple times should launch only a single copy.
IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, ActivateApp) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  const Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("app1"));

  ChromeLauncherController::instance()->ActivateApp(extension->id(), 0);
  EXPECT_EQ(++tab_count, tab_strip->count());
  ChromeLauncherController::instance()->ActivateApp(extension->id(), 0);
  EXPECT_EQ(tab_count, tab_strip->count());
}

// Launching the same app multiple times should launch a copy for each call.
IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, LaunchApp) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  const Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("app1"));

  ChromeLauncherController::instance()->LaunchApp(extension->id(), 0);
  EXPECT_EQ(++tab_count, tab_strip->count());
  ChromeLauncherController::instance()->LaunchApp(extension->id(), 0);
  EXPECT_EQ(++tab_count, tab_strip->count());
}

// Launch 2 apps and toggle which is active.
IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, MultipleApps) {
  int item_count = model_->item_count();
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  ash::LauncherID shortcut1 = CreateShortcut("app1");
  EXPECT_EQ(++item_count, model_->item_count());
  ash::LauncherID shortcut2 = CreateShortcut("app2");
  EXPECT_EQ(++item_count, model_->item_count());

  // Launch first app.
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut1));
  EXPECT_EQ(++tab_count, tab_strip->count());
  TabContents* tab1 = tab_strip->GetActiveTabContents();
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut1)).status);

  // Launch second app.
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut2));
  EXPECT_EQ(++tab_count, tab_strip->count());
  TabContents* tab2 = tab_strip->GetActiveTabContents();
  ASSERT_NE(tab1, tab2);
  EXPECT_EQ(ash::STATUS_RUNNING, (*model_->ItemByID(shortcut1)).status);
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut2)).status);

  // Reactivate first app.
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut1));
  EXPECT_EQ(tab_count, tab_strip->count());
  EXPECT_EQ(tab_strip->GetActiveTabContents(), tab1);
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut1)).status);
  EXPECT_EQ(ash::STATUS_RUNNING, (*model_->ItemByID(shortcut2)).status);

  // Open second tab for second app. This should activate it.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("http://www.example.com/path3/foo.html"),
      NEW_FOREGROUND_TAB,
      0);
  EXPECT_EQ(++tab_count, tab_strip->count());
  TabContents* tab3 = tab_strip->GetActiveTabContents();
  EXPECT_EQ(ash::STATUS_RUNNING, (*model_->ItemByID(shortcut1)).status);
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut2)).status);

  // Reactivate first app.
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut1));
  EXPECT_EQ(tab_count, tab_strip->count());
  EXPECT_EQ(tab_strip->GetActiveTabContents(), tab1);
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut1)).status);
  EXPECT_EQ(ash::STATUS_RUNNING, (*model_->ItemByID(shortcut2)).status);

  // And second again. This time the second tab should become active.
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut2));
  EXPECT_EQ(tab_count, tab_strip->count());
  EXPECT_EQ(tab_strip->GetActiveTabContents(), tab3);
  EXPECT_EQ(ash::STATUS_RUNNING, (*model_->ItemByID(shortcut1)).status);
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut2)).status);
}

// Confirm that a page can be navigated from and to while maintaining the
// correct running state.
IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, Navigation) {
  ash::LauncherID shortcut_id = CreateShortcut("app1");
  EXPECT_EQ(ash::STATUS_CLOSED, (*model_->ItemByID(shortcut_id)).status);
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut_id)).status);

  // Navigate away.
  ui_test_utils::NavigateToURL(
      browser(), GURL("http://www.example.com/path0/bar.html"));
  EXPECT_EQ(ash::STATUS_CLOSED, (*model_->ItemByID(shortcut_id)).status);

  // Navigate back.
  ui_test_utils::NavigateToURL(
      browser(), GURL("http://www.example.com/path1/foo.html"));
  EXPECT_EQ(ash::STATUS_ACTIVE, (*model_->ItemByID(shortcut_id)).status);
}

IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, MultipleOwnedTabs) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  ash::LauncherID shortcut_id = CreateShortcut("app1");
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(++tab_count, tab_strip->count());
  EXPECT_EQ(ash::STATUS_ACTIVE, model_->ItemByID(shortcut_id)->status);

  // Create new tab owned by app.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("http://www.example.com/path2/bar.html"),
      NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  EXPECT_EQ(++tab_count, tab_strip->count());
  // Confirm app is still active.
  EXPECT_EQ(ash::STATUS_ACTIVE, model_->ItemByID(shortcut_id)->status);
  TabContents* second_tab = tab_strip->GetActiveTabContents();

  // Create new tab not owned by app.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("http://www.example.com/path3/foo.html"),
      NEW_FOREGROUND_TAB,
      0);
  EXPECT_EQ(++tab_count, tab_strip->count());
  // No longer active.
  EXPECT_EQ(ash::STATUS_RUNNING, model_->ItemByID(shortcut_id)->status);

  // Activating app makes second tab active again.
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(ash::STATUS_ACTIVE, model_->ItemByID(shortcut_id)->status);
  EXPECT_EQ(tab_strip->GetActiveTabContents(), second_tab);
}

IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, RefocusFilter) {
  ChromeLauncherController* controller =
      static_cast<ChromeLauncherController*>(launcher_->delegate());
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  ash::LauncherID shortcut_id = CreateShortcut("app1");
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(++tab_count, tab_strip->count());
  EXPECT_EQ(ash::STATUS_ACTIVE, model_->ItemByID(shortcut_id)->status);
  TabContents* first_tab = tab_strip->GetActiveTabContents();

  controller->SetRefocusURLPattern(
      shortcut_id, GURL("http://www.example.com/path1/*"));
  // Create new tab owned by app.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("http://www.example.com/path2/bar.html"),
      NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  EXPECT_EQ(++tab_count, tab_strip->count());
  // Confirm app is still active.
  EXPECT_EQ(ash::STATUS_ACTIVE, model_->ItemByID(shortcut_id)->status);

  // Create new tab not owned by app.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("http://www.example.com/path3/foo.html"),
      NEW_FOREGROUND_TAB,
      0);
  EXPECT_EQ(++tab_count, tab_strip->count());
  // No longer active.
  EXPECT_EQ(ash::STATUS_RUNNING, model_->ItemByID(shortcut_id)->status);

  // Activating app makes first tab active again, because second tab isn't
  // in its refocus url path.
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(ash::STATUS_ACTIVE, model_->ItemByID(shortcut_id)->status);
  EXPECT_EQ(tab_strip->GetActiveTabContents(), first_tab);
}

IN_PROC_BROWSER_TEST_F(LauncherAppBrowserTest, RefocusFilterLaunch) {
  ChromeLauncherController* controller =
      static_cast<ChromeLauncherController*>(launcher_->delegate());
  TabStripModel* tab_strip = browser()->tab_strip_model();
  int tab_count = tab_strip->count();
  ash::LauncherID shortcut_id = CreateShortcut("app1");
  controller->SetRefocusURLPattern(
      shortcut_id, GURL("http://www.example.com/path1/*"));

  // Create new tab owned by app.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("http://www.example.com/path2/bar.html"),
      NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  EXPECT_EQ(++tab_count, tab_strip->count());
  TabContents* first_tab = tab_strip->GetActiveTabContents();
  // Confirm app is active.
  EXPECT_EQ(ash::STATUS_ACTIVE, model_->ItemByID(shortcut_id)->status);

  // Activating app should launch new tab, because second tab isn't
  // in its refocus url path.
  launcher_->ActivateLauncherItem(model_->ItemIndexByID(shortcut_id));
  EXPECT_EQ(++tab_count, tab_strip->count());
  TabContents* second_tab = tab_strip->GetActiveTabContents();
  EXPECT_EQ(ash::STATUS_ACTIVE, model_->ItemByID(shortcut_id)->status);
  EXPECT_NE(first_tab, second_tab);
  EXPECT_EQ(tab_strip->GetActiveTabContents(), second_tab);
}
