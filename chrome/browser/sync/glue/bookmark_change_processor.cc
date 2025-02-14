// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/bookmark_change_processor.h"

#include <stack>
#include <vector>

#include "base/location.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "content/public/browser/browser_thread.h"
#include "sync/internal_api/public/change_record.h"
#include "sync/internal_api/public/read_node.h"
#include "sync/internal_api/public/write_node.h"
#include "sync/internal_api/public/write_transaction.h"
#include "sync/syncable/entry.h"  // TODO(tim): Investigating bug 121587.
#include "sync/syncable/write_transaction.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/image/image_util.h"

using content::BrowserThread;

namespace browser_sync {

static const char kMobileBookmarksTag[] = "synced_bookmarks";

// Key for sync transaction version in bookmark node meta info.
const char kBookmarkTransactionVersionKey[] = "sync.transaction_version";

BookmarkChangeProcessor::BookmarkChangeProcessor(
    BookmarkModelAssociator* model_associator,
    DataTypeErrorHandler* error_handler)
    : ChangeProcessor(error_handler),
      bookmark_model_(NULL),
      model_associator_(model_associator) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(model_associator);
  DCHECK(error_handler);
}

BookmarkChangeProcessor::~BookmarkChangeProcessor() {
  if (bookmark_model_)
    bookmark_model_->RemoveObserver(this);
}

void BookmarkChangeProcessor::StartImpl(Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!bookmark_model_);
  bookmark_model_ = BookmarkModelFactory::GetForProfile(profile);
  DCHECK(bookmark_model_->IsLoaded());
  bookmark_model_->AddObserver(this);
}

void BookmarkChangeProcessor::UpdateSyncNodeProperties(
    const BookmarkNode* src,
    BookmarkModel* model,
    syncer::WriteNode* dst) {
  // Set the properties of the item.
  dst->SetIsFolder(src->is_folder());
  dst->SetTitle(UTF16ToWideHack(src->GetTitle()));
  sync_pb::BookmarkSpecifics bookmark_specifics(dst->GetBookmarkSpecifics());
  if (!src->is_folder())
    bookmark_specifics.set_url(src->url().spec());
  bookmark_specifics.set_creation_time_us(src->date_added().ToInternalValue());
  dst->SetBookmarkSpecifics(bookmark_specifics);
  SetSyncNodeFavicon(src, model, dst);
}

// static
void BookmarkChangeProcessor::EncodeFavicon(const BookmarkNode* src,
                                            BookmarkModel* model,
                                            std::vector<unsigned char>* dst) {
  const gfx::Image& favicon = model->GetFavicon(src);

  dst->clear();

  // Check for empty images.  This can happen if the favicon is
  // still being loaded.
  if (favicon.IsEmpty())
    return;

  // Re-encode the BookmarkNode's favicon as a PNG, and pass the data to the
  // sync subsystem.
  if (!gfx::PNGEncodedDataFromImage(favicon, dst))
    return;
}

void BookmarkChangeProcessor::RemoveOneSyncNode(
    syncer::WriteTransaction* trans, const BookmarkNode* node) {
  syncer::WriteNode sync_node(trans);
  if (!model_associator_->InitSyncNodeFromChromeId(node->id(), &sync_node)) {
    error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
                                                        std::string());
    return;
  }
  // This node should have no children.
  DCHECK(!sync_node.HasChildren());
  // Remove association and delete the sync node.
  model_associator_->Disassociate(sync_node.GetId());
  sync_node.Remove();
}

