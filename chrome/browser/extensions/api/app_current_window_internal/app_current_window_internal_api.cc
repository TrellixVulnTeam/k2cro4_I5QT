// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/app_current_window_internal/app_current_window_internal_api.h"

#include "chrome/browser/extensions/shell_window_registry.h"
#include "chrome/browser/ui/extensions/shell_window.h"
#include "chrome/common/extensions/api/app_current_window_internal.h"

namespace SetBounds = extensions::api::app_current_window_internal::SetBounds;
using extensions::api::app_current_window_internal::Bounds;

namespace extensions {

namespace {

const char kNoAssociatedShellWindow[] =
    "The context from which the function was called did not have an "
    "associated shell window.";

}  // namespace

bool AppCurrentWindowInternalExtensionFunction::RunImpl() {
  ShellWindowRegistry* registry = ShellWindowRegistry::Get(profile());
  DCHECK(registry);
  content::RenderViewHost* rvh = render_view_host();
  if (!rvh)
    // No need to set an error, since we won't return to the caller anyway if
    // there's no RVH.
    return false;
  ShellWindow* window = registry->GetShellWindowForRenderViewHost(rvh);
  if (!window) {
    error_ = kNoAssociatedShellWindow;
    return false;
  }
  return RunWithWindow(window);
}

bool AppCurrentWindowInternalFocusFunction::RunWithWindow(ShellWindow* window) {
  window->GetBaseWindow()->Activate();
  return true;
}

bool AppCurrentWindowInternalMaximizeFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->Maximize();
  return true;
}

bool AppCurrentWindowInternalMinimizeFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->Minimize();
  return true;
}

bool AppCurrentWindowInternalRestoreFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->Restore();
  return true;
}

bool AppCurrentWindowInternalDrawAttentionFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->FlashFrame(true);
  return true;
}

bool AppCurrentWindowInternalClearAttentionFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->FlashFrame(false);
  return true;
}

bool AppCurrentWindowInternalShowFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->Show();
  return true;
}

bool AppCurrentWindowInternalHideFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->Hide();
  return true;
}

bool AppCurrentWindowInternalSetBoundsFunction::RunWithWindow(
    ShellWindow* window) {
  // Start with the current bounds, and change any values that are specified in
  // the incoming parameters.
  gfx::Rect bounds = window->GetBaseWindow()->GetBounds();
  scoped_ptr<SetBounds::Params> params(SetBounds::Params::Create(*args_));
  CHECK(params.get());
  if (params->bounds.left)
    bounds.set_x(*(params->bounds.left));
  if (params->bounds.top)
    bounds.set_y(*(params->bounds.top));
  if (params->bounds.width)
    bounds.set_width(*(params->bounds.width));
  if (params->bounds.height)
    bounds.set_height(*(params->bounds.height));

  window->GetBaseWindow()->SetBounds(bounds);
  return true;
}

}  // namespace extensions
