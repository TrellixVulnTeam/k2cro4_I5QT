// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/io_thread.h"

#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/debug/leak_tracker.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/threading/thread.h"
#include "base/threading/worker_pool.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/event_router_forwarder.h"
#include "chrome/browser/net/async_dns_field_trial.h"
#include "chrome/browser/net/basic_http_user_agent_settings.h"
#include "chrome/browser/net/chrome_net_log.h"
#include "chrome/browser/net/chrome_network_delegate.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/net/connect_interceptor.h"
#include "chrome/browser/net/dns_probe_service.h"
#include "chrome/browser/net/http_pipelining_compatibility_client.h"
#include "chrome/browser/net/load_time_stats.h"
#include "chrome/browser/net/pref_proxy_config_tracker.h"
#include "chrome/browser/net/proxy_service_factory.h"
#include "chrome/browser/net/sdch_dictionary_fetcher.h"
#include "chrome/browser/net/spdyproxy/http_auth_handler_spdyproxy.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/cert_verifier.h"
#include "net/base/default_server_bound_cert_store.h"
#include "net/base/host_cache.h"
#include "net/base/host_mapping_rules.h"
#include "net/base/host_resolver.h"
#include "net/base/mapped_host_resolver.h"
#include "net/base/net_util.h"
#include "net/base/sdch_manager.h"
#include "net/base/server_bound_cert_service.h"
#include "net/cookies/cookie_monster.h"
#include "net/ftp/ftp_network_layer.h"
#include "net/http/http_auth_filter.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_network_layer.h"
#include "net/http/http_network_session.h"
#include "net/http/http_server_properties_impl.h"
#include "net/proxy/proxy_config_service.h"
#include "net/proxy/proxy_script_fetcher_impl.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_throttler_manager.h"

#if defined(USE_NSS)
#include "net/ocsp/nss_ocsp.h"
#endif  // defined(USE_NSS)

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/proxy_config_service_impl.h"
#endif  // defined(OS_CHROMEOS)

using content::BrowserThread;

class SafeBrowsingURLRequestContext;

// The IOThread object must outlive any tasks posted to the IO thread before the
// Quit task, so base::Bind() calls are not refcounted.

namespace {

#if defined(OS_MACOSX) && !defined(OS_IOS)
void ObserveKeychainEvents() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  net::CertDatabase::GetInstance()->SetMessageLoopForKeychainEvents();
}
#endif

// Used for the "system" URLRequestContext.
class SystemURLRequestContext : public net::URLRequestContext {
 public:
  SystemURLRequestContext() {
#if defined(USE_NSS)
    net::SetURLRequestContextForNSSHttpIO(this);
#endif  // defined(USE_NSS)
  }

 private:
  virtual ~SystemURLRequestContext() {
#if defined(USE_NSS)
    net::SetURLRequestContextForNSSHttpIO(NULL);
#endif  // defined(USE_NSS)
  }
};

