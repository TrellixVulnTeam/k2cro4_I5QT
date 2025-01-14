// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_HEADS_UP_DISPLAY_LAYER_H_
#define CC_HEADS_UP_DISPLAY_LAYER_H_

#include "base/memory/scoped_ptr.h"
#include "cc/font_atlas.h"
#include "cc/layer.h"

namespace cc {

class HeadsUpDisplayLayer : public Layer {
public:
    static scoped_refptr<HeadsUpDisplayLayer> create();

    virtual void update(ResourceUpdateQueue&, const OcclusionTracker*, RenderingStats&) OVERRIDE;
    virtual bool drawsContent() const OVERRIDE;

    void setFontAtlas(scoped_ptr<FontAtlas>);
    void setShowFPSCounter(bool);

    virtual scoped_ptr<LayerImpl> createLayerImpl() OVERRIDE;
    virtual void pushPropertiesTo(LayerImpl*) OVERRIDE;

protected:
    HeadsUpDisplayLayer();

private:
    virtual ~HeadsUpDisplayLayer();

    scoped_ptr<FontAtlas> m_fontAtlas;
    bool m_showFPSCounter;
};

}  // namespace cc

#endif  // CC_HEADS_UP_DISPLAY_LAYER_H_
