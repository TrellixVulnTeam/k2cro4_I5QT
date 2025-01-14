// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/ref_counted.h"
#include "base/stl_util.h"
#include "chrome/browser/extensions/autoupdate_interceptor.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/extensions/updater/extension_downloader.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/test/browser_test_utils.h"
#include "net/url_request/url_fetcher.h"

using extensions::Extension;

class ExtensionManagementTest : public ExtensionBrowserTest {
 protected:
  // Helper method that returns whether the extension is at the given version.
  // This calls version(), which must be defined in the extension's bg page,
  // as well as asking the extension itself.
  //
  // Note that 'version' here means something different than the version field
  // in the extension's manifest. We use the version as reported by the
  // background page to test how overinstalling crx files with the same
  // manifest version works.
  bool IsExtensionAtVersion(const Extension* extension,
                            const std::string& expected_version) {
    // Test that the extension's version from the manifest and reported by the
    // background page is correct.  This is to ensure that the processes are in
    // sync with the Extension.
    ExtensionProcessManager* manager =
        extensions::ExtensionSystem::Get(browser()->profile())->
            process_manager();
    extensions::ExtensionHost* ext_host =
        manager->GetBackgroundHostForExtension(extension->id());
    EXPECT_TRUE(ext_host);
    if (!ext_host)
      return false;

    std::string version_from_bg;
    bool exec = content::ExecuteJavaScriptAndExtractString(
        ext_host->render_view_host(), L"", L"version()", &version_from_bg);
    EXPECT_TRUE(exec);
    if (!exec)
      return false;

    if (version_from_bg != expected_version ||
        extension->VersionString() != expected_version)
      return false;
    return true;
  }
};

#if defined(OS_LINUX)
// Times out sometimes on Linux.  http://crbug.com/89727
#define MAYBE_InstallSameVersion DISABLED_InstallSameVersion
#else
#define MAYBE_InstallSameVersion InstallSameVersion
#endif

// Tests that installing the same version overwrites.
IN_PROC_BROWSER_TEST_F(ExtensionManagementTest, MAYBE_InstallSameVersion) {
  const Extension* extension = InstallExtension(
      test_data_dir_.AppendASCII("install/install.crx"), 1);
  ASSERT_TRUE(extension);
  FilePath old_path = extension->path();

  // Install an extension with the same version. The previous install should be
  // overwritten.
  extension = InstallExtension(
      test_data_dir_.AppendASCII("install/install_same_version.crx"), 0);
  ASSERT_TRUE(extension);
  FilePath new_path = extension->path();

  EXPECT_FALSE(IsExtensionAtVersion(extension, "1.0"));
  EXPECT_NE(old_path.value(), new_path.value());
}

IN_PROC_BROWSER_TEST_F(ExtensionManagementTest, InstallOlderVersion) {
  const Extension* extension = InstallExtension(
      test_data_dir_.AppendASCII("install/install.crx"), 1);
  ASSERT_TRUE(extension);
  ASSERT_FALSE(InstallExtension(
      test_data_dir_.AppendASCII("install/install_older_version.crx"), 0));
  EXPECT_TRUE(IsExtensionAtVersion(extension, "1.0"));
}

IN_PROC_BROWSER_TEST_F(ExtensionManagementTest, InstallThenCancel) {
  const Extension* extension = InstallExtension(
      test_data_dir_.AppendASCII("install/install.crx"), 1);
  ASSERT_TRUE(extension);

  // Cancel this install.
  ASSERT_FALSE(StartInstallButCancel(
      test_data_dir_.AppendASCII("install/install_v2.crx")));
  EXPECT_TRUE(IsExtensionAtVersion(extension, "1.0"));
}

