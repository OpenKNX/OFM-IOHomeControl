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
#ifndef IOHC_PAIR_KEY_EXCHANGE_MAX_ATTEMPTS
#define IOHC_PAIR_KEY_EXCHANGE_MAX_ATTEMPTS 3
#endif
#define IOHC_PAIR_KEY_EXCHANGE_TIMEOUT_MS 15000
#define IOHC_AUTH_DWELL_MS_SX1262 90
#define IOHC_AUTH_PREAMBLE_SX1262 64
#define IOHC_PAIR_TIMEOUT_MS 30000
// Diagnostic discovery sweep: listen window per frequency and how many full
// frequency sweeps a single discovery broadcast performs before giving up.
// The extended window adds a short grace period so a frame already arriving at
// the window boundary is not truncated by hopping to the next frequency.
#define IOHC_DISCOVERY_LISTEN_MS 2000
#define IOHC_DISCOVERY_LISTEN_EXTENDED_MS 2050
#define IOHC_DISCOVERY_MAX_SWEEPS 3
#define IOHC_DUTY_CYCLE_WINDOW_MS 3600000 // 1 hour
#define IOHC_LBT_RSSI_THRESHOLD_DBM -90   // clear channel threshold before TX
#define IOHC_LBT_MAX_RETRIES 5            // normal TX: 5 * 5ms worst-case
#define IOHC_LBT_AUTH_MAX_RETRIES 1       // auth responses must not be delayed too long
#define IOHC_LBT_RETRY_DELAY_MS 5
#define IOHC_RX_SCAN_INTERVAL_US 2700 // ~2.7ms frequency scan interval (per nicolas5000)
// Maximum raw Execute payload bytes before appending the 1W sequence number.
// Normal 1W authenticated frames include 6-byte HMAC in CTRL0 length, so keep
// 9(header) + raw + 2(seq) + 6(hmac) <= IOHC_FRAME_BUFFER_SIZE.
#define IOHC_1W_RAW_EXEC_MAX_DATA (IOHC_FRAME_BUFFER_SIZE - 9 - 2 - IOHC_HMAC_SIZE)

class IoHomecontrolChannel;

// 1W destination policy for queued commands.
// ProfileTyped is the normal/default path and resolves dst=((type << 6) | 0x3F).
// The reference-compatible default type is 0 (“All”), therefore dst=0x00003F.
// Explicit type 2/3 and Exact remain ETS/console/diagnostic override paths.
enum class OneWayDestinationMode : uint8_t
{
  ProfileTyped = 0,
  ExplicitType = 1,
  All = 2,
  Exact = 3
};

