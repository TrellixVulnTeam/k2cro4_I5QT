// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/shelf_layout_manager.h"

#include <algorithm>
#include <cmath>

#include "ash/ash_switches.h"
#include "ash/launcher/launcher.h"
#include "ash/root_window_controller.h"
#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/shell_window_ids.h"
#include "ash/system/status_area_widget.h"
#include "ash/wm/workspace_controller.h"
#include "ash/wm/workspace/workspace_animations.h"
#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/root_window.h"
#include "ui/base/events/event.h"
#include "ui/base/events/event_handler.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/screen.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace internal {

namespace {

// Delay before showing the launcher. This is after the mouse stops moving.
const int kAutoHideDelayMS = 200;

// To avoid hiding the shelf when the mouse transitions from a message bubble
// into the shelf, the hit test area is enlarged by this amount of pixels to
// keep the shelf from hiding.
const int kNotificationBubbleGapHeight = 6;

ui::Layer* GetLayer(views::Widget* widget) {
  return widget->GetNativeView()->layer();
}

bool IsDraggingTrayEnabled() {
  static bool dragging_tray_allowed = CommandLine::ForCurrentProcess()->
      HasSwitch(ash::switches::kAshEnableTrayDragging);
  return dragging_tray_allowed;
}

}  // namespace

// static
const int ShelfLayoutManager::kWorkspaceAreaBottomInset = 2;

// static
const int ShelfLayoutManager::kAutoHideSize = 3;

// ShelfLayoutManager::AutoHideEventFilter -------------------------------------

// Notifies ShelfLayoutManager any time the mouse moves.
class ShelfLayoutManager::AutoHideEventFilter : public ui::EventHandler {
 public:
  explicit AutoHideEventFilter(ShelfLayoutManager* shelf);
  virtual ~AutoHideEventFilter();

  // Returns true if the last mouse event was a mouse drag.
  bool in_mouse_drag() const { return in_mouse_drag_; }

  // Overridden from ui::EventHandler:
  virtual ui::EventResult OnMouseEvent(ui::MouseEvent* event) OVERRIDE;

 private:
  ShelfLayoutManager* shelf_;
  bool in_mouse_drag_;

  DISALLOW_COPY_AND_ASSIGN(AutoHideEventFilter);
};

ShelfLayoutManager::AutoHideEventFilter::AutoHideEventFilter(
    ShelfLayoutManager* shelf)
    : shelf_(shelf),
      in_mouse_drag_(false) {
  Shell::GetInstance()->AddPreTargetHandler(this);
}

ShelfLayoutManager::AutoHideEventFilter::~AutoHideEventFilter() {
  Shell::GetInstance()->RemovePreTargetHandler(this);
}

ui::EventResult ShelfLayoutManager::AutoHideEventFilter::OnMouseEvent(
    ui::MouseEvent* event) {
  // This also checks IsShelfWindow() to make sure we don't attempt to hide the
  // shelf if the mouse down occurs on the shelf.
  in_mouse_drag_ = (event->type() == ui::ET_MOUSE_DRAGGED ||
                    (in_mouse_drag_ && event->type() != ui::ET_MOUSE_RELEASED &&
                     event->type() != ui::ET_MOUSE_CAPTURE_CHANGED)) &&
      !shelf_->IsShelfWindow(static_cast<aura::Window*>(event->target()));
  if (event->type() == ui::ET_MOUSE_MOVED)
    shelf_->UpdateAutoHideState();
  return ui::ER_UNHANDLED;
}

// ShelfLayoutManager:UpdateShelfObserver --------------------------------------

// UpdateShelfObserver is used to delay updating the background until the
// animation completes.
class ShelfLayoutManager::UpdateShelfObserver
    : public ui::ImplicitAnimationObserver {
 public:
  explicit UpdateShelfObserver(ShelfLayoutManager* shelf) : shelf_(shelf) {
    shelf_->update_shelf_observer_ = this;
  }

  void Detach() {
    shelf_ = NULL;
  }

  virtual void OnImplicitAnimationsCompleted() OVERRIDE {
    if (shelf_) {
      shelf_->UpdateShelfBackground(
          internal::BackgroundAnimator::CHANGE_ANIMATE);
    }
    delete this;
  }

 private:
  virtual ~UpdateShelfObserver() {
    if (shelf_)
      shelf_->update_shelf_observer_ = NULL;
  }

  // Shelf we're in. NULL if deleted before we're deleted.
  ShelfLayoutManager* shelf_;

  DISALLOW_COPY_AND_ASSIGN(UpdateShelfObserver);
};

