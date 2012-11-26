// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/layer_impl.h"

#include "base/debug/trace_event.h"
#include "base/stringprintf.h"
#include "cc/debug_border_draw_quad.h"
#include "cc/debug_colors.h"
#include "cc/layer_sorter.h"
#include "cc/layer_tree_host_impl.h"
#include "cc/math_util.h"
#include "cc/proxy.h"
#include "cc/quad_sink.h"
#include "cc/scrollbar_animation_controller.h"
#include "third_party/skia/include/core/SkImageFilter.h"
#include "ui/gfx/point_conversions.h"
#include "ui/gfx/rect_conversions.h"

using WebKit::WebTransformationMatrix;

namespace cc {

LayerImpl::LayerImpl(int id)
    : m_parent(0)
    , m_maskLayerId(-1)
    , m_replicaLayerId(-1)
    , m_layerId(id)
    , m_layerTreeHostImpl(0)
    , m_anchorPoint(0.5, 0.5)
    , m_anchorPointZ(0)
    , m_contentsScaleX(1.0)
    , m_contentsScaleY(1.0)
    , m_scrollable(false)
    , m_shouldScrollOnMainThread(false)
    , m_haveWheelEventHandlers(false)
    , m_backgroundColor(0)
    , m_doubleSided(true)
    , m_layerPropertyChanged(false)
    , m_layerSurfacePropertyChanged(false)
    , m_masksToBounds(false)
    , m_contentsOpaque(false)
    , m_opacity(1.0)
    , m_preserves3D(false)
    , m_useParentBackfaceVisibility(false)
    , m_drawCheckerboardForMissingTiles(false)
    , m_useLCDText(false)
    , m_drawsContent(false)
    , m_forceRenderSurface(false)
    , m_isContainerForFixedPositionLayers(false)
    , m_fixedToContainerLayer(false)
    , m_renderTarget(0)
    , m_drawDepth(0)
    , m_drawOpacity(0)
    , m_drawOpacityIsAnimating(false)
    , m_filter(0)
    , m_drawTransformIsAnimating(false)
    , m_screenSpaceTransformIsAnimating(false)
    , m_isClipped(false)
#ifndef NDEBUG
    , m_betweenWillDrawAndDidDraw(false)
#endif
    , m_layerAnimationController(LayerAnimationController::create(this))
{
    DCHECK(m_layerId > 0);
}

LayerImpl::~LayerImpl()
{
#ifndef NDEBUG
    DCHECK(!m_betweenWillDrawAndDidDraw);
#endif
    SkSafeUnref(m_filter);
}

void LayerImpl::addChild(scoped_ptr<LayerImpl> child)
{
    child->setParent(this);
    m_children.append(child.Pass());
}

void LayerImpl::removeFromParent()
{
    if (!m_parent)
        return;

    LayerImpl* parent = m_parent;
    m_parent = 0;

    for (size_t i = 0; i < parent->m_children.size(); ++i) {
        if (parent->m_children[i] == this) {
            parent->m_children.remove(i);
            return;
        }
    }
}

void LayerImpl::removeAllChildren()
{
    while (m_children.size())
        m_children[0]->removeFromParent();
}

void LayerImpl::clearChildList()
{
    m_children.clear();
}

void LayerImpl::createRenderSurface()
{
    DCHECK(!m_renderSurface);
    m_renderSurface = make_scoped_ptr(new RenderSurfaceImpl(this));
    setRenderTarget(this);
}

bool LayerImpl::descendantDrawsContent()
{
    for (size_t i = 0; i < m_children.size(); ++i) {
        if (m_children[i]->drawsContent() || m_children[i]->descendantDrawsContent())
            return true;
    }
    return false;
}

scoped_ptr<SharedQuadState> LayerImpl::createSharedQuadState() const
{
  scoped_ptr<SharedQuadState> state = SharedQuadState::Create();
  state->SetAll(m_drawTransform, m_visibleContentRect, m_drawableContentRect, m_clipRect, m_isClipped, m_drawOpacity);
  return state.Pass();
}

void LayerImpl::willDraw(ResourceProvider*)
{
#ifndef NDEBUG
    // willDraw/didDraw must be matched.
    DCHECK(!m_betweenWillDrawAndDidDraw);
    m_betweenWillDrawAndDidDraw = true;
#endif
}

void LayerImpl::didDraw(ResourceProvider*)
{
#ifndef NDEBUG
    DCHECK(m_betweenWillDrawAndDidDraw);
    m_betweenWillDrawAndDidDraw = false;
#endif
}

bool LayerImpl::showDebugBorders() const
{
    if (!m_layerTreeHostImpl)
        return false;
    return m_layerTreeHostImpl->settings().showDebugBorders;
}

void LayerImpl::getDebugBorderProperties(SkColor* color, float* width) const
{
    if (m_drawsContent) {
        *color = DebugColors::ContentLayerBorderColor();
        *width = DebugColors::ContentLayerBorderWidth(m_layerTreeHostImpl);
        return;
    }

    if (m_masksToBounds) {
        *color = DebugColors::MaskingLayerBorderColor();
        *width = DebugColors::MaskingLayerBorderWidth(m_layerTreeHostImpl);
        return;
    }

    *color = DebugColors::ContainerLayerBorderColor();
    *width = DebugColors::ContainerLayerBorderWidth(m_layerTreeHostImpl);
}

void LayerImpl::appendDebugBorderQuad(QuadSink& quadList, const SharedQuadState* sharedQuadState, AppendQuadsData& appendQuadsData) const
{
    if (!showDebugBorders())
        return;

    SkColor color;
    float width;
    getDebugBorderProperties(&color, &width);

    gfx::Rect contentRect(gfx::Point(), contentBounds());
    scoped_ptr<DebugBorderDrawQuad> debugBorderQuad = DebugBorderDrawQuad::Create();
    debugBorderQuad->SetNew(sharedQuadState, contentRect, color, width);
    quadList.append(debugBorderQuad.PassAs<DrawQuad>(), appendQuadsData);
}

bool LayerImpl::hasContributingDelegatedRenderPasses() const
{
    return false;
}

RenderPass::Id LayerImpl::firstContributingRenderPassId() const
{
    return RenderPass::Id(0, 0);
}

RenderPass::Id LayerImpl::nextContributingRenderPassId(RenderPass::Id) const
{
    return RenderPass::Id(0, 0);
}

ResourceProvider::ResourceId LayerImpl::contentsResourceId() const
{
    NOTREACHED();
    return 0;
}

gfx::Vector2dF LayerImpl::scrollBy(const gfx::Vector2dF& scroll)
{
    gfx::Vector2dF minDelta = -m_scrollOffset;
    gfx::Vector2dF maxDelta = m_maxScrollOffset - m_scrollOffset;
    // Clamp newDelta so that position + delta stays within scroll bounds.
    gfx::Vector2dF newDelta = (m_scrollDelta + scroll);
    newDelta.ClampToMin(minDelta);
    newDelta.ClampToMax(maxDelta);
    gfx::Vector2dF unscrolled = m_scrollDelta + scroll - newDelta;

    if (m_scrollDelta == newDelta)
        return unscrolled;

    m_scrollDelta = newDelta;
    if (m_scrollbarAnimationController)
        m_scrollbarAnimationController->updateScrollOffset(this);
    noteLayerPropertyChangedForSubtree();

    return unscrolled;
}

InputHandlerClient::ScrollStatus LayerImpl::tryScroll(const gfx::PointF& screenSpacePoint, InputHandlerClient::ScrollInputType type) const
{
    if (shouldScrollOnMainThread()) {
        TRACE_EVENT0("cc", "LayerImpl::tryScroll: Failed shouldScrollOnMainThread");
        return InputHandlerClient::ScrollOnMainThread;
    }

    if (!screenSpaceTransform().isInvertible()) {
        TRACE_EVENT0("cc", "LayerImpl::tryScroll: Ignored nonInvertibleTransform");
        return InputHandlerClient::ScrollIgnored;
    }

    if (!nonFastScrollableRegion().IsEmpty()) {
        bool clipped = false;
        gfx::PointF hitTestPointInContentSpace = MathUtil::projectPoint(screenSpaceTransform().inverse(), screenSpacePoint, clipped);
        gfx::PointF hitTestPointInLayerSpace = gfx::ScalePoint(hitTestPointInContentSpace, 1 / contentsScaleX(), 1 / contentsScaleY());
        if (!clipped && nonFastScrollableRegion().Contains(gfx::ToRoundedPoint(hitTestPointInLayerSpace))) {
            TRACE_EVENT0("cc", "LayerImpl::tryScroll: Failed nonFastScrollableRegion");
            return InputHandlerClient::ScrollOnMainThread;
        }
    }

    if (type == InputHandlerClient::Wheel && haveWheelEventHandlers()) {
        TRACE_EVENT0("cc", "LayerImpl::tryScroll: Failed wheelEventHandlers");
        return InputHandlerClient::ScrollOnMainThread;
    }

    if (!scrollable()) {
        TRACE_EVENT0("cc", "LayerImpl::tryScroll: Ignored not scrollable");
        return InputHandlerClient::ScrollIgnored;
    }

    return InputHandlerClient::ScrollStarted;
}

bool LayerImpl::drawCheckerboardForMissingTiles() const
{
    return m_drawCheckerboardForMissingTiles && !m_layerTreeHostImpl->settings().backgroundColorInsteadOfCheckerboard;
}

gfx::Rect LayerImpl::layerRectToContentRect(const gfx::RectF& layerRect) const
{
    gfx::RectF contentRect = gfx::ScaleRect(layerRect, contentsScaleX(), contentsScaleY());
    // Intersect with content rect to avoid the extra pixel because for some
    // values x and y, ceil((x / y) * y) may be x + 1.
    contentRect.Intersect(gfx::Rect(gfx::Point(), contentBounds()));
    return gfx::ToEnclosingRect(contentRect);
}

std::string LayerImpl::indentString(int indent)
{
    std::string str;
    for (int i = 0; i != indent; ++i)
        str.append("  ");
    return str;
}

void LayerImpl::dumpLayerProperties(std::string* str, int indent) const
{
    std::string indentStr = indentString(indent);
    str->append(indentStr);
    base::StringAppendF(str, "layer ID: %d\n", m_layerId);

    str->append(indentStr);
    base::StringAppendF(str, "bounds: %d, %d\n", bounds().width(), bounds().height());

    if (m_renderTarget) {
        str->append(indentStr);
        base::StringAppendF(str, "renderTarget: %d\n", m_renderTarget->m_layerId);
    }

    str->append(indentStr);
    base::StringAppendF(str, "position: %f, %f\n", m_position.x(), m_position.y());

    str->append(indentStr);
    base::StringAppendF(str, "contentsOpaque: %d\n", m_contentsOpaque);

    str->append(indentStr);
    base::StringAppendF(str, "drawTransform: %f, %f, %f, %f  //  %f, %f, %f, %f  //  %f, %f, %f, %f  //  %f, %f, %f, %f\n",
        m_drawTransform.m11(), m_drawTransform.m12(), m_drawTransform.m13(), m_drawTransform.m14(),
        m_drawTransform.m21(), m_drawTransform.m22(), m_drawTransform.m23(), m_drawTransform.m24(),
        m_drawTransform.m31(), m_drawTransform.m32(), m_drawTransform.m33(), m_drawTransform.m34(),
        m_drawTransform.m41(), m_drawTransform.m42(), m_drawTransform.m43(), m_drawTransform.m44());

    str->append(indentStr);
    base::StringAppendF(str, "drawsContent: %s\n", m_drawsContent ? "yes" : "no");
}

void sortLayers(std::vector<LayerImpl*>::iterator first, std::vector<LayerImpl*>::iterator end, LayerSorter* layerSorter)
{
    TRACE_EVENT0("cc", "LayerImpl::sortLayers");
    layerSorter->sort(first, end);
}

std::string LayerImpl::layerTreeAsText() const
{
    std::string str;
    dumpLayer(&str, 0);
    return str;
}

void LayerImpl::dumpLayer(std::string* str, int indent) const
{
    str->append(indentString(indent));
    base::StringAppendF(str, "%s(%s)\n", layerTypeAsString(), m_debugName.data());
    dumpLayerProperties(str, indent+2);
    if (m_replicaLayer) {
        str->append(indentString(indent+2));
        str->append("Replica:\n");
        m_replicaLayer->dumpLayer(str, indent+3);
    }
    if (m_maskLayer) {
        str->append(indentString(indent+2));
        str->append("Mask:\n");
        m_maskLayer->dumpLayer(str, indent+3);
    }
    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->dumpLayer(str, indent+1);
}

void LayerImpl::setStackingOrderChanged(bool stackingOrderChanged)
{
    // We don't need to store this flag; we only need to track that the change occurred.
    if (stackingOrderChanged)
        noteLayerPropertyChangedForSubtree();
}

bool LayerImpl::layerSurfacePropertyChanged() const
{
    if (m_layerSurfacePropertyChanged)
        return true;

    // If this layer's surface property hasn't changed, we want to see if
    // some layer above us has changed this property. This is done for the
    // case when such parent layer does not draw content, and therefore will
    // not be traversed by the damage tracker. We need to make sure that
    // property change on such layer will be caught by its descendants.
    LayerImpl* current = this->m_parent;
    while (current && !current->m_renderSurface) {
        if (current->m_layerSurfacePropertyChanged)
            return true;
        current = current->m_parent;
    }

    return false;
}

void LayerImpl::noteLayerPropertyChangedForSubtree()
{
    m_layerPropertyChanged = true;
    noteLayerPropertyChangedForDescendants();
}

void LayerImpl::noteLayerPropertyChangedForDescendants()
{
    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->noteLayerPropertyChangedForSubtree();
}

const char* LayerImpl::layerTypeAsString() const
{
    return "Layer";
}

void LayerImpl::resetAllChangeTrackingForSubtree()
{
    m_layerPropertyChanged = false;
    m_layerSurfacePropertyChanged = false;

    m_updateRect = gfx::RectF();

    if (m_renderSurface)
        m_renderSurface->resetPropertyChangedFlag();

    if (m_maskLayer)
        m_maskLayer->resetAllChangeTrackingForSubtree();

    if (m_replicaLayer)
        m_replicaLayer->resetAllChangeTrackingForSubtree(); // also resets the replica mask, if it exists.

    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->resetAllChangeTrackingForSubtree();
}

bool LayerImpl::layerIsAlwaysDamaged() const
{
    return false;
}

int LayerImpl::id() const
{
     return m_layerId;
}

float LayerImpl::opacity() const
{
     return m_opacity;
}

void LayerImpl::setOpacityFromAnimation(float opacity)
{
    setOpacity(opacity);
}

const WebKit::WebTransformationMatrix& LayerImpl::transform() const
{
     return m_transform;
}

void LayerImpl::setTransformFromAnimation(const WebTransformationMatrix& transform)
{
    setTransform(transform);
}

void LayerImpl::setBounds(const gfx::Size& bounds)
{
    if (m_bounds == bounds)
        return;

    m_bounds = bounds;

    if (masksToBounds())
        noteLayerPropertyChangedForSubtree();
    else
        m_layerPropertyChanged = true;
}

void LayerImpl::setMaskLayer(scoped_ptr<LayerImpl> maskLayer)
{
    m_maskLayer = maskLayer.Pass();

    int newLayerId = m_maskLayer ? m_maskLayer->id() : -1;
    if (newLayerId == m_maskLayerId)
        return;

    m_maskLayerId = newLayerId;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setReplicaLayer(scoped_ptr<LayerImpl> replicaLayer)
{
    m_replicaLayer = replicaLayer.Pass();

    int newLayerId = m_replicaLayer ? m_replicaLayer->id() : -1;
    if (newLayerId == m_replicaLayerId)
        return;

    m_replicaLayerId = newLayerId;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setDrawsContent(bool drawsContent)
{
    if (m_drawsContent == drawsContent)
        return;

    m_drawsContent = drawsContent;
    m_layerPropertyChanged = true;
}

void LayerImpl::setAnchorPoint(const gfx::PointF& anchorPoint)
{
    if (m_anchorPoint == anchorPoint)
        return;

    m_anchorPoint = anchorPoint;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setAnchorPointZ(float anchorPointZ)
{
    if (m_anchorPointZ == anchorPointZ)
        return;

    m_anchorPointZ = anchorPointZ;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setBackgroundColor(SkColor backgroundColor)
{
    if (m_backgroundColor == backgroundColor)
        return;

    m_backgroundColor = backgroundColor;
    m_layerPropertyChanged = true;
}

void LayerImpl::setFilters(const WebKit::WebFilterOperations& filters)
{
    if (m_filters == filters)
        return;

    DCHECK(!m_filter);
    m_filters = filters;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setBackgroundFilters(const WebKit::WebFilterOperations& backgroundFilters)
{
    if (m_backgroundFilters == backgroundFilters)
        return;

    m_backgroundFilters = backgroundFilters;
    m_layerPropertyChanged = true;
}

void LayerImpl::setFilter(SkImageFilter* filter)
{
    if (m_filter == filter)
        return;

    DCHECK(m_filters.isEmpty());
    SkRefCnt_SafeAssign(m_filter, filter);
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setMasksToBounds(bool masksToBounds)
{
    if (m_masksToBounds == masksToBounds)
        return;

    m_masksToBounds = masksToBounds;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setContentsOpaque(bool opaque)
{
    if (m_contentsOpaque == opaque)
        return;

    m_contentsOpaque = opaque;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setOpacity(float opacity)
{
    if (m_opacity == opacity)
        return;

    m_opacity = opacity;
    m_layerSurfacePropertyChanged = true;
}

bool LayerImpl::opacityIsAnimating() const
{
    return m_layerAnimationController->isAnimatingProperty(ActiveAnimation::Opacity);
}

void LayerImpl::setPosition(const gfx::PointF& position)
{
    if (m_position == position)
        return;

    m_position = position;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setPreserves3D(bool preserves3D)
{
    if (m_preserves3D == preserves3D)
        return;

    m_preserves3D = preserves3D;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setSublayerTransform(const WebTransformationMatrix& sublayerTransform)
{
    if (m_sublayerTransform == sublayerTransform)
        return;

    m_sublayerTransform = sublayerTransform;
    // sublayer transform does not affect the current layer; it affects only its children.
    noteLayerPropertyChangedForDescendants();
}

void LayerImpl::setTransform(const WebTransformationMatrix& transform)
{
    if (m_transform == transform)
        return;

    m_transform = transform;
    m_layerSurfacePropertyChanged = true;
}

bool LayerImpl::transformIsAnimating() const
{
    return m_layerAnimationController->isAnimatingProperty(ActiveAnimation::Transform);
}

void LayerImpl::setContentBounds(const gfx::Size& contentBounds)
{
    if (m_contentBounds == contentBounds)
        return;

    m_contentBounds = contentBounds;
    m_layerPropertyChanged = true;
}

void LayerImpl::setContentsScale(float contentsScaleX, float contentsScaleY)
{
    if (m_contentsScaleX == contentsScaleX && m_contentsScaleY == contentsScaleY)
        return;

    m_contentsScaleX = contentsScaleX;
    m_contentsScaleY = contentsScaleY;
    m_layerPropertyChanged = true;
}

void LayerImpl::setScrollOffset(gfx::Vector2d scrollOffset)
{
    if (m_scrollOffset == scrollOffset)
        return;

    m_scrollOffset = scrollOffset;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setScrollDelta(const gfx::Vector2dF& scrollDelta)
{
    if (m_scrollDelta == scrollDelta)
        return;

    m_scrollDelta = scrollDelta;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setImplTransform(const WebKit::WebTransformationMatrix& transform)
{
    if (m_implTransform == transform)
        return;

    m_implTransform = transform;
    noteLayerPropertyChangedForSubtree();
}

void LayerImpl::setDoubleSided(bool doubleSided)
{
    if (m_doubleSided == doubleSided)
        return;

    m_doubleSided = doubleSided;
    noteLayerPropertyChangedForSubtree();
}

Region LayerImpl::visibleContentOpaqueRegion() const
{
    if (contentsOpaque())
        return visibleContentRect();
    return Region();
}

void LayerImpl::didLoseContext()
{
}

void LayerImpl::setMaxScrollOffset(gfx::Vector2d maxScrollOffset)
{
    m_maxScrollOffset = maxScrollOffset;

    if (!m_scrollbarAnimationController)
        return;
    m_scrollbarAnimationController->updateScrollOffset(this);
}

ScrollbarLayerImpl* LayerImpl::horizontalScrollbarLayer()
{
    return m_scrollbarAnimationController ? m_scrollbarAnimationController->horizontalScrollbarLayer() : 0;
}

const ScrollbarLayerImpl* LayerImpl::horizontalScrollbarLayer() const
{
    return m_scrollbarAnimationController ? m_scrollbarAnimationController->horizontalScrollbarLayer() : 0;
}

void LayerImpl::setHorizontalScrollbarLayer(ScrollbarLayerImpl* scrollbarLayer)
{
    if (!m_scrollbarAnimationController)
        m_scrollbarAnimationController = ScrollbarAnimationController::create(this);
    m_scrollbarAnimationController->setHorizontalScrollbarLayer(scrollbarLayer);
    m_scrollbarAnimationController->updateScrollOffset(this);
}

ScrollbarLayerImpl* LayerImpl::verticalScrollbarLayer()
{
    return m_scrollbarAnimationController ? m_scrollbarAnimationController->verticalScrollbarLayer() : 0;
}

const ScrollbarLayerImpl* LayerImpl::verticalScrollbarLayer() const
{
    return m_scrollbarAnimationController ? m_scrollbarAnimationController->verticalScrollbarLayer() : 0;
}

void LayerImpl::setVerticalScrollbarLayer(ScrollbarLayerImpl* scrollbarLayer)
{
    if (!m_scrollbarAnimationController)
        m_scrollbarAnimationController = ScrollbarAnimationController::create(this);
    m_scrollbarAnimationController->setVerticalScrollbarLayer(scrollbarLayer);
    m_scrollbarAnimationController->updateScrollOffset(this);
}

}  // namespace cc
