// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/tray/system_tray_notifier.h"

namespace ash {

SystemTrayNotifier::SystemTrayNotifier() {
}

SystemTrayNotifier::~SystemTrayNotifier() {
}

void SystemTrayNotifier::AddAccessibilityObserver(
    AccessibilityObserver* observer) {
  accessibility_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddAudioObserver(AudioObserver* observer) {
  audio_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddBluetoothObserver(BluetoothObserver* observer) {
  bluetooth_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddBrightnessObserver(BrightnessObserver* observer) {
  brightness_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddCapsLockObserver(CapsLockObserver* observer) {
  caps_lock_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddClockObserver(ClockObserver* observer) {
  clock_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddDriveObserver(DriveObserver* observer) {
  drive_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddIMEObserver(IMEObserver* observer) {
  ime_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddLocaleObserver(LocaleObserver* observer) {
  locale_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddLogoutButtonObserver(
    LogoutButtonObserver* observer) {
  logout_button_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddPowerStatusObserver(
    PowerStatusObserver* observer) {
  power_status_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddUpdateObserver(UpdateObserver* observer) {
  update_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddUserObserver(UserObserver* observer) {
  user_observers_.AddObserver(observer);
}

#if defined(OS_CHROMEOS)
void SystemTrayNotifier::AddNetworkObserver(NetworkObserver* observer) {
  network_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddVpnObserver(NetworkObserver* observer) {
  vpn_observers_.AddObserver(observer);
}

void SystemTrayNotifier::AddSmsObserver(SmsObserver* observer) {
  sms_observers_.AddObserver(observer);
}
#endif

void SystemTrayNotifier::NotifyAccessibilityModeChanged(
    bool spoken_feedback_enabled) {
  FOR_EACH_OBSERVER(
      AccessibilityObserver,
      accessibility_observers_,
      OnAccessibilityModeChanged(spoken_feedback_enabled));
}

void SystemTrayNotifier::NotifyVolumeChanged(float level) {
  FOR_EACH_OBSERVER(AudioObserver,
                    audio_observers_,
                    OnVolumeChanged(level));
}

void SystemTrayNotifier::NotifyMuteToggled() {
  FOR_EACH_OBSERVER(AudioObserver,
                    audio_observers_,
                    OnMuteToggled());
}

void SystemTrayNotifier::NotifyRefreshBluetooth() {
  FOR_EACH_OBSERVER(BluetoothObserver,
                    bluetooth_observers_,
                    OnBluetoothRefresh());
}

void SystemTrayNotifier::NotifyBluetoothDiscoveringChanged() {
  FOR_EACH_OBSERVER(BluetoothObserver,
                    bluetooth_observers_,
                    OnBluetoothDiscoveringChanged());
}

void SystemTrayNotifier::NotifyBrightnessChanged(double level,
                                                 bool user_initiated) {
  FOR_EACH_OBSERVER(
      BrightnessObserver,
      brightness_observers_,
      OnBrightnessChanged(level, user_initiated));
}

void SystemTrayNotifier::NotifyCapsLockChanged(
    bool enabled,
    bool search_mapped_to_caps_lock) {
  FOR_EACH_OBSERVER(CapsLockObserver,
                    caps_lock_observers_,
                    OnCapsLockChanged(enabled, search_mapped_to_caps_lock));
}

void SystemTrayNotifier::NotifyRefreshClock() {
  FOR_EACH_OBSERVER(ClockObserver, clock_observers_, Refresh());
}

void SystemTrayNotifier::NotifyDateFormatChanged() {
  FOR_EACH_OBSERVER(ClockObserver,
                    clock_observers_,
                    OnDateFormatChanged());
}

void SystemTrayNotifier::NotifyRefreshDrive(DriveOperationStatusList& list) {
  FOR_EACH_OBSERVER(DriveObserver,
                    drive_observers_,
                    OnDriveRefresh(list));
}

void SystemTrayNotifier::NotifyRefreshIME(bool show_message) {
  FOR_EACH_OBSERVER(IMEObserver,
                    ime_observers_,
                    OnIMERefresh(show_message));
}

void SystemTrayNotifier::NotifyShowLoginButtonChanged(bool show_login_button) {
  FOR_EACH_OBSERVER(LogoutButtonObserver,
                    logout_button_observers_,
                    OnShowLogoutButtonInTrayChanged(show_login_button));
}

void SystemTrayNotifier::NotifyLocaleChanged(
    LocaleObserver::Delegate* delegate,
    const std::string& cur_locale,
    const std::string& from_locale,
    const std::string& to_locale) {
  FOR_EACH_OBSERVER(
      LocaleObserver,
      locale_observers_,
      OnLocaleChanged(delegate, cur_locale, from_locale, to_locale));
}

void SystemTrayNotifier::NotifyPowerStatusChanged(
    const PowerSupplyStatus& power_status) {
  FOR_EACH_OBSERVER(PowerStatusObserver,
                    power_status_observers_,
                    OnPowerStatusChanged(power_status));
}

void SystemTrayNotifier::NotifyUpdateRecommended(
    UpdateObserver::UpdateSeverity severity) {
  FOR_EACH_OBSERVER(UpdateObserver,
                    update_observers_,
                    OnUpdateRecommended(severity));
}

void SystemTrayNotifier::NotifyUserUpdate() {
  FOR_EACH_OBSERVER(UserObserver,
                    user_observers_,
                    OnUserUpdate());
}

#if defined(OS_CHROMEOS)

void SystemTrayNotifier::NotifyRefreshNetwork(const NetworkIconInfo &info) {
  FOR_EACH_OBSERVER(NetworkObserver,
                    network_observers_,
                    OnNetworkRefresh(info));
}

void SystemTrayNotifier::NotifySetNetworkMessage(
    NetworkTrayDelegate* delegate,
    NetworkObserver::MessageType message_type,
    NetworkObserver::NetworkType network_type,
    const string16& title,
    const string16& message,
    const std::vector<string16>& links) {
  FOR_EACH_OBSERVER(NetworkObserver,
                    network_observers_,
                    SetNetworkMessage(
                        delegate,
                        message_type,
                        network_type,
                        title,
                        message,
                        links));
}

void SystemTrayNotifier::NotifyClearNetworkMessage(
    NetworkObserver::MessageType message_type) {
  FOR_EACH_OBSERVER(NetworkObserver,
                    network_observers_,
                    ClearNetworkMessage(message_type));
}

void SystemTrayNotifier::NotifyVpnRefreshNetwork(const NetworkIconInfo &info) {
  FOR_EACH_OBSERVER(NetworkObserver,
                    vpn_observers_,
                    OnNetworkRefresh(info));
}

void SystemTrayNotifier::NotifyWillToggleWifi() {
  FOR_EACH_OBSERVER(NetworkObserver,
                    network_observers_,
                    OnWillToggleWifi());
}

void SystemTrayNotifier::NotifyAddSmsMessage(
    const base::DictionaryValue& message) {
  FOR_EACH_OBSERVER(SmsObserver, sms_observers_, AddMessage(message));
}

#endif  // OS_CHROMEOS

}  // namespace ash
