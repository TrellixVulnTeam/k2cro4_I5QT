// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_gallery/mtp_device_delegate_impl_linux.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/sequenced_task_runner.h"
#include "base/sequenced_task_runner_helpers.h"
#include "base/string_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/media_transfer_protocol/media_transfer_protocol_manager.h"
#include "chrome/browser/media_transfer_protocol/mtp_file_entry.pb.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

using base::Bind;
using base::PlatformFileError;
using base::PlatformFileInfo;
using base::SequencedTaskRunner;
using base::Time;
using content::BrowserThread;
using fileapi::FileSystemFileUtil;

namespace chrome {

namespace {

using base::DeleteHelper;
using base::RefCountedThreadSafe;
using base::WaitableEvent;

// Helper struct to delete worker objects on |media_task_runner_| thread.
template <typename WORKER> struct WorkerDeleter {
  static void Destruct(const WORKER* worker) {
    if (!worker->media_task_runner()->RunsTasksOnCurrentThread()) {
      worker->media_task_runner()->DeleteSoon(FROM_HERE, worker);
      return;
    }
    delete worker;
  }
};

typedef struct WorkerDeleter<class GetFileInfoWorker> GetFileInfoWorkerDeleter;
typedef struct WorkerDeleter<class OpenStorageWorker> OpenStorageWorkerDeleter;
typedef struct WorkerDeleter<class ReadDirectoryWorker>
    ReadDirectoryWorkerDeleter;
typedef struct WorkerDeleter<class ReadFileWorker> ReadFileWorkerDeleter;

// File path separator constant.
const char kRootPath[] = "/";

// Returns MediaTransferProtocolManager instance on success or NULL on failure.
MediaTransferProtocolManager* GetMediaTransferProtocolManager() {
  MediaTransferProtocolManager* mtp_device_mgr =
      MediaTransferProtocolManager::GetInstance();
  DCHECK(mtp_device_mgr);
  return mtp_device_mgr;
}

// Does nothing.
// This method is used to handle the results of
// MediaTransferProtocolManager::CloseStorage method call.
void DoNothing(bool error) {
}

// Returns the device relative file path given |file_path|.
// E.g.: If the |file_path| is "/usb:2,2:12345/DCIM" and |registered_dev_path|
// is "/usb:2,2:12345", this function returns the device relative path which is
// "/DCIM".
std::string GetDeviceRelativePath(const std::string& registered_dev_path,
                                  const std::string& file_path) {
  DCHECK(!registered_dev_path.empty());
  DCHECK(!file_path.empty());

  std::string actual_file_path;
  if (registered_dev_path == file_path) {
    actual_file_path = kRootPath;
  } else {
    actual_file_path = file_path;
    ReplaceFirstSubstringAfterOffset(&actual_file_path, 0,
                                     registered_dev_path.c_str(), "");
  }
  DCHECK(!actual_file_path.empty());
  return actual_file_path;
}

// Worker class to open a MTP device for communication. This class is
// instantiated and destructed on |media_task_runner_|. In order to post a
// request on Dbus thread, the caller should run on UI thread. Therefore, this
// class posts the open device request on UI thread and receives the response
// on UI thread.
class OpenStorageWorker
    : public RefCountedThreadSafe<OpenStorageWorker, OpenStorageWorkerDeleter> {
 public:
  // Constructed on |media_task_runner_| thread.
  OpenStorageWorker(const std::string& name, SequencedTaskRunner* task_runner,
                    WaitableEvent* task_completed_event,
                    WaitableEvent* shutdown_event)
      : storage_name_(name),
        media_task_runner_(task_runner),
        on_task_completed_event_(task_completed_event),
        on_shutdown_event_(shutdown_event) {
    DCHECK(on_task_completed_event_);
    DCHECK(on_shutdown_event_);
  }

  // This function is invoked on |media_task_runner_| to post the task on UI
  // thread. This blocks the |media_task_runner_| until the task is complete.
  void Run() {
    if (on_shutdown_event_->IsSignaled()) {
      // Process is in shutdown mode.
      // Do not post any task on |media_task_runner_|.
      return;
    }

    DCHECK(media_task_runner_->RunsTasksOnCurrentThread());
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            Bind(&OpenStorageWorker::DoWorkOnUIThread, this));
    on_task_completed_event_->Wait();
  }

