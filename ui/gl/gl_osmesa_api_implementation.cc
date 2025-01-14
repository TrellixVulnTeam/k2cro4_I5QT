// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/gl_osmesa_api_implementation.h"

namespace gfx {

RealOSMESAApi* g_real_osmesa;

void InitializeGLBindingsOSMESA() {
  g_driver_osmesa.InitializeBindings();
  if (!g_real_osmesa) {
    g_real_osmesa = new RealOSMESAApi();
  }
  g_real_osmesa->Initialize(&g_driver_osmesa);
  g_current_osmesa_context = g_real_osmesa;
}

void InitializeGLExtensionBindingsOSMESA(GLContext* context) {
  g_driver_osmesa.InitializeExtensionBindings(context);
}

void InitializeDebugGLBindingsOSMESA() {
  g_driver_osmesa.InitializeDebugBindings();
}

void ClearGLBindingsOSMESA() {
  if (g_real_osmesa) {
    delete g_real_osmesa;
    g_real_osmesa = NULL;
  }
  g_current_osmesa_context = NULL;
  g_driver_osmesa.ClearBindings();
}

OSMESAApi::OSMESAApi() {
}

OSMESAApi::~OSMESAApi() {
}

RealOSMESAApi::RealOSMESAApi() {
}

RealOSMESAApi::~RealOSMESAApi() {
}

void RealOSMESAApi::Initialize(DriverOSMESA* driver) {
  driver_ = driver;
}

}  // namespace gfx


