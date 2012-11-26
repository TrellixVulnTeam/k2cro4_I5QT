// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/software_renderer.h"

#include "base/debug/trace_event.h"
#include "cc/debug_border_draw_quad.h"
#include "cc/render_pass_draw_quad.h"
#include "cc/solid_color_draw_quad.h"
#include "cc/texture_draw_quad.h"
#include "cc/tile_draw_quad.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkShader.h"
#include "third_party/skia/include/effects/SkLayerRasterizer.h"
#include "ui/gfx/rect_conversions.h"
#include "ui/gfx/skia_util.h"
#include <public/WebCompositorSoftwareOutputDevice.h>
#include <public/WebImage.h>
#include <public/WebSize.h>
#include <public/WebTransformationMatrix.h>

using WebKit::WebCompositorSoftwareOutputDevice;
using WebKit::WebSize;
using WebKit::WebTransformationMatrix;

namespace cc {

namespace {

void toSkMatrix(SkMatrix* flattened, const WebTransformationMatrix& m)
{
    // Convert from 4x4 to 3x3 by dropping the third row and column.
    flattened->set(0, SkDoubleToScalar(m.m11()));
    flattened->set(1, SkDoubleToScalar(m.m21()));
    flattened->set(2, SkDoubleToScalar(m.m41()));
    flattened->set(3, SkDoubleToScalar(m.m12()));
    flattened->set(4, SkDoubleToScalar(m.m22()));
    flattened->set(5, SkDoubleToScalar(m.m42()));
    flattened->set(6, SkDoubleToScalar(m.m14()));
    flattened->set(7, SkDoubleToScalar(m.m24()));
    flattened->set(8, SkDoubleToScalar(m.m44()));
}

bool isScaleAndTranslate(const SkMatrix& matrix)
{
    return SkScalarNearlyZero(matrix[SkMatrix::kMSkewX]) &&
           SkScalarNearlyZero(matrix[SkMatrix::kMSkewY]) &&
           SkScalarNearlyZero(matrix[SkMatrix::kMPersp0]) &&
           SkScalarNearlyZero(matrix[SkMatrix::kMPersp1]) &&
           SkScalarNearlyZero(matrix[SkMatrix::kMPersp2] - 1.0f);
}

} // anonymous namespace

scoped_ptr<SoftwareRenderer> SoftwareRenderer::create(RendererClient* client, ResourceProvider* resourceProvider, WebCompositorSoftwareOutputDevice* outputDevice)
{
    return make_scoped_ptr(new SoftwareRenderer(client, resourceProvider, outputDevice));
}

SoftwareRenderer::SoftwareRenderer(RendererClient* client, ResourceProvider* resourceProvider, WebCompositorSoftwareOutputDevice* outputDevice)
    : DirectRenderer(client, resourceProvider)
    , m_visible(true)
    , m_outputDevice(outputDevice)
    , m_skCurrentCanvas(0)
{
    m_resourceProvider->setDefaultResourceType(ResourceProvider::Bitmap);

    m_capabilities.maxTextureSize = INT_MAX;
    m_capabilities.bestTextureFormat = GL_RGBA;
    m_capabilities.contextHasCachedFrontBuffer = true;
    m_capabilities.usingSetVisibility = true;

    viewportChanged();
}

SoftwareRenderer::~SoftwareRenderer()
{
}

const RendererCapabilities& SoftwareRenderer::capabilities() const
{
    return m_capabilities;
}

void SoftwareRenderer::viewportChanged()
{
    m_outputDevice->didChangeViewportSize(WebSize(viewportSize().width(), viewportSize().height()));
}

void SoftwareRenderer::beginDrawingFrame(DrawingFrame& frame)
{
    TRACE_EVENT0("cc", "SoftwareRenderer::beginDrawingFrame");
    m_skRootCanvas = make_scoped_ptr(new SkCanvas(m_outputDevice->lock(true)->getSkBitmap()));
}

void SoftwareRenderer::finishDrawingFrame(DrawingFrame& frame)
{
    TRACE_EVENT0("cc", "SoftwareRenderer::finishDrawingFrame");
    m_currentFramebufferLock.reset();
    m_skCurrentCanvas = 0;
    m_skRootCanvas.reset();
    m_outputDevice->unlock();
}

bool SoftwareRenderer::flippedFramebuffer() const
{
    return false;
}

void SoftwareRenderer::ensureScissorTestEnabled()
{
    // Nothing to do here. Current implementation of software rendering has no
    // notion of enabling/disabling the feature.
}

void SoftwareRenderer::ensureScissorTestDisabled()
{
    // There is no explicit notion of enabling/disabling scissoring in software
    // rendering, but the underlying effect we want is to clear any existing
    // clipRect on the current SkCanvas. This is done by setting clipRect to
    // the viewport's dimensions.
    SkISize canvasSize = m_skCurrentCanvas->getDeviceSize();
    SkRect canvasRect = SkRect::MakeXYWH(0, 0, canvasSize.width(), canvasSize.height());
    m_skCurrentCanvas->clipRect(canvasRect, SkRegion::kReplace_Op);
}

void SoftwareRenderer::finish()
{
}

void SoftwareRenderer::bindFramebufferToOutputSurface(DrawingFrame& frame)
{
    m_currentFramebufferLock.reset();
    m_skCurrentCanvas = m_skRootCanvas.get();
}

bool SoftwareRenderer::bindFramebufferToTexture(DrawingFrame& frame, const ScopedResource* texture, const gfx::Rect& framebufferRect)
{
    m_currentFramebufferLock = make_scoped_ptr(new ResourceProvider::ScopedWriteLockSoftware(m_resourceProvider, texture->id()));
    m_skCurrentCanvas = m_currentFramebufferLock->skCanvas();
    initializeMatrices(frame, framebufferRect, false);
    setDrawViewportSize(framebufferRect.size());

    return true;
}

void SoftwareRenderer::setScissorTestRect(const gfx::Rect& scissorRect)
{
    m_skCurrentCanvas->clipRect(gfx::RectToSkRect(scissorRect), SkRegion::kReplace_Op);
}

void SoftwareRenderer::clearFramebuffer(DrawingFrame& frame)
{
    if (frame.currentRenderPass->has_transparent_background) {
        m_skCurrentCanvas->clear(SkColorSetARGB(0, 0, 0, 0));
    } else {
#ifndef NDEBUG
        // On DEBUG builds, opaque render passes are cleared to blue to easily see regions that were not drawn on the screen.
        m_skCurrentCanvas->clear(SkColorSetARGB(255, 0, 0, 255));
#endif
    }
}

void SoftwareRenderer::setDrawViewportSize(const gfx::Size& viewportSize)
{
}

bool SoftwareRenderer::isSoftwareResource(ResourceProvider::ResourceId id) const
{
    switch (m_resourceProvider->resourceType(id)) {
    case ResourceProvider::GLTexture:
        return false;
    case ResourceProvider::Bitmap:
        return true;
    }

    LOG(FATAL) << "Invalid resource type.";
    return false;
}

void SoftwareRenderer::drawQuad(DrawingFrame& frame, const DrawQuad* quad)
{
    TRACE_EVENT0("cc", "SoftwareRenderer::drawQuad");
    WebTransformationMatrix quadRectMatrix;
    quadRectTransform(&quadRectMatrix, quad->quadTransform(), quad->rect);
    WebTransformationMatrix contentsDeviceTransform = (frame.windowMatrix * frame.projectionMatrix * quadRectMatrix).to2dTransform();
    SkMatrix skDeviceMatrix;
    toSkMatrix(&skDeviceMatrix, contentsDeviceTransform);
    m_skCurrentCanvas->setMatrix(skDeviceMatrix);

    m_skCurrentPaint.reset();
    if (!isScaleAndTranslate(skDeviceMatrix)) {
      m_skCurrentPaint.setAntiAlias(true);
      m_skCurrentPaint.setFilterBitmap(true);
    }

    if (quad->ShouldDrawWithBlending()) {
        m_skCurrentPaint.setAlpha(quad->opacity() * 255);
        m_skCurrentPaint.setXfermodeMode(SkXfermode::kSrcOver_Mode);
    } else {
        m_skCurrentPaint.setXfermodeMode(SkXfermode::kSrc_Mode);
    }

    switch (quad->material) {
    case DrawQuad::DEBUG_BORDER:
        drawDebugBorderQuad(frame, DebugBorderDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::SOLID_COLOR:
        drawSolidColorQuad(frame, SolidColorDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::TEXTURE_CONTENT:
        drawTextureQuad(frame, TextureDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::TILED_CONTENT:
        drawTileQuad(frame, TileDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::RENDER_PASS:
        drawRenderPassQuad(frame, RenderPassDrawQuad::MaterialCast(quad));
        break;
    default:
        drawUnsupportedQuad(frame, quad);
        break;
    }

    m_skCurrentCanvas->resetMatrix();
}

void SoftwareRenderer::drawDebugBorderQuad(const DrawingFrame& frame, const DebugBorderDrawQuad* quad)
{
    // We need to apply the matrix manually to have pixel-sized stroke width.
    SkPoint vertices[4];
    gfx::RectFToSkRect(quadVertexRect()).toQuad(vertices);
    SkPoint transformedVertices[4];
    m_skCurrentCanvas->getTotalMatrix().mapPoints(transformedVertices, vertices, 4);
    m_skCurrentCanvas->resetMatrix();

    m_skCurrentPaint.setColor(quad->color);
    m_skCurrentPaint.setAlpha(quad->opacity() * SkColorGetA(quad->color));
    m_skCurrentPaint.setStyle(SkPaint::kStroke_Style);
    m_skCurrentPaint.setStrokeWidth(quad->width);
    m_skCurrentCanvas->drawPoints(SkCanvas::kPolygon_PointMode, 4, transformedVertices, m_skCurrentPaint);
}

void SoftwareRenderer::drawSolidColorQuad(const DrawingFrame& frame, const SolidColorDrawQuad* quad)
{
    m_skCurrentPaint.setColor(quad->color);
    m_skCurrentPaint.setAlpha(quad->opacity() * SkColorGetA(quad->color));
    m_skCurrentCanvas->drawRect(gfx::RectFToSkRect(quadVertexRect()), m_skCurrentPaint);
}

void SoftwareRenderer::drawTextureQuad(const DrawingFrame& frame, const TextureDrawQuad* quad)
{
    if (!isSoftwareResource(quad->resource_id)) {
        drawUnsupportedQuad(frame, quad);
        return;
    }

    // FIXME: Add support for non-premultiplied alpha.
    ResourceProvider::ScopedReadLockSoftware lock(m_resourceProvider, quad->resource_id);
    const SkBitmap* bitmap = lock.skBitmap();
    gfx::RectF uvRect = gfx::ScaleRect(quad->uv_rect, bitmap->width(), bitmap->height());
    SkRect skUvRect = gfx::RectFToSkRect(uvRect);
    if (quad->flipped)
        m_skCurrentCanvas->scale(1, -1);
    m_skCurrentCanvas->drawBitmapRectToRect(*bitmap, &skUvRect,
                                            gfx::RectFToSkRect(quadVertexRect()),
                                            &m_skCurrentPaint);
}

void SoftwareRenderer::drawTileQuad(const DrawingFrame& frame, const TileDrawQuad* quad)
{
    DCHECK(isSoftwareResource(quad->resource_id));
    ResourceProvider::ScopedReadLockSoftware lock(m_resourceProvider, quad->resource_id);

    SkRect uvRect = gfx::RectFToSkRect(quad->tex_coord_rect);
    m_skCurrentPaint.setFilterBitmap(true);
    m_skCurrentCanvas->drawBitmapRectToRect(*lock.skBitmap(), &uvRect,
                                            gfx::RectFToSkRect(quadVertexRect()),
                                            &m_skCurrentPaint);
}

void SoftwareRenderer::drawRenderPassQuad(const DrawingFrame& frame, const RenderPassDrawQuad* quad)
{
    CachedResource* contentTexture = m_renderPassTextures.get(quad->render_pass_id);
    if (!contentTexture || !contentTexture->id())
        return;

    const RenderPass* renderPass = frame.renderPassesById->get(quad->render_pass_id);
    DCHECK(renderPass);
    if (!renderPass)
        return;

    DCHECK(isSoftwareResource(contentTexture->id()));
    ResourceProvider::ScopedReadLockSoftware lock(m_resourceProvider, contentTexture->id());

    SkRect destRect = gfx::RectFToSkRect(quadVertexRect());

    const SkBitmap* content = lock.skBitmap();

    SkRect contentRect;
    content->getBounds(&contentRect);

    SkMatrix contentMat;
    contentMat.setRectToRect(contentRect, destRect, SkMatrix::kFill_ScaleToFit);

    SkAutoTUnref<SkShader> shader(SkShader::CreateBitmapShader(*content,
                                                               SkShader::kClamp_TileMode,
                                                               SkShader::kClamp_TileMode));
    shader->setLocalMatrix(contentMat);
    m_skCurrentPaint.setShader(shader);

    SkImageFilter* filter = renderPass->filter;
    if (filter)
        m_skCurrentPaint.setImageFilter(filter);

    if (quad->mask_resource_id) {
        ResourceProvider::ScopedReadLockSoftware maskLock(m_resourceProvider, quad->mask_resource_id);

        const SkBitmap* mask = maskLock.skBitmap();

        SkRect maskRect = SkRect::MakeXYWH(
            quad->mask_tex_coord_offset_x * mask->width(),
            quad->mask_tex_coord_offset_y * mask->height(),
            quad->mask_tex_coord_scale_x * mask->width(),
            quad->mask_tex_coord_scale_y * mask->height());

        SkMatrix maskMat;
        maskMat.setRectToRect(maskRect, destRect, SkMatrix::kFill_ScaleToFit);

        SkAutoTUnref<SkShader> maskShader(SkShader::CreateBitmapShader(*mask,
                                                                       SkShader::kClamp_TileMode,
                                                                       SkShader::kClamp_TileMode));
        maskShader->setLocalMatrix(maskMat);

        SkPaint maskPaint;
        maskPaint.setShader(maskShader);

        SkAutoTUnref<SkLayerRasterizer> maskRasterizer(new SkLayerRasterizer);
        maskRasterizer->addLayer(maskPaint);

        m_skCurrentPaint.setRasterizer(maskRasterizer);
        m_skCurrentCanvas->drawRect(destRect, m_skCurrentPaint);
    } else {
        // FIXME: Apply background filters and blend with content
        m_skCurrentCanvas->drawRect(destRect, m_skCurrentPaint);
    }
}

void SoftwareRenderer::drawUnsupportedQuad(const DrawingFrame& frame, const DrawQuad* quad)
{
    m_skCurrentPaint.setColor(SK_ColorMAGENTA);
    m_skCurrentPaint.setAlpha(quad->opacity() * 255);
    m_skCurrentCanvas->drawRect(gfx::RectFToSkRect(quadVertexRect()), m_skCurrentPaint);
}

bool SoftwareRenderer::swapBuffers()
{
    if (m_client->hasImplThread())
        m_client->onSwapBuffersComplete();
    return true;
}

void SoftwareRenderer::getFramebufferPixels(void *pixels, const gfx::Rect& rect)
{
    TRACE_EVENT0("cc", "SoftwareRenderer::getFramebufferPixels");
    SkBitmap fullBitmap = m_outputDevice->lock(false)->getSkBitmap();
    SkBitmap subsetBitmap;
    SkIRect invertRect = SkIRect::MakeXYWH(rect.x(), viewportSize().height() - rect.bottom(), rect.width(), rect.height());
    fullBitmap.extractSubset(&subsetBitmap, invertRect);
    subsetBitmap.copyPixelsTo(pixels, rect.width() * rect.height() * 4, rect.width() * 4);
    m_outputDevice->unlock();
}

void SoftwareRenderer::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
}

}  // namespace cc
