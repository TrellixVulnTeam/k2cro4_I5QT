// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/math_util.h"

#include <cmath>
#include <limits>

#include "ui/gfx/quad_f.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/rect_conversions.h"
#include "ui/gfx/rect_f.h"
#include "ui/gfx/vector2d_f.h"
#include <public/WebTransformationMatrix.h>

using WebKit::WebTransformationMatrix;

namespace cc {

const double MathUtil::PI_DOUBLE = 3.14159265358979323846;
const float MathUtil::PI_FLOAT = 3.14159265358979323846f;
const double MathUtil::EPSILON = 1e-9;

static HomogeneousCoordinate projectHomogeneousPoint(const WebTransformationMatrix& transform, const gfx::PointF& p)
{
    // In this case, the layer we are trying to project onto is perpendicular to ray
    // (point p and z-axis direction) that we are trying to project. This happens when the
    // layer is rotated so that it is infinitesimally thin, or when it is co-planar with
    // the camera origin -- i.e. when the layer is invisible anyway.
    if (!transform.m33())
        return HomogeneousCoordinate(0, 0, 0, 1);

    double x = p.x();
    double y = p.y();
    double z = -(transform.m13() * x + transform.m23() * y + transform.m43()) / transform.m33();
    // implicit definition of w = 1;

    double outX = x * transform.m11() + y * transform.m21() + z * transform.m31() + transform.m41();
    double outY = x * transform.m12() + y * transform.m22() + z * transform.m32() + transform.m42();
    double outZ = x * transform.m13() + y * transform.m23() + z * transform.m33() + transform.m43();
    double outW = x * transform.m14() + y * transform.m24() + z * transform.m34() + transform.m44();

    return HomogeneousCoordinate(outX, outY, outZ, outW);
}

static HomogeneousCoordinate mapHomogeneousPoint(const WebTransformationMatrix& transform, const gfx::Point3F& p)
{
    double x = p.x();
    double y = p.y();
    double z = p.z();
    // implicit definition of w = 1;

    double outX = x * transform.m11() + y * transform.m21() + z * transform.m31() + transform.m41();
    double outY = x * transform.m12() + y * transform.m22() + z * transform.m32() + transform.m42();
    double outZ = x * transform.m13() + y * transform.m23() + z * transform.m33() + transform.m43();
    double outW = x * transform.m14() + y * transform.m24() + z * transform.m34() + transform.m44();

    return HomogeneousCoordinate(outX, outY, outZ, outW);
}

static HomogeneousCoordinate computeClippedPointForEdge(const HomogeneousCoordinate& h1, const HomogeneousCoordinate& h2)
{
    // Points h1 and h2 form a line in 4d, and any point on that line can be represented
    // as an interpolation between h1 and h2:
    //    p = (1-t) h1 + (t) h2
    //
    // We want to compute point p such that p.w == epsilon, where epsilon is a small
    // non-zero number. (but the smaller the number is, the higher the risk of overflow)
    // To do this, we solve for t in the following equation:
    //    p.w = epsilon = (1-t) * h1.w + (t) * h2.w
    //
    // Once paramter t is known, the rest of p can be computed via p = (1-t) h1 + (t) h2.

    // Technically this is a special case of the following assertion, but its a good idea to keep it an explicit sanity check here.
    DCHECK(h2.w != h1.w);
    // Exactly one of h1 or h2 (but not both) must be on the negative side of the w plane when this is called.
    DCHECK(h1.shouldBeClipped() ^ h2.shouldBeClipped());

    double w = 0.00001; // or any positive non-zero small epsilon

    double t = (w - h1.w) / (h2.w - h1.w);

    double x = (1-t) * h1.x + t * h2.x;
    double y = (1-t) * h1.y + t * h2.y;
    double z = (1-t) * h1.z + t * h2.z;

    return HomogeneousCoordinate(x, y, z, w);
}

static inline void expandBoundsToIncludePoint(float& xmin, float& xmax, float& ymin, float& ymax, const gfx::PointF& p)
{
    xmin = std::min(p.x(), xmin);
    xmax = std::max(p.x(), xmax);
    ymin = std::min(p.y(), ymin);
    ymax = std::max(p.y(), ymax);
}

static inline void addVertexToClippedQuad(const gfx::PointF& newVertex, gfx::PointF clippedQuad[8], int& numVerticesInClippedQuad)
{
    clippedQuad[numVerticesInClippedQuad] = newVertex;
    numVerticesInClippedQuad++;
}

gfx::Rect MathUtil::mapClippedRect(const WebTransformationMatrix& transform, const gfx::Rect& srcRect)
{
    return gfx::ToEnclosingRect(mapClippedRect(transform, gfx::RectF(srcRect)));
}

gfx::RectF MathUtil::mapClippedRect(const WebTransformationMatrix& transform, const gfx::RectF& srcRect)
{
    if (transform.isIdentityOrTranslation())
        return srcRect + gfx::Vector2dF(static_cast<float>(transform.m41()), static_cast<float>(transform.m42()));

    // Apply the transform, but retain the result in homogeneous coordinates.
    gfx::QuadF q = gfx::QuadF(srcRect);
    HomogeneousCoordinate h1 = mapHomogeneousPoint(transform, gfx::Point3F(q.p1()));
    HomogeneousCoordinate h2 = mapHomogeneousPoint(transform, gfx::Point3F(q.p2()));
    HomogeneousCoordinate h3 = mapHomogeneousPoint(transform, gfx::Point3F(q.p3()));
    HomogeneousCoordinate h4 = mapHomogeneousPoint(transform, gfx::Point3F(q.p4()));

    return computeEnclosingClippedRect(h1, h2, h3, h4);
}

gfx::RectF MathUtil::projectClippedRect(const WebTransformationMatrix& transform, const gfx::RectF& srcRect)
{
    if (transform.isIdentityOrTranslation())
        return srcRect + gfx::Vector2dF(static_cast<float>(transform.m41()), static_cast<float>(transform.m42()));

    // Perform the projection, but retain the result in homogeneous coordinates.
    gfx::QuadF q = gfx::QuadF(srcRect);
    HomogeneousCoordinate h1 = projectHomogeneousPoint(transform, q.p1());
    HomogeneousCoordinate h2 = projectHomogeneousPoint(transform, q.p2());
    HomogeneousCoordinate h3 = projectHomogeneousPoint(transform, q.p3());
    HomogeneousCoordinate h4 = projectHomogeneousPoint(transform, q.p4());

    return computeEnclosingClippedRect(h1, h2, h3, h4);
}

void MathUtil::mapClippedQuad(const WebTransformationMatrix& transform, const gfx::QuadF& srcQuad, gfx::PointF clippedQuad[8], int& numVerticesInClippedQuad)
{
    HomogeneousCoordinate h1 = mapHomogeneousPoint(transform, gfx::Point3F(srcQuad.p1()));
    HomogeneousCoordinate h2 = mapHomogeneousPoint(transform, gfx::Point3F(srcQuad.p2()));
    HomogeneousCoordinate h3 = mapHomogeneousPoint(transform, gfx::Point3F(srcQuad.p3()));
    HomogeneousCoordinate h4 = mapHomogeneousPoint(transform, gfx::Point3F(srcQuad.p4()));

    // The order of adding the vertices to the array is chosen so that clockwise / counter-clockwise orientation is retained.

    numVerticesInClippedQuad = 0;

    if (!h1.shouldBeClipped())
        addVertexToClippedQuad(h1.cartesianPoint2d(), clippedQuad, numVerticesInClippedQuad);

    if (h1.shouldBeClipped() ^ h2.shouldBeClipped())
        addVertexToClippedQuad(computeClippedPointForEdge(h1, h2).cartesianPoint2d(), clippedQuad, numVerticesInClippedQuad);

    if (!h2.shouldBeClipped())
        addVertexToClippedQuad(h2.cartesianPoint2d(), clippedQuad, numVerticesInClippedQuad);

    if (h2.shouldBeClipped() ^ h3.shouldBeClipped())
        addVertexToClippedQuad(computeClippedPointForEdge(h2, h3).cartesianPoint2d(), clippedQuad, numVerticesInClippedQuad);

    if (!h3.shouldBeClipped())
        addVertexToClippedQuad(h3.cartesianPoint2d(), clippedQuad, numVerticesInClippedQuad);

    if (h3.shouldBeClipped() ^ h4.shouldBeClipped())
        addVertexToClippedQuad(computeClippedPointForEdge(h3, h4).cartesianPoint2d(), clippedQuad, numVerticesInClippedQuad);

    if (!h4.shouldBeClipped())
        addVertexToClippedQuad(h4.cartesianPoint2d(), clippedQuad, numVerticesInClippedQuad);

    if (h4.shouldBeClipped() ^ h1.shouldBeClipped())
        addVertexToClippedQuad(computeClippedPointForEdge(h4, h1).cartesianPoint2d(), clippedQuad, numVerticesInClippedQuad);

    DCHECK(numVerticesInClippedQuad <= 8);
}

gfx::RectF MathUtil::computeEnclosingRectOfVertices(gfx::PointF vertices[], int numVertices)
{
    if (numVertices < 2)
        return gfx::RectF();

    float xmin = std::numeric_limits<float>::max();
    float xmax = -std::numeric_limits<float>::max();
    float ymin = std::numeric_limits<float>::max();
    float ymax = -std::numeric_limits<float>::max();

    for (int i = 0; i < numVertices; ++i)
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, vertices[i]);

