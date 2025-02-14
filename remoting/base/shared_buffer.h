// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_BASE_SHARED_BUFFER_H_
#define REMOTING_BASE_SHARED_BUFFER_H_

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/process.h"
#include "base/shared_memory.h"

namespace remoting {

// Represents a memory buffer that can be shared between multiple processes.
// It is more or less a convenience wrapper around base::SharedMemory providing
// ref-counted lifetime management and unique buffer identifiers.
class SharedBuffer
    : public base::RefCountedThreadSafe<SharedBuffer> {
 public:
  // Creates a new shared memory buffer of the given size and maps it to
  // the memory of the calling process. If the operation fails for any reason,
  // ptr() method will return NULL. This constructor set the identifier of this
  // buffer to 0.
  explicit SharedBuffer(uint32 size);

  // Opens an existing shared memory buffer and maps it to the memory of
  // the calling process (in read only mode). If the operation fails for any
  // reason, ptr() method will return NULL.
  SharedBuffer(intptr_t id, base::SharedMemoryHandle handle, uint32 size);

  // Opens an existing shared memory buffer created by a different process and
  // maps it to the memory of the calling process (in read only mode). If
  // the operation fails for any reason, ptr() method will return NULL.
  SharedBuffer(intptr_t id, base::SharedMemoryHandle handle,
               base::ProcessHandle process, uint32 size);

  // Returns pointer to the beginning of the allocated data buffer. Returns NULL
  // if the object initialization failed for any reason.
  void* ptr() const { return shared_memory_.memory(); }

  // Returns handle of the shared memory section containing the allocated
  // data buffer.
  base::SharedMemoryHandle handle() const { return shared_memory_.handle(); }

  intptr_t id() const { return id_; }
  uint32 size() const { return size_; }

  void set_id(intptr_t id) { id_ = id; }

 private:
  friend class base::RefCountedThreadSafe<SharedBuffer>;
  virtual ~SharedBuffer();

  // Unique identifier of the buffer or 0 if ID hasn't been set.
  intptr_t id_;

  // Shared memory section backing up the buffer.
  base::SharedMemory shared_memory_;

  // Size of the buffer in bytes.
  uint32 size_;

  DISALLOW_COPY_AND_ASSIGN(SharedBuffer);
};

}  // namespace remoting

#endif  // REMOTING_BASE_SHARED_BUFFER_H_
