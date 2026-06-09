#pragma once
#include "../radio/Radio.h"
#include "../protocol/IoHomeFrame.h"
#include "../protocol/IoHomeCrypto.h"
#include "../protocol/IoHomeCommands.h"
#include <stdint.h>
#include <string>

#define IOHC_CMD_QUEUE_SIZE 8
#define IOHC_MAX_RETRIES 3
#define IOHC_TX_TIMEOUT_MS 500
#define IOHC_RX_TIMEOUT_MS 300
#define IOHC_RX_FINAL_TIMEOUT_MS 500
#define IOHC_RETRY_GAP_MS 250
#define IOHC_AUTH_DWELL_MS_SX1262 90
#define IOHC_PAIR_TIMEOUT_MS 30000
#define IOHC_DUTY_CYCLE_WINDOW_MS 3600000 // 1 hour
#define IOHC_RX_SCAN_INTERVAL_US 2700     // ~2.7ms frequency scan interval (per nicolas5000)
// Maximum raw Execute payload bytes before appending the 1W sequence number.
// Normal 1W authenticated frames include 6-byte HMAC in CTRL0 length, so keep
// 9(header) + raw + 2(seq) + 6(hmac) <= IOHC_FRAME_BUFFER_SIZE.
#define IOHC_1W_RAW_EXEC_MAX_DATA (IOHC_FRAME_BUFFER_SIZE - 9 - 2 - IOHC_HMAC_SIZE)

class IoHomecontrolChannel;

// Queued command entry
struct IoHomeQueueEntry
{
  uint32_t destNodeId;
  const uint8_t *encKey; // pointer to channel's key (valid as long as channel exists)
  IoHomeCommand command;
  uint8_t param;
  uint8_t param2;            // second parameter (e.g., slat angle); 0xFF = unused
  uint8_t param3;            // third parameter (for _p0x00_16 extended format); 0xFF = unused
  bool oneWayButton;         // true: 1W button-style Execute command
  uint16_t oneWayButtonCode; // 0x0000=up, 0x0001=down, 0x0002=stop, 0x0003=my/prog, 0x00FE=release, 0x00FF=stop2
  bool oneWayRawExecute;     // true: send exact raw Execute payload bytes before sequence/HMAC
  uint8_t oneWayRawData[IOHC_1W_RAW_EXEC_MAX_DATA];
  uint8_t oneWayRawLen;
  bool oneWayStandardExecute; // true: standard 14-byte 1W Execute payload mapping
  uint16_t oneWayMain;        // main[2] value, e.g. 0x0000=open, 0xC800=close, 0xD200=stop
  uint8_t oneWayFp1;
  uint8_t oneWayFp2;
  uint8_t oneWayBroadcastType;      // target type: dst = ((type << 6) | 0x3F)
  bool oneWayBroadcastTypeExplicit; // true when the caller explicitly requested a typed 1W broadcast target
  bool twoWayTilt;                  // true: 2W tilt-only Execute payload
  uint8_t twoWayTiltPercent;
  uint8_t retries;
  bool active;
  uint8_t nameData[IOHC_NAME_MAX_SIZE]; // SetName payload (zero-padded, Latin-1)
  uint8_t nameLen;                      // actual name length (0 = not a SetName)
};

// Controller states
enum class ControllerState : uint8_t
{
  Idle,
  TxPending,
  TxInProgress,
  Tx1WRepeat, // waiting between 1W repeat transmissions
  WaitResponse,
  ProcessResponse,

  // Pairing states
  PairSendDiscovery,
  PairWaitDiscoveryResponse,

