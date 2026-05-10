#pragma once
#include "OpenKNX.h"
#include "knxprod.h"

class IoHomeController;

class IoHomecontrolChannel : public OpenKNX::Channel
{
public:
  enum class SceneAction : uint8_t
  {
    Position = 0,
    Favorite = 1,
    Ventilation = 2,
  };

  IoHomecontrolChannel(uint8_t iIndex, IoHomeController &iController);

  const std::string name() override;
  void setup() override;
  void loop() override;

  void processInputKo(uint8_t iIoIndex, GroupObject &iKo);
  bool restoreLastKnownStateAfterStartup();

  // Callbacks from controller when radio responses arrive
  void onPositionFeedback(float iPositionPercent);
  void onTargetPositionFeedback(float iTargetPositionPercent);
  void onStatusUpdate(bool iIsMoving);
  void onSlatFeedback(float iSlatPercent);
  void onDeviceName(const char *iName, uint8_t iLen);
  void onDeviceInfo(uint16_t iType, uint8_t iSubtype, uint8_t iManufacturer);
  void onBatteryLevel(uint8_t iPercent);
  void onEstimate(uint8_t iSeconds);
  void onStatusExpected();
  void onRssiUpdate(uint8_t iScaledPercent);
  void logStatusSummary(float iCurrentPositionPercent, bool iHasCurrentPosition,
                        float iTargetPositionPercent, bool iHasTargetPosition,
                        bool iIsMoving);

  // Get estimated current position during travel (linear interpolation)
  float estimateCurrentPosition() const;

  // Pairing data
  bool isPaired() const;
  void setNodeId(uint32_t iNodeId);
  uint32_t getNodeId() const;
  void setEncryptionKey(const uint8_t *iKey);
  const uint8_t *getEncryptionKey() const;

  // Challenge tracking for HMAC verification of responses
  void setLastChallenge(const uint8_t *iChallenge);
  const uint8_t *getLastChallenge() const;

  // 1W protocol state
  uint16_t getSequence1W() const;
  void setSequence1W(uint16_t iSeq);
  uint16_t incrementSequence1W();
  bool is1W() const;
  void setIs1W(bool iIs1W);
  void setConfigured1WTargetNodeId(uint32_t iNodeId);
  uint32_t getConfigured1WTargetNodeId() const;

  // Lock control (P2)
  void setLocked(bool iLocked);
  bool isLocked() const;

  // Error status (P2)
  void setErrorStatus(uint8_t iStatus);
  uint8_t getErrorStatus() const;

  // Scene data (P3)
  void setScenePosition(uint8_t iScene, uint8_t iPosition);
  uint8_t getScenePosition(uint8_t iScene) const;
  void setSceneAction(uint8_t iScene, SceneAction iAction);
  SceneAction getSceneAction(uint8_t iScene) const;
  void setSceneSlat(uint8_t iScene, uint8_t iSlatPosition);
  uint8_t getSceneSlat(uint8_t iScene) const;

private:
  static constexpr uint8_t kMaxSceneCount = 10;

  IoHomeController &mController;
  uint32_t mNodeId = 0;           // 3-byte (24-bit) remote device address
  uint8_t mEncKey[16] = {};       // AES-128 encryption key
  uint8_t mLastChallenge[6] = {}; // challenge sent with last authenticated command
  bool mPaired = false;
  uint16_t mSequence1W = 0; // 1W monotonic sequence counter (persisted)
  bool mIs1W = false;       // true if channel uses 1W protocol
  uint32_t mConfigured1WTargetNodeId = 0;
  float mCurrentPosition = 0.0f;
  float mCurrentSlat = 0.0f;
  bool mIsMoving = false;
  bool mStatusExpected = false; // device will auto-send StatusUpdate
  uint8_t mBatteryLevel = 0xFF; // 0xFF = unknown, 0-100 = percent
  bool mLocked = false;         // P2: channel lock
  uint8_t mErrorStatus = 0;     // P2: error status enum (0=OK)
  uint8_t mLastRssi = 0;        // P1: scaled RSSI 0-100%

  // Blind travel-time position estimation
  float mTargetPosition = 0.0f;
  uint32_t mTravelStartTime = 0;
  uint32_t mTravelDurationMs = 0;
  float mTravelStartPosition = 0.0f;

  uint32_t mStatusPollTimer = 0;
  char mDeviceName[21] = {}; // max 20 chars + null terminator
  uint16_t mDeviceType = 0;
  uint8_t mDeviceSubtype = 0;
  uint8_t mManufacturer = 0;

  // P3: Scene position data (10 ETS-backed scenes x 1 byte, 0xFF = not set)
  uint8_t mScenePositions[kMaxSceneCount] = {};
  SceneAction mSceneActions[kMaxSceneCount] = {};
  uint8_t mSceneSlats[kMaxSceneCount] = {};

  void sendPositionCommand(float iPercent, uint8_t iSlatPercent = 0xFF);
  void sendUpDown(bool iDown);
  void sendStop();
  void sendFavorite();
  void sendSlatCommand(float iPercent);
  void sendVentilationPosition();
  void requestStatus();
  void requestStatusPrivate();
  void publishPositionFeedback(float iPositionPercent, bool iLogMessage);
  void startTravelEstimation(float iTargetPositionPercent);
  void stopTravelEstimation(bool iPublishPosition);
  void updateEstimatedPosition();
  float configuredOpeningTimeSeconds() const;
  float configuredClosingTimeSeconds() const;

  // P3 handlers
  void handleSceneRecall(uint8_t iScene);
  void handleSceneControl(uint8_t iControl);
  void handleWindAlarm(bool iAlarm);
  void handleStepStop(bool iDown);
  uint8_t getConfiguredSceneCount() const;
  void loadSceneConfiguration();
  float sceneToDevicePosition(uint8_t iScenePosition) const;
  uint8_t sceneToDeviceSlat(uint8_t iSceneSlat) const;
  uint8_t currentPositionToSceneValue() const;
  uint8_t currentSlatToSceneValue() const;
  bool storeSceneStateToEts(uint8_t iSceneIndex, uint8_t iScenePosition, uint8_t iSceneSlat);

  GroupObject &getKo(uint8_t iIoIndex);
  const std::string logPrefix() override;
};
