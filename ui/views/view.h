// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_VIEW_H_
#define UI_VIEWS_VIEW_H_

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "build/build_config.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/events/event.h"
#include "ui/base/events/event_target.h"
#include "ui/compositor/layer_delegate.h"
#include "ui/compositor/layer_owner.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/vector2d.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/focus_border.h"

#if defined(OS_WIN)
#include "base/win/scoped_comptr.h"
#endif

using ui::OSExchangeData;

namespace gfx {
class Canvas;
class Insets;
class Path;
class Transform;
}

namespace ui {
struct AccessibleViewState;
class Compositor;
class Layer;
class NativeTheme;
class TextInputClient;
class Texture;
class ThemeProvider;
}

#if defined(OS_WIN)
class __declspec(uuid("26f5641a-246d-457b-a96d-07f3fae6acf2"))
NativeViewAccessibilityWin;
#endif

namespace views {

class Background;
class Border;
class ContextMenuController;
class DragController;
class FocusBorder;
class FocusManager;
class FocusTraversable;
class InputMethod;
class LayoutManager;
class ScrollView;
class Widget;

namespace internal {
class RootView;
}

/////////////////////////////////////////////////////////////////////////////
//
// View class
//
//   A View is a rectangle within the views View hierarchy. It is the base
//   class for all Views.
//
//   A View is a container of other Views (there is no such thing as a Leaf
//   View - makes code simpler, reduces type conversion headaches, design
//   mistakes etc)
//
//   The View contains basic properties for sizing (bounds), layout (flex,
//   orientation, etc), painting of children and event dispatch.
//
//   The View also uses a simple Box Layout Manager similar to XUL's
//   SprocketLayout system. Alternative Layout Managers implementing the
//   LayoutManager interface can be used to lay out children if required.
//
//   It is up to the subclass to implement Painting and storage of subclass -
//   specific properties and functionality.
//
//   Unless otherwise documented, views is not thread safe and should only be
//   accessed from the main thread.
//
/////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT View : public ui::LayerDelegate,
                          public ui::LayerOwner,
                          public ui::AcceleratorTarget,
                          public ui::EventTarget {
 public:
  typedef std::vector<View*> Views;

  // Creation and lifetime -----------------------------------------------------

  View();
  virtual ~View();

  // By default a View is owned by its parent unless specified otherwise here.
  void set_owned_by_client() { owned_by_client_ = true; }

  // Tree operations -----------------------------------------------------------

  // Get the Widget that hosts this View, if any.
  virtual const Widget* GetWidget() const;
  virtual Widget* GetWidget();

  // Adds |view| as a child of this view, optionally at |index|.
  void AddChildView(View* view);
  void AddChildViewAt(View* view, int index);

  // Moves |view| to the specified |index|. A negative value for |index| moves
  // the view at the end.
  void ReorderChildView(View* view, int index);

  // Removes |view| from this view. The view's parent will change to NULL.
  void RemoveChildView(View* view);

  // Removes all the children from this view. If |delete_children| is true,
  // the views are deleted, unless marked as not parent owned.
  void RemoveAllChildViews(bool delete_children);

  int child_count() const { return static_cast<int>(children_.size()); }
  bool has_children() const { return !children_.empty(); }

  // Returns the child view at |index|.
  const View* child_at(int index) const {
    DCHECK_GE(index, 0);
    DCHECK_LT(index, child_count());
    return children_[index];
  }
  View* child_at(int index) {
    return const_cast<View*>(const_cast<const View*>(this)->child_at(index));
  }

  // Returns the parent view.
  const View* parent() const { return parent_; }
  View* parent() { return parent_; }

  // Returns true if |view| is contained within this View's hierarchy, even as
  // an indirect descendant. Will return true if child is also this view.
  bool Contains(const View* view) const;

  // Returns the index of |view|, or -1 if |view| is not a child of this view.
  int GetIndexOf(const View* view) const;

  // Size and disposition ------------------------------------------------------
  // Methods for obtaining and modifying the position and size of the view.
  // Position is in the coordinate system of the view's parent.
  // Position is NOT flipped for RTL. See "RTL positioning" for RTL-sensitive
  // position accessors.
  // Transformations are not applied on the size/position. For example, if
  // bounds is (0, 0, 100, 100) and it is scaled by 0.5 along the X axis, the
  // width will still be 100 (although when painted, it will be 50x50, painted
  // at location (0, 0)).

  void SetBounds(int x, int y, int width, int height);
  void SetBoundsRect(const gfx::Rect& bounds);
  void SetSize(const gfx::Size& size);
  void SetPosition(const gfx::Point& position);
  void SetX(int x);
  void SetY(int y);

  // No transformation is applied on the size or the locations.
  const gfx::Rect& bounds() const { return bounds_; }
  int x() const { return bounds_.x(); }
  int y() const { return bounds_.y(); }
  int width() const { return bounds_.width(); }
  int height() const { return bounds_.height(); }
  const gfx::Size& size() const { return bounds_.size(); }

  // Returns the bounds of the content area of the view, i.e. the rectangle
  // enclosed by the view's border.
  gfx::Rect GetContentsBounds() const;

  // Returns the bounds of the view in its own coordinates (i.e. position is
  // 0, 0).
  gfx::Rect GetLocalBounds() const;

  // Returns the bounds of the layer in its own pixel coordinates.
  gfx::Rect GetLayerBoundsInPixel() const;

  // Returns the insets of the current border. If there is no border an empty
  // insets is returned.
  virtual gfx::Insets GetInsets() const;

  // Returns the visible bounds of the receiver in the receivers coordinate
  // system.
  //
  // When traversing the View hierarchy in order to compute the bounds, the
  // function takes into account the mirroring setting and transformation for
  // each View and therefore it will return the mirrored and transformed version
  // of the visible bounds if need be.
  gfx::Rect GetVisibleBounds() const;

  // Return the bounds of the View in screen coordinate system.
  gfx::Rect GetBoundsInScreen() const;

  // Returns the baseline of this view, or -1 if this view has no baseline. The
  // return value is relative to the preferred height.
  virtual int GetBaseline() const;

  // Get the size the View would like to be, if enough space were available.
  virtual gfx::Size GetPreferredSize();

  // Convenience method that sizes this view to its preferred size.
  void SizeToPreferredSize();

  // Gets the minimum size of the view. View's implementation invokes
  // GetPreferredSize.
  virtual gfx::Size GetMinimumSize();

  // Gets the maximum size of the view. Currently only used for sizing shell
  // windows.
  virtual gfx::Size GetMaximumSize();

  // Return the height necessary to display this view with the provided width.
  // View's implementation returns the value from getPreferredSize.cy.
  // Override if your View's preferred height depends upon the width (such
  // as with Labels).
  virtual int GetHeightForWidth(int w);

  // Set whether this view is visible. Painting is scheduled as needed.
  virtual void SetVisible(bool visible);

  // Return whether a view is visible
  bool visible() const { return visible_; }

  // Returns true if this view is drawn on screen.
  virtual bool IsDrawn() const;

  // Set whether this view is enabled. A disabled view does not receive keyboard
  // or mouse inputs. If |enabled| differs from the current value, SchedulePaint
  // is invoked.
  void SetEnabled(bool enabled);

  // Returns whether the view is enabled.
  bool enabled() const { return enabled_; }

  // This indicates that the view completely fills its bounds in an opaque
  // color. This doesn't affect compositing but is a hint to the compositor to
  // optimize painting.
  // Note that this method does not implicitly create a layer if one does not
  // already exist for the View, but is a no-op in that case.
  void SetFillsBoundsOpaquely(bool fills_bounds_opaquely);

  // Transformations -----------------------------------------------------------

  // Methods for setting transformations for a view (e.g. rotation, scaling).

  const gfx::Transform& GetTransform() const;

  // Clipping parameters. Clipping is done relative to the view bounds.
  void set_clip_insets(gfx::Insets clip_insets) { clip_insets_ = clip_insets; }

  // Sets the transform to the supplied transform.
  void SetTransform(const gfx::Transform& transform);

  // Sets whether this view paints to a layer. A view paints to a layer if
  // either of the following are true:
  // . the view has a non-identity transform.
  // . SetPaintToLayer(true) has been invoked.
  // View creates the Layer only when it exists in a Widget with a non-NULL
  // Compositor.
  void SetPaintToLayer(bool paint_to_layer);

  // Recreates a layer for the view and returns the old layer. After this call,
  // the View no longer has a pointer to the old layer (so it won't be able to
  // update the old layer or destroy it). The caller must free the returned
  // layer.
  // Returns NULL and does not recreate layer if view does not own its layer.
  ui::Layer* RecreateLayer() WARN_UNUSED_RESULT;

  // RTL positioning -----------------------------------------------------------

  // Methods for accessing the bounds and position of the view, relative to its
  // parent. The position returned is mirrored if the parent view is using a RTL
  // layout.
  //
  // NOTE: in the vast majority of the cases, the mirroring implementation is
  //       transparent to the View subclasses and therefore you should use the
  //       bounds() accessor instead.
  gfx::Rect GetMirroredBounds() const;
  gfx::Point GetMirroredPosition() const;
  int GetMirroredX() const;

  // Given a rectangle specified in this View's coordinate system, the function
  // computes the 'left' value for the mirrored rectangle within this View. If
  // the View's UI layout is not right-to-left, then bounds.x() is returned.
  //
  // UI mirroring is transparent to most View subclasses and therefore there is
  // no need to call this routine from anywhere within your subclass
  // implementation.
  int GetMirroredXForRect(const gfx::Rect& rect) const;

  // Given the X coordinate of a point inside the View, this function returns
  // the mirrored X coordinate of the point if the View's UI layout is
  // right-to-left. If the layout is left-to-right, the same X coordinate is
  // returned.
  //
  // Following are a few examples of the values returned by this function for
  // a View with the bounds {0, 0, 100, 100} and a right-to-left layout:
  //
  // GetMirroredXCoordinateInView(0) -> 100
  // GetMirroredXCoordinateInView(20) -> 80
  // GetMirroredXCoordinateInView(99) -> 1
  int GetMirroredXInView(int x) const;

  // Given a X coordinate and a width inside the View, this function returns
  // the mirrored X coordinate if the View's UI layout is right-to-left. If the
  // layout is left-to-right, the same X coordinate is returned.
  //
  // Following are a few examples of the values returned by this function for
  // a View with the bounds {0, 0, 100, 100} and a right-to-left layout:
  //
  // GetMirroredXCoordinateInView(0, 10) -> 90
  // GetMirroredXCoordinateInView(20, 20) -> 60
  int GetMirroredXWithWidthInView(int x, int w) const;

  // Layout --------------------------------------------------------------------

  // Lay out the child Views (set their bounds based on sizing heuristics
  // specific to the current Layout Manager)
  virtual void Layout();

  // TODO(beng): I think we should remove this.
  // Mark this view and all parents to require a relayout. This ensures the
  // next call to Layout() will propagate to this view, even if the bounds of
  // parent views do not change.
  void InvalidateLayout();

  // Gets/Sets the Layout Manager used by this view to size and place its
  // children.
  // The LayoutManager is owned by the View and is deleted when the view is
  // deleted, or when a new LayoutManager is installed.
  LayoutManager* GetLayoutManager() const;
  void SetLayoutManager(LayoutManager* layout);

  // Attributes ----------------------------------------------------------------

  // The view class name.
  static const char kViewClassName[];

  // Return the receiving view's class name. A view class is a string which
  // uniquely identifies the view class. It is intended to be used as a way to
  // find out during run time if a view can be safely casted to a specific view
  // subclass. The default implementation returns kViewClassName.
  virtual std::string GetClassName() const;

  // Returns the first ancestor, starting at this, whose class name is |name|.
  // Returns null if no ancestor has the class name |name|.
  View* GetAncestorWithClassName(const std::string& name);

  // Recursively descends the view tree starting at this view, and returns
  // the first child that it encounters that has the given ID.
  // Returns NULL if no matching child view is found.
  virtual const View* GetViewByID(int id) const;
  virtual View* GetViewByID(int id);

  // Gets and sets the ID for this view. ID should be unique within the subtree
  // that you intend to search for it. 0 is the default ID for views.
  int id() const { return id_; }
  void set_id(int id) { id_ = id; }

  // A group id is used to tag views which are part of the same logical group.
  // Focus can be moved between views with the same group using the arrow keys.
  // Groups are currently used to implement radio button mutual exclusion.
  // The group id is immutable once it's set.
  void SetGroup(int gid);
  // Returns the group id of the view, or -1 if the id is not set yet.
  int GetGroup() const;

  // If this returns true, the views from the same group can each be focused
  // when moving focus with the Tab/Shift-Tab key.  If this returns false,
  // only the selected view from the group (obtained with
  // GetSelectedViewForGroup()) is focused.
  virtual bool IsGroupFocusTraversable() const;

  // Fills |views| with all the available views which belong to the provided
  // |group|.
  void GetViewsInGroup(int group, Views* views);

  // Returns the View that is currently selected in |group|.
  // The default implementation simply returns the first View found for that
  // group.
  virtual View* GetSelectedViewForGroup(int group);

  // Coordinate conversion -----------------------------------------------------

  // Note that the utility coordinate conversions functions always operate on
  // the mirrored position of the child Views if the parent View uses a
  // right-to-left UI layout.

  // Convert a point from the coordinate system of one View to another.
  //
  // |source| and |target| must be in the same widget, but doesn't need to be in
  // the same view hierarchy.
  // |source| can be NULL in which case it means the screen coordinate system.
  static void ConvertPointToTarget(const View* source,
                                   const View* target,
                                   gfx::Point* point);

  // Convert a point from a View's coordinate system to that of its Widget.
  static void ConvertPointToWidget(const View* src, gfx::Point* point);

  // Convert a point from the coordinate system of a View's Widget to that
  // View's coordinate system.
  static void ConvertPointFromWidget(const View* dest, gfx::Point* p);

  // Convert a point from a View's coordinate system to that of the screen.
  static void ConvertPointToScreen(const View* src, gfx::Point* point);

  // Convert a point from a View's coordinate system to that of the screen.
  static void ConvertPointFromScreen(const View* dst, gfx::Point* point);

  // Applies transformation on the rectangle, which is in the view's coordinate
  // system, to convert it into the parent's coordinate system.
  gfx::Rect ConvertRectToParent(const gfx::Rect& rect) const;

  // Converts a rectangle from this views coordinate system to its widget
  // coordinate system.
  gfx::Rect ConvertRectToWidget(const gfx::Rect& rect) const;

  // Painting ------------------------------------------------------------------

  // Mark all or part of the View's bounds as dirty (needing repaint).
  // |r| is in the View's coordinates.
  // Rectangle |r| should be in the view's coordinate system. The
  // transformations are applied to it to convert it into the parent coordinate
  // system before propagating SchedulePaint up the view hierarchy.
  // TODO(beng): Make protected.
  virtual void SchedulePaint();
  virtual void SchedulePaintInRect(const gfx::Rect& r);

  // Called by the framework to paint a View. Performs translation and clipping
  // for View coordinates and language direction as required, allows the View
  // to paint itself via the various OnPaint*() event handlers and then paints
  // the hierarchy beneath it.
  virtual void Paint(gfx::Canvas* canvas);

  // The background object is owned by this object and may be NULL.
  void set_background(Background* b) { background_.reset(b); }
  const Background* background() const { return background_.get(); }
  Background* background() { return background_.get(); }

  // The border object is owned by this object and may be NULL.
  void set_border(Border* b) { border_.reset(b); }
  const Border* border() const { return border_.get(); }
  Border* border() { return border_.get(); }

  // The focus_border object is owned by this object and may be NULL.
  void set_focus_border(FocusBorder* b) { focus_border_.reset(b); }
  const FocusBorder* focus_border() const { return focus_border_.get(); }
  FocusBorder* focus_border() { return focus_border_.get(); }

  // Get the theme provider from the parent widget.
  virtual ui::ThemeProvider* GetThemeProvider() const;

  // Returns the NativeTheme to use for this View. This calls through to
  // GetNativeTheme() on the Widget this View is in. If this View is not in a
  // Widget this returns ui::NativeTheme::instance().
  ui::NativeTheme* GetNativeTheme() {
    return const_cast<ui::NativeTheme*>(
        const_cast<const View*>(this)->GetNativeTheme());
  }
  const ui::NativeTheme* GetNativeTheme() const;

  // RTL painting --------------------------------------------------------------

  // This method determines whether the gfx::Canvas object passed to
  // View::Paint() needs to be transformed such that anything drawn on the
  // canvas object during View::Paint() is flipped horizontally.
  //
  // By default, this function returns false (which is the initial value of
  // |flip_canvas_on_paint_for_rtl_ui_|). View subclasses that need to paint on
  // a flipped gfx::Canvas when the UI layout is right-to-left need to call
  // EnableCanvasFlippingForRTLUI().
  bool FlipCanvasOnPaintForRTLUI() const {
    return flip_canvas_on_paint_for_rtl_ui_ ? base::i18n::IsRTL() : false;
  }

  // Enables or disables flipping of the gfx::Canvas during View::Paint().
  // Note that if canvas flipping is enabled, the canvas will be flipped only
  // if the UI layout is right-to-left; that is, the canvas will be flipped
  // only if base::i18n::IsRTL() returns true.
  //
  // Enabling canvas flipping is useful for leaf views that draw an image that
  // needs to be flipped horizontally when the UI layout is right-to-left
  // (views::Button, for example). This method is helpful for such classes
  // because their drawing logic stays the same and they can become agnostic to
  // the UI directionality.
  void EnableCanvasFlippingForRTLUI(bool enable) {
    flip_canvas_on_paint_for_rtl_ui_ = enable;
  }

  // Accelerated painting ------------------------------------------------------

  // Enable/Disable accelerated compositing.
  static void set_use_acceleration_when_possible(bool use);
  static bool get_use_acceleration_when_possible();

  // Input ---------------------------------------------------------------------
  // The points (and mouse locations) in the following functions are in the
  // view's coordinates, except for a RootView.

  // Returns the deepest visible descendant that contains the specified point.
  virtual View* GetEventHandlerForPoint(const gfx::Point& point);

  // Return the cursor that should be used for this view or the default cursor.
  // The event location is in the receiver's coordinate system. The caller is
  // responsible for managing the lifetime of the returned object, though that
  // lifetime may vary from platform to platform. On Windows and Aura,
  // the cursor is a shared resource.
  virtual gfx::NativeCursor GetCursor(const ui::MouseEvent& event);

  // A convenience function which calls HitTestRect() with a rect of size
  // 1x1 and an origin of |point|.
  bool HitTestPoint(const gfx::Point& point) const;

  // Tests whether |rect| intersects this view's bounds.
  virtual bool HitTestRect(const gfx::Rect& rect) const;

  // This method is invoked when the user clicks on this view.
  // The provided event is in the receiver's coordinate system.
  //
  // Return true if you processed the event and want to receive subsequent
  // MouseDraggged and MouseReleased events.  This also stops the event from
  // bubbling.  If you return false, the event will bubble through parent
  // views.
  //
  // If you remove yourself from the tree while processing this, event bubbling
  // stops as if you returned true, but you will not receive future events.
  // The return value is ignored in this case.
  //
  // Default implementation returns true if a ContextMenuController has been
  // set, false otherwise. Override as needed.
  //
  virtual bool OnMousePressed(const ui::MouseEvent& event);

  // This method is invoked when the user clicked on this control.
  // and is still moving the mouse with a button pressed.
  // The provided event is in the receiver's coordinate system.
  //
  // Return true if you processed the event and want to receive
  // subsequent MouseDragged and MouseReleased events.
  //
  // Default implementation returns true if a ContextMenuController has been
  // set, false otherwise. Override as needed.
  //
  virtual bool OnMouseDragged(const ui::MouseEvent& event);

  // This method is invoked when the user releases the mouse
  // button. The event is in the receiver's coordinate system.
  //
  // Default implementation notifies the ContextMenuController is appropriate.
  // Subclasses that wish to honor the ContextMenuController should invoke
  // super.
  virtual void OnMouseReleased(const ui::MouseEvent& event);

  // This method is invoked when the mouse press/drag was canceled by a
  // system/user gesture.
  virtual void OnMouseCaptureLost();

  // This method is invoked when the mouse is above this control
  // The event is in the receiver's coordinate system.
  //
  // Default implementation does nothing. Override as needed.
  virtual void OnMouseMoved(const ui::MouseEvent& event);

  // This method is invoked when the mouse enters this control.
  //
  // Default implementation does nothing. Override as needed.
  virtual void OnMouseEntered(const ui::MouseEvent& event);

  // This method is invoked when the mouse exits this control
  // The provided event location is always (0, 0)
  // Default implementation does nothing. Override as needed.
  virtual void OnMouseExited(const ui::MouseEvent& event);

  // Set the MouseHandler for a drag session.
  //
  // A drag session is a stream of mouse events starting
  // with a MousePressed event, followed by several MouseDragged
  // events and finishing with a MouseReleased event.
  //
  // This method should be only invoked while processing a
  // MouseDragged or MousePressed event.
  //
  // All further mouse dragged and mouse up events will be sent
  // the MouseHandler, even if it is reparented to another window.
  //
  // The MouseHandler is automatically cleared when the control
  // comes back from processing the MouseReleased event.
  //
  // Note: if the mouse handler is no longer connected to a
  // view hierarchy, events won't be sent.
  //
  // TODO(sky): rename this.
  virtual void SetMouseHandler(View* new_mouse_handler);

  // Invoked when a key is pressed or released.
  // Subclasser should return true if the event has been processed and false
  // otherwise. If the event has not been processed, the parent will be given a
  // chance.
  virtual bool OnKeyPressed(const ui::KeyEvent& event);
  virtual bool OnKeyReleased(const ui::KeyEvent& event);

  // Invoked when the user uses the mousewheel. Implementors should return true
  // if the event has been processed and false otherwise. This message is sent
  // if the view is focused. If the event has not been processed, the parent
  // will be given a chance.
  virtual bool OnMouseWheel(const ui::MouseWheelEvent& event);


  // See field for description.
  void set_notify_enter_exit_on_child(bool notify) {
    notify_enter_exit_on_child_ = notify;
  }
  bool notify_enter_exit_on_child() const {
    return notify_enter_exit_on_child_;
  }

  // Returns the View's TextInputClient instance or NULL if the View doesn't
  // support text input.
  virtual ui::TextInputClient* GetTextInputClient();

  // Convenience method to retrieve the InputMethod associated with the
  // Widget that contains this view. Returns NULL if this view is not part of a
  // view hierarchy with a Widget.
  virtual InputMethod* GetInputMethod();

  // Overridden from ui::EventTarget:
  virtual bool CanAcceptEvents() OVERRIDE;
  virtual ui::EventTarget* GetParentTarget() OVERRIDE;

  // Overridden from ui::EventHandler:
  virtual ui::EventResult OnKeyEvent(ui::KeyEvent* event) OVERRIDE;
  virtual ui::EventResult OnMouseEvent(ui::MouseEvent* event) OVERRIDE;
  virtual ui::EventResult OnScrollEvent(ui::ScrollEvent* event) OVERRIDE;
  virtual ui::EventResult OnTouchEvent(ui::TouchEvent* event) OVERRIDE;
  virtual ui::EventResult OnGestureEvent(ui::GestureEvent* event) OVERRIDE;

  // Accelerators --------------------------------------------------------------

  // Sets a keyboard accelerator for that view. When the user presses the
  // accelerator key combination, the AcceleratorPressed method is invoked.
  // Note that you can set multiple accelerators for a view by invoking this
  // method several times. Note also that AcceleratorPressed is invoked only
  // when CanHandleAccelerators() is true.
  virtual void AddAccelerator(const ui::Accelerator& accelerator);

  // Removes the specified accelerator for this view.
  virtual void RemoveAccelerator(const ui::Accelerator& accelerator);

  // Removes all the keyboard accelerators for this view.
  virtual void ResetAccelerators();

  // Overridden from AcceleratorTarget:
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) OVERRIDE;

