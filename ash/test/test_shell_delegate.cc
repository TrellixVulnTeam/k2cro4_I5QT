// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test/test_shell_delegate.h"

#include <algorithm>

#include "ash/caps_lock_delegate_stub.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/test/test_launcher_delegate.h"
#include "ash/wm/stacking_controller.h"
#include "ash/wm/window_util.h"
#include "content/public/test/test_browser_context.h"
#include "ui/aura/window.h"

namespace ash {
namespace test {

TestShellDelegate::TestShellDelegate()
    : locked_(false),
      session_started_(true),
      spoken_feedback_enabled_(false),
      user_logged_in_(true),
      can_lock_screen_(true),
      num_exit_requests_(0) {
}

TestShellDelegate::~TestShellDelegate() {
}

bool TestShellDelegate::IsUserLoggedIn() {
  return user_logged_in_;
}

bool TestShellDelegate::IsSessionStarted() {
  return session_started_;
}

bool TestShellDelegate::IsFirstRunAfterBoot() {
  return false;
}

bool TestShellDelegate::CanLockScreen() {
  return user_logged_in_ && can_lock_screen_;
}

void TestShellDelegate::LockScreen() {
  locked_ = true;
}

void TestShellDelegate::UnlockScreen() {
  locked_ = false;
}

bool TestShellDelegate::IsScreenLocked() const {
  return locked_;
}

void TestShellDelegate::Shutdown() {
}

void TestShellDelegate::Exit() {
  num_exit_requests_++;
}

void TestShellDelegate::NewTab() {
}

void TestShellDelegate::NewWindow(bool incognito) {
}

void TestShellDelegate::ToggleMaximized() {
  aura::Window* window = ash::wm::GetActiveWindow();
  if (window)
    ash::wm::ToggleMaximizedWindow(window);
}

void TestShellDelegate::OpenFileManager(bool as_dialog) {
}

void TestShellDelegate::OpenCrosh() {
}

void TestShellDelegate::OpenMobileSetup(const std::string& service_path) {
}

void TestShellDelegate::RestoreTab() {
}

bool TestShellDelegate::RotatePaneFocus(Shell::Direction direction) {
  return true;
}

void TestShellDelegate::ShowKeyboardOverlay() {
}

void TestShellDelegate::ShowTaskManager() {
}

content::BrowserContext* TestShellDelegate::GetCurrentBrowserContext() {
  current_browser_context_.reset(new content::TestBrowserContext());
  return current_browser_context_.get();
}

void TestShellDelegate::ToggleSpokenFeedback() {
  spoken_feedback_enabled_ = !spoken_feedback_enabled_;
}

bool TestShellDelegate::IsSpokenFeedbackEnabled() const {
  return spoken_feedback_enabled_;
}

app_list::AppListViewDelegate* TestShellDelegate::CreateAppListViewDelegate() {
  return NULL;
}

LauncherDelegate* TestShellDelegate::CreateLauncherDelegate(
    ash::LauncherModel* model) {
  return new TestLauncherDelegate(model);
}

SystemTrayDelegate* TestShellDelegate::CreateSystemTrayDelegate() {
  return NULL;
}

UserWallpaperDelegate* TestShellDelegate::CreateUserWallpaperDelegate() {
  return NULL;
}

CapsLockDelegate* TestShellDelegate::CreateCapsLockDelegate() {
  return new CapsLockDelegateStub;
}

aura::client::UserActionClient* TestShellDelegate::CreateUserActionClient() {
  return NULL;
}

void TestShellDelegate::OpenFeedbackPage() {
}

void TestShellDelegate::RecordUserMetricsAction(UserMetricsAction action) {
}

void TestShellDelegate::HandleMediaNextTrack() {
}

void TestShellDelegate::HandleMediaPlayPause() {
}

void TestShellDelegate::HandleMediaPrevTrack() {
}

string16 TestShellDelegate::GetTimeRemainingString(base::TimeDelta delta) {
  return string16();
}

void TestShellDelegate::SaveScreenMagnifierScale(double scale) {
}

ui::MenuModel* TestShellDelegate::CreateContextMenu(aura::RootWindow* root) {
  return NULL;
}

double TestShellDelegate::GetSavedScreenMagnifierScale() {
  return std::numeric_limits<double>::min();
}

aura::client::StackingClient* TestShellDelegate::CreateStackingClient() {
  return new StackingController;
}

void TestShellDelegate::SetSessionStarted(bool session_started) {
  session_started_ = session_started;
  if (session_started)
    user_logged_in_ = true;
}

void TestShellDelegate::SetUserLoggedIn(bool user_logged_in) {
  user_logged_in_ = user_logged_in;
  if (!user_logged_in)
    session_started_ = false;
}

void TestShellDelegate::SetCanLockScreen(bool can_lock_screen) {
  can_lock_screen_ = can_lock_screen;
}

}  // namespace test
}  // namespace ash
