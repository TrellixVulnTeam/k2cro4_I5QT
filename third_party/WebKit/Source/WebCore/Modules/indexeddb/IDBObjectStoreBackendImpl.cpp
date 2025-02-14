/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "IDBObjectStoreBackendImpl.h"

#if ENABLE(INDEXED_DATABASE)

#include "CrossThreadTask.h"
#include "IDBBackingStore.h"
#include "IDBBindingUtilities.h"
#include "IDBCallbacks.h"
#include "IDBCursorBackendImpl.h"
#include "IDBDatabaseBackendImpl.h"
#include "IDBDatabaseException.h"
#include "IDBIndexBackendImpl.h"
#include "IDBKey.h"
#include "IDBKeyPath.h"
#include "IDBKeyRange.h"
#include "IDBTracing.h"
#include "IDBTransactionBackendImpl.h"
#include <wtf/MathExtras.h>

namespace WebCore {

IDBObjectStoreBackendImpl::~IDBObjectStoreBackendImpl()
{
}

IDBObjectStoreBackendImpl::IDBObjectStoreBackendImpl(const IDBDatabaseBackendImpl* database, const IDBObjectStoreMetadata& metadata)
    : m_database(database)
    , m_metadata(metadata)
{
    loadIndexes();
}

IDBObjectStoreMetadata IDBObjectStoreBackendImpl::metadata() const
{
    IDBObjectStoreMetadata metadata(m_metadata);
    for (IndexMap::const_iterator it = m_indexes.begin(); it != m_indexes.end(); ++it)
        metadata.indexes.set(it->key, it->value->metadata());
    return metadata;
}

void IDBObjectStoreBackendImpl::get(PassRefPtr<IDBKeyRange> prpKeyRange, PassRefPtr<IDBCallbacks> prpCallbacks, IDBTransactionBackendInterface* transactionPtr, ExceptionCode&)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::get");
    RefPtr<IDBObjectStoreBackendImpl> objectStore = this;
    RefPtr<IDBKeyRange> keyRange = prpKeyRange;
    RefPtr<IDBCallbacks> callbacks = prpCallbacks;
    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);
    if (!transaction->scheduleTask(
            createCallbackTask(&IDBObjectStoreBackendImpl::getInternal, objectStore, keyRange, callbacks, transaction)))
        callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::IDB_ABORT_ERR));
}

void IDBObjectStoreBackendImpl::getInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBKeyRange> keyRange, PassRefPtr<IDBCallbacks> callbacks, PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::getInternal");
    RefPtr<IDBKey> key;
    if (keyRange->isOnlyKey())
        key = keyRange->lower();
    else {
        RefPtr<IDBBackingStore::Cursor> backingStoreCursor = objectStore->backingStore()->openObjectStoreCursor(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), keyRange.get(), IDBCursor::NEXT);
        if (!backingStoreCursor) {
            callbacks->onSuccess();
            return;
        }
        key = backingStoreCursor->key();
    }

    String wireData = objectStore->backingStore()->getRecord(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), *key);
    if (wireData.isNull()) {
        callbacks->onSuccess();
        return;
    }

    if (objectStore->autoIncrement() && !objectStore->keyPath().isNull()) {
        callbacks->onSuccess(SerializedScriptValue::createFromWire(wireData),
                             key, objectStore->keyPath());
        return;
    }
    callbacks->onSuccess(SerializedScriptValue::createFromWire(wireData));
}

void IDBObjectStoreBackendImpl::put(PassRefPtr<SerializedScriptValue> prpValue, PassRefPtr<IDBKey> prpKey, PutMode putMode, PassRefPtr<IDBCallbacks> prpCallbacks, IDBTransactionBackendInterface* transactionPtr, const Vector<int64_t>& indexIds, const Vector<IndexKeys>& indexKeys)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::put");

    RefPtr<IDBObjectStoreBackendImpl> objectStore = this;
    RefPtr<SerializedScriptValue> value = prpValue;
    RefPtr<IDBKey> key = prpKey;
    RefPtr<IDBCallbacks> callbacks = prpCallbacks;
    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);
    ASSERT(transaction->mode() != IDBTransaction::READ_ONLY);

    OwnPtr<Vector<int64_t> > newIndexIds = adoptPtr(new Vector<int64_t>(indexIds));
    OwnPtr<Vector<IndexKeys> > newIndexKeys = adoptPtr(new Vector<IndexKeys>(indexKeys));

    ASSERT(autoIncrement() || key);

    if (!transaction->scheduleTask(
        createCallbackTask(&IDBObjectStoreBackendImpl::putInternal, objectStore, value, key, putMode, callbacks, transaction, newIndexIds.release(), newIndexKeys.release())))
        callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::IDB_ABORT_ERR));
}

