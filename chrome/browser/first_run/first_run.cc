// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/first_run/first_run.h"

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/file_util.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/first_run/first_run_dialog.h"
#include "chrome/browser/first_run/first_run_import_observer.h"
#include "chrome/browser/first_run/first_run_internal.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_host.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/importer/importer_progress_dialog.h"
#include "chrome/browser/importer/importer_progress_observer.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/process_singleton.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/global_error/global_error_service.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/startup_metric_utils.h"
#include "chrome/common/url_constants.h"
#include "chrome/installer/util/master_preferences.h"
#include "chrome/installer/util/master_preferences_constants.h"
#include "chrome/installer/util/util_constants.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "googleurl/src/gurl.h"

using content::UserMetricsAction;

namespace {

FilePath GetDefaultPrefFilePath(bool create_profile_dir,
                                const FilePath& user_data_dir) {
  FilePath default_pref_dir =
      ProfileManager::GetDefaultProfileDir(user_data_dir);
  if (create_profile_dir) {
    if (!file_util::PathExists(default_pref_dir)) {
      if (!file_util::CreateDirectory(default_pref_dir))
        return FilePath();
    }
  }
  return ProfileManager::GetProfilePrefsPath(default_pref_dir);
}

// Sets the |items| bitfield according to whether the import data specified by
// |import_type| should be be auto imported or not.
void SetImportItem(PrefService* user_prefs,
                   const char* pref_path,
                   int import_items,
                   int dont_import_items,
                   importer::ImportItem import_type,
                   int& items) {
  // Work out whether an item is to be imported according to what is specified
  // in master preferences.
  bool should_import = false;
  bool master_pref_set =
      ((import_items | dont_import_items) & import_type) != 0;
  bool master_pref = ((import_items & ~dont_import_items) & import_type) != 0;

  if (import_type == importer::HISTORY ||
      ((import_type != importer::FAVORITES) &&
      first_run::internal::IsOrganicFirstRun())) {
    // History is always imported unless turned off in master_preferences.
    // Search engines are only imported in certain builds unless overridden
    // in master_preferences.Home page is imported in organic builds only unless
    // turned off in master_preferences.
    should_import = !master_pref_set || master_pref;
  } else {
    // Bookmarks are never imported, unless turned on in master_preferences.
    // Search engine and home page import behaviour is similar in non organic
    // builds.
    should_import = master_pref_set && master_pref;
  }

  // If an import policy is set, import items according to policy. If no master
  // preference is set, but a corresponding recommended policy is set, import
  // item according to recommended policy. If both a master preference and a
  // recommended policy is set, the master preference wins. If neither
  // recommended nor managed policies are set, import item according to what we
  // worked out above.
  if (master_pref_set)
    user_prefs->SetBoolean(pref_path, should_import);

  if (!user_prefs->FindPreference(pref_path)->IsDefaultValue()) {
    if (user_prefs->GetBoolean(pref_path))
      items |= import_type;
  } else { // no policy (recommended or managed) is set
    if (should_import)
      items |= import_type;
  }

  user_prefs->ClearPref(pref_path);
}

// Imports bookmarks from an html file. The path to the file is provided in
// the command line.
int ImportFromFile(Profile* profile, const CommandLine& cmdline) {
  FilePath file_path = cmdline.GetSwitchValuePath(switches::kImportFromFile);
  if (file_path.empty()) {
    NOTREACHED();
    return false;
  }
  scoped_refptr<ImporterHost> importer_host(new ImporterHost);
  importer_host->set_headless();

  importer::SourceProfile source_profile;
  source_profile.importer_type = importer::TYPE_BOOKMARKS_FILE;
  source_profile.source_path = file_path;

  FirstRunImportObserver importer_observer;
  importer::ShowImportProgressDialog(importer::FAVORITES,
                                     importer_host,
                                     &importer_observer,
                                     source_profile,
                                     profile,
                                     true);

  importer_observer.RunLoop();
  return importer_observer.import_result();
}

}  // namespace

