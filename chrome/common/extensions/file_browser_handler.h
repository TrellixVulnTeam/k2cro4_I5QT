// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_EXTENSIONS_FILE_BROWSER_HANDLER_H_
#define CHROME_COMMON_EXTENSIONS_FILE_BROWSER_HANDLER_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "extensions/common/url_pattern.h"
#include "extensions/common/url_pattern_set.h"
#include "googleurl/src/gurl.h"

class URLPattern;

// FileBrowserHandler encapsulates the state of a file browser action.
class FileBrowserHandler {
 public:
  FileBrowserHandler();
  ~FileBrowserHandler();

  // extension id
  std::string extension_id() const { return extension_id_; }
  void set_extension_id(const std::string& extension_id) {
    extension_id_ = extension_id;
  }

  // action id
  const std::string& id() const { return id_; }
  void set_id(const std::string& id) { id_ = id; }

  // default title
  const std::string& title() const { return title_; }
  void set_title(const std::string& title) { title_ = title; }

  // File schema URL patterns.
  const extensions::URLPatternSet& file_url_patterns() const {
    return url_set_;
  }
  void AddPattern(const URLPattern& pattern);
  bool MatchesURL(const GURL& url) const;
  void ClearPatterns();

  // Action icon path.
  const std::string icon_path() const { return default_icon_path_; }
  void set_icon_path(const std::string& path) {
    default_icon_path_ = path;
  }

  // File access permissions.
  // Adjusts file_access_permission_flags_ to allow specified permission.
  bool AddFileAccessPermission(const std::string& permission_str);
  // Checks that specified file access permissions are valid (all set
  // permissions are valid and there is no other permission specified with
  // "create")
  // If no access permissions were set, initialize them to default value.
  bool ValidateFileAccessPermissions();
  // Checks if handler has read access.
  bool CanRead() const;
  // Checks if handler has write access.
  bool CanWrite() const;
  // Checks if handler has "create" access specified.
  bool HasCreateAccessPermission() const;

 private:
  // The id for the extension this action belongs to (as defined in the
  // extension manifest).
  std::string extension_id_;
  std::string title_;
  std::string default_icon_path_;
  // The id for the FileBrowserHandler, for example: "PdfFileAction".
  std::string id_;
  unsigned int file_access_permission_flags_;

  // A list of file filters.
  extensions::URLPatternSet url_set_;
};

#endif  // CHROME_COMMON_EXTENSIONS_FILE_BROWSER_HANDLER_H_