// ShelfLayoutManager ----------------------------------------------------------

ShelfLayoutManager::ShelfLayoutManager(StatusAreaWidget* status_area_widget)
    : root_window_(status_area_widget->GetNativeView()->GetRootWindow()),
      in_layout_(false),
      auto_hide_behavior_(SHELF_AUTO_HIDE_BEHAVIOR_NEVER),
      alignment_(SHELF_ALIGNMENT_BOTTOM),
      launcher_(NULL),
      status_area_widget_(status_area_widget),
      workspace_controller_(NULL),
      window_overlaps_shelf_(false),
      gesture_drag_status_(GESTURE_DRAG_NONE),
      gesture_drag_amount_(0.f),
      gesture_drag_auto_hide_state_(AUTO_HIDE_SHOWN),
      update_shelf_observer_(NULL) {
  Shell::GetInstance()->AddShellObserver(this);
  aura::client::GetActivationClient(root_window_)->AddObserver(this);
}

ShelfLayoutManager::~ShelfLayoutManager() {
  if (update_shelf_observer_)
    update_shelf_observer_->Detach();

  FOR_EACH_OBSERVER(Observer, observers_, WillDeleteShelf());
  Shell::GetInstance()->RemoveShellObserver(this);
  aura::client::GetActivationClient(root_window_)->RemoveObserver(this);
}

void ShelfLayoutManager::SetAutoHideBehavior(ShelfAutoHideBehavior behavior) {
  if (auto_hide_behavior_ == behavior)
    return;
  auto_hide_behavior_ = behavior;
  UpdateVisibilityState();
  FOR_EACH_OBSERVER(Observer, observers_,
                    OnAutoHideStateChanged(state_.auto_hide_state));
}

bool ShelfLayoutManager::IsVisible() const {
  return status_area_widget_->IsVisible() &&
      (state_.visibility_state == VISIBLE ||
       (state_.visibility_state == AUTO_HIDE &&
        state_.auto_hide_state == AUTO_HIDE_SHOWN));
}

void ShelfLayoutManager::SetLauncher(Launcher* launcher) {
  if (launcher == launcher_)
    return;

  launcher_ = launcher;
  if (launcher_)
    launcher_->SetAlignment(alignment_);
  LayoutShelf();
}

bool ShelfLayoutManager::SetAlignment(ShelfAlignment alignment) {
  if (alignment_ == alignment)
    return false;

  alignment_ = alignment;
  if (launcher_)
    launcher_->SetAlignment(alignment);
  status_area_widget_->SetShelfAlignment(alignment);
  LayoutShelf();
  return true;
}

gfx::Rect ShelfLayoutManager::GetIdealBounds() {
  // TODO(oshima): this is wrong. Figure out what display shelf is on
  // and everything should be based on it.
  gfx::Rect bounds(ScreenAsh::GetDisplayBoundsInParent(
      status_area_widget_->GetNativeView()));
  int width = 0, height = 0;
  GetShelfSize(&width, &height);
  switch (alignment_) {
    case SHELF_ALIGNMENT_BOTTOM:
      return gfx::Rect(bounds.x(), bounds.bottom() - height,
                       bounds.width(), height);
    case SHELF_ALIGNMENT_LEFT:
      return gfx::Rect(bounds.x(), bounds.y(), width, bounds.height());
    case SHELF_ALIGNMENT_RIGHT:
      return gfx::Rect(bounds.right() - width, bounds.y(), width,
                       bounds.height());
  }
  NOTREACHED();
  return gfx::Rect();
}

void ShelfLayoutManager::LayoutShelf() {
  base::AutoReset<bool> auto_reset_in_layout(&in_layout_, true);
  StopAnimating();
  TargetBounds target_bounds;
  CalculateTargetBounds(state_, &target_bounds);
  if (launcher_widget()) {
    GetLayer(launcher_widget())->SetOpacity(target_bounds.opacity);
    launcher_->SetWidgetBounds(
        ScreenAsh::ConvertRectToScreen(
            launcher_widget()->GetNativeView()->parent(),
            target_bounds.launcher_bounds_in_root));
    launcher_->SetStatusSize(target_bounds.status_bounds_in_root.size());
  }
  GetLayer(status_area_widget_)->SetOpacity(target_bounds.opacity);
  status_area_widget_->SetBounds(
      ScreenAsh::ConvertRectToScreen(
          status_area_widget_->GetNativeView()->parent(),
          target_bounds.status_bounds_in_root));
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(),
      target_bounds.work_area_insets);
  UpdateHitTestBounds();
}