namespace {
class IndexWriter {
public:
    explicit IndexWriter(const IDBIndexMetadata& indexMetadata)
        : m_indexMetadata(indexMetadata)
    { }

    IndexWriter(const IDBIndexMetadata& indexMetadata,
                const IDBObjectStoreBackendInterface::IndexKeys& indexKeys)
        : m_indexMetadata(indexMetadata)
        , m_indexKeys(indexKeys)
    { }

    bool verifyIndexKeys(IDBBackingStore& backingStore, IDBBackingStore::Transaction* transaction,
                         int64_t databaseId, int64_t objectStoreId, int64_t indexId,
                         const IDBKey* primaryKey = 0, String* errorMessage = 0)
    {
        for (size_t i = 0; i < m_indexKeys.size(); ++i) {
            if (!addingKeyAllowed(backingStore, transaction, databaseId, objectStoreId, indexId,
                                  (m_indexKeys)[i].get(), primaryKey)) {
                if (errorMessage)
                    *errorMessage = String::format("Unable to add key to index '%s': at least one key does not satisfy the uniqueness requirements.",
                                                   m_indexMetadata.name.utf8().data());
                return false;
            }
        }
        return true;
    }

    void writeIndexKeys(const IDBBackingStore::RecordIdentifier& recordIdentifier, IDBBackingStore& backingStore, IDBBackingStore::Transaction* transaction, int64_t databaseId, int64_t objectStoreId) const
    {
        int64_t indexId = m_indexMetadata.id;
        for (size_t i = 0; i < m_indexKeys.size(); ++i) {
            backingStore.putIndexDataForRecord(transaction, databaseId, objectStoreId, indexId, *(m_indexKeys)[i].get(), recordIdentifier);
        }
    }

private:

    bool addingKeyAllowed(IDBBackingStore& backingStore, IDBBackingStore::Transaction* transaction,
                          int64_t databaseId, int64_t objectStoreId, int64_t indexId,
                          const IDBKey* indexKey, const IDBKey* primaryKey) const
    {
        if (!m_indexMetadata.unique)
            return true;

        RefPtr<IDBKey> foundPrimaryKey;
        bool found = backingStore.keyExistsInIndex(transaction, databaseId, objectStoreId, indexId, *indexKey, foundPrimaryKey);
        if (!found)
            return true;
        if (primaryKey && foundPrimaryKey->isEqual(primaryKey))
            return true;
        return false;
    }

    const IDBIndexMetadata m_indexMetadata;
    IDBObjectStoreBackendInterface::IndexKeys m_indexKeys;
};
}

static bool makeIndexWriters(PassRefPtr<IDBTransactionBackendImpl> transaction, IDBObjectStoreBackendImpl* objectStore, PassRefPtr<IDBKey> primaryKey, bool keyWasGenerated, const Vector<int64_t>& indexIds, const Vector<IDBObjectStoreBackendInterface::IndexKeys>& indexKeys, Vector<OwnPtr<IndexWriter> >* indexWriters, String* errorMessage)
{
    ASSERT(indexIds.size() == indexKeys.size());

    HashMap<int64_t, IDBObjectStoreBackendInterface::IndexKeys> indexKeyMap;
    for (size_t i = 0; i < indexIds.size(); ++i)
        indexKeyMap.add(indexIds[i], indexKeys[i]);

    for (IDBObjectStoreBackendImpl::IndexMap::iterator it = objectStore->iterIndexesBegin(); it != objectStore->iterIndexesEnd(); ++it) {

        const RefPtr<IDBIndexBackendImpl>& index = it->value;

        IDBObjectStoreBackendInterface::IndexKeys keys = indexKeyMap.get(it->key);
        // If the objectStore is using autoIncrement, then any indexes with an identical keyPath need to also use the primary (generated) key as a key.
        if (keyWasGenerated) {
            const IDBKeyPath& indexKeyPath = index->keyPath();
            if (indexKeyPath == objectStore->keyPath())
                keys.append(primaryKey);
        }

        OwnPtr<IndexWriter> indexWriter(adoptPtr(new IndexWriter(index->metadata(), keys)));
        if (!indexWriter->verifyIndexKeys(*objectStore->backingStore(),
                                          transaction->backingStoreTransaction(),
                                          objectStore->databaseId(),
                                          objectStore->id(),
                                          index->id(), primaryKey.get(), errorMessage)) {
            return false;
        }

        indexWriters->append(indexWriter.release());
    }

    return true;
}

