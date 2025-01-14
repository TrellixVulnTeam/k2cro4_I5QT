// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/workspace/workspace_window_resizer.h"

#include "ash/display/display_controller.h"
#include "ash/display/mouse_cursor_event_filter.h"
#include "ash/root_window_controller.h"
#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/cursor_manager_test_api.h"
#include "ash/wm/cursor_manager.h"
#include "ash/wm/property_util.h"
#include "ash/wm/shelf_layout_manager.h"
#include "ash/wm/window_util.h"
#include "ash/wm/workspace_controller.h"
#include "ash/wm/workspace/snap_sizer.h"
#include "ash/wm/workspace/phantom_window_controller.h"
#include "base/string_number_conversions.h"
#include "base/stringprintf.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/base/hit_test.h"
#include "ui/gfx/insets.h"
#include "ui/gfx/screen.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace internal {
namespace {

const int kRootHeight = 600;

// A simple window delegate that returns the specified min size.
class TestWindowDelegate : public aura::test::TestWindowDelegate {
 public:
  TestWindowDelegate() {
  }
  virtual ~TestWindowDelegate() {}

  void set_min_size(const gfx::Size& size) {
    min_size_ = size;
  }

 private:
  // Overridden from aura::Test::TestWindowDelegate:
  virtual gfx::Size GetMinimumSize() const OVERRIDE {
    return min_size_;
  }

  gfx::Size min_size_;

  DISALLOW_COPY_AND_ASSIGN(TestWindowDelegate);
};

class WorkspaceWindowResizerTest : public test::AshTestBase {
 public:
  WorkspaceWindowResizerTest() : window_(NULL) {}
  virtual ~WorkspaceWindowResizerTest() {}

  virtual void SetUp() OVERRIDE {
    AshTestBase::SetUp();
    UpdateDisplay(StringPrintf("800x%d", kRootHeight));

    aura::RootWindow* root = Shell::GetPrimaryRootWindow();
    gfx::Rect root_bounds(root->bounds());
    EXPECT_EQ(kRootHeight, root_bounds.height());
    EXPECT_EQ(800, root_bounds.width());
    Shell::GetInstance()->SetDisplayWorkAreaInsets(root, gfx::Insets());
    window_.reset(new aura::Window(&delegate_));
    window_->SetType(aura::client::WINDOW_TYPE_NORMAL);
    window_->Init(ui::LAYER_NOT_DRAWN);
    window_->SetParent(NULL);
    window_->set_id(1);

    window2_.reset(new aura::Window(&delegate2_));
    window2_->SetType(aura::client::WINDOW_TYPE_NORMAL);
    window2_->Init(ui::LAYER_NOT_DRAWN);
    window2_->SetParent(NULL);
    window2_->set_id(2);

    window3_.reset(new aura::Window(&delegate3_));
    window3_->SetType(aura::client::WINDOW_TYPE_NORMAL);
    window3_->Init(ui::LAYER_NOT_DRAWN);
    window3_->SetParent(NULL);
    window3_->set_id(3);
  }

  virtual void TearDown() OVERRIDE {
    window_.reset();
    window2_.reset();
    window3_.reset();
    AshTestBase::TearDown();
  }

  // Returns a string identifying the z-order of each of the known child windows
  // of |parent|.  The returned string constains the id of the known windows and
  // is ordered from topmost to bottomost windows.
  std::string WindowOrderAsString(aura::Window* parent) const {
    std::string result;
    const aura::Window::Windows& windows = parent->children();
    for (aura::Window::Windows::const_reverse_iterator i = windows.rbegin();
         i != windows.rend(); ++i) {
      if (*i == window_.get() || *i == window2_.get() || *i == window3_.get()) {
        if (!result.empty())
          result += " ";
        result += base::IntToString((*i)->id());
      }
    }
    return result;
  }

 protected:
  gfx::Point CalculateDragPoint(const WorkspaceWindowResizer& resizer,
                                int delta_x,
                                int delta_y) const {
    gfx::Point location = resizer.initial_location_in_parent();
    location.set_x(location.x() + delta_x);
    location.set_y(location.y() + delta_y);
    return location;
  }

  std::vector<aura::Window*> empty_windows() const {
    return std::vector<aura::Window*>();
  }

  internal::ShelfLayoutManager* shelf_layout_manager() {
    return Shell::GetPrimaryRootWindowController()->shelf();
  }

  TestWindowDelegate delegate_;
  TestWindowDelegate delegate2_;
  TestWindowDelegate delegate3_;
  scoped_ptr<aura::Window> window_;
  scoped_ptr<aura::Window> window2_;
  scoped_ptr<aura::Window> window3_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WorkspaceWindowResizerTest);
};

}  // namespace

// Fails on win_aura since ash::GetRootWindowRelativeToWindow is not implemented
// yet for the platform.
#if defined(OS_WIN)
#define MAYBE_WindowDragWithMultiDisplays \
  DISABLED_WindowDragWithMultiDisplays
#define MAYBE_WindowDragWithMultiDisplaysRightToLeft \
  DISABLED_WindowDragWithMultiDisplaysRightToLeft
#define MAYBE_PhantomStyle DISABLED_PhantomStyle
#define MAYBE_CancelSnapPhantom DISABLED_CancelSnapPhantom
#define MAYBE_CursorDeviceScaleFactor DISABLED_CursorDeviceScaleFactor
#else
#define MAYBE_WindowDragWithMultiDisplays WindowDragWithMultiDisplays
#define MAYBE_WindowDragWithMultiDisplaysRightToLeft \
  WindowDragWithMultiDisplaysRightToLeft
#define MAYBE_PhantomStyle PhantomStyle
#define MAYBE_CancelSnapPhantom CancelSnapPhantom
#define MAYBE_CursorDeviceScaleFactor CursorDeviceScaleFactor
#endif

// Assertions around attached window resize dragging from the right with 2
// windows.
TEST_F(WorkspaceWindowResizerTest, AttachedResize_RIGHT_2) {
  window_->SetBounds(gfx::Rect(0, 300, 400, 300));
  window2_->SetBounds(gfx::Rect(400, 200, 100, 200));

  std::vector<aura::Window*> windows;
  windows.push_back(window2_.get());
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTRIGHT, windows));
  ASSERT_TRUE(resizer.get());
  // Move it 100 to the right, which should expand w1 and push w2.
  resizer->Drag(CalculateDragPoint(*resizer, 100, 10), 0);
  EXPECT_EQ("0,300 500x300", window_->bounds().ToString());
  EXPECT_EQ("500,200 100x200", window2_->bounds().ToString());

  // Push off the screen, w2 should be resized to its min.
  delegate2_.set_min_size(gfx::Size(20, 20));
  resizer->Drag(CalculateDragPoint(*resizer, 800, 20), 0);
  EXPECT_EQ("0,300 780x300", window_->bounds().ToString());
  EXPECT_EQ("780,200 20x200", window2_->bounds().ToString());

  // Move back to 100 and verify w2 gets its original size.
  resizer->Drag(CalculateDragPoint(*resizer, 100, 10), 0);
  EXPECT_EQ("0,300 500x300", window_->bounds().ToString());
  EXPECT_EQ("500,200 100x200", window2_->bounds().ToString());

  // Revert and make sure everything moves back.
  resizer->Drag(CalculateDragPoint(*resizer, 800, 20), 0);
  resizer->RevertDrag();
  EXPECT_EQ("0,300 400x300", window_->bounds().ToString());
  EXPECT_EQ("400,200 100x200", window2_->bounds().ToString());
}

