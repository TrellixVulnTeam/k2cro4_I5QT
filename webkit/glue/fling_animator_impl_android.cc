// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/fling_animator_impl_android.h"

#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"
#include "base/logging.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebGestureCurveTarget.h"
#include "ui/gfx/vector2d.h"

using base::android::AttachCurrentThread;
using base::android::CheckException;
using base::android::GetApplicationContext;
using base::android::GetClass;
using base::android::MethodID;
using base::android::ScopedJavaLocalRef;

namespace webkit_glue {

FlingAnimatorImpl::FlingAnimatorImpl()
    : is_active_(false) {
  // hold the global reference of the Java objects.
  JNIEnv* env = AttachCurrentThread();
  DCHECK(env);
  ScopedJavaLocalRef<jclass> cls(GetClass(env, "android/widget/OverScroller"));
  jmethodID constructor = MethodID::Get<MethodID::TYPE_INSTANCE>(
      env, cls.obj(), "<init>", "(Landroid/content/Context;)V");
  ScopedJavaLocalRef<jobject> tmp(env, env->NewObject(cls.obj(), constructor,
                                                      GetApplicationContext()));
  DCHECK(tmp.obj());
  java_scroller_.Reset(tmp);

  fling_method_id_ = MethodID::Get<MethodID::TYPE_INSTANCE>(
      env, cls.obj(), "fling", "(IIIIIIII)V");
  abort_method_id_ = MethodID::Get<MethodID::TYPE_INSTANCE>(
      env, cls.obj(), "abortAnimation", "()V");
  compute_method_id_ = MethodID::Get<MethodID::TYPE_INSTANCE>(
      env, cls.obj(), "computeScrollOffset", "()Z");
  getX_method_id_ = MethodID::Get<MethodID::TYPE_INSTANCE>(
      env, cls.obj(), "getCurrX", "()I");
  getY_method_id_ = MethodID::Get<MethodID::TYPE_INSTANCE>(
      env, cls.obj(), "getCurrY", "()I");
}

FlingAnimatorImpl::~FlingAnimatorImpl()
{
}

void FlingAnimatorImpl::startFling(const WebKit::WebFloatPoint& velocity,
                                   const WebKit::WebRect& /* range */)
{
  // Ignore "range" as it's always empty -- see http://webkit.org/b/96403
  // Instead, use the largest possible bounds for minX/maxX/minY/maxY. The
  // compositor will ignore any attempt to scroll beyond the end of the page.

  DCHECK(velocity.x || velocity.y);
  if (is_active_)
    cancelFling();

  is_active_ = true;

  JNIEnv* env = AttachCurrentThread();

  env->CallVoidMethod(java_scroller_.obj(), fling_method_id_, 0, 0,
                      static_cast<int>(velocity.x),
                      static_cast<int>(velocity.y),
                      INT_MIN, INT_MAX, INT_MIN, INT_MAX);
  CheckException(env);
}

void FlingAnimatorImpl::cancelFling()
{
  if (!is_active_)
    return;

  is_active_ = false;
  JNIEnv* env = AttachCurrentThread();
  env->CallVoidMethod(java_scroller_.obj(), abort_method_id_);
  CheckException(env);
}

bool FlingAnimatorImpl::updatePosition()
{
  JNIEnv* env = AttachCurrentThread();
  bool result = env->CallBooleanMethod(java_scroller_.obj(),
                                       compute_method_id_);
  CheckException(env);
  return is_active_ = result;
}

WebKit::WebPoint FlingAnimatorImpl::getCurrentPosition()
{
  JNIEnv* env = AttachCurrentThread();
  WebKit::WebPoint position(
      env->CallIntMethod(java_scroller_.obj(), getX_method_id_),
      env->CallIntMethod(java_scroller_.obj(), getY_method_id_));
  CheckException(env);
  return position;
}

bool FlingAnimatorImpl::apply(double time,
                              WebKit::WebGestureCurveTarget* target) {
  if (!updatePosition())
    return false;

  gfx::Point current_position = getCurrentPosition();
  gfx::Vector2d diff(current_position - last_position_);
  WebKit::WebPoint scroll_amount(diff.x(), diff.y());
  target->scrollBy(scroll_amount);
  last_position_ = current_position;
  return true;
}

FlingAnimatorImpl* FlingAnimatorImpl::CreateAndroidGestureCurve(
    const WebKit::WebFloatPoint& velocity,
    const WebKit::WebSize&) {
  FlingAnimatorImpl* gesture_curve = new FlingAnimatorImpl();
  gesture_curve->startFling(velocity, WebKit::WebRect());
  return gesture_curve;
}

} // namespace webkit_glue
