// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_EXTENSIONS_EXTENSION_ICON_SOURCE_H_
#define CHROME_BROWSER_UI_WEBUI_EXTENSIONS_EXTENSION_ICON_SOURCE_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/extension_resource.h"
#include "third_party/skia/include/core/SkBitmap.h"

class ExtensionIconSet;
class Profile;

namespace extensions {
class Extension;
}

// ExtensionIconSource serves extension icons through network level chrome:
// requests. Icons can be retrieved for any installed extension or app.
//
// The format for requesting an icon is as follows:
//   chrome://extension-icon/<extension_id>/<icon_size>/<match_type>?[options]
//
//   Parameters (<> required, [] optional):
//    <extension_id>  = the id of the extension
//    <icon_size>     = the size of the icon, as the integer value of the
//                      corresponding Extension:Icons enum.
//    <match_type>    = the fallback matching policy, as the integer value of
//                      the corresponding ExtensionIconSet::MatchType enum.
//    [options]       = Optional transformations to apply. Supported options:
//                        grayscale=true to desaturate the image.
//
// Examples:
//   chrome-extension://gbmgkahjioeacddebbnengilkgbkhodg/32/1?grayscale=true
//     (ICON_SMALL, MATCH_BIGGER, grayscale)
//   chrome-extension://gbmgkahjioeacddebbnengilkgbkhodg/128/0
//     (ICON_LARGE, MATCH_EXACTLY)
//
// We attempt to load icons from the following sources in order:
//  1) The icons as listed in the extension / app manifests.
//  2) If a 16px icon was requested, the favicon for extension's launch URL.
//  3) The default extension / application icon if there are still no matches.
//
class ExtensionIconSource : public ChromeURLDataManager::DataSource {
 public:
  explicit ExtensionIconSource(Profile* profile);

  // Gets the URL of the |extension| icon in the given |icon_size|, falling back
  // based on the |match| type. If |grayscale|, the URL will be for the
  // desaturated version of the icon. |exists|, if non-NULL, will be set to true
  // if the icon exists; false if it will lead to a default or not-present
  // image.
  static GURL GetIconURL(const extensions::Extension* extension,
                         int icon_size,
                         ExtensionIconSet::MatchType match,
                         bool grayscale,
                         bool* exists);

  // A public utility function for accessing the bitmap of the image specified
  // by |resource_id|.
  static SkBitmap* LoadImageByResourceId(int resource_id);

  // ChromeURLDataManager::DataSource

  virtual std::string GetMimeType(const std::string&) const OVERRIDE;

  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id) OVERRIDE;

 private:
  // Encapsulates the request parameters for |request_id|.
  struct ExtensionIconRequest;

  virtual ~ExtensionIconSource();

  // Returns the bitmap for the default app image.
  const SkBitmap* GetDefaultAppImage();

  // Returns the bitmap for the default extension.
  const SkBitmap* GetDefaultExtensionImage();

  // Performs any remaining transformations (like desaturating the |image|),
  // then returns the |image| to the client and clears up any temporary data
  // associated with the |request_id|.
  void FinalizeImage(const SkBitmap* image, int request_id);

  // Loads the default image for |request_id| and returns to the client.
  void LoadDefaultImage(int request_id);

  // Loads the extension's |icon| for the given |request_id| and returns the
  // image to the client.
  void LoadExtensionImage(const ExtensionResource& icon, int request_id);

  // Loads the favicon image for the app associated with the |request_id|. If
  // the image does not exist, we fall back to the default image.
  void LoadFaviconImage(int request_id);

  // FaviconService callback
  void OnFaviconDataAvailable(
      FaviconService::Handle request_handle,
      const history::FaviconBitmapResult& bitmap_result);

  // ImageLoader callback
  void OnImageLoaded(int request_id, const gfx::Image& image);

  // Called when the extension doesn't have an icon. We fall back to multiple
  // sources, using the following order:
  //  1) The icons as listed in the extension / app manifests.
  //  2) If a 16px icon and the extension has a launch URL, see if Chrome
  //     has a corresponding favicon.
  //  3) If still no matches, load the default extension / application icon.
  void LoadIconFailed(int request_id);

  // Parses and savse an ExtensionIconRequest for the URL |path| for the
  // specified |request_id|.
  bool ParseData(const std::string& path, int request_id);

  // Sends the default response to |request_id|, used for invalid requests.
  void SendDefaultResponse(int request_id);

  // Stores the parameters associated with the |request_id|, making them
  // as an ExtensionIconRequest via GetData.
  void SetData(int request_id,
               const extensions::Extension* extension,
               bool grayscale,
               int size,
               ExtensionIconSet::MatchType match);

  // Returns the ExtensionIconRequest for the given |request_id|.
  ExtensionIconRequest* GetData(int request_id);

  // Removes temporary data associated with |request_id|.
  void ClearData(int request_id);

  Profile* profile_;

  // Maps tracker ids to request ids.
  std::map<int, int> tracker_map_;

  // Maps request_ids to ExtensionIconRequests.
  std::map<int, ExtensionIconRequest*> request_map_;

  scoped_ptr<SkBitmap> default_app_data_;

  scoped_ptr<SkBitmap> default_extension_data_;

  CancelableRequestConsumerT<int, 0> cancelable_consumer_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionIconSource);
};

#endif  // CHROME_BROWSER_UI_WEBUI_EXTENSIONS_EXTENSION_ICON_SOURCE_H_
