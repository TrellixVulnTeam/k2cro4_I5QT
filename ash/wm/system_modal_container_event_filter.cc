// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/system_modal_container_event_filter.h"

#include "ash/wm/system_modal_container_event_filter_delegate.h"
#include "ui/aura/window.h"
#include "ui/base/events/event.h"

namespace ash {
namespace internal {

SystemModalContainerEventFilter::SystemModalContainerEventFilter(
    SystemModalContainerEventFilterDelegate* delegate)
    : delegate_(delegate) {
}

SystemModalContainerEventFilter::~SystemModalContainerEventFilter() {
}

ui::EventResult SystemModalContainerEventFilter::OnKeyEvent(
    ui::KeyEvent* event) {
  aura::Window* target = static_cast<aura::Window*>(event->target());
  return delegate_->CanWindowReceiveEvents(target) ? ui::ER_UNHANDLED :
                                                     ui::ER_CONSUMED;
}

ui::EventResult SystemModalContainerEventFilter::OnMouseEvent(
    ui::MouseEvent* event) {
  aura::Window* target = static_cast<aura::Window*>(event->target());
  return delegate_->CanWindowReceiveEvents(target) ? ui::ER_UNHANDLED :
                                                     ui::ER_CONSUMED;
}

}  // namespace internal
}  // namespace ash
