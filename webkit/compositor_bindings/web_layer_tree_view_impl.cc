// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web_layer_tree_view_impl.h"

#include "cc/font_atlas.h"
#include "cc/input_handler.h"
#include "cc/layer.h"
#include "cc/layer_tree_host.h"
#include "cc/thread.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebGraphicsContext3D.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebInputHandler.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebLayer.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebLayerTreeViewClient.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebLayerTreeView.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebRenderingStats.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebSize.h"
#include "web_layer_impl.h"
#include "web_to_ccinput_handler_adapter.h"

using namespace cc;

namespace WebKit {

WebLayerTreeViewImpl::WebLayerTreeViewImpl(WebLayerTreeViewClient* client)
    : m_client(client)
{
}

WebLayerTreeViewImpl::~WebLayerTreeViewImpl()
{
}

bool WebLayerTreeViewImpl::initialize(const WebLayerTreeView::Settings& webSettings, scoped_ptr<Thread> implThread)
{
    LayerTreeSettings settings;
    settings.acceleratePainting = webSettings.acceleratePainting;
    settings.showDebugBorders = webSettings.showDebugBorders;
    settings.showPlatformLayerTree = webSettings.showPlatformLayerTree;
    settings.showPaintRects = webSettings.showPaintRects;
    settings.renderVSyncEnabled = webSettings.renderVSyncEnabled;
    settings.perTilePaintingEnabled = webSettings.perTilePaintingEnabled;
    settings.acceleratedAnimationEnabled = webSettings.acceleratedAnimationEnabled;
    settings.pageScalePinchZoomEnabled = webSettings.pageScalePinchZoomEnabled;
    settings.refreshRate = webSettings.refreshRate;
    settings.defaultTileSize = webSettings.defaultTileSize;
    settings.maxUntiledLayerSize = webSettings.maxUntiledLayerSize;
    m_layerTreeHost = LayerTreeHost::create(this, settings, implThread.Pass());
    if (!m_layerTreeHost.get())
        return false;

    if (webSettings.showFPSCounter)
        setShowFPSCounter(true);
    return true;
}

void WebLayerTreeViewImpl::setSurfaceReady()
{
    m_layerTreeHost->setSurfaceReady();
}

void WebLayerTreeViewImpl::setRootLayer(const WebLayer& root)
{
    m_layerTreeHost->setRootLayer(static_cast<const WebLayerImpl*>(&root)->layer());
}

void WebLayerTreeViewImpl::clearRootLayer()
{
    m_layerTreeHost->setRootLayer(scoped_refptr<Layer>());
}

void WebLayerTreeViewImpl::setViewportSize(const WebSize& layoutViewportSize, const WebSize& deviceViewportSize)
{
    if (!deviceViewportSize.isEmpty())
        m_layerTreeHost->setViewportSize(layoutViewportSize, deviceViewportSize);
    else
        m_layerTreeHost->setViewportSize(layoutViewportSize, layoutViewportSize);
}

WebSize WebLayerTreeViewImpl::layoutViewportSize() const
{
    return m_layerTreeHost->layoutViewportSize();
}

WebSize WebLayerTreeViewImpl::deviceViewportSize() const
{
    return m_layerTreeHost->deviceViewportSize();
}

WebFloatPoint WebLayerTreeViewImpl::adjustEventPointForPinchZoom(const WebFloatPoint& point) const
{
    return m_layerTreeHost->adjustEventPointForPinchZoom(point);
}

void WebLayerTreeViewImpl::setDeviceScaleFactor(const float deviceScaleFactor)
{
    m_layerTreeHost->setDeviceScaleFactor(deviceScaleFactor);
}

float WebLayerTreeViewImpl::deviceScaleFactor() const
{
    return m_layerTreeHost->deviceScaleFactor();
}

void WebLayerTreeViewImpl::setBackgroundColor(WebColor color)
{
    m_layerTreeHost->setBackgroundColor(color);
}

void WebLayerTreeViewImpl::setHasTransparentBackground(bool transparent)
{
    m_layerTreeHost->setHasTransparentBackground(transparent);
}

void WebLayerTreeViewImpl::setVisible(bool visible)
{
    m_layerTreeHost->setVisible(visible);
}

void WebLayerTreeViewImpl::setPageScaleFactorAndLimits(float pageScaleFactor, float minimum, float maximum)
{
    m_layerTreeHost->setPageScaleFactorAndLimits(pageScaleFactor, minimum, maximum);
}

void WebLayerTreeViewImpl::startPageScaleAnimation(const WebPoint& scroll, bool useAnchor, float newPageScale, double durationSec)
{
    base::TimeDelta duration = base::TimeDelta::FromMicroseconds(durationSec * base::Time::kMicrosecondsPerSecond);
    m_layerTreeHost->startPageScaleAnimation(gfx::Vector2d(scroll.x, scroll.y), useAnchor, newPageScale, duration);
}

void WebLayerTreeViewImpl::setNeedsAnimate()
{
    m_layerTreeHost->setNeedsAnimate();
}

void WebLayerTreeViewImpl::setNeedsRedraw()
{
    m_layerTreeHost->setNeedsRedraw();
}

bool WebLayerTreeViewImpl::commitRequested() const
{
    return m_layerTreeHost->commitRequested();
}

void WebLayerTreeViewImpl::composite()
{
    m_layerTreeHost->composite();
}

void WebLayerTreeViewImpl::updateAnimations(double frameBeginTimeSeconds)
{
    base::TimeTicks frameBeginTime = base::TimeTicks::FromInternalValue(frameBeginTimeSeconds * base::Time::kMicrosecondsPerSecond);
    m_layerTreeHost->updateAnimations(frameBeginTime);
}

bool WebLayerTreeViewImpl::compositeAndReadback(void *pixels, const WebRect& rect)
{
    return m_layerTreeHost->compositeAndReadback(pixels, rect);
}

void WebLayerTreeViewImpl::finishAllRendering()
{
    m_layerTreeHost->finishAllRendering();
}

void WebLayerTreeViewImpl::setDeferCommits(bool deferCommits)
{
    m_layerTreeHost->setDeferCommits(deferCommits);
}

void WebLayerTreeViewImpl::renderingStats(WebRenderingStats& stats) const
{
    RenderingStats ccStats;
    m_layerTreeHost->renderingStats(&ccStats);

    stats.numAnimationFrames = ccStats.numAnimationFrames;
    stats.numFramesSentToScreen = ccStats.numFramesSentToScreen;
    stats.droppedFrameCount = ccStats.droppedFrameCount;
    stats.totalPaintTimeInSeconds = ccStats.totalPaintTimeInSeconds;
    stats.totalRasterizeTimeInSeconds = ccStats.totalRasterizeTimeInSeconds;
    stats.totalCommitTimeInSeconds = ccStats.totalCommitTimeInSeconds;
    stats.totalCommitCount = ccStats.totalCommitCount;
    stats.totalPixelsPainted = ccStats.totalPixelsPainted;
    stats.totalPixelsRasterized = ccStats.totalPixelsRasterized;
    stats.numImplThreadScrolls = ccStats.numImplThreadScrolls;
    stats.numMainThreadScrolls = ccStats.numMainThreadScrolls;
}

void WebLayerTreeViewImpl::setShowFPSCounter(bool show)
{
    m_layerTreeHost->setShowFPSCounter(show);
}

void WebLayerTreeViewImpl::setFontAtlas(SkBitmap bitmap, WebRect asciiToWebRectTable[128], int fontHeight) {
    setFontAtlas(asciiToWebRectTable, bitmap, fontHeight);
}

void WebLayerTreeViewImpl::setFontAtlas(WebRect asciiToWebRectTable[128], const SkBitmap& bitmap, int fontHeight)
{
    gfx::Rect asciiToRectTable[128];
    for (int i = 0; i < 128; ++i)
        asciiToRectTable[i] = asciiToWebRectTable[i];
    scoped_ptr<FontAtlas> fontAtlas = FontAtlas::create(bitmap, asciiToRectTable, fontHeight);
    m_layerTreeHost->setFontAtlas(fontAtlas.Pass());
}

void WebLayerTreeViewImpl::loseCompositorContext(int numTimes)
{
    m_layerTreeHost->loseContext(numTimes);
}

void WebLayerTreeViewImpl::willBeginFrame()
{
    m_client->willBeginFrame();
}

void WebLayerTreeViewImpl::didBeginFrame()
{
    m_client->didBeginFrame();
}

void WebLayerTreeViewImpl::animate(double monotonicFrameBeginTime)
{
    m_client->updateAnimations(monotonicFrameBeginTime);
}

void WebLayerTreeViewImpl::layout()
{
    m_client->layout();
}

void WebLayerTreeViewImpl::applyScrollAndScale(gfx::Vector2d scrollDelta, float pageScale)
{
    m_client->applyScrollAndScale(scrollDelta, pageScale);
}

scoped_ptr<WebCompositorOutputSurface> WebLayerTreeViewImpl::createOutputSurface()
{
    return scoped_ptr<WebCompositorOutputSurface>(m_client->createOutputSurface());
}

void WebLayerTreeViewImpl::didRecreateOutputSurface(bool success)
{
    m_client->didRecreateOutputSurface(success);
}

scoped_ptr<InputHandler> WebLayerTreeViewImpl::createInputHandler()
{
    scoped_ptr<InputHandler> ret;
    scoped_ptr<WebInputHandler> handler(m_client->createInputHandler());
    if (handler)
        ret = WebToCCInputHandlerAdapter::create(handler.Pass());
    return ret.Pass();
}

void WebLayerTreeViewImpl::willCommit()
{
    m_client->willCommit();
}

void WebLayerTreeViewImpl::didCommit()
{
    m_client->didCommit();
}

void WebLayerTreeViewImpl::didCommitAndDrawFrame()
{
    m_client->didCommitAndDrawFrame();
}

void WebLayerTreeViewImpl::didCompleteSwapBuffers()
{
    m_client->didCompleteSwapBuffers();
}

void WebLayerTreeViewImpl::scheduleComposite()
{
    m_client->scheduleComposite();
}

} // namespace WebKit
