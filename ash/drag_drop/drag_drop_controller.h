// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DRAG_DROP_DRAG_DROP_CONTROLLER_H_
#define ASH_DRAG_DROP_DRAG_DROP_CONTROLLER_H_

#include "ash/ash_export.h"
#include "base/callback.h"
#include "ui/aura/client/drag_drop_client.h"
#include "ui/aura/window_observer.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/events/event_constants.h"
#include "ui/base/events/event_handler.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/gfx/point.h"

namespace aura {
class RootWindow;
class Window;
}

namespace ui {
class LayerAnimationSequence;
}

namespace ash {

namespace test {
class DragDropControllerTest;
}

namespace internal {

class DragDropTracker;
class DragImageView;

class ASH_EXPORT DragDropController
    : public aura::client::DragDropClient,
      public ui::EventHandler,
      public ui::ImplicitAnimationObserver,
      public aura::WindowObserver {
 public:
  DragDropController();
  virtual ~DragDropController();

  void set_should_block_during_drag_drop(bool should_block_during_drag_drop) {
    should_block_during_drag_drop_ = should_block_during_drag_drop;
  }

  // Overridden from aura::client::DragDropClient:
  virtual int StartDragAndDrop(
      const ui::OSExchangeData& data,
      aura::RootWindow* root_window,
      aura::Window* source_window,
      const gfx::Point& root_location,
      int operation,
      ui::DragDropTypes::DragEventSource source) OVERRIDE;
  virtual void DragUpdate(aura::Window* target,
                          const ui::LocatedEvent& event) OVERRIDE;
  virtual void Drop(aura::Window* target,
                    const ui::LocatedEvent& event) OVERRIDE;
  virtual void DragCancel() OVERRIDE;
  virtual bool IsDragDropInProgress() OVERRIDE;

  // Overridden from ui::EventHandler:
  virtual ui::EventResult OnKeyEvent(ui::KeyEvent* event) OVERRIDE;
  virtual ui::EventResult OnMouseEvent(ui::MouseEvent* event) OVERRIDE;
  virtual ui::EventResult OnTouchEvent(ui::TouchEvent* event) OVERRIDE;

  // Overridden from aura::WindowObserver.
  virtual void OnWindowDestroyed(aura::Window* window) OVERRIDE;

 private:
  friend class ash::test::DragDropControllerTest;

  // Implementation of ImplicitAnimationObserver
  virtual void OnImplicitAnimationsCompleted() OVERRIDE;

  // Helper method to start drag widget flying back animation.
  void StartCanceledAnimation();

  // Helper method to reset everything.
  void Cleanup();

  scoped_ptr<DragImageView> drag_image_;
  gfx::Vector2d drag_image_offset_;
  const ui::OSExchangeData* drag_data_;
  int drag_operation_;

  // Window that is currently under the drag cursor.
  aura::Window* drag_window_;
  gfx::Point drag_start_location_;

  // Indicates whether the caller should be blocked on a drag/drop session.
  // Only be used for tests.
  bool should_block_during_drag_drop_;

  // Closure for quitting nested message loop.
  base::Closure quit_closure_;

  scoped_ptr<ash::internal::DragDropTracker> drag_drop_tracker_;

  DISALLOW_COPY_AND_ASSIGN(DragDropController);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_DRAG_DROP_DRAG_DROP_CONTROLLER_H_