void BookmarkChangeProcessor::RemoveSyncNodeHierarchy(
    const BookmarkNode* topmost) {
  int64 new_version =
      syncer::syncable::kInvalidTransactionVersion;
  {
    syncer::WriteTransaction trans(FROM_HERE, share_handle(), &new_version);

    // Later logic assumes that |topmost| has been unlinked.
    DCHECK(topmost->is_root());

    // A BookmarkModel deletion event means that |node| and all its children
    // were deleted. Sync backend expects children to be deleted individually,
    // so we do a depth-first-search here.  At each step, we consider the
    // |index|-th child of |node|.  |index_stack| stores index values for the
    // parent levels.
    std::stack<int> index_stack;
    index_stack.push(0);  // For the final pop.  It's never used.
    const BookmarkNode* node = topmost;
    int index = 0;
    while (node) {
      // The top of |index_stack| should always be |node|'s index.
      DCHECK(node->is_root() || (node->parent()->GetIndexOf(node) ==
        index_stack.top()));
      if (index == node->child_count()) {
        // If we've processed all of |node|'s children, delete |node| and move
        // on to its successor.
        RemoveOneSyncNode(&trans, node);
        node = node->parent();
        index = index_stack.top() + 1;      // (top() + 0) was what we removed.
        index_stack.pop();
      } else {
        // If |node| has an unprocessed child, process it next after pushing the
        // current state onto the stack.
        DCHECK_LT(index, node->child_count());
        index_stack.push(index);
        node = node->GetChild(index);
        index = 0;
      }
    }
    DCHECK(index_stack.empty());  // Nothing should be left on the stack.
  }

  // Don't need to update versions of deleted nodes.
  UpdateTransactionVersion(new_version, bookmark_model_,
                           std::vector<const BookmarkNode*>());
}

void BookmarkChangeProcessor::Loaded(BookmarkModel* model,
                                     bool ids_reassigned) {
  NOTREACHED();
}

void BookmarkChangeProcessor::BookmarkModelBeingDeleted(
    BookmarkModel* model) {
  NOTREACHED();
  bookmark_model_ = NULL;
}

void BookmarkChangeProcessor::BookmarkNodeAdded(BookmarkModel* model,
                                                const BookmarkNode* parent,
                                                int index) {
  DCHECK(share_handle());

  int64 new_version = syncer::syncable::kInvalidTransactionVersion;
  int64 sync_id = syncer::kInvalidId;
  {
    // Acquire a scoped write lock via a transaction.
    syncer::WriteTransaction trans(FROM_HERE, share_handle(), &new_version);
    sync_id = CreateSyncNode(parent, model, index, &trans,
                             model_associator_, error_handler());
  }

  if (syncer::kInvalidId != sync_id) {
    // Siblings of added node in sync DB will also be updated to reflect new
    // PREV_ID/NEXT_ID and thus get a new version. But we only update version
    // of added node here. After switching to ordinals for positioning,
    // PREV_ID/NEXT_ID will be deprecated and siblings will not be updated.
    UpdateTransactionVersion(
        new_version, model,
        std::vector<const BookmarkNode*>(1, parent->GetChild(index)));
  }
}

// static
int64 BookmarkChangeProcessor::CreateSyncNode(const BookmarkNode* parent,
    BookmarkModel* model, int index, syncer::WriteTransaction* trans,
    BookmarkModelAssociator* associator,
    DataTypeErrorHandler* error_handler) {
  const BookmarkNode* child = parent->GetChild(index);
  DCHECK(child);

  // Create a WriteNode container to hold the new node.
  syncer::WriteNode sync_child(trans);

  // Actually create the node with the appropriate initial position.
  if (!PlaceSyncNode(CREATE, parent, index, trans, &sync_child, associator)) {
    error_handler->OnSingleDatatypeUnrecoverableError(FROM_HERE,
        "Sync node creation failed; recovery unlikely");
    return syncer::kInvalidId;
  }

  UpdateSyncNodeProperties(child, model, &sync_child);

  // Associate the ID from the sync domain with the bookmark node, so that we
  // can refer back to this item later.
  associator->Associate(child, sync_child.GetId());

  return sync_child.GetId();
}

void BookmarkChangeProcessor::BookmarkNodeRemoved(BookmarkModel* model,
                                                  const BookmarkNode* parent,
                                                  int index,
                                                  const BookmarkNode* node) {
  RemoveSyncNodeHierarchy(node);
}

