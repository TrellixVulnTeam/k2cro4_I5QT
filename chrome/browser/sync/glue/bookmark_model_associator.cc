// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/bookmark_model_associator.h"

#include <stack>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/hash_tables.h"
#include "base/location.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/glue/bookmark_change_processor.h"
#include "content/public/browser/browser_thread.h"
#include "sync/api/sync_error.h"
#include "sync/internal_api/public/read_node.h"
#include "sync/internal_api/public/read_transaction.h"
#include "sync/internal_api/public/write_node.h"
#include "sync/internal_api/public/write_transaction.h"
#include "sync/syncable/write_transaction.h"
#include "sync/util/cryptographer.h"
#include "sync/util/data_type_histogram.h"

using content::BrowserThread;

namespace browser_sync {

// The sync protocol identifies top-level entities by means of well-known tags,
// which should not be confused with titles.  Each tag corresponds to a
// singleton instance of a particular top-level node in a user's share; the
// tags are consistent across users. The tags allow us to locate the specific
// folders whose contents we care about synchronizing, without having to do a
// lookup by name or path.  The tags should not be made user-visible.
// For example, the tag "bookmark_bar" represents the permanent node for
// bookmarks bar in Chrome. The tag "other_bookmarks" represents the permanent
// folder Other Bookmarks in Chrome.
//
// It is the responsibility of something upstream (at time of writing,
// the sync server) to create these tagged nodes when initializing sync
// for the first time for a user.  Thus, once the backend finishes
// initializing, the ProfileSyncService can rely on the presence of tagged
// nodes.
//
// TODO(ncarter): Pull these tags from an external protocol specification
// rather than hardcoding them here.
static const char kBookmarkBarTag[] = "bookmark_bar";
static const char kMobileBookmarksTag[] = "synced_bookmarks";
static const char kOtherBookmarksTag[] = "other_bookmarks";
static const char kServerError[] =
    "Server did not create top-level nodes.  Possibly we are running against "
    "an out-of-date server?";

// Bookmark comparer for map of bookmark nodes.
class BookmarkComparer {
 public:
  // Compares the two given nodes and returns whether node1 should appear
  // before node2 in strict weak ordering.
  bool operator()(const BookmarkNode* node1,
                  const BookmarkNode* node2) const {
    DCHECK(node1);
    DCHECK(node2);

    // Keep folder nodes before non-folder nodes.
    if (node1->is_folder() != node2->is_folder())
      return node1->is_folder();

    int result = node1->GetTitle().compare(node2->GetTitle());
    if (result != 0)
      return result < 0;

    return node1->url() < node2->url();
  }
};

// Provides the following abstraction: given a parent bookmark node, find best
// matching child node for many sync nodes.
class BookmarkNodeFinder {
 public:
  // Creates an instance with the given parent bookmark node.
  explicit BookmarkNodeFinder(const BookmarkNode* parent_node);

  // Finds best matching node for the given sync node.
  // Returns the matching node if one exists; NULL otherwise. If a matching
  // node is found, it's removed for further matches.
  const BookmarkNode* FindBookmarkNode(const syncer::BaseNode& sync_node);

 private:
  typedef std::multiset<const BookmarkNode*, BookmarkComparer> BookmarkNodesSet;

  const BookmarkNode* parent_node_;
  BookmarkNodesSet child_nodes_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkNodeFinder);
};

class ScopedAssociationUpdater {
 public:
  explicit ScopedAssociationUpdater(BookmarkModel* model) {
    model_ = model;
    model->BeginExtensiveChanges();
  }

  ~ScopedAssociationUpdater() {
    model_->EndExtensiveChanges();
  }

 private:
  BookmarkModel* model_;

  DISALLOW_COPY_AND_ASSIGN(ScopedAssociationUpdater);
};

BookmarkNodeFinder::BookmarkNodeFinder(const BookmarkNode* parent_node)
    : parent_node_(parent_node) {
  for (int i = 0; i < parent_node_->child_count(); ++i) {
    child_nodes_.insert(parent_node_->GetChild(i));
  }
}

