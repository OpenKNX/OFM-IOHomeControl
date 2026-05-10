#include "IoHomeController.h"

#ifdef TEST_NATIVE
#include "IoHomeControllerNativeStubs.h"
#else
#include "../IoHomecontrol.h"
#include "../IoHomecontrolChannel.h"
#include "../IoHomecontrolHardware.h"
#include "knxprod.h"
#endif

#ifdef ESP32
#include <Arduino.h>
#else
static unsigned long millis() { return 0; }
static unsigned long micros() { return 0; }
#endif

namespace
{
    constexpr uint8_t kPair1WControllerManufacturer = static_cast<uint8_t>(IoHomeManufacturer::Somfy);
    constexpr uint8_t kPair1WFreqIdx = 1;
    constexpr uint8_t kGatewayNameLen = 16;
    constexpr char kGatewayName[] = "MY_GATEWAY";
    constexpr uint8_t kGatewayDiscoverInfo = 0xCC;
    constexpr uint8_t kGatewayInfoSubtype = 0x01;
    constexpr uint16_t kGatewayInfoDeviceType = 0x0002;
    constexpr uint8_t kGatewayDiscoverManufacturer = static_cast<uint8_t>(IoHomeManufacturer::Overkiz);
    constexpr uint8_t kGatewayInfoManufacturer = static_cast<uint8_t>(IoHomeManufacturer::Somfy);

    bool isPairDiagnosticCommand(IoHomeCommand iCommand)
    {
        switch (iCommand)
        {
        case IoHomeCommand::DiscoverRequest:
        case IoHomeCommand::DiscoverResponse:
        case IoHomeCommand::DiscoverSPERequest:
        case IoHomeCommand::DiscoverSPEResponse:
        case IoHomeCommand::Confirmation:
        case IoHomeCommand::ConfirmationACK:
        case IoHomeCommand::SendKey1W:
        case IoHomeCommand::KeyInitTransfer:
        case IoHomeCommand::KeyTransfer:
        case IoHomeCommand::KeyTransferConfirmation:
        case IoHomeCommand::LaunchKeyTransfer:
        case IoHomeCommand::RemoveController:
        case IoHomeCommand::ChallengeRequest:
        case IoHomeCommand::ChallengeResponse:
        case IoHomeCommand::SetConfig1:
        case IoHomeCommand::SetConfig1Response:
        case IoHomeCommand::ErrorResponse:
            return true;
        default:
            return false;
        }
    }

    size_t buildHmacInput(const IoHomeFrame &iFrame, uint8_t *oBuffer, size_t iBufferLen)
    {
        const size_t lLen = static_cast<size_t>(iFrame.dataLen) + 1;
        if (iBufferLen < lLen)
            return 0;

        oBuffer[0] = static_cast<uint8_t>(iFrame.commandId);
        if (iFrame.dataLen > 0)
            memcpy(oBuffer + 1, iFrame.data, iFrame.dataLen);
        return lLen;
    }

    void initGatewayResponseFrame(IoHomeFrame &oFrame,
                                  uint32_t iGatewayNodeId,
                                  uint32_t iDeviceNodeId,
                                  IoHomeCommand iCommand)
    {
        oFrame.init();
        oFrame.ctrlByte0 = IOHC_CTRL0_END;
        oFrame.ctrlByte1 = 0x00; // version 0, matching real 2W box responses
        oFrame.setSrcNode(iGatewayNodeId);
        oFrame.setDestNode(iDeviceNodeId);
        oFrame.commandId = iCommand;
        oFrame.hasHmac = false;
    }

    bool buildGatewayEncryptedKey(const uint8_t iGatewayKey[16],
                                  const uint8_t iDeviceChallenge[6],
                                  uint8_t oEncryptedKey[16])
    {
        const uint8_t lKeyInitData[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
        uint8_t lKeystream[16];
        if (!IoHomeCrypto::crypt2WKey(lKeyInitData, sizeof(lKeyInitData),
                                      iDeviceChallenge, IOHC_TRANSFER_KEY,
                                      lKeystream))
        {
            return false;
        }

        for (uint8_t i = 0; i < 16; i++)
            oEncryptedKey[i] = iGatewayKey[i] ^ lKeystream[i];

        return true;
    }

    uint8_t buildGatewayDiscoverAnswerFrame(uint8_t *oBuffer,
                                            uint8_t iBufferLen,
                                            uint32_t iGatewayNodeId,
                                            uint32_t iDeviceNodeId)
    {
        IoHomeFrame lFrame;
        initGatewayResponseFrame(lFrame, iGatewayNodeId, iDeviceNodeId,
                                 IoHomeCommand::DiscoverResponse);
        lFrame.data[0] = 0xFF;
        lFrame.data[1] = 0xC0;
        lFrame.data[2] = (iGatewayNodeId >> 16) & 0xFF;
        lFrame.data[3] = (iGatewayNodeId >> 8) & 0xFF;
        lFrame.data[4] = iGatewayNodeId & 0xFF;
        lFrame.data[5] = kGatewayDiscoverManufacturer;
        lFrame.data[6] = kGatewayDiscoverInfo;
        lFrame.data[7] = 0x00;
        lFrame.data[8] = 0x00;
        lFrame.dataLen = 9;
        return lFrame.serialize(oBuffer, iBufferLen);
    }

    uint8_t buildGatewayDiscoverActuatorAckFrame(uint8_t *oBuffer,
                                                 uint8_t iBufferLen,
                                                 uint32_t iGatewayNodeId,
                                                 uint32_t iDeviceNodeId)
    {
        IoHomeFrame lFrame;
        initGatewayResponseFrame(lFrame, iGatewayNodeId, iDeviceNodeId,
                                 IoHomeCommand::ConfirmationACK);
        lFrame.dataLen = 0;
        return lFrame.serialize(oBuffer, iBufferLen);
    }

    uint8_t buildGatewayKeyTransferFrame(uint8_t *oBuffer,
                                         uint8_t iBufferLen,
                                         uint32_t iGatewayNodeId,
                                         uint32_t iDeviceNodeId,
                                         const uint8_t iEncryptedKey[16])
    {
        IoHomeFrame lFrame;
        initGatewayResponseFrame(lFrame, iGatewayNodeId, iDeviceNodeId,
                                 IoHomeCommand::KeyTransfer);
        memcpy(lFrame.data, iEncryptedKey, 16);
        lFrame.dataLen = 16;
        return lFrame.serialize(oBuffer, iBufferLen);
    }

    uint8_t buildGatewayChallengeAnswerFrame(uint8_t *oBuffer,
                                             uint8_t iBufferLen,
                                             uint32_t iGatewayNodeId,
                                             uint32_t iDeviceNodeId,
                                             uint8_t iMemCmd,
                                             const uint8_t *iMemData,
                                             uint8_t iMemDataLen,
                                             const uint8_t iPeerChallenge[6],
                                             const uint8_t iGatewayKey[16])
    {
        if (iMemCmd == 0 || iMemData == nullptr || iMemDataLen == 0)
            return 0;

        uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA] = {};
        lHmacInput[0] = iMemCmd;
        memcpy(lHmacInput + 1, iMemData, iMemDataLen);

        uint8_t lHmac[IOHC_HMAC_SIZE];
        if (!IoHomeCrypto::createHmac2W(lHmacInput, 1 + iMemDataLen,
                                        iPeerChallenge, iGatewayKey, lHmac))
        {
            return 0;
        }

        IoHomeFrame lFrame;
        initGatewayResponseFrame(lFrame, iGatewayNodeId, iDeviceNodeId,
                                 IoHomeCommand::ChallengeResponse);
        memcpy(lFrame.data, lHmac, sizeof(lHmac));
        lFrame.dataLen = sizeof(lHmac);
        return lFrame.serialize(oBuffer, iBufferLen);
    }

    uint8_t buildGatewayNameResponseFrame(uint8_t *oBuffer,
                                          uint8_t iBufferLen,
                                          uint32_t iGatewayNodeId,
                                          uint32_t iDeviceNodeId)
    {
        IoHomeFrame lFrame;
        initGatewayResponseFrame(lFrame, iGatewayNodeId, iDeviceNodeId,
                                 IoHomeCommand::GetNameResponse);
        memset(lFrame.data, 0, kGatewayNameLen);
        memcpy(lFrame.data, kGatewayName, sizeof(kGatewayName) - 1);
        lFrame.dataLen = kGatewayNameLen;
        return lFrame.serialize(oBuffer, iBufferLen);
    }

    uint8_t buildGatewaySetNameResponseFrame(uint8_t *oBuffer,
                                             uint8_t iBufferLen,
                                             uint32_t iGatewayNodeId,
                                             uint32_t iDeviceNodeId)
    {
        IoHomeFrame lFrame;
        initGatewayResponseFrame(lFrame, iGatewayNodeId, iDeviceNodeId,
                                 IoHomeCommand::SetNameResponse);
        lFrame.dataLen = 0;
        return lFrame.serialize(oBuffer, iBufferLen);
    }

    uint8_t buildGatewayGetGeneralInfo1ResponseFrame(uint8_t *oBuffer,
                                                     uint8_t iBufferLen,
                                                     uint32_t iGatewayNodeId,
                                                     uint32_t iDeviceNodeId)
    {
        IoHomeFrame lFrame;
        initGatewayResponseFrame(lFrame, iGatewayNodeId, iDeviceNodeId,
                                 IoHomeCommand::GetGeneralInfo1Response);
        lFrame.data[0] = kGatewayInfoDeviceType & 0xFF;
        lFrame.data[1] = ((kGatewayInfoDeviceType >> 8) & 0x03) |
                         ((kGatewayInfoSubtype & 0x3F) << 2);
        lFrame.data[2] = kGatewayInfoManufacturer;
        lFrame.dataLen = 3;
        return lFrame.serialize(oBuffer, iBufferLen);
    }
}

IoHomeController::IoHomeController()
    : mModule(nullptr), mOwnNodeId(0),
      mState(ControllerState::Idle), mStateTimer(0),
      mCurrentFreqIdx(0), mQueueHead(0), mQueueTail(0),
      mPairingChannel(0), mDiscoveredNodeId(0),
      mPairingFreqIdx(0), mPairingStartTime(0),
      mDiscoverySendPhase(DiscoverySendPhase::SetFrequency),
      mDiscoveryTimingTrace{},
      mDiscoverySPE(false),
      mAuthSrcNodeId(0), mAuthChannelIdx(0),
      mStatusAckDestNodeId(0), mStatusAckFreqIdx(0),
      mPassiveMode(false), mPassivePairNodeId(0),
      mGatewayMode(false), mGatewayNodeId(0),
      mGatewayState(ControllerState::GatewayIdle),
      mGatewayPeerNodeId(0),
      mGatewayMemCmd(0), mGatewayMemDataLen(0),
      mGatewayDiscoverFreqIdx(0),
      mScanIndex(0), mScanTargetNode(0),
      mDutyCycleWindowStart(0),
      mRxScanLastSwitch(0), mRxScanIntervalUs(IOHC_RX_SCAN_INTERVAL_US),
      mRxScanEnabled(true), mLastResponseFreqIdx(0),
      mPairDiagnosticTraceEnabled(false),
      mLastPairDiagnosticTraceState(ControllerState::Idle)
{
    memset(mSystemKey, 0, sizeof(mSystemKey));
    memset(mCmdQueue, 0, sizeof(mCmdQueue));
    memset(mTxTimeAccum, 0, sizeof(mTxTimeAccum));
    memset(mPairingChallenge, 0, sizeof(mPairingChallenge));
    memset(mPairSetConfigChallenge, 0, sizeof(mPairSetConfigChallenge));
    memset(mPairKeyTransferChallenge, 0, sizeof(mPairKeyTransferChallenge));
    memset(mPairPulledKey, 0, sizeof(mPairPulledKey));
    memset(mPairPullAuthChallenge, 0, sizeof(mPairPullAuthChallenge));
    memset(&mCurrentCmd, 0, sizeof(mCurrentCmd));
    memset(mAuthChallenge, 0, sizeof(mAuthChallenge));
    memset(mPassiveChallenge, 0, sizeof(mPassiveChallenge));
    memset(mGatewayKey, 0, sizeof(mGatewayKey));
    memset(mGatewayKeyEncrypted, 0, sizeof(mGatewayKeyEncrypted));
    memset(mGatewayMemData, 0, sizeof(mGatewayMemData));
    memset(mGatewayPeerChallenge, 0, sizeof(mGatewayPeerChallenge));
    memset(mGatewayPairedNodeIds, 0, sizeof(mGatewayPairedNodeIds));
    mGatewayDeviceCount = 0;
    mPairSetConfigRequest.init();
    mPairLaunchKeyTransferFrame.init();
    mPairPulledKeyFrame.init();
    memset(mScanBuffer, 0, sizeof(mScanBuffer));
    mScanBufferHead = 0;
    mNetworkScanActive = false;
    memset(mNodeStats, 0, sizeof(mNodeStats));
}

const std::string IoHomeController::logPrefix() const
{
    return "IoHC-Ctrl";
}

void IoHomeController::init()
{
#if defined(RADIO_SX1262)
    logInfoP("SX1262 GPIOs: SCK=%d MISO=%d MOSI=%d NSS=%d RST=%d DIO1=%d BUSY=%d RF_SW=%d",
             IOHC_SPI_SCK, IOHC_SPI_MISO, IOHC_SPI_MOSI,
             IOHC_SPI_CS, IOHC_RADIO_RST, IOHC_RADIO_DIO1, IOHC_RADIO_BUSY,
#ifdef IOHC_RADIO_RF_SW
             IOHC_RADIO_RF_SW
#else
             -1
#endif
    );
    mRadio.init(IOHC_SPI_CS, IOHC_RADIO_RST, IOHC_RADIO_DIO1, IOHC_RADIO_BUSY);
#else
    logInfoP("SX1276 GPIOs: SCK=%d MISO=%d MOSI=%d NSS=%d RST=%d DIO0=%d DIO4=%d",
             IOHC_SPI_SCK, IOHC_SPI_MISO, IOHC_SPI_MOSI,
             IOHC_SPI_CS, IOHC_RADIO_RST, IOHC_RADIO_DIO0, IOHC_RADIO_DIO4);
    mRadio.init(IOHC_SPI_CS, IOHC_RADIO_RST, IOHC_RADIO_DIO0, IOHC_RADIO_DIO4);
#endif
    mState = ControllerState::Idle;
    mDutyCycleWindowStart = millis();
}

void IoHomeController::setModule(IoHomecontrol *iModule)
{
    mModule = iModule;
}

void IoHomeController::setOwnNodeId(uint32_t iNodeId)
{
    mOwnNodeId = iNodeId & 0x00FFFFFF;
}

uint32_t IoHomeController::getOwnNodeId() const
{
    return mOwnNodeId;
}

void IoHomeController::setSystemKey(const uint8_t *iKey)
{
    memcpy(mSystemKey, iKey, 16);
}

const uint8_t *IoHomeController::getSystemKey() const
{
    return mSystemKey;
}

ControllerState IoHomeController::state() const
{
    return mState;
}

int16_t IoHomeController::lastRssi() const
{
    return mRadio.lastRssi();
}

Radio &IoHomeController::radio()
{
    return mRadio;
}

const Radio &IoHomeController::radio() const
{
    return mRadio;
}

RadioError IoHomeController::startReceive()
{
    return mRadio.startReceive();
}

void IoHomeController::sleep()
{
    mRadio.sleep();
}

// --- Command queue ---

bool IoHomeController::sendCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                   IoHomeCommand iCmd, uint8_t iParam)
{
    return sendCommand(iDestNodeId, iEncKey, iCmd, iParam, 0xFF, 0xFF);
}

bool IoHomeController::sendCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                   IoHomeCommand iCmd, uint8_t iParam, uint8_t iParam2)
{
    return sendCommand(iDestNodeId, iEncKey, iCmd, iParam, iParam2, 0xFF);
}

bool IoHomeController::sendCommand(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                   IoHomeCommand iCmd, uint8_t iParam, uint8_t iParam2, uint8_t iParam3)
{
    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iDestNodeId;
    lEntry.encKey = iEncKey;
    lEntry.command = iCmd;
    lEntry.param = iParam;
    lEntry.param2 = iParam2;
    lEntry.param3 = iParam3;
    lEntry.retries = 0;
    lEntry.active = true;
    return queuePush(lEntry);
}

bool IoHomeController::sendSetName(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                   const char *iName, uint8_t iNameLen)
{
    if (iNameLen > IOHC_NAME_MAX_SIZE)
        iNameLen = IOHC_NAME_MAX_SIZE;

    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iDestNodeId;
    lEntry.encKey = iEncKey;
    lEntry.command = IoHomeCommand::SetName;
    lEntry.retries = 0;
    lEntry.active = true;
    memcpy(lEntry.nameData, iName, iNameLen);
    lEntry.nameLen = iNameLen;
    return queuePush(lEntry);
}

bool IoHomeController::queuePush(const IoHomeQueueEntry &iEntry)
{
    uint8_t lNext = (mQueueHead + 1) % IOHC_CMD_QUEUE_SIZE;
    if (lNext == mQueueTail)
        return false; // queue full
    mCmdQueue[mQueueHead] = iEntry;
    mQueueHead = lNext;
    return true;
}

bool IoHomeController::queuePop(IoHomeQueueEntry &oEntry)
{
    if (mQueueTail == mQueueHead)
        return false; // queue empty
    oEntry = mCmdQueue[mQueueTail];
    mQueueTail = (mQueueTail + 1) % IOHC_CMD_QUEUE_SIZE;
    return true;
}

bool IoHomeController::queueEmpty() const
{
    return mQueueTail == mQueueHead;
}

// --- Pairing ---

bool IoHomeController::startPairing(uint8_t iChannelIndex, uint32_t iKnownNodeId)
{
    mLastPairStartStatus = PairStartStatus::Ok;
    mLastPairStartBlockedState = mState;

    if (mState >= ControllerState::PairSendDiscovery &&
        mState <= ControllerState::PairFailed)
    {
        mLastPairStartStatus = PairStartStatus::Busy;
        return false;
    }

    if (mState != ControllerState::Idle)
    {
        logInfoP("Pairing: aborting controller state %s before starting pairing", stateName(mState));
        mQueueTail = mQueueHead;
        memset(&mCurrentCmd, 0, sizeof(mCurrentCmd));
        mAuthResponseSent = false;
        mNetworkScanActive = false;
        mPassiveMode = false;
        mScanIndex = 0;
        mScanTargetNode = 0;
        mDiscoverySPE = false;
        mTx1WRepeatRemaining = 0;
        mTx1WRepeatTimer = 0;
        mRadio.standby();
        mState = ControllerState::Idle;
        startReceive();
    }

    mPairingChannel = iChannelIndex;
    mPairingFreqIdx = 0;
    mPairingStartTime = millis();
    memset(mPairingChallenge, 0, sizeof(mPairingChallenge)); // will be filled by device's ChallengeRequest
    memset(mPairSetConfigChallenge, 0, sizeof(mPairSetConfigChallenge));
    memset(mPairKeyTransferChallenge, 0, sizeof(mPairKeyTransferChallenge));
    memset(mPairPulledKey, 0, sizeof(mPairPulledKey));
    memset(mPairPullAuthChallenge, 0, sizeof(mPairPullAuthChallenge));
    mPairLaunchKeyTransferFrame.init();
    mPairPulledKeyFrame.init();
    mDiscoverySPE = false;
    mDiscoveredNodeId = 0;
    mTx1WRepeatRemaining = 0;
    mTx1WRepeatTimer = 0;

    IoHomecontrolChannel *lCh = mModule ? mModule->getChannel(iChannelIndex) : nullptr;
    if (lCh && lCh->is1W())
    {
        uint32_t lKnownNodeId = iKnownNodeId & 0x00FFFFFF;
        if (lKnownNodeId == 0)
        {
            if (lCh->isPaired())
                lKnownNodeId = lCh->getNodeId();
            else
                lKnownNodeId = lCh->getConfigured1WTargetNodeId();
        }
        if (lKnownNodeId == 0)
        {
            mLastPairStartStatus = PairStartStatus::Missing1WTarget;
            return false;
        }

        mDiscoveredNodeId = lKnownNodeId;
        mState = ControllerState::PairSend1WRemove;
        if (mPairDiagnosticTraceEnabled)
        {
            logInfoP("PairDiag: starting 1W pairing ch=%u target=0x%06X", static_cast<unsigned>(iChannelIndex + 1), mDiscoveredNodeId);
            tracePairDiagnosticStateChange();
        }
        return true;
    }

    mState = ControllerState::PairSendDiscovery;
    if (mPairDiagnosticTraceEnabled)
    {
        logInfoP("PairDiag: starting 2W pairing ch=%u known=0x%06X", static_cast<unsigned>(iChannelIndex + 1), iKnownNodeId & 0x00FFFFFF);
        tracePairDiagnosticStateChange();
    }
    return true;
}

