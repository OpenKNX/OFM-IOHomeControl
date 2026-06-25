#include "IoHomeController.h"

#ifdef TEST_NATIVE
#include "IoHomeControllerNativeStubs.h"
#include <assert.h>
#else
#include "../IoHomecontrol.h"
#include "../IoHomecontrolChannel.h"
#include "../IoHomecontrolHardware.h"
#include "knxprod.h"
#endif

#ifdef ESP32
#include <Arduino.h>
#else
#ifdef TEST_NATIVE
static unsigned long millis() { return ioHomeTestMillis(); }
static unsigned long micros() { return ioHomeTestMicros(); }
#else
static unsigned long millis() { return 0; }
static unsigned long micros() { return 0; }
#endif
static void delay(unsigned long) {}
#endif

namespace
{
    constexpr uint8_t kPair1WFreqIdx = 1;
    constexpr uint32_t kNormal2WTxFreqHz = IOHC_FREQ_2;
    constexpr uint8_t kUnknownIohcChannel = 0;

    uint8_t frequencyIndexForHz(uint32_t iFreqHz)
    {
        for (uint8_t i = 0; i < IOHC_NUM_FREQUENCIES; i++)
        {
            if (IOHC_FREQUENCIES[i] == iFreqHz)
                return i;
        }
        return 0;
    }

    uint8_t iohcChannelNumberForFrequency(uint32_t iFreqHz)
    {
        switch (iFreqHz)
        {
        case IOHC_FREQ_1:
            return 1;
        case IOHC_FREQ_2:
            return 2;
        case IOHC_FREQ_3:
            return 3;
        default:
            return kUnknownIohcChannel;
        }
    }

    constexpr uint8_t kGatewayNameLen = 16;
    constexpr char kGatewayName[] = "MY_GATEWAY";
    constexpr uint8_t kGatewayDiscoverInfo = 0xCC;
    constexpr uint8_t kGatewayInfoSubtype = 0x01;
    constexpr uint16_t kGatewayInfoDeviceType = 0x0002;
    constexpr uint8_t kGatewayDiscoverManufacturer = static_cast<uint8_t>(IoHomeManufacturer::Overkiz);
    constexpr uint8_t kGatewayInfoManufacturer = static_cast<uint8_t>(IoHomeManufacturer::Somfy);
    constexpr uint16_t kPositionRawTolerance = 100;
#if defined(RADIO_SX1262) || defined(TEST_NATIVE)
    constexpr bool kIsSX1262Radio = true;
#else
    constexpr bool kIsSX1262Radio = false;
#endif

    bool isTrackedStatusPollCommand(const IoHomeQueueEntry &iCmd)
    {
        return iCmd.active &&
               iCmd.command == IoHomeCommand::Private &&
               iCmd.param == 0x03;
    }

    void notifyTrackedStatusPollFailure(IoHomecontrol *iModule,
                                        const IoHomeQueueEntry &iCmd,
                                        bool iAfterChallenge)
    {
        if (!iModule || !isTrackedStatusPollCommand(iCmd))
            return;

        for (uint8_t i = 0; i < IOHC_ChannelCount; i++)
        {
            IoHomecontrolChannel *lCh = iModule->getChannel(i);
            if (lCh && lCh->getNodeId() == iCmd.destNodeId)
            {
                lCh->onStatusPollFailed(iAfterChallenge);
                break;
            }
        }
    }

    std::string hexDump(const uint8_t *iData, uint8_t iLen)
    {
        static const char kHex[] = "0123456789ABCDEF";
        std::string lOut;
        lOut.reserve(static_cast<size_t>(iLen) * 2);
        for (uint8_t i = 0; i < iLen; i++)
        {
            lOut.push_back(kHex[(iData[i] >> 4) & 0x0F]);
            lOut.push_back(kHex[iData[i] & 0x0F]);
        }
        return lOut;
    }

    const char *pairing1WCommandSequence(Pairing1WMode iMode)
    {
        switch (iMode)
        {
        case Pairing1WMode::AnnounceOnly:
            return "0x2E";
        case Pairing1WMode::AddOnly:
            return "0x30";
        case Pairing1WMode::Remove:
            return "0x39";
        case Pairing1WMode::AnnounceAdd:
        default:
            return "0x2E,0x30";
        }
    }

    bool build1WSendKey30(IoHomeFrame &oFrame,
                          const uint8_t iEncryptedKey[16],
                          uint8_t iManufacturer,
                          uint16_t iSequence)
    {
        if (!iEncryptedKey)
            return false;

        // Reference-compatible 0x30 Add/SendKey frame payload:
        // encryptedKey[16] + manufacturer + 0x01 + sequence[2].
        // This command is intentionally unauthenticated: no appended 1W HMAC.
        memcpy(oFrame.data, iEncryptedKey, 16);
        oFrame.data[16] = iManufacturer;
        oFrame.data[17] = 0x01;
        oFrame.data[18] = static_cast<uint8_t>((iSequence >> 8) & 0xFF);
        oFrame.data[19] = static_cast<uint8_t>(iSequence & 0xFF);
        oFrame.dataLen = 20;
        oFrame.hasHmac = false;
        return true;
    }

    bool oneWayMainToRawClosedPercent(uint16_t iMain, uint8_t &oRawClosedPercent)
    {
        if (iMain > IOHC_POSITION_MAX)
            return false;

        // Standard 1W Execute main value is raw IOHC closedness percent × 2
        // in the high byte: 0x0000=open, 0x6400=50%, 0xC800=closed.
        const uint8_t lHigh = static_cast<uint8_t>((iMain >> 8) & 0xFF);
        const uint8_t lLow = static_cast<uint8_t>(iMain & 0xFF);
        if (lLow != 0 || (lHigh & 0x01) != 0)
            return false;

        oRawClosedPercent = static_cast<uint8_t>(lHigh / 2U);
        return oRawClosedPercent <= 100;
    }

    struct OneWayCommandProfile
    {
        uint8_t acei;
        uint8_t fp1;
        uint8_t fp2;
        uint8_t destinationType;
        bool rawPreferred;
    };

    OneWayCommandProfile oneWayCommandProfileForType(uint8_t iBroadcastType)
    {
        const uint8_t lType = iBroadcastType & 0x3F;
        switch (lType)
        {
        case 2: // shutter/blind default
            return {IOHC_ACEI_1W, 0x00, 0x00, 2, false};
        case 3: // awning default
            return {IOHC_ACEI_1W, 0x00, 0x00, 3, false};
        default: // light/unknown: keep typed destination, raw path remains available for deviations
            return {IOHC_ACEI_1W, 0x00, 0x00, lType, true};
        }
    }

    bool build1WExecute14(IoHomeFrame &oFrame, const OneWayCommandProfile &iProfile,
                          uint16_t iMain, uint8_t iFp1, uint8_t iFp2, uint16_t iSequence)
    {
        // Reference _p0x00_14 wire shape before HMAC:
        // origin + acei + main[2] + fp1 + fp2 + sequence[2].
        oFrame.data[0] = IOHC_ORIGINATOR_USER;
        oFrame.data[1] = iProfile.acei;
        oFrame.data[2] = static_cast<uint8_t>((iMain >> 8) & 0xFF);
        oFrame.data[3] = static_cast<uint8_t>(iMain & 0xFF);
        oFrame.data[4] = iFp1;
        oFrame.data[5] = iFp2;
        oFrame.data[6] = static_cast<uint8_t>((iSequence >> 8) & 0xFF);
        oFrame.data[7] = static_cast<uint8_t>(iSequence & 0xFF);
        oFrame.dataLen = 8;
        oFrame.hasHmac = true;
        return true;
    }

    bool build1WExecute(IoHomeFrame &oFrame, const OneWayCommandProfile &iProfile,
                        uint16_t iMain, uint8_t iFp1, uint8_t iFp2, uint16_t iSequence)
    {
        return build1WExecute14(oFrame, iProfile, iMain, iFp1, iFp2, iSequence);
    }

    bool build1WExecute16(IoHomeFrame &oFrame, const OneWayCommandProfile &iProfile,
                          uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                          uint8_t iData0, uint8_t iData1, uint16_t iSequence)
    {
        // Reference/diagnostic extended _p0x00_16 wire shape before HMAC:
        // origin + acei + main[2] + fp1 + fp2 + data[2] + sequence[2].
        oFrame.data[0] = IOHC_ORIGINATOR_USER;
        oFrame.data[1] = iProfile.acei;
        oFrame.data[2] = static_cast<uint8_t>((iMain >> 8) & 0xFF);
        oFrame.data[3] = static_cast<uint8_t>(iMain & 0xFF);
        oFrame.data[4] = iFp1;
        oFrame.data[5] = iFp2;
        oFrame.data[6] = iData0;
        oFrame.data[7] = iData1;
        oFrame.data[8] = static_cast<uint8_t>((iSequence >> 8) & 0xFF);
        oFrame.data[9] = static_cast<uint8_t>(iSequence & 0xFF);
        oFrame.dataLen = 10;
        oFrame.hasHmac = true;
        return true;
    }

    bool build1WActivateMode13(IoHomeFrame &oFrame, const OneWayCommandProfile &iProfile,
                               uint8_t iMain, uint8_t iFp1, uint8_t iFp2, uint16_t iSequence)
    {
        // Reference _p0x01_13 wire shape before HMAC:
        // origin + acei + main[1] + fp1 + fp2 + sequence[2].
        oFrame.data[0] = IOHC_ORIGINATOR_USER;
        oFrame.data[1] = iProfile.acei;
        oFrame.data[2] = iMain;
        oFrame.data[3] = iFp1;
        oFrame.data[4] = iFp2;
        oFrame.data[5] = static_cast<uint8_t>((iSequence >> 8) & 0xFF);
        oFrame.data[6] = static_cast<uint8_t>(iSequence & 0xFF);
        oFrame.dataLen = 7;
        oFrame.hasHmac = true;
        return true;
    }

    bool build1WPair2E(IoHomeFrame &oFrame, uint16_t iSequence)
    {
        oFrame.commandId = IoHomeCommand::Discover2ERequest;
        oFrame.data[0] = 0x00;
        oFrame.data[1] = static_cast<uint8_t>((iSequence >> 8) & 0xFF);
        oFrame.data[2] = static_cast<uint8_t>(iSequence & 0xFF);
        oFrame.dataLen = 3;
        oFrame.hasHmac = true;
        return true;
    }

    bool build1WRemove39(IoHomeFrame &oFrame, uint16_t iSequence)
    {
        oFrame.commandId = IoHomeCommand::RemoveController;
        oFrame.data[0] = 0x00;
        oFrame.data[1] = static_cast<uint8_t>((iSequence >> 8) & 0xFF);
        oFrame.data[2] = static_cast<uint8_t>(iSequence & 0xFF);
        oFrame.dataLen = 3;
        oFrame.hasHmac = true;
        return true;
    }

    bool build1WPairAuth(IoHomeFrame &oFrame, uint16_t iSequence)
    {
        return build1WPair2E(oFrame, iSequence);
    }

    bool build1WRemove(IoHomeFrame &oFrame, uint16_t iSequence)
    {
        return build1WRemove39(oFrame, iSequence);
    }

    bool build1WSendKey(IoHomeFrame &oFrame, const uint8_t iEncryptedKey[16],
                        uint8_t iManufacturer, uint16_t iSequence)
    {
        return build1WSendKey30(oFrame, iEncryptedKey, iManufacturer, iSequence);
    }

    const char *pairingModeName(Pairing2WMode iMode, ControllerState iState)
    {
        switch (iState)
        {
        case ControllerState::PairSend1WAnnounce:
        case ControllerState::PairWait1WAnnounce:
        case ControllerState::PairSend1WRemove:
        case ControllerState::PairWait1WRemove:
        case ControllerState::PairSend1WKeyTransfer:
        case ControllerState::PairWait1WKeyTransfer:
            return "1w";
        default:
            break;
        }

        switch (iMode)
        {
        case Pairing2WMode::Normal:
            return "normal";
        case Pairing2WMode::DiscoveryConfirmation:
            return "discovery-confirmation";
        case Pairing2WMode::LaunchKeyTransfer:
            return "launch-key-transfer";
        case Pairing2WMode::PullKey:
            return "pull-key";
        default:
            return "unknown";
        }
    }

