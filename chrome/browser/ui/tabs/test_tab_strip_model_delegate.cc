// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/test_tab_strip_model_delegate.h"

TestTabStripModelDelegate::TestTabStripModelDelegate() {
}

TestTabStripModelDelegate::~TestTabStripModelDelegate() {
}

void TestTabStripModelDelegate::AddBlankTabAt(int index, bool foreground) {
}

Browser* TestTabStripModelDelegate::CreateNewStripWithContents(
    const std::vector<NewStripContents>& contentses,
    const gfx::Rect& window_bounds,
    const DockInfo& dock_info,
    bool maximize) {
  return NULL;
}

int TestTabStripModelDelegate::GetDragActions() const {
  return 0;
}

bool TestTabStripModelDelegate::CanDuplicateContentsAt(int index) {
  return false;
}

void TestTabStripModelDelegate::DuplicateContentsAt(int index) {
}

void TestTabStripModelDelegate::CloseFrameAfterDragSession() {
}

void TestTabStripModelDelegate::CreateHistoricalTab(
    content::WebContents* contents) {
}

bool TestTabStripModelDelegate::RunUnloadListenerBeforeClosing(
    content::WebContents* contents) {
  return true;
}

bool TestTabStripModelDelegate::CanRestoreTab() {
  return false;
}

void TestTabStripModelDelegate::RestoreTab() {
}

bool TestTabStripModelDelegate::CanBookmarkAllTabs() const {
  return true;
}

void TestTabStripModelDelegate::BookmarkAllTabs() {
}
