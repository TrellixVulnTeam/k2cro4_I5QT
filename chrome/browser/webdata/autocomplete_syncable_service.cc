// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/webdata/autocomplete_syncable_service.h"

#include "base/location.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/webdata/autofill_table.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/browser/webdata/web_database.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_version_info.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "net/base/escape.h"
#include "sync/api/sync_error.h"
#include "sync/api/sync_error_factory.h"
#include "sync/protocol/autofill_specifics.pb.h"
#include "sync/protocol/sync.pb.h"

using content::BrowserThread;

namespace {

const char kAutofillEntryNamespaceTag[] = "autofill_entry|";

// Merges timestamps from the |autofill| entry and |timestamps|. Returns
// true if they were different, false if they were the same.
// All of the timestamp vectors are assummed to be sorted, resulting vector is
// sorted as well. Only two timestamps - the earliest and the latest are stored.
bool MergeTimestamps(const sync_pb::AutofillSpecifics& autofill,
                     const std::vector<base::Time>& timestamps,
                     std::vector<base::Time>* new_timestamps) {
  DCHECK(new_timestamps);

  new_timestamps->clear();
  size_t timestamps_count = autofill.usage_timestamp_size();
  if (timestamps_count == 0 && timestamps.empty()) {
    return false;
  } else if (timestamps_count == 0) {
    new_timestamps->insert(new_timestamps->begin(),
                           timestamps.begin(),
                           timestamps.end());
    return true;
  } else if (timestamps.empty()) {
    new_timestamps->reserve(2);
    new_timestamps->push_back(base::Time::FromInternalValue(
        autofill.usage_timestamp(0)));
    if (timestamps_count > 1) {
      new_timestamps->push_back(base::Time::FromInternalValue(
          autofill.usage_timestamp(timestamps_count - 1)));
    }
    return true;
  } else {
    base::Time sync_time_begin = base::Time::FromInternalValue(
        autofill.usage_timestamp(0));
    base::Time sync_time_end = base::Time::FromInternalValue(
        autofill.usage_timestamp(timestamps_count - 1));
    if (timestamps.front() != sync_time_begin ||
        timestamps.back() != sync_time_end) {
      new_timestamps->push_back(
          timestamps.front() < sync_time_begin ? timestamps.front() :
                                                 sync_time_begin);
      if (new_timestamps->back() != timestamps.back() ||
          new_timestamps->back() != sync_time_end) {
        new_timestamps->push_back(
            timestamps.back() > sync_time_end ? timestamps.back() :
                                                sync_time_end);
      }
      return true;
    } else {
      new_timestamps->insert(new_timestamps->begin(),
                             timestamps.begin(),
                             timestamps.end());
      return false;
    }
  }
}

bool ShouldCullSyncedData() {
  chrome::VersionInfo::Channel channel = chrome::VersionInfo::GetChannel();

  // To set probability to 10% - set it to 0.1, 5% to 0.05, etc.
  double culling_probability = 0.0;
  if (channel == chrome::VersionInfo::CHANNEL_CANARY)
    culling_probability = 1.0;
  else if (channel == chrome::VersionInfo::CHANNEL_DEV)
    culling_probability = 0.2;

  return (base::RandDouble() < culling_probability);
}

}  // namespace

AutocompleteSyncableService::AutocompleteSyncableService(
    WebDataService* web_data_service)
    : web_data_service_(web_data_service) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  DCHECK(web_data_service_);
  notification_registrar_.Add(
      this, chrome::NOTIFICATION_AUTOFILL_ENTRIES_CHANGED,
      content::Source<WebDataService>(web_data_service));
}

AutocompleteSyncableService::~AutocompleteSyncableService() {
  DCHECK(CalledOnValidThread());
}

AutocompleteSyncableService::AutocompleteSyncableService()
    : web_data_service_(NULL) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
}

