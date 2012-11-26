// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implements the Demuxer interface using FFmpeg's libavformat.  At this time
// will support demuxing any audio/video format thrown at it.  The streams
// output mime types audio/x-ffmpeg and video/x-ffmpeg and include an integer
// key FFmpegCodecID which contains the CodecID enumeration value.  The CodecIDs
// can be used to create and initialize the corresponding FFmpeg decoder.
//
// FFmpegDemuxer sets the duration of pipeline during initialization by using
// the duration of the longest audio/video stream.
//
// NOTE: since FFmpegDemuxer reads packets sequentially without seeking, media
// files with very large drift between audio/video streams may result in
// excessive memory consumption.
//
// When stopped, FFmpegDemuxer and FFmpegDemuxerStream release all callbacks
// and buffered packets.  Reads from a stopped FFmpegDemuxerStream will not be
// replied to.

#ifndef MEDIA_FILTERS_FFMPEG_DEMUXER_H_
#define MEDIA_FILTERS_FFMPEG_DEMUXER_H_

#include <deque>
#include <vector>

#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/threading/thread.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/decoder_buffer.h"
#include "media/base/demuxer.h"
#include "media/base/pipeline.h"
#include "media/base/video_decoder_config.h"
#include "media/filters/blocking_url_protocol.h"

// FFmpeg forward declarations.
struct AVPacket;
struct AVRational;
struct AVStream;

namespace media {

class FFmpegDemuxer;
class FFmpegGlue;
class FFmpegH264ToAnnexBBitstreamConverter;
class ScopedPtrAVFreePacket;

typedef scoped_ptr_malloc<AVPacket, ScopedPtrAVFreePacket> ScopedAVPacket;

class FFmpegDemuxerStream : public DemuxerStream {
 public:
  // Keeps a copy of |demuxer| and initializes itself using information
  // inside |stream|.  Both parameters must outlive |this|.
  FFmpegDemuxerStream(FFmpegDemuxer* demuxer, AVStream* stream);

  // Returns true is this stream has pending reads, false otherwise.
  bool HasPendingReads();

  // Enqueues the given AVPacket.  If |packet| is NULL an end of stream packet
  // is enqueued.
  void EnqueuePacket(ScopedAVPacket packet);

  // Signals to empty the buffer queue and mark next packet as discontinuous.
  void FlushBuffers();

  // Empties the queues and ignores any additional calls to Read().
  void Stop();

  // Returns the duration of this stream.
  base::TimeDelta duration();

  // DemuxerStream implementation.
  virtual Type type() OVERRIDE;
  virtual void Read(const ReadCB& read_cb) OVERRIDE;
  virtual void EnableBitstreamConverter() OVERRIDE;
  virtual const AudioDecoderConfig& audio_decoder_config() OVERRIDE;
  virtual const VideoDecoderConfig& video_decoder_config() OVERRIDE;

  // Returns the range of buffered data in this stream.
  Ranges<base::TimeDelta> GetBufferedRanges() const;

  // Returns elapsed time based on the already queued packets.
  // Used to determine stream duration when it's not known ahead of time.
  base::TimeDelta GetElapsedTime() const;

 protected:
  virtual ~FFmpegDemuxerStream();

 private:
  friend class FFmpegDemuxerTest;

  // Runs callbacks in |read_queue_| for each available |buffer_queue_|, calling
  // NotifyHasPendingRead() if there are still pending items in |read_queue_|.
  void SatisfyPendingReads();

  // Converts an FFmpeg stream timestamp into a base::TimeDelta.
  static base::TimeDelta ConvertStreamTimestamp(const AVRational& time_base,
                                                int64 timestamp);

  FFmpegDemuxer* demuxer_;
  scoped_refptr<base::MessageLoopProxy> message_loop_;
  AVStream* stream_;
  AudioDecoderConfig audio_config_;
  VideoDecoderConfig video_config_;
  Type type_;
  base::TimeDelta duration_;
  bool stopped_;
  base::TimeDelta last_packet_timestamp_;
  Ranges<base::TimeDelta> buffered_ranges_;

  typedef std::deque<scoped_refptr<DecoderBuffer> > BufferQueue;
  BufferQueue buffer_queue_;

  typedef std::deque<ReadCB> ReadQueue;
  ReadQueue read_queue_;

  scoped_ptr<FFmpegH264ToAnnexBBitstreamConverter> bitstream_converter_;
  bool bitstream_converter_enabled_;

  DISALLOW_COPY_AND_ASSIGN(FFmpegDemuxerStream);
};

class MEDIA_EXPORT FFmpegDemuxer : public Demuxer {
 public:
  FFmpegDemuxer(const scoped_refptr<base::MessageLoopProxy>& message_loop,
                const scoped_refptr<DataSource>& data_source);