void BookmarkChangeProcessor::BookmarkNodeChanged(BookmarkModel* model,
                                                  const BookmarkNode* node) {
  // We shouldn't see changes to the top-level nodes.
  if (model->is_permanent_node(node)) {
    NOTREACHED() << "Saw update to permanent node!";
    return;
  }

  int64 new_version = syncer::syncable::kInvalidTransactionVersion;
  {
    // Acquire a scoped write lock via a transaction.
    syncer::WriteTransaction trans(FROM_HERE, share_handle(), &new_version);

    // Lookup the sync node that's associated with |node|.
    syncer::WriteNode sync_node(&trans);
    if (!model_associator_->InitSyncNodeFromChromeId(node->id(), &sync_node)) {
      // TODO(tim): Investigating bug 121587.
      if (model_associator_->GetSyncIdFromChromeId(node->id()) ==
                                                   syncer::kInvalidId) {
        error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
            "Bookmark id not found in model associator on BookmarkNodeChanged");
        LOG(ERROR) << "Bad id.";
      } else if (!sync_node.GetEntry()->good()) {
        error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
            "Could not InitByIdLookup on BookmarkNodeChanged, good() failed");
        LOG(ERROR) << "Bad entry.";
      } else if (sync_node.GetEntry()->Get(syncer::syncable::IS_DEL)) {
        error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
            "Could not InitByIdLookup on BookmarkNodeChanged, is_del true");
        LOG(ERROR) << "Deleted entry.";
      } else {
        syncer::Cryptographer* crypto = trans.GetCryptographer();
        syncer::ModelTypeSet encrypted_types(trans.GetEncryptedTypes());
        const sync_pb::EntitySpecifics& specifics =
            sync_node.GetEntry()->Get(syncer::syncable::SPECIFICS);
        CHECK(specifics.has_encrypted());
        const bool can_decrypt = crypto->CanDecrypt(specifics.encrypted());
        const bool agreement = encrypted_types.Has(syncer::BOOKMARKS);
        if (!agreement && !can_decrypt) {
          error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
              "Could not InitByIdLookup on BookmarkNodeChanged, "
              " Cryptographer thinks bookmarks not encrypted, and CanDecrypt"
              " failed.");
          LOG(ERROR) << "Case 1.";
        } else if (agreement && can_decrypt) {
          error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
              "Could not InitByIdLookup on BookmarkNodeChanged, "
              " Cryptographer thinks bookmarks are encrypted, and CanDecrypt"
              " succeeded (?!), but DecryptIfNecessary failed.");
          LOG(ERROR) << "Case 2.";
        } else if (agreement) {
          error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
              "Could not InitByIdLookup on BookmarkNodeChanged, "
              " Cryptographer thinks bookmarks are encrypted, but CanDecrypt"
              " failed.");
          LOG(ERROR) << "Case 3.";
        } else {
          error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
              "Could not InitByIdLookup on BookmarkNodeChanged, "
              " Cryptographer thinks bookmarks not encrypted, but CanDecrypt"
              " succeeded (super weird, btw)");
          LOG(ERROR) << "Case 4.";
        }
      }
      return;
    }

    UpdateSyncNodeProperties(node, model, &sync_node);

    DCHECK_EQ(sync_node.GetIsFolder(), node->is_folder());
    DCHECK_EQ(model_associator_->GetChromeNodeFromSyncId(
              sync_node.GetParentId()),
              node->parent());
    // This node's index should be one more than the predecessor's index.
    DCHECK_EQ(node->parent()->GetIndexOf(node),
              CalculateBookmarkModelInsertionIndex(node->parent(),
                                                   &sync_node,
                                                   model_associator_));
  }

  UpdateTransactionVersion(new_version, model,
                           std::vector<const BookmarkNode*>(1, node));
}

void BookmarkChangeProcessor::BookmarkNodeMoved(BookmarkModel* model,
      const BookmarkNode* old_parent, int old_index,
      const BookmarkNode* new_parent, int new_index) {
  const BookmarkNode* child = new_parent->GetChild(new_index);
  // We shouldn't see changes to the top-level nodes.
  if (model->is_permanent_node(child)) {
    NOTREACHED() << "Saw update to permanent node!";
    return;
  }

  int64 new_version = syncer::syncable::kInvalidTransactionVersion;
  {
    // Acquire a scoped write lock via a transaction.
    syncer::WriteTransaction trans(FROM_HERE, share_handle(), &new_version);

    // Lookup the sync node that's associated with |child|.
    syncer::WriteNode sync_node(&trans);
    if (!model_associator_->InitSyncNodeFromChromeId(child->id(), &sync_node)) {
      error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
                                                          std::string());
      return;
    }

    if (!PlaceSyncNode(MOVE, new_parent, new_index, &trans, &sync_node,
                       model_associator_)) {
      error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
                                                          std::string());
      return;
    }
  }

  UpdateTransactionVersion(new_version, model,
                           std::vector<const BookmarkNode*>(1, child));
}