IoHomeController::PairStartStatus IoHomeController::lastPairStartStatus() const
{
    return mLastPairStartStatus;
}

ControllerState IoHomeController::lastPairStartBlockedState() const
{
    return mLastPairStartBlockedState;
}

void IoHomeController::cancelPairing()
{
    if (mState >= ControllerState::PairSendDiscovery &&
        mState <= ControllerState::PairFailed)
    {
        mState = ControllerState::Idle;
        startReceive();
        tracePairDiagnosticStateChange();
    }
}

void IoHomeController::startDiscovery()
{
    if (mState != ControllerState::Idle)
        return;

    mPairingFreqIdx = 0;
    mPairingStartTime = millis();
    mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
    resetDiscoveryTimingTrace();
    mDiscoverySPE = false;
    mState = ControllerState::DiscoverySending;
    if (mPairDiagnosticTraceEnabled)
    {
        logInfoP("PairDiag: starting discovery broadcast");
        tracePairDiagnosticStateChange();
    }
}

void IoHomeController::startDiscoverySPE()
{
    if (mState != ControllerState::Idle)
        return;

    mPairingFreqIdx = 0;
    mPairingStartTime = millis();
    mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
    resetDiscoveryTimingTrace();
    mDiscoverySPE = true;
    mState = ControllerState::DiscoverySending;
    if (mPairDiagnosticTraceEnabled)
    {
        logInfoP("PairDiag: starting encrypted discovery broadcast");
        tracePairDiagnosticStateChange();
    }
}

void IoHomeController::startCommandScan(uint32_t iNodeId)
{
    if (mState != ControllerState::Idle)
        return;

    mScanTargetNode = iNodeId;
    mScanIndex = 0;
    memset(mScanResult.bitmask, 0, sizeof(mScanResult.bitmask));
    mScanResult.targetNodeId = iNodeId;
    mScanResult.commandsScanned = IOHC_SCAN_COMMANDS_COUNT;
    mScanResult.responsesFound = 0;
    mState = ControllerState::ScanSending;
}

void IoHomeController::setPassiveMode(bool iEnabled)
{
    mPassiveMode = iEnabled;
    if (iEnabled)
    {
        mState = ControllerState::PassiveListening;
        startReceive();
    }
    else
    {
        mState = ControllerState::Idle;
    }
}

bool IoHomeController::isPassiveMode() const
{
    return mPassiveMode;
}

void IoHomeController::setGatewayMode(bool iEnabled)
{
    mGatewayMode = iEnabled;
    mGatewayState = ControllerState::GatewayIdle;
    mGatewayPeerNodeId = 0;
    mGatewayMemCmd = 0;
    mGatewayMemDataLen = 0;
    memset(mGatewayKeyEncrypted, 0, sizeof(mGatewayKeyEncrypted));
    memset(mGatewayMemData, 0, sizeof(mGatewayMemData));
    memset(mGatewayPeerChallenge, 0, sizeof(mGatewayPeerChallenge));

    if (iEnabled && mState == ControllerState::Idle)
        startReceive();
}

bool IoHomeController::isGatewayMode() const
{
    return mGatewayMode;
}

void IoHomeController::setGatewayNodeId(uint32_t iNodeId)
{
    mGatewayNodeId = iNodeId & 0x00FFFFFF;
}

uint32_t IoHomeController::getGatewayNodeId() const
{
    return mGatewayNodeId;
}

void IoHomeController::setGatewayKey(const uint8_t *iKey)
{
    if (iKey != nullptr)
        memcpy(mGatewayKey, iKey, sizeof(mGatewayKey));
}

const uint8_t *IoHomeController::getGatewayKey() const
{
    return mGatewayKey;
}

uint8_t IoHomeController::getGatewayPairedDeviceCount() const
{
    return mGatewayDeviceCount;
}

uint32_t IoHomeController::getGatewayPairedNodeId(uint8_t iIndex) const
{
    if (iIndex >= mGatewayDeviceCount || iIndex >= kMaxGatewayPairedDevices)
        return 0;
    return mGatewayPairedNodeIds[iIndex];
}

void IoHomeController::clearGatewayPairedDevices()
{
    memset(mGatewayPairedNodeIds, 0, sizeof(mGatewayPairedNodeIds));
    mGatewayDeviceCount = 0;
    mGatewayPeerNodeId = 0;
    mGatewayState = ControllerState::GatewayIdle;
    mGatewayMemCmd = 0;
    mGatewayMemDataLen = 0;
    memset(mGatewayKeyEncrypted, 0, sizeof(mGatewayKeyEncrypted));
    memset(mGatewayMemData, 0, sizeof(mGatewayMemData));
    memset(mGatewayPeerChallenge, 0, sizeof(mGatewayPeerChallenge));
}

void IoHomeController::setPairDiagnosticTraceEnabled(bool iEnabled)
{
    mPairDiagnosticTraceEnabled = iEnabled;
    mLastPairDiagnosticTraceState = mState;
}

bool IoHomeController::isPairDiagnosticTraceEnabled() const
{
    return mPairDiagnosticTraceEnabled;
}

void IoHomeController::setRxScanEnabled(bool iEnabled)
{
    mRxScanEnabled = iEnabled;
    if (iEnabled)
        mRxScanLastSwitch = micros();
}

bool IoHomeController::isRxScanEnabled() const
{
    return mRxScanEnabled;
}

uint8_t IoHomeController::lastResponseFreqIdx() const
{
    return mLastResponseFreqIdx;
}

// --- Command scan results ---

const IoHomeController::IoHomeCommandScanResult *
IoHomeController::commandScanResults() const
{
    return &mScanResult;
}

bool IoHomeController::commandHasResponse(uint8_t iCmdIndex) const
{
    if (iCmdIndex >= IOHC_SCAN_COMMANDS_COUNT)
        return false;
    uint8_t byteIdx = iCmdIndex / 8;
    uint8_t bitIdx = iCmdIndex % 8;
    return (mScanResult.bitmask[byteIdx] >> bitIdx) & 0x01;
}

bool IoHomeController::commandSupported(uint8_t iCmdCode) const
{
    uint8_t byteIdx = iCmdCode / 8;
    uint8_t bitIdx = iCmdCode % 8;
    if (byteIdx >= 32)
        return false;
    return (mScanResult.bitmask[byteIdx] >> bitIdx) & 0x01;
}

void IoHomeController::recordCommandScanResponse(uint8_t iCmdIndex)
{
    if (iCmdIndex >= IOHC_SCAN_COMMANDS_COUNT)
        return;
    uint8_t byteIdx = iCmdIndex / 8;
    uint8_t bitIdx = iCmdIndex % 8;
    uint8_t lBit = (mScanResult.bitmask[byteIdx] >> bitIdx) & 0x01;
    mScanResult.bitmask[byteIdx] |= (1 << bitIdx);
    if (!lBit)
        mScanResult.responsesFound++;
}

void IoHomeController::logCommandScanResults() const
{
    if (mScanResult.commandsScanned == 0)
        return;

    logInfoP("Command scan results for 0x%06X:", mScanResult.targetNodeId);
    logInfoP("  Commands: %u/%u responded", mScanResult.responsesFound, mScanResult.commandsScanned);
    logInfoP("  Supported commands:");

    for (uint8_t i = 0; i < mScanResult.commandsScanned; i++)
    {
        if (commandHasResponse(i))
        {
            uint8_t cmd = IOHC_SCAN_COMMANDS[i];
            logInfoP("    0x%02X (%s)", cmd, commandName(static_cast<IoHomeCommand>(cmd)));
        }
    }
}

// --- Network scan ---

void IoHomeController::startNetworkScan()
{
    mNetworkScanActive = true;
    mRxScanEnabled = true;
    setPassiveMode(true);
}

void IoHomeController::stopNetworkScan()
{
    mNetworkScanActive = false;
    setPassiveMode(false);
}

bool IoHomeController::isNetworkScanActive() const
{
    return mNetworkScanActive;
}

const IoHomeController::IoHomeScanEntry *IoHomeController::scanBuffer() const
{
    return mScanBuffer;
}

uint8_t IoHomeController::scanBufferHead() const
{
    return mScanBufferHead;
}

const IoHomeController::IoHomeNodeStats *IoHomeController::nodeStats() const
{
    return mNodeStats;
}

void IoHomeController::recordScanFrame(const IoHomeFrame &iFrame, int16_t iRssi, uint8_t iFreqIdx)
{
    mScanBuffer[mScanBufferHead].timestamp = millis();
    mScanBuffer[mScanBufferHead].frame = iFrame;
    mScanBuffer[mScanBufferHead].rssi = iRssi;
    mScanBuffer[mScanBufferHead].freqIdx = iFreqIdx;
    mScanBuffer[mScanBufferHead].valid = true;
    mScanBufferHead = (mScanBufferHead + 1) % kScanBufferSize;
}

void IoHomeController::updateNodeStats(uint32_t iNodeId, int16_t iRssi, IoHomeCommand iCmd)
{
    // Find existing entry or first empty slot
    int8_t lFreeSlot = -1;
    for (uint8_t i = 0; i < kMaxTrackedNodes; i++)
    {
        if (mNodeStats[i].active && mNodeStats[i].nodeId == iNodeId)
        {
            mNodeStats[i].packetCount++;
            mNodeStats[i].lastRssi = iRssi;
            mNodeStats[i].lastCommand = iCmd;
            return;
        }
        if (!mNodeStats[i].active && lFreeSlot < 0)
            lFreeSlot = i;
    }
    if (lFreeSlot >= 0)
    {
        mNodeStats[lFreeSlot].active = true;
        mNodeStats[lFreeSlot].nodeId = iNodeId;
        mNodeStats[lFreeSlot].packetCount = 1;
        mNodeStats[lFreeSlot].lastRssi = iRssi;
        mNodeStats[lFreeSlot].lastCommand = iCmd;
    }
    // If full, drop the new entry (graceful degradation)
}

const char *IoHomeController::commandName(IoHomeCommand iCmd)
{
    switch (iCmd)
    {
    case IoHomeCommand::Execute:
        return "Execute";
    case IoHomeCommand::ActivateMode:
        return "ActivateMode";
    case IoHomeCommand::DirectCommand:
        return "DirectCommand";
    case IoHomeCommand::Private:
        return "Private";
    case IoHomeCommand::PrivateResponse:
        return "PrivateResponse";
    case IoHomeCommand::Private2:
        return "Private2";
    case IoHomeCommand::Private2Response:
        return "Private2Response";
    case IoHomeCommand::WritePrivate:
        return "WritePrivate";
    case IoHomeCommand::WritePrivateResponse:
        return "WritePrivateResponse";
    case IoHomeCommand::DiscoverRequest:
        return "DiscoverRequest";
    case IoHomeCommand::DiscoverResponse:
        return "DiscoverResponse";
    case IoHomeCommand::DiscoverSPERequest:
        return "DiscoverSPERequest";
    case IoHomeCommand::DiscoverSPEResponse:
        return "DiscoverSPEResponse";
    case IoHomeCommand::Confirmation:
        return "Confirmation";
    case IoHomeCommand::ConfirmationACK:
        return "ConfirmationACK";
    case IoHomeCommand::Discover2ERequest:
        return "Discover2ERequest";
    case IoHomeCommand::SendKey1W:
        return "SendKey1W";
    case IoHomeCommand::KeyInitTransfer:
        return "KeyInitTransfer";
    case IoHomeCommand::KeyTransfer:
        return "KeyTransfer";
    case IoHomeCommand::KeyTransferConfirmation:
        return "KeyTransferConfirmation";
    case IoHomeCommand::AddressRequest:
        return "AddressRequest";
    case IoHomeCommand::AddressResponse:
        return "AddressResponse";
    case IoHomeCommand::LaunchKeyTransfer:
        return "LaunchKeyTransfer";
    case IoHomeCommand::RemoveController:
        return "RemoveController";
    case IoHomeCommand::ChallengeRequest:
        return "ChallengeRequest";
    case IoHomeCommand::ChallengeResponse:
        return "ChallengeResponse";
    case IoHomeCommand::GetName:
        return "GetName";
    case IoHomeCommand::GetNameResponse:
        return "GetNameResponse";
    case IoHomeCommand::SetName:
        return "SetName";
    case IoHomeCommand::SetNameResponse:
        return "SetNameResponse";
    case IoHomeCommand::GetGeneralInfo1:
        return "GetGeneralInfo1";
    case IoHomeCommand::GetGeneralInfo1Response:
        return "GetGeneralInfo1Response";
    case IoHomeCommand::GetGeneralInfo2:
        return "GetGeneralInfo2";
    case IoHomeCommand::GetGeneralInfo2Response:
        return "GetGeneralInfo2Response";
    case IoHomeCommand::GetGeneralInfo3:
        return "GetGeneralInfo3";
    case IoHomeCommand::GetGeneralInfo3Response:
        return "GetGeneralInfo3Response";
    case IoHomeCommand::SetConfig1:
        return "SetConfig1";
    case IoHomeCommand::SetConfig1Response:
        return "SetConfig1Response";
    case IoHomeCommand::StatusUpdate:
        return "StatusUpdate";
    case IoHomeCommand::StatusUpdateResponse:
        return "StatusUpdateResponse";
    case IoHomeCommand::ErrorResponse:
        return "ErrorResponse";
    default:
        return "Unknown";
    }
}

const char *IoHomeController::stateName(ControllerState iState)
{
    switch (iState)
    {
    case ControllerState::Idle:
        return "Idle";
    case ControllerState::TxPending:
        return "TxPending";
    case ControllerState::TxInProgress:
        return "TxInProgress";
    case ControllerState::Tx1WRepeat:
        return "Tx1WRepeat";
    case ControllerState::WaitResponse:
        return "WaitResponse";
    case ControllerState::ProcessResponse:
        return "ProcessResponse";
    case ControllerState::PairSendDiscovery:
        return "PairSendDiscovery";
    case ControllerState::PairWaitDiscoveryResponse:
        return "PairWaitDiscoveryResponse";
    case ControllerState::PairSendDiscoveryConfirmation:
        return "PairSendDiscoveryConfirmation";
    case ControllerState::PairWaitDiscoveryConfirmationAck:
        return "PairWaitDiscoveryConfirmationAck";
    case ControllerState::PairSendLaunchKeyTransfer:
        return "PairSendLaunchKeyTransfer";
    case ControllerState::PairWaitLaunchKeyTransfer:
        return "PairWaitLaunchKeyTransfer";
    case ControllerState::PairSendPullKeyChallenge:
        return "PairSendPullKeyChallenge";
    case ControllerState::PairWaitPullKeyChallengeResponse:
        return "PairWaitPullKeyChallengeResponse";
    case ControllerState::PairSend1WRemove:
        return "PairSend1WRemove";
    case ControllerState::PairWait1WRemove:
        return "PairWait1WRemove";
    case ControllerState::PairSend1WKeyTransfer:
        return "PairSend1WKeyTransfer";
    case ControllerState::PairWait1WKeyTransfer:
        return "PairWait1WKeyTransfer";
    case ControllerState::PairSendKeyInit:
        return "PairSendKeyInit";
    case ControllerState::PairWaitDeviceChallenge:
        return "PairWaitDeviceChallenge";
    case ControllerState::PairSendKeyTransfer:
        return "PairSendKeyTransfer";
    case ControllerState::PairSendKeyTransferAuthResponse:
        return "PairSendKeyTransferAuthResponse";
    case ControllerState::PairWaitKeyTransferConfirmation:
        return "PairWaitKeyTransferConfirmation";
    case ControllerState::PairSendSetConfig1:
        return "PairSendSetConfig1";
    case ControllerState::PairWaitSetConfig1Response:
        return "PairWaitSetConfig1Response";
    case ControllerState::PairSendSetConfig1AuthResponse:
        return "PairSendSetConfig1AuthResponse";
    case ControllerState::PairWaitSetConfig1FinalResponse:
        return "PairWaitSetConfig1FinalResponse";
    case ControllerState::PairComplete:
        return "PairComplete";
    case ControllerState::PairFailed:
        return "PairFailed";
    case ControllerState::GatewayIdle:
        return "GatewayIdle";
    case ControllerState::GatewayWaitDiscoveryResponse:
        return "GatewayWaitDiscoveryResponse";
    case ControllerState::GatewayWaitKeyTransfer:
        return "GatewayWaitKeyTransfer";
    case ControllerState::GatewayWaitChallenge:
        return "GatewayWaitChallenge";
    case ControllerState::AuthSendChallenge:
        return "AuthSendChallenge";
    case ControllerState::AuthWaitResponse:
        return "AuthWaitResponse";
    case ControllerState::StatusAckSend:
        return "StatusAckSend";
    case ControllerState::StatusAckTxWait:
        return "StatusAckTxWait";
    case ControllerState::DiscoverySending:
        return "DiscoverySending";
    case ControllerState::DiscoveryListening:
        return "DiscoveryListening";
    case ControllerState::ScanSending:
        return "ScanSending";
    case ControllerState::ScanWaitResponse:
        return "ScanWaitResponse";
    case ControllerState::PassiveListening:
        return "PassiveListening";
    default:
        return "Unknown";
    }
}

bool IoHomeController::isPairDiagnosticState(ControllerState iState) const
{
    return (iState >= ControllerState::PairSendDiscovery && iState <= ControllerState::PairFailed) ||
           iState == ControllerState::DiscoverySending ||
           iState == ControllerState::DiscoveryListening;
}

void IoHomeController::tracePairDiagnosticFrame(const char *iPrefix, const IoHomeFrame &iFrame, uint8_t iFreqIdx, int16_t iRssi) const
{
    if (!mPairDiagnosticTraceEnabled)
        return;

    const uint32_t lFreqHz = (iFreqIdx < IOHC_NUM_FREQUENCIES) ? IOHC_FREQUENCIES[iFreqIdx] : 0;
    logInfoP("PairDiag: %s cmd=%s(0x%02X) src=0x%06X dst=0x%06X freq=%u %luHz rssi=%d len=%u hmac=%d",
             iPrefix,
             commandName(iFrame.commandId),
             static_cast<unsigned>(iFrame.commandId),
             iFrame.getSrcNodeId(),
             iFrame.getDestNodeId(),
             iFreqIdx,
             static_cast<unsigned long>(lFreqHz),
             iRssi,
             iFrame.dataLen,
             iFrame.hasHmac ? 1 : 0);
}

void IoHomeController::tracePairDiagnosticStateChange()
{
    if (!mPairDiagnosticTraceEnabled || mState == mLastPairDiagnosticTraceState)
        return;

    const bool lRelevant = isPairDiagnosticState(mState) || isPairDiagnosticState(mLastPairDiagnosticTraceState);
    const ControllerState lPrevState = mLastPairDiagnosticTraceState;
    mLastPairDiagnosticTraceState = mState;
    if (!lRelevant)
        return;

    const uint32_t lPairFreqHz = (mPairingFreqIdx < IOHC_NUM_FREQUENCIES) ? IOHC_FREQUENCIES[mPairingFreqIdx] : 0;
    const unsigned long lElapsedMs = static_cast<unsigned long>(
        isPairDiagnosticState(mState) && mPairingStartTime != 0 ? (millis() - mPairingStartTime) : 0UL);

    logInfoP("PairDiag: state %s -> %s ch=%u node=0x%06X pairFreq=%u %luHz lastRespFreq=%u elapsed=%lums spe=%d rxScan=%d",
             stateName(lPrevState),
             stateName(mState),
             static_cast<unsigned>(mPairingChannel + 1),
             mDiscoveredNodeId,
             static_cast<unsigned>(mPairingFreqIdx),
             static_cast<unsigned long>(lPairFreqHz),
             static_cast<unsigned>(mLastResponseFreqIdx),
             lElapsedMs,
             mDiscoverySPE ? 1 : 0,
             mRxScanEnabled ? 1 : 0);
}