void ShelfLayoutManager::UpdateVisibilityState() {
  ShellDelegate* delegate = Shell::GetInstance()->delegate();
  if (delegate && delegate->IsScreenLocked()) {
    SetState(VISIBLE);
  } else if (gesture_drag_status_ == GESTURE_DRAG_COMPLETE_IN_PROGRESS) {
    SetState(AUTO_HIDE);
  } else {
    WorkspaceWindowState window_state(workspace_controller_->GetWindowState());
    switch (window_state) {
      case WORKSPACE_WINDOW_STATE_FULL_SCREEN:
        SetState(HIDDEN);
        break;

      case WORKSPACE_WINDOW_STATE_MAXIMIZED:
          SetState(auto_hide_behavior_ == SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS ?
                   AUTO_HIDE : VISIBLE);
        break;

      case WORKSPACE_WINDOW_STATE_WINDOW_OVERLAPS_SHELF:
      case WORKSPACE_WINDOW_STATE_DEFAULT:
        SetState(auto_hide_behavior_ == SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS ?
                 AUTO_HIDE : VISIBLE);
        SetWindowOverlapsShelf(window_state ==
                               WORKSPACE_WINDOW_STATE_WINDOW_OVERLAPS_SHELF);
        break;
    }
  }
}

void ShelfLayoutManager::UpdateAutoHideState() {
  AutoHideState auto_hide_state =
      CalculateAutoHideState(state_.visibility_state);
  if (auto_hide_state != state_.auto_hide_state) {
    if (auto_hide_state == AUTO_HIDE_HIDDEN) {
      // Hides happen immediately.
      SetState(state_.visibility_state);
      FOR_EACH_OBSERVER(Observer, observers_,
                        OnAutoHideStateChanged(auto_hide_state));
    } else {
      auto_hide_timer_.Stop();
      auto_hide_timer_.Start(
          FROM_HERE,
          base::TimeDelta::FromMilliseconds(kAutoHideDelayMS),
          this, &ShelfLayoutManager::UpdateAutoHideStateNow);
      FOR_EACH_OBSERVER(Observer, observers_, OnAutoHideStateChanged(
          CalculateAutoHideState(state_.visibility_state)));
    }
  } else {
    auto_hide_timer_.Stop();
  }
}

void ShelfLayoutManager::SetWindowOverlapsShelf(bool value) {
  window_overlaps_shelf_ = value;
  UpdateShelfBackground(internal::BackgroundAnimator::CHANGE_ANIMATE);
}

void ShelfLayoutManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ShelfLayoutManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

////////////////////////////////////////////////////////////////////////////////
// ShelfLayoutManager, Gesture dragging:

void ShelfLayoutManager::StartGestureDrag(const ui::GestureEvent& gesture) {
  gesture_drag_status_ = GESTURE_DRAG_IN_PROGRESS;
  gesture_drag_amount_ = 0.f;
  gesture_drag_auto_hide_state_ = visibility_state() == AUTO_HIDE ?
      auto_hide_state() : AUTO_HIDE_SHOWN;
  UpdateShelfBackground(internal::BackgroundAnimator::CHANGE_ANIMATE);
}

ShelfLayoutManager::DragState ShelfLayoutManager::UpdateGestureDrag(
    const ui::GestureEvent& gesture) {
  bool horizontal = alignment() == SHELF_ALIGNMENT_BOTTOM;
  gesture_drag_amount_ += horizontal ? gesture.details().scroll_y() :
                                       gesture.details().scroll_x();
  LayoutShelf();

  // Start reveling the status menu when:
  //   - dragging up on an already visible shelf
  //   - dragging up on a hidden shelf, but it is currently completely visible.
  if (horizontal && gesture.details().scroll_y() < 0) {
    int min_height = 0;
    if (gesture_drag_auto_hide_state_ == AUTO_HIDE_HIDDEN && launcher_widget())
      min_height = launcher_widget()->GetContentsView()->
          GetPreferredSize().height();

    if (min_height < launcher_widget()->GetWindowBoundsInScreen().height() &&
        gesture.root_location().x() >=
        status_area_widget_->GetWindowBoundsInScreen().x() &&
        IsDraggingTrayEnabled())
      return DRAG_TRAY;
  }

  return DRAG_SHELF;
}