  // Returns a device handle string if the OpenStorage() request was
  // successfully completed or an empty string otherwise.
  const std::string& device_handle() const { return device_handle_; }

  // Returns the |media_task_runner_| associated with this worker object.
  // This function is exposed for WorkerDeleter struct to access the
  // |media_task_runner_|.
  SequencedTaskRunner* media_task_runner() const {
    return media_task_runner_.get();
  }

 private:
  friend struct WorkerDeleter<OpenStorageWorker>;
  friend class DeleteHelper<OpenStorageWorker>;
  friend class RefCountedThreadSafe<OpenStorageWorker,
                                    OpenStorageWorkerDeleter>;

  // Destructed via OpenStorageWorkerDeleter struct.
  virtual ~OpenStorageWorker() {
    // This object must be destructed on |media_task_runner_|.
  }

  // Dispatches a request to MediaTransferProtocolManager to open the MTP
  // storage for communication. This is called on UI thread.
  void DoWorkOnUIThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    GetMediaTransferProtocolManager()->OpenStorage(
        storage_name_, mtpd::kReadOnlyMode,
        Bind(&OpenStorageWorker::OnDidWorkOnUIThread, this));
  }

  // Query callback for DoWorkOnUIThread(). |error| is set to true if the device
  // did not open successfully. This function signals to unblock
  // |media_task_runner_|.
  void OnDidWorkOnUIThread(const std::string& device_handle, bool error) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (!error)
      device_handle_ = device_handle;
    on_task_completed_event_->Signal();
  }

  // Stores the storage name to open the device.
  const std::string storage_name_;

  // Stores a reference to |media_task_runner_| to destruct this object on the
  // correct thread.
  scoped_refptr<SequencedTaskRunner> media_task_runner_;

  // |media_task_runner_| can wait on this event until the required operation
  // is complete.
  // TODO(kmadhusu): Remove this WaitableEvent after modifying the
  // DeviceMediaFileUtil functions as asynchronous functions.
  WaitableEvent* on_task_completed_event_;

  // Stores a reference to waitable event associated with the shut down message.
  WaitableEvent* on_shutdown_event_;

  // Stores the result of OpenStorage() request.
  std::string device_handle_;

  DISALLOW_COPY_AND_ASSIGN(OpenStorageWorker);
};