#if defined(OS_WIN)
// http://crbug.com/141913
#define MAYBE_InstallRequiresConfirm FLAKY_InstallRequiresConfirm
#else
#define MAYBE_InstallRequiresConfirm InstallRequiresConfirm
#endif
IN_PROC_BROWSER_TEST_F(ExtensionManagementTest, MAYBE_InstallRequiresConfirm) {
  // Installing the extension without an auto confirming UI should result in
  // it being disabled, since good.crx has permissions that require approval.
  ExtensionService* service = browser()->profile()->GetExtensionService();
  std::string id = "ldnnhddmnhbkjipkidpdiheffobcpfmf";
  ASSERT_FALSE(InstallExtension(test_data_dir_.AppendASCII("good.crx"), 0));
  ASSERT_TRUE(service->GetExtensionById(id, true));
  UninstallExtension(id);

  // And the install should succeed when the permissions are accepted.
  ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(
      test_data_dir_.AppendASCII("good.crx"), 1, browser()));
  UninstallExtension(id);
}

// Tests that disabling and re-enabling an extension works.
IN_PROC_BROWSER_TEST_F(ExtensionManagementTest, DisableEnable) {
  ExtensionProcessManager* manager =
      extensions::ExtensionSystem::Get(browser()->profile())->process_manager();
  ExtensionService* service = browser()->profile()->GetExtensionService();
  const size_t size_before = service->extensions()->size();

  // Load an extension, expect the background page to be available.
  std::string extension_id = "bjafgdebaacbbbecmhlhpofkepfkgcpa";
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("good").AppendASCII("Extensions")
                    .AppendASCII(extension_id)
                    .AppendASCII("1.0")));
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  EXPECT_EQ(0u, service->disabled_extensions()->size());
  EXPECT_TRUE(manager->GetBackgroundHostForExtension(extension_id));

  // After disabling, the background page should go away.
  DisableExtension(extension_id);
  EXPECT_EQ(size_before, service->extensions()->size());
  EXPECT_EQ(1u, service->disabled_extensions()->size());
  EXPECT_FALSE(manager->GetBackgroundHostForExtension(extension_id));

  // And bring it back.
  EnableExtension(extension_id);
  EXPECT_EQ(size_before + 1, service->extensions()->size());
  EXPECT_EQ(0u, service->disabled_extensions()->size());
  EXPECT_TRUE(manager->GetBackgroundHostForExtension(extension_id));
}

// Used for testing notifications sent during extension updates.
class NotificationListener : public content::NotificationObserver {
 public:
  NotificationListener() : started_(false), finished_(false) {
    int types[] = {
      chrome::NOTIFICATION_EXTENSION_UPDATING_STARTED,
      chrome::NOTIFICATION_EXTENSION_UPDATE_FOUND
    };
    for (size_t i = 0; i < arraysize(types); i++) {
      registrar_.Add(
          this, types[i], content::NotificationService::AllSources());
    }
  }
  ~NotificationListener() {}

  bool started() { return started_; }

  bool finished() { return finished_; }

  const std::set<std::string>& updates() { return updates_; }

  void Reset() {
    started_ = false;
    finished_ = false;
    updates_.clear();
  }

  // Implements content::NotificationObserver interface.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) {
    switch (type) {
      case chrome::NOTIFICATION_EXTENSION_UPDATING_STARTED: {
        EXPECT_FALSE(started_);
        started_ = true;
        break;
      }
      case chrome::NOTIFICATION_EXTENSION_UPDATE_FOUND: {
        const std::string& id =
            content::Details<extensions::UpdateDetails>(details)->id;
        updates_.insert(id);
        break;
      }
      default:
        NOTREACHED();
    }
  }

  void OnFinished() {
    EXPECT_FALSE(finished_);
    finished_ = true;
  }

 private:
  content::NotificationRegistrar registrar_;

  // Did we see EXTENSION_UPDATING_STARTED?
  bool started_;

  // Did we see EXTENSION_UPDATING_FINISHED?
  bool finished_;

  // The set of extension id's we've seen via EXTENSION_UPDATE_FOUND.
  std::set<std::string> updates_;
};

