// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/safe_browsing_service.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/debug/leak_tracker.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/prefs/public/pref_change_registrar.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/metrics/metrics_service.h"
#include "chrome/browser/net/sqlite_persistent_cookie_store.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/safe_browsing/client_side_detection_service.h"
#include "chrome/browser/safe_browsing/download_protection_service.h"
#include "chrome/browser/safe_browsing/malware_details.h"
#include "chrome/browser/safe_browsing/ping_manager.h"
#include "chrome/browser/safe_browsing/protocol_manager.h"
#include "chrome/browser/safe_browsing/safe_browsing_blocking_page.h"
#include "chrome/browser/safe_browsing/safe_browsing_database.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/cookies/cookie_monster.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

#if defined(OS_WIN)
#include "chrome/installer/util/browser_distribution.h"
#endif

using content::BrowserThread;
using content::NavigationEntry;
using content::WebContents;

namespace {

// Filename suffix for the cookie database.
const FilePath::CharType kCookiesFile[] = FILE_PATH_LITERAL(" Cookies");

// The default URL prefix where browser fetches chunk updates, hashes,
// and reports safe browsing hits and malware details.
const char* const kSbDefaultURLPrefix =
    "https://safebrowsing.google.com/safebrowsing";

// When download url check takes this long, client's callback will be called
// without waiting for the result.
const int64 kDownloadUrlCheckTimeoutMs = 10000;

// Similar to kDownloadUrlCheckTimeoutMs, but for download hash checks.
const int64 kDownloadHashCheckTimeoutMs = 10000;

// Records disposition information about the check.  |hit| should be
// |true| if there were any prefix hits in |full_hashes|.
void RecordGetHashCheckStatus(
    bool hit,
    bool is_download,
    const std::vector<SBFullHashResult>& full_hashes) {
  SafeBrowsingProtocolManager::ResultType result;
  if (full_hashes.empty()) {
    result = SafeBrowsingProtocolManager::GET_HASH_FULL_HASH_EMPTY;
  } else if (hit) {
    result = SafeBrowsingProtocolManager::GET_HASH_FULL_HASH_HIT;
  } else {
    result = SafeBrowsingProtocolManager::GET_HASH_FULL_HASH_MISS;
  }
  SafeBrowsingProtocolManager::RecordGetHashResult(is_download, result);
}

FilePath BaseFilename() {
  FilePath path;
  bool result = PathService::Get(chrome::DIR_USER_DATA, &path);
  DCHECK(result);
  return path.Append(chrome::kSafeBrowsingBaseFilename);
}

FilePath CookieFilePath() {
  return FilePath(BaseFilename().value() + kCookiesFile);
}

}  // namespace

class SafeBrowsingURLRequestContextGetter
    : public net::URLRequestContextGetter {
 public:
  explicit SafeBrowsingURLRequestContextGetter(
      SafeBrowsingService* sb_service_);

  // Implementation for net::UrlRequestContextGetter.
  virtual net::URLRequestContext* GetURLRequestContext() OVERRIDE;
  virtual scoped_refptr<base::SingleThreadTaskRunner>
      GetNetworkTaskRunner() const OVERRIDE;

 protected:
  virtual ~SafeBrowsingURLRequestContextGetter();

 private:
  SafeBrowsingService* const sb_service_;  // Owned by BrowserProcess.
  scoped_refptr<base::SingleThreadTaskRunner> network_task_runner_;

  base::debug::LeakTracker<SafeBrowsingURLRequestContextGetter> leak_tracker_;
};

SafeBrowsingURLRequestContextGetter::SafeBrowsingURLRequestContextGetter(
    SafeBrowsingService* sb_service)
    : sb_service_(sb_service),
      network_task_runner_(
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO)) {
}

SafeBrowsingURLRequestContextGetter::~SafeBrowsingURLRequestContextGetter() {}

net::URLRequestContext*
SafeBrowsingURLRequestContextGetter::GetURLRequestContext() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(sb_service_->url_request_context_.get());

  return sb_service_->url_request_context_.get();
}

scoped_refptr<base::SingleThreadTaskRunner>
SafeBrowsingURLRequestContextGetter::GetNetworkTaskRunner() const {
  return network_task_runner_;
}

// static
SafeBrowsingServiceFactory* SafeBrowsingService::factory_ = NULL;

// The default SafeBrowsingServiceFactory.  Global, made a singleton so we
// don't leak it.
class SafeBrowsingServiceFactoryImpl : public SafeBrowsingServiceFactory {
 public:
  virtual SafeBrowsingService* CreateSafeBrowsingService() {
    return new SafeBrowsingService();
  }

 private:
  friend struct base::DefaultLazyInstanceTraits<SafeBrowsingServiceFactoryImpl>;

  SafeBrowsingServiceFactoryImpl() { }

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingServiceFactoryImpl);
};

static base::LazyInstance<SafeBrowsingServiceFactoryImpl>
    g_safe_browsing_service_factory_impl = LAZY_INSTANCE_INITIALIZER;

struct SafeBrowsingService::WhiteListedEntry {
  int render_process_host_id;
  int render_view_id;
  std::string domain;
  SBThreatType threat_type;
};

SafeBrowsingService::UnsafeResource::UnsafeResource()
    : is_subresource(false),
      threat_type(SB_THREAT_TYPE_SAFE),
      render_process_host_id(-1),
      render_view_id(-1) {
}

SafeBrowsingService::UnsafeResource::~UnsafeResource() {}

SafeBrowsingService::SafeBrowsingCheck::SafeBrowsingCheck()
    : full_hash(NULL),
      client(NULL),
      need_get_hash(false),
      threat_type(SB_THREAT_TYPE_SAFE),
      is_download(false),
      timeout_factory_(NULL) {
}

SafeBrowsingService::SafeBrowsingCheck::~SafeBrowsingCheck() {}

