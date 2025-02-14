// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/display/display_manager.h"

#include <string>
#include <vector>

#include "ash/display/display_controller.h"
#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "base/command_line.h"
#include "base/stl_util.h"
#include "base/string_split.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "grit/ash_strings.h"
#include "ui/aura/aura_switches.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/display_util.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#include "ui/aura/root_window_host.h"
#include "ui/aura/window_property.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/size_conversions.h"

#if defined(USE_X11)
#include "ui/base/x/x11_util.h"
#endif

#if defined(OS_CHROMEOS)
#include "base/chromeos/chromeos_version.h"
#include "chromeos/display/output_configurator.h"
#endif

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#include "ui/aura/remote_root_window_host_win.h"
#endif

DECLARE_WINDOW_PROPERTY_TYPE(int64);
typedef std::vector<gfx::Display> DisplayList;

namespace ash {
namespace internal {
namespace {

struct DisplaySortFunctor {
  bool operator()(const gfx::Display& a, const gfx::Display& b) {
    return a.id() < b.id();
  }
};

gfx::Display& GetInvalidDisplay() {
  static gfx::Display* invalid_display = new gfx::Display();
  return *invalid_display;
}

#if defined(OS_CHROMEOS)
int64 GetDisplayIdForOutput(XID output) {
  uint16 manufacturer_id = 0;
  uint32 serial_number = 0;
  ui::GetOutputDeviceData(
      output, &manufacturer_id, &serial_number, NULL);
  return gfx::Display::GetID(manufacturer_id, serial_number);
}
#endif

}  // namespace

using aura::RootWindow;
using aura::Window;
using std::string;
using std::vector;

DEFINE_WINDOW_PROPERTY_KEY(int64, kDisplayIdKey,
                           gfx::Display::kInvalidDisplayID);

DisplayManager::DisplayManager() :
    internal_display_id_(gfx::Display::kInvalidDisplayID),
    force_bounds_changed_(false) {
  Init();
}

DisplayManager::~DisplayManager() {
}

// static
void DisplayManager::CycleDisplay() {
  Shell::GetInstance()->display_manager()->CycleDisplayImpl();
}

// static
void DisplayManager::ToggleDisplayScale() {
  Shell::GetInstance()->display_manager()->ScaleDisplayImpl();
}

bool DisplayManager::IsActiveDisplay(const gfx::Display& display) const {
  for (DisplayList::const_iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    if ((*iter).id() == display.id())
      return true;
  }
  return false;
}

bool DisplayManager::HasInternalDisplay() const {
  return internal_display_id_ != gfx::Display::kInvalidDisplayID;
}

bool DisplayManager::IsInternalDisplayId(int64 id) const {
  return internal_display_id_ == id;
}

bool DisplayManager::UpdateWorkAreaOfDisplayNearestWindow(
    const aura::Window* window,
    const gfx::Insets& insets) {
  const RootWindow* root = window->GetRootWindow();
  gfx::Display& display = FindDisplayForRootWindow(root);
  gfx::Rect old_work_area = display.work_area();
  display.UpdateWorkAreaFromInsets(insets);
  return old_work_area != display.work_area();
}

const gfx::Display& DisplayManager::GetDisplayForId(int64 id) const {
  return const_cast<DisplayManager*>(this)->FindDisplayForId(id);
}

const gfx::Display& DisplayManager::FindDisplayContainingPoint(
    const gfx::Point& point_in_screen) const {
  for (DisplayList::const_iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    const gfx::Display& display = *iter;
    if (display.bounds().Contains(point_in_screen))
      return display;
  }
  return GetInvalidDisplay();
}

void DisplayManager::SetOverscanInsets(int64 display_id,
                                       const gfx::Insets& insets_in_dip) {
  DisplayList displays = displays_;
  std::map<int64, gfx::Insets>::const_iterator old_overscan =
      overscan_mapping_.find(display_id);
  if (old_overscan != overscan_mapping_.end()) {
    gfx::Insets old_insets = old_overscan->second;
    for (DisplayList::iterator iter = displays.begin();
         iter != displays.end(); ++iter) {
      if (iter->id() == display_id) {
        // Undo the existing insets before applying the new insets.
        gfx::Rect bounds = iter->bounds_in_pixel();
        bounds.Inset(old_insets.Scale(-iter->device_scale_factor()));
        iter->SetScaleAndBounds(iter->device_scale_factor(), bounds);
        break;
      }
    }
  }
  overscan_mapping_[display_id] = insets_in_dip;
  OnNativeDisplaysChanged(displays);
}

gfx::Insets DisplayManager::GetOverscanInsets(int64 display_id) const {
  std::map<int64, gfx::Insets>::const_iterator it =
      overscan_mapping_.find(display_id);
  return (it != overscan_mapping_.end()) ? it->second : gfx::Insets();
}

void DisplayManager::OnNativeDisplaysChanged(
    const std::vector<gfx::Display>& updated_displays) {
  if (updated_displays.empty()) {
    // Don't update the displays when all displays are disconnected.
    // This happens when:
    // - the device is idle and powerd requested to turn off all displays.
    // - the device is suspended. (kernel turns off all displays)
    // - the internal display's brightness is set to 0 and no external
    //   display is connected.
    // - the internal display's brightness is 0 and external display is
    //   disconnected.
    // The display will be updated when one of displays is turned on, and the
    // display list will be updated correctly.
    return;
  }
  DisplayList new_displays = updated_displays;
  if (internal_display_id_ != gfx::Display::kInvalidDisplayID) {
    bool internal_display_connected = false;
    for (DisplayList::const_iterator iter = updated_displays.begin();
         iter != updated_displays.end(); ++iter) {
      if ((*iter).id() == internal_display_id_) {
        internal_display_connected = true;
        // Update the internal display cache.
        internal_display_.reset(new gfx::Display);
        *internal_display_.get() = *iter;
        break;
      }
    }
    // If the internal display wasn't connected, use the cached value.
    if (!internal_display_connected) {
      // Internal display may be reported as disconnect during startup time.
      if (!internal_display_.get()) {
        internal_display_.reset(new gfx::Display(internal_display_id_,
                                                 gfx::Rect(800, 600)));
      }
      new_displays.push_back(*internal_display_.get());
    }
  } else {
    new_displays = updated_displays;
  }

  for (DisplayList::iterator iter = new_displays.begin();
       iter != new_displays.end(); ++iter) {
    std::map<int64, gfx::Insets>::const_iterator overscan_insets =
        overscan_mapping_.find(iter->id());
    if (overscan_insets != overscan_mapping_.end()) {
      gfx::Rect bounds = iter->bounds_in_pixel();
      bounds.Inset(overscan_insets->second.Scale(iter->device_scale_factor()));
      iter->SetScaleAndBounds(iter->device_scale_factor(), bounds);
    }
  }

  std::sort(displays_.begin(), displays_.end(), DisplaySortFunctor());
  std::sort(new_displays.begin(), new_displays.end(), DisplaySortFunctor());
  DisplayList removed_displays;
  std::vector<size_t> changed_display_indices;
  std::vector<size_t> added_display_indices;
  gfx::Display current_primary;
  if (DisplayController::HasPrimaryDisplay())
    current_primary = DisplayController::GetPrimaryDisplay();

  for (DisplayList::iterator curr_iter = displays_.begin(),
       new_iter = new_displays.begin();
       curr_iter != displays_.end() || new_iter != new_displays.end();) {
    if (curr_iter == displays_.end()) {
      // more displays in new list.
      added_display_indices.push_back(new_iter - new_displays.begin());
      ++new_iter;
    } else if (new_iter == new_displays.end()) {
      // more displays in current list.
      removed_displays.push_back(*curr_iter);
      ++curr_iter;
    } else if ((*curr_iter).id() == (*new_iter).id()) {
      const gfx::Display& current_display = *curr_iter;
      gfx::Display& new_display = *new_iter;
      if (force_bounds_changed_ ||
          current_display.bounds_in_pixel() != new_display.bounds_in_pixel() ||
          current_display.device_scale_factor() !=
          new_display.device_scale_factor()) {
        changed_display_indices.push_back(new_iter - new_displays.begin());
      }
      // If the display is primary, then simpy set the origin to (0,0).
      // The secondary display's bounds will be updated by
      // |DisplayController::UpdateDisplayBoundsForLayout|, so no need
      // to change there.
      if ((*new_iter).id() == current_primary.id())
        new_display.set_bounds(gfx::Rect(new_display.bounds().size()));

      new_display.UpdateWorkAreaFromInsets(current_display.GetWorkAreaInsets());
      ++curr_iter;
      ++new_iter;
    } else if ((*curr_iter).id() < (*new_iter).id()) {
      // more displays in current list between ids, which means it is deleted.
      removed_displays.push_back(*curr_iter);
      ++curr_iter;
    } else {
      // more displays in new list between ids, which means it is added.
      added_display_indices.push_back(new_iter - new_displays.begin());
      ++new_iter;
    }
  }

  // Do not update |displays_| if there's nothing to be updated. Without this,
  // it will not update the display layout, which causes the bug
  // http://crbug.com/155948.
  if (changed_display_indices.empty() && added_display_indices.empty() &&
      removed_displays.empty()) {
    return;
  }

  displays_ = new_displays;
  RefreshDisplayNames();

  // Temporarily add displays to be removed because display object
  // being removed are accessed during shutting down the root.
  displays_.insert(displays_.end(), removed_displays.begin(),
                   removed_displays.end());
  for (std::vector<size_t>::iterator iter = changed_display_indices.begin();
       iter != changed_display_indices.end(); ++iter) {
    Shell::GetInstance()->screen()->NotifyBoundsChanged(displays_[*iter]);
  }
  for (std::vector<size_t>::iterator iter = added_display_indices.begin();
       iter != added_display_indices.end(); ++iter) {
    Shell::GetInstance()->screen()->NotifyDisplayAdded(displays_[*iter]);
  }
  for (DisplayList::const_reverse_iterator iter = removed_displays.rbegin();
       iter != removed_displays.rend(); ++iter) {
    Shell::GetInstance()->screen()->NotifyDisplayRemoved(displays_.back());
    displays_.pop_back();
  }
  EnsurePointerInDisplays();

#if defined(USE_X11) && defined(OS_CHROMEOS)
  if (!changed_display_indices.empty() && base::chromeos::IsRunningOnChromeOS())
    ui::ClearX11DefaultRootWindow();
#endif
}

RootWindow* DisplayManager::CreateRootWindowForDisplay(
    const gfx::Display& display) {
  RootWindow::CreateParams params(display.bounds_in_pixel());
#if defined(OS_WIN)
  if (base::win::GetVersion() >= base::win::VERSION_WIN8) {
    params.host = aura::RemoteRootWindowHostWin::Create(
        display.bounds_in_pixel());
  }
#endif
  aura::RootWindow* root_window = new aura::RootWindow(params);
  // No need to remove RootWindowObserver because
  // the DisplayManager object outlives RootWindow objects.
  root_window->AddRootWindowObserver(this);
  root_window->SetProperty(kDisplayIdKey, display.id());
  root_window->Init();
  return root_window;
}

gfx::Display* DisplayManager::GetDisplayAt(size_t index) {
  return index < displays_.size() ? &displays_[index] : NULL;
}

size_t DisplayManager::GetNumDisplays() const {
  return displays_.size();
}

const gfx::Display& DisplayManager::GetDisplayNearestWindow(
    const Window* window) const {
  if (!window)
    return DisplayController::GetPrimaryDisplay();
  const RootWindow* root = window->GetRootWindow();
  DisplayManager* manager = const_cast<DisplayManager*>(this);
  return root ?
      manager->FindDisplayForRootWindow(root) :
      DisplayController::GetPrimaryDisplay();
}

const gfx::Display& DisplayManager::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  // Fallback to the primary display if there is no root display containing
  // the |point|.
  const gfx::Display& display = FindDisplayContainingPoint(point);
  return display.is_valid() ? display : DisplayController::GetPrimaryDisplay();
}

const gfx::Display& DisplayManager::GetDisplayMatching(
    const gfx::Rect& rect) const {
  if (rect.IsEmpty())
    return GetDisplayNearestPoint(rect.origin());

  int max = 0;
  const gfx::Display* matching = 0;
  for (std::vector<gfx::Display>::const_iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    const gfx::Display& display = *iter;
    gfx::Rect intersect = gfx::IntersectRects(display.bounds(), rect);
    int area = intersect.width() * intersect.height();
    if (area > max) {
      max = area;
      matching = &(*iter);
    }
  }
  // Fallback to the primary display if there is no matching display.
  return matching ? *matching : DisplayController::GetPrimaryDisplay();
}

std::string DisplayManager::GetDisplayNameFor(
    const gfx::Display& display) {
  if (!display.is_valid())
    return l10n_util::GetStringUTF8(IDS_ASH_STATUS_TRAY_UNKNOWN_DISPLAY_NAME);

  std::map<int64, std::string>::const_iterator iter =
      display_names_.find(display.id());
  if (iter != display_names_.end())
    return iter->second;

  return base::StringPrintf("Display %d", static_cast<int>(display.id()));
}

void DisplayManager::OnRootWindowResized(const aura::RootWindow* root,
                                         const gfx::Size& old_size) {
  if (!aura::UseFullscreenHostWindow()) {
    gfx::Display& display = FindDisplayForRootWindow(root);
    if (display.size() != root->GetHostSize()) {
      display.SetSize(root->GetHostSize());
      Shell::GetInstance()->screen()->NotifyBoundsChanged(display);
    }
  }
}

void DisplayManager::Init() {
#if defined(OS_CHROMEOS)
  if (base::chromeos::IsRunningOnChromeOS()) {
    std::vector<XID> outputs;
    ui::GetOutputDeviceHandles(&outputs);
    std::vector<std::string> output_names = ui::GetOutputNames(outputs);
    for (size_t i = 0; i < output_names.size(); ++i) {
      if (chromeos::OutputConfigurator::IsInternalOutputName(
              output_names[i])) {
        internal_display_id_ = GetDisplayIdForOutput(outputs[i]);
        break;
      }
    }
  }
#endif

  RefreshDisplayNames();

#if defined(OS_WIN)
  if (base::win::GetVersion() >= base::win::VERSION_WIN8)
    aura::SetUseFullscreenHostWindow(true);
#endif
  // TODO(oshima): Move this logic to DisplayChangeObserver.
  const string size_str = CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      switches::kAuraHostWindowSize);
  vector<string> parts;
  base::SplitString(size_str, ',', &parts);
  for (vector<string>::const_iterator iter = parts.begin();
       iter != parts.end(); ++iter) {
    AddDisplayFromSpec(*iter);
  }
  if (displays_.empty())
    AddDisplayFromSpec(std::string() /* default */);
}

