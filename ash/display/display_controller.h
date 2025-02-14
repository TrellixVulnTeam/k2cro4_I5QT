// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DISPLAY_DISPLAY_CONTROLLER_H_
#define ASH_DISPLAY_DISPLAY_CONTROLLER_H_

#include <map>
#include <vector>

#include "ash/ash_export.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/observer_list.h"
#include "ui/gfx/display_observer.h"
#include "ui/gfx/display.h"

namespace aura {
class Display;
class RootWindow;
}

namespace base {
class Value;
template <typename T> class JSONValueConverter;
}

namespace ash {
namespace internal {
class RootWindowController;
}

struct ASH_EXPORT DisplayLayout {
  // Layout options where the secondary display should be positioned.
  enum Position {
    TOP,
    RIGHT,
    BOTTOM,
    LEFT
  };

  DisplayLayout();
  DisplayLayout(Position position, int offset);

  // Returns an inverted display layout.
  DisplayLayout Invert() const WARN_UNUSED_RESULT;

  // Converter functions to/from base::Value.
  static bool ConvertFromValue(const base::Value& value, DisplayLayout* layout);
  static bool ConvertToValue(const DisplayLayout& layout, base::Value* value);

  // This method is used by base::JSONValueConverter, you don't need to call
  // this directly. Instead consider using converter functions above.
  static void RegisterJSONConverter(
      base::JSONValueConverter<DisplayLayout>* converter);

  Position position;

  // The offset of the position of the secondary display.  The offset is
  // based on the top/left edge of the primary display.
  int offset;

  // Returns string representation of the layout for debugging/testing.
  std::string ToString() const;
};

// DisplayController owns and maintains RootWindows for each attached
// display, keeping them in sync with display configuration changes.
class ASH_EXPORT DisplayController : public gfx::DisplayObserver {
 public:
  class ASH_EXPORT Observer {
   public:
    // Invoked when the display configuration change is requested,
    // but before the change is applied to aura/ash.
    virtual void OnDisplayConfigurationChanging() = 0;

   protected:
    virtual ~Observer() {}
  };

  DisplayController();
  virtual ~DisplayController();

  // Retruns primary display. This is safe to use after ash::Shell is
  // deleted.
  static const gfx::Display& GetPrimaryDisplay();

  // Returns the number of display. This is safe to use after
  // ash::Shell is deleted.
  static int GetNumDisplays();

  // True if the primary display has been initialized.
  static bool HasPrimaryDisplay();

  // Initializes primary display.
  void InitPrimaryDisplay();

  // Initialize secondary displays.
  void InitSecondaryDisplays();

  // Add/Remove observers.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Returns the root window for primary display.
  aura::RootWindow* GetPrimaryRootWindow();

  // Returns the root window for |display_id|.
  aura::RootWindow* GetRootWindowForDisplayId(int64 id);

  // Sets the ID of the primary display.  If the display is not connected, it
  // will switch the primary display when connected.
  void SetPrimaryDisplayId(int64 id);

  // Sets primary display. This re-assigns the current root
  // window to given |display|.
  void SetPrimaryDisplay(const gfx::Display& display);

  // Returns the secondary display.
  gfx::Display* GetSecondaryDisplay();

  // Closes all child windows in the all root windows.
  void CloseChildWindows();

  // Returns all root windows. In non extended desktop mode, this
  // returns the primary root window only.
  std::vector<aura::RootWindow*> GetAllRootWindows();

  // Returns all oot window controllers. In non extended desktop
  // mode, this return a RootWindowController for the primary root window only.
  std::vector<internal::RootWindowController*> GetAllRootWindowControllers();

  // Gets/Sets the overscan insets for the specified |display_id|. See
  // display_manager.h for the details.
  gfx::Insets GetOverscanInsets(int64 display_id) const;
  void SetOverscanInsets(int64 display_id, const gfx::Insets& insets_in_dip);

  const DisplayLayout& default_display_layout() const {
    return default_display_layout_;
  }
  void SetDefaultDisplayLayout(const DisplayLayout& layout);

  // Sets/gets the display layout for the specified display or display
  // name.  Getter returns the default value in case it doesn't have
  // its own layout yet.
  void SetLayoutForDisplayName(const std::string& name,
                               const DisplayLayout& layout);
  const DisplayLayout& GetLayoutForDisplay(const gfx::Display& display) const;

  // Returns the display layout used for current secondary display.
  const DisplayLayout& GetCurrentDisplayLayout() const;

  // aura::DisplayObserver overrides:
  virtual void OnDisplayBoundsChanged(
      const gfx::Display& display) OVERRIDE;
  virtual void OnDisplayAdded(const gfx::Display& display) OVERRIDE;
  virtual void OnDisplayRemoved(const gfx::Display& display) OVERRIDE;

 private:
  // Creates a root window for |display| and stores it in the |root_windows_|
  // map.
  aura::RootWindow* AddRootWindowForDisplay(const gfx::Display& display);

  void UpdateDisplayBoundsForLayout();

  void NotifyDisplayConfigurationChanging();

  // The mapping from display ID to its root window.
  std::map<int64, aura::RootWindow*> root_windows_;

  // The default display layout.
  DisplayLayout default_display_layout_;

  // Per-device display layout.
  std::map<std::string, DisplayLayout> secondary_layouts_;

  // The ID of the display which should be primary when connected.
  // kInvalidDisplayID if no such preference is specified.
  int64 desired_primary_display_id_;

  ObserverList<Observer> observers_;

  DISALLOW_COPY_AND_ASSIGN(DisplayController);
};

}  // namespace ash

#endif  // ASH_DISPLAY_DISPLAY_CONTROLLER_H_
