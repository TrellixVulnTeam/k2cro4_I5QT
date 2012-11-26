# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import ctypes
import json

class ChromeDriverException(Exception):
  pass
class UnknownCommand(ChromeDriverException):
  pass
class UnknownError(ChromeDriverException):
  pass
class SessionNotCreatedException(ChromeDriverException):
  pass
class NoSuchSession(ChromeDriverException):
  pass

def _ExceptionForResponse(response):
  exception_class_map = {
    9: UnknownCommand,
    13: UnknownError,
    33: SessionNotCreatedException,
    100: NoSuchSession
  }
  status = response['status']
  msg = response['value']['message']
  return exception_class_map.get(status, ChromeDriverException)(msg)

class ChromeDriver(object):
  """Starts and controls a single Chrome instance on this machine."""

  def __init__(self, lib_path):
    self._lib = ctypes.CDLL(lib_path)
    self._session_id = self._ExecuteCommand('newSession')

  def _ExecuteCommand(self, name, params={}, session_id=''):
    cmd = {
      'name': name,
      'parameters': params,
      'sessionId': session_id
    }
    cmd_json = json.dumps(cmd)
    response_data = ctypes.c_char_p()
    response_size = ctypes.c_uint()
    self._lib.ExecuteCommand(
        ctypes.c_char_p(cmd_json),
        ctypes.c_uint(len(cmd_json)),
        ctypes.byref(response_data),
        ctypes.byref(response_size))
    response_json = ctypes.string_at(response_data, response_size.value)
    self._lib.Free(response_data)
    response = json.loads(response_json)
    if response['status'] != 0:
      raise _ExceptionForResponse(response)
    return response['value']

  def _ExecuteSessionCommand(self, name, params={}):
    return self._ExecuteCommand(name, params, self._session_id)

  def Quit(self):
    """Quits the browser and ends the session."""
    self._ExecuteSessionCommand('quit')