  // Returns whether accelerators are enabled for this view. Accelerators are
  // enabled if the containing widget is visible and the view is enabled() and
  // IsDrawn()
  virtual bool CanHandleAccelerators() const OVERRIDE;

  // Focus ---------------------------------------------------------------------

  // Returns whether this view currently has the focus.
  virtual bool HasFocus() const;

  // Returns the view that should be selected next when pressing Tab.
  View* GetNextFocusableView();
  const View* GetNextFocusableView() const;

  // Returns the view that should be selected next when pressing Shift-Tab.
  View* GetPreviousFocusableView();

  // Sets the component that should be selected next when pressing Tab, and
  // makes the current view the precedent view of the specified one.
  // Note that by default views are linked in the order they have been added to
  // their container. Use this method if you want to modify the order.
  // IMPORTANT NOTE: loops in the focus hierarchy are not supported.
  void SetNextFocusableView(View* view);

  // Sets whether this view is capable of taking focus.
  // Note that this is false by default so that a view used as a container does
  // not get the focus.
  void set_focusable(bool focusable) { focusable_ = focusable; }

  // Returns true if this view is capable of taking focus.
  bool focusable() const { return focusable_ && enabled_ && visible_; }

  // Returns true if this view is |focusable_|, |enabled_| and drawn.
  virtual bool IsFocusable() const;