syncer::SyncMergeResult AutocompleteSyncableService::MergeDataAndStartSyncing(
    syncer::ModelType type,
    const syncer::SyncDataList& initial_sync_data,
    scoped_ptr<syncer::SyncChangeProcessor> sync_processor,
    scoped_ptr<syncer::SyncErrorFactory> error_handler) {
  DCHECK(CalledOnValidThread());
  DCHECK(!sync_processor_.get());
  DCHECK(sync_processor.get());
  DCHECK(error_handler.get());
  VLOG(1) << "Associating Autocomplete: MergeDataAndStartSyncing";

  syncer::SyncMergeResult merge_result(type);
  error_handler_ = error_handler.Pass();
  std::vector<AutofillEntry> entries;
  if (!LoadAutofillData(&entries)) {
    merge_result.set_error(error_handler_->CreateAndUploadError(
        FROM_HERE,
        "Could not get the autocomplete data from WebDatabase."));
    return merge_result;
  }

  AutocompleteEntryMap new_db_entries;
  for (std::vector<AutofillEntry>::iterator it = entries.begin();
       it != entries.end(); ++it) {
    new_db_entries[it->key()] =
        std::make_pair(syncer::SyncChange::ACTION_ADD, it);
  }

  sync_processor_ = sync_processor.Pass();

  std::vector<AutofillEntry> new_synced_entries;
  // Go through and check for all the entries that sync already knows about.
  // CreateOrUpdateEntry() will remove entries that are same with the synced
  // ones from |new_db_entries|.
  for (syncer::SyncDataList::const_iterator sync_iter =
           initial_sync_data.begin();
       sync_iter != initial_sync_data.end(); ++sync_iter) {
    CreateOrUpdateEntry(*sync_iter, &new_db_entries, &new_synced_entries);
  }

  if (!SaveChangesToWebData(new_synced_entries)) {
    merge_result.set_error(error_handler_->CreateAndUploadError(
        FROM_HERE,
        "Failed to update webdata."));
    return merge_result;
  }

  WebDataService::NotifyOfMultipleAutofillChanges(web_data_service_);

  syncer::SyncChangeList new_changes;
  for (AutocompleteEntryMap::iterator i = new_db_entries.begin();
       i != new_db_entries.end(); ++i) {
    new_changes.push_back(
        syncer::SyncChange(FROM_HERE,
                           i->second.first,
                           CreateSyncData(*(i->second.second))));
  }

  if (ShouldCullSyncedData()) {
    // This will schedule a deletion operation on the DB thread, which will
    // trigger a notification to propagate the deletion to Sync.
    web_data_service_->RemoveExpiredFormElements();
  }

  merge_result.set_error(
      sync_processor_->ProcessSyncChanges(FROM_HERE, new_changes));
  return merge_result;
}

void AutocompleteSyncableService::StopSyncing(syncer::ModelType type) {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(syncer::AUTOFILL, type);

  sync_processor_.reset(NULL);
  error_handler_.reset();
}

syncer::SyncDataList AutocompleteSyncableService::GetAllSyncData(
    syncer::ModelType type) const {
  DCHECK(CalledOnValidThread());
  DCHECK(sync_processor_.get());
  DCHECK_EQ(type, syncer::AUTOFILL);

  syncer::SyncDataList current_data;

  std::vector<AutofillEntry> entries;
  if (!LoadAutofillData(&entries))
    return current_data;

  for (std::vector<AutofillEntry>::iterator it = entries.begin();
       it != entries.end(); ++it) {
    current_data.push_back(CreateSyncData(*it));
  }

  return current_data;
}

