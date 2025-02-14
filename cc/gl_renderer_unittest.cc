// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/gl_renderer.h"

#include "cc/draw_quad.h"
#include "cc/prioritized_resource_manager.h"
#include "cc/resource_provider.h"
#include "cc/test/fake_web_compositor_output_surface.h"
#include "cc/test/fake_web_graphics_context_3d.h"
#include "cc/test/render_pass_test_common.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/khronos/GLES2/gl2.h"
#include <public/WebTransformationMatrix.h>

using namespace WebKit;
using namespace WebKitTests;

namespace cc {
namespace {

class FrameCountingMemoryAllocationSettingContext : public FakeWebGraphicsContext3D {
public:
    FrameCountingMemoryAllocationSettingContext() : m_frame(0) { }

    // WebGraphicsContext3D methods.

    // This method would normally do a glSwapBuffers under the hood.
    virtual void prepareTexture() { m_frame++; }
    virtual void setMemoryAllocationChangedCallbackCHROMIUM(WebGraphicsMemoryAllocationChangedCallbackCHROMIUM* callback) { m_memoryAllocationChangedCallback = callback; }
    virtual WebString getString(WebKit::WGC3Denum name)
    {
        if (name == GL_EXTENSIONS)
            return WebString("GL_CHROMIUM_set_visibility GL_CHROMIUM_gpu_memory_manager GL_CHROMIUM_discard_framebuffer");
        return WebString();
    }

    // Methods added for test.
    int frameCount() { return m_frame; }
    void setMemoryAllocation(WebGraphicsMemoryAllocation allocation)
    {
        m_memoryAllocationChangedCallback->onMemoryAllocationChanged(allocation);
    }

private:
    int m_frame;
    WebGraphicsMemoryAllocationChangedCallbackCHROMIUM* m_memoryAllocationChangedCallback;
};

class FakeRendererClient : public RendererClient {
public:
    FakeRendererClient()
        : m_setFullRootLayerDamageCount(0)
        , m_lastCallWasSetVisibility(0)
        , m_rootLayer(LayerImpl::create(1))
        , m_memoryAllocationLimitBytes(PrioritizedResourceManager::defaultMemoryAllocationLimit())
    {
        m_rootLayer->createRenderSurface();
        RenderPass::Id renderPassId = m_rootLayer->renderSurface()->renderPassId();
        scoped_ptr<RenderPass> rootRenderPass = RenderPass::Create();
        rootRenderPass->SetNew(renderPassId, gfx::Rect(), gfx::Rect(), WebTransformationMatrix());
        m_renderPassesInDrawOrder.push_back(rootRenderPass.get());
        m_renderPasses.set(renderPassId, rootRenderPass.Pass());
    }

    // RendererClient methods.
    virtual const gfx::Size& deviceViewportSize() const OVERRIDE { static gfx::Size fakeSize(1, 1); return fakeSize; }
    virtual const LayerTreeSettings& settings() const OVERRIDE { static LayerTreeSettings fakeSettings; return fakeSettings; }
    virtual void didLoseContext() OVERRIDE { }
    virtual void onSwapBuffersComplete() OVERRIDE { }
    virtual void setFullRootLayerDamage() OVERRIDE { m_setFullRootLayerDamageCount++; }
    virtual void setManagedMemoryPolicy(const ManagedMemoryPolicy& policy) OVERRIDE { m_memoryAllocationLimitBytes = policy.bytesLimitWhenVisible; }
    virtual void enforceManagedMemoryPolicy(const ManagedMemoryPolicy& policy) OVERRIDE { if (m_lastCallWasSetVisibility) *m_lastCallWasSetVisibility = false; }
    virtual bool hasImplThread() const OVERRIDE { return false; }

    // Methods added for test.
    int setFullRootLayerDamageCount() const { return m_setFullRootLayerDamageCount; }
    void setLastCallWasSetVisibilityPointer(bool* lastCallWasSetVisibility) { m_lastCallWasSetVisibility = lastCallWasSetVisibility; }

    RenderPass* rootRenderPass() { return m_renderPassesInDrawOrder.back(); }
    const RenderPassList& renderPassesInDrawOrder() const { return m_renderPassesInDrawOrder; }
    const RenderPassIdHashMap& renderPasses() const { return m_renderPasses; }