#if defined(OS_WIN)
// Fails consistently on Windows XP, see: http://crbug.com/120640.
#define MAYBE_AutoUpdate DISABLED_AutoUpdate
#else
// See http://crbug.com/103371 and http://crbug.com/120640.
#if defined(ADDRESS_SANITIZER)
#define MAYBE_AutoUpdate DISABLED_AutoUpdate
#else
#define MAYBE_AutoUpdate AutoUpdate
#endif
#endif

// Tests extension autoupdate.
IN_PROC_BROWSER_TEST_F(ExtensionManagementTest, MAYBE_AutoUpdate) {
  NotificationListener notification_listener;
  FilePath basedir = test_data_dir_.AppendASCII("autoupdate");
  // Note: This interceptor gets requests on the IO thread.
  scoped_refptr<extensions::AutoUpdateInterceptor> interceptor(
      new extensions::AutoUpdateInterceptor());
  net::URLFetcher::SetEnableInterceptionForTests(true);

  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/manifest",
                                     basedir.AppendASCII("manifest_v2.xml"));
  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/v2.crx",
                                     basedir.AppendASCII("v2.crx"));

  // Install version 1 of the extension.
  ExtensionTestMessageListener listener1("v1 installed", false);
  ExtensionService* service = browser()->profile()->GetExtensionService();
  const size_t size_before = service->extensions()->size();
  ASSERT_TRUE(service->disabled_extensions()->is_empty());
  const Extension* extension =
      InstallExtension(basedir.AppendASCII("v1.crx"), 1);
  ASSERT_TRUE(extension);
  listener1.WaitUntilSatisfied();
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  ASSERT_EQ("ogjcoiohnmldgjemafoockdghcjciccf", extension->id());
  ASSERT_EQ("1.0", extension->VersionString());

  // We don't want autoupdate blacklist checks.
  extensions::ExtensionUpdater::CheckParams params;
  params.check_blacklist = false;
  params.callback =
      base::Bind(&NotificationListener::OnFinished,
                 base::Unretained(&notification_listener));

  // Run autoupdate and make sure version 2 of the extension was installed.
  ExtensionTestMessageListener listener2("v2 installed", false);
  service->updater()->CheckNow(params);
  ASSERT_TRUE(WaitForExtensionInstall());
  listener2.WaitUntilSatisfied();
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  extension = service->GetExtensionById(
      "ogjcoiohnmldgjemafoockdghcjciccf", false);
  ASSERT_TRUE(extension);
  ASSERT_EQ("2.0", extension->VersionString());
  ASSERT_TRUE(notification_listener.started());
  ASSERT_TRUE(notification_listener.finished());
  ASSERT_TRUE(ContainsKey(notification_listener.updates(),
                          "ogjcoiohnmldgjemafoockdghcjciccf"));
  notification_listener.Reset();

  // Now try doing an update to version 3, which has been incorrectly
  // signed. This should fail.
  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/manifest",
                                     basedir.AppendASCII("manifest_v3.xml"));
  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/v3.crx",
                                     basedir.AppendASCII("v3.crx"));

  service->updater()->CheckNow(params);
  ASSERT_TRUE(WaitForExtensionInstallError());
  ASSERT_TRUE(notification_listener.started());
  ASSERT_TRUE(notification_listener.finished());
  ASSERT_TRUE(ContainsKey(notification_listener.updates(),
                          "ogjcoiohnmldgjemafoockdghcjciccf"));

  // Make sure the extension state is the same as before.
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  extension = service->GetExtensionById(
      "ogjcoiohnmldgjemafoockdghcjciccf", false);
  ASSERT_TRUE(extension);
  ASSERT_EQ("2.0", extension->VersionString());
}