void SafeBrowsingService::Client::OnSafeBrowsingResult(
    const SafeBrowsingCheck& check) {
  if (!check.urls.empty()) {

    DCHECK(!check.full_hash.get());
    if (!check.is_download) {
      DCHECK_EQ(1U, check.urls.size());
      OnCheckBrowseUrlResult(check.urls[0], check.threat_type);
    } else {
      OnCheckDownloadUrlResult(check.urls, check.threat_type);
    }
  } else if (check.full_hash.get()) {
    OnCheckDownloadHashResult(
        safe_browsing_util::SBFullHashToString(*check.full_hash),
        check.threat_type);
  } else {
    NOTREACHED();
  }
}

// static
FilePath SafeBrowsingService::GetCookieFilePathForTesting() {
  return CookieFilePath();
}

// static
SafeBrowsingService* SafeBrowsingService::CreateSafeBrowsingService() {
  if (!factory_)
    factory_ = g_safe_browsing_service_factory_impl.Pointer();
  return factory_->CreateSafeBrowsingService();
}

SafeBrowsingService::SafeBrowsingService()
    : database_(NULL),
      protocol_manager_(NULL),
      ping_manager_(NULL),
      enabled_(false),
      enable_download_protection_(false),
      enable_csd_whitelist_(false),
      enable_download_whitelist_(false),
      update_in_progress_(false),
      database_update_in_progress_(false),
      closing_database_(false),
      download_urlcheck_timeout_ms_(kDownloadUrlCheckTimeoutMs),
      download_hashcheck_timeout_ms_(kDownloadHashCheckTimeoutMs) {
}

SafeBrowsingService::~SafeBrowsingService() {
  // We should have already been shut down. If we're still enabled, then the
  // database isn't going to be closed properly, which could lead to corruption.
  DCHECK(!enabled_);
}

void SafeBrowsingService::Initialize() {
  url_request_context_getter_ =
      new SafeBrowsingURLRequestContextGetter(this);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(
          &SafeBrowsingService::InitURLRequestContextOnIOThread, this,
          make_scoped_refptr(g_browser_process->system_request_context())));
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableClientSidePhishingDetection)) {
    csd_service_.reset(
        safe_browsing::ClientSideDetectionService::Create(
            url_request_context_getter_));
  }
  download_service_.reset(new safe_browsing::DownloadProtectionService(
      this,
      url_request_context_getter_));

  // Track the safe browsing preference of existing profiles.
  // The SafeBrowsingService will be started if any existing profile has the
  // preference enabled. It will also listen for updates to the preferences.
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (profile_manager) {
    std::vector<Profile*> profiles = profile_manager->GetLoadedProfiles();
    for (size_t i = 0; i < profiles.size(); ++i) {
      if (profiles[i]->IsOffTheRecord())
        continue;
      AddPrefService(profiles[i]->GetPrefs());
    }
  }

  // Track profile creation and destruction.
  prefs_registrar_.Add(this, chrome::NOTIFICATION_PROFILE_CREATED,
                       content::NotificationService::AllSources());
  prefs_registrar_.Add(this, chrome::NOTIFICATION_PROFILE_DESTROYED,
                       content::NotificationService::AllSources());
}

void SafeBrowsingService::ShutDown() {
  // Deletes the PrefChangeRegistrars, whose dtors also unregister |this| as an
  // observer of the preferences.
  STLDeleteValues(&prefs_map_);

  // Remove Profile creation/destruction observers.
  prefs_registrar_.RemoveAll();

  Stop();
  // The IO thread is going away, so make sure the ClientSideDetectionService
  // dtor executes now since it may call the dtor of URLFetcher which relies
  // on it.
  csd_service_.reset();
  download_service_.reset();

  url_request_context_getter_ = NULL;
  BrowserThread::PostNonNestableTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::DestroyURLRequestContextOnIOThread,
                 this));
}

bool SafeBrowsingService::CanCheckUrl(const GURL& url) const {
  return url.SchemeIs(chrome::kFtpScheme) ||
         url.SchemeIs(chrome::kHttpScheme) ||
         url.SchemeIs(chrome::kHttpsScheme);
}

// Only report SafeBrowsing related stats when UMA is enabled. User must also
// ensure that safe browsing is enabled from the calling profile.
bool SafeBrowsingService::CanReportStats() const {
  const MetricsService* metrics = g_browser_process->metrics_service();
  return metrics && metrics->reporting_active();
}

// Binhash verification is only enabled for UMA users for now.
bool SafeBrowsingService::DownloadBinHashNeeded() const {
  return (enable_download_protection_ && CanReportStats()) ||
      (download_protection_service() &&
       download_protection_service()->enabled());
}

bool SafeBrowsingService::CheckDownloadUrl(const std::vector<GURL>& url_chain,
                                           Client* client) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_ || !enable_download_protection_)
    return true;

  // We need to check the database for url prefix, and later may fetch the url
  // from the safebrowsing backends. These need to be asynchronous.
  SafeBrowsingCheck* check = new SafeBrowsingCheck();
  check->urls = url_chain;
  StartDownloadCheck(
      check,
      client,
      base::Bind(&SafeBrowsingService::CheckDownloadUrlOnSBThread, this, check),
      download_urlcheck_timeout_ms_);
  return false;
}

bool SafeBrowsingService::CheckDownloadHash(const std::string& full_hash,
                                            Client* client) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!full_hash.empty());
  if (!enabled_ || !enable_download_protection_ || full_hash.empty())
    return true;

  // We need to check the database for url prefix, and later may fetch the url
  // from the safebrowsing backends. These need to be asynchronous.
  SafeBrowsingCheck* check = new SafeBrowsingCheck();
  check->full_hash.reset(new SBFullHash);
  safe_browsing_util::StringToSBFullHash(full_hash, check->full_hash.get());
  StartDownloadCheck(
      check,
      client,
      base::Bind(&SafeBrowsingService::CheckDownloadHashOnSBThread,this, check),
      download_hashcheck_timeout_ms_);
  return false;
}

