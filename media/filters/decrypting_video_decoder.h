// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FILTERS_DECRYPTING_VIDEO_DECODER_H_
#define MEDIA_FILTERS_DECRYPTING_VIDEO_DECODER_H_

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "media/base/decryptor.h"
#include "media/base/demuxer_stream.h"
#include "media/base/video_decoder.h"

namespace base {
class MessageLoopProxy;
}

namespace media {

class DecoderBuffer;
class Decryptor;

// Decryptor-based VideoDecoder implementation that can decrypt and decode
// encrypted video buffers and return decrypted and decompressed video frames.
// All public APIs and callbacks are trampolined to the |message_loop_| so
// that no locks are required for thread safety.
//
// TODO(xhwang): For now, DecryptingVideoDecoder relies on the decryptor to do
// both decryption and video decoding. Add the path to use the decryptor for
// decryption only and use other VideoDecoder implementations within
// DecryptingVideoDecoder for video decoding.
class MEDIA_EXPORT DecryptingVideoDecoder : public VideoDecoder {
 public:
  // Callback to get a message loop.
  typedef base::Callback<
      scoped_refptr<base::MessageLoopProxy>()> MessageLoopFactoryCB;
  // Callback to notify decryptor creation.
  typedef base::Callback<void(Decryptor*)> DecryptorNotificationCB;
  // Callback to request/cancel decryptor creation notification.
  // Calling this callback with a non-null callback registers decryptor creation
  // notification. When the decryptor is created, notification will be sent
  // through the provided callback.
  // Calling this callback with a null callback cancels previously registered
  // decryptor creation notification. Any previously provided callback will be
  // fired immediately with NULL.
  typedef base::Callback<void(const DecryptorNotificationCB&)>
      RequestDecryptorNotificationCB;

  DecryptingVideoDecoder(
      const MessageLoopFactoryCB& message_loop_factory_cb,
      const RequestDecryptorNotificationCB& request_decryptor_notification_cb);

  // VideoDecoder implementation.
  virtual void Initialize(const scoped_refptr<DemuxerStream>& stream,
                          const PipelineStatusCB& status_cb,
                          const StatisticsCB& statistics_cb) OVERRIDE;
  virtual void Read(const ReadCB& read_cb) OVERRIDE;
  virtual void Reset(const base::Closure& closure) OVERRIDE;
  virtual void Stop(const base::Closure& closure) OVERRIDE;

 protected:
  virtual ~DecryptingVideoDecoder();

 private:
  // For a detailed state diagram please see this link: http://goo.gl/8jAok
  // TODO(xhwang): Add a ASCII state diagram in this file after this class
  // stabilizes.
  enum State {
    kUninitialized = 0,
    kDecryptorRequested,
    kPendingDecoderInit,
    kIdle,
    kPendingDemuxerRead,
    kPendingDecode,
    kWaitingForKey,
    kDecodeFinished,
    kStopped
  };

  // Carries out the initialization operation scheduled by Initialize().
  void DoInitialize(const scoped_refptr<DemuxerStream>& stream,
                    const PipelineStatusCB& status_cb,
                    const StatisticsCB& statistics_cb);

  // Callback for DecryptorHost::RequestDecryptor().
  void SetDecryptor(Decryptor* decryptor);

  // Callback for Decryptor::InitializeVideoDecoder().
  void FinishInitialization(bool success);

  // Carries out the buffer reading operation scheduled by Read().
  void DoRead(const ReadCB& read_cb);

  void ReadFromDemuxerStream();

  // Callback for DemuxerStream::Read().
  void DoDecryptAndDecodeBuffer(DemuxerStream::Status status,
                                const scoped_refptr<DecoderBuffer>& buffer);

  void DecodePendingBuffer();

  // Callback for Decryptor::DecryptAndDecodeVideo().
  void DeliverFrame(int buffer_size,
                    Decryptor::Status status,
                    const scoped_refptr<VideoFrame>& frame);

  // Carries out the frame delivery operation scheduled by DeliverFrame().
  void DoDeliverFrame(int buffer_size,
                      Decryptor::Status status,
                      const scoped_refptr<VideoFrame>& frame);

  // Callback for the |decryptor_| to notify this object that a new key has been
  // added.
  void OnKeyAdded();

  // Reset decoder and call |reset_cb_|.
  void DoReset();

  // Free decoder resources and call |stop_cb_|.
  void DoStop();

  // This is !is_null() iff Initialize() hasn't been called.
  MessageLoopFactoryCB message_loop_factory_cb_;

  scoped_refptr<base::MessageLoopProxy> message_loop_;

  State state_;

  PipelineStatusCB init_cb_;
  StatisticsCB statistics_cb_;
  ReadCB read_cb_;
  base::Closure reset_cb_;

  // Pointer to the demuxer stream that will feed us compressed buffers.
  scoped_refptr<DemuxerStream> demuxer_stream_;

  // Callback to request/cancel decryptor creation notification.
  RequestDecryptorNotificationCB request_decryptor_notification_cb_;

  Decryptor* decryptor_;

  // The buffer returned by the demuxer that needs decrypting/decoding.
  scoped_refptr<media::DecoderBuffer> pending_buffer_to_decode_;

  // Indicates the situation where new key is added during pending decode
  // (in other words, this variable can only be set in state kPendingDecode).
  // If this variable is true and kNoKey is returned then we need to try
  // decrypting/decoding again in case the newly added key is the correct
  // decryption key.
  bool key_added_while_decode_pending_;

  // A unique ID to trace Decryptor::DecryptAndDecodeVideo() call and the
  // matching DecryptCB call (in DoDeliverFrame()).
  uint32 trace_id_;

  DISALLOW_COPY_AND_ASSIGN(DecryptingVideoDecoder);
};

}  // namespace media

#endif  // MEDIA_FILTERS_DECRYPTING_VIDEO_DECODER_H_