  // Experimental / legacy 2W pairing states.
  //
  // These are intentionally not used by the default 2W pairing flow.
  // Normal 2W pairing follows:
  //   DiscoverRequest(0x28)
  //   -> DiscoverResponse(0x29)
  //   -> KeyInitTransfer(0x31)
  //   -> ChallengeRequest(0x3C)
  //   -> KeyTransfer(0x32)
  //   -> KeyTransferConfirmation(0x33/0x2D)
  //   -> optional SetConfig1(0x6F)
  //
  // The states below are kept for protocol research and device-specific tests.
  // They must only be entered through explicit diagnostic pairing modes.
  PairSendDiscoveryConfirmation,
  PairWaitDiscoveryConfirmationAck,
  PairSendLaunchKeyTransfer,
  PairWaitLaunchKeyTransfer,
  PairSendPullKeyChallenge,
  PairWaitPullKeyChallengeResponse,
  PairSend1WRemove,
  PairWait1WRemove,
  PairSend1WKeyTransfer,
  PairWait1WKeyTransfer,
  PairSendKeyInit,
  PairWaitDeviceChallenge,
  PairSendKeyTransfer,
  PairSendKeyTransferAuthResponse,
  PairWaitKeyTransferConfirmation,
  PairSendSetConfig1,
  PairWaitSetConfig1Response,
  PairSendSetConfig1AuthResponse,
  PairWaitSetConfig1FinalResponse,
  PairComplete,
  PairFailed,

  // Receive-side authentication (unsolicited StatusUpdate)
  AuthSendChallenge,
  AuthWaitResponse,

  // StatusUpdate ACK broadcast
  StatusAckSend,
  StatusAckTxWait,

  // Discovery (scan only)
  DiscoverySending,
  DiscoveryListening,

  // Command scanning (probe device capabilities)
  ScanSending,
  ScanWaitResponse,

  // Passive mode (listen only)
  PassiveListening,

  // Fake gateway mode (respond to device-initiated pairing)
  GatewayIdle,
  GatewayWaitDiscoveryResponse,
  GatewayWaitKeyTransfer,
  GatewayWaitChallenge
};

enum class Pairing2WMode : uint8_t
{
  Normal = 0,                // default: 0x28 -> 0x31 -> 0x32 -> 0x33 -> optional 0x6F
  DiscoveryConfirmation = 1, // experimental: 0x2C/0x2D after discovery
  LaunchKeyTransfer = 2,     // experimental: 0x38 path
  PullKey = 3                // experimental: pull existing key from device
};

// Pairing result callback
class IoHomecontrol;

class IoHomeController
{
public:
  enum class PairStartStatus : uint8_t
  {
    Ok = 0,
    Busy = 1,
    Missing1WTarget = 2,
    Failed = 3
  };

  IoHomeController();

  // Initialize radio with hardware pins from IoHomecontrolHardware.h
  void init();

  // Main loop - process state machine (must be called frequently)
  void loop();

  // Start receiving on current frequency
  RadioError startReceive();

  // Enter sleep mode
  void sleep();

