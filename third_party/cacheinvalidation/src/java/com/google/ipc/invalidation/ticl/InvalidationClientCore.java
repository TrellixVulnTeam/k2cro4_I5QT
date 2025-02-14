/*
 * Copyright 2011 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.ipc.invalidation.ticl;

import static com.google.ipc.invalidation.external.client.SystemResources.Scheduler.NO_DELAY;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.common.CommonInvalidationConstants2;
import com.google.ipc.invalidation.common.CommonProtoStrings2;
import com.google.ipc.invalidation.common.CommonProtos2;
import com.google.ipc.invalidation.common.DigestFunction;
import com.google.ipc.invalidation.common.ObjectIdDigestUtils;
import com.google.ipc.invalidation.common.TiclMessageValidator2;
import com.google.ipc.invalidation.external.client.InvalidationListener;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.external.client.SystemResources.NetworkChannel;
import com.google.ipc.invalidation.external.client.SystemResources.Scheduler;
import com.google.ipc.invalidation.external.client.SystemResources.Storage;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.Callback;
import com.google.ipc.invalidation.external.client.types.ErrorInfo;
import com.google.ipc.invalidation.external.client.types.Invalidation;
import com.google.ipc.invalidation.external.client.types.ObjectId;
import com.google.ipc.invalidation.external.client.types.SimplePair;
import com.google.ipc.invalidation.external.client.types.Status;
import com.google.ipc.invalidation.ticl.ProtocolHandler.ParsedMessage;
import com.google.ipc.invalidation.ticl.ProtocolHandler.ProtocolListener;
import com.google.ipc.invalidation.ticl.ProtocolHandler.ServerMessageHeader;
import com.google.ipc.invalidation.ticl.Statistics.ClientErrorType;
import com.google.ipc.invalidation.ticl.Statistics.IncomingOperationType;
import com.google.ipc.invalidation.ticl.Statistics.ReceivedMessageType;
import com.google.ipc.invalidation.util.Box;
import com.google.ipc.invalidation.util.Bytes;
import com.google.ipc.invalidation.util.InternalBase;
import com.google.ipc.invalidation.util.Marshallable;
import com.google.ipc.invalidation.util.Smearer;
import com.google.ipc.invalidation.util.TextBuilder;
import com.google.ipc.invalidation.util.TypedUtil;
import com.google.protobuf.ByteString;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protos.ipc.invalidation.Channel.NetworkEndpointId;
import com.google.protos.ipc.invalidation.Client.AckHandleP;
import com.google.protos.ipc.invalidation.Client.ExponentialBackoffState;
import com.google.protos.ipc.invalidation.Client.PersistentTiclState;
import com.google.protos.ipc.invalidation.Client.RunStateP;
import com.google.protos.ipc.invalidation.ClientProtocol.ApplicationClientIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientConfigP;
import com.google.protos.ipc.invalidation.ClientProtocol.ErrorMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.InfoRequestMessage.InfoType;
import com.google.protos.ipc.invalidation.ClientProtocol.InvalidationP;
import com.google.protos.ipc.invalidation.ClientProtocol.ObjectIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationStatus;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSubtree;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSummary;
import com.google.protos.ipc.invalidation.JavaClient.InvalidationClientState;
import com.google.protos.ipc.invalidation.JavaClient.ProtocolHandlerState;
import com.google.protos.ipc.invalidation.JavaClient.RecurringTaskState;
import com.google.protos.ipc.invalidation.JavaClient.RegistrationManagerStateP;
import com.google.protos.ipc.invalidation.JavaClient.StatisticsState;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.logging.Level;


/**
 * Core implementation of the  Invalidation Client Library (Ticl). Subclasses are required
 * to implement concurrency control for the Ticl.
 *
 */
