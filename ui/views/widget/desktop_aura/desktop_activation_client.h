// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_WIDGET_DESKTOP_AURA_DESTKOP_ACTIVATION_CLIENT_H_
#define UI_VIEWS_WIDGET_DESKTOP_AURA_DESTKOP_ACTIVATION_CLIENT_H_

#include "base/basictypes.h"
#include "base/observer_list.h"
#include "base/scoped_observer.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/env_observer.h"
#include "ui/aura/focus_change_observer.h"
#include "ui/aura/root_window_observer.h"
#include "ui/aura/window_observer.h"
#include "ui/views/views_export.h"

namespace aura {
class FocusManager;
class RootWindow;

namespace client {
class ActivationChangeObserver;
}
}

namespace views {

// An activation client that handles activation events in a single
// RootWindow. Used only on the Desktop where there can be multiple RootWindow
// objects.
class VIEWS_EXPORT DesktopActivationClient
    : public aura::client::ActivationClient,
      public aura::WindowObserver,
      public aura::FocusChangeObserver {
 public:
  explicit DesktopActivationClient(aura::RootWindow* root_window);
  virtual ~DesktopActivationClient();

  // ActivationClient:
  virtual void AddObserver(
      aura::client::ActivationChangeObserver* observer) OVERRIDE;
  virtual void RemoveObserver(
      aura::client::ActivationChangeObserver* observer) OVERRIDE;
  virtual void ActivateWindow(aura::Window* window) OVERRIDE;
  virtual void DeactivateWindow(aura::Window* window) OVERRIDE;
  virtual aura::Window* GetActiveWindow() OVERRIDE;
  virtual bool OnWillFocusWindow(aura::Window* window,
                                 const ui::Event* event) OVERRIDE;
  virtual bool CanActivateWindow(aura::Window* window) const OVERRIDE;

  // Overridden from aura::WindowObserver:
  virtual void OnWindowDestroying(aura::Window* window) OVERRIDE;

  // Overridden from aura::FocusChangeObserver:
  virtual void OnWindowFocused(aura::Window* window) OVERRIDE;

 private:
  // Walks up the chain to find the correct parent window to activate when we
  // try to activate |window|.
  aura::Window* GetActivatableWindow(aura::Window* window);

  aura::RootWindow* root_window_;

  // The current active window.
  aura::Window* current_active_;

  // True inside ActivateWindow(). Used to prevent recursion of focus
  // change notifications causing activation.
  bool updating_activation_;

  ObserverList<aura::client::ActivationChangeObserver> observers_;

  ScopedObserver<aura::Window, aura::WindowObserver> observer_manager_;

  DISALLOW_COPY_AND_ASSIGN(DesktopActivationClient);
};

}  // namespace views

#endif  // UI_VIEWS_WIDGET_DESKTOP_AURA_DESTKOP_ACTIVATION_CLIENT_H_
