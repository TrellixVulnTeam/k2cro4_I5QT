// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/sync_file_system_service.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/stl_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_dependency_manager.h"
#include "chrome/browser/sync_file_system/drive_file_sync_service.h"
#include "chrome/browser/sync_file_system/local_file_sync_service.h"
#include "chrome/browser/sync_file_system/sync_event_observer.h"
#include "content/public/browser/browser_thread.h"
#include "googleurl/src/gurl.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/syncable/sync_file_metadata.h"
#include "webkit/fileapi/syncable/sync_status_code.h"

using content::BrowserThread;
using fileapi::ConflictFileInfoCallback;
using fileapi::FileSystemURL;
using fileapi::SyncFileMetadata;
using fileapi::SyncStatusCallback;
using fileapi::SyncStatusCode;

namespace sync_file_system {

namespace {

// Run the given join_callback when all the callbacks created by this runner
// are run. If any of the callbacks return non-OK state the given join_callback
// will be dispatched with the non-OK state that comes first.
class SharedCallbackRunner
    : public base::RefCountedThreadSafe<SharedCallbackRunner> {
 public:
  explicit SharedCallbackRunner(const SyncStatusCallback& join_callback)
      : join_callback_(join_callback),
        num_shared_callbacks_(0),
        status_(fileapi::SYNC_STATUS_OK) {}

  SyncStatusCallback CreateCallback() {
    ++num_shared_callbacks_;
    return base::Bind(&SharedCallbackRunner::Done, this);
  }

  template <typename R>
  base::Callback<void(SyncStatusCode, const R& in)>
  CreateAssignAndRunCallback(R* out) {
    ++num_shared_callbacks_;
    return base::Bind(&SharedCallbackRunner::AssignAndRun<R>, this, out);
  }

 private:
  virtual ~SharedCallbackRunner() {}
  friend class base::RefCountedThreadSafe<SharedCallbackRunner>;

  template <typename R>
  void AssignAndRun(R* out, SyncStatusCode status, const R& in) {
    DCHECK(out);
    DCHECK_GT(num_shared_callbacks_, 0);
    if (join_callback_.is_null())
      return;
    *out = in;
    Done(status);
  }

  void Done(SyncStatusCode status) {
    if (status != fileapi::SYNC_STATUS_OK &&
        status_ == fileapi::SYNC_STATUS_OK) {
      status_ = status;
    }
    if (--num_shared_callbacks_ > 0)
      return;
    join_callback_.Run(status_);
    join_callback_.Reset();
  }

  SyncStatusCallback join_callback_;
  int num_shared_callbacks_;
  SyncStatusCode status_;
};

void VerifyFileSystemURLSetCallback(
    base::WeakPtr<SyncFileSystemService> service,
    const GURL& app_origin,
    const std::string& service_name,
    const fileapi::SyncFileSetCallback& callback,
    fileapi::SyncStatusCode status,
    const fileapi::FileSystemURLSet& urls) {
  if (!service.get())
    return;

#ifndef NDEBUG
  if (status == fileapi::SYNC_STATUS_OK) {
    for (fileapi::FileSystemURLSet::const_iterator iter = urls.begin();
         iter != urls.end(); ++iter) {
      DCHECK(iter->origin() == app_origin);
      DCHECK(iter->filesystem_id() == service_name);
    }
  }
#endif

  callback.Run(status, urls);
}

SyncEventObserver::SyncServiceState RemoteStateToSyncServiceState(
    RemoteServiceState state) {
  switch (state) {
    case REMOTE_SERVICE_OK:
      return SyncEventObserver::SYNC_SERVICE_RUNNING;
    case REMOTE_SERVICE_TEMPORARY_UNAVAILABLE:
      return SyncEventObserver::SYNC_SERVICE_TEMPORARY_UNAVAILABLE;
    case REMOTE_SERVICE_AUTHENTICATION_REQUIRED:
      return SyncEventObserver::SYNC_SERVICE_AUTHENTICATION_REQUIRED;
    case REMOTE_SERVICE_DISABLED:
      return SyncEventObserver::SYNC_SERVICE_DISABLED;
  }
  NOTREACHED();
  return SyncEventObserver::SYNC_SERVICE_DISABLED;
}

}  // namespace

void SyncFileSystemService::Shutdown() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  local_file_service_->Shutdown();
  local_file_service_.reset();

