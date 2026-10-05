#include "../protocol/IoHomeManagementCodecs.h"
#include "../protocol/IoHomeObjectTransfer.h"
#include "../protocol/IoHomeDurableReservation.h"
#pragma once
#include "../protocol/IoHomeRadioPolicy.h"
#include "../protocol/IoHomeTransactionTiming.h"
#include "../protocol/IoHomeSessionPolicy.h"
#include "../protocol/IoHomeResponseDescriptor.h"
#include "../radio/Radio.h"
#include "../protocol/IoHomeFrame.h"
#include "../protocol/IoHomeCrypto.h"
#include "../protocol/IoHomeCommands.h"
#include "../protocol/IoHomeProfileRegistry.h"
#include <stdint.h>
#include <string>

#define IOHC_CMD_QUEUE_SIZE 8
#define IOHC_EXCHANGE_MAX_ATTEMPTS 3
#define IOHC_MAX_RETRIES (IOHC_EXCHANGE_MAX_ATTEMPTS - 1)
#define IOHC_TX_TIMEOUT_MS 500
#define IOHC_RX_TIMEOUT_MS 400
#define IOHC_RX_FINAL_TIMEOUT_MS 500
#define IOHC_RETRY_GAP_MS 250
#define IOHC_UNCONFIRMED_EXECUTE_RETRY_GAP_MS 750
#define IOHC_EXCHANGE_TOTAL_BUDGET_MS 5000
#ifndef IOHC_PAIR_KEY_EXCHANGE_MAX_ATTEMPTS
#define IOHC_PAIR_KEY_EXCHANGE_MAX_ATTEMPTS 3
#endif
#define IOHC_PAIR_KEY_EXCHANGE_TIMEOUT_MS 15000
#define IOHC_PAIR_DISCOVER_CONFIRM_MAX_ATTEMPTS 3
#define IOHC_PAIR_DISCOVER_CONFIRM_TIMEOUT_MS 1500
#define IOHC_PAIR_KEY_INIT_DELAY_DEFAULT_MS 300
#define IOHC_AUTH_DWELL_MS_SX1262 90
#define IOHC_AUTH_PREAMBLE_SX1262 64
#define IOHC_PAIR_TIMEOUT_MS 45000
// Diagnostic discovery sweep: listen window per frequency and how many full
// frequency sweeps a single discovery broadcast performs before giving up.
// The extended window adds a short grace period so a frame already arriving at
// the window boundary is not truncated by hopping to the next frequency.
#define IOHC_DISCOVERY_LISTEN_MS 1353
#define IOHC_DISCOVERY_LISTEN_EXTENDED_MS 1403
#define IOHC_DISCOVERY_ARRIVAL_GRACE_MS 50
#define IOHC_DISCOVERY_MAX_SWEEPS 3
#define IOHC_DUTY_CYCLE_WINDOW_MS 3600000 // 1 hour
#define IOHC_LBT_RSSI_THRESHOLD_DBM -90   // clear channel threshold before TX
#define IOHC_LBT_MAX_RETRIES 5            // normal TX: 5 * 5ms worst-case
#define IOHC_LBT_AUTH_MAX_RETRIES 1       // auth responses must not be delayed too long
#define IOHC_LBT_RETRY_DELAY_MS 5
// Measured per-channel discovery/scan dwell defaults. SX1276 can use its
// FastHop path; SX1262 needs a longer standby -> retune -> RX allowance.
#define IOHC_RX_SCAN_INTERVAL_US_SX1276 5000
#define IOHC_RX_SCAN_INTERVAL_US_SX1262 7000
#if defined(RADIO_SX1262)
#define IOHC_RX_SCAN_INTERVAL_US IOHC_RX_SCAN_INTERVAL_US_SX1262
#else
#define IOHC_RX_SCAN_INTERVAL_US IOHC_RX_SCAN_INTERVAL_US_SX1276
#endif
// Passive correlation window after a received UNKNOWN_86 frame. Any frame
// exchanged between the same two nodes inside this window is logged verbatim so
// a possible request/response pairing can be established from real captures.
#define IOHC_UNKNOWN86_CORRELATION_WINDOW_MS 500
// Maximum raw Execute payload bytes before appending the 1W sequence number.
// Normal 1W authenticated frames include 6-byte HMAC in CTRL0 length, so keep
// 9(header) + raw + 2(seq) + 6(hmac) <= IOHC_FRAME_BUFFER_SIZE.
#define IOHC_1W_RAW_EXEC_MAX_DATA (IOHC_FRAME_BUFFER_SIZE - 9 - 2 - IOHC_HMAC_SIZE)
#define IOHC_2W_RAW_EXEC_SHORT_LEN 6
#define IOHC_2W_RAW_EXEC_EXTENDED_LEN 8
#define IOHC_2W_RAW_EXEC_MAX_DATA IOHC_2W_RAW_EXEC_EXTENDED_LEN

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

enum class TwoWayWakeBeliefUse : uint8_t
{
  NotLowPower = 0,
  ExplicitOverride,
  Disabled,
  NoChannel,
  Applied,
};

struct TwoWayPreamblePlan
{
  bool valid = false;
  TwoWayWakeBeliefUse use = TwoWayWakeBeliefUse::NotLowPower;
  TwoWayWakeBelief belief = TwoWayWakeBelief::Asleep;
  uint16_t normalPreamble = IOHC_PREAMBLE_NORMAL_START;
  uint16_t fixedPreamble = IOHC_PREAMBLE_SHORT;
  bool hasLastHeard = false;
  uint32_t lastHeardAgeMs = 0;
};

