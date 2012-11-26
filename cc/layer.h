// Copyright 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYER_H_
#define CC_LAYER_H_

#include "base/memory/ref_counted.h"
#include "cc/cc_export.h"
#include "cc/layer_animation_controller.h"
#include "cc/occlusion_tracker.h"
#include "cc/region.h"
#include "cc/render_surface.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/rect_f.h"
#include <public/WebFilterOperations.h>
#include <public/WebTransformationMatrix.h>
#include <string>
#include <vector>

namespace WebKit {
class WebAnimationDelegate;
class WebLayerScrollClient;
}

class SkImageFilter;

namespace cc {

class ActiveAnimation;
struct AnimationEvent;
class LayerAnimationDelegate;
class LayerImpl;
class LayerTreeHost;
class PriorityCalculator;
class ResourceUpdateQueue;
class ScrollbarLayer;
struct AnimationEvent;
struct RenderingStats;

// Base class for composited layers. Special layer types are derived from
// this class.
class CC_EXPORT Layer : public base::RefCounted<Layer>, public LayerAnimationControllerClient {
public:
    typedef std::vector<scoped_refptr<Layer> > LayerList;

    static scoped_refptr<Layer> create();

    // LayerAnimationControllerClient implementation
    virtual int id() const OVERRIDE;
    virtual void setOpacityFromAnimation(float) OVERRIDE;
    virtual float opacity() const OVERRIDE;
    virtual void setTransformFromAnimation(const WebKit::WebTransformationMatrix&) OVERRIDE;
    // A layer's transform operates layer space. That is, entirely in logical,
    // non-page-scaled pixels (that is, they have page zoom baked in, but not page scale).
    // The root layer is a special case -- it operates in physical pixels.
    virtual const WebKit::WebTransformationMatrix& transform() const OVERRIDE;

    Layer* rootLayer();
    Layer* parent() { return m_parent; }
    const Layer* parent() const { return m_parent; }
    void addChild(scoped_refptr<Layer>);
    void insertChild(scoped_refptr<Layer>, size_t index);
    void replaceChild(Layer* reference, scoped_refptr<Layer> newLayer);
    void removeFromParent();
    void removeAllChildren();
    void setChildren(const LayerList&);

    const LayerList& children() const { return m_children; }

    void setAnchorPoint(const gfx::PointF&);
    gfx::PointF anchorPoint() const { return m_anchorPoint; }

    void setAnchorPointZ(float);
    float anchorPointZ() const { return m_anchorPointZ; }

    virtual void setBackgroundColor(SkColor);
    SkColor backgroundColor() const { return m_backgroundColor; }

    // A layer's bounds are in logical, non-page-scaled pixels (however, the
    // root layer's bounds are in physical pixels).
    void setBounds(const gfx::Size&);
    const gfx::Size& bounds() const { return m_bounds; }
    virtual gfx::Size contentBounds() const;

    void setMasksToBounds(bool);
    bool masksToBounds() const { return m_masksToBounds; }

    void setMaskLayer(Layer*);
    Layer* maskLayer() { return m_maskLayer.get(); }
    const Layer* maskLayer() const { return m_maskLayer.get(); }

    virtual void setNeedsDisplayRect(const gfx::RectF& dirtyRect);
    void setNeedsDisplay() { setNeedsDisplayRect(gfx::RectF(gfx::PointF(), bounds())); }
    virtual bool needsDisplay() const;

    void setOpacity(float);
    bool opacityIsAnimating() const;

    void setFilters(const WebKit::WebFilterOperations&);
    const WebKit::WebFilterOperations& filters() const { return m_filters; }

    void setFilter(SkImageFilter* filter);
    SkImageFilter* filter() const { return m_filter; }

    // Background filters are filters applied to what is behind this layer, when they are viewed through non-opaque
    // regions in this layer. They are used through the WebLayer interface, and are not exposed to HTML.
    void setBackgroundFilters(const WebKit::WebFilterOperations&);
    const WebKit::WebFilterOperations& backgroundFilters() const { return m_backgroundFilters; }

    virtual void setContentsOpaque(bool);
    bool contentsOpaque() const { return m_contentsOpaque; }

    void setPosition(const gfx::PointF&);
    gfx::PointF position() const { return m_position; }

    void setIsContainerForFixedPositionLayers(bool);
    bool isContainerForFixedPositionLayers() const { return m_isContainerForFixedPositionLayers; }

    void setFixedToContainerLayer(bool);
    bool fixedToContainerLayer() const { return m_fixedToContainerLayer; }

