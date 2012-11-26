// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/ibus/ibus_panel_service.h"

#include <string>
#include "base/bind.h"
#include "base/callback.h"
#include "chromeos/dbus/ibus/ibus_constants.h"
#include "chromeos/dbus/ibus/ibus_lookup_table.h"
#include "chromeos/dbus/ibus/ibus_property.h"
#include "chromeos/dbus/ibus/ibus_text.h"
#include "dbus/bus.h"
#include "dbus/exported_object.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"

namespace chromeos {
// TODO(nona): Remove ibus namespace after complete libibus removal.
namespace ibus {

class IBusPanelServiceImpl : public IBusPanelService {
 public:
  explicit IBusPanelServiceImpl(dbus::Bus* bus)
      : bus_(bus),
        panel_handler_(NULL),
        weak_ptr_factory_(this) {
    exported_object_ = bus->GetExportedObject(
        dbus::ObjectPath(ibus::panel::kServicePath));

    exported_object_->ExportMethod(
        ibus::panel::kServiceInterface,
        ibus::panel::kUpdateLookupTableMethod,
        base::Bind(&IBusPanelServiceImpl::UpdateLookupTable,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&IBusPanelServiceImpl::OnMethodExported,
                   weak_ptr_factory_.GetWeakPtr()));

    exported_object_->ExportMethod(
        ibus::panel::kServiceInterface,
        ibus::panel::kHideLookupTableMethod,
        base::Bind(&IBusPanelServiceImpl::HideLookupTable,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&IBusPanelServiceImpl::OnMethodExported,
                   weak_ptr_factory_.GetWeakPtr()));

    exported_object_->ExportMethod(
        ibus::panel::kServiceInterface,
        ibus::panel::kUpdateAuxiliaryTextMethod,
        base::Bind(&IBusPanelServiceImpl::UpdateAuxiliaryText,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&IBusPanelServiceImpl::OnMethodExported,
                   weak_ptr_factory_.GetWeakPtr()));

    exported_object_->ExportMethod(
        ibus::panel::kServiceInterface,
        ibus::panel::kHideAuxiliaryTextMethod,
        base::Bind(&IBusPanelServiceImpl::HideAuxiliaryText,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&IBusPanelServiceImpl::OnMethodExported,
                   weak_ptr_factory_.GetWeakPtr()));

    exported_object_->ExportMethod(
        ibus::panel::kServiceInterface,
        ibus::panel::kUpdatePreeditTextMethod,
        base::Bind(&IBusPanelServiceImpl::UpdatePreeditText,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&IBusPanelServiceImpl::OnMethodExported,
                   weak_ptr_factory_.GetWeakPtr()));

    exported_object_->ExportMethod(
        ibus::panel::kServiceInterface,
        ibus::panel::kHidePreeditTextMethod,
        base::Bind(&IBusPanelServiceImpl::HidePreeditText,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&IBusPanelServiceImpl::OnMethodExported,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  virtual ~IBusPanelServiceImpl() {
    bus_->UnregisterExportedObject(
        dbus::ObjectPath(ibus::panel::kServicePath));
  }

  // IBusPanelService override.
  virtual void Initialize(IBusPanelHandlerInterface* handler) OVERRIDE {
    DCHECK(handler);
    if (panel_handler_ == NULL) {
      panel_handler_ = handler;
    } else {
      LOG(ERROR) << "Already initialized.";
    }
  }

  // IBusPanelService override.
  virtual void CandidateClicked(uint32 index,
                                ibus::IBusMouseButton button,
                                uint32 state) OVERRIDE {
    dbus::Signal signal(ibus::panel::kServiceInterface,
                        ibus::panel::kCandidateClickedSignal);
    dbus::MessageWriter writer(&signal);
    writer.AppendUint32(index);
    writer.AppendUint32(static_cast<uint32>(button));
    writer.AppendUint32(state);
    exported_object_->SendSignal(&signal);
  }

  // IBusPanelService override.
  virtual void CursorUp() OVERRIDE {
    dbus::Signal signal(ibus::panel::kServiceInterface,
                        ibus::panel::kCursorUpSignal);
    exported_object_->SendSignal(&signal);
  }

  // IBusPanelService override.
  virtual void CursorDown() OVERRIDE {
    dbus::Signal signal(ibus::panel::kServiceInterface,
                        ibus::panel::kCursorDownSignal);
    exported_object_->SendSignal(&signal);
  }

  // IBusPanelService override.
  virtual void PageUp() OVERRIDE {
    dbus::Signal signal(ibus::panel::kServiceInterface,
                        ibus::panel::kPageUpSignal);
    exported_object_->SendSignal(&signal);
  }

  // IBusPanelService override.
  virtual void PageDown() OVERRIDE {
    dbus::Signal signal(ibus::panel::kServiceInterface,
                        ibus::panel::kPageDownSignal);
    exported_object_->SendSignal(&signal);
  }

 private:
  // Handles UpdateLookupTable method call from ibus-daemon.
  void UpdateLookupTable(dbus::MethodCall* method_call,
                         dbus::ExportedObject::ResponseSender response_sender) {
    DCHECK(panel_handler_);
    dbus::MessageReader reader(method_call);
    ibus::IBusLookupTable table;
    if (!ibus::PopIBusLookupTable(&reader, &table)) {
      LOG(WARNING) << "UpdateLookupTable called with incorrect parameters: "
                   << method_call->ToString();
      return;
    }
    bool visible = false;
    if (!reader.PopBool(&visible)) {
      LOG(WARNING) << "UpdateLookupTable called with incorrect parameters: "
                   << method_call->ToString();
      return;
    }
    panel_handler_->UpdateLookupTable(table, visible);
    dbus::Response* response = dbus::Response::FromMethodCall(method_call);
    response_sender.Run(response);
  }

  // Handles HideLookupTable method call from ibus-daemon.
  void HideLookupTable(dbus::MethodCall* method_call,
                       dbus::ExportedObject::ResponseSender response_sender) {
    DCHECK(panel_handler_);
    panel_handler_->HideLookupTable();
    dbus::Response* response = dbus::Response::FromMethodCall(method_call);
    response_sender.Run(response);
  }

  // Handles UpdateAuxiliaryText method call from ibus-daemon.
  void UpdateAuxiliaryText(
      dbus::MethodCall* method_call,
      dbus::ExportedObject::ResponseSender response_sender) {
    DCHECK(panel_handler_);
    dbus::MessageReader reader(method_call);
    std::string text;
    if (!ibus::PopStringFromIBusText(&reader, &text)) {
      LOG(WARNING) << "UpdateAuxiliaryText called with incorrect parameters: "
                   << method_call->ToString();
      return;
    }
    bool visible = false;
    if (!reader.PopBool(&visible)) {
      LOG(WARNING) << "UpdateAuxiliaryText called with incorrect parameters: "
                   << method_call->ToString();
      return;
    }
    panel_handler_->UpdateAuxiliaryText(text, visible);
    dbus::Response* response = dbus::Response::FromMethodCall(method_call);
    response_sender.Run(response);
  }

  // Handles HideAuxiliaryText method call from ibus-daemon.
  void HideAuxiliaryText(dbus::MethodCall* method_call,
                         dbus::ExportedObject::ResponseSender response_sender) {
    DCHECK(panel_handler_);
    panel_handler_->HideAuxiliaryText();
    dbus::Response* response = dbus::Response::FromMethodCall(method_call);
    response_sender.Run(response);
  }

  // Handles UpdatePreeditText method call from ibus-daemon.
  void UpdatePreeditText(dbus::MethodCall* method_call,
                         dbus::ExportedObject::ResponseSender response_sender) {
    DCHECK(panel_handler_);
    dbus::MessageReader reader(method_call);
    std::string text;
    if (!ibus::PopStringFromIBusText(&reader, &text)) {
      LOG(WARNING) << "UpdatePreeditText called with incorrect parameters: "
                   << method_call->ToString();
      return;
    }
    uint32 cursor_pos = 0;
    if (!reader.PopUint32(&cursor_pos)) {
      LOG(WARNING) << "UpdatePreeditText called with incorrect parameters: "
                   << method_call->ToString();
      return;
    }
    bool visible = false;
    if (!reader.PopBool(&visible)) {
      LOG(WARNING) << "UpdatePreeditText called with incorrect parameters: "
                   << method_call->ToString();
      return;
    }
    panel_handler_->UpdatePreeditText(text, cursor_pos, visible);
    dbus::Response* response = dbus::Response::FromMethodCall(method_call);
    response_sender.Run(response);
  }

  // Handles HidePreeditText method call from ibus-daemon.
  void HidePreeditText(dbus::MethodCall* method_call,
                       dbus::ExportedObject::ResponseSender response_sender) {
    DCHECK(panel_handler_);
    panel_handler_->HidePreeditText();
    dbus::Response* response = dbus::Response::FromMethodCall(method_call);
    response_sender.Run(response);
  }

  // Called when the method call is exported.
  void OnMethodExported(const std::string& interface_name,
                        const std::string& method_name,
                        bool success) {
    LOG_IF(WARNING, !success) << "Failed to export "
                              << interface_name << "." << method_name;
  }

  // D-Bus bus object used for unregistering exported methods in dtor.
  dbus::Bus* bus_;

  // All incoming method calls are passed on to the |panel_handler_|.
  IBusPanelHandlerInterface* panel_handler_;

  scoped_refptr<dbus::ExportedObject> exported_object_;
  base::WeakPtrFactory<IBusPanelServiceImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(IBusPanelServiceImpl);
};

class IBusPanelServiceStubImpl : public IBusPanelService {
 public:
  IBusPanelServiceStubImpl() {}
  virtual ~IBusPanelServiceStubImpl() {}
  // IBusPanelService overrides.
  virtual void Initialize(IBusPanelHandlerInterface* handler) OVERRIDE {}
  virtual void CandidateClicked(uint32 index,
                                ibus::IBusMouseButton button,
                                uint32 state) OVERRIDE {}
  virtual void CursorUp() OVERRIDE {}
  virtual void CursorDown() OVERRIDE {}
  virtual void PageUp() OVERRIDE {}
  virtual void PageDown() OVERRIDE {}

 private:
  DISALLOW_COPY_AND_ASSIGN(IBusPanelServiceStubImpl);
};

IBusPanelService::IBusPanelService() {
}

IBusPanelService::~IBusPanelService() {
}

// static
IBusPanelService* IBusPanelService::Create(DBusClientImplementationType type,
                                           dbus::Bus* bus) {
  if (type == REAL_DBUS_CLIENT_IMPLEMENTATION) {
    return new IBusPanelServiceImpl(bus);
  } else {
    return new IBusPanelServiceStubImpl();
  }
}

}  // namespace ibus
}  // namespace chromeos
