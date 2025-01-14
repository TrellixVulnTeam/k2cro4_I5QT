// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/docked_panel_strip.h"

#include <math.h>

#include <algorithm>
#include <vector>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "chrome/browser/ui/panels/panel_mouse_watcher.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"

namespace {
// Occasionally some system, like Windows, might not bring up or down the bottom
// bar when the mouse enters or leaves the bottom screen area. This is the
// maximum time we will wait for the bottom bar visibility change notification.
// After the time expires, we bring up/down the titlebars as planned.
const int kMaxDelayWaitForBottomBarVisibilityChangeMs = 1000;

// See usage below.
#if defined(TOOLKIT_GTK)
const int kDelayBeforeCollapsingFromTitleOnlyStateMs = 2000;
#else
const int kDelayBeforeCollapsingFromTitleOnlyStateMs = 0;
#endif

// After focus changed, one panel lost active status, another got it,
// we refresh layout with a delay.
const int kRefreshLayoutAfterActivePanelChangeDelayMs = 600;  // arbitrary

// As we refresh panel positions, some or all panels may move. We make sure
// we do not animate too many panels at once as this tends to perform poorly.
const int kNumPanelsToAnimateSimultaneously = 3;

}  // namespace

DockedPanelStrip::DockedPanelStrip(PanelManager* panel_manager)
    : PanelStrip(PanelStrip::DOCKED),
      panel_manager_(panel_manager),
      minimized_panel_count_(0),
      are_titlebars_up_(false),
      minimizing_all_(false),
      delayed_titlebar_action_(NO_ACTION),
      titlebar_action_factory_(this),
      refresh_action_factory_(this) {
  dragging_panel_current_iterator_ = panels_.end();
  panel_manager_->display_settings_provider()->AddDesktopBarObserver(this);
}

DockedPanelStrip::~DockedPanelStrip() {
  DCHECK(panels_.empty());
  DCHECK_EQ(0, minimized_panel_count_);
  panel_manager_->display_settings_provider()->RemoveDesktopBarObserver(this);
}

gfx::Rect DockedPanelStrip::GetDisplayArea() const  {
  return display_area_;
}

void DockedPanelStrip::SetDisplayArea(const gfx::Rect& display_area) {
  if (display_area_ == display_area)
    return;

  gfx::Rect old_area = display_area_;
  display_area_ = display_area;

  if (panels_.empty())
    return;

  for (Panels::const_iterator iter = panels_.begin();
       iter != panels_.end(); ++iter) {
    (*iter)->LimitSizeToDisplayArea(display_area_);
  }

  RefreshLayout();
}

void DockedPanelStrip::AddPanel(Panel* panel,
                                PositioningMask positioning_mask) {
  // This method does not handle minimized panels.
  DCHECK_EQ(Panel::EXPANDED, panel->expansion_state());

  DCHECK(panel->initialized());
  DCHECK_NE(this, panel->panel_strip());
  panel->set_panel_strip(this);

  bool default_position = (positioning_mask & KNOWN_POSITION) == 0;
  bool update_bounds = (positioning_mask & DO_NOT_UPDATE_BOUNDS) == 0;

  if (default_position) {
    gfx::Size full_size = panel->full_size();
    gfx::Point pt = GetDefaultPositionForPanel(full_size);
    panel->SetPanelBounds(gfx::Rect(pt, full_size));
    panels_.push_back(panel);
  } else {
    DCHECK(update_bounds);
    int x = panel->GetBounds().x();
    Panels::iterator iter = panels_.begin();
    for (; iter != panels_.end(); ++iter)
      if (x > (*iter)->GetBounds().x())
        break;
    panels_.insert(iter, panel);
  }

  if (update_bounds) {
    if ((positioning_mask & DELAY_LAYOUT_REFRESH) != 0)
      ScheduleLayoutRefresh();
    else
      RefreshLayout();
  }
}