    size_t memoryAllocationLimitBytes() const { return m_memoryAllocationLimitBytes; }

private:
    int m_setFullRootLayerDamageCount;
    bool* m_lastCallWasSetVisibility;
    scoped_ptr<LayerImpl> m_rootLayer;
    RenderPassList m_renderPassesInDrawOrder;
    RenderPassIdHashMap m_renderPasses;
    size_t m_memoryAllocationLimitBytes;
};

class FakeRendererGL : public GLRenderer {
public:
    FakeRendererGL(RendererClient* client, ResourceProvider* resourceProvider) : GLRenderer(client, resourceProvider) { }

    // GLRenderer methods.

    // Changing visibility to public.
    using GLRenderer::initialize;
    using GLRenderer::isFramebufferDiscarded;
    using GLRenderer::drawQuad;
    using GLRenderer::beginDrawingFrame;
};

class GLRendererTest : public testing::Test {
protected:
    GLRendererTest()
        : m_suggestHaveBackbufferYes(1, true)
        , m_suggestHaveBackbufferNo(1, false)
        , m_context(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new FrameCountingMemoryAllocationSettingContext())))
        , m_resourceProvider(ResourceProvider::create(m_context.get()))
        , m_renderer(&m_mockClient, m_resourceProvider.get())
    {
    }

    virtual void SetUp()
    {
        m_renderer.initialize();
    }

    void swapBuffers()
    {
        m_renderer.swapBuffers();
    }

    FrameCountingMemoryAllocationSettingContext* context() { return static_cast<FrameCountingMemoryAllocationSettingContext*>(m_context->context3D()); }

    WebGraphicsMemoryAllocation m_suggestHaveBackbufferYes;
    WebGraphicsMemoryAllocation m_suggestHaveBackbufferNo;

    scoped_ptr<GraphicsContext> m_context;
    FakeRendererClient m_mockClient;
    scoped_ptr<ResourceProvider> m_resourceProvider;
    FakeRendererGL m_renderer;
};

// Test GLRenderer discardFramebuffer functionality:
// Suggest recreating framebuffer when one already exists.
// Expected: it does nothing.
TEST_F(GLRendererTest, SuggestBackbufferYesWhenItAlreadyExistsShouldDoNothing)
{
    context()->setMemoryAllocation(m_suggestHaveBackbufferYes);
    EXPECT_EQ(0, m_mockClient.setFullRootLayerDamageCount());
    EXPECT_FALSE(m_renderer.isFramebufferDiscarded());

    swapBuffers();
    EXPECT_EQ(1, context()->frameCount());
}

// Test GLRenderer discardFramebuffer functionality:
// Suggest discarding framebuffer when one exists and the renderer is not visible.
// Expected: it is discarded and damage tracker is reset.
TEST_F(GLRendererTest, SuggestBackbufferNoShouldDiscardBackbufferAndDamageRootLayerWhileNotVisible)
{
    m_renderer.setVisible(false);
    context()->setMemoryAllocation(m_suggestHaveBackbufferNo);
    EXPECT_EQ(1, m_mockClient.setFullRootLayerDamageCount());
    EXPECT_TRUE(m_renderer.isFramebufferDiscarded());
}

// Test GLRenderer discardFramebuffer functionality:
// Suggest discarding framebuffer when one exists and the renderer is visible.
// Expected: the allocation is ignored.
TEST_F(GLRendererTest, SuggestBackbufferNoDoNothingWhenVisible)
{
    m_renderer.setVisible(true);
    context()->setMemoryAllocation(m_suggestHaveBackbufferNo);
    EXPECT_EQ(0, m_mockClient.setFullRootLayerDamageCount());
    EXPECT_FALSE(m_renderer.isFramebufferDiscarded());
}


// Test GLRenderer discardFramebuffer functionality:
// Suggest discarding framebuffer when one does not exist.
// Expected: it does nothing.
TEST_F(GLRendererTest, SuggestBackbufferNoWhenItDoesntExistShouldDoNothing)
{
    m_renderer.setVisible(false);
    context()->setMemoryAllocation(m_suggestHaveBackbufferNo);
    EXPECT_EQ(1, m_mockClient.setFullRootLayerDamageCount());
    EXPECT_TRUE(m_renderer.isFramebufferDiscarded());

    context()->setMemoryAllocation(m_suggestHaveBackbufferNo);
    EXPECT_EQ(1, m_mockClient.setFullRootLayerDamageCount());
    EXPECT_TRUE(m_renderer.isFramebufferDiscarded());
}

