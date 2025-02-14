// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "win8/metro_driver/stdafx.h"
#include "win8/metro_driver/chrome_app_view_ash.h"

#include <windows.foundation.h>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/threading/thread.h"
#include "base/win/metro.h"
#include "base/win/win_util.h"
#include "chrome/common/chrome_switches.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_channel_proxy.h"
#include "ipc/ipc_sender.h"
#include "ui/base/events/event_constants.h"
#include "ui/metro_viewer/metro_viewer_messages.h"
#include "win8/metro_driver/metro_driver.h"
#include "win8/metro_driver/winrt_utils.h"

typedef winfoundtn::ITypedEventHandler<
    winapp::Core::CoreApplicationView*,
    winapp::Activation::IActivatedEventArgs*> ActivatedHandler;

typedef winfoundtn::ITypedEventHandler<
    winui::Core::CoreWindow*,
    winui::Core::PointerEventArgs*> PointerEventHandler;

typedef winfoundtn::ITypedEventHandler<
    winui::Core::CoreWindow*,
    winui::Core::KeyEventArgs*> KeyEventHandler;

typedef winfoundtn::ITypedEventHandler<
    winui::Core::CoreWindow*,
    winui::Core::CharacterReceivedEventArgs*> CharEventHandler;

typedef winfoundtn::ITypedEventHandler<
    winui::Core::CoreWindow*,
    winui::Core::VisibilityChangedEventArgs*> VisibilityChangedHandler;

// This function is exported by chrome.exe.
typedef int (__cdecl *BreakpadExceptionHandler)(EXCEPTION_POINTERS* info);

// Global information used across the metro driver.
struct Globals {
  LPTHREAD_START_ROUTINE host_main;
  HWND core_window;
  DWORD main_thread_id;
  winapp::Activation::ApplicationExecutionState previous_state;
  winapp::Core::ICoreApplicationExit* app_exit;
  BreakpadExceptionHandler breakpad_exception_handler;
} globals;

namespace {

// TODO(robertshield): Share this with chrome_app_view.cc
void MetroExit() {
  globals.app_exit->Exit();
  globals.core_window = NULL;
}

class ChromeChannelListener : public IPC::Listener {
 public:
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE {
    DVLOG(1) << "Received ipc message " << message.type();
    return true;
  }

  virtual void OnChannelError() OVERRIDE {
    DVLOG(1) << "Channel error";
    MetroExit();
  }
};

bool WaitForChromeIPCConnection(const std::string& channel_name) {
  int ms_elapsed = 0;
  while (!IPC::Channel::IsNamedServerInitialized(channel_name) &&
         ms_elapsed < 10000) {
    ms_elapsed += 500;
    Sleep(500);
  }
  return IPC::Channel::IsNamedServerInitialized(channel_name);
}

// This class helps decoding the pointer properties of an event.
class PointerInfoHandler {
 public:
  PointerInfoHandler() :
      x_(0),
      y_(0),
      wheel_delta_(0),
      update_kind_(winui::Input::PointerUpdateKind_Other) {};

  HRESULT Init(winui::Core::IPointerEventArgs* args) {
    HRESULT hr = args->get_CurrentPoint(&pointer_point_);
    if (FAILED(hr))
      return hr;

    winfoundtn::Point point;
    hr = pointer_point_->get_Position(&point);
    if (FAILED(hr))
      return hr;

    mswr::ComPtr<winui::Input::IPointerPointProperties> properties;
    hr = pointer_point_->get_Properties(&properties);
    if (FAILED(hr))
      return hr;

    hr = properties->get_PointerUpdateKind(&update_kind_);
    if (FAILED(hr))
      return hr;

    hr = properties->get_MouseWheelDelta(&wheel_delta_);
    if (FAILED(hr))
      return hr;

    x_ = point.X;
    y_ = point.Y;
    return S_OK;
  }

  bool IsMouse() const {
    mswr::ComPtr<windevs::Input::IPointerDevice> pointer_device;
    CheckHR(pointer_point_->get_PointerDevice(&pointer_device));
    windevs::Input::PointerDeviceType device_type;
    CheckHR(pointer_device->get_PointerDeviceType(&device_type));
    return  (device_type == windevs::Input::PointerDeviceType_Mouse);
  }

  int32 wheel_delta() const {
    return wheel_delta_;
  }

