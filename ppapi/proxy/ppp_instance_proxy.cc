// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppp_instance_proxy.h"

#include <algorithm>

#include "base/bind.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_fullscreen.h"
#include "ppapi/c/ppp_instance.h"
#include "ppapi/c/private/ppb_flash_fullscreen.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_resource_tracker.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/ppb_url_loader_proxy.h"
#include "ppapi/shared_impl/ppapi_globals.h"
#include "ppapi/shared_impl/ppb_view_shared.h"
#include "ppapi/shared_impl/scoped_pp_resource.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_view_api.h"

namespace ppapi {
namespace proxy {

namespace {

#if !defined(OS_NACL)
PP_Bool IsFlashFullscreen(PP_Instance instance,
                          HostDispatcher* dispatcher) {
  const PPB_FlashFullscreen* flash_fullscreen_interface =
      static_cast<const PPB_FlashFullscreen*>(
          dispatcher->local_get_interface()(PPB_FLASHFULLSCREEN_INTERFACE));
  DCHECK(flash_fullscreen_interface);
  return flash_fullscreen_interface->IsFullscreen(instance);
}

PP_Bool DidCreate(PP_Instance instance,
                  uint32_t argc,
                  const char* argn[],
                  const char* argv[]) {
  std::vector<std::string> argn_vect;
  std::vector<std::string> argv_vect;
  for (uint32_t i = 0; i < argc; i++) {
    argn_vect.push_back(std::string(argn[i]));
    argv_vect.push_back(std::string(argv[i]));
  }

  PP_Bool result = PP_FALSE;
  HostDispatcher::GetForInstance(instance)->Send(
      new PpapiMsg_PPPInstance_DidCreate(API_ID_PPP_INSTANCE, instance,
                                         argn_vect, argv_vect, &result));
  return result;
}

void DidDestroy(PP_Instance instance) {
  HostDispatcher::GetForInstance(instance)->Send(
      new PpapiMsg_PPPInstance_DidDestroy(API_ID_PPP_INSTANCE, instance));
}

void DidChangeView(PP_Instance instance, PP_Resource view_resource) {
  HostDispatcher* dispatcher = HostDispatcher::GetForInstance(instance);

  thunk::EnterResourceNoLock<thunk::PPB_View_API> enter(view_resource, false);
  if (enter.failed()) {
    NOTREACHED();
    return;
  }

  dispatcher->Send(new PpapiMsg_PPPInstance_DidChangeView(
      API_ID_PPP_INSTANCE, instance, enter.object()->GetData(),
      IsFlashFullscreen(instance, dispatcher)));
}

void DidChangeFocus(PP_Instance instance, PP_Bool has_focus) {
  HostDispatcher::GetForInstance(instance)->Send(
      new PpapiMsg_PPPInstance_DidChangeFocus(API_ID_PPP_INSTANCE,
                                              instance, has_focus));
}

PP_Bool HandleDocumentLoad(PP_Instance instance,
                           PP_Resource url_loader) {
  PP_Bool result = PP_FALSE;
  HostDispatcher* dispatcher = HostDispatcher::GetForInstance(instance);

  // Set up the URLLoader for proxying.

  PPB_URLLoader_Proxy* url_loader_proxy = static_cast<PPB_URLLoader_Proxy*>(
      dispatcher->GetInterfaceProxy(API_ID_PPB_URL_LOADER));
  url_loader_proxy->PrepareURLLoaderForSendingToPlugin(url_loader);

  // PluginResourceTracker in the plugin process assumes that resources that it
  // tracks have been addrefed on behalf of the plugin at the renderer side. So
  // we explicitly do it for |url_loader| here.
  //
  // Please also see comments in PPP_Instance_Proxy::OnMsgHandleDocumentLoad()
  // about releasing of this extra reference.
  const PPB_Core* core = reinterpret_cast<const PPB_Core*>(
      dispatcher->local_get_interface()(PPB_CORE_INTERFACE));
  if (!core) {
    NOTREACHED();
    return PP_FALSE;
  }
  core->AddRefResource(url_loader);

  HostResource serialized_loader;
  serialized_loader.SetHostResource(instance, url_loader);
  dispatcher->Send(new PpapiMsg_PPPInstance_HandleDocumentLoad(
      API_ID_PPP_INSTANCE, instance, serialized_loader, &result));
  return result;
}

static const PPP_Instance_1_1 instance_interface = {
  &DidCreate,
  &DidDestroy,
  &DidChangeView,
  &DidChangeFocus,
  &HandleDocumentLoad
};
#endif  // !defined(OS_NACL)

}  // namespace

PPP_Instance_Proxy::PPP_Instance_Proxy(Dispatcher* dispatcher)
    : InterfaceProxy(dispatcher) {
  if (dispatcher->IsPlugin()) {
    // The PPP_Instance proxy works by always proxying the 1.1 version of the
    // interface, and then detecting in the plugin process which one to use.
    // PPP_Instance_Combined handles dispatching to whatever interface is
    // supported.
    //
    // This means that if the plugin supports either 1.0 or 1.1 version of
    // the interface, we want to say it supports the 1.1 version since we'll
    // convert it here. This magic conversion code is hardcoded into
    // PluginDispatcher::OnMsgSupportsInterface.
    combined_interface_.reset(PPP_Instance_Combined::Create(
        base::Bind(dispatcher->local_get_interface())));
  }
}

PPP_Instance_Proxy::~PPP_Instance_Proxy() {
}

#if !defined(OS_NACL)
// static
const PPP_Instance* PPP_Instance_Proxy::GetInstanceInterface() {
  return &instance_interface;
}
#endif  // !defined(OS_NACL)

bool PPP_Instance_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPP_Instance_Proxy, msg)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPPInstance_DidCreate,
                        OnPluginMsgDidCreate)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPPInstance_DidDestroy,
                        OnPluginMsgDidDestroy)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPPInstance_DidChangeView,
                        OnPluginMsgDidChangeView)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPPInstance_DidChangeFocus,
                        OnPluginMsgDidChangeFocus)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPPInstance_HandleDocumentLoad,
                        OnPluginMsgHandleDocumentLoad)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PPP_Instance_Proxy::OnPluginMsgDidCreate(
    PP_Instance instance,
    const std::vector<std::string>& argn,
    const std::vector<std::string>& argv,
    PP_Bool* result) {
  *result = PP_FALSE;
  if (argn.size() != argv.size())
    return;

  // Set up the routing associating this new instance with the dispatcher we
  // just got the message from. This must be done before calling into the
  // plugin so it can in turn call PPAPI functions.
  PluginDispatcher* plugin_dispatcher =
      static_cast<PluginDispatcher*>(dispatcher());
  plugin_dispatcher->DidCreateInstance(instance);
  PpapiGlobals::Get()->GetResourceTracker()->DidCreateInstance(instance);

  // Make sure the arrays always have at least one element so we can take the
  // address below.
  std::vector<const char*> argn_array(
      std::max(static_cast<size_t>(1), argn.size()));
  std::vector<const char*> argv_array(
      std::max(static_cast<size_t>(1), argn.size()));
  for (size_t i = 0; i < argn.size(); i++) {
    argn_array[i] = argn[i].c_str();
    argv_array[i] = argv[i].c_str();
  }

  DCHECK(combined_interface_.get());
  *result = combined_interface_->DidCreate(instance,
                                           static_cast<uint32_t>(argn.size()),
                                           &argn_array[0], &argv_array[0]);
}

