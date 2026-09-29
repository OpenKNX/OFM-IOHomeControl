#pragma once

#include "../IoHomeRemoteMap.h"
#include "../IoHomecontrolHardware.h"

#include <cstdint>
#include <cstring>

#ifndef IOHC_ChannelCount
#define IOHC_ChannelCount 16
#endif

#ifndef IOHC_1W_SEQUENCE_RESERVE_WINDOW
#define IOHC_1W_SEQUENCE_RESERVE_WINDOW 16
#endif

#ifndef logInfoP
#define logInfoP(...) \
  do                  \
  {                   \
  } while (0)
#endif

#ifndef logDebugP
#define logDebugP(...) \
  do                   \
  {                    \
  } while (0)
#endif

#ifndef logDebug
#define logDebug(...) \
  do                  \
  {                   \
  } while (0)
#endif

#ifndef logWarnP
#define logWarnP(...) \
  do                  \
  {                   \
  } while (0)
#endif

#ifndef logErrorP
#define logErrorP(...) \
  do                   \
  {                    \
  } while (0)
#endif

struct OpenKnxNativeFlashStub
{
  uint32_t saveCount = 0;
  void save(bool = false) { saveCount++; }
};

struct OpenKnxNativeStub
{
  OpenKnxNativeFlashStub flash;
};

inline OpenKnxNativeStub openknx{};

inline uint32_t gIoHomeTestMillis = 0;
inline uint32_t gIoHomeTestMicros = 0;

inline uint32_t ioHomeTestMillis()
{
  return gIoHomeTestMillis;
}

inline uint32_t ioHomeTestMicros()
{
  return gIoHomeTestMicros;
}

inline void ioHomeTestSetMillis(uint32_t iMillis)
{
  gIoHomeTestMillis = iMillis;
}

inline void ioHomeTestSetMicros(uint32_t iMicros)
{
  gIoHomeTestMicros = iMicros;
}

inline void ioHomeTestAdvanceMillis(uint32_t iMillis)
{
  gIoHomeTestMillis += iMillis;
  gIoHomeTestMicros += iMillis * 1000UL;
}

inline void ioHomeTestAdvanceMicros(uint32_t iMicros)
{
  gIoHomeTestMicros += iMicros;
  gIoHomeTestMillis = gIoHomeTestMicros / 1000UL;
}

class IoHomecontrolChannel
{
public:
  bool is1W() const { return mIs1W; }
  void setIs1W(bool iIs1W) { mIs1W = iIs1W; }
  bool isPaired() const { return mPaired; }
  bool isOneWayEnrolled() const { return mOneWayEnrolled; }
  void setOneWayEnrolled(bool iEnrolled) { mOneWayEnrolled = iEnrolled; }
  bool isOperational() const { return mIs1W ? mOneWayEnrolled : mPaired; }
  void setPaired(bool iPaired) { mPaired = iPaired; }