void IoHomeController::logPairDiagnosticStatus() const
{
    const uint32_t lPairFreqHz = (mPairingFreqIdx < IOHC_NUM_FREQUENCIES) ? IOHC_FREQUENCIES[mPairingFreqIdx] : 0;
    const uint32_t lCurrentFreqHz = (mCurrentFreqIdx < IOHC_NUM_FREQUENCIES) ? IOHC_FREQUENCIES[mCurrentFreqIdx] : 0;
    const unsigned long lElapsedMs = static_cast<unsigned long>(
        (isPairDiagnosticState(mState) && mPairingStartTime != 0) ? (millis() - mPairingStartTime) : 0UL);
    const IoHomeRadioHealth lHealth = radioHealth();

    logInfoP("PairDiag: trace=%s state=%s ch=%u node=0x%06X pairFreq=%u %luHz currentFreq=%u %luHz lastRespFreq=%u elapsed=%lums spe=%d passive=%d rxScan=%d",
             mPairDiagnosticTraceEnabled ? "on" : "off",
             stateName(mState),
             static_cast<unsigned>(mPairingChannel + 1),
             mDiscoveredNodeId,
             static_cast<unsigned>(mPairingFreqIdx),
             static_cast<unsigned long>(lPairFreqHz),
             static_cast<unsigned>(mCurrentFreqIdx),
             static_cast<unsigned long>(lCurrentFreqHz),
             static_cast<unsigned>(mLastResponseFreqIdx),
             lElapsedMs,
             mDiscoverySPE ? 1 : 0,
             mPassiveMode ? 1 : 0,
             mRxScanEnabled ? 1 : 0);
    logInfoP("PairDiag: radio init=%d radioState=%d lastRssi=%d queue=%u duty=%u busyTO=%d irq=0x%04X txS=%lu txD=%lu rxS=%lu crc=%lu timeout=%lu",
             lHealth.initialized ? 1 : 0,
             static_cast<int>(lHealth.radioState),
             lHealth.lastRssi,
             lHealth.queueDepth,
             lHealth.dutyPermille,
             lHealth.busyTimedOut ? 1 : 0,
             lHealth.lastIrqStatus,
             static_cast<unsigned long>(lHealth.txStartCount),
             static_cast<unsigned long>(lHealth.txDoneCount),
             static_cast<unsigned long>(lHealth.rxStartCount),
             static_cast<unsigned long>(lHealth.crcErrorCount),
             static_cast<unsigned long>(lHealth.timeoutCount));
}

void IoHomeController::resetDiscoveryTimingTrace()
{
    memset(&mDiscoveryTimingTrace, 0, sizeof(mDiscoveryTimingTrace));
}

void IoHomeController::logDiscoveryTimingTrace(const char *iReason, unsigned long iListenElapsedMs) const
{
    if (!mPairDiagnosticTraceEnabled)
        return;

    const auto lDeltaUs = [](uint32_t iStartUs, uint32_t iEndUs) -> unsigned long
    {
        if (iStartUs == 0 || iEndUs == 0 || iEndUs < iStartUs)
            return 0UL;
        return static_cast<unsigned long>(iEndUs - iStartUs);
    };

#if defined(RADIO_SX1262)
    const unsigned long lTxBusyHits = static_cast<unsigned long>(mRadio.txBusyHighHitCount());
    const unsigned long lTxBusyTotalUs = static_cast<unsigned long>(mRadio.txBusyHighTotalUs());
    const unsigned long lTxBusyMaxUs = static_cast<unsigned long>(mRadio.txBusyHighMaxUs());
    const RadioSX1262TxBusyTrace lTxBusyTrace = mRadio.txBusyTrace();
#else
    const unsigned long lTxBusyHits = 0UL;
    const unsigned long lTxBusyTotalUs = 0UL;
    const unsigned long lTxBusyMaxUs = 0UL;
#endif

    logInfoP("PairDiag: discovery hop=%u %s prepFreq=%luus prepPre=%luus txLaunch=%luus txDone=%luus rxReady=%luus listen=%lums overshoot=%lums rxBusy=%lu txBusyHits=%lu txBusyTotal=%luus txBusyMax=%luus",
             static_cast<unsigned>(mPairingFreqIdx),
             iReason,
             lDeltaUs(mDiscoveryTimingTrace.hopStartUs, mDiscoveryTimingTrace.freqReadyUs),
             lDeltaUs(mDiscoveryTimingTrace.freqReadyUs, mDiscoveryTimingTrace.preambleReadyUs),
             lDeltaUs(mDiscoveryTimingTrace.preambleReadyUs, mDiscoveryTimingTrace.txStartUs),
             lDeltaUs(mDiscoveryTimingTrace.txStartUs, mDiscoveryTimingTrace.txDoneUs),
             lDeltaUs(mDiscoveryTimingTrace.txDoneUs, mDiscoveryTimingTrace.rxReadyUs),
             iListenElapsedMs,
             (iListenElapsedMs > 2000UL) ? (iListenElapsedMs - 2000UL) : 0UL,
             static_cast<unsigned long>(mDiscoveryTimingTrace.rxBusyCount),
             lTxBusyHits,
             lTxBusyTotalUs,
             lTxBusyMaxUs);

#if defined(RADIO_SX1262)
    logInfoP("PairDiag: discovery busy hop=%u irq=%lu/%luus status=%lu/%luus clear=%lu/%luus standby=%lu/%luus rx=%lu/%luus other=%lu/%luus",
             static_cast<unsigned>(mPairingFreqIdx),
             static_cast<unsigned long>(lTxBusyTrace.irqHits),
             static_cast<unsigned long>(lTxBusyTrace.irqTotalUs),
             static_cast<unsigned long>(lTxBusyTrace.statusHits),
             static_cast<unsigned long>(lTxBusyTrace.statusTotalUs),
             static_cast<unsigned long>(lTxBusyTrace.clearIrqHits),
             static_cast<unsigned long>(lTxBusyTrace.clearIrqTotalUs),
             static_cast<unsigned long>(lTxBusyTrace.standbyHits),
             static_cast<unsigned long>(lTxBusyTrace.standbyTotalUs),
             static_cast<unsigned long>(lTxBusyTrace.rxHits),
             static_cast<unsigned long>(lTxBusyTrace.rxTotalUs),
             static_cast<unsigned long>(lTxBusyTrace.otherHits),
             static_cast<unsigned long>(lTxBusyTrace.otherTotalUs));
#endif
}

IoHomeController::IoHomeRadioHealth IoHomeController::radioHealth() const
{
    IoHomeRadioHealth lHealth = {};
    lHealth.initialized = mRadio.isInitialized();
    lHealth.radioState = mRadio.state();
    lHealth.controllerState = mState;
    lHealth.passiveMode = mPassiveMode;
    lHealth.networkScanActive = mNetworkScanActive;
    lHealth.rxScanEnabled = mRxScanEnabled;
    lHealth.preambleDetected = mRadio.isPreambleDetected();
    lHealth.currentFreqIdx = mCurrentFreqIdx;
    lHealth.lastResponseFreqIdx = mLastResponseFreqIdx;
    lHealth.lastRssi = mRadio.lastRssi();
    lHealth.currentRssi = 0;
    lHealth.currentRssiValid = const_cast<Radio &>(mRadio).currentRssi(lHealth.currentRssi);
    lHealth.initError = 0;
    lHealth.initStatusByte = 0;
    lHealth.initCommandStatus = 0;
    lHealth.initDeviceErrors = 0;
    lHealth.lastDeviceErrors = 0;
    lHealth.tcxoStartupDelayUs = 0;
    lHealth.tcxoStartupAttempts = 0;
    lHealth.rfSwitchConfig = 0;
    lHealth.busyTimedOut = false;
    lHealth.txStartCount = 0;
    lHealth.txDoneCount = 0;
    lHealth.rxStartCount = 0;
    lHealth.irqCount = 0;
    lHealth.preambleIrqCount = 0;
    lHealth.syncWordIrqCount = 0;
    lHealth.rxDoneCount = 0;
    lHealth.crcErrorCount = 0;
    lHealth.timeoutCount = 0;
    lHealth.irqPollHitCount = 0;
    lHealth.preambleOnlyIrqCount = 0;
    lHealth.rxReadFailCount = 0;
    lHealth.lastIrqStatus = 0;
    lHealth.lastOpStatusBefore = 0;
    lHealth.lastOpStatusAfter = 0;
    lHealth.lastTxSetStatus = 0;
    lHealth.lastTxIrqImmediate = 0;
#if defined(RADIO_SX1262)
    lHealth.initError = static_cast<uint8_t>(mRadio.initError());
    lHealth.initStatusByte = mRadio.initStatusByte();
    lHealth.initCommandStatus = mRadio.initCommandStatus();
    lHealth.initDeviceErrors = mRadio.initDeviceErrors();
    lHealth.lastDeviceErrors = mRadio.lastDeviceErrors();
    lHealth.tcxoStartupDelayUs = mRadio.tcxoStartupDelayUs();
    lHealth.tcxoStartupAttempts = mRadio.tcxoStartupAttempts();
    lHealth.rfSwitchConfig = mRadio.rfSwitchConfig();
    lHealth.busyTimedOut = mRadio.busyTimedOut();
    lHealth.txStartCount = mRadio.txStartCount();
    lHealth.txDoneCount = mRadio.txDoneCount();
    lHealth.rxStartCount = mRadio.rxStartCount();
    lHealth.irqCount = mRadio.irqCount();
    lHealth.preambleIrqCount = mRadio.preambleIrqCount();
    lHealth.syncWordIrqCount = mRadio.syncWordIrqCount();
    lHealth.rxDoneCount = mRadio.rxDoneCount();
    lHealth.crcErrorCount = mRadio.crcErrorCount();
    lHealth.timeoutCount = mRadio.timeoutCount();
    lHealth.irqPollHitCount = mRadio.irqPollHitCount();
    lHealth.preambleOnlyIrqCount = mRadio.preambleOnlyIrqCount();
    lHealth.rxReadFailCount = mRadio.rxReadFailCount();
    lHealth.lastIrqStatus = mRadio.lastIrqStatus();
    lHealth.lastOpStatusBefore = mRadio.lastOpStatusBefore();
    lHealth.lastOpStatusAfter = mRadio.lastOpStatusAfter();
    lHealth.lastTxSetStatus = mRadio.lastTxSetStatus();
    lHealth.lastTxIrqImmediate = mRadio.lastTxIrqImmediate();
#elif defined(RADIO_SX1276)
    lHealth.txStartCount = mRadio.txStartCount();
    lHealth.txDoneCount = mRadio.txDoneCount();
    lHealth.rxStartCount = mRadio.rxStartCount();
    lHealth.irqCount = mRadio.irqCount();
    lHealth.lastIrqStatus = mRadio.lastIrqStatus();
    lHealth.lastOpStatusBefore = mRadio.lastOpStatusBefore();
    lHealth.lastOpStatusAfter = mRadio.lastOpStatusAfter();
    lHealth.lastTxSetStatus = mRadio.lastTxSetStatus();
    lHealth.lastTxIrqImmediate = mRadio.lastTxIrqImmediate();
#endif

    uint8_t lDepth = 0;
    if (mQueueHead >= mQueueTail)
        lDepth = mQueueHead - mQueueTail;
    else
        lDepth = IOHC_CMD_QUEUE_SIZE - mQueueTail + mQueueHead;
    lHealth.queueDepth = lDepth;

    uint32_t lAccum = 0;
    for (uint8_t i = 0; i < IOHC_NUM_FREQUENCIES; i++)
        if (mTxTimeAccum[i] > lAccum)
            lAccum = mTxTimeAccum[i];
    lHealth.dutyPermille = static_cast<uint16_t>((lAccum * 1000UL) / IOHC_DUTY_CYCLE_WINDOW_MS);
    return lHealth;
}

// --- Main loop ---

