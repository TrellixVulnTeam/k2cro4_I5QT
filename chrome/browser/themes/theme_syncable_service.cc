// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/themes/theme_syncable_service.h"

#include "base/stringprintf.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/common/extensions/extension.h"
#include "sync/protocol/sync.pb.h"
#include "sync/protocol/theme_specifics.pb.h"

using std::string;

namespace {

bool IsTheme(const extensions::Extension& extension) {
  return extension.is_theme();
}

// TODO(akalin): Remove this.
bool IsSystemThemeDistinctFromDefaultTheme() {
#if defined(TOOLKIT_GTK)
  return true;
#else
  return false;
#endif
}

}  // namespace

const char ThemeSyncableService::kCurrentThemeClientTag[] = "current_theme";
const char ThemeSyncableService::kCurrentThemeNodeTitle[] = "Current Theme";

ThemeSyncableService::ThemeSyncableService(Profile* profile,
                                           ThemeService* theme_service)
    : profile_(profile),
      theme_service_(theme_service),
      use_system_theme_by_default_(false) {
  DCHECK(profile_);
  DCHECK(theme_service_);
}

ThemeSyncableService::~ThemeSyncableService() {
}

void ThemeSyncableService::OnThemeChange() {
  if (sync_processor_.get()) {
    sync_pb::ThemeSpecifics current_specifics;
    GetThemeSpecificsFromCurrentTheme(&current_specifics);
    ProcessNewTheme(syncer::SyncChange::ACTION_UPDATE, current_specifics);
    use_system_theme_by_default_ =
        current_specifics.use_system_theme_by_default();
  }
}

syncer::SyncMergeResult ThemeSyncableService::MergeDataAndStartSyncing(
    syncer::ModelType type,
    const syncer::SyncDataList& initial_sync_data,
    scoped_ptr<syncer::SyncChangeProcessor> sync_processor,
    scoped_ptr<syncer::SyncErrorFactory> error_handler) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!sync_processor_.get());
  DCHECK(sync_processor.get());
  DCHECK(error_handler.get());

  syncer::SyncMergeResult merge_result(type);
  sync_processor_ = sync_processor.Pass();
  sync_error_handler_ = error_handler.Pass();

  if (initial_sync_data.size() > 1) {
    sync_error_handler_->CreateAndUploadError(
        FROM_HERE,
        StringPrintf("Received %d theme specifics.",
                     static_cast<int>(initial_sync_data.size())));
  }

  sync_pb::ThemeSpecifics current_specifics;
  GetThemeSpecificsFromCurrentTheme(&current_specifics);

  // Find the last SyncData that has theme data and set the current theme from
  // it.
  for (syncer::SyncDataList::const_reverse_iterator sync_data =
      initial_sync_data.rbegin(); sync_data != initial_sync_data.rend();
      ++sync_data) {
    if (sync_data->GetSpecifics().has_theme()) {
      MaybeSetTheme(current_specifics, *sync_data);
      return merge_result;
    }
  }

  // No theme specifics are found. Create one according to current theme.
  merge_result.set_error(ProcessNewTheme(
      syncer::SyncChange::ACTION_ADD, current_specifics));
  return merge_result;
}

void ThemeSyncableService::StopSyncing(syncer::ModelType type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(type, syncer::THEMES);

  sync_processor_.reset();
  sync_error_handler_.reset();
}

syncer::SyncDataList ThemeSyncableService::GetAllSyncData(
    syncer::ModelType type) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(type, syncer::THEMES);

  syncer::SyncDataList list;
  sync_pb::EntitySpecifics entity_specifics;
  GetThemeSpecificsFromCurrentTheme(entity_specifics.mutable_theme());
  list.push_back(syncer::SyncData::CreateLocalData(kCurrentThemeClientTag,
                                                   kCurrentThemeNodeTitle,
                                                   entity_specifics));
  return list;
}

