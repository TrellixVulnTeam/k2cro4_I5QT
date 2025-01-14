// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/widget/desktop_aura/desktop_activation_client.h"

#include "base/auto_reset.h"
#include "base/compiler_specific.h"
#include "ui/aura/client/activation_delegate.h"
#include "ui/aura/client/activation_change_observer.h"
#include "ui/aura/focus_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"

namespace views {

DesktopActivationClient::DesktopActivationClient(aura::RootWindow* root_window)
    : root_window_(root_window),
      current_active_(NULL),
      updating_activation_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(observer_manager_(this)) {
  root_window->GetFocusManager()->AddObserver(this);
  aura::client::SetActivationClient(root_window_, this);
}

DesktopActivationClient::~DesktopActivationClient() {
  root_window_->GetFocusManager()->RemoveObserver(this);
  aura::client::SetActivationClient(root_window_, NULL);
}

void DesktopActivationClient::AddObserver(
    aura::client::ActivationChangeObserver* observer) {
  observers_.AddObserver(observer);
}

void DesktopActivationClient::RemoveObserver(
    aura::client::ActivationChangeObserver* observer) {
  observers_.RemoveObserver(observer);
}

void DesktopActivationClient::ActivateWindow(aura::Window* window) {
  // Prevent recursion when called from focus.
  if (updating_activation_)
    return;

  base::AutoReset<bool> in_activate_window(&updating_activation_, true);
  // Nothing may actually have changed.
  if (current_active_ == window)
    return;
  // The stacking client may impose rules on what window configurations can be
  // activated or deactivated.
  if (window && !CanActivateWindow(window))
    return;
  // Switch internal focus before we change the activation. Will probably cause
  // recursion.
  if (window &&
      !window->Contains(window->GetFocusManager()->GetFocusedWindow())) {
    window->GetFocusManager()->SetFocusedWindow(window, NULL);
  }

  aura::Window* old_active = current_active_;
  current_active_ = window;
  if (window && !observer_manager_.IsObserving(window))
    observer_manager_.Add(window);

  FOR_EACH_OBSERVER(aura::client::ActivationChangeObserver,
                    observers_,
                    OnWindowActivated(window, old_active));

  // Invoke OnLostActive after we've changed the active window. That way if the
  // delegate queries for active state it doesn't think the window is still
  // active.
  if (old_active && aura::client::GetActivationDelegate(old_active))
    aura::client::GetActivationDelegate(old_active)->OnLostActive();

  // Send an activation event to the new window
  if (window && aura::client::GetActivationDelegate(window))
    aura::client::GetActivationDelegate(window)->OnActivated();
}

void DesktopActivationClient::DeactivateWindow(aura::Window* window) {
  if (window == current_active_)
    current_active_ = NULL;
}

aura::Window* DesktopActivationClient::GetActiveWindow() {
  return current_active_;
}

bool DesktopActivationClient::OnWillFocusWindow(aura::Window* window,
                                                const ui::Event* event) {
  return CanActivateWindow(GetActivatableWindow(window));
}

void DesktopActivationClient::OnWindowDestroying(aura::Window* window) {
  if (current_active_ == window) {
    current_active_ = NULL;
    FOR_EACH_OBSERVER(aura::client::ActivationChangeObserver,
                      observers_,
                      OnWindowActivated(NULL, window));

    // ash::ActivationController will also activate the next window here; we
    // don't do this because that's the desktop environment's job.
  }
  observer_manager_.Remove(window);
}

void DesktopActivationClient::OnWindowFocused(aura::Window* window) {
  ActivateWindow(GetActivatableWindow(window));
}

bool DesktopActivationClient::CanActivateWindow(aura::Window* window) const {
  return window &&
      window->IsVisible() &&
      (!aura::client::GetActivationDelegate(window) ||
       aura::client::GetActivationDelegate(window)->ShouldActivate(NULL));
}

aura::Window* DesktopActivationClient::GetActivatableWindow(
    aura::Window* window) {
  aura::Window* parent = window->parent();
  aura::Window* child = window;
  while (parent) {
    if (CanActivateWindow(child))
      return child;
    // If |child| isn't activatable, but has transient parent, trace
    // that path instead.
    if (child->transient_parent())
      return GetActivatableWindow(child->transient_parent());
    parent = parent->parent();
    child = child->parent();
  }
  return NULL;
}

}  // namespace views