const BookmarkNode* BookmarkNodeFinder::FindBookmarkNode(
    const syncer::BaseNode& sync_node) {
  // Create a bookmark node from the given sync node.
  BookmarkNode temp_node(sync_node.GetURL());
  temp_node.SetTitle(UTF8ToUTF16(sync_node.GetTitle()));
  if (sync_node.GetIsFolder())
    temp_node.set_type(BookmarkNode::FOLDER);
  else
    temp_node.set_type(BookmarkNode::URL);

  const BookmarkNode* result = NULL;
  BookmarkNodesSet::iterator iter = child_nodes_.find(&temp_node);
  if (iter != child_nodes_.end()) {
    result = *iter;
    // Remove the matched node so we don't match with it again.
    child_nodes_.erase(iter);
  }

  return result;
}

// Helper class to build an index of bookmark nodes by their IDs.
class BookmarkNodeIdIndex {
 public:
  BookmarkNodeIdIndex() { }
  ~BookmarkNodeIdIndex() { }

  // Adds the given bookmark node and all its descendants to the ID index.
  // Does nothing if node is NULL.
  void AddAll(const BookmarkNode* node);

  // Finds the bookmark node with the given ID.
  // Returns NULL if none exists with the given id.
  const BookmarkNode* Find(int64 id) const;

  // Returns the count of nodes in the index.
  size_t count() const { return node_index_.size(); }

 private:
  typedef base::hash_map<int64, const BookmarkNode*> BookmarkIdMap;
  // Map that holds nodes indexed by their ids.
  BookmarkIdMap node_index_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkNodeIdIndex);
};

void BookmarkNodeIdIndex::AddAll(const BookmarkNode* node) {
  if (!node)
    return;

  node_index_[node->id()] = node;

  if (!node->is_folder())
    return;

  for (int i = 0; i < node->child_count(); ++i)
    AddAll(node->GetChild(i));
}

const BookmarkNode* BookmarkNodeIdIndex::Find(int64 id) const {
  BookmarkIdMap::const_iterator iter = node_index_.find(id);
  return iter == node_index_.end() ? NULL : iter->second;
}

BookmarkModelAssociator::BookmarkModelAssociator(
    BookmarkModel* bookmark_model,
    syncer::UserShare* user_share,
    DataTypeErrorHandler* unrecoverable_error_handler,
    bool expect_mobile_bookmarks_folder)
    : bookmark_model_(bookmark_model),
      user_share_(user_share),
      unrecoverable_error_handler_(unrecoverable_error_handler),
      expect_mobile_bookmarks_folder_(expect_mobile_bookmarks_folder),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(bookmark_model_);
  DCHECK(user_share_);
  DCHECK(unrecoverable_error_handler_);
}

BookmarkModelAssociator::~BookmarkModelAssociator() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

void BookmarkModelAssociator::UpdatePermanentNodeVisibility() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(bookmark_model_->IsLoaded());

  bookmark_model_->SetPermanentNodeVisible(
      BookmarkNode::MOBILE,
      id_map_.find(bookmark_model_->mobile_node()->id()) != id_map_.end());
}

syncer::SyncError BookmarkModelAssociator::DisassociateModels() {
  id_map_.clear();
  id_map_inverse_.clear();
  dirty_associations_sync_ids_.clear();
  return syncer::SyncError();
}

int64 BookmarkModelAssociator::GetSyncIdFromChromeId(const int64& node_id) {
  BookmarkIdToSyncIdMap::const_iterator iter = id_map_.find(node_id);
  return iter == id_map_.end() ? syncer::kInvalidId : iter->second;
}

const BookmarkNode* BookmarkModelAssociator::GetChromeNodeFromSyncId(
    int64 sync_id) {
  SyncIdToBookmarkNodeMap::const_iterator iter = id_map_inverse_.find(sync_id);
  return iter == id_map_inverse_.end() ? NULL : iter->second;
}