    void setSublayerTransform(const WebKit::WebTransformationMatrix&);
    const WebKit::WebTransformationMatrix& sublayerTransform() const { return m_sublayerTransform; }

    void setTransform(const WebKit::WebTransformationMatrix&);
    bool transformIsAnimating() const;

    const gfx::Rect& visibleContentRect() const { return m_visibleContentRect; }
    void setVisibleContentRect(const gfx::Rect& visibleContentRect) { m_visibleContentRect = visibleContentRect; }

    void setScrollOffset(gfx::Vector2d);
    gfx::Vector2d scrollOffset() const { return m_scrollOffset; }

    void setMaxScrollOffset(gfx::Vector2d);
    gfx::Vector2d maxScrollOffset() const { return m_maxScrollOffset; }

    void setScrollable(bool);
    bool scrollable() const { return m_scrollable; }

    void setShouldScrollOnMainThread(bool);
    bool shouldScrollOnMainThread() const { return m_shouldScrollOnMainThread; }

    void setHaveWheelEventHandlers(bool);
    bool haveWheelEventHandlers() const { return m_haveWheelEventHandlers; }

    void setNonFastScrollableRegion(const Region&);
    void setNonFastScrollableRegionChanged() { m_nonFastScrollableRegionChanged = true; }
    const Region& nonFastScrollableRegion() const { return m_nonFastScrollableRegion; }

    void setTouchEventHandlerRegion(const Region&);
    void setTouchEventHandlerRegionChanged() { m_touchEventHandlerRegionChanged = true; }
    const Region& touchEventHandlerRegion() const { return m_touchEventHandlerRegion; }

    void setLayerScrollClient(WebKit::WebLayerScrollClient* layerScrollClient) { m_layerScrollClient = layerScrollClient; }

    void setDrawCheckerboardForMissingTiles(bool);
    bool drawCheckerboardForMissingTiles() const { return m_drawCheckerboardForMissingTiles; }

    bool forceRenderSurface() const { return m_forceRenderSurface; }
    void setForceRenderSurface(bool);

    gfx::Vector2d scrollDelta() const { return gfx::Vector2d(); }

    void setImplTransform(const WebKit::WebTransformationMatrix&);
    const WebKit::WebTransformationMatrix& implTransform() const { return m_implTransform; }

    void setDoubleSided(bool);
    bool doubleSided() const { return m_doubleSided; }

    void setPreserves3D(bool preserve3D) { m_preserves3D = preserve3D; }
    bool preserves3D() const { return m_preserves3D; }

    void setUseParentBackfaceVisibility(bool useParentBackfaceVisibility) { m_useParentBackfaceVisibility = useParentBackfaceVisibility; }
    bool useParentBackfaceVisibility() const { return m_useParentBackfaceVisibility; }

    void setUseLCDText(bool);
    bool useLCDText() const { return m_useLCDText; }

    virtual void setLayerTreeHost(LayerTreeHost*);

    bool hasContributingDelegatedRenderPasses() const { return false; }

    void setIsDrawable(bool);

    void setReplicaLayer(Layer*);
    Layer* replicaLayer() { return m_replicaLayer.get(); }
    const Layer* replicaLayer() const { return m_replicaLayer.get(); }

    bool hasMask() const { return !!m_maskLayer; }
    bool hasReplica() const { return !!m_replicaLayer; }
    bool replicaHasMask() const { return m_replicaLayer && (m_maskLayer || m_replicaLayer->m_maskLayer); }

    // These methods typically need to be overwritten by derived classes.
    virtual bool drawsContent() const;
    virtual void update(ResourceUpdateQueue&, const OcclusionTracker*, RenderingStats&) { }
    virtual bool needMoreUpdates();
    virtual void setIsMask(bool) { }
    virtual void bindContentsTexture() { }

    void setDebugName(const std::string&);

    virtual void pushPropertiesTo(LayerImpl*);

    void clearRenderSurface() { m_renderSurface.reset(); }
    RenderSurface* renderSurface() const { return m_renderSurface.get(); }
    void createRenderSurface();

    float drawOpacity() const { return m_drawOpacity; }
    void setDrawOpacity(float opacity) { m_drawOpacity = opacity; }

    bool drawOpacityIsAnimating() const { return m_drawOpacityIsAnimating; }
    void setDrawOpacityIsAnimating(bool drawOpacityIsAnimating) { m_drawOpacityIsAnimating = drawOpacityIsAnimating; }