    return gfx::RectF(gfx::PointF(xmin, ymin), gfx::SizeF(xmax - xmin, ymax - ymin));
}

gfx::RectF MathUtil::computeEnclosingClippedRect(const HomogeneousCoordinate& h1, const HomogeneousCoordinate& h2, const HomogeneousCoordinate& h3, const HomogeneousCoordinate& h4)
{
    // This function performs clipping as necessary and computes the enclosing 2d
    // gfx::RectF of the vertices. Doing these two steps simultaneously allows us to avoid
    // the overhead of storing an unknown number of clipped vertices.

    // If no vertices on the quad are clipped, then we can simply return the enclosing rect directly.
    bool somethingClipped = h1.shouldBeClipped() || h2.shouldBeClipped() || h3.shouldBeClipped() || h4.shouldBeClipped();
    if (!somethingClipped) {
        gfx::QuadF mappedQuad = gfx::QuadF(h1.cartesianPoint2d(), h2.cartesianPoint2d(), h3.cartesianPoint2d(), h4.cartesianPoint2d());
        return mappedQuad.BoundingBox();
    }

    bool everythingClipped = h1.shouldBeClipped() && h2.shouldBeClipped() && h3.shouldBeClipped() && h4.shouldBeClipped();
    if (everythingClipped)
        return gfx::RectF();


    float xmin = std::numeric_limits<float>::max();
    float xmax = -std::numeric_limits<float>::max();
    float ymin = std::numeric_limits<float>::max();
    float ymax = -std::numeric_limits<float>::max();

    if (!h1.shouldBeClipped())
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, h1.cartesianPoint2d());

    if (h1.shouldBeClipped() ^ h2.shouldBeClipped())
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, computeClippedPointForEdge(h1, h2).cartesianPoint2d());

    if (!h2.shouldBeClipped())
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, h2.cartesianPoint2d());

    if (h2.shouldBeClipped() ^ h3.shouldBeClipped())
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, computeClippedPointForEdge(h2, h3).cartesianPoint2d());

    if (!h3.shouldBeClipped())
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, h3.cartesianPoint2d());

    if (h3.shouldBeClipped() ^ h4.shouldBeClipped())
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, computeClippedPointForEdge(h3, h4).cartesianPoint2d());

    if (!h4.shouldBeClipped())
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, h4.cartesianPoint2d());

    if (h4.shouldBeClipped() ^ h1.shouldBeClipped())
        expandBoundsToIncludePoint(xmin, xmax, ymin, ymax, computeClippedPointForEdge(h4, h1).cartesianPoint2d());

    return gfx::RectF(gfx::PointF(xmin, ymin), gfx::SizeF(xmax - xmin, ymax - ymin));
}