void IoHomeController::loop()
{
    if (!mRadio.isInitialized())
        return;

    // Reset duty cycle accumulator every hour
    if (millis() - mDutyCycleWindowStart > IOHC_DUTY_CYCLE_WINDOW_MS)
    {
        memset(mTxTimeAccum, 0, sizeof(mTxTimeAccum));
        mDutyCycleWindowStart = millis();
    }

    // Multi-frequency RX scanning: cycle through frequencies during idle/passive listening
    if (mRxScanEnabled &&
        (mState == ControllerState::Idle || mState == ControllerState::PassiveListening))
    {
        uint32_t lNow = micros();
        if (lNow - mRxScanLastSwitch >= mRxScanIntervalUs)
        {
            if (!mRadio.isPreambleDetected()) // don't switch mid-packet
            {
                uint8_t lNextFreqIdx = (mCurrentFreqIdx + 1) % IOHC_NUM_FREQUENCIES;
                if (mRadio.setFrequency(IOHC_FREQUENCIES[lNextFreqIdx]) == RadioError::None)
                {
                    mCurrentFreqIdx = lNextFreqIdx;
                    if (mRadio.startReceive() == RadioError::None)
                        mRxScanLastSwitch = lNow;
                }
            }
        }
    }

    // Check for incoming packets in any state
    if (mRadio.isPacketAvailable())
    {
        uint8_t lLen = mRadio.readPacket(mRxBuffer, sizeof(mRxBuffer));
        if (lLen > 0 && mRxFrame.deserialize(mRxBuffer, lLen))
        {
            // Record which frequency the response came on
            mLastResponseFreqIdx = mCurrentFreqIdx;

            if (mPairDiagnosticTraceEnabled &&
                (isPairDiagnosticState(mState) || isPairDiagnosticCommand(mRxFrame.commandId)))
            {
                tracePairDiagnosticFrame("rx", mRxFrame, mLastResponseFreqIdx, mRadio.lastRssi());
            }

            // If we're waiting for a response, process it
            if (mState == ControllerState::WaitResponse)
            {
                mState = ControllerState::ProcessResponse;
            }
            else if (mState == ControllerState::PairWaitDiscoveryResponse)
            {
                if (mRxFrame.commandId == IoHomeCommand::DiscoverResponse ||
                    mRxFrame.commandId == IoHomeCommand::DiscoverSPEResponse)
                {
                    mDiscoveredNodeId = mRxFrame.getSrcNodeId();
                    mPairingFreqIdx = mLastResponseFreqIdx;
                    mState = ControllerState::PairSendDiscoveryConfirmation;
                }
            }
            else if (mState == ControllerState::PairWaitDiscoveryConfirmationAck)
            {
                if (mRxFrame.commandId == IoHomeCommand::ConfirmationACK &&
                    mRxFrame.getSrcNodeId() == mDiscoveredNodeId &&
                    mRxFrame.getDestNodeId() == mOwnNodeId)
                {
                    mState = ControllerState::PairSendLaunchKeyTransfer;
                }
            }
            else if (mState == ControllerState::PairWaitLaunchKeyTransfer)
            {
                if (mRxFrame.commandId == IoHomeCommand::KeyTransfer &&
                    mRxFrame.getSrcNodeId() == mDiscoveredNodeId &&
                    mRxFrame.getDestNodeId() == mOwnNodeId &&
                    mRxFrame.dataLen >= 16)
                {
                    uint8_t lLaunchData[1 + sizeof(mPairingChallenge)] = {static_cast<uint8_t>(IoHomeCommand::LaunchKeyTransfer)};
                    memcpy(lLaunchData + 1, mPairingChallenge, sizeof(mPairingChallenge));
                    uint8_t lKeystream[16];
                    if (IoHomeCrypto::crypt2WKey(lLaunchData, sizeof(lLaunchData), mPairingChallenge, IOHC_TRANSFER_KEY, lKeystream))
                    {
                        for (uint8_t i = 0; i < 16; i++)
                            mPairPulledKey[i] = mRxFrame.data[i] ^ lKeystream[i];

                        mPairPulledKeyFrame = mRxFrame;
                        mState = ControllerState::PairSendPullKeyChallenge;
                    }
                    else
                    {
                        logDebugP("Pairing: failed to decode pulled key from 0x%06X, continuing with push flow", mDiscoveredNodeId);
                        mState = ControllerState::PairSendKeyInit;
                    }
                }
            }
            else if (mState == ControllerState::PairWaitPullKeyChallengeResponse)
            {
                if (mRxFrame.commandId == IoHomeCommand::ChallengeResponse &&
                    mRxFrame.getSrcNodeId() == mDiscoveredNodeId &&
                    mRxFrame.getDestNodeId() == mOwnNodeId &&
                    mRxFrame.dataLen >= IOHC_HMAC_SIZE)
                {
                    uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA];
                    uint8_t lExpected[IOHC_HMAC_SIZE];
                    lHmacInput[0] = static_cast<uint8_t>(mPairPulledKeyFrame.commandId);
                    memcpy(lHmacInput + 1, mPairPulledKeyFrame.data, mPairPulledKeyFrame.dataLen);

                    if (IoHomeCrypto::createHmac2W(lHmacInput, 1 + mPairPulledKeyFrame.dataLen,
                                                   mPairPullAuthChallenge, mPairPulledKey, lExpected))
                    {
                        uint8_t lDiff = 0;
                        for (uint8_t i = 0; i < IOHC_HMAC_SIZE; i++)
                            lDiff |= (lExpected[i] ^ mRxFrame.data[i]);

                        if (lDiff == 0)
                            logInfoP("Pairing: pulled device key authenticated for 0x%06X", mDiscoveredNodeId);
                        else
                            logDebugP("Pairing: pulled key auth mismatch for 0x%06X, continuing with push flow", mDiscoveredNodeId);
                    }
                    else
                    {
                        logDebugP("Pairing: failed to verify pulled key auth for 0x%06X, continuing with push flow", mDiscoveredNodeId);
                    }

                    mState = ControllerState::PairSendKeyInit;
                }
            }
            else if (mState == ControllerState::PairWaitDeviceChallenge)
            {
                if (mRxFrame.commandId == IoHomeCommand::ChallengeRequest)
                {
                    // Device sends its challenge — extract and store for key transfer
                    if (mRxFrame.dataLen >= 6)
                        memcpy(mPairingChallenge, mRxFrame.data, 6);
                    mState = ControllerState::PairSendKeyTransfer;
                }
            }
            else if (mState == ControllerState::PairWaitKeyTransferConfirmation)
            {
                if (mRxFrame.commandId == IoHomeCommand::ChallengeRequest &&
                    mRxFrame.getSrcNodeId() == mDiscoveredNodeId &&
                    mRxFrame.getDestNodeId() == mOwnNodeId &&
                    mRxFrame.dataLen >= sizeof(mPairKeyTransferChallenge))
                {
                    memcpy(mPairKeyTransferChallenge, mRxFrame.data, sizeof(mPairKeyTransferChallenge));
                    mState = ControllerState::PairSendKeyTransferAuthResponse;
                }
                else if (mRxFrame.commandId == IoHomeCommand::Confirmation ||
                         mRxFrame.commandId == IoHomeCommand::KeyTransferConfirmation)
                {
                    // Pairing successful — store system key as channel encryption key
                    if (mModule)
                    {
                        IoHomecontrolChannel *lCh = mModule->getChannel(mPairingChannel);
                        if (lCh)
                        {
                            lCh->setNodeId(mDiscoveredNodeId);
                            lCh->setEncryptionKey(mSystemKey);
                            openknx.flash.save();
                        }
                    }
                    mState = ControllerState::PairSendSetConfig1;
                }
            }
            else if (mState == ControllerState::PairWaitSetConfig1Response)
            {
                if (mRxFrame.getSrcNodeId() == mDiscoveredNodeId &&
                    mRxFrame.getDestNodeId() == mOwnNodeId)
                {
                    if (mRxFrame.commandId == IoHomeCommand::ChallengeRequest && mRxFrame.dataLen >= 6)
                    {
                        memcpy(mPairSetConfigChallenge, mRxFrame.data, sizeof(mPairSetConfigChallenge));
                        mState = ControllerState::PairSendSetConfig1AuthResponse;
                    }
                    else if (mRxFrame.commandId == IoHomeCommand::SetConfig1Response)
                    {
                        if (mRxFrame.dataLen >= 1 && mRxFrame.data[0] == 0x05)
                            logInfoP("Pairing: automatic status feedback enabled for 0x%06X", mDiscoveredNodeId);
                        else
                            logInfoP("Pairing: SetConfig1 accepted by 0x%06X", mDiscoveredNodeId);
                        mState = ControllerState::PairComplete;
                    }
                    else if (mRxFrame.commandId == IoHomeCommand::ErrorResponse)
                    {
                        logInfoP("Pairing: device 0x%06X does not support automatic status feedback", mDiscoveredNodeId);
                        mState = ControllerState::PairComplete;
                    }
                    else
                    {
                        logDebugP("Pairing: unexpected SetConfig1 response 0x%02X from 0x%06X",
                                  static_cast<uint8_t>(mRxFrame.commandId), mDiscoveredNodeId);
                        mState = ControllerState::PairComplete;
                    }
                }
            }
            else if (mState == ControllerState::PairWaitSetConfig1FinalResponse)
            {
                if (mRxFrame.getSrcNodeId() == mDiscoveredNodeId &&
                    mRxFrame.getDestNodeId() == mOwnNodeId)
                {
                    if (mRxFrame.commandId == IoHomeCommand::SetConfig1Response)
                    {
                        if (mRxFrame.dataLen >= 1 && mRxFrame.data[0] == 0x05)
                            logInfoP("Pairing: automatic status feedback enabled for 0x%06X", mDiscoveredNodeId);
                        else
                            logInfoP("Pairing: SetConfig1 accepted by 0x%06X", mDiscoveredNodeId);
                    }
                    else if (mRxFrame.commandId == IoHomeCommand::ErrorResponse)
                    {
                        logInfoP("Pairing: device 0x%06X rejected automatic status feedback", mDiscoveredNodeId);
                    }
                    else
                    {
                        logDebugP("Pairing: unexpected final SetConfig1 response 0x%02X from 0x%06X",
                                  static_cast<uint8_t>(mRxFrame.commandId), mDiscoveredNodeId);
                    }
                    mState = ControllerState::PairComplete;
                }
            }
            else if (mState == ControllerState::AuthWaitResponse)
            {
                // Waiting for ChallengeResponse (0x3D) to authenticate unsolicited StatusUpdate
                if (mRxFrame.commandId == IoHomeCommand::ChallengeResponse &&
                    mRxFrame.getSrcNodeId() == mAuthSrcNodeId)
                {
                    if (mModule)
                    {
                        IoHomecontrolChannel *lCh = mModule->getChannel(mAuthChannelIdx);
                        if (lCh && mRxFrame.dataLen == IOHC_HMAC_SIZE)
                        {
                            uint8_t lFrameData[1 + IOHC_FRAME_MAX_DATA] = {0};
                            const size_t lFrameDataLen = buildHmacInput(mPendingAuthFrame, lFrameData, sizeof(lFrameData));

                            if (lFrameDataLen > 0 &&
                                IoHomeCrypto::verifyHmac(lFrameData,
                                                         lFrameDataLen,
                                                         mRxFrame.data, mAuthChallenge,
                                                         lCh->getEncryptionKey()))
                            {
                                // Auth passed — dispatch the saved StatusUpdate
                                IoHomeFrame lSaved = mPendingAuthFrame;
                                lSaved.hasHmac = false; // skip re-verification on dispatch
                                mRxFrame = lSaved;
                                static const uint8_t sZero[6] = {};
                                lCh->setLastChallenge(sZero);
                                dispatchRxFrame();
                            }
                            else
                            {
                                logInfoP("Auth failed for unsolicited StatusUpdate from 0x%06X", mAuthSrcNodeId);
                            }
                        }
                        else
                        {
                            logInfoP("Auth failed for unsolicited StatusUpdate from 0x%06X", mAuthSrcNodeId);
                        }
                    }
                    if (mState == ControllerState::AuthWaitResponse)
                    {
                        mState = ControllerState::Idle;
                        startReceive();
                    }
                }
            }
            else if (mState == ControllerState::PassiveListening)
            {
                // Passive mode: observe pairing exchanges, extract keys
                processPassiveFrame();
            }
            else if (mGatewayMode && mState == ControllerState::Idle)
            {
                processGatewayFrame();
            }
            else
            {
                // Unsolicited frame (e.g., status update from device)
                dispatchRxFrame();
            }
        }
    }

    tracePairDiagnosticStateChange();

    // State machine
    switch (mState)
    {
    case ControllerState::Idle:
        processIdle();
        break;
    case ControllerState::TxPending:
        processTxPending();
        break;
    case ControllerState::TxInProgress:
        processTxInProgress();
        break;
    case ControllerState::Tx1WRepeat:
        processTx1WRepeat();
        break;
    case ControllerState::WaitResponse:
        processWaitResponse();
        break;
    case ControllerState::ProcessResponse:
        processResponse();
        break;

    // Pairing
    case ControllerState::PairSendDiscovery:
        processPairSendDiscovery();
        break;
    case ControllerState::PairWaitDiscoveryResponse:
        processPairWaitDiscoveryResponse();
        break;
    case ControllerState::PairSendDiscoveryConfirmation:
        processPairSendDiscoveryConfirmation();
        break;
    case ControllerState::PairWaitDiscoveryConfirmationAck:
        processPairWaitDiscoveryConfirmationAck();
        break;
    case ControllerState::PairSendLaunchKeyTransfer:
        processPairSendLaunchKeyTransfer();
        break;
    case ControllerState::PairWaitLaunchKeyTransfer:
        processPairWaitLaunchKeyTransfer();
        break;
    case ControllerState::PairSendPullKeyChallenge:
        processPairSendPullKeyChallenge();
        break;
    case ControllerState::PairWaitPullKeyChallengeResponse:
        processPairWaitPullKeyChallengeResponse();
        break;
    case ControllerState::PairSend1WRemove:
        processPairSend1WRemove();
        break;
    case ControllerState::PairWait1WRemove:
        processPairWait1WRemove();
        break;
    case ControllerState::PairSend1WKeyTransfer:
        processPairSend1WKeyTransfer();
        break;
    case ControllerState::PairWait1WKeyTransfer:
        processPairWait1WKeyTransfer();
        break;
    case ControllerState::PairSendKeyInit:
        processPairSendKeyInit();
        break;
    case ControllerState::PairWaitDeviceChallenge:
        processPairWaitDeviceChallenge();
        break;
    case ControllerState::PairSendKeyTransfer:
        processPairSendKeyTransfer();
        break;
    case ControllerState::PairSendKeyTransferAuthResponse:
        processPairSendKeyTransferAuthResponse();
        break;
    case ControllerState::PairWaitKeyTransferConfirmation:
        processPairWaitKeyTransferConfirmation();
        break;
    case ControllerState::PairSendSetConfig1:
        processPairSendSetConfig1();
        break;
    case ControllerState::PairWaitSetConfig1Response:
        processPairWaitSetConfig1Response();
        break;
    case ControllerState::PairSendSetConfig1AuthResponse:
        processPairSendSetConfig1AuthResponse();
        break;
    case ControllerState::PairWaitSetConfig1FinalResponse:
        processPairWaitSetConfig1FinalResponse();
        break;
    case ControllerState::PairComplete:
    case ControllerState::PairFailed:
        mState = ControllerState::Idle;
        startReceive();
        break;

    case ControllerState::GatewayIdle:
    case ControllerState::GatewayWaitDiscoveryResponse:
    case ControllerState::GatewayWaitKeyTransfer:
    case ControllerState::GatewayWaitChallenge:
        mState = ControllerState::Idle;
        break;

    // Discovery
    case ControllerState::DiscoverySending:
    case ControllerState::DiscoveryListening:
        processDiscovery();
        break;

    // Command scanning
    case ControllerState::ScanSending:
        processScanSending();
        break;
    case ControllerState::ScanWaitResponse:
        processScanWaitResponse();
        break;

    // Receive-side authentication
    case ControllerState::AuthSendChallenge:
        processAuthSendChallenge();
        break;
    case ControllerState::AuthWaitResponse:
        processAuthWaitResponse();
        break;

    // StatusUpdate ACK broadcast
    case ControllerState::StatusAckSend:
        processStatusAckSend();
        break;
    case ControllerState::StatusAckTxWait:
        processStatusAckTxWait();
        break;

    // Passive mode — no state processing needed (all in RX dispatch)
    case ControllerState::PassiveListening:
        ensureReceiveAfterTransmit();
        break;
    }

    tracePairDiagnosticStateChange();
}

// --- State handlers ---

void IoHomeController::processIdle()
{
    if (mPassiveMode)
        return; // passive mode: never transmit

    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr != RadioError::None)
        return;

    if (!queueEmpty())
    {
        IoHomeQueueEntry lEntry;
        if (queuePop(lEntry))
        {
            mCurrentCmd = lEntry;
            buildTxFrame(mCurrentCmd);
            mState = ControllerState::TxPending;
        }
    }
}

void IoHomeController::processTxPending()
{
    if (!isDutyCycleOk())
        return; // wait for duty cycle to clear

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen == 0)
    {
        mState = ControllerState::Idle;
        return;
    }

    // Set preamble based on frame type: START frames need long preamble for low-power devices
    bool lIsStartFrame = (mTxFrame.ctrlByte0 & IOHC_CTRL0_START);
    const RadioError lPrepErr = configureTxRadio(lIsStartFrame ? IOHC_PREAMBLE_LONG : IOHC_PREAMBLE_SHORT);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::Idle;
        return;
    }

    RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
    if (lErr == RadioError::None)
    {
        mStateTimer = millis();
        mState = ControllerState::TxInProgress;
    }
    else if (lErr == RadioError::Busy)
    {
        return;
    }
    else
    {
        mState = ControllerState::Idle;
    }
}

void IoHomeController::processTxInProgress()
{
    if (mRadio.state() != RadioState::Transmitting)
    {
        const RadioError lRxErr = mRadio.startReceive();
        if (lRxErr == RadioError::Busy)
            return;
        if (lRxErr != RadioError::None)
        {
            mState = ControllerState::Idle;
            return;
        }

        mStateTimer = millis();
        mState = ControllerState::WaitResponse;
        return;
    }

    if (mRadio.isTxDone())
    {
        // Track TX time for duty cycle (approx: bytes * 8 / bitrate * 1000)
        uint32_t lTxTimeMs = ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;
        mTxTimeAccum[mCurrentFreqIdx] += lTxTimeMs;

        if (mTxFrame.ctrlByte0 & IOHC_CTRL0_MODE_1W)
        {
            // 1W: check for repeat transmissions
            if (mTx1WRepeatRemaining > 0)
            {
                mTx1WRepeatRemaining--;
                mTx1WRepeatTimer = millis();
                mState = ControllerState::Tx1WRepeat;
            }
            else
            {
                mCurrentCmd.active = false;
                mState = ControllerState::Idle;
            }
        }
        else
        {
            // 2W: switch to RX to listen for response
            const RadioError lRxErr = mRadio.startReceive();
            if (lRxErr == RadioError::Busy)
                return;
            if (lRxErr != RadioError::None)
            {
                mState = ControllerState::Idle;
                return;
            }
            mStateTimer = millis();
            mState = ControllerState::WaitResponse;
        }
    }
    else if (millis() - mStateTimer > IOHC_TX_TIMEOUT_MS)
    {
        // TX timeout
        mRadio.standby();
        mState = ControllerState::Idle;
    }
}

void IoHomeController::processTx1WRepeat()
{
    if (millis() - mTx1WRepeatTimer < IOHC_1W_REPEAT_INTERVAL_MS)
        return; // wait for interval

    // Re-send same buffer with short preamble (repeats don't need long preamble)
    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mCurrentCmd.active = false;
        mState = ControllerState::Idle;
        return;
    }

    RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
    if (lErr == RadioError::None)
    {
        mStateTimer = millis();
        mState = ControllerState::TxInProgress;
    }
    else if (lErr == RadioError::Busy)
    {
        return;
    }
    else
    {
        mCurrentCmd.active = false;
        mState = ControllerState::Idle;
    }
}

void IoHomeController::processWaitResponse()
{
    // Extend timeout if preamble detected (device is responding, packet not complete)
    if (mRadio.isPreambleDetected())
    {
        mStateTimer = millis();
        return;
    }

    if (millis() - mStateTimer > IOHC_RX_TIMEOUT_MS)
    {
        // No response — retry on next frequency or give up
        if (!hopFrequency())
            return;

        if (mCurrentCmd.active && mCurrentCmd.retries < IOHC_MAX_RETRIES)
        {
            mCurrentCmd.retries++;
            buildTxFrame(mCurrentCmd);
            mState = ControllerState::TxPending;
        }
        else
        {
            mCurrentCmd.active = false;
            mState = ControllerState::Idle;
        }
    }
}

void IoHomeController::processResponse()
{
    // Check for challenge-response authentication (0x3C) for authenticated 2W commands
    if (mRxFrame.commandId == IoHomeCommand::ChallengeRequest &&
        mCurrentCmd.active && !mAuthResponseSent &&
        mRxFrame.dataLen >= 6)
    {
        // Device challenges our authenticated command — build and send ChallengeResponse (0x3D)
        // HMAC input: {original command ID} + {original command data}
        // Challenge: first 6 bytes of the 0x3C payload
        // Key: channel encryption key (from queue entry)
        uint8_t lChallenge[6];
        memcpy(lChallenge, mRxFrame.data, 6);

        IoHomeFrame lFrame;
        lFrame.init();
        lFrame.ctrlByte0 = 0; // continuation frame (per nicolas5000)
        lFrame.ctrlByte1 = 0x00;
        lFrame.setSrcNode(mOwnNodeId);
        lFrame.setDestNode(mCurrentCmd.destNodeId);
        lFrame.commandId = IoHomeCommand::ChallengeResponse;

        // Build HMAC over {original_cmd_id, original_data...} with challenge and key
        uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA];
        const size_t lHmacInputLen = buildHmacInput(mTxFrame, lHmacInput, sizeof(lHmacInput));

        if (lHmacInputLen == 0 ||
            !IoHomeCrypto::createHmac2W(lHmacInput, lHmacInputLen,
                                        lChallenge, mCurrentCmd.encKey, lFrame.data))
        {
            mCurrentCmd.active = false;
            mState = ControllerState::Idle;
            return;
        }

        lFrame.dataLen = IOHC_HMAC_SIZE;
        lFrame.hasHmac = false;

        const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT);
        if (lPrepErr == RadioError::Busy)
            return;
        if (lPrepErr != RadioError::None)
        {
            mCurrentCmd.active = false;
            mState = ControllerState::Idle;
            return;
        }

        uint8_t lLen = lFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
        if (lLen > 0)
        {
            const RadioError lErr = mRadio.startTransmit(mTxBuffer, lLen);
            if (lErr == RadioError::None)
            {
                mAuthResponseSent = true;
                mStateTimer = millis();
                mState = ControllerState::TxInProgress; // will transition to WaitResponse when TX done
            }
            else if (lErr == RadioError::Busy)
            {
                return;
            }
            else
            {
                mCurrentCmd.active = false;
                mState = ControllerState::Idle;
            }
        }
        else
        {
            mCurrentCmd.active = false;
            mState = ControllerState::Idle;
        }
        return;
    }

    dispatchRxFrame();
    mCurrentCmd.active = false;
    if (mState == ControllerState::ProcessResponse)
        mState = ControllerState::Idle;
}

// --- Pairing state handlers ---

void IoHomeController::processPairSendDiscovery()
{
    // Check overall pairing timeout
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    // Build discovery frame
    mTxFrame.init();
    mTxFrame.setStart2W();
    mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END); // standalone: START+END (per nicolas5000)
    mTxFrame.setSrcNode(mOwnNodeId);
    mTxFrame.setDestBroadcast();
    mTxFrame.hasHmac = false;

    if (mDiscoverySPE)
    {
        mTxFrame.commandId = IoHomeCommand::DiscoverSPERequest;
        // SPE discovery payload: 6B random challenge + 6B HMAC(challenge, systemKey)
        uint8_t lChallenge[6];
        IoHomeCrypto::generateChallenge(lChallenge);
        memcpy(mTxFrame.data, lChallenge, 6);
        // Compute HMAC over the challenge using system key
        uint8_t lHmac[6];
        IoHomeCrypto::createHmac2W(lChallenge, 6, lChallenge, mSystemKey, lHmac);
        memcpy(mTxFrame.data + 6, lHmac, 6);
        mTxFrame.dataLen = 12;
    }
    else
    {
        mTxFrame.commandId = IoHomeCommand::DiscoverRequest;
        mTxFrame.dataLen = 0;
    }

    const uint32_t lDiscoveryFreq = IOHC_FREQUENCIES[mPairingFreqIdx];
#if defined(RADIO_SX1262)
    const RadioError lFreqErr = mRadio.setFrequencyBlocking(lDiscoveryFreq);
    if (lFreqErr != RadioError::None)
    {
        if (lFreqErr == RadioError::Busy)
            return;
        mState = ControllerState::PairFailed;
        return;
    }
    updateCurrentFrequencyIndex(lDiscoveryFreq);
    const RadioError lPrepErr = mRadio.setPreambleLengthBlocking(IOHC_PREAMBLE_LONG);
#else
    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_LONG, &lDiscoveryFreq);
#endif
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
#if defined(RADIO_SX1262)
        const RadioError lErr = mRadio.startTransmitBlocking(mTxBuffer, mTxLen);
#else
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
#endif
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitDiscoveryResponse;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            mState = ControllerState::PairFailed;
        }
    }
    else
    {
        mState = ControllerState::PairFailed;
    }
}

void IoHomeController::processPairWaitDiscoveryResponse()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    if (millis() - mStateTimer > 2000) // 2s timeout per frequency
    {
        // Hop to next frequency and retry
        mPairingFreqIdx = (mPairingFreqIdx + 1) % IOHC_NUM_FREQUENCIES;
        mState = ControllerState::PairSendDiscovery;
    }
}

void IoHomeController::processPairSendDiscoveryConfirmation()
{
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxFrame.init();
    mTxFrame.setStart2W();
    mTxFrame.setSrcNode(mOwnNodeId);
    mTxFrame.setDestNode(mDiscoveredNodeId);
    mTxFrame.commandId = IoHomeCommand::Confirmation;
    mTxFrame.dataLen = 0;
    mTxFrame.hasHmac = false;

    const uint32_t lConfirmationFreq = IOHC_FREQUENCIES[mPairingFreqIdx];
#if defined(RADIO_SX1262)
    const RadioError lFreqErr = mRadio.setFrequencyBlocking(lConfirmationFreq);
    if (lFreqErr != RadioError::None)
    {
        if (lFreqErr == RadioError::Busy)
            return;
        logDebugP("Pairing: failed to send discovery confirmation to 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
        return;
    }
    updateCurrentFrequencyIndex(lConfirmationFreq);
    const RadioError lPrepErr = mRadio.setPreambleLengthBlocking(IOHC_PREAMBLE_LONG);
#else
    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_LONG, &lConfirmationFreq);
#endif
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        logDebugP("Pairing: failed to send discovery confirmation to 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
        return;
    }

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
#if defined(RADIO_SX1262)
        const RadioError lErr = mRadio.startTransmitBlocking(mTxBuffer, mTxLen);
#else
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
#endif
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitDiscoveryConfirmationAck;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            logDebugP("Pairing: failed to send discovery confirmation to 0x%06X, falling back to push flow", mDiscoveredNodeId);
            mState = ControllerState::PairSendKeyInit;
        }
    }
    else
    {
        logDebugP("Pairing: failed to send discovery confirmation to 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
    }
}