void ShelfLayoutManager::CompleteGestureDrag(const ui::GestureEvent& gesture) {
  bool horizontal = alignment() == SHELF_ALIGNMENT_BOTTOM;
  bool should_change = false;
  if (gesture.type() == ui::ET_GESTURE_SCROLL_END) {
    // If the shelf was dragged X% towards the correct direction, then it is
    // hidden/shown.
    const float kDragHideThreshold = 0.4f;
    gfx::Rect bounds = GetIdealBounds();
    float drag_amount = gesture_drag_auto_hide_state_ == AUTO_HIDE_SHOWN ?
        gesture_drag_amount_ : -gesture_drag_amount_;
    if (horizontal)
      should_change = drag_amount > kDragHideThreshold * bounds.height();
    else if (alignment() == SHELF_ALIGNMENT_LEFT)
      should_change = -drag_amount > kDragHideThreshold * bounds.width();
    else
      should_change = drag_amount > kDragHideThreshold * bounds.width();
  } else if (gesture.type() == ui::ET_SCROLL_FLING_START) {
    if (horizontal)
      should_change = gesture.details().velocity_y() > 0;
    else if (alignment() == SHELF_ALIGNMENT_LEFT)
      should_change = gesture.details().velocity_x() < 0;
    else
      should_change = gesture.details().velocity_x() > 0;
    if (gesture_drag_auto_hide_state_ == AUTO_HIDE_HIDDEN)
      should_change = !should_change;
  } else {
    NOTREACHED();
  }

  if (!should_change) {
    CancelGestureDrag();
    return;
  }

  gesture_drag_auto_hide_state_ =
      gesture_drag_auto_hide_state_ == AUTO_HIDE_SHOWN ? AUTO_HIDE_HIDDEN :
                                                         AUTO_HIDE_SHOWN;
  if (launcher_widget())
    launcher_widget()->Deactivate();
  status_area_widget_->Deactivate();
  if (gesture_drag_auto_hide_state_ == AUTO_HIDE_HIDDEN &&
      auto_hide_behavior_ != SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS) {
    gesture_drag_status_ = GESTURE_DRAG_NONE;
    SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  } else if (gesture_drag_auto_hide_state_ == AUTO_HIDE_SHOWN &&
             auto_hide_behavior_ != SHELF_AUTO_HIDE_BEHAVIOR_NEVER) {
    gesture_drag_status_ = GESTURE_DRAG_NONE;
    SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  } else {
    gesture_drag_status_ = GESTURE_DRAG_COMPLETE_IN_PROGRESS;
    UpdateVisibilityState();
    gesture_drag_status_ = GESTURE_DRAG_NONE;
  }
}

void ShelfLayoutManager::CancelGestureDrag() {
  gesture_drag_status_ = GESTURE_DRAG_NONE;
  ui::ScopedLayerAnimationSettings
      launcher_settings(GetLayer(launcher_widget())->GetAnimator()),
      status_settings(GetLayer(status_area_widget_)->GetAnimator());
  LayoutShelf();
  UpdateVisibilityState();
  UpdateShelfBackground(internal::BackgroundAnimator::CHANGE_ANIMATE);
}

////////////////////////////////////////////////////////////////////////////////
// ShelfLayoutManager, aura::LayoutManager implementation:

void ShelfLayoutManager::OnWindowResized() {
  LayoutShelf();
}

void ShelfLayoutManager::OnWindowAddedToLayout(aura::Window* child) {
}

void ShelfLayoutManager::OnWillRemoveWindowFromLayout(aura::Window* child) {
}

void ShelfLayoutManager::OnWindowRemovedFromLayout(aura::Window* child) {
}

void ShelfLayoutManager::OnChildWindowVisibilityChanged(aura::Window* child,
                                                        bool visible) {
}

void ShelfLayoutManager::SetChildBounds(aura::Window* child,
                                        const gfx::Rect& requested_bounds) {
  SetChildBoundsDirect(child, requested_bounds);
  // We may contain other widgets (such as frame maximize bubble) but they don't
  // effect the layout in anyway.
  if (!in_layout_ &&
      ((launcher_widget() && launcher_widget()->GetNativeView() == child) ||
       (status_area_widget_->GetNativeView() == child))) {
    LayoutShelf();
  }
}

void ShelfLayoutManager::OnLockStateChanged(bool locked) {
  UpdateVisibilityState();
}

void ShelfLayoutManager::OnWindowActivated(aura::Window* active,
                                           aura::Window* old_active) {
  UpdateAutoHideStateNow();
}

////////////////////////////////////////////////////////////////////////////////
// ShelfLayoutManager, private:

ShelfLayoutManager::TargetBounds::TargetBounds() : opacity(0.0f) {}