struct OneWayCopyShape
{
  uint16_t preamble;
  bool lowPower;
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

enum class TwoWayRetryReason : uint8_t
{
  Initial = 0,
  NoResponse = 1,
  NoClosingReply = 2,
};

// Queued command entry
struct IoHomeQueueEntry
{
  uint32_t destNodeId;
  uint32_t observationGeneration;
  uint32_t productContextRevision;
  uint32_t objectReadToken;
  uint8_t objectReadData[9];
  uint8_t objectReadLength;
  const uint8_t *encKey; // pointer to channel's key (valid as long as channel exists)
  IoHomeCommand command;
  uint8_t param;
  uint16_t param2;           // second parameter; widened for Cozy LE16 temperature, 0xFF = unused elsewhere
  uint8_t param3;            // third parameter (for _p0x00_16 extended format); 0xFF = unused
  bool oneWayButton;         // true: 1W button-style Execute command
  uint16_t oneWayButtonCode; // 0x0000=up, 0x0001=down, 0x0002=stop, 0x0003=my/prog, 0x00FE=release, 0x00FF=stop2
  bool oneWayRawExecute;     // true: send exact raw Execute payload bytes before sequence/HMAC
  uint8_t oneWayRawData[IOHC_1W_RAW_EXEC_MAX_DATA];
  uint8_t oneWayRawLen;
  bool twoWayRawExecute;     // true: send exact 6/8-byte 2W Execute payload
  uint8_t twoWayRawData[IOHC_2W_RAW_EXEC_MAX_DATA];
  uint8_t twoWayRawLen;
  bool privateProbe;
  PrivateProbeShape privateProbeShape;
  uint8_t privateProbeFunction;
  uint8_t privateProbeValue;
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
  uint8_t twoWayTxFreqIdx;                     // 0xFF uses the normal 2W command channel
  uint8_t productActivation[10];
  uint8_t productActivationLength;
  uint8_t productActivationFamily;
  bool twoWayMovementFp;                       // MP movement with the selected speed FP
  bool twoWayFp;                               // profile-selected functional parameter
  bool managementRead;                        // key snapshot for correlated management reads
  uint8_t managementKey[16];
  uint32_t sensorSubscriptionBackbone;        // nonzero only for explicit default write
  bool mpFpRead;                              // source-backed standard/context GET
  uint16_t mpFpReadSelected;                  // logical FP mask; all requested fields required
  uint8_t mpFpReadMode;                       // 3=standard, 6/7=default min/max, 9=current alias
  bool diagnosticFpRead;                       // raw-only, no KO publication
  uint8_t diagnosticFpReadIndex;
  uint8_t twoWayFpIndex;
  uint16_t twoWayFpRaw;
  uint8_t retries;
  uint8_t maxAttempts;
  uint8_t mediaAttempts;
  IoHomeSessionPolicy sessionPolicy;
  uint8_t authenticatedUnconfirmedTries;
  bool hadAuthenticatedAccept;
  TwoWayRetryReason retryReason;
  bool previousChallengeSeen;
  bool previousChallengeResponseSent;
  bool previousFinalResponseSeen;
  bool previousStatusSeen;
  TwoWayPreamblePlan twoWayPreamblePlan;
  bool background;
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

  // 2W pairing states. The 0x2C/0x2D discovery confirmation is part of the
  // normal flow; 0x38 and pull-key remain explicit research modes.
  // Normal 2W pairing follows:
  //   DiscoverRequest(0x28)
  //   -> DiscoverResponse(0x29)
  //   -> Confirmation(0x2C) -> optional ConfirmationACK(0x2D)
  //   -> configurable non-blocking delay -> KeyInitTransfer(0x31)
  //   -> ChallengeRequest(0x3C)
  //   -> KeyTransfer(0x32)
  //   -> KeyTransferConfirmation(0x33/0x2D)
  //   -> optional GetName/GI1/GI2/GI3 enrichment (0x50/0x54/0x56/0x58)
  //   -> optional SetConfig1(0x6F)
  //
  PairSendDiscoveryConfirmation,
  PairWaitDiscoveryConfirmationAck,
  PairWaitKeyInitDelay,
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
  PairSend1WFinalizerStop,
  PairWait1WFinalizerStop,
  PairWait1WFinalizerGap,
  PairSend1WFinalizerDown,
  PairWait1WFinalizerDown,
  PairSendKeyInit,
  PairWaitDeviceChallenge,
  PairSendKeyTransfer,
  PairSendKeyTransferAuthResponse,
  PairWaitKeyTransferConfirmation,
  PairSendEnrichment,
  PairWaitEnrichment,
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
  ExtractSentNodeVerifyResp,

  // Fake gateway mode (respond to device-initiated pairing)
  GatewayIdle,
  GatewayWaitDiscoveryResponse,
  GatewayWaitKeyTransfer,
  GatewayWaitChallenge
};

enum class Pairing2WMode : uint8_t
{
  Normal = 0,                // default: 0x28 -> 0x29 -> 0x2C -> 0x2D? -> 0x31 -> 0x32 -> 0x33
  DiscoveryConfirmation = 1, // diagnostic alias for the normal confirmation step
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
    // 2 was Missing1WTarget. A 1W controller commands typed broadcasts and
    // needs no target node, so the value is retired but not reused.
    Failed = 3
  };

  // Outcome of the most recent pairing attempt. Optional post-pair status
  // configuration is reported separately and never turns a stored key into a
  // failed pairing result.
  enum class PairingOutcome : uint8_t
  {
    None = 0,
    InProgress = 1,
    Success = 2,
    NoResponse = 3,
    InvalidResponse = 4,
    KeyExchangeFailure = 5,
    ConfigurationFailure = 6,
    Cancelled = 7,
    StartRejected = 8,
    // 1W has no return path: a completed enrollment burst only proves that the
    // frames left the radio, never that the actuator stored the controller.
    OneWayEnrollmentTransmitted = 9
  };

  enum class PairingOptionalConfigResult : uint8_t
  {
    NotAttempted = 0,
    Accepted = 1,
    Rejected = 2,
    NoReply = 3,
    TxFailure = 4,
  };

  struct PairingTelemetry
  {
    PairingOutcome outcome = PairingOutcome::None;
    PairingOutcome diagnostic = PairingOutcome::None;
    uint8_t channel = 0;
    uint32_t peerNodeId = 0;
    uint8_t lastReceivedCommand = 0xFF;
    uint8_t rejectedFrames = 0;
    uint8_t keyExchangeAttempts = 0;
    enum class DiscoverConfirmResult : uint8_t
    {
      NotRun = 0,
      Skipped = 1,
      Acknowledged = 2,
      ErrorResponse = 3,
      NoReply = 4,
    } discoverConfirmResult = DiscoverConfirmResult::NotRun;
    uint8_t discoverConfirmAttempts = 0;
    PairingOptionalConfigResult optionalConfig = PairingOptionalConfigResult::NotAttempted;
  };