bool SafeBrowsingService::MatchCsdWhitelistUrl(const GURL& url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_ || !enable_csd_whitelist_ || !MakeDatabaseAvailable()) {
    // There is something funky going on here -- for example, perhaps the user
    // has not restarted since enabling metrics reporting, so we haven't
    // enabled the csd whitelist yet.  Just to be safe we return true in this
    // case.
    return true;
  }
  return database_->ContainsCsdWhitelistedUrl(url);
}

bool SafeBrowsingService::MatchDownloadWhitelistUrl(const GURL& url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_ || !enable_download_whitelist_ || !MakeDatabaseAvailable()) {
    return true;
  }
  return database_->ContainsDownloadWhitelistedUrl(url);
}

bool SafeBrowsingService::MatchDownloadWhitelistString(
    const std::string& str) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_ || !enable_download_whitelist_ || !MakeDatabaseAvailable()) {
    return true;
  }
  return database_->ContainsDownloadWhitelistedString(str);
}

bool SafeBrowsingService::CheckBrowseUrl(const GURL& url,
                                         Client* client) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_)
    return true;

  if (!CanCheckUrl(url))
    return true;

  const base::TimeTicks start = base::TimeTicks::Now();
  if (!MakeDatabaseAvailable()) {
    QueuedCheck check;
    check.client = client;
    check.url = url;
    check.start = start;
    queued_checks_.push_back(check);
    return false;
  }

  std::string list;
  std::vector<SBPrefix> prefix_hits;
  std::vector<SBFullHashResult> full_hits;
  bool prefix_match =
      database_->ContainsBrowseUrl(url, &list, &prefix_hits,
                                   &full_hits,
                                   protocol_manager_->last_update());

  UMA_HISTOGRAM_TIMES("SB2.FilterCheck", base::TimeTicks::Now() - start);

  if (!prefix_match)
    return true;  // URL is okay.

  // Needs to be asynchronous, since we could be in the constructor of a
  // ResourceDispatcherHost event handler which can't pause there.
  SafeBrowsingCheck* check = new SafeBrowsingCheck();
  check->urls.push_back(url);
  check->client = client;
  check->threat_type = SB_THREAT_TYPE_SAFE;
  check->is_download = false;
  check->need_get_hash = full_hits.empty();
  check->prefix_hits.swap(prefix_hits);
  check->full_hits.swap(full_hits);
  checks_.insert(check);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::OnCheckDone, this, check));

  return false;
}

void SafeBrowsingService::CancelCheck(Client* client) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  for (CurrentChecks::iterator i = checks_.begin(); i != checks_.end(); ++i) {
    // We can't delete matching checks here because the db thread has a copy of
    // the pointer.  Instead, we simply NULL out the client, and when the db
    // thread calls us back, we'll clean up the check.
    if ((*i)->client == client)
      (*i)->client = NULL;
  }

  // Scan the queued clients store. Clients may be here if they requested a URL
  // check before the database has finished loading.
  for (std::deque<QueuedCheck>::iterator it(queued_checks_.begin());
       it != queued_checks_.end(); ) {
    // In this case it's safe to delete matches entirely since nothing has a
    // pointer to them.
    if (it->client == client)
      it = queued_checks_.erase(it);
    else
      ++it;
  }
}

void SafeBrowsingService::DisplayBlockingPage(
    const GURL& url,
    const GURL& original_url,
    const std::vector<GURL>& redirect_urls,
    bool is_subresource,
    SBThreatType threat_type,
    const UrlCheckCallback& callback,
    int render_process_host_id,
    int render_view_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  UnsafeResource resource;
  resource.url = url;
  resource.original_url = original_url;
  resource.redirect_urls = redirect_urls;
  resource.is_subresource = is_subresource;
  resource.threat_type = threat_type;
  resource.callback = callback;
  resource.render_process_host_id = render_process_host_id;
  resource.render_view_id = render_view_id;

  // The blocking page must be created from the UI thread.
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&SafeBrowsingService::DoDisplayBlockingPage, this, resource));
}

void SafeBrowsingService::HandleGetHashResults(
    SafeBrowsingCheck* check,
    const std::vector<SBFullHashResult>& full_hashes,
    bool can_cache) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if (!enabled_)
    return;

  // If the service has been shut down, |check| should have been deleted.
  DCHECK(checks_.find(check) != checks_.end());

  // |start| is set before calling |GetFullHash()|, which should be
  // the only path which gets to here.
  DCHECK(!check->start.is_null());
  UMA_HISTOGRAM_LONG_TIMES("SB2.Network",
                           base::TimeTicks::Now() - check->start);

  std::vector<SBPrefix> prefixes = check->prefix_hits;
  OnHandleGetHashResults(check, full_hashes);  // 'check' is deleted here.

  if (can_cache && MakeDatabaseAvailable()) {
    // Cache the GetHash results in memory:
    database_->CacheHashResults(prefixes, full_hashes);
  }
}

void SafeBrowsingService::GetChunks(GetChunksCallback callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(enabled_);
  DCHECK(!callback.is_null());
  safe_browsing_thread_->message_loop()->PostTask(FROM_HERE, base::Bind(
      &SafeBrowsingService::GetAllChunksFromDatabase, this, callback));
}

void SafeBrowsingService::AddChunks(const std::string& list,
                                    SBChunkList* chunks) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(enabled_);
  safe_browsing_thread_->message_loop()->PostTask(FROM_HERE, base::Bind(
      &SafeBrowsingService::HandleChunkForDatabase, this, list, chunks));
}

void SafeBrowsingService::DeleteChunks(
    std::vector<SBChunkDelete>* chunk_deletes) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(enabled_);
  safe_browsing_thread_->message_loop()->PostTask(FROM_HERE, base::Bind(
      &SafeBrowsingService::DeleteDatabaseChunks, this, chunk_deletes));
}

void SafeBrowsingService::UpdateStarted() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(enabled_);
  DCHECK(!update_in_progress_);
  update_in_progress_ = true;
}