  ui::EventFlags flags() {
    switch (update_kind_) {
      case winui::Input::PointerUpdateKind_LeftButtonPressed:
        return ui::EF_LEFT_MOUSE_BUTTON;
      case winui::Input::PointerUpdateKind_LeftButtonReleased:
        return ui::EF_LEFT_MOUSE_BUTTON;
      case winui::Input::PointerUpdateKind_RightButtonPressed:
        return ui::EF_RIGHT_MOUSE_BUTTON;
      case winui::Input::PointerUpdateKind_RightButtonReleased:
        return ui::EF_RIGHT_MOUSE_BUTTON;
      case winui::Input::PointerUpdateKind_MiddleButtonPressed:
        return ui::EF_MIDDLE_MOUSE_BUTTON;
      case winui::Input::PointerUpdateKind_MiddleButtonReleased:
        return ui::EF_MIDDLE_MOUSE_BUTTON;
      default:
        return ui::EF_NONE;
    };
  }

  int x() const { return x_; }
  int y() const { return y_; }

 private:
  int x_;
  int y_;
  int wheel_delta_;
  winui::Input::PointerUpdateKind update_kind_;
  mswr::ComPtr<winui::Input::IPointerPoint> pointer_point_;
};

void RunMessageLoop(winui::Core::ICoreDispatcher* dispatcher) {
  // We're entering a nested message loop, let's allow dispatching
  // tasks while we're in there.
  MessageLoop::current()->SetNestableTasksAllowed(true);

  // Enter main core message loop. There are several ways to exit it
  // Nicely:
  // 1 - User action like ALT-F4.
  // 2 - Calling ICoreApplicationExit::Exit().
  // 3-  Posting WM_CLOSE to the core window.
  HRESULT hr = dispatcher->ProcessEvents(
      winui::Core::CoreProcessEventsOption
          ::CoreProcessEventsOption_ProcessUntilQuit);

  // Wind down the thread's chrome message loop.
  MessageLoop::current()->Quit();
}

// Helper to return the state of the shift/control/alt keys.
uint32 GetKeyboardEventFlags() {
  uint32 flags = 0;
  if (base::win::IsShiftPressed())
    flags |= ui::EF_SHIFT_DOWN;
  if (base::win::IsCtrlPressed())
    flags |= ui::EF_CONTROL_DOWN;
  if (base::win::IsAltPressed())
    flags |= ui::EF_ALT_DOWN;
  return flags;
}

}  // namespace

ChromeAppViewAsh::ChromeAppViewAsh()
    : ui_channel_(nullptr) {
  globals.previous_state =
      winapp::Activation::ApplicationExecutionState_NotRunning;
}

ChromeAppViewAsh::~ChromeAppViewAsh() {
  DVLOG(1) << __FUNCTION__;
}

IFACEMETHODIMP
ChromeAppViewAsh::Initialize(winapp::Core::ICoreApplicationView* view) {
  view_ = view;
  DVLOG(1) << __FUNCTION__;
  globals.main_thread_id = ::GetCurrentThreadId();

  HRESULT hr = view_->add_Activated(mswr::Callback<ActivatedHandler>(
      this, &ChromeAppViewAsh::OnActivate).Get(),
      &activated_token_);
  CheckHR(hr);
  return hr;
}

IFACEMETHODIMP
ChromeAppViewAsh::SetWindow(winui::Core::ICoreWindow* window) {
  window_ = window;
  DVLOG(1) << __FUNCTION__;
  // Register for pointer and keyboard notifications. We forward
  // them to the browser process via IPC.
  HRESULT hr = window_->add_PointerMoved(mswr::Callback<PointerEventHandler>(
      this, &ChromeAppViewAsh::OnPointerMoved).Get(),
      &pointermoved_token_);
  CheckHR(hr);

  hr = window_->add_PointerPressed(mswr::Callback<PointerEventHandler>(
      this, &ChromeAppViewAsh::OnPointerPressed).Get(),
      &pointerpressed_token_);
  CheckHR(hr);

  hr = window_->add_PointerReleased(mswr::Callback<PointerEventHandler>(
      this, &ChromeAppViewAsh::OnPointerReleased).Get(),
      &pointerreleased_token_);
  CheckHR(hr);

  hr = window_->add_KeyDown(mswr::Callback<KeyEventHandler>(
      this, &ChromeAppViewAsh::OnKeyDown).Get(),
      &keydown_token_);
  CheckHR(hr);

  hr = window_->add_KeyUp(mswr::Callback<KeyEventHandler>(
      this, &ChromeAppViewAsh::OnKeyUp).Get(),
      &keyup_token_);
  CheckHR(hr);

  hr = window_->add_PointerWheelChanged(mswr::Callback<PointerEventHandler>(
      this, &ChromeAppViewAsh::OnWheel).Get(),
      &wheel_token_);
  CheckHR(hr);

  hr = window_->add_CharacterReceived(mswr::Callback<CharEventHandler>(
      this, &ChromeAppViewAsh::OnCharacterReceived).Get(),
      &character_received_token_);
  CheckHR(hr);

  hr = window_->add_VisibilityChanged(mswr::Callback<VisibilityChangedHandler>(
      this, &ChromeAppViewAsh::OnVisibilityChanged).Get(),
      &visibility_changed_token_);
  CheckHR(hr);

  // By initializing the direct 3D swap chain with the corewindow
  // we can now directly blit to it from the browser process.
  direct3d_helper_.Initialize(window);
  DVLOG(1) << "Initialized Direct3D.";
  return S_OK;
}

