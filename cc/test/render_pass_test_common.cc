// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/render_pass_test_common.h"

#include "cc/checkerboard_draw_quad.h"
#include "cc/debug_border_draw_quad.h"
#include "cc/io_surface_draw_quad.h"
#include "cc/render_pass_draw_quad.h"
#include "cc/solid_color_draw_quad.h"
#include "cc/shared_quad_state.h"
#include "cc/stream_video_draw_quad.h"
#include "cc/texture_draw_quad.h"
#include "cc/tile_draw_quad.h"
#include "cc/yuv_video_draw_quad.h"
#include "cc/resource_provider.h"
#include <public/WebTransformationMatrix.h>

namespace WebKitTests {

using cc::DrawQuad;
using WebKit::WebTransformationMatrix;

void TestRenderPass::AppendOneOfEveryQuadType(cc::ResourceProvider* resourceProvider) {
  gfx::Rect rect(0, 0, 100, 100);
  gfx::Rect opaque_rect(10, 10, 80, 80);
  cc::ResourceProvider::ResourceId texture_resource =
      resourceProvider->createResourceFromExternalTexture(1);
  scoped_ptr<cc::SharedQuadState> shared_state = cc::SharedQuadState::Create();
  shared_state->SetAll(WebTransformationMatrix(),
                       rect,
                       rect,
                       rect,
                       false,
                       1);

  scoped_ptr<cc::CheckerboardDrawQuad> checkerboard_quad =
      cc::CheckerboardDrawQuad::Create();
  checkerboard_quad->SetNew(shared_state.get(),
                            rect,
                            SK_ColorRED);
  AppendQuad(checkerboard_quad.PassAs<DrawQuad>());

  scoped_ptr<cc::DebugBorderDrawQuad> debug_border_quad =
      cc::DebugBorderDrawQuad::Create();
  debug_border_quad->SetNew(shared_state.get(),
                            rect,
                            SK_ColorRED,
                            1);
  AppendQuad(debug_border_quad.PassAs<DrawQuad>());

  scoped_ptr<cc::IOSurfaceDrawQuad> io_surface_quad =
      cc::IOSurfaceDrawQuad::Create();
  io_surface_quad->SetNew(shared_state.get(),
                          rect,
                          opaque_rect,
                          gfx::Size(50, 50),
                          1,
                          cc::IOSurfaceDrawQuad::FLIPPED);
  AppendQuad(io_surface_quad.PassAs<DrawQuad>());

  scoped_ptr<cc::RenderPassDrawQuad> render_pass_quad =
      cc::RenderPassDrawQuad::Create();
  render_pass_quad->SetNew(shared_state.get(),
                           rect,
                           cc::RenderPass::Id(1, 1),
                           false,
                           0,
                           rect,
                           0,
                           0,
                           0,
                           0);
  AppendQuad(render_pass_quad.PassAs<DrawQuad>());

  scoped_ptr<cc::SolidColorDrawQuad> solid_color_quad =
      cc::SolidColorDrawQuad::Create();
  solid_color_quad->SetNew(shared_state.get(),
                           rect,
                           SK_ColorRED);
  AppendQuad(solid_color_quad.PassAs<DrawQuad>());

  scoped_ptr<cc::StreamVideoDrawQuad> stream_video_quad =
      cc::StreamVideoDrawQuad::Create();
  stream_video_quad->SetNew(shared_state.get(),
                            rect,
                            opaque_rect,
                            1,
                            WebKit::WebTransformationMatrix());
  AppendQuad(stream_video_quad.PassAs<DrawQuad>());

  scoped_ptr<cc::TextureDrawQuad> texture_quad =
      cc::TextureDrawQuad::Create();
  texture_quad->SetNew(shared_state.get(),
                       rect,
                       opaque_rect,
                       texture_resource,
                       false,
                       rect,
                       false);
  AppendQuad(texture_quad.PassAs<DrawQuad>());

  scoped_ptr<cc::TileDrawQuad> tile_quad =
      cc::TileDrawQuad::Create();
  tile_quad->SetNew(shared_state.get(),
                    rect,
                    opaque_rect,
                    texture_resource,
                    gfx::RectF(0, 0, 100, 100),
                    gfx::Size(100, 100),
                    false,
                    false,
                    false,
                    false,
                    false);
  AppendQuad(tile_quad.PassAs<DrawQuad>());

  cc::VideoLayerImpl::FramePlane planes[3];
  for (int i = 0; i < 3; ++i) {
    planes[i].resourceId =
        resourceProvider->createResourceFromExternalTexture(1);
    planes[i].size = gfx::Size(100, 100);
    planes[i].format = GL_LUMINANCE;
  }
  scoped_ptr<cc::YUVVideoDrawQuad> yuv_quad =
      cc::YUVVideoDrawQuad::Create();
  yuv_quad->SetNew(shared_state.get(),
                   rect,
                   opaque_rect,
                   gfx::Size(100, 100),
                   planes[0],
                   planes[1],
                   planes[2]);
  AppendQuad(yuv_quad.PassAs<DrawQuad>());

  AppendSharedQuadState(shared_state.Pass());
}

}  // namespace WebKitTests
