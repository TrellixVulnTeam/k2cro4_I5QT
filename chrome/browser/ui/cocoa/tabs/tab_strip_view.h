// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_TABS_TAB_STRIP_VIEW_H_
#define CHROME_BROWSER_UI_COCOA_TABS_TAB_STRIP_VIEW_H_

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_nsobject.h"
#import "chrome/browser/ui/cocoa/url_drop_target.h"

@class NewTabButton;
@class TabStripController;

// A view class that handles rendering the tab strip and drops of URLS with
// a positioning locator for drop feedback.

@interface TabStripView : NSView<URLDropTarget> {
 @private
  TabStripController* controller_;  // Weak; owns us.

  NSTimeInterval lastMouseUp_;

  // Handles being a drag-and-drop target.
  scoped_nsobject<URLDropTargetHandler> dropHandler_;

  // Weak; the following come from the nib.
  NewTabButton* newTabButton_;

  // Whether the drop-indicator arrow is shown, and if it is, the coordinate of
  // its tip.
  BOOL dropArrowShown_;
  NSPoint dropArrowPosition_;
}

@property(assign, nonatomic) BOOL dropArrowShown;
@property(assign, nonatomic) NSPoint dropArrowPosition;

// Name starts with "get" because methods staring with "new" return retained
// objects according to Cocoa's create rule.
- (NewTabButton*)getNewTabButton;

@end

// Interface for the controller to set and clear the weak reference to itself.
@interface TabStripView (TabStripControllerInterface)
- (void)setController:(TabStripController*)controller;
@end

// Protected methods subclasses can override to alter behavior. Clients should
// not call these directly.
@interface TabStripView (Protected)
- (void)drawBottomBorder:(NSRect)bounds;
- (BOOL)doubleClickMinimizesWindow;
@end

@interface TabStripView (TestingAPI)
- (void)setNewTabButton:(NewTabButton*)button;
@end

#endif  // CHROME_BROWSER_UI_COCOA_TABS_TAB_STRIP_VIEW_H_
