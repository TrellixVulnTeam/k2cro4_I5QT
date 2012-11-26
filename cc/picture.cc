// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/trace_event.h"
#include "cc/content_layer_client.h"
#include "cc/picture.h"
#include "cc/rendering_stats.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/rect_conversions.h"

namespace cc {

scoped_refptr<Picture> Picture::Create() {
  return make_scoped_refptr(new Picture());
}

Picture::Picture() : picture_(new SkPicture()) {
}

Picture::Picture(SkPicture* picture, gfx::Rect layer_rect,
                 gfx::Rect opaque_rect) :
    layer_rect_(layer_rect),
    opaque_rect_(opaque_rect),
    picture_(picture) {
}

Picture::~Picture() {
}

scoped_refptr<Picture> Picture::Clone() {
  // SkPicture is not thread-safe to rasterize with, so return a thread-safe
  // clone of it.
  SkPicture* clone = picture_->clone();
  return make_scoped_refptr(new Picture(clone, layer_rect_, opaque_rect_));
}

void Picture::Record(ContentLayerClient* painter, gfx::Rect layer_rect,
                     RenderingStats& stats) {
  TRACE_EVENT0("cc", "Picture::Record");
  SkCanvas* canvas = picture_->beginRecording(
      layer_rect.width(),
      layer_rect.height(),
      SkPicture::kOptimizeForClippedPlayback_RecordingFlag);

  canvas->save();
  canvas->translate(SkFloatToScalar(-layer_rect.x()),
                    SkFloatToScalar(-layer_rect.y()));

  SkPaint paint;
  paint.setAntiAlias(false);
  paint.setXfermodeMode(SkXfermode::kClear_Mode);
  SkRect layer_skrect = SkRect::MakeXYWH(layer_rect.x(),
                                         layer_rect.y(),
                                         layer_rect.width(),
                                         layer_rect.height());
  canvas->drawRect(layer_skrect, paint);
  canvas->clipRect(layer_skrect);

  gfx::RectF opaque_layer_rect;
  base::TimeTicks beginPaintTime = base::TimeTicks::Now();
  painter->paintContents(canvas, layer_rect, opaque_layer_rect);
  double delta = (base::TimeTicks::Now() - beginPaintTime).InSecondsF();
  stats.totalPaintTimeInSeconds += delta;

  canvas->restore();
  picture_->endRecording();

  opaque_rect_ = gfx::ToEnclosedRect(opaque_layer_rect);
  layer_rect_ = layer_rect;
}

void Picture::Raster(SkCanvas* canvas) {
  TRACE_EVENT0("cc", "Picture::Raster");
  canvas->translate(layer_rect_.x(), layer_rect_.y());
  canvas->save();
  canvas->drawPicture(*picture_);
  canvas->restore();
}

}  // namespace cc