// Test GLRenderer discardFramebuffer functionality:
// Begin drawing a frame while a framebuffer is discarded.
// Expected: will recreate framebuffer.
TEST_F(GLRendererTest, DiscardedBackbufferIsRecreatedForScopeDuration)
{
    m_renderer.setVisible(false);
    context()->setMemoryAllocation(m_suggestHaveBackbufferNo);
    EXPECT_TRUE(m_renderer.isFramebufferDiscarded());
    EXPECT_EQ(1, m_mockClient.setFullRootLayerDamageCount());

    m_renderer.setVisible(true);
    m_renderer.drawFrame(m_mockClient.renderPassesInDrawOrder(), m_mockClient.renderPasses());
    EXPECT_FALSE(m_renderer.isFramebufferDiscarded());

    swapBuffers();
    EXPECT_EQ(1, context()->frameCount());
}

TEST_F(GLRendererTest, FramebufferDiscardedAfterReadbackWhenNotVisible)
{
    m_renderer.setVisible(false);
    context()->setMemoryAllocation(m_suggestHaveBackbufferNo);
    EXPECT_TRUE(m_renderer.isFramebufferDiscarded());
    EXPECT_EQ(1, m_mockClient.setFullRootLayerDamageCount());

    char pixels[4];
    m_renderer.drawFrame(m_mockClient.renderPassesInDrawOrder(), m_mockClient.renderPasses());
    EXPECT_FALSE(m_renderer.isFramebufferDiscarded());

    m_renderer.getFramebufferPixels(pixels, gfx::Rect(0, 0, 1, 1));
    EXPECT_TRUE(m_renderer.isFramebufferDiscarded());
    EXPECT_EQ(2, m_mockClient.setFullRootLayerDamageCount());
}

class ForbidSynchronousCallContext : public FakeWebGraphicsContext3D {
public:
    ForbidSynchronousCallContext() { }

    virtual bool getActiveAttrib(WebGLId program, WGC3Duint index, ActiveInfo&) { ADD_FAILURE(); return false; }
    virtual bool getActiveUniform(WebGLId program, WGC3Duint index, ActiveInfo&) { ADD_FAILURE(); return false; }
    virtual void getAttachedShaders(WebGLId program, WGC3Dsizei maxCount, WGC3Dsizei* count, WebGLId* shaders) { ADD_FAILURE(); }
    virtual WGC3Dint getAttribLocation(WebGLId program, const WGC3Dchar* name) { ADD_FAILURE(); return 0; }
    virtual void getBooleanv(WGC3Denum pname, WGC3Dboolean* value) { ADD_FAILURE(); }
    virtual void getBufferParameteriv(WGC3Denum target, WGC3Denum pname, WGC3Dint* value) { ADD_FAILURE(); }
    virtual Attributes getContextAttributes() { ADD_FAILURE(); return m_attrs; }
    virtual WGC3Denum getError() { ADD_FAILURE(); return 0; }
    virtual void getFloatv(WGC3Denum pname, WGC3Dfloat* value) { ADD_FAILURE(); }
    virtual void getFramebufferAttachmentParameteriv(WGC3Denum target, WGC3Denum attachment, WGC3Denum pname, WGC3Dint* value) { ADD_FAILURE(); }
    virtual void getIntegerv(WGC3Denum pname, WGC3Dint* value)
    {
        if (pname == GL_MAX_TEXTURE_SIZE)
            *value = 1024; // MAX_TEXTURE_SIZE is cached client side, so it's OK to query.
        else
            ADD_FAILURE();
    }

    // We allow querying the shader compilation and program link status in debug mode, but not release.
    virtual void getProgramiv(WebGLId program, WGC3Denum pname, WGC3Dint* value)
    {
#ifndef NDEBUG
        *value = 1;
#else
        ADD_FAILURE();
#endif
    }

    virtual void getShaderiv(WebGLId shader, WGC3Denum pname, WGC3Dint* value)
    {
#ifndef NDEBUG
        *value = 1;
#else
        ADD_FAILURE();
#endif
    }

