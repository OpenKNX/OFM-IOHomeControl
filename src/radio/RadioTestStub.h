#pragma once

#include "RadioTypes.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct RadioSX1262TxBusyTrace
{
  uint32_t irqHits = 0;
  uint32_t irqTotalUs = 0;
  uint32_t statusHits = 0;
  uint32_t statusTotalUs = 0;
  uint32_t clearIrqHits = 0;
  uint32_t clearIrqTotalUs = 0;
  uint32_t standbyHits = 0;
  uint32_t standbyTotalUs = 0;
  uint32_t rxHits = 0;
  uint32_t rxTotalUs = 0;
  uint32_t otherHits = 0;
  uint32_t otherTotalUs = 0;
};

class RadioTestStub
{
public:
  static constexpr uint8_t PIN_NOT_CONNECTED = RADIO_PIN_NOT_CONNECTED;

  RadioTestStub() = default;

  std::string logPrefix() { return "RadioTestStub"; }

  void init(uint8_t, uint8_t, uint8_t, uint8_t = PIN_NOT_CONNECTED)
  {
    mInitialized = true;
    mState = RadioState::Idle;
  }

  RadioError configure() { return RadioError::None; }

  RadioError setFrequency(uint32_t iFreqHz)
  {
    if (mNextFrequencyError != RadioError::None)
    {
      const RadioError lErr = mNextFrequencyError;
      mNextFrequencyError = RadioError::None;
      return lErr;
    }
    mCurrentFreq = iFreqHz;
    return RadioError::None;
  }

  RadioError setFrequencyBlocking(uint32_t iFreqHz) { return setFrequency(iFreqHz); }
  RadioError setOutputPower(uint8_t) { return RadioError::None; }
  RadioError setPreambleLength(uint16_t iSymbols)
  {
    if (mNextPreambleError != RadioError::None)
    {
      const RadioError lErr = mNextPreambleError;
      mNextPreambleError = RadioError::None;
      return lErr;
    }
    mLastPreambleLength = iSymbols;
    return RadioError::None;
  }
  RadioError setPreambleLengthBlocking(uint16_t iSymbols) { return setPreambleLength(iSymbols); }

  RadioError startTransmit(const uint8_t *iData, uint8_t iLen)
  {
    if (!mInitialized)
      return RadioError::NotInitialized;
    if (iData == nullptr || iLen == 0)
      return RadioError::InvalidParam;
    if (mNextTransmitError != RadioError::None)
    {
      const RadioError lErr = mNextTransmitError;
      mNextTransmitError = RadioError::None;
      return lErr;
    }

    mLastTransmittedPacket.assign(iData, iData + iLen);
    mState = RadioState::Transmitting;
    mTxDonePending = true;
    mTxStartCount++;
    return RadioError::None;
  }

  RadioError startTransmitBlocking(const uint8_t *iData, uint8_t iLen)
  {
    return startTransmit(iData, iLen);
  }

  RadioError startReceive()
  {
    if (!mInitialized)
      return RadioError::NotInitialized;
    mState = RadioState::Receiving;
    mRxStartCount++;
    return RadioError::None;
  }

  RadioError startReceiveBlocking() { return startReceive(); }

  bool isTxDone()
  {
    if (mTxDonePending)
    {
      mTxDonePending = false;
      mTxDoneCount++;
      mState = RadioState::Idle;
      return true;
    }
    return (mState != RadioState::Transmitting);
  }

  bool isTxDoneBlocking() { return isTxDone(); }
  bool isPacketAvailable() { return !mReceiveQueue.empty(); }
  bool isPreambleDetected() const { return false; }
  bool isSyncDetected() const { return false; }

  uint8_t readPacket(uint8_t *oBuffer, uint8_t iMaxLen)
  {
    if (mReceiveQueue.empty() || oBuffer == nullptr || iMaxLen == 0)
      return 0;

    const QueuedPacket lPacket = mReceiveQueue.front();
    mReceiveQueue.pop_front();
    const uint8_t lLen = static_cast<uint8_t>(std::min<size_t>(lPacket.data.size(), iMaxLen));
    std::copy_n(lPacket.data.data(), lLen, oBuffer);
    mLastRssi = lPacket.rssi;
    mIrqCount++;
    mRxDoneCount++;
    return lLen;
  }

  int16_t lastRssi() const { return mLastRssi; }

