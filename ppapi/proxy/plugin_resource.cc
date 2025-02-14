// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/plugin_resource.h"

#include <limits>

#include "ppapi/proxy/ppapi_messages.h"

namespace ppapi {
namespace proxy {

PluginResource::PluginResource(Connection connection, PP_Instance instance)
    : Resource(OBJECT_IS_PROXY, instance),
      connection_(connection),
      next_sequence_number_(1),
      sent_create_to_browser_(false),
      sent_create_to_renderer_(false) {
}

PluginResource::~PluginResource() {
  if (sent_create_to_browser_) {
    connection_.browser_sender->Send(
        new PpapiHostMsg_ResourceDestroyed(pp_resource()));
  }
  if (sent_create_to_renderer_) {
    connection_.renderer_sender->Send(
        new PpapiHostMsg_ResourceDestroyed(pp_resource()));
  }
}

void PluginResource::OnReplyReceived(
    const proxy::ResourceMessageReplyParams& params,
    const IPC::Message& msg) {
  // Grab the callback for the reply sequence number and run it with |msg|.
  CallbackMap::iterator it = callbacks_.find(params.sequence());
  if (it == callbacks_.end()) {
    DCHECK(false) << "Callback does not exist for an expected sequence number.";
  } else {
    scoped_refptr<PluginResourceCallbackBase> callback = it->second;
    callbacks_.erase(it);
    callback->Run(params, msg);
  }
}

void PluginResource::NotifyLastPluginRefWasDeleted() {
  Resource::NotifyLastPluginRefWasDeleted();

  // The callbacks may hold referrences to this object. Normally, we will get
  // reply messages from the host side and remove them. However, it is possible
  // that some replies from the host never arrive, e.g., the corresponding
  // renderer crashes. In that case, we have to clean up the callbacks,
  // otherwise this object will live forever.
  callbacks_.clear();
}

void PluginResource::NotifyInstanceWasDeleted() {
  Resource::NotifyInstanceWasDeleted();

  // Please see comments in NotifyLastPluginRefWasDeleted() about why we must
  // clean up the callbacks.
  // It is possible that NotifyLastPluginRefWasDeleted() is never called for a
  // resource. For example, those singleton-style resources such as
  // GamepadResource never expose references to the plugin and thus won't
  // receive a NotifyLastPluginRefWasDeleted() call. For those resources, we
  // need to clean up callbacks when the instance goes away.
  callbacks_.clear();
}

void PluginResource::SendCreate(Destination dest, const IPC::Message& msg) {
  if (dest == RENDERER) {
    DCHECK(!sent_create_to_renderer_);
    sent_create_to_renderer_ = true;
  } else {
    DCHECK(!sent_create_to_browser_);
    sent_create_to_browser_ = true;
  }
  ResourceMessageCallParams params(pp_resource(), GetNextSequence());
  GetSender(dest)->Send(
      new PpapiHostMsg_ResourceCreated(params, pp_instance(), msg));
}

void PluginResource::Post(Destination dest, const IPC::Message& msg) {
  ResourceMessageCallParams params(pp_resource(), GetNextSequence());
  SendResourceCall(dest, params, msg);
}

bool PluginResource::SendResourceCall(
    Destination dest,
    const ResourceMessageCallParams& call_params,
    const IPC::Message& nested_msg) {
  return GetSender(dest)->Send(
      new PpapiHostMsg_ResourceCall(call_params, nested_msg));
}

int32_t PluginResource::GenericSyncCall(Destination dest,
                                        const IPC::Message& msg,
                                        IPC::Message* reply) {
  ResourceMessageCallParams params(pp_resource(), GetNextSequence());
  params.set_has_callback();
  ResourceMessageReplyParams reply_params;
  bool success = GetSender(dest)->Send(new PpapiHostMsg_ResourceSyncCall(
      params, msg, &reply_params, reply));
  if (success)
    return reply_params.result();
  return PP_ERROR_FAILED;
}

int32_t PluginResource::GetNextSequence() {
  // Return the value with wraparound, making sure we don't make a sequence
  // number with a 0 ID. Note that signed wraparound is undefined in C++ so we
  // manually check.
  int32_t ret = next_sequence_number_;
  if (next_sequence_number_ == std::numeric_limits<int32_t>::max())
    next_sequence_number_ = 1;  // Skip 0 which is invalid.
  else
    next_sequence_number_++;
  return ret;
}

}  // namespace proxy
}  // namespace ppapi