void ShelfLayoutManager::SetState(VisibilityState visibility_state) {
  ShellDelegate* delegate = Shell::GetInstance()->delegate();
  State state;
  state.visibility_state = visibility_state;
  state.auto_hide_state = CalculateAutoHideState(visibility_state);
  state.is_screen_locked = delegate && delegate->IsScreenLocked();

  // It's possible for SetState() when a window becomes maximized but the state
  // won't have changed value. Do the dimming check before the early exit.
  if (launcher_ && workspace_controller_) {
    launcher_->SetDimsShelf(
        (state.visibility_state == VISIBLE) &&
          workspace_controller_->GetWindowState() ==
              WORKSPACE_WINDOW_STATE_MAXIMIZED);
  }

  if (state_.Equals(state))
    return;  // Nothing changed.

  FOR_EACH_OBSERVER(Observer, observers_,
                    WillChangeVisibilityState(visibility_state));

  if (state.visibility_state == AUTO_HIDE) {
    // When state is AUTO_HIDE we need to track when the mouse is over the
    // launcher to unhide the shelf. AutoHideEventFilter does that for us.
    if (!event_filter_.get())
      event_filter_.reset(new AutoHideEventFilter(this));
  } else {
    event_filter_.reset(NULL);
  }

  auto_hide_timer_.Stop();

  // Animating the background when transitioning from auto-hide & hidden to
  // visible is janky. Update the background immediately in this case.
  internal::BackgroundAnimator::ChangeType change_type =
      (state_.visibility_state == AUTO_HIDE &&
       state_.auto_hide_state == AUTO_HIDE_HIDDEN &&
       state.visibility_state == VISIBLE) ?
      internal::BackgroundAnimator::CHANGE_IMMEDIATE :
      internal::BackgroundAnimator::CHANGE_ANIMATE;
  StopAnimating();

  State old_state = state_;
  state_ = state;
  TargetBounds target_bounds;
  CalculateTargetBounds(state_, &target_bounds);
  if (launcher_widget()) {
    ui::ScopedLayerAnimationSettings launcher_animation_setter(
        GetLayer(launcher_widget())->GetAnimator());
    launcher_animation_setter.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kWorkspaceSwitchTimeMS));
    launcher_animation_setter.SetTweenType(ui::Tween::EASE_OUT);
    GetLayer(launcher_widget())->SetBounds(
        target_bounds.launcher_bounds_in_root);
    GetLayer(launcher_widget())->SetOpacity(target_bounds.opacity);
  }
  ui::ScopedLayerAnimationSettings status_animation_setter(
      GetLayer(status_area_widget_)->GetAnimator());
  status_animation_setter.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kWorkspaceSwitchTimeMS));
  status_animation_setter.SetTweenType(ui::Tween::EASE_OUT);

  // Delay updating the background when going from AUTO_HIDE_SHOWN to
  // AUTO_HIDE_HIDDEN until the shelf animates out. Otherwise during the
  // animation you see the background change.
  // Also delay the animation when the shelf was hidden, and has just been made
  // visible (e.g. using a gesture-drag).
  bool delay_shelf_update =
      state.visibility_state == AUTO_HIDE &&
      state.auto_hide_state == AUTO_HIDE_HIDDEN &&
      old_state.visibility_state == AUTO_HIDE;

  if (state.visibility_state == VISIBLE &&
      old_state.visibility_state == AUTO_HIDE &&
      old_state.auto_hide_state == AUTO_HIDE_HIDDEN)
    delay_shelf_update = true;

  if (delay_shelf_update) {
    if (update_shelf_observer_)
      update_shelf_observer_->Detach();
    // UpdateShelfBackground deletes itself when the animation is done.
    update_shelf_observer_ = new UpdateShelfObserver(this);
    status_animation_setter.AddObserver(update_shelf_observer_);
  }
  ui::Layer* layer = GetLayer(status_area_widget_);
  layer->SetBounds(target_bounds.status_bounds_in_root);
  layer->SetOpacity(target_bounds.opacity);
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(),
      target_bounds.work_area_insets);
  UpdateHitTestBounds();
  if (!delay_shelf_update)
    UpdateShelfBackground(change_type);
}

void ShelfLayoutManager::StopAnimating() {
  ui::Layer* layer = GetLayer(status_area_widget_);
  if (launcher_widget())
    layer->GetAnimator()->StopAnimating();
  layer->GetAnimator()->StopAnimating();
}

void ShelfLayoutManager::GetShelfSize(int* width, int* height) {
  *width = *height = 0;
  gfx::Size status_size(status_area_widget_->GetWindowBoundsInScreen().size());
  gfx::Size launcher_size = launcher_ ?
      launcher_widget()->GetContentsView()->GetPreferredSize() : gfx::Size();
  if (alignment_ == SHELF_ALIGNMENT_BOTTOM)
    *height = std::max(launcher_size.height(), status_size.height());
  else
    *width = std::max(launcher_size.width(), status_size.width());
}

