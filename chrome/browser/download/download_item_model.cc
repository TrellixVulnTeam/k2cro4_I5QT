// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_item_model.h"

#include "base/i18n/number_formatting.h"
#include "base/i18n/rtl.h"
#include "base/string16.h"
#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/time.h"
#include "chrome/browser/download/download_crx_util.h"
#include "chrome/common/time_format.h"
#include "content/public/browser/download_danger_type.h"
#include "content/public/browser/download_interrupt_reasons.h"
#include "content/public/browser/download_item.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/base/text/text_elider.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/drive/drive_download_observer.h"
#endif

using base::TimeDelta;
using content::DownloadItem;

namespace {

string16 InterruptReasonStatusMessage(int reason) {
  int string_id = 0;

  switch (reason) {
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_ACCESS_DENIED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_DISK_FULL;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_PATH_TOO_LONG;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_FILE_TOO_LARGE;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_VIRUS;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TRANSIENT_ERROR:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_TEMPORARY_PROBLEM;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_BLOCKED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_SECURITY_CHECK_FAILED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_NETWORK_ERROR;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_NETWORK_TIMEOUT;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_NETWORK_DISCONNECTED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_SERVER_DOWN;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_SERVER_PROBLEM;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_NO_FILE;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED:
      string_id = IDS_DOWNLOAD_STATUS_CANCELLED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN:
    case content::DOWNLOAD_INTERRUPT_REASON_CRASH:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_SHUTDOWN;
      break;
    default:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS;
      break;
  }

  return l10n_util::GetStringUTF16(string_id);
}

string16 InterruptReasonMessage(int reason) {
  int string_id = 0;
  string16 status_text;

  switch (reason) {
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_ACCESS_DENIED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_DISK_FULL;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_PATH_TOO_LONG;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_FILE_TOO_LARGE;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_VIRUS;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TRANSIENT_ERROR:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_TEMPORARY_PROBLEM;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_BLOCKED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_SECURITY_CHECK_FAILED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_NETWORK_ERROR;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_NETWORK_TIMEOUT;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_NETWORK_DISCONNECTED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_SERVER_DOWN;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_SERVER_PROBLEM;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_NO_FILE;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED:
      string_id = IDS_DOWNLOAD_STATUS_CANCELLED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN:
    case content::DOWNLOAD_INTERRUPT_REASON_CRASH:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_SHUTDOWN;
      break;
    default:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS;
      break;
  }

  status_text = l10n_util::GetStringUTF16(string_id);

  return status_text;
}

} // namespace

// -----------------------------------------------------------------------------
// DownloadItemModel

DownloadItemModel::DownloadItemModel(DownloadItem* download)
    : download_(download) {
}

DownloadItemModel::~DownloadItemModel() {
}

void DownloadItemModel::CancelTask() {
  download_->Cancel(true /* update history service */);
}

string16 DownloadItemModel::GetStatusText() const {
  string16 status_text;
  switch (download_->GetState()) {
    case DownloadItem::IN_PROGRESS:
      status_text = GetInProgressStatusString();
      break;
    case DownloadItem::COMPLETE:
      if (download_->GetFileExternallyRemoved()) {
        status_text = l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_REMOVED);
      } else {
        status_text.clear();
      }
      break;
    case DownloadItem::CANCELLED:
      status_text = l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_CANCELLED);
      break;
    case DownloadItem::INTERRUPTED: {
      content::DownloadInterruptReason reason = download_->GetLastReason();
      if (reason != content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED) {
        string16 interrupt_reason = InterruptReasonStatusMessage(reason);
        string16 size_ratio = GetProgressSizesString();
        status_text = l10n_util::GetStringFUTF16(
            IDS_DOWNLOAD_STATUS_INTERRUPTED, size_ratio, interrupt_reason);
      } else {
        // Same as DownloadItem::CANCELLED.
        status_text = l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_CANCELLED);
      }
      break;
    }
    default:
      NOTREACHED();
  }

  return status_text;
}