  // Demuxer implementation.
  virtual void Initialize(DemuxerHost* host,
                          const PipelineStatusCB& status_cb) OVERRIDE;
  virtual void Stop(const base::Closure& callback) OVERRIDE;
  virtual void Seek(base::TimeDelta time, const PipelineStatusCB& cb) OVERRIDE;
  virtual void OnAudioRendererDisabled() OVERRIDE;
  virtual void SetPlaybackRate(float playback_rate) OVERRIDE;
  virtual scoped_refptr<DemuxerStream> GetStream(
      DemuxerStream::Type type) OVERRIDE;
  virtual base::TimeDelta GetStartTime() const OVERRIDE;

  // Allow FFmpegDemuxerStream to notify us when it requires more data or has
  // updated information about what buffered data is available.
  void NotifyHasPendingRead();
  void NotifyBufferingChanged();

 private:
  // To allow tests access to privates.
  friend class FFmpegDemuxerTest;

  virtual ~FFmpegDemuxer();

  // Carries out initialization on the demuxer thread.
  void InitializeTask(DemuxerHost* host, const PipelineStatusCB& status_cb);
  void OnOpenContextDone(const PipelineStatusCB& status_cb, bool result);
  void OnFindStreamInfoDone(const PipelineStatusCB& status_cb, int result);

  // Carries out a seek on the demuxer thread.
  void SeekTask(base::TimeDelta time, const PipelineStatusCB& cb);
  void OnSeekFrameDone(const PipelineStatusCB& cb, int result);

  // Carries out demuxing and satisfying stream reads on the demuxer thread.
  void DemuxTask();
  void OnReadFrameDone(ScopedAVPacket packet, int result);

  // Carries out stopping the demuxer streams on the demuxer thread.
  void StopTask(const base::Closure& callback);
  void OnDataSourceStopped(const base::Closure& callback);

  // Carries out disabling the audio stream on the demuxer thread.
  void DisableAudioStreamTask();

  // Returns true if any of the streams have pending reads.  Since we lazily
  // post a DemuxTask() for every read, we use this method to quickly terminate
  // the tasks if there is no work to do.
  //
  // Must be called on the demuxer thread.
  bool StreamsHavePendingReads();

  // Signal all FFmpegDemuxerStream that the stream has ended.
  //
  // Must be called on the demuxer thread.
  void StreamHasEnded();

  // Called by |url_protocol_| whenever |data_source_| returns a read error.
  void OnDataSourceError();

  // Returns the stream from |streams_| that matches |type| as an
  // FFmpegDemuxerStream.
  scoped_refptr<FFmpegDemuxerStream> GetFFmpegStream(
      DemuxerStream::Type type) const;

  DemuxerHost* host_;

  scoped_refptr<base::MessageLoopProxy> message_loop_;

  // Thread on which all blocking FFmpeg operations are executed.
  base::Thread blocking_thread_;

  // |streams_| mirrors the AVStream array in |format_context_|. It contains
  // FFmpegDemuxerStreams encapsluating AVStream objects at the same index.
  //
  // Since we only support a single audio and video stream, |streams_| will
  // contain NULL entries for additional audio/video streams as well as for
  // stream types that we do not currently support.
  //
  // Once initialized, operations on FFmpegDemuxerStreams should be carried out
  // on the demuxer thread.
  typedef std::vector<scoped_refptr<FFmpegDemuxerStream> > StreamVector;
  StreamVector streams_;

  // Reference to the data source. Asynchronous read requests are submitted to
  // this object.
  scoped_refptr<DataSource> data_source_;

  // Derived bitrate after initialization has completed.
  int bitrate_;

  // The first timestamp of the opened media file. This is used to set the
  // starting clock value to match the timestamps in the media file. Default
  // is 0.
  base::TimeDelta start_time_;

  // Whether audio has been disabled for this demuxer (in which case this class
  // drops packets destined for AUDIO demuxer streams on the floor).
  bool audio_disabled_;

  // Set if we know duration of the audio stream. Used when processing end of
  // stream -- at this moment we definitely know duration.
  bool duration_known_;

  // FFmpegURLProtocol implementation and corresponding glue bits.
  BlockingUrlProtocol url_protocol_;
  scoped_ptr<FFmpegGlue> glue_;

  DISALLOW_COPY_AND_ASSIGN(FFmpegDemuxer);
};

}  // namespace media

#endif  // MEDIA_FILTERS_FFMPEG_DEMUXER_H_