#if defined(OS_WIN)
// Fails consistently on Windows XP, see: http://crbug.com/120640.
#define MAYBE_AutoUpdateDisabledExtensions DISABLED_AutoUpdateDisabledExtensions
#else
#if defined(ADDRESS_SANITIZER)
#define MAYBE_AutoUpdateDisabledExtensions DISABLED_AutoUpdateDisabledExtensions
#else
#define MAYBE_AutoUpdateDisabledExtensions AutoUpdateDisabledExtensions
#endif
#endif

// Tests extension autoupdate.
IN_PROC_BROWSER_TEST_F(ExtensionManagementTest,
                       MAYBE_AutoUpdateDisabledExtensions) {
  NotificationListener notification_listener;
  FilePath basedir = test_data_dir_.AppendASCII("autoupdate");
  // Note: This interceptor gets requests on the IO thread.
  scoped_refptr<extensions::AutoUpdateInterceptor> interceptor(
      new extensions::AutoUpdateInterceptor());
  net::URLFetcher::SetEnableInterceptionForTests(true);

  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/manifest",
                                     basedir.AppendASCII("manifest_v2.xml"));
  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/v2.crx",
                                     basedir.AppendASCII("v2.crx"));

  // Install version 1 of the extension.
  ExtensionTestMessageListener listener1("v1 installed", false);
  ExtensionService* service = browser()->profile()->GetExtensionService();
  const size_t enabled_size_before = service->extensions()->size();
  const size_t disabled_size_before = service->disabled_extensions()->size();
  const Extension* extension =
      InstallExtension(basedir.AppendASCII("v1.crx"), 1);
  ASSERT_TRUE(extension);
  listener1.WaitUntilSatisfied();
  DisableExtension(extension->id());
  ASSERT_EQ(disabled_size_before + 1, service->disabled_extensions()->size());
  ASSERT_EQ(enabled_size_before, service->extensions()->size());
  ASSERT_EQ("ogjcoiohnmldgjemafoockdghcjciccf", extension->id());
  ASSERT_EQ("1.0", extension->VersionString());

  // We don't want autoupdate blacklist checks.
  extensions::ExtensionUpdater::CheckParams params;
  params.check_blacklist = false;
  params.callback =
      base::Bind(&NotificationListener::OnFinished,
                 base::Unretained(&notification_listener));

  ExtensionTestMessageListener listener2("v2 installed", false);
  // Run autoupdate and make sure version 2 of the extension was installed but
  // is still disabled.
  service->updater()->CheckNow(params);
  ASSERT_TRUE(WaitForExtensionInstall());
  ASSERT_EQ(disabled_size_before + 1, service->disabled_extensions()->size());
  ASSERT_EQ(enabled_size_before, service->extensions()->size());
  extension = service->GetExtensionById(
      "ogjcoiohnmldgjemafoockdghcjciccf", true);
  ASSERT_TRUE(extension);
  ASSERT_FALSE(service->GetExtensionById(
      "ogjcoiohnmldgjemafoockdghcjciccf", false));
  ASSERT_EQ("2.0", extension->VersionString());

  // The extension should have not made the callback because it is disabled.
  // When we enabled it, it should then make the callback.
  ASSERT_FALSE(listener2.was_satisfied());
  EnableExtension(extension->id());
  listener2.WaitUntilSatisfied();
  ASSERT_TRUE(notification_listener.started());
  ASSERT_TRUE(notification_listener.finished());
  ASSERT_TRUE(ContainsKey(notification_listener.updates(),
                          "ogjcoiohnmldgjemafoockdghcjciccf"));
  notification_listener.Reset();
}

