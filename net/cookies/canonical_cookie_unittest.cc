// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/canonical_cookie.h"

#include "base/memory/scoped_ptr.h"
#include "googleurl/src/gurl.h"
#include "net/cookies/cookie_options.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TEST(CanonicalCookieTest, GetCookieSourceFromURL) {
  EXPECT_EQ("http://example.com/",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("http://example.com")));
  EXPECT_EQ("http://example.com/",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("http://example.com/")));
  EXPECT_EQ("http://example.com/",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("http://example.com/test")));
  EXPECT_EQ("file:///tmp/test.html",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("file:///tmp/test.html")));
  EXPECT_EQ("http://example.com/",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("http://example.com:1234/")));
  EXPECT_EQ("http://example.com/",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("https://example.com/")));
  EXPECT_EQ("http://example.com/",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("http://user:pwd@example.com/")));
  EXPECT_EQ("http://example.com/",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("http://example.com/test?foo")));
  EXPECT_EQ("http://example.com/",
            CanonicalCookie::GetCookieSourceFromURL(
                GURL("http://example.com/test#foo")));
}

TEST(CanonicalCookieTest, Constructor) {
  GURL url("http://www.example.com/test");
  base::Time current_time = base::Time::Now();

  CanonicalCookie cookie(url, "A", "2", "www.example.com", "/test", "", "",
                         current_time, base::Time(), current_time, false,
                         false);
  EXPECT_EQ(url.GetOrigin().spec(), cookie.Source());
  EXPECT_EQ("A", cookie.Name());
  EXPECT_EQ("2", cookie.Value());
  EXPECT_EQ("www.example.com", cookie.Domain());
  EXPECT_EQ("/test", cookie.Path());
  EXPECT_FALSE(cookie.IsSecure());

  CanonicalCookie cookie2(url, "A", "2", "", "", "", "", current_time,
                          base::Time(), current_time, false, false);
  EXPECT_EQ(url.GetOrigin().spec(), cookie.Source());
  EXPECT_EQ("A", cookie2.Name());
  EXPECT_EQ("2", cookie2.Value());
  EXPECT_EQ("", cookie2.Domain());
  EXPECT_EQ("", cookie2.Path());
  EXPECT_FALSE(cookie2.IsSecure());

}

TEST(CanonicalCookieTest, Create) {
  GURL url("http://www.example.com/test/foo.html");
  base::Time creation_time = base::Time::Now();
  CookieOptions options;

  scoped_ptr<CanonicalCookie> cookie(
        CanonicalCookie::Create(url, "A=2", creation_time, options));
  EXPECT_EQ(url.GetOrigin().spec(), cookie->Source());
  EXPECT_EQ("A", cookie->Name());
  EXPECT_EQ("2", cookie->Value());
  EXPECT_EQ("www.example.com", cookie->Domain());
  EXPECT_EQ("/test", cookie->Path());
  EXPECT_FALSE(cookie->IsSecure());

  GURL url2("http://www.foo.com");
  cookie.reset(CanonicalCookie::Create(url2, "B=1", creation_time, options));
  EXPECT_EQ(url2.GetOrigin().spec(), cookie->Source());
  EXPECT_EQ("B", cookie->Name());
  EXPECT_EQ("1", cookie->Value());
  EXPECT_EQ("www.foo.com", cookie->Domain());
  EXPECT_EQ("/", cookie->Path());
  EXPECT_FALSE(cookie->IsSecure());

  cookie.reset(CanonicalCookie::Create(
      url, "A", "2", "www.example.com", "/test", "", "", creation_time,
      base::Time(), false, false));
  EXPECT_EQ(url.GetOrigin().spec(), cookie->Source());
  EXPECT_EQ("A", cookie->Name());
  EXPECT_EQ("2", cookie->Value());
  EXPECT_EQ(".www.example.com", cookie->Domain());
  EXPECT_EQ("/test", cookie->Path());
  EXPECT_FALSE(cookie->IsSecure());

  cookie.reset(CanonicalCookie::Create(
      url, "A", "2", ".www.example.com", "/test", "", "", creation_time,
      base::Time(), false, false));
  EXPECT_EQ(url.GetOrigin().spec(), cookie->Source());
  EXPECT_EQ("A", cookie->Name());
  EXPECT_EQ("2", cookie->Value());
  EXPECT_EQ(".www.example.com", cookie->Domain());
  EXPECT_EQ("/test", cookie->Path());
  EXPECT_FALSE(cookie->IsSecure());
}

