// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_CLIENT_SESSION_H_
#define REMOTING_HOST_CLIENT_SESSION_H_

#include <list>

#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner_helpers.h"
#include "base/time.h"
#include "base/timer.h"
#include "base/threading/non_thread_safe.h"
#include "remoting/host/mouse_clamping_filter.h"
#include "remoting/host/remote_input_filter.h"
#include "remoting/protocol/clipboard_echo_filter.h"
#include "remoting/protocol/clipboard_filter.h"
#include "remoting/protocol/clipboard_stub.h"
#include "remoting/protocol/connection_to_client.h"
#include "remoting/protocol/host_stub.h"
#include "remoting/protocol/input_event_tracker.h"
#include "remoting/protocol/input_filter.h"
#include "remoting/protocol/input_stub.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkSize.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace remoting {

class AudioEncoder;
class AudioScheduler;
struct ClientSessionTraits;
class DesktopEnvironment;
class DesktopEnvironmentFactory;
class VideoEncoder;
class VideoFrameCapturer;
class VideoScheduler;

// A ClientSession keeps a reference to a connection to a client, and maintains
// per-client state.
class ClientSession
    : public base::RefCountedThreadSafe<ClientSession, ClientSessionTraits>,
      public protocol::HostStub,
      public protocol::ConnectionToClient::EventHandler,
      public base::NonThreadSafe {
 public:
  // Callback interface for passing events to the ChromotingHost.
  class EventHandler {
   public:
    // Called after authentication has finished successfully.
    virtual void OnSessionAuthenticated(ClientSession* client) = 0;

    // Called after we've finished connecting all channels.
    virtual void OnSessionChannelsConnected(ClientSession* client) = 0;

    // Called after authentication has failed. Must not tear down this
    // object. OnSessionClosed() is notified after this handler
    // returns.
    virtual void OnSessionAuthenticationFailed(ClientSession* client) = 0;

    // Called after connection has failed or after the client closed it.
    virtual void OnSessionClosed(ClientSession* client) = 0;

    // Called to notify of each message's sequence number. The
    // callback must not tear down this object.
    virtual void OnSessionSequenceNumber(ClientSession* client,
                                         int64 sequence_number) = 0;

    // Called on notification of a route change event, when a channel is
    // connected.
    virtual void OnSessionRouteChange(
        ClientSession* client,
        const std::string& channel_name,
        const protocol::TransportRoute& route) = 0;

    // Called when the initial client dimensions are received, and when they
    // change.
    virtual void OnClientDimensionsChanged(ClientSession* client,
                                           const SkISize& size) = 0;

   protected:
    virtual ~EventHandler() {}
  };

  // |event_handler| must outlive |this|. |desktop_environment_factory| is only
  // used by the constructor to create an instance of DesktopEnvironment.
  ClientSession(
      EventHandler* event_handler,
      scoped_refptr<base::SingleThreadTaskRunner> audio_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> video_capture_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> video_encode_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> network_task_runner,
      scoped_ptr<protocol::ConnectionToClient> connection,
      DesktopEnvironmentFactory* desktop_environment_factory,
      const base::TimeDelta& max_duration);

  // protocol::HostStub interface.
  virtual void NotifyClientDimensions(
      const protocol::ClientDimensions& dimensions) OVERRIDE;
  virtual void ControlVideo(
      const protocol::VideoControl& video_control) OVERRIDE;
  virtual void ControlAudio(
      const protocol::AudioControl& audio_control) OVERRIDE;

  // protocol::ConnectionToClient::EventHandler interface.
  virtual void OnConnectionAuthenticated(
      protocol::ConnectionToClient* connection) OVERRIDE;
  virtual void OnConnectionChannelsConnected(
      protocol::ConnectionToClient* connection) OVERRIDE;
  virtual void OnConnectionClosed(protocol::ConnectionToClient* connection,
                                  protocol::ErrorCode error) OVERRIDE;
  virtual void OnSequenceNumberUpdated(
      protocol::ConnectionToClient* connection, int64 sequence_number) OVERRIDE;
  virtual void OnRouteChange(
      protocol::ConnectionToClient* connection,
      const std::string& channel_name,
      const protocol::TransportRoute& route) OVERRIDE;

  // Disconnects the session and destroys the transport. Event handler
  // is guaranteed not to be called after this method is called. The object
  // should not be used after this method returns.
  void Disconnect();

  // Stop all recorders asynchronously. |done_task| is executed when the session
  // is completely stopped.
  void Stop(const base::Closure& done_task);

  protocol::ConnectionToClient* connection() const {
    return connection_.get();
  }

  DesktopEnvironment* desktop_environment() const {
    return desktop_environment_.get();
  }

  const std::string& client_jid() { return client_jid_; }

  bool is_authenticated() { return auth_input_filter_.enabled();  }

  // Indicate that local mouse activity has been detected. This causes remote
  // inputs to be ignored for a short time so that the local user will always
  // have the upper hand in 'pointer wars'.
  void LocalMouseMoved(const SkIPoint& new_pos);

  // Disable handling of input events from this client. If the client has any
  // keys or mouse buttons pressed then these will be released.
  void SetDisableInputs(bool disable_inputs);

 private:
  friend class base::DeleteHelper<ClientSession>;
  friend struct ClientSessionTraits;
  virtual ~ClientSession();

  // Creates a proxy for sending clipboard events to the client.
  scoped_ptr<protocol::ClipboardStub> CreateClipboardProxy();

  void OnRecorderStopped();

  // Creates an audio encoder for the specified configuration.
  static scoped_ptr<AudioEncoder> CreateAudioEncoder(
      const protocol::SessionConfig& config);

  // Creates a video encoder for the specified configuration.
  static scoped_ptr<VideoEncoder> CreateVideoEncoder(
      const protocol::SessionConfig& config);

  EventHandler* event_handler_;

  // The connection to the client.
  scoped_ptr<protocol::ConnectionToClient> connection_;

  // The desktop environment used by this session.
  scoped_ptr<DesktopEnvironment> desktop_environment_;

  std::string client_jid_;

  // The host clipboard and input stubs to which this object delegates.
  // These are the final elements in the clipboard & input pipelines, which
  // appear in order below.
  protocol::ClipboardStub* host_clipboard_stub_;
  protocol::InputStub* host_input_stub_;

  // Tracker used to release pressed keys and buttons when disconnecting.
  protocol::InputEventTracker input_tracker_;

  // Filter used to disable remote inputs during local input activity.
  RemoteInputFilter remote_input_filter_;

  // Filter used to clamp mouse events to the current display dimensions.
  MouseClampingFilter mouse_clamping_filter_;

  // Filter to used to stop clipboard items sent from the client being echoed
  // back to it.
  protocol::ClipboardEchoFilter clipboard_echo_filter_;

  // Filters used to manage enabling & disabling of input & clipboard.
  protocol::InputFilter disable_input_filter_;
  protocol::ClipboardFilter disable_clipboard_filter_;

  // Filters used to disable input & clipboard when we're not authenticated.
  protocol::InputFilter auth_input_filter_;
  protocol::ClipboardFilter auth_clipboard_filter_;

  // Factory for weak pointers to the client clipboard stub.
  // This must appear after |clipboard_echo_filter_|, so that it won't outlive
  // it.
  base::WeakPtrFactory<protocol::ClipboardStub> client_clipboard_factory_;

  // The maximum duration of this session.
  // There is no maximum if this value is <= 0.
  base::TimeDelta max_duration_;

  // A timer that triggers a disconnect when the maximum session duration
  // is reached.
  base::OneShotTimer<ClientSession> max_duration_timer_;


  scoped_refptr<base::SingleThreadTaskRunner> audio_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> video_capture_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> video_encode_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> network_task_runner_;

  // Schedulers for audio and video capture.
  scoped_refptr<AudioScheduler> audio_scheduler_;
  scoped_refptr<VideoScheduler> video_scheduler_;

  // Number of screen recorders and audio schedulers that are currently being
  // used or shutdown. Used to delay shutdown if one or more
  // recorders/schedulers are asynchronously shutting down.
  int active_recorders_;

  // The task to be executed when the session is completely stopped.
  base::Closure done_task_;

  DISALLOW_COPY_AND_ASSIGN(ClientSession);
};

// Destroys |ClienSession| instances on the network thread.
struct ClientSessionTraits {
  static void Destruct(const ClientSession* client);
};

}  // namespace remoting

#endif  // REMOTING_HOST_CLIENT_SESSION_H_