IN_PROC_BROWSER_TEST_F(ExtensionManagementTest, ExternalUrlUpdate) {
  ExtensionService* service = browser()->profile()->GetExtensionService();
  const char* kExtensionId = "ogjcoiohnmldgjemafoockdghcjciccf";
  // We don't want autoupdate blacklist checks.
  extensions::ExtensionUpdater::CheckParams params;
  params.check_blacklist = false;

  FilePath basedir = test_data_dir_.AppendASCII("autoupdate");

  // Note: This interceptor gets requests on the IO thread.
  scoped_refptr<extensions::AutoUpdateInterceptor> interceptor(
      new extensions::AutoUpdateInterceptor());
  net::URLFetcher::SetEnableInterceptionForTests(true);

  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/manifest",
                                     basedir.AppendASCII("manifest_v2.xml"));
  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/v2.crx",
                                     basedir.AppendASCII("v2.crx"));

  const size_t size_before = service->extensions()->size();
  ASSERT_TRUE(service->disabled_extensions()->is_empty());

  extensions::PendingExtensionManager* pending_extension_manager =
      service->pending_extension_manager();

  // The code that reads external_extensions.json uses this method to inform
  // the ExtensionService of an extension to download.  Using the real code
  // is race-prone, because instantating the ExtensionService starts a read
  // of external_extensions.json before this test function starts.

  EXPECT_TRUE(pending_extension_manager->AddFromExternalUpdateUrl(
      kExtensionId, GURL("http://localhost/autoupdate/manifest"),
      Extension::EXTERNAL_PREF_DOWNLOAD));

  // Run autoupdate and make sure version 2 of the extension was installed.
  service->updater()->CheckNow(params);
  ASSERT_TRUE(WaitForExtensionInstall());
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  const Extension* extension = service->GetExtensionById(kExtensionId, false);
  ASSERT_TRUE(extension);
  ASSERT_EQ("2.0", extension->VersionString());

  // Uninstalling the extension should set a pref that keeps the extension from
  // being installed again the next time external_extensions.json is read.

  UninstallExtension(kExtensionId);

  extensions::ExtensionPrefs* extension_prefs = service->extension_prefs();
  EXPECT_TRUE(extension_prefs->IsExternalExtensionUninstalled(kExtensionId))
      << "Uninstalling should set kill bit on externaly installed extension.";

  // Try to install the extension again from an external source. It should fail
  // because of the killbit.
  EXPECT_FALSE(pending_extension_manager->AddFromExternalUpdateUrl(
      kExtensionId, GURL("http://localhost/autoupdate/manifest"),
      Extension::EXTERNAL_PREF_DOWNLOAD));
  EXPECT_FALSE(pending_extension_manager->IsIdPending(kExtensionId))
      << "External reinstall of a killed extension shouldn't work.";
  EXPECT_TRUE(extension_prefs->IsExternalExtensionUninstalled(kExtensionId))
      << "External reinstall of a killed extension should leave it killed.";

  // Installing from non-external source.
  ASSERT_TRUE(InstallExtension(basedir.AppendASCII("v2.crx"), 1));

  EXPECT_FALSE(extension_prefs->IsExternalExtensionUninstalled(kExtensionId))
      << "Reinstalling should clear the kill bit.";

  // Uninstalling from a non-external source should not set the kill bit.
  UninstallExtension(kExtensionId);

  EXPECT_FALSE(extension_prefs->IsExternalExtensionUninstalled(kExtensionId))
      << "Uninstalling non-external extension should not set kill bit.";
}

namespace {

const char* kForceInstallNotEmptyHelp =
    "A policy may already be controlling the list of force-installed "
    "extensions. Please remove all policy settings from your computer "
    "before running tests. E.g. from /etc/chromium/policies Linux or "
    "from the registry on Windows, etc.";

}

