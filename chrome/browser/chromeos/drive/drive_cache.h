// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_CACHE_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_CACHE_H_

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/chromeos/drive/drive_cache_metadata.h"
#include "chrome/browser/chromeos/drive/drive_file_error.h"

class Profile;

namespace base {

class SequencedTaskRunner;

}  // namespace base

namespace drive {

class DriveCacheEntry;
class DriveCacheMetadata;
class DriveCacheObserver;

// Callback for completion of cache operation.
typedef base::Callback<void(DriveFileError error,
                            const std::string& resource_id,
                            const std::string& md5)> CacheOperationCallback;

// Callback for GetFileFromCache.
typedef base::Callback<void(DriveFileError error,
                            const FilePath& cache_file_path)>
    GetFileFromCacheCallback;

// Callback for GetCacheEntry.
// |success| indicates if the operation was successful.
// |cache_entry| is the obtained cache entry. On failure, |cache_state| is
// set to TEST_CACHE_STATE_NONE.
typedef base::Callback<void(bool success, const DriveCacheEntry& cache_entry)>
    GetCacheEntryCallback;

// Callback for RequestInitialize.
// |success| indicates if the operation was successful.
// TODO(satorux): Change this to DriveFileError when it becomes necessary.
typedef base::Callback<void(bool success)>
    InitializeCacheCallback;

// DriveCache is used to maintain cache states of DriveFileSystem.
//
// All non-static public member functions, unless mentioned otherwise (see
// GetCacheFilePath() for example), should be called from the UI thread.
class DriveCache {
 public:
  // Enum defining GCache subdirectory location.
  // This indexes into |DriveCache::cache_paths_| vector.
  enum CacheSubDirectoryType {
    CACHE_TYPE_META = 0,       // Downloaded feeds.
    CACHE_TYPE_PINNED,         // Symlinks to files in persistent dir that are
                               // pinned, or to /dev/null for non-existent
                               // files.
    CACHE_TYPE_OUTGOING,       // Symlinks to files in persistent or tmp dir to
                               // be uploaded.
    CACHE_TYPE_PERSISTENT,     // Files that are pinned or modified locally,
                               // not evictable, hopefully.
    CACHE_TYPE_TMP,            // Files that don't meet criteria to be in
                               // persistent dir, and hence evictable.
    CACHE_TYPE_TMP_DOWNLOADS,  // Downloaded files.
    CACHE_TYPE_TMP_DOCUMENTS,  // Temporary JSON files for hosted documents.
    NUM_CACHE_TYPES,           // This must be at the end.
  };

  // Enum defining origin of a cached file.
  enum CachedFileOrigin {
    CACHED_FILE_FROM_SERVER = 0,
    CACHED_FILE_LOCALLY_MODIFIED,
    CACHED_FILE_MOUNTED,
  };

  // Enum defining type of file operation e.g. copy or move, etc.
  enum FileOperationType {
    FILE_OPERATION_MOVE = 0,
    FILE_OPERATION_COPY,
  };

  // Returns the sub-directory under drive cache directory for the given sub
  // directory type. Example:  <user_profile_dir>/GCache/v1/tmp
  //
  // Can be called on any thread.
  FilePath GetCacheDirectoryPath(CacheSubDirectoryType sub_dir_type) const;

  // Returns absolute path of the file if it were cached or to be cached.
  //
  // Can be called on any thread.
  FilePath GetCacheFilePath(const std::string& resource_id,
                            const std::string& md5,
                            CacheSubDirectoryType sub_dir_type,
                            CachedFileOrigin file_origin) const;

  // Returns true if the given path is under drive cache directory, i.e.
  // <user_profile_dir>/GCache/v1
  //
  // Can be called on any thread.
  bool IsUnderDriveCacheDirectory(const FilePath& path) const;

  // Adds observer.
  void AddObserver(DriveCacheObserver* observer);

  // Removes observer.
  void RemoveObserver(DriveCacheObserver* observer);

  // Gets the cache entry for file corresponding to |resource_id| and |md5|
  // and runs |callabck| with true and the entry found if entry exists in cache
  // map.  Otherwise, runs |callback| with false.
  // |md5| can be empty if only matching |resource_id| is desired, which may
  // happen when looking for pinned entries where symlinks' filenames have no
  // extension and hence no md5.
  // |callback| must not be null.
  void GetCacheEntry(const std::string& resource_id,
                     const std::string& md5,
                     const GetCacheEntryCallback& callback);

  // Iterates all files in the cache and calls |iteration_callback| for each
  // file. |completion_callback| is run upon completion.
  void Iterate(const CacheIterateCallback& iteration_callback,
               const base::Closure& completion_callback);

  // Frees up disk space to store the given number of bytes, while keeping
  // kMinFreeSpace bytes on the disk, if needed.
  // Runs |callback| with true when we successfully manage to have enough space.
  void FreeDiskSpaceIfNeededFor(int64 num_bytes,
                                const InitializeCacheCallback& callback);