namespace first_run {
namespace internal {

FirstRunState first_run_ = FIRST_RUN_UNKNOWN;

installer::MasterPreferences* LoadMasterPrefs(FilePath* master_prefs_path)
{
  *master_prefs_path = FilePath(MasterPrefsPath());
  if (master_prefs_path->empty())
    return NULL;
  installer::MasterPreferences* install_prefs =
      new installer::MasterPreferences(*master_prefs_path);
  if (!install_prefs->read_from_file()) {
    delete install_prefs;
    return NULL;
  }

  return install_prefs;
}

bool CopyPrefFile(const FilePath& user_data_dir,
                  const FilePath& master_prefs_path) {
  FilePath user_prefs = GetDefaultPrefFilePath(true, user_data_dir);
  if (user_prefs.empty())
    return false;

  // The master prefs are regular prefs so we can just copy the file
  // to the default place and they just work.
  return file_util::CopyFile(master_prefs_path, user_prefs);
}

void SetupMasterPrefsFromInstallPrefs(
    MasterPrefs* out_prefs,
    installer::MasterPreferences* install_prefs) {
  bool value = false;
  if (install_prefs->GetBool(
          installer::master_preferences::kDistroImportSearchPref, &value)) {
    if (value) {
      out_prefs->do_import_items |= importer::SEARCH_ENGINES;
    } else {
      out_prefs->dont_import_items |= importer::SEARCH_ENGINES;
    }
  }

  // If we're suppressing the first-run bubble, set that preference now.
  // Otherwise, wait until the user has completed first run to set it, so the
  // user is guaranteed to see the bubble iff he or she has completed the first
  // run process.
  if (install_prefs->GetBool(
          installer::master_preferences::kDistroSuppressFirstRunBubble,
          &value) && value)
    SetShowFirstRunBubblePref(false);

  if (install_prefs->GetBool(
          installer::master_preferences::kDistroImportHistoryPref,
          &value)) {
    if (value) {
      out_prefs->do_import_items |= importer::HISTORY;
    } else {
      out_prefs->dont_import_items |= importer::HISTORY;
    }
  }

  std::string not_used;
  out_prefs->homepage_defined = install_prefs->GetString(
      prefs::kHomePage, &not_used);

  if (install_prefs->GetBool(
          installer::master_preferences::kDistroImportHomePagePref,
          &value)) {
    if (value) {
      out_prefs->do_import_items |= importer::HOME_PAGE;
    } else {
      out_prefs->dont_import_items |= importer::HOME_PAGE;
    }
  }

  // Bookmarks are never imported unless specifically turned on.
  if (install_prefs->GetBool(
          installer::master_preferences::kDistroImportBookmarksPref,
          &value)) {
    if (value)
      out_prefs->do_import_items |= importer::FAVORITES;
    else
      out_prefs->dont_import_items |= importer::FAVORITES;
  }

  if (install_prefs->GetBool(
          installer::master_preferences::kMakeChromeDefaultForUser,
          &value) && value) {
    out_prefs->make_chrome_default = true;
  }

  if (install_prefs->GetBool(
          installer::master_preferences::kSuppressFirstRunDefaultBrowserPrompt,
          &value) && value) {
    out_prefs->suppress_first_run_default_browser_prompt = true;
  }
}

void SetDefaultBrowser(installer::MasterPreferences* install_prefs){
  // Even on the first run we only allow for the user choice to take effect if
  // no policy has been set by the admin.
  if (!g_browser_process->local_state()->IsManagedPreference(
          prefs::kDefaultBrowserSettingEnabled)) {
    bool value = false;
    if (install_prefs->GetBool(
            installer::master_preferences::kMakeChromeDefaultForUser,
            &value) && value) {
      ShellIntegration::SetAsDefaultBrowser();
    }
  } else {
    if (g_browser_process->local_state()->GetBoolean(
            prefs::kDefaultBrowserSettingEnabled)) {
      ShellIntegration::SetAsDefaultBrowser();
    }
  }
}

void SetShowWelcomePagePrefIfNeeded(
    installer::MasterPreferences* install_prefs) {
  bool value = false;
  if (install_prefs->GetBool(
          installer::master_preferences::kDistroShowWelcomePage, &value)
          && value) {
    SetShowWelcomePagePref();
  }
}

bool SkipFirstRunUI(installer::MasterPreferences* install_prefs) {
  bool value = false;
  install_prefs->GetBool(installer::master_preferences::kDistroSkipFirstRunPref,
                         &value);
  return value;
}

void SetRLZPref(first_run::MasterPrefs* out_prefs,
                installer::MasterPreferences* install_prefs) {
  if (!install_prefs->GetInt(installer::master_preferences::kDistroPingDelay,
                    &out_prefs->ping_delay)) {
    // Default value in case master preferences is missing or corrupt,
    // or ping_delay is missing.
    out_prefs->ping_delay = 90;
  }
}

// -- Platform-specific functions --

#if !defined(OS_LINUX) && !defined(OS_BSD)
bool IsOrganicFirstRun() {
  std::string brand;
  google_util::GetBrand(&brand);
  return google_util::IsOrganicFirstRun(brand);
}
#endif

#if !defined(USE_AURA)
void AutoImportPlatformCommon(
    scoped_refptr<ImporterHost> importer_host,
    Profile* profile,
    bool homepage_defined,
    int import_items,
    int dont_import_items,
    bool make_chrome_default) {
  FilePath local_state_path;
  PathService::Get(chrome::FILE_LOCAL_STATE, &local_state_path);
  bool local_state_file_exists = file_util::PathExists(local_state_path);

  scoped_refptr<ImporterList> importer_list(new ImporterList(NULL));
  importer_list->DetectSourceProfilesHack();

  // Do import if there is an available profile for us to import.
  if (importer_list->count() > 0) {
    // Don't show the warning dialog if import fails.
    importer_host->set_headless();
    int items = 0;

    if (IsOrganicFirstRun()) {
      // Home page is imported in organic builds only unless turned off or
      // defined in master_preferences.
      if (homepage_defined) {
        dont_import_items |= importer::HOME_PAGE;
        if (import_items & importer::HOME_PAGE)
          import_items &= ~importer::HOME_PAGE;
      }
      // Search engines are not imported automatically in organic builds if the
      // user already has a user preferences directory.
      if (local_state_file_exists) {
        dont_import_items |= importer::SEARCH_ENGINES;
        if (import_items & importer::SEARCH_ENGINES)
          import_items &= ~importer::SEARCH_ENGINES;
      }
    }

    PrefService* user_prefs = profile->GetPrefs();

    SetImportItem(user_prefs,
                  prefs::kImportHistory,
                  import_items,
                  dont_import_items,
                  importer::HISTORY,
                  items);
    SetImportItem(user_prefs,
                  prefs::kImportHomepage,
                  import_items,
                  dont_import_items,
                  importer::HOME_PAGE,
                  items);
    SetImportItem(user_prefs,
                  prefs::kImportSearchEngine,
                  import_items,
                  dont_import_items,
                  importer::SEARCH_ENGINES,
                  items);
    SetImportItem(user_prefs,
                  prefs::kImportBookmarks,
                  import_items,
                  dont_import_items,
                  importer::FAVORITES,
                  items);

    ImportSettings(profile, importer_host, importer_list, items);
  }

  content::RecordAction(UserMetricsAction("FirstRunDef_Accept"));

  // Launch the first run dialog only for certain builds, and only if the user
  // has not already set preferences.
  if (IsOrganicFirstRun() && !local_state_file_exists) {
    startup_metric_utils::SetNonBrowserUIDisplayed();
    ShowFirstRunDialog(profile);
  }

  if (make_chrome_default &&
      ShellIntegration::CanSetAsDefaultBrowser() ==
          ShellIntegration::SET_DEFAULT_UNATTENDED) {
    ShellIntegration::SetAsDefaultBrowser();
  }

  // Display the first run bubble if there is a default search provider.
  TemplateURLService* template_url =
      TemplateURLServiceFactory::GetForProfile(profile);
  if (template_url && template_url->GetDefaultSearchProvider())
    FirstRunBubbleLauncher::ShowFirstRunBubbleSoon();
  SetShowWelcomePagePref();
  SetPersonalDataManagerFirstRunPref();
}
#endif  // !defined(USE_AURA)

int ImportBookmarkFromFileIfNeeded(Profile* profile,
                                   const CommandLine& cmdline) {
  int return_code = true;
  if (cmdline.HasSwitch(switches::kImportFromFile)) {
    // Silently import preset bookmarks from file.
    // This is an OEM scenario.
    return_code = ImportFromFile(profile, cmdline);
  }
  return return_code;
}

}  // namespace internal
}  // namespace first_run

