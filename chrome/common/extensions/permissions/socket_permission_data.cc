// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/permissions/socket_permission_data.h"

#include <cstdlib>
#include <sstream>
#include <vector>

#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "googleurl/src/url_canon.h"

namespace {

using content::SocketPermissionRequest;
using extensions::SocketPermissionData;

const char kColon = ':';
const char kDot = '.';
const char kWildcard[] = "*";
const char kInvalid[] = "invalid";
const char kTCPConnect[] = "tcp-connect";
const char kTCPListen[] = "tcp-listen";
const char kUDPBind[] = "udp-bind";
const char kUDPSendTo[] = "udp-send-to";
const int kAnyPort = 0;
const int kInvalidPort = -1;

SocketPermissionRequest::OperationType StringToType(const std::string& s) {
  if (s == kTCPConnect)
    return SocketPermissionRequest::TCP_CONNECT;
  if (s == kTCPListen)
    return SocketPermissionRequest::TCP_LISTEN;
  if (s == kUDPBind)
    return SocketPermissionRequest::UDP_BIND;
  if (s == kUDPSendTo)
    return SocketPermissionRequest::UDP_SEND_TO;
  return SocketPermissionRequest::NONE;
}

const char* TypeToString(SocketPermissionRequest::OperationType type) {
  switch (type) {
    case SocketPermissionRequest::TCP_CONNECT:
      return kTCPConnect;
    case SocketPermissionRequest::TCP_LISTEN:
      return kTCPListen;
    case SocketPermissionRequest::UDP_BIND:
      return kUDPBind;
    case SocketPermissionRequest::UDP_SEND_TO:
      return kUDPSendTo;
    default:
      return kInvalid;
  }
}

bool StartsOrEndsWithWhitespace(const std::string& str) {
  if (str.find_first_not_of(kWhitespaceASCII) != 0)
    return true;
  if (str.find_last_not_of(kWhitespaceASCII) != str.length() - 1)
    return true;
  return false;
}

}  // namespace

namespace extensions {

SocketPermissionData::SocketPermissionData()
  : pattern_(SocketPermissionRequest::NONE, std::string(), kInvalidPort) {
  Reset();
}

SocketPermissionData::~SocketPermissionData() {
}

bool SocketPermissionData::operator<(const SocketPermissionData& rhs) const {
  if (pattern_.type < rhs.pattern_.type)
    return true;
  if (pattern_.type > rhs.pattern_.type)
    return false;

  if (pattern_.host < rhs.pattern_.host)
    return true;
  if (pattern_.host > rhs.pattern_.host)
    return false;

  if (match_subdomains_ < rhs.match_subdomains_)
    return true;
  if (match_subdomains_ > rhs.match_subdomains_)
    return false;

  if (pattern_.port < rhs.pattern_.port)
    return true;
  return false;
}

bool SocketPermissionData::operator==(const SocketPermissionData& rhs) const {
  return (pattern_.type == rhs.pattern_.type) &&
         (pattern_.host == rhs.pattern_.host) &&
         (match_subdomains_ == rhs.match_subdomains_) &&
         (pattern_.port == rhs.pattern_.port);
}

bool SocketPermissionData::Match(SocketPermissionRequest request) const {
  if (pattern_.type != request.type)
    return false;

  std::string lhost = StringToLowerASCII(request.host);
  if (pattern_.host != lhost) {
    if (!match_subdomains_)
      return false;

    if (!pattern_.host.empty()) {
      // Do not wildcard part of IP address.
      url_parse::Component component(0, lhost.length());
      url_canon::RawCanonOutputT<char, 128> ignored_output;
      url_canon::CanonHostInfo host_info;
      url_canon::CanonicalizeIPAddress(lhost.c_str(), component,
                                       &ignored_output, &host_info);
      if (host_info.IsIPAddress())
        return false;

      // host should equal one or more chars + "." +  host_.
      int i = lhost.length() - pattern_.host.length();
      if (i < 2)
        return false;

      if (lhost.compare(i, pattern_.host.length(), pattern_.host) != 0)
        return false;

      if (lhost[i - 1] != kDot)
        return false;
    }
  }

  if (pattern_.port != request.port && pattern_.port != kAnyPort)
    return false;

  return true;
}

bool SocketPermissionData::Parse(const std::string& permission) {
  do {
    pattern_.host.clear();
    match_subdomains_ = true;
    pattern_.port = kAnyPort;
    spec_.clear();

    std::vector<std::string> tokens;
    base::SplitStringDontTrim(permission, kColon, &tokens);

    if (tokens.empty() || tokens.size() > 3)
      break;

    pattern_.type = StringToType(tokens[0]);
    if (pattern_.type == SocketPermissionRequest::NONE)
      break;

    if (tokens.size() == 1)
      return true;

    pattern_.host = tokens[1];
    if (!pattern_.host.empty()) {
      if (StartsOrEndsWithWhitespace(pattern_.host))
        break;
      pattern_.host = StringToLowerASCII(pattern_.host);

      // The first component can optionally be '*' to match all subdomains.
      std::vector<std::string> host_components;
      base::SplitString(pattern_.host, kDot, &host_components);
      DCHECK(!host_components.empty());

      if (host_components[0] == kWildcard || host_components[0].empty()) {
        host_components.erase(host_components.begin(),
                              host_components.begin() + 1);
      } else {
        match_subdomains_ = false;
      }
      pattern_.host = JoinString(host_components, kDot);
    }

    if (tokens.size() == 2 || tokens[2].empty() || tokens[2] == kWildcard)
      return true;

    if (StartsOrEndsWithWhitespace(tokens[2]))
      break;

    if (!base::StringToInt(tokens[2], &pattern_.port) ||
        pattern_.port < 1 || pattern_.port > 65535)
      break;
    return true;
  } while (false);

  Reset();
  return false;
}

SocketPermissionData::HostType SocketPermissionData::GetHostType() const {
  return pattern_.host.empty() ? SocketPermissionData::ANY_HOST :
         match_subdomains_     ? SocketPermissionData::HOSTS_IN_DOMAINS :
                                 SocketPermissionData::SPECIFIC_HOSTS;
}

const std::string SocketPermissionData::GetHost() const {
  return pattern_.host;
}

const std::string& SocketPermissionData::GetAsString() const {
  if (!spec_.empty())
    return spec_;

  spec_.reserve(64);
  spec_.append(TypeToString(pattern_.type));

  if (match_subdomains_) {
    spec_.append(1, kColon).append(kWildcard);
    if (!pattern_.host.empty())
      spec_.append(1, kDot).append(pattern_.host);
  } else {
     spec_.append(1, kColon).append(pattern_.host);
  }

  if (pattern_.port == kAnyPort)
    spec_.append(1, kColon).append(kWildcard);
  else
    spec_.append(1, kColon).append(base::IntToString(pattern_.port));

  return spec_;
}

void SocketPermissionData::Reset() {
  pattern_.type = SocketPermissionRequest::NONE;
  pattern_.host.clear();
  match_subdomains_ = false;
  pattern_.port = kInvalidPort;
  spec_.clear();
}

}  // namespace extensions