// First-class 1W pairing/add/remove operation.
// These modes map directly to the rspaargaren/iohomecontrol user operations:
//   announce-only -> 0x2E only
//   add-only      -> 0x30 only
//   announce-add  -> 0x2E then 0x30 (diagnostic fallback)
//   remove        -> 0x39 only
//   remove-add    -> 0x39 then 0x30 (default, captured Smoove enrollment gesture)
enum class Pairing1WMode : uint8_t
{
  AnnounceAdd = 0,
  AnnounceOnly = 1,
  AddOnly = 2,
  Remove = 3,
  RemoveAdd = 4
};

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
  uint8_t oneWayAcei;         // ACEI byte for reference/default 1W Execute/Activate templates
  uint16_t oneWayMain;        // low-level raw IOHC main[2], e.g. 0x0000=open, 0xC800=closed, 0xD200=stop
  uint8_t oneWayFp1;
  uint8_t oneWayFp2;
  uint8_t oneWayBroadcastType;                 // target type: dst = ((type << 6) | 0x3F)
  bool oneWayBroadcastTypeExplicit;            // true when the caller explicitly requested a typed 1W broadcast target
  OneWayDestinationMode oneWayDestinationMode; // normal/profile typed, all, exact, or explicit type
  uint32_t oneWayExactDestination;             // exact 24-bit 1W dst for diagnostics
  uint8_t sourceChannelIndex;                  // 0xFF when not queued from a concrete channel
  bool twoWayTilt;                             // true: 2W tilt-only Execute payload
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
  PairSend1WAnnounce,
  PairWait1WAnnounce,
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

  // Active key extraction (device-role responder)
  ExtractIdle,
  ExtractSentDiscoverResp,
  ExtractSentConfirmAck,
  ExtractSentChallenge,
  Extracted,
  ExtractSentAddressResp,

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

  // Queue a command for a concrete 1W channel profile. This path does not
  // require a bound actuator node ID; targetNode=0 is a valid broadcast-only
  // virtual remote profile and still serializes to the typed/all 1W destination.
  bool sendChannelCommand(IoHomecontrolChannel *iChannel,
                          IoHomeCommand iCmd, uint8_t iParam,
                          uint8_t iParam2 = 0xFF, uint8_t iParam3 = 0xFF);

  // Explicit 1W position convention helpers. UI/Open percent uses 100=open;
  // raw IOHC closedness uses 0=open and 100=closed. The generic Execute
  // builder consumes raw closedness percent.
  static uint8_t uiOpenPercentToRawClosedPercent(uint8_t iUiOpenPercent);
  static uint16_t rawClosedPercentToOneWayMain(uint8_t iRawClosedPercent);

  // Queue a 1W raw/button-style Execute command.
  // Codes from known 1W remotes: 0x0000=up, 0x0001=down, 0x0002=stop,
  // 0x0003=my/prog, 0x00FE=release, 0x00FF=alternative stop.
  bool sendOneWayButton(uint32_t iDestNodeId, const uint8_t *iEncKey, uint16_t iButtonCode);
  bool sendOneWayChannelButton(IoHomecontrolChannel *iChannel, uint16_t iButtonCode);

  // Queue an exact 1W Execute payload. The controller appends sequence + HMAC.
  bool sendOneWayRawExecute(uint32_t iDestNodeId, const uint8_t *iEncKey,
                            const uint8_t *iPayload, uint8_t iPayloadLen);
  bool sendOneWayChannelRawExecute(IoHomecontrolChannel *iChannel,
                                   const uint8_t *iPayload, uint8_t iPayloadLen);

  // Queue a standard 1W Execute command with an explicit broadcast type:
  // payload = 01 43 main[2] fp1 fp2, then sequence + HMAC are appended.
  bool sendOneWayExecuteWithType(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                 uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                 uint8_t iBroadcastType);
  bool sendOneWayChannelExecuteWithType(IoHomecontrolChannel *iChannel,
                                        uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                        uint8_t iBroadcastType);
  bool sendOneWayExecuteWithDestination(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                        uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                        OneWayDestinationMode iDestinationMode,
                                        uint8_t iBroadcastType,
                                        uint32_t iExactDestination);
  bool sendOneWayExecuteWithTemplate(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                     uint8_t iAcei, uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                     OneWayDestinationMode iDestinationMode,
                                     uint8_t iBroadcastType,
                                     uint32_t iExactDestination);

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
  // Start explicit 1W pairing/add/remove operations.
  bool startPairing1W(uint8_t iChannelIndex, uint32_t iKnownNodeId, Pairing1WMode iMode);
  bool startPairing1WAnnounceOnly(uint8_t iChannelIndex, uint32_t iKnownNodeId);
  bool startPairing1WAddOnly(uint8_t iChannelIndex, uint32_t iKnownNodeId);
  bool startPairing1WAnnounceAdd(uint8_t iChannelIndex, uint32_t iKnownNodeId);
  bool startPairing1WRemove(uint8_t iChannelIndex, uint32_t iKnownNodeId);
  // Start a 1W learning flow with an explicit broadcast type override.
  bool startPairingWithType(uint8_t iChannelIndex, uint32_t iKnownNodeId, uint8_t iBroadcastType,
                            Pairing1WMode iMode = Pairing1WMode::RemoveAdd);
  PairStartStatus lastPairStartStatus() const;
  ControllerState lastPairStartBlockedState() const;
  Pairing1WMode lastPairing1WMode() const;
  static const char *pairing1WModeName(Pairing1WMode iMode);

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
    Timeout = 3,
    CapturedTrailerMacVerified = 4,
    TrailerMacInvalid = 5
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

  enum class KeyExtractStatus : uint8_t
  {
    Idle = 0,
    Armed = 1,
    Captured = 2,
    Timeout = 3
  };

  static constexpr uint32_t kKeyExtractDefaultTimeoutMs = 600000UL;
  static constexpr uint32_t kKeyExtractPostExtractGraceMs = 60000UL;

  bool startKeyExtraction(uint32_t iTimeoutMs = kKeyExtractDefaultTimeoutMs);
  void stopKeyExtraction();
  void clearKeyExtractResult();
  KeyExtractStatus keyExtractStatus() const;
  const PassiveKeyResult &keyExtractResult() const;

  // 1W key copy/clone: listen for an existing remote's over-air SendKey1W
  // (0x30) "copy remote" frame, decrypt its key with the well-known transfer
  // key, and store the captured remote identity + key into the target
  // channel's 1W controller profile. After capture the module transmits as a
  // true clone of the original remote, so an actuator that already trusts that
  // remote obeys the cloned commands.
  enum class OneWayKeyReceiveStatus : uint8_t
  {
    Idle = 0,
    Listening = 1,
    Captured = 2,
    Timeout = 3,
    CapturedTrailerMacVerified = 4,
    TrailerMacInvalid = 5,
    SharedProfile = 6
  };

  static constexpr uint32_t kOneWayKeyReceiveDefaultTimeoutMs = 60000UL;

  bool startOneWayKeyReceive(uint8_t iChannelIndex,
                             uint32_t iTimeoutMs = kOneWayKeyReceiveDefaultTimeoutMs);
  void stopOneWayKeyReceive();
  OneWayKeyReceiveStatus oneWayKeyReceiveStatus() const;
  uint32_t oneWayKeyReceiveCapturedNode() const;

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
    uint8_t raw[IOHC_FRAME_BUFFER_SIZE]; // exact on-air bytes as received
    uint8_t rawLen;
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
    bool lastLbtRssiValid;
    int16_t lastLbtRssi;
    uint8_t lastLbtAttempts;
    bool lastLbtBypassed;
    bool lastLbtAuthResponse;
    uint32_t lbtBusyCount;
    uint32_t lbtBypassCount;
    uint32_t lbtClearCount;
    uint32_t lbtInvalidRssiCount;
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
    uint32_t rxFifoOverrunCount;
    uint32_t rxFifoEmptyCount;
    uint32_t rxParseFailCount;
    uint8_t lastRxLen;
    uint16_t lastRxIrqStatus;
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
  // Map ETS device roles to the protocol class used for 1W typed broadcast.
  static uint8_t oneWayBroadcastTypeForEtsDeviceType(uint8_t iEtsDeviceType);
  IoHomecontrolChannel *oneWayProfileForChannel(IoHomecontrolChannel *iChannel) const;

  // Set pointer to parent module (for channel callbacks)
  void setModule(IoHomecontrol *iModule);

  // Get current state
  ControllerState state() const;

  // Pure 2W exchange classification helpers. These intentionally only inspect
  // the original request and the candidate response; they do not touch radio
  // state, timers, retries, or channel state. Unit tests can call these directly.
  enum class FirstResponseDisposition : uint8_t
  {
    Ignore,
    DirectComplete,
    NeedAuth
  };

  enum class FinalResponseDisposition : uint8_t
  {
    Ignore,
    Accept
  };

  static bool frameMatchesExchangeEndpoints(const IoHomeFrame &iRequest,
                                            const IoHomeFrame &iCandidate);
  static FirstResponseDisposition classifyFirstResponse(const IoHomeFrame &iRequest,
                                                        const IoHomeFrame &iCandidate);
  static FinalResponseDisposition classifyFinalResponse(const IoHomeFrame &iRequest,
                                                        const IoHomeFrame &iCandidate);

  // Pure status/position decoding helpers. These freeze the reference-compatible
  // raw position rules independently of channel callbacks and radio state.
  static constexpr uint16_t kPositionRawTolerance = 100;

  struct PositionDecodeResult
  {
    bool hasTargetPosition;
    float targetPositionPercent;
    bool hasCurrentPosition;
    float currentPositionPercent;
    bool moving;
  };

  static bool rawPositionToPercent(uint16_t iRaw, float &oPercent);
  static bool rawPositionNear(uint16_t iA, uint16_t iB);
  static PositionDecodeResult decodePositionStatus(uint16_t iTargetRaw,
                                                   uint16_t iCurrentRaw,
                                                   bool iStopped);
  static bool decodeTiltRaw(uint16_t iRaw, float &oPercent);

  // Radio RSSI of last received packet
  int16_t lastRssi() const;

  Radio &radio();
  const Radio &radio() const;

