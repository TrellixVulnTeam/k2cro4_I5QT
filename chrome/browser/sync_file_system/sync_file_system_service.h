// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_SYNC_FILE_SYSTEM_SERVICE_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_SYNC_FILE_SYSTEM_SERVICE_H_

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/sync_file_system/local_file_sync_service.h"
#include "chrome/browser/sync_file_system/remote_file_sync_service.h"
#include "googleurl/src/gurl.h"
#include "webkit/fileapi/syncable/sync_callbacks.h"

namespace fileapi {
class FileSystemContext;
}

namespace sync_file_system {

class SyncEventObserver;

class SyncFileSystemService
    : public ProfileKeyedService,
      public LocalFileSyncService::Observer,
      public RemoteFileSyncService::Observer,
      public base::SupportsWeakPtr<SyncFileSystemService> {
 public:
  // ProfileKeyedService overrides.
  virtual void Shutdown() OVERRIDE;

  void InitializeForApp(
      fileapi::FileSystemContext* file_system_context,
      const std::string& service_name,
      const GURL& app_origin,
      const fileapi::SyncStatusCallback& callback);

  // Returns a list (set) of files that are conflicting.
  void GetConflictFiles(
      const GURL& app_origin,
      const std::string& service_name,
      const fileapi::SyncFileSetCallback& callback);

  // Returns metadata info for a conflicting file |url|.
  void GetConflictFileInfo(
      const GURL& app_origin,
      const std::string& service_name,
      const fileapi::FileSystemURL& url,
      const fileapi::ConflictFileInfoCallback& callback);

  void AddSyncEventObserver(const GURL& app_origin,
                            SyncEventObserver* observer);
  void RemoveSyncEventObserver(const GURL& app_origin,
                               SyncEventObserver* observer);

 private:
  friend class SyncFileSystemServiceFactory;
  friend class SyncFileSystemServiceTest;
  friend class scoped_ptr<SyncFileSystemService>;

  explicit SyncFileSystemService(Profile* profile);
  virtual ~SyncFileSystemService();

  void Initialize(scoped_ptr<LocalFileSyncService> local_file_service,
                  scoped_ptr<RemoteFileSyncService> remote_file_service);

  void DidGetConflictFileInfo(const fileapi::ConflictFileInfoCallback& callback,
                              const fileapi::FileSystemURL& url,
                              const fileapi::SyncFileMetadata* local_metadata,
                              const fileapi::SyncFileMetadata* remote_metadata,
                              fileapi::SyncStatusCode status);

  void DidRegisterOrigin(const GURL& app_origin,
                         const fileapi::SyncStatusCallback& callback,
                         fileapi::SyncStatusCode status);

  // RemoteFileSyncService::Observer overrides.
  virtual void OnLocalChangeAvailable(int64 pending_changes) OVERRIDE;

  // LocalFileSyncService::Observer overrides.
  virtual void OnRemoteChangeAvailable(int64 pending_changes) OVERRIDE;
  virtual void OnRemoteServiceStateUpdated(
      RemoteServiceState state,
      const std::string& description) OVERRIDE;

  Profile* profile_;

  int64 pending_local_changes_;
  int64 pending_remote_changes_;

  scoped_ptr<LocalFileSyncService> local_file_service_;
  scoped_ptr<RemoteFileSyncService> remote_file_service_;

  // TODO(kinuko): clean up this.
  std::set<GURL> initialized_app_origins_;

  std::set<GURL> pending_register_origins_;

  typedef ObserverList<SyncEventObserver> EventObserverList;
  typedef std::map<GURL, EventObserverList*> ObserverMap;
  ObserverMap observer_map_;

  DISALLOW_COPY_AND_ASSIGN(SyncFileSystemService);
};

class SyncFileSystemServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static SyncFileSystemService* GetForProfile(Profile* profile);
  static SyncFileSystemService* FindForProfile(Profile* profile);
  static SyncFileSystemServiceFactory* GetInstance();

  // This overrides the remote service for testing.
  // For testing this must be called before GetForProfile is called.
  // Otherwise a new DriveFileSyncService is created for the new service.
  // Since we use scoped_ptr it's one-off and the instance is passed
  // to the newly created SyncFileSystemService.
  void set_mock_remote_file_service(
      scoped_ptr<RemoteFileSyncService> mock_remote_service);

 private:
  friend struct DefaultSingletonTraits<SyncFileSystemServiceFactory>;
  SyncFileSystemServiceFactory();
  virtual ~SyncFileSystemServiceFactory();

  // ProfileKeyedServiceFactory overrides.
  virtual ProfileKeyedService* BuildServiceInstanceFor(
      Profile* profile) const OVERRIDE;

  mutable scoped_ptr<RemoteFileSyncService> mock_remote_file_service_;
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_SYNC_FILE_SYSTEM_SERVICE_H_
