// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_SOLID_COLOR_LAYER_IMPL_H_
#define CC_SOLID_COLOR_LAYER_IMPL_H_

#include "cc/cc_export.h"
#include "cc/layer_impl.h"
#include <public/WebTransformationMatrix.h>

namespace cc {

class CC_EXPORT SolidColorLayerImpl : public LayerImpl {
public:
    static scoped_ptr<SolidColorLayerImpl> create(int id)
    {
        return make_scoped_ptr(new SolidColorLayerImpl(id));
    }
    virtual ~SolidColorLayerImpl();

    virtual void appendQuads(QuadSink&, AppendQuadsData&) OVERRIDE;

protected:
    explicit SolidColorLayerImpl(int id);

private:
    virtual const char* layerTypeAsString() const OVERRIDE;

    const int m_tileSize;
};

}

#endif  // CC_SOLID_COLOR_LAYER_IMPL_H_
