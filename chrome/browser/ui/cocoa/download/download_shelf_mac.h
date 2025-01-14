// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_DOWNLOAD_DOWNLOAD_SHELF_MAC_H_
#define CHROME_BROWSER_UI_COCOA_DOWNLOAD_DOWNLOAD_SHELF_MAC_H_

#import <Cocoa/Cocoa.h>

#include "base/compiler_specific.h"
#include "chrome/browser/download/download_shelf.h"

class DownloadItemModel;

@class DownloadShelfController;

// A class to bridge the chromium download shelf to mac gui. This is just a
// wrapper class that forward everything to DownloadShelfController.

class DownloadShelfMac : public DownloadShelf {
 public:
  DownloadShelfMac(Browser* browser, DownloadShelfController* controller);

  // DownloadShelf implementation.
  virtual bool IsShowing() const OVERRIDE;
  virtual bool IsClosing() const OVERRIDE;
  virtual Browser* browser() const OVERRIDE;

 protected:
  virtual void DoAddDownload(DownloadItemModel* download_model) OVERRIDE;
  virtual void DoShow() OVERRIDE;
  virtual void DoClose() OVERRIDE;

 private:
  // The browser that owns this shelf.
  Browser* browser_;

  DownloadShelfController* shelf_controller_;  // weak, owns us
};

#endif  // CHROME_BROWSER_UI_COCOA_DOWNLOAD_DOWNLOAD_SHELF_MAC_H_
