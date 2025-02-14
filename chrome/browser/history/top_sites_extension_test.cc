// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/browser/history/top_sites_extension_api.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"

namespace utils = extension_function_test_utils;

namespace {

class TopSitesExtensionTest : public InProcessBrowserTest {
 public:
  TopSitesExtensionTest() : top_sites_inited_(false), waiting_(false) {
  }

  void SetUpOnMainThread() {
    history::TopSites* top_sites = browser()->profile()->GetTopSites();

    // This may return async or sync. If sync, top_sites_inited_ will be true
    // before we get to the conditional below. Otherwise, we'll run a nested
    // message loop until the async callback.
    top_sites->GetMostVisitedURLs(
        base::Bind(&TopSitesExtensionTest::OnTopSitesAvailable, this));

    if (!top_sites_inited_) {
      waiting_ = true;
      MessageLoop::current()->Run();
    }

    // By this point, we know topsites has loaded. We can run the tests now.
  }

 private:
  void OnTopSitesAvailable(const history::MostVisitedURLList& data) {
    if (waiting_) {
      MessageLoop::current()->Quit();
      waiting_ = false;
    }
    top_sites_inited_ = true;
  }

  bool top_sites_inited_;
  bool waiting_;
};

}  // namespace

IN_PROC_BROWSER_TEST_F(TopSitesExtensionTest, GetTopSites) {
  scoped_refptr<GetTopSitesFunction> get_top_sites_function(
      new GetTopSitesFunction());
  // Without a callback the function will not generate a result.
  get_top_sites_function->set_has_callback(true);

  scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
      get_top_sites_function.get(), "[]", browser()));
  base::ListValue* list;
  ASSERT_TRUE(result->GetAsList(&list));
  EXPECT_GE(list->GetSize(), arraysize(history::kPrepopulatedPages));
}