string16 DownloadItemModel::GetInterruptReasonText() const {
  if (download_->GetState() != DownloadItem::INTERRUPTED ||
      download_->GetLastReason() ==
      content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED) {
    return string16();
  }
  return InterruptReasonMessage(download_->GetLastReason());
}

string16 DownloadItemModel::GetTooltipText(const gfx::Font& font,
                                           int max_width) const {
  string16 tooltip = ui::ElideFilename(
      download_->GetFileNameToReportUser(), font, max_width);
  content::DownloadInterruptReason reason = download_->GetLastReason();
  if (download_->GetState() == DownloadItem::INTERRUPTED &&
      reason != content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED) {
    tooltip += ASCIIToUTF16("\n");
    tooltip += ui::ElideText(InterruptReasonStatusMessage(reason),
                             font, max_width, ui::ELIDE_AT_END);
  }
  return tooltip;
}

// TODO(asanka,rdsmith): Once 'open' moves exclusively to the
//     ChromeDownloadManagerDelegate, we should calculate the percentage here
//     instead of calling into the DownloadItem or Drive.
int DownloadItemModel::PercentComplete() const {
#if defined(OS_CHROMEOS)
  // For Drive uploads, progress is based on the number of bytes
  // uploaded. Progress is unknown until the upload starts.
  if (IsDriveDownload())
    return drive::DriveDownloadObserver::PercentComplete(download_);
#endif
  return download_->PercentComplete();
}

string16 DownloadItemModel::GetWarningText(const gfx::Font& font,
                                           int base_width) const {
  // Should only be called if IsDangerous().
  DCHECK(IsDangerous());
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
      return l10n_util::GetStringUTF16(IDS_PROMPT_MALICIOUS_DOWNLOAD_URL);

    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE:
      if (download_crx_util::IsExtensionDownload(*download_)) {
        return l10n_util::GetStringUTF16(
            IDS_PROMPT_DANGEROUS_DOWNLOAD_EXTENSION);
      } else {
        return l10n_util::GetStringFUTF16(
            IDS_PROMPT_DANGEROUS_DOWNLOAD,
            ui::ElideFilename(download_->GetFileNameToReportUser(),
                              font, base_width));
      }

    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      return l10n_util::GetStringFUTF16(
          IDS_PROMPT_MALICIOUS_DOWNLOAD_CONTENT,
          ui::ElideFilename(download_->GetFileNameToReportUser(),
                            font, base_width));

    case content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT:
      return l10n_util::GetStringFUTF16(
          IDS_PROMPT_UNCOMMON_DOWNLOAD_CONTENT,
          ui::ElideFilename(download_->GetFileNameToReportUser(),
                            font, base_width));

    case content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS:
    case content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_MAX:
      NOTREACHED();
  }
  return string16();
}

string16 DownloadItemModel::GetWarningConfirmButtonText() const {
  // Should only be called if IsDangerous()
  DCHECK(IsDangerous());
  if (download_->GetDangerType() ==
          content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE &&
      download_crx_util::IsExtensionDownload(*download_)) {
    return l10n_util::GetStringUTF16(IDS_CONTINUE_EXTENSION_DOWNLOAD);
  } else {
    return l10n_util::GetStringUTF16(IDS_CONFIRM_DOWNLOAD);
  }
}

bool DownloadItemModel::IsMalicious() const {
  if (!IsDangerous())
    return false;
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT:
      return true;

    case content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS:
    case content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_MAX:
      // We shouldn't get any of these due to the IsDangerous() test above.
      NOTREACHED();
      // Fallthrough.
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE:
      return false;
  }
  NOTREACHED();
  return false;
}

bool DownloadItemModel::IsDangerous() const {
  return download_->GetSafetyState() == DownloadItem::DANGEROUS;
}

int64 DownloadItemModel::GetTotalBytes() const {
  return download_->AllDataSaved() ? download_->GetReceivedBytes() :
                                     download_->GetTotalBytes();
}