    bool isPairDiagnosticCommand(IoHomeCommand iCommand)
    {
        switch (iCommand)
        {
        case IoHomeCommand::DiscoverRequest:
        case IoHomeCommand::DiscoverResponse:
        case IoHomeCommand::DiscoverSPERequest:
        case IoHomeCommand::DiscoverSPEResponse:
        case IoHomeCommand::Discover2ERequest:
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

    bool rawPositionToPercent(uint16_t iRaw, float &oPercent)
    {
        if (iRaw > IOHC_POSITION_MAX)
            return false;

        oPercent = (float)iRaw * 100.0f / IOHC_POSITION_MAX;
        if (oPercent > 100.0f)
            oPercent = 100.0f;
        return true;
    }

    bool rawPositionNear(uint16_t iA, uint16_t iB)
    {
        return (iA > iB) ? ((iA - iB) <= kPositionRawTolerance) : ((iB - iA) <= kPositionRawTolerance);
    }

    uint32_t passivePairNodeFromFrame(const IoHomeFrame &iFrame)
    {
        const uint32_t lDestNode = iFrame.getDestNodeId();
        if (getAddressClass(lDestNode) == IoHomeAddressClass::Unicast)
            return lDestNode;
        return iFrame.getSrcNodeId();
    }

    uint16_t readU16BE(const uint8_t *iData, uint8_t iOffset)
    {
        return ((uint16_t)iData[iOffset] << 8) | iData[iOffset + 1];
    }

    void dispatchPositionStatus(IoHomecontrolChannel *iChannel,
                                const uint8_t *iData,
                                uint8_t iDataLen,
                                bool iStopped,
                                uint8_t iTargetOffset,
                                uint8_t iCurrentOffset)
    {
        if (!iChannel || !iData ||
            iDataLen < iTargetOffset + 2 ||
            iDataLen < iCurrentOffset + 2)
        {
            return;
        }

        const uint16_t lTargetRaw = readU16BE(iData, iTargetOffset);
        const uint16_t lCurrentRaw = readU16BE(iData, iCurrentOffset);

        float lCurrentPercent = 0.0f;
        bool lHasCurrentPosition = rawPositionToPercent(lCurrentRaw, lCurrentPercent);

        float lTargetPercent = 0.0f;
        bool lHasTargetPosition = rawPositionToPercent(lTargetRaw, lTargetPercent);
        if (!lHasTargetPosition && iStopped && lHasCurrentPosition)
        {
            lTargetPercent = lCurrentPercent;
            lHasTargetPosition = true;
        }
        if (!lHasCurrentPosition && iStopped && lHasTargetPosition)
        {
            lCurrentPercent = lTargetPercent;
            lHasCurrentPosition = true;
        }

        bool lMoving = !iStopped;
        if (lMoving && lHasCurrentPosition && lHasTargetPosition &&
            lTargetRaw <= IOHC_POSITION_MAX &&
            rawPositionNear(lCurrentRaw, lTargetRaw))
        {
            lMoving = false;
        }

        if (lHasTargetPosition)
            iChannel->onTargetPositionFeedback(lTargetPercent);
        if (lHasCurrentPosition)
            iChannel->onPositionFeedback(lCurrentPercent);

        iChannel->onStatusUpdate(lMoving);
        iChannel->logStatusSummary(lCurrentPercent, lHasCurrentPosition,
                                   lTargetPercent, lHasTargetPosition,
                                   lMoving);
    }

    void applyPrivateBatteryInfo(IoHomecontrolChannel *iChannel, const uint8_t *iData, uint8_t iDataLen)
    {
        if (!iChannel || !iData || iDataLen < 2)
            return;

        if (iDataLen >= 6 && iData[1] != 0x60)
            return;

        if (iData[1] == 0x60)
            iChannel->setLowPower2W(true);
        else if (iData[1] == 0x00)
            iChannel->setLowPower2W(false);

        if (iDataLen >= 3 && iData[2] <= 100)
            iChannel->onBatteryLevel(iData[2]);
        else if (iDataLen >= 4 && iData[3] <= 100)
            iChannel->onBatteryLevel(iData[3]);
    }

    void applyPrivateTiltInfo(IoHomecontrolChannel *iChannel, const uint8_t *iData, uint8_t iDataLen)
    {
        if (!iChannel || !iData || iDataLen < 15)
            return;

        const uint16_t lTiltRaw = readU16BE(iData, 13);
        if (lTiltRaw > IOHC_POSITION_MAX)
            return;

        float lTiltPercent = 100.0f - ((float)lTiltRaw * 100.0f / IOHC_POSITION_MAX);
        if (lTiltPercent < 0.0f)
            lTiltPercent = 0.0f;
        if (lTiltPercent > 100.0f)
            lTiltPercent = 100.0f;
        iChannel->onSlatFeedback(lTiltPercent);
    }

    void applyGeneralInfo2TiltInfo(IoHomecontrolChannel *iChannel, const uint8_t *iData, uint8_t iDataLen)
    {
        if (!iChannel || !iData || iDataLen < 15)
            return;

        if (iData[12] == 0x00)
            return;

        const uint16_t lTiltRaw = readU16BE(iData, 13);
        if (lTiltRaw > IOHC_POSITION_MAX)
            return;

        float lTiltPercent = 100.0f - ((float)lTiltRaw * 100.0f / IOHC_POSITION_MAX);
        if (lTiltPercent < 0.0f)
            lTiltPercent = 0.0f;
        if (lTiltPercent > 100.0f)
            lTiltPercent = 100.0f;
        iChannel->onSlatFeedback(lTiltPercent);
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
        return IoHomeCrypto::crypt2WKeyXor(lKeyInitData, sizeof(lKeyInitData),
                                           iDeviceChallenge, iGatewayKey,
                                           IOHC_TRANSFER_KEY, oEncryptedKey);
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
        return lFrame.serialize2W(oBuffer, iBufferLen);
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
        return lFrame.serialize2W(oBuffer, iBufferLen);
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
        return lFrame.serialize2W(oBuffer, iBufferLen);
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
        lFrame.setLowPower(true);
        memcpy(lFrame.data, lHmac, sizeof(lHmac));
        lFrame.dataLen = sizeof(lHmac);
        return lFrame.serialize2W(oBuffer, iBufferLen);
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
        return lFrame.serialize2W(oBuffer, iBufferLen);
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
        return lFrame.serialize2W(oBuffer, iBufferLen);
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
        return lFrame.serialize2W(oBuffer, iBufferLen);
    }

    bool copy2WPayload(uint8_t *oData, uint8_t &oLen, const uint8_t *iTemplate, uint8_t iTemplateLen)
    {
        if (oData == nullptr || iTemplate == nullptr || iTemplateLen > IOHC_FRAME_MAX_DATA)
            return false;

        memcpy(oData, iTemplate, iTemplateLen);
        oLen = iTemplateLen;
        return true;
    }

    bool build2WExecutePositionPayload(uint8_t iPositionPercent, uint8_t *oData, uint8_t &oLen)
    {
        if (oData == nullptr || iPositionPercent > 100)
            return false;

        // Reference template: 01 67 <pos*2> 00 80 D8 06 00
        oData[0] = IOHC_ORIGINATOR_USER;
        oData[1] = IOHC_ACEI_DEFAULT;
        oData[2] = static_cast<uint8_t>(iPositionPercent * 2U);
        oData[3] = 0x00;
        oData[4] = 0x80;
        oData[5] = 0xD8;
        oData[6] = 0x06;
        oData[7] = 0x00;
        oLen = 8;
        return true;
    }

    bool build2WExecuteSpecialPayload(uint8_t iSpecialPosition, uint8_t *oData, uint8_t &oLen)
    {
        if (oData == nullptr)
            return false;

        // Reference template: 01 67 <D2/D8> 00 00 00
        oData[0] = IOHC_ORIGINATOR_USER;
        oData[1] = IOHC_ACEI_DEFAULT;
        oData[2] = iSpecialPosition;
        oData[3] = 0x00;
        oData[4] = 0x00;
        oData[5] = 0x00;
        oLen = 6;
        return true;
    }

    bool build2WExecuteTiltPayload(uint8_t iTiltPercent, uint8_t *oData, uint8_t &oLen)
    {
        if (oData == nullptr || iTiltPercent > 100)
            return false;

        const uint16_t lTiltRaw = static_cast<uint16_t>(
            (static_cast<uint32_t>(100U - iTiltPercent) * IOHC_POSITION_MAX) / 100U);

        // Reference template: 01 E7 D4 00 20 <tiltRawHi> <tiltRawLo> 00
        oData[0] = IOHC_ORIGINATOR_USER;
        oData[1] = 0xE7;
        oData[2] = 0xD4;
        oData[3] = 0x00;
        oData[4] = 0x20;
        oData[5] = static_cast<uint8_t>((lTiltRaw >> 8) & 0xFF);
        oData[6] = static_cast<uint8_t>(lTiltRaw & 0xFF);
        oData[7] = 0x00;
        oLen = 8;
        return true;
    }

    bool build2WPrivatePayload(uint8_t iParam, uint8_t iParam2, uint8_t iParam3,
                               uint8_t *oData, uint8_t &oLen)
    {
        static constexpr uint8_t kPrivateStatusPayload[] = {0x03, 0x00, 0x00};
        static constexpr uint8_t kPrivateTiltStatusPayload[] = {0x03, 0x20, 0x01, 0x00};

        if (iParam == 0x03 && iParam2 == 0xFF && iParam3 == 0xFF)
            return copy2WPayload(oData, oLen, kPrivateStatusPayload, static_cast<uint8_t>(sizeof(kPrivateStatusPayload)));

        if (iParam == 0x03 && iParam2 == 0x20 && iParam3 == 0x01)
            return copy2WPayload(oData, oLen, kPrivateTiltStatusPayload, static_cast<uint8_t>(sizeof(kPrivateTiltStatusPayload)));

        if (oData == nullptr)
            return false;

        // Preserve non-reference diagnostic/battery Private variants.
        oData[0] = iParam;
        oData[1] = (iParam2 != 0xFF) ? iParam2 : 0x00;
        oData[2] = (iParam3 != 0xFF) ? iParam3 : 0x00;
        oLen = (iParam2 != 0xFF || iParam3 != 0xFF) ? 4 : 3;
        if (oLen == 4)
            oData[3] = 0x00;
        return true;
    }

    bool build2WSetConfig1Payload(uint8_t *oData, uint8_t &oLen)
    {
        static constexpr uint8_t kSetConfig1Payload[] = {0xE0, 0x10, 0x0A, 0x08, 0x00};
        return copy2WPayload(oData, oLen, kSetConfig1Payload, static_cast<uint8_t>(sizeof(kSetConfig1Payload)));
    }

    bool build2WDiscover(IoHomeFrame &oFrame, uint32_t iSrcNodeId, bool iSpeDiscovery, const uint8_t iSystemKey[16])
    {
        oFrame.init();
        oFrame.setStart2W();
        oFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);
        oFrame.setSrcNode(iSrcNodeId);
        oFrame.setDestBroadcast();
        oFrame.hasHmac = false;

        if (!iSpeDiscovery)
        {
            oFrame.commandId = IoHomeCommand::DiscoverRequest;
            oFrame.dataLen = 0;
            return true;
        }

        if (iSystemKey == nullptr)
            return false;

        oFrame.commandId = IoHomeCommand::DiscoverSPERequest;
        uint8_t lChallenge[6];
        IoHomeCrypto::generateChallenge(lChallenge);
        memcpy(oFrame.data, lChallenge, sizeof(lChallenge));

        uint8_t lHmac[IOHC_HMAC_SIZE];
        if (!IoHomeCrypto::createHmac2W(lChallenge, sizeof(lChallenge),
                                        lChallenge, iSystemKey, lHmac))
            return false;

        memcpy(oFrame.data + sizeof(lChallenge), lHmac, sizeof(lHmac));
        oFrame.dataLen = sizeof(lChallenge) + sizeof(lHmac);
        return true;
    }

    bool build2WKeyInit(IoHomeFrame &oFrame, uint32_t iSrcNodeId, uint32_t iDestNodeId)
    {
        oFrame.init();
        oFrame.setStart2W();
        oFrame.setLowPower(true);
        oFrame.setSrcNode(iSrcNodeId);
        oFrame.setDestNode(iDestNodeId);
        oFrame.commandId = IoHomeCommand::KeyInitTransfer;
        oFrame.dataLen = 0;
        oFrame.hasHmac = false;
        return true;
    }

    bool build2WKeyTransfer(IoHomeFrame &oFrame, uint32_t iSrcNodeId, uint32_t iDestNodeId,
                            const uint8_t iPairingChallenge[6], const uint8_t iSystemKey[16])
    {
        if (iPairingChallenge == nullptr || iSystemKey == nullptr)
            return false;

        uint8_t lEncryptedKey[16];
        const uint8_t lKeyInitTranscript[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
        if (!IoHomeCrypto::crypt2WKeyXor(lKeyInitTranscript, sizeof(lKeyInitTranscript),
                                         iPairingChallenge, iSystemKey,
                                         IOHC_TRANSFER_KEY, lEncryptedKey))
            return false;

        oFrame.init();
        // 0x32 is a continuation frame. Keep START/END and LOW_POWER clear.
        oFrame.ctrlByte0 = 0;
        oFrame.ctrlByte1 = 0x00;
        oFrame.setSrcNode(iSrcNodeId);
        oFrame.setDestNode(iDestNodeId);
        oFrame.commandId = IoHomeCommand::KeyTransfer;
        memcpy(oFrame.data, lEncryptedKey, sizeof(lEncryptedKey));
        oFrame.dataLen = sizeof(lEncryptedKey);
        oFrame.hasHmac = false;
        return true;
    }

    bool build2WChallengeResponse(IoHomeFrame &oFrame, uint32_t iSrcNodeId, uint32_t iDestNodeId,
                                  bool iLowPower, const IoHomeFrame &iAuthenticatedRequest,
                                  const uint8_t iChallenge[6], const uint8_t iKey[16])
    {
        if (iChallenge == nullptr || iKey == nullptr)
            return false;

        uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA];
        const size_t lHmacInputLen = buildHmacInput(iAuthenticatedRequest, lHmacInput, sizeof(lHmacInput));
        if (lHmacInputLen == 0)
            return false;

        oFrame.init();
        // 0x3D carries the 6-byte HMAC as normal data in a 2W continuation frame.
        oFrame.ctrlByte0 = 0;
        oFrame.ctrlByte1 = 0x00;
        oFrame.setLowPower(iLowPower);
        oFrame.setSrcNode(iSrcNodeId);
        oFrame.setDestNode(iDestNodeId);
        oFrame.commandId = IoHomeCommand::ChallengeResponse;
        if (!IoHomeCrypto::createHmac2W(lHmacInput, lHmacInputLen, iChallenge, iKey, oFrame.data))
            return false;
        oFrame.dataLen = IOHC_HMAC_SIZE;
        oFrame.hasHmac = false;
        return true;
    }

    bool build2WSetConfig1(IoHomeFrame &oFrame, uint32_t iSrcNodeId, uint32_t iDestNodeId)
    {
        oFrame.init();
        oFrame.setStart2W();
        oFrame.setLowPower(false);
        oFrame.setSrcNode(iSrcNodeId);
        oFrame.setDestNode(iDestNodeId);
        oFrame.commandId = IoHomeCommand::SetConfig1;
        if (!build2WSetConfig1Payload(oFrame.data, oFrame.dataLen))
            return false;
        oFrame.hasHmac = false;
        return true;
    }

#if defined(TEST_NATIVE)
    bool payloadEquals(const uint8_t *iActual, size_t iActualLen,
                       const uint8_t *iExpected, size_t iExpectedLen)
    {
        return iActualLen == iExpectedLen && memcmp(iActual, iExpected, iExpectedLen) == 0;
    }

    bool run2WPayloadTemplateSelfTest()
    {
        uint8_t lPayload[IOHC_FRAME_MAX_DATA] = {};
        uint8_t lLen = 0;

        static constexpr uint8_t kExecutePosition50[] = {0x01, 0x67, 0x64, 0x00, 0x80, 0xD8, 0x06, 0x00};
        if (!build2WExecutePositionPayload(50, lPayload, lLen) ||
            !payloadEquals(lPayload, lLen, kExecutePosition50, sizeof(kExecutePosition50)))
            return false;

        static constexpr uint8_t kExecuteStop[] = {0x01, 0x67, 0xD2, 0x00, 0x00, 0x00};
        if (!build2WExecuteSpecialPayload(0xD2, lPayload, lLen) ||
            !payloadEquals(lPayload, lLen, kExecuteStop, sizeof(kExecuteStop)))
            return false;

        static constexpr uint8_t kExecuteFavorite[] = {0x01, 0x67, 0xD8, 0x00, 0x00, 0x00};
        if (!build2WExecuteSpecialPayload(0xD8, lPayload, lLen) ||
            !payloadEquals(lPayload, lLen, kExecuteFavorite, sizeof(kExecuteFavorite)))
            return false;

        static constexpr uint8_t kExecuteTilt50[] = {0x01, 0xE7, 0xD4, 0x00, 0x20, 0x64, 0x00, 0x00};
        if (!build2WExecuteTiltPayload(50, lPayload, lLen) ||
            !payloadEquals(lPayload, lLen, kExecuteTilt50, sizeof(kExecuteTilt50)))
            return false;

        static constexpr uint8_t kPrivateStatus[] = {0x03, 0x00, 0x00};
        if (!build2WPrivatePayload(0x03, 0xFF, 0xFF, lPayload, lLen) ||
            !payloadEquals(lPayload, lLen, kPrivateStatus, sizeof(kPrivateStatus)))
            return false;

        static constexpr uint8_t kPrivateTiltStatus[] = {0x03, 0x20, 0x01, 0x00};
        if (!build2WPrivatePayload(0x03, 0x20, 0x01, lPayload, lLen) ||
            !payloadEquals(lPayload, lLen, kPrivateTiltStatus, sizeof(kPrivateTiltStatus)))
            return false;

        static constexpr uint8_t kSetConfig1[] = {0xE0, 0x10, 0x0A, 0x08, 0x00};
        if (!build2WSetConfig1Payload(lPayload, lLen) ||
            !payloadEquals(lPayload, lLen, kSetConfig1, sizeof(kSetConfig1)))
            return false;

        return true;
    }
#endif
}

IoHomeController::IoHomeController()
    : mModule(nullptr), mOwnNodeId(0),
      mState(ControllerState::Idle), mStateTimer(0),
      mCurrentFreqIdx(0), mQueueHead(0), mQueueTail(0),
      mPairingChannel(0), mDiscoveredNodeId(0),
      mPairingFreqIdx(frequencyIndexForHz(kNormal2WTxFreqHz)), mPairingStartTime(0),
      mDiscoverySendPhase(DiscoverySendPhase::SetFrequency),
      mDiscoveryTimingTrace{},
      mDiscoverySPE(false),
      mPairing1WStage(0),
      mRequestedPairing1WMode(Pairing1WMode::AnnounceAdd),
      mPairing1WMode(Pairing1WMode::AnnounceAdd),
      mPairing1WBroadcastType(0), mDefault1WBroadcastType(0),
      mAuthSrcNodeId(0), mAuthChannelIdx(0),
      mStatusAckDestNodeId(0), mStatusAckFreqIdx(0),
      mPassiveMode(false), mPassivePairNodeId(0),
      mPassiveChallengeValid(false),
      mPassiveKeySniffStatus(PassiveKeySniffStatus::Idle),
      mPassiveKeyResult{},
      mPassiveKeySniffStartedAt(0),
      mPassiveKeySniffTimeoutMs(0),
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
#if defined(TEST_NATIVE)
    const bool lPayloadTemplateSelfTestOk = run2WPayloadTemplateSelfTest();
    assert(lPayloadTemplateSelfTestOk);
    (void)lPayloadTemplateSelfTestOk;
#endif