IFACEMETHODIMP
ChromeAppViewAsh::Load(HSTRING entryPoint) {
  DVLOG(1) << __FUNCTION__;
  return S_OK;
}

IFACEMETHODIMP
ChromeAppViewAsh::Run() {
  DVLOG(1) << __FUNCTION__;
  mswr::ComPtr<winui::Core::ICoreDispatcher> dispatcher;
  HRESULT hr = window_->get_Dispatcher(&dispatcher);
  CheckHR(hr, "Dispatcher failed.");

  hr = window_->Activate();
  if (SUCCEEDED(hr)) {
    // TODO(cpu): Draw something here.
  } else {
    DVLOG(1) << "Activate failed, hr=" << hr;
  }

  // Create a message loop to allow message passing into this thread.
  MessageLoop msg_loop(MessageLoop::TYPE_UI);

  // Create the IPC channel IO thread. It needs to out-live the ChannelProxy.
  base::Thread io_thread("metro_IO_thread");
  base::Thread::Options options;
  options.message_loop_type = MessageLoop::TYPE_IO;
  io_thread.StartWithOptions(options);

  std::string ipc_channel_name("viewer");

  // TODO(robertshield): Figure out how to receive and append the channel ID
  // from the delegate_execute instance that launched the browser process.
  // See http://crbug.com/162474
  // ipc_channel_name.append(IPC::Channel::GenerateUniqueRandomChannelID());

  // Start up Chrome and wait for the desired IPC server connection to exist.
  WaitForChromeIPCConnection(ipc_channel_name);

  // In Aura mode we create an IPC channel to the browser, then ask it to
  // connect to us.
  ChromeChannelListener ui_channel_listener;
  IPC::ChannelProxy ui_channel(ipc_channel_name,
                               IPC::Channel::MODE_NAMED_CLIENT,
                               &ui_channel_listener,
                               io_thread.message_loop_proxy());
  ui_channel_ = &ui_channel;

  ui_channel_->Send(new MetroViewerHostMsg_SetTargetSurface(
                    gfx::NativeViewId(globals.core_window)));
  DVLOG(1) << "ICoreWindow sent " << globals.core_window;

  // And post the task that'll do the inner Metro message pumping to it.
  msg_loop.PostTask(FROM_HERE, base::Bind(&RunMessageLoop, dispatcher.Get()));
  msg_loop.Run();

  DVLOG(0) << "ProcessEvents done, hr=" << hr;
  return hr;
}

