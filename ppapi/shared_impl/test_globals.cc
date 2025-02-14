// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/test_globals.h"

namespace ppapi {

TestGlobals::TestGlobals()
    : ppapi::PpapiGlobals(),
      callback_tracker_(new CallbackTracker) {
}

TestGlobals::TestGlobals(PpapiGlobals::ForTest for_test)
    : ppapi::PpapiGlobals(for_test),
      callback_tracker_(new CallbackTracker) {
}

TestGlobals::~TestGlobals() {
}

ResourceTracker* TestGlobals::GetResourceTracker() {
  return &resource_tracker_;
}

VarTracker* TestGlobals::GetVarTracker() {
  return &var_tracker_;
}

CallbackTracker* TestGlobals::GetCallbackTrackerForInstance(
    PP_Instance instance) {
  return callback_tracker_.get();
}

thunk::PPB_Instance_API* TestGlobals::GetInstanceAPI(
    PP_Instance instance) {
  return NULL;
}

thunk::ResourceCreationAPI* TestGlobals::GetResourceCreationAPI(
    PP_Instance instance) {
  return NULL;
}

PP_Module TestGlobals::GetModuleForInstance(PP_Instance instance) {
  return 0;
}

std::string TestGlobals::GetCmdLine() {
  return std::string();
}

void TestGlobals::PreCacheFontForFlash(const void* /* logfontw */) {
}

base::Lock* TestGlobals::GetProxyLock() {
  return NULL;
}

void TestGlobals::LogWithSource(PP_Instance instance,
                                PP_LogLevel_Dev level,
                                const std::string& source,
                                const std::string& value) {
}

void TestGlobals::BroadcastLogWithSource(PP_Module module,
                                         PP_LogLevel_Dev level,
                                         const std::string& source,
                                         const std::string& value) {
}

MessageLoopShared* TestGlobals::GetCurrentMessageLoop() {
  return NULL;
}

bool TestGlobals::IsHostGlobals() const {
  // Pretend to be the host-side, for code that expects one or the other.
  // TODO(dmichael): just make it settable which one we're pretending to be?
  return true;
}

}  // namespace ppapi
