// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/layout_browsertest.h"

namespace content {

class IndexedDBLayoutTest : public InProcessBrowserLayoutTest {
 public:
  IndexedDBLayoutTest() : InProcessBrowserLayoutTest(
      FilePath(), FilePath().AppendASCII("storage").AppendASCII("indexeddb")) {
  }

  void RunLayoutTests(const char* file_names[]) {
    for (size_t i = 0; file_names[i]; i++)
      RunLayoutTest(file_names[i]);
  }
};

namespace {

static const char* kBasicTests[] = {
  "basics.html",
  "basics-shared-workers.html",
  // Failing on Precise bot (crbug.com/145592).
  // "basics-workers.html",
  // Failing on all platforms (crbug.com/160961).
  // "database-basics.html",
  "factory-basics.html",
  "index-basics.html",
  "objectstore-basics.html",
  NULL
};

static const char* kComplexTests[] = {
  "prefetch-bugfix-108071.html",
  // Flaky: http://crbug.com/123685
  // "pending-version-change-stuck-works-with-terminate.html",
  "pending-version-change-on-exit.html",
  NULL
};

static const char* kIndexTests[] = {
  "deleteIndex.html",
  // Flaky: http://crbug.com/123685
  // "index-basics-workers.html",
  "index-count.html",
  "index-cursor.html",  // Locally takes ~6s compared to <1 for the others.
  "index-get-key-argument-required.html",
  "index-multientry.html",
  "index-population.html",
  "index-unique.html",
  NULL
};

static const char* kKeyTests[] = {
  "key-generator.html",
  "keypath-basics.html",
  "keypath-edges.html",
  "keypath-fetch-key.html",
  "keyrange.html",
  "keyrange-required-arguments.html",
  "key-sort-order-across-types.html",
  "key-sort-order-date.html",
  // Flaky: http://crbug.com/159158
  // "key-type-array.html",
  "key-type-infinity.html",
  "invalid-keys.html",
  NULL
};

static const char* kTransactionTests[] = {
  "transaction-abort.html",
  "transaction-complete-with-js-recursion-cross-frame.html",
  "transaction-complete-with-js-recursion.html",
  "transaction-complete-workers.html",
  "transaction-after-close.html",
  "transaction-and-objectstore-calls.html",
  "transaction-basics.html",
  "transaction-crash-on-abort.html",
  "transaction-event-propagation.html",
  "transaction-read-only.html",
  "transaction-rollback.html",
  "transaction-storeNames-required.html",
  NULL
};

static const char* kRegressionTests[] = {
  "dont-commit-on-blocked.html",
  NULL
};

const char* kIntVersionTests[] = {
  "intversion-abort-in-initial-upgradeneeded.html",
  // Needs to be renamed after https://bugs.webkit.org/show_bug.cgi?id=102318
  // lands and is rolled into chromium.
//  "intversion-and-setversion.html",
  "intversion-blocked.html",
  "intversion-close-between-events.html",
  "intversion-close-in-oncomplete.html",
  "intversion-close-in-upgradeneeded.html",
  "intversion-delete-in-upgradeneeded.html",
  // "intversion-gated-on-delete.html", // behaves slightly differently in DRT
  "intversion-long-queue.html",
  "intversion-omit-parameter.html",
  "intversion-open-with-version.html",
  NULL
};

}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, BasicTests) {
  RunLayoutTests(kBasicTests);
}


// Started failing after WebKit roll: http://crbug.com/162204
IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, DISABLED_ComplexTests) {
  RunLayoutTests(kComplexTests);
}

// TODO(dgrogan): times out flakily. http://crbug.com/153064
IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, DISABLED_IndexTests) {
  RunLayoutTests(kIndexTests);
}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, KeyTests) {
  RunLayoutTests(kKeyTests);
}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, TransactionTests) {
  RunLayoutTests(kTransactionTests);
}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, IntVersionTests) {
  RunLayoutTests(kIntVersionTests);
}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, RegressionTests) {
  RunLayoutTests(kRegressionTests);
}

}  // namespace content
