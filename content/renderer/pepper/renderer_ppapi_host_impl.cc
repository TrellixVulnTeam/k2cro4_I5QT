// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/renderer_ppapi_host_impl.h"

#include "base/file_path.h"
#include "base/logging.h"
#include "content/renderer/pepper/pepper_in_process_resource_creation.h"
#include "content/renderer/pepper/pepper_in_process_router.h"
#include "content/renderer/pepper/pepper_plugin_delegate_impl.h"
#include "content/renderer/render_view_impl.h"
#include "content/renderer/render_widget_fullscreen_pepper.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebRect.h"
#include "ui/gfx/point.h"
#include "webkit/plugins/ppapi/fullscreen_container.h"
#include "webkit/plugins/ppapi/host_globals.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"

using webkit::ppapi::HostGlobals;
using webkit::ppapi::PluginInstance;
using webkit::ppapi::PluginModule;

namespace content {

// static
CONTENT_EXPORT RendererPpapiHost*
RendererPpapiHost::CreateExternalPluginModule(
    scoped_refptr<PluginModule> plugin_module,
    PluginInstance* plugin_instance,
    const FilePath& file_path,
    ppapi::PpapiPermissions permissions,
    const IPC::ChannelHandle& channel_handle,
    int plugin_child_id) {
  RendererPpapiHost* renderer_ppapi_host = NULL;
  // Since we're the embedder, we can make assumptions about the delegate on
  // the instance.
  PepperPluginDelegateImpl* pepper_plugin_delegate =
      static_cast<PepperPluginDelegateImpl*>(plugin_instance->delegate());
  if (pepper_plugin_delegate) {
    renderer_ppapi_host = pepper_plugin_delegate->CreateExternalPluginModule(
        plugin_module,
        file_path,
        permissions,
        channel_handle,
        plugin_child_id);
  }
  return renderer_ppapi_host;
}


// Out-of-process constructor.
RendererPpapiHostImpl::RendererPpapiHostImpl(
    PluginModule* module,
    ppapi::proxy::HostDispatcher* dispatcher,
    const ppapi::PpapiPermissions& permissions)
    : module_(module),
      dispatcher_(dispatcher) {
  // Hook the PpapiHost up to the dispatcher for out-of-process communication.
  ppapi_host_.reset(
      new ppapi::host::PpapiHost(dispatcher, permissions));
  ppapi_host_->AddHostFactoryFilter(scoped_ptr<ppapi::host::HostFactory>(
      new ContentRendererPepperHostFactory(this)));
  dispatcher->AddFilter(ppapi_host_.get());
}

// In-process constructor.
RendererPpapiHostImpl::RendererPpapiHostImpl(
    PluginModule* module,
    const ppapi::PpapiPermissions& permissions)
    : module_(module),
      dispatcher_(NULL) {
  // Hook the host up to the in-process router.
  in_process_router_.reset(new PepperInProcessRouter(this));
  ppapi_host_.reset(new ppapi::host::PpapiHost(
      in_process_router_->GetRendererToPluginSender(), permissions));
  ppapi_host_->AddHostFactoryFilter(scoped_ptr<ppapi::host::HostFactory>(
      new ContentRendererPepperHostFactory(this)));
}

RendererPpapiHostImpl::~RendererPpapiHostImpl() {
}

// static
RendererPpapiHostImpl* RendererPpapiHostImpl::CreateOnModuleForOutOfProcess(
    PluginModule* module,
    ppapi::proxy::HostDispatcher* dispatcher,
    const ppapi::PpapiPermissions& permissions) {
  DCHECK(!module->GetEmbedderState());
  RendererPpapiHostImpl* result = new RendererPpapiHostImpl(
      module, dispatcher, permissions);

  // Takes ownership of pointer.
  module->SetEmbedderState(
      scoped_ptr<PluginModule::EmbedderState>(result));

  return result;
}

// static
RendererPpapiHostImpl* RendererPpapiHostImpl::CreateOnModuleForInProcess(
    PluginModule* module,
    const ppapi::PpapiPermissions& permissions) {
  DCHECK(!module->GetEmbedderState());
  RendererPpapiHostImpl* result = new RendererPpapiHostImpl(
      module, permissions);

  // Takes ownership of pointer.
  module->SetEmbedderState(
      scoped_ptr<PluginModule::EmbedderState>(result));

  return result;
}

// static
RendererPpapiHostImpl* RendererPpapiHostImpl::GetForPPInstance(
    PP_Instance pp_instance) {
  PluginInstance* instance = HostGlobals::Get()->GetInstance(pp_instance);
  if (!instance)
    return NULL;

  // All modules created by content will have their embedders state be the
  // host impl.
  return static_cast<RendererPpapiHostImpl*>(
      instance->module()->GetEmbedderState());
}

scoped_ptr< ::ppapi::thunk::ResourceCreationAPI>
RendererPpapiHostImpl::CreateInProcessResourceCreationAPI(
    PluginInstance* instance) {
  return scoped_ptr< ::ppapi::thunk::ResourceCreationAPI>(
      new PepperInProcessResourceCreation(this, instance));
}

ppapi::host::PpapiHost* RendererPpapiHostImpl::GetPpapiHost() {
  return ppapi_host_.get();
}

RenderView* RendererPpapiHostImpl::GetRenderViewForInstance(
    PP_Instance instance) const {
  PluginInstance* instance_object = GetAndValidateInstance(instance);
  if (!instance_object)
    return NULL;

  // Since we're the embedder, we can make assumptions about the delegate on
  // the instance and get back to our RenderView.
  return static_cast<PepperPluginDelegateImpl*>(
      instance_object->delegate())->render_view();
}

bool RendererPpapiHostImpl::IsValidInstance(
    PP_Instance instance) const {
  return !!GetAndValidateInstance(instance);
}

webkit::ppapi::PluginInstance* RendererPpapiHostImpl::GetPluginInstance(
    PP_Instance instance) const {
  return GetAndValidateInstance(instance);
}

WebKit::WebPluginContainer* RendererPpapiHostImpl::GetContainerForInstance(
      PP_Instance instance) const {
  PluginInstance* instance_object = GetAndValidateInstance(instance);
  if (!instance_object)
    return NULL;
  return instance_object->container();
}

bool RendererPpapiHostImpl::HasUserGesture(PP_Instance instance) const {
  PluginInstance* instance_object = GetAndValidateInstance(instance);
  if (!instance_object)
    return false;

  if (instance_object->module()->permissions().HasPermission(
          ppapi::PERMISSION_BYPASS_USER_GESTURE))
    return true;
  return instance_object->IsProcessingUserGesture();
}

int RendererPpapiHostImpl::GetRoutingIDForWidget(PP_Instance instance) const {
  webkit::ppapi::PluginInstance* plugin_instance =
      GetAndValidateInstance(instance);
  if (!plugin_instance)
    return 0;
  if (plugin_instance->flash_fullscreen()) {
    webkit::ppapi::FullscreenContainer* container =
        plugin_instance->fullscreen_container();
    return static_cast<RenderWidgetFullscreenPepper*>(container)->routing_id();
  }
  return GetRenderViewForInstance(instance)->GetRoutingID();
}

gfx::Point RendererPpapiHostImpl::PluginPointToRenderView(
    PP_Instance instance,
    const gfx::Point& pt) const {
  webkit::ppapi::PluginInstance* plugin_instance =
      GetAndValidateInstance(instance);
  if (!plugin_instance)
    return pt;

  RenderViewImpl* render_view = static_cast<RenderViewImpl*>(
      GetRenderViewForInstance(instance));
  if (plugin_instance->view_data().is_fullscreen ||
      plugin_instance->flash_fullscreen()) {
    WebKit::WebRect window_rect = render_view->windowRect();
    WebKit::WebRect screen_rect = render_view->screenInfo().rect;
    return gfx::Point(pt.x() - window_rect.x + screen_rect.x,
                      pt.y() - window_rect.y + screen_rect.y);
  }
  return gfx::Point(pt.x() + plugin_instance->view_data().rect.point.x,
                    pt.y() + plugin_instance->view_data().rect.point.y);
}

IPC::PlatformFileForTransit RendererPpapiHostImpl::ShareHandleWithRemote(
    base::PlatformFile handle,
    bool should_close_source) {
  if (!dispatcher_) {
    if (should_close_source)
      base::ClosePlatformFile(handle);
    return IPC::InvalidPlatformFileForTransit();
  }
  return dispatcher_->ShareHandleWithRemote(handle, should_close_source);
}

PluginInstance* RendererPpapiHostImpl::GetAndValidateInstance(
    PP_Instance pp_instance) const {
  PluginInstance* instance = HostGlobals::Get()->GetInstance(pp_instance);
  if (!instance)
    return NULL;
  if (instance->module() != module_)
    return NULL;
  return instance;
}

}  // namespace content