    memset(mSystemKey, 0, sizeof(mSystemKey));
    mRxParseFailCount = 0;
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
    memset(&mPassiveKeyResult, 0, sizeof(mPassiveKeyResult));
    memset(mGatewayKey, 0, sizeof(mGatewayKey));
    clearGatewayPairedDevices();
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
    mWaitingFinalResponse = false;
    mSawChallenge = false;
    mResponseTimeoutMs = IOHC_RX_TIMEOUT_MS;
    mRetryAtMs = 0;
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

void IoHomeController::setOneWayBroadcastType(uint8_t iBroadcastType)
{
    mDefault1WBroadcastType = iBroadcastType & 0x3F;
}

uint8_t IoHomeController::getOneWayBroadcastType() const
{
    return mDefault1WBroadcastType;
}

uint32_t IoHomeController::oneWayBroadcastTarget(uint8_t iBroadcastType) const
{
    return static_cast<uint32_t>(((static_cast<uint16_t>(iBroadcastType & 0x3F) << 6) | 0x003F) & 0x00FFFF);
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

uint8_t IoHomeController::uiOpenPercentToRawClosedPercent(uint8_t iUiOpenPercent)
{
    if (iUiOpenPercent > 100)
        iUiOpenPercent = 100;
    return static_cast<uint8_t>(100U - iUiOpenPercent);
}

uint16_t IoHomeController::rawClosedPercentToOneWayMain(uint8_t iRawClosedPercent)
{
    if (iRawClosedPercent > 100)
        iRawClosedPercent = 100;
    return static_cast<uint16_t>(static_cast<uint16_t>(iRawClosedPercent) * 2U) << 8;
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
    lEntry.oneWayButton = false;
    lEntry.oneWayButtonCode = 0;
    lEntry.oneWayRawExecute = false;
    lEntry.oneWayRawLen = 0;
    lEntry.oneWayBroadcastType = oneWayBroadcastTypeForNode(iDestNodeId);
    const OneWayCommandProfile lOneWayProfile = oneWayCommandProfileForType(lEntry.oneWayBroadcastType);
    lEntry.oneWayAcei = lOneWayProfile.acei;
    lEntry.oneWayFp1 = lOneWayProfile.fp1;
    lEntry.oneWayFp2 = lOneWayProfile.fp2;
    if (iCmd == IoHomeCommand::Execute && iParam3 == 0xFF)
    {
        // Use the standard 14-byte 1W Execute layout by default.
        // The low-level Execute parameter is raw IOHC closedness percent:
        //   rawClosed=0 -> main=0000 (open)
        //   rawClosed=100 -> main=C800 (closed)
        // If a caller has UI/Home-Assistant-style open percentage, convert at
        // that boundary via uiOpenPercentToRawClosedPercent() before queuing.
        // The flag is ignored for 2W channels.
        lEntry.oneWayStandardExecute = true;
        if (iParam <= 100)
        {
            const uint8_t lRawClosedPercent = iParam;
            lEntry.oneWayMain = rawClosedPercentToOneWayMain(lRawClosedPercent);
            if (iParam2 != 0xFF)
            {
                lEntry.oneWayFp1 = 0x80;
                lEntry.oneWayFp2 = iParam2 * 2U;
            }
        }
        else if (iParam == 0xD8 && iParam2 == 0x03)
        {
            lEntry.oneWayMain = IOHC_POSITION_VENT;
        }
        else
        {
            lEntry.oneWayMain = static_cast<uint16_t>(iParam) << 8;
        }
    }
    lEntry.oneWayBroadcastTypeExplicit = false;
    lEntry.oneWayDestinationMode = OneWayDestinationMode::ProfileTyped;
    lEntry.oneWayExactDestination = 0;
    lEntry.sourceChannelIndex = 0xFF;
    lEntry.retries = 0;
    lEntry.active = true;
    return queuePush(lEntry);
}

bool IoHomeController::sendChannelCommand(IoHomecontrolChannel *iChannel,
                                          IoHomeCommand iCmd, uint8_t iParam,
                                          uint8_t iParam2, uint8_t iParam3)
{
    if (!iChannel || !iChannel->is1W())
        return false;
    IoHomecontrolChannel *lProfileChannel = oneWayProfileForChannel(iChannel);
    if (!lProfileChannel || !lProfileChannel->hasOneWayControllerIdentity())
        return false;

    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iChannel->getNodeId(); // may be 0 for virtual/broadcast-only profiles
    lEntry.encKey = iChannel->getEncryptionKey();
    lEntry.command = iCmd;
    lEntry.param = iParam;
    lEntry.param2 = iParam2;
    lEntry.param3 = iParam3;
    lEntry.oneWayButton = false;
    lEntry.oneWayButtonCode = 0;
    lEntry.oneWayRawExecute = false;
    lEntry.oneWayRawLen = 0;
    lEntry.oneWayBroadcastType = iChannel->getConfigured1WBroadcastType();
    const OneWayCommandProfile lOneWayProfile = oneWayCommandProfileForType(lEntry.oneWayBroadcastType);
    lEntry.oneWayAcei = lOneWayProfile.acei;
    lEntry.oneWayFp1 = lOneWayProfile.fp1;
    lEntry.oneWayFp2 = lOneWayProfile.fp2;
    if (iCmd == IoHomeCommand::Execute && iParam3 == 0xFF)
    {
        lEntry.oneWayStandardExecute = true;
        if (iParam <= 100)
        {
            const uint8_t lRawClosedPercent = iParam;
            lEntry.oneWayMain = rawClosedPercentToOneWayMain(lRawClosedPercent);
            if (iParam2 != 0xFF)
            {
                lEntry.oneWayFp1 = 0x80;
                lEntry.oneWayFp2 = iParam2 * 2U;
            }
        }
        else if (iParam == 0xD8 && iParam2 == 0x03)
        {
            lEntry.oneWayMain = IOHC_POSITION_VENT;
        }
        else
        {
            lEntry.oneWayMain = static_cast<uint16_t>(iParam) << 8;
        }
    }
    lEntry.oneWayBroadcastTypeExplicit = false;
    lEntry.oneWayDestinationMode = OneWayDestinationMode::ProfileTyped;
    lEntry.oneWayExactDestination = 0;
    lEntry.sourceChannelIndex = channelIndexFor(iChannel);
    lEntry.retries = 0;
    lEntry.active = true;
    return queuePush(lEntry);
}

bool IoHomeController::sendOneWayButton(uint32_t iDestNodeId, const uint8_t *iEncKey, uint16_t iButtonCode)
{
    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iDestNodeId;
    lEntry.encKey = iEncKey;
    lEntry.command = IoHomeCommand::Execute;
    lEntry.param = 0xFF;
    lEntry.param2 = 0xFF;
    lEntry.param3 = 0xFF;
    lEntry.oneWayButton = true;
    lEntry.oneWayButtonCode = iButtonCode;
    lEntry.oneWayRawExecute = false;
    lEntry.oneWayRawLen = 0;
    lEntry.oneWayBroadcastType = oneWayBroadcastTypeForNode(iDestNodeId);
    const OneWayCommandProfile lOneWayProfile = oneWayCommandProfileForType(lEntry.oneWayBroadcastType);
    lEntry.oneWayAcei = lOneWayProfile.acei;
    lEntry.oneWayFp1 = lOneWayProfile.fp1;
    lEntry.oneWayFp2 = lOneWayProfile.fp2;
    lEntry.oneWayBroadcastTypeExplicit = false;
    lEntry.oneWayDestinationMode = OneWayDestinationMode::ProfileTyped;
    lEntry.oneWayExactDestination = 0;
    lEntry.sourceChannelIndex = 0xFF;
    lEntry.retries = 0;
    lEntry.active = true;
    return queuePush(lEntry);
}

bool IoHomeController::sendOneWayChannelButton(IoHomecontrolChannel *iChannel, uint16_t iButtonCode)
{
    if (!iChannel || !iChannel->is1W())
        return false;
    IoHomecontrolChannel *lProfileChannel = oneWayProfileForChannel(iChannel);
    if (!lProfileChannel || !lProfileChannel->hasOneWayControllerIdentity())
        return false;

    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iChannel->getNodeId();
    lEntry.encKey = iChannel->getEncryptionKey();
    lEntry.command = IoHomeCommand::Execute;
    lEntry.param = 0xFF;
    lEntry.param2 = 0xFF;
    lEntry.param3 = 0xFF;
    lEntry.oneWayButton = true;
    lEntry.oneWayButtonCode = iButtonCode;
    lEntry.oneWayRawExecute = false;
    lEntry.oneWayRawLen = 0;
    lEntry.oneWayBroadcastType = iChannel->getConfigured1WBroadcastType();
    const OneWayCommandProfile lOneWayProfile = oneWayCommandProfileForType(lEntry.oneWayBroadcastType);
    lEntry.oneWayAcei = lOneWayProfile.acei;
    lEntry.oneWayFp1 = lOneWayProfile.fp1;
    lEntry.oneWayFp2 = lOneWayProfile.fp2;
    lEntry.oneWayBroadcastTypeExplicit = false;
    lEntry.oneWayDestinationMode = OneWayDestinationMode::ProfileTyped;
    lEntry.oneWayExactDestination = 0;
    lEntry.sourceChannelIndex = channelIndexFor(iChannel);
    lEntry.retries = 0;
    lEntry.active = true;
    return queuePush(lEntry);
}

bool IoHomeController::sendOneWayRawExecute(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                            const uint8_t *iPayload, uint8_t iPayloadLen)
{
    if (iPayload == nullptr || iPayloadLen == 0 || iPayloadLen > IOHC_1W_RAW_EXEC_MAX_DATA)
        return false;

    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iDestNodeId;
    lEntry.encKey = iEncKey;
    lEntry.command = IoHomeCommand::Execute;
    lEntry.param = 0xFF;
    lEntry.param2 = 0xFF;
    lEntry.param3 = 0xFF;
    lEntry.oneWayButton = false;
    lEntry.oneWayButtonCode = 0;
    lEntry.oneWayRawExecute = true;
    lEntry.oneWayRawLen = iPayloadLen;
    lEntry.oneWayBroadcastType = oneWayBroadcastTypeForNode(iDestNodeId);
    const OneWayCommandProfile lOneWayProfile = oneWayCommandProfileForType(lEntry.oneWayBroadcastType);
    lEntry.oneWayAcei = lOneWayProfile.acei;
    lEntry.oneWayFp1 = lOneWayProfile.fp1;
    lEntry.oneWayFp2 = lOneWayProfile.fp2;
    lEntry.oneWayBroadcastTypeExplicit = false;
    lEntry.oneWayDestinationMode = OneWayDestinationMode::ProfileTyped;
    lEntry.oneWayExactDestination = 0;
    lEntry.sourceChannelIndex = 0xFF;
    memcpy(lEntry.oneWayRawData, iPayload, iPayloadLen);
    lEntry.retries = 0;
    lEntry.active = true;
    return queuePush(lEntry);
}

bool IoHomeController::sendOneWayChannelRawExecute(IoHomecontrolChannel *iChannel,
                                                   const uint8_t *iPayload, uint8_t iPayloadLen)
{
    if (!iChannel || !iChannel->is1W())
        return false;
    IoHomecontrolChannel *lProfileChannel = oneWayProfileForChannel(iChannel);
    if (!lProfileChannel || !lProfileChannel->hasOneWayControllerIdentity())
        return false;
    if (iPayload == nullptr || iPayloadLen == 0 || iPayloadLen > IOHC_1W_RAW_EXEC_MAX_DATA)
        return false;

    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iChannel->getNodeId();
    lEntry.encKey = iChannel->getEncryptionKey();
    lEntry.command = IoHomeCommand::Execute;
    lEntry.param = 0xFF;
    lEntry.param2 = 0xFF;
    lEntry.param3 = 0xFF;
    lEntry.oneWayButton = false;
    lEntry.oneWayButtonCode = 0;
    lEntry.oneWayRawExecute = true;
    lEntry.oneWayRawLen = iPayloadLen;
    lEntry.oneWayBroadcastType = iChannel->getConfigured1WBroadcastType();
    const OneWayCommandProfile lOneWayProfile = oneWayCommandProfileForType(lEntry.oneWayBroadcastType);
    lEntry.oneWayAcei = lOneWayProfile.acei;
    lEntry.oneWayFp1 = lOneWayProfile.fp1;
    lEntry.oneWayFp2 = lOneWayProfile.fp2;
    lEntry.oneWayBroadcastTypeExplicit = false;
    lEntry.oneWayDestinationMode = OneWayDestinationMode::ProfileTyped;
    lEntry.oneWayExactDestination = 0;
    lEntry.sourceChannelIndex = channelIndexFor(iChannel);
    memcpy(lEntry.oneWayRawData, iPayload, iPayloadLen);
    lEntry.retries = 0;
    lEntry.active = true;
    return queuePush(lEntry);
}

bool IoHomeController::sendOneWayExecuteWithType(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                                 uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                                 uint8_t iBroadcastType)
{
    return sendOneWayExecuteWithDestination(iDestNodeId, iEncKey, iMain, iFp1, iFp2,
                                            OneWayDestinationMode::ExplicitType,
                                            iBroadcastType, 0);
}

bool IoHomeController::sendOneWayChannelExecuteWithType(IoHomecontrolChannel *iChannel,
                                                        uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                                        uint8_t iBroadcastType)
{
    if (!iChannel || !iChannel->is1W())
        return false;
    IoHomecontrolChannel *lProfileChannel = oneWayProfileForChannel(iChannel);
    if (!lProfileChannel || !lProfileChannel->hasOneWayControllerIdentity())
        return false;

    const OneWayCommandProfile lProfile = oneWayCommandProfileForType(iBroadcastType);
    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iChannel->getNodeId();
    lEntry.encKey = iChannel->getEncryptionKey();
    lEntry.command = IoHomeCommand::Execute;
    lEntry.oneWayStandardExecute = true;
    lEntry.oneWayMain = iMain;
    lEntry.oneWayAcei = lProfile.acei;
    lEntry.oneWayFp1 = iFp1;
    lEntry.oneWayFp2 = iFp2;
    lEntry.oneWayBroadcastType = iBroadcastType & 0x3F;
    lEntry.oneWayBroadcastTypeExplicit = true;
    lEntry.oneWayDestinationMode = OneWayDestinationMode::ExplicitType;
    lEntry.oneWayExactDestination = 0;
    lEntry.sourceChannelIndex = channelIndexFor(iChannel);
    lEntry.retries = 0;
    lEntry.active = true;
    return queuePush(lEntry);
}

bool IoHomeController::sendOneWayExecuteWithDestination(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                                        uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                                        OneWayDestinationMode iDestinationMode,
                                                        uint8_t iBroadcastType,
                                                        uint32_t iExactDestination)
{
    const uint8_t lResolvedType = (iDestinationMode == OneWayDestinationMode::ProfileTyped)
                                      ? oneWayBroadcastTypeForNode(iDestNodeId)
                                      : (iBroadcastType & 0x3F);
    const OneWayCommandProfile lProfile = oneWayCommandProfileForType(lResolvedType);
    return sendOneWayExecuteWithTemplate(iDestNodeId, iEncKey, lProfile.acei, iMain, iFp1, iFp2,
                                         iDestinationMode, iBroadcastType, iExactDestination);
}

bool IoHomeController::sendOneWayExecuteWithTemplate(uint32_t iDestNodeId, const uint8_t *iEncKey,
                                                     uint8_t iAcei, uint16_t iMain, uint8_t iFp1, uint8_t iFp2,
                                                     OneWayDestinationMode iDestinationMode,
                                                     uint8_t iBroadcastType,
                                                     uint32_t iExactDestination)
{
    if (iEncKey == nullptr)
        return false;

    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iDestNodeId;
    lEntry.encKey = iEncKey;
    lEntry.command = IoHomeCommand::Execute;
    lEntry.oneWayStandardExecute = true;
    lEntry.oneWayMain = iMain;
    lEntry.oneWayAcei = iAcei;
    lEntry.oneWayFp1 = iFp1;
    lEntry.oneWayFp2 = iFp2;
    lEntry.oneWayBroadcastType = (iDestinationMode == OneWayDestinationMode::ProfileTyped)
                                     ? oneWayBroadcastTypeForNode(iDestNodeId)
                                     : (iBroadcastType & 0x3F);
    lEntry.oneWayBroadcastTypeExplicit = (iDestinationMode == OneWayDestinationMode::ExplicitType);
    lEntry.oneWayDestinationMode = iDestinationMode;
    lEntry.oneWayExactDestination = iExactDestination & 0x00FFFFFF;
    lEntry.sourceChannelIndex = 0xFF;
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

bool IoHomeController::sendIdentify(uint32_t iDestNodeId, const uint8_t *iEncKey)
{
    IoHomecontrolChannel *lCh = channelForNode(iDestNodeId);
    if (lCh && lCh->is1W())
        return false;
    return sendCommand(iDestNodeId, iEncKey, IoHomeCommand::Identify, 0);
}

bool IoHomeController::sendBatteryStatusQuery(uint32_t iDestNodeId, const uint8_t *iEncKey)
{
    IoHomecontrolChannel *lCh = channelForNode(iDestNodeId);
    if (lCh && lCh->is1W())
        return false;
    return sendCommand(iDestNodeId, iEncKey, IoHomeCommand::Private, 0x06);
}

bool IoHomeController::sendBatteryStateQuery(uint32_t iDestNodeId, const uint8_t *iEncKey)
{
    IoHomecontrolChannel *lCh = channelForNode(iDestNodeId);
    if (lCh && lCh->is1W())
        return false;
    return sendCommand(iDestNodeId, iEncKey, IoHomeCommand::Private, 0x09);
}

bool IoHomeController::sendTiltStatusQuery(uint32_t iDestNodeId, const uint8_t *iEncKey)
{
    IoHomecontrolChannel *lCh = channelForNode(iDestNodeId);
    if (lCh && lCh->is1W())
        return false;
    return sendCommand(iDestNodeId, iEncKey, IoHomeCommand::Private, 0x03, 0x20, 0x01);
}

bool IoHomeController::sendTiltCommand(uint32_t iDestNodeId, const uint8_t *iEncKey, uint8_t iTiltPercent)
{
    IoHomecontrolChannel *lCh = channelForNode(iDestNodeId);
    if (lCh && lCh->is1W())
        return false;

    if (iTiltPercent > 100)
        iTiltPercent = 100;

    IoHomeQueueEntry lEntry;
    memset(&lEntry, 0, sizeof(lEntry));
    lEntry.destNodeId = iDestNodeId;
    lEntry.encKey = iEncKey;
    lEntry.command = IoHomeCommand::Execute;
    lEntry.param = 0xFF;
    lEntry.param2 = 0xFF;
    lEntry.param3 = 0xFF;
    lEntry.oneWayBroadcastType = oneWayBroadcastTypeForNode(iDestNodeId);
    lEntry.oneWayBroadcastTypeExplicit = false;
    lEntry.oneWayDestinationMode = OneWayDestinationMode::ProfileTyped;
    lEntry.oneWayExactDestination = 0;
    lEntry.twoWayTilt = true;
    lEntry.twoWayTiltPercent = iTiltPercent;
    lEntry.retries = 0;
    lEntry.active = true;
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

uint8_t IoHomeController::oneWayBroadcastTypeForNode(uint32_t iNodeId) const
{
    IoHomecontrolChannel *lCh = channelForNode(iNodeId);
    return lCh ? lCh->getConfigured1WBroadcastType() : mDefault1WBroadcastType;
}

uint32_t IoHomeController::oneWayDestinationForEntry(const IoHomeQueueEntry &iEntry) const
{
    switch (iEntry.oneWayDestinationMode)
    {
    case OneWayDestinationMode::All:
        return oneWayBroadcastTarget(0);
    case OneWayDestinationMode::Exact:
        return iEntry.oneWayExactDestination & 0x00FFFFFF;
    case OneWayDestinationMode::ExplicitType:
        return oneWayBroadcastTarget(iEntry.oneWayBroadcastType);
    case OneWayDestinationMode::ProfileTyped:
    default:
        // Default 1W behavior follows the rspaargaren forgePacket() target rule:
        //   dst = ((type << 6) | 0x3F).  Type 0 intentionally remains 0x00003F.
        return oneWayBroadcastTarget(iEntry.oneWayBroadcastType);
    }
}

IoHomecontrolChannel *IoHomeController::channelForNode(uint32_t iNodeId) const
{
    if (!mModule)
        return nullptr;

    const uint32_t lNodeId = iNodeId & 0x00FFFFFF;
    if (lNodeId == 0)
        return nullptr;

    for (uint8_t i = 0; i < IOHC_ChannelCount; i++)
    {
        IoHomecontrolChannel *lCh = mModule->getChannel(i);
        if (lCh && lCh->getNodeId() == lNodeId)
            return lCh;
    }
    return nullptr;
}

uint8_t IoHomeController::channelIndexFor(IoHomecontrolChannel *iChannel) const
{
    if (!mModule || !iChannel)
        return 0xFF;

    for (uint8_t i = 0; i < IOHC_ChannelCount; i++)
    {
        if (mModule->getChannel(i) == iChannel)
            return i;
    }
    return 0xFF;
}

IoHomecontrolChannel *IoHomeController::channelForQueueEntry(const IoHomeQueueEntry &iEntry) const
{
    if (mModule && iEntry.sourceChannelIndex < IOHC_ChannelCount)
    {
        IoHomecontrolChannel *lCh = mModule->getChannel(iEntry.sourceChannelIndex);
        if (lCh)
            return lCh;
    }
    return channelForNode(iEntry.destNodeId);
}

bool IoHomeController::resolveLowPower2W(uint32_t iNodeId) const
{
    IoHomecontrolChannel *lCh = channelForNode(iNodeId);
    if (!lCh)
        return true;
    if (lCh->is1W())
        return true;
    return lCh->isLowPower2W();
}

IoHomecontrolChannel *IoHomeController::oneWayProfileForNode(uint32_t iNodeId) const
{
    return oneWayProfileForChannel(channelForNode(iNodeId));
}

IoHomecontrolChannel *IoHomeController::oneWayProfileForChannel(IoHomecontrolChannel *iChannel) const
{
    if (!iChannel || !mModule)
        return iChannel;

    IoHomecontrolChannel *lProfile = iChannel;
    bool lVisited[IOHC_ChannelCount] = {};
    for (uint8_t depth = 0; depth < IOHC_ChannelCount; depth++)
    {
        const uint8_t lRef = lProfile->getConfigured1WProfileChannel();
        if (lRef == 0xFF)
            break;
        if (lRef >= IOHC_ChannelCount || lVisited[lRef])
            return iChannel;
        lVisited[lRef] = true;
        IoHomecontrolChannel *lReferenced = mModule->getChannel(lRef);
        if (!lReferenced || !lReferenced->is1W() || lReferenced == lProfile)
            return iChannel;
        lProfile = lReferenced;
    }

    // Imported legacy/QR profiles with the same address and key represent the
    // same remote and must share one sequence counter even without an ETS link.
    if (lProfile->hasOneWayControllerIdentity())
    {
        for (uint8_t i = 0; i < IOHC_ChannelCount; i++)
        {
            IoHomecontrolChannel *lCandidate = mModule->getChannel(i);
            if (!lCandidate || !lCandidate->is1W() ||
                lCandidate->getConfigured1WProfileChannel() != 0xFF ||
                !lCandidate->hasOneWayControllerIdentity())
                continue;
            if (lCandidate->getOneWayControllerNodeId() == lProfile->getOneWayControllerNodeId() &&
                memcmp(lCandidate->getOneWayControllerKey(), lProfile->getOneWayControllerKey(), 16) == 0)
                return lCandidate;
        }
    }
    return lProfile;
}

// --- Pairing ---

bool IoHomeController::startPairing(uint8_t iChannelIndex, uint32_t iKnownNodeId)
{
    mLastPairStartStatus = PairStartStatus::Ok;
    mLastPairStartBlockedState = mState;
    mPairing2WMode = Pairing2WMode::Normal;

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

    mPairing1WStage = 0;
    mPairing1WBroadcastType = mDefault1WBroadcastType;
    mPairingChannel = iChannelIndex;
    mPairingFreqIdx = frequencyIndexForHz(kNormal2WTxFreqHz);
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
        // A 1W remote can only build the authenticated 0x2E/0x30/0x39 frames
        // once it owns a controller identity (remote node id + key). On a
        // freshly ETS-configured channel that profile may still be empty, which
        // previously failed silently in PairSend1WKeyTransfer (remote=0 /
        // key=missing). Provision it on demand here, preserving the
        // ETS-configured manufacturer.
        if (mModule && !mModule->ensureOneWayControllerProfile(lCh))
        {
            mLastPairStartStatus = PairStartStatus::Failed;
            mLastPairStartBlockedState = mState;
            logInfoP("Pairing: 1W controller profile missing for ch%u and could not be generated; run 'iohc 1wnew %u' first",
                     static_cast<unsigned>(iChannelIndex + 1),
                     static_cast<unsigned>(iChannelIndex + 1));
            return false;
        }

        mPairing1WBroadcastType = lCh->getConfigured1WBroadcastType();
        uint32_t lKnownNodeId = iKnownNodeId & 0x00FFFFFF;
        if (lKnownNodeId == 0)
        {
            if (lCh->isPaired())
                lKnownNodeId = lCh->getNodeId();
            else
                lKnownNodeId = lCh->getConfigured1WTargetNodeId();
        }
        // For 1W, the actuator node ID is not part of the on-air Add/SendKey
        // transaction. A zero target is a valid virtual/broadcast-only remote
        // profile: SendKey1W still carries remote src + typed/all broadcast dst.
        mDiscoveredNodeId = lKnownNodeId;
        // First-class 1W operations are explicit and map directly to the
        // reference flow: 0x2E announce, 0x30 add/send-key, 0x39 remove.
        const Pairing1WMode lMode = mRequestedPairing1WMode;
        mPairing1WMode = lMode;
        mRequestedPairing1WMode = Pairing1WMode::AnnounceAdd;
        switch (lMode)
        {
        case Pairing1WMode::Remove:
            mPairing1WStage = 2;
            mState = ControllerState::PairSend1WRemove;
            break;
        case Pairing1WMode::AddOnly:
            mPairing1WStage = 1;
            mState = ControllerState::PairSend1WKeyTransfer;
            break;
        case Pairing1WMode::AnnounceOnly:
        case Pairing1WMode::AnnounceAdd:
        default:
            mPairing1WStage = 0;
            mState = ControllerState::PairSend1WAnnounce;
            break;
        }

        logInfoP("Pairing: 1W mode=%s sequence=%s ch=%u target=%s0x%06X",
                 pairing1WModeName(lMode),
                 pairing1WCommandSequence(lMode),
                 static_cast<unsigned>(iChannelIndex + 1),
                 mDiscoveredNodeId == 0 ? "broadcast-only " : "",
                 mDiscoveredNodeId);
        if (mPairDiagnosticTraceEnabled)
        {
            logInfoP("PairDiag: starting 1W mode=%s sequence=%s state=%s",
                     pairing1WModeName(lMode),
                     pairing1WCommandSequence(lMode),
                     stateName(mState));
            tracePairDiagnosticStateChange();
        }
        return true;
    }

    mPairing1WMode = Pairing1WMode::AnnounceAdd;
    mRequestedPairing1WMode = Pairing1WMode::AnnounceAdd;
    mState = ControllerState::PairSendDiscovery;
    if (mPairDiagnosticTraceEnabled)
    {
        logInfoP("PairDiag: starting 2W pairing ch=%u known=0x%06X", static_cast<unsigned>(iChannelIndex + 1), iKnownNodeId & 0x00FFFFFF);
        tracePairDiagnosticStateChange();
    }
    return true;
}

bool IoHomeController::startPairingExperimental(uint8_t iChannelIndex, uint32_t iKnownNodeId, Pairing2WMode iMode)
{
    if (iMode == Pairing2WMode::Normal)
        return startPairing(iChannelIndex, iKnownNodeId);

    IoHomecontrolChannel *lCh = mModule ? mModule->getChannel(iChannelIndex) : nullptr;
    if (lCh && lCh->is1W())
    {
        mLastPairStartStatus = PairStartStatus::Failed;
        mLastPairStartBlockedState = mState;
        return false;
    }

    const bool lOk = startPairing(iChannelIndex, iKnownNodeId);
    if (lOk)
    {
        mPairing2WMode = iMode;
        logInfoP("Pairing: experimental 2W mode enabled: %u",
                 static_cast<unsigned>(mPairing2WMode));
    }
    return lOk;
}

bool IoHomeController::startPairing1W(uint8_t iChannelIndex, uint32_t iKnownNodeId, Pairing1WMode iMode)
{
    mRequestedPairing1WMode = iMode;
    const bool lOk = startPairing(iChannelIndex, iKnownNodeId);
    if (!lOk)
        mRequestedPairing1WMode = Pairing1WMode::AnnounceAdd;
    return lOk;
}

bool IoHomeController::startPairing1WAnnounceOnly(uint8_t iChannelIndex, uint32_t iKnownNodeId)
{
    return startPairing1W(iChannelIndex, iKnownNodeId, Pairing1WMode::AnnounceOnly);
}

bool IoHomeController::startPairing1WAddOnly(uint8_t iChannelIndex, uint32_t iKnownNodeId)
{
    return startPairing1W(iChannelIndex, iKnownNodeId, Pairing1WMode::AddOnly);
}

bool IoHomeController::startPairing1WAnnounceAdd(uint8_t iChannelIndex, uint32_t iKnownNodeId)
{
    return startPairing1W(iChannelIndex, iKnownNodeId, Pairing1WMode::AnnounceAdd);
}

bool IoHomeController::startPairing1WRemove(uint8_t iChannelIndex, uint32_t iKnownNodeId)
{
    return startPairing1W(iChannelIndex, iKnownNodeId, Pairing1WMode::Remove);
}

bool IoHomeController::startPairingWithType(uint8_t iChannelIndex, uint32_t iKnownNodeId, uint8_t iBroadcastType, Pairing1WMode iMode)
{
    const bool lOk = startPairing1W(iChannelIndex, iKnownNodeId, iMode);
    if (lOk)
    {
        mPairing1WBroadcastType = iBroadcastType & 0x3F;
        logInfoP("Pairing: 1W mode=%s explicit type=%u target=0x%06X",
                 pairing1WModeName(mPairing1WMode),
                 static_cast<unsigned>(mPairing1WBroadcastType),
                 oneWayBroadcastTarget(mPairing1WBroadcastType));
    }
    return lOk;
}

IoHomeController::PairStartStatus IoHomeController::lastPairStartStatus() const
{
    return mLastPairStartStatus;
}

ControllerState IoHomeController::lastPairStartBlockedState() const
{
    return mLastPairStartBlockedState;
}

Pairing1WMode IoHomeController::lastPairing1WMode() const
{
    return mPairing1WMode;
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

void IoHomeController::startDiscovery(bool iEncrypted)
{
    if (mNetworkScanActive)
        stopNetworkScan();
    else if (mState == ControllerState::PassiveListening)
        setPassiveMode(false);

    if (mState != ControllerState::Idle)
        return;

    mPairingFreqIdx = 0;
    mPairingStartTime = millis();
    mDiscoverySendPhase = DiscoverySendPhase::SetFrequency;
    resetDiscoveryTimingTrace();
    mDiscoverySPE = iEncrypted;
    mState = ControllerState::DiscoverySending;
    if (mPairDiagnosticTraceEnabled)
    {
        logInfoP(iEncrypted ? "PairDiag: starting encrypted discovery broadcast"
                            : "PairDiag: starting discovery broadcast");
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
        mPassivePairNodeId = 0;
        mPassiveChallengeValid = false;
        memset(mPassiveChallenge, 0, sizeof(mPassiveChallenge));
        if (mPassiveKeySniffStatus == PassiveKeySniffStatus::Listening)
            mPassiveKeySniffStatus = PassiveKeySniffStatus::Idle;
        mState = ControllerState::Idle;
    }
}

bool IoHomeController::isPassiveMode() const
{
    return mPassiveMode;
}

bool IoHomeController::startPassiveKeySniff(uint32_t iTimeoutMs)
{
    if (mState != ControllerState::Idle && mState != ControllerState::PassiveListening)
        return false;

    clearPassiveKeyResult();
    mPassivePairNodeId = 0;
    mPassiveChallengeValid = false;
    memset(mPassiveChallenge, 0, sizeof(mPassiveChallenge));
    mPassiveKeySniffStartedAt = millis();
    mPassiveKeySniffTimeoutMs = iTimeoutMs;
    mPassiveKeySniffStatus = PassiveKeySniffStatus::Listening;
    setPassiveMode(true);
    return true;
}

void IoHomeController::stopPassiveKeySniff()
{
    if (mPassiveKeySniffStatus == PassiveKeySniffStatus::Listening ||
        mPassiveKeySniffStatus == PassiveKeySniffStatus::Timeout)
    {
        mPassiveKeySniffStatus = PassiveKeySniffStatus::Idle;
    }

    mPassivePairNodeId = 0;
    mPassiveChallengeValid = false;
    memset(mPassiveChallenge, 0, sizeof(mPassiveChallenge));

    if (mPassiveMode && !mNetworkScanActive)
        setPassiveMode(false);
}

void IoHomeController::clearPassiveKeyResult()
{
    memset(&mPassiveKeyResult, 0, sizeof(mPassiveKeyResult));
    if (mPassiveKeySniffStatus == PassiveKeySniffStatus::Captured ||
        mPassiveKeySniffStatus == PassiveKeySniffStatus::Timeout)
    {
        mPassiveKeySniffStatus = PassiveKeySniffStatus::Idle;
    }
}

IoHomeController::PassiveKeySniffStatus IoHomeController::passiveKeySniffStatus() const
{
    return mPassiveKeySniffStatus;
}

const IoHomeController::PassiveKeyResult &IoHomeController::passiveKeyResult() const
{
    return mPassiveKeyResult;
}

void IoHomeController::setGatewayMode(bool iEnabled)
{
    mGatewayMode = iEnabled;
    resetGatewaySessionState();

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
    resetGatewaySessionState();
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
    case IoHomeCommand::Identify:
        return "Identify";
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

const char *IoHomeController::pairing1WModeName(Pairing1WMode iMode)
{
    switch (iMode)
    {
    case Pairing1WMode::AnnounceOnly:
        return "announce-only";
    case Pairing1WMode::AddOnly:
        return "add-only";
    case Pairing1WMode::Remove:
        return "remove";
    case Pairing1WMode::AnnounceAdd:
    default:
        return "announce-add";
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
    case ControllerState::PairSend1WAnnounce:
        return "PairSend1WAnnounce";
    case ControllerState::PairWait1WAnnounce:
        return "PairWait1WAnnounce";
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

    if (mState == ControllerState::DiscoveryListening && strcmp(iPrefix, "rx") == 0)
        tracePairDiagnosticDiscoveryInterpretation(iFrame, iFreqIdx);
}

void IoHomeController::tracePairDiagnosticDiscoveryInterpretation(const IoHomeFrame &iFrame, uint8_t iFreqIdx) const
{
    const uint32_t lFreqHz = (iFreqIdx < IOHC_NUM_FREQUENCIES) ? IOHC_FREQUENCIES[iFreqIdx] : 0;

    switch (iFrame.commandId)
    {
    case IoHomeCommand::Discover2ERequest:
        logInfoP("PairDiag: discovery note 1W learn request src=0x%06X dst=0x%06X freq=%u %luHz",
                 iFrame.getSrcNodeId(),
                 iFrame.getDestNodeId(),
                 static_cast<unsigned>(iFreqIdx),
                 static_cast<unsigned long>(lFreqHz));
        break;

    case IoHomeCommand::RemoveController:
        logInfoP("PairDiag: discovery note 1W controller reset/removal src=0x%06X dst=0x%06X freq=%u %luHz",
                 iFrame.getSrcNodeId(),
                 iFrame.getDestNodeId(),
                 static_cast<unsigned>(iFreqIdx),
                 static_cast<unsigned long>(lFreqHz));
        break;

    case IoHomeCommand::SendKey1W:
        logInfoP("PairDiag: discovery note 1W key transfer already in progress src=0x%06X dst=0x%06X freq=%u %luHz",
                 iFrame.getSrcNodeId(),
                 iFrame.getDestNodeId(),
                 static_cast<unsigned>(iFreqIdx),
                 static_cast<unsigned long>(lFreqHz));
        break;

    default:
        break;
    }
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
    tracePairDiagnosticCompactPair();
}

void IoHomeController::tracePairDiagnosticCompactPair() const
{
    if (!mPairDiagnosticTraceEnabled)
        return;

    const uint32_t lPairFreqHz = (mPairingFreqIdx < IOHC_NUM_FREQUENCIES) ? IOHC_FREQUENCIES[mPairingFreqIdx] : 0;
    logInfoP("pair: mode=%s state=%s ch=%u freq=%lu node=0x%06X cmd=0x%02X",
             pairingModeName(mPairing2WMode, mState),
             stateName(mState),
             static_cast<unsigned>(mPairingChannel + 1),
             static_cast<unsigned long>(lPairFreqHz),
             mDiscoveredNodeId,
             static_cast<unsigned>(static_cast<uint8_t>(mTxFrame.commandId)));
}

void IoHomeController::tracePairDiagnosticCompactRx(const IoHomeRadioHealth &iHealth) const
{
    if (!mPairDiagnosticTraceEnabled)
        return;

    logInfoP("rx: dio0=%lu irqPoll=%lu overrun=%lu fifoEmpty=%lu parseFail=%lu crcFail=%lu lastLen=%u",
             static_cast<unsigned long>(iHealth.irqCount),
             static_cast<unsigned long>(iHealth.irqPollHitCount),
             static_cast<unsigned long>(iHealth.rxFifoOverrunCount),
             static_cast<unsigned long>(iHealth.rxFifoEmptyCount),
             static_cast<unsigned long>(iHealth.rxParseFailCount),
             static_cast<unsigned long>(iHealth.crcErrorCount),
             static_cast<unsigned>(iHealth.lastRxLen));
}

void IoHomeController::tracePairDiagnosticTx2W(const IoHomeFrame &iFrame, uint16_t iPreambleSymbols) const
{
    if (!mPairDiagnosticTraceEnabled || ((iFrame.ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0))
        return;

    const uint32_t lFreqHz = (mCurrentFreqIdx < IOHC_NUM_FREQUENCIES) ? IOHC_FREQUENCIES[mCurrentFreqIdx] : 0;
    logInfoP("tx2w: ch=%u freq=%lu cmd=0x%02X preamble=%u start=%u lp=%u",
             static_cast<unsigned>(iohcChannelNumberForFrequency(lFreqHz)),
             static_cast<unsigned long>(lFreqHz),
             static_cast<unsigned>(static_cast<uint8_t>(iFrame.commandId)),
             static_cast<unsigned>(iPreambleSymbols),
             (iFrame.ctrlByte0 & IOHC_CTRL0_START) ? 1U : 0U,
             (iFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER) ? 1U : 0U);
}

void IoHomeController::trace1WRepeatPlan(const char *iContext) const
{
    if (!mPairDiagnosticTraceEnabled)
        return;

    logInfoP("1w repeat: ctx=%s total=%u gap=%ums firstPre=%u repeatPre=%u",
             iContext ? iContext : "1w",
             static_cast<unsigned>(IOHC_1W_REPEAT_COUNT + 1U),
             static_cast<unsigned>(IOHC_1W_REPEAT_INTERVAL_MS),
             static_cast<unsigned>(IOHC_PREAMBLE_LONG),
             static_cast<unsigned>(IOHC_PREAMBLE_SHORT));
}

bool IoHomeController::createAndTraceHmac1W(const uint8_t *iTranscript, uint8_t iTranscriptLen,
                                            uint16_t iSequenceNum, const uint8_t iControllerKey[16],
                                            uint8_t oHmac[IOHC_HMAC_SIZE]) const
{
    uint8_t lIv[16];
    if (!IoHomeCrypto::createHmac1WWithIv(iTranscript, iTranscriptLen,
                                          iSequenceNum, iControllerKey,
                                          lIv, oHmac))
    {
        return false;
    }

    if (mPairDiagnosticTraceEnabled && iTranscript && iTranscriptLen > 0)
    {
        const std::string lTranscriptHex = hexDump(iTranscript, iTranscriptLen);
        const std::string lIvHex = hexDump(lIv, sizeof(lIv));
        const std::string lHmacHex = hexDump(oHmac, IOHC_HMAC_SIZE);
        logInfoP("1w crypto: cmd=0x%02X transcript=%s seq=0x%04X iv=%s hmac=%s",
                 static_cast<unsigned>(iTranscript[0]),
                 lTranscriptHex.c_str(),
                 static_cast<unsigned>(iSequenceNum),
                 lIvHex.c_str(),
                 lHmacHex.c_str());
    }

    return true;
}

RadioError IoHomeController::configureNormal2WTxRadio(uint16_t iPreambleSymbols)
{
    const uint32_t lTxFreq = kNormal2WTxFreqHz;
    return configureTxRadio(iPreambleSymbols, &lTxFreq);
}

void IoHomeController::serviceRxScan()
{
    if (!mRxScanEnabled || mRadio.state() != RadioState::Receiving)
        return;

    const uint32_t lNow = micros();
    if (lNow - mRxScanLastSwitch < mRxScanIntervalUs)
        return;

    if (mRadio.isPreambleDetected())
        return;

    const uint8_t lNextFreqIdx = (mCurrentFreqIdx + 1) % IOHC_NUM_FREQUENCIES;
    if (mRadio.setFrequency(IOHC_FREQUENCIES[lNextFreqIdx]) == RadioError::None)
    {
        mCurrentFreqIdx = lNextFreqIdx;
        if (mRadio.startReceive() == RadioError::None)
            mRxScanLastSwitch = lNow;
    }
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
    tracePairDiagnosticCompactPair();
    tracePairDiagnosticCompactRx(lHealth);
}

void IoHomeController::resetDiscoveryTimingTrace()
{
    memset(&mDiscoveryTimingTrace, 0, sizeof(mDiscoveryTimingTrace));
}

uint16_t IoHomeController::nextSequence1W(IoHomecontrolChannel *iProfile, bool iForceFlashSave)
{
    if (!iProfile)
        return 0;

    bool lFlashSaveRequired = false;
    const uint16_t lUsedSequence = iProfile->incrementSequence1W(iForceFlashSave, lFlashSaveRequired);
    const uint16_t lNextSequence = static_cast<uint16_t>(lUsedSequence + 1U);
    const uint16_t lReservedSequence = iProfile->getReservedSequence1W();

    if (lFlashSaveRequired)
        openknx.flash.save();

    if (mPairDiagnosticTraceEnabled)
    {
        logInfoP("1w seq: used=0x%04X next=0x%04X reserved=0x%04X saved=%u",
                 static_cast<unsigned>(lUsedSequence),
                 static_cast<unsigned>(lNextSequence),
                 static_cast<unsigned>(lReservedSequence),
                 lFlashSaveRequired ? 1U : 0U);
    }

    return lUsedSequence;
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
    lHealth.rxFifoOverrunCount = 0;
    lHealth.rxFifoEmptyCount = 0;
    lHealth.rxParseFailCount = mRxParseFailCount;
    lHealth.lastRxLen = 0;
    lHealth.lastRxIrqStatus = 0;
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
    lHealth.irqPollHitCount = mRadio.rxPayloadReadyPollCount();
    lHealth.rxFifoOverrunCount = mRadio.rxFifoOverrunCount();
    lHealth.rxFifoEmptyCount = mRadio.rxFifoEmptyCount();
    lHealth.crcErrorCount = mRadio.rxCrcFailCount();
    lHealth.lastRxLen = mRadio.lastRxLen();
    lHealth.lastRxIrqStatus = mRadio.lastRxIrqStatus();
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

    if (mPassiveKeySniffStatus == PassiveKeySniffStatus::Listening &&
        mPassiveKeySniffTimeoutMs > 0 &&
        millis() - mPassiveKeySniffStartedAt >= mPassiveKeySniffTimeoutMs)
    {
        mPassiveKeySniffStatus = PassiveKeySniffStatus::Timeout;
        mPassivePairNodeId = 0;
        mPassiveChallengeValid = false;
        memset(mPassiveChallenge, 0, sizeof(mPassiveChallenge));
        if (mPassiveMode && !mNetworkScanActive)
        {
            mPassiveMode = false;
            mState = ControllerState::Idle;
            startReceive();
        }
    }

    // Multi-frequency RX scanning: cycle through frequencies only while listening.
    // TX-side paths explicitly select their protocol channel before transmitting.
    if (mState == ControllerState::Idle || mState == ControllerState::PassiveListening)
        serviceRxScan();

    // Check for incoming packets in any state
    if (mRadio.isPacketAvailable())
    {
        uint8_t lLen = mRadio.readPacket(mRxBuffer, sizeof(mRxBuffer));
        bool lParsed = (lLen > 0 && mRxFrame.deserialize(mRxBuffer, lLen));
        if (lLen > 0 && !lParsed)
            mRxParseFailCount++;
        if (lParsed)
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

                    // Default laberning-style path:
                    // 0x28 DiscoverRequest -> 0x29/0x2B DiscoverResponse
                    // -> 0x31 KeyInitTransfer -> 0x3C -> 0x32 -> 0x33.
                    // Experimental branches are only reachable via explicit
                    // diagnostic pairing modes.
                    switch (mPairing2WMode)
                    {
                    case Pairing2WMode::DiscoveryConfirmation:
                        mState = ControllerState::PairSendDiscoveryConfirmation;
                        break;

                    case Pairing2WMode::LaunchKeyTransfer:
                        mState = ControllerState::PairSendLaunchKeyTransfer;
                        break;

                    case Pairing2WMode::PullKey:
                        mState = ControllerState::PairSendPullKeyChallenge;
                        break;

                    case Pairing2WMode::Normal:
                    default:
                        mState = ControllerState::PairSendKeyInit;
                        break;
                    }
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
                    if (IoHomeCrypto::crypt2WKeyXor(lLaunchData, sizeof(lLaunchData),
                                                    mPairingChallenge, mRxFrame.data,
                                                    IOHC_TRANSFER_KEY, mPairPulledKey))
                    {
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
                            lCh->setLowPower2W(true);
                            lCh->setEncryptionKey(mSystemKey);
                            openknx.flash.save(true); // pairing is rare & critical: bypass write throttle
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
                    else
                        interpretSetConfig1Result(false);
                }
            }
            else if (mState == ControllerState::PairWaitSetConfig1FinalResponse)
            {
                if (mRxFrame.getSrcNodeId() == mDiscoveredNodeId &&
                    mRxFrame.getDestNodeId() == mOwnNodeId)
                {
                    interpretSetConfig1Result(true);
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
    case ControllerState::PairSend1WAnnounce:
        processPairSend1WAnnounce();
        break;
    case ControllerState::PairWait1WAnnounce:
        processPairWait1WAnnounce();
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
            if (buildTxFrame(mCurrentCmd))
                mState = ControllerState::TxPending;
            else
                mCurrentCmd.active = false;
        }
    }
}

void IoHomeController::processTxPending()
{
    const bool lIs1WFrame = ((mTxFrame.ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0);
    if (lIs1WFrame)
    {
        // Queued 1W commands emulate a handheld one-way remote. A real remote
        // transmits each frame across all io-homecontrol channels (frequency
        // agility) so the actuator - which scans the three frequencies - can
        // catch it. Start on the canonical CH2 (the channel pairing uses)
        // instead of the random channel the RX scan happened to leave the radio
        // on, and hop across the channels for the repeats.
        mCurrentFreqIdx = frequencyIndexForHz(kNormal2WTxFreqHz);
        mTx1WHopFrequencies = true;
    }
    const uint8_t lTxDutyFreqIdx = lIs1WFrame ? mCurrentFreqIdx : frequencyIndexForHz(kNormal2WTxFreqHz);
    if (!isDutyCycleOk(lTxDutyFreqIdx))
        return; // wait for duty cycle to clear

    mTxLen = mTxFrame.serialize(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen == 0)
    {
        mState = ControllerState::Idle;
        return;
    }

    if (mPairDiagnosticTraceEnabled && lIs1WFrame)
    {
        const std::string lHex = hexDump(mTxBuffer, mTxLen);
        logInfoP("PairDiag: tx1w cmd=0x%02X name=%s len=%u hasHmac=%u seq=%u src=0x%06X dst=0x%06X hex=%s",
                 static_cast<unsigned>(mTxFrame.commandId),
                 commandName(mTxFrame.commandId),
                 static_cast<unsigned>(mTxLen),
                 mTxFrame.hasHmac ? 1U : 0U,
                 static_cast<unsigned>(mTxFrame.dataLen >= 2 ? ((static_cast<uint16_t>(mTxFrame.data[mTxFrame.dataLen - 2]) << 8) | mTxFrame.data[mTxFrame.dataLen - 1])
                                                             : 0),
                 mTxFrame.getSrcNodeId(),
                 mTxFrame.getDestNodeId(),
                 lHex.c_str());
        trace1WRepeatPlan("tx1w");
    }

    // Set preamble based on frame type: START frames need long preamble for low-power devices.
    // Normal 2W controller-originated TX is always sent on CH2; RX scanning remains separate.
    bool lIsStartFrame = (mTxFrame.ctrlByte0 & IOHC_CTRL0_START);
    const uint16_t lPreamble = lIsStartFrame ? IOHC_PREAMBLE_LONG : IOHC_PREAMBLE_SHORT;
    if (!lIs1WFrame)
    {
        mWaitingFinalResponse = false;
        mSawChallenge = false;
        mResponseTimeoutMs = lIsStartFrame ? IOHC_RX_TIMEOUT_MS : IOHC_RX_FINAL_TIMEOUT_MS;
        mRetryAtMs = 0;
    }
    const uint32_t l1WTxFreqHz = IOHC_FREQUENCIES[mCurrentFreqIdx];
    const RadioError lPrepErr = lIs1WFrame ? configureTxRadio(lPreamble, &l1WTxFreqHz) : configureNormal2WTxRadio(lPreamble);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::Idle;
        return;
    }

    if (!lIs1WFrame)
        tracePairDiagnosticTx2W(mTxFrame, lPreamble);

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

        mRxScanLastSwitch = micros();
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
            mRxScanLastSwitch = micros();
            mStateTimer = millis();
            mState = ControllerState::WaitResponse;
        }
    }
    else if (millis() - mStateTimer > currentTxTimeoutMs())
    {
        // TX timeout. Use a dynamic timeout because long io-homecontrol
        // preambles can exceed the generic 500 ms guard on SX1276.
        mRadio.standby();
        mState = ControllerState::Idle;
    }
}

void IoHomeController::processTx1WRepeat()
{
    if (millis() - mTx1WRepeatTimer < IOHC_1W_REPEAT_INTERVAL_MS)
        return; // wait for interval

    if (mTx1WHopFrequencies)
    {
        // Advance to the next io-homecontrol channel so the four transmissions
        // of a queued 1W command cover all three frequencies, matching the
        // frequency agility of a real one-way remote.
        const uint8_t lNextFreqIdx = (mCurrentFreqIdx + 1) % IOHC_NUM_FREQUENCIES;
        const RadioError lFreqErr = mRadio.setFrequency(IOHC_FREQUENCIES[lNextFreqIdx]);
        if (lFreqErr == RadioError::Busy)
            return; // radio busy: retry next loop without changing channel
        if (lFreqErr == RadioError::None)
            mCurrentFreqIdx = lNextFreqIdx;
    }

    // Re-send same buffer with the reference short repeat preamble.
    const RadioError lErr = startShortPreambleTransmit(mTxBuffer, mTxLen);
    if (lErr == RadioError::None)
    {
        if (mPairDiagnosticTraceEnabled)
        {
            const uint32_t lRepeatFreqHz = (mCurrentFreqIdx < IOHC_NUM_FREQUENCIES) ? IOHC_FREQUENCIES[mCurrentFreqIdx] : 0;
            logInfoP("PairDiag: 1W repeat tx remaining=%u len=%u pre=%u ch=%u freq=%lu",
                     static_cast<unsigned>(mTx1WRepeatRemaining),
                     static_cast<unsigned>(mTxLen),
                     static_cast<unsigned>(IOHC_PREAMBLE_SHORT),
                     static_cast<unsigned>(iohcChannelNumberForFrequency(lRepeatFreqHz)),
                     static_cast<unsigned long>(lRepeatFreqHz));
        }
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

    if (mSawChallenge && mWaitingFinalResponse && kIsSX1262Radio &&
        millis() - mStateTimer < IOHC_AUTH_DWELL_MS_SX1262)
    {
        // After sending 0x3D stay in RX on the same channel for a short dwell
        // before allowing RX scan/retry handling.
        return;
    }

    serviceRxScan();

    if (millis() - mStateTimer >= mResponseTimeoutMs)
    {
        if (mRetryAtMs == 0)
        {
            mRetryAtMs = millis() + IOHC_RETRY_GAP_MS;
            return;
        }

        if (millis() < mRetryAtMs)
            return;

        mRetryAtMs = 0;

        // No response — retry the controller-originated 2W command on CH2.
        // RX may scan/hop while waiting, but TX must not be moved to CH1/CH3.
        if (mCurrentCmd.active && mCurrentCmd.retries < IOHC_MAX_RETRIES)
        {
            const IoHomeQueueEntry lFailedCmd = mCurrentCmd;
            const bool lAfterChallenge = mSawChallenge;
            mCurrentCmd.retries++;
            mWaitingFinalResponse = false;
            mSawChallenge = false;
            mResponseTimeoutMs = IOHC_RX_TIMEOUT_MS;
            if (buildTxFrame(mCurrentCmd))
                mState = ControllerState::TxPending;
            else
            {
                mCurrentCmd.active = false;
                mWaitingFinalResponse = false;
                mSawChallenge = false;
                mState = ControllerState::Idle;
                notifyTrackedStatusPollFailure(mModule, lFailedCmd, lAfterChallenge);
            }
        }
        else
        {
            const IoHomeQueueEntry lFailedCmd = mCurrentCmd;
            const bool lAfterChallenge = mSawChallenge;
            mCurrentCmd.active = false;
            mWaitingFinalResponse = false;
            mSawChallenge = false;
            mState = ControllerState::Idle;
            notifyTrackedStatusPollFailure(mModule, lFailedCmd, lAfterChallenge);
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
        // Device challenges our authenticated command — build and send ChallengeResponse (0x3D).
        // The centralized builder keeps 0x3D as a continuation frame and puts
        // the HMAC bytes into data[], never as appended 2W HMAC.
        uint8_t lChallenge[6];
        memcpy(lChallenge, mRxFrame.data, sizeof(lChallenge));

        IoHomeFrame lFrame;
        if (!build2WChallengeResponse(lFrame, mOwnNodeId, mCurrentCmd.destNodeId,
                                      resolveLowPower2W(mCurrentCmd.destNodeId),
                                      mTxFrame, lChallenge, mCurrentCmd.encKey))
        {
            const IoHomeQueueEntry lFailedCmd = mCurrentCmd;
            mCurrentCmd.active = false;
            mState = ControllerState::Idle;
            notifyTrackedStatusPollFailure(mModule, lFailedCmd, true);
            return;
        }

        lFrame.dataLen = IOHC_HMAC_SIZE;
        lFrame.hasHmac = false;

        uint8_t lLen = lFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
        if (lLen > 0)
        {
            const uint16_t lPreamble = authResponsePreamble();
            const RadioError lPrepErr = configureNormal2WTxRadio(lPreamble);
            if (lPrepErr == RadioError::Busy)
                return;
            if (lPrepErr != RadioError::None)
            {
                const IoHomeQueueEntry lFailedCmd = mCurrentCmd;
                mCurrentCmd.active = false;
                mState = ControllerState::Idle;
                notifyTrackedStatusPollFailure(mModule, lFailedCmd, true);
                return;
            }
            tracePairDiagnosticTx2W(lFrame, lPreamble);
            const RadioError lErr = mRadio.startTransmit(mTxBuffer, lLen);
            if (lErr == RadioError::None)
            {
                mAuthResponseSent = true;
                mWaitingFinalResponse = true;
                mSawChallenge = true;
                mResponseTimeoutMs = IOHC_RX_FINAL_TIMEOUT_MS;
                mRetryAtMs = 0;
                mStateTimer = millis();
                mState = ControllerState::TxInProgress; // will transition to WaitResponse when TX done
            }
            else if (lErr == RadioError::Busy)
            {
                return;
            }
            else
            {
                const IoHomeQueueEntry lFailedCmd = mCurrentCmd;
                mCurrentCmd.active = false;
                mState = ControllerState::Idle;
                notifyTrackedStatusPollFailure(mModule, lFailedCmd, true);
            }
        }
        else
        {
            const IoHomeQueueEntry lFailedCmd = mCurrentCmd;
            mCurrentCmd.active = false;
            mState = ControllerState::Idle;
            notifyTrackedStatusPollFailure(mModule, lFailedCmd, true);
        }
        return;
    }

    dispatchRxFrame();
    mCurrentCmd.active = false;
    mWaitingFinalResponse = false;
    mSawChallenge = false;
    mRetryAtMs = 0;
    mResponseTimeoutMs = IOHC_RX_TIMEOUT_MS;
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

    // Build discovery frame through the centralized 2W builder.
    if (!build2WDiscover(mTxFrame, mOwnNodeId, mDiscoverySPE, mSystemKey))
    {
        mState = ControllerState::PairFailed;
        return;
    }

    const uint32_t lDiscoveryFreq = kNormal2WTxFreqHz;
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

    mTxLen = mTxFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        tracePairDiagnosticTx2W(mTxFrame, IOHC_PREAMBLE_LONG);
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

    serviceRxScan();

    if (millis() - mStateTimer > 2000) // retry cadence; TX stays on CH2
    {
        mPairingFreqIdx = frequencyIndexForHz(kNormal2WTxFreqHz);
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

    const uint32_t lConfirmationFreq = kNormal2WTxFreqHz;
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

    mTxLen = mTxFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        tracePairDiagnosticTx2W(mTxFrame, IOHC_PREAMBLE_LONG);
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

    serviceRxScan();

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

    const RadioError lPrepErr = configureNormal2WTxRadio(IOHC_PREAMBLE_LONG);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        logDebugP("Pairing: failed to send launch key transfer to 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
        return;
    }

    mTxLen = mPairLaunchKeyTransferFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        tracePairDiagnosticTx2W(mPairLaunchKeyTransferFrame, IOHC_PREAMBLE_LONG);
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

    serviceRxScan();

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
    lFrame.setLowPower(true);
    lFrame.setSrcNode(mOwnNodeId);
    lFrame.setDestNode(mDiscoveredNodeId);
    lFrame.commandId = IoHomeCommand::ChallengeRequest;
    IoHomeCrypto::generateChallenge(mPairPullAuthChallenge);
    memcpy(lFrame.data, mPairPullAuthChallenge, sizeof(mPairPullAuthChallenge));
    lFrame.dataLen = sizeof(mPairPullAuthChallenge);
    lFrame.hasHmac = false;

    uint8_t lLen = lFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (lLen > 0)
    {
        const RadioError lPrepErr = configureNormal2WTxRadio(IOHC_PREAMBLE_SHORT);
        if (lPrepErr == RadioError::Busy)
            return;
        if (lPrepErr != RadioError::None)
        {
            logDebugP("Pairing: failed to challenge pulled key for 0x%06X, falling back to push flow", mDiscoveredNodeId);
            mState = ControllerState::PairSendKeyInit;
            return;
        }
        tracePairDiagnosticTx2W(lFrame, IOHC_PREAMBLE_SHORT);
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

    serviceRxScan();

    if (millis() - mStateTimer > IOHC_RX_TIMEOUT_MS)
    {
        logDebugP("Pairing: pulled key auth timed out for 0x%06X, falling back to push flow", mDiscoveredNodeId);
        mState = ControllerState::PairSendKeyInit;
    }
}

void IoHomeController::processPairSend1WAnnounce()
{
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mState = ControllerState::PairFailed;
        return;
    }

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

    IoHomecontrolChannel *lCh = mModule ? mModule->getChannel(mPairingChannel) : nullptr;
    IoHomecontrolChannel *lProfile = oneWayProfileForChannel(lCh);
    if (!lCh || !lProfile || !lProfile->hasOneWayControllerIdentity())
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxFrame.init();
    mTxFrame.set1WMode();
    mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    mTxFrame.setDestNode(oneWayBroadcastTarget(mPairing1WBroadcastType));
    mTxFrame.setSrcNode(lProfile->getOneWayControllerNodeId());

    // 1W Pair/announce frame matching rspaargaren reference:
    //   cmd=0x2E, data=0x00, sequence[2], hmac[6]
    // HMAC input is cmd + data (2 bytes), sequence is supplied separately.
    const uint16_t lSeq = nextSequence1W(lProfile, true);
    build1WPairAuth(mTxFrame, lSeq);
    uint8_t lHmacInput[2] = {static_cast<uint8_t>(mTxFrame.commandId), 0x00};
    if (!createAndTraceHmac1W(lHmacInput, sizeof(lHmacInput), lSeq, lProfile->getOneWayControllerKey(), mTxFrame.hmac))
    {
        mState = ControllerState::PairFailed;
        return;
    }
    mTxFrame.hasHmac = true;

    mTxLen = mTxFrame.serialize1W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen == 0)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    if (mPairDiagnosticTraceEnabled)
    {
        tracePairDiagnosticCompactPair();
        const std::string lHex = hexDump(mTxBuffer, mTxLen);
        logInfoP("PairDiag: 1W mode=%s announce tx cmd=%s(0x%02X) len=%u seq=%u src=0x%06X dst=0x%06X hex=%s",
                 pairing1WModeName(mPairing1WMode),
                 commandName(mTxFrame.commandId),
                 static_cast<unsigned>(static_cast<uint8_t>(mTxFrame.commandId)),
                 static_cast<unsigned>(mTxLen),
                 static_cast<unsigned>(lSeq),
                 mTxFrame.getSrcNodeId(),
                 mTxFrame.getDestNodeId(),
                 lHex.c_str());
        trace1WRepeatPlan("1w announce");
    }

    const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
    if (lErr == RadioError::None)
    {
        mStateTimer = millis();
        mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
        mTx1WRepeatTimer = 0;
        mTx1WHopFrequencies = false; // pairing announce stays on the fixed CH2
        // Arm the live 1W TX IRQ trace for the upcoming announce wait.
        mPairDiag1WTxPollTimer = 0;
        mPairDiag1WTxLastIrq = 0xFFFF;
        mPairDiag1WTxSampleCount = 0;
        mState = ControllerState::PairWait1WAnnounce;
    }
    else if (lErr != RadioError::Busy)
    {
        mState = ControllerState::PairFailed;
    }
}

void IoHomeController::processPairWait1WAnnounce()
{
    const ControllerState lNextState = (mPairing1WMode == Pairing1WMode::AnnounceOnly)
                                           ? ControllerState::PairComplete
                                           : ControllerState::PairSend1WKeyTransfer;
    if (!processPairWait1WBlind(lNextState))
        return;

    if (mState == ControllerState::PairSend1WKeyTransfer)
    {
        mPairing1WStage = 1;
    }
    else if (mState == ControllerState::PairComplete)
    {
        logInfoP("Pairing: 1W mode=%s complete for channel %d (0x30 SendKey not sent)",
                 pairing1WModeName(mPairing1WMode),
                 mPairingChannel + 1);
    }
}

void IoHomeController::processPairSend1WRemove()
{
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mState = ControllerState::PairFailed;
        return;
    }

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

    IoHomecontrolChannel *lCh = mModule ? mModule->getChannel(mPairingChannel) : nullptr;
    IoHomecontrolChannel *lProfile = oneWayProfileForChannel(lCh);
    if (!lCh || !lProfile || !lProfile->hasOneWayControllerIdentity())
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxFrame.init();
    mTxFrame.set1WMode();
    mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    mTxFrame.setDestNode(oneWayBroadcastTarget(mPairing1WBroadcastType));
    mTxFrame.setSrcNode(lProfile->getOneWayControllerNodeId());

    // Explicit 1W Remove frame only:
    //   cmd=0x39, data=0x00, sequence[2], hmac[6]
    // This state is never entered by the normal add/learn flow.
    const uint16_t lSeq = nextSequence1W(lProfile, true);
    build1WRemove(mTxFrame, lSeq);
    uint8_t lHmacInput[2] = {static_cast<uint8_t>(mTxFrame.commandId), 0x00};
    if (!createAndTraceHmac1W(lHmacInput, sizeof(lHmacInput), lSeq, lProfile->getOneWayControllerKey(), mTxFrame.hmac))
    {
        mState = ControllerState::PairFailed;
        return;
    }
    mTxFrame.hasHmac = true;

    mTxLen = mTxFrame.serialize1W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen == 0)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    if (mPairDiagnosticTraceEnabled)
    {
        tracePairDiagnosticCompactPair();
        const std::string lHex = hexDump(mTxBuffer, mTxLen);
        logInfoP("PairDiag: 1W mode=%s remove tx cmd=%s(0x%02X) len=%u seq=%u src=0x%06X dst=0x%06X hex=%s",
                 pairing1WModeName(mPairing1WMode),
                 commandName(mTxFrame.commandId),
                 static_cast<unsigned>(static_cast<uint8_t>(mTxFrame.commandId)),
                 static_cast<unsigned>(mTxLen),
                 static_cast<unsigned>(lSeq),
                 mTxFrame.getSrcNodeId(),
                 mTxFrame.getDestNodeId(),
                 lHex.c_str());
        trace1WRepeatPlan("1w remove");
    }

    const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
    if (lErr == RadioError::None)
    {
        mStateTimer = millis();
        mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
        mTx1WRepeatTimer = 0;
        mTx1WHopFrequencies = false; // pairing remove stays on the fixed CH2
        mState = ControllerState::PairWait1WRemove;
    }
    else if (lErr != RadioError::Busy)
    {
        mState = ControllerState::PairFailed;
    }
}

void IoHomeController::processPairWait1WRemove()
{
    if (!processPairWait1WBlind(ControllerState::PairComplete))
        return;

    if (mState == ControllerState::PairComplete && mModule)
    {
        IoHomecontrolChannel *lCh = mModule->getChannel(mPairingChannel);
        if (lCh)
        {
            lCh->setNodeId(0);
            openknx.flash.save(true); // pairing is rare & critical: bypass write throttle
            logInfoP("Pairing: 1W mode=%s complete for channel %d (no 0x30 key transfer follows)",
                     pairing1WModeName(mPairing1WMode),
                     mPairingChannel + 1);
        }
    }
}

void IoHomeController::processPairSend1WKeyTransfer()
{
    if (millis() - mPairingStartTime > IOHC_PAIR_TIMEOUT_MS)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    IoHomecontrolChannel *lCh = mModule ? mModule->getChannel(mPairingChannel) : nullptr;
    IoHomecontrolChannel *lProfile = oneWayProfileForChannel(lCh);
    if (!lCh || !lProfile || !lProfile->hasOneWayControllerIdentity())
    {
        mState = ControllerState::PairFailed;
        return;
    }

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

    mTxFrame.init();
    mTxFrame.set1WMode();
    mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    mTxFrame.commandId = IoHomeCommand::SendKey1W;

    // In 1W mode we emulate a handheld remote. The address in the 0x30 frame
    // is the REMOTE/controller address, not the actuator address.
    // Frame shape: <ctrl> <type broadcast> <remoteNode> 30 <encryptedKey> <manufacturer> 01 <seq>.
    // The 1W key encryption IV also uses that remote/controller address repeated
    // to 16 bytes. The actuator then stores "remoteNode + key" and accepts later
    // 1W commands from this source address.
    uint32_t lRemoteNodeId = lProfile->getOneWayControllerNodeId();
    if (lRemoteNodeId == 0 || (mDiscoveredNodeId != 0 && lRemoteNodeId == mDiscoveredNodeId))
    {
        logInfoP("Pairing: refusing 1W SendKey1W because remote/controller node is invalid remote=0x%06X device=0x%06X",
                 lRemoteNodeId,
                 mDiscoveredNodeId);
        mState = ControllerState::PairFailed;
        return;
    }

    uint8_t lRemoteNodeAddr[3];
    lRemoteNodeAddr[0] = (lRemoteNodeId >> 16) & 0xFF;
    lRemoteNodeAddr[1] = (lRemoteNodeId >> 8) & 0xFF;
    lRemoteNodeAddr[2] = lRemoteNodeId & 0xFF;

    mTxFrame.setSrcNode(lRemoteNodeId);
    mTxFrame.setDestNode(oneWayBroadcastTarget(mPairing1WBroadcastType));

    uint8_t lEncryptedKey[16];
    if (!IoHomeCrypto::encrypt1WKey(lProfile->getOneWayControllerKey(), IOHC_TRANSFER_KEY, lRemoteNodeAddr, lEncryptedKey))
    {
        mState = ControllerState::PairFailed;
        return;
    }

    if (mPairDiagnosticTraceEnabled)
    {
        const std::string lEncKeyHex = hexDump(lEncryptedKey, sizeof(lEncryptedKey));
        logInfoP("1w key: remote=0x%06X device=0x%06X ivAddr=remote encKey=%s",
                 lRemoteNodeId,
                 mDiscoveredNodeId,
                 lEncKeyHex.c_str());
    }

    uint16_t lSeq = nextSequence1W(lProfile, true);
    if (!build1WSendKey(mTxFrame, lEncryptedKey, lProfile->getOneWayControllerManufacturer(), lSeq))
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxLen = mTxFrame.serialize1W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        if (mPairDiagnosticTraceEnabled)
        {
            tracePairDiagnosticCompactPair();
            logInfoP("PairDiag: mode=%s tx1w cmd=0x30 len=%u hasHmac=%u seq=%u remote=0x%06X device=0x%06X src=0x%06X dst=0x%06X type=%u mfg=0x%02X freq=%u %luHz",
                     pairing1WModeName(mPairing1WMode),
                     static_cast<unsigned>(mTxLen),
                     mTxFrame.hasHmac ? 1U : 0U,
                     static_cast<unsigned>(lSeq),
                     lRemoteNodeId,
                     mDiscoveredNodeId,
                     mTxFrame.getSrcNodeId(),
                     mTxFrame.getDestNodeId(),
                     static_cast<unsigned>(mPairing1WBroadcastType),
                     static_cast<unsigned>(lProfile->getOneWayControllerManufacturer()),
                     static_cast<unsigned>(mCurrentFreqIdx),
                     static_cast<unsigned long>(IOHC_FREQ_2));
            trace1WRepeatPlan("1w key");
            const std::string lHex = hexDump(mTxBuffer, mTxLen);
            logInfoP("PairDiag: 1W key tx hex=%s", lHex.c_str());
        }

        const RadioError lErr = mRadio.startTransmit(mTxBuffer, mTxLen);
        if (lErr == RadioError::None)
        {
            mStateTimer = millis();
            mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
            mTx1WRepeatTimer = 0;
            mTx1WHopFrequencies = false; // pairing key transfer stays on the fixed CH2
            // Arm the live 0x30 TX IRQ trace for the upcoming wait.
            mPairDiag1WTxPollTimer = 0;
            mPairDiag1WTxLastIrq = 0xFFFF;
            mPairDiag1WTxSampleCount = 0;
            mState = ControllerState::PairWait1WKeyTransfer;
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
            IoHomecontrolChannel *lProfile = oneWayProfileForChannel(lCh);
            if (mDiscoveredNodeId != 0)
                lCh->setNodeId(mDiscoveredNodeId);
            else
                lCh->setConfigured1WTargetNodeId(0);
            if (lProfile)
                lCh->setEncryptionKey(lProfile->getOneWayControllerKey());
            openknx.flash.save(true); // pairing is rare & critical: bypass write throttle
            if (mDiscoveredNodeId != 0)
                logInfoP("Pairing: 1W mode=%s complete for 0x%06X on channel %d (no device ACK in 1W mode)",
                         pairing1WModeName(mPairing1WMode),
                         mDiscoveredNodeId, mPairingChannel + 1);
            else
                logInfoP("Pairing: 1W mode=%s broadcast profile sent on channel %d without bound target node (no device ACK in 1W mode)",
                         pairing1WModeName(mPairing1WMode),
                         mPairingChannel + 1);
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

        // Reference 1W timing: the first TX uses the long wake-up preamble,
        // then all three repeats use the short preamble with a 40 ms gap.
        RadioError lErr = startShortPreambleTransmit(mTxBuffer, mTxLen);
        if (mPairDiagnosticTraceEnabled && lErr == RadioError::None)
        {
            logInfoP("PairDiag: 1W pair repeat tx state=%s remaining=%u len=%u pre=%u",
                     stateName(mState),
                     static_cast<unsigned>(mTx1WRepeatRemaining),
                     static_cast<unsigned>(mTxLen),
                     static_cast<unsigned>(IOHC_PREAMBLE_SHORT));
        }

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

    bool lTxDone = mRadio.isTxDone();
#if defined(RADIO_SX1262)
    // SX1262 may need one blocking IRQ/status read to complete the TX_DONE cleanup
    // before the controller can advance the blind 1W learn flow.
    if (!lTxDone)
        lTxDone = mRadio.isTxDoneBlocking();
#endif
    if (lTxDone)
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

    // Live IRQ/FIFO trace while a 1W blind TX (announce 0x2E / key 0x30 / remove
    // 0x39) is in flight. These frames intermittently stall (PacketSent never
    // asserts, the FIFO never drains) - notably under a heavy concurrent RX storm
    // from a device broadcasting 0x2E. Edge-log the SX1276 FSK status registers so
    // the on-hardware behaviour is visible. These are SX1276 FSK register
    // addresses/bits; on other radios debugReadRegister() returns 0, so the trace
    // is simply inert. This function only runs during the 1W blind waits.
    if (mPairDiagnosticTraceEnabled &&
        mTx1WRepeatTimer == 0 &&
        mPairDiag1WTxSampleCount < 32 &&
        (mPairDiag1WTxPollTimer == 0 || millis() - mPairDiag1WTxPollTimer >= 25))
    {
        constexpr uint8_t kRegOpMode = 0x01;
        constexpr uint8_t kRegIrqFlags1 = 0x3E;
        constexpr uint8_t kRegIrqFlags2 = 0x3F;
        constexpr uint8_t kIrq2FifoEmpty = 0x40;
        constexpr uint8_t kIrq2FifoLevel = 0x20;
        constexpr uint8_t kIrq2PacketSent = 0x08;
        // Command byte position in the serialized frame: ctrl0,ctrl1,dest[3],src[3],cmd.
        constexpr uint8_t kCmdOffset = 8;
        // TX-critical config registers (to catch a clobbered modem config).
        constexpr uint8_t kRegBitrateMsb = 0x02;
        constexpr uint8_t kRegBitrateLsb = 0x03;
        constexpr uint8_t kRegPayloadLength = 0x32;
        constexpr uint8_t kRegFifoThresh = 0x35;
        constexpr uint8_t kRegPacketConfig2 = 0x3D;

        mPairDiag1WTxPollTimer = millis();
        const uint8_t lIrq1 = mRadio.debugReadRegister(kRegIrqFlags1);
        const uint8_t lIrq2 = mRadio.debugReadRegister(kRegIrqFlags2);
        const uint16_t lIrq = (static_cast<uint16_t>(lIrq1) << 8) | lIrq2;
        if (lIrq != mPairDiag1WTxLastIrq)
        {
            mPairDiag1WTxLastIrq = lIrq;
            const uint8_t lOp = mRadio.debugReadRegister(kRegOpMode);
            const uint8_t lCmd = (mTxLen > kCmdOffset) ? mTxBuffer[kCmdOffset] : 0;
            const uint16_t lBitrate = (static_cast<uint16_t>(mRadio.debugReadRegister(kRegBitrateMsb)) << 8) |
                                      mRadio.debugReadRegister(kRegBitrateLsb);
            logInfoP("PairDiag: 1W tx poll state=%s cmd=0x%02X t=%lums op=0x%02X txSt=0x%02X irq1=0x%02X irq2=0x%02X (FifoEmpty=%u FifoLevel=%u PacketSent=%u) len=%u byte0=0x%02X br=0x%04X payLen=0x%02X fifoThr=0x%02X pc2=0x%02X",
                     stateName(mState),
                     static_cast<unsigned>(lCmd),
                     static_cast<unsigned long>(millis() - mStateTimer),
                     static_cast<unsigned>(lOp),
                     static_cast<unsigned>(mRadio.lastTxSetStatus()),
                     static_cast<unsigned>(lIrq1),
                     static_cast<unsigned>(lIrq2),
                     (lIrq2 & kIrq2FifoEmpty) ? 1U : 0U,
                     (lIrq2 & kIrq2FifoLevel) ? 1U : 0U,
                     (lIrq2 & kIrq2PacketSent) ? 1U : 0U,
                     static_cast<unsigned>(mTxLen),
                     static_cast<unsigned>(mTxBuffer[0]),
                     static_cast<unsigned>(lBitrate),
                     static_cast<unsigned>(mRadio.debugReadRegister(kRegPayloadLength)),
                     static_cast<unsigned>(mRadio.debugReadRegister(kRegFifoThresh)),
                     static_cast<unsigned>(mRadio.debugReadRegister(kRegPacketConfig2)));
            mPairDiag1WTxSampleCount++;
        }
    }

    const uint32_t lTxTimeoutMs = currentTxTimeoutMs();
    if (millis() - mStateTimer > lTxTimeoutMs)
    {
        if (mPairDiagnosticTraceEnabled && mState == ControllerState::PairWait1WKeyTransfer)
        {
            const IoHomeRadioHealth lHealth = radioHealth();
            logInfoP("PairDiag: 1W key tx timeout len=%u pre=%u timeout=%lums txS=%lu txD=%lu irq=%lu lastIrq=0x%04X txIrq=0x%04X txSt=0x%02X radioState=%d",
                     static_cast<unsigned>(mTxLen),
                     static_cast<unsigned>(mCurrentTxPreambleSymbols),
                     static_cast<unsigned long>(lTxTimeoutMs),
                     static_cast<unsigned long>(lHealth.txStartCount),
                     static_cast<unsigned long>(lHealth.txDoneCount),
                     static_cast<unsigned long>(lHealth.irqCount),
                     lHealth.lastIrqStatus,
                     lHealth.lastTxIrqImmediate,
                     static_cast<unsigned>(lHealth.lastTxSetStatus),
                     static_cast<int>(lHealth.radioState));
            logPairDiagnosticStatus();
        }

        mRadio.standby();
        mState = ControllerState::PairFailed;
        return true;
    }

    return false;
}

void IoHomeController::processPairSendKeyInit()
{
    // Send KeyInitTransfer (0x31) to discovered device to begin key exchange.
    // Reference-compatible 2W pairing uses an initial START frame with the
    // LOW_POWER bit set and a long preamble so low-power actuators wake up
    // before answering with ChallengeRequest (0x3C).
    if (!build2WKeyInit(mTxFrame, mOwnNodeId, mDiscoveredNodeId))
    {
        mState = ControllerState::PairFailed;
        return;
    }

    const RadioError lPrepErr = configureNormal2WTxRadio(IOHC_PREAMBLE_LONG);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxLen = mTxFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        tracePairDiagnosticTx2W(mTxFrame, IOHC_PREAMBLE_LONG);
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

    serviceRxScan();

    if (millis() - mStateTimer > 5000) // 5s timeout for device challenge
    {
        mState = ControllerState::PairFailed;
    }
    // ChallengeRequest response is handled in main loop RX dispatch
}

void IoHomeController::processPairSendKeyTransfer()
{
    // KeyTransfer (0x32) is the continuation frame after 0x31. The centralized
    // builder keeps START/END and LOW_POWER clear and encrypts the system key
    // using the 0x31-only key-transfer transcript.
    if (!build2WKeyTransfer(mTxFrame, mOwnNodeId, mDiscoveredNodeId,
                            mPairingChallenge, mSystemKey))
    {
        mState = ControllerState::PairFailed;
        return;
    }

    mTxLen = mTxFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lPrepErr = configureNormal2WTxRadio(IOHC_PREAMBLE_SHORT);
        if (lPrepErr == RadioError::Busy)
            return;
        if (lPrepErr != RadioError::None)
        {
            mState = ControllerState::PairFailed;
            return;
        }
        tracePairDiagnosticTx2W(mTxFrame, IOHC_PREAMBLE_SHORT);
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
    // 2W ChallengeResponse (0x3D) is an authenticated continuation response.
    // Keep LOW_POWER clear and use the short-preamble helper below; this avoids
    // changing normal 2W auth TX timing and does not affect the separate 1W path.
    if (!build2WChallengeResponse(lFrame, mOwnNodeId, mDiscoveredNodeId, false,
                                  mTxFrame, mPairKeyTransferChallenge, mSystemKey))
    {
        mState = ControllerState::PairFailed;
        return;
    }

    lFrame.dataLen = IOHC_HMAC_SIZE;
    lFrame.hasHmac = false;

    mTxLen = lFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lPrepErr = configureNormal2WTxRadio(IOHC_PREAMBLE_SHORT);
        if (lPrepErr == RadioError::Busy)
            return;
        if (lPrepErr != RadioError::None)
        {
            mState = ControllerState::PairFailed;
            return;
        }
        tracePairDiagnosticTx2W(lFrame, IOHC_PREAMBLE_SHORT);
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

    serviceRxScan();

    if (millis() - mStateTimer > 5000) // 5s timeout
    {
        mState = ControllerState::PairFailed;
    }
    // Confirmation response and channel storage handled in main loop RX dispatch
}

void IoHomeController::processPairSendSetConfig1()
{
    if (!build2WSetConfig1(mTxFrame, mOwnNodeId, mDiscoveredNodeId))
    {
        logDebugP("Pairing: failed to build SetConfig1 for 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
        return;
    }

    mPairSetConfigRequest = mTxFrame;

    if (mPairDiagnosticTraceEnabled)
    {
        const std::string lPayloadHex = hexDump(mPairSetConfigRequest.data, mPairSetConfigRequest.dataLen);
        logInfoP("PairDiag: SetConfig1 payload=%s", lPayloadHex.c_str());
    }

    mTxLen = mPairSetConfigRequest.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen == 0)
    {
        logDebugP("Pairing: failed to serialize SetConfig1 for 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
        return;
    }

    const RadioError lPrepErr = configureNormal2WTxRadio(IOHC_PREAMBLE_LONG);
    if (lPrepErr == RadioError::Busy)
        return;
    if (lPrepErr != RadioError::None)
    {
        logDebugP("Pairing: failed to send SetConfig1 to 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
        return;
    }

    tracePairDiagnosticTx2W(mPairSetConfigRequest, IOHC_PREAMBLE_LONG);
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

    serviceRxScan();

    if (millis() - mStateTimer > 2000)
    {
        logDebugP("Pairing: SetConfig1 timed out for 0x%06X", mDiscoveredNodeId);
        mState = ControllerState::PairComplete;
    }
}

void IoHomeController::interpretSetConfig1Result(bool iFinalResponse)
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
        logInfoP(iFinalResponse ? "Pairing: device 0x%06X rejected automatic status feedback"
                                : "Pairing: device 0x%06X does not support automatic status feedback",
                 mDiscoveredNodeId);
    }
    else
    {
        logDebugP(iFinalResponse ? "Pairing: unexpected final SetConfig1 response 0x%02X from 0x%06X"
                                 : "Pairing: unexpected SetConfig1 response 0x%02X from 0x%06X",
                  static_cast<uint8_t>(mRxFrame.commandId), mDiscoveredNodeId);
    }

    mState = ControllerState::PairComplete;
}

void IoHomeController::processPairSendSetConfig1AuthResponse()
{
    IoHomeFrame lFrame;

    uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA];
    const size_t lHmacInputLen = buildHmacInput(mPairSetConfigRequest, lHmacInput, sizeof(lHmacInput));

    if (mPairDiagnosticTraceEnabled && lHmacInputLen > 0)
    {
        const std::string lTranscriptHex = hexDump(lHmacInput, static_cast<uint8_t>(lHmacInputLen));
        logInfoP("PairDiag: SetConfig1 auth transcript=%s", lTranscriptHex.c_str());
    }

    if (!build2WChallengeResponse(lFrame, mOwnNodeId, mDiscoveredNodeId, true,
                                  mPairSetConfigRequest, mPairSetConfigChallenge, mSystemKey))
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

    mTxLen = lFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        tracePairDiagnosticTx2W(lFrame, IOHC_PREAMBLE_SHORT);
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

    serviceRxScan();

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

        mTxLen = mTxFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
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

    mTxLen = mTxFrame.serialize2W(mTxBuffer, sizeof(mTxBuffer));
    if (mTxLen > 0)
    {
        const RadioError lErr = startShortPreambleTransmit(mTxBuffer, mTxLen);
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
        bool lParsed = (lLen > 0 && mRxFrame.deserialize(mRxBuffer, lLen));
        if (lLen > 0 && !lParsed)
            mRxParseFailCount++;
        if (lParsed)
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

uint32_t IoHomeController::currentTxTimeoutMs() const
{
    // The old fixed 500 ms timeout is enough for short preambles and small
    // frames, but the measured SX1276 timing for 1024-symbol io-homecontrol
    // preambles can be well above 500 ms. Keep normal traffic fast, but allow
    // enough headroom for long-preamble pairing/learn frames.
    if (mCurrentTxPreambleSymbols >= IOHC_PREAMBLE_LONG)
        return 1500UL;
    if (mCurrentTxPreambleSymbols >= 512)
        return 900UL;
    if (mCurrentTxPreambleSymbols >= 256)
        return 700UL;
    return IOHC_TX_TIMEOUT_MS;
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

    const RadioError lPreambleErr = mRadio.setPreambleLength(iPreambleSymbols);
    if (lPreambleErr == RadioError::None)
        mCurrentTxPreambleSymbols = iPreambleSymbols;
    return lPreambleErr;
}

bool IoHomeController::waitForLbtClear(LbtContext iContext)
{
    mLastLbtRssi = 0;
    mLastLbtRssiValid = false;
    mLastLbtAttempts = 0;
    mLastLbtBypassed = false;
    mLastLbtAuthResponse = (iContext == LbtContext::AuthResponse);

    if (iContext == LbtContext::Bypass)
    {
        mLbtBypassCount++;
        mLastLbtBypassed = true;
        return true;
    }

    const uint8_t lMaxAttempts = (iContext == LbtContext::AuthResponse) ? 1 : 5;

    for (uint8_t i = 0; i < lMaxAttempts; i++)
    {
        int16_t lRssi = 0;
        mLastLbtAttempts = static_cast<uint8_t>(i + 1);

        if (!mRadio.currentRssi(lRssi))
        {
            mLbtInvalidRssiCount++;
            if (mPairDiagnosticTraceEnabled)
                logInfoP("PairDiag: LBT RSSI unavailable before TX, bypassing carrier-sense");
            return true;
        }

        mLastLbtRssi = lRssi;
        mLastLbtRssiValid = true;

        if (lRssi <= IOHC_LBT_RSSI_THRESHOLD_DBM)
        {
            mLbtClearCount++;
            return true;
        }

        mLbtBusyCount++;
        if (i + 1 < lMaxAttempts)
            delay(5);
    }

    // IOHC timing is tight, especially for 0x3D auth responses.  Treat LBT as
    // diagnostic/best-effort here: record the busy channel but do not deadlock
    // the protocol state machine or native tests when RSSI is high/stubbed.
    mLbtBypassCount++;
    mLastLbtBypassed = true;
    if (mPairDiagnosticTraceEnabled)
    {
        logInfoP("PairDiag: LBT busy before TX rssi=%d attempts=%u auth=%d - bypassing to preserve IOHC timing",
                 mLastLbtRssi,
                 static_cast<unsigned>(mLastLbtAttempts),
                 mLastLbtAuthResponse ? 1 : 0);
    }
    return true;
}

RadioError IoHomeController::startRadioTransmit(const uint8_t *iBuffer, uint8_t iLen, LbtContext iLbtContext)
{
    if (!waitForLbtClear(iLbtContext))
        return RadioError::Busy;

    return mRadio.startTransmit(iBuffer, iLen);
}

RadioError IoHomeController::startShortPreambleTransmit(const uint8_t *iBuffer, uint8_t iLen,
                                                        bool iTrackDutyCycle,
                                                        LbtContext iLbtContext)
{
    return startTransmitWithPreamble(iBuffer, iLen, IOHC_PREAMBLE_SHORT, iTrackDutyCycle, iLbtContext);
}

uint16_t IoHomeController::authResponsePreamble() const
{
#if defined(RADIO_SX1262) || defined(TEST_NATIVE)
    return 64;
#else
    return IOHC_PREAMBLE_SHORT;
#endif
}

RadioError IoHomeController::startTransmitWithPreamble(const uint8_t *iBuffer, uint8_t iLen,
                                                       uint16_t iPreambleSymbols,
                                                       bool iTrackDutyCycle,
                                                       LbtContext iLbtContext)
{
    const RadioError lPrepErr = configureTxRadio(iPreambleSymbols);
    if (lPrepErr != RadioError::None)
        return lPrepErr;

    const RadioError lTxErr = startRadioTransmit(iBuffer, iLen, iLbtContext);
    if (lTxErr == RadioError::None && iTrackDutyCycle)
        mTxTimeAccum[mCurrentFreqIdx] += ((uint32_t)iLen * 8 * 1000) / IOHC_BITRATE;

    return lTxErr;
}

RadioError IoHomeController::ensureReceiveAfterTransmit()
{
    if (mRadio.state() == RadioState::Transmitting && !mRadio.isTxDone())
        return RadioError::Busy;

    if (mRadio.state() == RadioState::Receiving)
        return RadioError::None;

    const RadioError lRxErr = mRadio.startReceive();
    if (lRxErr == RadioError::None)
        mRxScanLastSwitch = micros();
    return lRxErr;
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

bool IoHomeController::buildTxFrame(const IoHomeQueueEntry &iEntry)
{
    mTxFrame.init();
    // A retry after no 2W response is a fresh attempt at the same request,
    // not the next frame in a multi-frame exchange. Keep the original
    // controller-originated request order independent of retry count so START
    // frames keep using the long preamble on every retry. True continuation
    // frames such as 0x32 KeyTransfer and 0x3D ChallengeResponse are built by
    // their dedicated protocol helpers and bypass this queued-command path.
    mTxFrame.setStart2W();
    mTxFrame.setSrcNode(mOwnNodeId);
    mTxFrame.setDestNode(iEntry.destNodeId);
    mTxFrame.setLowPower(resolveLowPower2W(iEntry.destNodeId));
    mTxFrame.commandId = iEntry.command;

    switch (iEntry.command)
    {
    case IoHomeCommand::Execute:
    {
        // Check if the command was queued for a 1W channel. 1W virtual
        // remote profiles may have target node 0, so do not depend solely on
        // node-id lookup or isPaired() here.
        IoHomecontrolChannel *lTargetCh = channelForQueueEntry(iEntry);

        if (lTargetCh && lTargetCh->is1W())
        {
            IoHomecontrolChannel *lProfile = oneWayProfileForChannel(lTargetCh);
            if (!lProfile || !lProfile->hasOneWayControllerIdentity())
                return false;
            const uint8_t *lProfileKey = lProfile->getOneWayControllerKey();
            mTxFrame.setSrcNode(lProfile->getOneWayControllerNodeId());
            mTxFrame.setDestNode(oneWayDestinationForEntry(iEntry));
            mTxFrame.set1WMode();
            mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END); // START+END both set

            mTxFrame.data[0] = IOHC_ORIGINATOR_USER; // 0x01
            mTxFrame.data[1] = IOHC_ACEI_1W;         // 0x43

            if (iEntry.oneWayStandardExecute)
            {
                // Standard 1W Execute:
                // payload before sequence/HMAC = 01 43 main[2] fp1 fp2.
                // main[2] is raw IOHC closedness percent, not UI open percent:
                //   rawClosed=0   -> 0000 (fully open)
                //   rawClosed=50  -> 6400
                //   rawClosed=100 -> C800 (fully closed)
                //   STOP          -> D200
                uint8_t lRawClosedPercent = 0;
                if (mPairDiagnosticTraceEnabled && oneWayMainToRawClosedPercent(iEntry.oneWayMain, lRawClosedPercent))
                {
                    const uint8_t lUiOpenPercent = static_cast<uint8_t>(100U - lRawClosedPercent);
                    logInfoP("1w pos: uiOpen=%u rawClosed=%u main=%04X",
                             static_cast<unsigned>(lUiOpenPercent),
                             static_cast<unsigned>(lRawClosedPercent),
                             static_cast<unsigned>(iEntry.oneWayMain));
                }

                uint16_t lSeq = nextSequence1W(lProfile, false);
                OneWayCommandProfile lProfileTemplate = oneWayCommandProfileForType(iEntry.oneWayBroadcastType);
                lProfileTemplate.acei = iEntry.oneWayAcei ? iEntry.oneWayAcei : lProfileTemplate.acei;
                if (!build1WExecute(mTxFrame, lProfileTemplate, iEntry.oneWayMain,
                                    iEntry.oneWayFp1, iEntry.oneWayFp2, lSeq))
                    return false;

                uint8_t lHmacIn[7];
                lHmacIn[0] = static_cast<uint8_t>(mTxFrame.commandId);
                memcpy(lHmacIn + 1, mTxFrame.data, 6);
                if (!createAndTraceHmac1W(lHmacIn, sizeof(lHmacIn), lSeq, lProfileKey, mTxFrame.hmac))
                    return false;
                mTxFrame.hasHmac = true;
            }
            else if (iEntry.oneWayRawExecute)
            {
                // Exact raw 1W Execute payload. The payload is copied as-is and
                // the controller appends sequence + HMAC. Use this for reference
                // comparison and command discovery, e.g. 0000/00FE/0143....
                memcpy(mTxFrame.data, iEntry.oneWayRawData, iEntry.oneWayRawLen);

                uint16_t lSeq = nextSequence1W(lProfile, false);
                mTxFrame.data[iEntry.oneWayRawLen] = (lSeq >> 8) & 0xFF;
                mTxFrame.data[iEntry.oneWayRawLen + 1] = lSeq & 0xFF;
                mTxFrame.dataLen = iEntry.oneWayRawLen + 2;

                uint8_t lHmacIn[1 + IOHC_1W_RAW_EXEC_MAX_DATA];
                lHmacIn[0] = static_cast<uint8_t>(mTxFrame.commandId);
                memcpy(lHmacIn + 1, mTxFrame.data, iEntry.oneWayRawLen);
                if (!createAndTraceHmac1W(lHmacIn, 1 + iEntry.oneWayRawLen, lSeq, lProfileKey, mTxFrame.hmac))
                    return false;
                mTxFrame.hasHmac = true;
            }
            else if (iEntry.oneWayButton)
            {
                // Raw 1W remote-style button command.
                // Known values from 1W remotes:
                //   0x0000 = Up, 0x0001 = Down, 0x0002 = Stop, 0x0003 = My/Prog
                //   0x00FE = Button Released, 0x00FF = alternative Stop
                // Payload before sequence/HMAC:
                //   origin(1) + acei(1) + buttonCode[2] + fp1(0) + fp2(0)
                uint16_t lSeq = nextSequence1W(lProfile, false);
                OneWayCommandProfile lProfileTemplate = oneWayCommandProfileForType(iEntry.oneWayBroadcastType);
                lProfileTemplate.acei = iEntry.oneWayAcei ? iEntry.oneWayAcei : lProfileTemplate.acei;
                if (!build1WExecute(mTxFrame, lProfileTemplate, iEntry.oneWayButtonCode, 0x00, 0x00, lSeq))
                    return false;

                // 1W HMAC input excludes the appended sequence bytes and HMAC.
                uint8_t lHmacIn[7];
                lHmacIn[0] = static_cast<uint8_t>(mTxFrame.commandId);
                memcpy(lHmacIn + 1, mTxFrame.data, 6);
                if (!createAndTraceHmac1W(lHmacIn, sizeof(lHmacIn), lSeq, lProfileKey, mTxFrame.hmac))
                    return false;
                mTxFrame.hasHmac = true;
            }
            else if (iEntry.param3 != 0xFF)
            {
                // Extended 16-byte format (_p0x00_16):
                // origin(1)+acei(1)+main[2]+fp1(1)+fp2(1)+data[2]+seq(2)+hmac(6) = 16B
                uint16_t lSeq = nextSequence1W(lProfile, false);
                OneWayCommandProfile lProfileTemplate = oneWayCommandProfileForType(iEntry.oneWayBroadcastType);
                lProfileTemplate.acei = iEntry.oneWayAcei ? iEntry.oneWayAcei : lProfileTemplate.acei;
                if (!build1WExecute16(mTxFrame, lProfileTemplate,
                                      static_cast<uint16_t>(iEntry.param) << 8,
                                      iEntry.param2, iEntry.param3, 0x00, 0x00, lSeq))
                    return false;

                // HMAC input: cmd(1) + origin+acei+main[2]+fp1+fp2+data[2] = 9 bytes
                uint8_t lHmacIn[9];
                lHmacIn[0] = static_cast<uint8_t>(mTxFrame.commandId);
                memcpy(lHmacIn + 1, mTxFrame.data, 8);
                if (!createAndTraceHmac1W(lHmacIn, 9, lSeq, lProfileKey, mTxFrame.hmac))
                    return false;
                mTxFrame.hasHmac = true;
            }
            else
            {
                // Standard 14-byte format (_p0x00_14):
                // origin(1)+acei(1)+main[2]+fp1(1)+fp2(1)+seq(2)+hmac(6) = 14B
                uint16_t lMain = 0;
                uint8_t lFp1 = 0x00;
                uint8_t lFp2 = 0x00;
                if (iEntry.param <= 100)
                {
                    lMain = rawClosedPercentToOneWayMain(iEntry.param);
                    if (iEntry.param2 != 0xFF)
                    {
                        lFp1 = 0x80;
                        lFp2 = static_cast<uint8_t>(iEntry.param2 * 2U);
                    }
                }
                else
                {
                    lMain = static_cast<uint16_t>(iEntry.param) << 8;
                }

                uint16_t lSeq = nextSequence1W(lProfile, false);
                OneWayCommandProfile lProfileTemplate = oneWayCommandProfileForType(iEntry.oneWayBroadcastType);
                lProfileTemplate.acei = iEntry.oneWayAcei ? iEntry.oneWayAcei : lProfileTemplate.acei;
                if (!build1WExecute(mTxFrame, lProfileTemplate, lMain, lFp1, lFp2, lSeq))
                    return false;

                // 1W HMAC: input = cmd(1) + origin+acei+main[2]+fp1+fp2 = 7 bytes
                uint8_t lHmacIn[7];
                lHmacIn[0] = static_cast<uint8_t>(mTxFrame.commandId);
                memcpy(lHmacIn + 1, mTxFrame.data, 6);
                if (!createAndTraceHmac1W(lHmacIn, 7, lSeq, lProfileKey, mTxFrame.hmac))
                    return false;
                mTxFrame.hasHmac = true;
            }
            mTx1WRepeatRemaining = IOHC_1W_REPEAT_COUNT;
        }
        else
        {
            // 2W Execute: challenge-response authentication. Keep the
            // payload bytes in the central reference-template builders above.
            if (iEntry.twoWayTilt)
            {
                if (!build2WExecuteTiltPayload(iEntry.twoWayTiltPercent, mTxFrame.data, mTxFrame.dataLen))
                    return false;
            }
            else if (iEntry.param <= 100)
            {
                if (!build2WExecutePositionPayload(iEntry.param, mTxFrame.data, mTxFrame.dataLen))
                    return false;
            }
            else
            {
                if (!build2WExecuteSpecialPayload(iEntry.param, mTxFrame.data, mTxFrame.dataLen))
                    return false;
            }

            // 2W Execute: authenticated via challenge-response (per nicolas5000/rspaargaren)
            // Device will respond with ChallengeRequest (0x3C), processResponse() handles 0x3D
            mTxFrame.hasHmac = false;
            mAuthResponseSent = false;
        }
        break;
    }
    case IoHomeCommand::Private:
        // Private reference templates and legacy variants are centralized in
        // build2WPrivatePayload(). Known reference forms:
        //   03 00 00       = status
        //   03 20 01 00    = tilt status
        if (!build2WPrivatePayload(iEntry.param, iEntry.param2, iEntry.param3,
                                   mTxFrame.data, mTxFrame.dataLen))
            return false;
        mTxFrame.hasHmac = false;
        break;

    case IoHomeCommand::SetConfig1:
        // Pairing post-configuration command, built through the same 2W
        // command builder path as normal authenticated 2W commands. The
        // laberning reference sends 0x6F as a START frame with no LOW_POWER
        // flag and authenticates it by answering 0x3C with HMAC over
        // {0x6F, E0, 10, 0A, 08, 00}.
        mTxFrame.setLowPower(false);
        if (!build2WSetConfig1Payload(mTxFrame.data, mTxFrame.dataLen))
            return false;
        mTxFrame.hasHmac = false;
        mAuthResponseSent = false;
        break;

    case IoHomeCommand::Identify:
    {
        IoHomecontrolChannel *lTargetCh = channelForNode(iEntry.destNodeId);
        if (lTargetCh && lTargetCh->is1W())
            return false;

        // Identify (0x1E): authenticated 2W command, challenge-response flow.
        mTxFrame.data[0] = IOHC_ORIGINATOR_USER;
        mTxFrame.data[1] = 0xFF;
        mTxFrame.dataLen = 2;
        mTxFrame.hasHmac = false;
        mAuthResponseSent = false;
        break;
    }

    case IoHomeCommand::ActivateMode:
    {
        // Check if the command was queued for a 1W channel. 1W virtual
        // remote profiles may have target node 0, so do not depend solely on
        // node-id lookup or isPaired() here.
        IoHomecontrolChannel *lTargetChAM = channelForQueueEntry(iEntry);

        if (lTargetChAM && lTargetChAM->is1W())
        {
            IoHomecontrolChannel *lProfile = oneWayProfileForChannel(lTargetChAM);
            if (!lProfile || !lProfile->hasOneWayControllerIdentity())
                return false;
            mTxFrame.setSrcNode(lProfile->getOneWayControllerNodeId());
            // 1W ActivateMode (_p0x01_13): origin(1)+acei(1)+main(1)+fp1(1)+fp2(1)+seq(2)+hmac(6) = 13B
            // Note: main is 1 byte (not 2!) in _p0x01_13
            mTxFrame.setDestNode(oneWayDestinationForEntry(iEntry));
            mTxFrame.set1WMode();
            mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);

            uint16_t lSeqAM = nextSequence1W(lProfile, false);
            OneWayCommandProfile lProfileTemplate = oneWayCommandProfileForType(iEntry.oneWayBroadcastType);
            lProfileTemplate.acei = iEntry.oneWayAcei ? iEntry.oneWayAcei : lProfileTemplate.acei;
            const uint8_t lActivateFp1 = (iEntry.param2 != 0xFF) ? iEntry.param2 : 0x01;
            if (!build1WActivateMode13(mTxFrame, lProfileTemplate, iEntry.param, lActivateFp1, 0x00, lSeqAM))
                return false;

            // HMAC input: cmd(1) + origin+acei+main+fp1+fp2 = 6 bytes
            uint8_t lHmacInAM[6];
            lHmacInAM[0] = static_cast<uint8_t>(mTxFrame.commandId); // 0x01
            memcpy(lHmacInAM + 1, mTxFrame.data, 5);
            if (!createAndTraceHmac1W(lHmacInAM, 6, lSeqAM, lProfile->getOneWayControllerKey(), mTxFrame.hmac))
                return false;
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
        IoHomecontrolChannel *lChannel = channelForQueueEntry(iEntry);
        IoHomecontrolChannel *lProfile = lChannel ? oneWayProfileForChannel(lChannel) : oneWayProfileForNode(iEntry.destNodeId);
        if (!lProfile || !lProfile->hasOneWayControllerIdentity())
            return false;
        // 1W key transfer: 1W mode, 20-byte payload
        // Bytes 0-15: encrypted key, byte 16: manufacturer, byte 17: controller marker, bytes 18-19: sequence
        mTxFrame.set1WMode();
        mTxFrame.setFrameOrder(IOHC_CTRL0_ORDER_END); // standalone 1W: START+END (per rspaargaren)

        // Encrypt the key with the transfer key. For 1W key push, the node address
        // is the remote/controller source address, not the actuator address.
        uint8_t lEncKey1W[16];
        uint32_t lRemoteNodeId = lProfile->getOneWayControllerNodeId();
        const uint32_t lDeviceNodeId = iEntry.destNodeId & 0x00FFFFFF;
        if (lRemoteNodeId == 0 || (lDeviceNodeId != 0 && lRemoteNodeId == lDeviceNodeId))
        {
            logInfoP("1W SendKey1W: refusing invalid key IV address remote=0x%06X device=0x%06X",
                     lRemoteNodeId,
                     lDeviceNodeId);
            return false;
        }
        uint8_t lRemoteNodeAddr[3] = {
            static_cast<uint8_t>((lRemoteNodeId >> 16) & 0xFF),
            static_cast<uint8_t>((lRemoteNodeId >> 8) & 0xFF),
            static_cast<uint8_t>(lRemoteNodeId & 0xFF)};
        mTxFrame.setSrcNode(lRemoteNodeId);
        mTxFrame.setDestNode(oneWayDestinationForEntry(iEntry));
        if (!IoHomeCrypto::encrypt1WKey(lProfile->getOneWayControllerKey(), IOHC_TRANSFER_KEY, lRemoteNodeAddr, lEncKey1W))
            return false;
        if (mPairDiagnosticTraceEnabled)
        {
            const std::string lEncKeyHex = hexDump(lEncKey1W, sizeof(lEncKey1W));
            logInfoP("1w key: remote=0x%06X device=0x%06X ivAddr=remote encKey=%s",
                     lRemoteNodeId,
                     lDeviceNodeId,
                     lEncKeyHex.c_str());
        }
        const uint16_t lSequence = (static_cast<uint16_t>((iEntry.param2 != 0xFF) ? iEntry.param2 : 0x00) << 8) |
                                   static_cast<uint16_t>((iEntry.param3 != 0xFF) ? iEntry.param3 : 0x00);
        if (!build1WSendKey(mTxFrame, lEncKey1W, lProfile->getOneWayControllerManufacturer(), lSequence))
            return false;
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
    return true;
}

void IoHomeController::dispatchRxFrame()
{
    if (!mModule)
        return;

    uint32_t lSrcNode = mRxFrame.getSrcNodeId();
    uint32_t lDestNode = mRxFrame.getDestNodeId();

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

    const auto lScheduleStatusPollForDevice = [this](uint32_t iNodeId) -> bool
    {
        IoHomecontrolChannel *lCh = channelForNode(iNodeId);
        if (!lCh || !lCh->isPaired())
            return false;

        lCh->scheduleStatusPoll(2000UL);
        return true;
    };

    if (lSrcNode != mOwnNodeId && lScheduleStatusPollForDevice(lDestNode))
        return;

    const IoHomeRemoteEntry *lRemote = mModule->remoteMap().findRemote(lSrcNode);
    if (lRemote)
    {
        bool lScheduled = false;
        for (uint8_t i = 0; i < lRemote->linkCount; i++)
            lScheduled = lScheduleStatusPollForDevice(lRemote->linkedDevices[i]) || lScheduled;

        if (lScheduled)
            return;
    }

    // Find the channel that matches this source node
    for (uint8_t i = 0; i < IOHC_ChannelCount; i++)
    {
        IoHomecontrolChannel *lCh = mModule->getChannel(i);
        if (lCh && lCh->isPaired() && lCh->getNodeId() == lSrcNode)
        {
            if (!mPassiveMode &&
                mRxFrame.commandId == IoHomeCommand::StatusUpdate &&
                mRxFrame.getDestNodeId() == mOwnNodeId &&
                mState != ControllerState::AuthSendChallenge &&
                mState != ControllerState::AuthWaitResponse)
            {
                mPendingAuthFrame = mRxFrame;
                mAuthSrcNodeId = lSrcNode;
                mAuthChannelIdx = i;
                IoHomeCrypto::generateChallenge(mAuthChallenge);
                mState = ControllerState::AuthSendChallenge;
                return;
            }

            // Point 1 — destination filter: a frame that is not addressed to us
            // cannot be a reply to a 2W challenge we issued. The peer's own 1W
            // Discover/Execute frames are broadcast to group addresses (e.g.
            // 0x0000BF / 0x00037F / 0x00003F), so silently ignore foreign-target
            // authenticated traffic instead of running it through the challenge
            // path and emitting a misleading "rejected" log. Non-authenticated
            // broadcasts still fall through to the command switch below.
            if (mRxFrame.hasHmac && lDestNode != mOwnNodeId)
                break;

            // Point 2 — 1W is one-way and has no challenge-response: a 1W channel's
            // frames are self-authenticated via their embedded sequence number, not
            // via a controller-issued challenge. Never apply the 2W challenge/HMAC
            // gate to a 1W channel; only 2W frames addressed to us reach this block.
            // Verify HMAC on authenticated frames before trusting data.
            if (mRxFrame.hasHmac && !lCh->is1W())
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
                    logInfoP("HMAC frame rejected: no pending challenge for node 0x%06X (cmd=%s/0x%02X dst=0x%06X)",
                             lSrcNode, commandName(mRxFrame.commandId),
                             static_cast<unsigned>(mRxFrame.commandId), lDestNode);
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
                if (mRxFrame.dataLen >= 11)
                {
                    const bool lStopped = (mRxFrame.data[0] & 0x01) != 0;
                    dispatchPositionStatus(lCh, mRxFrame.data, mRxFrame.dataLen, lStopped, 5, 7);
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
                if (mRxFrame.dataLen >= 11)
                {
                    const bool lStopped = (mRxFrame.data[0] & 0x01) != 0;
                    dispatchPositionStatus(lCh, mRxFrame.data, mRxFrame.dataLen, lStopped, 5, 7);
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
                //   Minimum 8 bytes for full position+estimate payload
                if (mRxFrame.dataLen >= 8)
                {
                    const bool lStopped = (mRxFrame.data[0] & 0x01) != 0;
                    dispatchPositionStatus(lCh, mRxFrame.data, mRxFrame.dataLen, lStopped, 2, 4);
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
                applyPrivateBatteryInfo(lCh, mRxFrame.data, mRxFrame.dataLen);
                applyPrivateTiltInfo(lCh, mRxFrame.data, mRxFrame.dataLen);
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
                applyGeneralInfo2TiltInfo(lCh, mRxFrame.data, mRxFrame.dataLen);
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
    lFrame.setLowPower(resolveLowPower2W(iDestNodeId));
    lFrame.setSrcNode(mOwnNodeId);
    lFrame.setDestNode(iDestNodeId);
    lFrame.commandId = IoHomeCommand::StatusUpdateResponse;
    lFrame.data[0] = 0x05;
    lFrame.data[1] = 0x00;
    lFrame.dataLen = 2;
    lFrame.hasHmac = false;

    return lFrame.serialize2W(oBuffer, iBufferLen);
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
    else if (millis() - mStateTimer > currentTxTimeoutMs())
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
    lFrame.setLowPower(resolveLowPower2W(mAuthSrcNodeId));
    lFrame.setSrcNode(mOwnNodeId);
    lFrame.setDestNode(mAuthSrcNodeId);
    lFrame.commandId = IoHomeCommand::ChallengeRequest;
    memcpy(lFrame.data, mAuthChallenge, 6);
    lFrame.dataLen = 6;
    lFrame.hasHmac = false;

    uint8_t lBuf[IOHC_FRAME_BUFFER_SIZE];
    uint8_t lLen = lFrame.serialize2W(lBuf, sizeof(lBuf));
    if (lLen > 0)
    {
        const RadioError lErr = startShortPreambleTransmit(lBuf, lLen);
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
    // Passive mode: observe frames. Key extraction is only active while an
    // explicit passive key-sniff session is listening.
    // Flow: KeyInitTransfer (0x31) -> ChallengeRequest (0x3C) -> KeyTransfer (0x32)
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

    if (mPassiveKeySniffStatus != PassiveKeySniffStatus::Listening)
    {
        dispatchRxFrame();
        return;
    }

    switch (mRxFrame.commandId)
    {
    case IoHomeCommand::KeyInitTransfer:
        // Save the init frame and note which device is pairing
        mPassiveKeyInit = mRxFrame;
        mPassivePairNodeId = passivePairNodeFromFrame(mRxFrame);
        mPassiveChallengeValid = false;
        memset(mPassiveChallenge, 0, sizeof(mPassiveChallenge));
        logInfoP("Passive sniff: KeyInitTransfer src=0x%06X dst=0x%06X peer=0x%06X",
                 lSrcNode, mRxFrame.getDestNodeId(), mPassivePairNodeId);
        break;

    case IoHomeCommand::ChallengeRequest:
        // Save challenge from the device being paired
        if (mRxFrame.dataLen >= 6)
        {
            if (mPassivePairNodeId == 0 || mPassivePairNodeId == lSrcNode)
            {
                mPassivePairNodeId = lSrcNode;
                memcpy(mPassiveChallenge, mRxFrame.data, 6);
                mPassiveChallengeValid = true;
                logInfoP("Passive sniff: ChallengeRequest from 0x%06X", lSrcNode);
            }
        }
        break;

    case IoHomeCommand::KeyTransfer:
    {
        // Decrypt the system key using TRANSFER_KEY + observed challenge
        const uint32_t lPairNodeId = passivePairNodeFromFrame(mRxFrame);
        if (mRxFrame.dataLen >= 16 &&
            mPassivePairNodeId != 0 &&
            mPassiveChallengeValid &&
            lPairNodeId == mPassivePairNodeId)
        {
            const uint8_t lKeyInitData[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
            uint8_t lExtractedKey[16];
            if (IoHomeCrypto::crypt2WKeyXor(lKeyInitData, sizeof(lKeyInitData),
                                            mPassiveChallenge, mRxFrame.data,
                                            IOHC_TRANSFER_KEY, lExtractedKey))
            {
                mPassiveKeyResult.valid = true;
                mPassiveKeyResult.nodeId = mPassivePairNodeId;
                memcpy(mPassiveKeyResult.key, lExtractedKey, sizeof(mPassiveKeyResult.key));
                mPassiveKeyResult.capturedAt = millis();
                mPassiveKeyResult.freqIdx = mCurrentFreqIdx;
                mPassiveKeySniffStatus = PassiveKeySniffStatus::Captured;

                logInfoP("Passive sniff: captured system key for 0x%06X", mPassivePairNodeId);
                if (mModule)
                    mModule->onPassiveKeyCaptured(mPassiveKeyResult);

                if (!mNetworkScanActive)
                {
                    mPassiveMode = false;
                    mState = ControllerState::Idle;
                    startReceive();
                }
            }
            mPassivePairNodeId = 0;
            mPassiveChallengeValid = false;
            memset(mPassiveChallenge, 0, sizeof(mPassiveChallenge));
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
        if (startShortPreambleTransmit(mTxBuffer, mTxLen, true) != RadioError::None)
            return;
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
        if (startShortPreambleTransmit(mTxBuffer, mTxLen, true) != RadioError::None)
            return;
        mGatewayState = ControllerState::GatewayWaitDiscoveryResponse;
        return;

    case IoHomeCommand::Confirmation:
        mGatewayPeerNodeId = lSrcNode;
        mTxLen = buildGatewayDiscoverActuatorAckFrame(mTxBuffer, sizeof(mTxBuffer),
                                                      mGatewayNodeId, lSrcNode);
        if (mTxLen == 0)
            return;
        if (startShortPreambleTransmit(mTxBuffer, mTxLen, true) != RadioError::None)
            return;
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

    if (!precheckGatewayStateFrame(lSrcNode, false))
        return;

    if (mRxFrame.commandId == IoHomeCommand::Confirmation)
    {
        mTxLen = buildGatewayDiscoverActuatorAckFrame(mTxBuffer, sizeof(mTxBuffer),
                                                      mGatewayNodeId, lSrcNode);
        if (mTxLen == 0)
            return;
        if (startShortPreambleTransmit(mTxBuffer, mTxLen, true) != RadioError::None)
            return;
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

    if (!precheckGatewayStateFrame(lSrcNode, true))
        return;

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
    if (startShortPreambleTransmit(mTxBuffer, mTxLen, true) != RadioError::None)
        return;
    mGatewayState = ControllerState::GatewayWaitChallenge;
}

void IoHomeController::processGatewayWaitChallenge()
{
    const uint32_t lSrcNode = mRxFrame.getSrcNodeId();

    if (!precheckGatewayStateFrame(lSrcNode, true))
        return;

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
    if (startShortPreambleTransmit(mTxBuffer, mTxLen, true) != RadioError::None)
        return;

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

    resetGatewaySessionState();
}

bool IoHomeController::precheckGatewayStateFrame(uint32_t iSrcNode, bool iResetSessionOnDiscover)
{
    if (mRxFrame.commandId == IoHomeCommand::DiscoverRequest)
    {
        if (iResetSessionOnDiscover)
            resetGatewaySessionState();
        else
            mGatewayState = ControllerState::GatewayIdle;

        processGatewayIdle();
        return false;
    }

    if (mGatewayPeerNodeId != 0 && iSrcNode != mGatewayPeerNodeId)
    {
        dispatchRxFrame();
        return false;
    }

    return true;
}

void IoHomeController::resetGatewaySessionState()
{
    mGatewayState = ControllerState::GatewayIdle;
    mGatewayPeerNodeId = 0;
    mGatewayMemCmd = 0;
    mGatewayMemDataLen = 0;
    memset(mGatewayKeyEncrypted, 0, sizeof(mGatewayKeyEncrypted));
    memset(mGatewayMemData, 0, sizeof(mGatewayMemData));
    memset(mGatewayPeerChallenge, 0, sizeof(mGatewayPeerChallenge));
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
