// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "base/stringprintf.h"
#include "chrome/browser/extensions/api/bluetooth/bluetooth_api.h"
#include "chrome/browser/extensions/api/bluetooth/bluetooth_event_router.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_out_of_band_pairing_data.h"
#include "device/bluetooth/test/mock_bluetooth_adapter.h"
#include "device/bluetooth/test/mock_bluetooth_device.h"
#include "testing/gmock/include/gmock/gmock.h"

using device::BluetoothAdapter;
using device::BluetoothDevice;
using device::BluetoothOutOfBandPairingData;
using device::MockBluetoothAdapter;
using device::MockBluetoothDevice;
using extensions::Extension;

namespace utils = extension_function_test_utils;
namespace api = extensions::api;

namespace {

static const char* kAdapterAddress = "A1:A2:A3:A4:A5:A6";
static const char* kName = "whatsinaname";

class BluetoothApiTest : public ExtensionApiTest {
 public:
  BluetoothApiTest() : empty_extension_(utils::CreateEmptyExtension()) {}

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableExperimentalExtensionApis);
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    // The browser will clean this up when it is torn down
    mock_adapter_ = new testing::StrictMock<MockBluetoothAdapter>(
        kAdapterAddress, kName);
    event_router()->SetAdapterForTest(mock_adapter_);

    device1_.reset(new testing::NiceMock<MockBluetoothDevice>(
        mock_adapter_, "d1", "11:12:13:14:15:16",
        true /* paired */, false /* bonded */, true /* connected */));
    device2_.reset(new testing::NiceMock<MockBluetoothDevice>(
        mock_adapter_, "d2", "21:22:23:24:25:26",
        false /* paired */, true /* bonded */, false /* connected */));
  }

  virtual void CleanUpOnMainThread() OVERRIDE {
    EXPECT_CALL(*mock_adapter_, RemoveObserver(testing::_));
  }

  void expectBooleanResult(bool expected,
                           UIThreadExtensionFunction* function) {
    scoped_ptr<base::Value> result(
        utils::RunFunctionAndReturnSingleResult(function, "[]", browser()));
    ASSERT_TRUE(result.get() != NULL);
    ASSERT_EQ(base::Value::TYPE_BOOLEAN, result->GetType());
    bool boolean_value;
    result->GetAsBoolean(&boolean_value);
    EXPECT_EQ(expected, boolean_value);
  }

  void expectStringResult(const std::string& expected,
                         UIThreadExtensionFunction* function) {
    scoped_ptr<base::Value> result(
        utils::RunFunctionAndReturnSingleResult(function, "[]", browser()));
    ASSERT_TRUE(result.get() != NULL);
    ASSERT_EQ(base::Value::TYPE_STRING, result->GetType());
    std::string string_value;
    result->GetAsString(&string_value);
    EXPECT_EQ(expected, string_value);
  }

  template <class T>
  T* setupFunction(T* function) {
    function->set_extension(empty_extension_.get());
    function->set_has_callback(true);
    return function;
  }

 protected:
  testing::StrictMock<MockBluetoothAdapter>* mock_adapter_;
  scoped_ptr<testing::NiceMock<MockBluetoothDevice> > device1_;
  scoped_ptr<testing::NiceMock<MockBluetoothDevice> > device2_;

  extensions::ExtensionBluetoothEventRouter* event_router() {
    return extensions::BluetoothAPI::Get(browser()->profile())
        ->bluetooth_event_router();
  }

 private:
  scoped_refptr<Extension> empty_extension_;
};

// This is the canonical UUID for the short UUID 0010.
static const char kOutOfBandPairingDataHash[] = "0123456789ABCDEh";
static const char kOutOfBandPairingDataRandomizer[] = "0123456789ABCDEr";

static BluetoothOutOfBandPairingData GetOutOfBandPairingData() {
  BluetoothOutOfBandPairingData data;
  memcpy(&(data.hash), kOutOfBandPairingDataHash,
      device::kBluetoothOutOfBandPairingDataSize);
  memcpy(&(data.randomizer), kOutOfBandPairingDataRandomizer,
      device::kBluetoothOutOfBandPairingDataSize);
  return data;
}

static bool CallClosure(const base::Closure& callback) {
  callback.Run();
  return true;
}

