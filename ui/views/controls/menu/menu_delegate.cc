// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/menu/menu_config.h"
#include "ui/views/controls/menu/menu_delegate.h"

namespace views {

MenuDelegate::~MenuDelegate() {}

bool MenuDelegate::IsItemChecked(int id) const {
  return false;
}

string16 MenuDelegate::GetLabel(int id) const {
  return string16();
}

const gfx::Font* MenuDelegate::GetLabelFont(int id) const {
  return NULL;
}

string16 MenuDelegate::GetTooltipText(int id,
                                      const gfx::Point& screen_loc) const {
  return string16();
}

bool MenuDelegate::GetAccelerator(int id, ui::Accelerator* accelerator) {
  return false;
}

bool MenuDelegate::ShowContextMenu(MenuItemView* source,
                                   int id,
                                   const gfx::Point& p,
                                   bool is_mouse_gesture) {
  return false;
}

bool MenuDelegate::SupportsCommand(int id) const {
  return true;
}

bool MenuDelegate::IsCommandEnabled(int id) const {
  return true;
}

bool MenuDelegate::GetContextualLabel(int id, string16* out) const {
  return false;
}

bool MenuDelegate::ShouldCloseAllMenusOnExecute(int id) {
  return true;
}

void MenuDelegate::ExecuteCommand(int id, int mouse_event_flags) {
  ExecuteCommand(id);
}

bool MenuDelegate::IsTriggerableEvent(MenuItemView* source,
                                      const ui::Event& e) {
  return e.type() == ui::ET_GESTURE_TAP ||
         e.type() == ui::ET_GESTURE_TAP_DOWN ||
         (e.IsMouseEvent() && (e.flags() &
              (ui::EF_LEFT_MOUSE_BUTTON | ui::EF_RIGHT_MOUSE_BUTTON)));
}

bool MenuDelegate::CanDrop(MenuItemView* menu, const OSExchangeData& data) {
  return false;
}

bool MenuDelegate::GetDropFormats(
    MenuItemView* menu,
    int* formats,
    std::set<OSExchangeData::CustomFormat>* custom_formats) {
  return false;
}

bool MenuDelegate::AreDropTypesRequired(MenuItemView* menu) {
  return false;
}

int MenuDelegate::GetDropOperation(MenuItemView* item,
                                   const ui::DropTargetEvent& event,
                                   DropPosition* position) {
  NOTREACHED() << "If you override CanDrop, you need to override this too";
  return ui::DragDropTypes::DRAG_NONE;
}

int MenuDelegate::OnPerformDrop(MenuItemView* menu,
                                DropPosition position,
                                const ui::DropTargetEvent& event) {
  NOTREACHED() << "If you override CanDrop, you need to override this too";
  return ui::DragDropTypes::DRAG_NONE;
}

bool MenuDelegate::CanDrag(MenuItemView* menu) {
  return false;
}

void MenuDelegate::WriteDragData(MenuItemView* sender, OSExchangeData* data) {
  NOTREACHED() << "If you override CanDrag, you must override this too.";
}

int MenuDelegate::GetDragOperations(MenuItemView* sender) {
  NOTREACHED() << "If you override CanDrag, you must override this too.";
  return 0;
}

MenuItemView* MenuDelegate::GetSiblingMenu(MenuItemView* menu,
                                           const gfx::Point& screen_point,
                                           MenuItemView::AnchorPosition* anchor,
                                           bool* has_mnemonics,
                                           MenuButton** button) {
  return NULL;
}

int MenuDelegate::GetMaxWidthForMenu(MenuItemView* menu) {
  // NOTE: this needs to be large enough to accommodate the wrench menu with
  // big fonts.
  return 800;
}

void MenuDelegate::WillShowMenu(MenuItemView* menu) {
}

void MenuDelegate::WillHideMenu(MenuItemView* menu) {
}

}  // namespace views
