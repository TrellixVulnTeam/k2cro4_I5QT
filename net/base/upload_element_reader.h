// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_UPLOAD_ELEMENT_READER_H_
#define NET_BASE_UPLOAD_ELEMENT_READER_H_

#include "base/basictypes.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"

namespace net {

class IOBuffer;
class UploadElement;

// An interface to read an upload data element.
class NET_EXPORT UploadElementReader {
 public:
  UploadElementReader() {}
  virtual ~UploadElementReader() {}

  // Creates an appropriate UploadElementReader instance for the given element.
  static UploadElementReader* Create(const UploadElement& element);

  // Initializes the instance synchronously when possible, otherwise does
  // initialization aynschronously, returns ERR_IO_PENDING and runs callback.
  // Calling this method again after a Init() success results in resetting the
  // state.
  virtual int Init(const CompletionCallback& callback) = 0;

  // Initializes the instance always synchronously.
  // Use this method only if the thread is IO allowed or the data is in-memory.
  virtual int InitSync();

  // Returns the byte-length of the element.  For files that do not exist, 0
  // is returned.  This is done for consistency with Mozilla.
  virtual uint64 GetContentLength() const = 0;

  // Returns the number of bytes remaining to read.
  virtual uint64 BytesRemaining() const = 0;

  // Returns true if the upload element is entirely in memory.
  // The default implementation returns false.
  virtual bool IsInMemory() const;

  // Reads up to |buf_length| bytes synchronously and returns the number of
  // bytes read when possible, otherwise, returns ERR_IO_PENDING and runs
  // |callback| with the result. This function never fails. If there's less data
  // to read than we initially observed, then pad with zero (this can happen
  // with files). |buf_length| must be greater than 0.
  virtual int Read(IOBuffer* buf,
                   int buf_length,
                   const CompletionCallback& callback) = 0;

  // Reads the data always synchronously.
  // Use this method only if the thread is IO allowed or the data is in-memory.
  virtual int ReadSync(IOBuffer* buf, int buf_length);

 private:
  DISALLOW_COPY_AND_ASSIGN(UploadElementReader);
};

}  // namespace net

#endif  // NET_BASE_UPLOAD_ELEMENT_READER_H_
