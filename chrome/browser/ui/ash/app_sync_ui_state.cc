// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/app_sync_ui_state.h"

#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/pending_extension_manager.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/ash/app_sync_ui_state_factory.h"
#include "chrome/browser/ui/ash/app_sync_ui_state_observer.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user_manager.h"
#endif

namespace {

// Max loading animation time in milliseconds.
const int kMaxSyncingTimeMs = 60 * 1000;

}  // namespace

// static
AppSyncUIState* AppSyncUIState::Get(Profile* profile) {
  return AppSyncUIStateFactory::GetForProfile(profile);
}

// static
bool AppSyncUIState::ShouldObserveAppSyncForProfile(Profile* profile) {
#if defined(OS_CHROMEOS)
  if (chromeos::UserManager::Get()->IsLoggedInAsGuest())
    return false;

  if (!profile || profile->IsOffTheRecord())
    return false;

  if (!ProfileSyncServiceFactory::HasProfileSyncService(profile))
    return false;

  const bool is_new_profile = profile->GetPrefs()->GetInitializationStatus() ==
      PrefService::INITIALIZATION_STATUS_CREATED_NEW_PROFILE;
  return is_new_profile;
#else
  return false;
#endif
}

AppSyncUIState::AppSyncUIState(Profile* profile)
    : profile_(profile),
      sync_service_(NULL),
      status_(STATUS_NORMAL) {
  StartObserving();
}

AppSyncUIState::~AppSyncUIState() {
  StopObserving();
}

void AppSyncUIState::AddObserver(AppSyncUIStateObserver* observer) {
  observers_.AddObserver(observer);
}

void AppSyncUIState::RemoveObserver(AppSyncUIStateObserver* observer) {
  observers_.RemoveObserver(observer);
}

void AppSyncUIState::StartObserving() {
  DCHECK(ShouldObserveAppSyncForProfile(profile_));
  DCHECK(!sync_service_);

  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSION_LOADED,
                 content::Source<Profile>(profile_));

  sync_service_ = ProfileSyncServiceFactory::GetForProfile(profile_);
  CHECK(sync_service_);
  sync_service_->AddObserver(this);
}

void AppSyncUIState::StopObserving() {
  if (!sync_service_)
    return;

  registrar_.RemoveAll();
  sync_service_->RemoveObserver(this);
  sync_service_ = NULL;
  profile_ = NULL;
}

void AppSyncUIState::SetStatus(Status status) {
  if (status_ == status)
    return;

  status_ = status;
  switch (status_) {
    case STATUS_SYNCING:
      max_syncing_status_timer_.Start(
          FROM_HERE,
          base::TimeDelta::FromMilliseconds(kMaxSyncingTimeMs),
          this, &AppSyncUIState::OnMaxSyncingTimer);
      break;
    case STATUS_NORMAL:
    case STATUS_TIMED_OUT:
      max_syncing_status_timer_.Stop();
      StopObserving();
      break;
  }

  FOR_EACH_OBSERVER(AppSyncUIStateObserver,
                    observers_,
                    OnAppSyncUIStatusChanged());
}

void AppSyncUIState::CheckAppSync() {
  if (!sync_service_ || !sync_service_->HasSyncSetupCompleted())
    return;

  const bool synced = sync_service_->ShouldPushChanges();
  const bool has_pending_extension = profile_->GetExtensionService()->
      pending_extension_manager()->HasPendingExtensionFromSync();

  if (synced && !has_pending_extension)
    SetStatus(STATUS_NORMAL);
  else
    SetStatus(STATUS_SYNCING);
}

void AppSyncUIState::OnMaxSyncingTimer() {
  SetStatus(STATUS_TIMED_OUT);
}

void AppSyncUIState::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_EXTENSION_LOADED, type);
  CheckAppSync();
}

void AppSyncUIState::OnStateChanged() {
  DCHECK(sync_service_);
  CheckAppSync();
}