  remote_file_service_.reset();

  profile_ = NULL;
}

SyncFileSystemService::~SyncFileSystemService() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!profile_);
  STLDeleteContainerPairSecondPointers(observer_map_.begin(),
                                       observer_map_.end());
}

void SyncFileSystemService::InitializeForApp(
    fileapi::FileSystemContext* file_system_context,
    const std::string& service_name,
    const GURL& app_origin,
    const SyncStatusCallback& callback) {
  DCHECK(local_file_service_);
  DCHECK(remote_file_service_);
  DCHECK(app_origin == app_origin.GetOrigin());

  bool inserted = initialized_app_origins_.insert(app_origin).second;
  if (!inserted) {
    callback.Run(fileapi::SYNC_STATUS_OK);
    return;
  }

  if (ContainsKey(observer_map_, app_origin)) {
    FOR_EACH_OBSERVER(
        SyncEventObserver, *observer_map_[app_origin],
        OnSyncStateUpdated(SyncEventObserver::SYNC_SERVICE_INITIALIZING,
                           "Registering the application"));
  }

  scoped_refptr<SharedCallbackRunner> callback_runner(
      new SharedCallbackRunner(callback));

  local_file_service_->MaybeInitializeFileSystemContext(
      app_origin, service_name, file_system_context,
      callback_runner->CreateCallback());
  remote_file_service_->RegisterOriginForTrackingChanges(
      app_origin,
      base::Bind(&SyncFileSystemService::DidRegisterOrigin,
                 AsWeakPtr(), app_origin,
                 callback_runner->CreateCallback()));
}

void SyncFileSystemService::GetConflictFiles(
    const GURL& app_origin,
    const std::string& service_name,
    const fileapi::SyncFileSetCallback& callback) {
  DCHECK(remote_file_service_);
  DCHECK(app_origin == app_origin.GetOrigin());

  // TODO(kinuko): Should we just call Initialize first?
  if (!ContainsKey(initialized_app_origins_, app_origin)) {
    callback.Run(fileapi::SYNC_STATUS_NOT_INITIALIZED,
                 fileapi::FileSystemURLSet());
    return;
  }

  remote_file_service_->GetConflictFiles(
      app_origin, base::Bind(&VerifyFileSystemURLSetCallback,
                             AsWeakPtr(), app_origin, service_name, callback));
}

void SyncFileSystemService::GetConflictFileInfo(
    const GURL& app_origin,
    const std::string& service_name,
    const FileSystemURL& url,
    const ConflictFileInfoCallback& callback) {
  DCHECK(local_file_service_);
  DCHECK(remote_file_service_);
  DCHECK(app_origin == app_origin.GetOrigin());

  // TODO(kinuko): Should we just call Initialize first?
  if (!ContainsKey(initialized_app_origins_, app_origin)) {
    callback.Run(fileapi::SYNC_STATUS_NOT_INITIALIZED,
                 fileapi::ConflictFileInfo());
    return;
  }

  // Call DidGetConflictFileInfo when both remote and local service's
  // GetFileMetadata calls are done.
  SyncFileMetadata* remote_metadata = new SyncFileMetadata;
  SyncFileMetadata* local_metadata = new SyncFileMetadata;
  SyncStatusCallback completion_callback =
      base::Bind(&SyncFileSystemService::DidGetConflictFileInfo,
                 AsWeakPtr(), callback, url,
                 base::Owned(local_metadata),
                 base::Owned(remote_metadata));
  scoped_refptr<SharedCallbackRunner> callback_runner(
      new SharedCallbackRunner(completion_callback));
  local_file_service_->GetLocalFileMetadata(
      url, callback_runner->CreateAssignAndRunCallback(local_metadata));
  remote_file_service_->GetRemoteFileMetadata(
      url, callback_runner->CreateAssignAndRunCallback(remote_metadata));
}

void SyncFileSystemService::AddSyncEventObserver(
    const GURL& app_origin,
    SyncEventObserver* observer) {
  if (!ContainsKey(observer_map_, app_origin))
    observer_map_[app_origin] = new EventObserverList;
  observer_map_[app_origin]->AddObserver(observer);
}

void SyncFileSystemService::RemoveSyncEventObserver(
    const GURL& app_origin,
    SyncEventObserver* observer) {
  if (!ContainsKey(observer_map_, app_origin))
    return;
  observer_map_[app_origin]->RemoveObserver(observer);
}