// Worker class to get media device file information given a |path|.
class GetFileInfoWorker
    : public RefCountedThreadSafe<GetFileInfoWorker, GetFileInfoWorkerDeleter> {
 public:
  // Constructed on |media_task_runner_| thread.
  GetFileInfoWorker(const std::string& handle,
                    const std::string& path,
                    SequencedTaskRunner* task_runner,
                    WaitableEvent* task_completed_event,
                    WaitableEvent* shutdown_event)
      : device_handle_(handle),
        path_(path),
        media_task_runner_(task_runner),
        error_(base::PLATFORM_FILE_OK),
        on_task_completed_event_(task_completed_event),
        on_shutdown_event_(shutdown_event) {
    DCHECK(on_task_completed_event_);
    DCHECK(on_shutdown_event_);
  }

  // This function is invoked on |media_task_runner_| to post the task on UI
  // thread. This blocks the |media_task_runner_| until the task is complete.
  void Run() {
    if (on_shutdown_event_->IsSignaled()) {
      // Process is in shutdown mode.
      // Do not post any task on |media_task_runner_|.
      return;
    }

    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            Bind(&GetFileInfoWorker::DoWorkOnUIThread, this));
    on_task_completed_event_->Wait();
  }

  // Returns GetFileInfo() result and fills in |file_info| with requested file
  // entry details.
  PlatformFileError get_file_info(PlatformFileInfo* file_info) const {
    if (file_info)
      *file_info = file_entry_info_;
    return error_;
  }

  // Returns the |media_task_runner_| associated with this worker object.
  // This function is exposed for WorkerDeleter struct to access the
  // |media_task_runner_|.
  SequencedTaskRunner* media_task_runner() const {
    return media_task_runner_.get();
  }

 private:
  friend struct WorkerDeleter<GetFileInfoWorker>;
  friend class DeleteHelper<GetFileInfoWorker>;
  friend class RefCountedThreadSafe<GetFileInfoWorker,
                                    GetFileInfoWorkerDeleter>;

  // Destructed via GetFileInfoWorkerDeleter.
  virtual ~GetFileInfoWorker() {
    // This object must be destructed on |media_task_runner_|.
  }

  // Dispatches a request to MediaTransferProtocolManager to get file
  // information.
  void DoWorkOnUIThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    GetMediaTransferProtocolManager()->GetFileInfoByPath(
        device_handle_, path_,
        Bind(&GetFileInfoWorker::OnDidWorkOnUIThread, this));
  }

  // Query callback for DoWorkOnUIThread(). On success, |file_entry| has media
  // file information. On failure, |error| is set to true. This function signals
  // to unblock |media_task_runner_|.
  void OnDidWorkOnUIThread(const MtpFileEntry& file_entry, bool error) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (error) {
      error_ = base::PLATFORM_FILE_ERROR_NOT_FOUND;
    } else {
      file_entry_info_.size = file_entry.file_size();
      file_entry_info_.is_directory =
          file_entry.file_type() == MtpFileEntry::FILE_TYPE_FOLDER;
      file_entry_info_.is_symbolic_link = false;
      file_entry_info_.last_modified =
          base::Time::FromTimeT(file_entry.modification_time());
      file_entry_info_.last_accessed =
          base::Time::FromTimeT(file_entry.modification_time());
      file_entry_info_.creation_time = base::Time();
    }
    on_task_completed_event_->Signal();
  }

  // Stores the device handle to query the device.
  const std::string device_handle_;

  // Stores the requested media device file path.
  const std::string path_;

  // Stores a reference to |media_task_runner_| to destruct this object on the
  // correct thread.
  scoped_refptr<SequencedTaskRunner> media_task_runner_;

  // Stores the result of GetFileInfo().
  PlatformFileError error_;

  // Stores the media file entry information.
  PlatformFileInfo file_entry_info_;

  // |media_task_runner_| can wait on this event until the required operation
  // is complete.
  // TODO(kmadhusu): Remove this WaitableEvent after modifying the
  // DeviceMediaFileUtil functions as asynchronous functions.
  WaitableEvent* on_task_completed_event_;

  // Stores a reference to waitable event associated with the shut down message.
  WaitableEvent* on_shutdown_event_;

  DISALLOW_COPY_AND_ASSIGN(GetFileInfoWorker);
};

