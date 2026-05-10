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
  void save() {}
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

  void onPositionFeedback(float) {}
  void onTargetPositionFeedback(float) {}
  void onStatusUpdate(bool) {}
  void onSlatFeedback(float) {}
  void onDeviceName(const char *, uint8_t) {}
  void onDeviceInfo(uint16_t, uint8_t, uint8_t) {}
  void onBatteryLevel(uint8_t) {}
  void onEstimate(uint8_t) {}
  void onStatusExpected() {}
  void onRssiUpdate(uint8_t) {}
  void logStatusSummary(float, bool, float, bool, bool) {}

private:
  uint32_t mNodeId = 0;
  uint8_t mEncKey[16] = {};
  uint8_t mLastChallenge[6] = {};
  uint16_t mSequence1W = 0;
  bool mIs1W = false;
  bool mPaired = false;
  uint32_t mConfigured1WTargetNodeId = 0;
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