  void setConfigured1WTargetNodeId(uint32_t iNodeId) { mConfigured1WTargetNodeId = iNodeId; }
  uint32_t getConfigured1WTargetNodeId() const { return mConfigured1WTargetNodeId; }
  void setConfigured1WBroadcastType(uint8_t iBroadcastType) { mConfigured1WBroadcastType = iBroadcastType & 0x3F; }
  uint8_t getConfigured1WBroadcastType() const { return mConfigured1WBroadcastType; }
  void setConfigured1WAcei(uint8_t iAcei) { mConfigured1WAcei = iAcei; }
  uint8_t getConfigured1WAcei() const { return mConfigured1WAcei; }
  void setConfigured2WAcei(uint8_t iAcei) { mConfigured2WAcei = iAcei; }
  uint8_t getConfigured2WAcei() const { return mConfigured2WAcei; }
  void setConfigured1WEnrollmentMac(bool iEnabled) { mConfigured1WEnrollmentMac = iEnabled; }
  bool getConfigured1WEnrollmentMac() const { return mConfigured1WEnrollmentMac; }
  void setConfigured1WEnrollmentFinalizer(OneWayEnrollmentFinalizer iFinalizer) { mConfigured1WEnrollmentFinalizer = iFinalizer; }
  OneWayEnrollmentFinalizer getConfigured1WEnrollmentFinalizer() const { return mConfigured1WEnrollmentFinalizer; }
  void setConfigured1WExecuteDestinationPolicy(OneWayExecuteDestinationPolicy iPolicy) { mConfigured1WExecuteDestinationPolicy = iPolicy; }
  OneWayExecuteDestinationPolicy getConfigured1WExecuteDestinationPolicy() const { return mConfigured1WExecuteDestinationPolicy; }
  void setConfigured1WEnrollmentDestinationPolicy(OneWayEnrollmentDestinationPolicy iPolicy) { mConfigured1WEnrollmentDestinationPolicy = iPolicy; }
  OneWayEnrollmentDestinationPolicy getConfigured1WEnrollmentDestinationPolicy() const { return mConfigured1WEnrollmentDestinationPolicy; }
  void setConfigured1WEnrollmentClassMask(uint8_t iMask) { mConfigured1WEnrollmentClassMask = iMask <= IOHC_1W_ENROLL_CLASS_INTERIOR ? iMask : 0; }
  uint8_t getConfigured1WEnrollmentClassMask() const { return mConfigured1WEnrollmentClassMask; }
  void setConfigured1WPowerClass(OneWayPowerClass iPowerClass) { mConfigured1WPowerClass = iPowerClass; }
  OneWayPowerClass getConfigured1WPowerClass() const { return mConfigured1WPowerClass; }

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
  void setLowPower2W(bool iLowPower)
  {
    mLowPower2W = iLowPower;
    mHasLearnedLowPower2W = true;
  }
  void clearLearnedLowPower2W()
  {
    mLowPower2W = false;
    mHasLearnedLowPower2W = false;
  }
  bool isLowPower2W() const { return mLowPower2W; }
  bool hasLearnedLowPower2W() const { return mHasLearnedLowPower2W; }
  void setConfigured2WPowerClass(TwoWayPowerClass iPowerClass) { mConfigured2WPowerClass = iPowerClass; }
  TwoWayPowerClass getConfigured2WPowerClass() const { return mConfigured2WPowerClass; }
  void setConfigured2WDiscoverConfirmMode(PairingDiscoverConfirmMode iMode) { mConfigured2WDiscoverConfirmMode = iMode; }
  PairingDiscoverConfirmMode getConfigured2WDiscoverConfirmMode() const { return mConfigured2WDiscoverConfirmMode; }
  void setConfigured2WKeyInitDelay(uint16_t iDelayMs) { mConfigured2WKeyInitDelay = iDelayMs > 10000 ? 10000 : iDelayMs; }
  uint16_t getConfigured2WKeyInitDelay() const { return mConfigured2WKeyInitDelay; }
  void setConfigured2WDiscoverySettings(const TwoWayDiscoverySettings &iSettings) { mConfigured2WDiscoverySettings = iSettings; }
  const TwoWayDiscoverySettings &getConfigured2WDiscoverySettings() const { return mConfigured2WDiscoverySettings; }
  bool effectiveLowPower2W() const
  {
    if (mIs1W || mConfigured2WPowerClass == TwoWayPowerClass::AlwaysAlive)
      return false;
    if (mConfigured2WPowerClass == TwoWayPowerClass::LowPower)
      return true;
    if (mProtocolIdentity.valid &&
        mProtocolIdentity.powerSaveMode != IoHomePowerMode::Unknown)
      return mProtocolIdentity.powerSaveMode == IoHomePowerMode::LowPower;
    return mHasLearnedLowPower2W ? mLowPower2W : false;
  }

  void setLastChallenge(const uint8_t *iChallenge)
  {
    if (iChallenge)
      memcpy(mLastChallenge, iChallenge, sizeof(mLastChallenge));
    else
      memset(mLastChallenge, 0, sizeof(mLastChallenge));
  }

  const uint8_t *getLastChallenge() const { return mLastChallenge; }

