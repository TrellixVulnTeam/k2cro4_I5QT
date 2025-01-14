// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/slide_out_view.h"

#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/transform.h"

namespace views {

SlideOutView::SlideOutView()
    : gesture_scroll_amount_(0.f) {
  SetPaintToLayer(true);
  SetFillsBoundsOpaquely(false);
}

SlideOutView::~SlideOutView() {
}

ui::EventResult SlideOutView::OnGestureEvent(ui::GestureEvent* event) {
  if (event->type() == ui::ET_SCROLL_FLING_START) {
    // The threshold for the fling velocity is computed empirically.
    // The unit is in pixels/second.
    const float kFlingThresholdForClose = 800.f;
    if (fabsf(event->details().velocity_x()) > kFlingThresholdForClose) {
      SlideOutAndClose(event->details().velocity_x() < 0 ? SLIDE_LEFT :
                       SLIDE_RIGHT);
      return ui::ER_CONSUMED;
    }
    RestoreVisualState();
    return ui::ER_UNHANDLED;
  }

  if (!event->IsScrollGestureEvent())
    return ui::ER_UNHANDLED;

  if (event->type() == ui::ET_GESTURE_SCROLL_BEGIN) {
    gesture_scroll_amount_ = 0.f;
  } else if (event->type() == ui::ET_GESTURE_SCROLL_UPDATE) {
    // The scroll-update events include the incremental scroll amount.
    gesture_scroll_amount_ += event->details().scroll_x();

    gfx::Transform transform;
    transform.SetTranslateX(gesture_scroll_amount_);
    layer()->SetTransform(transform);
    layer()->SetOpacity(
        1.f - std::min(fabsf(gesture_scroll_amount_) / width(), 1.f));
  } else if (event->type() == ui::ET_GESTURE_SCROLL_END) {
    const float kScrollRatioForClosingNotification = 0.5f;
    float scrolled_ratio = fabsf(gesture_scroll_amount_) / width();
    if (scrolled_ratio >= kScrollRatioForClosingNotification) {
      SlideOutAndClose(gesture_scroll_amount_ < 0 ? SLIDE_LEFT : SLIDE_RIGHT);
      return ui::ER_CONSUMED;
    }
    RestoreVisualState();
  }

  return ui::ER_HANDLED;
}

void SlideOutView::RestoreVisualState() {
  // Restore the layer state.
  const int kSwipeRestoreDurationMS = 150;
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kSwipeRestoreDurationMS));
  layer()->SetTransform(gfx::Transform());
  layer()->SetOpacity(1.f);
}

void SlideOutView::SlideOutAndClose(SlideDirection direction) {
  const int kSwipeOutTotalDurationMS = 150;
  int swipe_out_duration = kSwipeOutTotalDurationMS * layer()->opacity();
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(swipe_out_duration));
  settings.AddObserver(this);

  gfx::Transform transform;
  transform.SetTranslateX(direction == SLIDE_LEFT ? -width() : width());
  layer()->SetTransform(transform);
  layer()->SetOpacity(0.f);
}

void SlideOutView::OnImplicitAnimationsCompleted() {
  OnSlideOut();
}

}  // namespace views