// Worker class to read media device file data given a file |path|.
class ReadFileWorker
    : public RefCountedThreadSafe<ReadFileWorker, ReadFileWorkerDeleter> {
 public:
  // Constructed on |media_task_runner_| thread.
  ReadFileWorker(const std::string& handle,
                 const std::string& path,
                 uint32 total_size,
                 SequencedTaskRunner* task_runner,
                 WaitableEvent* task_completed_event,
                 WaitableEvent* shutdown_event)
      : device_handle_(handle),
        path_(path),
        total_bytes_(total_size),
        error_occurred_(false),
        media_task_runner_(task_runner),
        on_task_completed_event_(task_completed_event),
        on_shutdown_event_(shutdown_event) {
    DCHECK(on_task_completed_event_);
    DCHECK(on_shutdown_event_);
  }

  // This function is invoked on |media_task_runner_| to post the task on UI
  // thread. This blocks the |media_task_runner_| until the task is complete.
  void Run() {
    if (on_shutdown_event_->IsSignaled()) {
      // Process is in shutdown mode.
      // Do not post any task on |media_task_runner_|.
      return;
    }

    while (!error_occurred_ && (data_.size() < total_bytes_)) {
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                              Bind(&ReadFileWorker::DoWorkOnUIThread, this));
      on_task_completed_event_->Wait();
    }
  }

  // Returns the media file contents received from mtpd.
  const std::string& data() const { return data_; }

  // Returns the |media_task_runner_| associated with this worker object.
  // This function is exposed for WorkerDeleter struct to access the
  // |media_task_runner_|.
  SequencedTaskRunner* media_task_runner() const {
    return media_task_runner_.get();
  }

 private:
  friend struct WorkerDeleter<ReadFileWorker>;
  friend class DeleteHelper<ReadFileWorker>;
  friend class RefCountedThreadSafe<ReadFileWorker, ReadFileWorkerDeleter>;

  // Destructed via ReadFileWorkerDeleter.
  virtual ~ReadFileWorker() {
    // This object must be destructed on |media_task_runner_|.
  }

  // Dispatches a request to MediaTransferProtocolManager to get the media file
  // contents.
  void DoWorkOnUIThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    GetMediaTransferProtocolManager()->ReadFileChunkByPath(
        device_handle_, path_, data_.size(), BytesToRead(),
        Bind(&ReadFileWorker::OnDidWorkOnUIThread, this));
  }

  // Query callback for DoWorkOnUIThread(). On success, |data| has the media
  // file contents. On failure, |error| is set to true. This function signals
  // to unblock |media_task_runner_|.
  void OnDidWorkOnUIThread(const std::string& data, bool error) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    error_occurred_ = error;
    if (!error) {
      if ((BytesToRead() == data.size())) {
        // TODO(kmadhusu): Data could be really huge. Consider passing data by
        // pointer/ref rather than by value here to avoid an extra data copy.
        data_.append(data);
      } else {
        NOTREACHED();
        error_occurred_ = true;
      }
    }
    on_task_completed_event_->Signal();
  }

  uint32 BytesToRead() const {
    // Read data in 1 MB chunks.
    static const uint32 kReadChunkSize = 1024 * 1024;
    return std::min(kReadChunkSize,
                    total_bytes_ - static_cast<uint32>(data_.size()));
  }

  // The device unique identifier to query the device.
  const std::string device_handle_;

  // The media device file path.
  const std::string path_;

  // The data from mtpd.
  std::string data_;

  // Number of bytes to read.
  const uint32 total_bytes_;

  // Whether an error occurred during file transfer.
  bool error_occurred_;

  // A reference to |media_task_runner_| to destruct this object on the correct
  // thread.
  scoped_refptr<SequencedTaskRunner> media_task_runner_;

  // |media_task_runner_| can wait on this event until the required operation
  // is complete.
  // TODO(kmadhusu): Remove this WaitableEvent after modifying the
  // DeviceMediaFileUtil functions as asynchronous functions.
  WaitableEvent* on_task_completed_event_;

  // Stores a reference to waitable event associated with the shut down message.
  WaitableEvent* on_shutdown_event_;

  DISALLOW_COPY_AND_ASSIGN(ReadFileWorker);
};

