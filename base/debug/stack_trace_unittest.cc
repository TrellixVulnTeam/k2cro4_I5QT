// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <sstream>
#include <string>

#include "base/debug/stack_trace.h"
#include "base/logging.h"
#include "base/process_util.h"
#include "base/test/test_timeouts.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

#if defined(OS_POSIX) && !defined(OS_ANDROID) && !defined(OS_IOS)
#include "base/test/multiprocess_test.h"
#endif

namespace base {
namespace debug {

#if defined(OS_POSIX) && !defined(OS_ANDROID) && !defined(OS_IOS)
typedef MultiProcessTest StackTraceTest;
#else
typedef testing::Test StackTraceTest;
#endif

// Note: On Linux, this test currently only fully works on Debug builds.
// See comments in the #ifdef soup if you intend to change this.
#if defined(OS_WIN)
// Always fails on Windows: crbug.com/32070
#define MAYBE_OutputToStream DISABLED_OutputToStream
#else
#define MAYBE_OutputToStream OutputToStream
#endif
TEST_F(StackTraceTest, MAYBE_OutputToStream) {
  StackTrace trace;

  // Dump the trace into a string.
  std::ostringstream os;
  trace.OutputToStream(&os);
  std::string backtrace_message = os.str();

  // ToString() should produce the same output.
  EXPECT_EQ(backtrace_message, trace.ToString());

#if defined(OS_POSIX) && !defined(OS_MACOSX) && NDEBUG
  // Stack traces require an extra data table that bloats our binaries,
  // so they're turned off for release builds.  We stop the test here,
  // at least letting us verify that the calls don't crash.
  return;
#endif  // defined(OS_POSIX) && !defined(OS_MACOSX) && NDEBUG

  size_t frames_found = 0;
  trace.Addresses(&frames_found);
  ASSERT_GE(frames_found, 5u) <<
      "No stack frames found.  Skipping rest of test.";

  // Check if the output has symbol initialization warning.  If it does, fail.
  ASSERT_EQ(backtrace_message.find("Dumping unresolved backtrace"),
            std::string::npos) <<
      "Unable to resolve symbols.  Skipping rest of test.";

#if defined(OS_MACOSX)
#if 0
  // Disabled due to -fvisibility=hidden in build config.

  // Symbol resolution via the backtrace_symbol function does not work well
  // in OS X.
  // See this thread:
  //
  //    http://lists.apple.com/archives/darwin-dev/2009/Mar/msg00111.html
  //
  // Just check instead that we find our way back to the "start" symbol
  // which should be the first symbol in the trace.
  //
  // TODO(port): Find a more reliable way to resolve symbols.

  // Expect to at least find main.
  EXPECT_TRUE(backtrace_message.find("start") != std::string::npos)
      << "Expected to find start in backtrace:\n"
      << backtrace_message;

#endif
#elif defined(USE_SYMBOLIZE)
  // This branch is for gcc-compiled code, but not Mac due to the
  // above #if.
  // Expect a demangled symbol.
  EXPECT_TRUE(backtrace_message.find("testing::Test::Run()") !=
              std::string::npos)
      << "Expected a demangled symbol in backtrace:\n"
      << backtrace_message;

#elif 0
  // This is the fall-through case; it used to cover Windows.
  // But it's disabled because of varying buildbot configs;
  // some lack symbols.

  // Expect to at least find main.
  EXPECT_TRUE(backtrace_message.find("main") != std::string::npos)
      << "Expected to find main in backtrace:\n"
      << backtrace_message;

#if defined(OS_WIN)
// MSVC doesn't allow the use of C99's __func__ within C++, so we fake it with
// MSVC's __FUNCTION__ macro.
#define __func__ __FUNCTION__
#endif

  // Expect to find this function as well.
  // Note: This will fail if not linked with -rdynamic (aka -export_dynamic)
  EXPECT_TRUE(backtrace_message.find(__func__) != std::string::npos)
      << "Expected to find " << __func__ << " in backtrace:\n"
      << backtrace_message;

#endif  // define(OS_MACOSX)
}

// The test is used for manual testing, e.g., to see the raw output.
TEST_F(StackTraceTest, DebugOutputToStream) {
  StackTrace trace;
  std::ostringstream os;
  trace.OutputToStream(&os);
  VLOG(1) << os.str();
}

// The test is used for manual testing, e.g., to see the raw output.
TEST_F(StackTraceTest, DebugPrintBacktrace) {
  StackTrace().PrintBacktrace();
}

#if defined(OS_POSIX) && !defined(OS_ANDROID)
#if !defined(OS_IOS)
MULTIPROCESS_TEST_MAIN(MismatchedMallocChildProcess) {
  char* pointer = new char[10];
  delete pointer;
  return 2;
}

// Regression test for StackDumpingSignalHandler async-signal unsafety.
// Combined with tcmalloc's debugallocation, that signal handler
// and e.g. mismatched new[]/delete would cause a hang because
// of re-entering malloc.
TEST_F(StackTraceTest, AsyncSignalUnsafeSignalHandlerHang) {
  ProcessHandle child = this->SpawnChild("MismatchedMallocChildProcess", false);
  ASSERT_NE(kNullProcessHandle, child);
  ASSERT_TRUE(WaitForSingleProcess(child, TestTimeouts::action_timeout()));
}
#endif  // !defined(OS_IOS)

namespace {

std::string itoa_r_wrapper(intptr_t i, size_t sz, int base) {
  char buffer[1024];
  CHECK_LE(sz, sizeof(buffer));

  char* result = internal::itoa_r(i, buffer, sz, base);
  EXPECT_TRUE(result);
  return std::string(buffer);
}

}  // namespace

TEST_F(StackTraceTest, itoa_r) {
  EXPECT_EQ("0", itoa_r_wrapper(0, 128, 10));
  EXPECT_EQ("-1", itoa_r_wrapper(-1, 128, 10));

  // Test edge cases.
  if (sizeof(intptr_t) == 4) {
    EXPECT_EQ("ffffffff", itoa_r_wrapper(-1, 128, 16));
    EXPECT_EQ("-2147483648",
              itoa_r_wrapper(std::numeric_limits<intptr_t>::min(), 128, 10));
    EXPECT_EQ("2147483647",
              itoa_r_wrapper(std::numeric_limits<intptr_t>::max(), 128, 10));

    EXPECT_EQ("80000000",
              itoa_r_wrapper(std::numeric_limits<intptr_t>::min(), 128, 16));
    EXPECT_EQ("7fffffff",
              itoa_r_wrapper(std::numeric_limits<intptr_t>::max(), 128, 16));
  } else if (sizeof(intptr_t) == 8) {
    EXPECT_EQ("ffffffffffffffff", itoa_r_wrapper(-1, 128, 16));
    EXPECT_EQ("-9223372036854775808",
              itoa_r_wrapper(std::numeric_limits<intptr_t>::min(), 128, 10));
    EXPECT_EQ("9223372036854775807",
              itoa_r_wrapper(std::numeric_limits<intptr_t>::max(), 128, 10));

    EXPECT_EQ("8000000000000000",
              itoa_r_wrapper(std::numeric_limits<intptr_t>::min(), 128, 16));
    EXPECT_EQ("7fffffffffffffff",
              itoa_r_wrapper(std::numeric_limits<intptr_t>::max(), 128, 16));
  } else {
    ADD_FAILURE() << "Missing test case for your size of intptr_t ("
                  << sizeof(intptr_t) << ")";
  }

  // Test hex output.
  EXPECT_EQ("688", itoa_r_wrapper(0x688, 128, 16));
  EXPECT_EQ("deadbeef", itoa_r_wrapper(0xdeadbeef, 128, 16));

  // Check that itoa_r respects passed buffer size limit.
  char buffer[1024];
  EXPECT_TRUE(internal::itoa_r(0xdeadbeef, buffer, 10, 16));
  EXPECT_TRUE(internal::itoa_r(0xdeadbeef, buffer, 9, 16));
  EXPECT_FALSE(internal::itoa_r(0xdeadbeef, buffer, 8, 16));
  EXPECT_FALSE(internal::itoa_r(0xdeadbeef, buffer, 7, 16));
}
#endif  // defined(OS_POSIX) && !defined(OS_ANDROID)

}  // namespace debug
}  // namespace base
