// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/renderer_main_platform_delegate.h"

#include <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

#include "base/command_line.h"
#include "base/logging.h"
#import "base/mac/foundation_util.h"
#import "base/mac/crash_logging.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/sys_string_conversions.h"
#include "content/common/sandbox_mac.h"
#include "content/public/common/content_switches.h"
#import "content/public/common/injection_test_mac.h"
#include "content/common/sandbox_init_mac.h"

namespace content {

RendererMainPlatformDelegate::RendererMainPlatformDelegate(
    const MainFunctionParams& parameters)
        : parameters_(parameters) {
}

RendererMainPlatformDelegate::~RendererMainPlatformDelegate() {
}

// TODO(mac-port): Any code needed to initialize a process for purposes of
// running a renderer needs to also be reflected in chrome_main.cc for
// --single-process support.
void RendererMainPlatformDelegate::PlatformInitialize() {
  // Initialize NSApplication up front.  Without this call, drawing of
  // native UI elements (e.g. buttons) in WebKit will explode.
  [NSApplication sharedApplication];

  if (![NSThread isMultiThreaded]) {
    NSString* string = @"";
    [NSThread detachNewThreadSelector:@selector(length)
                             toTarget:string
                           withObject:nil];
  }

  // Debugging for http://crbug.com/152566
  base::mac::ScopedCFTypeRef<TISInputSourceRef> input_source(
      TISCopyCurrentKeyboardInputSource());
  base::mac::ScopedCFTypeRef<CFStringRef> description(
      CFCopyDescription(input_source));
  base::mac::SetCrashKeyValue(@"tis_input_source",
                              base::mac::CFToNSCast(description));

  input_source.reset(TISCopyCurrentKeyboardLayoutInputSource());
  description.reset(CFCopyDescription(input_source));
  base::mac::SetCrashKeyValue(@"tis_layout_source",
                              base::mac::CFToNSCast(description));
}

void RendererMainPlatformDelegate::PlatformUninitialize() {
}

static void LogTestMessage(std::string message, bool is_error) {
  if (is_error)
    LOG(ERROR) << message;
  else
    LOG(INFO) << message;
}

bool RendererMainPlatformDelegate::InitSandboxTests(bool no_sandbox) {
  const CommandLine& command_line = parameters_.command_line;

  if (command_line.HasSwitch(switches::kTestSandbox)) {
    std::string bundle_path =
    command_line.GetSwitchValueNative(switches::kTestSandbox);
    if (bundle_path.empty()) {
      NOTREACHED() << "Bad bundle path";
      return false;
    }
    NSBundle* tests_bundle =
        [NSBundle bundleWithPath:base::SysUTF8ToNSString(bundle_path)];
    if (![tests_bundle load]) {
      NOTREACHED() << "Failed to load bundle";
      return false;
    }
    sandbox_tests_bundle_ = [tests_bundle retain];
    [objc_getClass("RendererSandboxTestsRunner") setLogFunction:LogTestMessage];
  }
  return true;
}

bool RendererMainPlatformDelegate::EnableSandbox() {
  return InitializeSandbox();
}

void RendererMainPlatformDelegate::RunSandboxTests(bool no_sandbox) {
  Class tests_runner = objc_getClass("RendererSandboxTestsRunner");
  if (tests_runner) {
    if (![tests_runner runTests])
      LOG(ERROR) << "Running renderer with failing sandbox tests!";
    [sandbox_tests_bundle_ unload];
    [sandbox_tests_bundle_ release];
    sandbox_tests_bundle_ = nil;
  }
}

}  // namespace content