void SafeBrowsingService::UpdateFinished(bool update_succeeded) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(enabled_);
  if (update_in_progress_) {
    update_in_progress_ = false;
    safe_browsing_thread_->message_loop()->PostTask(FROM_HERE,
      base::Bind(&SafeBrowsingService::DatabaseUpdateFinished,
                 this, update_succeeded));
  }
}

void SafeBrowsingService::OnBlockingPageDone(
    const std::vector<UnsafeResource>& resources,
    bool proceed) {
  for (std::vector<UnsafeResource>::const_iterator iter = resources.begin();
       iter != resources.end(); ++iter) {
    const UnsafeResource& resource = *iter;
    if (!resource.callback.is_null())
      resource.callback.Run(proceed);

    if (proceed) {
      BrowserThread::PostTask(
          BrowserThread::UI,
          FROM_HERE,
          base::Bind(&SafeBrowsingService::UpdateWhitelist, this, resource));
    }
  }
}

net::URLRequestContextGetter* SafeBrowsingService::url_request_context() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return url_request_context_getter_.get();
}

void SafeBrowsingService::ResetDatabase() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(enabled_);
  safe_browsing_thread_->message_loop()->PostTask(FROM_HERE, base::Bind(
      &SafeBrowsingService::OnResetDatabase, this));
}

void SafeBrowsingService::PurgeMemory() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  CloseDatabase();
}

void SafeBrowsingService::LogPauseDelay(base::TimeDelta time) {
  UMA_HISTOGRAM_LONG_TIMES("SB2.Delay", time);
}

void SafeBrowsingService::InitURLRequestContextOnIOThread(
    net::URLRequestContextGetter* system_url_request_context_getter) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!url_request_context_.get());

  scoped_refptr<net::CookieStore> cookie_store = new net::CookieMonster(
      new SQLitePersistentCookieStore(CookieFilePath(), false, NULL),
      NULL);

  url_request_context_.reset(new net::URLRequestContext);
  // |system_url_request_context_getter| may be NULL during tests.
  if (system_url_request_context_getter) {
    url_request_context_->CopyFrom(
        system_url_request_context_getter->GetURLRequestContext());
  }
  url_request_context_->set_cookie_store(cookie_store);
}

void SafeBrowsingService::DestroyURLRequestContextOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  url_request_context_->AssertNoURLRequests();

  // Need to do the CheckForLeaks on IOThread instead of in ShutDown where
  // url_request_context_getter_ is cleared,  since the URLRequestContextGetter
  // will PostTask to IOTread to delete itself.
  using base::debug::LeakTracker;
  LeakTracker<SafeBrowsingURLRequestContextGetter>::CheckForLeaks();

  url_request_context_.reset();
}

void SafeBrowsingService::StartOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (enabled_)
    return;
  DCHECK(!safe_browsing_thread_.get());
  safe_browsing_thread_.reset(new base::Thread("Chrome_SafeBrowsingThread"));
  if (!safe_browsing_thread_->Start())
    return;
  enabled_ = true;

  registrar_.reset(new content::NotificationRegistrar);

  MakeDatabaseAvailable();

  SafeBrowsingProtocolConfig config;
  // On Windows, get the safe browsing client name from the browser
  // distribution classes in installer util. These classes don't yet have
  // an analog on non-Windows builds so just keep the name specified here.
#if defined(OS_WIN)
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  config.client_name = dist->GetSafeBrowsingName();
#else
#if defined(GOOGLE_CHROME_BUILD)
  config.client_name = "googlechrome";
#else
  config.client_name = "chromium";
#endif
#endif
  CommandLine* cmdline = CommandLine::ForCurrentProcess();
  config.disable_auto_update =
      cmdline->HasSwitch(switches::kSbDisableAutoUpdate) ||
      cmdline->HasSwitch(switches::kDisableBackgroundNetworking);
  config.url_prefix =
      cmdline->HasSwitch(switches::kSbURLPrefix) ?
      cmdline->GetSwitchValueASCII(switches::kSbURLPrefix) :
      kSbDefaultURLPrefix;

  DCHECK(!protocol_manager_);
  protocol_manager_ =
      SafeBrowsingProtocolManager::Create(this,
                                          url_request_context_getter_,
                                          config);
  protocol_manager_->Initialize();

  DCHECK(!ping_manager_);
  ping_manager_ =
      SafeBrowsingPingManager::Create(url_request_context_getter_,
                                      config);
}

void SafeBrowsingService::StopOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_)
    return;

  enabled_ = false;

  registrar_.reset();

  // This cancels all in-flight GetHash requests.
  delete protocol_manager_;
  protocol_manager_ = NULL;

  delete ping_manager_;
  ping_manager_ = NULL;

  // Delete queued checks, calling back any clients with 'SB_THREAT_TYPE_SAFE'.
  // If we don't do this here we may fail to close the database below.
  while (!queued_checks_.empty()) {
    QueuedCheck queued = queued_checks_.front();
    if (queued.client) {
      SafeBrowsingCheck sb_check;
      sb_check.urls.push_back(queued.url);
      sb_check.client = queued.client;
      sb_check.threat_type = SB_THREAT_TYPE_SAFE;
      queued.client->OnSafeBrowsingResult(sb_check);
    }
    queued_checks_.pop_front();
  }

  // Close the database.  We don't simply DeleteSoon() because if a close is
  // already pending, we'll double-free, and we don't set |database_| to NULL
  // because if there is still anything running on the db thread, it could
  // create a new database object (via GetDatabase()) that would then leak.
  CloseDatabase();

  // Flush the database thread. Any in-progress database check results will be
  // ignored and cleaned up below.
  //
  // Note that to avoid leaking the database, we rely on the fact that no new
  // tasks will be added to the db thread between the call above and this one.
  // See comments on the declaration of |safe_browsing_thread_|.
  {
    // A ScopedAllowIO object is required to join the thread when calling Stop.
    // See http://crbug.com/72696.
    base::ThreadRestrictions::ScopedAllowIO allow_io_for_thread_join;
    safe_browsing_thread_.reset();
  }

  // Delete pending checks, calling back any clients with 'SB_THREAT_TYPE_SAFE'.
  // We have to do this after the db thread returns because methods on it can
  // have copies of these pointers, so deleting them might lead to accessing
  // garbage.
  for (CurrentChecks::iterator it = checks_.begin();
       it != checks_.end(); ++it) {
    SafeBrowsingCheck* check = *it;
    if (check->client) {
      check->threat_type = SB_THREAT_TYPE_SAFE;
      check->client->OnSafeBrowsingResult(*check);
    }
  }
  STLDeleteElements(&checks_);

  gethash_requests_.clear();
}