syncer::SyncError AutocompleteSyncableService::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const syncer::SyncChangeList& change_list) {
  DCHECK(CalledOnValidThread());
  DCHECK(sync_processor_.get());

  if (!sync_processor_.get()) {
    syncer::SyncError error(FROM_HERE, "Models not yet associated.",
                    syncer::AUTOFILL);
    return error;
  }

  // Data is loaded only if we get new ADD/UPDATE change.
  std::vector<AutofillEntry> entries;
  scoped_ptr<AutocompleteEntryMap> db_entries;
  std::vector<AutofillEntry> new_entries;

  syncer::SyncError list_processing_error;

  for (syncer::SyncChangeList::const_iterator i = change_list.begin();
       i != change_list.end() && !list_processing_error.IsSet(); ++i) {
    DCHECK(i->IsValid());
    switch (i->change_type()) {
      case syncer::SyncChange::ACTION_ADD:
      case syncer::SyncChange::ACTION_UPDATE:
        if (!db_entries.get()) {
          if (!LoadAutofillData(&entries)) {
            return error_handler_->CreateAndUploadError(
                FROM_HERE,
                "Could not get the autocomplete data from WebDatabase.");
          }
          db_entries.reset(new AutocompleteEntryMap);
          for (std::vector<AutofillEntry>::iterator it = entries.begin();
               it != entries.end(); ++it) {
            (*db_entries)[it->key()] =
                std::make_pair(syncer::SyncChange::ACTION_ADD, it);
          }
        }
        CreateOrUpdateEntry(i->sync_data(), db_entries.get(), &new_entries);
        break;
      case syncer::SyncChange::ACTION_DELETE: {
        DCHECK(i->sync_data().GetSpecifics().has_autofill())
            << "Autofill specifics data not present on delete!";
        const sync_pb::AutofillSpecifics& autofill =
            i->sync_data().GetSpecifics().autofill();
        if (autofill.has_value()) {
          list_processing_error = AutofillEntryDelete(autofill);
        } else {
          DLOG(WARNING)
              << "Delete for old-style autofill profile being dropped!";
        }
      } break;
      default:
        NOTREACHED() << "Unexpected sync change state.";
        return error_handler_->CreateAndUploadError(
            FROM_HERE,
            "ProcessSyncChanges failed on ChangeType " +
                 syncer::SyncChange::ChangeTypeToString(i->change_type()));
    }
  }

  if (!SaveChangesToWebData(new_entries)) {
    return error_handler_->CreateAndUploadError(
        FROM_HERE,
        "Failed to update webdata.");
  }

  WebDataService::NotifyOfMultipleAutofillChanges(web_data_service_);

  if (ShouldCullSyncedData()) {
    // This will schedule a deletion operation on the DB thread, which will
    // trigger a notification to propagate the deletion to Sync.
    web_data_service_->RemoveExpiredFormElements();
  }

  return list_processing_error;
}

void AutocompleteSyncableService::Observe(int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_AUTOFILL_ENTRIES_CHANGED, type);

  // Check if sync is on. If we receive notification prior to the sync being set
  // up we are going to process all when MergeData..() is called. If we receive
  // notification after the sync exited, it will be sinced next time Chrome
  // starts.
  if (!sync_processor_.get())
    return;
  WebDataService* wds = content::Source<WebDataService>(source).ptr();

  DCHECK_EQ(web_data_service_, wds);

  AutofillChangeList* changes =
      content::Details<AutofillChangeList>(details).ptr();
  ActOnChanges(*changes);
}

bool AutocompleteSyncableService::LoadAutofillData(
    std::vector<AutofillEntry>* entries) const {
  return web_data_service_->GetDatabase()->
             GetAutofillTable()->GetAllAutofillEntries(entries);
}

bool AutocompleteSyncableService::SaveChangesToWebData(
    const std::vector<AutofillEntry>& new_entries) {
  DCHECK(CalledOnValidThread());

  if (!new_entries.empty() &&
      !web_data_service_->GetDatabase()->
          GetAutofillTable()->UpdateAutofillEntries(new_entries)) {
    return false;
  }
  return true;
}

// Creates or updates an autocomplete entry based on |data|.
void AutocompleteSyncableService::CreateOrUpdateEntry(
    const syncer::SyncData& data,
    AutocompleteEntryMap* loaded_data,
    std::vector<AutofillEntry>* new_entries) {
  const sync_pb::EntitySpecifics& specifics = data.GetSpecifics();
  const sync_pb::AutofillSpecifics& autofill_specifics(
      specifics.autofill());

  if (!autofill_specifics.has_value()) {
    DLOG(WARNING)
        << "Add/Update for old-style autofill profile being dropped!";
    return;
  }

  AutofillKey key(autofill_specifics.name().c_str(),
                  autofill_specifics.value().c_str());
  AutocompleteEntryMap::iterator it = loaded_data->find(key);
  if (it == loaded_data->end()) {
    // New entry.
    std::vector<base::Time> timestamps;
    size_t timestamps_count = autofill_specifics.usage_timestamp_size();
    timestamps.reserve(2);
    if (timestamps_count) {
      timestamps.push_back(base::Time::FromInternalValue(
          autofill_specifics.usage_timestamp(0)));
    }
    if (timestamps_count > 1) {
      timestamps.push_back(base::Time::FromInternalValue(
          autofill_specifics.usage_timestamp(timestamps_count - 1)));
    }
    AutofillEntry new_entry(key, timestamps);
    new_entries->push_back(new_entry);
  } else {
    // Entry already present - merge if necessary.
    std::vector<base::Time> timestamps;
    bool different = MergeTimestamps(
        autofill_specifics, it->second.second->timestamps(), &timestamps);
    if (different) {
      AutofillEntry new_entry(it->second.second->key(), timestamps);
      new_entries->push_back(new_entry);
      // Update the sync db if the list of timestamps have changed.
      *(it->second.second) = new_entry;
      it->second.first = syncer::SyncChange::ACTION_UPDATE;
    } else {
      loaded_data->erase(it);
    }
  }
}

