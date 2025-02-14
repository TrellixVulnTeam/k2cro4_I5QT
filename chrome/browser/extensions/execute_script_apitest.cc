// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"
#include "net/base/mock_host_resolver.h"

class ExecuteScriptApiTest : public ExtensionApiTest {
 protected:
  void SetupDelayedHostResolver() {
    // We need a.com to be a little bit slow to trigger a race condition.
    host_resolver()->AddRuleWithLatency("a.com", "127.0.0.1", 500);
    host_resolver()->AddRule("b.com", "127.0.0.1");
    host_resolver()->AddRule("c.com", "127.0.0.1");
  }
};

// Disable it until the regression caused by the webkit patch r135082 is fixed.
// (See https://bugs.webkit.org/show_bug.cgi?id=102110 for details)
// If failing, mark disabled and update http://crbug.com/92105.
IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, DISABLED_ExecuteScriptBasic) {
  SetupDelayedHostResolver();
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("executescript/basic")) << message_;
}

// Disable it until the regression caused by the webkit patch r135082 is fixed.
// (See https://bugs.webkit.org/show_bug.cgi?id=102110 for details)
// If failing, mark disabled and update http://crbug.com/92105.
IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, DISABLED_ExecuteScriptInFrame) {
  SetupDelayedHostResolver();
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("executescript/in_frame")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, ExecuteScriptPermissions) {
  SetupDelayedHostResolver();
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("executescript/permissions")) << message_;
}

// If failing, mark disabled and update http://crbug.com/84760.
IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, ExecuteScriptFileAfterClose) {
  host_resolver()->AddRule("b.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("executescript/file_after_close")) << message_;
}

// If crashing, mark disabled and update http://crbug.com/67774.
IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, ExecuteScriptFragmentNavigation) {
  ASSERT_TRUE(StartTestServer());
  const char* extension_name = "executescript/fragment";
  ASSERT_TRUE(RunExtensionTest(extension_name)) << message_;
}

IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, NavigationRaceExecuteScript) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  host_resolver()->AddRule("b.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionSubtest("executescript/navigation_race",
                                  "execute_script.html")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, NavigationRaceJavaScriptURL) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  host_resolver()->AddRule("b.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionSubtest("executescript/navigation_race",
                                  "javascript_url.html")) << message_;
}

// If failing, mark disabled and update http://crbug.com/92105.
IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, ExecuteScriptFrameAfterLoad) {
  SetupDelayedHostResolver();
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("executescript/frame_after_load")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, ExecuteScriptRunAt) {
  SetupDelayedHostResolver();
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("executescript/run_at")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExecuteScriptApiTest, ExecuteScriptCallback) {
  SetupDelayedHostResolver();
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("executescript/callback")) << message_;
}