gfx::Point DockedPanelStrip::GetDefaultPositionForPanel(
    const gfx::Size& full_size) const {
  int x = 0;
  if (!panels_.empty() &&
      panels_.back()->GetBounds().x() < display_area_.x()) {
    // Panels go off screen. Make sure the default position will place
    // the panel in view.
    Panels::const_reverse_iterator iter = panels_.rbegin();
    for (; iter != panels_.rend(); ++iter) {
      if ((*iter)->GetBounds().x() >= display_area_.x()) {
        x = (*iter)->GetBounds().x();
        break;
      }
    }
    // At least one panel should fit on the screen.
    DCHECK(x > display_area_.x());
  } else {
    x = std::max(GetRightMostAvailablePosition() - full_size.width(),
                 display_area_.x());
  }
  return gfx::Point(x, display_area_.bottom() - full_size.height());
}

int DockedPanelStrip::StartingRightPosition() const {
  return display_area_.right();
}

int DockedPanelStrip::GetRightMostAvailablePosition() const {
  return panels_.empty() ? StartingRightPosition() :
      (panels_.back()->GetBounds().x() - kPanelsHorizontalSpacing);
}

void DockedPanelStrip::RemovePanel(Panel* panel) {
  DCHECK_EQ(this, panel->panel_strip());
  panel->set_panel_strip(NULL);

  // Removing an element from the list will invalidate the iterator that refers
  // to it. We need to update the iterator in that case.
  DCHECK(dragging_panel_current_iterator_ == panels_.end() ||
         *dragging_panel_current_iterator_ != panel);

  // Optimize for the common case of removing the last panel.
  DCHECK(!panels_.empty());
  if (panels_.back() == panel) {
    panels_.pop_back();

    // Update the saved panel placement if needed. This is because
    // we might remove |saved_panel_placement_.left_panel|.
    if (saved_panel_placement_.panel &&
        saved_panel_placement_.left_panel == panel)
      saved_panel_placement_.left_panel = NULL;

  } else {
    Panels::iterator iter = find(panels_.begin(), panels_.end(), panel);
    DCHECK(iter != panels_.end());
    iter = panels_.erase(iter);

    // Update the saved panel placement if needed. This is because
    // we might remove |saved_panel_placement_.left_panel|.
    if (saved_panel_placement_.panel &&
        saved_panel_placement_.left_panel == panel)
      saved_panel_placement_.left_panel = *iter;
  }

  if (panel->expansion_state() != Panel::EXPANDED)
    UpdateMinimizedPanelCount();

  RefreshLayout();
}

void DockedPanelStrip::SavePanelPlacement(Panel* panel) {
  DCHECK(!saved_panel_placement_.panel);

  saved_panel_placement_.panel = panel;

  // To recover panel to its original placement, we only need to track the panel
  // that is placed after it.
  Panels::iterator iter = find(panels_.begin(), panels_.end(), panel);
  DCHECK(iter != panels_.end());
  ++iter;
  saved_panel_placement_.left_panel = (iter == panels_.end()) ? NULL : *iter;
}

void DockedPanelStrip::RestorePanelToSavedPlacement() {
  DCHECK(saved_panel_placement_.panel);

  Panel* panel = saved_panel_placement_.panel;

  // Find next panel after this panel.
  Panels::iterator iter = std::find(panels_.begin(), panels_.end(), panel);
  DCHECK(iter != panels_.end());
  Panels::iterator next_iter = iter;
  next_iter++;
  Panel* next_panel = (next_iter == panels_.end()) ? NULL : *iter;

  // Restoring is only needed when this panel is not in the right position.
  if (next_panel != saved_panel_placement_.left_panel) {
    // Remove this panel from its current position.
    panels_.erase(iter);

    // Insert this panel into its previous position.
    if (saved_panel_placement_.left_panel) {
      Panels::iterator iter_to_insert_before = std::find(panels_.begin(),
          panels_.end(), saved_panel_placement_.left_panel);
      DCHECK(iter_to_insert_before != panels_.end());
      panels_.insert(iter_to_insert_before, panel);
    } else {
      panels_.push_back(panel);
    }
  }

  RefreshLayout();

  DiscardSavedPanelPlacement();
}

void DockedPanelStrip::DiscardSavedPanelPlacement() {
  DCHECK(saved_panel_placement_.panel);
  saved_panel_placement_.panel = NULL;
  saved_panel_placement_.left_panel = NULL;
}

