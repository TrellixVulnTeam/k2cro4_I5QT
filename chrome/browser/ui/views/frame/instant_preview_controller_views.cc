// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/instant_preview_controller_views.h"

#include "chrome/browser/instant/instant_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/contents_container.h"
#include "ui/views/controls/webview/webview.h"

InstantPreviewControllerViews::InstantPreviewControllerViews(
    Browser* browser,
    ContentsContainer* contents)
    : InstantPreviewController(browser),
      contents_(contents) {
}

InstantPreviewControllerViews::~InstantPreviewControllerViews() {
}

void InstantPreviewControllerViews::PreviewStateChanged(
    const InstantModel& model) {
  if (model.mode().is_ntp() || model.mode().is_search_suggestions()) {
    // Show the preview.
    if (!preview_) {
      preview_.reset(new views::WebView(browser_->profile()));
      preview_->set_id(VIEW_ID_TAB_CONTAINER);
    }
    content::WebContents* web_contents =
        model.GetPreviewContents()->web_contents();
    contents_->SetPreview(preview_.get(), web_contents,
                          model.height(), model.height_units());
    preview_->SetWebContents(web_contents);
  } else if (preview_) {
    // Hide the preview. SetWebContents() must happen before SetPreview().
    preview_->SetWebContents(NULL);
    contents_->SetPreview(NULL, NULL, 100, INSTANT_SIZE_PERCENT);
    preview_.reset();
  }
}
