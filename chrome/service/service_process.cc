// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/service/service_process.h"

#include <algorithm>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/i18n/rtl.h"
#include "base/memory/singleton.h"
#include "base/path_service.h"
#include "base/prefs/json_pref_store.h"
#include "base/string16.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/env_vars.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/service_process_util.h"
#include "chrome/service/cloud_print/cloud_print_proxy.h"
#include "chrome/service/net/service_url_request_context.h"
#include "chrome/service/service_ipc_server.h"
#include "chrome/service/service_process_prefs.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "net/base/network_change_notifier.h"
#include "net/url_request/url_fetcher.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_switches.h"

#if defined(TOOLKIT_GTK)
#include <gtk/gtk.h>
#include "ui/gfx/gtk_util.h"
#endif

ServiceProcess* g_service_process = NULL;

namespace {

// Delay in seconds after the last service is disabled before we attempt
// a shutdown.
const int kShutdownDelaySeconds = 60;

// Delay in hours between launching a browser process to check the
// policy for us.
const int64 kPolicyCheckDelayHours = 8;

const char kDefaultServiceProcessLocale[] = "en-US";

class ServiceIOThread : public base::Thread {
 public:
  explicit ServiceIOThread(const char* name);
  virtual ~ServiceIOThread();

 protected:
  virtual void CleanUp();

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceIOThread);
};

ServiceIOThread::ServiceIOThread(const char* name) : base::Thread(name) {}
ServiceIOThread::~ServiceIOThread() {
  Stop();
}

void ServiceIOThread::CleanUp() {
  net::URLFetcher::CancelAll();
}

// Prepares the localized strings that are going to be displayed to
// the user if the service process dies. These strings are stored in the
// environment block so they are accessible in the early stages of the
// chrome executable's lifetime.
void PrepareRestartOnCrashEnviroment(
    const CommandLine &parsed_command_line) {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  // Clear this var so child processes don't show the dialog by default.
  env->UnSetVar(env_vars::kShowRestart);

  // For non-interactive tests we don't restart on crash.
  if (env->HasVar(env_vars::kHeadless))
    return;

  // If the known command-line test options are used we don't create the
  // environment block which means we don't get the restart dialog.
  if (parsed_command_line.HasSwitch(switches::kNoErrorDialogs))
    return;

  // The encoding we use for the info is "title|context|direction" where
  // direction is either env_vars::kRtlLocale or env_vars::kLtrLocale depending
  // on the current locale.
  string16 dlg_strings(l10n_util::GetStringUTF16(IDS_CRASH_RECOVERY_TITLE));
  dlg_strings.push_back('|');
  string16 adjusted_string(
      l10n_util::GetStringFUTF16(IDS_SERVICE_CRASH_RECOVERY_CONTENT,
      l10n_util::GetStringUTF16(IDS_GOOGLE_CLOUD_PRINT)));
  base::i18n::AdjustStringForLocaleDirection(&adjusted_string);
  dlg_strings.append(adjusted_string);
  dlg_strings.push_back('|');
  dlg_strings.append(ASCIIToUTF16(
      base::i18n::IsRTL() ? env_vars::kRtlLocale : env_vars::kLtrLocale));

  env->SetVar(env_vars::kRestartInfo, UTF16ToUTF8(dlg_strings));
}

}  // namespace

ServiceProcess::ServiceProcess()
  : shutdown_event_(true, false),
    main_message_loop_(NULL),
    enabled_services_(0),
    update_available_(false) {
  DCHECK(!g_service_process);
  g_service_process = this;
}

