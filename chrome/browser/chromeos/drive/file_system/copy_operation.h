// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_COPY_OPERATION_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_COPY_OPERATION_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/drive/drive_resource_metadata.h"
#include "chrome/browser/google_apis/gdata_errorcode.h"

class FilePath;
class GURL;

namespace base {
class Value;
}

namespace google_apis {
class DriveServiceInterface;
class DriveUploaderInterface;
}

namespace drive {

class DriveEntryProto;
class DriveFileSystemInterface;

namespace file_system {

class OperationObserver;

// This class encapsulates the drive Copy function.  It is resposible for
// sending the request to the drive API, then updating the local state and
// metadata to reflect the new state.
class CopyOperation {
 public:
  CopyOperation(google_apis::DriveServiceInterface* drive_service,
                DriveFileSystemInterface* drive_file_system,
                DriveResourceMetadata* metadata,
                google_apis::DriveUploaderInterface* uploader,
                scoped_refptr<base::SequencedTaskRunner> blocking_task_runner,
                OperationObserver* observer);
  virtual ~CopyOperation();

  // Performs the copy operation on the file at drive path |src_file_path|
  // with a target of |dest_file_path|. Invokes |callback| when finished with
  // the result of the operation.
  virtual void Copy(const FilePath& src_file_path,
                    const FilePath& dest_file_path,
                    const FileOperationCallback& callback);

  // Initiates transfer of |remote_src_file_path| to |local_dest_file_path|.
  // |remote_src_file_path| is the virtual source path on the Drive file system.
  // |local_dest_file_path| is the destination path on the local file system.
  //
  // Must be called from *UI* thread. |callback| is run on the calling thread.
  // |callback| must not be null.
  virtual void TransferFileFromRemoteToLocal(
      const FilePath& remote_src_file_path,
      const FilePath& local_dest_file_path,
      const FileOperationCallback& callback);

  // Initiates transfer of |local_src_file_path| to |remote_dest_file_path|.
  // |local_src_file_path| must be a file from the local file system.
  // |remote_dest_file_path| is the virtual destination path within Drive file
  // system.
  //
  // Must be called from *UI* thread. |callback| is run on the calling thread.
  // |callback| must not be null.
  virtual void TransferFileFromLocalToRemote(
      const FilePath& local_src_file_path,
      const FilePath& remote_dest_file_path,
      const FileOperationCallback& callback);

  // Initiates transfer of |local_file_path| to |remote_dest_file_path|.
  // |local_file_path| must be a regular file (i.e. not a hosted document) from
  // the local file system, |remote_dest_file_path| is the virtual destination
  // path within Drive file system.
  //
  // Must be called from *UI* thread. |callback| is run on the calling thread.
  virtual void TransferRegularFile(const FilePath& local_file_path,
                                   const FilePath& remote_dest_file_path,
                                   const FileOperationCallback& callback);

 private:
  // Struct used for StartFileUpload().
  struct StartFileUploadParams;

  // Invoked upon completion of GetFileByPath initiated by
  // TransferFileFromRemoteToLocal. If GetFileByPath reports no error, calls
  // CopyLocalFileOnBlockingPool to copy |local_file_path| to
  // |local_dest_file_path|.
  //
  // Can be called from UI thread. |callback| is run on the calling thread.
  // |callback| must not be null.
  void OnGetFileCompleteForTransferFile(const FilePath& local_dest_file_path,
                                        const FileOperationCallback& callback,
                                        DriveFileError error,
                                        const FilePath& local_file_path,
                                        const std::string& unused_mime_type,
                                        DriveFileType file_type);

  // Copies a document with |resource_id| to the directory at |dir_path|
  // and names the copied document as |new_name|.
  //
  // Can be called from UI thread. |callback| is run on the calling thread.
  // |callback| must not be null.
  void CopyDocumentToDirectory(const FilePath& dir_path,
                               const std::string& resource_id,
                               const FilePath::StringType& new_name,
                               const FileOperationCallback& callback);

  // Callback for handling document copy attempt.
  // |callback| must not be null.
  void OnCopyDocumentCompleted(const FilePath& dir_path,
                               const FileOperationCallback& callback,
                               google_apis::GDataErrorCode status,
                               scoped_ptr<base::Value> data);

  // Moves a file or directory at |file_path| in the root directory to
  // another directory at |dir_path|. This function does nothing if
  // |dir_path| points to the root directory.
  //
  // Can be called from UI thread. |callback| is run on the calling thread.
  // |callback| must not be null.
  void MoveEntryFromRootDirectory(const FilePath& directory_path,
                                  const FileOperationCallback& callback,
                                  DriveFileError error,
                                  const FilePath& file_path);