void BookmarkChangeProcessor::BookmarkNodeFaviconChanged(
    BookmarkModel* model,
    const BookmarkNode* node) {
  BookmarkNodeChanged(model, node);
}

void BookmarkChangeProcessor::BookmarkNodeChildrenReordered(
    BookmarkModel* model, const BookmarkNode* node) {
  int64 new_version = syncer::syncable::kInvalidTransactionVersion;
  std::vector<const BookmarkNode*> children;
  {
    // Acquire a scoped write lock via a transaction.
    syncer::WriteTransaction trans(FROM_HERE, share_handle(), &new_version);

    // The given node's children got reordered. We need to reorder all the
    // children of the corresponding sync node.
    for (int i = 0; i < node->child_count(); ++i) {
      const BookmarkNode* child = node->GetChild(i);
      children.push_back(child);

      syncer::WriteNode sync_child(&trans);
      if (!model_associator_->InitSyncNodeFromChromeId(child->id(),
                                                       &sync_child)) {
        error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
                                                            std::string());
        return;
      }
      DCHECK_EQ(sync_child.GetParentId(),
                model_associator_->GetSyncIdFromChromeId(node->id()));

      if (!PlaceSyncNode(MOVE, node, i, &trans, &sync_child,
                         model_associator_)) {
        error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
                                                            std::string());
        return;
      }
    }
  }

  // TODO(haitaol): Filter out children that didn't actually change.
  UpdateTransactionVersion(new_version, model, children);
}

// static
bool BookmarkChangeProcessor::PlaceSyncNode(MoveOrCreate operation,
      const BookmarkNode* parent, int index, syncer::WriteTransaction* trans,
      syncer::WriteNode* dst, BookmarkModelAssociator* associator) {
  syncer::ReadNode sync_parent(trans);
  if (!associator->InitSyncNodeFromChromeId(parent->id(), &sync_parent)) {
    LOG(WARNING) << "Parent lookup failed";
    return false;
  }

  bool success = false;
  if (index == 0) {
    // Insert into first position.
    success = (operation == CREATE) ?
        dst->InitByCreation(syncer::BOOKMARKS, sync_parent, NULL) :
        dst->SetPosition(sync_parent, NULL);
    if (success) {
      DCHECK_EQ(dst->GetParentId(), sync_parent.GetId());
      DCHECK_EQ(dst->GetId(), sync_parent.GetFirstChildId());
      DCHECK_EQ(dst->GetPredecessorId(), syncer::kInvalidId);
    }
  } else {
    // Find the bookmark model predecessor, and insert after it.
    const BookmarkNode* prev = parent->GetChild(index - 1);
    syncer::ReadNode sync_prev(trans);
    if (!associator->InitSyncNodeFromChromeId(prev->id(), &sync_prev)) {
      LOG(WARNING) << "Predecessor lookup failed";
      return false;
    }
    success = (operation == CREATE) ?
        dst->InitByCreation(syncer::BOOKMARKS, sync_parent, &sync_prev) :
        dst->SetPosition(sync_parent, &sync_prev);
    if (success) {
      DCHECK_EQ(dst->GetParentId(), sync_parent.GetId());
      DCHECK_EQ(dst->GetPredecessorId(), sync_prev.GetId());
      DCHECK_EQ(dst->GetId(), sync_prev.GetSuccessorId());
    }
  }
  return success;
}