bool ServiceProcess::Initialize(MessageLoopForUI* message_loop,
                                const CommandLine& command_line,
                                ServiceProcessState* state) {
#if defined(TOOLKIT_GTK)
  // TODO(jamiewalch): Calling GtkInitFromCommandLine here causes the process
  // to abort if run headless. The correct fix for this is to refactor the
  // service process to be more modular, a task that is currently underway.
  // However, since this problem is blocking cloud print, the following quick
  // hack will have to do. Note that the situation with this hack in place is
  // no worse than it was when we weren't initializing GTK at all.
  int argc = 1;
  scoped_array<char*> argv(new char*[2]);
  argv[0] = strdup(command_line.argv()[0].c_str());
  argv[1] = NULL;
  char **argv_pointer = argv.get();
  gtk_init_check(&argc, &argv_pointer);
  free(argv[0]);
#endif
  main_message_loop_ = message_loop;
  service_process_state_.reset(state);
  network_change_notifier_.reset(net::NetworkChangeNotifier::Create());
  base::Thread::Options options;
  options.message_loop_type = MessageLoop::TYPE_IO;
  io_thread_.reset(new ServiceIOThread("ServiceProcess_IO"));
  file_thread_.reset(new base::Thread("ServiceProcess_File"));
  if (!io_thread_->StartWithOptions(options) ||
      !file_thread_->StartWithOptions(options)) {
    NOTREACHED();
    Teardown();
    return false;
  }
  blocking_pool_ = new base::SequencedWorkerPool(3, "ServiceBlocking");

  request_context_getter_ = new ServiceURLRequestContextGetter();

  FilePath user_data_dir;
  PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  FilePath pref_path = user_data_dir.Append(chrome::kServiceStateFileName);
  service_prefs_.reset(
      new ServiceProcessPrefs(
          pref_path,
          JsonPrefStore::GetTaskRunnerForFile(pref_path, blocking_pool_)));
  service_prefs_->ReadPrefs();

  // Check if a locale override has been specified on the command-line.
  std::string locale = command_line.GetSwitchValueASCII(switches::kLang);
  if (!locale.empty()) {
    service_prefs_->SetString(prefs::kApplicationLocale, locale);
    service_prefs_->WritePrefs();
  } else {
    // If no command-line value was specified, read the last used locale from
    // the prefs.
    locale = service_prefs_->GetString(prefs::kApplicationLocale, "");
    // If no locale was specified anywhere, use the default one.
    if (locale.empty())
      locale = kDefaultServiceProcessLocale;
  }
  ResourceBundle::InitSharedInstanceWithLocale(locale, NULL);

  PrepareRestartOnCrashEnviroment(command_line);

  // Enable Cloud Print if needed. First check the command-line.
  // Then check if the cloud print proxy was previously enabled.
  if (command_line.HasSwitch(switches::kEnableCloudPrintProxy) ||
      service_prefs_->GetBoolean(prefs::kCloudPrintProxyEnabled, false)) {
    GetCloudPrintProxy()->EnableForUser(std::string());
  }

  VLOG(1) << "Starting Service Process IPC Server";
  ipc_server_.reset(new ServiceIPCServer(
      service_process_state_->GetServiceProcessChannel()));
  ipc_server_->Init();

  // After the IPC server has started we signal that the service process is
  // ready.
  if (!service_process_state_->SignalReady(
      io_thread_->message_loop_proxy(),
      base::Bind(&ServiceProcess::Terminate, base::Unretained(this)))) {
    return false;
  }

  // See if we need to stay running.
  ScheduleShutdownCheck();

  // Occasionally check to see if we need to launch the browser to get the
  // policy state information.
  CloudPrintPolicyCheckIfNeeded();
  return true;
}

bool ServiceProcess::Teardown() {
  service_prefs_.reset();
  cloud_print_proxy_.reset();

  ipc_server_.reset();
  // Signal this event before shutting down the service process. That way all
  // background threads can cleanup.
  shutdown_event_.Signal();
  io_thread_.reset();
  file_thread_.reset();

  if (blocking_pool_.get()) {
    blocking_pool_->Shutdown();
    blocking_pool_ = NULL;
  }

  // The NetworkChangeNotifier must be destroyed after all other threads that
  // might use it have been shut down.
  network_change_notifier_.reset();

  service_process_state_->SignalStopped();
  return true;
}