void DisplayManager::CycleDisplayImpl() {
  DCHECK(!displays_.empty());
  std::vector<gfx::Display> new_displays;
  new_displays.push_back(DisplayController::GetPrimaryDisplay());
  // Add if there is only one display.
  if (displays_.size() == 1)
    new_displays.push_back(aura::CreateDisplayFromSpec("100+200-500x400"));
  OnNativeDisplaysChanged(new_displays);
}

void DisplayManager::ScaleDisplayImpl() {
  DCHECK(!displays_.empty());
  std::vector<gfx::Display> new_displays;
  for (DisplayList::const_iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    gfx::Display display = *iter;
    float factor = display.device_scale_factor() == 1.0f ? 2.0f : 1.0f;
    gfx::Point display_origin = display.bounds_in_pixel().origin();
    gfx::Size display_size = gfx::ToFlooredSize(
        gfx::ScaleSize(display.size(), factor));
    display.SetScaleAndBounds(factor, gfx::Rect(display_origin, display_size));
    new_displays.push_back(display);
  }
  OnNativeDisplaysChanged(new_displays);
}

gfx::Display& DisplayManager::FindDisplayForRootWindow(
    const aura::RootWindow* root_window) {
  int64 id = root_window->GetProperty(kDisplayIdKey);
  // if id is |kInvaildDisplayID|, it's being deleted.
  DCHECK(id != gfx::Display::kInvalidDisplayID);
  gfx::Display& display = FindDisplayForId(id);
  DCHECK(display.is_valid());
  return display;
}

