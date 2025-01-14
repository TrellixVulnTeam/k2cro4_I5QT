// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/candidate_window_controller_impl.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace input_method {

namespace {

class TestableCandidateWindowControllerImpl :
    public CandidateWindowControllerImpl {
 public:
  TestableCandidateWindowControllerImpl() {}
  virtual ~TestableCandidateWindowControllerImpl() {}

  // Changes access right for testing.
  using CandidateWindowControllerImpl::GetInfolistWindowPosition;

 private:
  DISALLOW_COPY_AND_ASSIGN(TestableCandidateWindowControllerImpl);
};

}  // namespace

class CandidateWindowControllerImplTest : public testing::Test {
 public:
  CandidateWindowControllerImplTest()
      : kScreenRect(gfx::Rect(0, 0, 1000, 1000)) {
  }

  virtual ~CandidateWindowControllerImplTest() {
  }

 protected:
  const gfx::Rect kScreenRect;
};

TEST_F(CandidateWindowControllerImplTest,
       GetInfolistWindowPositionTest_NoOverflow) {
  const gfx::Rect candidate_window_rect(100, 110, 120, 130);
  const gfx::Size infolist_window_size(200, 200);

  // If there is no overflow to show infolist window, the expected position is
  // right-top of candidate window position.
  const gfx::Point expected_point(candidate_window_rect.right(),
                                  candidate_window_rect.y());
  EXPECT_EQ(expected_point,
            TestableCandidateWindowControllerImpl::GetInfolistWindowPosition(
                candidate_window_rect,
                kScreenRect,
                infolist_window_size));
}

TEST_F(CandidateWindowControllerImplTest,
       GetInfolistWindowPositionTest_RightOverflow) {
  const gfx::Rect candidate_window_rect(900, 110, 120, 130);
  const gfx::Size infolist_window_size(200, 200);

  // If right of infolist window area is no in screen, show infolist left side
  // of candidate window, that is
  // x : candidate_window_rect.left - infolist_window.width,
  // y : candidate_window_rect.top
  const gfx::Point expected_point(
      candidate_window_rect.x() - infolist_window_size.width(),
      candidate_window_rect.y());
  EXPECT_EQ(expected_point,
            TestableCandidateWindowControllerImpl::GetInfolistWindowPosition(
                candidate_window_rect,
                kScreenRect,
                infolist_window_size));
}

TEST_F(CandidateWindowControllerImplTest,
       GetInfolistWindowPositionTest_BottomOverflow) {
  const gfx::Rect candidate_window_rect(100, 910, 120, 130);
  const gfx::Size infolist_window_size(200, 200);

  // If bottom of infolist window area is no in screen, show infolist clipping
  // with screen bounds.
  // x : candidate_window_rect.right
  // y : screen_rect.bottom - infolist_window_rect.height
  const gfx::Point expected_point(
      candidate_window_rect.right(),
      kScreenRect.bottom() - infolist_window_size.height());
  EXPECT_EQ(expected_point,
            TestableCandidateWindowControllerImpl::GetInfolistWindowPosition(
                candidate_window_rect,
                kScreenRect,
                infolist_window_size));
}

TEST_F(CandidateWindowControllerImplTest,
       GetInfolistWindowPositionTest_RightBottomOverflow) {
  const gfx::Rect candidate_window_rect(900, 910, 120, 130);
  const gfx::Size infolist_window_size(200, 200);

  const gfx::Point expected_point(
      candidate_window_rect.x() - infolist_window_size.width(),
      kScreenRect.bottom() - infolist_window_size.height());
  EXPECT_EQ(expected_point,
            TestableCandidateWindowControllerImpl::GetInfolistWindowPosition(
                candidate_window_rect,
                kScreenRect,
                infolist_window_size));
}

}  // namespace input_method
}  // namespace chromeos