// Assertions around collapsing and expanding.
TEST_F(WorkspaceWindowResizerTest, AttachedResize_RIGHT_Compress) {
  window_->SetBounds(gfx::Rect(   0, 300, 400, 300));
  window2_->SetBounds(gfx::Rect(400, 200, 100, 200));

  std::vector<aura::Window*> windows;
  windows.push_back(window2_.get());
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTRIGHT, windows));
  ASSERT_TRUE(resizer.get());
  // Move it 100 to the left, which should expand w2 and collapse w1.
  resizer->Drag(CalculateDragPoint(*resizer, -100, 10), 0);
  EXPECT_EQ("0,300 300x300", window_->bounds().ToString());
  EXPECT_EQ("300,200 200x200", window2_->bounds().ToString());

  // Collapse all the way to w1's min.
  delegate_.set_min_size(gfx::Size(20, 20));
  resizer->Drag(CalculateDragPoint(*resizer, -800, 20), 0);
  EXPECT_EQ("0,300 20x300", window_->bounds().ToString());
  EXPECT_EQ("20,200 480x200", window2_->bounds().ToString());

  // Move 100 to the left.
  resizer->Drag(CalculateDragPoint(*resizer, 100, 10), 0);
  EXPECT_EQ("0,300 500x300", window_->bounds().ToString());
  EXPECT_EQ("500,200 100x200", window2_->bounds().ToString());

  // Back to -100.
  resizer->Drag(CalculateDragPoint(*resizer, -100, 20), 0);
  EXPECT_EQ("0,300 300x300", window_->bounds().ToString());
  EXPECT_EQ("300,200 200x200", window2_->bounds().ToString());
}

// Assertions around attached window resize dragging from the right with 3
// windows.
TEST_F(WorkspaceWindowResizerTest, AttachedResize_RIGHT_3) {
  window_->SetBounds(gfx::Rect( 100, 300, 200, 300));
  window2_->SetBounds(gfx::Rect(300, 300, 150, 200));
  window3_->SetBounds(gfx::Rect(450, 300, 100, 200));
  delegate2_.set_min_size(gfx::Size(52, 50));
  delegate3_.set_min_size(gfx::Size(38, 50));

  std::vector<aura::Window*> windows;
  windows.push_back(window2_.get());
  windows.push_back(window3_.get());
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTRIGHT, windows));
  ASSERT_TRUE(resizer.get());
  // Move it 100 to the right, which should expand w1 and push w2 and w3.
  resizer->Drag(CalculateDragPoint(*resizer, 100, -10), 0);
  EXPECT_EQ("100,300 300x300", window_->bounds().ToString());
  EXPECT_EQ("400,300 150x200", window2_->bounds().ToString());
  EXPECT_EQ("550,300 100x200", window3_->bounds().ToString());

  // Move it 300, things should compress.
  resizer->Drag(CalculateDragPoint(*resizer, 300, -10), 0);
  EXPECT_EQ("100,300 500x300", window_->bounds().ToString());
  EXPECT_EQ("600,300 120x200", window2_->bounds().ToString());
  EXPECT_EQ("720,300 80x200", window3_->bounds().ToString());

  // Move it so much the last two end up at their min.
  resizer->Drag(CalculateDragPoint(*resizer, 800, 50), 0);
  EXPECT_EQ("100,300 610x300", window_->bounds().ToString());
  EXPECT_EQ("710,300 52x200", window2_->bounds().ToString());
  EXPECT_EQ("762,300 38x200", window3_->bounds().ToString());

  // Revert and make sure everything moves back.
  resizer->RevertDrag();
  EXPECT_EQ("100,300 200x300", window_->bounds().ToString());
  EXPECT_EQ("300,300 150x200", window2_->bounds().ToString());
  EXPECT_EQ("450,300 100x200", window3_->bounds().ToString());
}

// Assertions around attached window resizing (collapsing and expanding) with
// 3 windows.
TEST_F(WorkspaceWindowResizerTest, AttachedResize_RIGHT_3_Compress) {
  window_->SetBounds(gfx::Rect( 100, 300, 200, 300));
  window2_->SetBounds(gfx::Rect(300, 300, 200, 200));
  window3_->SetBounds(gfx::Rect(450, 300, 100, 200));
  delegate2_.set_min_size(gfx::Size(52, 50));
  delegate3_.set_min_size(gfx::Size(38, 50));

  std::vector<aura::Window*> windows;
  windows.push_back(window2_.get());
  windows.push_back(window3_.get());
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTRIGHT, windows));
  ASSERT_TRUE(resizer.get());
  // Move it -100 to the right, which should collapse w1 and expand w2 and w3.
  resizer->Drag(CalculateDragPoint(*resizer, -100, -10), 0);
  EXPECT_EQ("100,300 100x300", window_->bounds().ToString());
  EXPECT_EQ("200,300 266x200", window2_->bounds().ToString());
  EXPECT_EQ("466,300 134x200", window3_->bounds().ToString());

  // Move it 100 to the right.
  resizer->Drag(CalculateDragPoint(*resizer, 100, -10), 0);
  EXPECT_EQ("100,300 300x300", window_->bounds().ToString());
  EXPECT_EQ("400,300 200x200", window2_->bounds().ToString());
  EXPECT_EQ("600,300 100x200", window3_->bounds().ToString());

  // 100 to the left again.
  resizer->Drag(CalculateDragPoint(*resizer, -100, -10), 0);
  EXPECT_EQ("100,300 100x300", window_->bounds().ToString());
  EXPECT_EQ("200,300 266x200", window2_->bounds().ToString());
  EXPECT_EQ("466,300 134x200", window3_->bounds().ToString());
}

// Assertions around collapsing and expanding from the bottom.
TEST_F(WorkspaceWindowResizerTest, AttachedResize_BOTTOM_Compress) {
  window_->SetBounds(gfx::Rect(   0, 100, 400, 300));
  window2_->SetBounds(gfx::Rect(400, 400, 100, 200));

  std::vector<aura::Window*> windows;
  windows.push_back(window2_.get());
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTBOTTOM, windows));
  ASSERT_TRUE(resizer.get());
  // Move it up 100, which should expand w2 and collapse w1.
  resizer->Drag(CalculateDragPoint(*resizer, 10, -100), 0);
  EXPECT_EQ("0,100 400x200", window_->bounds().ToString());
  EXPECT_EQ("400,300 100x300", window2_->bounds().ToString());

  // Collapse all the way to w1's min.
  delegate_.set_min_size(gfx::Size(20, 20));
  resizer->Drag(CalculateDragPoint(*resizer, 20, -800), 0);
  EXPECT_EQ("0,100 400x20", window_->bounds().ToString());
  EXPECT_EQ("400,120 100x480", window2_->bounds().ToString());

  // Move 100 down.
  resizer->Drag(CalculateDragPoint(*resizer, 10, 100), 0);
  EXPECT_EQ("0,100 400x400", window_->bounds().ToString());
  EXPECT_EQ("400,500 100x100", window2_->bounds().ToString());

  // Back to -100.
  resizer->Drag(CalculateDragPoint(*resizer, 20, -100), 0);
  EXPECT_EQ("0,100 400x200", window_->bounds().ToString());
  EXPECT_EQ("400,300 100x300", window2_->bounds().ToString());
}

