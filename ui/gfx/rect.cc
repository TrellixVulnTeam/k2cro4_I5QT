// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/rect.h"

#include <algorithm>

#if defined(OS_WIN)
#include <windows.h>
#elif defined(TOOLKIT_GTK)
#include <gdk/gdk.h>
#endif

#include "base/logging.h"
#include "base/stringprintf.h"
#include "ui/gfx/insets.h"
#include "ui/gfx/rect_base_impl.h"

namespace gfx {

template class RectBase<Rect, Point, Size, Insets, Vector2d, int>;

typedef class RectBase<Rect, Point, Size, Insets, Vector2d, int> RectBaseT;

Rect::Rect() : RectBaseT(gfx::Point()) {
}

Rect::Rect(int width, int height)
    : RectBaseT(gfx::Size(width, height)) {
}

Rect::Rect(int x, int y, int width, int height)
    : RectBaseT(gfx::Point(x, y), gfx::Size(width, height)) {
}

Rect::Rect(const gfx::Size& size)
    : RectBaseT(size) {
}

Rect::Rect(const gfx::Point& origin, const gfx::Size& size)
    : RectBaseT(origin, size) {
}

Rect::~Rect() {}

#if defined(OS_WIN)
Rect::Rect(const RECT& r)
    : RectBaseT(gfx::Point(r.left, r.top)) {
  set_width(std::abs(r.right - r.left));
  set_height(std::abs(r.bottom - r.top));
}
#elif defined(OS_MACOSX)
Rect::Rect(const CGRect& r)
    : RectBaseT(gfx::Point(r.origin.x, r.origin.y)) {
  set_width(r.size.width);
  set_height(r.size.height);
}
#elif defined(TOOLKIT_GTK)
Rect::Rect(const GdkRectangle& r)
    : RectBaseT(gfx::Point(r.x, r.y)) {
  set_width(r.width);
  set_height(r.height);
}
#endif

#if defined(OS_WIN)
RECT Rect::ToRECT() const {
  RECT r;
  r.left = x();
  r.right = right();
  r.top = y();
  r.bottom = bottom();
  return r;
}
#elif defined(OS_MACOSX)
CGRect Rect::ToCGRect() const {
  return CGRectMake(x(), y(), width(), height());
}
#elif defined(TOOLKIT_GTK)
GdkRectangle Rect::ToGdkRectangle() const {
  GdkRectangle r = {x(), y(), width(), height()};
  return r;
}
#endif

std::string Rect::ToString() const {
  return base::StringPrintf("%s %s",
                            origin().ToString().c_str(),
                            size().ToString().c_str());
}

Rect operator+(const Rect& lhs, const Vector2d& rhs) {
  Rect result(lhs);
  result += rhs;
  return result;
}

Rect operator-(const Rect& lhs, const Vector2d& rhs) {
  Rect result(lhs);
  result -= rhs;
  return result;
}

Rect IntersectRects(const Rect& a, const Rect& b) {
  Rect result = a;
  result.Intersect(b);
  return result;
}

Rect UnionRects(const Rect& a, const Rect& b) {
  Rect result = a;
  result.Union(b);
  return result;
}

Rect SubtractRects(const Rect& a, const Rect& b) {
  Rect result = a;
  result.Subtract(b);
  return result;
}

Rect BoundingRect(const Point& p1, const Point& p2) {
  int rx = std::min(p1.x(), p2.x());
  int ry = std::min(p1.y(), p2.y());
  int rr = std::max(p1.x(), p2.x());
  int rb = std::max(p1.y(), p2.y());
  return Rect(rx, ry, rr - rx, rb - ry);
}

}  // namespace gfx
