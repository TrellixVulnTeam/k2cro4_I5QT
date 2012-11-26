// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/command_executor_impl.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/values.h"
#include "chrome/test/chromedriver/commands.h"
#include "chrome/test/chromedriver/session.h"
#include "chrome/test/chromedriver/session_command.h"
#include "chrome/test/chromedriver/session_map.h"
#include "chrome/test/chromedriver/status.h"

CommandExecutorImpl::CommandExecutorImpl() {
  base::Callback<Status(
      const SessionCommand&,
      const base::DictionaryValue&,
      const std::string&,
      scoped_ptr<base::Value>*,
      std::string*)> execute_session_command = base::Bind(
          &ExecuteSessionCommand,
          &session_map_);
  Command quit_command = base::Bind(execute_session_command,
      base::Bind(&ExecuteQuit, &session_map_));
  command_map_.Set("quit", quit_command);

  // Non-session commands.
  command_map_.Set("newSession",
      base::Bind(&ExecuteNewSession, &session_map_, &launcher_));
  command_map_.Set("quitAll",
      base::Bind(&ExecuteQuitAll, quit_command, &session_map_));
}

CommandExecutorImpl::~CommandExecutorImpl() {}

void CommandExecutorImpl::ExecuteCommand(
    const std::string& name,
    const base::DictionaryValue& params,
    const std::string& session_id,
    StatusCode* status_code,
    scoped_ptr<base::Value>* value,
    std::string* out_session_id) {
  Command cmd;
  Status status(kOk);
  if (command_map_.Get(name, &cmd)) {
    status = cmd.Run(params, session_id, value, out_session_id);
  } else {
    status = Status(kUnknownCommand, name);
    *out_session_id = session_id;
  }
  *status_code = status.code();
  if (status.IsError()) {
    scoped_ptr<base::DictionaryValue> error(new base::DictionaryValue());
    error->SetString("message", status.message());
    value->reset(error.release());
  }
  if (!*value)
    value->reset(base::Value::CreateNullValue());
}