gfx::Display& DisplayManager::FindDisplayForId(int64 id) {
  for (DisplayList::iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    if ((*iter).id() == id)
      return *iter;
  }
  DLOG(WARNING) << "Could not find display:" << id;
  return GetInvalidDisplay();
}

void DisplayManager::AddDisplayFromSpec(const std::string& spec) {
  gfx::Display display = aura::CreateDisplayFromSpec(spec);

  const gfx::Insets insets = display.GetWorkAreaInsets();
  const gfx::Rect& native_bounds = display.bounds_in_pixel();
  display.SetScaleAndBounds(display.device_scale_factor(), native_bounds);
  display.UpdateWorkAreaFromInsets(insets);
  displays_.push_back(display);
}

int64 DisplayManager::SetFirstDisplayAsInternalDisplayForTest() {
  internal_display_id_ = displays_[0].id();
  internal_display_.reset(new gfx::Display);
  *internal_display_ = displays_[0];
  return internal_display_id_;
}

void DisplayManager::EnsurePointerInDisplays() {
  // Don't try to move the pointer during the boot/startup.
  if (!DisplayController::HasPrimaryDisplay())
    return;
  gfx::Point location_in_screen = Shell::GetScreen()->GetCursorScreenPoint();
  gfx::Point target_location;
  int64 closest_distance_squared = -1;

  for (DisplayList::const_iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    const gfx::Rect& display_bounds = iter->bounds();

    if (display_bounds.Contains(location_in_screen)) {
      target_location = location_in_screen;
      break;
    }
    gfx::Point center = display_bounds.CenterPoint();
    // Use the distance squared from the center of the dislay. This is not
    // exactly "closest" display, but good enough to pick one
    // appropriate (and there are at most two displays).
    // We don't care about actual distance, only relative to other displays, so
    // using the LengthSquared() is cheaper than Length().
    int64 distance_squared = (center - location_in_screen).LengthSquared();
    if (closest_distance_squared < 0 ||
        closest_distance_squared > distance_squared) {
      target_location = center;
      closest_distance_squared = distance_squared;
    }
  }

  aura::RootWindow* root_window = Shell::GetPrimaryRootWindow();
  aura::client::ScreenPositionClient* client =
      aura::client::GetScreenPositionClient(root_window);
  client->ConvertPointFromScreen(root_window, &target_location);

  root_window->MoveCursorTo(target_location);
}