// Assertions around attached window resize dragging from the bottom with 2
// windows.
TEST_F(WorkspaceWindowResizerTest, AttachedResize_BOTTOM_2) {
  window_->SetBounds(gfx::Rect( 0,  50, 400, 200));
  window2_->SetBounds(gfx::Rect(0, 250, 200, 100));

  std::vector<aura::Window*> windows;
  windows.push_back(window2_.get());
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTBOTTOM, windows));
  ASSERT_TRUE(resizer.get());
  // Move it 100 to the bottom, which should expand w1 and push w2.
  resizer->Drag(CalculateDragPoint(*resizer, 10, 100), 0);
  EXPECT_EQ("0,50 400x300", window_->bounds().ToString());
  EXPECT_EQ("0,350 200x100", window2_->bounds().ToString());

  // Push off the screen, w2 should be resized to its min.
  delegate2_.set_min_size(gfx::Size(20, 20));
  resizer->Drag(CalculateDragPoint(*resizer, 50, 820), 0);
  EXPECT_EQ("0,50 400x530", window_->bounds().ToString());
  EXPECT_EQ("0,580 200x20", window2_->bounds().ToString());

  // Move back to 100 and verify w2 gets its original size.
  resizer->Drag(CalculateDragPoint(*resizer, 10, 100), 0);
  EXPECT_EQ("0,50 400x300", window_->bounds().ToString());
  EXPECT_EQ("0,350 200x100", window2_->bounds().ToString());

  // Revert and make sure everything moves back.
  resizer->Drag(CalculateDragPoint(*resizer, 800, 20), 0);
  resizer->RevertDrag();
  EXPECT_EQ("0,50 400x200", window_->bounds().ToString());
  EXPECT_EQ("0,250 200x100", window2_->bounds().ToString());
}

// Assertions around attached window resize dragging from the bottom with 3
// windows.
// TODO(oshima): Host window doesn't get a resize event after
// SetHostSize on Windows trybot, which gives wrong work/display area.
// crbug.com/141577.
#if defined(OS_WIN)
#define MAYBE_AttachedResize_BOTTOM_3 DISABLED_AttachedResize_BOTTOM_3
#else
#define MAYBE_AttachedResize_BOTTOM_3 AttachedResize_BOTTOM_3
#endif
TEST_F(WorkspaceWindowResizerTest, MAYBE_AttachedResize_BOTTOM_3) {
  aura::RootWindow* root = Shell::GetPrimaryRootWindow();
  root->SetHostSize(gfx::Size(600, 800));

  Shell::GetInstance()->SetDisplayWorkAreaInsets(root, gfx::Insets());

  window_->SetBounds(gfx::Rect( 300, 100, 300, 200));
  window2_->SetBounds(gfx::Rect(300, 300, 200, 150));
  window3_->SetBounds(gfx::Rect(300, 450, 200, 100));
  delegate2_.set_min_size(gfx::Size(50, 52));
  delegate3_.set_min_size(gfx::Size(50, 38));

  std::vector<aura::Window*> windows;
  windows.push_back(window2_.get());
  windows.push_back(window3_.get());
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTBOTTOM, windows));
  ASSERT_TRUE(resizer.get());
  // Move it 100 to the right, which should expand w1 and push w2 and w3.
  resizer->Drag(CalculateDragPoint(*resizer, -10, 100), 0);
  EXPECT_EQ("300,100 300x300", window_->bounds().ToString());
  EXPECT_EQ("300,400 200x150", window2_->bounds().ToString());
  EXPECT_EQ("300,550 200x100", window3_->bounds().ToString());

  // Move it 296 things should compress.
  resizer->Drag(CalculateDragPoint(*resizer, -10, 296), 0);
  EXPECT_EQ("300,100 300x496", window_->bounds().ToString());
  EXPECT_EQ("300,596 200x122", window2_->bounds().ToString());
  EXPECT_EQ("300,718 200x82", window3_->bounds().ToString());

  // Move it so much everything ends up at its min.
  resizer->Drag(CalculateDragPoint(*resizer, 50, 798), 0);
  EXPECT_EQ("300,100 300x610", window_->bounds().ToString());
  EXPECT_EQ("300,710 200x52", window2_->bounds().ToString());
  EXPECT_EQ("300,762 200x38", window3_->bounds().ToString());

  // Revert and make sure everything moves back.
  resizer->RevertDrag();
  EXPECT_EQ("300,100 300x200", window_->bounds().ToString());
  EXPECT_EQ("300,300 200x150", window2_->bounds().ToString());
  EXPECT_EQ("300,450 200x100", window3_->bounds().ToString());
}

// Assertions around attached window resizing (collapsing and expanding) with
// 3 windows.
TEST_F(WorkspaceWindowResizerTest, AttachedResize_BOTTOM_3_Compress) {
  window_->SetBounds(gfx::Rect(  0,   0, 200, 200));
  window2_->SetBounds(gfx::Rect(10, 200, 200, 200));
  window3_->SetBounds(gfx::Rect(20, 400, 100, 100));
  delegate2_.set_min_size(gfx::Size(52, 50));
  delegate3_.set_min_size(gfx::Size(38, 50));

  std::vector<aura::Window*> windows;
  windows.push_back(window2_.get());
  windows.push_back(window3_.get());
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTBOTTOM, windows));
  ASSERT_TRUE(resizer.get());
  // Move it 100 up, which should collapse w1 and expand w2 and w3.
  resizer->Drag(CalculateDragPoint(*resizer, -10, -100), 0);
  EXPECT_EQ("0,0 200x100", window_->bounds().ToString());
  EXPECT_EQ("10,100 200x266", window2_->bounds().ToString());
  EXPECT_EQ("20,366 100x134", window3_->bounds().ToString());

  // Move it 100 down.
  resizer->Drag(CalculateDragPoint(*resizer, 10, 100), 0);
  EXPECT_EQ("0,0 200x300", window_->bounds().ToString());
  EXPECT_EQ("10,300 200x200", window2_->bounds().ToString());
  EXPECT_EQ("20,500 100x100", window3_->bounds().ToString());

  // 100 up again.
  resizer->Drag(CalculateDragPoint(*resizer, -10, -100), 0);
  EXPECT_EQ("0,0 200x100", window_->bounds().ToString());
  EXPECT_EQ("10,100 200x266", window2_->bounds().ToString());
  EXPECT_EQ("20,366 100x134", window3_->bounds().ToString());
}

// Assertions around dragging to the left/right edge of the screen.
TEST_F(WorkspaceWindowResizerTest, Edge) {
  int bottom =
      ScreenAsh::GetDisplayWorkAreaBoundsInParent(window_.get()).bottom();
  window_->SetBounds(gfx::Rect(20, 30, 50, 60));
  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 10), 0);
    resizer->CompleteDrag(0);
    EXPECT_EQ("0,0 720x" + base::IntToString(bottom),
              window_->bounds().ToString());
    ASSERT_TRUE(GetRestoreBoundsInScreen(window_.get()));
    EXPECT_EQ("20,30 50x60",
              GetRestoreBoundsInScreen(window_.get())->ToString());
  }
  // Try the same with the right side.
  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 800, 10), 0);
    resizer->CompleteDrag(0);
    EXPECT_EQ("80,0 720x" + base::IntToString(bottom),
              window_->bounds().ToString());
    ASSERT_TRUE(GetRestoreBoundsInScreen(window_.get()));
    EXPECT_EQ("20,30 50x60",
              GetRestoreBoundsInScreen(window_.get())->ToString());
  }

#if !defined(OS_WIN)
  // Test if the restore bounds is correct in multiple displays.
  ClearRestoreBounds(window_.get());
  UpdateDisplay("800x600,200x600");
  Shell::RootWindowList root_windows = Shell::GetAllRootWindows();
  EXPECT_EQ(root_windows[0], window_->GetRootWindow());
  window_->SetBoundsInScreen(gfx::Rect(800, 10, 50, 60),
                             ScreenAsh::GetSecondaryDisplay());
  EXPECT_EQ(root_windows[1], window_->GetRootWindow());
  {
    bottom =
        ScreenAsh::GetDisplayWorkAreaBoundsInParent(window_.get()).bottom();
    EXPECT_EQ("800,10 50x60", window_->GetBoundsInScreen().ToString());

    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());

    resizer->Drag(CalculateDragPoint(*resizer, 199, 00), 0);
    resizer->CompleteDrag(0);
    // With the resolution of 200x600 we will hit in this case the 50% screen
    // size setting.
    EXPECT_EQ("100,0 100x" + base::IntToString(bottom),
              window_->bounds().ToString());
    EXPECT_EQ("800,10 50x60",
              GetRestoreBoundsInScreen(window_.get())->ToString());
  }