// Worker class to read directory contents. Device is already opened for
// communication.
class ReadDirectoryWorker
    : public RefCountedThreadSafe<ReadDirectoryWorker,
                                  ReadDirectoryWorkerDeleter> {
 public:
  // Construct a worker object given the directory |path|. This object is
  // constructed on |media_task_runner_| thread.
  ReadDirectoryWorker(const std::string& handle,
                      const std::string& path,
                      SequencedTaskRunner* task_runner,
                      WaitableEvent* task_completed_event,
                      WaitableEvent* shutdown_event)
      : device_handle_(handle),
        dir_path_(path),
        dir_entry_id_(0),
        media_task_runner_(task_runner),
        on_task_completed_event_(task_completed_event),
        on_shutdown_event_(shutdown_event) {
    DCHECK(!dir_path_.empty());
    DCHECK(on_task_completed_event_);
    DCHECK(on_shutdown_event_);
  }

  // Construct a worker object given the directory |entry_id|. This object is
  // constructed on |media_task_runner_| thread.
  ReadDirectoryWorker(const std::string& storage_name,
                      const uint32_t entry_id,
                      SequencedTaskRunner* task_runner,
                      WaitableEvent* task_completed_event,
                      WaitableEvent* shutdown_event)
      : device_handle_(storage_name),
        dir_entry_id_(entry_id),
        media_task_runner_(task_runner),
        on_task_completed_event_(task_completed_event),
        on_shutdown_event_(shutdown_event) {
    DCHECK(on_task_completed_event_);
    DCHECK(on_shutdown_event_);
  }

  // This function is invoked on |media_task_runner_| to post the task on UI
  // thread. This blocks the |media_task_runner_| until the task is complete.
  void Run() {
    if (on_shutdown_event_->IsSignaled()) {
      // Process is in shutdown mode.
      // Do not post any task on |media_task_runner_|.
      return;
    }

    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            Bind(&ReadDirectoryWorker::DoWorkOnUIThread, this));
    on_task_completed_event_->Wait();
  }

  // Returns the directory entries for the given directory path.
  const std::vector<MtpFileEntry>& get_file_entries() const {
    return file_entries_;
  }

  // Returns the |media_task_runner_| associated with this worker object.
  // This function is exposed for WorkerDeleter struct to access the
  // |media_task_runner_|.
  SequencedTaskRunner* media_task_runner() const {
    return media_task_runner_.get();
  }

 private:
  friend struct WorkerDeleter<ReadDirectoryWorker>;
  friend class DeleteHelper<ReadDirectoryWorker>;
  friend class RefCountedThreadSafe<ReadDirectoryWorker,
                                    ReadDirectoryWorkerDeleter>;

  // Destructed via ReadDirectoryWorkerDeleter.
  virtual ~ReadDirectoryWorker() {
    // This object must be destructed on |media_task_runner_|.
  }

  // Dispatches a request to MediaTransferProtocolManager to read the directory
  // entries. This is called on UI thread.
  void DoWorkOnUIThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    if (!dir_path_.empty()) {
      GetMediaTransferProtocolManager()->ReadDirectoryByPath(
          device_handle_, dir_path_,
          Bind(&ReadDirectoryWorker::OnDidWorkOnUIThread, this));
    } else {
      GetMediaTransferProtocolManager()->ReadDirectoryById(
          device_handle_, dir_entry_id_,
          Bind(&ReadDirectoryWorker::OnDidWorkOnUIThread, this));
    }
  }

  // Query callback for DoWorkOnUIThread(). On success, |file_entries| has the
  // directory file entries. |error| is true if there was an error. This
  // function signals to unblock |media_task_runner_|.
  void OnDidWorkOnUIThread(const std::vector<MtpFileEntry>& file_entries,
                           bool error) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (!error)
      file_entries_ = file_entries;
    on_task_completed_event_->Signal();
  }

  // Stores the device handle to communicate with storage device.
  const std::string device_handle_;

  // Stores the directory path whose contents needs to be listed.
  const std::string dir_path_;

  // Stores the directory entry id whose contents needs to be listed.
  const uint32_t dir_entry_id_;

  // Stores a reference to |media_task_runner_| to destruct this object on the
  // correct thread.
  scoped_refptr<SequencedTaskRunner> media_task_runner_;

  // |media_task_runner_| can wait on this event until the required operation
  // is complete.
  // TODO(kmadhusu): Remove this WaitableEvent after modifying the
  // DeviceMediaFileUtil functions as asynchronous functions.
  WaitableEvent* on_task_completed_event_;

  // Stores a reference to waitable event associated with the shut down message.
  WaitableEvent* on_shutdown_event_;

  // Stores the result of read directory request.
  std::vector<MtpFileEntry> file_entries_;

  DISALLOW_COPY_AND_ASSIGN(ReadDirectoryWorker);
};