void DockedPanelStrip::StartDraggingPanelWithinStrip(Panel* panel) {
  dragging_panel_current_iterator_ =
      find(panels_.begin(), panels_.end(), panel);
  DCHECK(dragging_panel_current_iterator_ != panels_.end());
}

void DockedPanelStrip::DragPanelWithinStrip(Panel* panel,
                                            const gfx::Point& target_position) {
  // Moves this panel to the dragging position.
  // Note that we still allow the panel to be moved vertically until it gets
  // aligned to the bottom area.
  gfx::Rect new_bounds(panel->GetBounds());
  new_bounds.set_x(target_position.x());
  int delta_x = new_bounds.x() - panel->GetBounds().x();
  int bottom = GetBottomPositionForExpansionState(panel->expansion_state());
  if (new_bounds.bottom() != bottom) {
    new_bounds.set_y(target_position.y());
    if (new_bounds.bottom() > bottom)
      new_bounds.set_y(bottom - new_bounds.height());
  }
  panel->SetPanelBoundsInstantly(new_bounds);

  if (delta_x) {
    // Checks and processes other affected panels.
    if (delta_x > 0)
      DragRight(panel);
    else
      DragLeft(panel);

    // Layout refresh will automatically recompute the bounds of all affected
    // panels due to their position changes.
    RefreshLayout();
  }
}

void DockedPanelStrip::DragLeft(Panel* dragging_panel) {
  // This is the left corner of the dragging panel. We use it to check against
  // all the panels on its left.
  int dragging_panel_left_boundary = dragging_panel->GetBounds().x();

  // Checks the panels to the left of the dragging panel.
  Panels::iterator current_panel_iterator = dragging_panel_current_iterator_;
  ++current_panel_iterator;
  for (; current_panel_iterator != panels_.end(); ++current_panel_iterator) {
    Panel* current_panel = *current_panel_iterator;

    // Can we swap dragging panel with its left panel? The criterion is that
    // the left corner of dragging panel should pass the middle position of
    // its left panel.
    if (dragging_panel_left_boundary > current_panel->GetBounds().x() +
            current_panel->GetBounds().width() / 2)
      break;

    // Swaps the contents and makes |dragging_panel_current_iterator_| refers
    // to the new position.
    *dragging_panel_current_iterator_ = current_panel;
    *current_panel_iterator = dragging_panel;
    dragging_panel_current_iterator_ = current_panel_iterator;
  }
}

void DockedPanelStrip::DragRight(Panel* dragging_panel) {
  // This is the right corner of the dragging panel. We use it to check against
  // all the panels on its right.
  int dragging_panel_right_boundary = dragging_panel->GetBounds().x() +
      dragging_panel->GetBounds().width() - 1;

  // Checks the panels to the right of the dragging panel.
  Panels::iterator current_panel_iterator = dragging_panel_current_iterator_;
  while (current_panel_iterator != panels_.begin()) {
    current_panel_iterator--;
    Panel* current_panel = *current_panel_iterator;

    // Can we swap dragging panel with its right panel? The criterion is that
    // the left corner of dragging panel should pass the middle position of
    // its right panel.
    if (dragging_panel_right_boundary < current_panel->GetBounds().x() +
            current_panel->GetBounds().width() / 2)
      break;

    // Swaps the contents and makes |dragging_panel_current_iterator_| refers
    // to the new position.
    *dragging_panel_current_iterator_ = current_panel;
    *current_panel_iterator = dragging_panel;
    dragging_panel_current_iterator_ = current_panel_iterator;
  }
}

void DockedPanelStrip::EndDraggingPanelWithinStrip(Panel* panel, bool aborted) {
  dragging_panel_current_iterator_ = panels_.end();

  // If the drag is aborted, the panel will be removed from this strip
  // or returned to its original position, causing RefreshLayout()
  if (!aborted)
    RefreshLayout();
}

void DockedPanelStrip::ClearDraggingStateWhenPanelClosed() {
  dragging_panel_current_iterator_ = panels_.end();
}