// Static.
// Determine the bookmark model index to which a node must be moved so that
// predecessor of the node (in the bookmark model) matches the predecessor of
// |source| (in the sync model).
// As a precondition, this assumes that the predecessor of |source| has been
// updated and is already in the correct position in the bookmark model.
int BookmarkChangeProcessor::CalculateBookmarkModelInsertionIndex(
    const BookmarkNode* parent,
    const syncer::BaseNode* child_info,
    BookmarkModelAssociator* model_associator) {
  DCHECK(parent);
  DCHECK(child_info);
  int64 predecessor_id = child_info->GetPredecessorId();
  // A return ID of kInvalidId indicates no predecessor.
  if (predecessor_id == syncer::kInvalidId)
    return 0;

  // Otherwise, insert after the predecessor bookmark node.
  const BookmarkNode* predecessor =
      model_associator->GetChromeNodeFromSyncId(predecessor_id);
  DCHECK(predecessor);
  DCHECK_EQ(predecessor->parent(), parent);
  return parent->GetIndexOf(predecessor) + 1;
}

// ApplyModelChanges is called by the sync backend after changes have been made
// to the sync engine's model.  Apply these changes to the browser bookmark
// model.
void BookmarkChangeProcessor::ApplyChangesFromSyncModel(
    const syncer::BaseTransaction* trans,
    int64 model_version,
    const syncer::ImmutableChangeRecordList& changes) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // A note about ordering.  Sync backend is responsible for ordering the change
  // records in the following order:
  //
  // 1. Deletions, from leaves up to parents.
  // 2. Existing items with synced parents & predecessors.
  // 3. New items with synced parents & predecessors.
  // 4. Items with parents & predecessors in the list.
  // 5. Repeat #4 until all items are in the list.
  //
  // "Predecessor" here means the previous item within a given folder; an item
  // in the first position is always said to have a synced predecessor.
  // For the most part, applying these changes in the order given will yield
  // the correct result.  There is one exception, however: for items that are
  // moved away from a folder that is being deleted, we will process the delete
  // before the move.  Since deletions in the bookmark model propagate from
  // parent to child, we must move them to a temporary location.
  BookmarkModel* model = bookmark_model_;

  // We are going to make changes to the bookmarks model, but don't want to end
  // up in a feedback loop, so remove ourselves as an observer while applying
  // changes.
  model->RemoveObserver(this);

  // A parent to hold nodes temporarily orphaned by parent deletion.  It is
  // lazily created inside the loop.
  const BookmarkNode* foster_parent = NULL;

  // Whether we have passed all the deletes (which should be at the
  // front of the list).
  bool passed_deletes = false;
  for (syncer::ChangeRecordList::const_iterator it =
           changes.Get().begin(); it != changes.Get().end(); ++it) {
    const BookmarkNode* dst =
        model_associator_->GetChromeNodeFromSyncId(it->id);
    // Ignore changes to the permanent top-level nodes.  We only care about
    // their children.
    if (model->is_permanent_node(dst))
      continue;
    if (it->action ==
        syncer::ChangeRecord::ACTION_DELETE) {
      // Deletions should always be at the front of the list.
      DCHECK(!passed_deletes);
      // Children of a deleted node should not be deleted; they may be
      // reparented by a later change record.  Move them to a temporary place.
      if (!dst) // Can't do anything if we can't find the chrome node.
        continue;
      const BookmarkNode* parent = dst->parent();
      if (!dst->empty()) {
        if (!foster_parent) {
          foster_parent = model->AddFolder(model->other_node(),
                                           model->other_node()->child_count(),
                                           string16());
          if (!foster_parent) {
            error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
                "Failed to create foster parent.");
            return;
          }
        }
        for (int i = dst->child_count() - 1; i >= 0; --i) {
          model->Move(dst->GetChild(i), foster_parent,
                      foster_parent->child_count());
        }
      }
      DCHECK_EQ(dst->child_count(), 0) << "Node being deleted has children";
      model_associator_->Disassociate(it->id);
      int index = parent->GetIndexOf(dst);
      if (index > -1)
        model->Remove(parent, index);
      dst = NULL;
    } else {
      DCHECK_EQ((it->action ==
          syncer::ChangeRecord::ACTION_ADD), (dst == NULL))
          << "ACTION_ADD should be seen if and only if the node is unknown.";
      passed_deletes = true;

      syncer::ReadNode src(trans);
      if (src.InitByIdLookup(it->id) != syncer::BaseNode::INIT_OK) {
        error_handler()->OnSingleDatatypeUnrecoverableError(FROM_HERE,
            "ApplyModelChanges was passed a bad ID");
        return;
      }

      const BookmarkNode* node = CreateOrUpdateBookmarkNode(&src, model,
                                                            model_associator_);
      if (node) {
        bookmark_model_->SetNodeMetaInfo(node, kBookmarkTransactionVersionKey,
                                         base::Int64ToString(model_version));
      } else {
        // Because the Synced Bookmarks node can be created server side, it's
        // possible it'll arrive at the client as an update. In that case it
        // won't have been associated at startup, the GetChromeNodeFromSyncId
        // call above will return NULL, and we won't detect it as a permanent
        // node, resulting in us trying to create it here (which will
        // fail). Therefore, we add special logic here just to detect the
        // Synced Bookmarks folder.
        syncer::ReadNode synced_bookmarks(trans);
        if (synced_bookmarks.InitByTagLookup(kMobileBookmarksTag) ==
                syncer::BaseNode::INIT_OK &&
            synced_bookmarks.GetId() == it->id) {
          // This is a newly created Synced Bookmarks node. Associate it.
          model_associator_->Associate(model->mobile_node(), it->id);
        } else {
          // We ignore bookmarks we can't add. Chances are this is caused by
          // a bookmark that was not fully associated.
          LOG(ERROR) << "Failed to create bookmark node with title "
                     << src.GetTitle() + " and url "
                     << src.GetURL().possibly_invalid_spec();
        }
      }
    }
  }
  // Clean up the temporary node.
  if (foster_parent) {
    // There should be no nodes left under the foster parent.
    DCHECK_EQ(foster_parent->child_count(), 0);
    model->Remove(foster_parent->parent(),
                  foster_parent->parent()->GetIndexOf(foster_parent));
    foster_parent = NULL;
  }

  // The visibility of the mobile node may need to change.
  model_associator_->UpdatePermanentNodeVisibility();

  // We are now ready to hear about bookmarks changes again.
  model->AddObserver(this);

  // All changes are applied in bookmark model. Set transaction version on
  // bookmark model to mark as synced.
  model->SetNodeMetaInfo(model->root_node(), kBookmarkTransactionVersionKey,
                         base::Int64ToString(model_version));
}