void IoHomeController::processPairWaitDiscoveryConfirmationAck()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::PairSendKeyInit;
        return;
    }

    if (millis() - mStateTimer > 1000)
    {
        logDebugP("Pairing: discovery confirmation ack timed out for 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
    }
}

void IoHomeController::processPairSendLaunchKeyTransfer()
{
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mPairLaunchKeyTransferFrame.init();
    mPairLaunchKeyTransferFrame.setStart2W();
    mPairLaunchKeyTransferFrame.setSrcNode(mOwnNodeId);
    mPairLaunchKeyTransferFrame.setDestNode(mDiscoveredNodeId);
    mPairLaunchKeyTransferFrame.commandId = IoHomeCommand::LaunchKeyTransfer;
    IoHomeCrypto::generateChallenge(mPairingChallenge);
    memcpy(mPairLaunchKeyTransferFrame.data, mPairingChallenge, sizeof(mPairingChallenge));
    mPairLaunchKeyTransferFrame.dataLen = sizeof(mPairingChallenge);
    mPairLaunchKeyTransferFrame.hasHmac = false;

    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_LONG);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        logDebugP("Pairing: failed to send launch key transfer to 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
        return;
    }

    mTxLen = mPairLaunchKeyTransferFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitLaunchKeyTransfer;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            logDebugP("Pairing: failed to send launch key transfer to 0x%06X, falling back to push flow", mDiscoveredNodeId);
            mState = ControllerState::PairSendKeyInit;
        }
    }
    else
    {
        logDebugP("Pairing: failed to send launch key transfer to 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
    }
}

void IoHomeController::processPairWaitLaunchKeyTransfer()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::PairSendKeyInit;
        return;
    }

    if (millis() - mStateTimer > 1000)
    {
        logDebugP("Pairing: pulled key transfer timed out for 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
    }
}

void IoHomeController::processPairSendPullKeyChallenge()
{
    IoHomeFrame lFrame;
    lFrame.init();
    lFrame.ctrlByte0 = 0;
    lFrame.ctrlByte1 = 0x00;
    lFrame.setSrcNode(mOwnNodeId);
    lFrame.setDestNode(mDiscoveredNodeId);
    lFrame.commandId = IoHomeCommand::ChallengeRequest;
    IoHomeCrypto::generateChallenge(mPairPullAuthChallenge);
    memcpy(lFrame.data, mPairPullAuthChallenge, sizeof(mPairPullAuthChallenge));
    lFrame.dataLen = sizeof(mPairPullAuthChallenge);
    lFrame.hasHmac = false;

    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        logDebugP("Pairing: failed to challenge pulled key for 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
        return;
    }

    uint8_t lLen = lFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (lLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, lLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitPullKeyChallengeResponse;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            logDebugP("Pairing: failed to challenge pulled key for 0x%06X, falling back to push flow", mDiscoveredNodeId);
            mState = ControllerState::PairSendKeyInit;
        }
    }
    else
    {
        logDebugP("Pairing: failed to challenge pulled key for 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
    }
}

void IoHomeController::processPairWaitPullKeyChallengeResponse()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::PairSendKeyInit;
        return;
    }

    if (millis() - mStateTimer > IOHC_RX_TIMEOUT_MS)
    {
        logDebugP("Pairing: pulled key auth timed out for 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
    }
}

void IoHomeController::processPairSend1WRemove()
{
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxFrame.init();
    mTxFrame.set1WMode();
    mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    mTxFrame.setSrcNode(mOwnNodeId);
    mTxFrame.setDestNode(0x00003F);
    mTxFrame.commandId = IoHomeCommand::RemoveController;
    mTxFrame.data[0] = 0x00;
    mTxFrame.dataLen = 1;
    mTxFrame.hasHmac = false;

    mCurrentFreqIdx = kPair1WFreqIdx;
    const uint32_t lPair1WFreq = IOHC_FREQ_2;
    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_LONG, &lPair1WFreq);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
            mTx1WRepeatTimer = 0;
            mState = ControllerState::PairWait1WRemove;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            mState = ControllerState::PairFailed;
        }
    }
    else
    {
        mState = ControllerState::PairFailed;
    }
}

void IoHomeController::processPairWait1WRemove()
{
    processPairWait1WBlind(ControllerState::PairSend1WKeyTransfer);
}

void IoHomeController::processPairSend1WKeyTransfer()
{
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    IoHomecontrolChannel *lCh = mModule ? mModule->getChannel(mPairingChannel) : nullptr;
    if (!lCh)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxFrame.init();
    mTxFrame.set1WMode();
    mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    mTxFrame.commandId = IoHomeCommand::SendKey1W;

    // The 1W key encryption IV must be the DEVICE's node address, not the controller's.
    // This ensures only the target device can decrypt the key.
    // IV = {node[0], node[1], node[2]} repeated to fill 16 bytes (io-homecontrol spec)
    uint8_t lDeviceNodeAddr[3];
    lDeviceNodeAddr[0] = (mDiscoveredNodeId >> 16) & 0xFF;
    lDeviceNodeAddr[1] = (mDiscoveredNodeId >> 8) & 0xFF;
    lDeviceNodeAddr[2] = mDiscoveredNodeId & 0xFF;

    // Working 1W references put the paired device node into the source field
    // and use 0x00003F as the broadcast target for 0x30.
    mTxFrame.setSrcNode(mDiscoveredNodeId);
    mTxFrame.setDestNode(0x00003F);

    uint8_t lEncryptedKey[16];
    if (!IoHomeCrypto::encrypt1WKey(mSystemKey, IOHC_TRANSFER_KEY, lDeviceNodeAddr, lEncryptedKey))
    {
        mState = ControllerState::PairFailed;
        return;
    }

    uint16_t lSeq = lCh->incrementSequence1W();
    memcpy(mTxFrame.data, lEncryptedKey, sizeof(lEncryptedKey));
    mTxFrame.data[16] = kPair1WControllerManufacturer;
    mTxFrame.data[17] = 0x01;
    mTxFrame.data[18] = (lSeq >> 8) & 0xFF;
    mTxFrame.data[19] = lSeq & 0xFF;
    mTxFrame.dataLen = 20;

    uint8_t lHmacInput[17];
    lHmacInput[0] = static_cast<uint8_t>(mTxFrame.commandId);
    memcpy(lHmacInput + 1, lEncryptedKey, sizeof(lEncryptedKey));
    if (!IoHomeCrypto::createHmac1W(lHmacInput, sizeof(lHmacInput), lSeq, mSystemKey, mTxFrame.hmac))
    {
        mState = ControllerState::PairFailed;
        return;
    }
    mTxFrame.hasHmac = true;

    mCurrentFreqIdx = kPair1WFreqIdx;
    const uint32_t lPair1WFreq = IOHC_FREQ_2;
    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_LONG, &lPair1WFreq);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
            mTx1WRepeatTimer = 0;
            mState = ControllerState::PairWait1WKeyTransfer;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            mState = ControllerState::PairFailed;
        }
    }
    else
    {
        mState = ControllerState::PairFailed;
    }
}

void IoHomeController::processPairWait1WKeyTransfer()
{
    if (!processPairWait1WBlind(ControllerState::PairComplete))
        return;

    if (mState == ControllerState::PairComplete && mModule)
    {
        IoHomecontrolChannel *lCh = mModule->getChannel(mPairingChannel);
        if (lCh)
        {
            lCh->setNodeId(mDiscoveredNodeId);
            lCh->setEncryptionKey(mSystemKey);
            openknx.flash.save();
            logInfoP("Pairing: 1W learn flow sent for 0x%06X on channel %d (no device ACK in 1W mode)",
                     mDiscoveredNodeId, mPairingChannel + 1);
        }
    }
}

bool IoHomeController::processPairWait1WBlind(ControllerState iNextState)
{
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mRadio.standby();
        mState = ControllerState::PairFailed;
        return true;
    }

    if (mTx1WRepeatTimer != 0)
    {
        if (millis() - mTx1WRepeatTimer < IOHC_1W_REPEAT_INTERVAL_MS)
            return true;

        mTx1WRepeatTimer = 0;
        const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT);
        if (lPrepErr == RadioError::Busy)
            return true;
        if (lPrepErr != RadioError::None)
        {
            mState = ControllerState::PairFailed;
            return true;
        }

        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
        }
        else if (lErr == RadioError::Busy)
        {
            return true;
        }
        else
        {
            mState = ControllerState::PairFailed;
        }
        return true;
    }

    if (mRadio.isTxDone())
    {
        uint32_t lTxTimeMs = ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;
        mTxTimeAccum[mCurrentFreqIdx] += lTxTimeMs;

        if (mTx1WRepeatRemaining > 0)
        {
            mTx1WRepeatRemaining--;
            mTx1WRepeatTimer = millis();
        }
        else
        {
            mState = iNextState;
        }
        return true;
    }

    if (millis() - mStateTimer > IOHC_TX_TIMEOUT_MS)
    {
        mRadio.standby();
        mState = ControllerState::PairFailed;
        return true;
    }

    return false;
}

void IoHomeController::processPairSendKeyInit()
{
    // Send KeyInitTransfer (0x31) to discovered device to begin key exchange
    mTxFrame.init();
    mTxFrame.setStart2W();
    mTxFrame.setSrcNode(mOwnNodeId);
    mTxFrame.setDestNode(mDiscoveredNodeId);
    mTxFrame.commandId = IoHomeCommand::KeyInitTransfer;
    mTxFrame.dataLen = 0;
    mTxFrame.hasHmac = false;

    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_LONG);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitDeviceChallenge;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            mState = ControllerState::PairFailed;
        }
    }
    else
    {
        mState = ControllerState::PairFailed;
    }
}

void IoHomeController::processPairWaitDeviceChallenge()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    if (millis() - mStateTimer > 5000) // 5s timeout for device challenge
    {
        mState = ControllerState::PairFailed;
    }
    // ChallengeRequest response is handled in main loop RX dispatch
}

void IoHomeController::processPairSendKeyTransfer()
{
    // Encrypt system key using transfer key + device's challenge
    uint8_t lEncryptedKey[16];

    mTxFrame.init();
    mTxFrame.ctrlByte0 = 0; // continuation frame: no START, no END (per nicolas5000)
    mTxFrame.ctrlByte1 = 0x00;
    mTxFrame.setSrcNode(mOwnNodeId);
    mTxFrame.setDestNode(mDiscoveredNodeId);
    mTxFrame.commandId = IoHomeCommand::KeyTransfer;

    mTxFrame.dataLen = 0;
    mTxFrame.hasHmac = false;

    const uint8_t lKeyInitData[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
    uint8_t lKeystream[16];
    if (!IoHomeCrypto::crypt2WKey(lKeyInitData, sizeof(lKeyInitData), mPairingChallenge, IOHC_TRANSFER_KEY, lKeystream))
    {
        mState = ControllerState::PairFailed;
        return;
    }
    for (int i = 0; i < 16; i++)
        lEncryptedKey[i] = mSystemKey[i] ^ lKeystream[i];

    // Put encrypted key as frame data
    memcpy(mTxFrame.data, lEncryptedKey, 16);
    mTxFrame.dataLen = 16;
    mTxFrame.hasHmac = false;

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitKeyTransferConfirmation;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            mState = ControllerState::PairFailed;
        }
    }
    else
    {
        mState = ControllerState::PairFailed;
    }
}

void IoHomeController::processPairSendKeyTransferAuthResponse()
{
    IoHomeFrame lFrame;
    lFrame.init();
    lFrame.ctrlByte0 = 0;
    lFrame.ctrlByte1 = 0x00;
    lFrame.setSrcNode(mOwnNodeId);
    lFrame.setDestNode(mDiscoveredNodeId);
    lFrame.commandId = IoHomeCommand::ChallengeResponse;

    uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA];
    const size_t lHmacInputLen = buildHmacInput(mTxFrame, lHmacInput, sizeof(lHmacInput));
    if (lHmacInputLen == 0 ||
        !IoHomeCrypto::createHmac2W(lHmacInput, lHmacInputLen,
                                    mPairKeyTransferChallenge, mSystemKey, lFrame.data))
    {
        mState = ControllerState::PairFailed;
        return;
    }

    lFrame.dataLen = IOHC_HMAC_SIZE;
    lFrame.hasHmac = false;

    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxLen = lFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitKeyTransferConfirmation;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            mState = ControllerState::PairFailed;
        }
    }
    else
    {
        mState = ControllerState::PairFailed;
    }
}

void IoHomeController::processPairWaitKeyTransferConfirmation()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    if (millis() - mStateTimer > 5000) // 5s timeout
    {
        mState = ControllerState::PairFailed;
    }
    // Confirmation response and channel storage handled in main loop RX dispatch
}

void IoHomeController::processPairSendSetConfig1()
{
    mPairSetConfigRequest.init();
    mPairSetConfigRequest.setStart2W();
    mPairSetConfigRequest.setSrcNode(mOwnNodeId);
    mPairSetConfigRequest.setDestNode(mDiscoveredNodeId);
    mPairSetConfigRequest.commandId = IoHomeCommand::SetConfig1;
    mPairSetConfigRequest.data[0] = 0xE0;
    mPairSetConfigRequest.data[1] = 0x10;
    mPairSetConfigRequest.data[2] = 0x0A;
    mPairSetConfigRequest.data[3] = 0x08;
    mPairSetConfigRequest.data[4] = 0x00;
    mPairSetConfigRequest.dataLen = 5;
    mPairSetConfigRequest.hasHmac = false;

    const uint32_t lSetConfigFreq = IOHC_FREQ_2;
    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_LONG, &lSetConfigFreq);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        logDebugP("Pairing: failed to send SetConfig1 to 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
        return;
    }

    mTxLen = mPairSetConfigRequest.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitSetConfig1Response;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            logDebugP("Pairing: failed to send SetConfig1 to 0x%06X", mDiscoveredNodeId);
            mState = ControllerState::PairComplete;
        }
    }
    else
    {
        logDebugP("Pairing: failed to send SetConfig1 to 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
    }
}

void IoHomeController::processPairWaitSetConfig1Response()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::PairComplete;
        return;
    }

    if (millis() - mStateTimer > 2000)
    {
        logDebugP("Pairing: SetConfig1 timed out for 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
    }
}

void IoHomeController::processPairSendSetConfig1AuthResponse()
{
    IoHomeFrame lFrame;
    lFrame.init();
    lFrame.ctrlByte0 = 0;
    lFrame.ctrlByte1 = 0x00;
    lFrame.setSrcNode(mOwnNodeId);
    lFrame.setDestNode(mDiscoveredNodeId);
    lFrame.commandId = IoHomeCommand::ChallengeResponse;

    uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA];
    const size_t lHmacInputLen = buildHmacInput(mPairSetConfigRequest, lHmacInput, sizeof(lHmacInput));

    if (lHmacInputLen == 0 ||
        !IoHomeCrypto::createHmac2W(lHmacInput, lHmacInputLen,
                                    mPairSetConfigChallenge, mSystemKey, lFrame.data))
    {
        logDebugP("Pairing: failed to build SetConfig1 challenge response for 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
        return;
    }

    lFrame.dataLen = IOHC_HMAC_SIZE;
    lFrame.hasHmac = false;

    const uint32_t lSetConfigFreq = IOHC_FREQ_2;
    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT, &lSetConfigFreq);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        logDebugP("Pairing: failed to send SetConfig1 challenge response to 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
        return;
    }

    mTxLen = lFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::PairWaitSetConfig1FinalResponse;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            logDebugP("Pairing: failed to send SetConfig1 challenge response to 0x%06X", mDiscoveredNodeId);
            mState = ControllerState::PairComplete;
        }
    }
    else
    {
        logDebugP("Pairing: failed to send SetConfig1 challenge response to 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
    }
}

void IoHomeController::processPairWaitSetConfig1FinalResponse()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::PairComplete;
        return;
    }

    if (millis() - mStateTimer > 2000)
    {
        logDebugP("Pairing: final SetConfig1 response timed out for 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
    }
}

void IoHomeController::processDiscovery()
{
    if (mState == ControllerState::DiscoverySending)
    {
        if (mDiscoverySendPhase == DiscoverySendPhase::SetFrequency && mDiscoveryTimingTrace.hopStartUs == 0)
            mDiscoveryTimingTrace.hopStartUs = micros();

        mTxFrame.init();
        mTxFrame.setStart2W();
        mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END); // standalone: START+END (per nicolas5000)
        mTxFrame.setSrcNode(mOwnNodeId);
        mTxFrame.setDestBroadcast();
        mTxFrame.hasHmac = false;

        if (mDiscoverySPE)
        {
            mTxFrame.commandId = IoHomeCommand::DiscoverSPERequest;
            // SPE discovery payload: 6B random challenge + 6B HMAC(challenge, systemKey)
            uint8_t lChallenge[6];
            IoHomeCrypto::generateChallenge(lChallenge);
            memcpy(mTxFrame.data, lChallenge, 6);
            uint8_t lHmac[6];
            IoHomeCrypto::createHmac2W(lChallenge, 6, lChallenge, mSystemKey, lHmac);
            memcpy(mTxFrame.data + 6, lHmac, 6);
            mTxFrame.dataLen = 12;
        }
        else
        {
            mTxFrame.commandId = IoHomeCommand::DiscoverRequest;
            mTxFrame.dataLen = 0;
        }

        const uint32_t lDiscoveryFreq = IOHC_FREQUENCIES[mPairingFreqIdx];
#if defined(RADIO_SX1262)
        RadioError lPrepErr = RadioError::None;
        if (mDiscoverySendPhase == DiscoverySendPhase::SetFrequency)
        {
            lPrepErr = mRadio.setFrequencyBlocking(lDiscoveryFreq);
            if (lPrepErr == RadioError::None)
            {
                mDiscoveryTimingTrace.freqReadyUs = micros();
                updateCurrentFrequencyIndex(lDiscoveryFreq);
                mDiscoverySendPhase = DiscoverySendPhase::SetPreamble;
                return;
            }
        }
        else if (mDiscoverySendPhase == DiscoverySendPhase::SetPreamble)
        {
            lPrepErr = mRadio.setPreambleLengthBlocking(IOHC_PREAMBLE_LONG);
            if (lPrepErr == RadioError::None)
            {
                mDiscoveryTimingTrace.preambleReadyUs = micros();
                mDiscoverySendPhase = DiscoverySendPhase::StartTransmit;
                return;
            }
        }
        if (lPrepErr == RadioError::Busy)
            return;
        if (lPrepErr != RadioError::None)
        {
            mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
            mState = ControllerState::Idle;
            return;
        }
#else
        const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_LONG, &lDiscoveryFreq);
        if (lPrepErr == RadioError::Busy)
            return;
        if (lPrepErr != RadioError::None)
        {
            mState = ControllerState::Idle;
            return;
        }
