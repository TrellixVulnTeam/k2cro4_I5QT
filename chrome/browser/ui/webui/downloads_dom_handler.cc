// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/downloads_dom_handler.h"

#include <algorithm>
#include <functional>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram.h"
#include "base/string_piece.h"
#include "base/threading/thread.h"
#include "base/utf_string_conversions.h"
#include "base/value_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_crx_util.h"
#include "chrome/browser/download/download_danger_prompt.h"
#include "chrome/browser/download/download_history.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_query.h"
#include "chrome/browser/download/download_service.h"
#include "chrome/browser/download/download_service_factory.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/fileicon_source.h"
#include "chrome/common/time_format.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "net/base/net_util.h"
#include "ui/gfx/image/image.h"

#if !defined(OS_MACOSX)
#include "content/public/browser/browser_thread.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/extensions/file_manager_util.h"
#endif

using content::BrowserContext;
using content::BrowserThread;
using content::UserMetricsAction;

namespace {

// Maximum number of downloads to show. TODO(glen): Remove this and instead
// stuff the downloads down the pipe slowly.
static const size_t kMaxDownloads = 150;

enum DownloadsDOMEvent {
  DOWNLOADS_DOM_EVENT_GET_DOWNLOADS = 0,
  DOWNLOADS_DOM_EVENT_OPEN_FILE = 1,
  DOWNLOADS_DOM_EVENT_DRAG = 2,
  DOWNLOADS_DOM_EVENT_SAVE_DANGEROUS = 3,
  DOWNLOADS_DOM_EVENT_DISCARD_DANGEROUS = 4,
  DOWNLOADS_DOM_EVENT_SHOW = 5,
  DOWNLOADS_DOM_EVENT_PAUSE = 6,
  DOWNLOADS_DOM_EVENT_REMOVE = 7,
  DOWNLOADS_DOM_EVENT_CANCEL = 8,
  DOWNLOADS_DOM_EVENT_CLEAR_ALL = 9,
  DOWNLOADS_DOM_EVENT_OPEN_FOLDER = 10,
  DOWNLOADS_DOM_EVENT_MAX
};

void CountDownloadsDOMEvents(DownloadsDOMEvent event) {
  UMA_HISTOGRAM_ENUMERATION("Download.DOMEvent",
                            event,
                            DOWNLOADS_DOM_EVENT_MAX);
}

// Returns a string constant to be used as the |danger_type| value in
// CreateDownloadItemValue().  Only return strings for DANGEROUS_FILE,
// DANGEROUS_URL, DANGEROUS_CONTENT, and UNCOMMON_CONTENT because the
// |danger_type| value is only defined if the value of |state| is |DANGEROUS|.
const char* GetDangerTypeString(content::DownloadDangerType danger_type) {
  switch (danger_type) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE:
      return "DANGEROUS_FILE";
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
      return "DANGEROUS_URL";
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      return "DANGEROUS_CONTENT";
    case content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT:
      return "UNCOMMON_CONTENT";
    default:
      // Don't return a danger type string if it is NOT_DANGEROUS or
      // MAYBE_DANGEROUS_CONTENT.
      NOTREACHED();
      return "";
  }
}

