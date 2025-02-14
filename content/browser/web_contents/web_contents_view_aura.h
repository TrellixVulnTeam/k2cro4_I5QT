// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_AURA_H_
#define CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_AURA_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "content/browser/renderer_host/overscroll_controller_delegate.h"
#include "content/common/content_export.h"
#include "content/port/browser/render_view_host_delegate_view.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/aura/client/drag_drop_delegate.h"
#include "ui/aura/window_delegate.h"
#include "ui/compositor/layer_animation_observer.h"

namespace aura {
class Window;
}

namespace ui {
class DropTargetEvent;
}

namespace content {
class WebContentsViewDelegate;
class WebContentsImpl;
class WebDragDestDelegate;

class CONTENT_EXPORT WebContentsViewAura
    : public WebContentsView,
      public RenderViewHostDelegateView,
      NON_EXPORTED_BASE(public OverscrollControllerDelegate),
      public ui::ImplicitAnimationObserver,
      public aura::WindowDelegate,
      public aura::client::DragDropDelegate {
 public:
  WebContentsViewAura(WebContentsImpl* web_contents,
                      WebContentsViewDelegate* delegate);

 private:
  class WindowObserver;

  virtual ~WebContentsViewAura();

  void SizeChangedCommon(const gfx::Size& size);

  void EndDrag(WebKit::WebDragOperationsMask ops);

  // Creates and sets up the overlay window that will be displayed during the
  // overscroll gesture.
  void PrepareOverscrollWindow();

  // Sets up the content window in preparation for starting an overscroll
  // gesture.
  void PrepareContentWindowForOverscroll();

  // Resets any in-progress animation for the overscroll gesture. Note that this
  // doesn't immediately reset the internal states; that happens after an
  // animation.
  void ResetOverscrollTransform();

  // Completes the navigation in response to a completed overscroll gesture.
  // The navigation happens after an animation (either the overlay window
  // animates in, or the content window animates out).
  void CompleteOverscrollNavigation(OverscrollMode mode);

  // Returns the window that should be animated for the overscroll gesture.
  // (note that during the overscroll gesture, either the overlay window or the
  // content window can be animated).
  aura::Window* GetWindowToAnimateForOverscroll();

  // Returns the amount the animating window should be translated in response to
  // the overscroll gesture.
  gfx::Vector2d GetTranslationForOverscroll(int delta_x, int delta_y);

  // Overridden from WebContentsView:
  virtual void CreateView(const gfx::Size& initial_size) OVERRIDE;
  virtual RenderWidgetHostView* CreateViewForWidget(
      RenderWidgetHost* render_widget_host) OVERRIDE;
  virtual void SetView(RenderWidgetHostView* view) OVERRIDE;
  virtual gfx::NativeView GetNativeView() const OVERRIDE;
  virtual gfx::NativeView GetContentNativeView() const OVERRIDE;
  virtual gfx::NativeWindow GetTopLevelNativeWindow() const OVERRIDE;
  virtual void GetContainerBounds(gfx::Rect *out) const OVERRIDE;
  virtual void SetPageTitle(const string16& title) OVERRIDE;
  virtual void OnTabCrashed(base::TerminationStatus status,
                            int error_code) OVERRIDE;
  virtual void SizeContents(const gfx::Size& size) OVERRIDE;
  virtual void RenderViewCreated(RenderViewHost* host) OVERRIDE;
  virtual void Focus() OVERRIDE;
  virtual void SetInitialFocus() OVERRIDE;
  virtual void StoreFocus() OVERRIDE;
  virtual void RestoreFocus() OVERRIDE;
  virtual WebDropData* GetDropData() const OVERRIDE;
  virtual bool IsEventTracking() const OVERRIDE;
  virtual void CloseTabAfterEventTracking() OVERRIDE;
  virtual gfx::Rect GetViewBounds() const OVERRIDE;

  // Overridden from RenderViewHostDelegateView:
  virtual void ShowContextMenu(
      const ContextMenuParams& params,
      ContextMenuSourceType type) OVERRIDE;
  virtual void ShowPopupMenu(const gfx::Rect& bounds,
                             int item_height,
                             double item_font_size,
                             int selected_item,
                             const std::vector<WebMenuItem>& items,
                             bool right_aligned,
                             bool allow_multiple_selection) OVERRIDE;
  virtual void StartDragging(const WebDropData& drop_data,
                             WebKit::WebDragOperationsMask operations,
                             const gfx::ImageSkia& image,
                             const gfx::Vector2d& image_offset,
                             const DragEventSourceInfo& event_info) OVERRIDE;
  virtual void UpdateDragCursor(WebKit::WebDragOperation operation) OVERRIDE;
  virtual void GotFocus() OVERRIDE;
  virtual void TakeFocus(bool reverse) OVERRIDE;

  // Overridden from OverscrollControllerDelegate:
  virtual void OnOverscrollUpdate(float delta_x, float delta_y) OVERRIDE;
  virtual void OnOverscrollComplete(OverscrollMode overscroll_mode) OVERRIDE;
  virtual void OnOverscrollModeChange(OverscrollMode old_mode,
                                      OverscrollMode new_mode) OVERRIDE;

  // Overridden from ui::ImplicitAnimationObserver:
  virtual void OnImplicitAnimationsCompleted() OVERRIDE;

  // Overridden from aura::WindowDelegate:
  virtual gfx::Size GetMinimumSize() const OVERRIDE;
  virtual void OnBoundsChanged(const gfx::Rect& old_bounds,
                               const gfx::Rect& new_bounds) OVERRIDE;
  virtual void OnFocus(aura::Window* old_focused_window) OVERRIDE;
  virtual void OnBlur() OVERRIDE;
  virtual gfx::NativeCursor GetCursor(const gfx::Point& point) OVERRIDE;
  virtual int GetNonClientComponent(const gfx::Point& point) const OVERRIDE;
  virtual bool ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) OVERRIDE;
  virtual bool CanFocus() OVERRIDE;
  virtual void OnCaptureLost() OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual void OnDeviceScaleFactorChanged(float device_scale_factor) OVERRIDE;
  virtual void OnWindowDestroying() OVERRIDE;
  virtual void OnWindowDestroyed() OVERRIDE;
  virtual void OnWindowTargetVisibilityChanged(bool visible) OVERRIDE;
  virtual bool HasHitTestMask() const OVERRIDE;
  virtual void GetHitTestMask(gfx::Path* mask) const OVERRIDE;
  virtual scoped_refptr<ui::Texture> CopyTexture() OVERRIDE;

