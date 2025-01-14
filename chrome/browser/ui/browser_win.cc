// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser.h"

#include "base/utf_string_conversions.h"
#include "base/win/metro.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/fullscreen/fullscreen_controller.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"

namespace {

void NewMetroWindow(Browser* source_browser, Profile* profile) {
  typedef void (*FlipFrameWindows)();

  static FlipFrameWindows flip_window_fn = reinterpret_cast<FlipFrameWindows>(
      ::GetProcAddress(base::win::GetMetroModule(), "FlipFrameWindows"));
  DCHECK(flip_window_fn);

  chrome::HostDesktopType host_desktop_type =
      source_browser->host_desktop_type();
  Browser* browser =
      browser::FindTabbedBrowser(profile, false, host_desktop_type);

  if (!browser) {
    chrome::OpenEmptyWindow(profile);
    return;
  }

  chrome::NewTab(browser);

  if (browser != source_browser) {
    // Tell the metro_driver to flip our window. This causes the current
    // browser window to be hidden and the next window to be shown.
    flip_window_fn();
  }
}

}  // namespace

namespace chrome {

void NewWindow(Browser* browser) {
#if !defined(USE_AURA)
  if (base::win::IsMetroProcess()) {
    NewMetroWindow(browser, browser->profile()->GetOriginalProfile());
    return;
  }
#endif
  NewEmptyWindow(browser->profile()->GetOriginalProfile(),
                 browser->host_desktop_type());
}

void NewIncognitoWindow(Browser* browser) {
#if !defined(USE_AURA)
  if (base::win::IsMetroProcess()) {
    NewMetroWindow(browser, browser->profile()->GetOffTheRecordProfile());
    return;
  }
#endif
  NewEmptyWindow(browser->profile()->GetOffTheRecordProfile(),
                 browser->host_desktop_type());
}

}  // namespace chrome

void Browser::SetMetroSnapMode(bool enable) {
  fullscreen_controller_->SetMetroSnapMode(enable);
}