    virtual WebString getString(WGC3Denum name)
    {
        // We allow querying the extension string.
        // FIXME: It'd be better to check that we only do this before starting any other expensive work (like starting a compilation)
        if (name != GL_EXTENSIONS)
            ADD_FAILURE();
        return WebString();
    }

    virtual WebString getProgramInfoLog(WebGLId program) { ADD_FAILURE(); return WebString(); }
    virtual void getRenderbufferParameteriv(WGC3Denum target, WGC3Denum pname, WGC3Dint* value) { ADD_FAILURE(); }

    virtual WebString getShaderInfoLog(WebGLId shader) { ADD_FAILURE(); return WebString(); }
    virtual void getShaderPrecisionFormat(WGC3Denum shadertype, WGC3Denum precisiontype, WGC3Dint* range, WGC3Dint* precision) { ADD_FAILURE(); }
    virtual WebString getShaderSource(WebGLId shader) { ADD_FAILURE(); return WebString(); }
    virtual void getTexParameterfv(WGC3Denum target, WGC3Denum pname, WGC3Dfloat* value) { ADD_FAILURE(); }
    virtual void getTexParameteriv(WGC3Denum target, WGC3Denum pname, WGC3Dint* value) { ADD_FAILURE(); }
    virtual void getUniformfv(WebGLId program, WGC3Dint location, WGC3Dfloat* value) { ADD_FAILURE(); }
    virtual void getUniformiv(WebGLId program, WGC3Dint location, WGC3Dint* value) { ADD_FAILURE(); }
    virtual WGC3Dint getUniformLocation(WebGLId program, const WGC3Dchar* name) { ADD_FAILURE(); return 0; }
    virtual void getVertexAttribfv(WGC3Duint index, WGC3Denum pname, WGC3Dfloat* value) { ADD_FAILURE(); }
    virtual void getVertexAttribiv(WGC3Duint index, WGC3Denum pname, WGC3Dint* value) { ADD_FAILURE(); }
    virtual WGC3Dsizeiptr getVertexAttribOffset(WGC3Duint index, WGC3Denum pname) { ADD_FAILURE(); return 0; }
};

// This test isn't using the same fixture as GLRendererTest, and you can't mix TEST() and TEST_F() with the same name, hence LRC2.
TEST(GLRendererTest2, initializationDoesNotMakeSynchronousCalls)
{
    FakeRendererClient mockClient;
    scoped_ptr<GraphicsContext> context(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new ForbidSynchronousCallContext)));
    scoped_ptr<ResourceProvider> resourceProvider(ResourceProvider::create(context.get()));
    FakeRendererGL renderer(&mockClient, resourceProvider.get());

    EXPECT_TRUE(renderer.initialize());
}

class LoseContextOnFirstGetContext : public FakeWebGraphicsContext3D {
public:
    LoseContextOnFirstGetContext()
        : m_contextLost(false)
    {
    }

    virtual bool makeContextCurrent() OVERRIDE
    {
        return !m_contextLost;
    }

    virtual void getProgramiv(WebGLId program, WGC3Denum pname, WGC3Dint* value) OVERRIDE
    {
        m_contextLost = true;
        *value = 0;
    }

    virtual void getShaderiv(WebGLId shader, WGC3Denum pname, WGC3Dint* value) OVERRIDE
    {
        m_contextLost = true;
        *value = 0;
    }

    virtual WGC3Denum getGraphicsResetStatusARB() OVERRIDE
    {
        return m_contextLost ? 1 : 0;
    }

private:
    bool m_contextLost;
};

TEST(GLRendererTest2, initializationWithQuicklyLostContextDoesNotAssert)
{
    FakeRendererClient mockClient;
    scoped_ptr<GraphicsContext> context(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new LoseContextOnFirstGetContext)));
    scoped_ptr<ResourceProvider> resourceProvider(ResourceProvider::create(context.get()));
    FakeRendererGL renderer(&mockClient, resourceProvider.get());

    renderer.initialize();
}

class ContextThatDoesNotSupportMemoryManagmentExtensions : public FakeWebGraphicsContext3D {
public:
    ContextThatDoesNotSupportMemoryManagmentExtensions() { }

    // WebGraphicsContext3D methods.