scoped_ptr<net::HostResolver> CreateGlobalHostResolver(net::NetLog* net_log) {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  bool allow_async_dns_field_trial = true;

  net::HostResolver::Options options;

  // Use the concurrency override from the command-line, if any.
  if (command_line.HasSwitch(switches::kHostResolverParallelism)) {
    allow_async_dns_field_trial = false;
    std::string s =
        command_line.GetSwitchValueASCII(switches::kHostResolverParallelism);

    // Parse the switch (it should be a positive integer formatted as decimal).
    int n;
    if (base::StringToInt(s, &n) && n > 0) {
      options.max_concurrent_resolves = static_cast<size_t>(n);
    } else {
      LOG(ERROR) << "Invalid switch for host resolver parallelism: " << s;
    }
  }

  // Use the retry attempts override from the command-line, if any.
  if (command_line.HasSwitch(switches::kHostResolverRetryAttempts)) {
    allow_async_dns_field_trial = false;
    std::string s =
        command_line.GetSwitchValueASCII(switches::kHostResolverRetryAttempts);
    // Parse the switch (it should be a non-negative integer).
    int n;
    if (base::StringToInt(s, &n) && n >= 0) {
      options.max_retry_attempts = static_cast<size_t>(n);
    } else {
      LOG(ERROR) << "Invalid switch for host resolver retry attempts: " << s;
    }
  }

  bool enable_async = false;
  if (command_line.HasSwitch(switches::kEnableAsyncDns)) {
    allow_async_dns_field_trial = false;
    enable_async = true;
  } else if (command_line.HasSwitch(switches::kDisableAsyncDns)) {
    allow_async_dns_field_trial = false;
    enable_async = false;
  }

  if (allow_async_dns_field_trial)
    enable_async = chrome_browser_net::ConfigureAsyncDnsFieldTrial();

  scoped_ptr<net::HostResolver> global_host_resolver(
      net::HostResolver::CreateSystemResolver(options, net_log));
  global_host_resolver->SetDnsClientEnabled(enable_async);

  // Determine if we should disable IPv6 support.
  if (!command_line.HasSwitch(switches::kEnableIPv6)) {
    if (command_line.HasSwitch(switches::kDisableIPv6)) {
      global_host_resolver->SetDefaultAddressFamily(net::ADDRESS_FAMILY_IPV4);
    } else {
      global_host_resolver->ProbeIPv6Support();
    }
  }

  // If hostname remappings were specified on the command-line, layer these
  // rules on top of the real host resolver. This allows forwarding all requests
  // through a designated test server.
  if (!command_line.HasSwitch(switches::kHostResolverRules))
    return global_host_resolver.PassAs<net::HostResolver>();

  scoped_ptr<net::MappedHostResolver> remapped_resolver(
      new net::MappedHostResolver(global_host_resolver.Pass()));
  remapped_resolver->SetRulesFromString(
      command_line.GetSwitchValueASCII(switches::kHostResolverRules));
  return remapped_resolver.PassAs<net::HostResolver>();
}

// TODO(willchan): Remove proxy script fetcher context since it's not necessary
// now that I got rid of refcounting URLRequestContexts.
// See IOThread::Globals for details.
net::URLRequestContext*
ConstructProxyScriptFetcherContext(IOThread::Globals* globals,
                                   net::NetLog* net_log) {
  net::URLRequestContext* context = new net::URLRequestContext;
  context->set_net_log(net_log);
  context->set_host_resolver(globals->host_resolver.get());
  context->set_cert_verifier(globals->cert_verifier.get());
  context->set_transport_security_state(
      globals->transport_security_state.get());
  context->set_http_auth_handler_factory(
      globals->http_auth_handler_factory.get());
  context->set_proxy_service(globals->proxy_script_fetcher_proxy_service.get());
  context->set_http_transaction_factory(
      globals->proxy_script_fetcher_http_transaction_factory.get());
  context->set_ftp_transaction_factory(
      globals->proxy_script_fetcher_ftp_transaction_factory.get());
  context->set_cookie_store(globals->system_cookie_store.get());
  context->set_server_bound_cert_service(
      globals->system_server_bound_cert_service.get());
  context->set_network_delegate(globals->system_network_delegate.get());
  context->set_http_user_agent_settings(
      globals->http_user_agent_settings.get());
  // TODO(rtenneti): We should probably use HttpServerPropertiesManager for the
  // system URLRequestContext too. There's no reason this should be tied to a
  // profile.
  return context;
}

net::URLRequestContext*
ConstructSystemRequestContext(IOThread::Globals* globals,
                              net::NetLog* net_log) {
  net::URLRequestContext* context = new SystemURLRequestContext;
  context->set_net_log(net_log);
  context->set_host_resolver(globals->host_resolver.get());
  context->set_cert_verifier(globals->cert_verifier.get());
  context->set_transport_security_state(
      globals->transport_security_state.get());
  context->set_http_auth_handler_factory(
      globals->http_auth_handler_factory.get());
  context->set_proxy_service(globals->system_proxy_service.get());
  context->set_http_transaction_factory(
      globals->system_http_transaction_factory.get());
  context->set_ftp_transaction_factory(
      globals->system_ftp_transaction_factory.get());
  context->set_cookie_store(globals->system_cookie_store.get());
  context->set_server_bound_cert_service(
      globals->system_server_bound_cert_service.get());
  context->set_throttler_manager(globals->throttler_manager.get());
  context->set_network_delegate(globals->system_network_delegate.get());
  context->set_http_user_agent_settings(
      globals->http_user_agent_settings.get());
  return context;
}

}  // namespace

