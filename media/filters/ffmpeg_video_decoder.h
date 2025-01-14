// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FILTERS_FFMPEG_VIDEO_DECODER_H_
#define MEDIA_FILTERS_FFMPEG_VIDEO_DECODER_H_

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "media/base/decryptor.h"
#include "media/base/demuxer_stream.h"
#include "media/base/video_decoder.h"

struct AVCodecContext;
struct AVFrame;

namespace base {
class MessageLoopProxy;
}

namespace media {

class DecoderBuffer;

class MEDIA_EXPORT FFmpegVideoDecoder : public VideoDecoder {
 public:
  typedef base::Callback<
      scoped_refptr<base::MessageLoopProxy>()> MessageLoopFactoryCB;
  FFmpegVideoDecoder(const MessageLoopFactoryCB& message_loop_factory_cb,
                     Decryptor* decryptor);

  // VideoDecoder implementation.
  virtual void Initialize(const scoped_refptr<DemuxerStream>& stream,
                          const PipelineStatusCB& status_cb,
                          const StatisticsCB& statistics_cb) OVERRIDE;
  virtual void Read(const ReadCB& read_cb) OVERRIDE;
  virtual void Reset(const base::Closure& closure) OVERRIDE;
  virtual void Stop(const base::Closure& closure) OVERRIDE;

  // Callback called from within FFmpeg to allocate a buffer based on
  // the dimensions of |codec_context|. See AVCodecContext.get_buffer
  // documentation inside FFmpeg.
  int GetVideoBuffer(AVCodecContext *codec_context, AVFrame* frame);

 protected:
  virtual ~FFmpegVideoDecoder();

 private:
  enum DecoderState {
    kUninitialized,
    kNormal,
    kFlushCodec,
    kDecodeFinished
  };

  // Carries out the reading operation scheduled by Read().
  void DoRead(const ReadCB& read_cb);

  // Reads from the demuxer stream.
  void ReadFromDemuxerStream();

  // Carries out the buffer processing operation scheduled by
  // DecryptOrDecodeBuffer().
  void DoDecryptOrDecodeBuffer(DemuxerStream::Status status,
                               const scoped_refptr<DecoderBuffer>& buffer);

  // Callback called by the decryptor to deliver decrypted data buffer and
  // reporting decrypt status. This callback could be called synchronously or
  // asynchronously.
  void BufferDecrypted(Decryptor::Status decrypt_status,
                       const scoped_refptr<DecoderBuffer>& buffer);

  // Carries out the operation scheduled by BufferDecrypted().
  void DoBufferDecrypted(Decryptor::Status decrypt_status,
                         const scoped_refptr<DecoderBuffer>& buffer);

  void DecodeBuffer(const scoped_refptr<DecoderBuffer>& buffer);
  bool Decode(const scoped_refptr<DecoderBuffer>& buffer,
              scoped_refptr<VideoFrame>* video_frame);

  // Handles (re-)initializing the decoder with a (new) config.
  // Returns true if initialization was successful.
  bool ConfigureDecoder();

  // Releases resources associated with |codec_context_| and |av_frame_|
  // and resets them to NULL.
  void ReleaseFFmpegResources();

  // Reset decoder and call |reset_cb_|.
  void DoReset();

  // This is !is_null() iff Initialize() hasn't been called.
  MessageLoopFactoryCB message_loop_factory_cb_;

  scoped_refptr<base::MessageLoopProxy> message_loop_;

  DecoderState state_;

  StatisticsCB statistics_cb_;

  ReadCB read_cb_;
  base::Closure reset_cb_;

  // FFmpeg structures owned by this object.
  AVCodecContext* codec_context_;
  AVFrame* av_frame_;

  // Pointer to the demuxer stream that will feed us compressed buffers.
  scoped_refptr<DemuxerStream> demuxer_stream_;

  Decryptor* decryptor_;

  DISALLOW_COPY_AND_ASSIGN(FFmpegVideoDecoder);
};

}  // namespace media

#endif  // MEDIA_FILTERS_FFMPEG_VIDEO_DECODER_H_