#endif

        mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
        if (mTxLen > 0)
        {
            RadioError lErr = RadioError::None;
#if defined(RADIO_SX1262)
            lErr = mRadio.startTransmitBlocking(mTxBuffer, mTxLen);
#else
            lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
#endif
            if (lErr == RadioError::None)
            {
                mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
                mDiscoveryTimingTrace.txStartUs = micros();
                mStateTimer = millis();
                mState = ControllerState::DiscoveryListening;
            }
            else if (lErr == RadioError::Busy)
            {
                return;
            }
            else
            {
                mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
                resetDiscoveryTimingTrace();
                mState = ControllerState::Idle;
            }
        }
        else
        {
            mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
            resetDiscoveryTimingTrace();
            mState = ControllerState::Idle;
        }
    }
    else if (mState == ControllerState::DiscoveryListening)
    {
        RadioError lRxErr = RadioError::None;
#if defined(RADIO_SX1262)
        if (mRadio.state() == RadioState::Transmitting)
        {
            if (!mRadio.isTxDone())
            {
                mDiscoveryTimingTrace.rxBusyCount++;
                return;
            }
            if (mDiscoveryTimingTrace.txDoneUs == 0)
                mDiscoveryTimingTrace.txDoneUs = micros();
        }

        if (mRadio.state() == RadioState::Receiving)
        {
            if (mDiscoveryTimingTrace.rxReadyUs == 0)
                mDiscoveryTimingTrace.rxReadyUs = micros();
        }
        else
        {
            lRxErr = mRadio.startReceive();
            if (lRxErr == RadioError::Busy)
            {
                mDiscoveryTimingTrace.rxBusyCount++;
                return;
            }
            if (lRxErr == RadioError::None && mDiscoveryTimingTrace.rxReadyUs == 0)
                mDiscoveryTimingTrace.rxReadyUs = micros();
        }
#else
        lRxErr = ensureReceiveAfterTransmit();
        if (lRxErr == RadioError::Busy)
            return;
#endif
        if (lRxErr != RadioError::None)
        {
            resetDiscoveryTimingTrace();
            mState = ControllerState::Idle;
            return;
        }

        const unsigned long lListenElapsedMs = static_cast<unsigned long>(millis() - mStateTimer);
        if (lListenElapsedMs > 2000)
        {
            logDiscoveryTimingTrace((mPairingFreqIdx + 1 < IOHC_NUM_FREQUENCIES) ? "next" : "done", lListenElapsedMs);
            // Next frequency or done
            mPairingFreqIdx++;
            if (mPairingFreqIdx < IOHC_NUM_FREQUENCIES)
            {
                mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
                resetDiscoveryTimingTrace();
                mState = ControllerState::DiscoverySending;
            }
            else
            {
                mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
                resetDiscoveryTimingTrace();
                mState = ControllerState::Idle;
                startReceive();
            }
        }
    }
}

void IoHomeController::processScanSending()
{
    if (mScanIndex >= IOHC_SCAN_COMMANDS_COUNT)
    {
        mState = ControllerState::Idle;
        return;
    }

    mTxFrame.init();
    mTxFrame.setStart2W();
    mTxFrame.setSrcNode(mOwnNodeId);
    mTxFrame.setDestNode(mScanTargetNode);
    mTxFrame.commandId = static_cast<IoHomeCommand>(IOHC_SCAN_COMMANDS[mScanIndex]);
    mTxFrame.dataLen = 0;
    mTxFrame.hasHmac = false;

    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::Idle;
        return;
    }

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::ScanWaitResponse;
        }
        else if (lErr == RadioError::Busy)
        {
            return;
        }
        else
        {
            mState = ControllerState::Idle;
        }
    }
    else
    {
        mState = ControllerState::Idle;
    }
}

void IoHomeController::processScanWaitResponse()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::Idle;
        return;
    }

    // Check for response
    bool lGotResponse = false;
    if (mRadio.isPacketAvailable())
    {
        uint8_t lLen = mRadio.readPacket(mRxBuffer, sizeof(mRxBuffer));
        if (lLen > 0 && mRxFrame.deserialize(mRxBuffer, lLen))
        {
            if (mRxFrame.getSrcNodeId() == mScanTargetNode)
            {
                recordCommandScanResponse(mScanIndex);
                logInfoP("Scan: cmd 0x%02X → response 0x%02X (%d bytes)",
                         IOHC_SCAN_COMMANDS[mScanIndex],
                         static_cast<uint8_t>(mRxFrame.commandId),
                         mRxFrame.dataLen);
                lGotResponse = true;
            }
        }
    }

    // Timeout: move to next command
    if (millis() - mStateTimer > IOHC_RX_TIMEOUT_MS)
    {
        if (!lGotResponse)
        {
            recordCommandScanResponse(mScanIndex);
        }

        mScanIndex++;
        if (mScanIndex < IOHC_SCAN_COMMANDS_COUNT)
        {
            mState = ControllerState::ScanSending;
        }
        else
        {
            // Scan complete — log summary and persist results
            logInfoP("Scan complete: %u/%u commands responded", mScanResult.responsesFound, mScanResult.commandsScanned);
            logInfoP("Scan: target=0x%06X", mScanResult.targetNodeId);

            // Persist discovered commands to remote map if available
            if (mModule)
            {
                IoHomeRemoteEntry *lEntry = mModule->remoteMap().findRemote(mScanResult.targetNodeId);
                if (lEntry)
                {
                    mModule->remoteMap().setRemoteSupportedCommands(mScanResult.targetNodeId,
                                                                    mScanResult.bitmask,
                                                                    sizeof(mScanResult.bitmask));
                    logInfoP("Scan: updated supported commands for 0x%06X", mScanResult.targetNodeId);
                }
            }

            mState = ControllerState::Idle;
            startReceive();
        }
    }
}

RadioError IoHomeController::configureTxRadio(uint16_t iPreambleSymbols, const uint32_t *iFrequencyHz)
{
    if (iFrequencyHz != nullptr)
    {
        const RadioError lFreqErr = mRadio.setFrequency(*iFrequencyHz);
        if (lFreqErr != RadioError::None)
            return lFreqErr;
        updateCurrentFrequencyIndex(*iFrequencyHz);
    }

    return mRadio.setPreambleLength(iPreambleSymbols);
}

RadioError IoHomeController::ensureReceiveAfterTransmit()
{
    if (mRadio.state() == RadioState::Transmitting && !mRadio.isTxDone())
        return RadioError::Busy;

    if (mRadio.state() == RadioState::Receiving)
        return RadioError::None;

    return mRadio.startReceive();
}

void IoHomeController::updateCurrentFrequencyIndex(uint32_t iFrequencyHz)
{
    for (uint8_t i = 0; i < IOHC_NUM_FREQUENCIES; i++)
    {
        if (IOHC_FREQUENCIES[i] == iFrequencyHz)
        {
            mCurrentFreqIdx = i;
            return;
        }
    }
}

// --- Helper methods ---

void IoHomeController::buildTxFrame(const IoHomeQueueEntry &iEntry)
{
    mTxFrame.init();
    if (iEntry.retries == 0)
    {
        mTxFrame.setStart2W(); // START flag only on first attempt
    }
    else
    {
        // Continuation frame: 2W mode, no START flag
        mTxFrame.ctrlByte0 = 0;
        mTxFrame.ctrlByte1 = 0x00; // version 0, matching real gateway behavior
    }
    mTxFrame.setSrcNode(mOwnNodeId);
    mTxFrame.setDestNode(iEntry.destNodeId);
    mTxFrame.commandId = iEntry.command;

    switch (iEntry.command)
    {
    case IoHomeCommand::Execute:
    {
        // Check if target channel uses 1W protocol
        IoHomecontrolChannel *lTargetCh = nullptr;
        if (mModule)
        {
            for (uint8_t i = 0; i < IOHC_ChannelCount; i++)
            {
                IoHomecontrolChannel *lCh = mModule->getChannel(i);
                if (lCh && lCh->isPaired() && lCh->getNodeId() == iEntry.destNodeId)
                {
                    lTargetCh = lCh;
                    break;
                }
            }
        }

        if (lTargetCh && lTargetCh->is1W())
        {
            // 1W Execute: fire-and-forget with sequence counter + HMAC
            mTxFrame.set1WMode();
            mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END); // START+END both set

            mTxFrame.data[0] = IOHC_ORIGINATOR_USER; // 0x01
            mTxFrame.data[1] = IOHC_ACEI_1W;         // 0x43

            if (iEntry.param3 != 0xFF)
            {
                // Extended 16-byte format (_p0x00_16):
                // origin(1)+acei(1)+main[2]+fp1(1)+fp2(1)+data[2]+seq(2)+hmac(6) = 16B
                mTxFrame.data[2] = iEntry.param;  // main high byte
                mTxFrame.data[3] = 0x00;          // main low byte
                mTxFrame.data[4] = iEntry.param2; // fp1
                mTxFrame.data[5] = iEntry.param3; // fp2
                mTxFrame.data[6] = 0x00;          // data[0]
                mTxFrame.data[7] = 0x00;          // data[1]

                uint16_t lSeq = lTargetCh->incrementSequence1W();
                mTxFrame.data[8] = (lSeq >> 8) & 0xFF;
                mTxFrame.data[9] = lSeq & 0xFF;
                mTxFrame.dataLen = 10;

                // HMAC input: cmd(1) + origin+acei+main[2]+fp1+fp2+data[2] = 9 bytes
                uint8_t lHmacIn[9];
                lHmacIn[0] = static_cast<uint8_t>(mTxFrame.commandId);
                memcpy(lHmacIn + 1, mTxFrame.data, 8);
                IoHomeCrypto::createHmac1W(lHmacIn, 9, lSeq, iEntry.encKey, mTxFrame.hmac);
                mTxFrame.hasHmac = true;
            }
            else
            {
                // Standard 14-byte format (_p0x00_14):
                // origin(1)+acei(1)+main[2]+fp1(1)+fp2(1)+seq(2)+hmac(6) = 14B
                if (iEntry.param <= 100)
                {
                    mTxFrame.data[2] = iEntry.param * 2; // position × 2 (0=open, 200=closed)
                    mTxFrame.data[3] = 0x00;
                }
                else
                {
                    mTxFrame.data[2] = iEntry.param; // special (STOP=0xD2, FAVORITE=0xD8)
                    mTxFrame.data[3] = 0x00;
                }

                if (iEntry.param <= 100 && iEntry.param2 != 0xFF)
                {
                    mTxFrame.data[4] = 0x80;              // FP1: slat flag
                    mTxFrame.data[5] = iEntry.param2 * 2; // FP2: slat × 2
                }
                else
                {
                    mTxFrame.data[4] = 0x00;
                    mTxFrame.data[5] = 0x00;
                }

                uint16_t lSeq = lTargetCh->incrementSequence1W();
                mTxFrame.data[6] = (lSeq >> 8) & 0xFF;
                mTxFrame.data[7] = lSeq & 0xFF;
                mTxFrame.dataLen = 8;

                // 1W HMAC: input = cmd(1) + origin+acei+main[2]+fp1+fp2 = 7 bytes
                uint8_t lHmacIn[7];
                lHmacIn[0] = static_cast<uint8_t>(mTxFrame.commandId);
                memcpy(lHmacIn + 1, mTxFrame.data, 6);
                IoHomeCrypto::createHmac1W(lHmacIn, 7, lSeq, iEntry.encKey, mTxFrame.hmac);
                mTxFrame.hasHmac = true;
            }
            mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
        }
        else
        {
            // 2W Execute: challenge-response authentication
            // Payload format (from reference: io-rts-esp32):
            // Byte 0: 0x01 (originator: user remote control)
            // Byte 1: 0x67 (ACEI priority: user level 2)
            // For normal positions (0-100%):
            //   Byte 2: position × 2 (0=open, 200=closed)
            //   Byte 3-7: {0x00, 0x80, 0xD8, 0x06, 0x00}
            //   Total: 8 bytes
            // For special positions (STOP=0xD2, FAVORITE=0xD8):
            //   Byte 2: raw special value
            //   Byte 3-5: {0x00, 0x00, 0x00}
            //   Total: 6 bytes

            mTxFrame.data[0] = IOHC_ORIGINATOR_USER; // originator: user
            mTxFrame.data[1] = IOHC_ACEI_DEFAULT;    // ACEI priority

            if (iEntry.param <= 100)
            {
                // Normal position (0-100%)
                mTxFrame.data[2] = iEntry.param * 2;
                mTxFrame.data[3] = 0x00;
                mTxFrame.data[4] = 0x80;
                mTxFrame.data[5] = 0xD8;
                mTxFrame.data[6] = 0x06; // standard mode (0x05 = quiet)
                mTxFrame.data[7] = 0x00;
                mTxFrame.dataLen = 8;

                // Second parameter (slat angle for venetian blinds) appended after
                if (iEntry.param2 != 0xFF)
                {
                    mTxFrame.data[8] = iEntry.param2 * 2;
                    mTxFrame.data[9] = 0x00;
                    mTxFrame.dataLen = 10;
                }
            }
            else
            {
                // Special position (STOP=0xD2, FAVORITE=0xD8)
                mTxFrame.data[2] = iEntry.param;
                mTxFrame.data[3] = 0x00;
                mTxFrame.data[4] = 0x00;
                mTxFrame.data[5] = 0x00;
                mTxFrame.dataLen = 6;
            }

            // 2W Execute: authenticated via challenge-response (per nicolas5000/rspaargaren)
            // Device will respond with ChallengeRequest (0x3C), processResponse() handles 0x3D
            mTxFrame.hasHmac = false;
            mAuthResponseSent = false;
        }
        break;
    }
    case IoHomeCommand::Private:
        // GetStatus03: 3-byte payload {0x03, 0x00, 0x00}, no auth
        mTxFrame.data[0] = 0x03;
        mTxFrame.data[1] = 0x00;
        mTxFrame.data[2] = 0x00;
        mTxFrame.dataLen = 3;
        mTxFrame.hasHmac = false;
        break;

    case IoHomeCommand::ActivateMode:
    {
        // Check if target channel uses 1W protocol
        IoHomecontrolChannel *lTargetChAM = nullptr;
        if (mModule)
        {
            for (uint8_t i = 0; i < IOHC_ChannelCount; i++)
            {
                IoHomecontrolChannel *lCh = mModule->getChannel(i);
                if (lCh && lCh->isPaired() && lCh->getNodeId() == iEntry.destNodeId)
                {
                    lTargetChAM = lCh;
                    break;
                }
            }
        }

        if (lTargetChAM && lTargetChAM->is1W())
        {
            // 1W ActivateMode (_p0x01_13): origin(1)+acei(1)+main(1)+fp1(1)+fp2(1)+seq(2)+hmac(6) = 13B
            // Note: main is 1 byte (not 2!) in _p0x01_13
            mTxFrame.set1WMode();
            mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);

            mTxFrame.data[0] = IOHC_ORIGINATOR_USER;                           // 0x01
            mTxFrame.data[1] = IOHC_ACEI_1W;                                   // 0x43
            mTxFrame.data[2] = iEntry.param;                                   // main (1 byte)
            mTxFrame.data[3] = (iEntry.param2 != 0xFF) ? iEntry.param2 : 0x01; // fp1
            mTxFrame.data[4] = 0x00;                                           // fp2

            uint16_t lSeqAM = lTargetChAM->incrementSequence1W();
            mTxFrame.data[5] = (lSeqAM >> 8) & 0xFF;
            mTxFrame.data[6] = lSeqAM & 0xFF;
            mTxFrame.dataLen = 7;

            // HMAC input: cmd(1) + origin+acei+main+fp1+fp2 = 6 bytes
            uint8_t lHmacInAM[6];
            lHmacInAM[0] = static_cast<uint8_t>(mTxFrame.commandId); // 0x01
            memcpy(lHmacInAM + 1, mTxFrame.data, 5);
            IoHomeCrypto::createHmac1W(lHmacInAM, 6, lSeqAM, iEntry.encKey, mTxFrame.hmac);
            mTxFrame.hasHmac = true;
            mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
        }
        else
        {
            // 2W ActivateMode (0x01) payload format (from cridp reference):
            // Byte 0: originator (0x01 = user remote)
            // Byte 1: ACEI priority (0x67 = user remote, level 3)
            // Byte 2-3: FP1 main parameter (e.g., 0xD803 = vent, 0xD800 = favorite)
            // Byte 4-5: FP2 secondary parameter (0xD400 = ignore)
            // Byte 6-12: padding/flags
            // Total: 13 bytes

            mTxFrame.data[0] = IOHC_ORIGINATOR_USER;
            mTxFrame.data[1] = IOHC_ACEI_DEFAULT;
            // FP1: param as 16-bit value (high byte in param, low byte in param2 if != 0xFF)
            mTxFrame.data[2] = iEntry.param;
            mTxFrame.data[3] = (iEntry.param2 != 0xFF) ? iEntry.param2 : 0x00;
            // FP2: ignore
            mTxFrame.data[4] = (IOHC_POSITION_UNKNOWN >> 8) & 0xFF;
            mTxFrame.data[5] = IOHC_POSITION_UNKNOWN & 0xFF;
            // Remaining bytes: standard padding
            mTxFrame.data[6] = 0x00;
            mTxFrame.data[7] = 0x00;
            mTxFrame.data[8] = 0x00;
            mTxFrame.data[9] = 0x00;
            mTxFrame.data[10] = 0x00;
            mTxFrame.data[11] = 0x00;
            mTxFrame.data[12] = 0x00;
            mTxFrame.dataLen = 13;

            // 2W ActivateMode: authenticated via challenge-response (per nicolas5000)
            mTxFrame.hasHmac = false;
            mAuthResponseSent = false;
        }
        break;
    }

    case IoHomeCommand::GetName:
    case IoHomeCommand::GetGeneralInfo1:
    case IoHomeCommand::GetGeneralInfo2:
    case IoHomeCommand::GetGeneralInfo3:
        mTxFrame.dataLen = 0;
        mTxFrame.hasHmac = false;
        break;

    case IoHomeCommand::SetName:
        // SetName (0x52) — authenticated via challenge-response (per nicolas5000)
        // Payload: 16 bytes zero-padded name (Latin-1)
        memcpy(mTxFrame.data, iEntry.nameData, IOHC_NAME_MAX_SIZE);
        mTxFrame.dataLen = IOHC_NAME_MAX_SIZE;
        mTxFrame.hasHmac = false;
        mAuthResponseSent = false; // reset auth state for new command
        break;

    case IoHomeCommand::WritePrivate:
    {
        // WritePrivate (0x20) — Cozy/Atlantic thermostat command
        // param = function selector (0x03=temp, 0x04=mode, etc.), param2 = value
        // Authenticated with HMAC
        if (iEntry.param == 0x03)
            mTxFrame.dataLen = IoHomeCozyPayload::buildTemperature(mTxFrame.data, iEntry.param2);
        else if (iEntry.param == 0x04)
            mTxFrame.dataLen = IoHomeCozyPayload::buildMode(mTxFrame.data, iEntry.param2);
        else if (iEntry.param == 0x10)
            mTxFrame.dataLen = IoHomeCozyPayload::buildPresence(mTxFrame.data, iEntry.param2);
        else if (iEntry.param == 0x0E)
            mTxFrame.dataLen = IoHomeCozyPayload::buildWindow(mTxFrame.data, iEntry.param2);
        else if (iEntry.param == 0x0C)
            mTxFrame.dataLen = IoHomeCozyPayload::buildPowerOn(mTxFrame.data);
        else if (iEntry.param == 0x00)
            mTxFrame.dataLen = IoHomeCozyPayload::buildMidnightSync(mTxFrame.data);
        else
        {
            // Unknown sub-command — use Cozy wire format
            mTxFrame.data[0] = IOHC_COZY_ORIGINATOR;
            mTxFrame.data[1] = IOHC_COZY_ACEI_WRITE;
            mTxFrame.data[2] = 0x01;
            mTxFrame.data[3] = iEntry.param;
            mTxFrame.data[4] = iEntry.param2;
            mTxFrame.dataLen = 5;
        }

        // WritePrivate (0x20): authenticated via challenge-response (per rspaargaren)
        // Device will respond with ChallengeRequest (0x3C), processResponse() handles 0x3D
        mTxFrame.hasHmac = false;
        mAuthResponseSent = false;
        break;
    }

    case IoHomeCommand::SendKey1W:
    {
        // 1W key transfer: 1W mode, 20-byte payload
        // Bytes 0-15: encrypted key, byte 16: manufacturer, byte 17: controller marker, bytes 18-19: sequence
        mTxFrame.set1WMode();
        mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END); // standalone 1W: START+END (per rspaargaren)

        // Encrypt the key with the transfer key
        uint8_t lEncKey1W[16];
        uint8_t lDeviceNodeAddr[3] = {
            static_cast<uint8_t>((iEntry.destNodeId >> 16) & 0xFF),
            static_cast<uint8_t>((iEntry.destNodeId >> 8) & 0xFF),
            static_cast<uint8_t>(iEntry.destNodeId & 0xFF)};
        mTxFrame.setSrcNode(iEntry.destNodeId);
        mTxFrame.setDestNode(0x00003F);
        IoHomeCrypto::encrypt1WKey(iEntry.encKey, IOHC_TRANSFER_KEY, lDeviceNodeAddr, lEncKey1W);
        memcpy(mTxFrame.data, lEncKey1W, 16);
        mTxFrame.data[16] = iEntry.param;                                   // manufacturer ID
        mTxFrame.data[17] = 0x01;                                           // controller marker
        mTxFrame.data[18] = (iEntry.param2 != 0xFF) ? iEntry.param2 : 0x00; // sequence high
        mTxFrame.data[19] = (iEntry.param3 != 0xFF) ? iEntry.param3 : 0x00; // sequence low
        mTxFrame.dataLen = 20;

        // 1W HMAC for 0x30 uses only command + encrypted key, with the sequence passed separately.
        uint16_t lSeq1W = ((uint16_t)mTxFrame.data[18] << 8) | mTxFrame.data[19];
        uint8_t lHmacInput1W[17];
        lHmacInput1W[0] = static_cast<uint8_t>(mTxFrame.commandId);
        memcpy(lHmacInput1W + 1, mTxFrame.data, 16); // encrypted key only
        IoHomeCrypto::createHmac1W(lHmacInput1W, 17, lSeq1W, iEntry.encKey, mTxFrame.hmac);
        mTxFrame.hasHmac = true;
        mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
        break;
    }

    case IoHomeCommand::AddressRequest:
        // Address request: empty payload, no HMAC
        mTxFrame.dataLen = 0;
        mTxFrame.hasHmac = false;
        break;

    case IoHomeCommand::LaunchKeyTransfer:
    {
        // Launch key transfer: 6-byte challenge payload, no HMAC
        uint8_t lChallengeLKT[6];
        IoHomeCrypto::generateChallenge(lChallengeLKT);
        memcpy(mTxFrame.data, lChallengeLKT, 6);
        mTxFrame.dataLen = 6;
        mTxFrame.hasHmac = false;

        // Store challenge for subsequent key exchange
        memcpy(mPairingChallenge, lChallengeLKT, 6);
        break;
    }

    case IoHomeCommand::RemoveController:
    {
        // RemoveController (0x39): 2W authenticated controller removal
        mTxFrame.data[0] = IOHC_ORIGINATOR_USER;
        mTxFrame.data[1] = IOHC_ACEI_DEFAULT;
        mTxFrame.dataLen = 2;
        mTxFrame.hasHmac = false;
        mAuthResponseSent = false;
        break;
    }

    default:
        mTxFrame.dataLen = 0;
        mTxFrame.hasHmac = false;
        break;
    }
}