namespace first_run {

MasterPrefs::MasterPrefs()
    : ping_delay(0),
      homepage_defined(false),
      do_import_items(0),
      dont_import_items(0),
      make_chrome_default(false),
      suppress_first_run_default_browser_prompt(false) {
}

MasterPrefs::~MasterPrefs() {}

bool IsChromeFirstRun() {
  if (internal::first_run_ != internal::FIRST_RUN_UNKNOWN)
    return internal::first_run_ == internal::FIRST_RUN_TRUE;

  FilePath first_run_sentinel;
  if (!internal::GetFirstRunSentinelFilePath(&first_run_sentinel) ||
      file_util::PathExists(first_run_sentinel)) {
    internal::first_run_ = internal::FIRST_RUN_FALSE;
    return false;
  }
  internal::first_run_ = internal::FIRST_RUN_TRUE;
  return true;
}

bool CreateSentinel() {
  FilePath first_run_sentinel;
  if (!internal::GetFirstRunSentinelFilePath(&first_run_sentinel))
    return false;
  return file_util::WriteFile(first_run_sentinel, "", 0) != -1;
}

std::string GetPingDelayPrefName() {
  return base::StringPrintf("%s.%s",
                            installer::master_preferences::kDistroDict,
                            installer::master_preferences::kDistroPingDelay);
}

void RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterIntegerPref(GetPingDelayPrefName().c_str(),
                             0,
                             PrefService::UNSYNCABLE_PREF);
}

