// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYER_TREE_HOST_IMPL_H_
#define CC_LAYER_TREE_HOST_IMPL_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/time.h"
#include "cc/animation_events.h"
#include "cc/cc_export.h"
#include "cc/input_handler.h"
#include "cc/layer_sorter.h"
#include "cc/render_pass.h"
#include "cc/render_pass_sink.h"
#include "cc/renderer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/rect.h"
#include <public/WebCompositorOutputSurfaceClient.h>

namespace cc {

class CompletionEvent;
class DebugRectHistory;
class FrameRateCounter;
class HeadsUpDisplayLayerImpl;
class LayerImpl;
class LayerTreeHostImplTimeSourceAdapter;
class PageScaleAnimation;
class RenderPassDrawQuad;
class ResourceProvider;
struct RendererCapabilities;
struct RenderingStats;

enum WhichTree {
  // Note: these must be 0 and 1 because we index with them in various places,
  // e.g. in Tile::priority_.
  ACTIVE_TREE = 0,
  PENDING_TREE = 1
};

// LayerTreeHost->Proxy callback interface.
class LayerTreeHostImplClient {
public:
    virtual void didLoseContextOnImplThread() = 0;
    virtual void onSwapBuffersCompleteOnImplThread() = 0;
    virtual void onVSyncParametersChanged(base::TimeTicks timebase, base::TimeDelta interval) = 0;
    virtual void onCanDrawStateChanged(bool canDraw) = 0;
    virtual void setNeedsRedrawOnImplThread() = 0;
    virtual void setNeedsCommitOnImplThread() = 0;
    virtual void postAnimationEventsToMainThreadOnImplThread(scoped_ptr<AnimationEventsVector>, base::Time wallClockTime) = 0;
    // Returns true if resources were deleted by this call.
    virtual bool reduceContentsTextureMemoryOnImplThread(size_t limitBytes, int priorityCutoff) = 0;
    virtual void sendManagedMemoryStats() = 0;
};

// PinchZoomViewport models the bounds and offset of the viewport that is used during a pinch-zoom operation.
// It tracks the layout-space dimensions of the viewport before any applied scale, and then tracks the layout-space
// coordinates of the viewport respecting the pinch settings.
class PinchZoomViewport {
public:
    PinchZoomViewport();

    float totalPageScaleFactor() const;

    void setPageScaleFactor(float factor) { m_pageScaleFactor = factor; }
    float pageScaleFactor() const { return m_pageScaleFactor; }

    void setPageScaleDelta(float delta);
    float pageScaleDelta() const  { return m_pageScaleDelta; }

    float minPageScaleFactor() const { return m_minPageScaleFactor; }
    float maxPageScaleFactor() const { return m_maxPageScaleFactor; }

    void setSentPageScaleDelta(float delta) { m_sentPageScaleDelta = delta; }
    float sentPageScaleDelta() const { return m_sentPageScaleDelta; }

    void setDeviceScaleFactor(float factor) { m_deviceScaleFactor = factor; }
    float deviceScaleFactor() const { return m_deviceScaleFactor; }

    // Returns true if the passed parameters were different from those previously
    // cached.
    bool setPageScaleFactorAndLimits(float pageScaleFactor,
                                     float minPageScaleFactor,
                                     float maxPageScaleFactor);

    // Returns the bounds and offset of the scaled and translated viewport to use for pinch-zoom.
    gfx::RectF bounds() const;
    const gfx::Vector2dF& zoomedViewportOffset() const { return m_zoomedViewportOffset; }

    void setLayoutViewportSize(const gfx::SizeF& size) { m_layoutViewportSize = size; }

    // Apply the scroll offset in layout space to the offset of the pinch-zoom viewport. The viewport cannot be
    // scrolled outside of the layout viewport bounds. Returns the component of the scroll that is un-applied due to
    // this constraint.
    gfx::Vector2dF applyScroll(const gfx::Vector2dF&);

