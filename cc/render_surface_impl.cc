// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/render_surface_impl.h"

#include <algorithm>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "cc/damage_tracker.h"
#include "cc/debug_border_draw_quad.h"
#include "cc/debug_colors.h"
#include "cc/delegated_renderer_layer_impl.h"
#include "cc/layer_impl.h"
#include "cc/math_util.h"
#include "cc/quad_sink.h"
#include "cc/render_pass.h"
#include "cc/render_pass_draw_quad.h"
#include "cc/render_pass_sink.h"
#include "cc/shared_quad_state.h"
#include "third_party/skia/include/core/SkImageFilter.h"
#include "ui/gfx/rect_conversions.h"
#include <public/WebTransformationMatrix.h>

using WebKit::WebTransformationMatrix;

namespace cc {

RenderSurfaceImpl::RenderSurfaceImpl(LayerImpl* owningLayer)
    : m_owningLayer(owningLayer)
    , m_surfacePropertyChanged(false)
    , m_drawOpacity(1)
    , m_drawOpacityIsAnimating(false)
    , m_targetSurfaceTransformsAreAnimating(false)
    , m_screenSpaceTransformsAreAnimating(false)
    , m_isClipped(false)
    , m_nearestAncestorThatMovesPixels(0)
    , m_targetRenderSurfaceLayerIndexHistory(0)
    , m_currentLayerIndexHistory(0)
{
    m_damageTracker = DamageTracker::create();
}

RenderSurfaceImpl::~RenderSurfaceImpl()
{
}

gfx::RectF RenderSurfaceImpl::drawableContentRect() const
{
    gfx::RectF drawableContentRect = MathUtil::mapClippedRect(m_drawTransform, m_contentRect);
    if (m_owningLayer->hasReplica())
        drawableContentRect.Union(MathUtil::mapClippedRect(m_replicaDrawTransform, m_contentRect));

    return drawableContentRect;
}

std::string RenderSurfaceImpl::name() const
{
    return base::StringPrintf("RenderSurfaceImpl(id=%i,owner=%s)", m_owningLayer->id(), m_owningLayer->debugName().data());
}

static std::string indentString(int indent)
{
    std::string str;
    for (int i = 0; i != indent; ++i)
        str.append("  ");
    return str;
}

void RenderSurfaceImpl::dumpSurface(std::string* str, int indent) const
{
    std::string indentStr = indentString(indent);
    str->append(indentStr);
    base::StringAppendF(str, "%s\n", name().data());

    indentStr.append("  ");
    str->append(indentStr);
    base::StringAppendF(str, "contentRect: (%d, %d, %d, %d)\n", m_contentRect.x(), m_contentRect.y(), m_contentRect.width(), m_contentRect.height());

    str->append(indentStr);
    base::StringAppendF(str, "drawTransform: %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f\n",
        m_drawTransform.m11(), m_drawTransform.m12(), m_drawTransform.m13(), m_drawTransform.m14(),
        m_drawTransform.m21(), m_drawTransform.m22(), m_drawTransform.m23(), m_drawTransform.m24(),
        m_drawTransform.m31(), m_drawTransform.m32(), m_drawTransform.m33(), m_drawTransform.m34(),
        m_drawTransform.m41(), m_drawTransform.m42(), m_drawTransform.m43(), m_drawTransform.m44());

    str->append(indentStr);
    base::StringAppendF(str, "damageRect is pos(%f, %f), size(%f, %f)\n",
        m_damageTracker->currentDamageRect().x(), m_damageTracker->currentDamageRect().y(),
        m_damageTracker->currentDamageRect().width(), m_damageTracker->currentDamageRect().height());
}

int RenderSurfaceImpl::owningLayerId() const
{
    return m_owningLayer ? m_owningLayer->id() : 0;
}


void RenderSurfaceImpl::setClipRect(const gfx::Rect& clipRect)
{
    if (m_clipRect == clipRect)
        return;

    m_surfacePropertyChanged = true;
    m_clipRect = clipRect;
}

bool RenderSurfaceImpl::contentsChanged() const
{
    return !m_damageTracker->currentDamageRect().IsEmpty();
}

void RenderSurfaceImpl::setContentRect(const gfx::Rect& contentRect)
{
    if (m_contentRect == contentRect)
        return;

    m_surfacePropertyChanged = true;
    m_contentRect = contentRect;
}

bool RenderSurfaceImpl::surfacePropertyChanged() const
{
    // Surface property changes are tracked as follows:
    //
    // - m_surfacePropertyChanged is flagged when the clipRect or contentRect change. As
    //   of now, these are the only two properties that can be affected by descendant layers.
    //
    // - all other property changes come from the owning layer (or some ancestor layer
    //   that propagates its change to the owning layer).
    //
    DCHECK(m_owningLayer);
    return m_surfacePropertyChanged || m_owningLayer->layerPropertyChanged();
}

bool RenderSurfaceImpl::surfacePropertyChangedOnlyFromDescendant() const
{
    return m_surfacePropertyChanged && !m_owningLayer->layerPropertyChanged();
}

void RenderSurfaceImpl::addContributingDelegatedRenderPassLayer(LayerImpl* layer)
{
    DCHECK(std::find(m_layerList.begin(), m_layerList.end(), layer) != m_layerList.end());
    DelegatedRendererLayerImpl* delegatedRendererLayer = static_cast<DelegatedRendererLayerImpl*>(layer);
    m_contributingDelegatedRenderPassLayerList.push_back(delegatedRendererLayer);
}

void RenderSurfaceImpl::clearLayerLists()
{
    m_layerList.clear();
    m_contributingDelegatedRenderPassLayerList.clear();
}

static inline gfx::Rect computeClippedRectInTarget(const LayerImpl* owningLayer)
{
    DCHECK(owningLayer->parent());

    const LayerImpl* renderTarget = owningLayer->parent()->renderTarget();
    const RenderSurfaceImpl* self = owningLayer->renderSurface();

    gfx::Rect clippedRectInTarget = self->clipRect();
    if (owningLayer->backgroundFilters().hasFilterThatMovesPixels()) {
        // If the layer has background filters that move pixels, we cannot scissor as tightly.
        // FIXME: this should be able to be a tighter scissor, perhaps expanded by the filter outsets?
        clippedRectInTarget = renderTarget->renderSurface()->contentRect();
    } else if (clippedRectInTarget.IsEmpty()) {
        // For surfaces, empty clipRect means that the surface does not clip anything.
        clippedRectInTarget = renderTarget->renderSurface()->contentRect();
        clippedRectInTarget.Intersect(gfx::ToEnclosingRect(self->drawableContentRect()));
    } else
        clippedRectInTarget.Intersect(gfx::ToEnclosingRect(self->drawableContentRect()));
    return clippedRectInTarget;
}

RenderPass::Id RenderSurfaceImpl::renderPassId()
{
    int layerId = m_owningLayer->id();
    int subId = 0;
    DCHECK(layerId > 0);
    return RenderPass::Id(layerId, subId);
}

void RenderSurfaceImpl::appendRenderPasses(RenderPassSink& passSink)
{
    for (size_t i = 0; i < m_contributingDelegatedRenderPassLayerList.size(); ++i)
        m_contributingDelegatedRenderPassLayerList[i]->appendContributingRenderPasses(passSink);

    scoped_ptr<RenderPass> pass = RenderPass::Create();
    pass->SetNew(renderPassId(), m_contentRect, m_damageTracker->currentDamageRect(), m_screenSpaceTransform);
    pass->filters = m_owningLayer->filters();
    SkRefCnt_SafeAssign(pass->filter, m_owningLayer->filter());
    pass->background_filters = m_owningLayer->backgroundFilters();
    passSink.appendRenderPass(pass.Pass());
}

void RenderSurfaceImpl::appendQuads(QuadSink& quadSink, AppendQuadsData& appendQuadsData, bool forReplica, RenderPass::Id renderPassId)
{
    DCHECK(!forReplica || m_owningLayer->hasReplica());

    gfx::Rect clippedRectInTarget = computeClippedRectInTarget(m_owningLayer);
    const WebTransformationMatrix& drawTransform = forReplica ? m_replicaDrawTransform : m_drawTransform;
    SharedQuadState* sharedQuadState = quadSink.useSharedQuadState(SharedQuadState::Create());
    sharedQuadState->SetAll(drawTransform, m_contentRect, clippedRectInTarget, m_clipRect, m_isClipped, m_drawOpacity);

    if (m_owningLayer->showDebugBorders()) {
        SkColor color = forReplica ? DebugColors::SurfaceReplicaBorderColor() : DebugColors::SurfaceBorderColor();
        float width = forReplica ? DebugColors::SurfaceReplicaBorderWidth(m_owningLayer->layerTreeHostImpl()) : DebugColors::SurfaceBorderWidth(m_owningLayer->layerTreeHostImpl());
        scoped_ptr<DebugBorderDrawQuad> debugBorderQuad = DebugBorderDrawQuad::Create();
        debugBorderQuad->SetNew(sharedQuadState, contentRect(), color, width);
        quadSink.append(debugBorderQuad.PassAs<DrawQuad>(), appendQuadsData);
    }

    // FIXME: By using the same RenderSurfaceImpl for both the content and its reflection,
    // it's currently not possible to apply a separate mask to the reflection layer
    // or correctly handle opacity in reflections (opacity must be applied after drawing
    // both the layer and its reflection). The solution is to introduce yet another RenderSurfaceImpl
    // to draw the layer and its reflection in. For now we only apply a separate reflection
    // mask if the contents don't have a mask of their own.
    LayerImpl* maskLayer = m_owningLayer->maskLayer();
    if (maskLayer && (!maskLayer->drawsContent() || maskLayer->bounds().IsEmpty()))
        maskLayer = 0;

    if (!maskLayer && forReplica) {
        maskLayer = m_owningLayer->replicaLayer()->maskLayer();
        if (maskLayer && (!maskLayer->drawsContent() || maskLayer->bounds().IsEmpty()))
            maskLayer = 0;
    }

    float maskTexCoordScaleX = 1;
    float maskTexCoordScaleY = 1;
    float maskTexCoordOffsetX = 0;
    float maskTexCoordOffsetY = 0;
    if (maskLayer) {
        maskTexCoordScaleX = contentRect().width() / maskLayer->contentsScaleX() / maskLayer->bounds().width();
        maskTexCoordScaleY = contentRect().height() / maskLayer->contentsScaleY() / maskLayer->bounds().height();
        maskTexCoordOffsetX = static_cast<float>(contentRect().x()) / contentRect().width() * maskTexCoordScaleX;
        maskTexCoordOffsetY = static_cast<float>(contentRect().y()) / contentRect().height() * maskTexCoordScaleY;
    }

    ResourceProvider::ResourceId maskResourceId = maskLayer ? maskLayer->contentsResourceId() : 0;
    gfx::Rect contentsChangedSinceLastFrame = contentsChanged() ? m_contentRect : gfx::Rect();

    scoped_ptr<RenderPassDrawQuad> quad = RenderPassDrawQuad::Create();
    quad->SetNew(sharedQuadState, contentRect(), renderPassId, forReplica, maskResourceId, contentsChangedSinceLastFrame,
                 maskTexCoordScaleX, maskTexCoordScaleY, maskTexCoordOffsetX, maskTexCoordOffsetY);
    quadSink.append(quad.PassAs<DrawQuad>(), appendQuadsData);
}

}  // namespace cc