#endif
}

// Check that non resizable windows will not get resized.
TEST_F(WorkspaceWindowResizerTest, NonResizableWindows) {
  window_->SetBounds(gfx::Rect(20, 30, 50, 60));
  window_->SetProperty(aura::client::kCanResizeKey, false);

  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, -20, 0), 0);
  resizer->CompleteDrag(0);
  EXPECT_EQ("0,30 50x60", window_->bounds().ToString());
}

// Verifies a window can be moved from the primary display to another.
TEST_F(WorkspaceWindowResizerTest, MAYBE_WindowDragWithMultiDisplays) {
  // The secondary display is logically on the right, but on the system (e.g. X)
  // layer, it's below the primary one. See UpdateDisplay() in ash_test_base.cc.
  UpdateDisplay("800x600,800x600");
  shelf_layout_manager()->LayoutShelf();
  Shell::RootWindowList root_windows = Shell::GetAllRootWindows();
  ASSERT_EQ(2U, root_windows.size());

  window_->SetBoundsInScreen(gfx::Rect(0, 0, 50, 60),
                             Shell::GetScreen()->GetPrimaryDisplay());
  EXPECT_EQ(root_windows[0], window_->GetRootWindow());
  {
    // Grab (0, 0) of the window.
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    // Drag the pointer to the right. Once it reaches the right edge of the
    // primary display, it warps to the secondary.
    resizer->Drag(CalculateDragPoint(*resizer, 800, 10), 0);
    resizer->CompleteDrag(0);
    // The whole window is on the secondary display now. The parent should be
    // changed.
    EXPECT_EQ(root_windows[1], window_->GetRootWindow());
    EXPECT_EQ("0,10 50x60", window_->bounds().ToString());
  }

  window_->SetBoundsInScreen(gfx::Rect(0, 0, 50, 60),
                             Shell::GetScreen()->GetPrimaryDisplay());
  EXPECT_EQ(root_windows[0], window_->GetRootWindow());
  {
    // Grab (0, 0) of the window and move the pointer to (790, 10).
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 790, 10), 0);
    resizer->CompleteDrag(0);
    // Since the pointer is still on the primary root window, the parent should
    // not be changed.
    EXPECT_EQ(root_windows[0], window_->GetRootWindow());
    EXPECT_EQ("790,10 50x60", window_->bounds().ToString());
  }

  window_->SetBoundsInScreen(gfx::Rect(0, 0, 50, 60),
                             Shell::GetScreen()->GetPrimaryDisplay());
  EXPECT_EQ(root_windows[0], window_->GetRootWindow());
  {
    // Grab the top-right edge of the window and move the pointer to (0, 10)
    // in the secondary root window's coordinates.
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(49, 0), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 751, 10), ui::EF_CONTROL_DOWN);
    resizer->CompleteDrag(0);
    // Since the pointer is on the secondary, the parent should be changed
    // even though only small fraction of the window is within the secondary
    // root window's bounds.
    EXPECT_EQ(root_windows[1], window_->GetRootWindow());
    EXPECT_EQ("-49,10 50x60", window_->bounds().ToString());
  }
}

// Verifies a window can be moved from the secondary display to primary.
TEST_F(WorkspaceWindowResizerTest,
       MAYBE_WindowDragWithMultiDisplaysRightToLeft) {
  UpdateDisplay("800x600,800x600");
  shelf_layout_manager()->LayoutShelf();
  Shell::RootWindowList root_windows = Shell::GetAllRootWindows();
  ASSERT_EQ(2U, root_windows.size());

  window_->SetBoundsInScreen(
      gfx::Rect(800, 00, 50, 60),
      Shell::GetScreen()->GetDisplayNearestWindow(root_windows[1]));
  EXPECT_EQ(root_windows[1], window_->GetRootWindow());
  {
    // Grab (0, 0) of the window.
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    // Move the mouse near the right edge, (798, 0), of the primary display.
    resizer->Drag(CalculateDragPoint(*resizer, -2, 0), ui::EF_CONTROL_DOWN);
    resizer->CompleteDrag(0);
    EXPECT_EQ(root_windows[0], window_->GetRootWindow());
    EXPECT_EQ("798,0 50x60", window_->bounds().ToString());
  }
}

// Verifies the style of the drag phantom window is correct.
TEST_F(WorkspaceWindowResizerTest, MAYBE_PhantomStyle) {
  UpdateDisplay("800x600,800x600");
  Shell::RootWindowList root_windows = Shell::GetAllRootWindows();
  ASSERT_EQ(2U, root_windows.size());

  window_->SetBoundsInScreen(gfx::Rect(0, 0, 50, 60),
                             Shell::GetScreen()->GetPrimaryDisplay());
  EXPECT_EQ(root_windows[0], window_->GetRootWindow());
  EXPECT_FLOAT_EQ(1.0f, window_->layer()->opacity());
  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    EXPECT_FALSE(resizer->snap_phantom_window_controller_.get());
    EXPECT_FALSE(resizer->drag_phantom_window_controller_.get());

    // The pointer is inside the primary root. Both phantoms should be NULL.
    resizer->Drag(CalculateDragPoint(*resizer, 10, 10), 0);
    EXPECT_FALSE(resizer->snap_phantom_window_controller_.get());
    EXPECT_FALSE(resizer->drag_phantom_window_controller_.get());

    // The window spans both root windows.
    resizer->Drag(CalculateDragPoint(*resizer, 798, 10), 0);
    EXPECT_FALSE(resizer->snap_phantom_window_controller_.get());
    PhantomWindowController* controller =
        resizer->drag_phantom_window_controller_.get();
    ASSERT_TRUE(controller);
    EXPECT_EQ(PhantomWindowController::STYLE_DRAGGING, controller->style());

    // Check if |resizer->layer_| is properly set to the phantom widget.
    const std::vector<ui::Layer*>& layers =
        controller->phantom_widget_->GetNativeWindow()->layer()->children();
    EXPECT_FALSE(layers.empty());
    EXPECT_EQ(resizer->layer_, layers.back());

    // |window_| should be opaque since the pointer is still on the primary
    // root window. The phantom should be semi-transparent.
    EXPECT_FLOAT_EQ(1.0f, window_->layer()->opacity());
    EXPECT_GT(1.0f, controller->GetOpacity());

    // Enter the pointer to the secondary display.
    resizer->Drag(CalculateDragPoint(*resizer, 800, 10), 0);
    EXPECT_FALSE(resizer->snap_phantom_window_controller_.get());
    controller = resizer->drag_phantom_window_controller_.get();
    ASSERT_TRUE(controller);
    EXPECT_EQ(PhantomWindowController::STYLE_DRAGGING, controller->style());
    // |window_| should be transparent, and the phantom should be opaque.
    EXPECT_GT(1.0f, window_->layer()->opacity());
    EXPECT_FLOAT_EQ(1.0f, controller->GetOpacity());

    resizer->CompleteDrag(0);
    EXPECT_EQ(root_windows[1], window_->GetRootWindow());
    EXPECT_FLOAT_EQ(1.0f, window_->layer()->opacity());
  }

  // Do the same test with RevertDrag().
  window_->SetBoundsInScreen(gfx::Rect(0, 0, 50, 60),
                             Shell::GetScreen()->GetPrimaryDisplay());
  EXPECT_EQ(root_windows[0], window_->GetRootWindow());
  EXPECT_FLOAT_EQ(1.0f, window_->layer()->opacity());
  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    EXPECT_FALSE(resizer->snap_phantom_window_controller_.get());
    EXPECT_FALSE(resizer->drag_phantom_window_controller_.get());

    resizer->Drag(CalculateDragPoint(*resizer, 0, 610), 0);
    resizer->RevertDrag();
    EXPECT_EQ(root_windows[0], window_->GetRootWindow());
    EXPECT_FLOAT_EQ(1.0f, window_->layer()->opacity());
  }
}