class IOThread::LoggingNetworkChangeObserver
    : public net::NetworkChangeNotifier::IPAddressObserver,
      public net::NetworkChangeNotifier::ConnectionTypeObserver {
 public:
  // |net_log| must remain valid throughout our lifetime.
  explicit LoggingNetworkChangeObserver(net::NetLog* net_log)
      : net_log_(net_log) {
    net::NetworkChangeNotifier::AddIPAddressObserver(this);
    net::NetworkChangeNotifier::AddConnectionTypeObserver(this);
  }

  ~LoggingNetworkChangeObserver() {
    net::NetworkChangeNotifier::RemoveIPAddressObserver(this);
    net::NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
  }

  virtual void OnIPAddressChanged() OVERRIDE {
    VLOG(1) << "Observed a change to the network IP addresses";

    net_log_->AddGlobalEntry(net::NetLog::TYPE_NETWORK_IP_ADDRESSES_CHANGED);
  }

  virtual void OnConnectionTypeChanged(
      net::NetworkChangeNotifier::ConnectionType type) OVERRIDE {
    std::string type_as_string =
        net::NetworkChangeNotifier::ConnectionTypeToString(type);

    VLOG(1) << "Observed a change to network connectivity state "
            << type_as_string;

    net_log_->AddGlobalEntry(
        net::NetLog::TYPE_NETWORK_CONNECTIVITY_CHANGED,
        net::NetLog::StringCallback("new_connection_type", &type_as_string));
  }

 private:
  net::NetLog* net_log_;
  DISALLOW_COPY_AND_ASSIGN(LoggingNetworkChangeObserver);
};

class SystemURLRequestContextGetter : public net::URLRequestContextGetter {
 public:
  explicit SystemURLRequestContextGetter(IOThread* io_thread);

  // Implementation for net::UrlRequestContextGetter.
  virtual net::URLRequestContext* GetURLRequestContext() OVERRIDE;
  virtual scoped_refptr<base::SingleThreadTaskRunner>
      GetNetworkTaskRunner() const OVERRIDE;

 protected:
  virtual ~SystemURLRequestContextGetter();

 private:
  IOThread* const io_thread_;  // Weak pointer, owned by BrowserProcess.
  scoped_refptr<base::SingleThreadTaskRunner> network_task_runner_;

  base::debug::LeakTracker<SystemURLRequestContextGetter> leak_tracker_;
};

SystemURLRequestContextGetter::SystemURLRequestContextGetter(
    IOThread* io_thread)
    : io_thread_(io_thread),
      network_task_runner_(
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO)) {
}

SystemURLRequestContextGetter::~SystemURLRequestContextGetter() {}

net::URLRequestContext* SystemURLRequestContextGetter::GetURLRequestContext() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(io_thread_->globals()->system_request_context.get());

  return io_thread_->globals()->system_request_context.get();
}

scoped_refptr<base::SingleThreadTaskRunner>
SystemURLRequestContextGetter::GetNetworkTaskRunner() const {
  return network_task_runner_;
}

IOThread::Globals::
SystemRequestContextLeakChecker::SystemRequestContextLeakChecker(
    Globals* globals)
    : globals_(globals) {
  DCHECK(globals_);
}

IOThread::Globals::
SystemRequestContextLeakChecker::~SystemRequestContextLeakChecker() {
  if (globals_->system_request_context.get())
    globals_->system_request_context->AssertNoURLRequests();
}

IOThread::Globals::Globals()
    : ALLOW_THIS_IN_INITIALIZER_LIST(
        system_request_context_leak_checker(this)),
      ignore_certificate_errors(false),
      http_pipelining_enabled(false),
      testing_fixed_http_port(0),
      testing_fixed_https_port(0) {}

IOThread::Globals::~Globals() {}