gfx::QuadF MathUtil::mapQuad(const WebTransformationMatrix& transform, const gfx::QuadF& q, bool& clipped)
{
    if (transform.isIdentityOrTranslation()) {
        gfx::QuadF mappedQuad(q);
        mappedQuad += gfx::Vector2dF(static_cast<float>(transform.m41()), static_cast<float>(transform.m42()));
        clipped = false;
        return mappedQuad;
    }

    HomogeneousCoordinate h1 = mapHomogeneousPoint(transform, gfx::Point3F(q.p1()));
    HomogeneousCoordinate h2 = mapHomogeneousPoint(transform, gfx::Point3F(q.p2()));
    HomogeneousCoordinate h3 = mapHomogeneousPoint(transform, gfx::Point3F(q.p3()));
    HomogeneousCoordinate h4 = mapHomogeneousPoint(transform, gfx::Point3F(q.p4()));

    clipped = h1.shouldBeClipped() || h2.shouldBeClipped() || h3.shouldBeClipped() || h4.shouldBeClipped();

    // Result will be invalid if clipped == true. But, compute it anyway just in case, to emulate existing behavior.
    return gfx::QuadF(h1.cartesianPoint2d(), h2.cartesianPoint2d(), h3.cartesianPoint2d(), h4.cartesianPoint2d());
}

