// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/models/simple_menu_model.h"

#include "base/bind.h"
#include "base/message_loop.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/image/image.h"

namespace ui {

const int kSeparatorId = -1;

struct SimpleMenuModel::Item {
  int command_id;
  string16 label;
  gfx::Image icon;
  ItemType type;
  int group_id;
  MenuModel* submenu;
  ButtonMenuItemModel* button_model;
  MenuSeparatorType separator_type;
};

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel::Delegate, public:

bool SimpleMenuModel::Delegate::IsCommandIdVisible(int command_id) const {
  return true;
}

bool SimpleMenuModel::Delegate::IsItemForCommandIdDynamic(
    int command_id) const {
  return false;
}

string16 SimpleMenuModel::Delegate::GetLabelForCommandId(int command_id) const {
  return string16();
}

bool SimpleMenuModel::Delegate::GetIconForCommandId(
    int command_id, gfx::Image* image_skia) const {
  return false;
}

void SimpleMenuModel::Delegate::CommandIdHighlighted(int command_id) {
}

void SimpleMenuModel::Delegate::ExecuteCommand(
    int command_id, int event_flags) {
  ExecuteCommand(command_id);
}

void SimpleMenuModel::Delegate::MenuWillShow(SimpleMenuModel* /*source*/) {
}

void SimpleMenuModel::Delegate::MenuClosed(SimpleMenuModel* /*source*/) {
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, public:

SimpleMenuModel::SimpleMenuModel(Delegate* delegate)
    : delegate_(delegate),
      menu_model_delegate_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)) {
}

SimpleMenuModel::~SimpleMenuModel() {
}

void SimpleMenuModel::AddItem(int command_id, const string16& label) {
  Item item = { command_id, label, gfx::Image(), TYPE_COMMAND, -1, NULL,
                NULL, NORMAL_SEPARATOR };
  AppendItem(item);
}

