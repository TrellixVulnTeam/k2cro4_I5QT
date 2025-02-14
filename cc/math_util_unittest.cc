// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/math_util.h"

#include <cmath>

#include "cc/test/geometry_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/rect_f.h"
#include <public/WebTransformationMatrix.h>

using WebKit::WebTransformationMatrix;

namespace cc {
namespace {

TEST(MathUtilTest, verifyBackfaceVisibilityBasicCases)
{
    WebTransformationMatrix transform;

    transform.makeIdentity();
    EXPECT_FALSE(transform.isBackFaceVisible());

    transform.makeIdentity();
    transform.rotate3d(0, 80, 0);
    EXPECT_FALSE(transform.isBackFaceVisible());

    transform.makeIdentity();
    transform.rotate3d(0, 100, 0);
    EXPECT_TRUE(transform.isBackFaceVisible());

    // Edge case, 90 degree rotation should return false.
    transform.makeIdentity();
    transform.rotate3d(0, 90, 0);
    EXPECT_FALSE(transform.isBackFaceVisible());
}

TEST(MathUtilTest, verifyBackfaceVisibilityForPerspective)
{
    WebTransformationMatrix layerSpaceToProjectionPlane;

    // This tests if isBackFaceVisible works properly under perspective transforms.
    // Specifically, layers that may have their back face visible in orthographic
    // projection, may not actually have back face visible under perspective projection.

    // Case 1: Layer is rotated by slightly more than 90 degrees, at the center of the
    //         prespective projection. In this case, the layer's back-side is visible to
    //         the camera.
    layerSpaceToProjectionPlane.makeIdentity();
    layerSpaceToProjectionPlane.applyPerspective(1);
    layerSpaceToProjectionPlane.translate3d(0, 0, 0);
    layerSpaceToProjectionPlane.rotate3d(0, 100, 0);
    EXPECT_TRUE(layerSpaceToProjectionPlane.isBackFaceVisible());

    // Case 2: Layer is rotated by slightly more than 90 degrees, but shifted off to the
    //         side of the camera. Because of the wide field-of-view, the layer's front
    //         side is still visible.
    //
    //                       |<-- front side of layer is visible to perspective camera
    //                    \  |            /
    //                     \ |           /
    //                      \|          /
    //                       |         /
    //                       |\       /<-- camera field of view
    //                       | \     /
    // back side of layer -->|  \   /
    //                           \./ <-- camera origin
    //
    layerSpaceToProjectionPlane.makeIdentity();
    layerSpaceToProjectionPlane.applyPerspective(1);
    layerSpaceToProjectionPlane.translate3d(-10, 0, 0);
    layerSpaceToProjectionPlane.rotate3d(0, 100, 0);
    EXPECT_FALSE(layerSpaceToProjectionPlane.isBackFaceVisible());

    // Case 3: Additionally rotating the layer by 180 degrees should of course show the
    //         opposite result of case 2.
    layerSpaceToProjectionPlane.rotate3d(0, 180, 0);
    EXPECT_TRUE(layerSpaceToProjectionPlane.isBackFaceVisible());
}

TEST(MathUtilTest, verifyProjectionOfPerpendicularPlane)
{
    // In this case, the m33() element of the transform becomes zero, which could cause a
    // divide-by-zero when projecting points/quads.

    WebTransformationMatrix transform;
    transform.makeIdentity();
    transform.setM33(0);

    gfx::RectF rect = gfx::RectF(0, 0, 1, 1);
    gfx::RectF projectedRect = MathUtil::projectClippedRect(transform, rect);

    EXPECT_EQ(0, projectedRect.x());
    EXPECT_EQ(0, projectedRect.y());
    EXPECT_TRUE(projectedRect.IsEmpty());
}

TEST(MathUtilTest, verifyEnclosingClippedRectUsesCorrectInitialBounds)
{
    HomogeneousCoordinate h1(-100, -100, 0, 1);
    HomogeneousCoordinate h2(-10, -10, 0, 1);
    HomogeneousCoordinate h3(10, 10, 0, -1);
    HomogeneousCoordinate h4(100, 100, 0, -1);

    // The bounds of the enclosing clipped rect should be -100 to -10 for both x and y.
    // However, if there is a bug where the initial xmin/xmax/ymin/ymax are initialized to
    // numeric_limits<float>::min() (which is zero, not -flt_max) then the enclosing
    // clipped rect will be computed incorrectly.
    gfx::RectF result = MathUtil::computeEnclosingClippedRect(h1, h2, h3, h4);

    EXPECT_FLOAT_RECT_EQ(gfx::RectF(gfx::PointF(-100, -100), gfx::SizeF(90, 90)), result);
}

TEST(MathUtilTest, verifyEnclosingRectOfVerticesUsesCorrectInitialBounds)
{
    gfx::PointF vertices[3];
    int numVertices = 3;

    vertices[0] = gfx::PointF(-10, -100);
    vertices[1] = gfx::PointF(-100, -10);
    vertices[2] = gfx::PointF(-30, -30);

    // The bounds of the enclosing rect should be -100 to -10 for both x and y. However,
    // if there is a bug where the initial xmin/xmax/ymin/ymax are initialized to
    // numeric_limits<float>::min() (which is zero, not -flt_max) then the enclosing
    // clipped rect will be computed incorrectly.
    gfx::RectF result = MathUtil::computeEnclosingRectOfVertices(vertices, numVertices);

    EXPECT_FLOAT_RECT_EQ(gfx::RectF(gfx::PointF(-100, -100), gfx::SizeF(90, 90)), result);
}

TEST(MathUtilTest, smallestAngleBetweenVectors)
{
    gfx::Vector2dF x(1, 0);
    gfx::Vector2dF y(0, 1);
    gfx::Vector2dF testVector(0.5, 0.5);

    // Orthogonal vectors are at an angle of 90 degress.
    EXPECT_EQ(90, MathUtil::smallestAngleBetweenVectors(x, y));

    // A vector makes a zero angle with itself.
    EXPECT_EQ(0, MathUtil::smallestAngleBetweenVectors(x, x));
    EXPECT_EQ(0, MathUtil::smallestAngleBetweenVectors(y, y));
    EXPECT_EQ(0, MathUtil::smallestAngleBetweenVectors(testVector, testVector));

    // Parallel but reversed vectors are at 180 degrees.
    EXPECT_FLOAT_EQ(180, MathUtil::smallestAngleBetweenVectors(x, -x));
    EXPECT_FLOAT_EQ(180, MathUtil::smallestAngleBetweenVectors(y, -y));
    EXPECT_FLOAT_EQ(180, MathUtil::smallestAngleBetweenVectors(testVector, -testVector));

    // The test vector is at a known angle.
    EXPECT_FLOAT_EQ(45, std::floor(MathUtil::smallestAngleBetweenVectors(testVector, x)));
    EXPECT_FLOAT_EQ(45, std::floor(MathUtil::smallestAngleBetweenVectors(testVector, y)));
}

TEST(MathUtilTest, vectorProjection)
{
    gfx::Vector2dF x(1, 0);
    gfx::Vector2dF y(0, 1);
    gfx::Vector2dF testVector(0.3f, 0.7f);

    // Orthogonal vectors project to a zero vector.
    EXPECT_VECTOR_EQ(gfx::Vector2dF(0, 0), MathUtil::projectVector(x, y));
    EXPECT_VECTOR_EQ(gfx::Vector2dF(0, 0), MathUtil::projectVector(y, x));

    // Projecting a vector onto the orthonormal basis gives the corresponding component of the
    // vector.
    EXPECT_VECTOR_EQ(gfx::Vector2dF(testVector.x(), 0), MathUtil::projectVector(testVector, x));
    EXPECT_VECTOR_EQ(gfx::Vector2dF(0, testVector.y()), MathUtil::projectVector(testVector, y));

    // Finally check than an arbitrary vector projected to another one gives a vector parallel to
    // the second vector.
    gfx::Vector2dF targetVector(0.5, 0.2f);
    gfx::Vector2dF projectedVector = MathUtil::projectVector(testVector, targetVector);
    EXPECT_EQ(projectedVector.x() / targetVector.x(),
              projectedVector.y() / targetVector.y());
}

// TODO(shawnsingh): these macros are redundant with those from
// web_transformation_matrix_unittests, but for now they
// are different enough to be appropriate here.

#define EXPECT_ROW1_EQ(a, b, c, d, transform)                 \
    EXPECT_FLOAT_EQ((a), (transform).matrix().getDouble(0, 0));     \
    EXPECT_FLOAT_EQ((b), (transform).matrix().getDouble(0, 1));     \
    EXPECT_FLOAT_EQ((c), (transform).matrix().getDouble(0, 2));     \
    EXPECT_FLOAT_EQ((d), (transform).matrix().getDouble(0, 3));

#define EXPECT_ROW2_EQ(a, b, c, d, transform)                 \
    EXPECT_FLOAT_EQ((a), (transform).matrix().getDouble(1, 0));     \
    EXPECT_FLOAT_EQ((b), (transform).matrix().getDouble(1, 1));     \
    EXPECT_FLOAT_EQ((c), (transform).matrix().getDouble(1, 2));     \
    EXPECT_FLOAT_EQ((d), (transform).matrix().getDouble(1, 3));

#define EXPECT_ROW3_EQ(a, b, c, d, transform)              \
    EXPECT_FLOAT_EQ((a), (transform).matrix().getDouble(2, 0));  \
    EXPECT_FLOAT_EQ((b), (transform).matrix().getDouble(2, 1));  \
    EXPECT_FLOAT_EQ((c), (transform).matrix().getDouble(2, 2));  \
    EXPECT_FLOAT_EQ((d), (transform).matrix().getDouble(2, 3));

#define EXPECT_ROW4_EQ(a, b, c, d, transform)              \
    EXPECT_FLOAT_EQ((a), (transform).matrix().getDouble(3, 0));  \
    EXPECT_FLOAT_EQ((b), (transform).matrix().getDouble(3, 1));  \
    EXPECT_FLOAT_EQ((c), (transform).matrix().getDouble(3, 2));  \
    EXPECT_FLOAT_EQ((d), (transform).matrix().getDouble(3, 3));

// Checking float values for equality close to zero is not robust using EXPECT_FLOAT_EQ
// (see gtest documentation). So, to verify rotation matrices, we must use a looser
// absolute error threshold in some places.
#define EXPECT_ROW1_NEAR(a, b, c, d, transform, errorThreshold)            \
    EXPECT_NEAR((a), (transform).matrix().getDouble(0, 0), (errorThreshold));    \
    EXPECT_NEAR((b), (transform).matrix().getDouble(0, 1), (errorThreshold));    \
    EXPECT_NEAR((c), (transform).matrix().getDouble(0, 2), (errorThreshold));    \
    EXPECT_NEAR((d), (transform).matrix().getDouble(0, 3), (errorThreshold));

#define EXPECT_ROW2_NEAR(a, b, c, d, transform, errorThreshold)            \
    EXPECT_NEAR((a), (transform).matrix().getDouble(1, 0), (errorThreshold));    \
    EXPECT_NEAR((b), (transform).matrix().getDouble(1, 1), (errorThreshold));    \
    EXPECT_NEAR((c), (transform).matrix().getDouble(1, 2), (errorThreshold));    \
    EXPECT_NEAR((d), (transform).matrix().getDouble(1, 3), (errorThreshold));

#define EXPECT_ROW3_NEAR(a, b, c, d, transform, errorThreshold)            \
    EXPECT_NEAR((a), (transform).matrix().getDouble(2, 0), (errorThreshold));    \
    EXPECT_NEAR((b), (transform).matrix().getDouble(2, 1), (errorThreshold));    \
    EXPECT_NEAR((c), (transform).matrix().getDouble(2, 2), (errorThreshold));    \
    EXPECT_NEAR((d), (transform).matrix().getDouble(2, 3), (errorThreshold));

#define ERROR_THRESHOLD 1e-14
#define LOOSE_ERROR_THRESHOLD 1e-7

static void initializeTestMatrix(gfx::Transform* transform)
{
    SkMatrix44& matrix = transform->matrix();
    matrix.setDouble(0, 0, 10);
    matrix.setDouble(1, 0, 11);
    matrix.setDouble(2, 0, 12);
    matrix.setDouble(3, 0, 13);
    matrix.setDouble(0, 1, 14);
    matrix.setDouble(1, 1, 15);
    matrix.setDouble(2, 1, 16);
    matrix.setDouble(3, 1, 17);
    matrix.setDouble(0, 2, 18);
    matrix.setDouble(1, 2, 19);
    matrix.setDouble(2, 2, 20);
    matrix.setDouble(3, 2, 21);
    matrix.setDouble(0, 3, 22);
    matrix.setDouble(1, 3, 23);
    matrix.setDouble(2, 3, 24);
    matrix.setDouble(3, 3, 25);

    // Sanity check
    EXPECT_ROW1_EQ(10, 14, 18, 22, (*transform));
    EXPECT_ROW2_EQ(11, 15, 19, 23, (*transform));
    EXPECT_ROW3_EQ(12, 16, 20, 24, (*transform));
    EXPECT_ROW4_EQ(13, 17, 21, 25, (*transform));
}

static void initializeTestMatrix2(gfx::Transform* transform)
{
    SkMatrix44& matrix = transform->matrix();
    matrix.setDouble(0, 0, 30);
    matrix.setDouble(1, 0, 31);
    matrix.setDouble(2, 0, 32);
    matrix.setDouble(3, 0, 33);
    matrix.setDouble(0, 1, 34);
    matrix.setDouble(1, 1, 35);
    matrix.setDouble(2, 1, 36);
    matrix.setDouble(3, 1, 37);
    matrix.setDouble(0, 2, 38);
    matrix.setDouble(1, 2, 39);
    matrix.setDouble(2, 2, 40);
    matrix.setDouble(3, 2, 41);
    matrix.setDouble(0, 3, 42);
    matrix.setDouble(1, 3, 43);
    matrix.setDouble(2, 3, 44);
    matrix.setDouble(3, 3, 45);

    // Sanity check
    EXPECT_ROW1_EQ(30, 34, 38, 42, (*transform));
    EXPECT_ROW2_EQ(31, 35, 39, 43, (*transform));
    EXPECT_ROW3_EQ(32, 36, 40, 44, (*transform));
    EXPECT_ROW4_EQ(33, 37, 41, 45, (*transform));
}

TEST(MathUtilGfxTransformTest, verifyDefaultConstructorCreatesIdentityMatrix)
{
    gfx::Transform A;
    EXPECT_ROW1_EQ(1, 0, 0, 0, A);
    EXPECT_ROW2_EQ(0, 1, 0, 0, A);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
    EXPECT_TRUE(MathUtil::isIdentity(A));
}

TEST(MathUtilGfxTransformTest, verifyCreateGfxTransformFor2dElements)
{
    gfx::Transform A = MathUtil::createGfxTransform(1, 2, 3, 4, 5, 6);
    EXPECT_ROW1_EQ(1, 3, 0, 5, A);
    EXPECT_ROW2_EQ(2, 4, 0, 6, A);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyCreateGfxTransformForAllElements)
{
    gfx::Transform A = MathUtil::createGfxTransform(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    EXPECT_ROW1_EQ(1, 5,  9, 13, A);
    EXPECT_ROW2_EQ(2, 6, 10, 14, A);
    EXPECT_ROW3_EQ(3, 7, 11, 15, A);
    EXPECT_ROW4_EQ(4, 8, 12, 16, A);
}

TEST(MathUtilGfxTransformTest, verifyCopyConstructor)
{
    gfx::Transform A;
    initializeTestMatrix(&A);

    // Copy constructor should produce exact same elements as matrix A.
    gfx::Transform B(A);
    EXPECT_ROW1_EQ(10, 14, 18, 22, B);
    EXPECT_ROW2_EQ(11, 15, 19, 23, B);
    EXPECT_ROW3_EQ(12, 16, 20, 24, B);
    EXPECT_ROW4_EQ(13, 17, 21, 25, B);
}

TEST(MathUtilGfxTransformTest, verifyMatrixInversion)
{
    // Invert a translation
    gfx::Transform translation;
    translation.PreconcatTranslate3d(2, 3, 4);
    EXPECT_TRUE(MathUtil::isInvertible(translation));

    gfx::Transform inverseTranslation = MathUtil::inverse(translation);
    EXPECT_ROW1_EQ(1, 0, 0, -2, inverseTranslation);
    EXPECT_ROW2_EQ(0, 1, 0, -3, inverseTranslation);
    EXPECT_ROW3_EQ(0, 0, 1, -4, inverseTranslation);
    EXPECT_ROW4_EQ(0, 0, 0,  1, inverseTranslation);

    // Note that inversion should not have changed the original matrix.
    EXPECT_ROW1_EQ(1, 0, 0, 2, translation);
    EXPECT_ROW2_EQ(0, 1, 0, 3, translation);
    EXPECT_ROW3_EQ(0, 0, 1, 4, translation);
    EXPECT_ROW4_EQ(0, 0, 0, 1, translation);

    // Invert a non-uniform scale
    gfx::Transform scale;
    scale.PreconcatScale3d(4, 10, 100);
    EXPECT_TRUE(MathUtil::isInvertible(scale));

    gfx::Transform inverseScale = MathUtil::inverse(scale);
    EXPECT_ROW1_EQ(0.25,   0,    0, 0, inverseScale);
    EXPECT_ROW2_EQ(0,    .1f,    0, 0, inverseScale);
    EXPECT_ROW3_EQ(0,      0, .01f, 0, inverseScale);
    EXPECT_ROW4_EQ(0,      0,    0, 1, inverseScale);

    // Try to invert a matrix that is not invertible.
    // The inverse() function should simply return an identity matrix.
    gfx::Transform notInvertible;
    notInvertible.matrix().setDouble(0, 0, 0);
    notInvertible.matrix().setDouble(1, 1, 0);
    notInvertible.matrix().setDouble(2, 2, 0);
    notInvertible.matrix().setDouble(3, 3, 0);
    EXPECT_FALSE(MathUtil::isInvertible(notInvertible));

    gfx::Transform inverseOfNotInvertible;
    initializeTestMatrix(&inverseOfNotInvertible); // initialize this to something non-identity, to make sure that assignment below actually took place.
    inverseOfNotInvertible = MathUtil::inverse(notInvertible);
    EXPECT_TRUE(MathUtil::isIdentity(inverseOfNotInvertible));
}

TEST(MathUtilGfxTransformTest, verifyTo2DTransform)
{
    gfx::Transform A;
    initializeTestMatrix(&A);

    gfx::Transform B = MathUtil::to2dTransform(A);

    EXPECT_ROW1_EQ(10, 14, 0, 22, B);
    EXPECT_ROW2_EQ(11, 15, 0, 23, B);
    EXPECT_ROW3_EQ(0,  0,  1, 0,  B);
    EXPECT_ROW4_EQ(13, 17, 0, 25, B);

    // Note that to2DTransform should not have changed the original matrix.
    EXPECT_ROW1_EQ(10, 14, 18, 22, A);
    EXPECT_ROW2_EQ(11, 15, 19, 23, A);
    EXPECT_ROW3_EQ(12, 16, 20, 24, A);
    EXPECT_ROW4_EQ(13, 17, 21, 25, A);
}

TEST(MathUtilGfxTransformTest, verifyAssignmentOperator)
{
    gfx::Transform A;
    initializeTestMatrix(&A);
    gfx::Transform B;
    initializeTestMatrix2(&B);
    gfx::Transform C;
    initializeTestMatrix2(&C);
    C = B = A;

    // Both B and C should now have been re-assigned to the value of A.
    EXPECT_ROW1_EQ(10, 14, 18, 22, B);
    EXPECT_ROW2_EQ(11, 15, 19, 23, B);
    EXPECT_ROW3_EQ(12, 16, 20, 24, B);
    EXPECT_ROW4_EQ(13, 17, 21, 25, B);

    EXPECT_ROW1_EQ(10, 14, 18, 22, C);
    EXPECT_ROW2_EQ(11, 15, 19, 23, C);
    EXPECT_ROW3_EQ(12, 16, 20, 24, C);
    EXPECT_ROW4_EQ(13, 17, 21, 25, C);
}

TEST(MathUtilGfxTransformTest, verifyEqualsBooleanOperator)
{
    gfx::Transform A;
    initializeTestMatrix(&A);

    gfx::Transform B;
    initializeTestMatrix(&B);
    EXPECT_TRUE(A == B);

    // Modifying multiple elements should cause equals operator to return false.
    gfx::Transform C;
    initializeTestMatrix2(&C);
    EXPECT_FALSE(A == C);

    // Modifying any one individual element should cause equals operator to return false.
    gfx::Transform D;
    D = A;
    D.matrix().setDouble(0, 0, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(1, 0, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(2, 0, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(3, 0, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(0, 1, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(1, 1, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(2, 1, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(3, 1, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(0, 2, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(1, 2, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(2, 2, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(3, 2, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(0, 3, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(1, 3, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(2, 3, 0);
    EXPECT_FALSE(A == D);

    D = A;
    D.matrix().setDouble(3, 3, 0);
    EXPECT_FALSE(A == D);
}

TEST(MathUtilGfxTransformTest, verifyMultiplyOperator)
{
    gfx::Transform A;
    initializeTestMatrix(&A);

    gfx::Transform B;
    initializeTestMatrix2(&B);

    gfx::Transform C = A * B;
    EXPECT_ROW1_EQ(2036, 2292, 2548, 2804, C);
    EXPECT_ROW2_EQ(2162, 2434, 2706, 2978, C);
    EXPECT_ROW3_EQ(2288, 2576, 2864, 3152, C);
    EXPECT_ROW4_EQ(2414, 2718, 3022, 3326, C);

    // Just an additional sanity check; matrix multiplication is not commutative.
    EXPECT_FALSE(A * B == B * A);
}

TEST(MathUtilGfxTransformTest, verifyMatrixMultiplication)
{
    gfx::Transform A;
    initializeTestMatrix(&A);

    gfx::Transform B;
    initializeTestMatrix2(&B);

    A.PreconcatTransform(B);
    EXPECT_ROW1_EQ(2036, 2292, 2548, 2804, A);
    EXPECT_ROW2_EQ(2162, 2434, 2706, 2978, A);
    EXPECT_ROW3_EQ(2288, 2576, 2864, 3152, A);
    EXPECT_ROW4_EQ(2414, 2718, 3022, 3326, A);
}

TEST(MathUtilGfxTransformTest, verifyMakeIdentiy)
{
    gfx::Transform A;
    initializeTestMatrix(&A);
    MathUtil::makeIdentity(&A);
    EXPECT_ROW1_EQ(1, 0, 0, 0, A);
    EXPECT_ROW2_EQ(0, 1, 0, 0, A);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
    EXPECT_TRUE(MathUtil::isIdentity(A));
}

TEST(MathUtilGfxTransformTest, verifyTranslate)
{
    gfx::Transform A;
    A.PreconcatTranslate(2, 3);
    EXPECT_ROW1_EQ(1, 0, 0, 2, A);
    EXPECT_ROW2_EQ(0, 1, 0, 3, A);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that PreconcatTranslate() post-multiplies the existing matrix.
    MathUtil::makeIdentity(&A);
    A.PreconcatScale(5, 5);
    A.PreconcatTranslate(2, 3);
    EXPECT_ROW1_EQ(5, 0, 0, 10, A);
    EXPECT_ROW2_EQ(0, 5, 0, 15, A);
    EXPECT_ROW3_EQ(0, 0, 1, 0,  A);
    EXPECT_ROW4_EQ(0, 0, 0, 1,  A);
}

TEST(MathUtilGfxTransformTest, verifyTranslate3d)
{
    gfx::Transform A;
    A.PreconcatTranslate3d(2, 3, 4);
    EXPECT_ROW1_EQ(1, 0, 0, 2, A);
    EXPECT_ROW2_EQ(0, 1, 0, 3, A);
    EXPECT_ROW3_EQ(0, 0, 1, 4, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that PreconcatTranslate3d() post-multiplies the existing matrix.
    MathUtil::makeIdentity(&A);
    A.PreconcatScale3d(6, 7, 8);
    A.PreconcatTranslate3d(2, 3, 4);
    EXPECT_ROW1_EQ(6, 0, 0, 12, A);
    EXPECT_ROW2_EQ(0, 7, 0, 21, A);
    EXPECT_ROW3_EQ(0, 0, 8, 32, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1,  A);
}

TEST(MathUtilGfxTransformTest, verifyScale)
{
    gfx::Transform A;
    A.PreconcatScale(6, 7);
    EXPECT_ROW1_EQ(6, 0, 0, 0, A);
    EXPECT_ROW2_EQ(0, 7, 0, 0, A);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that PreconcatScale() post-multiplies the existing matrix.
    MathUtil::makeIdentity(&A);
    A.PreconcatTranslate3d(2, 3, 4);
    A.PreconcatScale(6, 7);
    EXPECT_ROW1_EQ(6, 0, 0, 2, A);
    EXPECT_ROW2_EQ(0, 7, 0, 3, A);
    EXPECT_ROW3_EQ(0, 0, 1, 4, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyScale3d)
{
    gfx::Transform A;
    A.PreconcatScale3d(6, 7, 8);
    EXPECT_ROW1_EQ(6, 0, 0, 0, A);
    EXPECT_ROW2_EQ(0, 7, 0, 0, A);
    EXPECT_ROW3_EQ(0, 0, 8, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that scale3d() post-multiplies the existing matrix.
    MathUtil::makeIdentity(&A);
    A.PreconcatTranslate3d(2, 3, 4);
    A.PreconcatScale3d(6, 7, 8);
    EXPECT_ROW1_EQ(6, 0, 0, 2, A);
    EXPECT_ROW2_EQ(0, 7, 0, 3, A);
    EXPECT_ROW3_EQ(0, 0, 8, 4, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyRotate)
{
    gfx::Transform A;
    A.PreconcatRotate(90);
    EXPECT_ROW1_NEAR(0, -1, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_NEAR(1, 0, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that PreconcatRotate() post-multiplies the existing matrix.
    MathUtil::makeIdentity(&A);
    A.PreconcatScale3d(6, 7, 8);
    A.PreconcatRotate(90);
    EXPECT_ROW1_NEAR(0, -6, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_NEAR(7, 0,  0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_EQ(0, 0, 8, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyRotateEulerAngles)
{
    gfx::Transform A;

    // Check rotation about z-axis
    MathUtil::makeIdentity(&A);
    MathUtil::rotateEulerAngles(&A, 0, 0, 90);
    EXPECT_ROW1_NEAR(0, -1, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_NEAR(1, 0, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Check rotation about x-axis
    MathUtil::makeIdentity(&A);
    MathUtil::rotateEulerAngles(&A, 90, 0, 0);
    EXPECT_ROW1_EQ(1, 0, 0, 0, A);
    EXPECT_ROW2_NEAR(0, 0, -1, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_NEAR(0, 1, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Check rotation about y-axis.
    // Note carefully, the expected pattern is inverted compared to rotating about x axis or z axis.
    MathUtil::makeIdentity(&A);
    MathUtil::rotateEulerAngles(&A, 0, 90, 0);
    EXPECT_ROW1_NEAR(0, 0, 1, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_EQ(0, 1, 0, 0, A);
    EXPECT_ROW3_NEAR(-1, 0, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that rotate3d(rx, ry, rz) post-multiplies the existing matrix.
    MathUtil::makeIdentity(&A);
    A.PreconcatScale3d(6, 7, 8);
    MathUtil::rotateEulerAngles(&A, 0, 0, 90);
    EXPECT_ROW1_NEAR(0, -6, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_NEAR(7, 0,  0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_EQ(0, 0, 8, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyRotateEulerAnglesOrderOfCompositeRotations)
{
    // Rotate3d(degreesX, degreesY, degreesZ) is actually composite transform consiting of
    // three primitive rotations. This test verifies that the ordering of those three
    // transforms is the intended ordering.
    //
    // The correct ordering for this test case should be:
    //   1. rotate by 30 degrees about z-axis
    //   2. rotate by 20 degrees about y-axis
    //   3. rotate by 10 degrees about x-axis
    //
    // Note: there are 6 possible orderings of 3 transforms. For the specific transforms
    // used in this test, all 6 combinations produce a unique matrix that is different
    // from the other orderings. That way, this test verifies the exact ordering.

    gfx::Transform A;
    MathUtil::makeIdentity(&A);
    MathUtil::rotateEulerAngles(&A, 10, 20, 30);

    EXPECT_ROW1_NEAR(0.8137976813493738026394908,
                     -0.4409696105298823720630708,
                     0.3785223063697923939763257,
                     0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_NEAR(0.4698463103929541584413698,
                     0.8825641192593856043657752,
                     0.0180283112362972230968694,
                     0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_NEAR(-0.3420201433256686573969318,
                     0.1631759111665348205288950,
                     0.9254165783983233639631294,
                     0, A, ERROR_THRESHOLD);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyRotateAxisAngleForAlignedAxes)
{
    gfx::Transform A;

    // Check rotation about z-axis
    MathUtil::makeIdentity(&A);
    MathUtil::rotateAxisAngle(&A, 0, 0, 1, 90);
    EXPECT_ROW1_NEAR(0, -1, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_NEAR(1, 0, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Check rotation about x-axis
    MathUtil::makeIdentity(&A);
    MathUtil::rotateAxisAngle(&A, 1, 0, 0, 90);
    EXPECT_ROW1_EQ(1, 0, 0, 0, A);
    EXPECT_ROW2_NEAR(0, 0, -1, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_NEAR(0, 1, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Check rotation about y-axis.
    // Note carefully, the expected pattern is inverted compared to rotating about x axis or z axis.
    MathUtil::makeIdentity(&A);
    MathUtil::rotateAxisAngle(&A, 0, 1, 0, 90);
    EXPECT_ROW1_NEAR(0, 0, 1, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_EQ(0, 1, 0, 0, A);
    EXPECT_ROW3_NEAR(-1, 0, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that rotate3d(axis, angle) post-multiplies the existing matrix.
    MathUtil::makeIdentity(&A);
    A.PreconcatScale3d(6, 7, 8);
    MathUtil::rotateAxisAngle(&A, 0, 0, 1, 90);
    EXPECT_ROW1_NEAR(0, -6, 0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_NEAR(7, 0,  0, 0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_EQ(0, 0, 8, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyRotateAxisAngleForArbitraryAxis)
{
    // Check rotation about an arbitrary non-axis-aligned vector.
    gfx::Transform A;
    MathUtil::rotateAxisAngle(&A, 1, 1, 1, 90);
    EXPECT_ROW1_NEAR(0.3333333333333334258519187,
                     -0.2440169358562924717404030,
                     0.9106836025229592124219380,
                     0, A, ERROR_THRESHOLD);
    EXPECT_ROW2_NEAR(0.9106836025229592124219380,
                     0.3333333333333334258519187,
                     -0.2440169358562924717404030,
                     0, A, ERROR_THRESHOLD);
    EXPECT_ROW3_NEAR(-0.2440169358562924717404030,
                     0.9106836025229592124219380,
                     0.3333333333333334258519187,
                     0, A, ERROR_THRESHOLD);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyRotateAxisAngleForDegenerateAxis)
{
    // Check rotation about a degenerate zero vector.
    // It is expected to skip applying the rotation.
    gfx::Transform A;

    MathUtil::rotateAxisAngle(&A, 0, 0, 0, 45);
    // Verify that A remains unchanged.
    EXPECT_TRUE(MathUtil::isIdentity(A));

    initializeTestMatrix(&A);
    MathUtil::rotateAxisAngle(&A, 0, 0, 0, 35);

    // Verify that A remains unchanged.
    EXPECT_ROW1_EQ(10, 14, 18, 22, A);
    EXPECT_ROW2_EQ(11, 15, 19, 23, A);
    EXPECT_ROW3_EQ(12, 16, 20, 24, A);
    EXPECT_ROW4_EQ(13, 17, 21, 25, A);
}

TEST(MathUtilGfxTransformTest, verifySkewX)
{
    gfx::Transform A;
    A.PreconcatSkewX(45);
    EXPECT_ROW1_EQ(1, 1, 0, 0, A);
    EXPECT_ROW2_EQ(0, 1, 0, 0, A);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that skewX() post-multiplies the existing matrix.
    // Row 1, column 2, would incorrectly have value "7" if the matrix is pre-multiplied instead of post-multiplied.
    MathUtil::makeIdentity(&A);
    A.PreconcatScale3d(6, 7, 8);
    A.PreconcatSkewX(45);
    EXPECT_ROW1_EQ(6, 6, 0, 0, A);
    EXPECT_ROW2_EQ(0, 7, 0, 0, A);
    EXPECT_ROW3_EQ(0, 0, 8, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifySkewY)
{
    gfx::Transform A;
    A.PreconcatSkewY(45);
    EXPECT_ROW1_EQ(1, 0, 0, 0, A);
    EXPECT_ROW2_EQ(1, 1, 0, 0, A);
    EXPECT_ROW3_EQ(0, 0, 1, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);

    // Verify that skewY() post-multiplies the existing matrix.
    // Row 2, column 1, would incorrectly have value "6" if the matrix is pre-multiplied instead of post-multiplied.
    MathUtil::makeIdentity(&A);
    A.PreconcatScale3d(6, 7, 8);
    A.PreconcatSkewY(45);
    EXPECT_ROW1_EQ(6, 0, 0, 0, A);
    EXPECT_ROW2_EQ(7, 7, 0, 0, A);
    EXPECT_ROW3_EQ(0, 0, 8, 0, A);
    EXPECT_ROW4_EQ(0, 0, 0, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyPerspectiveDepth)
{
    gfx::Transform A;
    A.PreconcatPerspectiveDepth(1);
    EXPECT_ROW1_EQ(1, 0,  0, 0, A);
    EXPECT_ROW2_EQ(0, 1,  0, 0, A);
    EXPECT_ROW3_EQ(0, 0,  1, 0, A);
    EXPECT_ROW4_EQ(0, 0, -1, 1, A);

    // Verify that PreconcatPerspectiveDepth() post-multiplies the existing matrix.
    MathUtil::makeIdentity(&A);
    A.PreconcatTranslate3d(2, 3, 4);
    A.PreconcatPerspectiveDepth(1);
    EXPECT_ROW1_EQ(1, 0, -2, 2, A);
    EXPECT_ROW2_EQ(0, 1, -3, 3, A);
    EXPECT_ROW3_EQ(0, 0, -3, 4, A);
    EXPECT_ROW4_EQ(0, 0, -1, 1, A);
}

TEST(MathUtilGfxTransformTest, verifyHasPerspective)
{
    gfx::Transform A;
    A.PreconcatPerspectiveDepth(1);
    EXPECT_TRUE(MathUtil::hasPerspective(A));

    MathUtil::makeIdentity(&A);
    A.PreconcatPerspectiveDepth(0);
    EXPECT_FALSE(MathUtil::hasPerspective(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 0, -1);
    EXPECT_TRUE(MathUtil::hasPerspective(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 1, -1);
    EXPECT_TRUE(MathUtil::hasPerspective(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 2, -0.3);
    EXPECT_TRUE(MathUtil::hasPerspective(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 3, 0.5);
    EXPECT_TRUE(MathUtil::hasPerspective(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 3, 0);
    EXPECT_TRUE(MathUtil::hasPerspective(A));
}

TEST(MathUtilGfxTransformTest, verifyIsInvertible)
{
    gfx::Transform A;

    // Translations, rotations, scales, skews and arbitrary combinations of them are invertible.
    MathUtil::makeIdentity(&A);
    EXPECT_TRUE(MathUtil::isInvertible(A));

    MathUtil::makeIdentity(&A);
    A.PreconcatTranslate3d(2, 3, 4);
    EXPECT_TRUE(MathUtil::isInvertible(A));

    MathUtil::makeIdentity(&A);
    A.PreconcatScale3d(6, 7, 8);
    EXPECT_TRUE(MathUtil::isInvertible(A));

    MathUtil::makeIdentity(&A);
    MathUtil::rotateEulerAngles(&A, 10, 20, 30);
    EXPECT_TRUE(MathUtil::isInvertible(A));

    MathUtil::makeIdentity(&A);
    A.PreconcatSkewX(45);
    EXPECT_TRUE(MathUtil::isInvertible(A));

    // A perspective matrix (projection plane at z=0) is invertible. The intuitive
    // explanation is that perspective is eqivalent to a skew of the w-axis; skews are
    // invertible.
    MathUtil::makeIdentity(&A);
    A.PreconcatPerspectiveDepth(1);
    EXPECT_TRUE(MathUtil::isInvertible(A));

    // A "pure" perspective matrix derived by similar triangles, with m44() set to zero
    // (i.e. camera positioned at the origin), is not invertible.
    MathUtil::makeIdentity(&A);
    A.PreconcatPerspectiveDepth(1);
    A.matrix().setDouble(3, 3, 0);
    EXPECT_FALSE(MathUtil::isInvertible(A));

    // Adding more to a non-invertible matrix will not make it invertible in the general case.
    MathUtil::makeIdentity(&A);
    A.PreconcatPerspectiveDepth(1);
    A.matrix().setDouble(3, 3, 0);
    A.PreconcatScale3d(6, 7, 8);
    MathUtil::rotateEulerAngles(&A, 10, 20, 30);
    A.PreconcatTranslate3d(6, 7, 8);
    EXPECT_FALSE(MathUtil::isInvertible(A));

    // A degenerate matrix of all zeros is not invertible.
    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 0, 0);
    A.matrix().setDouble(1, 1, 0);
    A.matrix().setDouble(2, 2, 0);
    A.matrix().setDouble(3, 3, 0);
    EXPECT_FALSE(MathUtil::isInvertible(A));
}

TEST(MathUtilGfxTransformTest, verifyIsIdentity)
{
    gfx::Transform A;

    initializeTestMatrix(&A);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    EXPECT_TRUE(MathUtil::isIdentity(A));

    // Modifying any one individual element should cause the matrix to no longer be identity.
    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(1, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(2, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 1, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(1, 1, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(2, 1, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 1, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 2, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(1, 2, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(2, 2, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 2, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 3, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(1, 3, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(2, 3, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 3, 2);
    EXPECT_FALSE(MathUtil::isIdentity(A));
}

TEST(MathUtilGfxTransformTest, verifyIsIdentityOrTranslation)
{
    gfx::Transform A;

    initializeTestMatrix(&A);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    EXPECT_TRUE(MathUtil::isIdentityOrTranslation(A));

    // Modifying any non-translation components should cause isIdentityOrTranslation() to
    // return false. NOTE: (0, 3), (1, 3), and (2, 3) are the translation components, so
    // modifying them should still return true for isIdentityOrTranslation().
    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(1, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(2, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 0, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(1, 1, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(2, 1, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 1, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 2, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(1, 2, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(2, 2, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 2, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));

    // Note carefully - expecting true here.
    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(0, 3, 2);
    EXPECT_TRUE(MathUtil::isIdentityOrTranslation(A));

    // Note carefully - expecting true here.
    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(1, 3, 2);
    EXPECT_TRUE(MathUtil::isIdentityOrTranslation(A));

    // Note carefully - expecting true here.
    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(2, 3, 2);
    EXPECT_TRUE(MathUtil::isIdentityOrTranslation(A));

    MathUtil::makeIdentity(&A);
    A.matrix().setDouble(3, 3, 2);
    EXPECT_FALSE(MathUtil::isIdentityOrTranslation(A));
}

}  // namespace
}  // namespace cc