static void CallOutOfBandPairingDataCallback(
    const BluetoothAdapter::BluetoothOutOfBandPairingDataCallback& callback,
    const BluetoothAdapter::ErrorCallback& error_callback) {
  callback.Run(GetOutOfBandPairingData());
}

template <bool Value>
static void CallProvidesServiceCallback(
    const std::string& name,
    const BluetoothDevice::ProvidesServiceCallback& callback) {
  callback.Run(Value);
}

}  // namespace

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, IsAvailable) {
  EXPECT_CALL(*mock_adapter_, IsPresent())
      .WillOnce(testing::Return(false));

  scoped_refptr<api::BluetoothIsAvailableFunction> is_available;

  is_available = setupFunction(new api::BluetoothIsAvailableFunction);
  expectBooleanResult(false, is_available);

  testing::Mock::VerifyAndClearExpectations(mock_adapter_);
  EXPECT_CALL(*mock_adapter_, IsPresent())
      .WillOnce(testing::Return(true));

  is_available = setupFunction(new api::BluetoothIsAvailableFunction);
  expectBooleanResult(true, is_available);
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, IsPowered) {
  EXPECT_CALL(*mock_adapter_, IsPowered())
      .WillOnce(testing::Return(false));

  scoped_refptr<api::BluetoothIsPoweredFunction> is_powered;

  is_powered = setupFunction(new api::BluetoothIsPoweredFunction);
  expectBooleanResult(false, is_powered);

  testing::Mock::VerifyAndClearExpectations(mock_adapter_);
  EXPECT_CALL(*mock_adapter_, IsPowered())
      .WillOnce(testing::Return(true));

  is_powered = setupFunction(new api::BluetoothIsPoweredFunction);
  expectBooleanResult(true, is_powered);
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, GetAddress) {
  scoped_refptr<api::BluetoothGetAddressFunction> get_address;
  get_address = setupFunction(new api::BluetoothGetAddressFunction);
  expectStringResult(kAdapterAddress, get_address);
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, GetName) {
  scoped_refptr<api::BluetoothGetNameFunction> get_name;
  get_name = setupFunction(new api::BluetoothGetNameFunction);
  expectStringResult(kName, get_name);
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, GetLocalOutOfBandPairingData) {
  EXPECT_CALL(*mock_adapter_,
              ReadLocalOutOfBandPairingData(testing::_, testing::_))
      .WillOnce(testing::Invoke(CallOutOfBandPairingDataCallback));

  scoped_refptr<api::BluetoothGetLocalOutOfBandPairingDataFunction>
      get_oob_function(setupFunction(
            new api::BluetoothGetLocalOutOfBandPairingDataFunction));

  scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
      get_oob_function, "[]", browser()));

  base::DictionaryValue* dict;
  EXPECT_TRUE(result->GetAsDictionary(&dict));

  base::BinaryValue* binary_value;
  EXPECT_TRUE(dict->GetBinary("hash", &binary_value));
  EXPECT_STREQ(kOutOfBandPairingDataHash,
      std::string(binary_value->GetBuffer(), binary_value->GetSize()).c_str());
  EXPECT_TRUE(dict->GetBinary("randomizer", &binary_value));
  EXPECT_STREQ(kOutOfBandPairingDataRandomizer,
      std::string(binary_value->GetBuffer(), binary_value->GetSize()).c_str());

  // Try again with an error
  testing::Mock::VerifyAndClearExpectations(mock_adapter_);
  EXPECT_CALL(*mock_adapter_,
              ReadLocalOutOfBandPairingData(
                  testing::_,
                  testing::Truly(CallClosure)));

  get_oob_function =
      setupFunction(new api::BluetoothGetLocalOutOfBandPairingDataFunction);

  std::string error(
      utils::RunFunctionAndReturnError(get_oob_function, "[]", browser()));
  EXPECT_FALSE(error.empty());
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, SetOutOfBandPairingData) {
  std::string device_address("11:12:13:14:15:16");
  EXPECT_CALL(*mock_adapter_, GetDevice(device_address))
      .WillOnce(testing::Return(device1_.get()));
  EXPECT_CALL(*device1_,
              ClearOutOfBandPairingData(testing::Truly(CallClosure),
                                        testing::_));

  std::string params = base::StringPrintf(
      "[{\"deviceAddress\":\"%s\"}]", device_address.c_str());

  scoped_refptr<api::BluetoothSetOutOfBandPairingDataFunction> set_oob_function;
  set_oob_function = setupFunction(
      new api::BluetoothSetOutOfBandPairingDataFunction);
  // There isn't actually a result.
  (void)utils::RunFunctionAndReturnSingleResult(
      set_oob_function, params, browser());

  // Try again with an error
  testing::Mock::VerifyAndClearExpectations(mock_adapter_);
  testing::Mock::VerifyAndClearExpectations(device1_.get());
  EXPECT_CALL(*mock_adapter_, GetDevice(device_address))
      .WillOnce(testing::Return(device1_.get()));
  EXPECT_CALL(*device1_,
              ClearOutOfBandPairingData(testing::_,
                                        testing::Truly(CallClosure)));

  set_oob_function = setupFunction(
      new api::BluetoothSetOutOfBandPairingDataFunction);
  std::string error(
      utils::RunFunctionAndReturnError(set_oob_function, params, browser()));
  EXPECT_FALSE(error.empty());

  // TODO(bryeung): Also test setting the data when there is support for
  // ArrayBuffers in the arguments to the RunFunctionAnd* methods.
  // crbug.com/132796
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, Discovery) {
  // Try with a failure to start
  EXPECT_CALL(*mock_adapter_, IsDiscovering()).WillOnce(testing::Return(false));
  EXPECT_CALL(*mock_adapter_,
              SetDiscovering(true,
                             testing::_,
                             testing::Truly(CallClosure)));
  scoped_refptr<api::BluetoothStartDiscoveryFunction> start_function;
  start_function = setupFunction(new api::BluetoothStartDiscoveryFunction);
  std::string error(
      utils::RunFunctionAndReturnError(start_function, "[]", browser()));
  ASSERT_TRUE(!error.empty());

  // Reset for a successful start
  testing::Mock::VerifyAndClearExpectations(mock_adapter_);
  EXPECT_CALL(*mock_adapter_, IsDiscovering()).WillOnce(testing::Return(false));
  EXPECT_CALL(*mock_adapter_,
              SetDiscovering(true,
                             testing::Truly(CallClosure),
                             testing::_));

  start_function = setupFunction(new api::BluetoothStartDiscoveryFunction);
  (void)utils::RunFunctionAndReturnError(start_function, "[]", browser());

  // Reset to try stopping
  testing::Mock::VerifyAndClearExpectations(mock_adapter_);
  EXPECT_CALL(*mock_adapter_,
              SetDiscovering(false,
                             testing::Truly(CallClosure),
                             testing::_));
  scoped_refptr<api::BluetoothStopDiscoveryFunction> stop_function;
  stop_function = setupFunction(new api::BluetoothStopDiscoveryFunction);
  (void)utils::RunFunctionAndReturnSingleResult(stop_function, "[]", browser());

  // Reset to try stopping with an error
  testing::Mock::VerifyAndClearExpectations(mock_adapter_);
  EXPECT_CALL(*mock_adapter_,
              SetDiscovering(false,
                             testing::_,
                             testing::Truly(CallClosure)));
  stop_function = setupFunction(new api::BluetoothStopDiscoveryFunction);
  error = utils::RunFunctionAndReturnError(stop_function, "[]", browser());
  ASSERT_TRUE(!error.empty());
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, DiscoveryCallback) {
  EXPECT_CALL(*mock_adapter_, IsDiscovering()).WillOnce(testing::Return(false));
  EXPECT_CALL(*mock_adapter_,
              SetDiscovering(true, testing::Truly(CallClosure), testing::_));
  EXPECT_CALL(*mock_adapter_,
              SetDiscovering(false, testing::Truly(CallClosure), testing::_));

  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  ExtensionTestMessageListener discovery_started("ready", true);
  ASSERT_TRUE(LoadExtension(
        test_data_dir_.AppendASCII("bluetooth/discovery_callback")));
  EXPECT_TRUE(discovery_started.WaitUntilSatisfied());

  event_router()->DeviceAdded(mock_adapter_, device1_.get());

  discovery_started.Reply("go");
  ExtensionTestMessageListener discovery_stopped("ready", true);
  EXPECT_TRUE(discovery_stopped.WaitUntilSatisfied());

  event_router()->DeviceAdded(mock_adapter_, device2_.get());
  discovery_stopped.Reply("go");

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, DiscoveryInProgress) {
  // Fake that the adapter is discovering
  EXPECT_CALL(*mock_adapter_, IsDiscovering()).WillOnce(testing::Return(true));
  event_router()->AdapterDiscoveringChanged(mock_adapter_, true);

  // Cache a device before the extension starts discovering
  event_router()->DeviceAdded(mock_adapter_, device1_.get());

  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  ExtensionTestMessageListener discovery_started("ready", true);
  ASSERT_TRUE(LoadExtension(
        test_data_dir_.AppendASCII("bluetooth/discovery_in_progress")));
  EXPECT_TRUE(discovery_started.WaitUntilSatisfied());

  // This should be received in addition to the cached device above.
  event_router()->DeviceAdded(mock_adapter_, device2_.get());

  discovery_started.Reply("go");
  ExtensionTestMessageListener discovery_stopped("ready", true);
  EXPECT_TRUE(discovery_stopped.WaitUntilSatisfied());

  // This should never be received.
  event_router()->DeviceAdded(mock_adapter_, device2_.get());
  discovery_stopped.Reply("go");

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, Events) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  // Load and wait for setup
  ExtensionTestMessageListener listener("ready", true);
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("bluetooth/events")));
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  event_router()->AdapterPoweredChanged(mock_adapter_, true);
  event_router()->AdapterPoweredChanged(mock_adapter_, false);
  event_router()->AdapterPresentChanged(mock_adapter_, true);
  event_router()->AdapterPresentChanged(mock_adapter_, false);
  event_router()->AdapterDiscoveringChanged(mock_adapter_, true);
  event_router()->AdapterDiscoveringChanged(mock_adapter_, false);

  listener.Reply("go");

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, GetDevices) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  BluetoothAdapter::ConstDeviceList devices;
  devices.push_back(device1_.get());
  devices.push_back(device2_.get());

  EXPECT_CALL(*device1_, ProvidesServiceWithUUID(testing::_))
      .WillOnce(testing::Return(false));
  EXPECT_CALL(*device1_, ProvidesServiceWithName(testing::_, testing::_))
      .WillOnce(testing::Invoke(CallProvidesServiceCallback<true>));

  EXPECT_CALL(*device2_, ProvidesServiceWithUUID(testing::_))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*device2_, ProvidesServiceWithName(testing::_, testing::_))
      .WillOnce(testing::Invoke(CallProvidesServiceCallback<false>));

  EXPECT_CALL(*mock_adapter_, GetDevices())
      .Times(3)
      .WillRepeatedly(testing::Return(devices));

  // Load and wait for setup
  ExtensionTestMessageListener listener("ready", true);
  ASSERT_TRUE(
      LoadExtension(test_data_dir_.AppendASCII("bluetooth/get_devices")));
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  listener.Reply("go");

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, GetDevicesConcurrently) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  BluetoothAdapter::ConstDeviceList devices;
  devices.push_back(device1_.get());

  // Save the callback to delay execution so that we can force the calls to
  // happen concurrently.  This will be called after the listener is satisfied.
  BluetoothDevice::ProvidesServiceCallback callback;
  EXPECT_CALL(*device1_, ProvidesServiceWithName(testing::_, testing::_))
      .WillOnce(testing::SaveArg<1>(&callback));

  EXPECT_CALL(*mock_adapter_, GetDevices())
      .WillOnce(testing::Return(devices));

  // Load and wait for setup
  ExtensionTestMessageListener listener("ready", true);
  ASSERT_TRUE(LoadExtension(
        test_data_dir_.AppendASCII("bluetooth/get_devices_concurrently")));
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  callback.Run(false);
  listener.Reply("go");

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(BluetoothApiTest, GetDevicesError) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  // Load and wait for setup
  ExtensionTestMessageListener listener("ready", true);
  ASSERT_TRUE(LoadExtension(
        test_data_dir_.AppendASCII("bluetooth/get_devices_error")));
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  listener.Reply("go");

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}