    // This method would normally do a glSwapBuffers under the hood.
    virtual void prepareTexture() { }
    virtual void setMemoryAllocationChangedCallbackCHROMIUM(WebGraphicsMemoryAllocationChangedCallbackCHROMIUM* callback) { }
    virtual WebString getString(WebKit::WGC3Denum name) { return WebString(); }
};

TEST(GLRendererTest2, initializationWithoutGpuMemoryManagerExtensionSupportShouldDefaultToNonZeroAllocation)
{
    FakeRendererClient mockClient;
    scoped_ptr<GraphicsContext> context(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new ContextThatDoesNotSupportMemoryManagmentExtensions)));
    scoped_ptr<ResourceProvider> resourceProvider(ResourceProvider::create(context.get()));
    FakeRendererGL renderer(&mockClient, resourceProvider.get());

    renderer.initialize();

    EXPECT_GT(mockClient.memoryAllocationLimitBytes(), 0ul);
}

class ClearCountingContext : public FakeWebGraphicsContext3D {
public:
    ClearCountingContext() : m_clear(0) { }

    virtual void clear(WGC3Dbitfield)
    {
        m_clear++;
    }

    int clearCount() const { return m_clear; }

private:
    int m_clear;
};

TEST(GLRendererTest2, opaqueBackground)
{
    FakeRendererClient mockClient;
    scoped_ptr<GraphicsContext> outputSurface(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new ClearCountingContext)));
    ClearCountingContext* context = static_cast<ClearCountingContext*>(outputSurface->context3D());
    scoped_ptr<ResourceProvider> resourceProvider(ResourceProvider::create(outputSurface.get()));
    FakeRendererGL renderer(&mockClient, resourceProvider.get());

    mockClient.rootRenderPass()->has_transparent_background = false;

    EXPECT_TRUE(renderer.initialize());

    renderer.drawFrame(mockClient.renderPassesInDrawOrder(), mockClient.renderPasses());

    // On DEBUG builds, render passes with opaque background clear to blue to
    // easily see regions that were not drawn on the screen.
#ifdef NDEBUG
    EXPECT_EQ(0, context->clearCount());
#else
    EXPECT_EQ(1, context->clearCount());
#endif
}

TEST(GLRendererTest2, transparentBackground)
{
    FakeRendererClient mockClient;
    scoped_ptr<GraphicsContext> outputSurface(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new ClearCountingContext)));
    ClearCountingContext* context = static_cast<ClearCountingContext*>(outputSurface->context3D());
    scoped_ptr<ResourceProvider> resourceProvider(ResourceProvider::create(outputSurface.get()));
    FakeRendererGL renderer(&mockClient, resourceProvider.get());

    mockClient.rootRenderPass()->has_transparent_background = true;

    EXPECT_TRUE(renderer.initialize());

    renderer.drawFrame(mockClient.renderPassesInDrawOrder(), mockClient.renderPasses());

    EXPECT_EQ(1, context->clearCount());
}

class VisibilityChangeIsLastCallTrackingContext : public FakeWebGraphicsContext3D {
public:
    VisibilityChangeIsLastCallTrackingContext()
        : m_lastCallWasSetVisibility(0)
    {
    }

    // WebGraphicsContext3D methods.
    virtual void setVisibilityCHROMIUM(bool visible) {
        if (!m_lastCallWasSetVisibility)
            return;
        DCHECK(*m_lastCallWasSetVisibility == false);
        *m_lastCallWasSetVisibility = true;
    }
    virtual void flush() { if (m_lastCallWasSetVisibility) *m_lastCallWasSetVisibility = false; }
    virtual void deleteTexture(WebGLId) { if (m_lastCallWasSetVisibility) *m_lastCallWasSetVisibility = false; }
    virtual void deleteFramebuffer(WebGLId) { if (m_lastCallWasSetVisibility) *m_lastCallWasSetVisibility = false; }
    virtual void deleteRenderbuffer(WebGLId) { if (m_lastCallWasSetVisibility) *m_lastCallWasSetVisibility = false; }

    // This method would normally do a glSwapBuffers under the hood.
    virtual WebString getString(WebKit::WGC3Denum name)
    {
        if (name == GL_EXTENSIONS)
            return WebString("GL_CHROMIUM_set_visibility GL_CHROMIUM_gpu_memory_manager GL_CHROMIUM_discard_framebuffer");
        return WebString();
    }