syncer::SyncError ThemeSyncableService::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const syncer::SyncChangeList& change_list) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!sync_processor_.get()) {
    return syncer::SyncError(FROM_HERE,
                             "Theme syncable service is not started.",
                             syncer::THEMES);
  }

  // TODO(akalin): Normally, we should only have a single change and
  // it should be an update.  However, the syncapi may occasionally
  // generates multiple changes.  When we fix syncapi to not do that,
  // we can remove the extra logic below.  See:
  // http://code.google.com/p/chromium/issues/detail?id=41696 .
  if (change_list.size() != 1) {
    string err_msg = base::StringPrintf("Received %d theme changes: ",
                                        static_cast<int>(change_list.size()));
    for (size_t i = 0; i < change_list.size(); ++i) {
      base::StringAppendF(&err_msg, "[%s] ", change_list[i].ToString().c_str());
    }
    sync_error_handler_->CreateAndUploadError(FROM_HERE, err_msg);
  } else if (change_list.begin()->change_type() !=
          syncer::SyncChange::ACTION_ADD
      && change_list.begin()->change_type() !=
          syncer::SyncChange::ACTION_UPDATE) {
    sync_error_handler_->CreateAndUploadError(
        FROM_HERE,
        "Invalid theme change: " + change_list.begin()->ToString());
  }

  sync_pb::ThemeSpecifics current_specifics;
  GetThemeSpecificsFromCurrentTheme(&current_specifics);

  // Set current theme from the theme specifics of the last change of type
  // |ACTION_ADD| or |ACTION_UPDATE|.
  for (syncer::SyncChangeList::const_reverse_iterator theme_change =
      change_list.rbegin(); theme_change != change_list.rend();
      ++theme_change) {
    if (theme_change->sync_data().GetSpecifics().has_theme() &&
        (theme_change->change_type() == syncer::SyncChange::ACTION_ADD ||
            theme_change->change_type() == syncer::SyncChange::ACTION_UPDATE)) {
      MaybeSetTheme(current_specifics, theme_change->sync_data());
      return syncer::SyncError();
    }
  }

  return syncer::SyncError(FROM_HERE,
                           base::StringPrintf(
                               "Didn't find valid theme specifics."),
                           syncer::THEMES);
}

void ThemeSyncableService::MaybeSetTheme(
    const sync_pb::ThemeSpecifics& current_specs,
    const syncer::SyncData& sync_data) {
  const sync_pb::ThemeSpecifics& sync_theme = sync_data.GetSpecifics().theme();
  use_system_theme_by_default_ = sync_theme.use_system_theme_by_default();
  DVLOG(1) << "Set current theme from specifics: " << sync_data.ToString();
  if (!AreThemeSpecificsEqual(current_specs, sync_theme,
                              IsSystemThemeDistinctFromDefaultTheme())) {
    SetCurrentThemeFromThemeSpecifics(sync_theme);
  } else {
    DVLOG(1) << "Skip setting theme because specs are equal";
  }
}

void ThemeSyncableService::SetCurrentThemeFromThemeSpecifics(
    const sync_pb::ThemeSpecifics& theme_specifics) {
  if (theme_specifics.use_custom_theme()) {
    // TODO(akalin): Figure out what to do about third-party themes
    // (i.e., those not on either Google gallery).
    string id(theme_specifics.custom_theme_id());
    GURL update_url(theme_specifics.custom_theme_update_url());
    DVLOG(1) << "Applying theme " << id << " with update_url " << update_url;
    ExtensionServiceInterface* extensions_service =
        profile_->GetExtensionService();
    CHECK(extensions_service);
    const extensions::Extension* extension =
        extensions_service->GetExtensionById(id, true);
    if (extension) {
      if (!extension->is_theme()) {
        DVLOG(1) << "Extension " << id << " is not a theme; aborting";
        return;
      }
      if (!extensions_service->IsExtensionEnabled(id)) {
        DVLOG(1) << "Theme " << id << " is not enabled; aborting";
        return;
      }
      // An enabled theme extension with the given id was found, so
      // just set the current theme to it.
      theme_service_->SetTheme(extension);
    } else {
      // No extension with this id exists -- we must install it; we do
      // so by adding it as a pending extension and then triggering an
      // auto-update cycle.
      const bool kInstallSilently = true;
      if (!extensions_service->pending_extension_manager()->AddFromSync(
              id, update_url, &IsTheme, kInstallSilently)) {
        LOG(WARNING) << "Could not add pending extension for " << id;
        return;
      }
      extensions_service->CheckForUpdatesSoon();
    }
  } else if (theme_specifics.use_system_theme_by_default()) {
    DVLOG(1) << "Switch to use native theme";
    theme_service_->SetNativeTheme();
  } else {
    DVLOG(1) << "Switch to use default theme";
    theme_service_->UseDefaultTheme();
  }
}

