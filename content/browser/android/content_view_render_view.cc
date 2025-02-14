// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/android/content_view_render_view.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "content/browser/android/content_view_core_impl.h"
#include "content/public/browser/android/compositor.h"
#include "content/public/browser/android/content_view_layer_renderer.h"
#include "jni/ContentViewRenderView_jni.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebLayer.h"
#include "ui/gfx/size.h"

#include <android/native_window_jni.h>

using base::android::ScopedJavaLocalRef;

namespace content {

// static
bool ContentViewRenderView::RegisterContentViewRenderView(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

ContentViewRenderView::ContentViewRenderView()
    : scheduled_composite_(false),
      weak_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

ContentViewRenderView::~ContentViewRenderView() {
}

// static
jint Init(JNIEnv* env, jclass clazz) {
  ContentViewRenderView* content_view_render_view =
      new ContentViewRenderView();
  return reinterpret_cast<jint>(content_view_render_view);
}

void ContentViewRenderView::Destroy(JNIEnv* env, jobject obj) {
  delete this;
}

void ContentViewRenderView::SetCurrentContentView(
    JNIEnv* env, jobject obj, int native_content_view) {
  InitCompositor();
  ContentViewCoreImpl* content_view =
      reinterpret_cast<ContentViewCoreImpl*>(native_content_view);
  if (content_view)
    compositor_->SetRootLayer(content_view->GetWebLayer());
}

void ContentViewRenderView::SurfaceCreated(
    JNIEnv* env, jobject obj, jobject jsurface) {
  InitCompositor();
  ANativeWindow* native_window = ANativeWindow_fromSurface(env, jsurface);
  if (!native_window)
    return;

  compositor_->SetWindowSurface(native_window);
  ANativeWindow_release(native_window);
}

void ContentViewRenderView::SurfaceDestroyed(JNIEnv* env, jobject obj) {
  compositor_->SetWindowSurface(NULL);
}

void ContentViewRenderView::SurfaceSetSize(
    JNIEnv* env, jobject obj, jint width, jint height) {
  compositor_->SetWindowBounds(gfx::Size(width, height));
}

void ContentViewRenderView::ScheduleComposite() {
  if (scheduled_composite_)
    return;

  scheduled_composite_ = true;
  MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&ContentViewRenderView::Composite,
                 weak_factory_.GetWeakPtr()));
}

void ContentViewRenderView::InitCompositor() {
  if (compositor_.get())
    return;

  Compositor::Initialize();
  compositor_.reset(Compositor::Create(this));
}

void ContentViewRenderView::Composite() {
  if (!compositor_.get())
    return;

  scheduled_composite_ = false;
  compositor_->Composite();
}

}  // namespace content