void ShelfLayoutManager::AdjustBoundsBasedOnAlignment(int inset,
                                                      gfx::Rect* bounds) const {
  switch (alignment_) {
    case SHELF_ALIGNMENT_BOTTOM:
      bounds->Inset(gfx::Insets(0, 0, inset, 0));
      break;
    case SHELF_ALIGNMENT_LEFT:
      bounds->Inset(gfx::Insets(0, inset, 0, 0));
      break;
    case SHELF_ALIGNMENT_RIGHT:
      bounds->Inset(gfx::Insets(0, 0, 0, inset));
      break;
  }
}

void ShelfLayoutManager::CalculateTargetBounds(
    const State& state,
    TargetBounds* target_bounds) {
  const gfx::Rect& available_bounds(root_window_->bounds());
  gfx::Rect status_size(status_area_widget_->GetWindowBoundsInScreen().size());
  gfx::Size launcher_size = launcher_ ?
      launcher_widget()->GetContentsView()->GetPreferredSize() : gfx::Size();
  int shelf_size = 0;
  int shelf_width = 0, shelf_height = 0;
  GetShelfSize(&shelf_width, &shelf_height);
  if (state.visibility_state == VISIBLE ||
      (state.visibility_state == AUTO_HIDE &&
       state.auto_hide_state == AUTO_HIDE_SHOWN)) {
    shelf_size = std::max(shelf_width, shelf_height);
    launcher_size.set_width(std::max(shelf_width,launcher_size.width()));
    launcher_size.set_height(std::max(shelf_height,launcher_size.height()));
  } else if (state.visibility_state == AUTO_HIDE &&
             state.auto_hide_state == AUTO_HIDE_HIDDEN) {
    shelf_size = kAutoHideSize;
    // Keep the launcher to its full height when dragging is in progress.
    if (gesture_drag_status_ == GESTURE_DRAG_NONE) {
      if (SHELF_ALIGNMENT_BOTTOM == alignment_)
        launcher_size.set_height(kAutoHideSize);
      else
        launcher_size.set_width(kAutoHideSize);
    }
  }
  switch(alignment_) {
    case SHELF_ALIGNMENT_BOTTOM:
      // The status widget should extend to the bottom and right edges.
      target_bounds->status_bounds_in_root = gfx::Rect(
          base::i18n::IsRTL() ? available_bounds.x() :
              available_bounds.right() - status_size.width(),
          available_bounds.bottom() - shelf_size + shelf_height -
              status_size.height(),
         status_size.width(), status_size.height());
      if (launcher_widget())
        target_bounds->launcher_bounds_in_root = gfx::Rect(
            available_bounds.x(),
            available_bounds.bottom() - shelf_size,
            available_bounds.width(),
            launcher_size.height());
      target_bounds->work_area_insets.Set(
          0, 0, GetWorkAreaSize(state, shelf_height), 0);
      break;
    case SHELF_ALIGNMENT_LEFT:
      target_bounds->status_bounds_in_root = gfx::Rect(
          available_bounds.x() + launcher_size.width() - status_size.width(),
          available_bounds.bottom() - status_size.height(),
          shelf_width, status_size.height());
      if (launcher_widget())
        target_bounds->launcher_bounds_in_root = gfx::Rect(
            available_bounds.x(), available_bounds.y(),
            launcher_size.width(), available_bounds.height());
      target_bounds->work_area_insets.Set(
          0, GetWorkAreaSize(state, launcher_size.width()), 0, 0);
      break;
    case SHELF_ALIGNMENT_RIGHT:
      target_bounds->status_bounds_in_root = gfx::Rect(
          available_bounds.right()- status_size.width() -
              shelf_size + shelf_width,
          available_bounds.bottom() - status_size.height(),
          shelf_width, status_size.height());
      if (launcher_widget())
        target_bounds->launcher_bounds_in_root = gfx::Rect(
            available_bounds.right() - launcher_size.width(),
            available_bounds.y(),
            launcher_size.width(), available_bounds.height());
      target_bounds->work_area_insets.Set(
          0, 0, 0, GetWorkAreaSize(state, launcher_size.width()));
      break;
  }
  target_bounds->opacity =
      (gesture_drag_status_ != GESTURE_DRAG_NONE ||
       state.visibility_state == VISIBLE ||
       state.visibility_state == AUTO_HIDE) ? 1.0f : 0.0f;
  if (gesture_drag_status_ == GESTURE_DRAG_IN_PROGRESS)
    UpdateTargetBoundsForGesture(target_bounds);
}