// |local_state| is passed in explicitly in order to (1) reduce implicit
// dependencies and (2) make IOThread more flexible for testing.
IOThread::IOThread(
    PrefService* local_state,
    ChromeNetLog* net_log,
    extensions::EventRouterForwarder* extension_event_router_forwarder)
    : net_log_(net_log),
      extension_event_router_forwarder_(extension_event_router_forwarder),
      globals_(NULL),
      sdch_manager_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {
  // We call RegisterPrefs() here (instead of inside browser_prefs.cc) to make
  // sure that everything is initialized in the right order.
  RegisterPrefs(local_state);
  auth_schemes_ = local_state->GetString(prefs::kAuthSchemes);
  negotiate_disable_cname_lookup_ = local_state->GetBoolean(
      prefs::kDisableAuthNegotiateCnameLookup);
  negotiate_enable_port_ = local_state->GetBoolean(
      prefs::kEnableAuthNegotiatePort);
  auth_server_whitelist_ = local_state->GetString(prefs::kAuthServerWhitelist);
  auth_delegate_whitelist_ = local_state->GetString(
      prefs::kAuthNegotiateDelegateWhitelist);
  gssapi_library_name_ = local_state->GetString(prefs::kGSSAPILibraryName);
  pref_proxy_config_tracker_.reset(
      ProxyServiceFactory::CreatePrefProxyConfigTracker(local_state));
  ChromeNetworkDelegate::InitializePrefsOnUIThread(
      &system_enable_referrers_,
      NULL,
      NULL,
      local_state);
  ssl_config_service_manager_.reset(
      SSLConfigServiceManager::CreateDefaultManager(local_state, NULL));

  BrowserThread::SetDelegate(BrowserThread::IO, this);
}

IOThread::~IOThread() {
  // This isn't needed for production code, but in tests, IOThread may
  // be multiply constructed.
  BrowserThread::SetDelegate(BrowserThread::IO, NULL);

  if (pref_proxy_config_tracker_.get())
    pref_proxy_config_tracker_->DetachFromPrefService();
  DCHECK(!globals_);
}

IOThread::Globals* IOThread::globals() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  return globals_;
}

ChromeNetLog* IOThread::net_log() {
  return net_log_;
}

void IOThread::ChangedToOnTheRecord() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&IOThread::ChangedToOnTheRecordOnIOThread,
                 base::Unretained(this)));
}

net::URLRequestContextGetter* IOThread::system_url_request_context_getter() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!system_url_request_context_getter_) {
    InitSystemRequestContext();
  }
  return system_url_request_context_getter_;
}

void IOThread::Init() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

#if defined(USE_NSS)
  net::SetMessageLoopForNSSHttpIO();
