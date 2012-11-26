// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_GEOMETRY_TEST_UTILS_H_
#define CC_TEST_GEOMETRY_TEST_UTILS_H_

namespace WebKit {
class WebTransformationMatrix;
}

namespace WebKitTests {

// These are macros instead of functions so that we get useful line numbers where a test failed.
#define EXPECT_FLOAT_RECT_EQ(expected, actual) \
do { \
    EXPECT_FLOAT_EQ((expected).x(), (actual).x()); \
    EXPECT_FLOAT_EQ((expected).y(), (actual).y()); \
    EXPECT_FLOAT_EQ((expected).width(), (actual).width()); \
    EXPECT_FLOAT_EQ((expected).height(), (actual).height()); \
} while (false)

#define EXPECT_RECT_EQ(expected, actual) \
do { \
    EXPECT_EQ((expected).x(), (actual).x()); \
    EXPECT_EQ((expected).y(), (actual).y()); \
    EXPECT_EQ((expected).width(), (actual).width()); \
    EXPECT_EQ((expected).height(), (actual).height()); \
} while (false)

#define EXPECT_SIZE_EQ(expected, actual) \
do { \
    EXPECT_EQ((expected).width(), (actual).width()); \
    EXPECT_EQ((expected).height(), (actual).height()); \
} while (false)

#define EXPECT_POINT_EQ(expected, actual) \
do { \
    EXPECT_EQ((expected).x(), (actual).x()); \
    EXPECT_EQ((expected).y(), (actual).y()); \
} while (false)

#define EXPECT_VECTOR_EQ(expected, actual) \
do { \
    EXPECT_EQ((expected).x(), (actual).x()); \
    EXPECT_EQ((expected).y(), (actual).y()); \
} while (false)

// This is a function rather than a macro because when this is included as a macro
// in bulk, it causes a significant slow-down in compilation time. This problem
// exists with both gcc and clang, and bugs have been filed at
// http://llvm.org/bugs/show_bug.cgi?id=13651 and http://gcc.gnu.org/bugzilla/show_bug.cgi?id=54337
void ExpectTransformationMatrixEq(const WebKit::WebTransformationMatrix& expected,
                                  const WebKit::WebTransformationMatrix& actual);

#define EXPECT_TRANSFORMATION_MATRIX_EQ(expected, actual)            \
    {                                                                \
        SCOPED_TRACE("");                                            \
        WebKitTests::ExpectTransformationMatrixEq(expected, actual); \
    }

} // namespace WebKitTests

#endif  // CC_TEST_GEOMETRY_TEST_UTILS_H_
