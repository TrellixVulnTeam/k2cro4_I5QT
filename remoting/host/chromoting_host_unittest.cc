// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_proxy.h"
#include "remoting/base/auto_thread_task_runner.h"
#include "remoting/jingle_glue/mock_objects.h"
#include "remoting/host/audio_capturer.h"
#include "remoting/host/chromoting_host_context.h"
#include "remoting/host/chromoting_host.h"
#include "remoting/host/desktop_environment.h"
#include "remoting/host/desktop_environment_factory.h"
#include "remoting/host/event_executor_fake.h"
#include "remoting/host/host_mock_objects.h"
#include "remoting/host/it2me_host_user_interface.h"
#include "remoting/host/video_frame_capturer_fake.h"
#include "remoting/proto/video.pb.h"
#include "remoting/protocol/errors.h"
#include "remoting/protocol/protocol_mock_objects.h"
#include "remoting/protocol/session_config.h"
#include "testing/gmock_mutant.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::remoting::protocol::MockClientStub;
using ::remoting::protocol::MockConnectionToClient;
using ::remoting::protocol::MockConnectionToClientEventHandler;
using ::remoting::protocol::MockHostStub;
using ::remoting::protocol::MockSession;
using ::remoting::protocol::MockVideoStub;
using ::remoting::protocol::SessionConfig;

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::CreateFunctor;
using testing::DeleteArg;
using testing::DoAll;
using testing::Expectation;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeArgument;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;
using testing::Sequence;

namespace remoting {

namespace {

void PostQuitTask(MessageLoop* message_loop) {
  message_loop->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}

// Run the task and delete it afterwards. This action is used to deal with
// done callbacks.
ACTION(RunDoneTask) {
  arg1.Run();
}

}  // namespace

class MockIt2MeHostUserInterface : public It2MeHostUserInterface {
 public:
  MockIt2MeHostUserInterface(
      scoped_refptr<base::SingleThreadTaskRunner> network_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner);

  void InitFrom(
      scoped_ptr<DisconnectWindow> disconnect_window,
      scoped_ptr<ContinueWindow> continue_window,
      scoped_ptr<LocalInputMonitor> local_input_monitor);

  // A test-only version of Start that does not register a HostStatusObserver.
  // TODO(rmsousa): Make the unit tests work with the regular Start().
  virtual void Start(ChromotingHost* host,
                     const base::Closure& disconnect_callback) OVERRIDE;
};

MockIt2MeHostUserInterface::MockIt2MeHostUserInterface(
    scoped_refptr<base::SingleThreadTaskRunner> network_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner)
    : It2MeHostUserInterface(network_task_runner, ui_task_runner) {
}

void MockIt2MeHostUserInterface::InitFrom(
    scoped_ptr<DisconnectWindow> disconnect_window,
    scoped_ptr<ContinueWindow> continue_window,
    scoped_ptr<LocalInputMonitor> local_input_monitor) {
  DCHECK(ui_task_runner()->BelongsToCurrentThread());

  disconnect_window_ = disconnect_window.Pass();
  continue_window_ = continue_window.Pass();
  local_input_monitor_ = local_input_monitor.Pass();
}

void MockIt2MeHostUserInterface::Start(
    ChromotingHost* host, const base::Closure& disconnect_callback) {
  DCHECK(network_task_runner()->BelongsToCurrentThread());
  DCHECK(host_ == NULL);

  host_ = host;
  disconnect_callback_ = disconnect_callback;
}

class ChromotingHostTest : public testing::Test {
 public:
  ChromotingHostTest() {
  }

