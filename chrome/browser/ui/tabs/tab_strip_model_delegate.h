// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TABS_TAB_STRIP_MODEL_DELEGATE_H_
#define CHROME_BROWSER_UI_TABS_TAB_STRIP_MODEL_DELEGATE_H_

#include <vector>

#include "content/public/common/page_transition_types.h"

class Browser;
class DockInfo;

namespace content {
class WebContents;
}

namespace gfx {
class Rect;
}

///////////////////////////////////////////////////////////////////////////////
//
// TabStripModelDelegate
//
//  A delegate interface that the TabStripModel uses to perform work that it
//  can't do itself, such as obtain a container HWND for creating new
//  WebContentses, creating new TabStripModels for detached tabs, etc.
//
//  This interface is typically implemented by the controller that instantiates
//  the TabStripModel (in our case the Browser object).
//
///////////////////////////////////////////////////////////////////////////////
class TabStripModelDelegate {
 public:
  enum {
    TAB_MOVE_ACTION = 1,
    TAB_TEAROFF_ACTION = 2
  };

  virtual ~TabStripModelDelegate() {}

  // Adds what the delegate considers to be a blank tab to the model. An |index|
  // value of -1 means to append the contents to the end of the tab strip.
  virtual void AddBlankTabAt(int index, bool foreground) = 0;

  // Asks for a new TabStripModel to be created and the given web contentses to
  // be added to it. Its size and position are reflected in |window_bounds|.
  // If |dock_info|'s type is other than NONE, the newly created window should
  // be docked as identified by |dock_info|. Returns the Browser object
  // representing the newly created window and tab strip. This does not
  // show the window; it's up to the caller to do so.
  //
  // TODO(avi): This is a layering violation; the TabStripModel should not know
  // about the Browser type. At least fix so that this returns a
  // TabStripModelDelegate, or perhaps even move this code elsewhere.
  struct NewStripContents {
    // The WebContents to add.
    content::WebContents* web_contents;
    // A bitmask of TabStripModel::AddTabTypes to apply to the added contents.
    int add_types;
  };
  virtual Browser* CreateNewStripWithContents(
      const std::vector<NewStripContents>& contentses,
      const gfx::Rect& window_bounds,
      const DockInfo& dock_info,
      bool maximize) = 0;

  // Determines what drag actions are possible for the specified strip.
  virtual int GetDragActions() const = 0;

  // Returns whether some contents can be duplicated.
  virtual bool CanDuplicateContentsAt(int index) = 0;

  // Duplicates the contents at the provided index and places it into its own
  // window.
  virtual void DuplicateContentsAt(int index) = 0;

  // Called when a drag session has completed and the frame that initiated the
  // the session should be closed.
  virtual void CloseFrameAfterDragSession() = 0;

  // Creates an entry in the historical tab database for the specified
  // WebContents.
  virtual void CreateHistoricalTab(content::WebContents* contents) = 0;

  // Runs any unload listeners associated with the specified WebContents
  // before it is closed. If there are unload listeners that need to be run,
  // this function returns true and the TabStripModel will wait before closing
  // the WebContents. If it returns false, there are no unload listeners
  // and the TabStripModel will close the WebContents immediately.
  virtual bool RunUnloadListenerBeforeClosing(
      content::WebContents* contents) = 0;

  // Returns true if a tab can be restored.
  virtual bool CanRestoreTab() = 0;

  // Restores the last closed tab if CanRestoreTab would return true.
  virtual void RestoreTab() = 0;

  // Returns true if we should allow "bookmark all tabs" in this window; this is
  // true when there is more than one bookmarkable tab open.
  virtual bool CanBookmarkAllTabs() const = 0;

  // Creates a bookmark folder containing a bookmark for all open tabs.
  virtual void BookmarkAllTabs() = 0;
};

#endif  // CHROME_BROWSER_UI_TABS_TAB_STRIP_MODEL_DELEGATE_H_
