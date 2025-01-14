// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/congestion_control/fix_rate_receiver.h"

#include "base/basictypes.h"
#include "net/quic/congestion_control/receive_algorithm_interface.h"

namespace {
  static const int kInitialBitrate = 100000;  // In bytes per second.
}

namespace net {

FixRateReceiver::FixRateReceiver()
    : bitrate_in_bytes_per_second_(kInitialBitrate) {
}

bool FixRateReceiver::GenerateCongestionInfo(CongestionInfo* congestion_info) {
  congestion_info->type = kFixRate;
  congestion_info->fix_rate.bitrate_in_bytes_per_second =
      bitrate_in_bytes_per_second_;
  return true;
}

void FixRateReceiver::RecordIncomingPacket(
    size_t /*bytes*/,
    QuicPacketSequenceNumber /*sequence_number*/,
    QuicTime /*timestamp*/,
    bool /*recovered*/) {
  // Nothing to do for this simple implementation.
}

void FixRateReceiver::SetBitrate(int bytes_per_second) {
  bitrate_in_bytes_per_second_ = bytes_per_second;
}

}  // namespace net
