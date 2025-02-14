// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/widget/native_widget_aura.h"

#include "base/bind.h"
#include "base/string_util.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/aura/client/activation_change_observer.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/client/drag_drop_client.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/client/stacking_client.h"
#include "ui/aura/client/window_move_client.h"
#include "ui/aura/client/window_types.h"
#include "ui/aura/env.h"
#include "ui/aura/focus_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/events/event.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/screen.h"
#include "ui/native_theme/native_theme_aura.h"
#include "ui/views/drag_utils.h"
#include "ui/views/ime/input_method_bridge.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/drop_helper.h"
#include "ui/views/widget/native_widget_aura_window_observer.h"
#include "ui/views/widget/native_widget_delegate.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/tooltip_manager_aura.h"
#include "ui/views/widget/widget_aura_utils.h"
#include "ui/views/widget/widget_delegate.h"

#if defined(OS_WIN)
#include "base/win/scoped_gdi_object.h"
#include "base/win/win_util.h"
#include "ui/base/l10n/l10n_util_win.h"
#include "ui/views/widget/desktop_aura/desktop_root_window_host_win.h"
#endif

#if !defined(OS_CHROMEOS)
#include "ui/views/widget/desktop_aura/desktop_root_window_host.h"
#endif

namespace views {

namespace {

void SetRestoreBounds(aura::Window* window, const gfx::Rect& bounds) {
  window->SetProperty(aura::client::kRestoreBoundsKey, new gfx::Rect(bounds));
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, public:

NativeWidgetAura::NativeWidgetAura(internal::NativeWidgetDelegate* delegate)
    : delegate_(delegate),
      ALLOW_THIS_IN_INITIALIZER_LIST(window_(new aura::Window(this))),
      ownership_(Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET),
      ALLOW_THIS_IN_INITIALIZER_LIST(close_widget_factory_(this)),
      can_activate_(true),
      destroying_(false),
      cursor_(gfx::kNullCursor),
      saved_window_state_(ui::SHOW_STATE_DEFAULT) {
}

// static
gfx::Font NativeWidgetAura::GetWindowTitleFont() {
#if defined(OS_WIN)
  NONCLIENTMETRICS ncm;
  base::win::GetNonClientMetrics(&ncm);
  l10n_util::AdjustUIFont(&(ncm.lfCaptionFont));
  base::win::ScopedHFONT caption_font(CreateFontIndirect(&(ncm.lfCaptionFont)));
  return gfx::Font(caption_font);
#else
  return gfx::Font();
#endif
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, internal::NativeWidgetPrivate implementation:

void NativeWidgetAura::InitNativeWidget(const Widget::InitParams& params) {
  ownership_ = params.ownership;

  window_->set_user_data(this);
  window_->SetType(GetAuraWindowTypeForWidgetType(params.type));
  window_->SetProperty(aura::client::kShowStateKey, params.show_state);
  if (params.type == Widget::InitParams::TYPE_BUBBLE)
    aura::client::SetHideOnDeactivate(window_, true);
  window_->SetTransparent(params.transparent);
  window_->Init(params.layer_type);
  if (params.type == Widget::InitParams::TYPE_CONTROL)
    window_->Show();

  delegate_->OnNativeWidgetCreated();

  gfx::Rect window_bounds = params.bounds;
  if (params.child) {
    window_->SetParent(params.GetParent());
  } else {
    // Set up the transient child before the window is added. This way the
    // LayoutManager knows the window has a transient parent.
    gfx::NativeView parent = params.GetParent();
    if (parent && parent->type() != aura::client::WINDOW_TYPE_UNKNOWN) {
      parent->AddTransientChild(window_);
      parent = NULL;
    }
    // SetAlwaysOnTop before SetParent so that always-on-top container is used.
    SetAlwaysOnTop(params.keep_on_top);
    // If the parent is not specified, find the default parent for
    // the |window_| using the desired |window_bounds|.
    if (!parent) {
      parent = aura::client::GetStackingClient(params.GetParent())->
          GetDefaultParent(params.context, window_, window_bounds);
    } else if (window_bounds == gfx::Rect()) {
      // If a parent is specified but no bounds are given,
      // use the origin of the parent's display so that the widget
      // will be added to the same display as the parent.
      gfx::Rect bounds = gfx::Screen::GetScreenFor(parent)->
          GetDisplayNearestWindow(parent).bounds();
      window_bounds.set_origin(bounds.origin());
    }
    window_->SetParent(parent);
  }

  // Wait to set the bounds until we have a parent. That way we can know our
  // true state/bounds (the LayoutManager may enforce a particular
  // state/bounds).
  if (IsMaximized())
    SetRestoreBounds(window_, window_bounds);
  else
    SetBounds(window_bounds);
  window_->set_ignore_events(!params.accept_events);
  can_activate_ =
      params.can_activate && params.type != Widget::InitParams::TYPE_CONTROL;
  DCHECK(GetWidget()->GetRootView());
#if !defined(OS_MACOSX)
  if (params.type != Widget::InitParams::TYPE_TOOLTIP)
    tooltip_manager_.reset(new views::TooltipManagerAura(this));
#endif  // !defined(OS_MACOSX)

  drop_helper_.reset(new DropHelper(GetWidget()->GetRootView()));
  if (params.type != Widget::InitParams::TYPE_TOOLTIP &&
      params.type != Widget::InitParams::TYPE_POPUP) {
    aura::client::SetDragDropDelegate(window_, this);
  }

  aura::client::SetActivationDelegate(window_, this);

  window_->SetProperty(aura::client::kCanMaximizeKey,
                       GetWidget()->widget_delegate()->CanMaximize());
  window_->SetProperty(aura::client::kCanResizeKey,
                       GetWidget()->widget_delegate()->CanResize());
}

NonClientFrameView* NativeWidgetAura::CreateNonClientFrameView() {
  return NULL;
}

bool NativeWidgetAura::ShouldUseNativeFrame() const {
  // There is only one frame type for aura.
  return false;
}

void NativeWidgetAura::FrameTypeChanged() {
  // This is called when the Theme has changed; forward the event to the root
  // widget.
  GetWidget()->ThemeChanged();
  GetWidget()->GetRootView()->SchedulePaint();
}

Widget* NativeWidgetAura::GetWidget() {
  return delegate_->AsWidget();
}

const Widget* NativeWidgetAura::GetWidget() const {
  return delegate_->AsWidget();
}

gfx::NativeView NativeWidgetAura::GetNativeView() const {
  return window_;
}

gfx::NativeWindow NativeWidgetAura::GetNativeWindow() const {
  return window_;
}

Widget* NativeWidgetAura::GetTopLevelWidget() {
  NativeWidgetPrivate* native_widget = GetTopLevelNativeWidget(GetNativeView());
  return native_widget ? native_widget->GetWidget() : NULL;
}

const ui::Compositor* NativeWidgetAura::GetCompositor() const {
  return window_->layer()->GetCompositor();
}

ui::Compositor* NativeWidgetAura::GetCompositor() {
  return window_->layer()->GetCompositor();
}

gfx::Vector2d NativeWidgetAura::CalculateOffsetToAncestorWithLayer(
    ui::Layer** layer_parent) {
  if (layer_parent)
    *layer_parent = window_->layer();
  return gfx::Vector2d();
}

void NativeWidgetAura::ViewRemoved(View* view) {
  DCHECK(drop_helper_.get() != NULL);
  drop_helper_->ResetTargetViewIfEquals(view);
}

void NativeWidgetAura::SetNativeWindowProperty(const char* name, void* value) {
  if (window_)
    window_->SetNativeWindowProperty(name, value);
}

void* NativeWidgetAura::GetNativeWindowProperty(const char* name) const {
  return window_ ? window_->GetNativeWindowProperty(name) : NULL;
}

TooltipManager* NativeWidgetAura::GetTooltipManager() const {
  return tooltip_manager_.get();
}

bool NativeWidgetAura::IsScreenReaderActive() const {
  // http://crbug.com/102570
  // NOTIMPLEMENTED();
  return false;
}

void NativeWidgetAura::SendNativeAccessibilityEvent(
    View* view,
    ui::AccessibilityTypes::Event event_type) {
  // http://crbug.com/102570
  // NOTIMPLEMENTED();
}

void NativeWidgetAura::SetCapture() {
  window_->SetCapture();
}

void NativeWidgetAura::ReleaseCapture() {
  window_->ReleaseCapture();
}

bool NativeWidgetAura::HasCapture() const {
  return window_->HasCapture();
}

InputMethod* NativeWidgetAura::CreateInputMethod() {
  aura::RootWindow* root_window = window_->GetRootWindow();
  ui::InputMethod* host =
      root_window->GetProperty(aura::client::kRootWindowInputMethodKey);
  return new InputMethodBridge(this, host);
}

internal::InputMethodDelegate* NativeWidgetAura::GetInputMethodDelegate() {
  return this;
}

void NativeWidgetAura::CenterWindow(const gfx::Size& size) {
  gfx::Rect parent_bounds(window_->parent()->GetBoundsInRootWindow());
  // When centering window, we take the intersection of the host and
  // the parent. We assume the root window represents the visible
  // rect of a single screen.
  gfx::Rect work_area = gfx::Screen::GetScreenFor(window_)->
      GetDisplayNearestWindow(window_).work_area();

  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(window_->GetRootWindow());
  if (screen_position_client) {
    gfx::Point origin = work_area.origin();
    screen_position_client->ConvertPointFromScreen(window_->GetRootWindow(),
                                                   &origin);
    work_area.set_origin(origin);
  }

  parent_bounds.Intersect(work_area);

  // If |window_|'s transient parent's bounds are big enough to fit it, then we
  // center it with respect to the transient parent.
  if (window_->transient_parent()) {
    gfx::Rect transient_parent_rect = window_->transient_parent()->
        GetBoundsInRootWindow();
    transient_parent_rect.Intersect(work_area);
    if (transient_parent_rect.height() >= size.height() &&
        transient_parent_rect.width() >= size.width())
      parent_bounds = transient_parent_rect;
  }

  gfx::Rect window_bounds(
      parent_bounds.x() + (parent_bounds.width() - size.width()) / 2,
      parent_bounds.y() + (parent_bounds.height() - size.height()) / 2,
      size.width(),
      size.height());
  // Don't size the window bigger than the parent, otherwise the user may not be
  // able to close or move it.
  window_bounds.AdjustToFit(parent_bounds);

  // Convert the bounds back relative to the parent.
  gfx::Point origin = window_bounds.origin();
  aura::Window::ConvertPointToTarget(window_->GetRootWindow(),
      window_->parent(), &origin);
  window_bounds.set_origin(origin);
  window_->SetBounds(window_bounds);
}

void NativeWidgetAura::GetWindowPlacement(
    gfx::Rect* bounds,
    ui::WindowShowState* show_state) const {
  // The interface specifies returning restored bounds, not current bounds.
  *bounds = GetRestoredBounds();
  *show_state = window_->GetProperty(aura::client::kShowStateKey);
}

void NativeWidgetAura::SetWindowTitle(const string16& title) {
  window_->set_title(title);
}

void NativeWidgetAura::SetWindowIcons(const gfx::ImageSkia& window_icon,
                                      const gfx::ImageSkia& app_icon) {
  // Aura doesn't have window icons.
}

void NativeWidgetAura::SetAccessibleName(const string16& name) {
  // http://crbug.com/102570
  // NOTIMPLEMENTED();
}

void NativeWidgetAura::SetAccessibleRole(ui::AccessibilityTypes::Role role) {
  // http://crbug.com/102570
  // NOTIMPLEMENTED();
}

void NativeWidgetAura::SetAccessibleState(ui::AccessibilityTypes::State state) {
  // http://crbug.com/102570
  // NOTIMPLEMENTED();
}

void NativeWidgetAura::InitModalType(ui::ModalType modal_type) {
  if (modal_type != ui::MODAL_TYPE_NONE)
    window_->SetProperty(aura::client::kModalKey, modal_type);
}

gfx::Rect NativeWidgetAura::GetWindowBoundsInScreen() const {
  return window_->GetBoundsInScreen();
}

gfx::Rect NativeWidgetAura::GetClientAreaBoundsInScreen() const {
  // View-to-screen coordinate system transformations depend on this returning
  // the full window bounds, for example View::ConvertPointToScreen().
  return window_->GetBoundsInScreen();
}

gfx::Rect NativeWidgetAura::GetRestoredBounds() const {
  // Restored bounds should only be relevant if the window is minimized or
  // maximized. However, in some places the code expects GetRestoredBounds()
  // to return the current window bounds if the window is not in either state.
  if (IsMinimized() || IsMaximized() || IsFullscreen()) {
    // Restore bounds are in screen coordinates, no need to convert.
    gfx::Rect* restore_bounds =
        window_->GetProperty(aura::client::kRestoreBoundsKey);
    if (restore_bounds)
      return *restore_bounds;
  }
  return window_->GetBoundsInScreen();
}

void NativeWidgetAura::SetBounds(const gfx::Rect& bounds) {
  aura::RootWindow* root = window_->GetRootWindow();
  if (root) {
    aura::client::ScreenPositionClient* screen_position_client =
        aura::client::GetScreenPositionClient(root);
    if (screen_position_client) {
      gfx::Display dst_display =
          gfx::Screen::GetScreenFor(window_)->GetDisplayMatching(bounds);
      screen_position_client->SetBounds(window_, bounds, dst_display);
      return;
    }
  }
  window_->SetBounds(bounds);
}

void NativeWidgetAura::SetSize(const gfx::Size& size) {
  window_->SetBounds(gfx::Rect(window_->bounds().origin(), size));
}

void NativeWidgetAura::StackAbove(gfx::NativeView native_view) {
  if (window_->parent() && window_->parent() == native_view->parent())
    window_->parent()->StackChildAbove(window_, native_view);
}

void NativeWidgetAura::StackAtTop() {
  window_->parent()->StackChildAtTop(window_);
}

void NativeWidgetAura::StackBelow(gfx::NativeView native_view) {
  if (window_->parent() && window_->parent() == native_view->parent())
    window_->parent()->StackChildBelow(window_, native_view);
}

void NativeWidgetAura::SetShape(gfx::NativeRegion region) {
  // No need for this. Just delete and ignore.
  delete region;
}

void NativeWidgetAura::Close() {
  // |window_| may already be deleted by parent window. This can happen
  // when this widget is child widget or has transient parent
  // and ownership is WIDGET_OWNS_NATIVE_WIDGET.
  DCHECK(window_ ||
         ownership_ == Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  if (window_) {
    window_->SuppressPaint();
    Hide();
    window_->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_NONE);
  }

  if (!close_widget_factory_.HasWeakPtrs()) {
    MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&NativeWidgetAura::CloseNow,
                   close_widget_factory_.GetWeakPtr()));
  }
}

void NativeWidgetAura::CloseNow() {
  delete window_;
}

void NativeWidgetAura::Show() {
  ShowWithWindowState(ui::SHOW_STATE_INACTIVE);
}

void NativeWidgetAura::Hide() {
  window_->Hide();
}

void NativeWidgetAura::ShowMaximizedWithBounds(
    const gfx::Rect& restored_bounds) {
  ShowWithWindowState(ui::SHOW_STATE_MAXIMIZED);
  SetRestoreBounds(window_, restored_bounds);
}

void NativeWidgetAura::ShowWithWindowState(ui::WindowShowState state) {
  if (state == ui::SHOW_STATE_MAXIMIZED || state == ui::SHOW_STATE_FULLSCREEN)
    window_->SetProperty(aura::client::kShowStateKey, state);
  window_->Show();
  if (can_activate_) {
    if (state != ui::SHOW_STATE_INACTIVE)
      Activate();
    // SetInitialFocus() should be always be called, even for
    // SHOW_STATE_INACTIVE. When a frameless modal dialog is created by
    // a widget of TYPE_WINDOW_FRAMELESS, Widget::Show() will call into
    // this function with the window state SHOW_STATE_INACTIVE,
    // SetInitialFoucs() has to be called so that the dialog can get focus.
    // This also matches NativeWidgetWin which invokes SetInitialFocus
    // regardless of show state.
    SetInitialFocus();
  }
}

bool NativeWidgetAura::IsVisible() const {
  return window_->IsVisible();
}

void NativeWidgetAura::Activate() {
  // We don't necessarily have a root window yet. This can happen with
  // constrained windows.
  if (window_->GetRootWindow())
    aura::client::GetActivationClient(window_->GetRootWindow())->ActivateWindow(
        window_);
}

void NativeWidgetAura::Deactivate() {
  aura::client::GetActivationClient(window_->GetRootWindow())->DeactivateWindow(
      window_);
}

bool NativeWidgetAura::IsActive() const {
  return aura::client::GetActivationClient(window_->GetRootWindow())->
      GetActiveWindow() == window_;
}

void NativeWidgetAura::SetAlwaysOnTop(bool on_top) {
  window_->SetProperty(aura::client::kAlwaysOnTopKey, on_top);
}

void NativeWidgetAura::Maximize() {
  window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
}

void NativeWidgetAura::Minimize() {
  window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
}

bool NativeWidgetAura::IsMaximized() const {
  return window_->GetProperty(aura::client::kShowStateKey) ==
      ui::SHOW_STATE_MAXIMIZED;
}

bool NativeWidgetAura::IsMinimized() const {
  return window_->GetProperty(aura::client::kShowStateKey) ==
      ui::SHOW_STATE_MINIMIZED;
}

void NativeWidgetAura::Restore() {
  window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
}

void NativeWidgetAura::SetFullscreen(bool fullscreen) {
  if (IsFullscreen() == fullscreen)
    return;  // Nothing to do.

  // Save window state before entering full screen so that it could restored
  // when exiting full screen.
  if (fullscreen)
    saved_window_state_ = window_->GetProperty(aura::client::kShowStateKey);

  window_->SetProperty(
      aura::client::kShowStateKey,
      fullscreen ? ui::SHOW_STATE_FULLSCREEN : saved_window_state_);
}

bool NativeWidgetAura::IsFullscreen() const {
  return window_->GetProperty(aura::client::kShowStateKey) ==
      ui::SHOW_STATE_FULLSCREEN;
}

void NativeWidgetAura::SetOpacity(unsigned char opacity) {
  window_->layer()->SetOpacity(opacity / 255.0);
}

void NativeWidgetAura::SetUseDragFrame(bool use_drag_frame) {
  NOTIMPLEMENTED();
}

void NativeWidgetAura::FlashFrame(bool flash) {
  window_->SetProperty(aura::client::kDrawAttentionKey, flash);
}

bool NativeWidgetAura::IsAccessibleWidget() const {
  // http://crbug.com/102570
  // NOTIMPLEMENTED();
  return false;
}

void NativeWidgetAura::RunShellDrag(View* view,
                                    const ui::OSExchangeData& data,
                                    const gfx::Point& location,
                                    int operation,
                                    ui::DragDropTypes::DragEventSource source) {
  views::RunShellDrag(window_, data, location, operation, source);
}

void NativeWidgetAura::SchedulePaintInRect(const gfx::Rect& rect) {
  if (window_)
    window_->SchedulePaintInRect(rect);
}

void NativeWidgetAura::SetCursor(gfx::NativeCursor cursor) {
  cursor_ = cursor;
  aura::client::CursorClient* cursor_client =
      aura::client::GetCursorClient(window_->GetRootWindow());
  if (cursor_client)
    cursor_client->SetCursor(cursor);
}

void NativeWidgetAura::ClearNativeFocus() {
  if (window_ && window_->GetFocusManager() &&
      window_->Contains(window_->GetFocusManager()->GetFocusedWindow()))
    window_->GetFocusManager()->SetFocusedWindow(window_, NULL);
}

gfx::Rect NativeWidgetAura::GetWorkAreaBoundsInScreen() const {
  return gfx::Screen::GetScreenFor(GetNativeView())->
      GetDisplayNearestWindow(GetNativeView()).work_area();
}

void NativeWidgetAura::SetInactiveRenderingDisabled(bool value) {
  if (!value) {
    active_window_observer_.reset();
  } else {
    active_window_observer_.reset(
        new NativeWidgetAuraWindowObserver(window_, delegate_));
  }
}

Widget::MoveLoopResult NativeWidgetAura::RunMoveLoop(
    const gfx::Vector2d& drag_offset) {
  if (window_->parent() &&
      aura::client::GetWindowMoveClient(window_->parent())) {
    SetCapture();
    if (aura::client::GetWindowMoveClient(window_->parent())->RunMoveLoop(
            window_, drag_offset) == aura::client::MOVE_SUCCESSFUL) {
      return Widget::MOVE_LOOP_SUCCESSFUL;
    }
  }
  return Widget::MOVE_LOOP_CANCELED;
}

void NativeWidgetAura::EndMoveLoop() {
  if (window_->parent() &&
      aura::client::GetWindowMoveClient(window_->parent())) {
    aura::client::GetWindowMoveClient(window_->parent())->EndMoveLoop();
  }
}

void NativeWidgetAura::SetVisibilityChangedAnimationsEnabled(bool value) {
  window_->SetProperty(aura::client::kAnimationsDisabledKey, !value);
}

ui::NativeTheme* NativeWidgetAura::GetNativeTheme() const {
#if !defined(OS_CHROMEOS)
  return DesktopRootWindowHost::GetNativeTheme(window_);
#endif
  return ui::NativeThemeAura::instance();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, views::InputMethodDelegate implementation:

void NativeWidgetAura::DispatchKeyEventPostIME(const ui::KeyEvent& key) {
  FocusManager* focus_manager = GetWidget()->GetFocusManager();
  if (delegate_->OnKeyEvent(key) || !focus_manager)
    return;
  focus_manager->OnKeyEvent(key);
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, aura::WindowDelegate implementation:

gfx::Size NativeWidgetAura::GetMinimumSize() const {
  return delegate_->GetMinimumSize();
}

void NativeWidgetAura::OnBoundsChanged(const gfx::Rect& old_bounds,
                                       const gfx::Rect& new_bounds) {
  if (old_bounds.origin() != new_bounds.origin())
    delegate_->OnNativeWidgetMove();
  if (old_bounds.size() != new_bounds.size())
    delegate_->OnNativeWidgetSizeChanged(new_bounds.size());
}

void NativeWidgetAura::OnFocus(aura::Window* old_focused_window) {
  // In aura, it is possible for child native widgets to take input and focus,
  // this differs from the behavior on windows.
  GetWidget()->GetInputMethod()->OnFocus();
  delegate_->OnNativeFocus(old_focused_window);
}

void NativeWidgetAura::OnBlur() {
  // GetInputMethod() recreates the input method if it's previously been
  // destroyed.  If we get called during destruction, the input method will be
  // gone, and creating a new one and telling it that we lost the focus will
  // trigger a DCHECK (the new input method doesn't think that we have the focus
  // and doesn't expect a blur).  OnBlur() shouldn't be called during
  // destruction unless WIDGET_OWNS_NATIVE_WIDGET is set (which is just the case
  // in tests).
  if (!destroying_)
    GetWidget()->GetInputMethod()->OnBlur();
  else
    DCHECK_EQ(ownership_, Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);

  delegate_->OnNativeBlur(window_->GetFocusManager()->GetFocusedWindow());
}

gfx::NativeCursor NativeWidgetAura::GetCursor(const gfx::Point& point) {
  return cursor_;
}

int NativeWidgetAura::GetNonClientComponent(const gfx::Point& point) const {
  return delegate_->GetNonClientComponent(point);
}

bool NativeWidgetAura::ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) {
  views::WidgetDelegate* widget_delegate = GetWidget()->widget_delegate();
  if (widget_delegate &&
      !widget_delegate->ShouldDescendIntoChildForEventHandling(child, location))
    return false;

  // Don't descend into |child| if there is a view with a Layer that contains
  // the point and is stacked above |child|s layer.
  typedef std::vector<ui::Layer*> Layers;
  const Layers& root_layers(delegate_->GetRootLayers());
  if (root_layers.empty())
    return true;

  Layers::const_iterator child_layer_iter(
      std::find(window_->layer()->children().begin(),
                window_->layer()->children().end(), child->layer()));
  if (child_layer_iter == window_->layer()->children().end())
    return true;

  for (std::vector<ui::Layer*>::const_reverse_iterator i = root_layers.rbegin();
       i != root_layers.rend(); ++i) {
    ui::Layer* layer = *i;
    if (layer->visible() && layer->bounds().Contains(location)) {
      Layers::const_iterator root_layer_iter(
          std::find(window_->layer()->children().begin(),
                    window_->layer()->children().end(), layer));
      if (root_layer_iter > child_layer_iter)
        return false;
    }
  }
  return true;
}

bool NativeWidgetAura::CanFocus() {
  return can_activate_;
}

void NativeWidgetAura::OnCaptureLost() {
  delegate_->OnMouseCaptureLost();
}

void NativeWidgetAura::OnPaint(gfx::Canvas* canvas) {
  delegate_->OnNativeWidgetPaint(canvas);
}

void NativeWidgetAura::OnDeviceScaleFactorChanged(float device_scale_factor) {
  // Repainting with new scale factor will paint the content at the right scale.
}

void NativeWidgetAura::OnWindowDestroying() {
  delegate_->OnNativeWidgetDestroying();

  // If the aura::Window is destroyed, we can no longer show tooltips.
  tooltip_manager_.reset();
}

void NativeWidgetAura::OnWindowDestroyed() {
  window_ = NULL;
  tooltip_manager_.reset();
  delegate_->OnNativeWidgetDestroyed();
  if (ownership_ == Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET)
    delete this;
}

void NativeWidgetAura::OnWindowTargetVisibilityChanged(bool visible) {
  delegate_->OnNativeWidgetVisibilityChanged(visible);
}

bool NativeWidgetAura::HasHitTestMask() const {
  return delegate_->HasHitTestMask();
}

void NativeWidgetAura::GetHitTestMask(gfx::Path* mask) const {
  DCHECK(mask);
  delegate_->GetHitTestMask(mask);
}

scoped_refptr<ui::Texture> NativeWidgetAura::CopyTexture() {
  // The layer we create doesn't have an external texture, so this should never
  // get invoked.
  NOTREACHED();
  return scoped_refptr<ui::Texture>();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, ui::EventHandler implementation:

ui::EventResult NativeWidgetAura::OnKeyEvent(ui::KeyEvent* event) {
  if (event->is_char()) {
    // If a ui::InputMethod object is attached to the root window, character
    // events are handled inside the object and are not passed to this function.
    // If such object is not attached, character events might be sent (e.g. on
    // Windows). In this case, we just skip these.
    return ui::ER_UNHANDLED;
  }
  // Renderer may send a key event back to us if the key event wasn't handled,
  // and the window may be invisible by that time.
  if (!window_->IsVisible())
    return ui::ER_UNHANDLED;
#if defined(OS_WIN)
  // Work around for incomplete InputMethod wiring. If we're in a constrained
  // window DispatchKeyEvent below results in an infinite loop. Short circuit
  // that by invoking DispatchKeyEventIME() directly, which is what
  // InputMethodBridge::DispatchKeyEvent does.
  // TODO(ime): remove this hack.
  if (DesktopRootWindowHostWin::GetContentWindowForHWND(
          window_->GetRootWindow()->GetAcceleratedWidget()) &&
      (event->type() != ui::ET_TRANSLATED_KEY_PRESS &&
       event->type() != ui::ET_TRANSLATED_KEY_RELEASE)) {
    DispatchKeyEventPostIME(*event);
    return ui::ER_HANDLED;
  }
#endif
  GetWidget()->GetInputMethod()->DispatchKeyEvent(*event);
  return ui::ER_HANDLED;
}

ui::EventResult NativeWidgetAura::OnMouseEvent(ui::MouseEvent* event) {
  DCHECK(window_->IsVisible());
  if (event->type() == ui::ET_MOUSEWHEEL)
    return delegate_->OnMouseEvent(*event) ? ui::ER_HANDLED : ui::ER_UNHANDLED;

  if (tooltip_manager_.get())
    tooltip_manager_->UpdateTooltip();
  return delegate_->OnMouseEvent(*event) ? ui::ER_HANDLED : ui::ER_UNHANDLED;
}

ui::EventResult NativeWidgetAura::OnScrollEvent(ui::ScrollEvent* event) {
  if (event->type() == ui::ET_SCROLL) {
    ui::EventResult status = delegate_->OnScrollEvent(event);
    if (status != ui::ER_UNHANDLED)
      return status;

    // Convert unprocessed scroll events into wheel events.
    ui::MouseWheelEvent mwe(*static_cast<ui::ScrollEvent*>(event));
    return delegate_->OnMouseEvent(mwe) ? ui::ER_HANDLED : ui::ER_UNHANDLED;
  }
  return delegate_->OnScrollEvent(event);
}

ui::EventResult NativeWidgetAura::OnTouchEvent(ui::TouchEvent* event) {
  DCHECK(window_->IsVisible());
  return delegate_->OnTouchEvent(event);
}

ui::EventResult NativeWidgetAura::OnGestureEvent(ui::GestureEvent* event) {
  DCHECK(window_->IsVisible());
  return delegate_->OnGestureEvent(event);
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, aura::ActivationDelegate implementation:

bool NativeWidgetAura::ShouldActivate(const ui::Event* event) {
  return can_activate_ && delegate_->CanActivate();
}

void NativeWidgetAura::OnActivated() {
  if (GetWidget()->HasFocusManager())
    GetWidget()->GetFocusManager()->RestoreFocusedView();
  delegate_->OnNativeWidgetActivationChanged(true);
  if (IsVisible() && GetWidget()->non_client_view())
    GetWidget()->non_client_view()->SchedulePaint();
}

void NativeWidgetAura::OnLostActive() {
  if (GetWidget()->HasFocusManager())
    GetWidget()->GetFocusManager()->StoreFocusedView();
  delegate_->OnNativeWidgetActivationChanged(false);
  if (IsVisible() && GetWidget()->non_client_view())
    GetWidget()->non_client_view()->SchedulePaint();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, aura::WindowDragDropDelegate implementation:

void NativeWidgetAura::OnDragEntered(const ui::DropTargetEvent& event) {
  DCHECK(drop_helper_.get() != NULL);
  last_drop_operation_ = drop_helper_->OnDragOver(event.data(),
      event.location(), event.source_operations());
}

int NativeWidgetAura::OnDragUpdated(const ui::DropTargetEvent& event) {
  DCHECK(drop_helper_.get() != NULL);
  last_drop_operation_ = drop_helper_->OnDragOver(event.data(),
      event.location(), event.source_operations());
  return last_drop_operation_;
}

void NativeWidgetAura::OnDragExited() {
  DCHECK(drop_helper_.get() != NULL);
  drop_helper_->OnDragExit();
}

int NativeWidgetAura::OnPerformDrop(const ui::DropTargetEvent& event) {
  DCHECK(drop_helper_.get() != NULL);
  return drop_helper_->OnDrop(event.data(), event.location(),
      last_drop_operation_);
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, protected:

NativeWidgetAura::~NativeWidgetAura() {
  destroying_ = true;
  if (ownership_ == Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET)
    delete delegate_;
  else
    CloseNow();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, private:

void NativeWidgetAura::SetInitialFocus() {
  // The window does not get keyboard messages unless we focus it.
  if (!GetWidget()->SetInitialFocus())
    window_->Focus();
}

////////////////////////////////////////////////////////////////////////////////
// Widget, public:

// static
void Widget::NotifyLocaleChanged() {
  // Deliberately not implemented.
}

#if defined(OS_WIN)
namespace {
BOOL CALLBACK WindowCallbackProc(HWND hwnd, LPARAM lParam) {
  aura::Window* root_window =
      DesktopRootWindowHostWin::GetContentWindowForHWND(hwnd);
  if (root_window) {
    Widget* widget = Widget::GetWidgetForNativeView(root_window);
    if (widget && widget->is_secondary_widget())
      widget->Close();
  }
  return TRUE;
}
}  // namespace
#endif

// static
void Widget::CloseAllSecondaryWidgets() {
#if defined(OS_WIN)
  EnumThreadWindows(GetCurrentThreadId(), WindowCallbackProc, 0);
#endif
}

bool Widget::ConvertRect(const Widget* source,
                         const Widget* target,
                         gfx::Rect* rect) {
  return false;
}

namespace internal {

////////////////////////////////////////////////////////////////////////////////
// internal::NativeWidgetPrivate, public:

// static
NativeWidgetPrivate* NativeWidgetPrivate::CreateNativeWidget(
    internal::NativeWidgetDelegate* delegate) {
  return new NativeWidgetAura(delegate);
}

// static
NativeWidgetPrivate* NativeWidgetPrivate::GetNativeWidgetForNativeView(
    gfx::NativeView native_view) {
  return reinterpret_cast<NativeWidgetPrivate*>(native_view->user_data());
}

// static
NativeWidgetPrivate* NativeWidgetPrivate::GetNativeWidgetForNativeWindow(
    gfx::NativeWindow native_window) {
  return reinterpret_cast<NativeWidgetPrivate*>(native_window->user_data());
}

// static
NativeWidgetPrivate* NativeWidgetPrivate::GetTopLevelNativeWidget(
    gfx::NativeView native_view) {
  aura::Window* window = native_view;
  NativeWidgetPrivate* top_level_native_widget = NULL;
  while (window) {
    NativeWidgetPrivate* native_widget = GetNativeWidgetForNativeView(window);
    if (native_widget)
      top_level_native_widget = native_widget;
    window = window->parent();
  }
  return top_level_native_widget;
}

// static
void NativeWidgetPrivate::GetAllChildWidgets(gfx::NativeView native_view,
                                             Widget::Widgets* children) {
  {
    // Code expects widget for |native_view| to be added to |children|.
    NativeWidgetPrivate* native_widget = static_cast<NativeWidgetPrivate*>(
        GetNativeWidgetForNativeView(native_view));
    if (native_widget && native_widget->GetWidget())
      children->insert(native_widget->GetWidget());
  }

  const aura::Window::Windows& child_windows = native_view->children();
  for (aura::Window::Windows::const_iterator i = child_windows.begin();
       i != child_windows.end(); ++i) {
    NativeWidgetAura* native_widget =
        static_cast<NativeWidgetAura*>(GetNativeWidgetForNativeView(*i));
    if (native_widget)
      children->insert(native_widget->GetWidget());
  }
}

// static
void NativeWidgetPrivate::ReparentNativeView(gfx::NativeView native_view,
                                             gfx::NativeView new_parent) {
  DCHECK(native_view != new_parent);

  gfx::NativeView previous_parent = native_view->parent();
  if (previous_parent == new_parent)
    return;

  Widget::Widgets widgets;
  GetAllChildWidgets(native_view, &widgets);

  // First notify all the widgets that they are being disassociated
  // from their previous parent.
  for (Widget::Widgets::iterator it = widgets.begin();
      it != widgets.end(); ++it) {
    (*it)->NotifyNativeViewHierarchyChanged(false, previous_parent);
  }

  native_view->SetParent(new_parent);

  // And now, notify them that they have a brand new parent.
  for (Widget::Widgets::iterator it = widgets.begin();
      it != widgets.end(); ++it) {
    (*it)->NotifyNativeViewHierarchyChanged(true, new_parent);
  }
}

// static
bool NativeWidgetPrivate::IsMouseButtonDown() {
  return aura::Env::GetInstance()->is_mouse_button_down();
}

// static
bool NativeWidgetPrivate::IsTouchDown() {
  return aura::Env::GetInstance()->is_touch_down();
}

}  // namespace internal
}  // namespace views
