// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/transform_util.h"

#include <cmath>

#include "ui/gfx/point.h"

namespace gfx {

namespace {

double Length3(double v[3]) {
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void Scale3(double v[3], double scale) {
  for (int i = 0; i < 3; ++i)
    v[i] *= scale;
}

template <int n>
double Dot(const double* a, const double* b) {
  double toReturn = 0;
  for (int i = 0; i < n; ++i)
    toReturn += a[i] * b[i];
  return toReturn;
}

template <int n>
void Combine(double* out,
             const double* a,
             const double* b,
             double scale_a,
             double scale_b) {
  for (int i = 0; i < n; ++i)
    out[i] = a[i] * scale_a + b[i] * scale_b;
}

void Cross3(double out[3], double a[3], double b[3]) {
  double x = a[1] * b[2] - a[2] * b[1];
  double y = a[2] * b[0] - a[0] * b[2];
  double z = a[0] * b[1] - a[1] * b[0];
  out[0] = x;
  out[1] = y;
  out[2] = z;
}

// Taken from http://www.w3.org/TR/css3-transforms/.
bool Slerp(double out[4],
           const double q1[4],
           const double q2[4],
           double progress) {
  double product = Dot<4>(q1, q2);

  // Clamp product to -1.0 <= product <= 1.0.
  product = std::min(std::max(product, -1.0), 1.0);

  const double epsilon = 1e-5;
  if (std::abs(product - 1.0) < epsilon) {
    for (int i = 0; i < 4; ++i)
      out[i] = q1[i];
    return true;
  }

  if (std::abs(product) < epsilon) {
    // Rotation by 180 degrees. We'll fail. It's ambiguous how to interpolate.
    return false;
  }

  double denom = std::sqrt(1 - product * product);
  double theta = std::acos(product);
  double w = std::sin(progress * theta) * (1 / denom);

  double scale1 = std::cos(progress * theta) - product * w;
  double scale2 = w;
  Combine<4>(out, q1, q2, scale1, scale2);

  return true;
}

// Returns false if the matrix cannot be normalized.
bool Normalize(SkMatrix44& m) {
  if (m.getDouble(3, 3) == 0.0)
    // Cannot normalize.
    return false;

  double scale = 1.0 / m.getDouble(3, 3);
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      m.setDouble(i, j, m.getDouble(i, j) * scale);

  return true;
}

}  // namespace

Transform GetScaleTransform(const Point& anchor, float scale) {
  Transform transform;
  transform.ConcatScale(scale, scale);
  transform.ConcatTranslate(anchor.x() * (1 - scale),
                            anchor.y() * (1 - scale));
  return transform;
}

DecomposedTransform::DecomposedTransform() {
  translate[0] = translate[1] = translate[2] = 0.0;
  scale[0] = scale[1] = scale[2] = 1.0;
  skew[0] = skew[1] = skew[2] = 0.0;
  perspective[0] = perspective[1] = perspective[2] = 0.0;
  quaternion[0] = quaternion[1] = quaternion[2] = 0.0;
  perspective[3] = quaternion[3] = 1.0;
}

bool BlendDecomposedTransforms(DecomposedTransform* out,
                               const DecomposedTransform& to,
                               const DecomposedTransform& from,
                               double progress) {
  double scalea = progress;
  double scaleb = 1.0 - progress;
  Combine<3>(out->translate, to.translate, from.translate, scalea, scaleb);
  Combine<3>(out->scale, to.scale, from.scale, scalea, scaleb);
  Combine<3>(out->skew, to.skew, from.skew, scalea, scaleb);
  Combine<4>(
      out->perspective, to.perspective, from.perspective, scalea, scaleb);
  return Slerp(out->quaternion, from.quaternion, to.quaternion, progress);
}

// Taken from http://www.w3.org/TR/css3-transforms/.
bool DecomposeTransform(DecomposedTransform* decomp,
                        const Transform& transform) {
  if (!decomp)
    return false;

  // We'll operate on a copy of the matrix.
  SkMatrix44 matrix = transform.matrix();

  // If we cannot normalize the matrix, then bail early as we cannot decompose.
  if (!Normalize(matrix))
    return false;

  SkMatrix44 perspectiveMatrix = matrix;

  for (int i = 0; i < 3; ++i)
    perspectiveMatrix.setDouble(3, i, 0.0);

  perspectiveMatrix.setDouble(3, 3, 1.0);

  // If the perspective matrix is not invertible, we are also unable to
  // decompose, so we'll bail early. Constant taken from SkMatrix44::invert.
  if (std::abs(perspectiveMatrix.determinant()) < 1e-8)
    return false;

  if (matrix.getDouble(3, 0) != 0.0 ||
      matrix.getDouble(3, 1) != 0.0 ||
      matrix.getDouble(3, 2) != 0.0) {
    // rhs is the right hand side of the equation.
    SkMScalar rhs[4] = {
      matrix.get(3, 0),
      matrix.get(3, 1),
      matrix.get(3, 2),
      matrix.get(3, 3)
    };

    // Solve the equation by inverting perspectiveMatrix and multiplying
    // rhs by the inverse.
    SkMatrix44 inversePerspectiveMatrix;
    if (!perspectiveMatrix.invert(&inversePerspectiveMatrix))
      return false;

    SkMatrix44 transposedInversePerspectiveMatrix =
        inversePerspectiveMatrix;

    transposedInversePerspectiveMatrix.transpose();
    transposedInversePerspectiveMatrix.mapMScalars(rhs);

    for (int i = 0; i < 4; ++i)
      decomp->perspective[i] = rhs[i];

  } else {
    // No perspective.
    for (int i = 0; i < 3; ++i)
      decomp->perspective[i] = 0.0;
    decomp->perspective[3] = 1.0;
  }

  for (int i = 0; i < 3; i++)
    decomp->translate[i] = matrix.getDouble(i, 3);

  double row[3][3];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; ++j)
      row[i][j] = matrix.getDouble(j, i);