#endif  // defined(USE_NSS)

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  DCHECK(!globals_);
  globals_ = new Globals;

  // Add an observer that will emit network change events to the ChromeNetLog.
  // Assuming NetworkChangeNotifier dispatches in FIFO order, we should be
  // logging the network change before other IO thread consumers respond to it.
  network_change_observer_.reset(
      new LoggingNetworkChangeObserver(net_log_));

  // Setup the HistogramWatcher to run on the IO thread.
  net::NetworkChangeNotifier::InitHistogramWatcher();

  globals_->extension_event_router_forwarder =
      extension_event_router_forwarder_;
  ChromeNetworkDelegate* network_delegate = new ChromeNetworkDelegate(
      extension_event_router_forwarder_,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      &system_enable_referrers_,
      NULL,
      NULL,
      NULL);
  if (command_line.HasSwitch(switches::kDisableExtensionsHttpThrottling))
    network_delegate->NeverThrottleRequests();
  globals_->system_network_delegate.reset(network_delegate);
  globals_->host_resolver = CreateGlobalHostResolver(net_log_);
  globals_->cert_verifier.reset(net::CertVerifier::CreateDefault());
  globals_->transport_security_state.reset(new net::TransportSecurityState());
  globals_->ssl_config_service = GetSSLConfigService();
  if (command_line.HasSwitch(switches::kSpdyProxyOrigin)) {
    spdyproxy_origin_ =
        command_line.GetSwitchValueASCII(switches::kSpdyProxyOrigin);
  }
  globals_->http_auth_handler_factory.reset(CreateDefaultAuthHandlerFactory(
      globals_->host_resolver.get()));
  globals_->http_server_properties.reset(new net::HttpServerPropertiesImpl);
  // For the ProxyScriptFetcher, we use a direct ProxyService.
  globals_->proxy_script_fetcher_proxy_service.reset(
      net::ProxyService::CreateDirectWithNetLog(net_log_));
  // In-memory cookie store.
  globals_->system_cookie_store = new net::CookieMonster(NULL, NULL);
  // In-memory server bound cert store.
  globals_->system_server_bound_cert_service.reset(
      new net::ServerBoundCertService(
          new net::DefaultServerBoundCertStore(NULL),
          base::WorkerPool::GetTaskRunner(true)));
  globals_->dns_probe_service.reset(new chrome_browser_net::DnsProbeService());
  globals_->load_time_stats.reset(new chrome_browser_net::LoadTimeStats());
  globals_->host_mapping_rules.reset(new net::HostMappingRules());
  globals_->http_user_agent_settings.reset(
      new BasicHttpUserAgentSettings(EmptyString(), EmptyString()));
  if (command_line.HasSwitch(switches::kHostRules)) {
    globals_->host_mapping_rules->SetRulesFromString(
        command_line.GetSwitchValueASCII(switches::kHostRules));
  }
  if (command_line.HasSwitch(switches::kIgnoreCertificateErrors))
    globals_->ignore_certificate_errors = true;
  if (command_line.HasSwitch(switches::kEnableHttpPipelining))
    globals_->http_pipelining_enabled = true;
  if (command_line.HasSwitch(switches::kTestingFixedHttpPort)) {
    int value;
    base::StringToInt(
        command_line.GetSwitchValueASCII(
            switches::kTestingFixedHttpPort),
        &value);
    globals_->testing_fixed_http_port = value;
  }
  if (command_line.HasSwitch(switches::kTestingFixedHttpsPort)) {
    int value;
    base::StringToInt(
        command_line.GetSwitchValueASCII(
            switches::kTestingFixedHttpsPort),
        &value);
    globals_->testing_fixed_https_port = value;
  }

  net::HttpNetworkSession::Params session_params;
  session_params.host_resolver = globals_->host_resolver.get();
  session_params.cert_verifier = globals_->cert_verifier.get();
  session_params.server_bound_cert_service =
      globals_->system_server_bound_cert_service.get();
  session_params.transport_security_state =
      globals_->transport_security_state.get();
  session_params.proxy_service =
      globals_->proxy_script_fetcher_proxy_service.get();
  session_params.ssl_config_service = globals_->ssl_config_service.get();
  session_params.http_auth_handler_factory =
      globals_->http_auth_handler_factory.get();
  session_params.http_server_properties =
      globals_->http_server_properties.get();
  session_params.network_delegate = globals_->system_network_delegate.get();
  // TODO(rtenneti): We should probably use HttpServerPropertiesManager for the
  // system URLRequestContext too. There's no reason this should be tied to a
  // profile.
  session_params.net_log = net_log_;
  session_params.host_mapping_rules = globals_->host_mapping_rules.get();
  session_params.ignore_certificate_errors =
      globals_->ignore_certificate_errors;
  session_params.http_pipelining_enabled = globals_->http_pipelining_enabled;
  session_params.testing_fixed_http_port = globals_->testing_fixed_http_port;
  session_params.testing_fixed_https_port = globals_->testing_fixed_https_port;

  scoped_refptr<net::HttpNetworkSession> network_session(
      new net::HttpNetworkSession(session_params));
  globals_->proxy_script_fetcher_http_transaction_factory.reset(
      new net::HttpNetworkLayer(network_session));
  globals_->proxy_script_fetcher_ftp_transaction_factory.reset(
      new net::FtpNetworkLayer(globals_->host_resolver.get()));

  globals_->throttler_manager.reset(new net::URLRequestThrottlerManager());
  globals_->throttler_manager->set_net_log(net_log_);
  // Always done in production, disabled only for unit tests.
  globals_->throttler_manager->set_enable_thread_checks(true);

  globals_->proxy_script_fetcher_context.reset(
      ConstructProxyScriptFetcherContext(globals_, net_log_));

  sdch_manager_ = new net::SdchManager();

