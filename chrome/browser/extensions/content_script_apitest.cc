// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/api/permissions/permissions_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "googleurl/src/gurl.h"
#include "net/base/mock_host_resolver.h"

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptAllFrames) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/all_frames")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptAboutBlankIframes) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(
      RunExtensionTest("content_scripts/about_blank_iframes")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptExtensionIframe) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/extension_iframe")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptExtensionProcess) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(
      RunExtensionTest("content_scripts/extension_process")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptFragmentNavigation) {
  ASSERT_TRUE(StartTestServer());
  const char* extension_name = "content_scripts/fragment";
  ASSERT_TRUE(RunExtensionTest(extension_name)) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptIsolatedWorlds) {
  // This extension runs various bits of script and tests that they all run in
  // the same isolated world.
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/isolated_world1")) << message_;

  // Now load a different extension, inject into same page, verify worlds aren't
  // shared.
  ASSERT_TRUE(RunExtensionTest("content_scripts/isolated_world2")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptIgnoreHostPermissions) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  host_resolver()->AddRule("b.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest(
      "content_scripts/dont_match_host_permissions")) << message_;
}

IN_PROC_BROWSER_TEST_F(
    ExtensionApiTest, ContentScriptInjectedIntoMultipartPage) {
  ASSERT_TRUE(StartTestServer());

  // Start with a renderer already open at a URL.
  GURL url(test_server()->GetURL("multipart-slow"));
  ui_test_utils::NavigateToURL(browser(), url);

  string16 title;
  ui_test_utils::GetCurrentTabTitle(browser(), &title);
  EXPECT_EQ(std::string("PASS"), UTF16ToUTF8(title));
}

// crbug.com/39249 -- content scripts js should not run on view source.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptViewSource) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/view_source")) << message_;
}

// crbug.com/126257 -- content scripts should not get injected into other
// extensions.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptOtherExtensions) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  // First, load extension that sets up content script.
  ASSERT_TRUE(RunExtensionTest("content_scripts/other_extensions/injector"))
      << message_;
  // Then load targeted extension to make sure its content isn't changed.
  ASSERT_TRUE(RunExtensionTest("content_scripts/other_extensions/victim"))
      << message_;
}

// crbug.com/120762
IN_PROC_BROWSER_TEST_F(
    ExtensionApiTest,
    DISABLED_ContentScriptStylesInjectedIntoExistingRenderers) {
  ASSERT_TRUE(StartTestServer());

  content::WindowedNotificationObserver signal(
      chrome::NOTIFICATION_USER_SCRIPTS_UPDATED,
      content::Source<Profile>(browser()->profile()));

  // Start with a renderer already open at a URL.
  GURL url(test_server()->GetURL("file/extensions/test_file.html"));
  ui_test_utils::NavigateToURL(browser(), url);

  LoadExtension(
      test_data_dir_.AppendASCII("content_scripts/existing_renderers"));

  signal.Wait();

  // And check that its styles were affected by the styles that just got loaded.
  bool styles_injected;
  ASSERT_TRUE(content::ExecuteJavaScriptAndExtractBool(
      chrome::GetActiveWebContents(browser())->GetRenderViewHost(), L"",
      L"window.domAutomationController.send("
      L"document.defaultView.getComputedStyle(document.body, null)."
      L"getPropertyValue('background-color') == 'rgb(255, 0, 0)')",
      &styles_injected));
  ASSERT_TRUE(styles_injected);
}

// Disable it until the regression caused by the webkit patch r135082 is fixed.
// (See https://bugs.webkit.org/show_bug.cgi?id=102110 for details)
IN_PROC_BROWSER_TEST_F(ExtensionApiTest,
                       DISABLED_ContentScriptCSSLocalization) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/css_l10n")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptExtensionAPIs) {
  ASSERT_TRUE(StartTestServer());

  const extensions::Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("content_scripts/extension_api"));

  ResultCatcher catcher;
  ui_test_utils::NavigateToURL(
      browser(), test_server()->GetURL("functions.html"));
  EXPECT_TRUE(catcher.GetNextResult());

  // Navigate to a page that will cause a content script to run that starts
  // listening for an extension event.
  ui_test_utils::NavigateToURL(
      browser(), test_server()->GetURL("events.html"));

  // Navigate to an extension page that will fire the event events.js is
  // listening for.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), extension->GetResourceURL("fire_event.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_NONE);
  EXPECT_TRUE(catcher.GetNextResult());
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptPermissionsApi) {
  RequestPermissionsFunction::SetIgnoreUserGestureForTests(true);
  RequestPermissionsFunction::SetAutoConfirmForTests(true);
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/permissions")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptBypassPageCSP) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/bypass_page_csp")) << message_;
}