// Static.
// Create a bookmark node corresponding to |src| if one is not already
// associated with |src|.
const BookmarkNode* BookmarkChangeProcessor::CreateOrUpdateBookmarkNode(
    syncer::BaseNode* src,
    BookmarkModel* model,
    BookmarkModelAssociator* model_associator) {
  const BookmarkNode* parent =
      model_associator->GetChromeNodeFromSyncId(src->GetParentId());
  if (!parent) {
    DLOG(WARNING) << "Could not find parent of node being added/updated."
      << " Node title: " << src->GetTitle()
      << ", parent id = " << src->GetParentId();

    return NULL;
  }
  int index = CalculateBookmarkModelInsertionIndex(parent,
                                                   src,
                                                   model_associator);
  const BookmarkNode* dst = model_associator->GetChromeNodeFromSyncId(
      src->GetId());
  if (!dst) {
    dst = CreateBookmarkNode(src, parent, model, index);
    if (dst)
      model_associator->Associate(dst, src->GetId());
  } else {
    // URL and is_folder are not expected to change.
    // TODO(ncarter): Determine if such changes should be legal or not.
    DCHECK_EQ(src->GetIsFolder(), dst->is_folder());

    // Handle reparenting and/or repositioning.
    model->Move(dst, parent, index);

    if (!src->GetIsFolder())
      model->SetURL(dst, src->GetURL());
    model->SetTitle(dst, UTF8ToUTF16(src->GetTitle()));
    if (src->GetBookmarkSpecifics().has_creation_time_us()) {
      model->SetDateAdded(dst,
                          base::Time::FromInternalValue(
                              src->GetBookmarkSpecifics().creation_time_us()));
    }

    SetBookmarkFavicon(src, dst, model);
  }

  return dst;
}