  // Return whether this view is focusable when the user requires full keyboard
  // access, even though it may not be normally focusable.
  bool IsAccessibilityFocusable() const;

  // Set whether this view can be made focusable if the user requires
  // full keyboard access, even though it's not normally focusable.
  // Note that this is false by default.
  void set_accessibility_focusable(bool accessibility_focusable) {
    accessibility_focusable_ = accessibility_focusable;
  }

  // Convenience method to retrieve the FocusManager associated with the
  // Widget that contains this view.  This can return NULL if this view is not
  // part of a view hierarchy with a Widget.
  virtual FocusManager* GetFocusManager();
  virtual const FocusManager* GetFocusManager() const;

  // Request the keyboard focus. The receiving view will become the
  // focused view.
  virtual void RequestFocus();

  // Invoked when a view is about to be requested for focus due to the focus
  // traversal. Reverse is this request was generated going backward
  // (Shift-Tab).
  virtual void AboutToRequestFocusFromTabTraversal(bool reverse) {}

  // Invoked when a key is pressed before the key event is processed (and
  // potentially eaten) by the focus manager for tab traversal, accelerators and
  // other focus related actions.
  // The default implementation returns false, ensuring that tab traversal and
  // accelerators processing is performed.
  // Subclasses should return true if they want to process the key event and not
  // have it processed as an accelerator (if any) or as a tab traversal (if the
  // key event is for the TAB key).  In that case, OnKeyPressed will
  // subsequently be invoked for that event.
  virtual bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event);

  // Subclasses that contain traversable children that are not directly
  // accessible through the children hierarchy should return the associated
  // FocusTraversable for the focus traversal to work properly.
  virtual FocusTraversable* GetFocusTraversable();

  // Subclasses that can act as a "pane" must implement their own
  // FocusTraversable to keep the focus trapped within the pane.
  // If this method returns an object, any view that's a direct or
  // indirect child of this view will always use this FocusTraversable
  // rather than the one from the widget.
  virtual FocusTraversable* GetPaneFocusTraversable();

  // Tooltips ------------------------------------------------------------------

  // Gets the tooltip for this View. If the View does not have a tooltip,
  // return false. If the View does have a tooltip, copy the tooltip into
  // the supplied string and return true.
  // Any time the tooltip text that a View is displaying changes, it must
  // invoke TooltipTextChanged.
  // |p| provides the coordinates of the mouse (relative to this view).
  virtual bool GetTooltipText(const gfx::Point& p, string16* tooltip) const;

  // Returns the location (relative to this View) for the text on the tooltip
  // to display. If false is returned (the default), the tooltip is placed at
  // a default position.
  virtual bool GetTooltipTextOrigin(const gfx::Point& p, gfx::Point* loc) const;

  // Context menus -------------------------------------------------------------

  // Sets the ContextMenuController. Setting this to non-null makes the View
  // process mouse events.
  ContextMenuController* context_menu_controller() {
    return context_menu_controller_;
  }
  void set_context_menu_controller(ContextMenuController* menu_controller) {
    context_menu_controller_ = menu_controller;
  }

  // Provides default implementation for context menu handling. The default
  // implementation calls the ShowContextMenu of the current
  // ContextMenuController (if it is not NULL). Overridden in subclassed views
  // to provide right-click menu display triggerd by the keyboard (i.e. for the
  // Chrome toolbar Back and Forward buttons). No source needs to be specified,
  // as it is always equal to the current View.
  virtual void ShowContextMenu(const gfx::Point& p, bool is_mouse_gesture);

  // Drag and drop -------------------------------------------------------------

  DragController* drag_controller() { return drag_controller_; }
  void set_drag_controller(DragController* drag_controller) {
    drag_controller_ = drag_controller;
  }

  // During a drag and drop session when the mouse moves the view under the
  // mouse is queried for the drop types it supports by way of the
  // GetDropFormats methods. If the view returns true and the drag site can
  // provide data in one of the formats, the view is asked if the drop data
  // is required before any other drop events are sent. Once the
  // data is available the view is asked if it supports the drop (by way of
  // the CanDrop method). If a view returns true from CanDrop,
  // OnDragEntered is sent to the view when the mouse first enters the view,
  // as the mouse moves around within the view OnDragUpdated is invoked.
  // If the user releases the mouse over the view and OnDragUpdated returns a
  // valid drop, then OnPerformDrop is invoked. If the mouse moves outside the
  // view or over another view that wants the drag, OnDragExited is invoked.
  //
  // Similar to mouse events, the deepest view under the mouse is first checked
  // if it supports the drop (Drop). If the deepest view under
  // the mouse does not support the drop, the ancestors are walked until one
  // is found that supports the drop.

  // Override and return the set of formats that can be dropped on this view.
  // |formats| is a bitmask of the formats defined bye OSExchangeData::Format.
  // The default implementation returns false, which means the view doesn't
  // support dropping.
  virtual bool GetDropFormats(
      int* formats,
      std::set<OSExchangeData::CustomFormat>* custom_formats);

  // Override and return true if the data must be available before any drop
  // methods should be invoked. The default is false.
  virtual bool AreDropTypesRequired();

  // A view that supports drag and drop must override this and return true if
  // data contains a type that may be dropped on this view.
  virtual bool CanDrop(const OSExchangeData& data);

  // OnDragEntered is invoked when the mouse enters this view during a drag and
  // drop session and CanDrop returns true. This is immediately
  // followed by an invocation of OnDragUpdated, and eventually one of
  // OnDragExited or OnPerformDrop.
  virtual void OnDragEntered(const ui::DropTargetEvent& event);

  // Invoked during a drag and drop session while the mouse is over the view.
  // This should return a bitmask of the DragDropTypes::DragOperation supported
  // based on the location of the event. Return 0 to indicate the drop should
  // not be accepted.
  virtual int OnDragUpdated(const ui::DropTargetEvent& event);

  // Invoked during a drag and drop session when the mouse exits the views, or
  // when the drag session was canceled and the mouse was over the view.
  virtual void OnDragExited();

  // Invoked during a drag and drop session when OnDragUpdated returns a valid
  // operation and the user release the mouse.
  virtual int OnPerformDrop(const ui::DropTargetEvent& event);

  // Invoked from DoDrag after the drag completes. This implementation does
  // nothing, and is intended for subclasses to do cleanup.
  virtual void OnDragDone();

  // Returns true if the mouse was dragged enough to start a drag operation.
  // delta_x and y are the distance the mouse was dragged.
  static bool ExceededDragThreshold(const gfx::Vector2d& delta);

  // Accessibility -------------------------------------------------------------

  // Modifies |state| to reflect the current accessible state of this view.
  virtual void GetAccessibleState(ui::AccessibleViewState* state) { }

  // Returns an instance of the native accessibility interface for this view.
  virtual gfx::NativeViewAccessible GetNativeViewAccessible();

  // Scrolling -----------------------------------------------------------------
  // TODO(beng): Figure out if this can live somewhere other than View, i.e.
  //             closer to ScrollView.

  // Scrolls the specified region, in this View's coordinate system, to be
  // visible. View's implementation passes the call onto the parent View (after
  // adjusting the coordinates). It is up to views that only show a portion of
  // the child view, such as Viewport, to override appropriately.
  virtual void ScrollRectToVisible(const gfx::Rect& rect);

  // The following methods are used by ScrollView to determine the amount
  // to scroll relative to the visible bounds of the view. For example, a
  // return value of 10 indicates the scrollview should scroll 10 pixels in
  // the appropriate direction.
  //
  // Each method takes the following parameters:
  //
  // is_horizontal: if true, scrolling is along the horizontal axis, otherwise
  //                the vertical axis.
  // is_positive: if true, scrolling is by a positive amount. Along the
  //              vertical axis scrolling by a positive amount equates to
  //              scrolling down.
  //
  // The return value should always be positive and gives the number of pixels
  // to scroll. ScrollView interprets a return value of 0 (or negative)
  // to scroll by a default amount.
  //
  // See VariableRowHeightScrollHelper and FixedRowHeightScrollHelper for
  // implementations of common cases.
  virtual int GetPageScrollIncrement(ScrollView* scroll_view,
                                     bool is_horizontal, bool is_positive);
  virtual int GetLineScrollIncrement(ScrollView* scroll_view,
                                     bool is_horizontal, bool is_positive);

 protected:
  // Size and disposition ------------------------------------------------------

  // Override to be notified when the bounds of the view have changed.
  virtual void OnBoundsChanged(const gfx::Rect& previous_bounds);

  // Called when the preferred size of a child view changed.  This gives the
  // parent an opportunity to do a fresh layout if that makes sense.
  virtual void ChildPreferredSizeChanged(View* child) {}

  // Called when the visibility of a child view changed.  This gives the parent
  // an opportunity to do a fresh layout if that makes sense.
  virtual void ChildVisibilityChanged(View* child) {}

  // Invalidates the layout and calls ChildPreferredSizeChanged on the parent
  // if there is one. Be sure to call View::PreferredSizeChanged when
  // overriding such that the layout is properly invalidated.
  virtual void PreferredSizeChanged();

  // Override returning true when the view needs to be notified when its visible
  // bounds relative to the root view may have changed. Only used by
  // NativeViewHost.
  virtual bool NeedsNotificationWhenVisibleBoundsChange() const;

  // Notification that this View's visible bounds relative to the root view may
  // have changed. The visible bounds are the region of the View not clipped by
  // its ancestors. This is used for clipping NativeViewHost.
  virtual void OnVisibleBoundsChanged();

  // Override to be notified when the enabled state of this View has
  // changed. The default implementation calls SchedulePaint() on this View.
  virtual void OnEnabledChanged();

  // Tree operations -----------------------------------------------------------

  // This method is invoked when the tree changes.
  //
  // When a view is removed, it is invoked for all children and grand
  // children. For each of these views, a notification is sent to the
  // view and all parents.
  //
  // When a view is added, a notification is sent to the view, all its
  // parents, and all its children (and grand children)
  //
  // Default implementation does nothing. Override to perform operations
  // required when a view is added or removed from a view hierarchy
  //
  // parent is the new or old parent. Child is the view being added or
  // removed.
  //
  virtual void ViewHierarchyChanged(bool is_add, View* parent, View* child);

  // When SetVisible() changes the visibility of a view, this method is
  // invoked for that view as well as all the children recursively.
  virtual void VisibilityChanged(View* starting_from, bool is_visible);

  // Called when the native view hierarchy changed.
  // |attached| is true if that view has been attached to a new NativeView
  // hierarchy, false if it has been detached.
  // |native_view| is the NativeView this view was attached/detached from, and
  // |root_view| is the root view associated with the NativeView.
  // Views created without a native view parent don't have a focus manager.
  // When this function is called they could do the processing that requires
  // it - like registering accelerators, for example.
  virtual void NativeViewHierarchyChanged(bool attached,
                                          gfx::NativeView native_view,
                                          internal::RootView* root_view);

  // Painting ------------------------------------------------------------------

  // Responsible for calling Paint() on child Views. Override to control the
  // order child Views are painted.
  virtual void PaintChildren(gfx::Canvas* canvas);

  // Override to provide rendering in any part of the View's bounds. Typically
  // this is the "contents" of the view. If you override this method you will
  // have to call the subsequent OnPaint*() methods manually.
  virtual void OnPaint(gfx::Canvas* canvas);

  // Override to paint a background before any content is drawn. Typically this
  // is done if you are satisfied with a default OnPaint handler but wish to
  // supply a different background.
  virtual void OnPaintBackground(gfx::Canvas* canvas);

  // Override to paint a border not specified by SetBorder().
  virtual void OnPaintBorder(gfx::Canvas* canvas);

  // Override to paint a focus border not specified by set_focus_border() around
  // relevant contents.  The focus border is usually a dotted rectangle.
  virtual void OnPaintFocusBorder(gfx::Canvas* canvas);

  // Accelerated painting ------------------------------------------------------

  // This creates a layer for the view, if one does not exist. It then
  // passes the texture to a layer associated with the view. While an external
  // texture is set, the view will not update the layer contents.
  //
  // |texture| cannot be NULL.
  //
  // Returns false if it cannot create a layer to which to assign the texture.
  bool SetExternalTexture(ui::Texture* texture);

  // Returns the offset from this view to the nearest ancestor with a layer. If
  // |layer_parent| is non-NULL it is set to the nearest ancestor with a layer.
  virtual gfx::Vector2d CalculateOffsetToAncestorWithLayer(
      ui::Layer** layer_parent);

  // If this view has a layer, the layer is reparented to |parent_layer| and its
  // bounds is set based on |point|. If this view does not have a layer, then
  // recurses through all children. This is used when adding a layer to an
  // existing view to make sure all descendants that have layers are parented to
  // the right layer.
  virtual void MoveLayerToParent(ui::Layer* parent_layer,
                                 const gfx::Point& point);

  // Called to update the bounds of any child layers within this View's
  // hierarchy when something happens to the hierarchy.
  virtual void UpdateChildLayerBounds(const gfx::Vector2d& offset);

  // Overridden from ui::LayerDelegate:
  virtual void OnPaintLayer(gfx::Canvas* canvas) OVERRIDE;
  virtual void OnDeviceScaleFactorChanged(float device_scale_factor) OVERRIDE;
  virtual base::Closure PrepareForLayerBoundsChange() OVERRIDE;

  // Finds the layer that this view paints to (it may belong to an ancestor
  // view), then reorders the immediate children of that layer to match the
  // order of the view tree.
  virtual void ReorderLayers();

  // This reorders the immediate children of |*parent_layer| to match the
  // order of the view tree.
  virtual void ReorderChildLayers(ui::Layer* parent_layer);

  // Input ---------------------------------------------------------------------

  // Called by HitTestRect() to see if this View has a custom hit test mask. If
  // the return value is true, GetHitTestMask() will be called to obtain the
  // mask. Default value is false, in which case the View will hit-test against
  // its bounds.
  virtual bool HasHitTestMask() const;

  // Called by HitTestRect() to retrieve a mask for hit-testing against.
  // Subclasses override to provide custom shaped hit test regions.
  virtual void GetHitTestMask(gfx::Path* mask) const;

  // Focus ---------------------------------------------------------------------

  // Override to be notified when focus has changed either to or from this View.
  virtual void OnFocus();
  virtual void OnBlur();

  // Handle view focus/blur events for this view.
  void Focus();
  void Blur();

  // System events -------------------------------------------------------------

  // Called when the UI theme (not the NativeTheme) has changed, overriding
  // allows individual Views to do special cleanup and processing (such as
  // dropping resource caches).  To dispatch a theme changed notification, call
  // Widget::ThemeChanged().
  virtual void OnThemeChanged() {}

  // Called when the locale has changed, overriding allows individual Views to
  // update locale-dependent strings.
  // To dispatch a locale changed notification, call Widget::LocaleChanged().
  virtual void OnLocaleChanged() {}

  // Tooltips ------------------------------------------------------------------

  // Views must invoke this when the tooltip text they are to display changes.
  void TooltipTextChanged();

  // Context menus -------------------------------------------------------------

  // Returns the location, in screen coordinates, to show the context menu at
  // when the context menu is shown from the keyboard. This implementation
  // returns the middle of the visible region of this view.
  //
  // This method is invoked when the context menu is shown by way of the
  // keyboard.
  virtual gfx::Point GetKeyboardContextMenuLocation();

  // Drag and drop -------------------------------------------------------------

  // These are cover methods that invoke the method of the same name on
  // the DragController. Subclasses may wish to override rather than install
  // a DragController.
  // See DragController for a description of these methods.
  virtual int GetDragOperations(const gfx::Point& press_pt);
  virtual void WriteDragData(const gfx::Point& press_pt, OSExchangeData* data);

  // Returns whether we're in the middle of a drag session that was initiated
  // by us.
  bool InDrag();

  // Returns how much the mouse needs to move in one direction to start a
  // drag. These methods cache in a platform-appropriate way. These values are
  // used by the public static method ExceededDragThreshold().
  static int GetHorizontalDragThreshold();
  static int GetVerticalDragThreshold();

  // NativeTheme ---------------------------------------------------------------

  // Invoked when the NativeTheme associated with this View changes.
  virtual void OnNativeThemeChanged(const ui::NativeTheme* theme) {}

  // Debugging -----------------------------------------------------------------