void IDBObjectStoreBackendImpl::setIndexKeys(PassRefPtr<IDBKey> prpPrimaryKey, const Vector<int64_t>& indexIds, const Vector<IndexKeys>& indexKeys, IDBTransactionBackendInterface* transactionPtr)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::setIndexKeys");
    RefPtr<IDBKey> primaryKey = prpPrimaryKey;
    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);

    // FIXME: This method could be asynchronous, but we need to evaluate if it's worth the extra complexity.
    IDBBackingStore::RecordIdentifier recordIdentifier;
    if (!backingStore()->keyExistsInObjectStore(transaction->backingStoreTransaction(), databaseId(), id(), *primaryKey, &recordIdentifier)) {
        transaction->abort();
        return;
    }

    Vector<OwnPtr<IndexWriter> > indexWriters;
    String errorMessage;
    if (!makeIndexWriters(transaction, this, primaryKey, false, indexIds, indexKeys, &indexWriters, &errorMessage)) {
        // FIXME: Need to deal with errorMessage here. makeIndexWriters only fails on uniqueness constraint errors.
        transaction->abort(IDBDatabaseError::create(IDBDatabaseException::CONSTRAINT_ERR, "Duplicate index keys exist in the object store."));
        return;
    }

    for (size_t i = 0; i < indexWriters.size(); ++i) {
        IndexWriter* indexWriter = indexWriters[i].get();
        indexWriter->writeIndexKeys(recordIdentifier, *backingStore(), transaction->backingStoreTransaction(), databaseId(), m_metadata.id);
    }
}

void IDBObjectStoreBackendImpl::setIndexesReady(const Vector<int64_t>& indexIds, IDBTransactionBackendInterface* transactionInterface)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::setIndexesReady");
    RefPtr<IDBObjectStoreBackendImpl> objectStore = this;

    OwnPtr<Vector<int64_t> > newIndexIds = adoptPtr(new Vector<int64_t>(indexIds));
    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionInterface);

    if (!transaction->scheduleTask(
            IDBTransactionBackendInterface::PreemptiveTask,
            createCallbackTask(&IDBObjectStoreBackendImpl::setIndexesReadyInternal, objectStore, newIndexIds.release(), transaction)))
        ASSERT_NOT_REACHED();
}

void IDBObjectStoreBackendImpl::setIndexesReadyInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassOwnPtr<Vector<int64_t> > popIndexIds, PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::setIndexesReadyInternal");
    OwnPtr<Vector<int64_t> > indexIds = popIndexIds;
    for (size_t i = 0; i < indexIds->size(); ++i)
        transaction->didCompletePreemptiveEvent();
    transaction->didCompleteTaskEvents();
}