// Simply enumerate each files from a given file entry list.
// Used to enumerate top-level files of an media file system.
class MediaFileEnumerator : public FileSystemFileUtil::AbstractFileEnumerator {
 public:
  explicit MediaFileEnumerator(const std::vector<MtpFileEntry>& entries)
      : file_entries_(entries),
        file_entry_iter_(file_entries_.begin()) {
  }

  virtual ~MediaFileEnumerator() {}

  // AbstractFileEnumerator override.
  // Returns the next file entry path on success and empty file path on
  // failure.
  virtual FilePath Next() OVERRIDE {
    if (file_entry_iter_ == file_entries_.end())
      return FilePath();

    current_file_info_ = *file_entry_iter_;
    ++file_entry_iter_;
    return FilePath(current_file_info_.file_name());
  }

  // AbstractFileEnumerator override.
  // Returns the size of the current file entry.
  virtual int64 Size() OVERRIDE {
    return current_file_info_.file_size();
  }

  // AbstractFileEnumerator override.
  // Returns true if the current file entry is a directory else false.
  virtual bool IsDirectory() OVERRIDE {
    return current_file_info_.file_type() == MtpFileEntry::FILE_TYPE_FOLDER;
  }

  // AbstractFileEnumerator override.
  // Returns the last modified time of the current file entry.
  virtual base::Time LastModifiedTime() OVERRIDE {
    return base::Time::FromTimeT(current_file_info_.modification_time());
  }

 private:
  // List of directory file entries information.
  const std::vector<MtpFileEntry> file_entries_;

  // Iterator to access the individual file entries.
  std::vector<MtpFileEntry>::const_iterator file_entry_iter_;

  // Stores the current file information.
  MtpFileEntry current_file_info_;

  DISALLOW_COPY_AND_ASSIGN(MediaFileEnumerator);
};

