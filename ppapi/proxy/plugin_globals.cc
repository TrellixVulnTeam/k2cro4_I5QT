// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/plugin_globals.h"

#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_proxy_delegate.h"
#include "ppapi/proxy/ppb_message_loop_proxy.h"
#include "ppapi/thunk/enter.h"

namespace ppapi {
namespace proxy {

PluginGlobals* PluginGlobals::plugin_globals_ = NULL;

PluginGlobals::PluginGlobals()
    : ppapi::PpapiGlobals(),
      plugin_proxy_delegate_(NULL),
      callback_tracker_(new CallbackTracker),
      loop_for_main_thread_(
          new MessageLoopResource(MessageLoopResource::ForMainThread())) {
#if defined(ENABLE_PEPPER_THREADING)
  enable_threading_ = true;
#else
  enable_threading_ = false;
#endif

  DCHECK(!plugin_globals_);
  plugin_globals_ = this;
}

PluginGlobals::PluginGlobals(ForTest for_test)
    : ppapi::PpapiGlobals(for_test),
      plugin_proxy_delegate_(NULL),
      callback_tracker_(new CallbackTracker) {
#if defined(ENABLE_PEPPER_THREADING)
  enable_threading_ = true;
#else
  enable_threading_ = false;
#endif
  DCHECK(!plugin_globals_);
}

PluginGlobals::~PluginGlobals() {
  DCHECK(plugin_globals_ == this || !plugin_globals_);
  plugin_globals_ = NULL;
}

ResourceTracker* PluginGlobals::GetResourceTracker() {
  return &plugin_resource_tracker_;
}

VarTracker* PluginGlobals::GetVarTracker() {
  return &plugin_var_tracker_;
}

CallbackTracker* PluginGlobals::GetCallbackTrackerForInstance(
    PP_Instance instance) {
  // In the plugin process, the callback tracker is always the same, regardless
  // of the instance.
  return callback_tracker_.get();
}

thunk::PPB_Instance_API* PluginGlobals::GetInstanceAPI(PP_Instance instance) {
  PluginDispatcher* dispatcher = PluginDispatcher::GetForInstance(instance);
  if (dispatcher)
    return dispatcher->GetInstanceAPI();
  return NULL;
}

thunk::ResourceCreationAPI* PluginGlobals::GetResourceCreationAPI(
    PP_Instance instance) {
  PluginDispatcher* dispatcher = PluginDispatcher::GetForInstance(instance);
  if (dispatcher)
    return dispatcher->GetResourceCreationAPI();
  return NULL;
}

PP_Module PluginGlobals::GetModuleForInstance(PP_Instance instance) {
  // Currently proxied plugins don't use the PP_Module for anything useful.
  return 0;
}

std::string PluginGlobals::GetCmdLine() {
  return command_line_;
}

void PluginGlobals::PreCacheFontForFlash(const void* logfontw) {
  plugin_proxy_delegate_->PreCacheFont(logfontw);
}

base::Lock* PluginGlobals::GetProxyLock() {
  if (enable_threading_)
    return &proxy_lock_;
  return NULL;
}

void PluginGlobals::LogWithSource(PP_Instance instance,
                                  PP_LogLevel_Dev level,
                                  const std::string& source,
                                  const std::string& value) {
  const std::string& fixed_up_source = source.empty() ? plugin_name_ : source;
  PluginDispatcher::LogWithSource(instance, level, fixed_up_source, value);
}

void PluginGlobals::BroadcastLogWithSource(PP_Module /* module */,
                                           PP_LogLevel_Dev level,
                                           const std::string& source,
                                           const std::string& value) {
  // Since we have only one module in a plugin process, broadcast is always
  // the same as "send to everybody" which is what the dispatcher implements
  // for the "instance = 0" case.
  LogWithSource(0, level, source, value);
}

MessageLoopShared* PluginGlobals::GetCurrentMessageLoop() {
  return MessageLoopResource::GetCurrent();
}

MessageLoopResource* PluginGlobals::loop_for_main_thread() {
  return loop_for_main_thread_.get();
}

bool PluginGlobals::IsPluginGlobals() const {
  return true;
}

}  // namespace proxy
}  // namespace ppapi
