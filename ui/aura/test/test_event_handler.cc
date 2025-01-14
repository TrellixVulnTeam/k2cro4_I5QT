// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/test/test_event_handler.h"

namespace aura {
namespace test {

TestEventHandler::TestEventHandler()
    : num_key_events_(0),
      num_mouse_events_(0),
      num_scroll_events_(0),
      num_touch_events_(0),
      num_gesture_events_(0) {
}

TestEventHandler::~TestEventHandler() {}

void TestEventHandler::Reset() {
  num_key_events_ = 0;
  num_mouse_events_ = 0;
  num_scroll_events_ = 0;
  num_touch_events_ = 0;
  num_gesture_events_ = 0;
}

ui::EventResult TestEventHandler::OnKeyEvent(ui::KeyEvent* event) {
  num_key_events_++;
  return ui::ER_UNHANDLED;
}

ui::EventResult TestEventHandler::OnMouseEvent(ui::MouseEvent* event) {
  num_mouse_events_++;
  return ui::ER_UNHANDLED;
}

ui::EventResult TestEventHandler::OnScrollEvent(ui::ScrollEvent* event) {
  num_scroll_events_++;
  return ui::ER_UNHANDLED;
}

ui::EventResult TestEventHandler::OnTouchEvent(ui::TouchEvent* event) {
  num_touch_events_++;
  return ui::ER_UNHANDLED;
}

ui::EventResult TestEventHandler::OnGestureEvent(ui::GestureEvent* event) {
  num_gesture_events_++;
  return ui::ER_UNHANDLED;
}

}  // namespace test
}  // namespace aura