// Recursively enumerate each file entry from a given media file entry set.
class RecursiveMediaFileEnumerator
    : public FileSystemFileUtil::AbstractFileEnumerator {
 public:
  RecursiveMediaFileEnumerator(const std::string& handle,
                               SequencedTaskRunner* task_runner,
                               const std::vector<MtpFileEntry>& entries,
                               WaitableEvent* task_completed_event,
                               WaitableEvent* shutdown_event)
      : device_handle_(handle),
        media_task_runner_(task_runner),
        file_entries_(entries),
        file_entry_iter_(file_entries_.begin()),
        on_task_completed_event_(task_completed_event),
        on_shutdown_event_(shutdown_event) {
    DCHECK(on_task_completed_event_);
    DCHECK(on_shutdown_event_);
    current_enumerator_.reset(new MediaFileEnumerator(entries));
  }

  virtual ~RecursiveMediaFileEnumerator() {}

  // AbstractFileEnumerator override.
  // Returns the next file entry path on success and empty file path on
  // failure or when it reaches the end.
  virtual FilePath Next() OVERRIDE {
    if (on_shutdown_event_->IsSignaled()) {
      // Process is in shut down mode.
      return FilePath();
    }

    FilePath path = current_enumerator_->Next();
    if (!path.empty())
      return path;

    // We reached the end.
    if (file_entry_iter_ == file_entries_.end())
      return FilePath();

    // Enumerate subdirectories of the next media file entry.
    MtpFileEntry next_file_entry = *file_entry_iter_;
    ++file_entry_iter_;

    // Create a ReadDirectoryWorker object to enumerate sub directories.
    scoped_refptr<ReadDirectoryWorker> worker(new ReadDirectoryWorker(
        device_handle_, next_file_entry.item_id(), media_task_runner_,
        on_task_completed_event_, on_shutdown_event_));
    worker->Run();
    if (!worker->get_file_entries().empty()) {
      current_enumerator_.reset(
          new MediaFileEnumerator(worker->get_file_entries()));
    } else {
      current_enumerator_.reset(new FileSystemFileUtil::EmptyFileEnumerator());
    }
    return current_enumerator_->Next();
  }

  // AbstractFileEnumerator override.
  // Returns the size of the current file entry.
  virtual int64 Size() OVERRIDE {
    return current_enumerator_->Size();
  }

  // AbstractFileEnumerator override.
  // Returns true if the current media file entry is a folder type else false.
  virtual bool IsDirectory() OVERRIDE {
    return current_enumerator_->IsDirectory();
  }

  // AbstractFileEnumerator override.
  // Returns the last modified time of the current file entry.
  virtual base::Time LastModifiedTime() OVERRIDE {
    return current_enumerator_->LastModifiedTime();
  }

 private:
  // Stores the device handle that was used to open the device.
  const std::string device_handle_;

  // Stores a reference to |media_task_runner_| to construct and destruct
  // ReadDirectoryWorker object on the correct thread.
  scoped_refptr<SequencedTaskRunner> media_task_runner_;

  // List of top-level directory file entries.
  const std::vector<MtpFileEntry> file_entries_;

  // Iterator to access the individual file entries.
  std::vector<MtpFileEntry>::const_iterator file_entry_iter_;

  // Enumerator to access current directory Id/path entries.
  scoped_ptr<FileSystemFileUtil::AbstractFileEnumerator> current_enumerator_;

  // |media_task_runner_| can wait on this event until the requested operation
  // is complete.
  WaitableEvent* on_task_completed_event_;

  // Stores a reference to waitable event associated with the shut down message.
  WaitableEvent* on_shutdown_event_;

  DISALLOW_COPY_AND_ASSIGN(RecursiveMediaFileEnumerator);
};

}  // namespace

MTPDeviceDelegateImplLinux::MTPDeviceDelegateImplLinux(
    const std::string& device_location)
    : device_path_(device_location),
      on_task_completed_event_(false, false),
      on_shutdown_event_(true, false) {
  CHECK(!device_path_.empty());
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  base::SequencedWorkerPool* pool = BrowserThread::GetBlockingPool();
  base::SequencedWorkerPool::SequenceToken media_sequence_token =
      pool->GetNamedSequenceToken("media-task-runner");
  media_task_runner_ = pool->GetSequencedTaskRunner(media_sequence_token);
  registrar_.Add(this, chrome::NOTIFICATION_APP_TERMINATING,
                 content::NotificationService::AllSources());

  DCHECK(media_task_runner_);
}

MTPDeviceDelegateImplLinux::~MTPDeviceDelegateImplLinux() {
  registrar_.RemoveAll();
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  GetMediaTransferProtocolManager()->CloseStorage(device_handle_,
                                                  Bind(&DoNothing));
}

PlatformFileError MTPDeviceDelegateImplLinux::GetFileInfo(
    const FilePath& file_path,
    PlatformFileInfo* file_info) {
  if (!LazyInit())
    return base::PLATFORM_FILE_ERROR_FAILED;

  scoped_refptr<GetFileInfoWorker> worker(new GetFileInfoWorker(
      device_handle_, GetDeviceRelativePath(device_path_, file_path.value()),
      media_task_runner_, &on_task_completed_event_, &on_shutdown_event_));
  worker->Run();
  return worker->get_file_info(file_info);
}