void DisplayManager::RefreshDisplayNames() {
  display_names_.clear();

#if defined(OS_CHROMEOS)
  if (!base::chromeos::IsRunningOnChromeOS())
    return;
#endif

#if defined(USE_X11)
  std::vector<XID> outputs;
  if (!ui::GetOutputDeviceHandles(&outputs))
    return;

  for (size_t i = 0; i < outputs.size(); ++i) {
    uint16 manufacturer_id = 0;
    uint32 serial_number = 0;
    std::string name;
    if (ui::GetOutputDeviceData(
            outputs[i], &manufacturer_id, &serial_number, &name)) {
      int64 id = gfx::Display::GetID(manufacturer_id, serial_number);
      if (IsInternalDisplayId(id)) {
        display_names_[id] =
            l10n_util::GetStringUTF8(IDS_ASH_INTERNAL_DISPLAY_NAME);
      } else {
        display_names_[id] = name;
      }
    }
  }
#endif
}

void DisplayManager::SetDisplayIdsForTest(DisplayList* to_update) const {
  DisplayList::iterator iter_to_update = to_update->begin();
  DisplayList::const_iterator iter = displays_.begin();
  for (; iter != displays_.end() && iter_to_update != to_update->end();
       ++iter, ++iter_to_update) {
    (*iter_to_update).set_id((*iter).id());
  }
}

}  // namespace internal
}  // namespace ash