void IoHomeController::dispatchRxFrame()
{
    if (!mModule)
        return;

    uint32_t lSrcNode = mRxFrame.getSrcNodeId();

    if (mState == ControllerState::DiscoveryListening &&
        (mRxFrame.commandId == IoHomeCommand::DiscoverResponse ||
         mRxFrame.commandId == IoHomeCommand::DiscoverSPEResponse))
    {
        mModule->remoteMap().observeAddress(lSrcNode);
        recordScanFrame(mRxFrame, mRadio.lastRssi(), mCurrentFreqIdx);
        updateNodeStats(lSrcNode, mRadio.lastRssi(), mRxFrame.commandId);
        logInfoP("Discovery: %s from 0x%06X freq=%d rssi=%ddBm",
                 commandName(mRxFrame.commandId), lSrcNode, mCurrentFreqIdx, mRadio.lastRssi());
        return;
    }

    // Find the channel that matches this source node
    for (uint8_t i = 0; i < IOHC_ChannelCount; i++)
    {
        IoHomecontrolChannel *lCh = mModule->getChannel(i);
        if (lCh && lCh->isPaired() && lCh->getNodeId() == lSrcNode)
        {
            // Verify HMAC on authenticated frames before trusting data
            if (mRxFrame.hasHmac)
            {
                // Check that we have a pending challenge (not all zeros)
                // All-zero challenge means no command was sent — reject as potential replay
                const uint8_t *lChallenge = lCh->getLastChallenge();
                bool lHasPendingChallenge = false;
                for (int j = 0; j < 6; j++)
                {
                    if (lChallenge[j] != 0)
                    {
                        lHasPendingChallenge = true;
                        break;
                    }
                }

                if (!lHasPendingChallenge)
                {
                    // For unsolicited StatusUpdate with HMAC, initiate challenge-response auth
                    if (mRxFrame.commandId == IoHomeCommand::StatusUpdate &&
                        mState == ControllerState::Idle && !mPassiveMode)
                    {
                        mPendingAuthFrame = mRxFrame;
                        mAuthSrcNodeId = lSrcNode;
                        mAuthChannelIdx = i;
                        IoHomeCrypto::generateChallenge(mAuthChallenge);
                        mState = ControllerState::AuthSendChallenge;
                        return; // don't dispatch yet — wait for auth
                    }
                    logInfoP("HMAC frame rejected: no pending challenge for node 0x%06X", lSrcNode);
                    break;
                }

                uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA] = {0};
                const size_t lHmacInputLen = buildHmacInput(mRxFrame, lHmacInput, sizeof(lHmacInput));
                if (lHmacInputLen == 0 ||
                    !IoHomeCrypto::verifyHmac(lHmacInput, lHmacInputLen,
                                              mRxFrame.hmac, lChallenge, lCh->getEncryptionKey()))
                {
                    logInfoP("HMAC verification failed for node 0x%06X", lSrcNode);
                    break;
                }

                // Clear challenge after successful verification to prevent replay
                static const uint8_t sZeroChallenge[6] = {};
                lCh->setLastChallenge(sZeroChallenge);
            }

            // Dispatch based on command type
            switch (mRxFrame.commandId)
            {
            case IoHomeCommand::StatusUpdate:
            {
                // StatusUpdate (0x71) layout per reference (io-rts-esp32):
                //   data[0]:    flags (bit 0 = stopped)
                //   data[1]:    flags (bit 7 = status-expected: device will auto-send updates)
                //   data[5:6]:  target position (16-bit BE)
                //   data[7:8]:  current position (16-bit BE)
                //   data[10]:   estimate / timer
                //   Position encoding: raw * 100 / IOHC_POSITION_MAX (0xC800)
                //   Minimum 11 bytes for full status
                if (mRxFrame.dataLen >= 9)
                {
                    bool lStopped = (mRxFrame.data[0] & 0x01) != 0;
                    float lCurrentPercent = 0.0f;
                    bool lHasCurrentPosition = false;
                    float lTargetPercent = 0.0f;
                    bool lHasTargetPosition = false;
                    uint16_t lTargetRaw = ((uint16_t)mRxFrame.data[5] << 8) | mRxFrame.data[6];
                    if (lTargetRaw <= IOHC_POSITION_MAX)
                    {
                        lTargetPercent = (float)lTargetRaw * 100.0f / IOHC_POSITION_MAX;
                        if (lTargetPercent > 100.0f)
                            lTargetPercent = 100.0f;
                        lHasTargetPosition = true;
                        lCh->onTargetPositionFeedback(lTargetPercent);
                    }
                    uint16_t lCurrentRaw = ((uint16_t)mRxFrame.data[7] << 8) | mRxFrame.data[8];

                    // Filter special values before percentage conversion
                    if (lCurrentRaw != (IOHC_POSITION_STOP & 0xFFFF) &&
                        lCurrentRaw != (IOHC_POSITION_UNKNOWN & 0xFFFF) &&
                        lCurrentRaw != (IOHC_POSITION_FAVORITE & 0xFFFF))
                    {
                        uint16_t lPosRaw = lCurrentRaw;
                        // Unknown position fallback: if current > max and stopped, use target
                        if (lPosRaw > IOHC_POSITION_MAX && lStopped && mRxFrame.dataLen >= 7)
                        {
                            uint16_t lTargetRaw = ((uint16_t)mRxFrame.data[5] << 8) | mRxFrame.data[6];
                            if (lTargetRaw <= IOHC_POSITION_MAX)
                                lPosRaw = lTargetRaw;
                        }
                        lCurrentPercent = (float)lPosRaw * 100.0f / IOHC_POSITION_MAX;
                        if (lCurrentPercent > 100.0f)
                            lCurrentPercent = 100.0f;
                        lHasCurrentPosition = true;
                        lCh->onPositionFeedback(lCurrentPercent);
                    }
                    lCh->onStatusUpdate(!lStopped);
                    lCh->logStatusSummary(lCurrentPercent, lHasCurrentPosition,
                                          lTargetPercent, lHasTargetPosition,
                                          !lStopped);
                }
                // Status-expected flag: device will auto-send StatusUpdate
                if (mRxFrame.dataLen >= 2 && (mRxFrame.data[1] & 0x80))
                    lCh->onStatusExpected();
                // Battery level in byte 3 (if present)
                if (mRxFrame.dataLen >= 4)
                {
                    uint8_t lBattery = mRxFrame.data[3];
                    if (lBattery <= 100)
                        lCh->onBatteryLevel(lBattery);
                }
                // Send StatusUpdateResponse ACK for unsolicited StatusUpdate
                // Only if addressed directly to us (not broadcast)
                {
                    uint32_t lDestNode = mRxFrame.getDestNodeId();
                    if (lDestNode == mOwnNodeId)
                        sendStatusUpdateResponse(lSrcNode);
                }
                break;
            }
            case IoHomeCommand::Execute: // response to execute
            {
                // Execute response uses same layout as StatusUpdate
                if (mRxFrame.dataLen >= 9)
                {
                    bool lStopped = (mRxFrame.data[0] & 0x01) != 0;
                    float lCurrentPercent = 0.0f;
                    bool lHasCurrentPosition = false;
                    float lTargetPercent = 0.0f;
                    bool lHasTargetPosition = false;
                    uint16_t lTargetRaw = ((uint16_t)mRxFrame.data[5] << 8) | mRxFrame.data[6];
                    if (lTargetRaw <= IOHC_POSITION_MAX)
                    {
                        lTargetPercent = (float)lTargetRaw * 100.0f / IOHC_POSITION_MAX;
                        if (lTargetPercent > 100.0f)
                            lTargetPercent = 100.0f;
                        lHasTargetPosition = true;
                        lCh->onTargetPositionFeedback(lTargetPercent);
                    }
                    uint16_t lCurrentRaw = ((uint16_t)mRxFrame.data[7] << 8) | mRxFrame.data[8];

                    if (lCurrentRaw != (IOHC_POSITION_STOP & 0xFFFF) &&
                        lCurrentRaw != (IOHC_POSITION_UNKNOWN & 0xFFFF) &&
                        lCurrentRaw != (IOHC_POSITION_FAVORITE & 0xFFFF))
                    {
                        uint16_t lPosRaw = lCurrentRaw;
                        if (lPosRaw > IOHC_POSITION_MAX && lStopped && mRxFrame.dataLen >= 7)
                        {
                            if (lTargetRaw <= IOHC_POSITION_MAX)
                                lPosRaw = lTargetRaw;
                        }
                        lCurrentPercent = (float)lPosRaw * 100.0f / IOHC_POSITION_MAX;
                        if (lCurrentPercent > 100.0f)
                            lCurrentPercent = 100.0f;
                        lHasCurrentPosition = true;
                        lCh->onPositionFeedback(lCurrentPercent);
                    }
                    lCh->onStatusUpdate(!lStopped);
                    lCh->logStatusSummary(lCurrentPercent, lHasCurrentPosition,
                                          lTargetPercent, lHasTargetPosition,
                                          !lStopped);
                }
                break;
            }
            case IoHomeCommand::PrivateResponse:
            {
                // PrivateResponse (0x04) layout per reference:
                //   data[0]:    flags (bit 0 = stopped)
                //   data[1]:    flags (bit 7 = status-expected)
                //   data[2:3]:  target position (16-bit BE)
                //   data[4:5]:  current position (16-bit BE)
                //   data[7]:    estimate (travel time in seconds; 0xFF/0x00 = unknown)
                //   Minimum 6 bytes for position data
                if (mRxFrame.dataLen >= 6)
                {
                    bool lStopped = (mRxFrame.data[0] & 0x01) != 0;
                    float lCurrentPercent = 0.0f;
                    bool lHasCurrentPosition = false;
                    float lTargetPercent = 0.0f;
                    bool lHasTargetPosition = false;
                    uint16_t lTargetRaw = ((uint16_t)mRxFrame.data[2] << 8) | mRxFrame.data[3];
                    if (lTargetRaw <= IOHC_POSITION_MAX)
                    {
                        lTargetPercent = (float)lTargetRaw * 100.0f / IOHC_POSITION_MAX;
                        if (lTargetPercent > 100.0f)
                            lTargetPercent = 100.0f;
                        lHasTargetPosition = true;
                        lCh->onTargetPositionFeedback(lTargetPercent);
                    }
                    uint16_t lCurrentRaw = ((uint16_t)mRxFrame.data[4] << 8) | mRxFrame.data[5];

                    if (lCurrentRaw != (IOHC_POSITION_STOP & 0xFFFF) &&
                        lCurrentRaw != (IOHC_POSITION_UNKNOWN & 0xFFFF) &&
                        lCurrentRaw != (IOHC_POSITION_FAVORITE & 0xFFFF))
                    {
                        uint16_t lPosRaw = lCurrentRaw;
                        // Fallback: if current > max and stopped, use target
                        if (lPosRaw > IOHC_POSITION_MAX && lStopped && mRxFrame.dataLen >= 4)
                        {
                            if (lTargetRaw <= IOHC_POSITION_MAX)
                                lPosRaw = lTargetRaw;
                        }
                        lCurrentPercent = (float)lPosRaw * 100.0f / IOHC_POSITION_MAX;
                        if (lCurrentPercent > 100.0f)
                            lCurrentPercent = 100.0f;
                        lHasCurrentPosition = true;
                        lCh->onPositionFeedback(lCurrentPercent);
                    }
                    lCh->onStatusUpdate(!lStopped);
                    lCh->logStatusSummary(lCurrentPercent, lHasCurrentPosition,
                                          lTargetPercent, lHasTargetPosition,
                                          !lStopped);
                }
                // Status-expected flag
                if (mRxFrame.dataLen >= 2 && (mRxFrame.data[1] & 0x80))
                    lCh->onStatusExpected();
                // Estimate byte: travel time remaining in seconds
                if (mRxFrame.dataLen >= 8)
                {
                    uint8_t lEstimate = mRxFrame.data[7];
                    lCh->onEstimate(lEstimate);
                }
                break;
            }
            case IoHomeCommand::GetNameResponse:
            {
                lCh->onDeviceName((const char *)mRxFrame.data, mRxFrame.dataLen);
                break;
            }
            case IoHomeCommand::SetNameResponse:
            {
                // Device confirmed SetName — no payload to process
                break;
            }
            case IoHomeCommand::GetGeneralInfo1Response:
            {
                if (mRxFrame.dataLen >= 3)
                {
                    uint16_t lType = (mRxFrame.data[0] | ((uint16_t)mRxFrame.data[1] << 8)) & 0x3FF;
                    uint8_t lSubtype = mRxFrame.data[1] & 0x3F;
                    uint8_t lMfg = mRxFrame.data[2];
                    lCh->onDeviceInfo(lType, lSubtype, lMfg);
                }
                break;
            }
            case IoHomeCommand::GetGeneralInfo2Response:
            {
                // GetGeneralInfo2 (0x57) response: device type at data[10:11]
                // Type = data[10] << 2 | data[11] >> 6, Subtype = data[11] & 0x3F
                if (mRxFrame.dataLen >= 12)
                {
                    uint16_t lType = ((uint16_t)mRxFrame.data[10] << 2) | (mRxFrame.data[11] >> 6);
                    uint8_t lSubtype = mRxFrame.data[11] & 0x3F;
                    lCh->onDeviceInfo(lType, lSubtype, 0);
                }
                break;
            }
            case IoHomeCommand::GetGeneralInfo3Response:
            {
                // Position and status from info query
                float lPercent = 0.0f;
                bool lHasCurrentPosition = false;
                if (mRxFrame.dataLen >= 1)
                {
                    lPercent = (float)mRxFrame.data[0];
                    if (lPercent > 100.0f)
                        lPercent = 100.0f;
                    lHasCurrentPosition = true;
                    lCh->onPositionFeedback(lPercent);
                }
                if (mRxFrame.dataLen >= 3)
                {
                    bool lMoving = (mRxFrame.data[2] & 0x01) != 0;
                    lCh->onStatusUpdate(lMoving);
                    lCh->logStatusSummary(lPercent, lHasCurrentPosition,
                                          0.0f, false, lMoving);
                }
                break;
            }
            // --- Unused commands (documented for protocol completeness) ---
            case IoHomeCommand::Private2Response:        // 0x0D — response to alternate private command (not used)
            case IoHomeCommand::ConfirmationACK:         // 0x2D — device ACKs discovery confirmation (consumed implicitly)
            case IoHomeCommand::KeyTransferConfirmation: // 0x33 — device confirms key storage (not parsed in reference)
            case IoHomeCommand::Unknown46Response:       // 0x47 — undocumented (not used)
            case IoHomeCommand::Unknown4AResponse:       // 0x4B — undocumented (not used)
            case IoHomeCommand::SetConfig1Response:      // 0x70 — handled during pairing post-configuration
            case IoHomeCommand::StatusUpdateResponse:    // 0x72 — we send this, shouldn't receive it
            case IoHomeCommand::ErrorResponse:           // 0xFE — error from device
                break;

            default:
                break;
            }
            break; // found the channel
        }
    }
}

void IoHomeController::sendStatusUpdateResponse(uint32_t iDestNodeId)
{
    if (mPassiveMode)
        return;

    if (mState != ControllerState::Idle &&
        mState != ControllerState::AuthWaitResponse &&
        mState != ControllerState::ProcessResponse)
        return;

    mStatusAckDestNodeId = iDestNodeId;
    mStatusAckFreqIdx = 0;
    mState = ControllerState::StatusAckSend;
}

uint8_t IoHomeController::buildStatusUpdateResponse(uint32_t iDestNodeId, uint8_t *oBuffer, uint8_t iBufferLen) const
{
    IoHomeFrame lFrame;
    lFrame.init();
    lFrame.ctrlByte0 = IOHC_CTRL0_END; // response: END only (per nicolas5000)
    lFrame.ctrlByte1 = 0x00;
    lFrame.setSrcNode(mOwnNodeId);
    lFrame.setDestNode(iDestNodeId);
    lFrame.commandId = IoHomeCommand::StatusUpdateResponse;
    lFrame.data[0] = 0x05;
    lFrame.data[1] = 0x00;
    lFrame.dataLen = 2;
    lFrame.hasHmac = false;

    return lFrame.serialize(oBuffer, iBufferLen);
}

void IoHomeController::processStatusAckSend()
{
    if (mStatusAckFreqIdx >= IOHC_NUM_FREQUENCIES)
    {
        const RadioError lRxErr = mRadio.startReceive();
        if (lRxErr == RadioError::Busy)
            return;
        mState = ControllerState::Idle;
        return;
    }

    if (!isDutyCycleOk(mStatusAckFreqIdx))
    {
        mStatusAckFreqIdx++;
        return;
    }

    mTxLen = buildStatusUpdateResponse(mStatusAckDestNodeId, mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen == 0)
    {
        const RadioError lRxErr = mRadio.startReceive();
        if (lRxErr == RadioError::Busy)
            return;
        mState = ControllerState::Idle;
        return;
    }

    const uint32_t lAckFrequency = IOHC_FREQUENCIES[mStatusAckFreqIdx];
    const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT, &lAckFrequency);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mStatusAckFreqIdx++;
        return;
    }

    const RadioError lTxErr = mRadio.startTransmit(mTxBuffer, mTxLen);
    if (lTxErr == RadioError::None)
    {
        mStateTimer = millis();
        mState = ControllerState::StatusAckTxWait;
    }
    else if (lTxErr != RadioError::Busy)
    {
        mStatusAckFreqIdx++;
    }
}