  // Checks if file corresponding to |resource_id| and |md5| exists in cache.
  // |callback| must not be null.
  void GetFile(const std::string& resource_id,
               const std::string& md5,
               const GetFileFromCacheCallback& callback);

  // Modifies cache state, which involves the following:
  // - moves or copies (per |file_operation_type|) |source_path|
  //   to |dest_path| in the cache dir
  // - if necessary, creates symlink
  // - deletes stale cached versions of |resource_id| in
  // |dest_path|'s directory.
  void Store(const std::string& resource_id,
             const std::string& md5,
             const FilePath& source_path,
             FileOperationType file_operation_type,
             const CacheOperationCallback& callback);

  // Modifies cache state, which involves the following:
  // - moves |source_path| to |dest_path| in persistent dir if
  //   file is not dirty
  // - creates symlink in pinned dir that references downloaded or locally
  //   modified file
  void Pin(const std::string& resource_id,
           const std::string& md5,
           const CacheOperationCallback& callback);

  // Modifies cache state, which involves the following:
  // - moves |source_path| to |dest_path| in tmp dir if file is not dirty
  // - deletes symlink from pinned dir
  void Unpin(const std::string& resource_id,
             const std::string& md5,
             const CacheOperationCallback& callback);

  // Modifies cache state, which involves the following:
  // - moves |source_path| to |dest_path|, where
  //   if we're mounting: |source_path| is the unmounted path and has .<md5>
  //       extension, and |dest_path| is the mounted path in persistent dir
  //       and has .<md5>.mounted extension;
  //   if we're unmounting: the opposite is true for the two paths, i.e.
  //       |dest_path| is the mounted path and |source_path| the unmounted path.
  void SetMountedState(const FilePath& file_path,
                       bool to_mount,
                       const GetFileFromCacheCallback& callback);

  // Modifies cache state, which involves the following:
  // - moves |source_path| to |dest_path| in persistent dir, where
  //   |source_path| has .<md5> extension and |dest_path| has .local extension
  // - if file is pinned, updates symlink in pinned dir to reference dirty file
  // |callback| must not be null.
  void MarkDirty(const std::string& resource_id,
                 const std::string& md5,
                 const GetFileFromCacheCallback& callback);

  // Modifies cache state, i.e. creates symlink in outgoing
  // dir to reference dirty file in persistent dir.
  void CommitDirty(const std::string& resource_id,
                   const std::string& md5,
                   const CacheOperationCallback& callback);

  // Modifies cache state, which involves the following:
  // - moves |source_path| to |dest_path| in persistent dir if
  //   file is pinned or tmp dir otherwise, where |source_path| has .local
  //   extension and |dest_path| has .<md5> extension
  // - deletes symlink in outgoing dir
  // - if file is pinned, updates symlink in pinned dir to reference
  //   |dest_path|
  void ClearDirty(const std::string& resource_id,
                  const std::string& md5,
                  const CacheOperationCallback& callback);

  // Does the following:
  // - remove all delete stale cache versions corresponding to |resource_id| in
  //   persistent, tmp and pinned directories
  // - remove entry corresponding to |resource_id| from cache map.
  void Remove(const std::string& resource_id,
              const CacheOperationCallback& callback);

  // Does the following:
  // - remove all the files in the cache directory.
  // - re-create the |metadata_| instance.
  // |callback| must not be null.
  void ClearAll(const InitializeCacheCallback& callback);

  // Utility method to call Initialize on UI thread. |callback| is called on
  // UI thread when the initialization is complete.
  // |callback| must not be null.
  void RequestInitialize(const InitializeCacheCallback& callback);

  // Utility method to call InitializeForTesting on UI thread.
  void RequestInitializeForTesting();

  // Factory methods for DriveCache.
  // |pool| and |sequence_token| are used to assert that the functions are
  // called on the right sequenced worker pool with the right sequence token.
  //
  // For testing, the thread assertion can be disabled by passing NULL and
  // the default value of SequenceToken.
  static DriveCache* CreateDriveCache(
      const FilePath& cache_root_path,
      base::SequencedTaskRunner* blocking_task_runner);

  // Deletes the cache.
  void Destroy();

  // Gets the cache root path (i.e. <user_profile_dir>/GCache/v1) from the
  // profile.
  // TODO(satorux): Write a unit test for this.
  static FilePath GetCacheRootPath(Profile* profile);

  // Returns file paths for all the cache sub directories under
  // |cache_root_path|.
  static std::vector<FilePath> GetCachePaths(const FilePath& cache_root_path);

  // Creates cache directory and its sub-directories if they don't exist.
  // TODO(glotov): take care of this when the setup and cleanup part is
  // landed, noting that these directories need to be created for development
  // in linux box and unittest. (http://crosbug.com/27577)
  static bool CreateCacheDirectories(
      const std::vector<FilePath>& paths_to_create);

