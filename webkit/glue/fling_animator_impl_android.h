// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_GLUE_FLING_ANIMATOR_IMPL_ANDROID_H_
#define WEBKIT_GLUE_FLING_ANIMATOR_IMPL_ANDROID_H_

#include "base/android/scoped_java_ref.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebFlingAnimator.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebFloatPoint.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebGestureCurve.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebPoint.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebRect.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebSize.h"
#include "ui/gfx/point.h"

namespace WebKit {
class WebGestureCurveTarget;
}

namespace webkit_glue {

// TODO(rjkroege): Remove WebFlingAnimator once all of the gesture curve
// implementation has moved to Chromium.
class FlingAnimatorImpl : public WebKit::WebFlingAnimator,
                          public WebKit::WebGestureCurve {
 public:
  FlingAnimatorImpl();
  virtual ~FlingAnimatorImpl();

  static FlingAnimatorImpl* CreateAndroidGestureCurve(
      const WebKit::WebFloatPoint& velocity,
      const WebKit::WebSize&);

  // WebKit::WebFlingAnimator methods.
  virtual void startFling(const WebKit::WebFloatPoint& velocity,
                          const WebKit::WebRect& range);
  // Returns true if the animation is not yet finished.
  virtual bool updatePosition();
  virtual WebKit::WebPoint getCurrentPosition();
  virtual void cancelFling();

  // WebKit::WebGestureCurve methods.
  virtual bool apply(double time,
                     WebKit::WebGestureCurveTarget* target);
 private:
  bool is_active_;

  // Java OverScroller instance and methods.
  base::android::ScopedJavaGlobalRef<jobject> java_scroller_;
  jmethodID fling_method_id_;
  jmethodID abort_method_id_;
  jmethodID compute_method_id_;
  jmethodID getX_method_id_;
  jmethodID getY_method_id_;

  gfx::Point last_position_;

  DISALLOW_COPY_AND_ASSIGN(FlingAnimatorImpl);
};

} // namespace webkit_glue

#endif // WEBKIT_GLUE_FLING_ANIMATOR_IMPL_ANDROID_H_