void ThemeSyncableService::GetThemeSpecificsFromCurrentTheme(
    sync_pb::ThemeSpecifics* theme_specifics) const {
  const extensions::Extension* current_theme =
      theme_service_->UsingDefaultTheme() ?
          NULL :
          profile_->GetExtensionService()->GetExtensionById(
              theme_service_->GetThemeID(), false);
  bool use_custom_theme = (current_theme != NULL);
  theme_specifics->set_use_custom_theme(use_custom_theme);
  if (IsSystemThemeDistinctFromDefaultTheme()) {
    // On platform where system theme is different from default theme, set
    // use_system_theme_by_default to true if system theme is used, false
    // if default system theme is used. Otherwise restore it to value in sync.
    if (theme_service_->UsingNativeTheme()) {
      theme_specifics->set_use_system_theme_by_default(true);
    } else if (theme_service_->UsingDefaultTheme()) {
      theme_specifics->set_use_system_theme_by_default(false);
    } else {
      theme_specifics->set_use_system_theme_by_default(
          use_system_theme_by_default_);
    }
  } else {
    // Restore use_system_theme_by_default when platform doesn't distinguish
    // between default theme and system theme.
    theme_specifics->set_use_system_theme_by_default(
        use_system_theme_by_default_);
  }

  if (use_custom_theme) {
    DCHECK(current_theme);
    DCHECK(current_theme->is_theme());
    theme_specifics->set_custom_theme_name(current_theme->name());
    theme_specifics->set_custom_theme_id(current_theme->id());
    theme_specifics->set_custom_theme_update_url(
        current_theme->update_url().spec());
  } else {
    DCHECK(!current_theme);
    theme_specifics->clear_custom_theme_name();
    theme_specifics->clear_custom_theme_id();
    theme_specifics->clear_custom_theme_update_url();
  }
}

/* static */
bool ThemeSyncableService::AreThemeSpecificsEqual(
    const sync_pb::ThemeSpecifics& a,
    const sync_pb::ThemeSpecifics& b,
    bool is_system_theme_distinct_from_default_theme) {
  if (a.use_custom_theme() != b.use_custom_theme()) {
    return false;
  }

  if (a.use_custom_theme()) {
    // We're using a custom theme, so simply compare IDs since those
    // are guaranteed unique.
    return a.custom_theme_id() == b.custom_theme_id();
  } else if (is_system_theme_distinct_from_default_theme) {
    // We're not using a custom theme, but we care about system
    // vs. default.
    return a.use_system_theme_by_default() == b.use_system_theme_by_default();
  } else {
    // We're not using a custom theme, and we don't care about system
    // vs. default.
    return true;
  }
}

syncer::SyncError ThemeSyncableService::ProcessNewTheme(
    syncer::SyncChange::SyncChangeType change_type,
    const sync_pb::ThemeSpecifics& theme_specifics) {
  syncer::SyncChangeList changes;
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.mutable_theme()->CopyFrom(theme_specifics);

  changes.push_back(
      syncer::SyncChange(FROM_HERE, change_type,
                         syncer::SyncData::CreateLocalData(
                             kCurrentThemeClientTag, kCurrentThemeNodeTitle,
                             entity_specifics)));

  DVLOG(1) << "Update theme specifics from current theme: "
      << changes.back().ToString();

  return sync_processor_->ProcessSyncChanges(FROM_HERE, changes);
}