  virtual void SetUp() OVERRIDE {
    ui_task_runner_ = new AutoThreadTaskRunner(
        message_loop_.message_loop_proxy(),
        base::Bind(&ChromotingHostTest::QuitMainMessageLoop,
                   base::Unretained(this)));

    EXPECT_CALL(context_, ui_task_runner())
        .Times(AnyNumber())
        .WillRepeatedly(Return(ui_task_runner_.get()));
    EXPECT_CALL(context_, capture_task_runner())
        .Times(AnyNumber())
        .WillRepeatedly(Return(ui_task_runner_.get()));
    EXPECT_CALL(context_, encode_task_runner())
        .Times(AnyNumber())
        .WillRepeatedly(Return(ui_task_runner_.get()));
    EXPECT_CALL(context_, network_task_runner())
        .Times(AnyNumber())
        .WillRepeatedly(Return(ui_task_runner_.get()));

    desktop_environment_factory_.reset(new MockDesktopEnvironmentFactory());
    EXPECT_CALL(*desktop_environment_factory_, CreatePtr(_))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke(this,
                               &ChromotingHostTest::CreateDesktopEnvironment));

    session_manager_ = new protocol::MockSessionManager();

    host_ = new ChromotingHost(
        &signal_strategy_,
        desktop_environment_factory_.get(),
        scoped_ptr<protocol::SessionManager>(session_manager_),
        context_.audio_task_runner(),
        context_.capture_task_runner(),
        context_.encode_task_runner(),
        context_.network_task_runner());
    host_->AddStatusObserver(&host_status_observer_);

    disconnect_window_ = new MockDisconnectWindow();
    continue_window_ = new MockContinueWindow();
    local_input_monitor_ = new MockLocalInputMonitor();
    it2me_host_user_interface_.reset(
        new MockIt2MeHostUserInterface(context_.network_task_runner(),
                                       context_.ui_task_runner()));
    it2me_host_user_interface_->InitFrom(
        scoped_ptr<DisconnectWindow>(disconnect_window_),
        scoped_ptr<ContinueWindow>(continue_window_),
        scoped_ptr<LocalInputMonitor>(local_input_monitor_));

    it2me_host_user_interface_->Start(
        host_, base::Bind(&ChromotingHost::Shutdown, host_, base::Closure()));

    xmpp_login_ = "host@domain";
    session1_ = new MockSession();
    session2_ = new MockSession();
    session_unowned1_.reset(new MockSession());
    session_unowned2_.reset(new MockSession());
    session_config1_ = SessionConfig::ForTest();
    session_jid1_ = "user@domain/rest-of-jid";
    session_config2_ = SessionConfig::ForTest();
    session_jid2_ = "user2@domain/rest-of-jid";
    session_unowned_config1_ = SessionConfig::ForTest();
    session_unowned_jid1_ = "user3@doman/rest-of-jid";
    session_unowned_config2_ = SessionConfig::ForTest();
    session_unowned_jid2_ = "user4@doman/rest-of-jid";

    EXPECT_CALL(*session1_, jid())
        .WillRepeatedly(ReturnRef(session_jid1_));
    EXPECT_CALL(*session2_, jid())
        .WillRepeatedly(ReturnRef(session_jid2_));
    EXPECT_CALL(*session_unowned1_, jid())
        .WillRepeatedly(ReturnRef(session_unowned_jid1_));
    EXPECT_CALL(*session_unowned2_, jid())
        .WillRepeatedly(ReturnRef(session_unowned_jid2_));
    EXPECT_CALL(*session1_, SetEventHandler(_))
        .Times(AnyNumber());
    EXPECT_CALL(*session2_, SetEventHandler(_))
        .Times(AnyNumber());
    EXPECT_CALL(*session_unowned1_, SetEventHandler(_))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke(this, &ChromotingHostTest::SetEventHandler));
    EXPECT_CALL(*session_unowned2_, SetEventHandler(_))
        .Times(AnyNumber());
    EXPECT_CALL(*session1_, config())
        .WillRepeatedly(ReturnRef(session_config1_));
    EXPECT_CALL(*session2_, config())
        .WillRepeatedly(ReturnRef(session_config2_));

    owned_connection1_.reset(new MockConnectionToClient(session1_,
                                                        &host_stub1_));
    connection1_ = owned_connection1_.get();
    owned_connection2_.reset(new MockConnectionToClient(session2_,
                                                        &host_stub2_));
    connection2_ = owned_connection2_.get();

