// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/fileicon_source.h"

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/file_path.h"
#include "base/memory/ref_counted_memory.h"
#include "base/string_split.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "chrome/common/time_format.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"

namespace {

typedef std::map<std::string, IconLoader::IconSize> QueryIconSizeMap;

// The path used in internal URLs to file icon data.
const char kFileIconPath[] = "fileicon";

// URL parameter specifying icon size.
const char kIconSize[] = "iconsize";

// URL parameter specifying scale factor.
const char kScaleFactor[] = "scale";

// Assuming the url is of the form '/path?query', convert the path portion into
// a FilePath and return the resulting |file_path| and |query|.  The path
// portion may have been encoded using encodeURIComponent().
void GetFilePathAndQuery(const std::string& url,
                         FilePath* file_path,
                         std::string* query) {
  // We receive the url with chrome://fileicon/ stripped but GURL expects it.
  const GURL gurl("chrome://fileicon/" + url);
  std::string path = net::UnescapeURLComponent(
      gurl.path().substr(1), (net::UnescapeRule::URL_SPECIAL_CHARS |
                              net::UnescapeRule::SPACES));

#if defined(OS_WIN)
  // The path we receive has the wrong slashes and escaping for what we need;
  // this only appears to matter for getting icons from .exe files.
  std::replace(path.begin(), path.end(), '/', '\\');
  *file_path = FilePath(UTF8ToWide(path));
#elif defined(OS_POSIX)
  // The correct encoding on Linux may not actually be UTF8.
  *file_path = FilePath(path);
#endif
  query->assign(gurl.query());
}

IconLoader::IconSize SizeStringToIconSize(const std::string& size_string) {
  if (size_string == "small") return IconLoader::SMALL;
  if (size_string == "large") return IconLoader::LARGE;
  // We default to NORMAL if we don't recognize the size_string. Including
  // size_string=="normal".
  return IconLoader::NORMAL;
}

// Simple parser for data on the query.
void ParseQueryParams(const std::string& query,
                      ui::ScaleFactor* scale_factor,
                      IconLoader::IconSize* icon_size) {
  typedef std::pair<std::string, std::string> KVPair;
  std::vector<KVPair> parameters;
  if (icon_size)
    *icon_size = IconLoader::NORMAL;
  if (scale_factor)
    *scale_factor = ui::SCALE_FACTOR_100P;
  base::SplitStringIntoKeyValuePairs(query, '=', '&', &parameters);
  for (std::vector<KVPair>::const_iterator iter = parameters.begin();
       iter != parameters.end(); ++iter) {
    if (icon_size && iter->first == kIconSize)
      *icon_size = SizeStringToIconSize(iter->second);
    else if (scale_factor && iter->first == kScaleFactor)
      web_ui_util::ParseScaleFactor(iter->second, scale_factor);
  }
}

}  // namespace

FileIconSource::FileIconSource()
    : DataSource(kFileIconPath, MessageLoop::current()) {}

FileIconSource::~FileIconSource() {
  cancelable_consumer_.CancelAllRequests();
}

void FileIconSource::FetchFileIcon(const FilePath& path,
                                   ui::ScaleFactor scale_factor,
                                   IconLoader::IconSize icon_size,
                                   int request_id) {
  IconManager* im = g_browser_process->icon_manager();
  gfx::Image* icon = im->LookupIcon(path, icon_size);

  if (icon) {
    scoped_refptr<base::RefCountedBytes> icon_data(new base::RefCountedBytes);
    gfx::PNGCodec::EncodeBGRASkBitmap(
        icon->ToImageSkia()->GetRepresentation(scale_factor).sk_bitmap(),
        false, &icon_data->data());

    SendResponse(request_id, icon_data);
  } else {
    // Icon was not in cache, go fetch it slowly.
    IconManager::Handle h = im->LoadIcon(
        path, icon_size, &cancelable_consumer_,
        base::Bind(&FileIconSource::OnFileIconDataAvailable,
                   base::Unretained(this)));

    // Attach the ChromeURLDataManager request ID to the history request.
    IconRequestDetails details;
    details.request_id = request_id;
    details.scale_factor = scale_factor;
    cancelable_consumer_.SetClientData(im, h, details);
  }
}

void FileIconSource::StartDataRequest(const std::string& url_path,
                                      bool is_incognito,
                                      int request_id) {
  std::string query;
  FilePath file_path;
  ui::ScaleFactor scale_factor;
  IconLoader::IconSize icon_size;
  GetFilePathAndQuery(url_path, &file_path, &query);
  ParseQueryParams(query, &scale_factor, &icon_size);
  FetchFileIcon(file_path, scale_factor, icon_size, request_id);
}

std::string FileIconSource::GetMimeType(const std::string&) const {
  // Rely on image decoder inferring the correct type.
  return std::string();
}

void FileIconSource::OnFileIconDataAvailable(IconManager::Handle handle,
                                             gfx::Image* icon) {
  IconManager* im = g_browser_process->icon_manager();
  IconRequestDetails details = cancelable_consumer_.GetClientData(im, handle);

  if (icon) {
    scoped_refptr<base::RefCountedBytes> icon_data(new base::RefCountedBytes);
    gfx::PNGCodec::EncodeBGRASkBitmap(
        icon->ToImageSkia()->GetRepresentation(
            details.scale_factor).sk_bitmap(),
        false, &icon_data->data());

    SendResponse(details.request_id, icon_data);
  } else {
    // TODO(glen): send a dummy icon.
    SendResponse(details.request_id, NULL);
  }
}