void IoHomeController::processStatusAckTxWait()
{
    if (mRadio.state() != RadioState::Transmitting)
    {
        mStatusAckFreqIdx++;
        mState = ControllerState::StatusAckSend;
        return;
    }

    if (mRadio.isTxDone())
    {
        uint32_t lTxTimeMs = ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;
        mTxTimeAccum[mCurrentFreqIdx] += lTxTimeMs;
        mStatusAckFreqIdx++;
        mState = ControllerState::StatusAckSend;
    }
    else if (millis() - mStateTimer > IOHC_TX_TIMEOUT_MS)
    {
        mRadio.standby();
        mStatusAckFreqIdx++;
        mState = ControllerState::StatusAckSend;
    }
}

// --- Receive-side authentication ---

void IoHomeController::processAuthSendChallenge()
{
    // Build and send ChallengeRequest (0x3C) with 6-byte random challenge
    IoHomeFrame lFrame;
    lFrame.init();
    lFrame.ctrlByte0 = 0; // continuation frame: no START, no END (per nicolas5000)
    lFrame.ctrlByte1 = 0x00;
    lFrame.setSrcNode(mOwnNodeId);
    lFrame.setDestNode(mAuthSrcNodeId);
    lFrame.commandId = IoHomeCommand::ChallengeRequest;
    memcpy(lFrame.data, mAuthChallenge, 6);
    lFrame.dataLen = 6;
    lFrame.hasHmac = false;

    uint8_t lBuf[IOHC_FRAME_MAX_SIZE];
    uint8_t lLen = lFrame.serialize(lBuf, sizeof(lBuf));
    if (lLen > 0)
    {
        const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT);
        if (lPrepErr == RadioError::Busy)
            return;
        if (lPrepErr != RadioError::None)
        {
            mState = ControllerState::Idle;
            startReceive();
            return;
        }

        const RadioError lErr = mRadio.startTransmit(lBuf, lLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mState = ControllerState::AuthWaitResponse;
            return;
        }
        if (lErr == RadioError::Busy)
            return;
    }
    // Failed to send — discard and go idle
    mState = ControllerState::Idle;
    startReceive();
}

void IoHomeController::processAuthWaitResponse()
{
    const RadioError lRxErr = ensureReceiveAfterTransmit();
    if (lRxErr == RadioError::Busy)
        return;
    if (lRxErr != RadioError::None)
    {
        mState = ControllerState::Idle;
        return;
    }

    if (millis() - mStateTimer > IOHC_RX_TIMEOUT_MS)
    {
        // Timeout — discard pending frame, go idle
        mState = ControllerState::Idle;
        startReceive();
    }
    // ChallengeResponse arrival is handled in main loop() RX dispatch
}

// --- Passive mode ---

void IoHomeController::processPassiveFrame()
{
    // Passive mode: observe pairing exchanges to extract encryption keys
    // Flow: KeyInitTransfer (0x31) → ChallengeRequest (0x3C) → KeyTransfer (0x32)
    uint32_t lSrcNode = mRxFrame.getSrcNodeId();

    // Track all observed source addresses for remote management
    if (mModule)
        mModule->remoteMap().observeAddress(lSrcNode);

    // Record frame in scan buffer if network scan is active
    if (mNetworkScanActive)
    {
        recordScanFrame(mRxFrame, mRadio.lastRssi(), mCurrentFreqIdx);
        updateNodeStats(lSrcNode, mRadio.lastRssi(), mRxFrame.commandId);
    }

    switch (mRxFrame.commandId)
    {
    case IoHomeCommand::KeyInitTransfer:
        // Save the init frame and note which device is pairing
        mPassiveKeyInit = mRxFrame;
        mPassivePairNodeId = lSrcNode;
        logInfoP("Passive: KeyInitTransfer from 0x%06X", lSrcNode);
        break;

    case IoHomeCommand::ChallengeRequest:
        // Save challenge from the device being paired
        if (mRxFrame.dataLen >= 6)
        {
            memcpy(mPassiveChallenge, mRxFrame.data, 6);
            logInfoP("Passive: ChallengeRequest from 0x%06X", lSrcNode);
        }
        break;

    case IoHomeCommand::KeyTransfer:
    {
        // Decrypt the system key using TRANSFER_KEY + observed challenge
        if (mRxFrame.dataLen >= 16 && mPassivePairNodeId != 0)
        {
            const uint8_t lKeyInitData[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
            uint8_t lKeystream[16];
            if (IoHomeCrypto::crypt2WKey(lKeyInitData, sizeof(lKeyInitData), mPassiveChallenge, IOHC_TRANSFER_KEY, lKeystream))
            {
                uint8_t lExtractedKey[16];
                for (int k = 0; k < 16; k++)
                    lExtractedKey[k] = mRxFrame.data[k] ^ lKeystream[k];

                logInfoP("Passive: extracted system key from pairing of 0x%06X:", mPassivePairNodeId);
                // Log key bytes for debugging
                logInfoP("  Key: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                         lExtractedKey[0], lExtractedKey[1], lExtractedKey[2], lExtractedKey[3],
                         lExtractedKey[4], lExtractedKey[5], lExtractedKey[6], lExtractedKey[7],
                         lExtractedKey[8], lExtractedKey[9], lExtractedKey[10], lExtractedKey[11],
                         lExtractedKey[12], lExtractedKey[13], lExtractedKey[14], lExtractedKey[15]);
            }
            mPassivePairNodeId = 0; // reset
        }
        break;
    }
    default:
        // In passive mode, also dispatch to channels for status updates
        dispatchRxFrame();
        break;
    }
}

void IoHomeController::processGatewayFrame()
{
    if (!mGatewayMode)
    {
        dispatchRxFrame();
        return;
    }

    const uint32_t lSrcNode = mRxFrame.getSrcNodeId();

    if (mModule)
        mModule->remoteMap().observeAddress(lSrcNode);

    if (mNetworkScanActive)
    {
        recordScanFrame(mRxFrame, mRadio.lastRssi(), mCurrentFreqIdx);
        updateNodeStats(lSrcNode, mRadio.lastRssi(), mRxFrame.commandId);
    }

    switch (mRxFrame.commandId)
    {
    case IoHomeCommand::GetName:
        mTxLen = buildGatewayNameResponseFrame(mTxBuffer, sizeof(mTxBuffer),
                                               mGatewayNodeId, lSrcNode);
        break;
    case IoHomeCommand::SetName:
        mTxLen = buildGatewaySetNameResponseFrame(mTxBuffer, sizeof(mTxBuffer),
                                                  mGatewayNodeId, lSrcNode);
        break;
    case IoHomeCommand::GetGeneralInfo1:
        mTxLen = buildGatewayGetGeneralInfo1ResponseFrame(mTxBuffer, sizeof(mTxBuffer),
                                                          mGatewayNodeId, lSrcNode);
        break;
    default:
        mTxLen = 0;
        break;
    }

    if (mTxLen > 0)
    {
        const RadioError lPrepErr = configureTxRadio(IOHC_PREAMBLE_SHORT);
        if (lPrepErr != RadioError::None)
            return;

        const RadioError lTxErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lTxErr == RadioError::None)
            mTxTimeAccum[mCurrentFreqIdx] += ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;
        return;
    }

    switch (mGatewayState)
    {
    case ControllerState::GatewayIdle:
        processGatewayIdle();
        break;
    case ControllerState::GatewayWaitDiscoveryResponse:
        processGatewayWaitDiscoveryResponse();
        break;
    case ControllerState::GatewayWaitKeyTransfer:
        processGatewayWaitKeyTransfer();
        break;
    case ControllerState::GatewayWaitChallenge:
        processGatewayWaitChallenge();
        break;
    default:
        mGatewayState = ControllerState::GatewayIdle;
        processGatewayIdle();
        break;
    }
}

void IoHomeController::processGatewayIdle()
{
    const uint32_t lSrcNode = mRxFrame.getSrcNodeId();

    switch (mRxFrame.commandId)
    {
    case IoHomeCommand::DiscoverRequest:
        mGatewayPeerNodeId = lSrcNode;
        mGatewayDiscoverFreqIdx = mLastResponseFreqIdx;
        mTxLen = buildGatewayDiscoverAnswerFrame(mTxBuffer, sizeof(mTxBuffer),
                                                 mGatewayNodeId, lSrcNode);
        if (mTxLen == 0)
            return;
        if (configureTxRadio(IOHC_PREAMBLE_SHORT) != RadioError::None)
            return;
        if (mRadio.startTransmit(mTxBuffer, mTxLen) != RadioError::None)
            return;
        mTxTimeAccum[mCurrentFreqIdx] += ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;
        mGatewayState = ControllerState::GatewayWaitDiscoveryResponse;
        return;

    case IoHomeCommand::Confirmation:
        mGatewayPeerNodeId = lSrcNode;
        mTxLen = buildGatewayDiscoverActuatorAckFrame(mTxBuffer, sizeof(mTxBuffer),
                                                      mGatewayNodeId, lSrcNode);
        if (mTxLen == 0)
            return;
        if (configureTxRadio(IOHC_PREAMBLE_SHORT) != RadioError::None)
            return;
        if (mRadio.startTransmit(mTxBuffer, mTxLen) != RadioError::None)
            return;
        mTxTimeAccum[mCurrentFreqIdx] += ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;
        mGatewayState = ControllerState::GatewayWaitKeyTransfer;
        return;

    case IoHomeCommand::LaunchKeyTransfer:
        mGatewayPeerNodeId = lSrcNode;
        processGatewayWaitKeyTransfer();
        return;

    case IoHomeCommand::ChallengeRequest:
        if (mGatewayMemDataLen > 0)
        {
            mGatewayPeerNodeId = lSrcNode;
            processGatewayWaitChallenge();
            return;
        }
        break;

    default:
        break;
    }

    dispatchRxFrame();
}

void IoHomeController::processGatewayWaitDiscoveryResponse()
{
    const uint32_t lSrcNode = mRxFrame.getSrcNodeId();

    if (mRxFrame.commandId == IoHomeCommand::DiscoverRequest)
    {
        mGatewayState = ControllerState::GatewayIdle;
        processGatewayIdle();
        return;
    }

    if (mGatewayPeerNodeId != 0 && lSrcNode != mGatewayPeerNodeId)
    {
        dispatchRxFrame();
        return;
    }

    if (mRxFrame.commandId == IoHomeCommand::Confirmation)
    {
        mTxLen = buildGatewayDiscoverActuatorAckFrame(mTxBuffer, sizeof(mTxBuffer),
                                                      mGatewayNodeId, lSrcNode);
        if (mTxLen == 0)
            return;
        if (configureTxRadio(IOHC_PREAMBLE_SHORT) != RadioError::None)
            return;
        if (mRadio.startTransmit(mTxBuffer, mTxLen) != RadioError::None)
            return;
        mTxTimeAccum[mCurrentFreqIdx] += ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;
        mGatewayState = ControllerState::GatewayWaitKeyTransfer;
        return;
    }

    if (mRxFrame.commandId == IoHomeCommand::LaunchKeyTransfer)
    {
        processGatewayWaitKeyTransfer();
        return;
    }

    if (mRxFrame.commandId == IoHomeCommand::ChallengeRequest && mGatewayMemDataLen > 0)
    {
        processGatewayWaitChallenge();
        return;
    }

    dispatchRxFrame();
}

void IoHomeController::processGatewayWaitKeyTransfer()
{
    const uint32_t lSrcNode = mRxFrame.getSrcNodeId();

    if (mRxFrame.commandId == IoHomeCommand::DiscoverRequest)
    {
        memset(mGatewayKeyEncrypted, 0, sizeof(mGatewayKeyEncrypted));
        mGatewayMemCmd = 0;
        mGatewayMemDataLen = 0;
        mGatewayState = ControllerState::GatewayIdle;
        processGatewayIdle();
        return;
    }

    if (mGatewayPeerNodeId != 0 && lSrcNode != mGatewayPeerNodeId)
    {
        dispatchRxFrame();
        return;
    }

    if (mRxFrame.commandId != IoHomeCommand::LaunchKeyTransfer || mRxFrame.dataLen < 6)
    {
        dispatchRxFrame();
        return;
    }

    mGatewayPeerNodeId = lSrcNode;
    if (!buildGatewayEncryptedKey(mGatewayKey, mRxFrame.data, mGatewayKeyEncrypted))
        return;

    mGatewayMemCmd = static_cast<uint8_t>(IoHomeCommand::KeyTransfer);
    memcpy(mGatewayMemData, mGatewayKeyEncrypted, sizeof(mGatewayKeyEncrypted));
    mGatewayMemDataLen = sizeof(mGatewayKeyEncrypted);

    mTxLen = buildGatewayKeyTransferFrame(mTxBuffer, sizeof(mTxBuffer),
                                          mGatewayNodeId, lSrcNode,
                                          mGatewayKeyEncrypted);
    if (mTxLen == 0)
        return;
    if (configureTxRadio(IOHC_PREAMBLE_SHORT) != RadioError::None)
        return;
    if (mRadio.startTransmit(mTxBuffer, mTxLen) != RadioError::None)
        return;

    mTxTimeAccum[mCurrentFreqIdx] += ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;
    mGatewayState = ControllerState::GatewayWaitChallenge;
}

void IoHomeController::processGatewayWaitChallenge()
{
    const uint32_t lSrcNode = mRxFrame.getSrcNodeId();

    if (mRxFrame.commandId == IoHomeCommand::DiscoverRequest)
    {
        mGatewayPeerNodeId = 0;
        mGatewayMemCmd = 0;
        mGatewayMemDataLen = 0;
        memset(mGatewayKeyEncrypted, 0, sizeof(mGatewayKeyEncrypted));
        memset(mGatewayPeerChallenge, 0, sizeof(mGatewayPeerChallenge));
        mGatewayState = ControllerState::GatewayIdle;
        processGatewayIdle();
        return;
    }

    if (mGatewayPeerNodeId != 0 && lSrcNode != mGatewayPeerNodeId)
    {
        dispatchRxFrame();
        return;
    }

    if (mRxFrame.commandId != IoHomeCommand::ChallengeRequest || mRxFrame.dataLen < 6)
    {
        dispatchRxFrame();
        return;
    }

    memcpy(mGatewayPeerChallenge, mRxFrame.data, sizeof(mGatewayPeerChallenge));
    mTxLen = buildGatewayChallengeAnswerFrame(mTxBuffer, sizeof(mTxBuffer),
                                              mGatewayNodeId, lSrcNode,
                                              mGatewayMemCmd, mGatewayMemData,
                                              mGatewayMemDataLen,
                                              mGatewayPeerChallenge, mGatewayKey);
    if (mTxLen == 0)
        return;
    if (configureTxRadio(IOHC_PREAMBLE_SHORT) != RadioError::None)
        return;
    if (mRadio.startTransmit(mTxBuffer, mTxLen) != RadioError::None)
        return;

    mTxTimeAccum[mCurrentFreqIdx] += ((uint32_t)mTxLen * 8 * 1000) / IOHC_BITRATE;

    bool lKnownDevice = false;
    for (uint8_t i = 0; i < mGatewayDeviceCount; i++)
    {
        if (mGatewayPairedNodeIds[i] == lSrcNode)
        {
            lKnownDevice = true;
            break;
        }
    }

    if (!lKnownDevice && mGatewayDeviceCount < kMaxGatewayPairedDevices)
        mGatewayPairedNodeIds[mGatewayDeviceCount++] = lSrcNode;

    mGatewayPeerNodeId = 0;
    mGatewayMemCmd = 0;
    mGatewayMemDataLen = 0;
    memset(mGatewayKeyEncrypted, 0, sizeof(mGatewayKeyEncrypted));
    memset(mGatewayMemData, 0, sizeof(mGatewayMemData));
    memset(mGatewayPeerChallenge, 0, sizeof(mGatewayPeerChallenge));
    mGatewayState = ControllerState::GatewayIdle;
}

void IoHomeController::buildGatewayDiscoverAnswer(uint8_t *oBuffer,
                                                  uint8_t iBufferLen,
                                                  uint32_t iDeviceNodeId) const
{
    buildGatewayDiscoverAnswerFrame(oBuffer, iBufferLen, mGatewayNodeId, iDeviceNodeId);
}

void IoHomeController::buildGatewayDiscoverActuatorAck(uint8_t *oBuffer,
                                                       uint8_t iBufferLen,
                                                       uint32_t iDeviceNodeId) const
{
    buildGatewayDiscoverActuatorAckFrame(oBuffer, iBufferLen, mGatewayNodeId, iDeviceNodeId);
}

void IoHomeController::buildGatewayKeyTransfer(uint8_t *oBuffer,
                                               uint8_t iBufferLen,
                                               uint32_t iDeviceNodeId,
                                               const uint8_t iDeviceChallenge[6]) const
{
    uint8_t lEncryptedKey[16];
    if (!buildGatewayEncryptedKey(mGatewayKey, iDeviceChallenge, lEncryptedKey))
        return;
    buildGatewayKeyTransferFrame(oBuffer, iBufferLen, mGatewayNodeId, iDeviceNodeId,
                                 lEncryptedKey);
}

void IoHomeController::buildGatewayChallengeAnswer(uint8_t *oBuffer,
                                                   uint8_t iBufferLen,
                                                   uint32_t iDeviceNodeId) const
{
    buildGatewayChallengeAnswerFrame(oBuffer, iBufferLen, mGatewayNodeId,
                                     iDeviceNodeId, mGatewayMemCmd,
                                     mGatewayMemData, mGatewayMemDataLen,
                                     mGatewayPeerChallenge, mGatewayKey);
}

void IoHomeController::buildGatewayNameResponse(uint8_t *oBuffer,
                                                uint8_t iBufferLen,
                                                uint32_t iDeviceNodeId) const
{
    buildGatewayNameResponseFrame(oBuffer, iBufferLen, mGatewayNodeId, iDeviceNodeId);
}

void IoHomeController::buildGatewaySetNameResponse(uint8_t *oBuffer,
                                                   uint8_t iBufferLen,
                                                   uint32_t iDeviceNodeId) const
{
    buildGatewaySetNameResponseFrame(oBuffer, iBufferLen, mGatewayNodeId, iDeviceNodeId);
}

void IoHomeController::buildGatewayGetGeneralInfo1Response(uint8_t *oBuffer,
                                                           uint8_t iBufferLen,
                                                           uint32_t iDeviceNodeId,
                                                           const IoHomeFrame &iRequest) const
{
    (void)iRequest;
    buildGatewayGetGeneralInfo1ResponseFrame(oBuffer, iBufferLen,
                                             mGatewayNodeId, iDeviceNodeId);
}

void IoHomeController::logGatewayState(const char *iLabel) const
{
    logDebugP("Gateway: %s mode=%d state=%s peer=0x%06X paired=%u", iLabel,
              mGatewayMode ? 1 : 0,
              stateName(mGatewayState),
              mGatewayPeerNodeId,
              static_cast<unsigned>(mGatewayDeviceCount));
}

bool IoHomeController::hopFrequency()
{
    const uint8_t lNextFreqIdx = (mCurrentFreqIdx + 1) % IOHC_NUM_FREQUENCIES;
    if (mRadio.setFrequency(IOHC_FREQUENCIES[lNextFreqIdx]) != RadioError::None)
        return false;
    mCurrentFreqIdx = lNextFreqIdx;
    return true;
}

bool IoHomeController::isDutyCycleOk() const
{
    return isDutyCycleOk(mCurrentFreqIdx);
}

bool IoHomeController::isDutyCycleOk(uint8_t iFreqIdx) const
{
    // EU 868 MHz duty cycle: 1% per sub-band per hour = 36000 ms in 3600000 ms window
    uint32_t lMaxTxTime = IOHC_DUTY_CYCLE_WINDOW_MS / 100; // 1%
    if (iFreqIdx >= IOHC_NUM_FREQUENCIES)
        return false;
    return mTxTimeAccum[iFreqIdx] < lMaxTxTime;
}
