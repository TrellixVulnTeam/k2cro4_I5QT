// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import android.test.suitebuilder.annotation.MediumTest;

import org.chromium.android_webview.AwContents;
import org.chromium.android_webview.AndroidProtocolHandler;
import org.chromium.android_webview.ErrorCodeConversionHelper;
import org.chromium.base.test.util.Feature;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content.browser.test.util.TestCallbackHelperContainer;

import java.util.concurrent.TimeUnit;

/**
 * Tests for the ContentViewClient.onReceivedError() method.
 */
public class ClientOnReceivedErrorTest extends AndroidWebViewTestBase {

    private TestAwContentsClient mContentsClient;
    private AwContents mAwContents;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        mContentsClient = new TestAwContentsClient();
        final AwTestContainerView testContainerView =
                createAwTestContainerViewOnMainSync(mContentsClient);
        mAwContents = testContainerView.getAwContents();
    }

    @MediumTest
    @Feature({"AndroidWebView"})
    public void testOnReceivedErrorOnInvalidUrl() throws Throwable {
        TestCallbackHelperContainer.OnReceivedErrorHelper onReceivedErrorHelper =
                mContentsClient.getOnReceivedErrorHelper();

        String url = "http://man.id.be.really.surprised.if.this.address.existed.blah/";
        int onReceivedErrorCallCount = onReceivedErrorHelper.getCallCount();
        loadUrlAsync(mAwContents, url);

        // TODO(boliu): This is spuriously timing out on build bots but cannot
        // be reproduced locally. Trying a longer timeout value to see if it is
        // due to value too slow or some other issue. See crbug.com/152033.
        onReceivedErrorHelper.waitForCallback(onReceivedErrorCallCount,
                                              1 /* numberOfCallsToWaitFor */,
                                              30 /* timeout */,
                                              TimeUnit.SECONDS);
        assertEquals(ErrorCodeConversionHelper.ERROR_HOST_LOOKUP,
                onReceivedErrorHelper.getErrorCode());
        assertEquals(url, onReceivedErrorHelper.getFailingUrl());
        assertNotNull(onReceivedErrorHelper.getDescription());
    }

    @MediumTest
    @Feature({"AndroidWebView"})
    public void testOnReceivedErrorOnInvalidScheme() throws Throwable {
        TestCallbackHelperContainer.OnReceivedErrorHelper onReceivedErrorHelper =
                mContentsClient.getOnReceivedErrorHelper();

        String url = "foo://some/resource";
        int onReceivedErrorCallCount = onReceivedErrorHelper.getCallCount();
        loadUrlAsync(mAwContents, url);

        onReceivedErrorHelper.waitForCallback(onReceivedErrorCallCount);
        assertEquals(ErrorCodeConversionHelper.ERROR_UNSUPPORTED_SCHEME,
                onReceivedErrorHelper.getErrorCode());
        assertEquals(url, onReceivedErrorHelper.getFailingUrl());
        assertNotNull(onReceivedErrorHelper.getDescription());
    }

    @MediumTest
    @Feature({"AndroidWebView"})
    public void testNoErrorOnFailedSubresourceLoad() throws Throwable {
        TestCallbackHelperContainer.OnReceivedErrorHelper onReceivedErrorHelper =
                mContentsClient.getOnReceivedErrorHelper();
        TestCallbackHelperContainer.OnPageFinishedHelper onPageFinishedHelper =
                mContentsClient.getOnPageFinishedHelper();

        int currentCallCount = onPageFinishedHelper.getCallCount();
        loadDataAsync(mAwContents,
                      "<html><iframe src=\"http//invalid.url.co/\" /></html>",
                      "text/html",
                      false);

        onPageFinishedHelper.waitForCallback(currentCallCount);
        assertEquals(0, onReceivedErrorHelper.getCallCount());
    }

    @MediumTest
    @Feature({"AndroidWebView"})
    public void testNonExistentAssetUrl() throws Throwable {
        TestCallbackHelperContainer.OnReceivedErrorHelper onReceivedErrorHelper =
                mContentsClient.getOnReceivedErrorHelper();
        try {
            final String url = "file:///android_asset/does_not_exist.html";
            int onReceivedErrorCallCount = onReceivedErrorHelper.getCallCount();
            useTestResourceContext();
            loadUrlAsync(mAwContents, url);

            onReceivedErrorHelper.waitForCallback(onReceivedErrorCallCount);
            assertEquals(ErrorCodeConversionHelper.ERROR_UNKNOWN,
                         onReceivedErrorHelper.getErrorCode());
            assertEquals(url, onReceivedErrorHelper.getFailingUrl());
            assertNotNull(onReceivedErrorHelper.getDescription());
        } finally {
            resetResourceContext();
        }
    }

    @MediumTest
    @Feature({"AndroidWebView"})
    public void testNonExistentResourceUrl() throws Throwable {
        TestCallbackHelperContainer.OnReceivedErrorHelper onReceivedErrorHelper =
                mContentsClient.getOnReceivedErrorHelper();
        try {
            final String url = "file:///android_res/raw/does_not_exist.html";
            int onReceivedErrorCallCount = onReceivedErrorHelper.getCallCount();
            useTestResourceContext();
            loadUrlAsync(mAwContents, url);

            onReceivedErrorHelper.waitForCallback(onReceivedErrorCallCount);
            assertEquals(ErrorCodeConversionHelper.ERROR_UNKNOWN,
                         onReceivedErrorHelper.getErrorCode());
            assertEquals(url, onReceivedErrorHelper.getFailingUrl());
            assertNotNull(onReceivedErrorHelper.getDescription());
        } finally {
            resetResourceContext();
        }
    }

    /**
     * Configure the browser to load resources from the test harness instead of the browser
     * application.
     */
    private void useTestResourceContext() {
        AndroidProtocolHandler.setResourceContextForTesting(getInstrumentation().getContext());
    }

    /**
     * Configure the browser to load resources from the browser application.
     */
    private void resetResourceContext() {
        AndroidProtocolHandler.setResourceContextForTesting(null);
    }
}
