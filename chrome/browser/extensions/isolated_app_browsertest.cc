// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/utf_string_conversions.h"
#include "chrome/browser/automation/automation_util.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/mock_host_resolver.h"

using content::ExecuteJavaScript;
using content::ExecuteJavaScriptAndExtractString;
using content::NavigationController;
using content::WebContents;
using content::RenderViewHost;

namespace {

std::wstring WrapForJavascriptAndExtract(
    const wchar_t* javascript_expression) {
  return std::wstring(L"window.domAutomationController.send(") +
      javascript_expression + L")";
}

class IsolatedAppTest : public ExtensionBrowserTest {
 public:
  // Returns whether the given tab's current URL has the given cookie.
  bool WARN_UNUSED_RESULT HasCookie(WebContents* contents, std::string cookie) {
    int value_size;
    std::string actual_cookie;
    automation_util::GetCookies(contents->GetURL(), contents, &value_size,
                                &actual_cookie);
    return actual_cookie.find(cookie) != std::string::npos;
  }

  const extensions::Extension* GetInstalledApp(WebContents* contents) {
    const extensions::Extension* installed_app = NULL;
    Profile* profile =
        Profile::FromBrowserContext(contents->GetBrowserContext());
    ExtensionService* service = profile->GetExtensionService();
    if (service) {
      std::set<std::string> extension_ids =
          service->process_map()->GetExtensionsInProcess(
              contents->GetRenderViewHost()->GetProcess()->GetID());
      for (std::set<std::string>::iterator iter = extension_ids.begin();
           iter != extension_ids.end(); ++iter) {
        installed_app = service->extensions()->GetByID(*iter);
        if (installed_app && installed_app->is_app())
          return installed_app;
      }
    }
    return NULL;
  }

 private:
  virtual void SetUpCommandLine(CommandLine* command_line) {
    ExtensionBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableExperimentalExtensionApis);
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(IsolatedAppTest, CrossProcessClientRedirect) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app1")));
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app2")));

  GURL base_url = test_server()->GetURL("files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  // redirect to app2.
  GURL redirect_url(test_server()->GetURL(
      "client-redirect?files/extensions/isolated_apps/app2/main.html"));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), redirect_url,
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  // Go back twice.
  // If bug fixed, we cannot go back anymore.
  // If not fixed, we will redirect back to app2 and can go back again.
  chrome::GoBack(browser(), CURRENT_TAB);
  chrome::GoBack(browser(), CURRENT_TAB);
  EXPECT_FALSE(chrome::CanGoBack(browser()));
}

// Tests that cookies set within an isolated app are not visible to normal
// pages or other apps.
//
// TODO(ajwong): Also test what happens if an app spans multiple sites in its
// extent.  These origins should also be isolated, but still have origin-based
// separation as you would expect.
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, CookieIsolation) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app1")));
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app2")));

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL base_url = test_server()->GetURL("files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app2/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("non_app/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  ASSERT_EQ(3, browser()->tab_count());

  // Ensure first two tabs have installed apps.
  WebContents* tab0 = chrome::GetWebContentsAt(browser(), 0);
  WebContents* tab1 = chrome::GetWebContentsAt(browser(), 1);
  WebContents* tab2 = chrome::GetWebContentsAt(browser(), 2);
  ASSERT_TRUE(GetInstalledApp(tab0));
  ASSERT_TRUE(GetInstalledApp(tab1));
  ASSERT_TRUE(!GetInstalledApp(tab2));

  // Check that tabs see cannot each other's localStorage even though they are
  // in the same origin.
  RenderViewHost* app1_rvh = tab0->GetRenderViewHost();
  RenderViewHost* app2_rvh = tab1->GetRenderViewHost();
  RenderViewHost* non_app_rvh = tab2->GetRenderViewHost();
  ASSERT_TRUE(ExecuteJavaScript(
      app1_rvh, L"", L"window.localStorage.setItem('testdata', 'ls_app1');"));
  ASSERT_TRUE(ExecuteJavaScript(
      app2_rvh, L"", L"window.localStorage.setItem('testdata', 'ls_app2');"));
  ASSERT_TRUE(ExecuteJavaScript(
      non_app_rvh, L"",
      L"window.localStorage.setItem('testdata', 'ls_normal');"));

  const std::wstring& kRetrieveLocalStorage =
      WrapForJavascriptAndExtract(
          L"window.localStorage.getItem('testdata') || 'badval'");
  std::string result;
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      app1_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_app1", result);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      app2_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_app2", result);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      non_app_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_normal", result);

  // Check that each tab sees its own cookie.
  EXPECT_TRUE(HasCookie(tab0, "app1=3"));
  EXPECT_TRUE(HasCookie(tab1, "app2=4"));
  EXPECT_TRUE(HasCookie(tab2, "normalPage=5"));

  // Check that app1 tab cannot see the other cookies.
  EXPECT_FALSE(HasCookie(tab0, "app2"));
  EXPECT_FALSE(HasCookie(tab0, "normalPage"));

  // Check that app2 tab cannot see the other cookies.
  EXPECT_FALSE(HasCookie(tab1, "app1"));
  EXPECT_FALSE(HasCookie(tab1, "normalPage"));

  // Check that normal tab cannot see the other cookies.
  EXPECT_FALSE(HasCookie(tab2, "app1"));
  EXPECT_FALSE(HasCookie(tab2, "app2"));

  // Check that the non_app iframe cookie is associated with app1 and not the
  // normal tab.  (For now, iframes are always rendered in their parent
  // process, even if they aren't in the app manifest.)
  EXPECT_TRUE(HasCookie(tab0, "nonAppFrame=6"));
  EXPECT_FALSE(HasCookie(tab2, "nonAppFrame"));

  // Check that isolation persists even if the tab crashes and is reloaded.
  chrome::SelectNumberedTab(browser(), 0);
  content::CrashTab(tab0);
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<NavigationController>(
          &chrome::GetActiveWebContents(browser())->GetController()));
  chrome::Reload(browser(), CURRENT_TAB);
  observer.Wait();
  EXPECT_TRUE(HasCookie(tab0, "app1=3"));
  EXPECT_FALSE(HasCookie(tab0, "app2"));
  EXPECT_FALSE(HasCookie(tab0, "normalPage"));

}

