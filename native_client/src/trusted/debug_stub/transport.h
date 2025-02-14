/*
 * Copyright (c) 2010 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


// This module provides interfaces for an IO stream.  The stream is
// expected to throw a std::exception if the stream is terminated on
// either side.
#ifndef NATIVE_CLIENT_PORT_TRANSPORT_H_
#define NATIVE_CLIENT_PORT_TRANSPORT_H_

#include "native_client/src/include/portability.h"
#include "native_client/src/include/portability_sockets.h"

namespace port {

class ITransport {
 public:
  virtual ~ITransport() {}  // Allow to delete using base pointer

  // Read from this transport, return true on success.
  virtual bool Read(void *ptr, int32_t len) = 0;

  // Write to this transport, return true on success.
  virtual bool Write(const void *ptr, int32_t len)  = 0;

  // Return true if there is data to read.
  virtual bool IsDataAvailable() = 0;

  // Disconnect the transport, R/W and Select will now throw an exception
  virtual void Disconnect() = 0;
};

class SocketBinding {
 public:
  // Wrap existing socket handle.
  explicit SocketBinding(NaClSocketHandle socket_handle);
  // Bind to the specified TCP port.
  static SocketBinding *Bind(const char *addr);

  // Accept a connection on an already-bound TCP port.
  ITransport *AcceptConnection();

 private:
  NaClSocketHandle socket_handle_;
};

}  // namespace port

#endif  // NATIVE_CLIENT_PORT_TRANSPORT_H_