IFACEMETHODIMP
ChromeAppViewAsh::Uninitialize() {
  DVLOG(1) << __FUNCTION__;
  window_ = nullptr;
  view_ = nullptr;
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnActivate(winapp::Core::ICoreApplicationView*,
    winapp::Activation::IActivatedEventArgs* args) {
  DVLOG(1) << __FUNCTION__;

  args->get_PreviousExecutionState(&globals.previous_state);
  DVLOG(1) << "Previous Execution State: " << globals.previous_state;

  window_->Activate();

  if (globals.previous_state ==
      winapp::Activation::ApplicationExecutionState_Running) {
    DVLOG(1) << "Already running. Skipping rest of OnActivate.";
    return S_OK;
  }

  globals.core_window =
      winrt_utils::FindCoreWindow(globals.main_thread_id, 10);

  DVLOG(1) << "CoreWindow found: " << std::hex << globals.core_window;
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnPointerMoved(winui::Core::ICoreWindow* sender,
                                      winui::Core::IPointerEventArgs* args) {
  PointerInfoHandler pointer;
  HRESULT hr = pointer.Init(args);
  if (FAILED(hr))
    return hr;
  DCHECK(pointer.IsMouse());

  ui_channel_->Send(new MetroViewerHostMsg_MouseMoved(pointer.x(),
                                                      pointer.y(),
                                                      0));
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnPointerPressed(winui::Core::ICoreWindow* sender,
    winui::Core::IPointerEventArgs* args) {
  PointerInfoHandler pointer;
  HRESULT hr = pointer.Init(args);
  if (FAILED(hr))
    return hr;
  DCHECK(pointer.IsMouse());
  ui_channel_->Send(new MetroViewerHostMsg_MouseButton(pointer.x(), pointer.y(),
                                                       0,
                                                       ui::ET_MOUSE_PRESSED,
                                                       pointer.flags()));
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnPointerReleased(winui::Core::ICoreWindow* sender,
    winui::Core::IPointerEventArgs* args) {
  PointerInfoHandler pointer;
  HRESULT hr = pointer.Init(args);
  if (FAILED(hr))
    return hr;
  DCHECK(pointer.IsMouse());
  ui_channel_->Send(new MetroViewerHostMsg_MouseButton(pointer.x(), pointer.y(),
                                                       0,
                                                       ui::ET_MOUSE_RELEASED,
                                                       pointer.flags()));
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnWheel(winui::Core::ICoreWindow* sender,
    winui::Core::IPointerEventArgs* args) {
  PointerInfoHandler pointer;
  HRESULT hr = pointer.Init(args);
  if (FAILED(hr))
    return hr;
  DCHECK(pointer.IsMouse());
  ui_channel_->Send(new MetroViewerHostMsg_MouseButton(pointer.x(), pointer.y(),
                                                       pointer.wheel_delta(),
                                                       ui::ET_MOUSEWHEEL,
                                                       ui::EF_NONE));
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnKeyDown(winui::Core::ICoreWindow* sender,
                                 winui::Core::IKeyEventArgs* args) {
  winsys::VirtualKey virtual_key;
  HRESULT hr = args->get_VirtualKey(&virtual_key);
  if (FAILED(hr))
    return hr;
  winui::Core::CorePhysicalKeyStatus status;
  hr = args->get_KeyStatus(&status);
  if (FAILED(hr))
    return hr;

  ui_channel_->Send(new MetroViewerHostMsg_KeyDown(virtual_key,
                                                   status.RepeatCount,
                                                   status.ScanCode,
                                                   GetKeyboardEventFlags()));
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnKeyUp(winui::Core::ICoreWindow* sender,
                               winui::Core::IKeyEventArgs* args) {
  winsys::VirtualKey virtual_key;
  HRESULT hr = args->get_VirtualKey(&virtual_key);
  if (FAILED(hr))
    return hr;
  winui::Core::CorePhysicalKeyStatus status;
  hr = args->get_KeyStatus(&status);
  if (FAILED(hr))
    return hr;

  ui_channel_->Send(new MetroViewerHostMsg_KeyUp(virtual_key,
                                                 status.RepeatCount,
                                                 status.ScanCode,
                                                 GetKeyboardEventFlags()));
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnCharacterReceived(
  winui::Core::ICoreWindow* sender,
  winui::Core::ICharacterReceivedEventArgs* args) {
  unsigned int char_code = 0;
  HRESULT hr = args->get_KeyCode(&char_code);
  if (FAILED(hr))
    return hr;

  winui::Core::CorePhysicalKeyStatus status;
  hr = args->get_KeyStatus(&status);
  if (FAILED(hr))
    return hr;

  ui_channel_->Send(new MetroViewerHostMsg_Character(char_code,
                                                     status.RepeatCount,
                                                     status.ScanCode,
                                                     GetKeyboardEventFlags()));
  return S_OK;
}

HRESULT ChromeAppViewAsh::OnVisibilityChanged(
    winui::Core::ICoreWindow* sender,
    winui::Core::IVisibilityChangedEventArgs* args) {
  boolean visible = false;
  HRESULT hr = args->get_Visible(&visible);
  if (FAILED(hr))
    return hr;

  ui_channel_->Send(new MetroViewerHostMsg_VisibilityChanged(!!visible));
  return S_OK;
}


///////////////////////////////////////////////////////////////////////////////

ChromeAppViewFactory::ChromeAppViewFactory(
    winapp::Core::ICoreApplication* icore_app,
    LPTHREAD_START_ROUTINE host_main,
    void* host_context) {
  mswr::ComPtr<winapp::Core::ICoreApplication> core_app(icore_app);
  mswr::ComPtr<winapp::Core::ICoreApplicationExit> app_exit;
  CheckHR(core_app.As(&app_exit));
  globals.app_exit = app_exit.Detach();
}

IFACEMETHODIMP
ChromeAppViewFactory::CreateView(winapp::Core::IFrameworkView** view) {
  *view = mswr::Make<ChromeAppViewAsh>().Detach();
  return (*view) ? S_OK :  E_OUTOFMEMORY;
}