panel::Resizability DockedPanelStrip::GetPanelResizability(
    const Panel* panel) const {
  return (panel->expansion_state() == Panel::EXPANDED) ?
      panel::RESIZABLE_ALL_SIDES_EXCEPT_BOTTOM : panel::NOT_RESIZABLE;
}

void DockedPanelStrip::OnPanelResizedByMouse(Panel* panel,
                                             const gfx::Rect& new_bounds) {
  DCHECK_EQ(this, panel->panel_strip());
  panel->set_full_size(new_bounds.size());
  panel->SetPanelBoundsInstantly(new_bounds);
}

void DockedPanelStrip::OnPanelExpansionStateChanged(Panel* panel) {
  gfx::Rect panel_bounds = panel->GetBounds();
  AdjustPanelBoundsPerExpansionState(panel, &panel_bounds);
  panel->SetPanelBounds(panel_bounds);

  UpdateMinimizedPanelCount();

  // Ensure minimized panel does not get the focus. If minimizing all,
  // the active panel will be deactivated once when all panels are minimized
  // rather than per minimized panel.
  if (panel->expansion_state() != Panel::EXPANDED && !minimizing_all_ &&
      panel->IsActive()) {
    panel->Deactivate();
    // The layout will refresh itself in response
    // to (de)activation notification.
  }
}

void DockedPanelStrip::AdjustPanelBoundsPerExpansionState(Panel* panel,
                                                          gfx::Rect* bounds) {
  Panel::ExpansionState expansion_state = panel->expansion_state();
  switch (expansion_state) {
    case Panel::EXPANDED:
      bounds->set_height(panel->full_size().height());

      break;
    case Panel::TITLE_ONLY:
      bounds->set_height(panel->TitleOnlyHeight());

      break;
    case Panel::MINIMIZED:
      bounds->set_height(panel::kMinimizedPanelHeight);

      break;
    default:
      NOTREACHED();
      break;
  }

  int bottom = GetBottomPositionForExpansionState(expansion_state);
  bounds->set_y(bottom - bounds->height());
}

void DockedPanelStrip::OnPanelAttentionStateChanged(Panel* panel) {
  DCHECK_EQ(this, panel->panel_strip());
  Panel::ExpansionState state = panel->expansion_state();
  if (panel->IsDrawingAttention()) {
    // Bring up the titlebar to get user's attention.
    if (state == Panel::MINIMIZED)
      panel->SetExpansionState(Panel::TITLE_ONLY);
    return;
  }

  // Panel is no longer drawing attention, but leave the panel in
  // title-only mode if all titlebars are currently up.
  if (state != Panel::TITLE_ONLY || are_titlebars_up_)
    return;

  // Leave titlebar up if panel is being dragged.
  if (dragging_panel_current_iterator_ != panels_.end() &&
      *dragging_panel_current_iterator_ == panel)
    return;

  // Leave titlebar up if mouse is in/below the panel.
  const gfx::Point mouse_position =
      panel_manager_->mouse_watcher()->GetMousePosition();
  gfx::Rect bounds = panel->GetBounds();
  if (bounds.x() <= mouse_position.x() &&
      mouse_position.x() <= bounds.right() &&
      mouse_position.y() >= bounds.y())
    return;

  // Bring down the titlebar now that panel is not drawing attention.
  panel->SetExpansionState(Panel::MINIMIZED);
}

void DockedPanelStrip::OnPanelTitlebarClicked(Panel* panel,
                                              panel::ClickModifier modifier) {
  DCHECK_EQ(this, panel->panel_strip());
  if (!IsPanelMinimized(panel))
    return;

  if (modifier == panel::APPLY_TO_ALL)
    RestoreAll();
  else
    RestorePanel(panel);
}

void DockedPanelStrip::ActivatePanel(Panel* panel) {
  DCHECK_EQ(this, panel->panel_strip());

  // Make sure the panel is expanded when activated so the user input
  // does not go into a collapsed window.
  panel->SetExpansionState(Panel::EXPANDED);

  // If the layout needs to be refreshed, it will happen in response to
  // the activation notification (and with a slight delay to let things settle).
}