// Verifies the style of the drag phantom window is correct.
TEST_F(WorkspaceWindowResizerTest, MAYBE_CancelSnapPhantom) {
  UpdateDisplay("800x600,800x600");
  Shell::RootWindowList root_windows = Shell::GetAllRootWindows();
  ASSERT_EQ(2U, root_windows.size());

  window_->SetBoundsInScreen(gfx::Rect(0, 0, 50, 60),
                             Shell::GetScreen()->GetPrimaryDisplay());
  EXPECT_EQ(root_windows[0], window_->GetRootWindow());
  EXPECT_FLOAT_EQ(1.0f, window_->layer()->opacity());
  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    EXPECT_FALSE(resizer->snap_phantom_window_controller_.get());
    EXPECT_FALSE(resizer->drag_phantom_window_controller_.get());
    EXPECT_EQ(WorkspaceWindowResizer::SNAP_NONE, resizer->snap_type_);

    // The pointer is on the edge but not shared.  Both controllers should be
    // non-NULL.
    resizer->Drag(CalculateDragPoint(*resizer, 799, 0), 0);
    EXPECT_TRUE(resizer->snap_phantom_window_controller_.get());
    EXPECT_EQ(WorkspaceWindowResizer::SNAP_RIGHT_EDGE, resizer->snap_type_);
    PhantomWindowController* controller =
        resizer->drag_phantom_window_controller_.get();
    ASSERT_TRUE(controller);
    EXPECT_EQ(PhantomWindowController::STYLE_DRAGGING, controller->style());

    // Move the cursor across the edge.  Now the snap phantom controller
    // should be canceled.
    resizer->Drag(CalculateDragPoint(*resizer, 800, 0), 0);
    EXPECT_FALSE(resizer->snap_phantom_window_controller_.get());
    EXPECT_EQ(WorkspaceWindowResizer::SNAP_NONE, resizer->snap_type_);
    controller =
        resizer->drag_phantom_window_controller_.get();
    ASSERT_TRUE(controller);
    EXPECT_EQ(PhantomWindowController::STYLE_DRAGGING, controller->style());
  }
}

// Verifies if the resizer sets and resets
// MouseCursorEventFilter::mouse_warp_mode_ as expected.
TEST_F(WorkspaceWindowResizerTest, WarpMousePointer) {
  MouseCursorEventFilter* event_filter =
      Shell::GetInstance()->mouse_cursor_filter();
  ASSERT_TRUE(event_filter);
  window_->SetBounds(gfx::Rect(0, 0, 50, 60));

  EXPECT_EQ(MouseCursorEventFilter::WARP_ALWAYS,
            event_filter->mouse_warp_mode_);
  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    // While dragging a window, warp should be allowed.
    EXPECT_EQ(MouseCursorEventFilter::WARP_DRAG,
              event_filter->mouse_warp_mode_);
    resizer->CompleteDrag(0);
  }
  EXPECT_EQ(MouseCursorEventFilter::WARP_ALWAYS,
            event_filter->mouse_warp_mode_);

  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    EXPECT_EQ(MouseCursorEventFilter::WARP_DRAG,
              event_filter->mouse_warp_mode_);
    resizer->RevertDrag();
  }
  EXPECT_EQ(MouseCursorEventFilter::WARP_ALWAYS,
            event_filter->mouse_warp_mode_);

  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTRIGHT, empty_windows()));
    // While resizing a window, warp should NOT be allowed.
    EXPECT_EQ(MouseCursorEventFilter::WARP_NONE,
              event_filter->mouse_warp_mode_);
    resizer->CompleteDrag(0);
  }
  EXPECT_EQ(MouseCursorEventFilter::WARP_ALWAYS,
            event_filter->mouse_warp_mode_);

  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTRIGHT, empty_windows()));
    EXPECT_EQ(MouseCursorEventFilter::WARP_NONE,
              event_filter->mouse_warp_mode_);
    resizer->RevertDrag();
  }
  EXPECT_EQ(MouseCursorEventFilter::WARP_ALWAYS,
            event_filter->mouse_warp_mode_);
}

// Verifies windows are correctly restacked when reordering multiple windows.
TEST_F(WorkspaceWindowResizerTest, RestackAttached) {
  window_->SetBounds(gfx::Rect(   0, 0, 200, 300));
  window2_->SetBounds(gfx::Rect(200, 0, 100, 200));
  window3_->SetBounds(gfx::Rect(300, 0, 100, 100));

  {
    std::vector<aura::Window*> windows;
    windows.push_back(window2_.get());
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTRIGHT, windows));
    ASSERT_TRUE(resizer.get());
    // Move it 100 to the right, which should expand w1 and push w2 and w3.
    resizer->Drag(CalculateDragPoint(*resizer, 100, -10), 0);

    // 2 should be topmost since it's initially the highest in the stack.
    EXPECT_EQ("2 1 3", WindowOrderAsString(window_->parent()));
  }

  {
    std::vector<aura::Window*> windows;
    windows.push_back(window3_.get());
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window2_.get(), gfx::Point(), HTRIGHT, windows));
    ASSERT_TRUE(resizer.get());
    // Move it 100 to the right, which should expand w1 and push w2 and w3.
    resizer->Drag(CalculateDragPoint(*resizer, 100, -10), 0);

    // 2 should be topmost since it's initially the highest in the stack.
    EXPECT_EQ("2 3 1", WindowOrderAsString(window_->parent()));
  }
}

// Makes sure we don't allow dragging below the work area.
TEST_F(WorkspaceWindowResizerTest, DontDragOffBottom) {
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(0, 0, 10, 0));

  ASSERT_EQ(1, Shell::GetScreen()->GetNumDisplays());

  window_->SetBounds(gfx::Rect(100, 200, 300, 400));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, 0, 600), 0);
  int expected_y =
      kRootHeight - WorkspaceWindowResizer::kMinOnscreenHeight - 10;
  EXPECT_EQ("100," + base::IntToString(expected_y) + " 300x400",
            window_->bounds().ToString());
}

// Makes sure we don't allow dragging on the work area with multidisplay.
TEST_F(WorkspaceWindowResizerTest, DontDragOffBottomWithMultiDisplay) {
  UpdateDisplay("800x600,800x600");
  ASSERT_EQ(2, Shell::GetScreen()->GetNumDisplays());

  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(0, 0, 10, 0));

  // Positions the secondary display at the bottom the primary display.
  ash::DisplayLayout display_layout(ash::DisplayLayout::BOTTOM, 0);
  Shell::GetInstance()->display_controller()->SetDefaultDisplayLayout(
      display_layout);

  {
    window_->SetBounds(gfx::Rect(100, 200, 300, 400));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 400), 0);
    int expected_y =
        kRootHeight - WorkspaceWindowResizer::kMinOnscreenHeight - 10;
    // When the mouse cursor is in the primary display, the window cannot move
    // on non-work area with kMinOnscreenHeight margin.
    EXPECT_EQ("100," + base::IntToString(expected_y) + " 300x400",
              window_->bounds().ToString());
  }

  {
    window_->SetBounds(gfx::Rect(100, 200, 300, 400));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 600), 0);
    // The window can move to the secondary display beyond non-work area of
    // the primary display.
    EXPECT_EQ("100,800 300x400", window_->bounds().ToString());
  }
}

// Makes sure we don't allow dragging off the top of the work area.
TEST_F(WorkspaceWindowResizerTest, DontDragOffTop) {
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(10, 0, 0, 0));

  window_->SetBounds(gfx::Rect(100, 200, 300, 400));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, 0, -600), 0);
  EXPECT_EQ("100,10 300x400", window_->bounds().ToString());
}