gfx::PointF MathUtil::mapPoint(const WebTransformationMatrix& transform, const gfx::PointF& p, bool& clipped)
{
    HomogeneousCoordinate h = mapHomogeneousPoint(transform, gfx::Point3F(p));

    if (h.w > 0) {
        clipped = false;
        return h.cartesianPoint2d();
    }

    // The cartesian coordinates will be invalid after dividing by w.
    clipped = true;

    // Avoid dividing by w if w == 0.
    if (!h.w)
        return gfx::PointF();

    // This return value will be invalid because clipped == true, but (1) users of this
    // code should be ignoring the return value when clipped == true anyway, and (2) this
    // behavior is more consistent with existing behavior of WebKit transforms if the user
    // really does not ignore the return value.
    return h.cartesianPoint2d();
}

gfx::Point3F MathUtil::mapPoint(const WebTransformationMatrix& transform, const gfx::Point3F& p, bool& clipped)
{
    HomogeneousCoordinate h = mapHomogeneousPoint(transform, p);

    if (h.w > 0) {
        clipped = false;
        return h.cartesianPoint3d();
    }

    // The cartesian coordinates will be invalid after dividing by w.
    clipped = true;

    // Avoid dividing by w if w == 0.
    if (!h.w)
        return gfx::Point3F();

    // This return value will be invalid because clipped == true, but (1) users of this
    // code should be ignoring the return value when clipped == true anyway, and (2) this
    // behavior is more consistent with existing behavior of WebKit transforms if the user
    // really does not ignore the return value.
    return h.cartesianPoint3d();
}

gfx::QuadF MathUtil::projectQuad(const WebTransformationMatrix& transform, const gfx::QuadF& q, bool& clipped)
{
    gfx::QuadF projectedQuad;
    bool clippedPoint;
    projectedQuad.set_p1(projectPoint(transform, q.p1(), clippedPoint));
    clipped = clippedPoint;
    projectedQuad.set_p2(projectPoint(transform, q.p2(), clippedPoint));
    clipped |= clippedPoint;
    projectedQuad.set_p3(projectPoint(transform, q.p3(), clippedPoint));
    clipped |= clippedPoint;
    projectedQuad.set_p4(projectPoint(transform, q.p4(), clippedPoint));
    clipped |= clippedPoint;

    return projectedQuad;
}

gfx::PointF MathUtil::projectPoint(const WebTransformationMatrix& transform, const gfx::PointF& p, bool& clipped)
{
    HomogeneousCoordinate h = projectHomogeneousPoint(transform, p);

    if (h.w > 0) {
        // The cartesian coordinates will be valid in this case.
        clipped = false;
        return h.cartesianPoint2d();
    }

    // The cartesian coordinates will be invalid after dividing by w.
    clipped = true;

    // Avoid dividing by w if w == 0.
    if (!h.w)
        return gfx::PointF();

    // This return value will be invalid because clipped == true, but (1) users of this
    // code should be ignoring the return value when clipped == true anyway, and (2) this
    // behavior is more consistent with existing behavior of WebKit transforms if the user
    // really does not ignore the return value.
    return h.cartesianPoint2d();
}