  struct ExchangeRadioSnapshot
  {
    bool valid = false;
    uint32_t nodeId = 0;
    IoHomeCommand command = IoHomeCommand::Execute;
    uint8_t attempt = 0;
    bool sawChallenge = false;
    uint32_t frequencyHz = 0;
    bool rxDone = false;
    bool crcError = false;
    bool preambleDetected = false;
    bool syncDetected = false;
    uint16_t lastIrq = 0;
    uint8_t lastLength = 0;
    int16_t rssi = 0;
    uint16_t preamble = 0;
    TwoWayWakeBeliefUse wakeBeliefUse = TwoWayWakeBeliefUse::NotLowPower;
    TwoWayWakeBelief wakeBelief = TwoWayWakeBelief::Asleep;
    bool hasLastHeard = false;
    uint32_t lastHeardAgeMs = 0;
  };

  struct ExchangeDiagnostics
  {
    uint16_t timeoutCount = 0;
    uint16_t unconfirmedCount = 0;
    ExchangeRadioSnapshot lastUnconfirmed{};
  };

  struct ResponseTimingSample
  {
    bool valid = false;
    bool hasFirstResponse = false;
    bool hasFinalResponse = false;
    uint32_t ioAddress = 0;
    IoHomeCommand command = IoHomeCommand::Execute;
    uint8_t manufacturerId = 0;
    uint16_t profile = 0;
    uint8_t subProfile = 0;
    uint8_t powerSaveModeRaw = 0xFF;
    uint8_t responseTimeClass = 0xFF;
    uint16_t selectedTimeoutMs = 0;
    uint8_t timeoutGroup = 0;
    bool timeoutFallback = true;
    uint8_t sessionMode = 0, stateRetries = 0, wholeSessionRetries = 0, mediaRetries = 0;
    uint8_t peerResult[IOHC_FRAME_MAX_DATA]{};
    uint8_t peerResultLength = 0;
    IoHomeResponseDisposition disposition = IoHomeResponseDisposition::Ignore;
    uint32_t txEndToFirstResponseUs = 0;
    uint32_t txEndToFinalResponseUs = 0;
  };

  struct DiagnosticFpSample
  {
    bool valid = false;
    uint32_t ioAddress = 0;
    uint16_t packedProfile = 0xFFFF;
    uint8_t manufacturerId = 0xFF;
    uint8_t fpIndex = 0;
    uint8_t fpi1 = 0;
    uint8_t fpi2 = 0;
    uint8_t payloadLength = 0;
    uint16_t raw = 0;
    RawParameterValueKind kind = RawParameterValueKind::Unknown;
  };

  enum class OneWayEnrollPhase : uint8_t
  {
    Remove,
    Add,
    FinalizeStop,
    FinalizeDown,
    Complete,
    Failed,
  };

  // Vendor-specific 1W enrollment behavior. Keeping it in a table prevents
  // profile-exact frame details from leaking into the generic 1W builders.
  struct OneWayPairingProfile
  {
    const char *name;
    uint32_t removeDestination; // 0 = derive from the configured broadcast type
    uint32_t finalizerDestination;
    const IoHomeDeviceType *addClasses; // nullptr = derive from the broadcast type
    uint8_t addClassCount;
    uint32_t fixedAddDestination; // 0 = class sweep or configured broadcast type
  };

  enum class OneWayPairingProfileId : uint8_t { Generic, VeluxKli, SomfyRemote };

  struct OneWayPairingDestinationPreview
  {
    const char *profileName;
    uint32_t removeDestination;
    uint32_t firstAddDestination;
    uint8_t addDestinationCount;
  };

  static const OneWayPairingProfile &oneWayPairingProfileGeneric();
  static const OneWayPairingProfile &oneWayPairingProfileVeluxKli();
  static const OneWayPairingProfile &oneWayPairingProfileSomfy();
  OneWayPairingDestinationPreview oneWayPairingDestinationPreview(IoHomecontrolChannel *iChannel) const;

  struct OneWayEnrollmentTraceEntry
  {
    OneWayEnrollPhase phase = OneWayEnrollPhase::Failed;
    uint16_t sequence = 0;
    uint32_t source = 0;
    uint32_t destination = 0;
    uint32_t timestampMs = 0;
    uint32_t elapsedMs = 0;
    bool txSuccess = false;
    bool valid = false;
  };