scoped_ptr<FileSystemFileUtil::AbstractFileEnumerator>
    MTPDeviceDelegateImplLinux::CreateFileEnumerator(
        const FilePath& root,
        bool recursive) {
  if (root.value().empty() || !LazyInit()) {
    return make_scoped_ptr(new FileSystemFileUtil::EmptyFileEnumerator())
        .PassAs<FileSystemFileUtil::AbstractFileEnumerator>();
  }

  scoped_refptr<ReadDirectoryWorker> worker(new ReadDirectoryWorker(
      device_handle_, GetDeviceRelativePath(device_path_, root.value()),
      media_task_runner_, &on_task_completed_event_, &on_shutdown_event_));
  worker->Run();

  if (worker->get_file_entries().empty()) {
    return make_scoped_ptr(new FileSystemFileUtil::EmptyFileEnumerator())
        .PassAs<FileSystemFileUtil::AbstractFileEnumerator>();
  }

  if (recursive) {
    return make_scoped_ptr(new RecursiveMediaFileEnumerator(
        device_handle_, media_task_runner_, worker->get_file_entries(),
        &on_task_completed_event_, &on_shutdown_event_))
        .PassAs<FileSystemFileUtil::AbstractFileEnumerator>();
  }
  return make_scoped_ptr(new MediaFileEnumerator(worker->get_file_entries()))
      .PassAs<FileSystemFileUtil::AbstractFileEnumerator>();
}

PlatformFileError MTPDeviceDelegateImplLinux::CreateSnapshotFile(
    const FilePath& device_file_path,
    const FilePath& local_path,
    PlatformFileInfo* file_info) {
  if (!LazyInit())
    return base::PLATFORM_FILE_ERROR_FAILED;

  PlatformFileError error = GetFileInfo(device_file_path, file_info);
  if (error != base::PLATFORM_FILE_OK)
    return error;

  if (file_info->size <= 0 || file_info->size > kuint32max)
    return base::PLATFORM_FILE_ERROR_FAILED;

  scoped_refptr<ReadFileWorker> worker(new ReadFileWorker(
      device_handle_,
      GetDeviceRelativePath(device_path_, device_file_path.value()),
      file_info->size,
      media_task_runner_, &on_task_completed_event_, &on_shutdown_event_));
  worker->Run();

  const std::string& file_data = worker->data();
  int data_size = static_cast<int>(file_data.length());
  if (file_data.empty() ||
      file_util::WriteFile(local_path, file_data.c_str(),
                           data_size) != data_size) {
    return base::PLATFORM_FILE_ERROR_FAILED;
  }

  // Modify the last modified time to null. This prevents the time stamp
  // verfication in LocalFileStreamReader.
  file_info->last_modified = base::Time();
  return error;
}

SequencedTaskRunner* MTPDeviceDelegateImplLinux::GetMediaTaskRunner() {
  return media_task_runner_.get();
}

void MTPDeviceDelegateImplLinux::DeleteOnCorrectThread() const {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::DeleteSoon(BrowserThread::UI, FROM_HERE, this);
    return;
  }
  delete this;
}

void MTPDeviceDelegateImplLinux::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_APP_TERMINATING, type);
  on_shutdown_event_.Signal();
  on_task_completed_event_.Signal();
}

bool MTPDeviceDelegateImplLinux::LazyInit() {
  DCHECK(media_task_runner_);
  DCHECK(media_task_runner_->RunsTasksOnCurrentThread());

  if (!device_handle_.empty())
    return true;  // Already successfully initialized.

  std::string storage_name;
  RemoveChars(device_path_, kRootPath, &storage_name);
  DCHECK(!storage_name.empty());
  scoped_refptr<OpenStorageWorker> worker(new OpenStorageWorker(
      storage_name, media_task_runner_, &on_task_completed_event_,
      &on_shutdown_event_));
  worker->Run();
  device_handle_ = worker->device_handle();
  return !device_handle_.empty();
}

}  // namespace chrome