void DockedPanelStrip::MinimizePanel(Panel* panel) {
  DCHECK_EQ(this, panel->panel_strip());

  if (panel->expansion_state() != Panel::EXPANDED)
    return;

  panel->SetExpansionState(panel->IsDrawingAttention() ?
      Panel::TITLE_ONLY : Panel::MINIMIZED);
}

void DockedPanelStrip::RestorePanel(Panel* panel) {
  DCHECK_EQ(this, panel->panel_strip());
  panel->SetExpansionState(Panel::EXPANDED);
}

void DockedPanelStrip::MinimizeAll() {
  // Set minimizing_all_ to prevent deactivation of each panel when it
  // is minimized. See comments in OnPanelExpansionStateChanged.
  base::AutoReset<bool> pin(&minimizing_all_, true);
  Panel* minimized_active_panel = NULL;
  for (Panels::const_iterator iter = panels_.begin();
       iter != panels_.end(); ++iter) {
    if ((*iter)->IsActive())
      minimized_active_panel = *iter;
    MinimizePanel(*iter);
  }

  // When a single panel is minimized, it is deactivated to ensure that
  // a minimized panel does not have focus. However, when minimizing all,
  // the deactivation is only done once after all panels are minimized,
  // rather than per minimized panel, both for efficiency and to avoid
  // temporary activations of random not-yet-minimized panels.
  if (minimized_active_panel) {
    minimized_active_panel->Deactivate();
    // Layout will be refreshed in response to (de)activation notification.
  }
}

void DockedPanelStrip::RestoreAll() {
  for (Panels::const_iterator iter = panels_.begin();
       iter != panels_.end(); ++iter) {
    RestorePanel(*iter);
  }
}

bool DockedPanelStrip::CanMinimizePanel(const Panel* panel) const {
  DCHECK_EQ(this, panel->panel_strip());
  // Docked panels can be minimized.
  return true;
}

bool DockedPanelStrip::IsPanelMinimized(const Panel* panel) const {
  return panel->expansion_state() != Panel::EXPANDED;
}

void DockedPanelStrip::UpdateMinimizedPanelCount() {
  int prev_minimized_panel_count = minimized_panel_count_;
  minimized_panel_count_ = 0;
  for (Panels::const_iterator panel_iter = panels_.begin();
        panel_iter != panels_.end(); ++panel_iter) {
    if ((*panel_iter)->expansion_state() != Panel::EXPANDED)
      minimized_panel_count_++;
  }

  if (prev_minimized_panel_count == 0 && minimized_panel_count_ > 0)
    panel_manager_->mouse_watcher()->AddObserver(this);
  else if (prev_minimized_panel_count > 0 &&  minimized_panel_count_ == 0)
    panel_manager_->mouse_watcher()->RemoveObserver(this);

  DCHECK_LE(minimized_panel_count_, num_panels());
}

void DockedPanelStrip::ResizePanelWindow(
    Panel* panel,
    const gfx::Size& preferred_window_size) {
  DCHECK_EQ(this, panel->panel_strip());
  // Make sure the new size does not violate panel's size restrictions.
  gfx::Size new_size(preferred_window_size.width(),
                     preferred_window_size.height());
  new_size = panel->ClampSize(new_size);

  if (new_size == panel->full_size())
    return;

  panel->set_full_size(new_size);

  RefreshLayout();
}