void IDBObjectStoreBackendImpl::putInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<SerializedScriptValue> prpValue, PassRefPtr<IDBKey> prpKey, PutMode putMode, PassRefPtr<IDBCallbacks> callbacks, PassRefPtr<IDBTransactionBackendImpl> prpTransaction, PassOwnPtr<Vector<int64_t> > popIndexIds, PassOwnPtr<Vector<IndexKeys> > popIndexKeys)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::putInternal");
    RefPtr<IDBTransactionBackendImpl> transaction = prpTransaction;
    ASSERT(transaction->mode() != IDBTransaction::READ_ONLY);
    RefPtr<SerializedScriptValue> value = prpValue;
    RefPtr<IDBKey> key = prpKey;
    OwnPtr<Vector<int64_t> > indexIds = popIndexIds;
    OwnPtr<Vector<IndexKeys> > indexKeys = popIndexKeys;
    ASSERT(indexIds && indexKeys && indexIds->size() == indexKeys->size());
    const bool autoIncrement = objectStore->autoIncrement();
    bool keyWasGenerated = false;

    if (putMode != CursorUpdate && autoIncrement && !key) {
        RefPtr<IDBKey> autoIncKey = objectStore->generateKey(transaction);
        keyWasGenerated = true;
        if (!autoIncKey->isValid()) {
            callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::CONSTRAINT_ERR, "Maximum key generator value reached."));
            return;
        }
        key = autoIncKey;
    }

    ASSERT(key && key->isValid());

    IDBBackingStore::RecordIdentifier recordIdentifier;
    if (putMode == AddOnly && objectStore->backingStore()->keyExistsInObjectStore(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), *key, &recordIdentifier)) {
        callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::CONSTRAINT_ERR, "Key already exists in the object store."));
        return;
    }

    Vector<OwnPtr<IndexWriter> > indexWriters;
    String errorMessage;
    if (!makeIndexWriters(transaction, objectStore.get(), key, keyWasGenerated, *indexIds, *indexKeys, &indexWriters, &errorMessage)) {
        callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::CONSTRAINT_ERR, errorMessage));
        return;
    }

    // Before this point, don't do any mutation.  After this point, rollback the transaction in case of error.

    objectStore->backingStore()->putRecord(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), *key, value->toWireString(), &recordIdentifier);

    for (size_t i = 0; i < indexWriters.size(); ++i) {
        IndexWriter* indexWriter = indexWriters[i].get();
        indexWriter->writeIndexKeys(recordIdentifier, *objectStore->backingStore(), transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->m_metadata.id);
    }

    if (autoIncrement && putMode != CursorUpdate && key->type() == IDBKey::NumberType)
        objectStore->updateKeyGenerator(transaction, key.get(), !keyWasGenerated);

    callbacks->onSuccess(key.release());
}

void IDBObjectStoreBackendImpl::deleteFunction(PassRefPtr<IDBKeyRange> prpKeyRange, PassRefPtr<IDBCallbacks> prpCallbacks, IDBTransactionBackendInterface* transactionPtr, ExceptionCode&)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::delete");

    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);
    ASSERT(transaction->mode() != IDBTransaction::READ_ONLY);

    RefPtr<IDBObjectStoreBackendImpl> objectStore = this;
    RefPtr<IDBKeyRange> keyRange = prpKeyRange;
    RefPtr<IDBCallbacks> callbacks = prpCallbacks;

    if (!transaction->scheduleTask(
            createCallbackTask(&IDBObjectStoreBackendImpl::deleteInternal, objectStore, keyRange, callbacks, transaction)))
        callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::IDB_ABORT_ERR));
}

void IDBObjectStoreBackendImpl::deleteInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBKeyRange> keyRange, PassRefPtr<IDBCallbacks> callbacks, PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::deleteInternal");

    RefPtr<IDBBackingStore::Cursor> backingStoreCursor = objectStore->backingStore()->openObjectStoreCursor(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), keyRange.get(), IDBCursor::NEXT);
    if (backingStoreCursor) {

        do {
            objectStore->backingStore()->deleteRecord(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), backingStoreCursor->recordIdentifier());

        } while (backingStoreCursor->continueFunction(0));
    }

    callbacks->onSuccess();
}

void IDBObjectStoreBackendImpl::clear(PassRefPtr<IDBCallbacks> prpCallbacks, IDBTransactionBackendInterface* transactionPtr, ExceptionCode&)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::clear");

    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);
    ASSERT(transaction->mode() != IDBTransaction::READ_ONLY);

    RefPtr<IDBObjectStoreBackendImpl> objectStore = this;
    RefPtr<IDBCallbacks> callbacks = prpCallbacks;

    if (!transaction->scheduleTask(
            createCallbackTask(&IDBObjectStoreBackendImpl::clearInternal, objectStore, callbacks, transaction)))
        callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::IDB_ABORT_ERR));
}

void IDBObjectStoreBackendImpl::clearInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBCallbacks> callbacks, PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    objectStore->backingStore()->clearObjectStore(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id());
    callbacks->onSuccess();
}

