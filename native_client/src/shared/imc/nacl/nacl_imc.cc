/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// NaCl inter-module communication primitives.

#include "native_client/src/shared/imc/nacl_imc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/nacl_syscalls.h>
#include <unistd.h>

#include <algorithm>

#include "native_client/src/shared/imc/nacl_imc_c.h"

// Duplicate a NaCl file descriptor.
NaClHandle NaClDuplicateNaClHandle(NaClHandle handle) {
  return dup(handle);
}

namespace nacl {

bool WouldBlock() {
  return (errno == EAGAIN) ? true : false;
}

int GetLastErrorString(char* buffer, size_t length) {
  // Note newlib provides only GNU version of strerror_r().
  if (buffer == NULL || length == 0) {
    errno = ERANGE;
    return -1;
  }
  char* message = strerror_r(errno, buffer, length);
  if (message != buffer) {
    size_t message_bytes = strlen(message) + 1;
    length = std::min(message_bytes, length);
    memmove(buffer, message, length);
    buffer[length - 1] = '\0';
  }
  return 0;
}

Handle BoundSocket(const SocketAddress* address) {
  // TODO(shiki): Switch to the following once the make_bound_sock() prototype
  //              is cleaned up.
  // return make_bound_sock(address);
  return -1;
}

int SocketPair(Handle pair[2]) {
  return imc_socketpair(pair);
}

int Close(Handle handle) {
  return close(handle);
}

int SendDatagram(Handle handle, const MessageHeader* message, int flags) {
  return imc_sendmsg(handle, reinterpret_cast<const NaClImcMsgHdr*>(message),
                     flags);
}

int SendDatagramTo(const MessageHeader* message, int flags,
                   const SocketAddress* name) {
  return -1;  // TODO(bsy): how to implement this for NaCl?
}

int ReceiveDatagram(Handle handle, MessageHeader* message, int flags) {
  return imc_recvmsg(handle, reinterpret_cast<NaClImcMsgHdr*>(message), flags);
}

Handle CreateMemoryObject(size_t length, bool executable) {
  if (executable) {
    return -1;  // Will never work with NaCl and should never be invoked.
  }
  return imc_mem_obj_create(length);
}

void* Map(struct NaClDescEffector* effp,
          void* start, size_t length, int prot, int flags,
          Handle memory, off_t offset) {
  static int posix_prot[4] = {
    PROT_NONE,
    PROT_READ,
    PROT_WRITE,
    PROT_READ | PROT_WRITE
  };

  int adjusted = 0;
  if (flags & kMapShared) {
    adjusted |= MAP_SHARED;
  }
  if (flags & kMapPrivate) {
    adjusted |= MAP_PRIVATE;
  }
  if (flags & kMapFixed) {
    adjusted |= MAP_FIXED;
  }
  return mmap(start, length, posix_prot[prot & 3], adjusted, memory, offset);
}

int Unmap(void* start, size_t length) {
  return munmap(start, length);
}

}  // namespace nacl