TEST_F(WorkspaceWindowResizerTest, ResizeBottomOutsideWorkArea) {
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(0, 0, 50, 0));

  window_->SetBounds(gfx::Rect(100, 200, 300, 380));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTTOP, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, 8, 0), 0);
  EXPECT_EQ("100,200 300x380", window_->bounds().ToString());
}

TEST_F(WorkspaceWindowResizerTest, ResizeWindowOutsideLeftWorkArea) {
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(0, 0, 50, 0));
  int left = ScreenAsh::GetDisplayWorkAreaBoundsInParent(window_.get()).x();
  int pixels_to_left_border = 50;
  int window_width = 300;
  int window_x = left - window_width + pixels_to_left_border;
  window_->SetBounds(gfx::Rect(window_x, 100, window_width, 380));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(pixels_to_left_border, 0), HTRIGHT,
      empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, -window_width, 0), 0);
  EXPECT_EQ(base::IntToString(window_x) + ",100 " +
            base::IntToString(kMinimumOnScreenArea - window_x) +
            "x380", window_->bounds().ToString());
}

TEST_F(WorkspaceWindowResizerTest, ResizeWindowOutsideRightWorkArea) {
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(0, 0, 50, 0));
  int right = ScreenAsh::GetDisplayWorkAreaBoundsInParent(
      window_.get()).right();
  int pixels_to_right_border = 50;
  int window_width = 300;
  int window_x = right - pixels_to_right_border;
  window_->SetBounds(gfx::Rect(window_x, 100, window_width, 380));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(window_x, 0), HTLEFT,
      empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, window_width, 0), 0);
  EXPECT_EQ(base::IntToString(right - kMinimumOnScreenArea) +
            ",100 " +
            base::IntToString(window_width - pixels_to_right_border +
                              kMinimumOnScreenArea) +
            "x380", window_->bounds().ToString());
}

TEST_F(WorkspaceWindowResizerTest, ResizeWindowOutsideBottomWorkArea) {
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(0, 0, 50, 0));
  int bottom = ScreenAsh::GetDisplayWorkAreaBoundsInParent(
      window_.get()).bottom();
  int delta_to_bottom = 50;
  int height = 380;
  window_->SetBounds(gfx::Rect(100, bottom - delta_to_bottom, 300, height));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(0, bottom - delta_to_bottom), HTTOP,
      empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, 0, bottom), 0);
  EXPECT_EQ("100," +
            base::IntToString(bottom - kMinimumOnScreenArea) +
            " 300x" +
            base::IntToString(height - (delta_to_bottom -
                                        kMinimumOnScreenArea)),
            window_->bounds().ToString());
}

// Verifies that 'outside' check of the resizer take into account the extended
// desktop in case of repositions.
TEST_F(WorkspaceWindowResizerTest, DragWindowOutsideRightToSecondaryDisplay) {
  // Only primary display.  Changes the window position to fit within the
  // display.
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(0, 0, 50, 0));
  int right = ScreenAsh::GetDisplayWorkAreaBoundsInParent(
      window_.get()).right();
  int pixels_to_right_border = 50;
  int window_width = 300;
  int window_x = right - pixels_to_right_border;
  window_->SetBounds(gfx::Rect(window_x, 100, window_width, 380));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(window_x, 0), HTCAPTION,
      empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, window_width, 0), 0);
  EXPECT_EQ(base::IntToString(right - kMinimumOnScreenArea) +
            ",100 " +
            base::IntToString(window_width) +
            "x380", window_->bounds().ToString());

  // With secondary display.  Operation itself is same but doesn't change
  // the position because the window is still within the secondary display.
  UpdateDisplay("1000x600,600x400");
  Shell::GetInstance()->SetDisplayWorkAreaInsets(
      Shell::GetPrimaryRootWindow(), gfx::Insets(0, 0, 50, 0));
  window_->SetBounds(gfx::Rect(window_x, 100, window_width, 380));
  resizer->Drag(CalculateDragPoint(*resizer, window_width, 0), 0);
  EXPECT_EQ(base::IntToString(window_x + window_width) +
            ",100 " +
            base::IntToString(window_width) +
            "x380", window_->bounds().ToString());
}