  // Queue a command for transmission
  bool sendCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                   IoHomeCommand iCmd, uint8_t iParam);
  bool sendCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                   IoHomeCommand iCmd, uint8_t iParam, uint8_t iParam2);
  bool sendCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                   IoHomeCommand iCmd, uint8_t iParam, uint8_t iParam2, uint8_t iParam3);

  // Queue a 1W raw/button-style Execute command.
  // Codes from known 1W remotes: 0x0000=up, 0x0001=down, 0x0002=stop,
  // 0x0003=my/prog, 0x00FE=release, 0x00FF=alternative stop.
  bool sendOneWayButton(uint32_t iDestNodeId, const uint8_t *iEncKey, uint16_t iButtonCode);

  // Queue an exact 1W Execute payload. The controller appends sequence + HMAC.
  bool sendOneWayRawExecute(uint32_t iDestNodeId, const uint8_t *iEncKey,
                            const uint8_t *iPayload, uint8_t iPayloadLen);

  // Queue a standard 1W Execute command with an explicit broadcast type:
  // payload = 01 43 main[2] fp1 fp2, then sequence + HMAC are appended.
  bool sendOneWayExecuteWithType(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                 uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                 uint8_t iBroadcastType);

  // Set device name (authenticated 2W command: 0x52 → 0x3C → 0x3D → 0x53)
  bool sendSetName(uint32_t iDestNodeId, const uint8_t *iEncKey,
                   const char *iName, uint8_t iNameLen);

  // Ask a paired 2W device to identify itself (authenticated 0x1E → 0x3C → 0x3D)
  bool sendIdentify(uint32_t iDestNodeId, const uint8_t *iEncKey);

  bool sendBatteryStatusQuery(uint32_t iDestNodeId, const uint8_t *iEncKey);
  bool sendBatteryStateQuery(uint32_t iDestNodeId, const uint8_t *iEncKey);
  bool sendTiltStatusQuery(uint32_t iDestNodeId, const uint8_t *iEncKey);
  bool sendTiltCommand(uint32_t iDestNodeId, const uint8_t *iEncKey, uint8_t iTiltPercent);

  // Start pairing process for a channel
  bool startPairing(uint8_t iChannelIndex, uint32_t iKnownNodeId = 0);
  bool startPairingExperimental(uint8_t iChannelIndex, uint32_t iKnownNodeId, Pairing2WMode iMode);
  // Start the standard 1W learning flow with an explicit broadcast type override.
  bool startPairingWithType(uint8_t iChannelIndex, uint32_t iKnownNodeId, uint8_t iBroadcastType);
  PairStartStatus lastPairStartStatus() const;
  ControllerState lastPairStartBlockedState() const;

  // Cancel ongoing pairing
  void cancelPairing();

  // Start discovery scan (no pairing); encrypted mode only lets paired/known devices respond
  void startDiscovery(bool iEncrypted = false);

  // Start command scan (probe device for supported commands)
  void startCommandScan(uint32_t iNodeId);

  // Set passive mode (listen-only, no transmit). Key extraction is opt-in via
  // startPassiveKeySniff().
  void setPassiveMode(bool iEnabled);
  bool isPassiveMode() const;

  enum class PassiveKeySniffStatus : uint8_t
  {
    Idle = 0,
    Listening = 1,
    Captured = 2,
    Timeout = 3
  };

  struct PassiveKeyResult
  {
    bool valid;
    uint32_t nodeId;
    uint8_t key[16];
    uint32_t capturedAt;
    uint8_t freqIdx;
  };

  static constexpr uint32_t kPassiveKeySniffDefaultTimeoutMs = 60000UL;

  bool startPassiveKeySniff(uint32_t iTimeoutMs = kPassiveKeySniffDefaultTimeoutMs);
  void stopPassiveKeySniff();
  void clearPassiveKeyResult();
  PassiveKeySniffStatus passiveKeySniffStatus() const;
  const PassiveKeyResult &passiveKeyResult() const;

  // Fake gateway mode (respond to device-initiated pairing requests)
  void setGatewayMode(bool iEnabled);
  bool isGatewayMode() const;
  void setGatewayNodeId(uint32_t iNodeId);
  uint32_t getGatewayNodeId() const;
  void setGatewayKey(const uint8_t *iKey); // 16-byte stack key for push pairing
  const uint8_t *getGatewayKey() const;
  uint8_t getGatewayPairedDeviceCount() const;
  uint32_t getGatewayPairedNodeId(uint8_t iIndex) const; // returns 0 if not found
  void clearGatewayPairedDevices();

  void setPairDiagnosticTraceEnabled(bool iEnabled);
  bool isPairDiagnosticTraceEnabled() const;
  static const char *stateName(ControllerState iState);
  void logPairDiagnosticStatus() const;

  // Multi-frequency RX scanning (cycle through all frequencies during idle/passive)
  void setRxScanEnabled(bool iEnabled);
  bool isRxScanEnabled() const;
  uint8_t lastResponseFreqIdx() const;

  // Update controller's current frequency index from an absolute frequency (Hz)
  // This allows external callers to inform the controller when the radio
  // frequency was changed directly at the radio layer.
  void updateCurrentFrequencyIndex(uint32_t iFrequencyHz);

  // Network scan (passive packet capture with per-node stats)
  struct IoHomeScanEntry
  {
    uint32_t timestamp;
    IoHomeFrame frame;
    int16_t rssi;
    uint8_t freqIdx;
    bool valid;
  };

  struct IoHomeNodeStats
  {
    uint32_t nodeId;
    uint16_t packetCount;
    int16_t lastRssi;
    IoHomeCommand lastCommand;
    bool active;
  };

  static constexpr uint8_t kScanBufferSize = 32;
  static constexpr uint8_t kMaxTrackedNodes = 16;

  void startNetworkScan();
  void stopNetworkScan();
  bool isNetworkScanActive() const;
  const IoHomeScanEntry *scanBuffer() const;
  uint8_t scanBufferHead() const;
  const IoHomeNodeStats *nodeStats() const;
  static const char *commandName(IoHomeCommand iCmd);

  struct IoHomeRadioHealth
  {
    bool initialized;
    RadioState radioState;
    ControllerState controllerState;
    bool passiveMode;
    bool networkScanActive;
    bool rxScanEnabled;
    bool preambleDetected;
    uint8_t currentFreqIdx;
    uint8_t lastResponseFreqIdx;
    int16_t lastRssi;
    bool currentRssiValid;
    int16_t currentRssi;
    uint8_t queueDepth;
    uint16_t dutyPermille;
    uint8_t initError;
    uint8_t initStatusByte;
    uint8_t initCommandStatus;
    uint16_t initDeviceErrors;
    uint16_t lastDeviceErrors;
    uint32_t tcxoStartupDelayUs;
    uint8_t tcxoStartupAttempts;
    uint8_t rfSwitchConfig;
    bool busyTimedOut;
    uint32_t txStartCount;
    uint32_t txDoneCount;
    uint32_t rxStartCount;
    uint32_t irqCount;
    uint32_t preambleIrqCount;
    uint32_t syncWordIrqCount;
    uint32_t rxDoneCount;
    uint32_t crcErrorCount;
    uint32_t timeoutCount;
    uint32_t irqPollHitCount;
    uint32_t preambleOnlyIrqCount;
    uint32_t rxReadFailCount;
    uint16_t lastIrqStatus;
    uint8_t lastOpStatusBefore;
    uint8_t lastOpStatusAfter;
    uint8_t lastTxSetStatus;
    uint16_t lastTxIrqImmediate;
  };

  IoHomeRadioHealth radioHealth() const;

  // Set own node ID (3-byte, 24-bit)
  void setOwnNodeId(uint32_t iNodeId);
  uint32_t getOwnNodeId() const;

  // Set system key (16 bytes AES-128)
  void setSystemKey(const uint8_t *iKey);
  const uint8_t *getSystemKey() const;
  void setOneWayBroadcastType(uint8_t iBroadcastType);
  uint8_t getOneWayBroadcastType() const;
  uint32_t oneWayBroadcastTarget(uint8_t iBroadcastType) const;
  IoHomecontrolChannel *oneWayProfileForChannel(IoHomecontrolChannel *iChannel) const;

  // Set pointer to parent module (for channel callbacks)
  void setModule(IoHomecontrol *iModule);

  // Get current state
  ControllerState state() const;

  // Radio RSSI of last received packet
  int16_t lastRssi() const;

  Radio &radio();
  const Radio &radio() const;