  uint16_t getSequence1W() const { return mSequence1W; }
  uint16_t getReservedSequence1W() const { return mReservedSequence1W; }
  void setSequence1W(uint16_t iSequence)
  {
    mSequence1W = iSequence;
    mReservedSequence1W = iSequence;
  }
  void setReservedSequence1W(uint16_t iSequence) { mReservedSequence1W = iSequence; }
  uint16_t incrementSequence1W()
  {
    bool lSaveRequired = false;
    return incrementSequence1W(false, lSaveRequired);
  }
  uint16_t incrementSequence1W(bool iForceReserve, bool &oFlashSaveRequired)
  {
    mSequence1W = static_cast<uint16_t>(mSequence1W + 1U);
    const int16_t lRemainingReserved = static_cast<int16_t>(mReservedSequence1W - mSequence1W);
    oFlashSaveRequired = iForceReserve || mReservedSequence1W == 0 || lRemainingReserved <= 0;
    if (oFlashSaveRequired)
      mReservedSequence1W = static_cast<uint16_t>(mSequence1W + IOHC_1W_SEQUENCE_RESERVE_WINDOW);
    return mSequence1W;
  }
  void setOneWayControllerNodeId(uint32_t iNodeId) { mOneWayControllerNodeId = iNodeId & 0x00FFFFFF; }
  uint32_t getOneWayControllerNodeId() const { return mOneWayControllerNodeId; }
  void setOneWayControllerKey(const uint8_t *iKey)
  {
    if (iKey)
      memcpy(mOneWayControllerKey, iKey, sizeof(mOneWayControllerKey));
  }
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
    mHas2WHeardEvidence = true;
    mLast2WHeardMs = ioHomeTestMillis();
    mHas2WMovingEvidence = iMoving;
    if (iMoving)
      mLast2WMovingEvidenceMs = ioHomeTestMillis();
    else
      mStopSettlePollPending = false;
  }
  void onSlatFeedback(float iPercent)
  {
    mHasSlatFeedback = true;
    mSlatFeedback = iPercent;
  }
  void onScalarFeedback(float iPercent)
  {
    mHasScalarFeedback = true;
    mScalarFeedback = iPercent;
  }
  void onVelocityFeedback(ParameterSemantic iSemantic, float iPercent)
  {
    mHasVelocityFeedback = true;
    mVelocitySemantic = iSemantic;
    mVelocityFeedback = iPercent;
  }
  void onDeviceName(const char *iName, uint8_t iLen)
  {
    uint8_t lStart = 0;
    while (lStart < iLen && static_cast<uint8_t>(iName[lStart]) <= 0x20)
      ++lStart;
    uint8_t lEnd = iLen;
    while (lEnd > lStart &&
           (static_cast<uint8_t>(iName[lEnd - 1]) <= 0x20 || iName[lEnd - 1] == '\0'))
      --lEnd;
    uint8_t lOut = 0;
    for (uint8_t i = lStart; i < lEnd && lOut < sizeof(mDeviceName) - 1; ++i)
    {
      const uint8_t lByte = static_cast<uint8_t>(iName[i]);
      if (lByte < 0x80)
        mDeviceName[lOut++] = static_cast<char>(lByte);
      else if (lOut + 1 < sizeof(mDeviceName) - 1)
      {
        mDeviceName[lOut++] = static_cast<char>(0xC0 | (lByte >> 6));
        mDeviceName[lOut++] = static_cast<char>(0x80 | (lByte & 0x3F));
      }
    }
    mDeviceName[lOut] = '\0';
  }
  void onPostPairEnrichmentResponse(IoHomeCommand iResponse,
                                    const uint8_t *iData, uint8_t iDataLen)
  {
    const uint8_t lLen = iDataLen < IOHC_DEVICE_INFO_RAW_MAX_SIZE
                             ? iDataLen
                             : IOHC_DEVICE_INFO_RAW_MAX_SIZE;
    uint8_t *lTarget = nullptr;
    uint8_t *lStoredLen = nullptr;
    switch (iResponse)
    {
    case IoHomeCommand::GetNameResponse:
      lTarget = mProductIdentityEvidence.nameResponse;
      lStoredLen = &mProductIdentityEvidence.nameResponseLen;
      break;
    case IoHomeCommand::GetGeneralInfo1Response:
      lTarget = mProductIdentityEvidence.generalInfo1;
      lStoredLen = &mProductIdentityEvidence.generalInfo1Len;
      break;
    case IoHomeCommand::GetGeneralInfo2Response:
      lTarget = mProductIdentityEvidence.generalInfo2;
      lStoredLen = &mProductIdentityEvidence.generalInfo2Len;
      break;
    case IoHomeCommand::GetGeneralInfo3Response:
      lTarget = mProductIdentityEvidence.generalInfo3;
      lStoredLen = &mProductIdentityEvidence.generalInfo3Len;
      mProductIdentityEvidence.generalInfo3Outcome =
          IoHomeGeneralInfo3Outcome::Response;
      memset(mProductIdentityEvidence.generalInfo3ErrorResponse, 0,
             sizeof(mProductIdentityEvidence.generalInfo3ErrorResponse));
      mProductIdentityEvidence.generalInfo3ErrorResponseLen = 0;
      break;
    default:
      return;
    }
    memset(lTarget, 0, IOHC_DEVICE_INFO_RAW_MAX_SIZE);
    if (iData && lLen > 0)
      memcpy(lTarget, iData, lLen);
    *lStoredLen = lLen;
    ioHomeUpdateVendorProductEvidence(mProtocolIdentity, mProductIdentityEvidence);
    if (iResponse == IoHomeCommand::GetNameResponse)
      onDeviceName(
          reinterpret_cast<const char *>(mProductIdentityEvidence.nameResponse),
          mProductIdentityEvidence.nameResponseLen);
    else if (iResponse == IoHomeCommand::GetGeneralInfo2Response && iDataLen >= 12)
    {
      mProductIdentityEvidence.generalInfo2TypeValid = true;
      mProductIdentityEvidence.generalInfo2Profile =
          decodePackedProfile(iData[10], iData[11]);
      mProductIdentityEvidence.generalInfo2SubProfile =
          decodePackedSubProfile(iData[11]);
      mProductIdentityEvidence.generalInfo2MatchesDiscovery =
          mProtocolIdentity.valid &&
          mProductIdentityEvidence.generalInfo2Profile == mProtocolIdentity.profile &&
          mProductIdentityEvidence.generalInfo2SubProfile == mProtocolIdentity.subProfile;
    }
  }
  void onGeneralInfo3Requested()
  {
    mProductIdentityEvidence.generalInfo3Outcome =
        IoHomeGeneralInfo3Outcome::Requested;
    memset(mProductIdentityEvidence.generalInfo3, 0,
           sizeof(mProductIdentityEvidence.generalInfo3));
    mProductIdentityEvidence.generalInfo3Len = 0;
    memset(mProductIdentityEvidence.generalInfo3ErrorResponse, 0,
           sizeof(mProductIdentityEvidence.generalInfo3ErrorResponse));
    mProductIdentityEvidence.generalInfo3ErrorResponseLen = 0;
  }
  void onGeneralInfo3Failure(IoHomeGeneralInfo3Outcome iOutcome,
                             const uint8_t *iData = nullptr, uint8_t iDataLen = 0)
  {
    if (iOutcome != IoHomeGeneralInfo3Outcome::ErrorResponse &&
        iOutcome != IoHomeGeneralInfo3Outcome::Timeout &&
        iOutcome != IoHomeGeneralInfo3Outcome::TransportFailure)
      return;
    mProductIdentityEvidence.generalInfo3Outcome = iOutcome;
    const uint8_t lLen = iDataLen < IOHC_DEVICE_INFO_RAW_MAX_SIZE
                             ? iDataLen
                             : IOHC_DEVICE_INFO_RAW_MAX_SIZE;
    memset(mProductIdentityEvidence.generalInfo3ErrorResponse, 0,
           sizeof(mProductIdentityEvidence.generalInfo3ErrorResponse));
    if (iData && lLen > 0)
      memcpy(mProductIdentityEvidence.generalInfo3ErrorResponse, iData, lLen);
    mProductIdentityEvidence.generalInfo3ErrorResponseLen = lLen;
  }
  void clearProductIdentityEvidence()
  {
    memset(mDeviceName, 0, sizeof(mDeviceName));
    mProductIdentityEvidence = IoHomeProductIdentityEvidence{};
  }
  void restoreProductIdentityEvidence(const IoHomeProductIdentityEvidence &iEvidence)
  {
    mProductIdentityEvidence = iEvidence;
    onDeviceName(reinterpret_cast<const char *>(iEvidence.nameResponse),
                 iEvidence.nameResponseLen);
    mProductIdentityEvidence.generalInfo2TypeValid = iEvidence.generalInfo2Len >= 12;
    if (mProductIdentityEvidence.generalInfo2TypeValid)
    {
      mProductIdentityEvidence.generalInfo2Profile = decodePackedProfile(
          iEvidence.generalInfo2[10], iEvidence.generalInfo2[11]);
      mProductIdentityEvidence.generalInfo2SubProfile =
          decodePackedSubProfile(iEvidence.generalInfo2[11]);
      mProductIdentityEvidence.generalInfo2MatchesDiscovery =
          mProtocolIdentity.valid &&
          mProductIdentityEvidence.generalInfo2Profile == mProtocolIdentity.profile &&
          mProductIdentityEvidence.generalInfo2SubProfile == mProtocolIdentity.subProfile;
    }
    ioHomeUpdateVendorProductEvidence(mProtocolIdentity, mProductIdentityEvidence);
  }
  const IoHomeProductIdentityEvidence &getProductIdentityEvidence() const { return mProductIdentityEvidence; }
  IoHomeProductSignature getGeneralInfo1ProductSignature() const
  {
    return ioHomeGeneralInfo1ProductSignature(mProductIdentityEvidence);
  }
  const char *getDeviceName() const { return mDeviceName; }
  void onProtocolIdentity(uint32_t iIoAddress,
                          const IoHomeProtocolIdentity &iIdentity)
  {
    if (!iIdentity.valid)
      return;
    mIoAddress = iIoAddress & 0x00FFFFFF;
    IoHomeProtocolIdentity lIdentity = iIdentity;
    lIdentity.ioAddress = mIoAddress;
    if (!ioHomeShouldAcceptProtocolIdentity(mProtocolIdentity, lIdentity))
      return;
    if (lIdentity.nodeClass == IoHomeNodeClass::Unknown &&
        mProtocolIdentity.valid && mProtocolIdentity.ioAddress == mIoAddress)
      lIdentity.nodeClass = mProtocolIdentity.nodeClass;
    mProtocolIdentity = lIdentity;
    ioHomeUpdateVendorProductEvidence(mProtocolIdentity, mProductIdentityEvidence);
  }
  void clearProtocolIdentity()
  {
    mIoAddress = 0;
    mProtocolIdentity = IoHomeProtocolIdentity{};
  }
  bool hasProtocolIdentity() const { return mProtocolIdentity.valid; }
  uint32_t getIoAddress() const
  {
    return mProtocolIdentity.valid ? mProtocolIdentity.ioAddress : mIoAddress;
  }
  const IoHomeProtocolIdentity &getProtocolIdentity() const { return mProtocolIdentity; }
  const IoHomeProfileDescriptor *getEffectiveProfileDescriptor() const
  {
    return mManualPackedProfile != 0
               ? ioHomeProfileDescriptor(mManualPackedProfile >> 6,
                                         mManualPackedProfile & 0x3F)
               : ioHomeProfileDescriptor(mProtocolIdentity);
  }
  void setManualProfileOverride(uint16_t iPackedType)
  {
    mManualPackedProfile = iPackedType != 0 &&
                                   ioHomeProfileDescriptor(iPackedType >> 6,
                                                           iPackedType & 0x3F)
                               ? iPackedType : 0;
  }
  bool allowsActuatorControls() const
  {
    return mProtocolIdentity.nodeClass == IoHomeNodeClass::Unknown ||
           mProtocolIdentity.nodeClass == IoHomeNodeClass::Actuator;
  }
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
  void onStatusPollFailed(bool iAfterChallenge)
  {
    mHasStatusPollFailure = true;
    mStatusPollFailureAfterChallenge = iAfterChallenge;
    mStatusPollFailureCount++;
    if (iAfterChallenge)
      mAuthPollFailureCount++;
    else
      mDirectPollFailureCount++;
  }
  void onCommandExchangeResult(IoHomeCommand iCommand, uint8_t iParam,
                               IoHomeCommandExchangeResult iResult)
  {
    mHasCommandExchangeResult = true;
    mLastCommandExchangeCommand = iCommand;
    mLastCommandExchangeParam = iParam;
    mLastCommandExchangeResult = iResult;
    if (iCommand == IoHomeCommand::Execute &&
        (iResult == IoHomeCommandExchangeResult::Completed ||
         iResult == IoHomeCommandExchangeResult::ExplicitlyRejected))
      mConfirmsExecute = true;
    if (iResult == IoHomeCommandExchangeResult::Completed ||
        iResult == IoHomeCommandExchangeResult::AuthenticatedUnconfirmed ||
        iResult == IoHomeCommandExchangeResult::ExplicitlyRejected)
    {
      mHas2WHeardEvidence = true;
      mLast2WHeardMs = ioHomeTestMillis();
    }
    const bool lAccepted = iResult == IoHomeCommandExchangeResult::Completed ||
                           iResult == IoHomeCommandExchangeResult::AuthenticatedUnconfirmed;
    if (iCommand == IoHomeCommand::Execute && iParam != 0xD2 && iParam != 0xD8 && lAccepted)
    {
      mHas2WMovingEvidence = true;
      mLast2WMovingEvidenceMs = ioHomeTestMillis();
    }
    if (iCommand == IoHomeCommand::Execute && iParam == 0xD2 &&
        (lAccepted || iResult == IoHomeCommandExchangeResult::Unknown))
    {
      mHas2WMovingEvidence = false;
      mStopSettlePollPending = true;
    }
  }
  bool confirmsExecute() const { return mConfirmsExecute; }
  TwoWayWakeBelief twoWayWakeBeliefAt(uint32_t iNowMs, bool iStopCommand = false) const
  {
    return ::twoWayWakeBelief(mHas2WMovingEvidence, mLast2WMovingEvidenceMs,
                              mHas2WHeardEvidence, mLast2WHeardMs,
                              iNowMs, iStopCommand);
  }
  bool twoWayLastHeardAgeAt(uint32_t iNowMs, uint32_t &oAgeMs) const
  {
    if (!mHas2WHeardEvidence)
      return false;
    oAgeMs = static_cast<uint32_t>(iNowMs - mLast2WHeardMs);
    return true;
  }
  bool hasStopSettlePollPending() const { return mStopSettlePollPending; }
  void onExchangeTimeout(bool iAuthenticatedUnconfirmed)
  {
    uint16_t &lCounter = iAuthenticatedUnconfirmed
                             ? mUnconfirmedExchangeCount
                             : mExchangeTimeoutCount;
    if (lCounter != UINT16_MAX)
      ++lCounter;
  }
  void scheduleStatusPoll(uint32_t iDelayMs)
  {
    mHasScheduledStatusPoll = true;
    mScheduledStatusPollCount++;
    mLastScheduledStatusPollMs = iDelayMs;
  }
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
  bool testHasScalarFeedback() const { return mHasScalarFeedback; }
  float testScalarFeedback() const { return mScalarFeedback; }
  bool testHasVelocityFeedback() const { return mHasVelocityFeedback; }
  ParameterSemantic testVelocitySemantic() const { return mVelocitySemantic; }
  float testVelocityFeedback() const { return mVelocityFeedback; }
  bool testHasBatteryLevel() const { return mHasBatteryLevel; }
  uint8_t testBatteryLevel() const { return mBatteryLevel; }
  bool testHasEstimate() const { return mHasEstimate; }
  uint8_t testEstimate() const { return mEstimate; }
  bool testStatusExpected() const { return mStatusExpected; }
  bool testHasStatusPollFailure() const { return mHasStatusPollFailure; }
  bool testStatusPollFailureAfterChallenge() const { return mStatusPollFailureAfterChallenge; }
  uint8_t testStatusPollFailureCount() const { return mStatusPollFailureCount; }
  uint8_t testAuthPollFailureCount() const { return mAuthPollFailureCount; }
  uint8_t testDirectPollFailureCount() const { return mDirectPollFailureCount; }
  bool testHasScheduledStatusPoll() const { return mHasScheduledStatusPoll; }
  uint8_t testScheduledStatusPollCount() const { return mScheduledStatusPollCount; }
  uint32_t testLastScheduledStatusPollMs() const { return mLastScheduledStatusPollMs; }
  bool testHasCommandExchangeResult() const { return mHasCommandExchangeResult; }
  IoHomeCommand testLastCommandExchangeCommand() const { return mLastCommandExchangeCommand; }
  uint8_t testLastCommandExchangeParam() const { return mLastCommandExchangeParam; }
  IoHomeCommandExchangeResult testLastCommandExchangeResult() const { return mLastCommandExchangeResult; }
  uint16_t exchangeTimeoutCount() const { return mExchangeTimeoutCount; }
  uint16_t unconfirmedExchangeCount() const { return mUnconfirmedExchangeCount; }

