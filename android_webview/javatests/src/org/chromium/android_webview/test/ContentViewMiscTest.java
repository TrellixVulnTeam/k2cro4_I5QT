// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.net.Proxy;
import android.test.mock.MockContext;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.android_webview.AwContents;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.base.ThreadUtils;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content.browser.ContentViewStatics;
import org.chromium.net.ProxyChangeListener;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.Callable;

/**
 *  Tests for ContentView methods that don't fall into any other category.
 */
public class ContentViewMiscTest extends AndroidWebViewTestBase {

    private TestAwContentsClient mContentsClient;
    private AwContents mAwContents;
    private ContentViewCore mContentViewCore;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        mContentsClient = new TestAwContentsClient();
        final AwTestContainerView testContainerView =
                createAwTestContainerViewOnMainSync(mContentsClient);
        mAwContents = testContainerView.getAwContents();
        mContentViewCore = testContainerView.getContentViewCore();
    }

    @SmallTest
    @Feature({"AndroidWebView"})
    public void testFlingScroll() throws Throwable {
        StringBuffer testPage = new StringBuffer().append("data:text/html;utf-8,")
                .append("<html><head><style>body { width: 5000px; height: 5000px; }</head><body>")
                .append("</body></html>");

        // Test flinging in the y axis
        loadUrlSync(mAwContents , mContentsClient.getOnPageFinishedHelper(),
                testPage.toString());
        assertEquals(0, mContentViewCore.getNativeScrollYForTest());
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mContentViewCore.flingScroll(0, 2000);
            }
        });
        Thread.sleep(1000);
        assertNotSame(0, mContentViewCore.getNativeScrollYForTest());

        // Test flinging in the x axis
        assertEquals(0, mContentViewCore.getNativeScrollXForTest());
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mContentViewCore.flingScroll(2000, 0);
            }
        });
        Thread.sleep(1000);
        assertNotSame(0, mContentViewCore.getNativeScrollXForTest());
    }

    @SmallTest
    @Feature({"AndroidWebView"})
    public void testFindAddress() {
        assertNull(ContentViewStatics.findAddress("This is some random text"));

        String googleAddr = "1600 Amphitheatre Pkwy, Mountain View, CA 94043";
        assertEquals(googleAddr, ContentViewStatics.findAddress(googleAddr));
    }

    @SmallTest
    @Feature({"AndroidWebView"})
    public void testEnableDisablePlatformNotifications() {

        // Set up mock contexts to use with the listener
        final AtomicReference<BroadcastReceiver> receiverRef =
                new AtomicReference<BroadcastReceiver>();
        final MockContext appContext = new MockContext() {
            @Override
            public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
                receiverRef.set(receiver);
                return null;
            }
        };
        final MockContext context = new MockContext() {
            @Override
            public Context getApplicationContext() {
                return appContext;
            }
        };

        // Set up a delegate so we know when native code is about to get
        // informed of a proxy change.
        final AtomicBoolean proxyChanged = new AtomicBoolean();
        final ProxyChangeListener.Delegate delegate = new ProxyChangeListener.Delegate() {
            @Override
            public void proxySettingsChanged() {
                proxyChanged.set(true);
            }
        };
        Intent intent = new Intent();
        intent.setAction(Proxy.PROXY_CHANGE_ACTION);

        // Create the listener that's going to be used for the test
        ProxyChangeListener listener = ProxyChangeListener.create(context);
        listener.setDelegateForTesting(delegate);
        listener.start(0);

        // Start the actual tests

        // Make sure everything works by default
        proxyChanged.set(false);
        receiverRef.get().onReceive(context, intent);
        assertEquals(true, proxyChanged.get());

        // Now disable platform notifications and make sure we don't notify
        // native code.
        proxyChanged.set(false);
        ContentViewStatics.disablePlatformNotifications();
        receiverRef.get().onReceive(context, intent);
        assertEquals(false, proxyChanged.get());

        // Now re-enable notifications to make sure they work again.
        ContentViewStatics.enablePlatformNotifications();
        receiverRef.get().onReceive(context, intent);
        assertEquals(true, proxyChanged.get());
    }

    /**
     * @SmallTest
     * @Feature({"AndroidWebView"})
     * Bug 6931901
     */
    @DisabledTest
    public void testSetGetBackgroundColor() throws Throwable {
        loadUrlSync(mAwContents , mContentsClient.getOnPageFinishedHelper(), "about:blank");
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mContentViewCore.setBackgroundColor(Color.MAGENTA);
            }
        });
        int backgroundColor = ThreadUtils.runOnUiThreadBlocking(new Callable<Integer>() {
            @Override
            public Integer call() {
                Bitmap map = mContentViewCore.getBitmap(1, 1);
                return map.getPixel(0,0);
            }
        });
        assertEquals(Color.MAGENTA, backgroundColor);
    }
}