#if !defined(NDEBUG)
  // Returns string containing a graph of the views hierarchy in graphViz DOT
  // language (http://graphviz.org/). Can be called within debugger and save
  // to a file to compile/view.
  // Note: Assumes initial call made with first = true.
  virtual std::string PrintViewGraph(bool first);

  // Some classes may own an object which contains the children to displayed in
  // the views hierarchy. The above function gives the class the flexibility to
  // decide which object should be used to obtain the children, but this
  // function makes the decision explicit.
  std::string DoPrintViewGraph(bool first, View* view_with_children);
#endif

 private:
  friend class internal::RootView;
  friend class FocusManager;
  friend class Widget;

  // Used to track a drag. RootView passes this into
  // ProcessMousePressed/Dragged.
  struct DragInfo {
    // Sets possible_drag to false and start_x/y to 0. This is invoked by
    // RootView prior to invoke ProcessMousePressed.
    void Reset();

    // Sets possible_drag to true and start_pt to the specified point.
    // This is invoked by the target view if it detects the press may generate
    // a drag.
    void PossibleDrag(const gfx::Point& p);

    // Whether the press may generate a drag.
    bool possible_drag;

    // Coordinates of the mouse press.
    gfx::Point start_pt;
  };

  // Painting  -----------------------------------------------------------------

  enum SchedulePaintType {
    // Indicates the size is the same (only the origin changed).
    SCHEDULE_PAINT_SIZE_SAME,

    // Indicates the size changed (and possibly the origin).
    SCHEDULE_PAINT_SIZE_CHANGED
  };

  // Invoked before and after the bounds change to schedule painting the old and
  // new bounds.
  void SchedulePaintBoundsChanged(SchedulePaintType type);

  // Common Paint() code shared by accelerated and non-accelerated code paths to
  // invoke OnPaint() on the View.
  void PaintCommon(gfx::Canvas* canvas);

  // Tree operations -----------------------------------------------------------

  // Removes |view| from the hierarchy tree.  If |update_focus_cycle| is true,
  // the next and previous focusable views of views pointing to this view are
  // updated.  If |update_tool_tip| is true, the tooltip is updated.  If
  // |delete_removed_view| is true, the view is also deleted (if it is parent
  // owned).
  void DoRemoveChildView(View* view,
                         bool update_focus_cycle,
                         bool update_tool_tip,
                         bool delete_removed_view);

  // Call ViewHierarchyChanged for all child views on all parents
  void PropagateRemoveNotifications(View* parent);

  // Call ViewHierarchyChanged for all children
  void PropagateAddNotifications(View* parent, View* child);

  // Propagates NativeViewHierarchyChanged() notification through all the
  // children.
  void PropagateNativeViewHierarchyChanged(bool attached,
                                           gfx::NativeView native_view,
                                           internal::RootView* root_view);

  // Takes care of registering/unregistering accelerators if
  // |register_accelerators| true and calls ViewHierarchyChanged().
  void ViewHierarchyChangedImpl(bool register_accelerators,
                                bool is_add,
                                View* parent,
                                View* child);

  // Invokes OnNativeThemeChanged() on this and all descendants.
  void PropagateNativeThemeChanged(const ui::NativeTheme* theme);

  // Size and disposition ------------------------------------------------------

  // Call VisibilityChanged() recursively for all children.
  void PropagateVisibilityNotifications(View* from, bool is_visible);

  // Registers/unregisters accelerators as necessary and calls
  // VisibilityChanged().
  void VisibilityChangedImpl(View* starting_from, bool is_visible);

  // Responsible for propagating bounds change notifications to relevant
  // views.
  void BoundsChanged(const gfx::Rect& previous_bounds);

  // Visible bounds notification registration.
  // When a view is added to a hierarchy, it and all its children are asked if
  // they need to be registered for "visible bounds within root" notifications
  // (see comment on OnVisibleBoundsChanged()). If they do, they are registered
  // with every ancestor between them and the root of the hierarchy.
  static void RegisterChildrenForVisibleBoundsNotification(View* view);
  static void UnregisterChildrenForVisibleBoundsNotification(View* view);
  void RegisterForVisibleBoundsNotification();
  void UnregisterForVisibleBoundsNotification();

  // Adds/removes view to the list of descendants that are notified any time
  // this views location and possibly size are changed.
  void AddDescendantToNotify(View* view);
  void RemoveDescendantToNotify(View* view);

  // Sets the layer's bounds given in DIP coordinates.
  void SetLayerBounds(const gfx::Rect& bounds_in_dip);

  // Transformations -----------------------------------------------------------

  // Returns in |transform| the transform to get from coordinates of |ancestor|
  // to this. Returns true if |ancestor| is found. If |ancestor| is not found,
  // or NULL, |transform| is set to convert from root view coordinates to this.
  bool GetTransformRelativeTo(const View* ancestor,
                              gfx::Transform* transform) const;

  // Coordinate conversion -----------------------------------------------------

  // Convert a point in the view's coordinate to an ancestor view's coordinate
  // system using necessary transformations. Returns whether the point was
  // successfully converted to the ancestor's coordinate system.
  bool ConvertPointForAncestor(const View* ancestor, gfx::Point* point) const;

  // Convert a point in the ancestor's coordinate system to the view's
  // coordinate system using necessary transformations. Returns whether the
  // point was successfully from the ancestor's coordinate system to the view's
  // coordinate system.
  bool ConvertPointFromAncestor(const View* ancestor, gfx::Point* point) const;

  // Accelerated painting ------------------------------------------------------

  // Creates the layer and related fields for this view.
  void CreateLayer();

  // Parents all un-parented layers within this view's hierarchy to this view's
  // layer.
  void UpdateParentLayers();

  // Updates the view's layer's parent. Called when a view is added to a view
  // hierarchy, responsible for parenting the view's layer to the enclosing
  // layer in the hierarchy.
  void UpdateParentLayer();

  // Parents this view's layer to |parent_layer|, and sets its bounds and other
  // properties in accordance to |offset|, the view's offset from the
  // |parent_layer|.
  void ReparentLayer(const gfx::Vector2d& offset, ui::Layer* parent_layer);

  // Called to update the layer visibility. The layer will be visible if the
  // View itself, and all its parent Views are visible. This also updates
  // visibility of the child layers.
  void UpdateLayerVisibility();
  void UpdateChildLayerVisibility(bool visible);

  // Orphans the layers in this subtree that are parented to layers outside of
  // this subtree.
  void OrphanLayers();

  // Destroys the layer associated with this view, and reparents any descendants
  // to the destroyed layer's parent.
  void DestroyLayer();

  // Input ---------------------------------------------------------------------

  // RootView invokes these. These in turn invoke the appropriate OnMouseXXX
  // method. If a drag is detected, DoDrag is invoked.
  bool ProcessMousePressed(const ui::MouseEvent& event, DragInfo* drop_info);
  bool ProcessMouseDragged(const ui::MouseEvent& event, DragInfo* drop_info);
  void ProcessMouseReleased(const ui::MouseEvent& event);

  // RootView will invoke this with incoming TouchEvents. Returns the result
  // of OnTouchEvent.
  ui::EventResult ProcessTouchEvent(ui::TouchEvent* event);

  // RootView will invoke this with incoming GestureEvents. This will invoke
  // OnGestureEvent and return the result.
  ui::EventResult ProcessGestureEvent(ui::GestureEvent* event);

  // Accelerators --------------------------------------------------------------

  // Registers this view's keyboard accelerators that are not registered to
  // FocusManager yet, if possible.
  void RegisterPendingAccelerators();

  // Unregisters all the keyboard accelerators associated with this view.
  // |leave_data_intact| if true does not remove data from accelerators_ array,
  // so it could be re-registered with other focus manager
  void UnregisterAccelerators(bool leave_data_intact);

  // Focus ---------------------------------------------------------------------

  // Initialize the previous/next focusable views of the specified view relative
  // to the view at the specified index.
  void InitFocusSiblings(View* view, int index);

  // System events -------------------------------------------------------------

  // Used to propagate theme changed notifications from the root view to all
  // views in the hierarchy.
  virtual void PropagateThemeChanged();

  // Used to propagate locale changed notifications from the root view to all
  // views in the hierarchy.
  virtual void PropagateLocaleChanged();

  // Tooltips ------------------------------------------------------------------

  // Propagates UpdateTooltip() to the TooltipManager for the Widget.
  // This must be invoked any time the View hierarchy changes in such a way
  // the view under the mouse differs. For example, if the bounds of a View is
  // changed, this is invoked. Similarly, as Views are added/removed, this
  // is invoked.
  void UpdateTooltip();

  // Drag and drop -------------------------------------------------------------

  // Starts a drag and drop operation originating from this view. This invokes
  // WriteDragData to write the data and GetDragOperations to determine the
  // supported drag operations. When done, OnDragDone is invoked. |press_pt| is
  // in the view's coordinate system.
  // Returns true if a drag was started.
  bool DoDrag(const ui::LocatedEvent& event,
              const gfx::Point& press_pt,
              ui::DragDropTypes::DragEventSource source);

  //////////////////////////////////////////////////////////////////////////////

  // Creation and lifetime -----------------------------------------------------

  // False if this View is owned by its parent - i.e. it will be deleted by its
  // parent during its parents destruction. False is the default.
  bool owned_by_client_;

  // Attributes ----------------------------------------------------------------

  // The id of this View. Used to find this View.
  int id_;

  // The group of this view. Some view subclasses use this id to find other
  // views of the same group. For example radio button uses this information
  // to find other radio buttons.
  int group_;

  // Tree operations -----------------------------------------------------------

  // This view's parent.
  View* parent_;

  // This view's children.
  Views children_;

  // Size and disposition ------------------------------------------------------

  // This View's bounds in the parent coordinate system.
  gfx::Rect bounds_;

  // Whether this view is visible.
  bool visible_;

  // Whether this view is enabled.
  bool enabled_;

  // When this flag is on, a View receives a mouse-enter and mouse-leave event
  // even if a descendant View is the event-recipient for the real mouse
  // events. When this flag is turned on, and mouse moves from outside of the
  // view into a child view, both the child view and this view receives
  // mouse-enter event. Similarly, if the mouse moves from inside a child view
  // and out of this view, then both views receive a mouse-leave event.
  // When this flag is turned off, if the mouse moves from inside this view into
  // a child view, then this view receives a mouse-leave event. When this flag
  // is turned on, it does not receive the mouse-leave event in this case.
  // When the mouse moves from inside the child view out of the child view but
  // still into this view, this view receives a mouse-enter event if this flag
  // is turned off, but doesn't if this flag is turned on.
  // This flag is initialized to false.
  bool notify_enter_exit_on_child_;

  // Whether or not RegisterViewForVisibleBoundsNotification on the RootView
  // has been invoked.
  bool registered_for_visible_bounds_notification_;

  // List of descendants wanting notification when their visible bounds change.
  scoped_ptr<Views> descendants_to_notify_;

  // Transformations -----------------------------------------------------------

  // Clipping parameters. skia transformation matrix does not give us clipping.
  // So we do it ourselves.
  gfx::Insets clip_insets_;

  // Layout --------------------------------------------------------------------

  // Whether the view needs to be laid out.
  bool needs_layout_;

  // The View's LayoutManager defines the sizing heuristics applied to child
  // Views. The default is absolute positioning according to bounds_.
  scoped_ptr<LayoutManager> layout_manager_;

  // Painting ------------------------------------------------------------------

  // Background
  scoped_ptr<Background> background_;

  // Border.
  scoped_ptr<Border> border_;

  // Focus border.
  scoped_ptr<FocusBorder> focus_border_;

  // RTL painting --------------------------------------------------------------

  // Indicates whether or not the gfx::Canvas object passed to View::Paint()
  // is going to be flipped horizontally (using the appropriate transform) on
  // right-to-left locales for this View.
  bool flip_canvas_on_paint_for_rtl_ui_;

  // Accelerated painting ------------------------------------------------------

  bool paint_to_layer_;

  // Accelerators --------------------------------------------------------------

  // true if when we were added to hierarchy we were without focus manager
  // attempt addition when ancestor chain changed.
  bool accelerator_registration_delayed_;

  // Focus manager accelerators registered on.
  FocusManager* accelerator_focus_manager_;

  // The list of accelerators. List elements in the range
  // [0, registered_accelerator_count_) are already registered to FocusManager,
  // and the rest are not yet.
  scoped_ptr<std::vector<ui::Accelerator> > accelerators_;
  size_t registered_accelerator_count_;

  // Focus ---------------------------------------------------------------------

  // Next view to be focused when the Tab key is pressed.
  View* next_focusable_view_;

  // Next view to be focused when the Shift-Tab key combination is pressed.
  View* previous_focusable_view_;

  // Whether this view can be focused.
  bool focusable_;

  // Whether this view is focusable if the user requires full keyboard access,
  // even though it may not be normally focusable.
  bool accessibility_focusable_;

  // Context menus -------------------------------------------------------------

  // The menu controller.
  ContextMenuController* context_menu_controller_;

  // Drag and drop -------------------------------------------------------------

  DragController* drag_controller_;

  // Accessibility -------------------------------------------------------------

  // The Windows-specific accessibility implementation for this view.
#if defined(OS_WIN)
  base::win::ScopedComPtr<NativeViewAccessibilityWin>
      native_view_accessibility_win_;
#endif

  DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace views

#endif  // UI_VIEWS_VIEW_H_