    ON_CALL(video_stub1_, ProcessVideoPacketPtr(_, _))
        .WillByDefault(DeleteArg<0>());
    ON_CALL(video_stub2_, ProcessVideoPacketPtr(_, _))
        .WillByDefault(DeleteArg<0>());
    ON_CALL(*connection1_, video_stub())
        .WillByDefault(Return(&video_stub1_));
    ON_CALL(*connection1_, client_stub())
        .WillByDefault(Return(&client_stub1_));
    ON_CALL(*connection1_, session())
        .WillByDefault(Return(session1_));
    ON_CALL(*connection2_, video_stub())
        .WillByDefault(Return(&video_stub2_));
    ON_CALL(*connection2_, client_stub())
        .WillByDefault(Return(&client_stub2_));
    ON_CALL(*connection2_, session())
        .WillByDefault(Return(session2_));
    EXPECT_CALL(*connection1_, video_stub())
        .Times(AnyNumber());
    EXPECT_CALL(*connection1_, client_stub())
        .Times(AnyNumber());
    EXPECT_CALL(*connection1_, session())
        .Times(AnyNumber());
    EXPECT_CALL(*connection2_, video_stub())
        .Times(AnyNumber());
    EXPECT_CALL(*connection2_, client_stub())
        .Times(AnyNumber());
    EXPECT_CALL(*connection2_, session())
        .Times(AnyNumber());