    // The implTransform goes from the origin of the unzoomedDeviceViewport to the
    // origin of the zoomedDeviceViewport.
    //
    // implTransform = S[pageScale] * Tr[-zoomedDeviceViewportOffset]
    WebKit::WebTransformationMatrix implTransform(bool pageScalePinchZoomEnabled) const;

private:
    float m_pageScaleFactor;
    float m_pageScaleDelta;
    float m_sentPageScaleDelta;
    float m_maxPageScaleFactor;
    float m_minPageScaleFactor;
    float m_deviceScaleFactor;

    gfx::Vector2dF m_zoomedViewportOffset;
    gfx::SizeF m_layoutViewportSize;
};

// LayerTreeHostImpl owns the LayerImpl tree as well as associated rendering state
class CC_EXPORT LayerTreeHostImpl : public InputHandlerClient,
                                    public RendererClient,
                                    public NON_EXPORTED_BASE(WebKit::WebCompositorOutputSurfaceClient) {
    typedef std::vector<LayerImpl*> LayerList;

public:
    static scoped_ptr<LayerTreeHostImpl> create(const LayerTreeSettings&, LayerTreeHostImplClient*, Proxy*);
    virtual ~LayerTreeHostImpl();

    // InputHandlerClient implementation
    virtual InputHandlerClient::ScrollStatus scrollBegin(gfx::Point, InputHandlerClient::ScrollInputType) OVERRIDE;
    virtual bool scrollBy(const gfx::Point&, const gfx::Vector2d&) OVERRIDE;
    virtual void scrollEnd() OVERRIDE;
    virtual void pinchGestureBegin() OVERRIDE;
    virtual void pinchGestureUpdate(float, gfx::Point) OVERRIDE;
    virtual void pinchGestureEnd() OVERRIDE;
    virtual void startPageScaleAnimation(gfx::Vector2d targetOffset, bool anchorPoint, float pageScale, base::TimeTicks startTime, base::TimeDelta duration) OVERRIDE;
    virtual void scheduleAnimation() OVERRIDE;
    virtual bool haveTouchEventHandlersAt(const gfx::Point&) OVERRIDE;

    struct CC_EXPORT FrameData : public RenderPassSink {
        FrameData();
        ~FrameData();

        std::vector<gfx::Rect> occludingScreenSpaceRects;
        std::vector<gfx::Rect> nonOccludingScreenSpaceRects;
        RenderPassList renderPasses;
        RenderPassIdHashMap renderPassesById;
        LayerList* renderSurfaceLayerList;
        LayerList willDrawLayers;

        // RenderPassSink implementation.
        virtual void appendRenderPass(scoped_ptr<RenderPass>) OVERRIDE;
    };

    // Virtual for testing.
    virtual void beginCommit();
    virtual void commitComplete();
    virtual void animate(base::TimeTicks monotonicTime, base::Time wallClockTime);

    // Returns false if problems occured preparing the frame, and we should try
    // to avoid displaying the frame. If prepareToDraw is called,
    // didDrawAllLayers must also be called, regardless of whether drawLayers is
    // called between the two.
    virtual bool prepareToDraw(FrameData&);
    virtual void drawLayers(const FrameData&);
    // Must be called if and only if prepareToDraw was called.
    void didDrawAllLayers(const FrameData&);

    // RendererClient implementation
    virtual const gfx::Size& deviceViewportSize() const OVERRIDE;
    virtual const LayerTreeSettings& settings() const OVERRIDE;
    virtual void didLoseContext() OVERRIDE;
    virtual void onSwapBuffersComplete() OVERRIDE;
    virtual void setFullRootLayerDamage() OVERRIDE;
    virtual void setManagedMemoryPolicy(const ManagedMemoryPolicy& policy) OVERRIDE;
    virtual void enforceManagedMemoryPolicy(const ManagedMemoryPolicy& policy) OVERRIDE;
    virtual bool hasImplThread() const OVERRIDE;

    // WebCompositorOutputSurfaceClient implementation.
    virtual void onVSyncParametersChanged(double monotonicTimebase, double intervalInSeconds) OVERRIDE;

    // Implementation
    bool canDraw();
    GraphicsContext* context() const;

    std::string layerTreeAsText() const;

    void finishAllRendering();
    int sourceAnimationFrameNumber() const;

    bool initializeRenderer(scoped_ptr<GraphicsContext>);
    bool isContextLost();
    Renderer* renderer() { return m_renderer.get(); }
    const RendererCapabilities& rendererCapabilities() const;

    bool swapBuffers();

    void readback(void* pixels, const gfx::Rect&);

    void setRootLayer(scoped_ptr<LayerImpl>);
    LayerImpl* rootLayer() { return m_rootLayerImpl.get(); }

    void setHudLayer(HeadsUpDisplayLayerImpl* layerImpl) { m_hudLayerImpl = layerImpl; }
    HeadsUpDisplayLayerImpl* hudLayer() { return m_hudLayerImpl; }

    // Release ownership of the current layer tree and replace it with an empty
    // tree. Returns the root layer of the detached tree.
    scoped_ptr<LayerImpl> detachLayerTree();

    LayerImpl* rootScrollLayer() const { return m_rootScrollLayerImpl; }

    bool visible() const { return m_visible; }
    void setVisible(bool);

    int sourceFrameNumber() const { return m_sourceFrameNumber; }
    void setSourceFrameNumber(int frameNumber) { m_sourceFrameNumber = frameNumber; }

    bool contentsTexturesPurged() const { return m_contentsTexturesPurged; }
    void setContentsTexturesPurged();
    void resetContentsTexturesPurged();
    size_t memoryAllocationLimitBytes() const { return m_managedMemoryPolicy.bytesLimitWhenVisible; }

    void setViewportSize(const gfx::Size& layoutViewportSize, const gfx::Size& deviceViewportSize);
    const gfx::Size& layoutViewportSize() const { return m_layoutViewportSize; }

    float deviceScaleFactor() const { return m_deviceScaleFactor; }
    void setDeviceScaleFactor(float);

    float pageScaleFactor() const;
    void setPageScaleFactorAndLimits(float pageScaleFactor, float minPageScaleFactor, float maxPageScaleFactor);

    scoped_ptr<ScrollAndScaleSet> processScrollDeltas();
    WebKit::WebTransformationMatrix implTransform() const;

    void startPageScaleAnimation(gfx::Vector2d targetOffset, bool useAnchor, float scale, base::TimeDelta duration);

    SkColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(SkColor color) { m_backgroundColor = color; }

    bool hasTransparentBackground() const { return m_hasTransparentBackground; }
    void setHasTransparentBackground(bool transparent) { m_hasTransparentBackground = transparent; }

    bool needsAnimateLayers() const { return m_needsAnimateLayers; }
    void setNeedsAnimateLayers() { m_needsAnimateLayers = true; }

    void setNeedsRedraw();

    void renderingStats(RenderingStats*) const;

    void updateRootScrollLayerImplTransform();

    FrameRateCounter* fpsCounter() const { return m_fpsCounter.get(); }
    DebugRectHistory* debugRectHistory() const { return m_debugRectHistory.get(); }
    ResourceProvider* resourceProvider() const { return m_resourceProvider.get(); }
    Proxy* proxy() const { return m_proxy; }

    class CC_EXPORT CullRenderPassesWithCachedTextures {
    public:
        bool shouldRemoveRenderPass(const RenderPassDrawQuad&, const FrameData&) const;

        // Iterates from the root first, in order to remove the surfaces closest
        // to the root with cached textures, and all surfaces that draw into
        // them.
        size_t renderPassListBegin(const RenderPassList& list) const { return list.size() - 1; }
        size_t renderPassListEnd(const RenderPassList&) const { return 0 - 1; }
        size_t renderPassListNext(size_t it) const { return it - 1; }

        CullRenderPassesWithCachedTextures(Renderer& renderer) : m_renderer(renderer) { }
    private:
        Renderer& m_renderer;
    };

    class CC_EXPORT CullRenderPassesWithNoQuads {
    public:
        bool shouldRemoveRenderPass(const RenderPassDrawQuad&, const FrameData&) const;

        // Iterates in draw order, so that when a surface is removed, and its
        // target becomes empty, then its target can be removed also.
        size_t renderPassListBegin(const RenderPassList&) const { return 0; }
        size_t renderPassListEnd(const RenderPassList& list) const { return list.size(); }
        size_t renderPassListNext(size_t it) const { return it + 1; }
    };

    template<typename RenderPassCuller>
    static void removeRenderPasses(RenderPassCuller, FrameData&);

protected:
    LayerTreeHostImpl(const LayerTreeSettings&, LayerTreeHostImplClient*, Proxy*);

    void animatePageScale(base::TimeTicks monotonicTime);
    void animateScrollbars(base::TimeTicks monotonicTime);

    // Exposed for testing.
    void calculateRenderSurfaceLayerList(LayerList&);

    // Virtual for testing.
    virtual void animateLayers(base::TimeTicks monotonicTime, base::Time wallClockTime);

    // Virtual for testing.
    virtual base::TimeDelta lowFrequencyAnimationInterval() const;

    LayerTreeHostImplClient* m_client;
    Proxy* m_proxy;
    int m_sourceFrameNumber;

private:
    void computeDoubleTapZoomDeltas(ScrollAndScaleSet* scrollInfo);
    void computePinchZoomDeltas(ScrollAndScaleSet* scrollInfo);
    void makeScrollAndScaleSet(ScrollAndScaleSet* scrollInfo, gfx::Vector2d scrollOffset, float pageScale);

    void setPageScaleDelta(float);
    void updateMaxScrollOffset();
    void trackDamageForAllSurfaces(LayerImpl* rootDrawLayer, const LayerList& renderSurfaceLayerList);

    // Returns false if the frame should not be displayed. This function should
    // only be called from prepareToDraw, as didDrawAllLayers must be called
    // if this helper function is called.
    bool calculateRenderPasses(FrameData&);
    void animateLayersRecursive(LayerImpl*, base::TimeTicks monotonicTime, base::Time wallClockTime, AnimationEventsVector*, bool& didAnimate, bool& needsAnimateLayers);
    void setBackgroundTickingEnabled(bool);
    gfx::Size contentSize() const;

    void sendDidLoseContextRecursive(LayerImpl*);
    void clearRenderSurfaces();
    bool ensureRenderSurfaceLayerList();
    void clearCurrentlyScrollingLayer();

    void animateScrollbarsRecursive(LayerImpl*, base::TimeTicks monotonicTime);

    void dumpRenderSurfaces(std::string*, int indent, const LayerImpl*) const;

    scoped_ptr<GraphicsContext> m_context;
    scoped_ptr<ResourceProvider> m_resourceProvider;
    scoped_ptr<Renderer> m_renderer;
    scoped_ptr<LayerImpl> m_rootLayerImpl;
    LayerImpl* m_rootScrollLayerImpl;
    LayerImpl* m_currentlyScrollingLayerImpl;
    HeadsUpDisplayLayerImpl* m_hudLayerImpl;
    int m_scrollingLayerIdFromPreviousTree;
    bool m_scrollDeltaIsInViewportSpace;
    LayerTreeSettings m_settings;
    gfx::Size m_layoutViewportSize;
    gfx::Size m_deviceViewportSize;
    float m_deviceScaleFactor;
    bool m_visible;
    bool m_contentsTexturesPurged;
    ManagedMemoryPolicy m_managedMemoryPolicy;

    SkColor m_backgroundColor;
    bool m_hasTransparentBackground;

    // If this is true, it is necessary to traverse the layer tree ticking the animators.
    bool m_needsAnimateLayers;
    bool m_pinchGestureActive;
    gfx::Point m_previousPinchAnchor;

    scoped_ptr<PageScaleAnimation> m_pageScaleAnimation;

    // This is used for ticking animations slowly when hidden.
    scoped_ptr<LayerTreeHostImplTimeSourceAdapter> m_timeSourceClientAdapter;

    LayerSorter m_layerSorter;

    // List of visible layers for the most recently prepared frame. Used for
    // rendering and input event hit testing.
    LayerList m_renderSurfaceLayerList;

    PinchZoomViewport m_pinchZoomViewport;

    scoped_ptr<FrameRateCounter> m_fpsCounter;
    scoped_ptr<DebugRectHistory> m_debugRectHistory;

    size_t m_numImplThreadScrolls;
    size_t m_numMainThreadScrolls;

    DISALLOW_COPY_AND_ASSIGN(LayerTreeHostImpl);
};

}  // namespace cc

#endif  // CC_LAYER_TREE_HOST_IMPL_H_