void SimpleMenuModel::AddItemWithStringId(int command_id, int string_id) {
  AddItem(command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::AddSeparator(MenuSeparatorType separator_type) {
#if !defined(USE_AURA)
  if (separator_type != NORMAL_SEPARATOR) {
    NOTIMPLEMENTED();
  }
#endif
  Item item = { kSeparatorId, string16(), gfx::Image(), TYPE_SEPARATOR,
                -1, NULL, NULL , separator_type };
  AppendItem(item);
}

void SimpleMenuModel::AddCheckItem(int command_id, const string16& label) {
  Item item = { command_id, label, gfx::Image(), TYPE_CHECK, -1, NULL,
                NULL, NORMAL_SEPARATOR };
  AppendItem(item);
}

void SimpleMenuModel::AddCheckItemWithStringId(int command_id, int string_id) {
  AddCheckItem(command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::AddRadioItem(int command_id, const string16& label,
                                   int group_id) {
  Item item = { command_id, label, gfx::Image(), TYPE_RADIO, group_id, NULL,
                NULL, NORMAL_SEPARATOR };
  AppendItem(item);
}

void SimpleMenuModel::AddRadioItemWithStringId(int command_id, int string_id,
                                               int group_id) {
  AddRadioItem(command_id, l10n_util::GetStringUTF16(string_id), group_id);
}

void SimpleMenuModel::AddButtonItem(int command_id,
                                    ButtonMenuItemModel* model) {
  Item item = { command_id, string16(), gfx::Image(), TYPE_BUTTON_ITEM, -1,
                NULL, model, NORMAL_SEPARATOR };
  AppendItem(item);
}

void SimpleMenuModel::AddSubMenu(int command_id, const string16& label,
                                 MenuModel* model) {
  Item item = { command_id, label, gfx::Image(), TYPE_SUBMENU, -1, model,
                NULL, NORMAL_SEPARATOR };
  AppendItem(item);
}

void SimpleMenuModel::AddSubMenuWithStringId(int command_id,
                                             int string_id, MenuModel* model) {
  AddSubMenu(command_id, l10n_util::GetStringUTF16(string_id), model);
}

void SimpleMenuModel::InsertItemAt(
    int index, int command_id, const string16& label) {
  Item item = { command_id, label, gfx::Image(), TYPE_COMMAND, -1, NULL,
                NULL, NORMAL_SEPARATOR };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertItemWithStringIdAt(
    int index, int command_id, int string_id) {
  InsertItemAt(index, command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::InsertSeparatorAt(int index,
                                        MenuSeparatorType separator_type) {
#if !defined(USE_AURA)
  if (separator_type != NORMAL_SEPARATOR) {
    NOTIMPLEMENTED();
  }
#endif
  Item item = { kSeparatorId, string16(), gfx::Image(), TYPE_SEPARATOR,
                -1, NULL, NULL, separator_type };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertCheckItemAt(
    int index, int command_id, const string16& label) {
  Item item = { command_id, label, gfx::Image(), TYPE_CHECK, -1, NULL,
                NULL, NORMAL_SEPARATOR };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertCheckItemWithStringIdAt(
    int index, int command_id, int string_id) {
  InsertCheckItemAt(
      FlipIndex(index), command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::InsertRadioItemAt(
    int index, int command_id, const string16& label, int group_id) {
  Item item = { command_id, label, gfx::Image(), TYPE_RADIO, group_id, NULL,
                NULL, NORMAL_SEPARATOR };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertRadioItemWithStringIdAt(
    int index, int command_id, int string_id, int group_id) {
  InsertRadioItemAt(
      index, command_id, l10n_util::GetStringUTF16(string_id), group_id);
}

void SimpleMenuModel::InsertSubMenuAt(
    int index, int command_id, const string16& label, MenuModel* model) {
  Item item = { command_id, label, gfx::Image(), TYPE_SUBMENU, -1, model,
                NULL, NORMAL_SEPARATOR };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertSubMenuWithStringIdAt(
    int index, int command_id, int string_id, MenuModel* model) {
  InsertSubMenuAt(index, command_id, l10n_util::GetStringUTF16(string_id),
                  model);
}

void SimpleMenuModel::SetIcon(int index, const gfx::Image& icon) {
  items_[ValidateItemIndex(index)].icon = icon;
}

void SimpleMenuModel::Clear() {
  items_.clear();
}

int SimpleMenuModel::GetIndexOfCommandId(int command_id) {
  for (ItemVector::iterator i = items_.begin(); i != items_.end(); ++i) {
    if (i->command_id == command_id) {
      return FlipIndex(static_cast<int>(std::distance(items_.begin(), i)));
    }
  }
  return -1;
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, MenuModel implementation:

bool SimpleMenuModel::HasIcons() const {
  for (ItemVector::const_iterator i = items_.begin(); i != items_.end(); ++i) {
    if (!i->icon.IsEmpty())
      return true;
  }

  return false;
}

int SimpleMenuModel::GetItemCount() const {
  return static_cast<int>(items_.size());
}

MenuModel::ItemType SimpleMenuModel::GetTypeAt(int index) const {
  return items_[ValidateItemIndex(FlipIndex(index))].type;
}

ui::MenuSeparatorType SimpleMenuModel::GetSeparatorTypeAt(int index) const {
  return items_[ValidateItemIndex(FlipIndex(index))].separator_type;
}

int SimpleMenuModel::GetCommandIdAt(int index) const {
  return items_[ValidateItemIndex(FlipIndex(index))].command_id;
}

string16 SimpleMenuModel::GetLabelAt(int index) const {
  if (IsItemDynamicAt(index))
    return delegate_->GetLabelForCommandId(GetCommandIdAt(index));
  return items_[ValidateItemIndex(FlipIndex(index))].label;
}

bool SimpleMenuModel::IsItemDynamicAt(int index) const {
  if (delegate_)
    return delegate_->IsItemForCommandIdDynamic(GetCommandIdAt(index));
  return false;
}

bool SimpleMenuModel::GetAcceleratorAt(int index,
                                       ui::Accelerator* accelerator) const {
  if (delegate_) {
    return delegate_->GetAcceleratorForCommandId(GetCommandIdAt(index),
                                                 accelerator);
  }
  return false;
}

bool SimpleMenuModel::IsItemCheckedAt(int index) const {
  if (!delegate_)
    return false;
  MenuModel::ItemType item_type = GetTypeAt(index);
  return (item_type == TYPE_CHECK || item_type == TYPE_RADIO) ?
      delegate_->IsCommandIdChecked(GetCommandIdAt(index)) : false;
}

int SimpleMenuModel::GetGroupIdAt(int index) const {
  return items_[ValidateItemIndex(FlipIndex(index))].group_id;
}

bool SimpleMenuModel::GetIconAt(int index, gfx::Image* icon) {
  if (IsItemDynamicAt(index))
    return delegate_->GetIconForCommandId(GetCommandIdAt(index), icon);

  ValidateItemIndex(index);
  if (items_[index].icon.IsEmpty())
    return false;

  *icon = items_[index].icon;
  return true;
}

ButtonMenuItemModel* SimpleMenuModel::GetButtonMenuItemAt(int index) const {
  return items_[ValidateItemIndex(FlipIndex(index))].button_model;
}

bool SimpleMenuModel::IsEnabledAt(int index) const {
  int command_id = GetCommandIdAt(index);
  if (!delegate_ || command_id == kSeparatorId || GetButtonMenuItemAt(index))
    return true;
  return delegate_->IsCommandIdEnabled(command_id);
}

bool SimpleMenuModel::IsVisibleAt(int index) const {
  int command_id = GetCommandIdAt(index);
  if (!delegate_ || command_id == kSeparatorId || GetButtonMenuItemAt(index))
    return true;
  return delegate_->IsCommandIdVisible(command_id);
}

void SimpleMenuModel::HighlightChangedTo(int index) {
  if (delegate_)
    delegate_->CommandIdHighlighted(GetCommandIdAt(index));
}

void SimpleMenuModel::ActivatedAt(int index) {
  if (delegate_)
    delegate_->ExecuteCommand(GetCommandIdAt(index));
}

void SimpleMenuModel::ActivatedAt(int index, int event_flags) {
  if (delegate_)
    delegate_->ExecuteCommand(GetCommandIdAt(index), event_flags);
}

MenuModel* SimpleMenuModel::GetSubmenuModelAt(int index) const {
  return items_[ValidateItemIndex(FlipIndex(index))].submenu;
}

void SimpleMenuModel::MenuWillShow() {
  if (delegate_)
    delegate_->MenuWillShow(this);
}

void SimpleMenuModel::MenuClosed() {
  // Due to how menus work on the different platforms, ActivatedAt will be
  // called after this.  It's more convenient for the delegate to be called
  // afterwards though, so post a task.
  MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&SimpleMenuModel::OnMenuClosed, method_factory_.GetWeakPtr()));
}

void SimpleMenuModel::SetMenuModelDelegate(
      ui::MenuModelDelegate* menu_model_delegate) {
  menu_model_delegate_ = menu_model_delegate;
}

MenuModelDelegate* SimpleMenuModel::GetMenuModelDelegate() const {
  return menu_model_delegate_;
}

void SimpleMenuModel::OnMenuClosed() {
  if (delegate_)
    delegate_->MenuClosed(this);
}

int SimpleMenuModel::FlipIndex(int index) const {
  return index;
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, Private:

int SimpleMenuModel::ValidateItemIndex(int index) const {
  CHECK_GE(index, 0);
  CHECK_LT(static_cast<size_t>(index), items_.size());
  return index;
}

void SimpleMenuModel::AppendItem(const Item& item) {
  ValidateItem(item);
  items_.push_back(item);
}

void SimpleMenuModel::InsertItemAtIndex(const Item& item, int index) {
  ValidateItem(item);
  items_.insert(items_.begin() + FlipIndex(index), item);
}

void SimpleMenuModel::ValidateItem(const Item& item) {
#ifndef NDEBUG
  if (item.type == TYPE_SEPARATOR) {
    DCHECK_EQ(item.command_id, kSeparatorId);
  } else {
    DCHECK_GE(item.command_id, 0);
  }
#endif  // NDEBUG
}

}  // namespace ui