private:
  uint32_t mNodeId = 0;
  uint8_t mEncKey[16] = {};
  uint8_t mLastChallenge[6] = {};
  uint16_t mSequence1W = 0;
  uint16_t mReservedSequence1W = 0;
  uint32_t mOneWayControllerNodeId = 0;
  uint8_t mOneWayControllerKey[16] = {};
  uint8_t mOneWayControllerManufacturer = 2;
  uint8_t mConfigured1WManufacturer = 0;
  uint8_t mConfigured1WProfileChannel = 0xFF;
  bool mIs1W = false;
  bool mPaired = false;
  bool mLowPower2W = false;
  bool mHasLearnedLowPower2W = false;
  TwoWayPowerClass mConfigured2WPowerClass = TwoWayPowerClass::Automatic;
  PairingDiscoverConfirmMode mConfigured2WDiscoverConfirmMode = PairingDiscoverConfirmMode::Send;
  uint16_t mConfigured2WKeyInitDelay = 300;
  TwoWayDiscoverySettings mConfigured2WDiscoverySettings{};
  bool mOneWayEnrolled = false;
  uint32_t mConfigured1WTargetNodeId = 0;
  // TEST_NATIVE mirrors the production channel default: unspecified 1W type is
  // type 0 / All, which serializes to destination 0x00003F.
  uint8_t mConfigured1WBroadcastType = 0;
  uint8_t mConfigured1WAcei = 0; // mirrors production automatic-by-manufacturer default
  uint8_t mConfigured2WAcei = IOHC_ACEI_DEFAULT;
  uint32_t mIoAddress = 0;
  IoHomeProtocolIdentity mProtocolIdentity{};
  uint16_t mManualPackedProfile = 0;
  IoHomeProductIdentityEvidence mProductIdentityEvidence{};
  char mDeviceName[21] = {};
  bool mConfigured1WEnrollmentMac = false;
  OneWayEnrollmentFinalizer mConfigured1WEnrollmentFinalizer = OneWayEnrollmentFinalizer::Automatic;
  OneWayExecuteDestinationPolicy mConfigured1WExecuteDestinationPolicy = OneWayExecuteDestinationPolicy::Automatic;
  OneWayEnrollmentDestinationPolicy mConfigured1WEnrollmentDestinationPolicy = OneWayEnrollmentDestinationPolicy::Automatic;
  uint8_t mConfigured1WEnrollmentClassMask = 0;
  OneWayPowerClass mConfigured1WPowerClass = OneWayPowerClass::Automatic;
  bool mHasPositionFeedback = false;
  float mPositionFeedback = 0.0f;
  bool mHasTargetPositionFeedback = false;
  float mTargetPositionFeedback = 0.0f;
  bool mHasStatusUpdate = false;
  bool mStatusMoving = false;
  bool mHas2WHeardEvidence = false;
  bool mHas2WMovingEvidence = false;
  bool mConfirmsExecute = false;
  uint32_t mLast2WHeardMs = 0;
  uint32_t mLast2WMovingEvidenceMs = 0;
  bool mStopSettlePollPending = false;
  bool mHasSlatFeedback = false;
  float mSlatFeedback = 0.0f;
  bool mHasScalarFeedback = false;
  float mScalarFeedback = 0.0f;
  bool mHasVelocityFeedback = false;
  ParameterSemantic mVelocitySemantic = ParameterSemantic::Unknown;
  float mVelocityFeedback = 0.0f;
  bool mHasBatteryLevel = false;
  uint8_t mBatteryLevel = 0xFF;
  bool mHasEstimate = false;
  uint8_t mEstimate = 0xFF;
  bool mStatusExpected = false;
  bool mHasStatusPollFailure = false;
  bool mStatusPollFailureAfterChallenge = false;
  uint8_t mStatusPollFailureCount = 0;
  uint8_t mAuthPollFailureCount = 0;
  uint8_t mDirectPollFailureCount = 0;
  bool mHasScheduledStatusPoll = false;
  uint8_t mScheduledStatusPollCount = 0;
  uint32_t mLastScheduledStatusPollMs = 0;
  bool mHasCommandExchangeResult = false;
  IoHomeCommand mLastCommandExchangeCommand = IoHomeCommand::Execute;
  uint8_t mLastCommandExchangeParam = 0;
  IoHomeCommandExchangeResult mLastCommandExchangeResult = IoHomeCommandExchangeResult::Completed;
  uint16_t mExchangeTimeoutCount = 0;
  uint16_t mUnconfirmedExchangeCount = 0;
};