// static
void BookmarkChangeProcessor::UpdateTransactionVersion(
    int64 new_version,
    BookmarkModel* model,
    const std::vector<const BookmarkNode*>& nodes) {
  if (new_version != syncer::syncable::kInvalidTransactionVersion) {
    model->SetNodeMetaInfo(model->root_node(), kBookmarkTransactionVersionKey,
                           base::Int64ToString(new_version));
    for (size_t i = 0; i < nodes.size(); ++i) {
      model->SetNodeMetaInfo(nodes[i], kBookmarkTransactionVersionKey,
                             base::Int64ToString(new_version));
    }
  }
}

// static
// Creates a bookmark node under the given parent node from the given sync
// node. Returns the newly created node.
const BookmarkNode* BookmarkChangeProcessor::CreateBookmarkNode(
    syncer::BaseNode* sync_node,
    const BookmarkNode* parent,
    BookmarkModel* model,
    int index) {
  DCHECK(parent);
  DCHECK(index >= 0 && index <= parent->child_count());

  const BookmarkNode* node;
  if (sync_node->GetIsFolder()) {
    node = model->AddFolder(parent, index,
                            UTF8ToUTF16(sync_node->GetTitle()));
  } else {
    // 'creation_time_us' was added in m24. Assume a time of 0 means now.
    const int64 create_time_internal =
        sync_node->GetBookmarkSpecifics().creation_time_us();
    base::Time create_time = (create_time_internal == 0) ?
        base::Time::Now() : base::Time::FromInternalValue(create_time_internal);
    node = model->AddURLWithCreationTime(parent, index,
                                         UTF8ToUTF16(sync_node->GetTitle()),
                                         sync_node->GetURL(), create_time);
    if (node)
      SetBookmarkFavicon(sync_node, node, model);
  }
  return node;
}

// static
// Sets the favicon of the given bookmark node from the given sync node.
bool BookmarkChangeProcessor::SetBookmarkFavicon(
    syncer::BaseNode* sync_node,
    const BookmarkNode* bookmark_node,
    BookmarkModel* bookmark_model) {
  std::vector<unsigned char> icon_bytes_vector;
  sync_node->GetFaviconBytes(&icon_bytes_vector);
  if (icon_bytes_vector.empty())
    return false;

  ApplyBookmarkFavicon(bookmark_node, bookmark_model->profile(),
                       icon_bytes_vector);

  return true;
}

// static
// Applies the given favicon bytes vector to the given bookmark node.
void BookmarkChangeProcessor::ApplyBookmarkFavicon(
    const BookmarkNode* bookmark_node,
    Profile* profile,
    const std::vector<unsigned char>& icon_bytes_vector) {
  HistoryService* history =
      HistoryServiceFactory::GetForProfile(profile, Profile::EXPLICIT_ACCESS);
  FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(profile, Profile::EXPLICIT_ACCESS);

  history->AddPageNoVisitForBookmark(bookmark_node->url(),
                                     bookmark_node->GetTitle());
  // The client may have cached the favicon at 2x. Use MergeFavicon() as not to
  // overwrite the cached 2x favicon bitmap. Use the page URL as a fake icon URL
  // as it is guaranteed to be unique. Sync favicons are always
  // gfx::kFaviconSize in width and height. Store the favicon into history as
  // such.
  scoped_refptr<base::RefCountedMemory> bitmap_data(
      new base::RefCountedBytes(icon_bytes_vector));
  gfx::Size pixel_size(gfx::kFaviconSize, gfx::kFaviconSize);
  favicon_service->MergeFavicon(bookmark_node->url(),
                                bookmark_node->url(),
                                history::FAVICON,
                                bitmap_data,
                                pixel_size);
}

// static
void BookmarkChangeProcessor::SetSyncNodeFavicon(
    const BookmarkNode* bookmark_node,
    BookmarkModel* model,
    syncer::WriteNode* sync_node) {
  std::vector<unsigned char> favicon_bytes;
  EncodeFavicon(bookmark_node, model, &favicon_bytes);
  if (!favicon_bytes.empty())
    sync_node->SetFaviconBytes(favicon_bytes);
}

}  // namespace browser_sync