private:
  enum class TxContext : uint8_t
  {
    InitialStartFrame,
    ContinuationFrame,
    AuthResponse
  };

  enum class LbtContext : uint8_t
  {
    Bypass,
    Normal,
    AuthResponse
  };

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
  uint8_t mRxRawLen = 0; // length of the last raw frame read into mRxBuffer
  uint32_t mRxParseFailCount;

  // 1W repeat transmission state
  uint8_t mTx1WRepeatRemaining = 0; // remaining 1W repeats (0 = done)
  uint32_t mTx1WRepeatTimer = 0;    // millis timestamp for next repeat
  bool mTx1WHopFrequencies = false; // queued 1W commands hop channels per repeat

  // TX timing diagnostics/guard. Long io-homecontrol preambles can exceed the
  // generic 500 ms TX timeout on SX1276, especially for 1W learn/key frames.
  uint16_t mCurrentTxPreambleSymbols = IOHC_PREAMBLE_SHORT;

  // Live IRQ/FIFO trace for the 1W key transfer (0x30) TX (PairDiag only).
  // Edge-logs the SX1276 FSK IRQ + OpMode registers while waiting for PacketSent
  // so a stalled 0x30 (FIFO never drains) can be told apart from a clean TX.
  uint32_t mPairDiag1WTxPollTimer = 0;    // millis of last poll sample (0 = none yet)
  uint16_t mPairDiag1WTxLastIrq = 0xFFFF; // last sampled (irq1<<8)|irq2, 0xFFFF = none
  uint8_t mPairDiag1WTxSampleCount = 0;   // logged edge samples this TX (capped)

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
  // Optional 2W target supplied by the caller. Discovery is broadcast, but a
  // response must not bind this pairing transaction to another learn-mode device.
  uint32_t mPairingKnownNodeId;
  uint8_t mPairKeyExchangeAttempts;
  uint32_t mPairKeyExchangeStartTime;
  uint8_t mPairingFreqIdx;
  uint8_t mDiscoverySweep; // diagnostic discovery: current full-sweep attempt (0-based)
  uint32_t mPairingStartTime;
  DiscoverySendPhase mDiscoverySendPhase;
  DiscoveryTimingTrace mDiscoveryTimingTrace;
  bool mDiscoverySPE; // encrypted discovery mode
  // Standard (non-encrypted) discovery mirrors a TaHoma box, which broadcasts
  // both the classic DiscoverRequest (0x28 -> 0x00003B) and the alternative
  // Discover2ERequest (0x2E -> 0x00003F) per frequency. This flag selects the
  // 0x2E frame for the second transmit within one frequency step.
  bool mDiscoveryAltFrame = false;
  uint8_t mPairSetConfigChallenge[6];
  uint8_t mPairKeyTransferChallenge[6];
  IoHomeFrame mPairLaunchKeyTransferFrame;
  IoHomeFrame mPairPulledKeyFrame;
  uint8_t mPairPulledKey[16];
  uint8_t mPairPullAuthChallenge[6];
  uint8_t mPairing1WStage = 0; // 0=announce(0x2E), 1=add/send-key(0x30), 2=remove(0x39)
  Pairing1WMode mRequestedPairing1WMode = Pairing1WMode::RemoveAdd;
  Pairing1WMode mPairing1WMode = Pairing1WMode::RemoveAdd;
  uint8_t mPairing1WBroadcastType = 0;
  uint8_t mDefault1WBroadcastType = 0;
  Pairing2WMode mPairing2WMode = Pairing2WMode::Normal;
  bool mPairing2WExperimental = false; // true only when entered via explicit pair2w-exp diagnostic command

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

  // Active key extraction (device-role responder)
  bool mKeyExtractArmed;
  uint32_t mKeyExtractThrowawayId;
  uint8_t mKeyExtractChallenge[6];
  uint32_t mKeyExtractHubNodeId;
  uint8_t mKeyExtractKey[16];
  ControllerState mKeyExtractState;
  uint32_t mKeyExtractArmedAt;
  uint32_t mKeyExtractTimeoutMs;
  uint32_t mKeyExtractGraceDeadlineMs = 0;
  KeyExtractStatus mKeyExtractStatus;
  PassiveKeyResult mKeyExtractResult;
  uint8_t mKeyExtractReplyBuffer[IOHC_FRAME_BUFFER_SIZE] = {};
  uint8_t mKeyExtractReplyLen = 0;
  uint8_t mKeyExtractReplyPhase = 0;
  uint16_t mKeyExtractReplyPreamble = IOHC_PREAMBLE_SHORT;

  // 1W key copy/clone receive state
  bool mOneWayKeyReceiveActive = false;
  uint8_t mOneWayKeyReceiveChannel = 0xFF;
  uint32_t mOneWayKeyReceiveStartedAt = 0;
  uint32_t mOneWayKeyReceiveTimeoutMs = 0;
  uint32_t mOneWayKeyReceiveCapturedNode = 0;
  OneWayKeyReceiveStatus mOneWayKeyReceiveStatus = OneWayKeyReceiveStatus::Idle;

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
  uint32_t mLbtBusyCount;
  uint32_t mLbtBypassCount;
  uint32_t mLbtClearCount;
  uint32_t mLbtInvalidRssiCount;
  int16_t mLastLbtRssi;
  bool mLastLbtRssiValid;
  uint8_t mLastLbtAttempts;
  bool mLastLbtBypassed;
  bool mLastLbtAuthResponse;

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
  void tracePairDiagnosticCompactPair() const;
  void tracePairDiagnosticCompactRx(const IoHomeRadioHealth &iHealth) const;
  void tracePairDiagnosticTx2W(const IoHomeFrame &iFrame, uint16_t iPreambleSymbols) const;
  void trace1WRepeatPlan(const char *iContext) const;
  bool createAndTraceHmac1W(const uint8_t *iTranscript, uint8_t iTranscriptLen,
                            uint16_t iSequenceNum, const uint8_t iControllerKey[16],
                            uint8_t oHmac[IOHC_HMAC_SIZE]) const;
  uint16_t nextSequence1W(IoHomecontrolChannel *iProfile, bool iForceFlashSave);
  void tracePairDiagnosticFrame(const char *iPrefix, const IoHomeFrame &iFrame, uint8_t iFreqIdx, int16_t iRssi) const;
  void tracePairDiagnosticDiscoveryInterpretation(const IoHomeFrame &iFrame, uint8_t iFreqIdx) const;
  void processIdle();
  void processTxPending();
  void processTxInProgress();
  void processTx1WRepeat();
  void processWaitResponse();
  void processResponse();
  bool startPairingInternal(uint8_t iChannelIndex,
                            uint32_t iKnownNodeId,
                            Pairing2WMode iMode,
                            bool iExplicitDiagnosticMode);

  void processPairSendDiscovery();
  void processPairWaitDiscoveryResponse();
  void processPairSendDiscoveryConfirmation();
  void processPairWaitDiscoveryConfirmationAck();
  void processPairSendLaunchKeyTransfer();
  void processPairWaitLaunchKeyTransfer();
  void processPairSendPullKeyChallenge();
  void processPairWaitPullKeyChallengeResponse();
  void processPairSend1WAnnounce();
  void processPairWait1WAnnounce();
  void processPairSend1WRemove();
  void processPairWait1WRemove();
  void processPairSend1WKeyTransfer();
  void processPairWait1WKeyTransfer();
  void processPairSendKeyInit();
  void processPairWaitDeviceChallenge();
  void processPairSendKeyTransfer();
  void processPairSendKeyTransferAuthResponse();
  void processPairWaitKeyTransferConfirmation();
  bool retry2WKeyExchange();
  void processPairSendSetConfig1();
  void processPairWaitSetConfig1Response();
  void processPairSendSetConfig1AuthResponse();
  void processPairWaitSetConfig1FinalResponse();
  void interpretSetConfig1Result(bool iFinalResponse);
  // Store the system key into the paired channel and advance to SetConfig1.
  // Shared by the normal 0x33 confirmation path and the early-confirm path
  // where a device skips its 0x3C challenge and confirms the key directly.
  void finalize2WPairingKey();
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
  void handleOneWayKeyReceiveFrame();

  // Active key extraction handlers
  void processKeyExtractFrame();
  bool queueKeyExtractReply(const uint8_t *iBuffer, uint8_t iLen, uint16_t iPreambleSymbols);
  void serviceKeyExtractReply();
  void extendKeyExtractGrace();
  void resetKeyExtractSessionState();
  uint32_t generateKeyExtractNodeId() const;

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
  void recordScanFrame(const IoHomeFrame &iFrame, const uint8_t *iRaw, uint8_t iRawLen, int16_t iRssi, uint8_t iFreqIdx);
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
  uint16_t preambleForFrame(const IoHomeFrame &iFrame, TxContext iContext) const;
  bool radioIsSX1262() const;
  RadioError configureTxRadio(uint16_t iPreambleSymbols, const uint32_t *iFrequencyHz = nullptr);
  RadioError configureNormal2WTxRadio(uint16_t iPreambleSymbols);
  void serviceRxScan();
  bool waitForLbtClear(LbtContext iContext);
  RadioError startRadioTransmit(const uint8_t *iBuffer, uint8_t iLen, LbtContext iLbtContext);
  RadioError startTransmitWithPreamble(const uint8_t *iBuffer, uint8_t iLen,
                                       uint16_t iPreambleSymbols,
                                       bool iTrackDutyCycle = false,
                                       LbtContext iLbtContext = LbtContext::Normal);
  RadioError startShortPreambleTransmit(const uint8_t *iBuffer, uint8_t iLen,
                                        bool iTrackDutyCycle = false,
                                        LbtContext iLbtContext = LbtContext::Normal);
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
  uint8_t channelIndexFor(IoHomecontrolChannel *iChannel) const;
  IoHomecontrolChannel *channelForQueueEntry(const IoHomeQueueEntry &iEntry) const;
  bool resolveLowPower2W(uint32_t iNodeId) const;
  IoHomecontrolChannel *oneWayProfileForNode(uint32_t iNodeId) const;
  uint8_t oneWayBroadcastTypeForNode(uint32_t iNodeId) const;
  uint32_t oneWayDestinationForEntry(const IoHomeQueueEntry &iEntry) const;
};