  // Returns the type of the sub directory where the cache file is stored.
  static CacheSubDirectoryType GetSubDirectoryType(
      const DriveCacheEntry& cache_entry);

 private:
  typedef std::pair<DriveFileError, FilePath> GetFileResult;

  DriveCache(const FilePath& cache_root_path,
             base::SequencedTaskRunner* blocking_task_runner);
  virtual ~DriveCache();

  // Checks whether the current thread is on the right sequenced worker pool
  // with the right sequence ID. If not, DCHECK will fail.
  void AssertOnSequencedWorkerPool();

  // Initializes the cache. Returns true on success.
  bool InitializeOnBlockingPool();

  // Initializes the cache with in-memory cache for testing.
  // The in-memory cache is used since it's faster than the db.
  void InitializeOnBlockingPoolForTesting();

  // Deletes the cache.
  void DestroyOnBlockingPool();

  // Gets the cache entry by the given resource ID and MD5.
  // See also GetCacheEntry().
  bool GetCacheEntryOnBlockingPool(const std::string& resource_id,
                                   const std::string& md5,
                                   DriveCacheEntry* entry);

  // Used to implement Iterate().
  void IterateOnBlockingPool(const CacheIterateCallback& iteration_callback);

  // Used to implement FreeDiskSpaceIfNeededFor().
  bool FreeDiskSpaceOnBlockingPoolIfNeededFor(int64 num_bytes);

  // Used to implement GetFile.
  scoped_ptr<GetFileResult> GetFileOnBlockingPool(
      const std::string& resource_id,
      const std::string& md5);

  // Used to implement Store.
  DriveFileError StoreOnBlockingPool(const std::string& resource_id,
                                     const std::string& md5,
                                     const FilePath& source_path,
                                     FileOperationType file_operation_type);

  // Used to implement Pin.
  DriveFileError PinOnBlockingPool(const std::string& resource_id,
                                   const std::string& md5);

  // Used to implement Unpin.
  DriveFileError UnpinOnBlockingPool(const std::string& resource_id,
                                     const std::string& md5);

  // Used to implement SetMountedState.
  scoped_ptr<GetFileResult> SetMountedStateOnBlockingPool(
      const FilePath& file_path,
      bool to_mount);

  // Used to implement MarkDirty.
  scoped_ptr<GetFileResult> MarkDirtyOnBlockingPool(
      const std::string& resource_id,
      const std::string& md5);

  // Used to implement CommitDirty.
  DriveFileError CommitDirtyOnBlockingPool(const std::string& resource_id,
                                           const std::string& md5);

  // Used to implement ClearDirty.
  DriveFileError ClearDirtyOnBlockingPool(const std::string& resource_id,
                                          const std::string& md5);

  // Used to implement Remove.
  DriveFileError RemoveOnBlockingPool(const std::string& resource_id);

  // Used to implement ClearAll.
  bool ClearAllOnBlockingPool();

  // Runs callback and notifies the observers when file is pinned.
  void OnPinned(const std::string& resource_id,
                const std::string& md5,
                const CacheOperationCallback& callback,
                DriveFileError error);

  // Runs callback and notifies the observers when file is unpinned.
  void OnUnpinned(const std::string& resource_id,
                  const std::string& md5,
                  const CacheOperationCallback& callback,
                  DriveFileError error);

  // Runs callback and notifies the observers when file is committed.
  void OnCommitDirty(const std::string& resource_id,
                     const std::string& md5,
                     const CacheOperationCallback& callback,
                     DriveFileError error);

  // The root directory of the cache (i.e. <user_profile_dir>/GCache/v1).
  const FilePath cache_root_path_;
  // Paths for all subdirectories of GCache, one for each
  // DriveCache::CacheSubDirectoryType enum.
  const std::vector<FilePath> cache_paths_;
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;

  // The cache state data. This member must be access only on the blocking pool.
  scoped_ptr<DriveCacheMetadata> metadata_;

  // List of observers, this member must be accessed on UI thread.
  ObserverList<DriveCacheObserver> observers_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<DriveCache> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(DriveCache);
};


// The minimum free space to keep. DriveFileSystem::GetFileByPath() returns
// GDATA_FILE_ERROR_NO_SPACE if the available space is smaller than
// this value.
//
// Copied from cryptohome/homedirs.h.
// TODO(satorux): Share the constant.
const int64 kMinFreeSpace = 512 * 1LL << 20;

// Interface class used for getting the free disk space. Only for testing.
class FreeDiskSpaceGetterInterface {
 public:
  virtual ~FreeDiskSpaceGetterInterface() {}
  virtual int64 AmountOfFreeDiskSpace() const = 0;
};

// Sets the free disk space getter for testing.
// The existing getter is deleted.
void SetFreeDiskSpaceGetterForTesting(FreeDiskSpaceGetterInterface* getter);

}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_CACHE_H_