  static constexpr uint8_t kOneWayEnrollmentTraceSize = 8;

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
                   IoHomeCommand iCmd, uint8_t iParam, uint16_t iParam2);
  bool sendCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                   IoHomeCommand iCmd, uint8_t iParam, uint16_t iParam2, uint8_t iParam3);
  bool sendCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                   IoHomeCommand iCmd, uint8_t iParam, uint16_t iParam2, uint8_t iParam3,
                   uint8_t iMaxAttempts);
  bool sendBackgroundCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                             IoHomeCommand iCmd, uint8_t iParam,
                             uint16_t iParam2 = 0xFF, uint8_t iParam3 = 0xFF,
                             uint8_t iMaxAttempts = 0);
  bool verifyKnownNetworkNode(uint32_t iNodeId, const uint8_t *iKey,
                              uint8_t iFrequencyIndex);
  uint16_t normal2WStartPreamble() const;
  static uint32_t estimatedTxAirtimeMs(uint8_t iFrameLen, uint16_t iPreambleSymbols);

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

  // Queue an exact diagnostic 2W Execute payload through the normal exchange
  // engine. Only the payload is raw; addressing, power class, preamble,
  // retries, challenge-response authentication, and result handling remain
  // the same as for ordinary registered-device commands.
  bool sendRawTwoWayExecute(IoHomecontrolChannel *iChannel,
                            const uint8_t *iPayload, uint8_t iPayloadLen,
                            bool iSingleAttempt = false);
  static bool buildRawTwoWayExecuteFrame(IoHomeFrame &oFrame,
                                         uint32_t iSrcNodeId,
                                         uint32_t iDestNodeId,
                                         bool iLowPower,
                                         const uint8_t *iPayload,
                                         uint8_t iPayloadLen);

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
  bool requestProductRgb(IoHomecontrolChannel *channel,uint8_t red,uint8_t green,uint8_t blue);
  bool requestProductWhite(IoHomecontrolChannel *channel,uint16_t kelvin);
  bool requestMpFpContext(IoHomecontrolChannel *channel,uint8_t mode);
  bool requestMpFpRead(IoHomecontrolChannel *channel,uint8_t index);
  bool requestProfileParameterRead(IoHomecontrolChannel *channel);
  bool requestMpFpMaskRead(IoHomecontrolChannel *channel,uint16_t selected);
  bool sendDiagnosticFpRead(IoHomecontrolChannel *iChannel, uint8_t iFpIndex);
  bool requestObjectRead(IoHomecontrolChannel *channel,uint8_t provider,uint16_t key,uint16_t offset,uint16_t span);
  bool cancelObjectRead(uint32_t token);
  const IoHomeObjectTransfer &objectRead() const {return mObjectRead;}
  uint32_t objectReadToken() const {return mObjectReadToken;}
  uint32_t objectReadPeer() const {return mObjectReadPeer;}
  uint8_t objectReadChannel() const {return mObjectReadChannel;}
  bool objectReadIdentityValid() const;
  bool requestPriority(IoHomecontrolChannel *channel,uint8_t priority);
  bool requestSensorStatus(IoHomecontrolChannel *channel);
  bool requestSensorInformation(IoHomecontrolChannel *channel);
  bool requestDefaultSensorSubscription(IoHomecontrolChannel *channel,uint32_t backbone);
  struct SensorMonitor {
    bool active=false;uint32_t node=0,contextRevision=0,startedMs=0,lastQueuedMs=0,intervalMs=0,durationMs=0,requests=0;
    uint8_t key[16]{};
  };
  bool startSensorMonitor(IoHomecontrolChannel *channel,uint32_t intervalSeconds,uint32_t durationSeconds);
  bool stopSensorMonitor(uint8_t channel);
  const SensorMonitor *sensorMonitor(uint8_t channel) const;
  struct PrioritySample {bool valid=false,refreshArmed=false;uint32_t node=0,receivedMs=0;IoHomePriorityState state;uint8_t key[16]{};uint32_t contextRevision=0;};
  struct SensorSample {bool valid=false;uint32_t node=0,receivedMs=0;IoHomeSensorStatus state;uint8_t key[16]{};uint32_t contextRevision=0;};
  struct SensorInformationSample {bool valid=false;uint32_t node=0,receivedMs=0;IoHomeSensorInformation state;uint8_t key[16]{},raw[17]{};uint32_t contextRevision=0;};
  const PrioritySample *prioritySample(uint8_t channel,uint8_t priority) const;
  const SensorSample *sensorSample(uint8_t channel) const;
  const SensorInformationSample *sensorInformationSample(uint8_t channel) const;
  bool sendPrivateProbe(uint32_t iDestNodeId, const uint8_t *iEncKey,
                        PrivateProbeShape iShape, uint8_t iFunctionId,
                        uint8_t iSelectorOrBlock = 0);
  static bool buildTwoWayPrivateProbePayload(uint8_t *oData, uint8_t &oLen,
                                             PrivateProbeShape iShape,
                                             uint8_t iFunctionId,
                                             uint8_t iSelectorOrBlock);
  static bool decodeStatusUpdateOriginator(const IoHomeFrame &iFrame, uint8_t &oOriginator);
  bool sendTiltCommand(uint32_t iDestNodeId, const uint8_t *iEncKey, uint8_t iTiltPercent);
  bool sendProfileMovementCommand(uint32_t node, const uint8_t *key, uint8_t position, uint8_t speedIndex, uint16_t raw);
  bool sendProfileParameterCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                   ParameterSemantic iSemantic, uint8_t iPercent);

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

  // Resolve and build discovery-family frames without coupling CTRL1 flags to
  // the preamble.  The defaults preserve generic cold-pairing behavior for
  // 0x28 and model the captured KLR300 shapes for 0x2E and authenticated SPE.
  static TwoWayDiscoveryFrameOptions referenceTwoWayDiscoveryOptions(IoHomeCommand iCommand);
  static TwoWayDiscoveryFrameOptions klr300TwoWayDiscoveryOptions(IoHomeCommand iCommand);
  static TwoWayDiscoverySettings mergeTwoWayDiscoverySettings(
      const TwoWayDiscoverySettings &iBase,
      const TwoWayDiscoverySettings &iOverride);
  TwoWayDiscoveryFrameOptions resolveTwoWayDiscoveryOptions(
      IoHomeCommand iRequestedCommand,
      const TwoWayDiscoverySettings &iSettings) const;
  static bool buildTwoWayDiscoveryFrame(IoHomeFrame &oFrame,
                                        uint32_t iSrcNodeId,
                                        const TwoWayDiscoveryFrameOptions &iOptions,
                                        const uint8_t iSystemKey[16] = nullptr,
                                        const uint8_t iChallenge[6] = nullptr);
  static bool buildTwoWayDiscoveryConfirmationFrame(
      IoHomeFrame &oFrame, uint32_t iSrcNodeId, uint32_t iDestNodeId,
      bool iLowPower, PairingDiscoverConfirmMode iMode);

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
  static constexpr uint32_t kPairEnrichmentStepTimeoutMs = 2000UL;

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
  static constexpr uint32_t kKeyExtractMidAttemptHoldMs = 5000UL;
  static constexpr uint32_t kKeyExtractPostExtractGraceMs = 60000UL;

  bool startKeyExtraction(uint32_t iTimeoutMs = kKeyExtractDefaultTimeoutMs);
  void stopKeyExtraction();
  void clearKeyExtractResult();
  KeyExtractStatus keyExtractStatus() const;
  const PassiveKeyResult &keyExtractResult() const;
  bool isKeyExtractionActive() const;
  bool keyExtractAwaitingReply() const;
  uint32_t keyExtractControllerNodeId() const;
  uint32_t keyExtractCandidateHubNodeId() const;
  uint32_t keyExtractHubNodeId() const;
  bool keyExtractKeyCaptured() const;
  bool keyExtractNodeVerificationSeen() const;
  bool keyExtractNodeVerificationAuthDone() const;
  ControllerState keyExtractState() const;
  uint32_t keyExtractHoldRemainingMs() const;
  void setKeyExtractColdReplyPreamble(uint16_t iPreambleSymbols);
  void setKeyExtractResponsePreamble(uint16_t iPreambleSymbols);
  uint16_t keyExtractColdReplyPreambleOverride() const;
  uint16_t keyExtractResponsePreambleOverride() const;
  uint16_t keyExtractColdReplyPreamble() const;
  uint16_t keyExtractResponsePreamble() const;

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
  bool setDiagnostic2WFrameVersion(uint8_t iVersion); // 0=auto, 3=bench override
  uint8_t diagnostic2WFrameVersion() const { return mDiagnostic2WFrameVersion; }
  void setDiagnostic2WPowerClass(TwoWayPowerClass iPowerClass);
  TwoWayPowerClass diagnostic2WPowerClass() const;
  void setDiagnostic2WStartPreamble(uint16_t iPreambleSymbols);
  uint16_t diagnostic2WStartPreamble() const;
  void setDiagnostic2WWakeBelief(bool iEnabled);
  bool diagnostic2WWakeBelief() const;
  void setDiagnosticDiscoverySettings(const TwoWayDiscoverySettings &iSettings);
  const TwoWayDiscoverySettings &diagnosticDiscoverySettings() const;
  bool setDiagnosticDiscoveryListenMs(uint16_t iMilliseconds);
  uint16_t diagnosticDiscoveryListenMs() const;
  static const char *stateName(ControllerState iState);
  static const char *pairingOutcomeName(PairingOutcome iOutcome);
  const PairingTelemetry &pairingTelemetry() const;
  const ExchangeDiagnostics &exchangeDiagnostics() const;
  const ResponseTimingSample &lastResponseTimingSample() const;
  const DiagnosticFpSample &lastDiagnosticFpSample() const { return mLastDiagnosticFpSample; }