void MathUtil::flattenTransformTo2d(WebTransformationMatrix& transform)
{
    // Set both the 3rd row and 3rd column to (0, 0, 1, 0).
    //
    // One useful interpretation of doing this operation:
    //  - For x and y values, the new transform behaves effectively like an orthographic
    //    projection was added to the matrix sequence.
    //  - For z values, the new transform overrides any effect that the transform had on
    //    z, and instead it preserves the z value for any points that are transformed.
    //  - Because of linearity of transforms, this flattened transform also preserves the
    //    effect that any subsequent (post-multiplied) transforms would have on z values.
    //
    transform.setM13(0);
    transform.setM23(0);
    transform.setM31(0);
    transform.setM32(0);
    transform.setM33(1);
    transform.setM34(0);
    transform.setM43(0);
}

static inline float scaleOnAxis(double a, double b, double c)
{
    return std::sqrt(a * a + b * b + c * c);
}

gfx::Vector2dF MathUtil::computeTransform2dScaleComponents(const WebTransformationMatrix& transform)
{
    if (transform.hasPerspective())
        return gfx::Vector2dF(1, 1);
    float xScale = scaleOnAxis(transform.m11(), transform.m12(), transform.m13());
    float yScale = scaleOnAxis(transform.m21(), transform.m22(), transform.m23());
    return gfx::Vector2dF(xScale, yScale);
}

float MathUtil::smallestAngleBetweenVectors(gfx::Vector2dF v1, gfx::Vector2dF v2)
{
    double dotProduct = gfx::DotProduct(v1, v2) / v1.Length() / v2.Length();
    // Clamp to compensate for rounding errors.
    dotProduct = std::max(-1.0, std::min(1.0, dotProduct));
    return static_cast<float>(Rad2Deg(std::acos(dotProduct)));
}

gfx::Vector2dF MathUtil::projectVector(gfx::Vector2dF source, gfx::Vector2dF destination)
{
    float projectedLength = gfx::DotProduct(source, destination) / destination.LengthSquared();
    return gfx::Vector2dF(projectedLength * destination.x(), projectedLength * destination.y());
}

bool MathUtil::isInvertible(const gfx::Transform& transform)
{
    const SkMatrix44& matrix = transform.matrix();
    double determinant = matrix.determinant();
    return abs(determinant) > EPSILON;
}

bool MathUtil::isBackFaceVisible(const gfx::Transform&)
{
    // TODO (shawnsingh): to be implemented in a follow up patch very soon.
    NOTREACHED();
    return false;
}

bool MathUtil::isIdentity(const gfx::Transform& transform)
{
    return transform.matrix().isIdentity();
}

bool MathUtil::isIdentityOrTranslation(const gfx::Transform& transform)
{
    const SkMatrix44& matrix = transform.matrix();

    bool hasNoPerspective = !matrix.getDouble(3, 0) && !matrix.getDouble(3, 1) && !matrix.getDouble(3, 2) && (matrix.getDouble(3, 3) == 1);
    bool hasNoRotationOrSkew = !matrix.getDouble(0, 1) && !matrix.getDouble(0, 2) && !matrix.getDouble(1, 0) &&
        !matrix.getDouble(1, 2) && !matrix.getDouble(2, 0) && !matrix.getDouble(2, 1);
    bool hasNoScale = matrix.getDouble(0, 0) == 1 && matrix.getDouble(1, 1) == 1 && matrix.getDouble(2, 2) == 1;

    return hasNoPerspective && hasNoRotationOrSkew && hasNoScale;
}

bool MathUtil::hasPerspective(const gfx::Transform& transform)
{
    // Mathematically it is a bit too strict to expect the 4th element to be
    // equal to 1. However, the only non-perspective case where this element
    // becomes non-1 is when it was explicitly initialized. In that case it
    // still causes us to have a nontrivial divide-by-w, so we count it as
    // being perspective here.
    const SkMatrix44& matrix = transform.matrix();
    return matrix.getDouble(3, 0) || matrix.getDouble(3, 1) || matrix.getDouble(3, 2) || (matrix.getDouble(3, 3) != 1);
}

