#pragma once
#include "IoHomecontrolChannel.h"
#include "IoHomeRemoteMap.h"
#include "OpenKNX.h"
#include "controller/IoHomeController.h"
#include "knxprod.h"

class IoHomecontrol : public OpenKNX::Module
{
public:
  static constexpr uint8_t kFunctionPropertyObjectIndex = 160;
  static constexpr uint8_t kFunctionPropertyId = 10;

  enum class StatusLedMode : uint8_t
  {
    Unknown,
    IdleUnpaired,
    IdlePaired,
    Pairing
  };

  enum class RadioDiagnosticKind : uint8_t
  {
    None,
    TxTest,
    Sweep,
    Soak
  };

  IoHomecontrol();
  ~IoHomecontrol();

  const std::string name() override;
  const std::string version() override;
  void setup() override;
  void loop() override;
  void processInputKo(GroupObject &iKo) override;
  bool processFunctionProperty(uint8_t objectIndex, uint8_t propertyId,
                               uint8_t length, uint8_t *data,
                               uint8_t *resultData, uint8_t &resultLength) override;
  void readFlash(const uint8_t *iBuffer, const uint16_t iSize) override;
  void writeFlash() override;
  uint16_t flashSize() override;
  bool processCommand(const std::string cmd, bool diagnoseKo) override;
  void showHelp() override;
  void processAfterStartupDelay() override;
  void savePower() override;
  bool restorePower() override;

  IoHomeController &controller();
  IoHomecontrolChannel *getChannel(uint8_t iIndex);
  // Make sure the (possibly linked) 1W controller profile for a channel owns a
  // remote node id + key, generating one on demand if it is still empty. The
  // ETS-configured manufacturer is preserved. Returns true when a usable
  // identity is available afterwards.
  bool ensureOneWayControllerProfile(IoHomecontrolChannel *iChannel);
  IoHomeRemoteMap &remoteMap();
  void onPassiveKeyCaptured(const IoHomeController::PassiveKeyResult &iResult);

private:
  struct FlashChannelState
  {
    bool valid = false;
    bool paired = false;
    bool is1W = false;
    bool lowPower2W = true;
    uint32_t nodeId = 0;
    uint8_t key[16] = {};
    uint16_t sequence1W = 0; // persisted reserved/high-water sequence
    uint32_t oneWayControllerNodeId = 0;
    uint8_t oneWayControllerKey[16] = {};
    uint8_t oneWayControllerManufacturer = 2;
  };

  IoHomecontrolChannel *mChannels[IOHC_ChannelCount] = {};
  FlashChannelState mPendingFlashChannels[IOHC_ChannelCount] = {};
  uint8_t mNumChannels = 0;
  IoHomeController mController;
  IoHomeRemoteMap mRemoteMap;

  ControllerState mLastControllerState = ControllerState::Idle;
  uint8_t mLastPairedCount = 0xFF;
  StatusLedMode mStatusLedMode = StatusLedMode::Unknown;
  uint32_t mLedEffectUntil = 0;
  bool mLedEffectIsError = false;
  bool mLastDiscoveryActive = false;
  bool mLastScanActive = false;
  uint8_t mLastObservedCount = 0;
  bool mAutoSpeDiscoveryAfterPairing = false;
  bool mPendingPostPairSpeDiscovery = false;

  struct RadioSweepStat
  {
    uint32_t okCount = 0;
    uint32_t failCount = 0;
    uint32_t timeoutCount = 0;
    uint32_t maxWaitMs = 0;
    uint16_t lastIrq = 0;
    uint8_t lastTxStatus = 0;
    uint16_t lastDevErr = 0;
  };

  struct RadioDiagnosticState
  {
    RadioDiagnosticKind kind = RadioDiagnosticKind::None;
    bool active = false;
    bool txInProgress = false;
    bool prevRxScanEnabled = false;
    uint8_t restoreFreqIdx = 0xFF;
    uint8_t currentFreqPos = 0;
    uint8_t requestedRounds = 0;
    uint32_t completedRounds = 0;
    uint32_t soakBudgetMs = 0;
    uint32_t startedAtMs = 0;
    uint32_t txStartedAtMs = 0;
    uint32_t busySinceMs = 0;
    uint32_t totalOk = 0;
    uint32_t totalFail = 0;
    uint32_t totalTimeout = 0;
    RadioError lastError = RadioError::None;
    bool txDone = false;
    uint32_t txWaitedMs = 0;
    uint16_t devErrBefore = 0;
    uint16_t devErrCleared = 0;
    uint16_t devErrNow = 0;
    uint8_t statusNow = 0;
    uint8_t modeNow = 0;
    uint8_t cmdNow = 0;
    uint16_t irqNow = 0;
    int dio1Now = 0;
    int busyNow = 0;
    RadioSweepStat sweepStats[3] = {};
  };

  RadioDiagnosticState mRadioDiagnostic;

  void deriveOwnNodeId();
  void initSystemKey();
  bool generateOneWayControllerProfile(IoHomecontrolChannel *iChannel);
  void initOneWayControllerProfiles();
  void consolidateOneWayProfileSequences();
  void applyOneWayControllerConfiguration();
  uint8_t configuredChannelCount() const;
  void restoreChannelFlashState(uint8_t iIndex, const FlashChannelState &iState);
  void applyPendingFlashChannelState();
  uint8_t oneWayProfileIndex(IoHomecontrolChannel *iProfile) const;
  bool oneWayProfileUsedByPairedChannel(IoHomecontrolChannel *iProfile) const;
  uint8_t countPairedChannels() const;
  bool isPairingState(ControllerState iState) const;
  OpenKNX::Led::FunctionGroup *statusLedFunction();
  void applyStatusLedMode(StatusLedMode iMode);
  void updateStatusLed();
  bool startRadioDiagnostic(RadioDiagnosticKind iKind, uint8_t iValue = 0);
  void processRadioDiagnostic();
  void advanceRadioDiagnosticFrequency();
  bool deferRadioDiagnosticBusy();
  void clearRadioDiagnosticBusy();
  bool restoreRadioDiagnosticReceive();
  bool forceRestoreRadioDiagnosticReceive();
  void recordRadioDiagnosticSweepSuccess(RadioSweepStat &iStat, uint32_t iWaitedMs, uint16_t iIrq);
  void recordRadioDiagnosticSweepFailure(RadioSweepStat &iStat, bool iTimeout);
  void finishRadioTxTest();
  void finishRadioSweep();
  bool handleRadioRaw(bool iDebugKo);
};

extern IoHomecontrol openknxIoHomecontrol;