bool BookmarkModelAssociator::InitSyncNodeFromChromeId(
    const int64& node_id,
    syncer::BaseNode* sync_node) {
  DCHECK(sync_node);
  int64 sync_id = GetSyncIdFromChromeId(node_id);
  if (sync_id == syncer::kInvalidId)
    return false;
  if (sync_node->InitByIdLookup(sync_id) != syncer::BaseNode::INIT_OK)
    return false;
  DCHECK(sync_node->GetId() == sync_id);
  return true;
}

void BookmarkModelAssociator::Associate(const BookmarkNode* node,
                                        int64 sync_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  int64 node_id = node->id();
  DCHECK_NE(sync_id, syncer::kInvalidId);
  DCHECK(id_map_.find(node_id) == id_map_.end());
  DCHECK(id_map_inverse_.find(sync_id) == id_map_inverse_.end());
  id_map_[node_id] = sync_id;
  id_map_inverse_[sync_id] = node;
  dirty_associations_sync_ids_.insert(sync_id);
  PostPersistAssociationsTask();
  UpdatePermanentNodeVisibility();
}

void BookmarkModelAssociator::Disassociate(int64 sync_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  SyncIdToBookmarkNodeMap::iterator iter = id_map_inverse_.find(sync_id);
  if (iter == id_map_inverse_.end())
    return;
  id_map_.erase(iter->second->id());
  id_map_inverse_.erase(iter);
  dirty_associations_sync_ids_.erase(sync_id);
}

bool BookmarkModelAssociator::SyncModelHasUserCreatedNodes(bool* has_nodes) {
  DCHECK(has_nodes);
  *has_nodes = false;
  bool has_mobile_folder = true;
  int64 bookmark_bar_sync_id;
  if (!GetSyncIdForTaggedNode(kBookmarkBarTag, &bookmark_bar_sync_id)) {
    return false;
  }
  int64 other_bookmarks_sync_id;
  if (!GetSyncIdForTaggedNode(kOtherBookmarksTag, &other_bookmarks_sync_id)) {
    return false;
  }
  int64 mobile_bookmarks_sync_id;
  if (!GetSyncIdForTaggedNode(kMobileBookmarksTag, &mobile_bookmarks_sync_id)) {
    has_mobile_folder = false;
  }

  syncer::ReadTransaction trans(FROM_HERE, user_share_);

  syncer::ReadNode bookmark_bar_node(&trans);
  if (bookmark_bar_node.InitByIdLookup(bookmark_bar_sync_id) !=
          syncer::BaseNode::INIT_OK) {
    return false;
  }

  syncer::ReadNode other_bookmarks_node(&trans);
  if (other_bookmarks_node.InitByIdLookup(other_bookmarks_sync_id) !=
          syncer::BaseNode::INIT_OK) {
    return false;
  }

  syncer::ReadNode mobile_bookmarks_node(&trans);
  if (has_mobile_folder &&
      mobile_bookmarks_node.InitByIdLookup(mobile_bookmarks_sync_id) !=
          syncer::BaseNode::INIT_OK) {
    return false;
  }

  // Sync model has user created nodes if any of the permanent nodes has
  // children.
  *has_nodes = bookmark_bar_node.HasChildren() ||
      other_bookmarks_node.HasChildren() ||
      (has_mobile_folder && mobile_bookmarks_node.HasChildren());
  return true;
}

bool BookmarkModelAssociator::NodesMatch(
    const BookmarkNode* bookmark,
    const syncer::BaseNode* sync_node) const {
  if (bookmark->GetTitle() != UTF8ToUTF16(sync_node->GetTitle()))
    return false;
  if (bookmark->is_folder() != sync_node->GetIsFolder())
    return false;
  if (bookmark->is_url()) {
    if (bookmark->url() != sync_node->GetURL())
      return false;
  }
  // Don't compare favicons here, because they are not really
  // user-updated and we don't have versioning information -- a site changing
  // its favicon shouldn't result in a bookmark mismatch.
  return true;
}