// See http://crbug.com/57378 for flakiness details.
IN_PROC_BROWSER_TEST_F(ExtensionManagementTest, ExternalPolicyRefresh) {
  ExtensionService* service = browser()->profile()->GetExtensionService();
  const char* kExtensionId = "ogjcoiohnmldgjemafoockdghcjciccf";
  // We don't want autoupdate blacklist checks.
  extensions::ExtensionUpdater::CheckParams params;
  params.check_blacklist = false;

  FilePath basedir = test_data_dir_.AppendASCII("autoupdate");

  // Note: This interceptor gets requests on the IO thread.
  scoped_refptr<extensions::AutoUpdateInterceptor> interceptor(
      new extensions::AutoUpdateInterceptor());
  net::URLFetcher::SetEnableInterceptionForTests(true);

  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/manifest",
                                     basedir.AppendASCII("manifest_v2.xml"));
  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/v2.crx",
                                     basedir.AppendASCII("v2.crx"));

  const size_t size_before = service->extensions()->size();
  ASSERT_TRUE(service->disabled_extensions()->is_empty());

  PrefService* prefs = browser()->profile()->GetPrefs();
  const ListValue* forcelist =
      prefs->GetList(prefs::kExtensionInstallForceList);
  ASSERT_TRUE(forcelist->empty()) << kForceInstallNotEmptyHelp;

  {
    // Set the policy as a user preference and fire notification observers.
    ListPrefUpdate pref_update(prefs, prefs::kExtensionInstallForceList);
    ListValue* forcelist = pref_update.Get();
    ASSERT_TRUE(forcelist->empty());
    forcelist->Append(Value::CreateStringValue(
        std::string(kExtensionId) +
        ";http://localhost/autoupdate/manifest"));
  }

  // Check if the extension got installed.
  ASSERT_TRUE(WaitForExtensionInstall());
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  const Extension* extension = service->GetExtensionById(kExtensionId, false);
  ASSERT_TRUE(extension);
  ASSERT_EQ("2.0", extension->VersionString());
  EXPECT_EQ(Extension::EXTERNAL_POLICY_DOWNLOAD, extension->location());

  // Try to disable and uninstall the extension which should fail.
  DisableExtension(kExtensionId);
  EXPECT_EQ(size_before + 1, service->extensions()->size());
  EXPECT_EQ(0u, service->disabled_extensions()->size());
  UninstallExtension(kExtensionId);
  EXPECT_EQ(size_before + 1, service->extensions()->size());
  EXPECT_EQ(0u, service->disabled_extensions()->size());

  // Now try to disable it through the management api, again failing.
  ExtensionTestMessageListener listener1("ready", false);
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("management/uninstall_extension")));
  ASSERT_TRUE(listener1.WaitUntilSatisfied());
  EXPECT_EQ(size_before + 2, service->extensions()->size());
  EXPECT_EQ(0u, service->disabled_extensions()->size());

  // Check that emptying the list triggers uninstall.
  {
    prefs->ClearPref(prefs::kExtensionInstallForceList);
  }
  EXPECT_EQ(size_before + 1, service->extensions()->size());
  EXPECT_FALSE(service->GetExtensionById(kExtensionId, true));
}

// See http://crbug.com/103371 and http://crbug.com/120640.
#if defined(ADDRESS_SANITIZER)
#define MAYBE_PolicyOverridesUserInstall DISABLED_PolicyOverridesUserInstall
#else
#define MAYBE_PolicyOverridesUserInstall PolicyOverridesUserInstall
#endif