int64 DownloadItemModel::GetCompletedBytes() const {
#if defined(OS_CHROMEOS)
  // For Drive downloads, the size is the count of bytes uploaded.
  if (IsDriveDownload())
    return drive::DriveDownloadObserver::GetUploadedBytes(download_);
#endif
  return download_->GetReceivedBytes();
}

bool DownloadItemModel::IsDriveDownload() const {
#if defined(OS_CHROMEOS)
  return drive::DriveDownloadObserver::IsDriveDownload(download_);
#else
  return false;
#endif
}

string16 DownloadItemModel::GetProgressSizesString() const {
  string16 size_ratio;
  int64 size = GetCompletedBytes();
  int64 total = GetTotalBytes();
  if (total > 0) {
    ui::DataUnits amount_units = ui::GetByteDisplayUnits(total);
    string16 simple_size = ui::FormatBytesWithUnits(size, amount_units, false);

    // In RTL locales, we render the text "size/total" in an RTL context. This
    // is problematic since a string such as "123/456 MB" is displayed
    // as "MB 123/456" because it ends with an LTR run. In order to solve this,
    // we mark the total string as an LTR string if the UI layout is
    // right-to-left so that the string "456 MB" is treated as an LTR run.
    string16 simple_total = base::i18n::GetDisplayStringInLTRDirectionality(
        ui::FormatBytesWithUnits(total, amount_units, true));
    size_ratio = l10n_util::GetStringFUTF16(IDS_DOWNLOAD_STATUS_SIZES,
                                            simple_size, simple_total);
  } else {
    size_ratio = ui::FormatBytes(size);
  }
  return size_ratio;
}

string16 DownloadItemModel::GetInProgressStatusString() const {
  DCHECK(download_->IsInProgress());

  TimeDelta time_remaining;
  // time_remaining is only known if the download isn't paused and is not a
  // Drive download.
  // TODO(asanka): Calculate a TimeRemaining() for Drive uploads.
  bool time_remaining_known = (!IsDriveDownload() && !download_->IsPaused() &&
                               download_->TimeRemaining(&time_remaining));

  // Indication of progress. (E.g.:"100/200 MB" or "100MB")
  string16 size_ratio = GetProgressSizesString();

  // The download is a CRX (app, extension, theme, ...) and it is being unpacked
  // and validated.
  if (download_->AllDataSaved() &&
      download_crx_util::IsExtensionDownload(*download_)) {
    return l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_CRX_INSTALL_RUNNING);
  }

  // A paused download: "100/120 MB, Paused"
  if (download_->IsPaused()) {
    return l10n_util::GetStringFUTF16(
        IDS_DOWNLOAD_STATUS_IN_PROGRESS, size_ratio,
        l10n_util::GetStringUTF16(IDS_DOWNLOAD_PROGRESS_PAUSED));
  }

  // A download scheduled to be opened when complete: "Opening in 10 secs"
  if (download_->GetOpenWhenComplete()) {
    if (!time_remaining_known)
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_OPEN_WHEN_COMPLETE);

    return l10n_util::GetStringFUTF16(
        IDS_DOWNLOAD_STATUS_OPEN_IN,
        TimeFormat::TimeRemainingShort(time_remaining));
  }

  // In progress download with known time left: "100/120 MB, 10 secs left"
  if (time_remaining_known) {
    return l10n_util::GetStringFUTF16(
        IDS_DOWNLOAD_STATUS_IN_PROGRESS, size_ratio,
        TimeFormat::TimeRemaining(time_remaining));
  }

  // In progress download with no known time left and non-zero completed bytes:
  // "100/120 MB" or "100 MB"
  if (GetCompletedBytes() > 0)
    return size_ratio;

#if defined(OS_CHROMEOS)
  // We haven't started the upload yet. The download needs to progress
  // further before we will see any upload progress. Show "Downloading..."
  // until we start uploading.
  if (IsDriveDownload())
    return l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_WAITING);
#endif

  // Instead of displaying "0 B" we say "Starting..."
  return l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_STARTING);
}