class IoHomecontrol
{
public:
  IoHomecontrolChannel *getChannel(uint8_t iIndex)
  {
    return (iIndex < IOHC_ChannelCount) ? mChannels[iIndex] : nullptr;
  }

  // Native test stub: the real module auto-provisions a missing 1W controller
  // profile here. Tests configure identities explicitly, so simply report
  // whether a usable identity already exists.
  bool ensureOneWayControllerProfile(IoHomecontrolChannel *iChannel)
  {
    return iChannel && iChannel->is1W() && iChannel->hasOneWayControllerIdentity();
  }

  void testSetChannel(uint8_t iIndex, IoHomecontrolChannel *iChannel)
  {
    if (iIndex < IOHC_ChannelCount)
      mChannels[iIndex] = iChannel;
  }

  IoHomeRemoteMap &remoteMap() { return mRemoteMap; }

  void onPassiveKeyCaptured(const IoHomeController::PassiveKeyResult &iResult)
  {
    mPassiveCaptureCount++;
    mLastPassiveKeyResult = iResult;
  }

  void onDiscoveryResponse(const IoHomeFrame &,
                           const IoHomeProtocolIdentity &) {}

  void onKeyImportCandidateObserved(uint32_t iNodeId)
  {
    mLastKeyImportCandidate = iNodeId;
    mKeyImportCandidateCount++;
  }

