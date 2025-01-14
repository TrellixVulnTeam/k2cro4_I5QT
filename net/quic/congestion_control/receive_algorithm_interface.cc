// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/congestion_control/receive_algorithm_interface.h"

#include "net/quic/congestion_control/fix_rate_receiver.h"

namespace net {

// Factory for receive side congestion control algorithm.
ReceiveAlgorithmInterface* ReceiveAlgorithmInterface::Create(
    const QuicClock* clock,
    CongestionFeedbackType type) {
  switch (type) {
    case kNone:
      LOG(DFATAL) << "Attempted to create a ReceiveAlgorithm with kNone.";
      break;
    case kTCP:
      //return new TcpReceiver(clock);
    case kInterArrival:
      break;  // TODO(pwestin) Implement.
    case kFixRate:
      return new FixRateReceiver();
  }
  return NULL;
}

}  // namespace net