#if defined(OS_MACOSX) && !defined(OS_IOS)
  // Start observing Keychain events. This needs to be done on the UI thread,
  // as Keychain services requires a CFRunLoop.
  BrowserThread::PostTask(BrowserThread::UI,
                          FROM_HERE,
                          base::Bind(&ObserveKeychainEvents));
#endif

  // InitSystemRequestContext turns right around and posts a task back
  // to the IO thread, so we can't let it run until we know the IO
  // thread has started.
  //
  // Note that since we are at BrowserThread::Init time, the UI thread
  // is blocked waiting for the thread to start.  Therefore, posting
  // this task to the main thread's message loop here is guaranteed to
  // get it onto the message loop while the IOThread object still
  // exists.  However, the message might not be processed on the UI
  // thread until after IOThread is gone, so use a weak pointer.
  BrowserThread::PostTask(BrowserThread::UI,
                          FROM_HERE,
                          base::Bind(&IOThread::InitSystemRequestContext,
                                     weak_factory_.GetWeakPtr()));

  // We constructed the weak pointer on the IO thread but it will be
  // used on the UI thread.  Call this to avoid a thread checker
  // error.
  weak_factory_.DetachFromThread();
}

void IOThread::CleanUp() {
  base::debug::LeakTracker<SafeBrowsingURLRequestContext>::CheckForLeaks();

  delete sdch_manager_;
  sdch_manager_ = NULL;

#if defined(USE_NSS)
  net::ShutdownNSSHttpIO();
#endif  // defined(USE_NSS)

  system_url_request_context_getter_ = NULL;

  // Release objects that the net::URLRequestContext could have been pointing
  // to.

  // This must be reset before the ChromeNetLog is destroyed.
  network_change_observer_.reset();

  system_proxy_config_service_.reset();

  delete globals_;
  globals_ = NULL;

  base::debug::LeakTracker<SystemURLRequestContextGetter>::CheckForLeaks();
}

// static
void IOThread::RegisterPrefs(PrefService* local_state) {
  local_state->RegisterStringPref(prefs::kAuthSchemes,
                                  "basic,digest,ntlm,negotiate,"
                                  "spdyproxy");
  local_state->RegisterBooleanPref(prefs::kDisableAuthNegotiateCnameLookup,
                                   false);
  local_state->RegisterBooleanPref(prefs::kEnableAuthNegotiatePort, false);
  local_state->RegisterStringPref(prefs::kAuthServerWhitelist, "");
  local_state->RegisterStringPref(prefs::kAuthNegotiateDelegateWhitelist, "");
  local_state->RegisterStringPref(prefs::kGSSAPILibraryName, "");
  local_state->RegisterStringPref(prefs::kSpdyProxyOrigin, "");
  local_state->RegisterBooleanPref(prefs::kEnableReferrers, true);
  local_state->RegisterInt64Pref(prefs::kHttpReceivedContentLength, 0);
  local_state->RegisterInt64Pref(prefs::kHttpOriginalContentLength, 0);
}

net::HttpAuthHandlerFactory* IOThread::CreateDefaultAuthHandlerFactory(
    net::HostResolver* resolver) {
  net::HttpAuthFilterWhitelist* auth_filter_default_credentials = NULL;
  if (!auth_server_whitelist_.empty()) {
    auth_filter_default_credentials =
        new net::HttpAuthFilterWhitelist(auth_server_whitelist_);
  }
  net::HttpAuthFilterWhitelist* auth_filter_delegate = NULL;
  if (!auth_delegate_whitelist_.empty()) {
    auth_filter_delegate =
        new net::HttpAuthFilterWhitelist(auth_delegate_whitelist_);
  }
  globals_->url_security_manager.reset(
      net::URLSecurityManager::Create(auth_filter_default_credentials,
                                      auth_filter_delegate));
  std::vector<std::string> supported_schemes;
  base::SplitString(auth_schemes_, ',', &supported_schemes);

  scoped_ptr<net::HttpAuthHandlerRegistryFactory> registry_factory(
      net::HttpAuthHandlerRegistryFactory::Create(
          supported_schemes, globals_->url_security_manager.get(),
          resolver, gssapi_library_name_, negotiate_disable_cname_lookup_,
          negotiate_enable_port_));

  if (!spdyproxy_origin_.empty()) {
    GURL origin_url(spdyproxy_origin_);
    if (origin_url.is_valid()) {
      registry_factory->RegisterSchemeFactory(
          "spdyproxy",
          new spdyproxy::HttpAuthHandlerSpdyProxy::Factory(origin_url));
    } else {
      LOG(WARNING) << "Skipping creation of SpdyProxy auth handler since "
                   << "authorized origin is invalid: "
                   << spdyproxy_origin_;
    }
  }

  return registry_factory.release();
}

