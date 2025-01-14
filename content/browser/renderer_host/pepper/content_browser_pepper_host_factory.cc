// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/pepper/content_browser_pepper_host_factory.h"

#include "content/browser/renderer_host/pepper/browser_ppapi_host_impl.h"
#include "content/browser/renderer_host/pepper/pepper_gamepad_host.h"
#include "content/browser/renderer_host/pepper/pepper_print_settings_manager.h"
#include "content/browser/renderer_host/pepper/pepper_printing_host.h"
#include "ppapi/host/resource_host.h"
#include "ppapi/proxy/ppapi_messages.h"

using ppapi::host::ResourceHost;

namespace content {

ContentBrowserPepperHostFactory::ContentBrowserPepperHostFactory(
    BrowserPpapiHostImpl* host)
    : host_(host) {
}

ContentBrowserPepperHostFactory::~ContentBrowserPepperHostFactory() {
}

scoped_ptr<ResourceHost> ContentBrowserPepperHostFactory::CreateResourceHost(
    ppapi::host::PpapiHost* host,
    const ppapi::proxy::ResourceMessageCallParams& params,
    PP_Instance instance,
    const IPC::Message& message) {
  DCHECK(host == host_->GetPpapiHost());

  // Make sure the plugin is giving us a valid instance for this resource.
  if (!host_->IsValidInstance(instance))
    return scoped_ptr<ResourceHost>();

  // Public interfaces.
  switch (message.type()) {
    case PpapiHostMsg_Gamepad_Create::ID:
      return scoped_ptr<ResourceHost>(new PepperGamepadHost(
          host_, instance, params.pp_resource()));
  }

  // Dev interfaces.
  if (host_->GetPpapiHost()->permissions().HasPermission(
          ppapi::PERMISSION_DEV)) {
    switch (message.type()) {
      case PpapiHostMsg_Printing_Create::ID: {
         scoped_ptr<PepperPrintSettingsManager> manager(
             new PepperPrintSettingsManagerImpl());
         return scoped_ptr<ResourceHost>(new PepperPrintingHost(
             host_->GetPpapiHost(), instance,
             params.pp_resource(), manager.Pass()));
      }
    }
  }
  return scoped_ptr<ResourceHost>();
}

}  // namespace content
