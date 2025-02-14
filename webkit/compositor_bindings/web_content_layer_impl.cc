// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web_content_layer_impl.h"

#include "SkMatrix44.h"
#include "base/command_line.h"
#include "cc/content_layer.h"
#include "cc/picture_layer.h"
#include "cc/switches.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebContentLayerClient.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebFloatPoint.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebFloatRect.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebRect.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebSize.h"

using namespace cc;

namespace WebKit {

static bool usingPictureLayer()
{
    return CommandLine::ForCurrentProcess()->HasSwitch(cc::switches::kImplSidePainting);
}

WebContentLayer* WebContentLayer::create(WebContentLayerClient* client)
{
    return new WebContentLayerImpl(client);
}

WebContentLayerImpl::WebContentLayerImpl(WebContentLayerClient* client)
    : m_client(client)
{
    if (usingPictureLayer())
        m_layer = make_scoped_ptr(new WebLayerImpl(PictureLayer::create(this)));
    else
        m_layer = make_scoped_ptr(new WebLayerImpl(ContentLayer::create(this)));
    m_layer->layer()->setIsDrawable(true);
}

WebContentLayerImpl::~WebContentLayerImpl()
{
    if (usingPictureLayer())
        static_cast<PictureLayer*>(m_layer->layer())->clearClient();
    else
        static_cast<ContentLayer*>(m_layer->layer())->clearClient();
}

WebLayer* WebContentLayerImpl::layer()
{
    return m_layer.get();
}

void WebContentLayerImpl::setDoubleSided(bool doubleSided)
{
    m_layer->layer()->setDoubleSided(doubleSided);
}

void WebContentLayerImpl::setBoundsContainPageScale(bool boundsContainPageScale)
{
    return m_layer->layer()->setBoundsContainPageScale(boundsContainPageScale);
}

bool WebContentLayerImpl::boundsContainPageScale() const
{
    return m_layer->layer()->boundsContainPageScale();
}

void WebContentLayerImpl::setAutomaticallyComputeRasterScale(bool automatic)
{
  m_layer->layer()->setAutomaticallyComputeRasterScale(automatic);
}

void WebContentLayerImpl::setUseLCDText(bool enable)
{
    m_layer->layer()->setUseLCDText(enable);
}

void WebContentLayerImpl::setDrawCheckerboardForMissingTiles(bool enable)
{
    m_layer->layer()->setDrawCheckerboardForMissingTiles(enable);
}


void WebContentLayerImpl::paintContents(SkCanvas* canvas, const gfx::Rect& clip, gfx::RectF& opaque)
{
    if (!m_client)
        return;
    WebFloatRect webOpaque;
    m_client->paintContents(canvas,
                            clip,
#if WEBCONTENTLAYERCLIENT_HAS_CANPAINTLCDTEXT
                            m_layer->layer()->useLCDText(),
#endif  // WEBCONTENTLAYERCLIENT_HAS_CANPAINTLCDTEXT
                            webOpaque);
    opaque = webOpaque;
}

} // namespace WebKit
