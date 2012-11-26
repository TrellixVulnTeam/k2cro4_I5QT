// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/gl_context_virtual.h"

#include "gpu/command_buffer/service/gl_state_restorer_impl.h"
#include "ui/gl/gl_surface.h"

namespace gpu {

GLContextVirtual::GLContextVirtual(
    gfx::GLShareGroup* share_group,
    gfx::GLContext* shared_context,
    gles2::GLES2Decoder* decoder)
  : GLContext(share_group),
    shared_context_(shared_context),
    display_(NULL),
    state_restorer_(new GLStateRestorerImpl(decoder)) {
  shared_context_->SetupForVirtualization();
}

gfx::Display* GLContextVirtual::display() {
  return display_;
}

bool GLContextVirtual::Initialize(
    gfx::GLSurface* compatible_surface, gfx::GpuPreference gpu_preference) {
  display_ = static_cast<gfx::Display*>(compatible_surface->GetDisplay());

  return true;
}

void GLContextVirtual::Destroy() {
}

bool GLContextVirtual::MakeCurrent(gfx::GLSurface* surface) {
  shared_context_->MakeVirtuallyCurrent(this, surface);
  return true;
}

void GLContextVirtual::ReleaseCurrent(gfx::GLSurface* surface) {
  shared_context_ = NULL;
  display_ = NULL;
}

bool GLContextVirtual::IsCurrent(gfx::GLSurface* surface) {
  return true;
}

void* GLContextVirtual::GetHandle() {
  return NULL;
}

gfx::GLStateRestorer* GLContextVirtual::GetGLStateRestorer() {
  return state_restorer_.get();
}

void GLContextVirtual::SetSwapInterval(int interval) {
  shared_context_->SetSwapInterval(interval);
}

std::string GLContextVirtual::GetExtensions() {
  return shared_context_->GetExtensions();
}

bool GLContextVirtual::GetTotalGpuMemory(size_t* bytes) {
  return shared_context_->GetTotalGpuMemory(bytes);
}

bool GLContextVirtual::WasAllocatedUsingRobustnessExtension() {
  return shared_context_->WasAllocatedUsingRobustnessExtension();
}

GLContextVirtual::~GLContextVirtual() {
  Destroy();
}

}  // namespace gpu
