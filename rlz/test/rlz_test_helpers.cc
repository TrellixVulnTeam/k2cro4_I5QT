// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Main entry point for all unit tests.

#include "rlz_test_helpers.h"

#include "rlz/lib/rlz_lib.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_WIN)
#include <shlwapi.h>
#include "base/win/registry.h"
#include "rlz/win/lib/rlz_lib.h"
#elif defined(OS_MACOSX) || defined(OS_CHROMEOS)
#include "base/file_path.h"
#include "rlz/lib/rlz_value_store.h"
#endif
#if defined(OS_CHROMEOS)
#include "rlz/chromeos/lib/rlz_value_store_chromeos.h"
#endif

#if defined(OS_WIN)
namespace {

const wchar_t* kHKCUReplacement = L"Software\\Google\\RlzUtilUnittest\\HKCU";
const wchar_t* kHKLMReplacement = L"Software\\Google\\RlzUtilUnittest\\HKLM";

void OverrideRegistryHives() {
  // Wipe the keys we redirect to.
  // This gives us a stable run, even in the presence of previous
  // crashes or failures.
  LSTATUS err = SHDeleteKey(HKEY_CURRENT_USER, kHKCUReplacement);
  EXPECT_TRUE(err == ERROR_SUCCESS || err == ERROR_FILE_NOT_FOUND);
  err = SHDeleteKey(HKEY_CURRENT_USER, kHKLMReplacement);
  EXPECT_TRUE(err == ERROR_SUCCESS || err == ERROR_FILE_NOT_FOUND);

  // Create the keys we're redirecting HKCU and HKLM to.
  base::win::RegKey hkcu;
  base::win::RegKey hklm;
  ASSERT_EQ(ERROR_SUCCESS,
      hkcu.Create(HKEY_CURRENT_USER, kHKCUReplacement, KEY_READ));
  ASSERT_EQ(ERROR_SUCCESS,
      hklm.Create(HKEY_CURRENT_USER, kHKLMReplacement, KEY_READ));

  rlz_lib::InitializeTempHivesForTesting(hklm, hkcu);

  // And do the switcharoo.
  ASSERT_EQ(ERROR_SUCCESS,
      ::RegOverridePredefKey(HKEY_CURRENT_USER, hkcu.Handle()));
  ASSERT_EQ(ERROR_SUCCESS,
      ::RegOverridePredefKey(HKEY_LOCAL_MACHINE, hklm.Handle()));
}

void UndoOverrideRegistryHives() {
  // Undo the redirection.
  EXPECT_EQ(ERROR_SUCCESS, ::RegOverridePredefKey(HKEY_CURRENT_USER, NULL));
  EXPECT_EQ(ERROR_SUCCESS, ::RegOverridePredefKey(HKEY_LOCAL_MACHINE, NULL));
}

}  // namespace
#endif  // defined(OS_WIN)


#if defined(OS_CHROMEOS)
RlzLibTestNoMachineState::RlzLibTestNoMachineState()
    : pref_store_io_thread_("test_rlz_pref_store_io_thread") {
}
#endif  // defined(OS_CHROMEOS)

void RlzLibTestNoMachineState::SetUp() {
#if defined(OS_WIN)
  OverrideRegistryHives();
#elif defined(OS_MACOSX)
  base::mac::ScopedNSAutoreleasePool pool;
#endif  // defined(OS_WIN)
#if defined(OS_MACOSX) || defined(OS_CHROMEOS)
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  rlz_lib::testing::SetRlzStoreDirectory(temp_dir_.path());
#endif  // defined(OS_MACOSX) || defined(OS_CHROMEOS)
#if defined(OS_CHROMEOS)
  base::Thread::Options options;
  options.message_loop_type = MessageLoop::TYPE_IO;
  ASSERT_TRUE(pref_store_io_thread_.StartWithOptions(options));
  rlz_lib::SetIOTaskRunner(pref_store_io_thread_.message_loop_proxy());
  rlz_lib::RlzValueStoreChromeOS::ResetForTesting();
#endif  // defined(OS_CHROMEOS)
}

void RlzLibTestNoMachineState::TearDown() {
#if defined(OS_WIN)
  UndoOverrideRegistryHives();
#elif defined(OS_MACOSX) || defined(OS_CHROMEOS)
  rlz_lib::testing::SetRlzStoreDirectory(FilePath());
#endif  // defined(OS_WIN)
#if defined(OS_CHROMEOS)
  pref_store_io_thread_.Stop();
#endif  // defined(OS_CHROMEOS)
}

void RlzLibTestBase::SetUp() {
  RlzLibTestNoMachineState::SetUp();
#if defined(OS_WIN)
  rlz_lib::CreateMachineState();
#endif  // defined(OS_WIN)
}