    void setRenderTarget(Layer* target) { m_renderTarget = target; }
    Layer* renderTarget() { DCHECK(!m_renderTarget || m_renderTarget->renderSurface()); return m_renderTarget; }
    const Layer* renderTarget() const { DCHECK(!m_renderTarget || m_renderTarget->renderSurface()); return m_renderTarget; }

    bool drawTransformIsAnimating() const { return m_drawTransformIsAnimating; }
    void setDrawTransformIsAnimating(bool animating) { m_drawTransformIsAnimating = animating; }
    bool screenSpaceTransformIsAnimating() const { return m_screenSpaceTransformIsAnimating; }
    void setScreenSpaceTransformIsAnimating(bool animating) { m_screenSpaceTransformIsAnimating = animating; }

    // This moves from layer space, with origin in the center to target space with origin in the top left.
    // That is, it converts from logical, non-page-scaled, to target pixels (and if the target is the
    // root render surface, then this converts to physical pixels).
    const WebKit::WebTransformationMatrix& drawTransform() const { return m_drawTransform; }
    void setDrawTransform(const WebKit::WebTransformationMatrix& matrix) { m_drawTransform = matrix; }
    // This moves from content space, with origin the top left to screen space with origin in the top left.
    // It converts logical, non-page-scaled pixels to physical pixels.
    const WebKit::WebTransformationMatrix& screenSpaceTransform() const { return m_screenSpaceTransform; }
    void setScreenSpaceTransform(const WebKit::WebTransformationMatrix& matrix) { m_screenSpaceTransform = matrix; }

    bool isClipped() const { return m_isClipped; }
    void setIsClipped(bool isClipped) { m_isClipped = isClipped; }

    const gfx::Rect& clipRect() const { return m_clipRect; }
    void setClipRect(const gfx::Rect& clipRect) { m_clipRect = clipRect; }

    const gfx::Rect& drawableContentRect() const { return m_drawableContentRect; }
    void setDrawableContentRect(const gfx::Rect& rect) { m_drawableContentRect = rect; }

    // The contentsScale converts from logical, non-page-scaled pixels to target pixels.
    // The contentsScale is 1 for the root layer as it is already in physical pixels.
    // By default contentsScale is forced to be 1 except for subclasses of ContentsScalingLayer.
    virtual float contentsScaleX() const;
    virtual float contentsScaleY() const;
    virtual void setContentsScale(float contentsScale) { }

    // The scale at which contents should be rastered, to match the scale at
    // which they will drawn to the screen. This scale is a component of the
    // contentsScale() but does not include page/device scale factors.
    float rasterScale() const { return m_rasterScale; }
    void setRasterScale(float scale);

    // When true, the rasterScale() will be set by the compositor. If false, it
    // will use whatever value is given to it by the embedder.
    bool automaticallyComputeRasterScale() { return m_automaticallyComputeRasterScale; }
    void setAutomaticallyComputeRasterScale(bool);

    void forceAutomaticRasterScaleToBeRecomputed();

    // When true, the layer's contents are not scaled by the current page scale factor.
    // setBoundsContainPageScale recursively sets the value on all child layers.
    void setBoundsContainPageScale(bool);
    bool boundsContainPageScale() const { return m_boundsContainPageScale; }

    // Returns true if any of the layer's descendants has content to draw.
    bool descendantDrawsContent();

    LayerTreeHost* layerTreeHost() const { return m_layerTreeHost; }

    // Set the priority of all desired textures in this layer.
    virtual void setTexturePriorities(const PriorityCalculator&) { }

    bool addAnimation(scoped_ptr<ActiveAnimation>);
    void pauseAnimation(int animationId, double timeOffset);
    void removeAnimation(int animationId);

    void suspendAnimations(double monotonicTime);
    void resumeAnimations(double monotonicTime);

    LayerAnimationController* layerAnimationController() { return m_layerAnimationController.get(); }
    void setLayerAnimationController(scoped_ptr<LayerAnimationController>);
    scoped_ptr<LayerAnimationController> releaseLayerAnimationController();

    void setLayerAnimationDelegate(WebKit::WebAnimationDelegate* layerAnimationDelegate) { m_layerAnimationDelegate = layerAnimationDelegate; }

    bool hasActiveAnimation() const;

    virtual void notifyAnimationStarted(const AnimationEvent&, double wallClockTime);
    virtual void notifyAnimationFinished(double wallClockTime);

    virtual Region visibleContentOpaqueRegion() const;