  // Overridden from ui::EventHandler:
  virtual ui::EventResult OnKeyEvent(ui::KeyEvent* event) OVERRIDE;
  virtual ui::EventResult OnMouseEvent(ui::MouseEvent* event) OVERRIDE;
  virtual ui::EventResult OnTouchEvent(ui::TouchEvent* event) OVERRIDE;
  virtual ui::EventResult OnGestureEvent(ui::GestureEvent* event) OVERRIDE;

  // Overridden from aura::client::DragDropDelegate:
  virtual void OnDragEntered(const ui::DropTargetEvent& event) OVERRIDE;
  virtual int OnDragUpdated(const ui::DropTargetEvent& event) OVERRIDE;
  virtual void OnDragExited() OVERRIDE;
  virtual int OnPerformDrop(const ui::DropTargetEvent& event) OVERRIDE;

  scoped_ptr<aura::Window> window_;
  scoped_ptr<aura::Window> overscroll_window_;

  scoped_ptr<WindowObserver> window_observer_;

  // The WebContentsImpl whose contents we display.
  WebContentsImpl* web_contents_;

  RenderWidgetHostView* view_;

  scoped_ptr<WebContentsViewDelegate> delegate_;

  WebKit::WebDragOperationsMask current_drag_op_;

  scoped_ptr<WebDropData> current_drop_data_;

  WebDragDestDelegate* drag_dest_delegate_;

  // We keep track of the render view host we're dragging over.  If it changes
  // during a drag, we need to re-send the DragEnter message.  WARNING:
  // this pointer should never be dereferenced.  We only use it for comparing
  // pointers.
  void* current_rvh_for_drag_;

  // The overscroll gesture currently in progress.
  OverscrollMode current_overscroll_gesture_;

  // This is the completed overscroll gesture. This is used for the animation
  // callback that happens in response to a completed overscroll gesture.
  OverscrollMode completed_overscroll_gesture_;

  DISALLOW_COPY_AND_ASSIGN(WebContentsViewAura);
};

}  // namespace content

#endif  // CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_AURA_H_
