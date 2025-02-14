// Copyright 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/gl_renderer.h"

#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "build/build_config.h"
#include "cc/damage_tracker.h"
#include "cc/geometry_binding.h"
#include "cc/layer_quad.h"
#include "cc/math_util.h"
#include "cc/platform_color.h"
#include "cc/priority_calculator.h"
#include "cc/proxy.h"
#include "cc/render_pass.h"
#include "cc/render_surface_filters.h"
#include "cc/scoped_resource.h"
#include "cc/single_thread_proxy.h"
#include "cc/stream_video_draw_quad.h"
#include "cc/texture_draw_quad.h"
#include "cc/video_layer_impl.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/gpu/GrContext.h"
#include "third_party/skia/include/gpu/GrTexture.h"
#include "third_party/skia/include/gpu/SkGpuDevice.h"
#include "third_party/skia/include/gpu/SkGrTexturePixelRef.h"
#include "ui/gfx/quad_f.h"
#include "ui/gfx/rect_conversions.h"
#include <public/WebGraphicsContext3D.h>
#include <public/WebSharedGraphicsContext3D.h>
#include <set>
#include <string>
#include <vector>

using namespace std;
using WebKit::WebGraphicsContext3D;
using WebKit::WebGraphicsMemoryAllocation;
using WebKit::WebSharedGraphicsContext3D;
using WebKit::WebTransformationMatrix;

namespace cc {

namespace {

bool needsIOSurfaceReadbackWorkaround()
{
#if defined(OS_MACOSX)
    return true;
#else
    return false;
#endif
}

} // anonymous namespace

scoped_ptr<GLRenderer> GLRenderer::create(RendererClient* client, ResourceProvider* resourceProvider)
{
    scoped_ptr<GLRenderer> renderer(make_scoped_ptr(new GLRenderer(client, resourceProvider)));
    if (!renderer->initialize())
        return scoped_ptr<GLRenderer>();

    return renderer.Pass();
}

GLRenderer::GLRenderer(RendererClient* client, ResourceProvider* resourceProvider)
    : DirectRenderer(client, resourceProvider)
    , m_offscreenFramebufferId(0)
    , m_sharedGeometryQuad(gfx::RectF(-0.5f, -0.5f, 1.0f, 1.0f))
    , m_context(resourceProvider->graphicsContext3D())
    , m_isViewportChanged(false)
    , m_isFramebufferDiscarded(false)
    , m_discardFramebufferWhenNotVisible(false)
    , m_isUsingBindUniform(false)
    , m_visible(true)
    , m_isScissorEnabled(false)
{
    DCHECK(m_context);
}

bool GLRenderer::initialize()
{
    if (!m_context->makeContextCurrent())
        return false;

    m_context->setContextLostCallback(this);
    m_context->pushGroupMarkerEXT("CompositorContext");

    std::string extensionsString = UTF16ToASCII(m_context->getString(GL_EXTENSIONS));
    std::vector<std::string> extensionsList;
    base::SplitString(extensionsString, ' ', &extensionsList);
    std::set<string> extensions(extensionsList.begin(), extensionsList.end());

    if (settings().acceleratePainting && extensions.count("GL_EXT_texture_format_BGRA8888")
                                      && extensions.count("GL_EXT_read_format_bgra"))
        m_capabilities.usingAcceleratedPainting = true;
    else
        m_capabilities.usingAcceleratedPainting = false;


    m_capabilities.contextHasCachedFrontBuffer = extensions.count("GL_CHROMIUM_front_buffer_cached");

    m_capabilities.usingPartialSwap = settings().partialSwapEnabled && extensions.count("GL_CHROMIUM_post_sub_buffer");

    // Use the swapBuffers callback only with the threaded proxy.
    if (m_client->hasImplThread())
        m_capabilities.usingSwapCompleteCallback = extensions.count("GL_CHROMIUM_swapbuffers_complete_callback");
    if (m_capabilities.usingSwapCompleteCallback)
        m_context->setSwapBuffersCompleteCallbackCHROMIUM(this);

    m_capabilities.usingSetVisibility = extensions.count("GL_CHROMIUM_set_visibility");

    if (extensions.count("GL_CHROMIUM_iosurface"))
        DCHECK(extensions.count("GL_ARB_texture_rectangle"));

    m_capabilities.usingGpuMemoryManager = extensions.count("GL_CHROMIUM_gpu_memory_manager");
    if (m_capabilities.usingGpuMemoryManager)
        m_context->setMemoryAllocationChangedCallbackCHROMIUM(this);

    m_capabilities.usingDiscardFramebuffer = extensions.count("GL_CHROMIUM_discard_framebuffer");

    m_capabilities.usingEglImage = extensions.count("GL_OES_EGL_image_external");

    GLC(m_context, m_context->getIntegerv(GL_MAX_TEXTURE_SIZE, &m_capabilities.maxTextureSize));
    m_capabilities.bestTextureFormat = PlatformColor::bestTextureFormat(m_context, extensions.count("GL_EXT_texture_format_BGRA8888"));

    m_isUsingBindUniform = extensions.count("GL_CHROMIUM_bind_uniform_location");

    // Make sure scissoring starts as disabled.
    GLC(m_context, m_context->disable(GL_SCISSOR_TEST));
    DCHECK(!m_isScissorEnabled);

    if (!initializeSharedObjects())
        return false;

    // Make sure the viewport and context gets initialized, even if it is to zero.
    viewportChanged();
    return true;
}

GLRenderer::~GLRenderer()
{
    m_context->setSwapBuffersCompleteCallbackCHROMIUM(0);
    m_context->setMemoryAllocationChangedCallbackCHROMIUM(0);
    m_context->setContextLostCallback(0);
    cleanupSharedObjects();
}

const RendererCapabilities& GLRenderer::capabilities() const
{
    return m_capabilities;
}

WebGraphicsContext3D* GLRenderer::context()
{
    return m_context;
}

void GLRenderer::debugGLCall(WebGraphicsContext3D* context, const char* command, const char* file, int line)
{
    unsigned long error = context->getError();
    if (error != GL_NO_ERROR)
        LOG(ERROR) << "GL command failed: File: " << file << "\n\tLine " << line << "\n\tcommand: " << command << ", error " << static_cast<int>(error) << "\n";
}

void GLRenderer::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;

    enforceMemoryPolicy();

    // TODO: Replace setVisibilityCHROMIUM with an extension to explicitly manage front/backbuffers
    // crbug.com/116049
    if (m_capabilities.usingSetVisibility)
        m_context->setVisibilityCHROMIUM(visible);
}

void GLRenderer::sendManagedMemoryStats(size_t bytesVisible, size_t bytesVisibleAndNearby, size_t bytesAllocated)
{
    WebKit::WebGraphicsManagedMemoryStats stats;
    stats.bytesVisible = bytesVisible;
    stats.bytesVisibleAndNearby = bytesVisibleAndNearby;
    stats.bytesAllocated = bytesAllocated;
    stats.backbufferRequested = !m_isFramebufferDiscarded;
    m_context->sendManagedMemoryStatsCHROMIUM(&stats);
}

void GLRenderer::releaseRenderPassTextures()
{
    m_renderPassTextures.clear();
}

void GLRenderer::viewportChanged()
{
    m_isViewportChanged = true;
}

void GLRenderer::clearFramebuffer(DrawingFrame& frame)
{
    // On DEBUG builds, opaque render passes are cleared to blue to easily see regions that were not drawn on the screen.
    if (frame.currentRenderPass->has_transparent_background)
        GLC(m_context, m_context->clearColor(0, 0, 0, 0));
    else
        GLC(m_context, m_context->clearColor(0, 0, 1, 1));

#ifdef NDEBUG
    if (frame.currentRenderPass->has_transparent_background)
#endif
        m_context->clear(GL_COLOR_BUFFER_BIT);
}