void ShelfLayoutManager::UpdateTargetBoundsForGesture(
    TargetBounds* target_bounds) const {
  CHECK_EQ(GESTURE_DRAG_IN_PROGRESS, gesture_drag_status_);
  bool horizontal = alignment() == SHELF_ALIGNMENT_BOTTOM;
  int resistance_free_region = 0;

  if (gesture_drag_auto_hide_state_ == AUTO_HIDE_HIDDEN &&
      visibility_state() == AUTO_HIDE && auto_hide_state() != AUTO_HIDE_SHOWN) {
    // If the shelf was hidden when the drag started (and the state hasn't
    // changed since then, e.g. because the tray-menu was shown because of the
    // drag), then allow the drag some resistance-free region at first to make
    // sure the shelf sticks with the finger until the shelf is visible.
    resistance_free_region += horizontal ?
        target_bounds->launcher_bounds_in_root.height() :
        target_bounds->launcher_bounds_in_root.width();
    resistance_free_region -= kAutoHideSize;
  }

  bool resist = false;
  if (horizontal)
    resist = gesture_drag_amount_ < -resistance_free_region;
  else if (alignment() == SHELF_ALIGNMENT_LEFT)
    resist = gesture_drag_amount_ > resistance_free_region;
  else
    resist = gesture_drag_amount_ < -resistance_free_region;

  float translate = 0.f;
  if (resist) {
    float diff = fabsf(gesture_drag_amount_) - resistance_free_region;
    diff = std::min(diff, sqrtf(diff));
    if (gesture_drag_amount_ < 0)
      translate = -resistance_free_region - diff;
    else
      translate = resistance_free_region + diff;
  } else {
    translate = gesture_drag_amount_;
  }

  if (horizontal) {
    // Move the launcher with the gesture.
    target_bounds->launcher_bounds_in_root.Offset(0, translate);

    if (translate > 0) {
      // When dragging down, the statusbar should move.
      target_bounds->status_bounds_in_root.Offset(0, translate);
    } else {
      // When dragging up, the launcher height should increase.
      float move = std::max(translate,
                            -static_cast<float>(resistance_free_region));
      target_bounds->launcher_bounds_in_root.set_height(
          target_bounds->launcher_bounds_in_root.height() + move - translate);

      // The statusbar should be in the center.
      gfx::Rect status_y = target_bounds->launcher_bounds_in_root;
      status_y.ClampToCenteredSize(
          target_bounds->status_bounds_in_root.size());
      target_bounds->status_bounds_in_root.set_y(status_y.y());
    }
  } else {
    // Move the launcher with the gesture.
    if (alignment() == SHELF_ALIGNMENT_RIGHT)
      target_bounds->launcher_bounds_in_root.Offset(translate, 0);

    if ((translate > 0 && alignment() == SHELF_ALIGNMENT_RIGHT) ||
        (translate < 0 && alignment() == SHELF_ALIGNMENT_LEFT)) {
      // When dragging towards the edge, the statusbar should move.
      target_bounds->status_bounds_in_root.Offset(translate, 0);
    } else {
      // When dragging away from the edge, the launcher width should increase.
      float move = alignment() == SHELF_ALIGNMENT_RIGHT ?
          std::max(translate, -static_cast<float>(resistance_free_region)) :
          std::min(translate, static_cast<float>(resistance_free_region));

      if (alignment() == SHELF_ALIGNMENT_RIGHT) {
        target_bounds->launcher_bounds_in_root.set_width(
            target_bounds->launcher_bounds_in_root.width() + move - translate);
      } else {
        target_bounds->launcher_bounds_in_root.set_width(
            target_bounds->launcher_bounds_in_root.width() - move + translate);
      }

      // The statusbar should be in the center.
      gfx::Rect status_x = target_bounds->launcher_bounds_in_root;
      status_x.ClampToCenteredSize(
          target_bounds->status_bounds_in_root.size());
      target_bounds->status_bounds_in_root.set_x(status_x.x());
    }
  }
}

void ShelfLayoutManager::UpdateShelfBackground(
    BackgroundAnimator::ChangeType type) {
  bool launcher_paints = GetLauncherPaintsBackground();
  if (launcher_)
    launcher_->SetPaintsBackground(launcher_paints, type);
  // The status area normally draws a background, but we don't want it to draw a
  // background when the launcher does or when we're at login/lock screen.
  ShellDelegate* delegate = Shell::GetInstance()->delegate();
  bool delegate_allows_tray_bg =
      delegate->IsUserLoggedIn() && !delegate->IsScreenLocked();
  bool status_area_paints = !launcher_paints && delegate_allows_tray_bg;
  status_area_widget_->SetPaintsBackground(status_area_paints, type);
}