// static
void AutocompleteSyncableService::WriteAutofillEntry(
    const AutofillEntry& entry, sync_pb::EntitySpecifics* autofill_specifics) {
  sync_pb::AutofillSpecifics* autofill =
      autofill_specifics->mutable_autofill();
  autofill->set_name(UTF16ToUTF8(entry.key().name()));
  autofill->set_value(UTF16ToUTF8(entry.key().value()));
  const std::vector<base::Time>& ts(entry.timestamps());
  for (std::vector<base::Time>::const_iterator timestamp = ts.begin();
       timestamp != ts.end(); ++timestamp) {
    autofill->add_usage_timestamp(timestamp->ToInternalValue());
  }
}

syncer::SyncError AutocompleteSyncableService::AutofillEntryDelete(
    const sync_pb::AutofillSpecifics& autofill) {
  if (!web_data_service_->GetDatabase()->GetAutofillTable()->RemoveFormElement(
          UTF8ToUTF16(autofill.name()), UTF8ToUTF16(autofill.value()))) {
    return error_handler_->CreateAndUploadError(
        FROM_HERE,
        "Could not remove autocomplete entry from WebDatabase.");
  }
  return syncer::SyncError();
}

void AutocompleteSyncableService::ActOnChanges(
     const AutofillChangeList& changes) {
  DCHECK(sync_processor_.get());
  syncer::SyncChangeList new_changes;
  for (AutofillChangeList::const_iterator change = changes.begin();
       change != changes.end(); ++change) {
    switch (change->type()) {
      case AutofillChange::ADD:
      case AutofillChange::UPDATE: {
        std::vector<base::Time> timestamps;
        if (!web_data_service_->GetDatabase()->
                GetAutofillTable()->GetAutofillTimestamps(
                    change->key().name(),
                    change->key().value(),
                    &timestamps)) {
          NOTREACHED();
          return;
        }
        AutofillEntry entry(change->key(), timestamps);
        syncer::SyncChange::SyncChangeType change_type =
           (change->type() == AutofillChange::ADD) ?
            syncer::SyncChange::ACTION_ADD :
            syncer::SyncChange::ACTION_UPDATE;
        new_changes.push_back(syncer::SyncChange(FROM_HERE,
                                                 change_type,
                                                 CreateSyncData(entry)));
        break;
      }
      case AutofillChange::REMOVE: {
        std::vector<base::Time> timestamps;
        AutofillEntry entry(change->key(), timestamps);
        new_changes.push_back(
            syncer::SyncChange(FROM_HERE,
                               syncer::SyncChange::ACTION_DELETE,
                               CreateSyncData(entry)));
        break;
      }
      default:
        NOTREACHED();
        break;
    }
  }
  syncer::SyncError error =
      sync_processor_->ProcessSyncChanges(FROM_HERE, new_changes);
  if (error.IsSet()) {
    DLOG(WARNING) << "[AUTOCOMPLETE SYNC]"
                  << " Failed processing change:"
                  << " Error:" << error.message();
  }
}

syncer::SyncData AutocompleteSyncableService::CreateSyncData(
    const AutofillEntry& entry) const {
  sync_pb::EntitySpecifics autofill_specifics;
  WriteAutofillEntry(entry, &autofill_specifics);
  std::string tag(KeyToTag(UTF16ToUTF8(entry.key().name()),
                           UTF16ToUTF8(entry.key().value())));
  return syncer::SyncData::CreateLocalData(tag, tag, autofill_specifics);
}

// static
std::string AutocompleteSyncableService::KeyToTag(const std::string& name,
                                                  const std::string& value) {
  std::string ns(kAutofillEntryNamespaceTag);
  return ns + net::EscapePath(name) + "|" + net::EscapePath(value);
}