  // Compute X scale factor and normalize first row.
  decomp->scale[0] = Length3(row[0]);
  if (decomp->scale[0] != 0.0)
    Scale3(row[0], 1.0 / decomp->scale[0]);

  // Compute XY shear factor and make 2nd row orthogonal to 1st.
  decomp->skew[0] = Dot<3>(row[0], row[1]);
  Combine<3>(row[1], row[1], row[0], 1.0, -decomp->skew[0]);

  // Now, compute Y scale and normalize 2nd row.
  decomp->scale[1] = Length3(row[1]);
  if (decomp->scale[1] != 0.0)
    Scale3(row[1], 1.0 / decomp->scale[1]);

  decomp->skew[0] /= decomp->scale[1];

  // Compute XZ and YZ shears, orthogonalize 3rd row
  decomp->skew[1] = Dot<3>(row[0], row[2]);
  Combine<3>(row[2], row[2], row[0], 1.0, -decomp->skew[1]);
  decomp->skew[2] = Dot<3>(row[1], row[2]);
  Combine<3>(row[2], row[2], row[1], 1.0, -decomp->skew[2]);

  // Next, get Z scale and normalize 3rd row.
  decomp->scale[2] = Length3(row[2]);
  if (decomp->scale[2] != 0.0)
    Scale3(row[2], 1.0 / decomp->scale[2]);

  decomp->skew[1] /= decomp->scale[2];
  decomp->skew[2] /= decomp->scale[2];

  // At this point, the matrix (in rows) is orthonormal.
  // Check for a coordinate system flip.  If the determinant
  // is -1, then negate the matrix and the scaling factors.
  double pdum3[3];
  Cross3(pdum3, row[1], row[2]);
  if (Dot<3>(row[0], pdum3) < 0) {
    for (int i = 0; i < 3; i++) {
      decomp->scale[i] *= -1.0;
      for (int j = 0; j < 3; ++j)
        row[i][j] *= -1.0;
    }
  }

  decomp->quaternion[0] =
      0.5 * std::sqrt(std::max(1.0 + row[0][0] - row[1][1] - row[2][2], 0.0));
  decomp->quaternion[1] =
      0.5 * std::sqrt(std::max(1.0 - row[0][0] + row[1][1] - row[2][2], 0.0));
  decomp->quaternion[2] =
      0.5 * std::sqrt(std::max(1.0 - row[0][0] - row[1][1] + row[2][2], 0.0));
  decomp->quaternion[3] =
      0.5 * std::sqrt(std::max(1.0 + row[0][0] + row[1][1] + row[2][2], 0.0));

  if (row[2][1] > row[1][2])
      decomp->quaternion[0] = -decomp->quaternion[0];
  if (row[0][2] > row[2][0])
      decomp->quaternion[1] = -decomp->quaternion[1];
  if (row[1][0] > row[0][1])
      decomp->quaternion[2] = -decomp->quaternion[2];

  return true;
}

// Taken from http://www.w3.org/TR/css3-transforms/.
Transform ComposeTransform(const DecomposedTransform& decomp) {
  SkMatrix44 matrix;
  for (int i = 0; i < 4; i++)
    matrix.setDouble(3, i, decomp.perspective[i]);

  SkMatrix44 tempTranslation;
  tempTranslation.setTranslate(SkDoubleToMScalar(decomp.translate[0]),
                               SkDoubleToMScalar(decomp.translate[1]),
                               SkDoubleToMScalar(decomp.translate[2]));
  matrix.preConcat(tempTranslation);

  double x = decomp.quaternion[0];
  double y = decomp.quaternion[1];
  double z = decomp.quaternion[2];
  double w = decomp.quaternion[3];

  SkMatrix44 rotation_matrix;
  rotation_matrix.setDouble(0, 0, 1.0 - 2.0 * (y * y + z * z));
  rotation_matrix.setDouble(0, 1, 2.0 * (x * y - z * w));
  rotation_matrix.setDouble(0, 2, 2.0 * (x * z + y * w));
  rotation_matrix.setDouble(1, 0, 2.0 * (x * y + z * w));
  rotation_matrix.setDouble(1, 1, 1.0 - 2.0 * (x * x + z * z));
  rotation_matrix.setDouble(1, 2, 2.0 * (y * z - x * w));
  rotation_matrix.setDouble(2, 0, 2.0 * (x * z - y * w));
  rotation_matrix.setDouble(2, 1, 2.0 * (y * z + x * w));
  rotation_matrix.setDouble(2, 2, 1.0 - 2.0 * (x * x + y * y));

  matrix.preConcat(rotation_matrix);

  SkMatrix44 temp;
  if (decomp.skew[2]) {
    temp.setDouble(1, 2, decomp.skew[2]);
    matrix.preConcat(temp);
  }

  if (decomp.skew[1]) {
    temp.setDouble(1, 2, 0);
    temp.setDouble(0, 2, decomp.skew[1]);
    matrix.preConcat(temp);
  }

  if (decomp.skew[0]) {
    temp.setDouble(0, 2, 0);
    temp.setDouble(0, 1, decomp.skew[0]);
    matrix.preConcat(temp);
  }

  SkMatrix44 tempScale;
  tempScale.setScale(SkDoubleToMScalar(decomp.scale[0]),
                     SkDoubleToMScalar(decomp.scale[1]),
                     SkDoubleToMScalar(decomp.scale[2]));
  matrix.preConcat(tempScale);

  Transform to_return;
  to_return.matrix() = matrix;
  return to_return;
}

}  // namespace ui