// This method is called when a shutdown command is received from IPC channel
// or there was an error in the IPC channel.
void ServiceProcess::Shutdown() {
#if defined(OS_MACOSX)
  // On MacOS X the service must be removed from the launchd job list.
  // http://www.chromium.org/developers/design-documents/service-processes
  // The best way to do that is to go through the ForceServiceProcessShutdown
  // path. If it succeeds Terminate() will be called from the handler registered
  // via service_process_state_->SignalReady().
  // On failure call Terminate() directly to force the process to actually
  // terminate.
  if (!ForceServiceProcessShutdown("", 0)) {
    Terminate();
  }
#else
  Terminate();
#endif
}

void ServiceProcess::Terminate() {
  main_message_loop_->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}

bool ServiceProcess::HandleClientDisconnect() {
  // If there are no enabled services or if there is an update available
  // we want to shutdown right away. Else we want to keep listening for
  // new connections.
  if (!enabled_services_ || update_available()) {
    Shutdown();
    return false;
  }
  return true;
}

CloudPrintProxy* ServiceProcess::GetCloudPrintProxy() {
  if (!cloud_print_proxy_.get()) {
    cloud_print_proxy_.reset(new CloudPrintProxy());
    cloud_print_proxy_->Initialize(service_prefs_.get(), this);
  }
  return cloud_print_proxy_.get();
}

void ServiceProcess::OnCloudPrintProxyEnabled(bool persist_state) {
  if (persist_state) {
    // Save the preference that we have enabled the cloud print proxy.
    service_prefs_->SetBoolean(prefs::kCloudPrintProxyEnabled, true);
    service_prefs_->WritePrefs();
  }
  OnServiceEnabled();
}

void ServiceProcess::OnCloudPrintProxyDisabled(bool persist_state) {
  if (persist_state) {
    // Save the preference that we have disabled the cloud print proxy.
    service_prefs_->SetBoolean(prefs::kCloudPrintProxyEnabled, false);
    service_prefs_->WritePrefs();
  }
  OnServiceDisabled();
}

ServiceURLRequestContextGetter*
ServiceProcess::GetServiceURLRequestContextGetter() {
  DCHECK(request_context_getter_.get());
  return request_context_getter_.get();
}

void ServiceProcess::OnServiceEnabled() {
  enabled_services_++;
  if ((1 == enabled_services_) &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kNoServiceAutorun)) {
    if (!service_process_state_->AddToAutoRun()) {
      // TODO(scottbyer/sanjeevr/dmaclach): Handle error condition
      LOG(ERROR) << "Unable to AddToAutoRun";
    }
  }
}

void ServiceProcess::OnServiceDisabled() {
  DCHECK_NE(enabled_services_, 0);
  enabled_services_--;
  if (0 == enabled_services_) {
    if (!service_process_state_->RemoveFromAutoRun()) {
      // TODO(scottbyer/sanjeevr/dmaclach): Handle error condition
      LOG(ERROR) << "Unable to RemoveFromAutoRun";
    }
    // We will wait for some time to respond to IPCs before shutting down.
    ScheduleShutdownCheck();
  }
}

void ServiceProcess::ScheduleShutdownCheck() {
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ServiceProcess::ShutdownIfNeeded, base::Unretained(this)),
      base::TimeDelta::FromSeconds(kShutdownDelaySeconds));
}

void ServiceProcess::ShutdownIfNeeded() {
  if (0 == enabled_services_) {
    if (ipc_server_->is_client_connected()) {
      // If there is a client connected, we need to try again later.
      // Note that there is still a timing window here because a client may
      // decide to connect at this point.
      // TODO(sanjeevr): Fix this timing window.
      ScheduleShutdownCheck();
    } else {
      Shutdown();
    }
  }
}

void ServiceProcess::ScheduleCloudPrintPolicyCheck() {
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ServiceProcess::CloudPrintPolicyCheckIfNeeded,
                 base::Unretained(this)),
      base::TimeDelta::FromHours(kPolicyCheckDelayHours));
}

void ServiceProcess::CloudPrintPolicyCheckIfNeeded() {
  if (enabled_services_ && !ipc_server_->is_client_connected()) {
    GetCloudPrintProxy()->CheckCloudPrintProxyPolicy();
  }
  ScheduleCloudPrintPolicyCheck();
}

ServiceProcess::~ServiceProcess() {
  Teardown();
  g_service_process = NULL;
}