PassRefPtr<IDBIndexBackendInterface> IDBObjectStoreBackendImpl::createIndex(int64_t id, const String& name, const IDBKeyPath& keyPath, bool unique, bool multiEntry, IDBTransactionBackendInterface* transactionPtr, ExceptionCode& ec)
{
    ASSERT_WITH_MESSAGE(!m_indexes.contains(id), "Indexes already contain %s", name.utf8().data());

    RefPtr<IDBIndexBackendImpl> index = IDBIndexBackendImpl::create(m_database, this, IDBIndexMetadata(name, id, keyPath, unique, multiEntry));
    ASSERT(index->name() == name);

    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);
    ASSERT(transaction->mode() == IDBTransaction::VERSION_CHANGE);
    ASSERT(id > m_metadata.maxIndexId);
    m_metadata.maxIndexId = id;

    RefPtr<IDBObjectStoreBackendImpl> objectStore = this;
    if (!transaction->scheduleTask(
              createCallbackTask(&IDBObjectStoreBackendImpl::createIndexInternal, objectStore, index, transaction),
              createCallbackTask(&IDBObjectStoreBackendImpl::removeIndexFromMap, objectStore, index))) {
        ec = IDBDatabaseException::TRANSACTION_INACTIVE_ERR;
        return 0;
    }

    m_indexes.set(id, index);
    return index.release();
}

void IDBObjectStoreBackendImpl::createIndexInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBIndexBackendImpl> index, PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    if (!objectStore->backingStore()->createIndex(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), index->id(), index->name(), index->keyPath(), index->unique(), index->multiEntry())) {
        transaction->abort();
        return;
    }

    transaction->didCompleteTaskEvents();
}

PassRefPtr<IDBIndexBackendInterface> IDBObjectStoreBackendImpl::index(int64_t indexId)
{
    RefPtr<IDBIndexBackendInterface> index = m_indexes.get(indexId);
    ASSERT(index);
    return index.release();
}

void IDBObjectStoreBackendImpl::deleteIndex(int64_t indexId, IDBTransactionBackendInterface* transactionPtr, ExceptionCode& ec)
{
    ASSERT(m_indexes.contains(indexId));

    RefPtr<IDBObjectStoreBackendImpl> objectStore = this;
    RefPtr<IDBIndexBackendImpl> index = m_indexes.get(indexId);
    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);
    ASSERT(transaction->mode() == IDBTransaction::VERSION_CHANGE);

    if (!transaction->scheduleTask(
              createCallbackTask(&IDBObjectStoreBackendImpl::deleteIndexInternal, objectStore, index, transaction),
              createCallbackTask(&IDBObjectStoreBackendImpl::addIndexToMap, objectStore, index))) {
        ec = IDBDatabaseException::TRANSACTION_INACTIVE_ERR;
        return;
    }
    m_indexes.remove(indexId);
}

void IDBObjectStoreBackendImpl::deleteIndexInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBIndexBackendImpl> index, PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    objectStore->backingStore()->deleteIndex(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), index->id());
    transaction->didCompleteTaskEvents();
}

void IDBObjectStoreBackendImpl::openCursor(PassRefPtr<IDBKeyRange> prpRange, IDBCursor::Direction direction, PassRefPtr<IDBCallbacks> prpCallbacks, IDBTransactionBackendInterface::TaskType taskType, IDBTransactionBackendInterface* transactionPtr, ExceptionCode&)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::openCursor");
    RefPtr<IDBObjectStoreBackendImpl> objectStore = this;
    RefPtr<IDBKeyRange> range = prpRange;
    RefPtr<IDBCallbacks> callbacks = prpCallbacks;
    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);
    if (!transaction->scheduleTask(
            createCallbackTask(&IDBObjectStoreBackendImpl::openCursorInternal, objectStore, range, direction, callbacks, taskType, transaction))) {
        callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::IDB_ABORT_ERR));
    }
}