private:
  enum class DiscoverySendPhase : uint8_t
  {
    SetFrequency,
    SetPreamble,
    StartTransmit
  };

  struct DiscoveryTimingTrace
  {
    uint32_t hopStartUs;
    uint32_t freqReadyUs;
    uint32_t preambleReadyUs;
    uint32_t txStartUs;
    uint32_t txDoneUs;
    uint32_t rxReadyUs;
    uint32_t rxBusyCount;
  };

  Radio mRadio;
  IoHomecontrol *mModule;

  // Own identity
  uint32_t mOwnNodeId;
  uint8_t mSystemKey[16];

  // State machine
  ControllerState mState;
  uint32_t mStateTimer;
  uint8_t mCurrentFreqIdx;

  // Command queue (circular buffer)
  IoHomeQueueEntry mCmdQueue[IOHC_CMD_QUEUE_SIZE];
  uint8_t mQueueHead;
  uint8_t mQueueTail;

  // Current command being processed (for retry on timeout)
  IoHomeQueueEntry mCurrentCmd;

  // Current TX frame
  IoHomeFrame mTxFrame;
  IoHomeFrame mRxFrame;
  IoHomeFrame mPairSetConfigRequest;
  uint8_t mTxBuffer[IOHC_FRAME_BUFFER_SIZE];
  uint8_t mTxLen;
  uint8_t mRxBuffer[IOHC_FRAME_BUFFER_SIZE];

  // 1W repeat transmission state
  uint8_t mTx1WRepeatRemaining = 0; // remaining 1W repeats (0 = done)
  uint32_t mTx1WRepeatTimer = 0;    // millis timestamp for next repeat

  // TX timing diagnostics/guard. Long io-homecontrol preambles can exceed the
  // generic 500 ms TX timeout on SX1276, especially for 1W learn/key frames.
  uint16_t mCurrentTxPreambleSymbols = IOHC_PREAMBLE_SHORT;

  // 2W challenge-response auth state (for authenticated commands like SetName)
  bool mAuthResponseSent = false; // true after sending ChallengeResponse, reset on new command
  bool mWaitingFinalResponse = false;
  bool mSawChallenge = false;
  uint32_t mResponseTimeoutMs = IOHC_RX_TIMEOUT_MS;
  uint32_t mRetryAtMs = 0;

  // Pairing state
  uint8_t mPairingChannel;
  PairStartStatus mLastPairStartStatus = PairStartStatus::Ok;
  ControllerState mLastPairStartBlockedState = ControllerState::Idle;
  uint8_t mPairingChallenge[6];
  uint32_t mDiscoveredNodeId;
  uint8_t mPairingFreqIdx;
  uint32_t mPairingStartTime;
  DiscoverySendPhase mDiscoverySendPhase;
  DiscoveryTimingTrace mDiscoveryTimingTrace;
  bool mDiscoverySPE; // encrypted discovery mode
  uint8_t mPairSetConfigChallenge[6];
  uint8_t mPairKeyTransferChallenge[6];
  IoHomeFrame mPairLaunchKeyTransferFrame;
  IoHomeFrame mPairPulledKeyFrame;
  uint8_t mPairPulledKey[16];
  uint8_t mPairPullAuthChallenge[6];
  uint8_t mPairing1WStage = 0; // 0=Pair(0x2E), 1=Remove(0x39), 2=Add(0x30)
  uint8_t mPairing1WBroadcastType = 2;
  uint8_t mDefault1WBroadcastType = 2;
  Pairing2WMode mPairing2WMode = Pairing2WMode::Normal;

  // Receive-side authentication state
  IoHomeFrame mPendingAuthFrame; // saved unsolicited frame awaiting verification
  uint8_t mAuthChallenge[6];     // challenge we sent for receive-side auth
  uint32_t mAuthSrcNodeId;       // source node of the unsolicited frame
  uint8_t mAuthChannelIdx;       // channel index for the pending auth

  // StatusUpdateResponse ACK broadcast state
  uint32_t mStatusAckDestNodeId;
  uint8_t mStatusAckFreqIdx;

  // Passive mode (listen-only key extraction)
  bool mPassiveMode;
  IoHomeFrame mPassiveKeyInit;  // saved KeyInitTransfer frame
  uint8_t mPassiveChallenge[6]; // challenge from observed ChallengeRequest
  uint32_t mPassivePairNodeId;  // node ID being paired (observed)
  bool mPassiveChallengeValid;
  PassiveKeySniffStatus mPassiveKeySniffStatus;
  PassiveKeyResult mPassiveKeyResult;
  uint32_t mPassiveKeySniffStartedAt;
  uint32_t mPassiveKeySniffTimeoutMs;

  // Fake gateway mode state
  bool mGatewayMode;
  uint32_t mGatewayNodeId;          // gateway node ID (source address in responses)
  uint8_t mGatewayKey[16];          // stack key used during 2W push pairing
  ControllerState mGatewayState;    // gateway state machine state
  uint32_t mGatewayPeerNodeId;      // node ID of device being paired
  uint8_t mGatewayKeyEncrypted[16]; // encrypted stack key (ready to send in 0x32)
  uint8_t mGatewayMemCmd;           // last command sent (for 0x3D HMAC)
  uint8_t mGatewayMemData[21];      // last data sent (for 0x3D HMAC)
  uint8_t mGatewayMemDataLen;       // length of last data sent
  uint8_t mGatewayPeerChallenge[6]; // challenge from device (for 0x3D HMAC)
  uint8_t mGatewayDiscoverFreqIdx;  // frequency for discovery responses
  uint8_t mGatewayDeviceCount;      // number of paired devices tracked
  static constexpr uint8_t kMaxGatewayPairedDevices = 8;
  uint32_t mGatewayPairedNodeIds[kMaxGatewayPairedDevices];

  // Network scan buffer (passive packet capture)
  IoHomeScanEntry mScanBuffer[kScanBufferSize];
  uint8_t mScanBufferHead;
  bool mNetworkScanActive;

  // Per-node statistics (observed during network scan)
  IoHomeNodeStats mNodeStats[kMaxTrackedNodes];

  // Command scan result — tracks which commands elicited responses
  struct IoHomeCommandScanResult
  {
    uint32_t targetNodeId;   // node that was scanned
    uint8_t commandsScanned; // total commands in scan list
    uint8_t responsesFound;  // number of commands that got a response
    uint8_t bitmask[32];     // bitmask per command: 1=response, 0=no response
  };

  // Command scanning state
  uint8_t mScanIndex;
  uint32_t mScanTargetNode;
  IoHomeCommandScanResult mScanResult; // accumulated scan results

  // Duty cycle tracking (per sub-band per hour)
  uint32_t mTxTimeAccum[IOHC_NUM_FREQUENCIES]; // accumulated TX time in ms
  uint32_t mDutyCycleWindowStart;

  // Multi-frequency RX scanning
  uint32_t mRxScanLastSwitch;   // timestamp (micros) of last frequency switch
  uint32_t mRxScanIntervalUs;   // interval between frequency switches
  bool mRxScanEnabled;          // whether to cycle frequencies during idle RX
  uint8_t mLastResponseFreqIdx; // frequency index where last response was received
  bool mPairDiagnosticTraceEnabled;
  ControllerState mLastPairDiagnosticTraceState;

  void resetDiscoveryTimingTrace();
  void logDiscoveryTimingTrace(const char *iReason, unsigned long iListenElapsedMs) const;

  // Internal methods
  const std::string logPrefix() const;
  bool isPairDiagnosticState(ControllerState iState) const;
  void tracePairDiagnosticStateChange();
  void tracePairDiagnosticFrame(const char *iPrefix, const IoHomeFrame &iFrame, uint8_t iFreqIdx, int16_t iRssi) const;
  void tracePairDiagnosticDiscoveryInterpretation(const IoHomeFrame &iFrame, uint8_t iFreqIdx) const;
  void processIdle();
  void processTxPending();
  void processTxInProgress();
  void processTx1WRepeat();
  void processWaitResponse();
  void processResponse();

  void processPairSendDiscovery();
  void processPairWaitDiscoveryResponse();
  void processPairSendDiscoveryConfirmation();
  void processPairWaitDiscoveryConfirmationAck();
  void processPairSendLaunchKeyTransfer();
  void processPairWaitLaunchKeyTransfer();
  void processPairSendPullKeyChallenge();
  void processPairWaitPullKeyChallengeResponse();
  void processPairSend1WRemove();
  void processPairWait1WRemove();
  void processPairSend1WKeyTransfer();
  void processPairWait1WKeyTransfer();
  void processPairSendKeyInit();
  void processPairWaitDeviceChallenge();
  void processPairSendKeyTransfer();
  void processPairSendKeyTransferAuthResponse();
  void processPairWaitKeyTransferConfirmation();
  void processPairSendSetConfig1();
  void processPairWaitSetConfig1Response();
  void processPairSendSetConfig1AuthResponse();
  void processPairWaitSetConfig1FinalResponse();
  void interpretSetConfig1Result(bool iFinalResponse);
  bool processPairWait1WBlind(ControllerState iNextState);

  void processDiscovery();

  // Command scan handlers
  void processScanSending();
  void processScanWaitResponse();

  // Receive-side authentication handlers
  void processAuthSendChallenge();
  void processAuthWaitResponse();

  // StatusUpdateResponse ACK handlers
  void processStatusAckSend();
  void processStatusAckTxWait();

  // Passive mode frame processing
  void processPassiveFrame();

  // Fake gateway mode handlers
  void processGatewayFrame();
  void processGatewayIdle();
  void processGatewayWaitDiscoveryResponse();
  void processGatewayWaitKeyTransfer();
  void processGatewayWaitChallenge();
  bool precheckGatewayStateFrame(uint32_t iSrcNode, bool iResetSessionOnDiscover);
  void resetGatewaySessionState();
  void logGatewayState(const char *iLabel) const;

  // Network scan recording
  void recordScanFrame(const IoHomeFrame &iFrame, int16_t iRssi, uint8_t iFreqIdx);
  void updateNodeStats(uint32_t iNodeId, int16_t iRssi, IoHomeCommand iCmd);

  // Command scan results
  const IoHomeCommandScanResult *commandScanResults() const;
  bool commandHasResponse(uint8_t iCmdIndex) const;
  bool commandSupported(uint8_t iCmdCode) const;
  void recordCommandScanResponse(uint8_t iCmdIndex);
  void logCommandScanResults() const;

  // Build frame from queue entry
  bool buildTxFrame(const IoHomeQueueEntry &iEntry);

  // Dispatch received frame to appropriate channel
  void dispatchRxFrame();

  // Send StatusUpdateResponse (0x72) ACK for unsolicited StatusUpdate
  void sendStatusUpdateResponse(uint32_t iDestNodeId);
  uint8_t buildStatusUpdateResponse(uint32_t iDestNodeId, uint8_t *oBuffer, uint8_t iBufferLen) const;

  // Configure TX-side radio settings without blocking on BUSY.
  RadioError configureTxRadio(uint16_t iPreambleSymbols, const uint32_t *iFrequencyHz = nullptr);
  RadioError startTransmitWithPreamble(const uint8_t *iBuffer, uint8_t iLen,
                                       uint16_t iPreambleSymbols,
                                       bool iTrackDutyCycle = false);
  RadioError startShortPreambleTransmit(const uint8_t *iBuffer, uint8_t iLen,
                                        bool iTrackDutyCycle = false);
  uint16_t authResponsePreamble() const;
  uint32_t currentTxTimeoutMs() const;

  RadioError ensureReceiveAfterTransmit();

  // Hop to next frequency
  bool hopFrequency();

  // Check duty cycle limit
  bool isDutyCycleOk() const;
  bool isDutyCycleOk(uint8_t iFreqIdx) const;

  // Queue helpers
  bool queuePush(const IoHomeQueueEntry &iEntry);
  bool queuePop(IoHomeQueueEntry &oEntry);
  bool queueEmpty() const;
  IoHomecontrolChannel *channelForNode(uint32_t iNodeId) const;
  bool resolveLowPower2W(uint32_t iNodeId) const;
  IoHomecontrolChannel *oneWayProfileForNode(uint32_t iNodeId) const;
  uint8_t oneWayBroadcastTypeForNode(uint32_t iNodeId) const;
};