// Returns a JSON dictionary containing some of the attributes of |download|.
// The JSON dictionary will also have a field "id" set to |id|, and a field
// "otr" set to |incognito|.
DictionaryValue* CreateDownloadItemValue(
    content::DownloadItem* download_item,
    bool incognito) {
  // TODO(asanka): Move towards using download_model here for getting status and
  // progress. The difference currently only matters to Drive downloads and
  // those don't show up on the downloads page, but should.
  DownloadItemModel download_model(download_item);
  DictionaryValue* file_value = new DictionaryValue();

  file_value->SetInteger(
      "started", static_cast<int>(download_item->GetStartTime().ToTimeT()));
  file_value->SetString(
      "since_string", TimeFormat::RelativeDate(
          download_item->GetStartTime(), NULL));
  file_value->SetString(
      "date_string", base::TimeFormatShortDate(download_item->GetStartTime()));
  file_value->SetInteger("id", download_item->GetId());

  FilePath download_path(download_item->GetTargetFilePath());
  file_value->Set("file_path", base::CreateFilePathValue(download_path));
  file_value->SetString("file_url",
                        net::FilePathToFileURL(download_path).spec());

  // Keep file names as LTR.
  string16 file_name =
    download_item->GetFileNameToReportUser().LossyDisplayName();
  file_name = base::i18n::GetDisplayStringInLTRDirectionality(file_name);
  file_value->SetString("file_name", file_name);
  file_value->SetString("url", download_item->GetURL().spec());
  file_value->SetBoolean("otr", incognito);
  file_value->SetInteger("total", static_cast<int>(
      download_item->GetTotalBytes()));
  file_value->SetBoolean("file_externally_removed",
                         download_item->GetFileExternallyRemoved());

  if (download_item->IsInProgress()) {
    if (download_item->GetSafetyState() == content::DownloadItem::DANGEROUS) {
      file_value->SetString("state", "DANGEROUS");
      // These are the only danger states that the UI is equipped to handle.
      DCHECK(download_item->GetDangerType() ==
                 content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE ||
             download_item->GetDangerType() ==
                 content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL ||
             download_item->GetDangerType() ==
                 content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT ||
             download_item->GetDangerType() ==
                 content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT);
      const char* danger_type_value =
          GetDangerTypeString(download_item->GetDangerType());
      file_value->SetString("danger_type", danger_type_value);
    } else if (download_item->IsPaused()) {
      file_value->SetString("state", "PAUSED");
    } else {
      file_value->SetString("state", "IN_PROGRESS");
    }

    file_value->SetString("progress_status_text",
        download_util::GetProgressStatusText(download_item));

    file_value->SetInteger("percent",
        static_cast<int>(download_item->PercentComplete()));
    file_value->SetInteger("received",
        static_cast<int>(download_item->GetReceivedBytes()));
  } else if (download_item->IsInterrupted()) {
    file_value->SetString("state", "INTERRUPTED");

    file_value->SetString("progress_status_text",
        download_util::GetProgressStatusText(download_item));

    file_value->SetInteger("percent",
        static_cast<int>(download_item->PercentComplete()));
    file_value->SetInteger("received",
        static_cast<int>(download_item->GetReceivedBytes()));
    file_value->SetString("last_reason_text",
                          download_model.GetInterruptReasonText());
  } else if (download_item->IsCancelled()) {
    file_value->SetString("state", "CANCELLED");
  } else if (download_item->IsComplete()) {
    if (download_item->GetSafetyState() == content::DownloadItem::DANGEROUS)
      file_value->SetString("state", "DANGEROUS");
    else
      file_value->SetString("state", "COMPLETE");
  } else {
    NOTREACHED() << "state undefined";
  }

  return file_value;
}

// Filters out extension downloads and downloads that don't have a filename yet.
bool IsDownloadDisplayable(const content::DownloadItem& item) {
  return (!download_crx_util::IsExtensionDownload(item) &&
          !item.IsTemporary() &&
          !item.GetFileNameToReportUser().empty() &&
          !item.GetTargetFilePath().empty());
}

}  // namespace

DownloadsDOMHandler::DownloadsDOMHandler(content::DownloadManager* dlm)
    : search_text_(),
      ALLOW_THIS_IN_INITIALIZER_LIST(main_notifier_(dlm, this)),
      update_scheduled_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)) {
  // Create our fileicon data source.
  Profile* profile = Profile::FromBrowserContext(dlm->GetBrowserContext());
  ChromeURLDataManager::AddDataSource(profile, new FileIconSource());

  if (profile->IsOffTheRecord()) {
    original_notifier_.reset(new AllDownloadItemNotifier(
        BrowserContext::GetDownloadManager(profile->GetOriginalProfile()),
        this));
  }
}

DownloadsDOMHandler::~DownloadsDOMHandler() {
}

// DownloadsDOMHandler, public: -----------------------------------------------

void DownloadsDOMHandler::OnPageLoaded(const base::ListValue* args) {
  SendCurrentDownloads();
}

void DownloadsDOMHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("onPageLoaded",
      base::Bind(&DownloadsDOMHandler::OnPageLoaded,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("getDownloads",
      base::Bind(&DownloadsDOMHandler::HandleGetDownloads,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("openFile",
      base::Bind(&DownloadsDOMHandler::HandleOpenFile,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("drag",
      base::Bind(&DownloadsDOMHandler::HandleDrag,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("saveDangerous",
      base::Bind(&DownloadsDOMHandler::HandleSaveDangerous,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("discardDangerous",
      base::Bind(&DownloadsDOMHandler::HandleDiscardDangerous,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("show",
      base::Bind(&DownloadsDOMHandler::HandleShow,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("togglepause",
      base::Bind(&DownloadsDOMHandler::HandlePause,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("resume",
      base::Bind(&DownloadsDOMHandler::HandlePause,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("remove",
      base::Bind(&DownloadsDOMHandler::HandleRemove,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("cancel",
      base::Bind(&DownloadsDOMHandler::HandleCancel,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("clearAll",
      base::Bind(&DownloadsDOMHandler::HandleClearAll,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback("openDownloadsFolder",
      base::Bind(&DownloadsDOMHandler::HandleOpenDownloadsFolder,
                 weak_ptr_factory_.GetWeakPtr()));
}

void DownloadsDOMHandler::OnDownloadCreated(
    content::DownloadManager* manager, content::DownloadItem* download_item) {
  if (IsDownloadDisplayable(*download_item))
    ScheduleSendCurrentDownloads();
}

void DownloadsDOMHandler::OnDownloadUpdated(
    content::DownloadManager* manager,
    content::DownloadItem* download_item) {
  if (IsDownloadDisplayable(*download_item)) {
    if (!search_text_.empty()) {
      // Don't CallDownloadUpdated() if download_item doesn't match
      // search_text_.
      // TODO(benjhayden): Consider splitting MatchesQuery() out to a function.
      content::DownloadManager::DownloadVector all_items, filtered_items;
      all_items.push_back(download_item);
      DownloadQuery query;
      scoped_ptr<base::Value> query_text(base::Value::CreateStringValue(
          search_text_));
      query.AddFilter(DownloadQuery::FILTER_QUERY, *query_text.get());
      query.Search(all_items.begin(), all_items.end(), &filtered_items);
      if (filtered_items.empty())
        return;
    }
    base::ListValue results_value;
    results_value.Append(CreateDownloadItemValue(
        download_item,
        (original_notifier_.get() &&
          (manager == main_notifier_.GetManager()))));
    CallDownloadUpdated(results_value);
  }
}

void DownloadsDOMHandler::OnDownloadRemoved(
    content::DownloadManager* manager,
    content::DownloadItem* download_item) {
  // This relies on |download_item| being removed from DownloadManager in this
  // MessageLoop iteration. |download_item| may not have been removed from
  // DownloadManager when OnDownloadRemoved() is fired, so bounce off the
  // MessageLoop to give it a chance to be removed. SendCurrentDownloads() looks
  // at all downloads, and we do not tell it that |download_item| is being
  // removed. If DownloadManager is ever changed to not immediately remove
  // |download_item| from its map when OnDownloadRemoved is sent, then
  // DownloadsDOMHandler::OnDownloadRemoved() will need to explicitly tell
  // SendCurrentDownloads() that |download_item| was removed. A
  // SupportsUserData::Data would be the correct way to do this.
  ScheduleSendCurrentDownloads();
}

void DownloadsDOMHandler::HandleGetDownloads(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_GET_DOWNLOADS);
  search_text_ = ExtractStringValue(args);
  SendCurrentDownloads();
}

void DownloadsDOMHandler::HandleOpenFile(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_OPEN_FILE);
  content::DownloadItem* file = GetDownloadByValue(args);
  if (file)
    file->OpenDownload();
}

void DownloadsDOMHandler::HandleDrag(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_DRAG);
  content::DownloadItem* file = GetDownloadByValue(args);
  content::WebContents* web_contents = GetWebUIWebContents();
  // |web_contents| is only NULL in the test.
  if (!file || !web_contents)
    return;
  gfx::Image* icon = g_browser_process->icon_manager()->LookupIcon(
      file->GetUserVerifiedFilePath(), IconLoader::NORMAL);
  gfx::NativeView view = web_contents->GetNativeView();
  {
    // Enable nested tasks during DnD, while |DragDownload()| blocks.
    MessageLoop::ScopedNestableTaskAllower allow(MessageLoop::current());
    download_util::DragDownload(file, icon, view);
  }
}

void DownloadsDOMHandler::HandleSaveDangerous(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_SAVE_DANGEROUS);
  content::DownloadItem* file = GetDownloadByValue(args);
  if (file)
    ShowDangerPrompt(file);
}

void DownloadsDOMHandler::HandleDiscardDangerous(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_DISCARD_DANGEROUS);
  content::DownloadItem* file = GetDownloadByValue(args);
  if (file)
    file->Delete(content::DownloadItem::DELETE_DUE_TO_USER_DISCARD);
}

void DownloadsDOMHandler::HandleShow(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_SHOW);
  content::DownloadItem* file = GetDownloadByValue(args);
  if (file)
    file->ShowDownloadInShell();
}

void DownloadsDOMHandler::HandlePause(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_PAUSE);
  content::DownloadItem* file = GetDownloadByValue(args);
  if (file)
    file->TogglePause();
}

void DownloadsDOMHandler::HandleRemove(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_REMOVE);
  content::DownloadItem* file = GetDownloadByValue(args);
  if (file)
    file->Remove();
}

void DownloadsDOMHandler::HandleCancel(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_CANCEL);
  content::DownloadItem* file = GetDownloadByValue(args);
  if (file)
    file->Cancel(true);
}

void DownloadsDOMHandler::HandleClearAll(const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_CLEAR_ALL);
  if (main_notifier_.GetManager())
    main_notifier_.GetManager()->RemoveAllDownloads();

  // If this is an incognito downloads page, clear All should clear main
  // download manager as well.
  if (original_notifier_.get() && original_notifier_->GetManager())
    original_notifier_->GetManager()->RemoveAllDownloads();
}

void DownloadsDOMHandler::HandleOpenDownloadsFolder(
    const base::ListValue* args) {
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_OPEN_FOLDER);
  if (main_notifier_.GetManager()) {
    platform_util::OpenItem(DownloadPrefs::FromDownloadManager(
        main_notifier_.GetManager())->DownloadPath());
  }
}

// DownloadsDOMHandler, private: ----------------------------------------------

void DownloadsDOMHandler::ScheduleSendCurrentDownloads() {
  // Don't call SendCurrentDownloads() every time anything changes. Batch them
  // together instead. This may handle hundreds of OnDownloadDestroyed() calls
  // in a single UI message loop iteration when the user Clears All downloads.
  if (update_scheduled_)
    return;
  update_scheduled_ = true;
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&DownloadsDOMHandler::SendCurrentDownloads,
                 weak_ptr_factory_.GetWeakPtr()));
}

void DownloadsDOMHandler::SendCurrentDownloads() {
  update_scheduled_ = false;
  content::DownloadManager::DownloadVector all_items, filtered_items;
  if (main_notifier_.GetManager()) {
    main_notifier_.GetManager()->GetAllDownloads(&all_items);
    main_notifier_.GetManager()->CheckForHistoryFilesRemoval();
  }
  if (original_notifier_.get() && original_notifier_->GetManager()) {
    original_notifier_->GetManager()->GetAllDownloads(&all_items);
    original_notifier_->GetManager()->CheckForHistoryFilesRemoval();
  }
  DownloadQuery query;
  if (!search_text_.empty()) {
    scoped_ptr<base::Value> query_text(base::Value::CreateStringValue(
        search_text_));
    query.AddFilter(DownloadQuery::FILTER_QUERY, *query_text.get());
  }
  query.AddFilter(base::Bind(&IsDownloadDisplayable));
  query.AddSorter(DownloadQuery::SORT_START_TIME, DownloadQuery::DESCENDING);
  query.Limit(kMaxDownloads);
  query.Search(all_items.begin(), all_items.end(), &filtered_items);
  base::ListValue results_value;
  for (content::DownloadManager::DownloadVector::const_iterator
       iter = filtered_items.begin(); iter != filtered_items.end(); ++iter) {
    results_value.Append(CreateDownloadItemValue(
        *iter,
        (original_notifier_.get() &&
          main_notifier_.GetManager() &&
          (main_notifier_.GetManager()->GetDownload((*iter)->GetId()) ==
          *iter))));
  }
  CallDownloadsList(results_value);
}

void DownloadsDOMHandler::ShowDangerPrompt(
    content::DownloadItem* dangerous_item) {
  DownloadDangerPrompt* danger_prompt = DownloadDangerPrompt::Create(
      dangerous_item,
      TabContents::FromWebContents(GetWebUIWebContents()),
      base::Bind(&DownloadsDOMHandler::DangerPromptAccepted,
                 weak_ptr_factory_.GetWeakPtr(), dangerous_item->GetId()),
      base::Closure());
  // danger_prompt will delete itself.
  DCHECK(danger_prompt);
}

void DownloadsDOMHandler::DangerPromptAccepted(int download_id) {
  content::DownloadItem* item = NULL;
  if (main_notifier_.GetManager())
    item = main_notifier_.GetManager()->GetDownload(download_id);
  if (!item && original_notifier_.get() && original_notifier_->GetManager())
    item = original_notifier_->GetManager()->GetDownload(download_id);
  if (!item || (item->GetState() != content::DownloadItem::IN_PROGRESS))
    return;
  CountDownloadsDOMEvents(DOWNLOADS_DOM_EVENT_SAVE_DANGEROUS);
  item->DangerousDownloadValidated();
}

content::DownloadItem* DownloadsDOMHandler::GetDownloadByValue(
    const base::ListValue* args) {
  int download_id = -1;
  if (!ExtractIntegerValue(args, &download_id))
    return NULL;
  content::DownloadItem* item = NULL;
  if (main_notifier_.GetManager())
    item = main_notifier_.GetManager()->GetDownload(download_id);
  if (!item && original_notifier_.get() && original_notifier_->GetManager())
    item = original_notifier_->GetManager()->GetDownload(download_id);
  return item;
}

content::WebContents* DownloadsDOMHandler::GetWebUIWebContents() {
  return web_ui()->GetWebContents();
}

void DownloadsDOMHandler::CallDownloadsList(const base::ListValue& downloads) {
  web_ui()->CallJavascriptFunction("downloadsList", downloads);
}

void DownloadsDOMHandler::CallDownloadUpdated(
    const base::ListValue& download_item) {
  web_ui()->CallJavascriptFunction("downloadUpdated", download_item);
}