void GLRenderer::beginDrawingFrame(DrawingFrame& frame)
{
    // FIXME: Remove this once framebuffer is automatically recreated on first use
    ensureFramebuffer();

    if (viewportSize().IsEmpty())
        return;

    TRACE_EVENT0("cc", "GLRenderer::drawLayers");
    if (m_isViewportChanged) {
        // Only reshape when we know we are going to draw. Otherwise, the reshape
        // can leave the window at the wrong size if we never draw and the proper
        // viewport size is never set.
        m_isViewportChanged = false;
        m_context->reshape(viewportWidth(), viewportHeight());
    }

    makeContextCurrent();
    // Bind the common vertex attributes used for drawing all the layers.
    m_sharedGeometry->prepareForDraw();

    GLC(m_context, m_context->disable(GL_DEPTH_TEST));
    GLC(m_context, m_context->disable(GL_CULL_FACE));
    GLC(m_context, m_context->colorMask(true, true, true, true));
    GLC(m_context, m_context->enable(GL_BLEND));
    GLC(m_context, m_context->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
    GLC(context(), context()->activeTexture(GL_TEXTURE0));
}

void GLRenderer::doNoOp()
{
    GLC(m_context, m_context->bindFramebuffer(GL_FRAMEBUFFER, 0));
    GLC(m_context, m_context->flush());
}

void GLRenderer::drawQuad(DrawingFrame& frame, const DrawQuad* quad)
{
    DCHECK(quad->rect.Contains(quad->visible_rect));

    if (quad->ShouldDrawWithBlending())
        GLC(m_context, m_context->enable(GL_BLEND));
    else
        GLC(m_context, m_context->disable(GL_BLEND));

    switch (quad->material) {
    case DrawQuad::INVALID:
        NOTREACHED();
        break;
    case DrawQuad::CHECKERBOARD:
        drawCheckerboardQuad(frame, CheckerboardDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::DEBUG_BORDER:
        drawDebugBorderQuad(frame, DebugBorderDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::IO_SURFACE_CONTENT:
        drawIOSurfaceQuad(frame, IOSurfaceDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::RENDER_PASS:
        drawRenderPassQuad(frame, RenderPassDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::SOLID_COLOR:
        drawSolidColorQuad(frame, SolidColorDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::STREAM_VIDEO_CONTENT:
        drawStreamVideoQuad(frame, StreamVideoDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::TEXTURE_CONTENT:
        drawTextureQuad(frame, TextureDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::TILED_CONTENT:
        drawTileQuad(frame, TileDrawQuad::MaterialCast(quad));
        break;
    case DrawQuad::YUV_VIDEO_CONTENT:
        drawYUVVideoQuad(frame, YUVVideoDrawQuad::MaterialCast(quad));
        break;
    }
}

void GLRenderer::drawCheckerboardQuad(const DrawingFrame& frame, const CheckerboardDrawQuad* quad)
{
    const TileCheckerboardProgram* program = tileCheckerboardProgram();
    DCHECK(program && (program->initialized() || isContextLost()));
    GLC(context(), context()->useProgram(program->program()));

    SkColor color = quad->color;
    GLC(context(), context()->uniform4f(program->fragmentShader().colorLocation(), SkColorGetR(color) / 255.0, SkColorGetG(color) / 255.0, SkColorGetB(color) / 255.0, 1));

    const int checkerboardWidth = 16;
    float frequency = 1.0 / checkerboardWidth;

    gfx::Rect tileRect = quad->rect;
    float texOffsetX = tileRect.x() % checkerboardWidth;
    float texOffsetY = tileRect.y() % checkerboardWidth;
    float texScaleX = tileRect.width();
    float texScaleY = tileRect.height();
    GLC(context(), context()->uniform4f(program->fragmentShader().texTransformLocation(), texOffsetX, texOffsetY, texScaleX, texScaleY));

    GLC(context(), context()->uniform1f(program->fragmentShader().frequencyLocation(), frequency));

    setShaderOpacity(quad->opacity(), program->fragmentShader().alphaLocation());
    drawQuadGeometry(frame, quad->quadTransform(), quad->rect, program->vertexShader().matrixLocation());
}

void GLRenderer::drawDebugBorderQuad(const DrawingFrame& frame, const DebugBorderDrawQuad* quad)
{
    static float glMatrix[16];
    const SolidColorProgram* program = solidColorProgram();
    DCHECK(program && (program->initialized() || isContextLost()));
    GLC(context(), context()->useProgram(program->program()));

    // Use the full quadRect for debug quads to not move the edges based on partial swaps.
    const gfx::Rect& layerRect = quad->rect;
    WebTransformationMatrix renderMatrix = quad->quadTransform();
    renderMatrix.translate(0.5 * layerRect.width() + layerRect.x(), 0.5 * layerRect.height() + layerRect.y());
    renderMatrix.scaleNonUniform(layerRect.width(), layerRect.height());
    GLRenderer::toGLMatrix(&glMatrix[0], frame.projectionMatrix * renderMatrix);
    GLC(context(), context()->uniformMatrix4fv(program->vertexShader().matrixLocation(), 1, false, &glMatrix[0]));

    SkColor color = quad->color;
    float alpha = SkColorGetA(color) / 255.0;

    GLC(context(), context()->uniform4f(program->fragmentShader().colorLocation(), (SkColorGetR(color) / 255.0) * alpha, (SkColorGetG(color) / 255.0) * alpha, (SkColorGetB(color) / 255.0) * alpha, alpha));

    GLC(context(), context()->lineWidth(quad->width));

    // The indices for the line are stored in the same array as the triangle indices.
    GLC(context(), context()->drawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_SHORT, 6 * sizeof(unsigned short)));
}

static WebGraphicsContext3D* getFilterContext(bool hasImplThread)
{
    if (hasImplThread)
        return WebSharedGraphicsContext3D::compositorThreadContext();
    else
        return WebSharedGraphicsContext3D::mainThreadContext();
}

static GrContext* getFilterGrContext(bool hasImplThread)
{
    if (hasImplThread)
        return WebSharedGraphicsContext3D::compositorThreadGrContext();
    else
        return WebSharedGraphicsContext3D::mainThreadGrContext();
}

static inline SkBitmap applyFilters(GLRenderer* renderer, const WebKit::WebFilterOperations& filters, ScopedResource* sourceTexture, bool hasImplThread)
{
    if (filters.isEmpty())
        return SkBitmap();

    WebGraphicsContext3D* filterContext = getFilterContext(hasImplThread);
    GrContext* filterGrContext = getFilterGrContext(hasImplThread);

    if (!filterContext || !filterGrContext)
        return SkBitmap();

    renderer->context()->flush();

    ResourceProvider::ScopedWriteLockGL lock(renderer->resourceProvider(), sourceTexture->id());
    SkBitmap source = RenderSurfaceFilters::apply(filters, lock.textureId(), sourceTexture->size(), filterContext, filterGrContext);
    return source;
}

static SkBitmap applyImageFilter(GLRenderer* renderer, SkImageFilter* filter, ScopedResource* sourceTexture, bool hasImplThread)
{
    if (!filter)
        return SkBitmap();

    WebGraphicsContext3D* context3d = getFilterContext(hasImplThread);
    GrContext* grContext = getFilterGrContext(hasImplThread);

    if (!context3d || !grContext)
        return SkBitmap();

    renderer->context()->flush();

    ResourceProvider::ScopedWriteLockGL lock(renderer->resourceProvider(), sourceTexture->id());

    // Wrap the source texture in a Ganesh platform texture.
    GrPlatformTextureDesc platformTextureDescription;
    platformTextureDescription.fWidth = sourceTexture->size().width();
    platformTextureDescription.fHeight = sourceTexture->size().height();
    platformTextureDescription.fConfig = kSkia8888_GrPixelConfig;
    platformTextureDescription.fTextureHandle = lock.textureId();
    SkAutoTUnref<GrTexture> texture(grContext->createPlatformTexture(platformTextureDescription));

    // Place the platform texture inside an SkBitmap.
    SkBitmap source;
    source.setConfig(SkBitmap::kARGB_8888_Config, sourceTexture->size().width(), sourceTexture->size().height());
    source.setPixelRef(new SkGrPixelRef(texture.get()))->unref();

    // Create a scratch texture for backing store.
    GrTextureDesc desc;
    desc.fFlags = kRenderTarget_GrTextureFlagBit | kNoStencil_GrTextureFlagBit;
    desc.fSampleCnt = 0;
    desc.fWidth = source.width();
    desc.fHeight = source.height();
    desc.fConfig = kSkia8888_GrPixelConfig;
    GrAutoScratchTexture scratchTexture(grContext, desc, GrContext::kExact_ScratchTexMatch);
    SkAutoTUnref<GrTexture> backingStore(scratchTexture.detach());

    // Create a device and canvas using that backing store.
    SkGpuDevice device(grContext, backingStore.get());
    SkCanvas canvas(&device);

    // Draw the source bitmap through the filter to the canvas.
    SkPaint paint;
    paint.setImageFilter(filter);
    canvas.clear(0x0);
    canvas.drawSprite(source, 0, 0, &paint);
    canvas.flush();
    context3d->flush();
    return device.accessBitmap(false);
}

scoped_ptr<ScopedResource> GLRenderer::drawBackgroundFilters(
    DrawingFrame& frame, const RenderPassDrawQuad* quad,
    const WebKit::WebFilterOperations& filters,
    const WebTransformationMatrix& contentsDeviceTransform,
    const WebTransformationMatrix& contentsDeviceTransformInverse)
{
    // This method draws a background filter, which applies a filter to any pixels behind the quad and seen through its background.
    // The algorithm works as follows:
    // 1. Compute a bounding box around the pixels that will be visible through the quad.
    // 2. Read the pixels in the bounding box into a buffer R.
    // 3. Apply the background filter to R, so that it is applied in the pixels' coordinate space.
    // 4. Apply the quad's inverse transform to map the pixels in R into the quad's content space. This implicitly
    // clips R by the content bounds of the quad since the destination texture has bounds matching the quad's content.
    // 5. Draw the background texture for the contents using the same transform as used to draw the contents itself. This is done
    // without blending to replace the current background pixels with the new filtered background.
    // 6. Draw the contents of the quad over drop of the new background with blending, as per usual. The filtered background
    // pixels will show through any non-opaque pixels in this draws.
    //
    // Pixel copies in this algorithm occur at steps 2, 3, 4, and 5.

    // FIXME: When this algorithm changes, update LayerTreeHost::prioritizeTextures() accordingly.

    if (filters.isEmpty())
        return scoped_ptr<ScopedResource>();

    // FIXME: We only allow background filters on an opaque render surface because other surfaces may contain
    // translucent pixels, and the contents behind those translucent pixels wouldn't have the filter applied.
    if (frame.currentRenderPass->has_transparent_background)
        return scoped_ptr<ScopedResource>();
    DCHECK(!frame.currentTexture);

    // FIXME: Do a single readback for both the surface and replica and cache the filtered results (once filter textures are not reused).
    gfx::Rect deviceRect = gfx::ToEnclosingRect(MathUtil::mapClippedRect(contentsDeviceTransform, sharedGeometryQuad().BoundingBox()));

    int top, right, bottom, left;
    filters.getOutsets(top, right, bottom, left);
    deviceRect.Inset(-left, -top, -right, -bottom);

    deviceRect.Intersect(frame.currentRenderPass->output_rect);

    scoped_ptr<ScopedResource> deviceBackgroundTexture = ScopedResource::create(m_resourceProvider);
    if (!getFramebufferTexture(deviceBackgroundTexture.get(), deviceRect))
        return scoped_ptr<ScopedResource>();

    SkBitmap filteredDeviceBackground = applyFilters(this, filters, deviceBackgroundTexture.get(), m_client->hasImplThread());
    if (!filteredDeviceBackground.getTexture())
        return scoped_ptr<ScopedResource>();

    GrTexture* texture = reinterpret_cast<GrTexture*>(filteredDeviceBackground.getTexture());
    int filteredDeviceBackgroundTextureId = texture->getTextureHandle();

    scoped_ptr<ScopedResource> backgroundTexture = ScopedResource::create(m_resourceProvider);
    if (!backgroundTexture->Allocate(Renderer::ImplPool, quad->rect.size(), GL_RGBA, ResourceProvider::TextureUsageFramebuffer))
        return scoped_ptr<ScopedResource>();

    const RenderPass* targetRenderPass = frame.currentRenderPass;
    bool usingBackgroundTexture = useScopedTexture(frame, backgroundTexture.get(), quad->rect);

    if (usingBackgroundTexture) {
        // Copy the readback pixels from device to the background texture for the surface.
        WebTransformationMatrix deviceToFramebufferTransform;
        deviceToFramebufferTransform.translate(quad->rect.width() / 2.0, quad->rect.height() / 2.0);
        deviceToFramebufferTransform.scale3d(quad->rect.width(), quad->rect.height(), 1);
        deviceToFramebufferTransform.multiply(contentsDeviceTransformInverse);
        copyTextureToFramebuffer(frame, filteredDeviceBackgroundTextureId, deviceRect, deviceToFramebufferTransform);
    }

    useRenderPass(frame, targetRenderPass);

    if (!usingBackgroundTexture)
        return scoped_ptr<ScopedResource>();
    return backgroundTexture.Pass();
}

void GLRenderer::drawRenderPassQuad(DrawingFrame& frame, const RenderPassDrawQuad* quad)
{
    CachedResource* contentsTexture = m_renderPassTextures.get(quad->render_pass_id);
    if (!contentsTexture || !contentsTexture->id())
        return;

    const RenderPass* renderPass = frame.renderPassesById->get(quad->render_pass_id);
    DCHECK(renderPass);
    if (!renderPass)
        return;

    WebTransformationMatrix quadRectMatrix;
    quadRectTransform(&quadRectMatrix, quad->quadTransform(), quad->rect);
    WebTransformationMatrix contentsDeviceTransform = (frame.windowMatrix * frame.projectionMatrix * quadRectMatrix).to2dTransform();

    // Can only draw surface if device matrix is invertible.
    if (!contentsDeviceTransform.isInvertible())
        return;

    WebTransformationMatrix contentsDeviceTransformInverse = contentsDeviceTransform.inverse();
    scoped_ptr<ScopedResource> backgroundTexture = drawBackgroundFilters(
        frame, quad, renderPass->background_filters,
        contentsDeviceTransform, contentsDeviceTransformInverse);

    // FIXME: Cache this value so that we don't have to do it for both the surface and its replica.
    // Apply filters to the contents texture.
    SkBitmap filterBitmap;
    if (renderPass->filter) {
        filterBitmap = applyImageFilter(this, renderPass->filter, contentsTexture, m_client->hasImplThread());
    } else {
        filterBitmap = applyFilters(this, renderPass->filters, contentsTexture, m_client->hasImplThread());
    }
    scoped_ptr<ResourceProvider::ScopedReadLockGL> contentsResourceLock;
    unsigned contentsTextureId = 0;
    if (filterBitmap.getTexture()) {
        GrTexture* texture = reinterpret_cast<GrTexture*>(filterBitmap.getTexture());
        contentsTextureId = texture->getTextureHandle();
    } else {
        contentsResourceLock = make_scoped_ptr(new ResourceProvider::ScopedReadLockGL(m_resourceProvider, contentsTexture->id()));
        contentsTextureId = contentsResourceLock->textureId();
    }

    // Draw the background texture if there is one.
    if (backgroundTexture) {
        DCHECK(backgroundTexture->size() == quad->rect.size());
        ResourceProvider::ScopedReadLockGL lock(m_resourceProvider, backgroundTexture->id());
        copyTextureToFramebuffer(frame, lock.textureId(), quad->rect, quad->quadTransform());
    }

    bool clipped = false;
    gfx::QuadF deviceQuad = MathUtil::mapQuad(contentsDeviceTransform, sharedGeometryQuad(), clipped);
    DCHECK(!clipped);
    LayerQuad deviceLayerBounds = LayerQuad(gfx::QuadF(deviceQuad.BoundingBox()));
    LayerQuad deviceLayerEdges = LayerQuad(deviceQuad);

    // Use anti-aliasing programs only when necessary.
    bool useAA = (!deviceQuad.IsRectilinear() || !deviceQuad.BoundingBox().IsExpressibleAsRect());
    if (useAA) {
        deviceLayerBounds.inflateAntiAliasingDistance();
        deviceLayerEdges.inflateAntiAliasingDistance();
    }

    scoped_ptr<ResourceProvider::ScopedReadLockGL> maskResourceLock;
    unsigned maskTextureId = 0;
    if (quad->mask_resource_id) {
        maskResourceLock.reset(new ResourceProvider::ScopedReadLockGL(m_resourceProvider, quad->mask_resource_id));
        maskTextureId = maskResourceLock->textureId();
    }

    // FIXME: use the backgroundTexture and blend the background in with this draw instead of having a separate copy of the background texture.

    context()->bindTexture(GL_TEXTURE_2D, contentsTextureId);

    int shaderQuadLocation = -1;
    int shaderEdgeLocation = -1;
    int shaderMaskSamplerLocation = -1;
    int shaderMaskTexCoordScaleLocation = -1;
    int shaderMaskTexCoordOffsetLocation = -1;
    int shaderMatrixLocation = -1;
    int shaderAlphaLocation = -1;
    if (useAA && maskTextureId) {
        const RenderPassMaskProgramAA* program = renderPassMaskProgramAA();
        GLC(context(), context()->useProgram(program->program()));
        GLC(context(), context()->uniform1i(program->fragmentShader().samplerLocation(), 0));

        shaderQuadLocation = program->vertexShader().pointLocation();
        shaderEdgeLocation = program->fragmentShader().edgeLocation();
        shaderMaskSamplerLocation = program->fragmentShader().maskSamplerLocation();
        shaderMaskTexCoordScaleLocation = program->fragmentShader().maskTexCoordScaleLocation();
        shaderMaskTexCoordOffsetLocation = program->fragmentShader().maskTexCoordOffsetLocation();
        shaderMatrixLocation = program->vertexShader().matrixLocation();
        shaderAlphaLocation = program->fragmentShader().alphaLocation();
    } else if (!useAA && maskTextureId) {
        const RenderPassMaskProgram* program = renderPassMaskProgram();
        GLC(context(), context()->useProgram(program->program()));
        GLC(context(), context()->uniform1i(program->fragmentShader().samplerLocation(), 0));

        shaderMaskSamplerLocation = program->fragmentShader().maskSamplerLocation();
        shaderMaskTexCoordScaleLocation = program->fragmentShader().maskTexCoordScaleLocation();
        shaderMaskTexCoordOffsetLocation = program->fragmentShader().maskTexCoordOffsetLocation();
        shaderMatrixLocation = program->vertexShader().matrixLocation();
        shaderAlphaLocation = program->fragmentShader().alphaLocation();
    } else if (useAA && !maskTextureId) {
        const RenderPassProgramAA* program = renderPassProgramAA();
        GLC(context(), context()->useProgram(program->program()));
        GLC(context(), context()->uniform1i(program->fragmentShader().samplerLocation(), 0));

        shaderQuadLocation = program->vertexShader().pointLocation();
        shaderEdgeLocation = program->fragmentShader().edgeLocation();
        shaderMatrixLocation = program->vertexShader().matrixLocation();
        shaderAlphaLocation = program->fragmentShader().alphaLocation();
    } else {
        const RenderPassProgram* program = renderPassProgram();
        GLC(context(), context()->useProgram(program->program()));
        GLC(context(), context()->uniform1i(program->fragmentShader().samplerLocation(), 0));

        shaderMatrixLocation = program->vertexShader().matrixLocation();
        shaderAlphaLocation = program->fragmentShader().alphaLocation();
    }

    if (shaderMaskSamplerLocation != -1) {
        DCHECK(shaderMaskTexCoordScaleLocation != 1);
        DCHECK(shaderMaskTexCoordOffsetLocation != 1);
        GLC(context(), context()->activeTexture(GL_TEXTURE1));
        GLC(context(), context()->uniform1i(shaderMaskSamplerLocation, 1));
        GLC(context(), context()->uniform2f(shaderMaskTexCoordScaleLocation, quad->mask_tex_coord_scale_x, quad->mask_tex_coord_scale_y));
        GLC(context(), context()->uniform2f(shaderMaskTexCoordOffsetLocation, quad->mask_tex_coord_offset_x, quad->mask_tex_coord_offset_y));
        context()->bindTexture(GL_TEXTURE_2D, maskTextureId);
        GLC(context(), context()->activeTexture(GL_TEXTURE0));
    }

    if (shaderEdgeLocation != -1) {
        float edge[24];
        deviceLayerEdges.toFloatArray(edge);
        deviceLayerBounds.toFloatArray(&edge[12]);
        GLC(context(), context()->uniform3fv(shaderEdgeLocation, 8, edge));
    }

    // Map device space quad to surface space. contentsDeviceTransform has no 3d component since it was generated with to2dTransform() so we don't need to project.
    gfx::QuadF surfaceQuad = MathUtil::mapQuad(contentsDeviceTransformInverse, deviceLayerEdges.ToQuadF(), clipped);
    DCHECK(!clipped);

    setShaderOpacity(quad->opacity(), shaderAlphaLocation);
    setShaderQuadF(surfaceQuad, shaderQuadLocation);
    drawQuadGeometry(frame, quad->quadTransform(), quad->rect, shaderMatrixLocation);

    // Flush the compositor context before the filter bitmap goes out of
    // scope, so the draw gets processed before the filter texture gets deleted.
    if (filterBitmap.getTexture())
        m_context->flush();
}

void GLRenderer::drawSolidColorQuad(const DrawingFrame& frame, const SolidColorDrawQuad* quad)
{
    const SolidColorProgram* program = solidColorProgram();
    GLC(context(), context()->useProgram(program->program()));

    SkColor color = quad->color;
    float opacity = quad->opacity();
    float alpha = (SkColorGetA(color) / 255.0) * opacity;

    GLC(context(), context()->uniform4f(program->fragmentShader().colorLocation(), (SkColorGetR(color) / 255.0) * alpha, (SkColorGetG(color) / 255.0) * alpha, (SkColorGetB(color) / 255.0) * alpha, alpha));

    drawQuadGeometry(frame, quad->quadTransform(), quad->rect, program->vertexShader().matrixLocation());
}

struct TileProgramUniforms {
    unsigned program;
    unsigned samplerLocation;
    unsigned vertexTexTransformLocation;
    unsigned fragmentTexTransformLocation;
    unsigned edgeLocation;
    unsigned matrixLocation;
    unsigned alphaLocation;
    unsigned pointLocation;
};

template<class T>
static void tileUniformLocation(T program, TileProgramUniforms& uniforms)
{
    uniforms.program = program->program();
    uniforms.vertexTexTransformLocation = program->vertexShader().vertexTexTransformLocation();
    uniforms.matrixLocation = program->vertexShader().matrixLocation();
    uniforms.pointLocation = program->vertexShader().pointLocation();

    uniforms.samplerLocation = program->fragmentShader().samplerLocation();
    uniforms.alphaLocation = program->fragmentShader().alphaLocation();
    uniforms.fragmentTexTransformLocation = program->fragmentShader().fragmentTexTransformLocation();
    uniforms.edgeLocation = program->fragmentShader().edgeLocation();
}

void GLRenderer::drawTileQuad(const DrawingFrame& frame, const TileDrawQuad* quad)
{
    gfx::Rect tileRect = quad->visible_rect;

    gfx::RectF texCoordRect = quad->tex_coord_rect;
    float texToGeomScaleX = quad->rect.width() / texCoordRect.width();
    float texToGeomScaleY = quad->rect.height() / texCoordRect.height();

    // texCoordRect corresponds to quadRect, but quadVisibleRect may be
    // smaller than quadRect due to occlusion or clipping. Adjust
    // texCoordRect to match.
    gfx::Vector2d topLeftDiff = tileRect.origin() - quad->rect.origin();
    gfx::Vector2d bottomRightDiff =
        tileRect.bottom_right() - quad->rect.bottom_right();
    texCoordRect.Inset(topLeftDiff.x() / texToGeomScaleX,
                       topLeftDiff.y() / texToGeomScaleY,
                       -bottomRightDiff.x() / texToGeomScaleX,
                       -bottomRightDiff.y() / texToGeomScaleY);

    gfx::RectF clampGeomRect(tileRect);
    gfx::RectF clampTexRect(texCoordRect);
    // Clamp texture coordinates to avoid sampling outside the layer
    // by deflating the tile region half a texel or half a texel
    // minus epsilon for one pixel layers. The resulting clamp region
    // is mapped to the unit square by the vertex shader and mapped
    // back to normalized texture coordinates by the fragment shader
    // after being clamped to 0-1 range.
    const float epsilon = 1 / 1024.0f;
    float texClampX = std::min(0.5f, 0.5f * clampTexRect.width() - epsilon);
    float texClampY = std::min(0.5f, 0.5f * clampTexRect.height() - epsilon);
    float geomClampX = std::min(texClampX * texToGeomScaleX,
                                0.5f * clampGeomRect.width() - epsilon);
    float geomClampY = std::min(texClampY * texToGeomScaleY,
                                0.5f * clampGeomRect.height() - epsilon);
    clampGeomRect.Inset(geomClampX, geomClampY, geomClampX, geomClampY);
    clampTexRect.Inset(texClampX, texClampY, texClampX, texClampY);

    // Map clamping rectangle to unit square.
    float vertexTexTranslateX = -clampGeomRect.x() / clampGeomRect.width();
    float vertexTexTranslateY = -clampGeomRect.y() / clampGeomRect.height();
    float vertexTexScaleX = tileRect.width() / clampGeomRect.width();
    float vertexTexScaleY = tileRect.height() / clampGeomRect.height();

    // Map to normalized texture coordinates.
    const gfx::Size& textureSize = quad->texture_size;
    float fragmentTexTranslateX = clampTexRect.x() / textureSize.width();
    float fragmentTexTranslateY = clampTexRect.y() / textureSize.height();
    float fragmentTexScaleX = clampTexRect.width() / textureSize.width();
    float fragmentTexScaleY = clampTexRect.height() / textureSize.height();


    gfx::QuadF localQuad;
    WebTransformationMatrix deviceTransform = WebTransformationMatrix(frame.windowMatrix * frame.projectionMatrix * quad->quadTransform()).to2dTransform();
    if (!deviceTransform.isInvertible())
        return;

    bool clipped = false;
    gfx::QuadF deviceLayerQuad = MathUtil::mapQuad(deviceTransform, gfx::QuadF(quad->visibleContentRect()), clipped);
    DCHECK(!clipped);

    TileProgramUniforms uniforms;
    // For now, we simply skip anti-aliasing with the quad is clipped. This only happens
    // on perspective transformed layers that go partially behind the camera.
    if (quad->IsAntialiased() && !clipped) {
        if (quad->swizzle_contents)
            tileUniformLocation(tileProgramSwizzleAA(), uniforms);
        else
            tileUniformLocation(tileProgramAA(), uniforms);
    } else {
        if (quad->ShouldDrawWithBlending()) {
            if (quad->swizzle_contents)
                tileUniformLocation(tileProgramSwizzle(), uniforms);
            else
                tileUniformLocation(tileProgram(), uniforms);
        } else {
            if (quad->swizzle_contents)
                tileUniformLocation(tileProgramSwizzleOpaque(), uniforms);
            else
                tileUniformLocation(tileProgramOpaque(), uniforms);
        }
    }

    GLC(context(), context()->useProgram(uniforms.program));
    GLC(context(), context()->uniform1i(uniforms.samplerLocation, 0));
    ResourceProvider::ScopedReadLockGL quadResourceLock(m_resourceProvider, quad->resource_id);
    GLC(context(), context()->bindTexture(GL_TEXTURE_2D, quadResourceLock.textureId()));

    bool useAA = !clipped && quad->IsAntialiased();
    if (useAA) {
        LayerQuad deviceLayerBounds = LayerQuad(gfx::QuadF(deviceLayerQuad.BoundingBox()));
        deviceLayerBounds.inflateAntiAliasingDistance();

        LayerQuad deviceLayerEdges = LayerQuad(deviceLayerQuad);
        deviceLayerEdges.inflateAntiAliasingDistance();

        float edge[24];
        deviceLayerEdges.toFloatArray(edge);
        deviceLayerBounds.toFloatArray(&edge[12]);
        GLC(context(), context()->uniform3fv(uniforms.edgeLocation, 8, edge));

        GLC(context(), context()->uniform4f(uniforms.vertexTexTransformLocation, vertexTexTranslateX, vertexTexTranslateY, vertexTexScaleX, vertexTexScaleY));
        GLC(context(), context()->uniform4f(uniforms.fragmentTexTransformLocation, fragmentTexTranslateX, fragmentTexTranslateY, fragmentTexScaleX, fragmentTexScaleY));

        gfx::PointF bottomRight = tileRect.bottom_right();
        gfx::PointF bottomLeft = tileRect.bottom_left();
        gfx::PointF topLeft = tileRect.origin();
        gfx::PointF topRight = tileRect.top_right();

        // Map points to device space.
        bottomRight = MathUtil::mapPoint(deviceTransform, bottomRight, clipped);
        DCHECK(!clipped);
        bottomLeft = MathUtil::mapPoint(deviceTransform, bottomLeft, clipped);
        DCHECK(!clipped);
        topLeft = MathUtil::mapPoint(deviceTransform, topLeft, clipped);
        DCHECK(!clipped);
        topRight = MathUtil::mapPoint(deviceTransform, topRight, clipped);
        DCHECK(!clipped);

        LayerQuad::Edge bottomEdge(bottomRight, bottomLeft);
        LayerQuad::Edge leftEdge(bottomLeft, topLeft);
        LayerQuad::Edge topEdge(topLeft, topRight);
        LayerQuad::Edge rightEdge(topRight, bottomRight);

        // Only apply anti-aliasing to edges not clipped by culling or scissoring.
        if (quad->top_edge_aa && tileRect.y() == quad->rect.y())
            topEdge = deviceLayerEdges.top();
        if (quad->left_edge_aa && tileRect.x() == quad->rect.x())
            leftEdge = deviceLayerEdges.left();
        if (quad->right_edge_aa && tileRect.right() == quad->rect.right())
            rightEdge = deviceLayerEdges.right();
        if (quad->bottom_edge_aa && tileRect.bottom() == quad->rect.bottom())
            bottomEdge = deviceLayerEdges.bottom();

        float sign = gfx::QuadF(tileRect).IsCounterClockwise() ? -1 : 1;
        bottomEdge.scale(sign);
        leftEdge.scale(sign);
        topEdge.scale(sign);
        rightEdge.scale(sign);

        // Create device space quad.
        LayerQuad deviceQuad(leftEdge, topEdge, rightEdge, bottomEdge);

        // Map device space quad to local space. deviceTransform has no 3d component since it was generated with to2dTransform() so we don't need to project.
        WebTransformationMatrix deviceTransformInverse = deviceTransform.inverse();
        localQuad = MathUtil::mapQuad(deviceTransformInverse, deviceQuad.ToQuadF(), clipped);

        // We should not DCHECK(!clipped) here, because anti-aliasing inflation may cause deviceQuad to become
        // clipped. To our knowledge this scenario does not need to be handled differently than the unclipped case.
    } else {
        // Move fragment shader transform to vertex shader. We can do this while
        // still producing correct results as fragmentTexTransformLocation
        // should always be non-negative when tiles are transformed in a way
        // that could result in sampling outside the layer.
        vertexTexScaleX *= fragmentTexScaleX;
        vertexTexScaleY *= fragmentTexScaleY;
        vertexTexTranslateX *= fragmentTexScaleX;
        vertexTexTranslateY *= fragmentTexScaleY;
        vertexTexTranslateX += fragmentTexTranslateX;
        vertexTexTranslateY += fragmentTexTranslateY;

        GLC(context(), context()->uniform4f(uniforms.vertexTexTransformLocation, vertexTexTranslateX, vertexTexTranslateY, vertexTexScaleX, vertexTexScaleY));

        localQuad = gfx::RectF(tileRect);
    }

    // Normalize to tileRect.
    localQuad.Scale(1.0f / tileRect.width(), 1.0f / tileRect.height());

    setShaderOpacity(quad->opacity(), uniforms.alphaLocation);
    setShaderQuadF(localQuad, uniforms.pointLocation);

    // The tile quad shader behaves differently compared to all other shaders.
    // The transform and vertex data are used to figure out the extents that the
    // un-antialiased quad should have and which vertex this is and the float
    // quad passed in via uniform is the actual geometry that gets used to draw
    // it. This is why this centered rect is used and not the original quadRect.
    gfx::RectF centeredRect(gfx::PointF(-0.5 * tileRect.width(), -0.5 * tileRect.height()), tileRect.size());
    drawQuadGeometry(frame, quad->quadTransform(), centeredRect, uniforms.matrixLocation);
}

void GLRenderer::drawYUVVideoQuad(const DrawingFrame& frame, const YUVVideoDrawQuad* quad)
{
    const VideoYUVProgram* program = videoYUVProgram();
    DCHECK(program && (program->initialized() || isContextLost()));

    const VideoLayerImpl::FramePlane& yPlane = quad->y_plane;
    const VideoLayerImpl::FramePlane& uPlane = quad->u_plane;
    const VideoLayerImpl::FramePlane& vPlane = quad->v_plane;

    ResourceProvider::ScopedReadLockGL yPlaneLock(m_resourceProvider, yPlane.resourceId);
    ResourceProvider::ScopedReadLockGL uPlaneLock(m_resourceProvider, uPlane.resourceId);
    ResourceProvider::ScopedReadLockGL vPlaneLock(m_resourceProvider, vPlane.resourceId);
    GLC(context(), context()->activeTexture(GL_TEXTURE1));
    GLC(context(), context()->bindTexture(GL_TEXTURE_2D, yPlaneLock.textureId()));
    GLC(context(), context()->activeTexture(GL_TEXTURE2));
    GLC(context(), context()->bindTexture(GL_TEXTURE_2D, uPlaneLock.textureId()));
    GLC(context(), context()->activeTexture(GL_TEXTURE3));
    GLC(context(), context()->bindTexture(GL_TEXTURE_2D, vPlaneLock.textureId()));

    GLC(context(), context()->useProgram(program->program()));

    GLC(context(), context()->uniform2f(program->vertexShader().texScaleLocation(), quad->tex_scale.width(), quad->tex_scale.height()));
    GLC(context(), context()->uniform1i(program->fragmentShader().yTextureLocation(), 1));
    GLC(context(), context()->uniform1i(program->fragmentShader().uTextureLocation(), 2));
    GLC(context(), context()->uniform1i(program->fragmentShader().vTextureLocation(), 3));

    // These values are magic numbers that are used in the transformation from YUV to RGB color values.
    // They are taken from the following webpage: http://www.fourcc.org/fccyvrgb.php
    float yuv2RGB[9] = {
        1.164f, 1.164f, 1.164f,
        0.f, -.391f, 2.018f,
        1.596f, -.813f, 0.f,
    };
    GLC(context(), context()->uniformMatrix3fv(program->fragmentShader().yuvMatrixLocation(), 1, 0, yuv2RGB));

    // These values map to 16, 128, and 128 respectively, and are computed
    // as a fraction over 256 (e.g. 16 / 256 = 0.0625).
    // They are used in the YUV to RGBA conversion formula:
    //   Y - 16   : Gives 16 values of head and footroom for overshooting
    //   U - 128  : Turns unsigned U into signed U [-128,127]
    //   V - 128  : Turns unsigned V into signed V [-128,127]
    float yuvAdjust[3] = {
        -0.0625f,
        -0.5f,
        -0.5f,
    };
    GLC(context(), context()->uniform3fv(program->fragmentShader().yuvAdjLocation(), 1, yuvAdjust));

    setShaderOpacity(quad->opacity(), program->fragmentShader().alphaLocation());
    drawQuadGeometry(frame, quad->quadTransform(), quad->rect, program->vertexShader().matrixLocation());

    // Reset active texture back to texture 0.
    GLC(context(), context()->activeTexture(GL_TEXTURE0));
}

void GLRenderer::drawStreamVideoQuad(const DrawingFrame& frame, const StreamVideoDrawQuad* quad)
{
    static float glMatrix[16];

    DCHECK(m_capabilities.usingEglImage);

    const VideoStreamTextureProgram* program = videoStreamTextureProgram();
    GLC(context(), context()->useProgram(program->program()));

    toGLMatrix(&glMatrix[0], quad->matrix);
    GLC(context(), context()->uniformMatrix4fv(program->vertexShader().texMatrixLocation(), 1, false, glMatrix));

    GLC(context(), context()->bindTexture(GL_TEXTURE_EXTERNAL_OES, quad->texture_id));

    GLC(context(), context()->uniform1i(program->fragmentShader().samplerLocation(), 0));

    setShaderOpacity(quad->opacity(), program->fragmentShader().alphaLocation());
    drawQuadGeometry(frame, quad->quadTransform(), quad->rect, program->vertexShader().matrixLocation());
}

struct TextureProgramBinding {
    template<class Program> void set(
        Program* program, WebKit::WebGraphicsContext3D* context)
    {
        DCHECK(program && (program->initialized() || context->isContextLost()));
        programId = program->program();
        samplerLocation = program->fragmentShader().samplerLocation();
        matrixLocation = program->vertexShader().matrixLocation();
        alphaLocation = program->fragmentShader().alphaLocation();
    }
    int programId;
    int samplerLocation;
    int matrixLocation;
    int alphaLocation;
};

struct TexTransformTextureProgramBinding : TextureProgramBinding {
    template<class Program> void set(
        Program* program, WebKit::WebGraphicsContext3D* context)
    {
        TextureProgramBinding::set(program, context);
        texTransformLocation = program->vertexShader().texTransformLocation();
    }
    int texTransformLocation;
};

void GLRenderer::drawTextureQuad(const DrawingFrame& frame, const TextureDrawQuad* quad)
{
    TexTransformTextureProgramBinding binding;
    if (quad->flipped)
        binding.set(textureProgramFlip(), context());
    else
        binding.set(textureProgram(), context());
    GLC(context(), context()->useProgram(binding.programId));
    GLC(context(), context()->uniform1i(binding.samplerLocation, 0));
    const gfx::RectF& uvRect = quad->uv_rect;
    GLC(context(), context()->uniform4f(binding.texTransformLocation, uvRect.x(), uvRect.y(), uvRect.width(), uvRect.height()));

    ResourceProvider::ScopedReadLockGL quadResourceLock(m_resourceProvider, quad->resource_id);
    GLC(context(), context()->bindTexture(GL_TEXTURE_2D, quadResourceLock.textureId()));

    if (!quad->premultiplied_alpha) {
        // As it turns out, the premultiplied alpha blending function (ONE, ONE_MINUS_SRC_ALPHA)
        // will never cause the alpha channel to be set to anything less than 1.0 if it is
        // initialized to that value! Therefore, premultipliedAlpha being false is the first
        // situation we can generally see an alpha channel less than 1.0 coming out of the
        // compositor. This is causing platform differences in some layout tests (see
        // https://bugs.webkit.org/show_bug.cgi?id=82412), so in this situation, use a separate
        // blend function for the alpha channel to avoid modifying it. Don't use colorMask for this
        // as it has performance implications on some platforms.
        GLC(context(), context()->blendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE));
    }

    setShaderOpacity(quad->opacity(), binding.alphaLocation);
    drawQuadGeometry(frame, quad->quadTransform(), quad->rect, binding.matrixLocation);

    if (!quad->premultiplied_alpha)
        GLC(m_context, m_context->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
}

void GLRenderer::drawIOSurfaceQuad(const DrawingFrame& frame, const IOSurfaceDrawQuad* quad)
{
    TexTransformTextureProgramBinding binding;
    binding.set(textureIOSurfaceProgram(), context());

    GLC(context(), context()->useProgram(binding.programId));
    GLC(context(), context()->uniform1i(binding.samplerLocation, 0));
    if (quad->orientation == IOSurfaceDrawQuad::FLIPPED)
        GLC(context(), context()->uniform4f(binding.texTransformLocation, 0, quad->io_surface_size.height(), quad->io_surface_size.width(), quad->io_surface_size.height() * -1.0));
    else
        GLC(context(), context()->uniform4f(binding.texTransformLocation, 0, 0, quad->io_surface_size.width(), quad->io_surface_size.height()));

    GLC(context(), context()->bindTexture(GL_TEXTURE_RECTANGLE_ARB, quad->io_surface_texture_id));

    setShaderOpacity(quad->opacity(), binding.alphaLocation);
    drawQuadGeometry(frame, quad->quadTransform(), quad->rect, binding.matrixLocation);

    GLC(context(), context()->bindTexture(GL_TEXTURE_RECTANGLE_ARB, 0));
}

void GLRenderer::finishDrawingFrame(DrawingFrame& frame)
{
    m_currentFramebufferLock.reset();
    m_swapBufferRect.Union(gfx::ToEnclosingRect(frame.rootDamageRect));

    GLC(m_context, m_context->disable(GL_BLEND));
}

bool GLRenderer::flippedFramebuffer() const
{
    return true;
}

void GLRenderer::ensureScissorTestEnabled()
{
    if (m_isScissorEnabled)
        return;

    GLC(m_context, m_context->enable(GL_SCISSOR_TEST));
    m_isScissorEnabled = true;
}

void GLRenderer::ensureScissorTestDisabled()
{
    if (!m_isScissorEnabled)
        return;

    GLC(m_context, m_context->disable(GL_SCISSOR_TEST));
    m_isScissorEnabled = false;
}

void GLRenderer::toGLMatrix(float* flattened, const WebTransformationMatrix& m)
{
    flattened[0] = m.m11();
    flattened[1] = m.m12();
    flattened[2] = m.m13();
    flattened[3] = m.m14();
    flattened[4] = m.m21();
    flattened[5] = m.m22();
    flattened[6] = m.m23();
    flattened[7] = m.m24();
    flattened[8] = m.m31();
    flattened[9] = m.m32();
    flattened[10] = m.m33();
    flattened[11] = m.m34();
    flattened[12] = m.m41();
    flattened[13] = m.m42();
    flattened[14] = m.m43();
    flattened[15] = m.m44();
}

void GLRenderer::setShaderQuadF(const gfx::QuadF& quad, int quadLocation)
{
    if (quadLocation == -1)
        return;

    float point[8];
    point[0] = quad.p1().x();
    point[1] = quad.p1().y();
    point[2] = quad.p2().x();
    point[3] = quad.p2().y();
    point[4] = quad.p3().x();
    point[5] = quad.p3().y();
    point[6] = quad.p4().x();
    point[7] = quad.p4().y();
    GLC(m_context, m_context->uniform2fv(quadLocation, 4, point));
}

void GLRenderer::setShaderOpacity(float opacity, int alphaLocation)
{
    if (alphaLocation != -1)
        GLC(m_context, m_context->uniform1f(alphaLocation, opacity));
}

void GLRenderer::drawQuadGeometry(const DrawingFrame& frame, const WebKit::WebTransformationMatrix& drawTransform, const gfx::RectF& quadRect, int matrixLocation)
{
    WebTransformationMatrix quadRectMatrix;
    quadRectTransform(&quadRectMatrix, drawTransform, quadRect);
    static float glMatrix[16];
    toGLMatrix(&glMatrix[0], frame.projectionMatrix * quadRectMatrix);
    GLC(m_context, m_context->uniformMatrix4fv(matrixLocation, 1, false, &glMatrix[0]));

    GLC(m_context, m_context->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0));
}

void GLRenderer::copyTextureToFramebuffer(const DrawingFrame& frame, int textureId, const gfx::Rect& rect, const WebTransformationMatrix& drawMatrix)
{
    const RenderPassProgram* program = renderPassProgram();

    GLC(context(), context()->bindTexture(GL_TEXTURE_2D, textureId));

    GLC(context(), context()->useProgram(program->program()));
    GLC(context(), context()->uniform1i(program->fragmentShader().samplerLocation(), 0));
    setShaderOpacity(1, program->fragmentShader().alphaLocation());
    drawQuadGeometry(frame, drawMatrix, rect, program->vertexShader().matrixLocation());
}

void GLRenderer::finish()
{
    TRACE_EVENT0("cc", "GLRenderer::finish");
    m_context->finish();
}

bool GLRenderer::swapBuffers()
{
    DCHECK(m_visible);
    DCHECK(!m_isFramebufferDiscarded);

    TRACE_EVENT0("cc", "GLRenderer::swapBuffers");
    // We're done! Time to swapbuffers!

    if (m_capabilities.usingPartialSwap) {
        // If supported, we can save significant bandwidth by only swapping the damaged/scissored region (clamped to the viewport)
        m_swapBufferRect.Intersect(gfx::Rect(gfx::Point(), viewportSize()));
        int flippedYPosOfRectBottom = viewportHeight() - m_swapBufferRect.y() - m_swapBufferRect.height();
        m_context->postSubBufferCHROMIUM(m_swapBufferRect.x(), flippedYPosOfRectBottom, m_swapBufferRect.width(), m_swapBufferRect.height());
    } else {
        // Note that currently this has the same effect as swapBuffers; we should
        // consider exposing a different entry point on WebGraphicsContext3D.
        m_context->prepareTexture();
    }

    m_swapBufferRect = gfx::Rect();

    return true;
}

void GLRenderer::onSwapBuffersComplete()
{
    m_client->onSwapBuffersComplete();
}

void GLRenderer::onMemoryAllocationChanged(WebGraphicsMemoryAllocation allocation)
{
    // Just ignore the memory manager when it says to set the limit to zero
    // bytes. This will happen when the memory manager thinks that the renderer
    // is not visible (which the renderer knows better).
    if (allocation.bytesLimitWhenVisible) {
        ManagedMemoryPolicy policy(
            allocation.bytesLimitWhenVisible,
            priorityCutoffValue(allocation.priorityCutoffWhenVisible),
            allocation.bytesLimitWhenNotVisible,
            priorityCutoffValue(allocation.priorityCutoffWhenNotVisible));

        if (allocation.enforceButDoNotKeepAsPolicy)
            m_client->enforceManagedMemoryPolicy(policy);
        else
            m_client->setManagedMemoryPolicy(policy);
    }

    bool oldDiscardFramebufferWhenNotVisible = m_discardFramebufferWhenNotVisible;
    m_discardFramebufferWhenNotVisible = !allocation.suggestHaveBackbuffer;
    enforceMemoryPolicy();
    if (allocation.enforceButDoNotKeepAsPolicy)
        m_discardFramebufferWhenNotVisible = oldDiscardFramebufferWhenNotVisible;
}

int GLRenderer::priorityCutoffValue(WebKit::WebGraphicsMemoryAllocation::PriorityCutoff priorityCutoff)
{
    switch (priorityCutoff) {
    case WebKit::WebGraphicsMemoryAllocation::PriorityCutoffAllowNothing:
        return PriorityCalculator::allowNothingCutoff();
    case WebKit::WebGraphicsMemoryAllocation::PriorityCutoffAllowVisibleOnly:
        return PriorityCalculator::allowVisibleOnlyCutoff();
    case WebKit::WebGraphicsMemoryAllocation::PriorityCutoffAllowVisibleAndNearby:
        return PriorityCalculator::allowVisibleAndNearbyCutoff();
    case WebKit::WebGraphicsMemoryAllocation::PriorityCutoffAllowEverything:
        return PriorityCalculator::allowEverythingCutoff();
    }
    NOTREACHED();
    return 0;
}

void GLRenderer::enforceMemoryPolicy()
{
    if (!m_visible) {
        TRACE_EVENT0("cc", "GLRenderer::enforceMemoryPolicy dropping resources");
        releaseRenderPassTextures();
        if (m_discardFramebufferWhenNotVisible)
            discardFramebuffer();
        GLC(m_context, m_context->flush());
    }
}

void GLRenderer::discardFramebuffer()
{
    if (m_isFramebufferDiscarded)
        return;

    if (!m_capabilities.usingDiscardFramebuffer)
        return;

    // FIXME: Update attachments argument to appropriate values once they are no longer ignored.
    m_context->discardFramebufferEXT(GL_TEXTURE_2D, 0, 0);
    m_isFramebufferDiscarded = true;

    // Damage tracker needs a full reset every time framebuffer is discarded.
    m_client->setFullRootLayerDamage();
}

void GLRenderer::ensureFramebuffer()
{
    if (!m_isFramebufferDiscarded)
        return;

    if (!m_capabilities.usingDiscardFramebuffer)
        return;

    m_context->ensureFramebufferCHROMIUM();
    m_isFramebufferDiscarded = false;
}

void GLRenderer::onContextLost()
{
    m_client->didLoseContext();
}


void GLRenderer::getFramebufferPixels(void *pixels, const gfx::Rect& rect)
{
    DCHECK(rect.right() <= viewportWidth());
    DCHECK(rect.bottom() <= viewportHeight());

    if (!pixels)
        return;

    makeContextCurrent();

    bool doWorkaround = needsIOSurfaceReadbackWorkaround();

    GLuint temporaryTexture = 0;
    GLuint temporaryFBO = 0;

    if (doWorkaround) {
        // On Mac OS X, calling glReadPixels against an FBO whose color attachment is an
        // IOSurface-backed texture causes corruption of future glReadPixels calls, even those on
        // different OpenGL contexts. It is believed that this is the root cause of top crasher
        // http://crbug.com/99393. <rdar://problem/10949687>

        temporaryTexture = m_context->createTexture();
        GLC(m_context, m_context->bindTexture(GL_TEXTURE_2D, temporaryTexture));
        GLC(m_context, m_context->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GLC(m_context, m_context->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GLC(m_context, m_context->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GLC(m_context, m_context->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        // Copy the contents of the current (IOSurface-backed) framebuffer into a temporary texture.
        GLC(m_context, m_context->copyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, viewportSize().width(), viewportSize().height(), 0));
        temporaryFBO = m_context->createFramebuffer();
        // Attach this texture to an FBO, and perform the readback from that FBO.
        GLC(m_context, m_context->bindFramebuffer(GL_FRAMEBUFFER, temporaryFBO));
        GLC(m_context, m_context->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, temporaryTexture, 0));

        DCHECK(m_context->checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    }

    scoped_array<uint8_t> srcPixels(new uint8_t[rect.width() * rect.height() * 4]);
    GLC(m_context, m_context->readPixels(rect.x(), viewportSize().height() - rect.bottom(), rect.width(), rect.height(),
                                         GL_RGBA, GL_UNSIGNED_BYTE, srcPixels.get()));

    uint8_t* destPixels = static_cast<uint8_t*>(pixels);
    size_t rowBytes = rect.width() * 4;
    int numRows = rect.height();
    size_t totalBytes = numRows * rowBytes;
    for (size_t destY = 0; destY < totalBytes; destY += rowBytes) {
        // Flip Y axis.
        size_t srcY = totalBytes - destY - rowBytes;
        // Swizzle BGRA -> RGBA.
        for (size_t x = 0; x < rowBytes; x += 4) {
            destPixels[destY + (x+0)] = srcPixels.get()[srcY + (x+2)];
            destPixels[destY + (x+1)] = srcPixels.get()[srcY + (x+1)];
            destPixels[destY + (x+2)] = srcPixels.get()[srcY + (x+0)];
            destPixels[destY + (x+3)] = srcPixels.get()[srcY + (x+3)];
        }
    }

    if (doWorkaround) {
        // Clean up.
        GLC(m_context, m_context->bindFramebuffer(GL_FRAMEBUFFER, 0));
        GLC(m_context, m_context->bindTexture(GL_TEXTURE_2D, 0));
        GLC(m_context, m_context->deleteFramebuffer(temporaryFBO));
        GLC(m_context, m_context->deleteTexture(temporaryTexture));
    }

    enforceMemoryPolicy();
}

bool GLRenderer::getFramebufferTexture(ScopedResource* texture, const gfx::Rect& deviceRect)
{
    DCHECK(!texture->id() || (texture->size() == deviceRect.size() && texture->format() == GL_RGB));

    if (!texture->id() && !texture->Allocate(Renderer::ImplPool, deviceRect.size(), GL_RGB, ResourceProvider::TextureUsageAny))
        return false;

    ResourceProvider::ScopedWriteLockGL lock(m_resourceProvider, texture->id());
    GLC(m_context, m_context->bindTexture(GL_TEXTURE_2D, lock.textureId()));
    GLC(m_context, m_context->copyTexImage2D(GL_TEXTURE_2D, 0, texture->format(),
                                             deviceRect.x(), deviceRect.y(), deviceRect.width(), deviceRect.height(), 0));
    return true;
}

bool GLRenderer::useScopedTexture(DrawingFrame& frame, const ScopedResource* texture, const gfx::Rect& viewportRect)
{
    DCHECK(texture->id());
    frame.currentRenderPass = 0;
    frame.currentTexture = texture;

    return bindFramebufferToTexture(frame, texture, viewportRect);
}

void GLRenderer::bindFramebufferToOutputSurface(DrawingFrame& frame)
{
    m_currentFramebufferLock.reset();
    GLC(m_context, m_context->bindFramebuffer(GL_FRAMEBUFFER, 0));
}

bool GLRenderer::bindFramebufferToTexture(DrawingFrame& frame, const ScopedResource* texture, const gfx::Rect& framebufferRect)
{
    DCHECK(texture->id());

    GLC(m_context, m_context->bindFramebuffer(GL_FRAMEBUFFER, m_offscreenFramebufferId));
    m_currentFramebufferLock = make_scoped_ptr(new ResourceProvider::ScopedWriteLockGL(m_resourceProvider, texture->id()));
    unsigned textureId = m_currentFramebufferLock->textureId();
    GLC(m_context, m_context->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0));

    DCHECK(m_context->checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    initializeMatrices(frame, framebufferRect, false);
    setDrawViewportSize(framebufferRect.size());

    return true;
}

void GLRenderer::setScissorTestRect(const gfx::Rect& scissorRect)
{
    ensureScissorTestEnabled();

    // Don't unnecessarily ask the context to change the scissor, because it
    // may cause undesired GPU pipeline flushes.
    if (scissorRect == m_scissorRect)
        return;

    m_scissorRect = scissorRect;
    GLC(m_context, m_context->scissor(scissorRect.x(), scissorRect.y(), scissorRect.width(), scissorRect.height()));
}

void GLRenderer::setDrawViewportSize(const gfx::Size& viewportSize)
{
    GLC(m_context, m_context->viewport(0, 0, viewportSize.width(), viewportSize.height()));
}

bool GLRenderer::makeContextCurrent()
{
    return m_context->makeContextCurrent();
}

bool GLRenderer::initializeSharedObjects()
{
    TRACE_EVENT0("cc", "GLRenderer::initializeSharedObjects");
    makeContextCurrent();

    // Create an FBO for doing offscreen rendering.
    GLC(m_context, m_offscreenFramebufferId = m_context->createFramebuffer());

    // We will always need these programs to render, so create the programs eagerly so that the shader compilation can
    // start while we do other work. Other programs are created lazily on first access.
    m_sharedGeometry = make_scoped_ptr(new GeometryBinding(m_context, quadVertexRect()));
    m_renderPassProgram = make_scoped_ptr(new RenderPassProgram(m_context));
    m_tileProgram = make_scoped_ptr(new TileProgram(m_context));
    m_tileProgramOpaque = make_scoped_ptr(new TileProgramOpaque(m_context));

    GLC(m_context, m_context->flush());

    return true;
}

const GLRenderer::TileCheckerboardProgram* GLRenderer::tileCheckerboardProgram()
{
    if (!m_tileCheckerboardProgram)
        m_tileCheckerboardProgram = make_scoped_ptr(new TileCheckerboardProgram(m_context));
    if (!m_tileCheckerboardProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::checkerboardProgram::initalize");
        m_tileCheckerboardProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_tileCheckerboardProgram.get();
}

const GLRenderer::SolidColorProgram* GLRenderer::solidColorProgram()
{
    if (!m_solidColorProgram)
        m_solidColorProgram = make_scoped_ptr(new SolidColorProgram(m_context));
    if (!m_solidColorProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::solidColorProgram::initialize");
        m_solidColorProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_solidColorProgram.get();
}

const GLRenderer::RenderPassProgram* GLRenderer::renderPassProgram()
{
    DCHECK(m_renderPassProgram);
    if (!m_renderPassProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::renderPassProgram::initialize");
        m_renderPassProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_renderPassProgram.get();
}

const GLRenderer::RenderPassProgramAA* GLRenderer::renderPassProgramAA()
{
    if (!m_renderPassProgramAA)
        m_renderPassProgramAA = make_scoped_ptr(new RenderPassProgramAA(m_context));
    if (!m_renderPassProgramAA->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::renderPassProgramAA::initialize");
        m_renderPassProgramAA->initialize(m_context, m_isUsingBindUniform);
    }
    return m_renderPassProgramAA.get();
}

const GLRenderer::RenderPassMaskProgram* GLRenderer::renderPassMaskProgram()
{
    if (!m_renderPassMaskProgram)
        m_renderPassMaskProgram = make_scoped_ptr(new RenderPassMaskProgram(m_context));
    if (!m_renderPassMaskProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::renderPassMaskProgram::initialize");
        m_renderPassMaskProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_renderPassMaskProgram.get();
}

const GLRenderer::RenderPassMaskProgramAA* GLRenderer::renderPassMaskProgramAA()
{
    if (!m_renderPassMaskProgramAA)
        m_renderPassMaskProgramAA = make_scoped_ptr(new RenderPassMaskProgramAA(m_context));
    if (!m_renderPassMaskProgramAA->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::renderPassMaskProgramAA::initialize");
        m_renderPassMaskProgramAA->initialize(m_context, m_isUsingBindUniform);
    }
    return m_renderPassMaskProgramAA.get();
}

const GLRenderer::TileProgram* GLRenderer::tileProgram()
{
    DCHECK(m_tileProgram);
    if (!m_tileProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::tileProgram::initialize");
        m_tileProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_tileProgram.get();
}

const GLRenderer::TileProgramOpaque* GLRenderer::tileProgramOpaque()
{
    DCHECK(m_tileProgramOpaque);
    if (!m_tileProgramOpaque->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::tileProgramOpaque::initialize");
        m_tileProgramOpaque->initialize(m_context, m_isUsingBindUniform);
    }
    return m_tileProgramOpaque.get();
}

const GLRenderer::TileProgramAA* GLRenderer::tileProgramAA()
{
    if (!m_tileProgramAA)
        m_tileProgramAA = make_scoped_ptr(new TileProgramAA(m_context));
    if (!m_tileProgramAA->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::tileProgramAA::initialize");
        m_tileProgramAA->initialize(m_context, m_isUsingBindUniform);
    }
    return m_tileProgramAA.get();
}

const GLRenderer::TileProgramSwizzle* GLRenderer::tileProgramSwizzle()
{
    if (!m_tileProgramSwizzle)
        m_tileProgramSwizzle = make_scoped_ptr(new TileProgramSwizzle(m_context));
    if (!m_tileProgramSwizzle->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::tileProgramSwizzle::initialize");
        m_tileProgramSwizzle->initialize(m_context, m_isUsingBindUniform);
    }
    return m_tileProgramSwizzle.get();
}

const GLRenderer::TileProgramSwizzleOpaque* GLRenderer::tileProgramSwizzleOpaque()
{
    if (!m_tileProgramSwizzleOpaque)
        m_tileProgramSwizzleOpaque = make_scoped_ptr(new TileProgramSwizzleOpaque(m_context));
    if (!m_tileProgramSwizzleOpaque->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::tileProgramSwizzleOpaque::initialize");
        m_tileProgramSwizzleOpaque->initialize(m_context, m_isUsingBindUniform);
    }
    return m_tileProgramSwizzleOpaque.get();
}

const GLRenderer::TileProgramSwizzleAA* GLRenderer::tileProgramSwizzleAA()
{
    if (!m_tileProgramSwizzleAA)
        m_tileProgramSwizzleAA = make_scoped_ptr(new TileProgramSwizzleAA(m_context));
    if (!m_tileProgramSwizzleAA->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::tileProgramSwizzleAA::initialize");
        m_tileProgramSwizzleAA->initialize(m_context, m_isUsingBindUniform);
    }
    return m_tileProgramSwizzleAA.get();
}

const GLRenderer::TextureProgram* GLRenderer::textureProgram()
{
    if (!m_textureProgram)
        m_textureProgram = make_scoped_ptr(new TextureProgram(m_context));
    if (!m_textureProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::textureProgram::initialize");
        m_textureProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_textureProgram.get();
}

const GLRenderer::TextureProgramFlip* GLRenderer::textureProgramFlip()
{
    if (!m_textureProgramFlip)
        m_textureProgramFlip = make_scoped_ptr(new TextureProgramFlip(m_context));
    if (!m_textureProgramFlip->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::textureProgramFlip::initialize");
        m_textureProgramFlip->initialize(m_context, m_isUsingBindUniform);
    }
    return m_textureProgramFlip.get();
}

const GLRenderer::TextureIOSurfaceProgram* GLRenderer::textureIOSurfaceProgram()
{
    if (!m_textureIOSurfaceProgram)
        m_textureIOSurfaceProgram = make_scoped_ptr(new TextureIOSurfaceProgram(m_context));
    if (!m_textureIOSurfaceProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::textureIOSurfaceProgram::initialize");
        m_textureIOSurfaceProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_textureIOSurfaceProgram.get();
}

const GLRenderer::VideoYUVProgram* GLRenderer::videoYUVProgram()
{
    if (!m_videoYUVProgram)
        m_videoYUVProgram = make_scoped_ptr(new VideoYUVProgram(m_context));
    if (!m_videoYUVProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::videoYUVProgram::initialize");
        m_videoYUVProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_videoYUVProgram.get();
}

const GLRenderer::VideoStreamTextureProgram* GLRenderer::videoStreamTextureProgram()
{
    if (!m_videoStreamTextureProgram)
        m_videoStreamTextureProgram = make_scoped_ptr(new VideoStreamTextureProgram(m_context));
    if (!m_videoStreamTextureProgram->initialized()) {
        TRACE_EVENT0("cc", "GLRenderer::streamTextureProgram::initialize");
        m_videoStreamTextureProgram->initialize(m_context, m_isUsingBindUniform);
    }
    return m_videoStreamTextureProgram.get();
}

void GLRenderer::cleanupSharedObjects()
{
    makeContextCurrent();

    m_sharedGeometry.reset();

    if (m_tileProgram)
        m_tileProgram->cleanup(m_context);
    if (m_tileProgramOpaque)
        m_tileProgramOpaque->cleanup(m_context);
    if (m_tileProgramSwizzle)
        m_tileProgramSwizzle->cleanup(m_context);
    if (m_tileProgramSwizzleOpaque)
        m_tileProgramSwizzleOpaque->cleanup(m_context);
    if (m_tileProgramAA)
        m_tileProgramAA->cleanup(m_context);
    if (m_tileProgramSwizzleAA)
        m_tileProgramSwizzleAA->cleanup(m_context);
    if (m_tileCheckerboardProgram)
        m_tileCheckerboardProgram->cleanup(m_context);

    if (m_renderPassMaskProgram)
        m_renderPassMaskProgram->cleanup(m_context);
    if (m_renderPassProgram)
        m_renderPassProgram->cleanup(m_context);
    if (m_renderPassMaskProgramAA)
        m_renderPassMaskProgramAA->cleanup(m_context);
    if (m_renderPassProgramAA)
        m_renderPassProgramAA->cleanup(m_context);

    if (m_textureProgram)
        m_textureProgram->cleanup(m_context);
    if (m_textureProgramFlip)
        m_textureProgramFlip->cleanup(m_context);
    if (m_textureIOSurfaceProgram)
        m_textureIOSurfaceProgram->cleanup(m_context);

    if (m_videoYUVProgram)
        m_videoYUVProgram->cleanup(m_context);
    if (m_videoStreamTextureProgram)
        m_videoStreamTextureProgram->cleanup(m_context);

    if (m_solidColorProgram)
        m_solidColorProgram->cleanup(m_context);

    if (m_offscreenFramebufferId)
        GLC(m_context, m_context->deleteFramebuffer(m_offscreenFramebufferId));

    releaseRenderPassTextures();
}

bool GLRenderer::isContextLost()
{
    return (m_context->getGraphicsResetStatusARB() != GL_NO_ERROR);
}

}  // namespace cc
