// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_H_
#define CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_H_

#include <string>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/time.h"
#include "base/timer.h"
#include "content/browser/download/download_net_log_parameters.h"
#include "content/browser/download/download_request_handle.h"
#include "content/common/content_export.h"
#include "content/public/browser/download_destination_observer.h"
#include "content/public/browser/download_id.h"
#include "content/public/browser/download_item.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"

namespace content {
class DownloadFile;
class DownloadItemImplDelegate;

// See download_item.h for usage.
class CONTENT_EXPORT DownloadItemImpl
    : public DownloadItem,
      public DownloadDestinationObserver {
 public:
  // Note that it is the responsibility of the caller to ensure that a
  // DownloadItemImplDelegate passed to a DownloadItemImpl constructor
  // outlives the DownloadItemImpl.

  // Constructing from persistent store:
  // |bound_net_log| is constructed externally for our use.
  DownloadItemImpl(DownloadItemImplDelegate* delegate,
                   DownloadId download_id,
                   const FilePath& path,
                   const GURL& url,
                   const GURL& referrer_url,
                   const base::Time& start_time,
                   const base::Time& end_time,
                   int64 received_bytes,
                   int64 total_bytes,
                   DownloadItem::DownloadState state,
                   bool opened,
                   const net::BoundNetLog& bound_net_log);

  // Constructing for a regular download.
  // |bound_net_log| is constructed externally for our use.
  DownloadItemImpl(DownloadItemImplDelegate* delegate,
                   const DownloadCreateInfo& info,
                   scoped_ptr<DownloadRequestHandleInterface> request_handle,
                   const net::BoundNetLog& bound_net_log);

  // Constructing for the "Save Page As..." feature:
  // |bound_net_log| is constructed externally for our use.
  DownloadItemImpl(DownloadItemImplDelegate* delegate,
                   const FilePath& path,
                   const GURL& url,
                   DownloadId download_id,
                   const std::string& mime_type,
                   const net::BoundNetLog& bound_net_log);

  virtual ~DownloadItemImpl();

  // DownloadItem
  virtual void AddObserver(DownloadItem::Observer* observer) OVERRIDE;
  virtual void RemoveObserver(DownloadItem::Observer* observer) OVERRIDE;
  virtual void UpdateObservers() OVERRIDE;
  virtual void DangerousDownloadValidated() OVERRIDE;
  virtual void TogglePause() OVERRIDE;
  virtual void Cancel(bool user_cancel) OVERRIDE;
  virtual void Delete(DeleteReason reason) OVERRIDE;
  virtual void Remove() OVERRIDE;
  virtual void OpenDownload() OVERRIDE;
  virtual void ShowDownloadInShell() OVERRIDE;
  virtual int32 GetId() const OVERRIDE;
  virtual DownloadId GetGlobalId() const OVERRIDE;
  virtual DownloadState GetState() const OVERRIDE;
  virtual DownloadInterruptReason GetLastReason() const OVERRIDE;
  virtual bool IsPaused() const OVERRIDE;
  virtual bool IsTemporary() const OVERRIDE;
  virtual bool IsPartialDownload() const OVERRIDE;
  virtual bool IsInProgress() const OVERRIDE;
  virtual bool IsCancelled() const OVERRIDE;
  virtual bool IsInterrupted() const OVERRIDE;
  virtual bool IsComplete() const OVERRIDE;
  virtual const GURL& GetURL() const OVERRIDE;
  virtual const std::vector<GURL>& GetUrlChain() const OVERRIDE;
  virtual const GURL& GetOriginalUrl() const OVERRIDE;
  virtual const GURL& GetReferrerUrl() const OVERRIDE;
  virtual std::string GetSuggestedFilename() const OVERRIDE;
  virtual std::string GetContentDisposition() const OVERRIDE;
  virtual std::string GetMimeType() const OVERRIDE;
  virtual std::string GetOriginalMimeType() const OVERRIDE;
  virtual std::string GetRemoteAddress() const OVERRIDE;
  virtual bool HasUserGesture() const OVERRIDE;
  virtual PageTransition GetTransitionType() const OVERRIDE;
  virtual const std::string& GetLastModifiedTime() const OVERRIDE;
  virtual const std::string& GetETag() const OVERRIDE;
  virtual bool IsSavePackageDownload() const OVERRIDE;
  virtual const FilePath& GetFullPath() const OVERRIDE;
  virtual const FilePath& GetTargetFilePath() const OVERRIDE;
  virtual const FilePath& GetForcedFilePath() const OVERRIDE;
  virtual FilePath GetUserVerifiedFilePath() const OVERRIDE;
  virtual FilePath GetFileNameToReportUser() const OVERRIDE;
  virtual TargetDisposition GetTargetDisposition() const OVERRIDE;
  virtual const std::string& GetHash() const OVERRIDE;
  virtual const std::string& GetHashState() const OVERRIDE;
  virtual bool GetFileExternallyRemoved() const OVERRIDE;
  virtual SafetyState GetSafetyState() const OVERRIDE;
  virtual bool IsDangerous() const OVERRIDE;
  virtual DownloadDangerType GetDangerType() const OVERRIDE;
  virtual bool TimeRemaining(base::TimeDelta* remaining) const OVERRIDE;
  virtual int64 CurrentSpeed() const OVERRIDE;
  virtual int PercentComplete() const OVERRIDE;
  virtual bool AllDataSaved() const OVERRIDE;
  virtual int64 GetTotalBytes() const OVERRIDE;
  virtual int64 GetReceivedBytes() const OVERRIDE;
  virtual base::Time GetStartTime() const OVERRIDE;
  virtual base::Time GetEndTime() const OVERRIDE;
  virtual bool CanShowInFolder() OVERRIDE;
  virtual bool CanOpenDownload() OVERRIDE;
  virtual bool ShouldOpenFileBasedOnExtension() OVERRIDE;
  virtual bool GetOpenWhenComplete() const OVERRIDE;
  virtual bool GetAutoOpened() OVERRIDE;
  virtual bool GetOpened() const OVERRIDE;
  virtual BrowserContext* GetBrowserContext() const OVERRIDE;
  virtual WebContents* GetWebContents() const OVERRIDE;
  virtual void OnContentCheckCompleted(DownloadDangerType danger_type) OVERRIDE;
  virtual void SetOpenWhenComplete(bool open) OVERRIDE;
  virtual void SetIsTemporary(bool temporary) OVERRIDE;
  virtual void SetOpened(bool opened) OVERRIDE;
  virtual void SetDisplayName(const FilePath& name) OVERRIDE;
  virtual std::string DebugString(bool verbose) const OVERRIDE;
  virtual void MockDownloadOpenForTesting() OVERRIDE;

  // All remaining public interfaces virtual to allow for DownloadItemImpl
  // mocks.

  // Main entry points for regular downloads, in order -------------------------

  // TODO(rdsmith): Fold the process that uses these fully into
  // DownloadItemImpl and pass callbacks to the delegate so that all of
  // these other than Start() can be made private.

  // Start the download
  virtual void Start(scoped_ptr<DownloadFile> download_file);

  // If all pre-requisites have been met, complete download processing, i.e. do
  // internal cleanup, file rename, and potentially auto-open.  (Dangerous
  // downloads still may block on user acceptance after this point.)
  virtual void MaybeCompleteDownload();

  // Needed because of interwining with DownloadManagerImpl --------------------

  // TODO(rdsmith): Unwind DownloadManagerImpl and DownloadItemImpl,
  // removing these from the public interface.

  // Notify observers that this item is being removed by the user.
  virtual void NotifyRemoved();

  virtual void OnDownloadedFileRemoved();

  // Provide a weak pointer reference to a DownloadDestinationObserver
  // for use by download destinations.
  base::WeakPtr<DownloadDestinationObserver> DestinationObserverAsWeakPtr();

  // For dispatching on whether we're dealing with a SavePackage download.

  // DownloadItemImpl routines only needed by SavePackage ----------------------

  // Called by SavePackage to set the total number of bytes on the item.
  virtual void SetTotalBytes(int64 total_bytes);

  // Indicate progress in saving data to its destination.
  // |bytes_so_far| is the number of bytes received so far.
  // |hash_state| is the current hash state.
  virtual void UpdateProgress(int64 bytes_so_far,
                              int64 bytes_per_sec,
                              const std::string& hash_state);

  virtual void OnAllDataSaved(const std::string& final_hash);

  // Called by SavePackage to display progress when the DownloadItem
  // should be considered complete.
  virtual void MarkAsComplete();

 private:
  // Fine grained states of a download.
  enum DownloadInternalState {
    // Unless otherwise specified, state transitions are linear forward
    // in this list.

    // Includes both before and after file name determination.
    // TODO(rdsmith): Put in state variable for file name determination.
    IN_PROGRESS_INTERNAL,

    // Between commit point (dispatch of download file release) and completed.
    // Embedder may be opening the file in this state.  Note that the
    // DownloadItem may be deleted (by shutdown) or interrupted (e.g. due to a
    // failure during AnnotateWithSourceInformation()) in this state.
    COMPLETING_INTERNAL,

    // After embedder has had a chance to auto-open.  User may now open
    // or auto-open based on extension.
    COMPLETE_INTERNAL,

    // User has cancelled the download.
    // Only incoming transition IN_PROGRESS->
    CANCELLED_INTERNAL,

    // An error has interrupted the download.
    // Only incoming transition IN_PROGRESS->
    INTERRUPTED_INTERNAL,

    MAX_DOWNLOAD_INTERNAL_STATE,
  };

  // DownloadDestinationObserver
  virtual void DestinationUpdate(int64 bytes_so_far,
                                 int64 bytes_per_sec,
                                 const std::string& hash_state) OVERRIDE;
  virtual void DestinationError(DownloadInterruptReason reason) OVERRIDE;
  virtual void DestinationCompleted(const std::string& final_hash) OVERRIDE;

  // Normal progression of a download ------------------------------------------

  // These are listed in approximately chronological order.  There are also
  // public methods involved in normal download progression; see
  // the implementation ordering in download_item_impl.cc.

  // Construction common to all constructors. |active| should be true for new
  // downloads and false for downloads from the history.
  // |download_type| indicates to the net log system what kind of download
  // this is.
  void Init(bool active, DownloadType download_type);

  // Called when the target path has been determined. |target_path| is the
  // suggested target path. |disposition| indicates how the target path should
  // be used (see TargetDisposition). |danger_type| is the danger level of
  // |target_path| as determined by the caller. |intermediate_path| is the path
  // to use to store the download until OnDownloadCompleting() is called.
  virtual void OnDownloadTargetDetermined(
      const FilePath& target_path,
      TargetDisposition disposition,
      DownloadDangerType danger_type,
      const FilePath& intermediate_path);

  // Callback from file thread when we initialize the DownloadFile.
  void OnDownloadFileInitialized(DownloadInterruptReason result);

  void OnDownloadRenamedToIntermediateName(
      DownloadInterruptReason reason, const FilePath& full_path);

  // Called when the download is ready to complete.
  // This may perform final rename if necessary and will eventually call
  // DownloadItem::Completed().
  virtual void OnDownloadCompleting();

  // Called after the delegate has given the go-ahead to actually complete
  // the download.
  void ReadyForDownloadCompletionDone();

  void OnDownloadRenamedToFinalName(DownloadInterruptReason reason,
                                    const FilePath& full_path);

  // Called if the embedder took over opening a download, to indicate that
  // the download has been opened.
  virtual void DelayedDownloadOpened(bool auto_opened);

  // Called when the entire download operation (including renaming etc)
  // is completed.
  void Completed();

  // Helper routines -----------------------------------------------------------

  // Indicate that an error has occurred on the download.
  virtual void Interrupt(DownloadInterruptReason reason);

  // Cancel the DownloadFile if we have it.
  void CancelDownloadFile();

  // Check if a download is ready for completion.
  bool IsDownloadReadyForCompletion();

  // Call to transition state; all state transitions should go through this.
  void TransitionTo(DownloadInternalState new_state);

  // Set the |danger_type_| and invoke obserers if necessary.
  void SetDangerType(DownloadDangerType danger_type);

  void SetFullPath(const FilePath& new_path);

  // Mapping between internal and external states.
  static DownloadState InternalToExternalState(
      DownloadInternalState internal_state);
  static DownloadInternalState ExternalToInternalState(
      DownloadState external_state);

  // Debugging routines --------------------------------------------------------
  static const char* DebugDownloadStateString(DownloadInternalState state);

  // Will be false for save package downloads retrieved from the history.
  // TODO(rdsmith): Replace with a generalized enum for "download source".
  const bool is_save_package_download_;

  // The handle to the request information.  Used for operations outside the
  // download system.
  scoped_ptr<DownloadRequestHandleInterface> request_handle_;

  // Download ID assigned by DownloadResourceHandler.
  DownloadId download_id_;

  // Display name for the download. If this is empty, then the display name is
  // considered to be |target_path_.BaseName()|.
  FilePath display_name_;

  // Full path to the downloaded or downloading file. This is the path to the
  // physical file, if one exists. The final target path is specified by
  // |target_path_|. |current_path_| can be empty if the in-progress path hasn't
  // been determined.
  FilePath current_path_;

  // Target path of an in-progress download. We may be downloading to a
  // temporary or intermediate file (specified by |current_path_|.  Once the
  // download completes, we will rename the file to |target_path_|.
  FilePath target_path_;

  // Whether the target should be overwritten, uniquified or prompted for.
  TargetDisposition target_disposition_;

  // The chain of redirects that leading up to and including the final URL.
  std::vector<GURL> url_chain_;

  // The URL of the page that initiated the download.
  GURL referrer_url_;

  // Filename suggestion from DownloadSaveInfo. It could, among others, be the
  // suggested filename in 'download' attribute of an anchor. Details:
  // http://www.whatwg.org/specs/web-apps/current-work/#downloading-hyperlinks
  std::string suggested_filename_;

  // If non-empty, contains an externally supplied path that should be used as
  // the target path.
  FilePath forced_file_path_;

  // Page transition that triggerred the download.
  PageTransition transition_type_;

  // Whether the download was triggered with a user gesture.
  bool has_user_gesture_;

  // Information from the request.
  // Content-disposition field from the header.
  std::string content_disposition_;

  // Mime-type from the header.  Subject to change.
  std::string mime_type_;

  // The value of the content type header sent with the downloaded item.  It
  // may be different from |mime_type_|, which may be set based on heuristics
  // which may look at the file extension and first few bytes of the file.
  std::string original_mime_type_;

  // The remote IP address where the download was fetched from.  Copied from
  // DownloadCreateInfo::remote_address.
  std::string remote_address_;

  // Total bytes expected.
  int64 total_bytes_;

  // Current received bytes.
  int64 received_bytes_;

  // Current speed. Calculated by the DownloadFile.
  int64 bytes_per_sec_;

  // Sha256 hash of the content.  This might be empty either because
  // the download isn't done yet or because the hash isn't needed
  // (ChromeDownloadManagerDelegate::GenerateFileHash() returned false).
  std::string hash_;

  // A blob containing the state of the hash algorithm.  Only valid while the
  // download is in progress.
  std::string hash_state_;

  // Server's time stamp for the file.
  std::string last_modified_time_;

  // Server's ETAG for the file.
  std::string etag_;

  // Last reason.
  DownloadInterruptReason last_reason_;

  // Start time for recording statistics.
  base::TimeTicks start_tick_;

  // The current state of this download.
  DownloadInternalState state_;

  // Current danger type for the download.
  DownloadDangerType danger_type_;

  // The views of this item in the download shelf and download contents.
  ObserverList<Observer> observers_;

  // Time the download was started.
  base::Time start_time_;

  // Time the download completed.
  base::Time end_time_;

  // Our delegate.
  DownloadItemImplDelegate* delegate_;

  // In progress downloads may be paused by the user, we note it here.
  bool is_paused_;

  // A flag for indicating if the download should be opened at completion.
  bool open_when_complete_;

  // A flag for indicating if the downloaded file is externally removed.
  bool file_externally_removed_;

  // Indicates if the download is considered potentially safe or dangerous
  // (executable files are typically considered dangerous).
  SafetyState safety_state_;

  // True if the download was auto-opened. We set this rather than using
  // an observer as it's frequently possible for the download to be auto opened
  // before the observer is added.
  bool auto_opened_;

  // True if the item was downloaded temporarily.
  bool is_temporary_;

  // True if we've saved all the data for the download.
  bool all_data_saved_;

  // Did the user open the item either directly or indirectly (such as by
  // setting always open files of this type)? The shelf also sets this field
  // when the user closes the shelf before the item has been opened but should
  // be treated as though the user opened it.
  bool opened_;

  // Do we actually open downloads when requested?  For testing purposes only.
  bool open_enabled_;

  // Did the delegate delay calling Complete on this download?
  bool delegate_delayed_complete_;

  // DownloadFile associated with this download.  Note that this
  // pointer may only be used or destroyed on the FILE thread.
  // This pointer will be non-null only while the DownloadItem is in
  // the IN_PROGRESS state.
  scoped_ptr<DownloadFile> download_file_;

  // Net log to use for this download.
  const net::BoundNetLog bound_net_log_;

  base::WeakPtrFactory<DownloadItemImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DownloadItemImpl);
};

}  // namespace content

#endif  // CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_H_
