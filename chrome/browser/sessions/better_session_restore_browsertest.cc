// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/content_settings.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/net_util.h"
#include "net/base/upload_data.h"
#include "net/base/upload_element.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_filter.h"
#include "net/url_request/url_request_test_job.h"

namespace {

// We need to serve the test files so that PRE_Test and Test can access the same
// page using the same URL. In addition, perceived security origin of the page
// needs to stay the same, so e.g., redirecting the URL requests doesn't
// work. (If we used a test server, the PRE_Test and Test would have separate
// instances running on separate ports.)

base::LazyInstance<std::map<std::string, std::string> > g_file_contents =
    LAZY_INSTANCE_INITIALIZER;

net::URLRequestJob* URLRequestFaker(
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate,
    const std::string& scheme) {
  return new net::URLRequestTestJob(
      request, network_delegate, net::URLRequestTestJob::test_headers(),
      g_file_contents.Get()[request->url().path()], true);
}

base::LazyInstance<std::string> g_last_upload_bytes = LAZY_INSTANCE_INITIALIZER;

net::URLRequestJob* URLRequestFakerForPostRequests(
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate,
    const std::string& scheme) {
  // Read the uploaded data and store it to g_last_upload_bytes.
  const net::UploadData* upload_data = request->get_upload();
  g_last_upload_bytes.Get().clear();
  if (upload_data) {
    const ScopedVector<net::UploadElement>& elements = upload_data->elements();
    for (size_t i = 0; i < elements.size(); ++i) {
      if (elements[i]->type() == net::UploadElement::TYPE_BYTES) {
        g_last_upload_bytes.Get() +=
            std::string(elements[i]->bytes(), elements[i]->bytes_length());
      }
    }
  }
  return new net::URLRequestTestJob(
      request, network_delegate, net::URLRequestTestJob::test_headers(),
      "<html><head><title>PASS</title></head><body>Data posted</body></html>",
      true);
}

}  // namespace

class BetterSessionRestoreTest : public InProcessBrowserTest {
 public:
  BetterSessionRestoreTest()
      : fake_server_address_("http://www.test.com/"),
        test_path_("session_restore/"),
        title_pass_(ASCIIToUTF16("PASS")),
        title_storing_(ASCIIToUTF16("STORING")),
        title_error_write_failed_(ASCIIToUTF16("ERROR_WRITE_FAILED")),
        title_error_empty_(ASCIIToUTF16("ERROR_EMPTY")) {
    // Set up the URL request filtering.
    std::vector<std::string> test_files;
    test_files.push_back("common.js");
    test_files.push_back("cookies.html");
    test_files.push_back("local_storage.html");
    test_files.push_back("post.html");
    test_files.push_back("post_with_password.html");
    test_files.push_back("session_cookies.html");
    test_files.push_back("session_storage.html");
    FilePath test_file_dir;
    CHECK(PathService::Get(base::DIR_SOURCE_ROOT, &test_file_dir));
    test_file_dir =
        test_file_dir.AppendASCII("chrome/test/data").AppendASCII(test_path_);

    for (std::vector<std::string>::const_iterator it = test_files.begin();
         it != test_files.end(); ++it) {
      FilePath path = test_file_dir.AppendASCII(*it);
      std::string contents;
      CHECK(file_util::ReadFileToString(path, &contents));
      g_file_contents.Get()["/" + test_path_ + *it] = contents;
      net::URLRequestFilter::GetInstance()->AddUrlHandler(
          GURL(fake_server_address_ + test_path_ + *it),
          &URLRequestFaker);
    }
    net::URLRequestFilter::GetInstance()->AddUrlHandler(
        GURL(fake_server_address_ + test_path_ + "posted.php"),
        &URLRequestFakerForPostRequests);
  }

 protected:
  void StoreDataWithPage(const std::string& filename) {
    content::WebContents* web_contents =
        chrome::GetActiveWebContents(browser());
    content::TitleWatcher title_watcher(web_contents, title_storing_);
    title_watcher.AlsoWaitForTitle(title_pass_);
    title_watcher.AlsoWaitForTitle(title_error_write_failed_);
    title_watcher.AlsoWaitForTitle(title_error_empty_);
    ui_test_utils::NavigateToURL(
        browser(), GURL(fake_server_address_ + test_path_ + filename));
    string16 final_title = title_watcher.WaitAndGetTitle();
    EXPECT_EQ(title_storing_, final_title);
  }

  void CheckReloadedPageRestored() {
    CheckTitle(title_pass_);
  }

  void CheckReloadedPageNotRestored() {
    CheckTitle(title_storing_);
  }

