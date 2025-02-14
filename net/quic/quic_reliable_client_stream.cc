// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_reliable_client_stream.h"

#include "net/base/net_errors.h"

namespace net {

QuicReliableClientStream::QuicReliableClientStream(QuicStreamId id,
                                                   QuicSession* session)
    : ReliableQuicStream(id, session) {
}

QuicReliableClientStream::~QuicReliableClientStream() {
}

uint32 QuicReliableClientStream::ProcessData(const char* data,
                                             uint32 data_len) {
  int rv = delegate_->OnDataReceived(data, data_len);
  if (rv != OK) {
    DLOG(ERROR) << "Delegate refused data, rv: " << rv;
    Close(QUIC_BAD_APPLICATION_PAYLOAD);
    return 0;
  }
  return data_len;
}

void QuicReliableClientStream::TerminateFromPeer(bool half_close) {
  delegate_->OnClose(error());
}

void QuicReliableClientStream::SetDelegate(
    QuicReliableClientStream::Delegate* delegate) {
  delegate_ = delegate;
}

}  // namespace net