bool BookmarkModelAssociator::AssociateTaggedPermanentNode(
    const BookmarkNode* permanent_node, const std::string&tag) {
  // Do nothing if |permanent_node| is already initialized and associated.
  int64 sync_id = GetSyncIdFromChromeId(permanent_node->id());
  if (sync_id != syncer::kInvalidId)
    return true;
  if (!GetSyncIdForTaggedNode(tag, &sync_id))
    return false;

  Associate(permanent_node, sync_id);
  return true;
}

bool BookmarkModelAssociator::GetSyncIdForTaggedNode(const std::string& tag,
                                                     int64* sync_id) {
  syncer::ReadTransaction trans(FROM_HERE, user_share_);
  syncer::ReadNode sync_node(&trans);
  if (sync_node.InitByTagLookup(tag.c_str()) != syncer::BaseNode::INIT_OK)
    return false;
  *sync_id = sync_node.GetId();
  return true;
}

syncer::SyncError BookmarkModelAssociator::AssociateModels(
    syncer::SyncMergeResult* local_merge_result,
    syncer::SyncMergeResult* syncer_merge_result) {
  CheckModelSyncState();

  scoped_ptr<ScopedAssociationUpdater> association_updater(
      new ScopedAssociationUpdater(bookmark_model_));
  DisassociateModels();

  return BuildAssociations(local_merge_result, syncer_merge_result);
}