bool DockedPanelStrip::ShouldBringUpTitlebars(int mouse_x, int mouse_y) const {
  // We should always bring up the titlebar if the mouse is over the
  // visible auto-hiding bottom bar.
  DisplaySettingsProvider* provider =
      panel_manager_->display_settings_provider();
  if (provider->IsAutoHidingDesktopBarEnabled(
          DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_BOTTOM) &&
      provider->GetDesktopBarVisibility(
          DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_BOTTOM) ==
              DisplaySettingsProvider::DESKTOP_BAR_VISIBLE &&
      mouse_y >= display_area_.bottom())
    return true;

  // Bring up titlebars if any panel needs the titlebar up.
  Panel* dragging_panel = dragging_panel_current_iterator_ == panels_.end() ?
      NULL : *dragging_panel_current_iterator_;
  for (Panels::const_iterator iter = panels_.begin();
       iter != panels_.end(); ++iter) {
    Panel* panel = *iter;
    Panel::ExpansionState state = panel->expansion_state();
    // Skip the expanded panel.
    if (state == Panel::EXPANDED)
      continue;

    // If the panel is showing titlebar only, we want to keep it up when it is
    // being dragged.
    if (state == Panel::TITLE_ONLY && panel == dragging_panel)
      return true;

    // We do not want to bring up other minimized panels if the mouse is over
    // the panel that pops up the titlebar to attract attention.
    if (panel->IsDrawingAttention())
      continue;

    gfx::Rect bounds = panel->GetBounds();
    if (bounds.x() <= mouse_x && mouse_x <= bounds.right() &&
        mouse_y >= bounds.y())
      return true;
  }
  return false;
}

void DockedPanelStrip::BringUpOrDownTitlebars(bool bring_up) {
  if (are_titlebars_up_ == bring_up)
    return;

  are_titlebars_up_ = bring_up;
  int task_delay_ms = 0;

  // If the auto-hiding bottom bar exists, delay the action until the bottom
  // bar is fully visible or hidden. We do not want both bottom bar and panel
  // titlebar to move at the same time but with different speeds.
  DisplaySettingsProvider* provider =
      panel_manager_->display_settings_provider();
  if (provider->IsAutoHidingDesktopBarEnabled(
          DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_BOTTOM)) {
    DisplaySettingsProvider::DesktopBarVisibility visibility =
        provider->GetDesktopBarVisibility(
            DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_BOTTOM);
    if (visibility !=
        (bring_up ? DisplaySettingsProvider::DESKTOP_BAR_VISIBLE
                  : DisplaySettingsProvider::DESKTOP_BAR_HIDDEN)) {
      // Occasionally some system, like Windows, might not bring up or down the
      // bottom bar when the mouse enters or leaves the bottom screen area.
      // Thus, we schedule a delayed task to do the work if we do not receive
      // the bottom bar visibility change notification within a certain period
      // of time.
      task_delay_ms = kMaxDelayWaitForBottomBarVisibilityChangeMs;
    }
  }

  // On some OSes, the interaction with native Taskbars/Docks may be improved
  // if the panels do not go back to minimized state too fast. For example,
  // with a taskbar in auto-hide mode, the taskbar will cover the panel in
  // title-only mode which appears on hover. Leaving it up for a little longer
  // would allow the user to be able to click on it.
  //
  // Currently, no platforms use both delays.
  DCHECK(task_delay_ms == 0 ||
         kDelayBeforeCollapsingFromTitleOnlyStateMs == 0);
  if (!bring_up && task_delay_ms == 0) {
    task_delay_ms = kDelayBeforeCollapsingFromTitleOnlyStateMs;
  }

  // OnAutoHidingDesktopBarVisibilityChanged will handle this.
  delayed_titlebar_action_ = bring_up ? BRING_UP : BRING_DOWN;

  // If user moves the mouse in and out of mouse tracking area, we might have
  // previously posted but not yet dispatched task in the queue. New action
  // should always 'reset' the delays so cancel any tasks that haven't run yet
  // and post a new one.
  titlebar_action_factory_.InvalidateWeakPtrs();
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DockedPanelStrip::DelayedBringUpOrDownTitlebarsCheck,
                 titlebar_action_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(
          PanelManager::AdjustTimeInterval(task_delay_ms)));
}

void DockedPanelStrip::DelayedBringUpOrDownTitlebarsCheck() {
  // Task was already processed or cancelled - bail out.
  if (delayed_titlebar_action_ == NO_ACTION)
    return;

  bool need_to_bring_up_titlebars = (delayed_titlebar_action_ == BRING_UP);

  delayed_titlebar_action_ = NO_ACTION;

  // Check if the action is still needed based on the latest mouse position. The
  // user could move the mouse into the tracking area and then quickly move it
  // out of the area. In case of this, cancel the action.
  if (are_titlebars_up_ != need_to_bring_up_titlebars)
    return;

  DoBringUpOrDownTitlebars(need_to_bring_up_titlebars);
}