// Verifies snapping to edges works.
TEST_F(WorkspaceWindowResizerTest, SnapToEdge) {
  Shell::GetPrimaryRootWindowController()->
      SetShelfAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  window_->SetBounds(gfx::Rect(96, 112, 320, 160));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
  ASSERT_TRUE(resizer.get());
  // Move to an x-coordinate of 15, which should not snap.
  resizer->Drag(CalculateDragPoint(*resizer, -81, 0), 0);
  // An x-coordinate of 7 should snap.
  resizer->Drag(CalculateDragPoint(*resizer, -89, 0), 0);
  EXPECT_EQ("0,112 320x160", window_->bounds().ToString());
  // Move to -15, should still snap to 0.
  resizer->Drag(CalculateDragPoint(*resizer, -111, 0), 0);
  EXPECT_EQ("0,112 320x160", window_->bounds().ToString());
  // At -32 should move past snap points.
  resizer->Drag(CalculateDragPoint(*resizer, -128, 0), 0);
  EXPECT_EQ("-32,112 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, -129, 0), 0);
  EXPECT_EQ("-33,112 320x160", window_->bounds().ToString());

  // Right side should similarly snap.
  resizer->Drag(CalculateDragPoint(*resizer, 800 - 320 - 96 - 15, 0), 0);
  EXPECT_EQ("465,112 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, 800 - 320 - 96 - 7, 0), 0);
  EXPECT_EQ("480,112 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, 800 - 320 - 96 + 15, 0), 0);
  EXPECT_EQ("480,112 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, 800 - 320 - 96 + 32, 0), 0);
  EXPECT_EQ("512,112 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, 800 - 320 - 96 + 33, 0), 0);
  EXPECT_EQ("513,112 320x160", window_->bounds().ToString());

  // And the bottom should snap too.
  resizer->Drag(CalculateDragPoint(*resizer, 0, 600 - 160 - 112 - 3 - 7), 0);
  EXPECT_EQ("96,437 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, 0, 600 - 160 - 112 - 3 + 15), 0);
  EXPECT_EQ("96,437 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, 0, 600 - 160 - 112 - 2 + 32), 0);
  EXPECT_EQ("96,470 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, 0, 600 - 160 - 112 - 2 + 33), 0);
  EXPECT_EQ("96,471 320x160", window_->bounds().ToString());

  // And the top should snap too.
  resizer->Drag(CalculateDragPoint(*resizer, 0, -112 + 20), 0);
  EXPECT_EQ("96,20 320x160", window_->bounds().ToString());
  resizer->Drag(CalculateDragPoint(*resizer, 0, -112 + 7), 0);
  EXPECT_EQ("96,0 320x160", window_->bounds().ToString());
  // No need to test dragging < 0 as we force that to 0.
}

// Verifies a resize snap when dragging TOPLEFT.
TEST_F(WorkspaceWindowResizerTest, SnapToWorkArea_TOPLEFT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTTOPLEFT, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, -98, -199), 0);
  EXPECT_EQ("0,0 120x230", window_->bounds().ToString());
}

// Verifies a resize snap when dragging TOPRIGHT.
TEST_F(WorkspaceWindowResizerTest, SnapToWorkArea_TOPRIGHT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  gfx::Rect work_area(ScreenAsh::GetDisplayWorkAreaBoundsInParent(
                          window_.get()));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTTOPRIGHT, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(
      CalculateDragPoint(*resizer, work_area.right() - 120 - 1, -199), 0);
  EXPECT_EQ(100, window_->bounds().x());
  EXPECT_EQ(work_area.y(), window_->bounds().y());
  EXPECT_EQ(work_area.right() - 100, window_->bounds().width());
  EXPECT_EQ(230, window_->bounds().height());
}

// Verifies a resize snap when dragging BOTTOMRIGHT.
TEST_F(WorkspaceWindowResizerTest, SnapToWorkArea_BOTTOMRIGHT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  gfx::Rect work_area(ScreenAsh::GetDisplayWorkAreaBoundsInParent(
                          window_.get()));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTBOTTOMRIGHT, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(
      CalculateDragPoint(*resizer, work_area.right() - 120 - 1,
                         work_area.bottom() - 220 - 2), 0);
  EXPECT_EQ(100, window_->bounds().x());
  EXPECT_EQ(200, window_->bounds().y());
  EXPECT_EQ(work_area.right() - 100, window_->bounds().width());
  EXPECT_EQ(work_area.bottom() - 200, window_->bounds().height());
}

// Verifies a resize snap when dragging BOTTOMLEFT.
TEST_F(WorkspaceWindowResizerTest, SnapToWorkArea_BOTTOMLEFT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  gfx::Rect work_area(ScreenAsh::GetDisplayWorkAreaBoundsInParent(
                          window_.get()));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTBOTTOMLEFT, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(
      CalculateDragPoint(*resizer, -98, work_area.bottom() - 220 - 2), 0);
  EXPECT_EQ(0, window_->bounds().x());
  EXPECT_EQ(200, window_->bounds().y());
  EXPECT_EQ(120, window_->bounds().width());
  EXPECT_EQ(work_area.bottom() - 200, window_->bounds().height());
}

TEST_F(WorkspaceWindowResizerTest, CtrlDragResizeToExactPosition) {
  window_->SetBounds(gfx::Rect(96, 112, 320, 160));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTBOTTOMRIGHT, empty_windows()));
  ASSERT_TRUE(resizer.get());
  // Resize the right bottom to add 10 in width, 12 in height.
  resizer->Drag(CalculateDragPoint(*resizer, 10, 12), ui::EF_CONTROL_DOWN);
  // Both bottom and right sides to resize to exact size requested.
  EXPECT_EQ("96,112 330x172", window_->bounds().ToString());
}

TEST_F(WorkspaceWindowResizerTest, CtrlCompleteDragMoveToExactPosition) {
  window_->SetBounds(gfx::Rect(96, 112, 320, 160));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
  ASSERT_TRUE(resizer.get());
  // Ctrl + drag the window to new poistion by adding (10, 12) to its origin,
  // the window should move to the exact position.
  resizer->Drag(CalculateDragPoint(*resizer, 10, 12), 0);
  resizer->CompleteDrag(ui::EF_CONTROL_DOWN);
  EXPECT_EQ("106,124 320x160", window_->bounds().ToString());
}

// Check that only usable sizes get returned by the resizer.
TEST_F(WorkspaceWindowResizerTest, TestProperSizerResolutions) {
  // Check that we have the correct work area resolution which fits our
  // expected test result.
  gfx::Rect work_area(ScreenAsh::GetDisplayWorkAreaBoundsInParent(
                          window_.get()));
  EXPECT_EQ(800, work_area.width());

  window_->SetBounds(gfx::Rect(96, 112, 320, 160));
  scoped_ptr<SnapSizer> resizer(new SnapSizer(
      window_.get(),
      gfx::Point(),
      SnapSizer::LEFT_EDGE,
      SnapSizer::OTHER_INPUT));
  ASSERT_TRUE(resizer.get());
  shelf_layout_manager()->SetAutoHideBehavior(
      SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);

  // Check that the list is declining and contains elements of the
  // ideal size list [1280, 1024, 768, 640] as well as 50% and 90% the work
  // area.
  gfx::Rect rect = resizer->GetTargetBoundsForSize(0);
  EXPECT_EQ("0,0 720x597", rect.ToString());
  rect = resizer->GetTargetBoundsForSize(1);
  EXPECT_EQ("0,0 640x597", rect.ToString());
  rect = resizer->GetTargetBoundsForSize(2);
  EXPECT_EQ("0,0 400x597", rect.ToString());
  shelf_layout_manager()->SetAutoHideBehavior(
      SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  rect = resizer->GetTargetBoundsForSize(0);
  EXPECT_EQ("0,0 720x552", rect.ToString());
  rect = resizer->GetTargetBoundsForSize(1);
  EXPECT_EQ("0,0 640x552", rect.ToString());
  rect = resizer->GetTargetBoundsForSize(2);
  EXPECT_EQ("0,0 400x552", rect.ToString());
}

// Verifies that a dragged window will restore to its pre-maximized size.
TEST_F(WorkspaceWindowResizerTest, RestoreToPreMaximizeCoordinates) {
  window_->SetBounds(gfx::Rect(0, 0, 1000, 1000));
  SetRestoreBoundsInScreen(window_.get(), gfx::Rect(96, 112, 320, 160));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
  ASSERT_TRUE(resizer.get());
  // Drag the window to new position by adding (10, 10) to original point,
  // the window should get restored.
  resizer->Drag(CalculateDragPoint(*resizer, 10, 10), 0);
  resizer->CompleteDrag(0);
  EXPECT_EQ("10,10 320x160", window_->bounds().ToString());
  // The restore rectangle should get cleared as well.
  EXPECT_EQ(NULL, GetRestoreBoundsInScreen(window_.get()));
}

// Verifies that a dragged window will restore to its pre-maximized size.
TEST_F(WorkspaceWindowResizerTest, RevertResizeOperation) {
  const gfx::Rect initial_bounds(0, 0, 200, 400);
  window_->SetBounds(initial_bounds);
  SetRestoreBoundsInScreen(window_.get(), gfx::Rect(96, 112, 320, 160));
  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
  ASSERT_TRUE(resizer.get());
  // Drag the window to new poistion by adding (180, 16) to original point,
  // the window should get restored.
  resizer->Drag(CalculateDragPoint(*resizer, 180, 16), 0);
  resizer->RevertDrag();
  EXPECT_EQ(initial_bounds.ToString(), window_->bounds().ToString());
  EXPECT_EQ("96,112 320x160",
      GetRestoreBoundsInScreen(window_.get())->ToString());
}

// Check that only usable sizes get returned by the resizer.
TEST_F(WorkspaceWindowResizerTest, MagneticallyAttach) {
  window_->SetBounds(gfx::Rect(10, 10, 20, 30));
  window2_->SetBounds(gfx::Rect(150, 160, 25, 20));
  window2_->Show();

  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
  ASSERT_TRUE(resizer.get());
  // Move |window| one pixel to the left of |window2|. Should snap to right and
  // top.
  resizer->Drag(CalculateDragPoint(*resizer, 119, 145), 0);
  EXPECT_EQ("130,160 20x30", window_->bounds().ToString());

  // Move |window| one pixel to the right of |window2|. Should snap to left and
  // top.
  resizer->Drag(CalculateDragPoint(*resizer, 164, 145), 0);
  EXPECT_EQ("175,160 20x30", window_->bounds().ToString());

  // Move |window| one pixel above |window2|. Should snap to top and left.
  resizer->Drag(CalculateDragPoint(*resizer, 142, 119), 0);
  EXPECT_EQ("150,130 20x30", window_->bounds().ToString());

  // Move |window| one pixel above the bottom of |window2|. Should snap to
  // bottom and left.
  resizer->Drag(CalculateDragPoint(*resizer, 142, 169), 0);
  EXPECT_EQ("150,180 20x30", window_->bounds().ToString());
}

// The following variants verify magnetic snapping during resize when dragging a
// particular edge.
TEST_F(WorkspaceWindowResizerTest, MagneticallyResize_TOP) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  window2_->SetBounds(gfx::Rect(99, 179, 10, 20));
  window2_->Show();

  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
    window_.get(), gfx::Point(), HTTOP, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
  EXPECT_EQ("100,199 20x31", window_->bounds().ToString());
}

TEST_F(WorkspaceWindowResizerTest, MagneticallyResize_TOPLEFT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  window2_->SetBounds(gfx::Rect(99, 179, 10, 20));
  window2_->Show();

  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTTOPLEFT, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
    EXPECT_EQ("99,199 21x31", window_->bounds().ToString());
    resizer->RevertDrag();
  }

  {
    window2_->SetBounds(gfx::Rect(88, 201, 10, 20));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTTOPLEFT, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
    EXPECT_EQ("98,201 22x29", window_->bounds().ToString());
    resizer->RevertDrag();
  }
}

TEST_F(WorkspaceWindowResizerTest, MagneticallyResize_TOPRIGHT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  window2_->Show();

  {
    window2_->SetBounds(gfx::Rect(111, 179, 10, 20));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTTOPRIGHT, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
    EXPECT_EQ("100,199 21x31", window_->bounds().ToString());
    resizer->RevertDrag();
  }

  {
    window2_->SetBounds(gfx::Rect(121, 199, 10, 20));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTTOPRIGHT, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
    EXPECT_EQ("100,199 21x31", window_->bounds().ToString());
    resizer->RevertDrag();
  }
}

TEST_F(WorkspaceWindowResizerTest, MagneticallyResize_RIGHT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  window2_->SetBounds(gfx::Rect(121, 199, 10, 20));
  window2_->Show();

  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTRIGHT, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
  EXPECT_EQ("100,200 21x30", window_->bounds().ToString());
}

TEST_F(WorkspaceWindowResizerTest, MagneticallyResize_BOTTOMRIGHT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  window2_->Show();

  {
    window2_->SetBounds(gfx::Rect(122, 212, 10, 20));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTBOTTOMRIGHT, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
    EXPECT_EQ("100,200 22x32", window_->bounds().ToString());
    resizer->RevertDrag();
  }

  {
    window2_->SetBounds(gfx::Rect(111, 233, 10, 20));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTBOTTOMRIGHT, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
    EXPECT_EQ("100,200 21x33", window_->bounds().ToString());
    resizer->RevertDrag();
  }
}

TEST_F(WorkspaceWindowResizerTest, MagneticallyResize_BOTTOM) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  window2_->SetBounds(gfx::Rect(111, 233, 10, 20));
  window2_->Show();

  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTBOTTOM, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
  EXPECT_EQ("100,200 20x33", window_->bounds().ToString());
}

TEST_F(WorkspaceWindowResizerTest, MagneticallyResize_BOTTOMLEFT) {
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  window2_->Show();

  {
    window2_->SetBounds(gfx::Rect(99, 231, 10, 20));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTBOTTOMLEFT, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
    EXPECT_EQ("99,200 21x31", window_->bounds().ToString());
    resizer->RevertDrag();
  }

  {
    window2_->SetBounds(gfx::Rect(89, 209, 10, 20));
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTBOTTOMLEFT, empty_windows()));
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
    EXPECT_EQ("99,200 21x29", window_->bounds().ToString());
    resizer->RevertDrag();
  }
}