bool SafeBrowsingService::DatabaseAvailable() const {
  base::AutoLock lock(database_lock_);
  return !closing_database_ && (database_ != NULL);
}

bool SafeBrowsingService::MakeDatabaseAvailable() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(enabled_);
  if (DatabaseAvailable())
    return true;
  safe_browsing_thread_->message_loop()->PostTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(&SafeBrowsingService::GetDatabase), this));
  return false;
}

void SafeBrowsingService::CloseDatabase() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Cases to avoid:
  //  * If |closing_database_| is true, continuing will queue up a second
  //    request, |closing_database_| will be reset after handling the first
  //    request, and if any functions on the db thread recreate the database, we
  //    could start using it on the IO thread and then have the second request
  //    handler delete it out from under us.
  //  * If |database_| is NULL, then either no creation request is in flight, in
  //    which case we don't need to do anything, or one is in flight, in which
  //    case the database will be recreated before our deletion request is
  //    handled, and could be used on the IO thread in that time period, leading
  //    to the same problem as above.
  //  * If |queued_checks_| is non-empty and |database_| is non-NULL, we're
  //    about to be called back (in DatabaseLoadComplete()).  This will call
  //    CheckUrl(), which will want the database.  Closing the database here
  //    would lead to an infinite loop in DatabaseLoadComplete(), and even if it
  //    didn't, it would be pointless since we'd just want to recreate.
  //
  // The first two cases above are handled by checking DatabaseAvailable().
  if (!DatabaseAvailable() || !queued_checks_.empty())
    return;

  closing_database_ = true;
  if (safe_browsing_thread_.get()) {
    safe_browsing_thread_->message_loop()->PostTask(FROM_HERE,
        base::Bind(&SafeBrowsingService::OnCloseDatabase, this));
  }
}

SafeBrowsingDatabase* SafeBrowsingService::GetDatabase() {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  if (database_)
    return database_;
  const base::TimeTicks before = base::TimeTicks::Now();

  SafeBrowsingDatabase* database =
      SafeBrowsingDatabase::Create(enable_download_protection_,
                                   enable_csd_whitelist_,
                                   enable_download_whitelist_);

  database->Init(BaseFilename());
  {
    // Acquiring the lock here guarantees correct ordering between the writes to
    // the new database object above, and the setting of |databse_| below.
    base::AutoLock lock(database_lock_);
    database_ = database;
  }

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::DatabaseLoadComplete, this));

  UMA_HISTOGRAM_TIMES("SB2.DatabaseOpen", base::TimeTicks::Now() - before);
  return database_;
}

void SafeBrowsingService::OnCheckDone(SafeBrowsingCheck* check) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if (!enabled_)
    return;

  // If the service has been shut down, |check| should have been deleted.
  DCHECK(checks_.find(check) != checks_.end());

  if (check->client && check->need_get_hash) {
    // We have a partial match so we need to query Google for the full hash.
    // Clean up will happen in HandleGetHashResults.

    // See if we have a GetHash request already in progress for this particular
    // prefix. If so, we just append ourselves to the list of interested parties
    // when the results arrive. We only do this for checks involving one prefix,
    // since that is the common case (multiple prefixes will issue the request
    // as normal).
    if (check->prefix_hits.size() == 1) {
      SBPrefix prefix = check->prefix_hits[0];
      GetHashRequests::iterator it = gethash_requests_.find(prefix);
      if (it != gethash_requests_.end()) {
        // There's already a request in progress.
        it->second.push_back(check);
        return;
      }

      // No request in progress, so we're the first for this prefix.
      GetHashRequestors requestors;
      requestors.push_back(check);
      gethash_requests_[prefix] = requestors;
    }

    // Reset the start time so that we can measure the network time without the
    // database time.
    check->start = base::TimeTicks::Now();
    // Note: If |this| is deleted or stopped, the protocol_manager will
    // be destroyed as well - hence it's OK to do unretained in this case.
    protocol_manager_->GetFullHash(
        check->prefix_hits,
        base::Bind(&SafeBrowsingService::HandleGetHashResults,
                   base::Unretained(this),
                   check),
        check->is_download);
  } else {
    // We may have cached results for previous GetHash queries.  Since
    // this data comes from cache, don't histogram hits.
    HandleOneCheck(check, check->full_hits);
  }
}

void SafeBrowsingService::GetAllChunksFromDatabase(GetChunksCallback callback) {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());

  bool database_error = true;
  std::vector<SBListChunkRanges> lists;
  DCHECK(!database_update_in_progress_);
  database_update_in_progress_ = true;
  GetDatabase();  // This guarantees that |database_| is non-NULL.
  if (database_->UpdateStarted(&lists)) {
    database_error = false;
  } else {
    database_->UpdateFinished(false);
  }

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::OnGetAllChunksFromDatabase,
                 this, lists, database_error, callback));
}

void SafeBrowsingService::OnGetAllChunksFromDatabase(
    const std::vector<SBListChunkRanges>& lists, bool database_error,
    GetChunksCallback callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (enabled_)
    callback.Run(lists, database_error);
}

void SafeBrowsingService::OnChunkInserted() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (enabled_)
    protocol_manager_->OnChunkInserted();
}