public abstract class InvalidationClientCore extends InternalBase
    implements Marshallable<InvalidationClientState>, ProtocolListener,
        TestableInvalidationClient {

  /**
   * A subclass of {@link RecurringTask} with simplified constructors to provide common
   * parameters automatically (scheduler, logger, smearer).
   */
  private abstract class TiclRecurringTask extends RecurringTask {
    /**
     * Constructs a task with {@code initialDelayMs} and {@code timeoutDelayMs}. If
     * {@code useExponentialBackoff}, an exponential backoff generator with initial delay
     * {@code timeoutDelayMs} is used as well; if not, exponential backoff is not used.
     */
    TiclRecurringTask(String name, int initialDelayMs, int timeoutDelayMs,
        boolean useExponentialBackoff) {
      super(name, internalScheduler, logger, smearer,
          useExponentialBackoff ? createExpBackOffGenerator(timeoutDelayMs, null) : null,
          initialDelayMs, timeoutDelayMs);
    }

    /**
     * Constructs an instance from {@code marshalledState} that does not use exponential backoff.
     * @param name name of the recurring task
     */
    private TiclRecurringTask(String name, RecurringTaskState marshalledState) {
      super(name, internalScheduler, logger, smearer, null, marshalledState);
    }

    /**
     * Constructs an instance from {@code marshalledState} that uses exponential backoff with an
     * initial backoff of {@code timeoutMs}.
     *
     * @param name name of the recurring task
     */
    private TiclRecurringTask(String name, int timeoutMs, RecurringTaskState marshalledState) {
      super(name, internalScheduler, logger, smearer,
          createExpBackOffGenerator(timeoutMs, marshalledState.getBackoffState()), marshalledState);
    }
  }

  /** A task for acquiring tokens from the server. */
  private class AcquireTokenTask extends TiclRecurringTask {
    private static final String TASK_NAME = "AcquireToken";

    AcquireTokenTask() {
      super(TASK_NAME, NO_DELAY, config.getNetworkTimeoutDelayMs(), true);
    }

    AcquireTokenTask(RecurringTaskState marshalledState) {
      super(TASK_NAME, config.getNetworkTimeoutDelayMs(), marshalledState);
    }

    @Override
    public boolean runTask() {
      // If token is still not assigned (as expected), sends a request. Otherwise, ignore.
      if (clientToken == null) {
        // Allocate a nonce and send a message requesting a new token.
        setNonce(ByteString.copyFromUtf8(Long.toString(
            internalScheduler.getCurrentTimeMs())));
        protocolHandler.sendInitializeMessage(applicationClientId, nonce, batchingTask, TASK_NAME);
        return true;  // Reschedule to check state, retry if necessary after timeout.
      } else {
        return false;  // Don't reschedule.
      }
    }
  }

  /**
   * A task that schedules heartbeats when the registration summary at the client is not
   * in sync with the registration summary from the server.
   */
  private class RegSyncHeartbeatTask extends TiclRecurringTask {
    private static final String TASK_NAME = "RegSyncHeartbeat";

    RegSyncHeartbeatTask() {
      super(TASK_NAME, config.getNetworkTimeoutDelayMs(), config.getNetworkTimeoutDelayMs(), true);
    }

    RegSyncHeartbeatTask(RecurringTaskState marshalledState) {
      super(TASK_NAME, config.getNetworkTimeoutDelayMs(), marshalledState);
    }

    @Override
    public boolean runTask() {
      if (!registrationManager.isStateInSyncWithServer()) {
        // Simply send an info message to ensure syncing happens.
        logger.info("Registration state not in sync with server: %s", registrationManager);
        sendInfoMessageToServer(false, true /* request server summary */);
        return true;
      } else {
        logger.info("Not sending message since state is now in sync");
        return false;
      }
    }
  }

  /** A task that writes the token to persistent storage. */
  private class PersistentWriteTask extends TiclRecurringTask {
    /*
     * This class implements a "train" of events that attempt to reliably write state to
     * storage. The train continues until runTask encounters a termination condition, in
     * which the state currently in memory and the state currently in storage match.
     */

    private static final String TASK_NAME = "PersistentWrite";

    /** The last client token that was written to to persistent state successfully. */
    private final Box<ProtoWrapper<PersistentTiclState>> lastWrittenState =
        Box.of(ProtoWrapper.of(PersistentTiclState.getDefaultInstance()));

    PersistentWriteTask() {
      super(TASK_NAME, NO_DELAY, config.getWriteRetryDelayMs(), true);
    }

    PersistentWriteTask(RecurringTaskState marshalledState) {
      super(TASK_NAME, config.getWriteRetryDelayMs(), marshalledState);
    }

    @Override
    public boolean runTask() {
      if (clientToken == null) {
        // We cannot write without a token. We must do this check before creating the
        // PersistentTiclState because newPersistentTiclState cannot handle null tokens.
        return false;
      }

      // Compute the state that we will write if we decide to go ahead with the write.
      final ProtoWrapper<PersistentTiclState> state =
          ProtoWrapper.of(CommonProtos2.newPersistentTiclState(clientToken, lastMessageSendTimeMs));
      byte[] serializedState = PersistenceUtils.serializeState(state.getProto(), digestFn);

      // Decide whether or not to do the write. The decision varies depending on whether or
      // not the channel supports offline delivery. If we decide not to do the write, then
      // that means the in-memory and stored state match semantically, and the train stops.
      if (config.getChannelSupportsOfflineDelivery()) {
        // For offline delivery, we want the entire state to match, since we write the last
        // send time for every message.
        if (state.equals(lastWrittenState.get())) {
          return false;
        }
      } else {
        // If we do not support offline delivery, we avoid writing the state on each message, and
        // we avoid checking the last-sent time (we check only the client token).
        if (state.getProto().getClientToken().equals(
                lastWrittenState.get().getProto().getClientToken())) {
          return false;
        }
      }

      // We decided to do the write.
      storage.writeKey(CLIENT_TOKEN_KEY, serializedState, new Callback<Status>() {
        @Override
        public void accept(Status status) {
          logger.info("Write state completed: %s for %s", status, state.getProto());
          Preconditions.checkState(resources.getInternalScheduler().isRunningOnThread());
          if (status.isSuccess()) {
            // Set lastWrittenToken to be the token that was written (NOT clientToken - which
            // could have changed while the write was happening).
            lastWrittenState.set(state);
          } else {
            statistics.recordError(ClientErrorType.PERSISTENT_WRITE_FAILURE);
          }
        }
      });
      return true;  // Reschedule after timeout to make sure that write does happen.
    }
  }

  /** A task for sending heartbeats to the server. */
  private class HeartbeatTask extends TiclRecurringTask {
    private static final String TASK_NAME = "Heartbeat";

    /** Next time that the performance counters are sent to the server. */
    private long nextPerformanceSendTimeMs;

    HeartbeatTask() {
      super(TASK_NAME, config.getHeartbeatIntervalMs(), NO_DELAY, false);
    }

    HeartbeatTask(RecurringTaskState marshalledState) {
      super(TASK_NAME, marshalledState);
    }

    @Override
    public boolean runTask() {
      // Send info message. If needed, send performance counters and reset the next performance
      // counter send time.
      logger.info("Sending heartbeat to server: %s", this);
      boolean mustSendPerfCounters =
          nextPerformanceSendTimeMs > internalScheduler.getCurrentTimeMs();
      if (mustSendPerfCounters) {
        this.nextPerformanceSendTimeMs = internalScheduler.getCurrentTimeMs() +
            getSmearer().getSmearedDelay(config.getPerfCounterDelayMs());
      }
      sendInfoMessageToServer(mustSendPerfCounters, !registrationManager.isStateInSyncWithServer());
      return true;  // Reschedule.
    }
  }

  /** The task that is scheduled to send batched messages to the server (when needed). **/
  
  static class BatchingTask extends RecurringTask {
    /*
     * This class is static and extends RecurringTask directly so that it can be instantiated
     * independently in ProtocolHandlerTest.
     */
    private static final String TASK_NAME = "Batching";

    /** {@link ProtocolHandler} instance from which messages will be pulled. */
    private final ProtocolHandler protocolHandler;

    /** Creates a new instance with default state. */
    BatchingTask(ProtocolHandler protocolHandler, SystemResources resources, Smearer smearer,
        int batchingDelayMs) {
      super(TASK_NAME, resources.getInternalScheduler(), resources.getLogger(), smearer, null,
          batchingDelayMs, NO_DELAY);
      this.protocolHandler = protocolHandler;
    }

    /** Creates a new instance with state from {@code marshalledState}. */
    BatchingTask(ProtocolHandler protocolHandler, SystemResources resources, Smearer smearer,
        RecurringTaskState marshalledState) {
      super(TASK_NAME, resources.getInternalScheduler(), resources.getLogger(), smearer, null,
          marshalledState);
      this.protocolHandler = protocolHandler;
    }

    @Override
    public boolean runTask() {
      protocolHandler.sendMessageToServer();
      return false;  // Don't reschedule.
    }
  }

  /**
   * A (slightly strange) recurring task that executes exactly once for the first heartbeat
   * performed by a Ticl restarting from persistent state. The Android Ticl implementation
   * requires that all work to be scheduled in the future occur in the form of a recurring task,
   * hence this class.
   */
  private class InitialPersistentHeartbeatTask extends TiclRecurringTask {
    private static final String TASK_NAME = "InitialPersistentHeartbeat";

    InitialPersistentHeartbeatTask(int delayMs) {
      super(TASK_NAME, delayMs, NO_DELAY, false);
    }

    @Override
    public boolean runTask() {
      sendInfoMessageToServer(false, true);
      return false; // Don't reschedule.
    }
  }

  //
  // End of nested classes.
  //

  /** The single key used to write all the Ticl state. */
  
  public static final String CLIENT_TOKEN_KEY = "ClientToken";

  /** Resources for the Ticl. */
  private final SystemResources resources;

  /**
   * Reference into the resources object for cleaner code. All Ticl code must execute on this
   * scheduler.
   */
  private final Scheduler internalScheduler;

  /** Logger reference into the resources object for cleaner code. */
  private final Logger logger;

  /** Storage for the Ticl peristent state. */
  Storage storage;

  /** Application callback interface. */
  final InvalidationListener listener;

  /** Configuration for this instance. */
  private ClientConfigP config;

  /** Application identifier for this client. */
  private final ApplicationClientIdP applicationClientId;

  /** Object maintaining the registration state for this client. */
  private final RegistrationManager registrationManager;

  /** Object handling low-level wire format interactions. */
  private final ProtocolHandler protocolHandler;

  /** Used to validate messages */
  private final TiclMessageValidator2 msgValidator;

  /** The function for computing the registration and persistence state digests. */
  private final DigestFunction digestFn = new ObjectIdDigestUtils.Sha1DigestFunction();

  /** The state of the Ticl whether it has started or not. */
  private final RunState ticlState;

  /** Statistics objects to track number of sent messages, etc. */
  final Statistics statistics;

  /** A smearer to make sure that delays are randomized a little bit. */
  private final Smearer smearer;

  /** Current client token known from the server. */
  private ByteString clientToken = null;

  // After the client starts, exactly one of nonce and clientToken is non-null.

  /** If not {@code null}, nonce for pending identifier request. */
  private ByteString nonce = null;

  /** Whether we should send registrations to the server or not. */
  // TODO: Make the server summary in the registration manager nullable
  // and replace this variable with a test for whether it's null or not.
  private boolean shouldSendRegistrations;

  /** Whether the network is online.  Assume so when we start. */
  private boolean isOnline = true;

  /** A random number generator. */
  private final Random random;

  /** Last time a message was sent to the server. */
  private long lastMessageSendTimeMs = 0;

  /** A task for acquiring the token (if the client has no token). */
  private AcquireTokenTask acquireTokenTask;

  /** Task for checking if reg summary is out of sync and then sending a heartbeat to the server. */
  private RegSyncHeartbeatTask regSyncHeartbeatTask;

  /** Task for writing the state blob to persistent storage. */
  private PersistentWriteTask persistentWriteTask;

  /** A task for periodic heartbeats. */
  private HeartbeatTask heartbeatTask;

  /** Task to send all batched messages to the server. */
  private BatchingTask batchingTask;

  /** Task to do the first heartbeat after a persistent restart. */
  private InitialPersistentHeartbeatTask initialPersistentHeartbeatTask;

  /**
   * Constructs a client.
   *
   * @param resources resources to use during execution
   * @param random a random number generator
   * @param clientType client type code
   * @param clientName application identifier for the client
   * @param config configuration for the client
   * @param applicationName name of the application using the library (for debugging/monitoring)
   * @param regManagerState marshalled registration manager state, if any
   * @param protocolHandlerState marshalled protocol handler state, if any
   * @param listener application callback
   */
  private InvalidationClientCore(final SystemResources resources, Random random, int clientType,
      final byte[] clientName, ClientConfigP config, String applicationName,
      RunStateP ticlRunState,
      RegistrationManagerStateP regManagerState,
      ProtocolHandlerState protocolHandlerState,
      StatisticsState statisticsState,
      InvalidationListener listener) {
    this.resources = Preconditions.checkNotNull(resources);
    this.random = random;
    this.logger = Preconditions.checkNotNull(resources.getLogger());
    this.internalScheduler = resources.getInternalScheduler();
    this.storage = resources.getStorage();
    this.config = config;
    this.ticlState = (ticlRunState == null) ? new RunState() : new RunState(ticlRunState);
    this.smearer = new Smearer(random, this.config.getSmearPercent());
    this.applicationClientId =
        CommonProtos2.newApplicationClientIdP(clientType, ByteString.copyFrom(clientName));
    this.listener = listener;
    this.msgValidator = new TiclMessageValidator2(resources.getLogger());
    this.statistics = (statisticsState != null) ?
        Statistics.deserializeStatistics(resources.getLogger(), statisticsState.getCounterList()) :
        new Statistics();
    this.registrationManager = new RegistrationManager(logger, statistics, digestFn,
        regManagerState);
    this.protocolHandler = new ProtocolHandler(config.getProtocolHandlerConfig(), resources,
        smearer, statistics, applicationName, this, msgValidator, protocolHandlerState);
  }

  /**
   * Constructs a client with default state.
   *
   * @param resources resources to use during execution
   * @param random a random number generator
   * @param clientType client type code
   * @param clientName application identifier for the client
   * @param config configuration for the client
   * @param applicationName name of the application using the library (for debugging/monitoring)
   * @param listener application callback
   */
  public InvalidationClientCore(final SystemResources resources, Random random, int clientType,
      final byte[] clientName, ClientConfigP config, String applicationName,
      InvalidationListener listener) {
    this(resources, random, clientType, clientName, config, applicationName, null, null, null, null,
        listener);
    createSchedulingTasks(null);
    registerWithNetwork(resources);
    logger.info("Created client: %s", this);
  }

  /**
   * Constructs a client with state initialized from {@code marshalledState}.
   *
   * @param resources resources to use during execution
   * @param random a random number generator
   * @param clientType client type code
   * @param clientName application identifier for the client
   * @param config configuration for the client
   * @param applicationName name of the application using the library (for debugging/monitoring)
   * @param listener application callback
   */
  public InvalidationClientCore(final SystemResources resources, Random random, int clientType,
      final byte[] clientName, ClientConfigP config, String applicationName,
      InvalidationClientState marshalledState, InvalidationListener listener) {
    this(resources, random, clientType, clientName, config, applicationName,
        marshalledState.getRunState(), marshalledState.getRegistrationManagerState(),
        marshalledState.getProtocolHandlerState(), marshalledState.getStatisticsState(), listener);
    // Unmarshall.
    if (marshalledState.hasClientToken()) {
      clientToken = marshalledState.getClientToken();
    }
    if (marshalledState.hasNonce()) {
      nonce = marshalledState.getNonce();
    }
    this.shouldSendRegistrations = marshalledState.getShouldSendRegistrations();
    this.lastMessageSendTimeMs = marshalledState.getLastMessageSendTimeMs();
    this.isOnline = marshalledState.getIsOnline();
    createSchedulingTasks(marshalledState);

    // We register with the network after unmarshalling our isOnline value. This is because when
    // we register with the network, it may give us a new value for isOnline. If we unmarshalled
    // after registering, then we would clobber the new value with the old marshalled value, which
    // is wrong.
    registerWithNetwork(resources);
    logger.info("Created client: %s", this);
  }

  /**
   * Registers handlers for received messages and network status changes with the network of
   * {@code resources}.
   */
  private void registerWithNetwork(final SystemResources resources) {
    resources.getNetwork().setListener(new NetworkChannel.NetworkListener() {
      @Override
      public void onMessageReceived(byte[] incomingMessage) {
        final String name = "handleIncomingMessage";
        InvalidationClientCore.this.handleIncomingMessage(incomingMessage);
      }
      @Override
      public void onOnlineStatusChange(boolean isOnline) {
        InvalidationClientCore.this.handleNetworkStatusChange(isOnline);
      }
      @Override
      public void onAddressChange() {
        // Send a message to the server. The header will include the new network address.
        Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
        sendInfoMessageToServer(false, false);
      }
    });
  }

  /** Returns a default config builder for the client. */
  public static ClientConfigP.Builder createConfig() {
    return ClientConfigP.newBuilder()
        .setVersion(CommonProtos2.newVersion(CommonInvalidationConstants2.CONFIG_MAJOR_VERSION,
            CommonInvalidationConstants2.CONFIG_MINOR_VERSION))
        .setProtocolHandlerConfig(ProtocolHandler.createConfig());
  }

  /** Returns a configuration builder with parameters set for unit tests. */
  public static ClientConfigP.Builder createConfigForTest() {
    return ClientConfigP.newBuilder()
        .setVersion(CommonProtos2.newVersion(CommonInvalidationConstants2.CONFIG_MAJOR_VERSION,
            CommonInvalidationConstants2.CONFIG_MINOR_VERSION))
        .setProtocolHandlerConfig(ProtocolHandler.createConfigForTest())
        .setNetworkTimeoutDelayMs(2 * 1000)
        .setHeartbeatIntervalMs(5 * 1000)
        .setWriteRetryDelayMs(500);
  }

  /**
   * Creates the tasks used by the Ticl for token acquisition, heartbeats, persistent writes and
   * registration sync.
   *
   * @param marshalledState saved state of recurring tasks
   */
  private void createSchedulingTasks(InvalidationClientState marshalledState) {
    if (marshalledState == null) {
      this.acquireTokenTask = new AcquireTokenTask();
      this.heartbeatTask = new HeartbeatTask();
      this.regSyncHeartbeatTask = new RegSyncHeartbeatTask();
      this.persistentWriteTask = new PersistentWriteTask();
      this.batchingTask = new BatchingTask(protocolHandler, resources, smearer,
          config.getProtocolHandlerConfig().getBatchingDelayMs());
    } else {
      this.acquireTokenTask = new AcquireTokenTask(marshalledState.getAcquireTokenTaskState());
      this.heartbeatTask = new HeartbeatTask(marshalledState.getHeartbeatTaskState());
      this.regSyncHeartbeatTask =
          new RegSyncHeartbeatTask(marshalledState.getRegSyncHeartbeatTaskState());
      this.persistentWriteTask =
          new PersistentWriteTask(marshalledState.getPersistentWriteTaskState());
      this.batchingTask = new BatchingTask(protocolHandler, resources, smearer,
          marshalledState.getBatchingTaskState());
      if (marshalledState.hasLastWrittenState()) {
        persistentWriteTask.lastWrittenState.set(
            ProtoWrapper.of(marshalledState.getLastWrittenState()));
      }
    }
    // The handling of new InitialPersistentHeartbeatTask is a little strange. We create one when
    // the Ticl is first created so that it can be called by the scheduler if it had been scheduled
    // in the past. Otherwise, when we are ready to schedule one ourselves, we create a new instance
    // with the proper delay, then schedule it. We have to do this because we don't know what delay
    // to use here, since we don't compute it until start().
    this.initialPersistentHeartbeatTask = new InitialPersistentHeartbeatTask(0);
  }

  /** Returns the configuration used by the client. */
  protected ClientConfigP getConfig() {
    return config;
  }

  // Methods for TestableInvalidationClient.

  @Override
  
  public ClientConfigP getConfigForTest() {
    return getConfig();
  }

  @Override
  
  public byte[] getApplicationClientIdForTest() {
    return applicationClientId.toByteArray();
  }

  /** Returns the application client id of this client. */
  protected ApplicationClientIdP getApplicationClientIdP() {
    return applicationClientId;
  }

  @Override
  
  public InvalidationListener getInvalidationListenerForTest() {
    return (listener instanceof CheckingInvalidationListener) ?
        ((CheckingInvalidationListener) this.listener).getDelegate() : this.listener;
  }

  
  public SystemResources getResourcesForTest() {
    return resources;
  }

  public SystemResources getResources() {
    return resources;
  }

  @Override
  
  public Statistics getStatisticsForTest() {
    return statistics;
  }

  Statistics getStatistics() {
    return statistics;
  }

  @Override
  
  public DigestFunction getDigestFunctionForTest() {
    return this.digestFn;
  }

  @Override
  
  public long getNextMessageSendTimeMsForTest() {
    Preconditions.checkState(resources.getInternalScheduler().isRunningOnThread());
    return protocolHandler.getNextMessageSendTimeMsForTest();
  }

  @Override
  
  public RegistrationManagerState getRegistrationManagerStateCopyForTest() {
    Preconditions.checkState(resources.getInternalScheduler().isRunningOnThread());
    return registrationManager.getRegistrationManagerStateCopyForTest(
        new ObjectIdDigestUtils.Sha1DigestFunction());
  }

  @Override
  
  public void changeNetworkTimeoutDelayForTest(int networkTimeoutDelayMs) {
    config = ClientConfigP.newBuilder(config).setNetworkTimeoutDelayMs(networkTimeoutDelayMs)
        .build();
    createSchedulingTasks(null);
  }

  @Override
  
  public void changeHeartbeatDelayForTest(int heartbeatDelayMs) {
    config = ClientConfigP.newBuilder(config).setHeartbeatIntervalMs(heartbeatDelayMs).build();
    createSchedulingTasks(null);
  }

  @Override
  
  public void setDigestStoreForTest(DigestStore<ObjectIdP> digestStore) {
    Preconditions.checkState(!resources.isStarted());
    registrationManager.setDigestStoreForTest(digestStore);
  }

  @Override
  
  public ByteString getClientTokenForTest() {
    return getClientToken();
  }

  @Override
  
  public String getClientTokenKeyForTest() {
    return CLIENT_TOKEN_KEY;
  }

  @Override
  public boolean isStartedForTest() {
    return isStarted();
  }

  /**
   * Returns whether the Ticl is started, i.e., whether it at some point had a session with the
   * data center after being constructed.
   */
  protected boolean isStarted() {
    return ticlState.isStarted();
  }

  @Override
  public void stopResources() {
    resources.stop();
  }

  @Override
  public long getResourcesTimeMs() {
    return resources.getInternalScheduler().getCurrentTimeMs();
  }

  @Override
  public Scheduler getInternalSchedulerForTest() {
    return resources.getInternalScheduler();
  }

  @Override
  public Storage getStorage() {
    return storage;
  }

  @Override
  public NetworkEndpointId getNetworkIdForTest() {
    NetworkChannel network = resources.getNetwork();
    if (!(network instanceof TestableNetworkChannel)) {
      throw new UnsupportedOperationException(
          "getNetworkIdForTest requires a TestableNetworkChannel, not: " + network.getClass());
    }
    return ((TestableNetworkChannel) network).getNetworkIdForTest();
  }

  // End of methods for TestableInvalidationClient

  @Override // InvalidationClient
  public void start() {
    Preconditions.checkState(resources.isStarted(), "Resources must be started before starting " +
        "the Ticl");
    Preconditions.checkState(!ticlState.isStarted(), "Already started");

    // Initialize the nonce so that we can maintain the invariant that exactly one of
    // "nonce" and "clientToken" is non-null.
    setNonce(ByteString.copyFromUtf8(Long.toString(internalScheduler.getCurrentTimeMs())));

    logger.info("Starting with Java config: %s", config);
    // Read the state blob and then schedule startInternal once the value is there.
    scheduleStartAfterReadingStateBlob();
  }

  /**
   * Implementation of {@link #start} on the internal thread with the persistent
   * {@code serializedState} if any. Starts the TICL protocol and makes the TICL ready to receive
   * registrations, invalidations, etc.
   */
  private void startInternal(byte[] serializedState) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    // Initialize the session manager using the persisted client token.
    PersistentTiclState persistentState =
        (serializedState == null) ? null : PersistenceUtils.deserializeState(logger,
            serializedState, digestFn);

    if ((serializedState != null) && (persistentState == null)) {
      // In this case, we'll proceed as if we had no persistent state -- i.e., obtain a new client
      // id from the server.
      statistics.recordError(ClientErrorType.PERSISTENT_DESERIALIZATION_FAILURE);
      logger.severe("Failed deserializing persistent state: %s",
          CommonProtoStrings2.toLazyCompactString(serializedState));
    }
    if (persistentState != null) {
      // If we have persistent state, use the previously-stored token and send a heartbeat to
      // let the server know that we've restarted, since we may have been marked offline.

      // In the common case, the server will already have all of our
      // registrations, but we won't know for sure until we've gotten its summary.
      // We'll ask the application for all of its registrations, but to avoid
      // making the registrar redo the work of performing registrations that
      // probably already exist, we'll suppress sending them to the registrar.
      logger.info("Restarting from persistent state: %s",
          CommonProtoStrings2.toLazyCompactString(persistentState.getClientToken()));
      setNonce(null);
      setClientToken(persistentState.getClientToken());
      shouldSendRegistrations = false;

      // Schedule an info message for the near future.
      int initialHeartbeatDelayMs = computeInitialPersistentHeartbeatDelayMs(
          config, resources, persistentState.getLastMessageSendTimeMs());
      initialPersistentHeartbeatTask = new InitialPersistentHeartbeatTask(initialHeartbeatDelayMs);
      initialPersistentHeartbeatTask.ensureScheduled("");

      // We need to ensure that heartbeats are sent, regardless of whether we start fresh or from
      // persistent state. The line below ensures that they are scheduled in the persistent startup
      // case. For the other case, the task is scheduled when we acquire a token.
      heartbeatTask.ensureScheduled("Startup-after-persistence");
    } else {
      // If we had no persistent state or couldn't deserialize the state that we had, start fresh.
      // Request a new client identifier.

      // The server can't possibly have our registrations, so whatever we get
      // from the application we should send to the registrar.
      logger.info("Starting with no previous state");
      shouldSendRegistrations = true;
      acquireToken("Startup");
    }

    // listener.ready() is called when ticl has acquired a new token.
  }

  /**
   * Returns the delay for the initial heartbeat, given that the last message to the server was
   * sent at {@code lastSendTimeMs}.
   * @param config configuration object used by the client
   * @param resources resources used by the client
   */
  
  static int computeInitialPersistentHeartbeatDelayMs(ClientConfigP config,
      SystemResources resources, long lastSendTimeMs) {
      // There are five cases:
      // 1. Channel does not support offline delivery. We delay a little bit to allow the
      // application to reissue its registrations locally and avoid triggering registration
      // sync with the data center due to a hash mismatch. This is the "minimum delay," and we
      // never use a delay less than it.
      //
      // All other cases are for channels supporting offline delivery.
      //
      // 2. Last send time is in the future (something weird happened). Use the minimum delay.
      // 3. We have been asleep for more than one heartbeat interval. Use the minimum delay.
      // 4. We have been asleep for less than one heartbeat interval.
      //    (a). The time remaining to the end of the interval is less than the minimum delay.
      //         Use the minimum delay.
      //    (b). The time remaining to the end of the interval is more than the minimum delay.
      //         Use the remaining delay.
    final long nowMs = resources.getInternalScheduler().getCurrentTimeMs();
    final int initialHeartbeatDelayMs;
    if (!config.getChannelSupportsOfflineDelivery()) {
      // Case 1.
      initialHeartbeatDelayMs = config.getInitialPersistentHeartbeatDelayMs();
    } else {
      // Offline delivery cases (2, 3, 4).
      // The default of the last send time is zero, so even if it wasn't written in the persistent
      // state, this logic is still correct.
      if ((lastSendTimeMs > nowMs) ||                                       // Case 2.
          ((lastSendTimeMs + config.getHeartbeatIntervalMs()) < nowMs)) {   // Case 3.
        // Either something strange happened and the last send time is in the future, or we
        // have been asleep for more than one heartbeat interval. Send immediately.
        initialHeartbeatDelayMs = config.getInitialPersistentHeartbeatDelayMs();
      } else {
        // Case 4.
        // We have been asleep for less than one heartbeat interval. Send after it expires,
        // but ensure we let the initial heartbeat interval elapse.
        final long timeSinceLastMessageMs = nowMs - lastSendTimeMs;
        final int remainingHeartbeatIntervalMs =
             (int) (config.getHeartbeatIntervalMs() - timeSinceLastMessageMs);
        initialHeartbeatDelayMs = Math.max(remainingHeartbeatIntervalMs,
            config.getInitialPersistentHeartbeatDelayMs());
      }
    }
    resources.getLogger().info("Computed heartbeat delay %s from: offline-delivery = %s, "
        + "initial-persistent-delay = %s, heartbeat-interval = %s, nowMs = %s",
        initialHeartbeatDelayMs, config.getChannelSupportsOfflineDelivery(),
        config.getInitialPersistentHeartbeatDelayMs(), config.getHeartbeatIntervalMs(),
        nowMs);
    return initialHeartbeatDelayMs;
  }

  @Override  // InvalidationClient
  public void stop() {
    logger.warning("Ticl being stopped: %s", InvalidationClientCore.this);
    if (ticlState.isStarted()) {  // RunState is thread-safe.
      ticlState.stop();
    }
  }

  @Override  // InvalidationClient
  public void register(ObjectId objectId) {
    List<ObjectId> objectIds = new ArrayList<ObjectId>();
    objectIds.add(objectId);
    performRegisterOperations(objectIds, RegistrationP.OpType.REGISTER);
  }

  @Override  // InvalidationClient
  public void unregister(ObjectId objectId) {
    List<ObjectId> objectIds = new ArrayList<ObjectId>();
    objectIds.add(objectId);
    performRegisterOperations(objectIds, RegistrationP.OpType.UNREGISTER);
  }

  @Override  // InvalidationClient
  public void register(Collection<ObjectId> objectIds) {
    performRegisterOperations(objectIds, RegistrationP.OpType.REGISTER);
  }

  @Override  // InvalidationClient
  public void unregister(Collection<ObjectId> objectIds) {
    performRegisterOperations(objectIds, RegistrationP.OpType.UNREGISTER);
  }

  /**
   * Implementation of (un)registration.
   *
   * @param objectIds object ids on which to operate
   * @param regOpType whether to register or unregister
   */
  private void performRegisterOperations(final Collection<ObjectId> objectIds,
      final RegistrationP.OpType regOpType) {
    Preconditions.checkState(!objectIds.isEmpty(), "Must specify some object id");
    Preconditions.checkNotNull(regOpType, "Must specify (un)registration");
    Preconditions.checkState(internalScheduler.isRunningOnThread(),
        "Not running on internal thread");

    Preconditions.checkState(ticlState.isStarted() || ticlState.isStopped(),
      "Cannot call %s for object %s when the Ticl has not been started (currently in state %s)." +
      "If start has been called, caller must wait for InvalidationListener.ready",
      ticlState, regOpType, objectIds);
    if (ticlState.isStopped()) {
      // The Ticl has been stopped. This might be some old registration op coming in. Just ignore
      // instead of crashing.
      logger.warning("Ticl stopped: register (%s) of %s ignored.", regOpType, objectIds);
      return;
    }

    List<ObjectIdP> objectIdProtos = new ArrayList<ObjectIdP>(objectIds.size());
    for (ObjectId objectId : objectIds) {
      Preconditions.checkNotNull(objectId, "Must specify object id");
      ObjectIdP objectIdProto = ProtoConverter.convertToObjectIdProto(objectId);
      IncomingOperationType opType = (regOpType == RegistrationP.OpType.REGISTER) ?
          IncomingOperationType.REGISTRATION : IncomingOperationType.UNREGISTRATION;
      statistics.recordIncomingOperation(opType);
      logger.info("Register %s, %s", CommonProtoStrings2.toLazyCompactString(objectIdProto),
          regOpType);
      objectIdProtos.add(objectIdProto);
      // Inform immediately of success so that the application is informed even if the reply
      // message from the server is lost. When we get a real ack from the server, we do
      // not need to inform the application.
      InvalidationListener.RegistrationState regState = convertOpTypeToRegState(regOpType);
      listener.informRegistrationStatus(InvalidationClientCore.this, objectId, regState);
    }

    // Update the registration manager state, then have the protocol client send a message.
    // performOperations returns only those elements of objectIdProtos that caused a state
    // change (i.e., elements not present if regOpType == REGISTER or elements that were present
    // if regOpType == UNREGISTER).
    Collection<ObjectIdP> objectProtosToSend = registrationManager.performOperations(
        objectIdProtos, regOpType);

    // Check whether we should suppress sending registrations because we don't
    // yet know the server's summary.
    if (shouldSendRegistrations && (!objectProtosToSend.isEmpty())) {
      protocolHandler.sendRegistrations(objectProtosToSend, regOpType, batchingTask);
    }
    InvalidationClientCore.this.regSyncHeartbeatTask.ensureScheduled("performRegister");
  }

  @Override  // InvalidationClient
  public void acknowledge(final AckHandle acknowledgeHandle) {
    Preconditions.checkNotNull(acknowledgeHandle);
    Preconditions.checkState(internalScheduler.isRunningOnThread(),
        "Not running on internal thread");

    // 1. Parse the ack handle first.
    AckHandleP ackHandle;
    try {
      ackHandle = AckHandleP.parseFrom(acknowledgeHandle.getHandleData());
    } catch (InvalidProtocolBufferException exception) {
      logger.warning("Bad ack handle : %s",
        CommonProtoStrings2.toLazyCompactString(acknowledgeHandle.getHandleData()));
      statistics.recordError(ClientErrorType.ACKNOWLEDGE_HANDLE_FAILURE);
      return;
    }

    // 2. Validate ack handle - it should have a valid invalidation.
    if (!ackHandle.hasInvalidation() ||
        !msgValidator.isValid(ackHandle.getInvalidation())) {
      logger.warning("Incorrect ack handle data: %s", acknowledgeHandle);
      statistics.recordError(ClientErrorType.ACKNOWLEDGE_HANDLE_FAILURE);
      return;
    }

    // Currently, only invalidations have non-trivial ack handle.
    InvalidationP invalidation = ackHandle.getInvalidation();
    if (invalidation.hasPayload()) {
      // Don't send the payload back.
      invalidation = invalidation.toBuilder().clearPayload().build();
    }
    statistics.recordIncomingOperation(IncomingOperationType.ACKNOWLEDGE);
    protocolHandler.sendInvalidationAck(invalidation, batchingTask);
  }

  //
  // Protocol listener methods
  //

  @Override
  public ByteString getClientToken() {
    Preconditions.checkState((clientToken == null) || (nonce == null));
    return clientToken;
  }

  @Override
  public void handleNetworkStatusChange(final boolean isOnline) {
    // If we're back online and haven't sent a message to the server in a while, send a heartbeat to
    // make sure the server knows we're online.
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    boolean wasOnline = this.isOnline;
    this.isOnline = isOnline;
    if (isOnline && !wasOnline && (internalScheduler.getCurrentTimeMs() >
        lastMessageSendTimeMs + config.getOfflineHeartbeatThresholdMs())) {
      logger.log(
          Level.INFO, "Sending heartbeat after reconnection, previous send was %s ms ago",
          internalScheduler.getCurrentTimeMs() - lastMessageSendTimeMs);
      sendInfoMessageToServer(false, !registrationManager.isStateInSyncWithServer());
    }
  }

  @Override
  public void handleMessageSent() {
    // The ProtocolHandler just sent a message to the server. If the channel supports offline
    // delivery (see the comment in the ClientConfigP), store this time to stable storage. This
    // only needs to be a best-effort write; if it fails, then we will "forget" that we sent the
    // message and heartbeat needlessly when next restarted. That is a performance/battery bug,
    // not a correctness bug.
    lastMessageSendTimeMs = getResourcesTimeMs();
    if (config.getChannelSupportsOfflineDelivery()) {
      // Write whether or not we have a token. The persistent write task is a no-op if there is
      // no token. We only write if the channel supports offline delivery. We could do the write
      // regardless, and may want to do so in the future, since it might simplify some of the
      // Ticl implementation.
      persistentWriteTask.ensureScheduled("sent-message");
    }
  }

  @Override
  public RegistrationSummary getRegistrationSummary() {
    return registrationManager.getRegistrationSummary();
  }

  //
  // Private methods and toString.
  //

  /**
   * Handles an {@code incomingMessage} from the data center. If it is valid and addressed to
   * this client, dispatches to methods to handle sub-parts of the message; if not, drops the
   * message.
   */
  void handleIncomingMessage(byte[] incomingMessage) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    statistics.recordReceivedMessage(ReceivedMessageType.TOTAL);
    ParsedMessage parsedMessage = protocolHandler.handleIncomingMessage(incomingMessage);
    if (parsedMessage == null) {
      // Invalid message.
      return;
    }

    // Ensure we have either a matching token or a matching nonce.
    if (!validateToken(parsedMessage)) {
      return;
    }

    // Handle a token-control message, if present.
    if (parsedMessage.tokenControlMessage != null) {
      statistics.recordReceivedMessage(ReceivedMessageType.TOKEN_CONTROL);
      handleTokenChanged(parsedMessage.header.token,
          parsedMessage.tokenControlMessage.hasNewToken() ?
              parsedMessage.tokenControlMessage.getNewToken() : null);
    }

    // We might have lost our token or failed to acquire one. Ensure that we do not proceed in
    // either case.
    if (clientToken == null) {
      return;
    }

    // First, handle the message header.
    handleIncomingHeader(parsedMessage.header);

    // Then, handle any work remaining in the message.
    if (parsedMessage.invalidationMessage != null) {
      statistics.recordReceivedMessage(ReceivedMessageType.INVALIDATION);
      handleInvalidations(parsedMessage.invalidationMessage.getInvalidationList());
    }
    if (parsedMessage.registrationStatusMessage != null) {
      statistics.recordReceivedMessage(ReceivedMessageType.REGISTRATION_STATUS);
      handleRegistrationStatus(parsedMessage.registrationStatusMessage.getRegistrationStatusList());
    }
    if (parsedMessage.registrationSyncRequestMessage != null) {
      statistics.recordReceivedMessage(ReceivedMessageType.REGISTRATION_SYNC_REQUEST);
      handleRegistrationSyncRequest();
    }
    if (parsedMessage.infoRequestMessage != null) {
      statistics.recordReceivedMessage(ReceivedMessageType.INFO_REQUEST);
      handleInfoMessage(parsedMessage.infoRequestMessage.getInfoTypeList());
    }
    if (parsedMessage.errorMessage != null) {
      statistics.recordReceivedMessage(ReceivedMessageType.ERROR);
      handleErrorMessage(parsedMessage.header, parsedMessage.errorMessage.getCode(),
          parsedMessage.errorMessage.getDescription());
    }
  }

  /**
   * Handles a token-control message.
   * @param headerToken token in the server message
   * @param newToken the new token provided, or {@code null} if this is a destroy message.
   */
  private void handleTokenChanged(ByteString headerToken, final ByteString newToken) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    // If we have a new token, then we know that the nonce matched. Accept the token and clear
    // the nonce.
    if (newToken != null) {
      Preconditions.checkArgument(TypedUtil.<ByteString>equals(headerToken, nonce),
          "Provided with new token and mismatched nonce: header = %s, nonce = %s",
          headerToken, nonce);
      setNonce(null);
      logger.info("New token being assigned at client: %s, Old = %s",
          CommonProtoStrings2.toLazyCompactString(newToken),
          CommonProtoStrings2.toLazyCompactString(clientToken));

      // Start the regular heartbeats now.
      heartbeatTask.ensureScheduled("Heartbeat-after-new-token");
      setNonce(null);
      setClientToken(newToken);
      persistentWriteTask.ensureScheduled("Write-after-new-token");
    } else {
      logger.info("Destroying existing token: %s",
          CommonProtoStrings2.toLazyCompactString(clientToken));
      acquireToken("Destroy");
    }
  }

  /** Handles a server {@code header}. */
  private void handleIncomingHeader(ServerMessageHeader header) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    Preconditions.checkState(nonce == null,
        "Cannot process server header with non-null nonce (have %s): %s", nonce, header);
    if (header.registrationSummary != null) {
      // We've received a summary from the server, so if we were suppressing
      // registrations, we should now allow them to go to the registrar.
      shouldSendRegistrations = true;
      registrationManager.informServerRegistrationSummary(header.registrationSummary);
    }
  }

  /** Handles incoming {@code invalidations}. */
  private void handleInvalidations(Collection<InvalidationP> invalidations) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    for (InvalidationP invalidation : invalidations) {
      AckHandle ackHandle = AckHandle.newInstance(
          CommonProtos2.newAckHandleP(invalidation).toByteArray());
      if (CommonProtos2.isAllObjectId(invalidation.getObjectId())) {
        logger.info("Issuing invalidate all");
        listener.invalidateAll(InvalidationClientCore.this, ackHandle);
      } else {
        // Regular object. Could be unknown version or not.
        Invalidation inv = ProtoConverter.convertFromInvalidationProto(invalidation);
        logger.info("Issuing invalidate (known-version = %s): %s", invalidation.getIsKnownVersion(),
            inv);
        if (invalidation.getIsKnownVersion()) {
          listener.invalidate(InvalidationClientCore.this, inv, ackHandle);
        } else {
          // Unknown version
          listener.invalidateUnknownVersion(InvalidationClientCore.this, inv.getObjectId(),
              ackHandle);
        }
      }
    }
  }

  /** Handles incoming registration statuses. */
  private void handleRegistrationStatus(List<RegistrationStatus> regStatusList) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    List<Boolean> localProcessingStatuses =
        registrationManager.handleRegistrationStatus(regStatusList);
    Preconditions.checkState(localProcessingStatuses.size() == regStatusList.size(),
        "Not all registration statuses were processed");

    // Inform app about the success or failure of each registration based
    // on what the registration manager has indicated.
    for (int i = 0; i < regStatusList.size(); ++i) {
      RegistrationStatus regStatus = regStatusList.get(i);
      boolean wasSuccess = localProcessingStatuses.get(i);
      logger.fine("Process reg status: %s", regStatus);

      // Only inform in the case of failure since the success path has already
      // been dealt with (the ticl issued informRegistrationStatus immediately
      // after receiving the register/unregister call).
      ObjectId objectId = ProtoConverter.convertFromObjectIdProto(
        regStatus.getRegistration().getObjectId());
      if (!wasSuccess) {
        String description = CommonProtos2.isSuccess(regStatus.getStatus()) ?
            "Registration discrepancy detected" : regStatus.getStatus().getDescription();

        // Note "success" shows up as transient failure in this scenario.
        boolean isPermanent = CommonProtos2.isPermanentFailure(regStatus.getStatus());
        listener.informRegistrationFailure(InvalidationClientCore.this, objectId, !isPermanent,
            description);
      }
    }
  }

  /** Handles a registration sync request. */
  private void handleRegistrationSyncRequest() {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    // Send all the registrations in the reg sync message.
    // Generate a single subtree for all the registrations.
    RegistrationSubtree subtree =
        registrationManager.getRegistrations(Bytes.EMPTY_BYTES.getByteArray(), 0);
    protocolHandler.sendRegistrationSyncSubtree(subtree, batchingTask);
  }

  /** Handles an info message request. */
  private void handleInfoMessage(Collection<InfoType> infoTypes) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    boolean mustSendPerformanceCounters = false;
    for (InfoType infoType : infoTypes) {
      mustSendPerformanceCounters = (infoType == InfoType.GET_PERFORMANCE_COUNTERS);
      if (mustSendPerformanceCounters) {
        break;
      }
    }
    sendInfoMessageToServer(mustSendPerformanceCounters,
        !registrationManager.isStateInSyncWithServer());
  }

  /** Handles an error message. */
  private void handleErrorMessage(ServerMessageHeader header,
      ErrorMessage.Code code, String description) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    // If it is an auth failure, we shut down the ticl.
    logger.severe("Received error message: %s, %s, %s", header, code, description);

    // Translate the code to error reason.
    int reason;
    switch (code) {
      case AUTH_FAILURE:
        reason = ErrorInfo.ErrorReason.AUTH_FAILURE;
        break;
      case UNKNOWN_FAILURE:
        reason = ErrorInfo.ErrorReason.UNKNOWN_FAILURE;
        break;
      default:
        reason = ErrorInfo.ErrorReason.UNKNOWN_FAILURE;
        break;
    }

    // Issue an informError to the application.
    ErrorInfo errorInfo = ErrorInfo.newInstance(reason, false, description, null);
    listener.informError(this, errorInfo);

    // If this is an auth failure, remove registrations and stop the Ticl. Otherwise do nothing.
    if (code != ErrorMessage.Code.AUTH_FAILURE) {
      return;
    }

    // If there are any registrations, remove them and issue registration failure.
    Collection<ObjectIdP> desiredRegistrations = registrationManager.removeRegisteredObjects();
    logger.warning("Issuing failure for %s objects", desiredRegistrations.size());
    for (ObjectIdP objectId : desiredRegistrations) {
      listener.informRegistrationFailure(this,
        ProtoConverter.convertFromObjectIdProto(objectId), false, "Auth error: " + description);
    }
  }

  /**
   * Returns whether the token in the header of {@code parsedMessage} matches either the
   * client token or nonce of this Ticl (depending on which is non-{@code null}).
   */
  private boolean validateToken(ParsedMessage parsedMessage) {
    if (clientToken != null) {
      // Client token case.
      if (!TypedUtil.<ByteString>equals(clientToken, parsedMessage.header.token)) {
        logger.info("Incoming message has bad token: server = %s, client = %s",
            CommonProtoStrings2.toLazyCompactString(parsedMessage.header.token),
            CommonProtoStrings2.toLazyCompactString(clientToken));
        statistics.recordError(ClientErrorType.TOKEN_MISMATCH);
        return false;
      }
      return true;
    } else if (nonce != null) {
      // Nonce case.
      Preconditions.checkState(nonce != null, "Client token and nonce are both null: %s, %s",
          clientToken, nonce);
      if (!TypedUtil.<ByteString>equals(nonce, parsedMessage.header.token)) {
        statistics.recordError(ClientErrorType.NONCE_MISMATCH);
        logger.info("Rejecting server message with mismatched nonce: Client = %s, Server = %s",
            CommonProtoStrings2.toLazyCompactString(nonce),
            CommonProtoStrings2.toLazyCompactString(parsedMessage.header.token));
        return false;
      } else {
        logger.info("Accepting server message with matching nonce: %s",
            CommonProtoStrings2.toLazyCompactString(nonce));
        return true;
      }
    }
    // Neither token nor nonce; ignore message.
    return false;
  }

  /**
   * Requests a new client identifier from the server.
   * <p>
   * REQUIRES: no token currently be held.
   *
   * @param debugString information to identify the caller
   */
  private void acquireToken(final String debugString) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    // Clear the current token and schedule the token acquisition.
    setClientToken(null);
    acquireTokenTask.ensureScheduled(debugString);
  }

  /**
   * Sends an info message to the server. If {@code mustSendPerformanceCounters} is true,
   * the performance counters are sent regardless of when they were sent earlier.
   */
  private void sendInfoMessageToServer(boolean mustSendPerformanceCounters,
      boolean requestServerSummary) {
    logger.info("Sending info message to server");
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    List<SimplePair<String, Integer>> performanceCounters =
        new ArrayList<SimplePair<String, Integer>>();
    List<SimplePair<String, Integer>> configParams =
        new ArrayList<SimplePair<String, Integer>>();
    ClientConfigP configToSend = null;
    if (mustSendPerformanceCounters) {
      statistics.getNonZeroStatistics(performanceCounters);
      configToSend = config;
    }
    protocolHandler.sendInfoMessage(performanceCounters, configToSend, requestServerSummary,
        batchingTask);
  }

  /** Reads the Ticl state from persistent storage (if any) and calls {@code startInternal}. */
  private void scheduleStartAfterReadingStateBlob() {
    storage.readKey(CLIENT_TOKEN_KEY, new Callback<SimplePair<Status, byte[]>>() {
      @Override
      public void accept(final SimplePair<Status, byte[]> readResult) {
        Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
        final byte[] serializedState = readResult.getFirst().isSuccess() ?
            readResult.getSecond() : null;
        // Call start now.
        if (!readResult.getFirst().isSuccess()) {
          statistics.recordError(ClientErrorType.PERSISTENT_READ_FAILURE);
          logger.warning("Could not read state blob: %s", readResult.getFirst().getMessage());
        }
        startInternal(serializedState);
      }
    });
  }

  /**
   * Converts an operation type {@code regOpType} to a
   * {@code InvalidationListener.RegistrationState}.
   */
  private static InvalidationListener.RegistrationState convertOpTypeToRegState(
      RegistrationP.OpType regOpType) {
    InvalidationListener.RegistrationState regState =
        regOpType == RegistrationP.OpType.REGISTER ?
            InvalidationListener.RegistrationState.REGISTERED :
              InvalidationListener.RegistrationState.UNREGISTERED;
    return regState;
  }

  /**
   * Sets the nonce to {@code newNonce}.
   * <p>
   * REQUIRES: {@code newNonce} be null or {@link #clientToken} be null.
   * The goal is to ensure that a nonce is never set unless there is no
   * client token, unless the nonce is being cleared.
   */
  private void setNonce(ByteString newNonce) {
    Preconditions.checkState((newNonce == null) || (clientToken == null),
        "Tried to set nonce with existing token %s", clientToken);
    this.nonce = newNonce;
  }

  /**
   * Sets the clientToken to {@code newClientToken}.
   * <p>
   * REQUIRES: {@code newClientToken} be null or {@link #nonce} be null.
   * The goal is to ensure that a token is never set unless there is no
   * nonce, unless the token is being cleared.
   */
  private void setClientToken(ByteString newClientToken) {
    Preconditions.checkState((newClientToken == null) || (nonce == null),
        "Tried to set token with existing nonce %s", nonce);

    // If the ticl is in the process of being started and we are getting a new token (either from
    // persistence or from the server, start the ticl and inform the application.
    boolean finishStartingTicl = !ticlState.isStarted() &&
       (clientToken == null) && (newClientToken != null);
    this.clientToken = newClientToken;

    if (finishStartingTicl) {
      finishStartingTiclAndInformListener();
    }
  }

  /** Start the ticl and inform the listener that it is ready. */
  private void finishStartingTiclAndInformListener() {
    Preconditions.checkState(!ticlState.isStarted());
    ticlState.start();
    listener.ready(this);

    // We are not currently persisting our registration digest, so regardless of whether or not
    // we are restarting from persistent state, we need to query the application for all of
    // its registrations.
    listener.reissueRegistrations(InvalidationClientCore.this, RegistrationManager.EMPTY_PREFIX, 0);
    logger.info("Ticl started: %s", this);
  }

  /**
   * Returns an exponential backoff generator with {@code initialDelayMs} and other state as
   * given in {@code marshalledState}.
   */
  private TiclExponentialBackoffDelayGenerator createExpBackOffGenerator(int initialDelayMs,
      ExponentialBackoffState marshalledState) {
    if (marshalledState != null) {
      return new TiclExponentialBackoffDelayGenerator(random, initialDelayMs,
          config.getMaxExponentialBackoffFactor(), marshalledState);
    } else {
      return new TiclExponentialBackoffDelayGenerator(random, initialDelayMs,
          config.getMaxExponentialBackoffFactor());
    }
  }

  /** Returns a map from recurring task name to the runnable for that recurring task. */
  protected Map<String, Runnable> getRecurringTasks() {
    final int numPersistentTasks = 6;
    HashMap<String, Runnable> tasks = new HashMap<String, Runnable>(numPersistentTasks);
    tasks.put(AcquireTokenTask.TASK_NAME, acquireTokenTask.getRunnable());
    tasks.put(RegSyncHeartbeatTask.TASK_NAME, regSyncHeartbeatTask.getRunnable());
    tasks.put(PersistentWriteTask.TASK_NAME, persistentWriteTask.getRunnable());
    tasks.put(HeartbeatTask.TASK_NAME, heartbeatTask.getRunnable());
    tasks.put(BatchingTask.TASK_NAME, batchingTask.getRunnable());
    tasks.put(InitialPersistentHeartbeatTask.TASK_NAME,
        initialPersistentHeartbeatTask.getRunnable());
    return tasks;
  }

  @Override
  public void toCompactString(TextBuilder builder) {
    builder.appendFormat("Client: %s, %s", applicationClientId,
        CommonProtoStrings2.toLazyCompactString(clientToken));
  }

  @Override
  public InvalidationClientState marshal() {
    Preconditions.checkState(internalScheduler.isRunningOnThread(),
        "Not running on internal thread");
    InvalidationClientState.Builder builder = InvalidationClientState.newBuilder();
    if (clientToken != null) {
      builder.setClientToken(clientToken);
    }
    builder.setLastMessageSendTimeMs(lastMessageSendTimeMs);
    if (nonce != null) {
      builder.setNonce(nonce);
    }
    builder.setProtocolHandlerState(protocolHandler.marshal())
      .setRegistrationManagerState(registrationManager.marshal())
      .setShouldSendRegistrations(shouldSendRegistrations)
      .setRunState(ticlState.marshal())
      .setIsOnline(isOnline)
      .setAcquireTokenTaskState(acquireTokenTask.marshal())
      .setPersistentWriteTaskState(persistentWriteTask.marshal())
      .setRegSyncHeartbeatTaskState(regSyncHeartbeatTask.marshal())
      .setHeartbeatTaskState(heartbeatTask.marshal())
      .setBatchingTaskState(batchingTask.marshal())
      .setStatisticsState(statistics.marshal());
    if (clientToken != null) {
      builder.setClientToken(clientToken);
    }
    if (persistentWriteTask.lastWrittenState.get() != null) {
      builder.setLastWrittenState(persistentWriteTask.lastWrittenState.get().getProto());
    }
    return builder.build();
  }
}