bool RemoveSentinel() {
  FilePath first_run_sentinel;
  if (!internal::GetFirstRunSentinelFilePath(&first_run_sentinel))
    return false;
  return file_util::Delete(first_run_sentinel, false);
}

bool SetShowFirstRunBubblePref(bool show_bubble) {
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state)
    return false;
  local_state->SetBoolean(prefs::kShouldShowFirstRunBubble, show_bubble);
  return true;
}

bool SetShowWelcomePagePref() {
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state)
    return false;
  if (!local_state->FindPreference(prefs::kShouldShowWelcomePage)) {
    local_state->RegisterBooleanPref(prefs::kShouldShowWelcomePage, false);
    local_state->SetBoolean(prefs::kShouldShowWelcomePage, true);
  }
  return true;
}

bool SetPersonalDataManagerFirstRunPref() {
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state)
    return false;
  if (!local_state->FindPreference(
          prefs::kAutofillPersonalDataManagerFirstRun)) {
    local_state->RegisterBooleanPref(
        prefs::kAutofillPersonalDataManagerFirstRun, false);
    local_state->SetBoolean(prefs::kAutofillPersonalDataManagerFirstRun, true);
  }
  return true;
}

void LogFirstRunMetric(FirstRunBubbleMetric metric) {
  UMA_HISTOGRAM_ENUMERATION("FirstRun.SearchEngineBubble", metric,
                            NUM_FIRST_RUN_BUBBLE_METRICS);
}

// static
void FirstRunBubbleLauncher::ShowFirstRunBubbleSoon() {
  SetShowFirstRunBubblePref(true);
  // This FirstRunBubbleLauncher instance will manage its own lifetime.
  new FirstRunBubbleLauncher();
}

FirstRunBubbleLauncher::FirstRunBubbleLauncher() {
  registrar_.Add(this, content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
                 content::NotificationService::AllSources());
}

FirstRunBubbleLauncher::~FirstRunBubbleLauncher() {}

void FirstRunBubbleLauncher::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(type, content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME);
  Browser* browser = browser::FindBrowserWithWebContents(
      content::Source<content::WebContents>(source).ptr());
  if (!browser || !browser->is_type_tabbed())
    return;

  // Check the preference to determine if the bubble should be shown.
  PrefService* prefs = g_browser_process->local_state();
  if (!prefs || !prefs->GetBoolean(prefs::kShouldShowFirstRunBubble)) {
    delete this;
    return;
  }

  content::WebContents* contents = chrome::GetActiveWebContents(browser);

  // Suppress the first run bubble if a Gaia sign in page is showing.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kUseWebBasedSigninFlow) &&
      gaia::IsGaiaSignonRealm(contents->GetURL().GetOrigin())) {
      return;
  }

  if (contents && contents->GetURL().SchemeIs(chrome::kChromeUIScheme)) {
    // Suppress the first run bubble if the sync promo is showing.
    if (contents->GetURL().host() == chrome::kChromeUISyncPromoHost)
      return;

    // Suppress the first run bubble if 'make chrome metro' flow is showing.
    if (contents->GetURL().host() == chrome::kChromeUIMetroFlowHost)
      return;

    // Suppress the first run bubble if the NTP sync promo bubble is showing.
    if (contents->GetURL().host() == chrome::kChromeUINewTabHost) {
      NewTabUI* new_tab_ui =
          NewTabUI::FromWebUIController(contents->GetWebUI()->GetController());
      if (new_tab_ui && new_tab_ui->showing_sync_bubble())
        return;
    }
  }

  // Suppress the first run bubble if a global error bubble is pending.
  GlobalErrorService* global_error_service =
      GlobalErrorServiceFactory::GetForProfile(browser->profile());
  if (global_error_service->GetFirstGlobalErrorWithBubbleView() != NULL)
    return;

  // Reset the preference and notifications to avoid showing the bubble again.
  prefs->SetBoolean(prefs::kShouldShowFirstRunBubble, false);

  // Show the bubble now and destroy this bubble launcher.
  browser->ShowFirstRunBubble();
  delete this;
}

}  // namespace first_run
