// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines a simple integer vector class.  This class is used to indicate a
// distance in two dimensions between two points. Subtracting two points should
// produce a vector, and adding a vector to a point produces the point at the
// vector's distance from the original point.

#ifndef UI_GFX_VECTOR2D_H_
#define UI_GFX_VECTOR2D_H_

#include <string>

#include "base/basictypes.h"
#include "ui/base/ui_export.h"
#include "ui/gfx/vector2d_f.h"

namespace gfx {

class UI_EXPORT Vector2d {
 public:
  Vector2d();
  Vector2d(int x, int y);

  int x() const { return x_; }
  void set_x(int x) { x_ = x; }

  int y() const { return y_; }
  void set_y(int y) { y_ = y; }

  // True if both components of the vector are 0.
  bool IsZero() const;

  // Add the components of the |other| vector to the current vector.
  void Add(const Vector2d& other);
  // Subtract the components of the |other| vector from the current vector.
  void Subtract(const Vector2d& other);

  void operator+=(const Vector2d& other) { Add(other); }
  void operator-=(const Vector2d& other) { Subtract(other); }

  void ClampToMax(const Vector2d& max) {
    x_ = x_ <= max.x_ ? x_ : max.x_;
    y_ = y_ <= max.y_ ? y_ : max.y_;
  }

  void ClampToMin(const Vector2d& min) {
    x_ = x_ >= min.x_ ? x_ : min.x_;
    y_ = y_ >= min.y_ ? y_ : min.y_;
  }

  // Gives the square of the diagonal length of the vector. Since this is
  // cheaper to compute than Length(), it is useful when you want to compare
  // relative lengths of different vectors without needing the actual lengths.
  int64 LengthSquared() const;
  // Gives the diagonal length of the vector.
  float Length() const;

  std::string ToString() const;

  operator Vector2dF() const { return Vector2dF(x_, y_); }

 private:
  int x_;
  int y_;
};

inline bool operator==(const Vector2d& lhs, const Vector2d& rhs) {
  return lhs.x() == rhs.x() && lhs.y() == rhs.y();
}

inline Vector2d operator-(const Vector2d& v) {
  return Vector2d(-v.x(), -v.y());
}

inline Vector2d operator+(const Vector2d& lhs, const Vector2d& rhs) {
  Vector2d result = lhs;
  result.Add(rhs);
  return result;
}

inline Vector2d operator-(const Vector2d& lhs, const Vector2d& rhs) {
  Vector2d result = lhs;
  result.Add(-rhs);
  return result;
}

}  // namespace gfx

#endif // UI_GFX_VECTOR2D_H_