syncer::SyncError BookmarkModelAssociator::BuildAssociations(
    syncer::SyncMergeResult* local_merge_result,
    syncer::SyncMergeResult* syncer_merge_result) {
  // Algorithm description:
  // Match up the roots and recursively do the following:
  // * For each sync node for the current sync parent node, find the best
  //   matching bookmark node under the corresponding bookmark parent node.
  //   If no matching node is found, create a new bookmark node in the same
  //   position as the corresponding sync node.
  //   If a matching node is found, update the properties of it from the
  //   corresponding sync node.
  // * When all children sync nodes are done, add the extra children bookmark
  //   nodes to the sync parent node.
  //
  // This algorithm will do a good job of merging when folder names are a good
  // indicator of the two folders being the same. It will handle reordering and
  // new node addition very well (without creating duplicates).
  // This algorithm will not do well if the folder name has changes but the
  // children under them are all the same.

  DCHECK(bookmark_model_->IsLoaded());

  // To prime our association, we associate the top-level nodes, Bookmark Bar
  // and Other Bookmarks.
  if (!AssociateTaggedPermanentNode(bookmark_model_->bookmark_bar_node(),
                                    kBookmarkBarTag)) {
    return unrecoverable_error_handler_->CreateAndUploadError(
        FROM_HERE,
        "Bookmark bar node not found",
        model_type());
  }

  if (!AssociateTaggedPermanentNode(bookmark_model_->other_node(),
                                    kOtherBookmarksTag)) {
    return unrecoverable_error_handler_->CreateAndUploadError(
        FROM_HERE,
        "Other bookmarks node not found",
        model_type());
  }

  if (!AssociateTaggedPermanentNode(bookmark_model_->mobile_node(),
                                    kMobileBookmarksTag) &&
      expect_mobile_bookmarks_folder_) {
    return unrecoverable_error_handler_->CreateAndUploadError(
        FROM_HERE,
        "Mobile bookmarks node not found",
        model_type());
  }

  int64 bookmark_bar_sync_id = GetSyncIdFromChromeId(
      bookmark_model_->bookmark_bar_node()->id());
  DCHECK_NE(bookmark_bar_sync_id, syncer::kInvalidId);
  int64 other_bookmarks_sync_id = GetSyncIdFromChromeId(
      bookmark_model_->other_node()->id());
  DCHECK_NE(other_bookmarks_sync_id, syncer::kInvalidId);
  int64 mobile_bookmarks_sync_id = GetSyncIdFromChromeId(
       bookmark_model_->mobile_node()->id());
  if (expect_mobile_bookmarks_folder_) {
    DCHECK_NE(syncer::kInvalidId, mobile_bookmarks_sync_id);
  }

  // WARNING: The order in which we push these should match their order in the
  // bookmark model (see BookmarkModel::DoneLoading(..)).
  std::stack<int64> dfs_stack;
  dfs_stack.push(bookmark_bar_sync_id);
  dfs_stack.push(other_bookmarks_sync_id);
  if (mobile_bookmarks_sync_id != syncer::kInvalidId)
    dfs_stack.push(mobile_bookmarks_sync_id);

  syncer::WriteTransaction trans(FROM_HERE, user_share_);
  syncer::ReadNode bm_root(&trans);
  if (bm_root.InitByTagLookup(syncer::ModelTypeToRootTag(syncer::BOOKMARKS)) ==
      syncer::BaseNode::INIT_OK) {
    syncer_merge_result->set_num_items_before_association(
        bm_root.GetTotalNodeCount());
  }
  local_merge_result->set_num_items_before_association(
      bookmark_model_->root_node()->GetTotalNodeCount());

  while (!dfs_stack.empty()) {
    int64 sync_parent_id = dfs_stack.top();
    dfs_stack.pop();

    syncer::ReadNode sync_parent(&trans);
    if (sync_parent.InitByIdLookup(sync_parent_id) !=
            syncer::BaseNode::INIT_OK) {
      return unrecoverable_error_handler_->CreateAndUploadError(
          FROM_HERE,
          "Failed to lookup node.",
          model_type());
    }
    // Only folder nodes are pushed on to the stack.
    DCHECK(sync_parent.GetIsFolder());

    const BookmarkNode* parent_node = GetChromeNodeFromSyncId(sync_parent_id);
    DCHECK(parent_node->is_folder());

    BookmarkNodeFinder node_finder(parent_node);

    int index = 0;
    int64 sync_child_id = sync_parent.GetFirstChildId();
    while (sync_child_id != syncer::kInvalidId) {
      syncer::WriteNode sync_child_node(&trans);
      if (sync_child_node.InitByIdLookup(sync_child_id) !=
              syncer::BaseNode::INIT_OK) {
        return unrecoverable_error_handler_->CreateAndUploadError(
            FROM_HERE,
            "Failed to lookup node.",
            model_type());
      }

      const BookmarkNode* child_node = NULL;
      child_node = node_finder.FindBookmarkNode(sync_child_node);
      if (child_node)
        Associate(child_node, sync_child_id);
      // All bookmarks are currently modified at association time (even if
      // it doesn't change anything).
      // TODO(sync): introduce logic to only modify the bookmark model if
      // necessary.
      const BookmarkNode* new_child_node =
          BookmarkChangeProcessor::CreateOrUpdateBookmarkNode(
              &sync_child_node,
              bookmark_model_,
              this);
      if (new_child_node != child_node) {
        local_merge_result->set_num_items_added(
            local_merge_result->num_items_added() + 1);
      } else {
        local_merge_result->set_num_items_modified(
            local_merge_result->num_items_modified() + 1);
      }
      if (sync_child_node.GetIsFolder())
        dfs_stack.push(sync_child_id);

      sync_child_id = sync_child_node.GetSuccessorId();
      ++index;
    }

    // At this point all the children nodes of the parent sync node have
    // corresponding children in the parent bookmark node and they are all in
    // the right positions: from 0 to index - 1.
    // So the children starting from index in the parent bookmark node are the
    // ones that are not present in the parent sync node. So create them.
    for (int i = index; i < parent_node->child_count(); ++i) {
      sync_child_id = BookmarkChangeProcessor::CreateSyncNode(
          parent_node, bookmark_model_, i, &trans, this,
          unrecoverable_error_handler_);
      if (syncer::kInvalidId == sync_child_id) {
        return unrecoverable_error_handler_->CreateAndUploadError(
            FROM_HERE,
            "Failed to create sync node.",
            model_type());
      }
      syncer_merge_result->set_num_items_added(
          syncer_merge_result->num_items_added() + 1);
      if (parent_node->GetChild(i)->is_folder())
        dfs_stack.push(sync_child_id);
    }
  }

  local_merge_result->set_num_items_after_association(
      bookmark_model_->root_node()->GetTotalNodeCount());
  syncer_merge_result->set_num_items_after_association(
      bm_root.GetTotalNodeCount());

  return syncer::SyncError();
}