void PPP_Instance_Proxy::OnPluginMsgDidDestroy(PP_Instance instance) {
  combined_interface_->DidDestroy(instance);

  PpapiGlobals* globals = PpapiGlobals::Get();
  globals->GetResourceTracker()->DidDeleteInstance(instance);
  globals->GetVarTracker()->DidDeleteInstance(instance);

  static_cast<PluginDispatcher*>(dispatcher())->DidDestroyInstance(instance);
}

void PPP_Instance_Proxy::OnPluginMsgDidChangeView(
    PP_Instance instance,
    const ViewData& new_data,
    PP_Bool flash_fullscreen) {
  PluginDispatcher* dispatcher = PluginDispatcher::GetForInstance(instance);
  if (!dispatcher)
    return;
  InstanceData* data = dispatcher->GetInstanceData(instance);
  if (!data)
    return;

  data->view = new_data;
  data->flash_fullscreen = flash_fullscreen;

  ScopedPPResource resource(
      ScopedPPResource::PassRef(),
      (new PPB_View_Shared(OBJECT_IS_PROXY,
                           instance, new_data))->GetReference());

  combined_interface_->DidChangeView(instance, resource,
                                     &new_data.rect,
                                     &new_data.clip_rect);
}

void PPP_Instance_Proxy::OnPluginMsgDidChangeFocus(PP_Instance instance,
                                                   PP_Bool has_focus) {
  combined_interface_->DidChangeFocus(instance, has_focus);
}

void PPP_Instance_Proxy::OnPluginMsgHandleDocumentLoad(
    PP_Instance instance,
    const HostResource& url_loader,
    PP_Bool* result) {
  PP_Resource plugin_loader =
      PPB_URLLoader_Proxy::TrackPluginResource(url_loader);
  *result = combined_interface_->HandleDocumentLoad(instance, plugin_loader);

  // This balances the one reference that TrackPluginResource() initialized it
  // with. The plugin will normally take an additional reference which will keep
  // the resource alive in the plugin (and the one reference in the renderer
  // representing all plugin references).
  // Once all references at the plugin side are released, the renderer side will
  // be notified and release the reference added in HandleDocumentLoad() above.
  PpapiGlobals::Get()->GetResourceTracker()->ReleaseResource(plugin_loader);
}

}  // namespace proxy
}  // namespace ppapi