void DockedPanelStrip::DoBringUpOrDownTitlebars(bool bring_up) {
  for (Panels::const_iterator iter = panels_.begin();
       iter != panels_.end(); ++iter) {
    Panel* panel = *iter;

    // Skip any panel that is drawing the attention.
    if (panel->IsDrawingAttention())
      continue;

    if (bring_up) {
      if (panel->expansion_state() == Panel::MINIMIZED)
        panel->SetExpansionState(Panel::TITLE_ONLY);
    } else {
      if (panel->expansion_state() == Panel::TITLE_ONLY)
        panel->SetExpansionState(Panel::MINIMIZED);
    }
  }
}

int DockedPanelStrip::GetBottomPositionForExpansionState(
    Panel::ExpansionState expansion_state) const {
  int bottom = display_area_.bottom();
  // If there is an auto-hiding desktop bar aligned to the bottom edge, we need
  // to move the title-only panel above the auto-hiding desktop bar.
  DisplaySettingsProvider* provider =
      panel_manager_->display_settings_provider();
  if (expansion_state == Panel::TITLE_ONLY &&
      provider->IsAutoHidingDesktopBarEnabled(
          DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_BOTTOM)) {
    bottom -= provider->GetDesktopBarThickness(
        DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_BOTTOM);
  }

  return bottom;
}

void DockedPanelStrip::OnMouseMove(const gfx::Point& mouse_position) {
  bool bring_up_titlebars = ShouldBringUpTitlebars(mouse_position.x(),
                                                   mouse_position.y());
  BringUpOrDownTitlebars(bring_up_titlebars);
}

void DockedPanelStrip::OnAutoHidingDesktopBarVisibilityChanged(
    DisplaySettingsProvider::DesktopBarAlignment alignment,
    DisplaySettingsProvider::DesktopBarVisibility visibility) {
  if (delayed_titlebar_action_ == NO_ACTION)
    return;

  DisplaySettingsProvider::DesktopBarVisibility expected_visibility =
      delayed_titlebar_action_ == BRING_UP
          ? DisplaySettingsProvider::DESKTOP_BAR_VISIBLE
          : DisplaySettingsProvider::DESKTOP_BAR_HIDDEN;
  if (visibility != expected_visibility)
    return;

  DoBringUpOrDownTitlebars(delayed_titlebar_action_ == BRING_UP);
  delayed_titlebar_action_ = NO_ACTION;
}

void DockedPanelStrip::OnFullScreenModeChanged(bool is_full_screen) {
  for (Panels::const_iterator iter = panels_.begin();
       iter != panels_.end(); ++iter) {
    (*iter)->FullScreenModeChanged(is_full_screen);
  }
}