// This test is disabled due to being flaky. http://crbug.com/145588
// Ensure that cookies are not isolated if the isolated apps are not installed.
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, DISABLED_NoCookieIsolationWithoutApp) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL base_url = test_server()->GetURL("files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app2/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("non_app/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  ASSERT_EQ(3, browser()->tab_count());

  // Check that tabs see each other's cookies.
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 0), "app2=4"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 0), "normalPage=5"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 0), "nonAppFrame=6"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 1), "app1=3"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 1), "normalPage=5"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 1), "nonAppFrame=6"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 2), "app1=3"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 2), "app2=4"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 2), "nonAppFrame=6"));

  // Check that all tabs share the same localStorage if they have the same
  // origin.
  RenderViewHost* app1_rvh =
      chrome::GetWebContentsAt(browser(), 0)->GetRenderViewHost();
  RenderViewHost* app2_rvh =
      chrome::GetWebContentsAt(browser(), 1)->GetRenderViewHost();
  RenderViewHost* non_app_rvh =
      chrome::GetWebContentsAt(browser(), 2)->GetRenderViewHost();
  ASSERT_TRUE(ExecuteJavaScript(
      app1_rvh, L"", L"window.localStorage.setItem('testdata', 'ls_app1');"));
  ASSERT_TRUE(ExecuteJavaScript(
      app2_rvh, L"", L"window.localStorage.setItem('testdata', 'ls_app2');"));
  ASSERT_TRUE(ExecuteJavaScript(
      non_app_rvh, L"",
      L"window.localStorage.setItem('testdata', 'ls_normal');"));

  const std::wstring& kRetrieveLocalStorage =
      WrapForJavascriptAndExtract(L"window.localStorage.getItem('testdata')");
  std::string result;
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      app1_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_normal", result);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      app2_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_normal", result);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      non_app_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_normal", result);
}

// Tests that subresource and media requests use the app's cookie store.
// See http://crbug.com/141172.
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, SubresourceCookieIsolation) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app1")));

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL root_url = test_server()->GetURL("");
  GURL base_url = test_server()->GetURL("files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  root_url = root_url.ReplaceComponents(replace_host);
  base_url = base_url.ReplaceComponents(replace_host);

  // First set cookies inside and outside the app.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), root_url.Resolve("set-cookie?nonApp=1"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  WebContents* tab0 = chrome::GetWebContentsAt(browser(), 0);
  ASSERT_FALSE(GetInstalledApp(tab0));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  WebContents* tab1 = chrome::GetWebContentsAt(browser(), 1);
  ASSERT_TRUE(GetInstalledApp(tab1));

  // Check that each tab sees its own cookie.
  EXPECT_TRUE(HasCookie(tab0, "nonApp=1"));
  EXPECT_FALSE(HasCookie(tab0, "app1=3"));
  EXPECT_FALSE(HasCookie(tab1, "nonApp=1"));
  EXPECT_TRUE(HasCookie(tab1, "app1=3"));

  // Now visit an app page that loads subresources located outside the app.
  // For both images and video tags, it loads two URLs:
  //  - One will set nonApp{Media,Image}=1 cookies if nonApp=1 is set.
  //  - One will set app1{Media,Image}=1 cookies if app1=3 is set.
  // We expect only the app's cookies to be present.
  // We must wait for the onload event, to allow the subresources to finish.
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::Source<WebContents>(chrome::GetActiveWebContents(browser())));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/app_subresources.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  observer.Wait();
  EXPECT_FALSE(HasCookie(tab1, "nonAppMedia=1"));
  EXPECT_TRUE(HasCookie(tab1, "app1Media=1"));
  EXPECT_FALSE(HasCookie(tab1, "nonAppImage=1"));
  EXPECT_TRUE(HasCookie(tab1, "app1Image=1"));

  // Also create a non-app tab to ensure no new cookies were set in that jar.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), root_url,
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  WebContents* tab2 = chrome::GetWebContentsAt(browser(), 2);
  EXPECT_FALSE(HasCookie(tab2, "nonAppMedia=1"));
  EXPECT_FALSE(HasCookie(tab2, "app1Media=1"));
  EXPECT_FALSE(HasCookie(tab2, "nonAppImage=1"));
  EXPECT_FALSE(HasCookie(tab2, "app1Image=1"));
}

