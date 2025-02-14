// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_CHROME_HTTP_USER_AGENT_SETTINGS_H_
#define CHROME_BROWSER_NET_CHROME_HTTP_USER_AGENT_SETTINGS_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/api/prefs/pref_member.h"
#include "net/url_request/http_user_agent_settings.h"

class PrefService;

// An implementation of |HttpUserAgentSettings| that provides HTTP headers
// Accept-Language and Accept-Charset values that track Pref settings and uses
// |content::GetUserAgent| to provide the HTTP User-Agent header value.
class ChromeHttpUserAgentSettings : public net::HttpUserAgentSettings {
 public:
  // Must be called on the UI thread.
  explicit ChromeHttpUserAgentSettings(PrefService* prefs);
  // Must be called on the IO thread.
  virtual ~ChromeHttpUserAgentSettings();

  void CleanupOnUIThread();

  // net::HttpUserAgentSettings implementation
  virtual std::string GetAcceptLanguage() const OVERRIDE;
  virtual std::string GetAcceptCharset() const OVERRIDE;
  virtual std::string GetUserAgent(const GURL& url) const OVERRIDE;

 private:
  StringPrefMember pref_accept_language_;
  StringPrefMember pref_accept_charset_;

  // Avoid re-processing by caching the last value from the preferences and the
  // last result of processing via net::HttpUtil::GenerateAccept*Header().
  mutable std::string last_pref_accept_language_;
  mutable std::string last_http_accept_language_;
  mutable std::string last_pref_accept_charset_;
  mutable std::string last_http_accept_charset_;

  DISALLOW_COPY_AND_ASSIGN(ChromeHttpUserAgentSettings);
};

#endif  // CHROME_BROWSER_NET_CHROME_HTTP_USER_AGENT_SETTINGS_H_

