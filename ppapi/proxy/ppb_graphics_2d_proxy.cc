// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppb_graphics_2d_proxy.h"

#include <string.h>  // For memset.

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/ppb_graphics_2d.h"
#include "ppapi/proxy/enter_proxy.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_resource_tracker.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/ppb_image_data_proxy.h"
#include "ppapi/shared_impl/tracked_callback.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_graphics_2d_api.h"
#include "ppapi/thunk/thunk.h"

using ppapi::thunk::PPB_Graphics2D_API;

namespace ppapi {
namespace proxy {

class Graphics2D : public Resource, public thunk::PPB_Graphics2D_API {
 public:
  Graphics2D(const HostResource& host_resource,
             const PP_Size& size,
             PP_Bool is_always_opaque);
  virtual ~Graphics2D();

  // Resource.
  virtual PPB_Graphics2D_API* AsPPB_Graphics2D_API();

  // PPB_Graphics_2D_API.
  PP_Bool Describe(PP_Size* size, PP_Bool* is_always_opaque);
  void PaintImageData(PP_Resource image_data,
                      const PP_Point* top_left,
                      const PP_Rect* src_rect);
  void Scroll(const PP_Rect* clip_rect,
              const PP_Point* amount);
  void ReplaceContents(PP_Resource image_data);
  bool SetScale(float scale);
  float GetScale();
  int32_t Flush(scoped_refptr<TrackedCallback> callback,
                PP_Resource* old_image_data);

  // Notification that the host has sent an ACK for a pending Flush.
  void FlushACK(int32_t result_code);

 private:
  PluginDispatcher* GetDispatcher() const {
    return PluginDispatcher::GetForResource(this);
  }

  static const ApiID kApiID = API_ID_PPB_GRAPHICS_2D;

  PP_Size size_;
  PP_Bool is_always_opaque_;
  float scale_;

  // In the plugin, this is the current callback set for Flushes. When the
  // callback is pending, we're waiting for a flush ACK.
  scoped_refptr<TrackedCallback> current_flush_callback_;

  DISALLOW_COPY_AND_ASSIGN(Graphics2D);
};

Graphics2D::Graphics2D(const HostResource& host_resource,
                       const PP_Size& size,
                       PP_Bool is_always_opaque)
    : Resource(OBJECT_IS_PROXY, host_resource),
      size_(size),
      is_always_opaque_(is_always_opaque),
      scale_(1.0f) {
}

Graphics2D::~Graphics2D() {
}

PPB_Graphics2D_API* Graphics2D::AsPPB_Graphics2D_API() {
  return this;
}

PP_Bool Graphics2D::Describe(PP_Size* size, PP_Bool* is_always_opaque) {
  *size = size_;
  *is_always_opaque = is_always_opaque_;
  return PP_TRUE;
}

void Graphics2D::PaintImageData(PP_Resource image_data,
                                const PP_Point* top_left,
                                const PP_Rect* src_rect) {
  Resource* image_object =
      PpapiGlobals::Get()->GetResourceTracker()->GetResource(image_data);
  if (!image_object || pp_instance() != image_object->pp_instance()) {
    Log(PP_LOGLEVEL_ERROR,
        "PPB_Graphics2D.PaintImageData: Bad image resource.");
    return;
  }

  PP_Rect dummy;
  memset(&dummy, 0, sizeof(PP_Rect));
  GetDispatcher()->Send(new PpapiHostMsg_PPBGraphics2D_PaintImageData(
      kApiID, host_resource(), image_object->host_resource(), *top_left,
      !!src_rect, src_rect ? *src_rect : dummy));
}

void Graphics2D::Scroll(const PP_Rect* clip_rect,
                        const PP_Point* amount) {
  PP_Rect dummy;
  memset(&dummy, 0, sizeof(PP_Rect));
  GetDispatcher()->Send(new PpapiHostMsg_PPBGraphics2D_Scroll(
      kApiID, host_resource(), !!clip_rect, clip_rect ? *clip_rect : dummy,
      *amount));
}

void Graphics2D::ReplaceContents(PP_Resource image_data) {
  thunk::EnterResourceNoLock<thunk::PPB_ImageData_API> enter_image(
      image_data, true);
  if (enter_image.failed())
    return;

  ImageData* image_object = static_cast<ImageData*>(enter_image.object());
  if (pp_instance() != image_object->pp_instance()) {
    Log(PP_LOGLEVEL_ERROR,
        "PPB_Graphics2D.ReplaceContents: Image resource for another instance.");
    return;
  }
  image_object->set_used_in_replace_contents();

  GetDispatcher()->Send(new PpapiHostMsg_PPBGraphics2D_ReplaceContents(
      kApiID, host_resource(), image_object->host_resource()));
}

bool Graphics2D::SetScale(float scale) {
  if (scale <= 0.0f)
    return false;
  GetDispatcher()->Send(new PpapiHostMsg_PPBGraphics2D_Dev_SetScale(
      kApiID, host_resource(), scale));
  scale_ = scale;
  return true;
}

float Graphics2D::GetScale() {
  return scale_;
}

int32_t Graphics2D::Flush(scoped_refptr<TrackedCallback> callback,
                          PP_Resource* old_image_data) {
  // We don't support this feature, it's for in-renderer only.
  if (old_image_data)
    *old_image_data = 0;

  if (TrackedCallback::IsPending(current_flush_callback_))
    return PP_ERROR_INPROGRESS;  // Can't have >1 flush pending.
  current_flush_callback_ = callback;

  GetDispatcher()->Send(new PpapiHostMsg_PPBGraphics2D_Flush(kApiID,
                                                             host_resource()));
  return PP_OK_COMPLETIONPENDING;
}

void Graphics2D::FlushACK(int32_t result_code) {
  current_flush_callback_->Run(result_code);
}

PPB_Graphics2D_Proxy::PPB_Graphics2D_Proxy(Dispatcher* dispatcher)
    : InterfaceProxy(dispatcher),
      callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

PPB_Graphics2D_Proxy::~PPB_Graphics2D_Proxy() {
}

// static
PP_Resource PPB_Graphics2D_Proxy::CreateProxyResource(
    PP_Instance instance,
    const PP_Size& size,
    PP_Bool is_always_opaque) {
  PluginDispatcher* dispatcher = PluginDispatcher::GetForInstance(instance);
  if (!dispatcher)
    return 0;

  HostResource result;
  dispatcher->Send(new PpapiHostMsg_PPBGraphics2D_Create(
      kApiID, instance, size, is_always_opaque, &result));
  if (result.is_null())
    return 0;
  return (new Graphics2D(result, size, is_always_opaque))->GetReference();
}

bool PPB_Graphics2D_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPB_Graphics2D_Proxy, msg)
#if !defined(OS_NACL)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBGraphics2D_Create,
                        OnHostMsgCreate)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBGraphics2D_PaintImageData,
                        OnHostMsgPaintImageData)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBGraphics2D_Scroll,
                        OnHostMsgScroll)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBGraphics2D_ReplaceContents,
                        OnHostMsgReplaceContents)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBGraphics2D_Flush,
                        OnHostMsgFlush)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBGraphics2D_Dev_SetScale,
                        OnHostMsgSetScale)