void SyncFileSystemService::OnLocalChangeAvailable(int64 pending_changes) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_GE(pending_changes, 0);
  pending_local_changes_ = pending_changes;
}

void SyncFileSystemService::OnRemoteChangeAvailable(int64 pending_changes) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_GE(pending_changes, 0);
  pending_remote_changes_ = pending_changes;
}

void SyncFileSystemService::OnRemoteServiceStateUpdated(
    RemoteServiceState state,
    const std::string& description) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  for (ObserverMap::iterator iter = observer_map_.begin();
       iter != observer_map_.end(); ++iter) {
    FOR_EACH_OBSERVER(SyncEventObserver, *iter->second,
                      OnSyncStateUpdated(RemoteStateToSyncServiceState(state),
                                         description));
  }
}

SyncFileSystemService::SyncFileSystemService(Profile* profile)
    : profile_(profile),
      pending_local_changes_(0),
      pending_remote_changes_(0) {}

void SyncFileSystemService::Initialize(
    scoped_ptr<LocalFileSyncService> local_file_service,
    scoped_ptr<RemoteFileSyncService> remote_file_service) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(local_file_service);
  DCHECK(remote_file_service);
  DCHECK(profile_);

  local_file_service_ = local_file_service.Pass();
  remote_file_service_ = remote_file_service.Pass();

  remote_file_service_->AddObserver(this);
}

void SyncFileSystemService::DidGetConflictFileInfo(
    const ConflictFileInfoCallback& callback,
    const FileSystemURL& url,
    const SyncFileMetadata* local_metadata,
    const SyncFileMetadata* remote_metadata,
    SyncStatusCode status) {
  DCHECK(local_metadata);
  DCHECK(remote_metadata);
  fileapi::ConflictFileInfo info;
  info.url = url;
  info.local_metadata = *local_metadata;
  info.remote_metadata = *remote_metadata;
  callback.Run(status, info);
}

void SyncFileSystemService::DidRegisterOrigin(
    const GURL& app_origin,
    const fileapi::SyncStatusCallback& callback,
    fileapi::SyncStatusCode status) {
  if (status == fileapi::SYNC_STATUS_AUTHENTICATION_FAILED ||
      status == fileapi::SYNC_STATUS_RETRY ||
      status == fileapi::SYNC_STATUS_NETWORK_ERROR) {
    // We're having temporary network errors or authentication errors.
    // We're not yet sure if they're resolvable, but queue them up so that
    // we can retry.
    pending_register_origins_.insert(app_origin);
    status = fileapi::SYNC_STATUS_OK;
  }
  callback.Run(status);
}

// SyncFileSystemServiceFactory -----------------------------------------------

// static
SyncFileSystemService* SyncFileSystemServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<SyncFileSystemService*>(
      GetInstance()->GetServiceForProfile(profile, true));
}

// static
SyncFileSystemServiceFactory* SyncFileSystemServiceFactory::GetInstance() {
  return Singleton<SyncFileSystemServiceFactory>::get();
}

void SyncFileSystemServiceFactory::set_mock_remote_file_service(
    scoped_ptr<RemoteFileSyncService> mock_remote_service) {
  mock_remote_file_service_ = mock_remote_service.Pass();
}

SyncFileSystemServiceFactory::SyncFileSystemServiceFactory()
    : ProfileKeyedServiceFactory("SyncFileSystemService",
                                 ProfileDependencyManager::GetInstance()) {
}

SyncFileSystemServiceFactory::~SyncFileSystemServiceFactory() {}

ProfileKeyedService* SyncFileSystemServiceFactory::BuildServiceInstanceFor(
    Profile* profile) const {
  SyncFileSystemService* service = new SyncFileSystemService(profile);

  scoped_ptr<LocalFileSyncService> local_file_service(
      new LocalFileSyncService);

  scoped_ptr<RemoteFileSyncService> remote_file_service;
  if (mock_remote_file_service_)
    remote_file_service = mock_remote_file_service_.Pass();
  else
    remote_file_service.reset(new DriveFileSyncService(profile));

  service->Initialize(local_file_service.Pass(),
                      remote_file_service.Pass());
  return service;
}

}  // namespace sync_file_system