TEST(CanonicalCookieTest, IsEquivalent) {
  GURL url("http://www.example.com/");
  std::string cookie_name = "A";
  std::string cookie_value = "2EDA-EF";
  std::string cookie_domain = ".www.example.com";
  std::string cookie_path = "/";
  std::string mac_key;
  std::string mac_algorithm;
  base::Time creation_time = base::Time::Now();
  base::Time last_access_time = creation_time;
  base::Time expiration_time = creation_time + base::TimeDelta::FromDays(2);
  bool secure(false);
  bool httponly(false);

  // Test that a cookie is equivalent to itself.
  scoped_ptr<CanonicalCookie> cookie(
      new CanonicalCookie(url, cookie_name, cookie_value, cookie_domain,
                          cookie_path, mac_key, mac_algorithm, creation_time,
                          expiration_time, last_access_time, secure, httponly));
  EXPECT_TRUE(cookie->IsEquivalent(*cookie));

  // Test that two identical cookies are equivalent.
  scoped_ptr<CanonicalCookie> other_cookie(
      new CanonicalCookie(url, cookie_name, cookie_value, cookie_domain,
                          cookie_path, mac_key, mac_algorithm, creation_time,
                          expiration_time, last_access_time, secure, httponly));
  EXPECT_TRUE(cookie->IsEquivalent(*other_cookie));

  // Tests that use different variations of attribute values that
  // DON'T affect cookie equivalence.
  other_cookie.reset(new CanonicalCookie(url, cookie_name, "2", cookie_domain,
                                         cookie_path, mac_key, mac_algorithm,
                                         creation_time, expiration_time,
                                         last_access_time, secure, httponly));
  EXPECT_TRUE(cookie->IsEquivalent(*other_cookie));

  base::Time other_creation_time =
      creation_time + base::TimeDelta::FromMinutes(2);
  other_cookie.reset(new CanonicalCookie(url, cookie_name, "2", cookie_domain,
                                         cookie_path, mac_key, mac_algorithm,
                                         other_creation_time, expiration_time,
                                         last_access_time, secure, httponly));
  EXPECT_TRUE(cookie->IsEquivalent(*other_cookie));

  other_cookie.reset(new CanonicalCookie(url, cookie_name, cookie_name,
                                         cookie_domain, cookie_path, mac_key,
                                         mac_algorithm, creation_time,
                                         expiration_time, last_access_time,
                                         true, httponly));
  EXPECT_TRUE(cookie->IsEquivalent(*other_cookie));

  // Tests that use different variations of attribute values that
  // DO affect cookie equivalence.
  other_cookie.reset(new CanonicalCookie(url, "B", cookie_value, cookie_domain,
                                         cookie_path, mac_key, mac_algorithm,
                                         creation_time, expiration_time,
                                         last_access_time, secure, httponly));
  EXPECT_FALSE(cookie->IsEquivalent(*other_cookie));

  other_cookie.reset(new CanonicalCookie(url, cookie_name, cookie_value,
                                         "www.example.com", cookie_path,
                                         mac_key, mac_algorithm, creation_time,
                                         expiration_time, last_access_time,
                                         secure, httponly));
  EXPECT_TRUE(cookie->IsDomainCookie());
  EXPECT_FALSE(other_cookie->IsDomainCookie());
  EXPECT_FALSE(cookie->IsEquivalent(*other_cookie));

  other_cookie.reset(new CanonicalCookie(url, cookie_name, cookie_value,
                                         ".example.com", cookie_path, mac_key,
                                         mac_algorithm, creation_time,
                                         expiration_time, last_access_time,
                                         secure, httponly));
  EXPECT_FALSE(cookie->IsEquivalent(*other_cookie));

  other_cookie.reset(new CanonicalCookie(url, cookie_name, cookie_value,
                                         cookie_domain, "/test/0", mac_key,
                                         mac_algorithm, creation_time,
                                         expiration_time, last_access_time,
                                         secure, httponly));
  EXPECT_FALSE(cookie->IsEquivalent(*other_cookie));
}

}  // namespace net