  bool currentRssi(int16_t &oRssi)
  {
    oRssi = mLastRssi;
    return true;
  }

  void sleep() { mState = RadioState::Sleep; }
  void standby() { mState = RadioState::Idle; }
  RadioState state() const { return mState; }
  bool isInitialized() const { return mInitialized; }

  uint32_t txStartCount() const { return mTxStartCount; }
  uint32_t txDoneCount() const { return mTxDoneCount; }
  uint32_t rxStartCount() const { return mRxStartCount; }
  uint32_t irqCount() const { return mIrqCount; }
  uint32_t preambleIrqCount() const { return 0; }
  uint32_t syncWordIrqCount() const { return 0; }
  uint32_t rxDoneCount() const { return mRxDoneCount; }
  uint32_t crcErrorCount() const { return 0; }
  uint32_t timeoutCount() const { return 0; }
  uint32_t irqPollHitCount() const { return 0; }
  uint32_t preambleOnlyIrqCount() const { return 0; }
  uint32_t rxReadFailCount() const { return 0; }
  uint32_t txBusyHighHitCount() const { return 0; }
  uint32_t txBusyHighTotalUs() const { return 0; }
  uint32_t txBusyHighMaxUs() const { return 0; }
  RadioSX1262TxBusyTrace txBusyTrace() const { return {}; }
  uint16_t lastIrqStatus() const { return 0; }
  uint8_t lastOpStatusBefore() const { return 0; }
  uint8_t lastOpStatusAfter() const { return 0; }
  uint8_t lastTxSetStatus() const { return 0; }
  uint16_t lastTxIrqImmediate() const { return 0; }
  uint8_t initError() const { return 0; }
  uint8_t initStatusByte() const { return 0; }
  uint8_t initCommandStatus() const { return 0; }
  uint16_t initDeviceErrors() const { return 0; }
  uint16_t lastDeviceErrors() const { return 0; }
  uint32_t tcxoStartupDelayUs() const { return 0; }
  uint8_t tcxoStartupAttempts() const { return 0; }
  uint8_t rfSwitchConfig() const { return 0; }
  bool busyTimedOut() const { return false; }

  uint8_t debugReadRegister(uint8_t) { return 0; }
  void debugWriteRegister(uint8_t, uint8_t) {}
  uint16_t debugReadDeviceErrors() { return 0; }
  void configureEms2Mode() {}
  void configureStandardMode() {}
  RadioError sendEms2Wake() { return RadioError::None; }

  void testQueueReceivedPacket(const uint8_t *iData, uint8_t iLen, int16_t iRssi = -70)
  {
    if (iData == nullptr || iLen == 0)
      return;
    mReceiveQueue.push_back({std::vector<uint8_t>(iData, iData + iLen), iRssi});
  }

  void testClearReceivedPackets() { mReceiveQueue.clear(); }
  void testClearTransmittedPacket() { mLastTransmittedPacket.clear(); }
  void testSetNextFrequencyError(RadioError iError) { mNextFrequencyError = iError; }
  void testSetNextPreambleError(RadioError iError) { mNextPreambleError = iError; }
  void testSetNextTransmitError(RadioError iError) { mNextTransmitError = iError; }
  uint32_t testTransmitCount() const { return mTxStartCount; }
  uint16_t testLastPreambleLength() const { return mLastPreambleLength; }
  uint32_t testCurrentFrequency() const { return mCurrentFreq; }
  const std::vector<uint8_t> &testLastTransmittedPacket() const { return mLastTransmittedPacket; }

private:
  struct QueuedPacket
  {
    std::vector<uint8_t> data;
    int16_t rssi;
  };

  bool mInitialized = false;
  bool mTxDonePending = false;
  RadioState mState = RadioState::Idle;
  int16_t mLastRssi = -70;
  uint32_t mCurrentFreq = 0;
  uint32_t mTxStartCount = 0;
  uint32_t mTxDoneCount = 0;
  uint32_t mRxStartCount = 0;
  uint32_t mIrqCount = 0;
  uint32_t mRxDoneCount = 0;
  uint16_t mLastPreambleLength = 0;
  RadioError mNextFrequencyError = RadioError::None;
  RadioError mNextPreambleError = RadioError::None;
  RadioError mNextTransmitError = RadioError::None;
  std::deque<QueuedPacket> mReceiveQueue;
  std::vector<uint8_t> mLastTransmittedPacket;
};