IN_PROC_BROWSER_TEST_F(ExtensionManagementTest,
                       MAYBE_PolicyOverridesUserInstall) {
  ExtensionService* service = browser()->profile()->GetExtensionService();
  const char* kExtensionId = "ogjcoiohnmldgjemafoockdghcjciccf";
  extensions::ExtensionUpdater::CheckParams params;
  params.check_blacklist = false;
  service->updater()->set_default_check_params(params);
  const size_t size_before = service->extensions()->size();
  FilePath basedir = test_data_dir_.AppendASCII("autoupdate");
  ASSERT_TRUE(service->disabled_extensions()->is_empty());

  // Note: This interceptor gets requests on the IO thread.
  scoped_refptr<extensions::AutoUpdateInterceptor> interceptor(
      new extensions::AutoUpdateInterceptor());
  net::URLFetcher::SetEnableInterceptionForTests(true);

  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/manifest",
                                     basedir.AppendASCII("manifest_v2.xml"));
  interceptor->SetResponseOnIOThread("http://localhost/autoupdate/v2.crx",
                                     basedir.AppendASCII("v2.crx"));

  // Check that the policy is initially empty.
  PrefService* prefs = browser()->profile()->GetPrefs();
  const ListValue* forcelist =
      prefs->GetList(prefs::kExtensionInstallForceList);
  ASSERT_TRUE(forcelist->empty()) << kForceInstallNotEmptyHelp;

  // User install of the extension.
  ASSERT_TRUE(InstallExtension(basedir.AppendASCII("v2.crx"), 1));
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  const Extension* extension = service->GetExtensionById(kExtensionId, false);
  ASSERT_TRUE(extension);
  EXPECT_EQ(Extension::INTERNAL, extension->location());
  EXPECT_TRUE(service->IsExtensionEnabled(kExtensionId));

  // Setup the force install policy. It should override the location.
  {
    ListPrefUpdate pref_update(prefs, prefs::kExtensionInstallForceList);
    ListValue* forcelist = pref_update.Get();
    ASSERT_TRUE(forcelist->empty());
    forcelist->Append(Value::CreateStringValue(
        std::string(kExtensionId) + ";http://localhost/autoupdate/manifest"));
  }
  ASSERT_TRUE(WaitForExtensionInstall());
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  extension = service->GetExtensionById(kExtensionId, false);
  ASSERT_TRUE(extension);
  EXPECT_EQ(Extension::EXTERNAL_POLICY_DOWNLOAD, extension->location());
  EXPECT_TRUE(service->IsExtensionEnabled(kExtensionId));

  // Remove the policy, and verify that the extension was uninstalled.
  // TODO(joaodasilva): it would be nicer if the extension was kept instead,
  // and reverted location to INTERNAL or whatever it was before the policy
  // was applied.
  {
    ListPrefUpdate pref_update(prefs, prefs::kExtensionInstallForceList);
    ListValue* forcelist = pref_update.Get();
    ASSERT_TRUE(!forcelist->empty());
    forcelist->Clear();
  }
  ASSERT_EQ(size_before, service->extensions()->size());
  extension = service->GetExtensionById(kExtensionId, true);
  EXPECT_FALSE(extension);

  // User install again, but have it disabled too before setting the policy.
  ASSERT_TRUE(InstallExtension(basedir.AppendASCII("v2.crx"), 1));
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  extension = service->GetExtensionById(kExtensionId, false);
  ASSERT_TRUE(extension);
  EXPECT_EQ(Extension::INTERNAL, extension->location());
  EXPECT_TRUE(service->IsExtensionEnabled(kExtensionId));
  EXPECT_TRUE(service->disabled_extensions()->is_empty());

  DisableExtension(kExtensionId);
  EXPECT_EQ(1u, service->disabled_extensions()->size());
  extension = service->GetExtensionById(kExtensionId, true);
  EXPECT_TRUE(extension);
  EXPECT_FALSE(service->IsExtensionEnabled(kExtensionId));

  // Install the policy again. It should overwrite the extension's location,
  // and force enable it too.
  {
    ListPrefUpdate pref_update(prefs, prefs::kExtensionInstallForceList);
    ListValue* forcelist = pref_update.Get();
    ASSERT_TRUE(forcelist->empty());
    forcelist->Append(Value::CreateStringValue(
        std::string(kExtensionId) + ";http://localhost/autoupdate/manifest"));
  }
  ASSERT_TRUE(WaitForExtensionInstall());
  ASSERT_EQ(size_before + 1, service->extensions()->size());
  extension = service->GetExtensionById(kExtensionId, false);
  ASSERT_TRUE(extension);
  EXPECT_EQ(Extension::EXTERNAL_POLICY_DOWNLOAD, extension->location());
  EXPECT_TRUE(service->IsExtensionEnabled(kExtensionId));
  EXPECT_TRUE(service->disabled_extensions()->is_empty());
}