void DockedPanelStrip::RefreshLayout() {
  int total_active_width = 0;
  int total_inactive_width = 0;

  for (Panels::const_iterator panel_iter = panels_.begin();
       panel_iter != panels_.end(); ++panel_iter) {
    Panel* panel = *panel_iter;
    if (panel->IsActive())
      total_active_width += panel->full_size().width();
    else
      total_inactive_width += panel->full_size().width();
  }

  double display_width_for_inactive_panels =
      display_area_.width() - total_active_width -
          kPanelsHorizontalSpacing * panels_.size();
  double overflow_squeeze_factor = (total_inactive_width > 0) ?
      std::min(display_width_for_inactive_panels / total_inactive_width, 1.0) :
      1.0;

  // We want to calculate all bounds first, then apply them in a specific order.
  typedef std::pair<Panel*, gfx::Rect> PanelBoundsInfo;
  // The next pair of variables will hold panels that move, respectively,
  // to the right and to the left. We want to process them from the center
  // outwards, so one is a stack and another is a queue.
  std::vector<PanelBoundsInfo> moving_right;
  std::queue<PanelBoundsInfo> moving_left;

  int rightmost_position = StartingRightPosition();
  for (Panels::const_iterator panel_iter = panels_.begin();
       panel_iter != panels_.end(); ++panel_iter) {
    Panel* panel = *panel_iter;
    gfx::Rect old_bounds = panel->GetBounds();
    gfx::Rect new_bounds = old_bounds;
    AdjustPanelBoundsPerExpansionState(panel, &new_bounds);

    new_bounds.set_width(
      WidthToDisplayPanelInStrip(panel->IsActive(),
                                 overflow_squeeze_factor,
                                 panel->full_size().width()));
    int x = rightmost_position - new_bounds.width();
    new_bounds.set_x(x);

    if (x < old_bounds.x() ||
        (x == old_bounds.x() && new_bounds.width() <= old_bounds.width()))
      moving_left.push(std::make_pair(panel, new_bounds));
    else
      moving_right.push_back(std::make_pair(panel, new_bounds));

    rightmost_position = x - kPanelsHorizontalSpacing;
  }

  // Update panels going in both directions.
  // This is important on Mac where bounds changes are slow and you see a
  // "wave" instead of a smooth sliding effect.
  int num_animated = 0;
  bool going_right = true;
  while (!moving_right.empty() || !moving_left.empty()) {
    PanelBoundsInfo bounds_info;
    // Alternate between processing the panels that moving left and right,
    // starting from the center.
    going_right = !going_right;
    bool take_panel_on_right =
        (going_right && !moving_right.empty()) ||
        moving_left.empty();
    if (take_panel_on_right) {
      bounds_info = moving_right.back();
      moving_right.pop_back();
    } else {
      bounds_info = moving_left.front();
      moving_left.pop();
    }

    // Don't update the docked panel that is in preview mode.
    Panel* panel = bounds_info.first;
    gfx::Rect bounds = bounds_info.second;
    if (!panel->in_preview_mode() && bounds != panel->GetBounds()) {
      // We animate a limited number of panels, starting with the
      // "most important" ones, that is, ones that are close to the center
      // of the action. Other panels are moved instantly to improve performance.
      if (num_animated < kNumPanelsToAnimateSimultaneously) {
        panel->SetPanelBounds(bounds);  // Animates.
        ++num_animated;
      } else {
        panel->SetPanelBoundsInstantly(bounds);
      }
    }
  }

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_STRIP_UPDATED,
      content::Source<PanelStrip>(this),
      content::NotificationService::NoDetails());
}

int DockedPanelStrip::WidthToDisplayPanelInStrip(bool is_for_active_panel,
                                                 double squeeze_factor,
                                                 int full_width) const {
  return is_for_active_panel ? full_width :
      std::max(panel::kPanelMinWidth,
               static_cast<int>(floor(full_width * squeeze_factor)));
}

void DockedPanelStrip::CloseAll() {
  // This should only be called at the end of tests to clean up.

  // Make a copy of the iterator as closing panels can modify the vector.
  Panels panels_copy = panels_;

  // Start from the bottom to avoid reshuffling.
  for (Panels::reverse_iterator iter = panels_copy.rbegin();
       iter != panels_copy.rend(); ++iter)
    (*iter)->Close();
}

void DockedPanelStrip::UpdatePanelOnStripChange(Panel* panel) {
  panel->set_attention_mode(Panel::USE_PANEL_ATTENTION);
  panel->SetAlwaysOnTop(true);
  panel->EnableResizeByMouse(true);
  panel->UpdateMinimizeRestoreButtonVisibility();
}

void DockedPanelStrip::ScheduleLayoutRefresh() {
  refresh_action_factory_.InvalidateWeakPtrs();
  MessageLoop::current()->PostDelayedTask(FROM_HERE,
      base::Bind(&DockedPanelStrip::RefreshLayout,
                 refresh_action_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(PanelManager::AdjustTimeInterval(
          kRefreshLayoutAfterActivePanelChangeDelayMs)));
}

void DockedPanelStrip::OnPanelActiveStateChanged(Panel* panel) {
  // Refresh layout, but wait till active states settle.
  // This lets us avoid refreshing too many times when one panel loses
  // focus and another gains it.
  ScheduleLayoutRefresh();
}

bool DockedPanelStrip::HasPanel(Panel* panel) const {
  return find(panels_.begin(), panels_.end(), panel) != panels_.end();
}