    virtual ScrollbarLayer* toScrollbarLayer();

    gfx::Rect layerRectToContentRect(const gfx::RectF& layerRect) const;

protected:
    friend class LayerImpl;
    friend class TreeSynchronizer;
    virtual ~Layer();

    Layer();

    void setNeedsCommit();

    // This flag is set when layer need repainting/updating.
    bool m_needsDisplay;

    // Tracks whether this layer may have changed stacking order with its siblings.
    bool m_stackingOrderChanged;

    // The update rect is the region of the compositor resource that was actually updated by the compositor.
    // For layers that may do updating outside the compositor's control (i.e. plugin layers), this information
    // is not available and the update rect will remain empty.
    // Note this rect is in layer space (not content space).
    gfx::RectF m_updateRect;

    scoped_refptr<Layer> m_maskLayer;

    // Constructs a LayerImpl of the correct runtime type for this Layer type.
    virtual scoped_ptr<LayerImpl> createLayerImpl();
    int m_layerId;

private:
    friend class base::RefCounted<Layer>;

    void setParent(Layer*);
    bool hasAncestor(Layer*) const;
    bool descendantIsFixedToContainerLayer() const;

    size_t numChildren() const { return m_children.size(); }

    // Returns the index of the child or -1 if not found.
    int indexOfChild(const Layer*);

    // This should only be called from removeFromParent.
    void removeChild(Layer*);

    LayerList m_children;
    Layer* m_parent;

    // Layer instances have a weak pointer to their LayerTreeHost.
    // This pointer value is nil when a Layer is not in a tree and is
    // updated via setLayerTreeHost() if a layer moves between trees.
    LayerTreeHost* m_layerTreeHost;

    scoped_ptr<LayerAnimationController> m_layerAnimationController;

    // Layer properties.
    gfx::Size m_bounds;

    // Uses layer's content space.
    gfx::Rect m_visibleContentRect;

    gfx::Vector2d m_scrollOffset;
    gfx::Vector2d m_maxScrollOffset;
    bool m_scrollable;
    bool m_shouldScrollOnMainThread;
    bool m_haveWheelEventHandlers;
    Region m_nonFastScrollableRegion;
    bool m_nonFastScrollableRegionChanged;
    Region m_touchEventHandlerRegion;
    bool m_touchEventHandlerRegionChanged;
    gfx::PointF m_position;
    gfx::PointF m_anchorPoint;
    SkColor m_backgroundColor;
    std::string m_debugName;
    float m_opacity;
    SkImageFilter* m_filter;
    WebKit::WebFilterOperations m_filters;
    WebKit::WebFilterOperations m_backgroundFilters;
    float m_anchorPointZ;
    bool m_isContainerForFixedPositionLayers;
    bool m_fixedToContainerLayer;
    bool m_isDrawable;
    bool m_masksToBounds;
    bool m_contentsOpaque;
    bool m_doubleSided;
    bool m_useLCDText;
    bool m_preserves3D;
    bool m_useParentBackfaceVisibility;
    bool m_drawCheckerboardForMissingTiles;
    bool m_forceRenderSurface;

    WebKit::WebTransformationMatrix m_transform;
    WebKit::WebTransformationMatrix m_sublayerTransform;

    // Replica layer used for reflections.
    scoped_refptr<Layer> m_replicaLayer;

    // Transient properties.
    scoped_ptr<RenderSurface> m_renderSurface;
    float m_drawOpacity;
    bool m_drawOpacityIsAnimating;

    Layer* m_renderTarget;

    WebKit::WebTransformationMatrix m_drawTransform;
    WebKit::WebTransformationMatrix m_screenSpaceTransform;
    bool m_drawTransformIsAnimating;
    bool m_screenSpaceTransformIsAnimating;

    // Uses target surface space.
    gfx::Rect m_drawableContentRect;
    gfx::Rect m_clipRect;

    // True if the layer is clipped by m_clipRect
    bool m_isClipped;

    float m_rasterScale;
    bool m_automaticallyComputeRasterScale;
    bool m_boundsContainPageScale;

    WebKit::WebTransformationMatrix m_implTransform;

    WebKit::WebAnimationDelegate* m_layerAnimationDelegate;
    WebKit::WebLayerScrollClient* m_layerScrollClient;
};

void sortLayers(std::vector<scoped_refptr<Layer> >::iterator, std::vector<scoped_refptr<Layer> >::iterator, void*);

}  // namespace cc

#endif  // CC_LAYER_H_
