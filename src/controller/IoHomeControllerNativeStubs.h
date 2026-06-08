#pragma once

#include "../IoHomeRemoteMap.h"
#include "../IoHomecontrolHardware.h"

#include <cstdint>
#include <cstring>

#ifndef IOHC_ChannelCount
#define IOHC_ChannelCount 16
#endif

#ifndef logInfoP
#define logInfoP(...) do {} while (0)
#endif

#ifndef logDebugP
#define logDebugP(...) do {} while (0)
#endif

#ifndef logWarnP
#define logWarnP(...) do {} while (0)
#endif

#ifndef logErrorP
#define logErrorP(...) do {} while (0)
#endif

struct OpenKnxNativeFlashStub
{
  uint32_t saveCount = 0;
  void save() { saveCount++; }
};

struct OpenKnxNativeStub
{
  OpenKnxNativeFlashStub flash;
};

inline OpenKnxNativeStub openknx{};

class IoHomecontrolChannel
{
public:
  bool is1W() const { return mIs1W; }
  void setIs1W(bool iIs1W) { mIs1W = iIs1W; }
  bool isPaired() const { return mPaired; }
  void setPaired(bool iPaired) { mPaired = iPaired; }

  void setConfigured1WTargetNodeId(uint32_t iNodeId) { mConfigured1WTargetNodeId = iNodeId; }
  uint32_t getConfigured1WTargetNodeId() const { return mConfigured1WTargetNodeId; }
  void setConfigured1WBroadcastType(uint8_t iBroadcastType) { mConfigured1WBroadcastType = iBroadcastType & 0x3F; }
  uint8_t getConfigured1WBroadcastType() const { return mConfigured1WBroadcastType; }

  void setNodeId(uint32_t iNodeId)
  {
    mNodeId = iNodeId;
    mPaired = (iNodeId != 0);
  }
  uint32_t getNodeId() const { return mNodeId; }

  void setEncryptionKey(const uint8_t *iKey)
  {
    if (iKey)
      memcpy(mEncKey, iKey, sizeof(mEncKey));
  }

  const uint8_t *getEncryptionKey() const { return mEncKey; }
  void setLowPower2W(bool iLowPower) { mLowPower2W = iLowPower; }
  bool isLowPower2W() const { return mLowPower2W; }

  void setLastChallenge(const uint8_t *iChallenge)
  {
    if (iChallenge)
      memcpy(mLastChallenge, iChallenge, sizeof(mLastChallenge));
    else
      memset(mLastChallenge, 0, sizeof(mLastChallenge));
  }

  const uint8_t *getLastChallenge() const { return mLastChallenge; }

  uint16_t getSequence1W() const { return mSequence1W; }
  void setSequence1W(uint16_t iSequence) { mSequence1W = iSequence; }
  uint16_t incrementSequence1W() { return ++mSequence1W; }
  void setOneWayControllerNodeId(uint32_t iNodeId) { mOneWayControllerNodeId = iNodeId & 0x00FFFFFF; }
  uint32_t getOneWayControllerNodeId() const { return mOneWayControllerNodeId; }
  void setOneWayControllerKey(const uint8_t *iKey) { if (iKey) memcpy(mOneWayControllerKey, iKey, sizeof(mOneWayControllerKey)); }
  const uint8_t *getOneWayControllerKey() const { return mOneWayControllerKey; }
  void setOneWayControllerManufacturer(uint8_t iManufacturer) { mOneWayControllerManufacturer = iManufacturer; }
  uint8_t getOneWayControllerManufacturer() const { return mOneWayControllerManufacturer; }
  uint8_t getConfigured1WManufacturer() const { return mConfigured1WManufacturer; }
  bool hasOneWayControllerIdentity() const
  {
    if (mOneWayControllerNodeId == 0)
      return false;
    for (uint8_t b : mOneWayControllerKey)
      if (b != 0)
        return true;
    return false;
  }
  void setConfigured1WProfileChannel(uint8_t iChannelIndex) { mConfigured1WProfileChannel = iChannelIndex; }
  uint8_t getConfigured1WProfileChannel() const { return mConfigured1WProfileChannel; }

