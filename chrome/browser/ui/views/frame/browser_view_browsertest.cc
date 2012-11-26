// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_view.h"

#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/views/immersive_mode_controller.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"

using views::FocusManager;
using views::View;

typedef InProcessBrowserTest BrowserViewTest;

// Active window and focus testing is not reliable on Windows crbug.com/79493
#if defined(OS_WIN)
#define MAYBE_FullscreenClearsFocus DISABLED_FullscreenClearsFocus
#else
#define MAYBE_FullscreenClearsFocus FullscreenClearsFocus
#endif
IN_PROC_BROWSER_TEST_F(BrowserViewTest, MAYBE_FullscreenClearsFocus) {
  BrowserView* browser_view = static_cast<BrowserView*>(browser()->window());
  LocationBarView* location_bar_view = browser_view->GetLocationBarView();
  FocusManager* focus_manager = browser_view->GetFocusManager();

  // Focus starts in the location bar or one of its children.
  EXPECT_TRUE(location_bar_view->Contains(focus_manager->GetFocusedView()));

  chrome::ToggleFullscreenMode(browser());
  EXPECT_TRUE(browser_view->IsFullscreen());

  // Focus is released from the location bar.
  EXPECT_FALSE(location_bar_view->Contains(focus_manager->GetFocusedView()));
}

IN_PROC_BROWSER_TEST_F(BrowserViewTest, ImmersiveMode) {
  BrowserView* browser_view = static_cast<BrowserView*>(browser()->window());
  ImmersiveModeController* controller =
      browser_view->immersive_mode_controller();

  // Immersive mode is not on by default.
  EXPECT_FALSE(controller->enabled());
  EXPECT_FALSE(controller->ShouldHideTopViews());

  // Top-of-window views are visible.
  EXPECT_TRUE(browser_view->IsTabStripVisible());
  EXPECT_TRUE(browser_view->IsToolbarVisible());

  // Turning immersive mode on sets the toolbar to immersive style and hides
  // the top-of-window views while leaving the tab strip visible.
  controller->SetEnabled(true);
  EXPECT_TRUE(controller->enabled());
  EXPECT_TRUE(controller->ShouldHideTopViews());
  EXPECT_TRUE(browser_view->tabstrip()->IsImmersiveStyle());
  EXPECT_TRUE(browser_view->IsTabStripVisible());
  EXPECT_FALSE(browser_view->IsToolbarVisible());

  // Trigger a reveal keeps us in immersive mode, but top-of-window views
  // become visible.
  controller->RevealTopViews();
  EXPECT_TRUE(controller->enabled());
  EXPECT_FALSE(controller->ShouldHideTopViews());
  EXPECT_FALSE(browser_view->tabstrip()->IsImmersiveStyle());
  EXPECT_TRUE(browser_view->IsTabStripVisible());
  EXPECT_TRUE(browser_view->IsToolbarVisible());
}
