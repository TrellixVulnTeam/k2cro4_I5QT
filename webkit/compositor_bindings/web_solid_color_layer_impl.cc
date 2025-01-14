// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web_solid_color_layer_impl.h"

#include "cc/solid_color_layer.h"
#include "web_layer_impl.h"

using cc::SolidColorLayer;

namespace WebKit {

WebSolidColorLayer* WebSolidColorLayer::create()
{
    return new WebSolidColorLayerImpl();
}

WebSolidColorLayerImpl::WebSolidColorLayerImpl()
    : m_layer(new WebLayerImpl(SolidColorLayer::create()))
{
    m_layer->layer()->setIsDrawable(true);
}

WebSolidColorLayerImpl::~WebSolidColorLayerImpl()
{
}

WebLayer* WebSolidColorLayerImpl::layer()
{
    return m_layer.get();
}

void WebSolidColorLayerImpl::setBackgroundColor(WebColor color)
{
    m_layer->setBackgroundColor(color);
}

} // namespace WebKit