    empty_candidate_config_ =
        protocol::CandidateSessionConfig::CreateEmpty();
    default_candidate_config_ =
        protocol::CandidateSessionConfig::CreateDefault();
  }

  // Helper method to pretend a client is connected to ChromotingHost.
  void SimulateClientConnection(int connection_index, bool authenticate,
                                bool reject) {
    scoped_ptr<protocol::ConnectionToClient> connection =
        ((connection_index == 0) ? owned_connection1_ : owned_connection2_).
        PassAs<protocol::ConnectionToClient>();
    protocol::ConnectionToClient* connection_ptr = connection.get();
    scoped_refptr<ClientSession> client = new ClientSession(
        host_.get(),
        context_.audio_task_runner(),
        context_.capture_task_runner(),
        context_.encode_task_runner(),
        context_.network_task_runner(),
        connection.Pass(),
        desktop_environment_factory_.get(),
        base::TimeDelta());
    connection_ptr->set_host_stub(client);
    connection_ptr->set_input_stub(
        client->desktop_environment()->event_executor());

    context_.network_task_runner()->PostTask(
        FROM_HERE, base::Bind(&ChromotingHostTest::AddClientToHost,
                              host_, client));

    if (authenticate) {
      context_.network_task_runner()->PostTask(
          FROM_HERE, base::Bind(&ClientSession::OnConnectionAuthenticated,
                                client, connection_ptr));
      if (!reject) {
        context_.network_task_runner()->PostTask(
            FROM_HERE,
            base::Bind(&ClientSession::OnConnectionChannelsConnected,
                       client, connection_ptr));
      }
    } else {
      context_.network_task_runner()->PostTask(
          FROM_HERE, base::Bind(&ClientSession::OnConnectionClosed,
                                client, connection_ptr,
                                protocol::AUTHENTICATION_FAILED));
    }

    get_client(connection_index) = client;
  }

  virtual void TearDown() OVERRIDE {
    // Make sure that the host has been properly deleted.
    DCHECK(host_.get() == NULL);
  }

  // Change the session route for |client1_|.
  void ChangeSessionRoute(const std::string& channel_name,
                          const protocol::TransportRoute& route) {
    host_->OnSessionRouteChange(get_client(0), channel_name, route);
  }

  DesktopEnvironment* CreateDesktopEnvironment(ClientSession* client) {
    scoped_ptr<EventExecutor> event_executor(new EventExecutorFake());
    scoped_ptr<VideoFrameCapturer> video_capturer(new VideoFrameCapturerFake());
    return new DesktopEnvironment(scoped_ptr<AudioCapturer>(NULL),
                                  event_executor.Pass(),
                                  video_capturer.Pass());
  }

  void DisconnectAllClients() {
    host_->DisconnectAllClients();
  }

  // Helper method to disconnect client 1 from the host.
  void DisconnectClient1() {
    client1_->OnConnectionClosed(connection1_, protocol::OK);
  }

  // Notify |host_| that the authenticating client has been rejected.
  void RejectAuthenticatingClient() {
    host_->RejectAuthenticatingClient();
  }

  // Notify |host_| that a client session has closed.
  void NotifyClientSessionClosed(int connection_index) {
    host_->OnSessionClosed(get_client(connection_index));
  }

  void SetEventHandler(protocol::Session::EventHandler* event_handler) {
    session_event_handler_ = event_handler;
  }

  void NotifyConnectionClosed() {
    if (session_event_handler_) {
      session_event_handler_->OnSessionStateChange(protocol::Session::CLOSED);
    }
  }

  static void AddClientToHost(scoped_refptr<ChromotingHost> host,
                              ClientSession* session) {
    host->clients_.push_back(session);
    host->clients_count_++;
  }

  void ShutdownHost() {
    ui_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&ChromotingHost::Shutdown, host_,
                   base::Bind(&ChromotingHostTest::ReleaseUiTaskRunner,
                              base::Unretained(this))));
  }

  void ReleaseUiTaskRunner() {
    it2me_host_user_interface_.reset();
    ui_task_runner_ = NULL;
    host_ = NULL;
  }

  void QuitMainMessageLoop() {
    PostQuitTask(&message_loop_);
  }

  // Expect the host to start.
  void ExpectHostStart() {
    EXPECT_CALL(*disconnect_window_, Hide());
    EXPECT_CALL(*continue_window_, Hide());
  }

  // Expect the host and session manager to start, and return the expectation
  // that the session manager has started.
  Expectation ExpectHostAndSessionManagerStart() {
    ExpectHostStart();
    EXPECT_CALL(host_status_observer_, OnStart(xmpp_login_));
    return EXPECT_CALL(*session_manager_, Init(_, host_.get()));
  }

  // Expect a client to connect.
  // Return an expectation that a session has started, and that the first
  // video packet has been sent to the client.
  // Do |action| when that happens.
  template <class A>
  Expectation ExpectClientConnected(int connection_index, A action) {
    const std::string& session_jid = get_session_jid(connection_index);
    MockVideoStub& video_stub = get_video_stub(connection_index);

    Expectation client_authenticated =
        EXPECT_CALL(host_status_observer_, OnClientAuthenticated(session_jid));
    EXPECT_CALL(host_status_observer_, OnClientConnected(session_jid))
        .After(client_authenticated);
    Expectation video_packet_sent =
        EXPECT_CALL(video_stub, ProcessVideoPacketPtr(_, _))
        .After(client_authenticated)
        .WillOnce(DoAll(
            action,
            RunDoneTask()))
        .RetiresOnSaturation();
    EXPECT_CALL(video_stub, ProcessVideoPacketPtr(_, _))
        .Times(AnyNumber())
        .After(video_packet_sent)
        .WillRepeatedly(RunDoneTask());
    return video_packet_sent;
  }

  // Return an expectation that a client will disconnect after a given
  // expectation. The given action will be done after the event executor is
  // notified that the session has finished.
  template <class A>
  Expectation ExpectClientDisconnected(int connection_index,
                                       bool expect_host_status_change,
                                       Expectation after,
                                       A action) {
    MockConnectionToClient* connection = get_connection(connection_index);

    Expectation client_disconnected =
        EXPECT_CALL(*connection, Disconnect())
            .After(after)
            .WillOnce(InvokeWithoutArgs(CreateFunctor(
                this, &ChromotingHostTest::NotifyClientSessionClosed,
                connection_index)))
            .RetiresOnSaturation();
    ExpectClientDisconnectEffects(connection_index,
                                  expect_host_status_change,
                                  after,
                                  action);
    return client_disconnected;
  }

  // Expect the side-effects of a client disconnection, after a given
  // expectation. The given action will be done after the event executor is
  // notifed that the session has finished.
  template <class A>
  void ExpectClientDisconnectEffects(int connection_index,
                                     bool expect_host_status_change,
                                     Expectation after,
                                     A action) {
    const std::string& session_jid = get_session_jid(connection_index);

    if (expect_host_status_change) {
      EXPECT_CALL(host_status_observer_, OnClientDisconnected(session_jid))
          .After(after)
          .WillOnce(action)
          .RetiresOnSaturation();
    }
  }

 protected:
  MessageLoop message_loop_;
  scoped_refptr<AutoThreadTaskRunner> ui_task_runner_;
  MockChromotingHostContext context_;
  MockConnectionToClientEventHandler handler_;
  MockSignalStrategy signal_strategy_;
  scoped_ptr<MockDesktopEnvironmentFactory> desktop_environment_factory_;
  scoped_ptr<MockIt2MeHostUserInterface> it2me_host_user_interface_;
  scoped_refptr<ChromotingHost> host_;
  MockHostStatusObserver host_status_observer_;
  protocol::MockSessionManager* session_manager_;
  std::string xmpp_login_;
  MockConnectionToClient* connection1_;
  scoped_ptr<MockConnectionToClient> owned_connection1_;
  ClientSession* client1_;
  std::string session_jid1_;
  MockSession* session1_;  // Owned by |connection_|.
  SessionConfig session_config1_;
  MockVideoStub video_stub1_;
  MockClientStub client_stub1_;
  MockHostStub host_stub1_;
  MockConnectionToClient* connection2_;
  scoped_ptr<MockConnectionToClient> owned_connection2_;
  ClientSession* client2_;
  std::string session_jid2_;
  MockSession* session2_;  // Owned by |connection2_|.
  SessionConfig session_config2_;
  MockVideoStub video_stub2_;
  MockClientStub client_stub2_;
  MockHostStub host_stub2_;
  scoped_ptr<MockSession> session_unowned1_;  // Not owned by a connection.
  SessionConfig session_unowned_config1_;
  std::string session_unowned_jid1_;
  scoped_ptr<MockSession> session_unowned2_;  // Not owned by a connection.
  SessionConfig session_unowned_config2_;
  std::string session_unowned_jid2_;
  protocol::Session::EventHandler* session_event_handler_;
  scoped_ptr<protocol::CandidateSessionConfig> empty_candidate_config_;
  scoped_ptr<protocol::CandidateSessionConfig> default_candidate_config_;

  // Owned by |host_|.
  MockDisconnectWindow* disconnect_window_;
  MockContinueWindow* continue_window_;
  MockLocalInputMonitor* local_input_monitor_;

  MockConnectionToClient*& get_connection(int connection_index) {
    return (connection_index == 0) ? connection1_ : connection2_;
  }

  ClientSession*& get_client(int connection_index) {
    return (connection_index == 0) ? client1_ : client2_;
  }

  const std::string& get_session_jid(int connection_index) {
    return (connection_index == 0) ? session_jid1_ : session_jid2_;
  }

  MockVideoStub& get_video_stub(int connection_index) {
    return (connection_index == 0) ? video_stub1_ : video_stub2_;
  }
};

