// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/webui_screen_locker.h"

#include "ash/shell.h"
#include "ash/wm/session_state_controller.h"
#include "ash/wm/session_state_observer.h"
#include "base/command_line.h"
#include "base/metrics/histogram.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/accessibility/accessibility_util.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/webui_login_display.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/x/x11_util.h"
#include "ui/gfx/screen.h"

namespace {

// URL which corresponds to the login WebUI.
const char kLoginURL[] = "chrome://oobe/login";

}  // namespace

namespace chromeos {

////////////////////////////////////////////////////////////////////////////////
// WebUIScreenLocker implementation.

WebUIScreenLocker::WebUIScreenLocker(ScreenLocker* screen_locker)
    : ScreenLockerDelegate(screen_locker),
      lock_ready_(false),
      webui_ready_(false) {
  set_should_emit_login_prompt_visible(false);
  if (ash::Shell::GetInstance())
    ash::Shell::GetInstance()->session_state_controller()->AddObserver(this);
}

void WebUIScreenLocker::LockScreen(bool unlock_on_input) {
  gfx::Rect bounds(ash::Shell::GetScreen()->GetPrimaryDisplay().bounds());

  lock_time_ = base::TimeTicks::Now();
  LockWindow* lock_window = LockWindow::Create();
  lock_window->set_observer(this);
  lock_window_ = lock_window->GetWidget();
  WebUILoginView::Init(lock_window_);
  lock_window_->SetContentsView(this);
  lock_window_->Show();
  OnWindowCreated();
  LoadURL(GURL(kLoginURL));
  lock_window->Grab();

  // User list consisting of a single logged-in user.
  UserList users(1, chromeos::UserManager::Get()->GetLoggedInUser());
  login_display_.reset(new WebUILoginDisplay(this));
  login_display_->set_background_bounds(bounds);
  login_display_->set_parent_window(GetNativeWindow());
  login_display_->Init(users, false, true, false);

  static_cast<OobeUI*>(GetWebUI()->GetController())->ShowSigninScreen(
      login_display_.get(), login_display_.get());

  registrar_.Add(this,
                 chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_LOCK_WEBUI_READY,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_LOCK_BACKGROUND_DISPLAYED,
                 content::NotificationService::AllSources());
}

void WebUIScreenLocker::ScreenLockReady() {
  UMA_HISTOGRAM_TIMES("LockScreen.LockReady",
                      base::TimeTicks::Now() - lock_time_);
  ScreenLockerDelegate::ScreenLockReady();
  SetInputEnabled(true);
}

void WebUIScreenLocker::OnAuthenticate() {
}

void WebUIScreenLocker::SetInputEnabled(bool enabled) {
  login_display_->SetUIEnabled(enabled);
}

void WebUIScreenLocker::ShowErrorMessage(
    int error_msg_id,
    HelpAppLauncher::HelpTopic help_topic_id) {
  login_display_->ShowError(error_msg_id,
                  0 /* login_attempts */,
                  help_topic_id);
}

void WebUIScreenLocker::AnimateAuthenticationSuccess() {
  GetWebUI()->CallJavascriptFunction("cr.ui.Oobe.animateAuthenticationSuccess");
}

void WebUIScreenLocker::ClearErrors() {
  GetWebUI()->CallJavascriptFunction("cr.ui.Oobe.clearErrors");
}

gfx::NativeWindow WebUIScreenLocker::GetNativeWindow() const {
  return lock_window_->GetNativeWindow();
}

content::WebUI* WebUIScreenLocker::GetAssociatedWebUI() {
  return GetWebUI();
}

WebUIScreenLocker::~WebUIScreenLocker() {
  if (ash::Shell::GetInstance())
    ash::Shell::GetInstance()->session_state_controller()->RemoveObserver(this);
  DCHECK(lock_window_);
  lock_window_->Close();
  // If LockScreen() was called, we need to clear the signin screen handler
  // delegate set in ShowSigninScreen so that it no longer points to us.
  if (login_display_.get()) {
    static_cast<OobeUI*>(GetWebUI()->GetController())->
        ResetSigninScreenHandlerDelegate();
  }
}

////////////////////////////////////////////////////////////////////////////////
// WebUIScreenLocker, content::NotificationObserver implementation:

void WebUIScreenLocker::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED: {
      const User& user = *content::Details<User>(details).ptr();
      login_display_->OnUserImageChanged(user);
      break;
    }
    case chrome::NOTIFICATION_LOCK_WEBUI_READY: {
      VLOG(1) << "WebUI ready; lock window is "
              << (lock_ready_ ? "too" : "not");
      webui_ready_ = true;
      if (lock_ready_)
        ScreenLockReady();
      break;
    }
    case chrome::NOTIFICATION_LOCK_BACKGROUND_DISPLAYED: {
      UMA_HISTOGRAM_TIMES("LockScreen.BackgroundReady",
                          base::TimeTicks::Now() - lock_time_);
      break;
    }
    default:
      WebUILoginView::Observe(type, source, details);
  }
}

////////////////////////////////////////////////////////////////////////////////
// WebUIScreenLocker, LoginDisplay::Delegate implementation:

void WebUIScreenLocker::CreateAccount() {
  NOTREACHED();
}

string16 WebUIScreenLocker::GetConnectedNetworkName() {
  return GetCurrentNetworkName(CrosLibrary::Get()->GetNetworkLibrary());
}

void WebUIScreenLocker::SetDisplayEmail(const std::string& email) {
  NOTREACHED();
}

void WebUIScreenLocker::CompleteLogin(const std::string& username,
                                      const std::string& password) {
  NOTREACHED();
}

void WebUIScreenLocker::Login(const std::string& username,
                              const std::string& password) {
  chromeos::ScreenLocker::default_screen_locker()->Authenticate(
      ASCIIToUTF16(password));
}

void WebUIScreenLocker::LoginAsRetailModeUser() {
  NOTREACHED();
}

void WebUIScreenLocker::LoginAsGuest() {
  NOTREACHED();
}

void WebUIScreenLocker::Signout() {
  chromeos::ScreenLocker::default_screen_locker()->Signout();
}

void WebUIScreenLocker::OnUserSelected(const std::string& username) {
}

void WebUIScreenLocker::OnStartEnterpriseEnrollment() {
  NOTREACHED();
}

void WebUIScreenLocker::OnStartDeviceReset() {
  NOTREACHED();
}

////////////////////////////////////////////////////////////////////////////////
// LockWindow::Observer implementation:

void WebUIScreenLocker::OnLockWindowReady() {
  VLOG(1) << "Lock window ready; WebUI is " << (webui_ready_ ? "too" : "not");
  lock_ready_ = true;
  if (webui_ready_)
    ScreenLockReady();
}

////////////////////////////////////////////////////////////////////////////////
// SessionStateObserver override.

void WebUIScreenLocker::OnSessionStateEvent(
    ash::SessionStateObserver::EventType event) {
  if (event == ash::SessionStateObserver::EVENT_LOCK_ANIMATION_FINISHED)
    GetWebUI()->CallJavascriptFunction("cr.ui.Oobe.animateOnceFullyDisplayed");
}


}  // namespace chromeos