void MathUtil::makeIdentity(gfx::Transform* transform)
{
    transform->matrix().setIdentity();
}

void MathUtil::rotateEulerAngles(gfx::Transform* transform, double eulerX, double eulerY, double eulerZ)
{
    // TODO (shawnsingh): make this implementation faster and more accurate by
    // hard-coding each matrix instead of calling rotateAxisAngle().
    gfx::Transform rotationAboutX;
    gfx::Transform rotationAboutY;
    gfx::Transform rotationAboutZ;

    MathUtil::rotateAxisAngle(&rotationAboutX, 1, 0, 0, eulerX);
    MathUtil::rotateAxisAngle(&rotationAboutY, 0, 1, 0, eulerY);
    MathUtil::rotateAxisAngle(&rotationAboutZ, 0, 0, 1, eulerZ);

    gfx::Transform composite = rotationAboutZ * rotationAboutY * rotationAboutX;
    transform->PreconcatTransform(composite);
}

void MathUtil::rotateAxisAngle(gfx::Transform* transform, double i, double j, double k, double degrees)
{
    // TODO (shawnsingh): fix gfx::Transform API to receive vector instead of
    // point for the axis.
    gfx::Point3F axis(i, j, k);
    transform->PreconcatRotateAbout(axis, degrees);
}

gfx::Transform MathUtil::inverse(const gfx::Transform& transform)
{
    gfx::Transform result;
    bool invertedSuccessfully = transform.GetInverse(&result);

    if (invertedSuccessfully)
        return result;

    // If transform was un-invertible, then just return identity.
    return gfx::Transform();
}

gfx::Transform MathUtil::to2dTransform(const gfx::Transform& transform)
{
    gfx::Transform result = transform;
    SkMatrix44& matrix = result.matrix();
    matrix.setDouble(0, 2, 0);
    matrix.setDouble(1, 2, 0);
    matrix.setDouble(2, 2, 1);
    matrix.setDouble(3, 2, 0);

    matrix.setDouble(2, 0, 0);
    matrix.setDouble(2, 1, 0);
    matrix.setDouble(2, 3, 0);

    return result;
}

gfx::Transform MathUtil::createGfxTransform(double m11, double m12, double m13, double m14,
                                            double m21, double m22, double m23, double m24,
                                            double m31, double m32, double m33, double m34,
                                            double m41, double m42, double m43, double m44)
{
    gfx::Transform result;
    SkMatrix44& matrix = result.matrix();

    // Initialize column 1
    matrix.setDouble(0, 0, m11);
    matrix.setDouble(1, 0, m12);
    matrix.setDouble(2, 0, m13);
    matrix.setDouble(3, 0, m14);

    // Initialize column 2
    matrix.setDouble(0, 1, m21);
    matrix.setDouble(1, 1, m22);
    matrix.setDouble(2, 1, m23);
    matrix.setDouble(3, 1, m24);

    // Initialize column 3
    matrix.setDouble(0, 2, m31);
    matrix.setDouble(1, 2, m32);
    matrix.setDouble(2, 2, m33);
    matrix.setDouble(3, 2, m34);

    // Initialize column 4
    matrix.setDouble(0, 3, m41);
    matrix.setDouble(1, 3, m42);
    matrix.setDouble(2, 3, m43);
    matrix.setDouble(3, 3, m44);

    return result;
}

gfx::Transform MathUtil::createGfxTransform(double a, double b, double c,
                                            double d, double e, double f)
{
    gfx::Transform result;
    SkMatrix44& matrix = result.matrix();
    matrix.setDouble(0, 0, a);
    matrix.setDouble(1, 0, b);
    matrix.setDouble(0, 1, c);
    matrix.setDouble(1, 1, d);
    matrix.setDouble(0, 3, e);
    matrix.setDouble(1, 3, f);

    return result;
}

gfx::Transform operator*(const gfx::Transform& A, const gfx::Transform& B)
{
    // Compute A * B.
    gfx::Transform result = A;
    result.PreconcatTransform(B);
    return result;
}

}  // namespace cc