TEST_F(ChromotingHostTest, StartAndShutdown) {
  Expectation start = ExpectHostAndSessionManagerStart();
  EXPECT_CALL(host_status_observer_, OnShutdown()).After(start);

  host_->Start(xmpp_login_);
  ShutdownHost();
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, Connect) {
  ExpectHostAndSessionManagerStart();

  // Shut down the host when the first video packet is received.
  Expectation video_packet_sent = ExpectClientConnected(
      0, InvokeWithoutArgs(this, &ChromotingHostTest::ShutdownHost));
  Expectation client_disconnected = ExpectClientDisconnected(
      0, true, video_packet_sent, InvokeWithoutArgs(base::DoNothing));
  EXPECT_CALL(host_status_observer_, OnShutdown()).After(client_disconnected);

  host_->Start(xmpp_login_);
  SimulateClientConnection(0, true, false);
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, RejectAuthenticatingClient) {
  Expectation start = ExpectHostAndSessionManagerStart();
  EXPECT_CALL(host_status_observer_, OnClientAuthenticated(session_jid1_))
      .WillOnce(InvokeWithoutArgs(
      this, &ChromotingHostTest::RejectAuthenticatingClient));
  ExpectClientDisconnected(
      0, true, start,
      InvokeWithoutArgs(this, &ChromotingHostTest::ShutdownHost));
  EXPECT_CALL(host_status_observer_, OnShutdown());

  host_->Start(xmpp_login_);
  SimulateClientConnection(0, true, true);
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, AuthenticationFailed) {
  ExpectHostAndSessionManagerStart();
  EXPECT_CALL(host_status_observer_, OnAccessDenied(session_jid1_))
      .WillOnce(InvokeWithoutArgs(this, &ChromotingHostTest::ShutdownHost));
  EXPECT_CALL(host_status_observer_, OnShutdown());

  host_->Start(xmpp_login_);
  SimulateClientConnection(0, false, false);
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, Reconnect) {
  ExpectHostAndSessionManagerStart();

  // When a video packet is received on the first connection, disconnect it,
  // then quit the message loop.
  Expectation video_packet_sent1 = ExpectClientConnected(0, DoAll(
      InvokeWithoutArgs(this, &ChromotingHostTest::DisconnectClient1),
      InvokeWithoutArgs(this, &ChromotingHostTest::QuitMainMessageLoop)));
  ExpectClientDisconnectEffects(
      0, true, video_packet_sent1, InvokeWithoutArgs(base::DoNothing));

  // When a video packet is received on the second connection, shut down the
  // host.
  Expectation video_packet_sent2 = ExpectClientConnected(
      1, InvokeWithoutArgs(this, &ChromotingHostTest::ShutdownHost));
  Expectation client_disconnected2 = ExpectClientDisconnected(
      1, true, video_packet_sent2, InvokeWithoutArgs(base::DoNothing));
  EXPECT_CALL(host_status_observer_, OnShutdown()).After(client_disconnected2);

  host_->Start(xmpp_login_);
  SimulateClientConnection(0, true, false);
  message_loop_.Run();
  SimulateClientConnection(1, true, false);
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, ConnectWhenAnotherClientIsConnected) {
  ExpectHostAndSessionManagerStart();

  // When a video packet is received, connect the second connection.
  // This should disconnect the first connection.
  Expectation video_packet_sent1 = ExpectClientConnected(
      0,
      InvokeWithoutArgs(
          CreateFunctor(
              this,
              &ChromotingHostTest::SimulateClientConnection, 1, true, false)));
  ExpectClientDisconnected(
      0, true, video_packet_sent1, InvokeWithoutArgs(base::DoNothing));
  Expectation video_packet_sent2 = ExpectClientConnected(
      1, InvokeWithoutArgs(this, &ChromotingHostTest::ShutdownHost));
  Expectation client_disconnected2 = ExpectClientDisconnected(
      1, true, video_packet_sent2, InvokeWithoutArgs(base::DoNothing));
  EXPECT_CALL(host_status_observer_, OnShutdown()).After(client_disconnected2);

  host_->Start(xmpp_login_);
  SimulateClientConnection(0, true, false);
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, IncomingSessionDeclined) {
  ExpectHostStart();
  protocol::SessionManager::IncomingSessionResponse response =
      protocol::SessionManager::ACCEPT;
  host_->OnIncomingSession(session1_, &response);
  EXPECT_EQ(protocol::SessionManager::DECLINE, response);

  ShutdownHost();
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, IncomingSessionIncompatible) {
  ExpectHostAndSessionManagerStart();
  EXPECT_CALL(*session_unowned1_, candidate_config()).WillOnce(Return(
      empty_candidate_config_.get()));
  EXPECT_CALL(host_status_observer_, OnShutdown());

  host_->Start(xmpp_login_);

  protocol::SessionManager::IncomingSessionResponse response =
      protocol::SessionManager::ACCEPT;
  host_->OnIncomingSession(session_unowned1_.get(), &response);
  EXPECT_EQ(protocol::SessionManager::INCOMPATIBLE, response);

  ShutdownHost();
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, IncomingSessionAccepted) {
  ExpectHostAndSessionManagerStart();
  EXPECT_CALL(*session_unowned1_, candidate_config()).WillOnce(Return(
      default_candidate_config_.get()));
  EXPECT_CALL(*session_unowned1_, set_config(_));
  EXPECT_CALL(*session_unowned1_, Close()).WillOnce(InvokeWithoutArgs(
      this, &ChromotingHostTest::NotifyConnectionClosed));
  EXPECT_CALL(host_status_observer_, OnAccessDenied(_));
  EXPECT_CALL(host_status_observer_, OnShutdown());

  host_->Start(xmpp_login_);

  protocol::SessionManager::IncomingSessionResponse response =
      protocol::SessionManager::DECLINE;
  host_->OnIncomingSession(session_unowned1_.release(), &response);
  EXPECT_EQ(protocol::SessionManager::ACCEPT, response);

  ShutdownHost();
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, IncomingSessionOverload) {
  ExpectHostAndSessionManagerStart();
  EXPECT_CALL(*session_unowned1_, candidate_config()).WillOnce(Return(
      default_candidate_config_.get()));
  EXPECT_CALL(*session_unowned1_, set_config(_));
  EXPECT_CALL(*session_unowned1_, Close()).WillOnce(InvokeWithoutArgs(
      this, &ChromotingHostTest::NotifyConnectionClosed));
  EXPECT_CALL(host_status_observer_, OnAccessDenied(_));
  EXPECT_CALL(host_status_observer_, OnShutdown());

  host_->Start(xmpp_login_);

  protocol::SessionManager::IncomingSessionResponse response =
      protocol::SessionManager::DECLINE;
  host_->OnIncomingSession(session_unowned1_.release(), &response);
  EXPECT_EQ(protocol::SessionManager::ACCEPT, response);

  host_->OnIncomingSession(session_unowned2_.get(), &response);
  EXPECT_EQ(protocol::SessionManager::OVERLOAD, response);

  ShutdownHost();
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, OnSessionRouteChange) {
  std::string channel_name("ChannelName");
  protocol::TransportRoute route;

  ExpectHostAndSessionManagerStart();
  Expectation video_packet_sent = ExpectClientConnected(
      0, InvokeWithoutArgs(CreateFunctor(
          this, &ChromotingHostTest::ChangeSessionRoute, channel_name, route)));
  Expectation route_change =
      EXPECT_CALL(host_status_observer_, OnClientRouteChange(
          session_jid1_, channel_name, _))
      .After(video_packet_sent)
      .WillOnce(InvokeWithoutArgs(this, &ChromotingHostTest::ShutdownHost));
  ExpectClientDisconnected(0, true, route_change,
                           InvokeWithoutArgs(base::DoNothing));
  EXPECT_CALL(host_status_observer_, OnShutdown());

  host_->Start(xmpp_login_);
  SimulateClientConnection(0, true, false);
  message_loop_.Run();
}

TEST_F(ChromotingHostTest, DisconnectAllClients) {
  ExpectHostAndSessionManagerStart();
  Expectation video_packet_sent = ExpectClientConnected(
      0, InvokeWithoutArgs(this, &ChromotingHostTest::DisconnectAllClients));
  ExpectClientDisconnected(0, true, video_packet_sent,
      InvokeWithoutArgs(this, &ChromotingHostTest::ShutdownHost));
  EXPECT_CALL(host_status_observer_, OnShutdown());

  host_->Start(xmpp_login_);
  SimulateClientConnection(0, true, false);
  message_loop_.Run();
}

}  // namespace remoting