void BookmarkModelAssociator::PostPersistAssociationsTask() {
  // No need to post a task if a task is already pending.
  if (weak_factory_.HasWeakPtrs())
    return;
  MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(
          &BookmarkModelAssociator::PersistAssociations,
          weak_factory_.GetWeakPtr()));
}

void BookmarkModelAssociator::PersistAssociations() {
  // If there are no dirty associations we have nothing to do. We handle this
  // explicity instead of letting the for loop do it to avoid creating a write
  // transaction in this case.
  if (dirty_associations_sync_ids_.empty()) {
    DCHECK(id_map_.empty());
    DCHECK(id_map_inverse_.empty());
    return;
  }

  int64 new_version = syncer::syncable::kInvalidTransactionVersion;
  std::vector<const BookmarkNode*> bnodes;
  {
    syncer::WriteTransaction trans(FROM_HERE, user_share_, &new_version);
    DirtyAssociationsSyncIds::iterator iter;
    for (iter = dirty_associations_sync_ids_.begin();
         iter != dirty_associations_sync_ids_.end();
         ++iter) {
      int64 sync_id = *iter;
      syncer::WriteNode sync_node(&trans);
      if (sync_node.InitByIdLookup(sync_id) != syncer::BaseNode::INIT_OK) {
        unrecoverable_error_handler_->OnSingleDatatypeUnrecoverableError(
            FROM_HERE,
            "Could not lookup bookmark node for ID persistence.");
        return;
      }
      const BookmarkNode* node = GetChromeNodeFromSyncId(sync_id);
      if (node && sync_node.GetExternalId() != node->id()) {
        sync_node.SetExternalId(node->id());
        bnodes.push_back(node);
      }
    }
    dirty_associations_sync_ids_.clear();
  }

  BookmarkChangeProcessor::UpdateTransactionVersion(new_version,
                                                    bookmark_model_,
                                                    bnodes);
}

bool BookmarkModelAssociator::CryptoReadyIfNecessary() {
  // We only access the cryptographer while holding a transaction.
  syncer::ReadTransaction trans(FROM_HERE, user_share_);
  const syncer::ModelTypeSet encrypted_types = trans.GetEncryptedTypes();
  return !encrypted_types.Has(syncer::BOOKMARKS) ||
      trans.GetCryptographer()->is_ready();
}

void BookmarkModelAssociator::CheckModelSyncState() const {
  std::string version_str;
  if (bookmark_model_->root_node()->GetMetaInfo(kBookmarkTransactionVersionKey,
                                                &version_str)) {
    syncer::ReadTransaction trans(FROM_HERE, user_share_);
    int64 native_version;
    if (base::StringToInt64(version_str, &native_version) &&
        native_version != trans.GetModelVersion(syncer::BOOKMARKS)) {
      UMA_HISTOGRAM_ENUMERATION("Sync.LocalModelOutOfSync",
                                syncer::BOOKMARKS, syncer::MODEL_TYPE_COUNT);
      // Clear version on bookmark model so that we only report error once.
      bookmark_model_->DeleteNodeMetaInfo(bookmark_model_->root_node(),
                                          kBookmarkTransactionVersionKey);
    }
  }
}

}  // namespace browser_sync
