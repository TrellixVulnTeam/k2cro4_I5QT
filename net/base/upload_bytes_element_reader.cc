// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/upload_bytes_element_reader.h"

#include "base/logging.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"

namespace net {

UploadBytesElementReader::UploadBytesElementReader(const char* bytes,
                                                   int bytes_length)
    : bytes_(bytes),
      bytes_length_(bytes_length),
      offset_(0) {
}

UploadBytesElementReader::~UploadBytesElementReader() {
}

int UploadBytesElementReader::Init(const CompletionCallback& callback) {
  return InitSync();
}

int UploadBytesElementReader::InitSync() {
  offset_ = 0;
  return OK;
}

uint64 UploadBytesElementReader::GetContentLength() const {
  return bytes_length_;
}

uint64 UploadBytesElementReader::BytesRemaining() const {
  return bytes_length_ - offset_;
}

bool UploadBytesElementReader::IsInMemory() const {
  return true;
}

int UploadBytesElementReader::Read(IOBuffer* buf,
                                   int buf_length,
                                   const CompletionCallback& callback) {
  DCHECK(!callback.is_null());
  return ReadSync(buf, buf_length);
}

int UploadBytesElementReader::ReadSync(IOBuffer* buf, int buf_length) {
  DCHECK_LT(0, buf_length);

  const size_t num_bytes_to_read =
      std::min(BytesRemaining(), static_cast<uint64>(buf_length));

  // Check if we have anything to copy first, because we are getting
  // the address of an element in |bytes_| and that will throw an
  // exception if |bytes_| is an empty vector.
  if (num_bytes_to_read > 0)
    memcpy(buf->data(), bytes_ + offset_, num_bytes_to_read);

  offset_ += num_bytes_to_read;
  return num_bytes_to_read;
}

}  // namespace net