TEST_F(WorkspaceWindowResizerTest, MagneticallyResize_LEFT) {
  window2_->SetBounds(gfx::Rect(89, 209, 10, 20));
  window_->SetBounds(gfx::Rect(100, 200, 20, 30));
  window2_->Show();

  scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
      window_.get(), gfx::Point(), HTLEFT, empty_windows()));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(CalculateDragPoint(*resizer, 0, 0), 0);
  EXPECT_EQ("99,200 21x30", window_->bounds().ToString());
}

// Verifies cursor's device scale factor is updated whe a window is moved across
// root windows with different device scale factors (http://crbug.com/154183).
TEST_F(WorkspaceWindowResizerTest, MAYBE_CursorDeviceScaleFactor) {
  // The secondary display is logically on the right, but on the system (e.g. X)
  // layer, it's below the primary one. See UpdateDisplay() in ash_test_base.cc.
  UpdateDisplay("400x400,800x800*2");
  shelf_layout_manager()->LayoutShelf();
  Shell::RootWindowList root_windows = Shell::GetAllRootWindows();
  ASSERT_EQ(2U, root_windows.size());

  test::CursorManagerTestApi cursor_test_api(
      Shell::GetInstance()->cursor_manager());
  MouseCursorEventFilter* event_filter =
      Shell::GetInstance()->mouse_cursor_filter();
  // Move window from the root window with 1.0 device scale factor to the root
  // window with 2.0 device scale factor.
  {
    window_->SetBoundsInScreen(gfx::Rect(0, 0, 50, 60),
                               Shell::GetScreen()->GetPrimaryDisplay());
    EXPECT_EQ(root_windows[0], window_->GetRootWindow());
    // Grab (0, 0) of the window.
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    EXPECT_EQ(1.0f, cursor_test_api.GetDeviceScaleFactor());
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, 399, 200), 0);
    event_filter->WarpMouseCursorIfNecessary(root_windows[0],
                                             gfx::Point(399, 200));
    EXPECT_EQ(2.0f, cursor_test_api.GetDeviceScaleFactor());
    resizer->CompleteDrag(0);
    EXPECT_EQ(2.0f, cursor_test_api.GetDeviceScaleFactor());
  }

  // Move window from the root window with 2.0 device scale factor to the root
  // window with 1.0 device scale factor.
  {
    window_->SetBoundsInScreen(
        gfx::Rect(600, 0, 50, 60),
        Shell::GetScreen()->GetDisplayNearestWindow(root_windows[1]));
    EXPECT_EQ(root_windows[1], window_->GetRootWindow());
    // Grab (0, 0) of the window.
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, empty_windows()));
    EXPECT_EQ(2.0f, cursor_test_api.GetDeviceScaleFactor());
    ASSERT_TRUE(resizer.get());
    resizer->Drag(CalculateDragPoint(*resizer, -200, 200), 0);
    event_filter->WarpMouseCursorIfNecessary(root_windows[1],
                                             gfx::Point(400, 200));
    EXPECT_EQ(1.0f, cursor_test_api.GetDeviceScaleFactor());
    resizer->CompleteDrag(0);
    EXPECT_EQ(1.0f, cursor_test_api.GetDeviceScaleFactor());
  }
}

// Test that the user user moved window flag is getting properly set.
TEST_F(WorkspaceWindowResizerTest, CheckUserWindowMangedFlags) {
  window_->SetBounds(gfx::Rect( 0,  50, 400, 200));

  std::vector<aura::Window*> no_attached_windows;
  // Check that an abort doesn't change anything.
  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, no_attached_windows));
    ASSERT_TRUE(resizer.get());
    // Move it 100 to the bottom.
    resizer->Drag(CalculateDragPoint(*resizer, 0, 100), 0);
    EXPECT_EQ("0,150 400x200", window_->bounds().ToString());
    resizer->RevertDrag();

    EXPECT_FALSE(ash::wm::HasUserChangedWindowPositionOrSize(window_.get()));
  }

  // Check that a completed move / size does change the user coordinates.
  {
    scoped_ptr<WorkspaceWindowResizer> resizer(WorkspaceWindowResizer::Create(
        window_.get(), gfx::Point(), HTCAPTION, no_attached_windows));
    ASSERT_TRUE(resizer.get());
    // Move it 100 to the bottom.
    resizer->Drag(CalculateDragPoint(*resizer, 0, 100), 0);
    EXPECT_EQ("0,150 400x200", window_->bounds().ToString());
    resizer->CompleteDrag(0);
    EXPECT_TRUE(ash::wm::HasUserChangedWindowPositionOrSize(window_.get()));
  }
}

}  // namespace internal
}  // namespace ash