  // Part of MoveEntryFromRootDirectory(). Called after
  // GetEntryInfoPairByPaths() is complete. |callback| must not be null.
  void MoveEntryFromRootDirectoryAfterGetEntryInfoPair(
      const FileOperationCallback& callback,
      scoped_ptr<EntryInfoPairResult> result);

  // Moves entry specified by |file_path| to the directory specified by
  // |dir_path| and calls |callback| asynchronously.
  // |callback| must not be null.
  void MoveEntryToDirectory(const FilePath& file_path,
                            const FilePath& directory_path,
                            const FileMoveCallback& callback,
                            google_apis::GDataErrorCode status,
                            const GURL& /* document_url */);

  // Callback when an entry is moved to another directory on the client side.
  // Notifies the directory change and runs |callback|.
  // |callback| must not be null.
  void NotifyAndRunFileOperationCallback(
      const FileOperationCallback& callback,
      DriveFileError error,
      const FilePath& moved_file_path);

  // Part of Copy(). Called after GetEntryInfoPairByPaths() is
  // complete. |callback| must not be null.
  void CopyAfterGetEntryInfoPair(const FilePath& dest_file_path,
                                 const FileOperationCallback& callback,
                                 scoped_ptr<EntryInfoPairResult> result);

  // Invoked upon completion of GetFileByPath initiated by Copy. If
  // GetFileByPath reports no error, calls TransferRegularFile to transfer
  // |local_file_path| to |remote_dest_file_path|.
  //
  // Can be called from UI thread. |callback| is run on the calling thread.
  void OnGetFileCompleteForCopy(const FilePath& remote_dest_file_path,
                                const FileOperationCallback& callback,
                                DriveFileError error,
                                const FilePath& local_file_path,
                                const std::string& unused_mime_type,
                                DriveFileType file_type);

  // Kicks off file upload once it receives |file_size| and |content_type|.
  void StartFileUpload(const StartFileUploadParams& params,
                       int64* file_size,
                       std::string* content_type,
                       DriveFileError error);

  // Part of StartFileUpload(). Called after GetEntryInfoByPath()
  // is complete.
  void StartFileUploadAfterGetEntryInfo(
      const StartFileUploadParams& params,
      int64 file_size,
      const std::string& content_type,
      DriveFileError error,
      scoped_ptr<DriveEntryProto> entry_proto);

  // Helper function that completes bookkeeping tasks related to
  // completed file transfer.
  void OnTransferCompleted(
      const FileOperationCallback& callback,
      google_apis::DriveUploadError error,
      const FilePath& drive_path,
      const FilePath& file_path,
      scoped_ptr<google_apis::DocumentEntry> document_entry);

  // Part of TransferFileFromLocalToRemote(). Called after
  // GetEntryInfoByPath() is complete.
  void TransferFileFromLocalToRemoteAfterGetEntryInfo(
      const FilePath& local_src_file_path,
      const FilePath& remote_dest_file_path,
      const FileOperationCallback& callback,
      DriveFileError error,
      scoped_ptr<DriveEntryProto> entry_proto);

  // Initiates transfer of |local_file_path| with |resource_id| to
  // |remote_dest_file_path|. |local_file_path| must be a file from the local
  // file system, |remote_dest_file_path| is the virtual destination path within
  // Drive file system. If |resource_id| is a non-empty string, the transfer is
  // handled by CopyDocumentToDirectory. Otherwise, the transfer is handled by
  // TransferRegularFile.
  //
  // Must be called from *UI* thread. |callback| is run on the calling thread.
  // |callback| must not be null.
  void TransferFileForResourceId(
      const FilePath& local_file_path,
      const FilePath& remote_dest_file_path,
      const FileOperationCallback& callback,
      std::string* resource_id);

  google_apis::DriveServiceInterface* drive_service_;
  DriveFileSystemInterface* drive_file_system_;
  DriveResourceMetadata* metadata_;
  google_apis::DriveUploaderInterface* uploader_;
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  OperationObserver* observer_;

  // WeakPtrFactory bound to the UI thread.
  // Note: This should remain the last member so it'll be destroyed and
  // invalidate the weak pointers before any other members are destroyed.
  base::WeakPtrFactory<CopyOperation> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(CopyOperation);
};

}  // namespace file_system
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_COPY_OPERATION_H_