#ifdef TEST_NATIVE
  void testFailReservationWrites(bool iFail) { mReservationJournal.failWrites=iFail; }
  void testSetTrustRxPosition(bool iTrust) { mTrustRxPosition = iTrust; }
#endif
  enum class OneWayRecovery : uint8_t { Ready, OwnerUnavailable, JournalCorrupt, StoreUnavailable, CommitFailed };
  OneWayRecovery oneWayRecovery() const { return mOneWayRecovery; }
  bool oneWayRecoveryRequired() const { return mReservationFailed; }
  void logPairDiagnosticStatus() const;
  const OneWayEnrollmentTraceEntry *oneWayEnrollmentTrace() const;
  uint8_t oneWayEnrollmentTraceCount() const;

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
    // Exact on-air bytes except that 0x30/0x32 payloads are zeroed at capture time.
    uint8_t raw[IOHC_FRAME_BUFFER_SIZE];
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
    // Source-keyed discovery inventory. Both normal 0x29 pairing and
    // layout-compatible 0x2B SPE responses populate this same model.
    IoHomeProtocolIdentity protocolIdentity;
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
  const IoHomeProtocolIdentity *protocolIdentityForIoAddress(uint32_t iIoAddress) const;
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
  // Resolve the capture-backed 1W Execute ACEI for a controller manufacturer.
  static uint8_t oneWayAceiForManufacturer(uint8_t iManufacturer);
  // VELUX keeps the proven short-repeat timing; other identities follow the
  // reference hardware's long preamble on every copy in a 1W burst.
  static uint16_t oneWayRepeatPreambleForManufacturer(uint8_t iManufacturer);
  static OneWayCopyShape oneWayCopyShape(OneWayPowerClass iPowerClass,
                                         uint8_t iManufacturer,
                                         uint8_t iCopyIndex);
  static const char *oneWayPowerClassName(OneWayPowerClass iPowerClass);
  static OneWayEnrollmentFinalizer resolveOneWayEnrollmentFinalizer(
      OneWayEnrollmentFinalizer iConfigured, uint8_t iManufacturer);
  static const char *oneWayEnrollmentFinalizerName(OneWayEnrollmentFinalizer iFinalizer);
  IoHomecontrolChannel *oneWayProfileForChannel(IoHomecontrolChannel *iChannel) const;
  uint8_t effectiveOneWayAcei(IoHomecontrolChannel *iChannel) const;
  OneWayDestinationMode effectiveOneWayDestinationMode(IoHomecontrolChannel *iChannel) const;
  uint8_t effectiveOneWayEnrollmentClassMask(IoHomecontrolChannel *iChannel) const;
  OneWayPowerClass effectiveOneWayPowerClass(IoHomecontrolChannel *iChannel) const;

  // Set pointer to parent module (for channel callbacks)
  void setModule(IoHomecontrol *iModule);

  // Get current state
  IoHomeRadioPolicy &hostRadioPolicy(){return mHostRadioPolicy;}
  bool idleForManagedOperation() const {
    return mState==ControllerState::Idle&&queueEmpty()&&!mCurrentCmd.active&&!mPassiveMode&&!mGatewayMode&&
        !mOneWayKeyReceiveActive&&!mKeyExtractArmed&&!mNetworkScanActive&&!mObjectRead.active();
  }
  ControllerState state() const;

  // Shared production builder for software/bench qualification. Extended
  // form derives from the authenticated working request, not the peer 3C.
  static bool buildControllerChallengeResponse(IoHomeFrame &oFrame,
      uint32_t iSource, uint32_t iDestination, const IoHomeFrame &iWorkingRequest,
      const uint8_t iChallenge[6], const uint8_t iKey[16]);

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
    uint32_t rotations;
    uint32_t preambleHolds;
    uint32_t syncHolds;
    uint32_t firstPreambleUs;
    uint32_t firstSyncUs;
    uint32_t firstPacketUs;
    uint32_t lastPacketUs;
    uint32_t maxLoopUs;
    uint32_t maxRxHotPathUs;
    uint32_t maxProcessingUs;
    uint32_t packets;
  };

  struct PendingDiscoveryResponse
  {
    IoHomeFrame frame;
    uint32_t source;
    uint32_t destination;
    uint32_t receivedAtUs;
    int16_t rssi;
    uint8_t frequencyIndex;
    uint8_t rawLength;
    uint8_t raw[IOHC_FRAME_BUFFER_SIZE];
  };
  static constexpr uint8_t kPendingDiscoveryCapacity = 24;
  PendingDiscoveryResponse mPendingDiscovery[kPendingDiscoveryCapacity]{};
  uint8_t mPendingDiscoveryCount = 0;
  uint8_t mPendingDiscoveryHighWater = 0;
  uint32_t mPendingDiscoveryOverflow = 0;
  uint32_t mPendingDiscoveryOverflowReported = 0;
  uint32_t mDiscoveryResponsesReceived = 0;
  uint32_t mDiscoveryBroadcastsSent = 0;
  uint32_t mDiscoveryDuplicates = 0;
  uint32_t mDiscoveryUniqueNodes = 0;
  uint32_t mDiscoverySeenNodes[kPendingDiscoveryCapacity]{};
  uint32_t mDiscoveryParseFailures = 0;
  uint32_t mDiscoveryStartCrcErrors = 0;
  uint32_t mDiscoveryStartPreambleDetections = 0;
  uint32_t mDiscoveryStartSyncDetections = 0;
  uint32_t mDiscoveryLoopStartedUs = 0;
  bool mDiscoveryMetadataDirty = false;
  void enqueueDiscoveryResponse();
  void processPendingDiscoveryResponses();

  Radio mRadio;
  IoHomecontrol *mModule;

  // Own identity
  uint32_t mOwnNodeId;
  uint8_t mSystemKey[16];

  // State machine
  ControllerState mState;
  uint32_t mStateTimer;
  uint32_t mNextObservationGeneration=0;
  IoHomeObjectTransfer mObjectRead;
  uint32_t mObjectReadToken=0,mObjectReadPeer=0;
  uint8_t mObjectReadChannel=0xFF,mObjectReadKey[16]{};
  void serviceObjectRead();
  bool enqueueObjectReadPart(bool opening);
  PrioritySample mPrioritySamples[16][8]{};
  SensorInformationSample mSensorInformationSamples[16]{};
  SensorSample mSensorSamples[16]{};
  void servicePriorityRefresh();
  void serviceSensorMonitors();
  SensorMonitor mSensorMonitors[16]{};
  uint8_t mNextSensorMonitor=0;
  uint8_t mCurrentFreqIdx;

  // Command queue (circular buffer)
  IoHomeQueueEntry mCmdQueue[IOHC_CMD_QUEUE_SIZE];
  uint8_t mQueueHead;
  uint8_t mQueueTail;

  // Current command being processed (for retry on timeout)
  IoHomeQueueEntry mCurrentCmd;

  // Current TX frame
  IoHomeDurableReservation mReservationJournal;
  bool mReservationLoaded[16]{};
  uint32_t mReservationNodes[16]{};
  uint16_t mReservationWatermarks[16]{};
  uint8_t mReservationKeys[16][16]{};
  bool mReservationFailed = false;
  OneWayRecovery mOneWayRecovery = OneWayRecovery::Ready;
  RadioError startControllerTransmit(const uint8_t *iData, uint8_t iLength);
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
  uint32_t mExchangeStartMs = 0;
  ExchangeDiagnostics mExchangeDiagnostics{};
  uint32_t mExchangeStartRxDoneCount = 0;
  uint32_t mExchangeStartCrcErrorCount = 0;
  uint32_t mExchangeStartPreambleCount = 0;
  uint32_t mExchangeStartSyncCount = 0;
  uint32_t mExchangeRequestTxEndUs = 0;
  uint32_t mDirectedRequestTxStartUs = 0;
  uint32_t mDirectedRequestTxEndUs = 0;
  uint32_t mDirectedRxReadyUs = 0;
  uint32_t mDirectedFirstPreambleUs = 0;
  uint32_t mDirectedFirstSyncUs = 0;
  bool mDirectedWrongSourceSeen = false;
  bool mDirectedWrongCommandSeen = false;
  uint32_t mExchangeAuthTxEndUs = 0;
  bool mExchangeRequestTxEndValid = false;
  bool mExchangeAuthTxEndValid = false;
  ResponseTimingSample mLastResponseTimingSample{};
  DiagnosticFpSample mLastDiagnosticFpSample{};
  bool mTrustRxPosition = true; // false while dispatching an immediate Execute reply

  // Passive UNKNOWN_86 (0x86) observation. No semantics are assumed; only the
  // raw frame and any traffic between the same node pair are logged.
  bool mUnknown86Pending = false;
  uint32_t mUnknown86Source = 0;
  uint32_t mUnknown86Dest = 0;
  uint32_t mUnknown86ObservedAtMs = 0;
  uint16_t mUnknown86Count = 0;

  // Pairing state
  uint8_t mPairingChannel;
  PairStartStatus mLastPairStartStatus = PairStartStatus::Ok;
  ControllerState mLastPairStartBlockedState = ControllerState::Idle;
  PairingTelemetry mPairingTelemetry{};
  OneWayEnrollmentTraceEntry mOneWayEnrollmentTrace[kOneWayEnrollmentTraceSize]{};
  uint8_t mOneWayEnrollmentTraceCount = 0;
  int8_t mOneWayEnrollmentActiveTrace = -1;
  uint8_t mPairingChallenge[6];
  uint32_t mDiscoveredNodeId;
  IoHomeProtocolIdentity mPairProtocolIdentity{};
  // Optional 2W target supplied by the caller. Discovery is broadcast, but a
  // response must not bind this pairing transaction to another learn-mode device.
  uint32_t mPairingKnownNodeId;
  uint8_t mPairKeyExchangeAttempts;
  uint8_t mPairDiscoverConfirmAttempts = 0;
  PairingDiscoverConfirmMode mPairDiscoverConfirmMode = PairingDiscoverConfirmMode::Send;
  uint16_t mPairKeyInitDelayMs = IOHC_PAIR_KEY_INIT_DELAY_DEFAULT_MS;
  uint16_t mPairDiscoveryTxPreamble = 0;
  uint16_t mPairAcceptedDiscoveryPreamble = 0;
  uint32_t mPairKeyExchangeStartTime;
  uint8_t mPairingFreqIdx;
  uint8_t mDiscoveryLastTxFreqIdx = 0xFF;
  uint8_t mDiscoverySweep; // diagnostic discovery: current full-sweep attempt (0-based)
  uint16_t mDiagnosticDiscoveryListenMs = 0; // zero selects recovered destination/CTRL1 timing
  uint16_t mDiscoveryBudgetMs = IOHC_DISCOVERY_LISTEN_MS;
  bool mDiscoveryRxWindowStarted = false;
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
  enum class PairEnrichmentStep : uint8_t
  {
    Name = 0,
    GeneralInfo1 = 1,
    GeneralInfo2 = 2,
    GeneralInfo3 = 3,
    Complete = 4,
  };
  PairEnrichmentStep mPairEnrichmentStep = PairEnrichmentStep::Name;
  uint8_t mPairKeyTransferChallenge[6];
  IoHomeFrame mPairLaunchKeyTransferFrame;
  IoHomeFrame mPairPulledKeyFrame;
  uint8_t mPairPulledKey[16];
  uint8_t mPairPullAuthChallenge[6];
  uint8_t mPairing1WStage = 0; // 0=announce, 1=add, 2=remove, 3=stop, 4=down
  Pairing1WMode mRequestedPairing1WMode = Pairing1WMode::RemoveAdd;
  Pairing1WMode mPairing1WMode = Pairing1WMode::RemoveAdd;
  uint8_t mPairing1WBroadcastType = 0;
  OneWayEnrollmentFinalizer mPairing1WFinalizer = OneWayEnrollmentFinalizer::None;
  OneWayPairingProfileId mPairing1WProfileId = OneWayPairingProfileId::Generic;
  OneWayEnrollmentDestinationPolicy mPairing1WEnrollmentDestinationPolicy = OneWayEnrollmentDestinationPolicy::Automatic;
  uint8_t mPairing1WEnrollmentClassMask = IOHC_1W_ENROLL_CLASS_ALL;
  uint8_t mPairing1WAddDestinationIndex = 0;
  uint16_t mPairing1WAddSequence = 0;
  uint32_t mPairing1WEnrollmentOpenedAt = 0;
  uint32_t mPairing1WStopStartedAt = 0;
  uint32_t mPairing1WDownStartedAt = 0;
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
  uint32_t mKeyExtractCandidateHubNodeId;
  uint32_t mKeyExtractHubNodeId;
  uint8_t mKeyExtractKey[16];
  bool mKeyExtractKeyCaptured = false;
  bool mKeyExtractNodeVerificationSeen = false;
  bool mKeyExtractNodeVerificationAuthDone = false;
  ControllerState mKeyExtractState;
  uint32_t mKeyExtractArmedAt;
  uint32_t mKeyExtractTimeoutMs;
  uint32_t mKeyExtractGraceDeadlineMs = 0;
  uint32_t mKeyExtractHoldDeadlineMs = 0;
  KeyExtractStatus mKeyExtractStatus;
  PassiveKeyResult mKeyExtractResult;
  uint8_t mKeyExtractReplyBuffer[IOHC_FRAME_BUFFER_SIZE] = {};
  uint8_t mKeyExtractReplyLen = 0;
  uint8_t mKeyExtractReplyPhase = 0;
  uint16_t mKeyExtractReplyPreamble = IOHC_PREAMBLE_SHORT;
  uint16_t mKeyExtractColdReplyPreambleOverride = 0;
  uint16_t mKeyExtractResponsePreambleOverride = 0;

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
  IoHomeRadioPolicy mHostRadioPolicy;
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
  uint8_t mDiagnostic2WFrameVersion = 0; // queued 2W bench override only; never persisted
  TwoWayPowerClass mDiagnostic2WPowerClass = TwoWayPowerClass::Automatic;
  uint16_t mDiagnostic2WStartPreamble = 0; // 0 = derive from effective power class
  bool mDiagnostic2WWakeBelief = true;
  TwoWayDiscoverySettings mDiagnosticDiscoverySettings{};
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
  void trace1WRepeatPlan(const char *iContext, OneWayPowerClass iPowerClass,
                         uint8_t iManufacturer) const;
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
  void processPairWaitKeyInitDelay();
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
  void processPairSend1WFinalizerStop();
  void processPairWait1WFinalizerStop();
  void processPairWait1WFinalizerGap();
  void processPairSend1WFinalizerDown();
  void processPairWait1WFinalizerDown();
  void processPairSendKeyInit();
  void processPairWaitDeviceChallenge();
  void processPairSendKeyTransfer();
  void processPairSendKeyTransferAuthResponse();
  void processPairWaitKeyTransferConfirmation();
  bool retry2WKeyExchange();
  void processPairSendEnrichment();
  void processPairWaitEnrichment();
  void advancePairEnrichment(const char *iResult);
  void recordGeneralInfo3Failure(IoHomeGeneralInfo3Outcome iOutcome,
                                 const uint8_t *iData = nullptr,
                                 uint8_t iDataLen = 0);
  IoHomeCommand pairEnrichmentRequest() const;
  IoHomeCommand pairEnrichmentResponse() const;
  void processPairSendSetConfig1();
  void processPairWaitSetConfig1Response();
  void processPairSendSetConfig1AuthResponse();
  void processPairWaitSetConfig1FinalResponse();
  void interpretSetConfig1Result(bool iFinalResponse);
  void beginPairingTelemetry(uint8_t iChannelIndex, uint32_t iKnownNodeId);
  void recordPairingDiagnostic(PairingOutcome iOutcome, const char *iAction);
  void completePairingTelemetry(PairingOutcome iOutcome);
  void setPairingOptionalConfigResult(PairingOptionalConfigResult iResult);
  void beginPairKeyInitDelay(PairingTelemetry::DiscoverConfirmResult iResult);
  uint16_t pairingStartPreamble(const IoHomeFrame &iFrame) const;
  void resetPairingPreambleState();
  void beginExchangeDiagnosticsWindow();
  void selectDirectedResponseTimeout(uint8_t ctrl1);
  void beginResponseTimingAttempt();
  void markResponseTimingTxEnd();
  void recordResponseTiming(bool iFinalResponse);
  void recordExchangeFailure(const IoHomeQueueEntry &iEntry, bool iAuthenticatedUnconfirmed);
  void notifyCommandExchangeResult(const IoHomeQueueEntry &iEntry,
                                   IoHomeCommandExchangeResult iResult);
  void resetOneWayEnrollmentTrace();
  void observeUnknown86Frame();
  void recordOneWayEnrollmentPhase(OneWayEnrollPhase iPhase, uint16_t iSequence,
                                   uint32_t iDestination);
  void completeOneWayEnrollmentPhase(bool iSuccess);
  void failOneWayEnrollment(const char *iReason);
  void completeOneWayEnrollment();
  bool prepareOneWayEnrollmentExecute(uint16_t iMain, uint16_t iSequence,
                                      OneWayEnrollPhase iPhase);
  uint32_t pairing1WAddDestination() const;
  uint8_t pairing1WAddDestinationCount() const;
  const OneWayPairingProfile &pairing1WProfile() const;
  uint32_t pairing1WRemoveDestination() const;
  uint32_t pairing1WFinalizerDestination() const;
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
  uint16_t keyExtractReplyPreamble(bool iColdReply) const;
  bool queueKeyExtractReply(const uint8_t *iBuffer, uint8_t iLen, uint16_t iPreambleSymbols);
  void serviceKeyExtractReply();
  void serviceKeyExtractChannelHold();
  void holdKeyExtractChannel(uint32_t iDurationMs);
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
  bool managementIdentityMatches(const IoHomeQueueEntry &entry) const;
  bool sampleIdentityMatches(uint8_t channel,uint32_t node,const uint8_t *key,uint32_t contextRevision=0) const;
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
  // Passive/background monitoring may rotate across every IOHC channel. A
  // response wait must select its policy explicitly: broadcast discovery
  // replies rotate off the broadcast request channel; unicast replies hold
  // the channel that carried their request.
  void serviceBackgroundRxScan();
  void serviceBroadcastResponseScan(uint8_t iRequestFrequencyIndex,
                                    TwoWayDiscoveryListenChannels iListenChannels = TwoWayDiscoveryListenChannels::SkipRequest);
  bool waitForLbtClear(LbtContext iContext);
  RadioError startRadioTransmit(const uint8_t *iBuffer, uint8_t iLen, LbtContext iLbtContext);
  RadioError startTransmitWithPreamble(const uint8_t *iBuffer, uint8_t iLen,
                                       uint16_t iPreambleSymbols,
                                       bool iTrackDutyCycle = false,
                                       LbtContext iLbtContext = LbtContext::Normal);
  RadioError startShortPreambleTransmit(const uint8_t *iBuffer, uint8_t iLen,
                                        bool iTrackDutyCycle = false,
                                        LbtContext iLbtContext = LbtContext::Normal);
  OneWayCopyShape queuedOneWayCopyShape(uint8_t iCopyIndex) const;
  OneWayCopyShape pairingOneWayCopyShape(uint8_t iCopyIndex) const;
  OneWayPowerClass pairingOneWayPowerClass() const;
  uint8_t pairingOneWayManufacturer() const;
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
  bool queuePopForeground(IoHomeQueueEntry &oEntry);
  bool queueEmpty() const;
  bool sendCommandInternal(uint32_t iDestNodeId, const uint8_t *iEncKey,
                           IoHomeCommand iCmd, uint8_t iParam, uint16_t iParam2,
                           uint8_t iParam3, uint8_t iMaxAttempts, bool iBackground,
                           uint8_t iFrequencyIndex = 0xFF);
  IoHomecontrolChannel *channelForNode(uint32_t iNodeId) const;
  uint8_t channelIndexFor(IoHomecontrolChannel *iChannel) const;
  IoHomecontrolChannel *channelForQueueEntry(const IoHomeQueueEntry &iEntry) const;
  bool resolveLowPower2W(uint32_t iNodeId) const;
  bool pairingLowPower2W() const;
  uint16_t preambleFor2WRequest(const IoHomeFrame &iFrame) const;
  void resolveTwoWayPreamblePlan(const IoHomeFrame &iFrame, IoHomeQueueEntry &ioEntry) const;
  uint16_t preambleForQueued2WAttempt(const IoHomeFrame &iFrame,
                                      const IoHomeQueueEntry &iEntry) const;
  TwoWayDiscoverySettings pairingDiscoverySettings() const;
  bool learnPowerClassFromDiscovery(IoHomecontrolChannel *iChannel,
                                    const IoHomeProtocolIdentity &iMetadata,
                                    uint32_t iSourceNodeId,
                                    const char *iSource);
  bool captureProtocolIdentity(IoHomecontrolChannel *iChannel,
                               const IoHomeFrame &iFrame,
                               const char *iSource,
                               IoHomeProtocolIdentity *oIdentity = nullptr,
                               uint8_t iFrequencyIndex = 0xFF,
                               int16_t iRssi = 0);
  IoHomeNodeStats *findOrAddNodeStats(uint32_t iNodeId);
  void rememberProtocolIdentity(uint32_t iIoAddress,
                                const IoHomeProtocolIdentity &iIdentity);
  IoHomecontrolChannel *oneWayProfileForNode(uint32_t iNodeId) const;
  uint8_t oneWayBroadcastTypeForNode(uint32_t iNodeId) const;
  uint32_t oneWayDestinationForEntry(const IoHomeQueueEntry &iEntry) const;
};