// Tests that isolated apps processes do not render top-level non-app pages.
// This is true even in the case of the OAuth workaround for hosted apps,
// where non-app popups may be kept in the hosted app process.
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, IsolatedAppProcessModel) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app1")));

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL base_url = test_server()->GetURL("files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);

  // Create three tabs in the isolated app in different ways.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  // For the third tab, use window.open to keep it in process with an opener.
  OpenWindow(chrome::GetWebContentsAt(browser(), 0),
             base_url.Resolve("app1/main.html"), true, NULL);

  // In a fourth tab, use window.open to a non-app URL.  It should open in a
  // separate process, even though this would trigger the OAuth workaround
  // for hosted apps (from http://crbug.com/59285).
  OpenWindow(chrome::GetWebContentsAt(browser(), 0),
             base_url.Resolve("non_app/main.html"), false, NULL);

  // We should now have four tabs, the first and third sharing a process.
  // The second one is an independent instance in a separate process.
  ASSERT_EQ(4, browser()->tab_count());
  int process_id_0 =
      chrome::GetWebContentsAt(browser(), 0)->GetRenderProcessHost()->GetID();
  int process_id_1 =
      chrome::GetWebContentsAt(browser(), 1)->GetRenderProcessHost()->GetID();
  EXPECT_NE(process_id_0, process_id_1);
  EXPECT_EQ(process_id_0,
            chrome::GetWebContentsAt(browser(), 2)->GetRenderProcessHost()->GetID());
  EXPECT_NE(process_id_0,
            chrome::GetWebContentsAt(browser(), 3)->GetRenderProcessHost()->GetID());

  // Navigating the second tab out of the app should cause a process swap.
  const GURL& non_app_url(base_url.Resolve("non_app/main.html"));
  NavigateInRenderer(chrome::GetWebContentsAt(browser(), 1), non_app_url);
  EXPECT_NE(process_id_1,
            chrome::GetWebContentsAt(browser(), 1)->GetRenderProcessHost()->GetID());
}

// This test no longer passes, since we don't properly isolate sessionStorage
// for isolated apps. This was broken as part of the changes for storage
// partition support for webview tags.
// TODO(nasko): If isolated apps is no longer developed, this test should be
// removed. http://crbug.com/159932
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, DISABLED_SessionStorage) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app1")));
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app2")));

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL base_url = test_server()->GetURL("files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);

  // Enter some state into sessionStorage three times on the same origin, but
  // for three URLs that correspond to app1, app2, and a non-isolated site.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ASSERT_TRUE(ExecuteJavaScript(
      chrome::GetWebContentsAt(browser(), 0)->GetRenderViewHost(),
      L"",
      L"window.sessionStorage.setItem('testdata', 'ss_app1');"));

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app2/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ASSERT_TRUE(ExecuteJavaScript(
      chrome::GetWebContentsAt(browser(), 0)->GetRenderViewHost(),
      L"",
      L"window.sessionStorage.setItem('testdata', 'ss_app2');"));

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("non_app/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ASSERT_TRUE(ExecuteJavaScript(
      chrome::GetWebContentsAt(browser(), 0)->GetRenderViewHost(),
      L"",
      L"window.sessionStorage.setItem('testdata', 'ss_normal');"));

  // Now, ensure that the sessionStorage is correctly partitioned, and persists
  // when we navigate around all over the dang place.
  const std::wstring& kRetrieveSessionStorage =
      WrapForJavascriptAndExtract(
          L"window.sessionStorage.getItem('testdata') || 'badval'");
  std::string result;
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      chrome::GetWebContentsAt(browser(), 0)->GetRenderViewHost(),
      L"", kRetrieveSessionStorage.c_str(), &result));
  EXPECT_EQ("ss_app1", result);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app2/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      chrome::GetWebContentsAt(browser(), 0)->GetRenderViewHost(),
      L"", kRetrieveSessionStorage.c_str(), &result));
  EXPECT_EQ("ss_app2", result);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("non_app/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      chrome::GetWebContentsAt(browser(), 0)->GetRenderViewHost(),
      L"", kRetrieveSessionStorage.c_str(), &result));
  EXPECT_EQ("ss_normal", result);
}
