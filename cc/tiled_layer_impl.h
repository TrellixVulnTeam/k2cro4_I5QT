// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TILED_LAYER_IMPL_H_
#define CC_TILED_LAYER_IMPL_H_

#include "cc/cc_export.h"
#include "cc/layer_impl.h"
#include <public/WebTransformationMatrix.h>

namespace cc {

class LayerTilingData;
class DrawableTile;

class CC_EXPORT TiledLayerImpl : public LayerImpl {
public:
    static scoped_ptr<TiledLayerImpl> create(int id)
    {
        return make_scoped_ptr(new TiledLayerImpl(id));
    }
    virtual ~TiledLayerImpl();

    virtual void appendQuads(QuadSink&, AppendQuadsData&) OVERRIDE;

    virtual ResourceProvider::ResourceId contentsResourceId() const OVERRIDE;

    virtual void dumpLayerProperties(std::string*, int indent) const OVERRIDE;

    void setSkipsDraw(bool skipsDraw) { m_skipsDraw = skipsDraw; }
    void setTilingData(const LayerTilingData& tiler);
    void pushTileProperties(int, int, ResourceProvider::ResourceId, const gfx::Rect& opaqueRect, bool contentsSwizzled);
    void pushInvalidTile(int, int);

    virtual Region visibleContentOpaqueRegion() const OVERRIDE;
    virtual void didLoseContext() OVERRIDE;

protected:
    explicit TiledLayerImpl(int id);
    // Exposed for testing.
    bool hasTileAt(int, int) const;
    bool hasResourceIdForTileAt(int, int) const;

    virtual void getDebugBorderProperties(SkColor*, float* width) const OVERRIDE;

private:

    virtual const char* layerTypeAsString() const OVERRIDE;

    DrawableTile* tileAt(int, int) const;
    DrawableTile* createTile(int, int);

    bool m_skipsDraw;

    scoped_ptr<LayerTilingData> m_tiler;
};

}

#endif  // CC_TILED_LAYER_IMPL_H_
