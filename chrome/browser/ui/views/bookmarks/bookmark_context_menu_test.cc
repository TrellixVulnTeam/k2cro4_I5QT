// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_context_menu.h"

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/test/test_browser_thread.h"
#include "grit/generated_resources.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/clipboard/clipboard.h"

#if defined(OS_WIN)
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#endif

using content::BrowserThread;
using content::OpenURLParams;
using content::PageNavigator;
using content::WebContents;

namespace {

// PageNavigator implementation that records the URL.
class TestingPageNavigator : public PageNavigator {
 public:
  virtual WebContents* OpenURL(const OpenURLParams& params) OVERRIDE {
    urls_.push_back(params.url);
    return NULL;
  }

  std::vector<GURL> urls_;
};

}  // namespace

class BookmarkContextMenuTest : public testing::Test {
 public:
  BookmarkContextMenuTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        file_thread_(BrowserThread::FILE, &message_loop_),
        model_(NULL) {
  }

  virtual void SetUp() {
#if defined(OS_WIN)
    bookmark_utils::DisableBookmarkBarViewAnimationsForTesting(true);
#endif

    profile_.reset(new TestingProfile());
    profile_->CreateBookmarkModel(true);
    profile_->BlockUntilBookmarkModelLoaded();

    model_ = BookmarkModelFactory::GetForProfile(profile_.get());

    AddTestData();
  }

  virtual void TearDown() {
#if defined(OS_WIN)
    bookmark_utils::DisableBookmarkBarViewAnimationsForTesting(false);
#endif

    ui::Clipboard::DestroyClipboardForCurrentThread();

    BrowserThread::GetBlockingPool()->FlushForTesting();
    // Flush the message loop to make application verifiers happy.
    message_loop_.RunUntilIdle();
  }

 protected:
  MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;
  scoped_ptr<TestingProfile> profile_;
  BookmarkModel* model_;
  TestingPageNavigator navigator_;

 private:
  // Creates the following structure:
  // a
  // F1
  //  f1a
  //  F11
  //   f11a
  // F2
  // F3
  // F4
  //   f4a
  void AddTestData() {
    const BookmarkNode* bb_node = model_->bookmark_bar_node();
    std::string test_base = "file:///c:/tmp/";
    model_->AddURL(bb_node, 0, ASCIIToUTF16("a"), GURL(test_base + "a"));
    const BookmarkNode* f1 = model_->AddFolder(bb_node, 1, ASCIIToUTF16("F1"));
    model_->AddURL(f1, 0, ASCIIToUTF16("f1a"), GURL(test_base + "f1a"));
    const BookmarkNode* f11 = model_->AddFolder(f1, 1, ASCIIToUTF16("F11"));
    model_->AddURL(f11, 0, ASCIIToUTF16("f11a"), GURL(test_base + "f11a"));
    model_->AddFolder(bb_node, 2, ASCIIToUTF16("F2"));
    model_->AddFolder(bb_node, 3, ASCIIToUTF16("F3"));
    const BookmarkNode* f4 = model_->AddFolder(bb_node, 4, ASCIIToUTF16("F4"));
    model_->AddURL(f4, 0, ASCIIToUTF16("f4a"), GURL(test_base + "f4a"));
  }
};

// Tests Deleting from the menu.
TEST_F(BookmarkContextMenuTest, DeleteURL) {
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(model_->bookmark_bar_node()->GetChild(0));
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false);
  GURL url = model_->bookmark_bar_node()->GetChild(0)->url();
  ASSERT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  // Delete the URL.
  controller.ExecuteCommand(IDC_BOOKMARK_BAR_REMOVE);
  // Model shouldn't have URL anymore.
  ASSERT_FALSE(model_->IsBookmarked(url));
}

// Tests open all on a folder with a couple of bookmarks.
TEST_F(BookmarkContextMenuTest, OpenAll) {
  const BookmarkNode* folder = model_->bookmark_bar_node()->GetChild(1);
  chrome::OpenAll(NULL, &navigator_, folder, NEW_FOREGROUND_TAB);

  // Should have navigated to F1's child but not F11's child.
  ASSERT_EQ(static_cast<size_t>(1), navigator_.urls_.size());
  ASSERT_TRUE(folder->GetChild(0)->url() == navigator_.urls_[0]);
}