bool ShelfLayoutManager::GetLauncherPaintsBackground() const {
  return gesture_drag_status_ != GESTURE_DRAG_NONE ||
      (!state_.is_screen_locked && window_overlaps_shelf_) ||
      (state_.visibility_state == AUTO_HIDE) ;
}

void ShelfLayoutManager::UpdateAutoHideStateNow() {
  SetState(state_.visibility_state);
}

ShelfLayoutManager::AutoHideState ShelfLayoutManager::CalculateAutoHideState(
    VisibilityState visibility_state) const {
  if (visibility_state != AUTO_HIDE || !launcher_widget())
    return AUTO_HIDE_HIDDEN;

  if (gesture_drag_status_ == GESTURE_DRAG_COMPLETE_IN_PROGRESS)
    return gesture_drag_auto_hide_state_;

  Shell* shell = Shell::GetInstance();
  if (shell->GetAppListTargetVisibility())
    return AUTO_HIDE_SHOWN;

  if (status_area_widget_ && status_area_widget_->ShouldShowLauncher())
    return AUTO_HIDE_SHOWN;

  if (launcher_ && launcher_->IsShowingMenu())
    return AUTO_HIDE_SHOWN;

  if (launcher_ && launcher_->IsShowingOverflowBubble())
    return AUTO_HIDE_SHOWN;

  if (launcher_widget()->IsActive() || status_area_widget_->IsActive())
    return AUTO_HIDE_SHOWN;

  // Don't show if the user is dragging the mouse.
  if (event_filter_.get() && event_filter_->in_mouse_drag())
    return AUTO_HIDE_HIDDEN;

  gfx::Rect shelf_region = launcher_widget()->GetWindowBoundsInScreen();
  if (status_area_widget_ &&
      status_area_widget_->IsMessageBubbleShown() &&
      IsVisible()) {
    // Increase the the hit test area to prevent the shelf from disappearing
    // when the mouse is over the bubble gap.
    shelf_region.Inset(alignment_ == SHELF_ALIGNMENT_RIGHT ?
                           -kNotificationBubbleGapHeight : 0,
                       alignment_ == SHELF_ALIGNMENT_BOTTOM ?
                           -kNotificationBubbleGapHeight : 0,
                       alignment_ == SHELF_ALIGNMENT_LEFT ?
                           -kNotificationBubbleGapHeight : 0,
                       0);
  }
  return shelf_region.Contains(Shell::GetScreen()->GetCursorScreenPoint()) ?
      AUTO_HIDE_SHOWN : AUTO_HIDE_HIDDEN;
}

void ShelfLayoutManager::UpdateHitTestBounds() {
  gfx::Insets insets;
  // Only modify the hit test when the shelf is visible, so we don't mess with
  // hover hit testing in the auto-hide state.
  if (state_.visibility_state == VISIBLE) {
    // Let clicks at the very top of the launcher through so windows can be
    // resized with the bottom-right corner and bottom edge.
    switch (alignment_) {
      case SHELF_ALIGNMENT_BOTTOM:
        insets.Set(kWorkspaceAreaBottomInset, 0, 0, 0);
        break;
      case SHELF_ALIGNMENT_LEFT:
        insets.Set(0, 0, 0, kWorkspaceAreaBottomInset);
        break;
      case SHELF_ALIGNMENT_RIGHT:
        insets.Set(0, kWorkspaceAreaBottomInset, 0, 0);
        break;
    }
  }
  if (launcher_widget() && launcher_widget()->GetNativeWindow()) {
    launcher_widget()->GetNativeWindow()->SetHitTestBoundsOverrideOuter(
        insets, 1);
  }
  status_area_widget_->GetNativeWindow()->
      SetHitTestBoundsOverrideOuter(insets, 1);
}

bool ShelfLayoutManager::IsShelfWindow(aura::Window* window) {
  if (!window)
    return false;
  return (launcher_widget() &&
          launcher_widget()->GetNativeWindow()->Contains(window)) ||
      (status_area_widget_->GetNativeWindow()->Contains(window));
}

int ShelfLayoutManager::GetWorkAreaSize(const State& state, int size) const {
  if (state.visibility_state == VISIBLE)
    return size;
  if (state.visibility_state == AUTO_HIDE)
    return kAutoHideSize;
  return 0;
}

}  // namespace internal
}  // namespace ash