void SafeBrowsingService::DatabaseLoadComplete() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_)
    return;

  HISTOGRAM_COUNTS("SB.QueueDepth", queued_checks_.size());
  if (queued_checks_.empty())
    return;

  // If the database isn't already available, calling CheckUrl() in the loop
  // below will add the check back to the queue, and we'll infinite-loop.
  DCHECK(DatabaseAvailable());
  while (!queued_checks_.empty()) {
    QueuedCheck check = queued_checks_.front();
    DCHECK(!check.start.is_null());
    HISTOGRAM_TIMES("SB.QueueDelay", base::TimeTicks::Now() - check.start);
    // If CheckUrl() determines the URL is safe immediately, it doesn't call the
    // client's handler function (because normally it's being directly called by
    // the client).  Since we're not the client, we have to convey this result.
    if (check.client && CheckBrowseUrl(check.url, check.client)) {
      SafeBrowsingCheck sb_check;
      sb_check.urls.push_back(check.url);
      sb_check.client = check.client;
      sb_check.threat_type = SB_THREAT_TYPE_SAFE;
      check.client->OnSafeBrowsingResult(sb_check);
    }
    queued_checks_.pop_front();
  }
}

void SafeBrowsingService::HandleChunkForDatabase(
    const std::string& list_name, SBChunkList* chunks) {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  if (chunks) {
    GetDatabase()->InsertChunks(list_name, *chunks);
    delete chunks;
  }
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::OnChunkInserted, this));
}

void SafeBrowsingService::DeleteDatabaseChunks(
    std::vector<SBChunkDelete>* chunk_deletes) {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  if (chunk_deletes) {
    GetDatabase()->DeleteChunks(*chunk_deletes);
    delete chunk_deletes;
  }
}

SBThreatType SafeBrowsingService::GetThreatTypeFromListname(
    const std::string& list_name) {
  if (safe_browsing_util::IsPhishingList(list_name)) {
    return SB_THREAT_TYPE_URL_PHISHING;
  }

  if (safe_browsing_util::IsMalwareList(list_name)) {
    return SB_THREAT_TYPE_URL_MALWARE;
  }

  if (safe_browsing_util::IsBadbinurlList(list_name)) {
    return SB_THREAT_TYPE_BINARY_MALWARE_URL;
  }

  if (safe_browsing_util::IsBadbinhashList(list_name)) {
    return SB_THREAT_TYPE_BINARY_MALWARE_HASH;
  }

  DVLOG(1) << "Unknown safe browsing list " << list_name;
  return SB_THREAT_TYPE_SAFE;
}

void SafeBrowsingService::DatabaseUpdateFinished(bool update_succeeded) {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  GetDatabase()->UpdateFinished(update_succeeded);
  DCHECK(database_update_in_progress_);
  database_update_in_progress_ = false;
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&SafeBrowsingService::NotifyDatabaseUpdateFinished,
                 this, update_succeeded));
}

void SafeBrowsingService::NotifyDatabaseUpdateFinished(bool update_succeeded) {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_SAFE_BROWSING_UPDATE_COMPLETE,
      content::Source<SafeBrowsingService>(this),
      content::Details<bool>(&update_succeeded));
}

void SafeBrowsingService::Start() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  CommandLine* cmdline = CommandLine::ForCurrentProcess();
  enable_download_protection_ =
      !cmdline->HasSwitch(switches::kSbDisableDownloadProtection);

  // We only download the csd-whitelist if client-side phishing detection is
  // enabled.
  enable_csd_whitelist_ =
      !cmdline->HasSwitch(switches::kDisableClientSidePhishingDetection);

  // TODO(noelutz): remove this boolean variable since it should always be true
  // if SafeBrowsing is enabled.  Unfortunately, we have no test data for this
  // list right now.  This means that we need to be able to disable this list
  // for the SafeBrowsing test to pass.
  enable_download_whitelist_ = enable_csd_whitelist_;

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::StartOnIOThread, this));
}

void SafeBrowsingService::Stop() {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::StopOnIOThread, this));
}

void SafeBrowsingService::OnCloseDatabase() {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  DCHECK(closing_database_);

  // Because |closing_database_| is true, nothing on the IO thread will be
  // accessing the database, so it's safe to delete and then NULL the pointer.
  delete database_;
  database_ = NULL;

  // Acquiring the lock here guarantees correct ordering between the resetting
  // of |database_| above and of |closing_database_| below, which ensures there
  // won't be a window during which the IO thread falsely believes the database
  // is available.
  base::AutoLock lock(database_lock_);
  closing_database_ = false;
}

void SafeBrowsingService::OnResetDatabase() {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  GetDatabase()->ResetDatabase();
}

void SafeBrowsingService::CacheHashResults(
  const std::vector<SBPrefix>& prefixes,
  const std::vector<SBFullHashResult>& full_hashes) {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  GetDatabase()->CacheHashResults(prefixes, full_hashes);
}

void SafeBrowsingService::OnHandleGetHashResults(
    SafeBrowsingCheck* check,
    const std::vector<SBFullHashResult>& full_hashes) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  bool is_download = check->is_download;
  SBPrefix prefix = check->prefix_hits[0];
  GetHashRequests::iterator it = gethash_requests_.find(prefix);
  if (check->prefix_hits.size() > 1 || it == gethash_requests_.end()) {
    const bool hit = HandleOneCheck(check, full_hashes);
    RecordGetHashCheckStatus(hit, is_download, full_hashes);
    return;
  }

  // Call back all interested parties, noting if any has a hit.
  GetHashRequestors& requestors = it->second;
  bool hit = false;
  for (GetHashRequestors::iterator r = requestors.begin();
       r != requestors.end(); ++r) {
    if (HandleOneCheck(*r, full_hashes))
      hit = true;
  }
  RecordGetHashCheckStatus(hit, is_download, full_hashes);

  gethash_requests_.erase(it);
}

