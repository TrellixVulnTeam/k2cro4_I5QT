// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/launcher_favicon_loader.h"

#include "base/logging.h"
#include "chrome/browser/favicon/favicon_download_helper.h"
#include "chrome/browser/favicon/favicon_download_helper_delegate.h"
#include "chrome/browser/ui/ash/launcher/browser_launcher_item_controller.h"
#include "chrome/common/favicon_url.h"
#include "chrome/common/icon_messages.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "googleurl/src/gurl.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace internal {

const int kMaxBitmapSize = 256;

////////////////////////////////////////////////////////////////////////////////
// FaviconBitmapHandler fetchs all bitmaps with the 'icon' (or 'shortcut icon')
// link tag, storing the one that best matches ash::kLauncherPreferredSize.
// These icon bitmaps are not resized and are not cached beyond the lifetime
// of the class. Bitmaps larger than kMaxBitmapSize are ignored.

class FaviconBitmapHandler : public FaviconDownloadHelperDelegate {
 public:
  FaviconBitmapHandler(content::WebContents* web_contents,
                       LauncherFaviconLoader::Delegate* delegate)
      : delegate_(delegate) {
    download_helper_.reset(new FaviconDownloadHelper(web_contents, this));
  }

  ~FaviconBitmapHandler() {}

  const SkBitmap& bitmap() const { return bitmap_; }

  bool HasPendingDownloads() const;

  // FaviconDownloadHelperDelegate methods
  virtual void OnUpdateFaviconURL(
    int32 page_id,
    const std::vector<FaviconURL>& candidates) OVERRIDE;

  virtual void OnDidDownloadFavicon(
    int id,
    const GURL& image_url,
    bool errored,
    int requested_size,
    const std::vector<SkBitmap>& bitmaps) OVERRIDE;

 private:
  void DownloadFavicon(const GURL& image_url);
  void AddFavicon(const GURL& image_url, const SkBitmap& new_bitmap);

  LauncherFaviconLoader::Delegate* delegate_;

  scoped_ptr<FaviconDownloadHelper> download_helper_;

  typedef std::set<GURL> UrlSet;
  // Map of pending download urls.
  UrlSet pending_requests_;
  // Map of processed urls.
  UrlSet processed_requests_;
  // Current bitmap and source url.
  SkBitmap bitmap_;
  GURL bitmap_url_;

  DISALLOW_COPY_AND_ASSIGN(FaviconBitmapHandler);
};

void FaviconBitmapHandler::OnUpdateFaviconURL(
    int32 page_id,
    const std::vector<FaviconURL>& candidates) {
  // This function receives a complete list of faviocn urls for the page.
  // It may get called multiple times with the same list, and will also get
  // called any time an item is added or removed. As such, we track processed
  // and pending urls, but only until they are removed from the list.
  UrlSet new_pending, new_processed;
  // Create a map of valid favicon urls.
  std::set<GURL> urls;
  for (std::vector<FaviconURL>::const_iterator iter = candidates.begin();
       iter != candidates.end(); ++iter) {
    if (iter->icon_type != FaviconURL::FAVICON)
      continue;
    const GURL& url = iter->icon_url;
    if (url.is_valid())
      urls.insert(url);
    // Preserve matching pending requests amd processed requests.
    if (pending_requests_.find(url) != pending_requests_.end())
      new_pending.insert(url);
    if (processed_requests_.find(url) != processed_requests_.end())
      new_processed.insert(url);
  }
  pending_requests_ = new_pending;
  processed_requests_ = new_processed;
  // Reset bitmap_ if no longer valid (i.e. not in the list of urls).
  if (urls.find(bitmap_url_) == urls.end()) {
    bitmap_url_ = GURL();
    bitmap_.reset();
  }
  // Request any new urls.
  for (std::set<GURL>::iterator iter = urls.begin();
       iter != urls.end(); ++iter) {
    if (processed_requests_.find(*iter) != processed_requests_.end())
      continue;  // Skip already processed downloads.
    if (pending_requests_.find(*iter) != pending_requests_.end())
      continue;  // Skip already pending downloads.
    pending_requests_.insert(*iter);
    download_helper_->DownloadFavicon(*iter, 0);
  }
}

void FaviconBitmapHandler::OnDidDownloadFavicon(
    int id,
    const GURL& image_url,
    bool errored,
    int requested_size,
    const std::vector<SkBitmap>& bitmaps) {
  UrlSet::iterator iter = pending_requests_.find(image_url);
  if (iter == pending_requests_.end()) {
    // Updates are received for all downloads; ignore unrequested urls.
    return;
  }
  pending_requests_.erase(iter);

  // Favicon bitmaps are ordered by decreasing width.
  if (!errored && !bitmaps.empty())
    AddFavicon(image_url, bitmaps[0]);
}

bool FaviconBitmapHandler::HasPendingDownloads() const {
  return !pending_requests_.empty();
}

void FaviconBitmapHandler::AddFavicon(const GURL& image_url,
                                      const SkBitmap& new_bitmap) {
  processed_requests_.insert(image_url);
  if (new_bitmap.height() > kMaxBitmapSize ||
      new_bitmap.width() > kMaxBitmapSize)
    return;
  if (new_bitmap.height() < ash::kLauncherPreferredSize)
    return;
  if (!bitmap_.isNull()) {
    // We want the smallest icon that is large enough.
    if (new_bitmap.height() > bitmap_.height())
      return;
  }
  bitmap_url_ = image_url;
  bitmap_ = new_bitmap;
  delegate_->FaviconUpdated();
}

}  // namespace internal

////////////////////////////////////////////////////////////////////////////////

LauncherFaviconLoader::LauncherFaviconLoader(Delegate* delegate,
                                             content::WebContents* web_contents)
    : web_contents_(web_contents) {
  favicon_handler_.reset(
      new internal::FaviconBitmapHandler(web_contents, delegate));
}

LauncherFaviconLoader::~LauncherFaviconLoader() {
}

SkBitmap LauncherFaviconLoader::GetFavicon() const {
  return favicon_handler_->bitmap();
}

bool LauncherFaviconLoader::HasPendingDownloads() const {
  return favicon_handler_->HasPendingDownloads();
}
