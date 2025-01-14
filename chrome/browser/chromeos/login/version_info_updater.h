// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_VERSION_INFO_UPDATER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_VERSION_INFO_UPDATER_H_

#include <string>

#include "chrome/browser/chromeos/boot_times_loader.h"
#include "chrome/browser/chromeos/version_loader.h"
#include "chrome/browser/policy/cloud_policy_subsystem.h"
#include "content/public/browser/notification_observer.h"

namespace chromeos {

class CrosSettings;

// Fetches all info we want to show on OOBE/Login screens about system
// version, boot times and cloud policy.
class VersionInfoUpdater : public policy::CloudPolicySubsystem::Observer,
                           public content::NotificationObserver {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Called when OS version label should be updated.
    virtual void OnOSVersionLabelTextUpdated(
        const std::string& os_version_label_text) = 0;

    // Called when boot times label should be updated.
    virtual void OnBootTimesLabelTextUpdated(
        const std::string& boot_times_label_text) = 0;

    // Called when the enterprise info notice should be updated.
    virtual void OnEnterpriseInfoUpdated(
        const std::string& enterprise_info,
        bool reporting_hint) = 0;
  };

  explicit VersionInfoUpdater(Delegate* delegate);
  virtual ~VersionInfoUpdater();

  // Sets delegate.
  void set_delegate(Delegate* delegate) { delegate_ = delegate; }

  // Starts fetching version info. The delegate will be notified when update
  // is received.
  void StartUpdate(bool is_official_build);

 private:
  // policy::CloudPolicySubsystem::Observer methods:
  virtual void OnPolicyStateChanged(
      policy::CloudPolicySubsystem::PolicySubsystemState state,
      policy::CloudPolicySubsystem::ErrorDetails error_details) OVERRIDE;

  // content::NotificationObserver interface.
  virtual void Observe(
      int type,
      const content::NotificationSource& source,
      const content::NotificationDetails& details) OVERRIDE;

  // Update the version label.
  void UpdateVersionLabel();

  // Check and update enterprise domain.
  void UpdateEnterpriseInfo();

  // Set enterprise domain name.
  void SetEnterpriseInfo(const std::string& domain_name,
                         const std::string& status_text,
                         bool reporting_hint);

  // Callback from chromeos::VersionLoader giving the version.
  void OnVersion(const std::string& version);
  // Callback from chromeos::InfoLoader giving the boot times.
  void OnBootTimes(const BootTimesLoader::BootTimes& boot_times);
  // Null callback from chromeos::InfoLoader.
  void OnBootTimesNoop(const BootTimesLoader::BootTimes& boot_times);

  // Handles asynchronously loading the version.
  VersionLoader version_loader_;
  // Used to request the version.
  CancelableRequestConsumer version_consumer_;

  // Handles asynchronously loading the boot times.
  BootTimesLoader boot_times_loader_;
  // Used to request boot times.
  CancelableTaskTracker tracker_;

  // Information pieces for version label.
  std::string version_text_;
  std::string enterprise_domain_text_;
  std::string enterprise_status_text_;
  bool enterprise_reporting_hint_;

  // Full text for the OS version label.
  std::string os_version_label_text_;

  // CloudPolicySubsysterm observer registrar
  scoped_ptr<policy::CloudPolicySubsystem::ObserverRegistrar>
      cloud_policy_registrar_;

  chromeos::CrosSettings* cros_settings_;

  Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(VersionInfoUpdater);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_VERSION_INFO_UPDATER_H_