bool SafeBrowsingService::HandleOneCheck(
    SafeBrowsingCheck* check,
    const std::vector<SBFullHashResult>& full_hashes) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(check);

  // Always calculate the index, for recording hits.
  int index = -1;
  if (!check->urls.empty()) {
    for (size_t i = 0; i < check->urls.size(); ++i) {
      index = safe_browsing_util::GetUrlHashIndex(check->urls[i], full_hashes);
      if (index != -1)
        break;
    }
  } else {
    index = safe_browsing_util::GetHashIndex(*(check->full_hash), full_hashes);
  }

  // |client| is NULL if the request was cancelled.
  if (check->client) {
    check->threat_type = SB_THREAT_TYPE_SAFE;
    if (index != -1) {
      check->threat_type = GetThreatTypeFromListname(
          full_hashes[index].list_name);
    }
  }
  SafeBrowsingCheckDone(check);
  return (index != -1);
}

void SafeBrowsingService::DoDisplayBlockingPage(
    const UnsafeResource& resource) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Check if the user has already ignored our warning for this render_view
  // and domain.
  if (IsWhitelisted(resource)) {
    if (!resource.callback.is_null()) {
      BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE, base::Bind(resource.callback, true));
    }
    return;
  }

  // The tab might have been closed.
  WebContents* web_contents =
      tab_util::GetWebContentsByID(resource.render_process_host_id,
                                   resource.render_view_id);

  if (!web_contents) {
    // The tab is gone and we did not have a chance at showing the interstitial.
    // Just act as if "Don't Proceed" were chosen.
    std::vector<UnsafeResource> resources;
    resources.push_back(resource);
    BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::OnBlockingPageDone,
                 this, resources, false));
    return;
  }

  if (resource.threat_type != SB_THREAT_TYPE_SAFE &&
      CanReportStats()) {
    GURL page_url = web_contents->GetURL();
    GURL referrer_url;
    NavigationEntry* entry = web_contents->GetController().GetActiveEntry();
    if (entry)
      referrer_url = entry->GetReferrer().url;

    // When the malicious url is on the main frame, and resource.original_url
    // is not the same as the resource.url, that means we have a redirect from
    // resource.original_url to resource.url.
    // Also, at this point, page_url points to the _previous_ page that we
    // were on. We replace page_url with resource.original_url and referrer
    // with page_url.
    if (!resource.is_subresource &&
        !resource.original_url.is_empty() &&
        resource.original_url != resource.url) {
      referrer_url = page_url;
      page_url = resource.original_url;
    }
    ReportSafeBrowsingHit(resource.url, page_url, referrer_url,
                          resource.is_subresource, resource.threat_type,
                          std::string() /* post_data */);
  }
  if (resource.threat_type != SB_THREAT_TYPE_SAFE) {
    FOR_EACH_OBSERVER(Observer, observer_list_, OnSafeBrowsingHit(resource));
  }
  SafeBrowsingBlockingPage::ShowBlockingPage(this, resource);
}

// A safebrowsing hit is sent after a blocking page for malware/phishing
// or after the warning dialog for download urls, only for UMA users.
void SafeBrowsingService::ReportSafeBrowsingHit(
    const GURL& malicious_url,
    const GURL& page_url,
    const GURL& referrer_url,
    bool is_subresource,
    SBThreatType threat_type,
    const std::string& post_data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!CanReportStats())
    return;

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::ReportSafeBrowsingHitOnIOThread, this,
                 malicious_url, page_url, referrer_url, is_subresource,
                 threat_type, post_data));
}

void SafeBrowsingService::AddObserver(Observer* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  observer_list_.AddObserver(observer);
}

void SafeBrowsingService::RemoveObserver(Observer* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  observer_list_.RemoveObserver(observer);
}

void SafeBrowsingService::ReportSafeBrowsingHitOnIOThread(
    const GURL& malicious_url,
    const GURL& page_url,
    const GURL& referrer_url,
    bool is_subresource,
    SBThreatType threat_type,
    const std::string& post_data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_)
    return;

  DVLOG(1) << "ReportSafeBrowsingHit: " << malicious_url << " " << page_url
           << " " << referrer_url << " " << is_subresource << " "
           << threat_type;
  ping_manager_->ReportSafeBrowsingHit(malicious_url, page_url,
                                           referrer_url, is_subresource,
                                           threat_type, post_data);
}

// If the user had opted-in to send MalwareDetails, this gets called
// when the report is ready.
void SafeBrowsingService::SendSerializedMalwareDetails(
    const std::string& serialized) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!enabled_)
    return;

  if (!serialized.empty()) {
    DVLOG(1) << "Sending serialized malware details.";
    ping_manager_->ReportMalwareDetails(serialized);
  }
}

void SafeBrowsingService::CheckDownloadHashOnSBThread(
    SafeBrowsingCheck* check) {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  DCHECK(enable_download_protection_);

  if (!database_->ContainsDownloadHashPrefix(check->full_hash->prefix)) {
    // Good, we don't have hash for this url prefix.
    check->threat_type = SB_THREAT_TYPE_SAFE;
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&SafeBrowsingService::CheckDownloadHashDone, this, check));
    return;
  }

  check->need_get_hash = true;
  check->prefix_hits.push_back(check->full_hash->prefix);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::OnCheckDone, this, check));
}

void SafeBrowsingService::CheckDownloadUrlOnSBThread(SafeBrowsingCheck* check) {
  DCHECK_EQ(MessageLoop::current(), safe_browsing_thread_->message_loop());
  DCHECK(enable_download_protection_);

  std::vector<SBPrefix> prefix_hits;

  if (!database_->ContainsDownloadUrl(check->urls, &prefix_hits)) {
    // Good, we don't have hash for this url prefix.
    check->threat_type = SB_THREAT_TYPE_SAFE;
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&SafeBrowsingService::CheckDownloadUrlDone, this, check));
    return;
  }

  check->need_get_hash = true;
  check->prefix_hits.clear();
  check->prefix_hits = prefix_hits;
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingService::OnCheckDone, this, check));
}