  void onPositionFeedback(float iPercent)
  {
    mHasPositionFeedback = true;
    mPositionFeedback = iPercent;
  }
  void onTargetPositionFeedback(float iPercent)
  {
    mHasTargetPositionFeedback = true;
    mTargetPositionFeedback = iPercent;
  }
  void onStatusUpdate(bool iMoving)
  {
    mHasStatusUpdate = true;
    mStatusMoving = iMoving;
  }
  void onSlatFeedback(float iPercent)
  {
    mHasSlatFeedback = true;
    mSlatFeedback = iPercent;
  }
  void onDeviceName(const char *, uint8_t) {}
  void onDeviceInfo(uint16_t, uint8_t, uint8_t) {}
  void onBatteryLevel(uint8_t iPercent)
  {
    mHasBatteryLevel = true;
    mBatteryLevel = iPercent;
  }
  void onEstimate(uint8_t iSeconds)
  {
    mHasEstimate = true;
    mEstimate = iSeconds;
  }
  void onStatusExpected() { mStatusExpected = true; }
  void onRssiUpdate(uint8_t) {}
  void logStatusSummary(float, bool, float, bool, bool) {}

  bool testHasPositionFeedback() const { return mHasPositionFeedback; }
  float testPositionFeedback() const { return mPositionFeedback; }
  bool testHasTargetPositionFeedback() const { return mHasTargetPositionFeedback; }
  float testTargetPositionFeedback() const { return mTargetPositionFeedback; }
  bool testHasStatusUpdate() const { return mHasStatusUpdate; }
  bool testStatusMoving() const { return mStatusMoving; }
  bool testHasSlatFeedback() const { return mHasSlatFeedback; }
  float testSlatFeedback() const { return mSlatFeedback; }
  bool testHasBatteryLevel() const { return mHasBatteryLevel; }
  uint8_t testBatteryLevel() const { return mBatteryLevel; }
  bool testHasEstimate() const { return mHasEstimate; }
  uint8_t testEstimate() const { return mEstimate; }
  bool testStatusExpected() const { return mStatusExpected; }

private:
  uint32_t mNodeId = 0;
  uint8_t mEncKey[16] = {};
  uint8_t mLastChallenge[6] = {};
  uint16_t mSequence1W = 0;
  uint32_t mOneWayControllerNodeId = 0;
  uint8_t mOneWayControllerKey[16] = {};
  uint8_t mOneWayControllerManufacturer = 2;
  uint8_t mConfigured1WManufacturer = 0;
  uint8_t mConfigured1WProfileChannel = 0xFF;
  bool mIs1W = false;
  bool mPaired = false;
  bool mLowPower2W = true;
  uint32_t mConfigured1WTargetNodeId = 0;
  uint8_t mConfigured1WBroadcastType = 2;
  bool mHasPositionFeedback = false;
  float mPositionFeedback = 0.0f;
  bool mHasTargetPositionFeedback = false;
  float mTargetPositionFeedback = 0.0f;
  bool mHasStatusUpdate = false;
  bool mStatusMoving = false;
  bool mHasSlatFeedback = false;
  float mSlatFeedback = 0.0f;
  bool mHasBatteryLevel = false;
  uint8_t mBatteryLevel = 0xFF;
  bool mHasEstimate = false;
  uint8_t mEstimate = 0xFF;
  bool mStatusExpected = false;
};

class IoHomecontrol
{
public:
  IoHomecontrolChannel *getChannel(uint8_t iIndex)
  {
    return (iIndex < IOHC_ChannelCount) ? mChannels[iIndex] : nullptr;
  }

  void testSetChannel(uint8_t iIndex, IoHomecontrolChannel *iChannel)
  {
    if (iIndex < IOHC_ChannelCount)
      mChannels[iIndex] = iChannel;
  }

  IoHomeRemoteMap &remoteMap() { return mRemoteMap; }

private:
  IoHomecontrolChannel *mChannels[IOHC_ChannelCount] = {};
  IoHomeRemoteMap mRemoteMap;
};