  void onAuthenticatedDirectedDiscovery(uint32_t iNodeId)
  {
    mLastAuthenticatedDirectedNode = iNodeId;
    mAuthenticatedDirectedCount++;
  }

  uint8_t testPassiveCaptureCount() const { return mPassiveCaptureCount; }
  const IoHomeController::PassiveKeyResult &testLastPassiveKeyResult() const { return mLastPassiveKeyResult; }
  uint8_t testKeyImportCandidateCount() const { return mKeyImportCandidateCount; }
  uint32_t testLastKeyImportCandidate() const { return mLastKeyImportCandidate; }
  uint8_t testAuthenticatedDirectedCount() const { return mAuthenticatedDirectedCount; }
  uint32_t testLastAuthenticatedDirectedNode() const { return mLastAuthenticatedDirectedNode; }

private:
  IoHomecontrolChannel *mChannels[IOHC_ChannelCount] = {};
  IoHomeRemoteMap mRemoteMap;
  uint8_t mPassiveCaptureCount = 0;
  IoHomeController::PassiveKeyResult mLastPassiveKeyResult = {};
  uint8_t mKeyImportCandidateCount = 0;
  uint32_t mLastKeyImportCandidate = 0;
  uint8_t mAuthenticatedDirectedCount = 0;
  uint32_t mLastAuthenticatedDirectedNode = 0;
};