void SafeBrowsingService::TimeoutCallback(SafeBrowsingCheck* check) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(check);

  if (!enabled_)
    return;

  DCHECK(checks_.find(check) != checks_.end());
  DCHECK_EQ(check->threat_type, SB_THREAT_TYPE_SAFE);
  if (check->client) {
    check->client->OnSafeBrowsingResult(*check);
    check->client = NULL;
  }
}

void SafeBrowsingService::CheckDownloadUrlDone(SafeBrowsingCheck* check) {
  DCHECK(enable_download_protection_);
  SafeBrowsingCheckDone(check);
}

void SafeBrowsingService::CheckDownloadHashDone(SafeBrowsingCheck* check) {
  DCHECK(enable_download_protection_);
  SafeBrowsingCheckDone(check);
}

void SafeBrowsingService::SafeBrowsingCheckDone(SafeBrowsingCheck* check) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(check);

  if (!enabled_)
    return;

  VLOG(1) << "SafeBrowsingCheckDone: " << check->threat_type;
  DCHECK(checks_.find(check) != checks_.end());
  if (check->client)
    check->client->OnSafeBrowsingResult(*check);
  checks_.erase(check);
  delete check;
}

void SafeBrowsingService::StartDownloadCheck(SafeBrowsingCheck* check,
                                             Client* client,
                                             const base::Closure& task,
                                             int64 timeout_ms) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  check->client = client;
  check->threat_type = SB_THREAT_TYPE_SAFE;
  check->is_download = true;
  check->timeout_factory_.reset(
      new base::WeakPtrFactory<SafeBrowsingService>(this));
  checks_.insert(check);

  safe_browsing_thread_->message_loop()->PostTask(FROM_HERE, task);

  MessageLoop::current()->PostDelayedTask(FROM_HERE,
      base::Bind(&SafeBrowsingService::TimeoutCallback,
                 check->timeout_factory_->GetWeakPtr(), check),
      base::TimeDelta::FromMilliseconds(timeout_ms));
}

void SafeBrowsingService::UpdateWhitelist(const UnsafeResource& resource) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Whitelist this domain and warning type for the given tab.
  WhiteListedEntry entry;
  entry.render_process_host_id = resource.render_process_host_id;
  entry.render_view_id = resource.render_view_id;
  entry.domain = net::RegistryControlledDomainService::GetDomainAndRegistry(
      resource.url);
  entry.threat_type = resource.threat_type;
  white_listed_entries_.push_back(entry);
}

void SafeBrowsingService::Observe(int type,
                                  const content::NotificationSource& source,
                                  const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_PROFILE_CREATED: {
      DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
      Profile* profile = content::Source<Profile>(source).ptr();
      if (!profile->IsOffTheRecord())
        AddPrefService(profile->GetPrefs());
      break;
    }
    case chrome::NOTIFICATION_PROFILE_DESTROYED: {
      DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
      Profile* profile = content::Source<Profile>(source).ptr();
      if (!profile->IsOffTheRecord())
        RemovePrefService(profile->GetPrefs());
      break;
    }
    default:
      NOTREACHED();
  }
}

bool SafeBrowsingService::IsWhitelisted(const UnsafeResource& resource) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Check if the user has already ignored our warning for this render_view
  // and domain.
  for (size_t i = 0; i < white_listed_entries_.size(); ++i) {
    const WhiteListedEntry& entry = white_listed_entries_[i];
    if (entry.render_process_host_id == resource.render_process_host_id &&
        entry.render_view_id == resource.render_view_id &&
        // Threat type must be the same or in the case of phishing they can
        // either be client-side phishing URL or a SafeBrowsing phishing URL.
        // If we show one type of phishing warning we don't want to show a
        // second phishing warning.
        (entry.threat_type == resource.threat_type ||
         (entry.threat_type == SB_THREAT_TYPE_URL_PHISHING &&
          resource.threat_type == SB_THREAT_TYPE_CLIENT_SIDE_PHISHING_URL) ||
         (entry.threat_type == SB_THREAT_TYPE_CLIENT_SIDE_PHISHING_URL &&
          resource.threat_type == SB_THREAT_TYPE_URL_PHISHING))  &&
        entry.domain ==
        net::RegistryControlledDomainService::GetDomainAndRegistry(
            resource.url)) {
      return true;
    }
  }
  return false;
}

void SafeBrowsingService::AddPrefService(PrefService* pref_service) {
  DCHECK(prefs_map_.find(pref_service) == prefs_map_.end());
  PrefChangeRegistrar* registrar = new PrefChangeRegistrar();
  registrar->Init(pref_service);
  registrar->Add(prefs::kSafeBrowsingEnabled,
                 base::Bind(&SafeBrowsingService::RefreshState,
                            base::Unretained(this)));
  prefs_map_[pref_service] = registrar;
  RefreshState();
}

void SafeBrowsingService::RemovePrefService(PrefService* pref_service) {
  if (prefs_map_.find(pref_service) != prefs_map_.end()) {
    delete prefs_map_[pref_service];
    prefs_map_.erase(pref_service);
    RefreshState();
  } else {
    NOTREACHED();
  }
}

void SafeBrowsingService::RefreshState() {
  // Check if any profile requires the service to be active.
  bool enable = false;
  std::map<PrefService*, PrefChangeRegistrar*>::iterator iter;
  for (iter = prefs_map_.begin(); iter != prefs_map_.end(); ++iter) {
    if (iter->first->GetBoolean(prefs::kSafeBrowsingEnabled)) {
      enable = true;
      break;
    }
  }

  if (enable)
    Start();
  else
    Stop();

  if (csd_service_.get())
    csd_service_->SetEnabledAndRefreshState(enable);
  if (download_service_.get()) {
    download_service_->SetEnabled(
        enable && !CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisableImprovedDownloadProtection));
  }
}