  void CheckTitle(const string16& expected_title) {
    content::WebContents* web_contents = chrome::GetWebContentsAt(browser(), 0);
    content::TitleWatcher title_watcher(web_contents, expected_title);
    title_watcher.AlsoWaitForTitle(title_pass_);
    title_watcher.AlsoWaitForTitle(title_storing_);
    title_watcher.AlsoWaitForTitle(title_error_write_failed_);
    title_watcher.AlsoWaitForTitle(title_error_empty_);
    // It's possible that the title was already the right one before
    // title_watcher was created.
    string16 first_title = web_contents->GetTitle();
    if (first_title != title_pass_ &&
        first_title != title_storing_ &&
        first_title != title_error_write_failed_ &&
        first_title != title_error_empty_) {
      string16 final_title = title_watcher.WaitAndGetTitle();
      EXPECT_EQ(expected_title, final_title);
    } else {
      EXPECT_EQ(expected_title, first_title);
    }
  }

 protected:
  std::string fake_server_address_;
  std::string test_path_;
  string16 title_pass_;

 private:
  string16 title_storing_;
  string16 title_error_write_failed_;
  string16 title_error_empty_;

  DISALLOW_COPY_AND_ASSIGN(BetterSessionRestoreTest);
};

class ContinueWhereILeftOffTest : public BetterSessionRestoreTest {
};

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_SessionCookies) {
  // Set the startup preference to "continue where I left off" and visit a page
  // which stores a session cookie.
  SessionStartupPref::SetStartupPref(
      browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
  StoreDataWithPage("session_cookies.html");
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, SessionCookies) {
  // The browsing session will be continued; just wait for the page to reload
  // and check the stored data.
  CheckReloadedPageRestored();
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_SessionStorage) {
  SessionStartupPref::SetStartupPref(
      browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
  StoreDataWithPage("session_storage.html");
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, SessionStorage) {
  CheckReloadedPageRestored();
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest,
                       PRE_PRE_LocalStorageClearedOnExit) {
  SessionStartupPref::SetStartupPref(
      browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
  StoreDataWithPage("local_storage.html");
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest,
                       PRE_LocalStorageClearedOnExit) {
  // Normally localStorage is restored.
  CheckReloadedPageRestored();
  // ... but not if it's set to clear on exit.
  CookieSettings::Factory::GetForProfile(browser()->profile())->
      SetDefaultCookieSetting(CONTENT_SETTING_SESSION_ONLY);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, LocalStorageClearedOnExit) {
  CheckReloadedPageNotRestored();
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest,
                       PRE_PRE_CookiesClearedOnExit) {
  SessionStartupPref::SetStartupPref(
      browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
  StoreDataWithPage("cookies.html");
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_CookiesClearedOnExit) {
  // Normally cookies are restored.
  CheckReloadedPageRestored();
  // ... but not if the content setting is set to clear on exit.
  CookieSettings::Factory::GetForProfile(browser()->profile())->
      SetDefaultCookieSetting(CONTENT_SETTING_SESSION_ONLY);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, CookiesClearedOnExit) {
  CheckReloadedPageNotRestored();
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_Post) {
  SessionStartupPref::SetStartupPref(
      browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
  content::WebContents* web_contents =
      chrome::GetActiveWebContents(browser());
  content::TitleWatcher title_watcher(web_contents, title_pass_);
  ui_test_utils::NavigateToURL(
      browser(), GURL(fake_server_address_ + test_path_ + "post.html"));
  string16 final_title = title_watcher.WaitAndGetTitle();
  EXPECT_EQ(title_pass_, final_title);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-text") !=
              std::string::npos);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("text-entered") !=
              std::string::npos);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, Post) {
  CheckReloadedPageRestored();
  EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-text") !=
              std::string::npos);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("text-entered") !=
              std::string::npos);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_PostWithPassword) {
  SessionStartupPref::SetStartupPref(
      browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
  content::WebContents* web_contents =
      chrome::GetActiveWebContents(browser());
  content::TitleWatcher title_watcher(web_contents, title_pass_);
  ui_test_utils::NavigateToURL(
      browser(),
      GURL(fake_server_address_ + test_path_ +
           "post_with_password.html"));
  string16 final_title = title_watcher.WaitAndGetTitle();
  EXPECT_EQ(title_pass_, final_title);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-text") !=
              std::string::npos);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("text-entered") !=
              std::string::npos);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-password") !=
              std::string::npos);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("password-entered") !=
              std::string::npos);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PostWithPassword) {
  CheckReloadedPageRestored();
  // The form data contained passwords, so it's removed completely.
  EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-text") ==
              std::string::npos);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("text-entered") ==
              std::string::npos);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-password") ==
              std::string::npos);
  EXPECT_TRUE(g_last_upload_bytes.Get().find("password-entered") ==
              std::string::npos);
}
