// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_H_
#define CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_H_

#include <string>

class Profile;

// Interface to allow the view delegate to call out to whatever is controlling
// the app list. This will have different implementations for different
// platforms.
class AppListControllerDelegate {
 public:
  virtual ~AppListControllerDelegate();

  // Close the view.
  virtual void CloseView() = 0;

  // Handle the view being closed.
  virtual void ViewClosing();

  // Handle the view being activated or deactivated.
  virtual void ViewActivationChanged(bool active);

  // Control of pinning apps.
  virtual bool IsAppPinned(const std::string& extension_id);
  virtual void PinApp(const std::string& extension_id);
  virtual void UnpinApp(const std::string& extension_id);
  virtual bool CanPin() = 0;

  // Whether the controller supports showing the Create Shortcuts dialog.
  virtual bool CanShowCreateShortcutsDialog() = 0;
  virtual void ShowCreateShortcutsDialog(Profile* profile,
                                         const std::string& extension_id);

  // Handle the "create window" context menu items of Chrome App.
  // |incognito| is true to create an incognito window.
  virtual void CreateNewWindow(bool incognito);

  // Show the app's most recent window, or launch it if it is not running.
  virtual void ActivateApp(Profile* profile,
                           const std::string& extension_id,
                           int event_flags) = 0;

  // Launch the app.
  virtual void LaunchApp(Profile* profile,
                         const std::string& extension_id,
                         int event_flags) = 0;
};

namespace app_list_controller {

// Show the app list.
void ShowAppList();

// Check that the presence of the app list shortcut matches the flag
// kShowAppListShortcut. This will either create or delete a shortcut
// file in the user data directory.
// TODO(benwells): Remove this and the flag once the app list installation
// is implemented.
void CheckAppListTaskbarShortcut();

}  // namespace app_list_controller

#endif  // CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_H_