void IDBObjectStoreBackendImpl::openCursorInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBKeyRange> range, IDBCursor::Direction direction, PassRefPtr<IDBCallbacks> callbacks, IDBTransactionBackendInterface::TaskType taskType, PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::openCursorInternal");

    RefPtr<IDBBackingStore::Cursor> backingStoreCursor = objectStore->backingStore()->openObjectStoreCursor(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), range.get(), direction);
    // The frontend has begun indexing, so this pauses the transaction
    // until the indexing is complete. This can't happen any earlier
    // because we don't want to switch to early mode in case multiple
    // indexes are being created in a row, with put()'s in between.
    if (taskType == IDBTransactionBackendInterface::PreemptiveTask)
        transaction->addPreemptiveEvent();
    if (!backingStoreCursor) {
        callbacks->onSuccess(static_cast<SerializedScriptValue*>(0));
        return;
    }

    RefPtr<IDBCursorBackendImpl> cursor = IDBCursorBackendImpl::create(backingStoreCursor.release(), IDBCursorBackendInterface::ObjectStoreCursor, taskType, transaction.get(), objectStore.get());
    callbacks->onSuccess(cursor, cursor->key(), cursor->primaryKey(), cursor->value());
}

void IDBObjectStoreBackendImpl::count(PassRefPtr<IDBKeyRange> range, PassRefPtr<IDBCallbacks> callbacks, IDBTransactionBackendInterface* transactionPtr, ExceptionCode&)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::count");
    RefPtr<IDBTransactionBackendImpl> transaction = IDBTransactionBackendImpl::from(transactionPtr);
    if (!transaction->scheduleTask(
            createCallbackTask(&IDBObjectStoreBackendImpl::countInternal, this, range, callbacks, transaction)))
        callbacks->onError(IDBDatabaseError::create(IDBDatabaseException::IDB_ABORT_ERR));
}

void IDBObjectStoreBackendImpl::countInternal(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBKeyRange> range, PassRefPtr<IDBCallbacks> callbacks, PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    IDB_TRACE("IDBObjectStoreBackendImpl::countInternal");
    uint32_t count = 0;
    RefPtr<IDBBackingStore::Cursor> backingStoreCursor = objectStore->backingStore()->openObjectStoreKeyCursor(transaction->backingStoreTransaction(), objectStore->databaseId(), objectStore->id(), range.get(), IDBCursor::NEXT);
    if (!backingStoreCursor) {
        callbacks->onSuccess(count);
        return;
    }

    do {
        ++count;
    } while (backingStoreCursor->continueFunction(0));

    callbacks->onSuccess(count);
}

void IDBObjectStoreBackendImpl::loadIndexes()
{
    Vector<IDBIndexMetadata> indexes = backingStore()->getIndexes(databaseId(), m_metadata.id);

    for (size_t i = 0; i < indexes.size(); ++i)
        m_indexes.set(indexes[i].id, IDBIndexBackendImpl::create(m_database, this, indexes[i]));
}

void IDBObjectStoreBackendImpl::removeIndexFromMap(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBIndexBackendImpl> index)
{
    ASSERT(objectStore->m_indexes.contains(index->id()));
    objectStore->m_indexes.remove(index->id());
}

void IDBObjectStoreBackendImpl::addIndexToMap(ScriptExecutionContext*, PassRefPtr<IDBObjectStoreBackendImpl> objectStore, PassRefPtr<IDBIndexBackendImpl> index)
{
    RefPtr<IDBIndexBackendImpl> indexPtr = index;
    ASSERT(!objectStore->m_indexes.contains(indexPtr->id()));
    objectStore->m_indexes.set(indexPtr->id(), indexPtr);
}

PassRefPtr<IDBKey> IDBObjectStoreBackendImpl::generateKey(PassRefPtr<IDBTransactionBackendImpl> transaction)
{
    const int64_t maxGeneratorValue = 9007199254740992LL; // Maximum integer storable as ECMAScript number.
    int64_t currentNumber = backingStore()->getKeyGeneratorCurrentNumber(transaction->backingStoreTransaction(), databaseId(), id());
    if (currentNumber < 0 || currentNumber > maxGeneratorValue)
        return IDBKey::createInvalid();

    return IDBKey::createNumber(currentNumber);
}

void IDBObjectStoreBackendImpl::updateKeyGenerator(PassRefPtr<IDBTransactionBackendImpl> transaction, const IDBKey* key, bool checkCurrent)
{
    ASSERT(key && key->type() == IDBKey::NumberType);
    backingStore()->maybeUpdateKeyGeneratorCurrentNumber(transaction->backingStoreTransaction(), databaseId(), id(), static_cast<int64_t>(floor(key->number())) + 1, checkCurrent);
}

} // namespace WebCore

#endif
