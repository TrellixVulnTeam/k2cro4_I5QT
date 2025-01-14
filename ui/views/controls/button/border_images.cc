// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/button/border_images.h"

#include "base/logging.h"
#include "grit/ui_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/rect.h"

namespace views {

// static
const int BorderImages::kHot[] = { BORDER_IMAGES(IDR_TEXTBUTTON_HOVER) };
// static
const int BorderImages::kPushed[] = { BORDER_IMAGES(IDR_TEXTBUTTON_PRESSED) };

BorderImages::BorderImages() {}

BorderImages::BorderImages(const int image_ids[]) {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  top_left_ = *rb.GetImageSkiaNamed(image_ids[0]);
  top_ = *rb.GetImageSkiaNamed(image_ids[1]);
  top_right_ = *rb.GetImageSkiaNamed(image_ids[2]);
  left_ = *rb.GetImageSkiaNamed(image_ids[3]);
  center_ = *rb.GetImageSkiaNamed(image_ids[4]);
  right_ = *rb.GetImageSkiaNamed(image_ids[5]);
  bottom_left_ = *rb.GetImageSkiaNamed(image_ids[6]);
  bottom_ = *rb.GetImageSkiaNamed(image_ids[7]);
  bottom_right_ = *rb.GetImageSkiaNamed(image_ids[8]);
}

BorderImages::~BorderImages() {}

bool BorderImages::IsEmpty() const {
  return top_left_.isNull();
}

void BorderImages::Paint(gfx::Canvas* canvas, const gfx::Size& size) {
  if (IsEmpty())
    return;

  // Images must share widths by column and heights by row as depicted below.
  //     x0   x1   x2   x3
  // y0__|____|____|____|
  // y1__|_tl_|_t__|_tr_|
  // y2__|_l__|_c__|_r__|
  // y3__|_bl_|_b__|_br_|
  const gfx::Rect rect(size);
  const int x[] = { rect.x(), rect.x() + top_left_.width(),
                    rect.right() - top_right_.width(), rect.right() };
  const int y[] = { rect.y(), rect.y() + top_left_.height(),
                    rect.bottom() - bottom_left_.height(), rect.bottom() };

  canvas->DrawImageInt(top_left_, x[0], y[0]);
  canvas->TileImageInt(top_, x[1], y[0], x[2] - x[1], y[1] - y[0]);
  canvas->DrawImageInt(top_right_, x[2], y[0]);
  canvas->TileImageInt(left_, x[0], y[1], x[1] - x[0], y[2] - y[1]);
  canvas->DrawImageInt(center_, 0, 0, center_.width(), center_.height(),
                       x[1], y[1], x[2] - x[1], y[2] - y[1], false);
  canvas->TileImageInt(right_, x[2], y[1], x[3] - x[2], y[2] - y[1]);
  canvas->DrawImageInt(bottom_left_, 0, y[2]);
  canvas->TileImageInt(bottom_, x[1], y[2], x[2] - x[1], y[3] - y[2]);
  canvas->DrawImageInt(bottom_right_, x[2], y[2]);
}

}  // namespace views