    // Methods added for test.
    void setLastCallWasSetVisibilityPointer(bool* lastCallWasSetVisibility) { m_lastCallWasSetVisibility = lastCallWasSetVisibility; }

private:
    bool* m_lastCallWasSetVisibility;
};

TEST(GLRendererTest2, visibilityChangeIsLastCall)
{
    FakeRendererClient mockClient;
    scoped_ptr<GraphicsContext> outputSurface(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new VisibilityChangeIsLastCallTrackingContext)));
    VisibilityChangeIsLastCallTrackingContext* context = static_cast<VisibilityChangeIsLastCallTrackingContext*>(outputSurface->context3D());
    scoped_ptr<ResourceProvider> resourceProvider(ResourceProvider::create(outputSurface.get()));
    FakeRendererGL renderer(&mockClient, resourceProvider.get());

    EXPECT_TRUE(renderer.initialize());

    bool lastCallWasSetVisiblity = false;
    // Ensure that the call to setVisibilityCHROMIUM is the last call issue to the GPU
    // process, after glFlush is called, and after the RendererClient's enforceManagedMemoryPolicy
    // is called. Plumb this tracking between both the RenderClient and the Context by giving
    // them both a pointer to a variable on the stack.
    context->setLastCallWasSetVisibilityPointer(&lastCallWasSetVisiblity);
    mockClient.setLastCallWasSetVisibilityPointer(&lastCallWasSetVisiblity);
    renderer.setVisible(true);
    renderer.drawFrame(mockClient.renderPassesInDrawOrder(), mockClient.renderPasses());
    renderer.setVisible(false);
    EXPECT_TRUE(lastCallWasSetVisiblity);
}


class TextureStateTrackingContext : public FakeWebGraphicsContext3D {
public:
    TextureStateTrackingContext()
        : m_activeTexture(GL_INVALID_ENUM)
        , m_inDraw(false)
    {
    }

    virtual WebString getString(WGC3Denum name)
    {
        if (name == GL_EXTENSIONS)
            return WebString("GL_OES_EGL_image_external");
        return WebString();
    }

    // We shouldn't set any texture parameters during the draw sequence, although
    // we might when creating the quads.
    void setInDraw() { m_inDraw = true; }

    virtual void texParameteri(WGC3Denum target, WGC3Denum pname, WGC3Dint param)
    {
        if (m_inDraw)
            ADD_FAILURE();
    }

    virtual void activeTexture(WGC3Denum texture)
    {
        EXPECT_NE(texture, m_activeTexture);
        m_activeTexture = texture;
    }

    WGC3Denum activeTexture() const { return m_activeTexture; }

private:
    bool m_inDraw;
    WGC3Denum m_activeTexture;
};

TEST(GLRendererTest2, activeTextureState)
{
    FakeRendererClient fakeClient;
    scoped_ptr<GraphicsContext> outputSurface(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new TextureStateTrackingContext)));
    TextureStateTrackingContext* context = static_cast<TextureStateTrackingContext*>(outputSurface->context3D());
    scoped_ptr<ResourceProvider> resourceProvider(ResourceProvider::create(outputSurface.get()));
    FakeRendererGL renderer(&fakeClient, resourceProvider.get());

    EXPECT_TRUE(renderer.initialize());

    cc::RenderPass::Id id(1, 1);
    scoped_ptr<TestRenderPass> pass = TestRenderPass::Create();
    pass->SetNew(id, gfx::Rect(0, 0, 100, 100), gfx::Rect(0, 0, 100, 100), WebTransformationMatrix());
    pass->AppendOneOfEveryQuadType(resourceProvider.get());

    context->setInDraw();

    cc::DirectRenderer::DrawingFrame drawingFrame;
    renderer.beginDrawingFrame(drawingFrame);
    EXPECT_EQ(context->activeTexture(), GL_TEXTURE0);

    for (cc::QuadList::backToFrontIterator it = pass->quad_list.backToFrontBegin();
         it != pass->quad_list.backToFrontEnd(); ++it) {
        renderer.drawQuad(drawingFrame, *it);
    }
    EXPECT_EQ(context->activeTexture(), GL_TEXTURE0);
}

}  // namespace
}  // namespace cc