#endif  // !defined(OS_NACL)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBGraphics2D_FlushACK,
                        OnPluginMsgFlushACK)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  // FIXME(brettw) handle bad messages!
  return handled;
}

#if !defined(OS_NACL)
void PPB_Graphics2D_Proxy::OnHostMsgCreate(PP_Instance instance,
                                           const PP_Size& size,
                                           PP_Bool is_always_opaque,
                                           HostResource* result) {
  thunk::EnterResourceCreation enter(instance);
  if (enter.succeeded()) {
    result->SetHostResource(instance, enter.functions()->CreateGraphics2D(
        instance, size, is_always_opaque));
  }
}

void PPB_Graphics2D_Proxy::OnHostMsgPaintImageData(
    const HostResource& graphics_2d,
    const HostResource& image_data,
    const PP_Point& top_left,
    bool src_rect_specified,
    const PP_Rect& src_rect) {
  EnterHostFromHostResource<PPB_Graphics2D_API> enter(graphics_2d);
  if (enter.failed())
    return;
  enter.object()->PaintImageData(image_data.host_resource(), &top_left,
      src_rect_specified ? &src_rect : NULL);
}

void PPB_Graphics2D_Proxy::OnHostMsgScroll(const HostResource& graphics_2d,
                                           bool clip_specified,
                                           const PP_Rect& clip,
                                           const PP_Point& amount) {
  EnterHostFromHostResource<PPB_Graphics2D_API> enter(graphics_2d);
  if (enter.failed())
    return;
  enter.object()->Scroll(clip_specified ? &clip : NULL, &amount);
}

void PPB_Graphics2D_Proxy::OnHostMsgReplaceContents(
    const HostResource& graphics_2d,
    const HostResource& image_data) {
  EnterHostFromHostResource<PPB_Graphics2D_API> enter(graphics_2d);
  if (enter.failed())
    return;
  enter.object()->ReplaceContents(image_data.host_resource());
}

void PPB_Graphics2D_Proxy::OnHostMsgFlush(const HostResource& graphics_2d) {
  EnterHostFromHostResourceForceCallback<PPB_Graphics2D_API> enter(
      graphics_2d, callback_factory_,
      &PPB_Graphics2D_Proxy::SendFlushACKToPlugin, graphics_2d);
  if (enter.failed())
    return;
  PP_Resource old_image_data = 0;
  enter.SetResult(enter.object()->Flush(enter.callback(), &old_image_data));
  if (old_image_data) {
    // If the Graphics2D has an old image data it's not using any more, send
    // it back to the plugin for possible re-use. See ppb_image_data_proxy.cc
    // for a description how this process works.
    HostResource old_image_data_host_resource;
    old_image_data_host_resource.SetHostResource(graphics_2d.instance(),
                                                 old_image_data);
    dispatcher()->Send(new PpapiMsg_PPBImageData_NotifyUnusedImageData(
        API_ID_PPB_IMAGE_DATA, old_image_data_host_resource));
  }
}

void PPB_Graphics2D_Proxy::OnHostMsgSetScale(const HostResource& graphics_2d,
                                             float scale) {
  EnterHostFromHostResource<PPB_Graphics2D_API> enter(graphics_2d);
  if (enter.failed())
    return;
  enter.object()->SetScale(scale);
}
#endif  // !defined(OS_NACL)

void PPB_Graphics2D_Proxy::OnPluginMsgFlushACK(
    const HostResource& host_resource,
    int32_t pp_error) {
  EnterPluginFromHostResource<PPB_Graphics2D_API> enter(host_resource);
  if (enter.succeeded())
    static_cast<Graphics2D*>(enter.object())->FlushACK(pp_error);
}

#if !defined(OS_NACL)
void PPB_Graphics2D_Proxy::SendFlushACKToPlugin(
    int32_t result,
    const HostResource& graphics_2d) {
  dispatcher()->Send(new PpapiMsg_PPBGraphics2D_FlushACK(kApiID, graphics_2d,
                                                         result));
}
#endif  // !defined(OS_NACL)

}  // namespace proxy
}  // namespace ppapi
