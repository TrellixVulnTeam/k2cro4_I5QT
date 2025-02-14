// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/native_theme/common_theme.h"

#include "base/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/controls/menu/menu_config.h"

namespace {

// Theme colors returned by GetSystemColor().

// MenuItem:
const SkColor kMenuBackgroundColor = SK_ColorWHITE;
const SkColor kMenuHighlightBackgroundColor = SkColorSetA(SK_ColorBLACK, 15);
const SkColor kMenuBorderColor = SkColorSetRGB(0xBA, 0xBA, 0xBA);
const SkColor kMenuSeparatorColor = SkColorSetRGB(0xE9, 0xE9, 0xE9);

}  // namespace

namespace ui {

bool CommonThemeGetSystemColor(NativeTheme::ColorId color_id, SkColor* color) {
  switch (color_id) {
    // MenuItem
    case NativeTheme::kColorId_MenuBorderColor:
      *color = kMenuBorderColor;
      break;
    case NativeTheme::kColorId_MenuSeparatorColor:
      *color = kMenuSeparatorColor;
      break;
    case NativeTheme::kColorId_MenuBackgroundColor:
      *color = kMenuBackgroundColor;
      break;
    default:
      return false;
  }
  return true;
}

void CommonThemePaintMenuSeparator(
    SkCanvas* canvas,
    const gfx::Rect& rect,
    const NativeTheme::MenuSeparatorExtraParams& extra) {
  SkPaint paint;
  paint.setColor(kMenuSeparatorColor);
  int position_y = rect.y() + rect.height() / 2;
  canvas->drawLine(rect.x(), position_y, rect.right(), position_y, paint);
}

void CommonThemePaintMenuGutter(SkCanvas* canvas, const gfx::Rect& rect) {
  SkPaint paint;
  paint.setColor(kMenuSeparatorColor);
  int position_x = rect.x() + rect.width() / 2;
  canvas->drawLine(position_x, rect.y(), position_x, rect.bottom(), paint);
}

void CommonThemePaintMenuBackground(SkCanvas* canvas, const gfx::Rect& rect) {
  SkPaint paint;
  paint.setColor(kMenuBackgroundColor);
  canvas->drawRect(gfx::RectToSkRect(rect), paint);
}

void CommonThemePaintMenuItemBackground(SkCanvas* canvas,
                                        NativeTheme::State state,
                                        const gfx::Rect& rect) {
  SkPaint paint;
  switch (state) {
    case NativeTheme::kNormal:
    case NativeTheme::kDisabled:
      paint.setColor(kMenuBackgroundColor);
      break;
    case NativeTheme::kHovered:
      paint.setColor(kMenuHighlightBackgroundColor);
      break;
    default:
      NOTREACHED() << "Invalid state " << state;
      break;
  }
  canvas->drawRect(gfx::RectToSkRect(rect), paint);
}

}  // namespace ui