// Tests the enabled state of the menus when supplied an empty vector.
TEST_F(BookmarkContextMenuTest, EmptyNodes) {
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, model_->other_node(),
      std::vector<const BookmarkNode*>(), false);
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_NEW_FOLDER));
}

// Tests the enabled state of the menus when supplied a vector with a single
// url.
TEST_F(BookmarkContextMenuTest, SingleURL) {
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(model_->bookmark_bar_node()->GetChild(0));
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false);
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW));
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_NEW_FOLDER));
}

// Tests the enabled state of the menus when supplied a vector with multiple
// urls.
TEST_F(BookmarkContextMenuTest, MultipleURLs) {
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(model_->bookmark_bar_node()->GetChild(0));
  nodes.push_back(model_->bookmark_bar_node()->GetChild(1)->GetChild(0));
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false);
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW));
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_NEW_FOLDER));
}

// Tests the enabled state of the menus when supplied an vector with a single
// folder.
TEST_F(BookmarkContextMenuTest, SingleFolder) {
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(model_->bookmark_bar_node()->GetChild(2));
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false);
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_NEW_FOLDER));
}

// Tests the enabled state of the menus when supplied a vector with multiple
// folders, all of which are empty.
TEST_F(BookmarkContextMenuTest, MultipleEmptyFolders) {
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(model_->bookmark_bar_node()->GetChild(2));
  nodes.push_back(model_->bookmark_bar_node()->GetChild(3));
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false);
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_NEW_FOLDER));
}

// Tests the enabled state of the menus when supplied a vector with multiple
// folders, some of which contain URLs.
TEST_F(BookmarkContextMenuTest, MultipleFoldersWithURLs) {
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(model_->bookmark_bar_node()->GetChild(3));
  nodes.push_back(model_->bookmark_bar_node()->GetChild(4));
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false);
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW));
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
  EXPECT_TRUE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK));
  EXPECT_TRUE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_NEW_FOLDER));
}

// Tests the enabled state of open incognito.
TEST_F(BookmarkContextMenuTest, DisableIncognito) {
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(model_->bookmark_bar_node()->GetChild(0));
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false);
  profile_->set_incognito(true);
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_INCOGNITO));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
}

// Tests that you can't remove/edit when showing the other node.
TEST_F(BookmarkContextMenuTest, DisabledItemsWithOtherNode) {
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(model_->other_node());
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, nodes[0], nodes, false);
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_EDIT));
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
}

// Tests the enabled state of the menus when supplied an empty vector and null
// parent.
TEST_F(BookmarkContextMenuTest, EmptyNodesNullParent) {
  BookmarkContextMenu controller(
      NULL, NULL, profile_.get(), NULL, NULL,
      std::vector<const BookmarkNode*>(), false);
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
  EXPECT_FALSE(controller.IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK));
  EXPECT_FALSE(
      controller.IsCommandEnabled(IDC_BOOKMARK_BAR_NEW_FOLDER));
}

TEST_F(BookmarkContextMenuTest, CutCopyPasteNode) {
  const BookmarkNode* bb_node = model_->bookmark_bar_node();
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(bb_node->GetChild(0));
  scoped_ptr<BookmarkContextMenu> controller(new BookmarkContextMenu(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false));
  EXPECT_TRUE(controller->IsCommandEnabled(IDC_COPY));
  EXPECT_TRUE(controller->IsCommandEnabled(IDC_CUT));

  // Copy the URL.
  controller->ExecuteCommand(IDC_COPY);

  controller.reset(new BookmarkContextMenu(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false));
  int old_count = bb_node->child_count();
  controller->ExecuteCommand(IDC_PASTE);

  ASSERT_TRUE(bb_node->GetChild(1)->is_url());
  ASSERT_EQ(old_count + 1, bb_node->child_count());
  ASSERT_EQ(bb_node->GetChild(0)->url(), bb_node->GetChild(1)->url());

  controller.reset(new BookmarkContextMenu(
      NULL, NULL, profile_.get(), NULL, nodes[0]->parent(), nodes, false));
  // Cut the URL.
  controller->ExecuteCommand(IDC_CUT);
  ASSERT_TRUE(bb_node->GetChild(0)->is_url());
  ASSERT_TRUE(bb_node->GetChild(1)->is_folder());
  ASSERT_EQ(old_count, bb_node->child_count());
}
