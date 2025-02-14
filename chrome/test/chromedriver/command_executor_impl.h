// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_CHROMEDRIVER_COMMAND_EXECUTOR_IMPL_H_
#define CHROME_TEST_CHROMEDRIVER_COMMAND_EXECUTOR_IMPL_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/test/chromedriver/chrome_launcher_impl.h"
#include "chrome/test/chromedriver/command.h"
#include "chrome/test/chromedriver/command_executor.h"
#include "chrome/test/chromedriver/session_map.h"
#include "chrome/test/chromedriver/status.h"
#include "chrome/test/chromedriver/synchronized_map.h"

namespace base {
class DictionaryValue;
class Value;
}

class CommandExecutorImpl : public CommandExecutor {
 public:
  CommandExecutorImpl();
  virtual ~CommandExecutorImpl();

  // Overridden from CommandExecutor:
  virtual void ExecuteCommand(const std::string& name,
                              const base::DictionaryValue& params,
                              const std::string& session_id,
                              StatusCode* status_code,
                              scoped_ptr<base::Value>* value,
                              std::string* out_session_id) OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(CommandExecutorImplTest, SimpleCommand);
  FRIEND_TEST_ALL_PREFIXES(
      CommandExecutorImplTest, CommandThatDoesntSetValueOrSessionId);
  FRIEND_TEST_ALL_PREFIXES(CommandExecutorImplTest, CommandThatReturnsError);

  SessionMap session_map_;
  ChromeLauncherImpl launcher_;
  SynchronizedMap<std::string, Command> command_map_;

  DISALLOW_COPY_AND_ASSIGN(CommandExecutorImpl);
};

#endif  // CHROME_TEST_CHROMEDRIVER_COMMAND_EXECUTOR_IMPL_H_
