// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_INTERNAL_API_PUBLIC_BASE_NODE_H_
#define SYNC_INTERNAL_API_PUBLIC_BASE_NODE_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/time.h"
#include "googleurl/src/gurl.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/protocol/sync.pb.h"

// Forward declarations of internal class types so that sync API objects
// may have opaque pointers to these types.
namespace base {
class DictionaryValue;
}

namespace sync_pb {
class AppSpecifics;
class AutofillSpecifics;
class AutofillProfileSpecifics;
class BookmarkSpecifics;
class EntitySpecifics;
class ExtensionSpecifics;
class SessionSpecifics;
class NigoriSpecifics;
class PreferenceSpecifics;
class PasswordSpecificsData;
class ThemeSpecifics;
class TypedUrlSpecifics;
}

namespace syncer {

class BaseTransaction;

namespace syncable {
class BaseTransaction;
class Entry;
}

// A valid BaseNode will never have an ID of zero.
static const int64 kInvalidId = 0;

// BaseNode wraps syncable::Entry, and corresponds to a single object's state.
// This, like syncable::Entry, is intended for use on the stack.  A valid
// transaction is necessary to create a BaseNode or any of its children.
// Unlike syncable::Entry, a sync API BaseNode is identified primarily by its
// int64 metahandle, which we call an ID here.
class BaseNode {
 public:
  // Enumerates the possible outcomes of trying to initialize a sync node.
  enum InitByLookupResult {
    INIT_OK,
    // Could not find an entry matching the lookup criteria.
    INIT_FAILED_ENTRY_NOT_GOOD,
    // Found an entry, but it is already deleted.
    INIT_FAILED_ENTRY_IS_DEL,
    // Found an entry, but was unable to decrypt.
    INIT_FAILED_DECRYPT_IF_NECESSARY,
    // A precondition was not met for calling init, such as legal input
    // arguments.
    INIT_FAILED_PRECONDITION,
  };

  // All subclasses of BaseNode must provide a way to initialize themselves by
  // doing an ID lookup.  Returns false on failure.  An invalid or deleted
  // ID will result in failure.
  virtual InitByLookupResult InitByIdLookup(int64 id) = 0;

  // All subclasses of BaseNode must also provide a way to initialize themselves
  // by doing a client tag lookup. Returns false on failure. A deleted node
  // will return FALSE.
  virtual InitByLookupResult InitByClientTagLookup(
      ModelType model_type,
      const std::string& tag) = 0;

  // Each object is identified by a 64-bit id (internally, the syncable
  // metahandle).  These ids are strictly local handles.  They will persist
  // on this client, but the same object on a different client may have a
  // different ID value.
  virtual int64 GetId() const;

  // Returns the modification time of the object.
  base::Time GetModificationTime() const;

  // Nodes are hierarchically arranged into a single-rooted tree.
  // InitByRootLookup on ReadNode allows access to the root. GetParentId is
  // how you find a node's parent.
  int64 GetParentId() const;

  // Nodes are either folders or not.  This corresponds to the IS_DIR property
  // of syncable::Entry.
  bool GetIsFolder() const;

  // Returns the title of the object.
  // Uniqueness of the title is not enforced on siblings -- it is not an error
  // for two children to share a title.
  std::string GetTitle() const;

  // Returns the model type of this object.  The model type is set at node
  // creation time and is expected never to change.
  ModelType GetModelType() const;

  // Getter specific to the BOOKMARK datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == BOOKMARK.
  const sync_pb::BookmarkSpecifics& GetBookmarkSpecifics() const;

  // Legacy, bookmark-specific getter that wraps GetBookmarkSpecifics() above.
  // Returns the URL of a bookmark object.
  // TODO(ncarter): Remove this datatype-specific accessor.
  GURL GetURL() const;

  // Legacy, bookmark-specific getter that wraps GetBookmarkSpecifics() above.
  // Fill in a vector with the byte data of this node's favicon.  Assumes
  // that the node is a bookmark.
  // Favicons are expected to be PNG images, and though no verification is
  // done on the syncapi client of this, the server may reject favicon updates
  // that are invalid for whatever reason.
  // TODO(ncarter): Remove this datatype-specific accessor.
  void GetFaviconBytes(std::vector<unsigned char>* output) const;

  // Getter specific to the APPS datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == APPS.
  const sync_pb::AppSpecifics& GetAppSpecifics() const;

  // Getter specific to the AUTOFILL datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == AUTOFILL.
  const sync_pb::AutofillSpecifics& GetAutofillSpecifics() const;

  virtual const sync_pb::AutofillProfileSpecifics&
      GetAutofillProfileSpecifics() const;

  // Getter specific to the NIGORI datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == NIGORI.
  const sync_pb::NigoriSpecifics& GetNigoriSpecifics() const;

  // Getter specific to the PASSWORD datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == PASSWORD.
  const sync_pb::PasswordSpecificsData& GetPasswordSpecifics() const;