void IOThread::ClearHostCache() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  net::HostCache* host_cache = globals_->host_resolver->GetHostCache();
  if (host_cache)
    host_cache->clear();
}

net::SSLConfigService* IOThread::GetSSLConfigService() {
  return ssl_config_service_manager_->Get();
}

void IOThread::ChangedToOnTheRecordOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Clear the host cache to avoid showing entries from the OTR session
  // in about:net-internals.
  ClearHostCache();
}

void IOThread::InitSystemRequestContext() {
  if (system_url_request_context_getter_)
    return;
  // If we're in unit_tests, IOThread may not be run.
  if (!BrowserThread::IsMessageLoopValid(BrowserThread::IO))
    return;
  bool wait_for_first_update = (pref_proxy_config_tracker_.get() != NULL);
  ChromeProxyConfigService* proxy_config_service =
      ProxyServiceFactory::CreateProxyConfigService(wait_for_first_update);
  system_proxy_config_service_.reset(proxy_config_service);
  if (pref_proxy_config_tracker_.get()) {
    pref_proxy_config_tracker_->SetChromeProxyConfigService(
        proxy_config_service);
  }
  system_url_request_context_getter_ =
      new SystemURLRequestContextGetter(this);
  // Safe to post an unretained this pointer, since IOThread is
  // guaranteed to outlive the IO BrowserThread.
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&IOThread::InitSystemRequestContextOnIOThread,
                 base::Unretained(this)));
}

void IOThread::InitSystemRequestContextOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!globals_->system_proxy_service.get());
  DCHECK(system_proxy_config_service_.get());

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  globals_->system_proxy_service.reset(
      ProxyServiceFactory::CreateProxyService(
          net_log_,
          globals_->proxy_script_fetcher_context.get(),
          system_proxy_config_service_.release(),
          command_line));

  net::HttpNetworkSession::Params system_params;
  system_params.host_resolver = globals_->host_resolver.get();
  system_params.cert_verifier = globals_->cert_verifier.get();
  system_params.server_bound_cert_service =
      globals_->system_server_bound_cert_service.get();
  system_params.transport_security_state =
      globals_->transport_security_state.get();
  system_params.proxy_service = globals_->system_proxy_service.get();
  system_params.ssl_config_service = globals_->ssl_config_service.get();
  system_params.http_auth_handler_factory =
      globals_->http_auth_handler_factory.get();
  system_params.http_server_properties = globals_->http_server_properties.get();
  system_params.network_delegate = globals_->system_network_delegate.get();
  system_params.net_log = net_log_;
  system_params.host_mapping_rules = globals_->host_mapping_rules.get();
  system_params.ignore_certificate_errors = globals_->ignore_certificate_errors;
  system_params.http_pipelining_enabled = globals_->http_pipelining_enabled;
  system_params.testing_fixed_http_port = globals_->testing_fixed_http_port;
  system_params.testing_fixed_https_port = globals_->testing_fixed_https_port;

  globals_->system_http_transaction_factory.reset(
      new net::HttpNetworkLayer(
          new net::HttpNetworkSession(system_params)));
  globals_->system_ftp_transaction_factory.reset(
      new net::FtpNetworkLayer(globals_->host_resolver.get()));
  globals_->system_request_context.reset(
      ConstructSystemRequestContext(globals_, net_log_));

  sdch_manager_->set_sdch_fetcher(
      new SdchDictionaryFetcher(system_url_request_context_getter_.get()));
}