  // Getter specific to the PREFERENCE datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == PREFERENCE.
  const sync_pb::PreferenceSpecifics& GetPreferenceSpecifics() const;

  // Getter specific to the THEME datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == THEME.
  const sync_pb::ThemeSpecifics& GetThemeSpecifics() const;

  // Getter specific to the TYPED_URLS datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == TYPED_URLS.
  const sync_pb::TypedUrlSpecifics& GetTypedUrlSpecifics() const;

  // Getter specific to the EXTENSIONS datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == EXTENSIONS.
  const sync_pb::ExtensionSpecifics& GetExtensionSpecifics() const;

  // Getter specific to the SESSIONS datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == SESSIONS.
  const sync_pb::SessionSpecifics& GetSessionSpecifics() const;

  // Getter specific to the DEVICE_INFO datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == DEVICE_INFO.
  const sync_pb::DeviceInfoSpecifics& GetDeviceInfoSpecifics() const;

  // Getter specific to the EXPERIMENTS datatype.  Returns protobuf
  // data.  Can only be called if GetModelType() == EXPERIMENTS.
  const sync_pb::ExperimentsSpecifics& GetExperimentsSpecifics() const;

  const sync_pb::EntitySpecifics& GetEntitySpecifics() const;

  // Returns the local external ID associated with the node.
  int64 GetExternalId() const;

  // Returns true iff this node has children.
  bool HasChildren() const;

  // Return the ID of the node immediately before this in the sibling order.
  // For the first node in the ordering, return 0.
  int64 GetPredecessorId() const;

  // Return the ID of the node immediately after this in the sibling order.
  // For the last node in the ordering, return 0.
  int64 GetSuccessorId() const;

  // Return the ID of the first child of this node.  If this node has no
  // children, return 0.
  int64 GetFirstChildId() const;

  // Returns the total number of nodes including and beneath this node.
  // Recursively iterates through all children.
  int GetTotalNodeCount() const;

  // These virtual accessors provide access to data members of derived classes.
  virtual const syncable::Entry* GetEntry() const = 0;
  virtual const BaseTransaction* GetTransaction() const = 0;

  // Dumps a summary of node info into a DictionaryValue and returns it.
  // Transfers ownership of the DictionaryValue to the caller.
  base::DictionaryValue* GetSummaryAsValue() const;

  // Dumps all node details into a DictionaryValue and returns it.
  // Transfers ownership of the DictionaryValue to the caller.
  base::DictionaryValue* GetDetailsAsValue() const;

 protected:
  BaseNode();
  virtual ~BaseNode();
  // The server has a size limit on client tags, so we generate a fixed length
  // hash locally. This also ensures that ModelTypes have unique namespaces.
  static std::string GenerateSyncableHash(
      ModelType model_type,
      const std::string& client_tag);

  // Determines whether part of the entry is encrypted, and if so attempts to
  // decrypt it. Unless decryption is necessary and fails, this will always
  // return |true|. If the contents are encrypted, the decrypted data will be
  // stored in |unencrypted_data_|.
  // This method is invoked once when the BaseNode is initialized.
  bool DecryptIfNecessary();

  // Returns the unencrypted specifics associated with |entry|. If |entry| was
  // not encrypted, it directly returns |entry|'s EntitySpecifics. Otherwise,
  // returns |unencrypted_data_|.
  const sync_pb::EntitySpecifics& GetUnencryptedSpecifics(
      const syncable::Entry* entry) const;

  // Copy |specifics| into |unencrypted_data_|.
  void SetUnencryptedSpecifics(const sync_pb::EntitySpecifics& specifics);

 private:
  // Have to friend the test class as well to allow member functions to access
  // protected/private BaseNode methods.
  friend class SyncManagerTest;
  FRIEND_TEST_ALL_PREFIXES(SyncApiTest, GenerateSyncableHash);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, UpdateEntryWithEncryption);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest,
                           UpdatePasswordSetEntitySpecificsNoChange);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, UpdatePasswordSetPasswordSpecifics);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, UpdatePasswordNewPassphrase);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, UpdatePasswordReencryptEverything);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, SetBookmarkTitle);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, SetBookmarkTitleWithEncryption);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, SetNonBookmarkTitle);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, SetNonBookmarkTitleWithEncryption);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, SetPreviouslyEncryptedSpecifics);
  FRIEND_TEST_ALL_PREFIXES(SyncManagerTest, IncrementTransactionVersion);

  void* operator new(size_t size);  // Node is meant for stack use only.

  // A holder for the unencrypted data stored in an encrypted node.
  sync_pb::EntitySpecifics unencrypted_data_;

  // Same as |unencrypted_data_|, but for legacy password encryption.
  scoped_ptr<sync_pb::PasswordSpecificsData> password_data_;

  DISALLOW_COPY_AND_ASSIGN(BaseNode);
};

}  // namespace syncer

#endif  // SYNC_INTERNAL_API_PUBLIC_BASE_NODE_H_
