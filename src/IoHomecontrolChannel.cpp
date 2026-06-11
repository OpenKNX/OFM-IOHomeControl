#include "IoHomecontrolChannel.h"
#include "IoHomecontrol.h"
#include "controller/IoHomeController.h"
#include "protocol/IoHomeCommands.h"
#include "knxprod.h"
#include "OpenKNX.h"

namespace
{
    constexpr uint32_t kTrackedStatusPollDefaultMs = 2000UL;
    constexpr uint32_t kTrackedStatusEstimateBiasMs = 1000UL;

    uint8_t resolveOneWayBroadcastType(uint8_t iConfiguredType, uint8_t iDeviceType)
    {
        (void)iDeviceType;
        if (iConfiguredType != 0xFF)
            return iConfiguredType & 0x3F;

        // Reference-compatible rspaargaren/iohomecontrol default:
        // type 0 is the generic “All” target and serializes to 0x00003F via
        // dst = ((type << 6) | 0x3F).  Typed targets such as 2 (shutter/blind)
        // and 3 (awning) remain explicit ETS/console/diagnostic choices.
        return 0;
    }

    float clampPercent(float iValue)
    {
        if (iValue < 0.0f)
            return 0.0f;
        if (iValue > 100.0f)
            return 100.0f;
        return iValue;
    }

    bool timeReached(uint32_t iNow, uint32_t iDeadline)
    {
        return static_cast<int32_t>(iNow - iDeadline) >= 0;
    }

    const char *iohcDeviceTypeLabel(uint16_t iType)
    {
        switch (static_cast<IoHomeDeviceType>(iType))
        {
        case IoHomeDeviceType::VenetianBlind:
            return "venetian_blind";
        case IoHomeDeviceType::RollerShutter:
            return "roller_shutter";
        case IoHomeDeviceType::Awning:
            return "awning";
        case IoHomeDeviceType::WindowOpener:
            return "window_opener";
        case IoHomeDeviceType::GarageOpener:
            return "garage_opener";
        case IoHomeDeviceType::Light:
            return "light";
        case IoHomeDeviceType::GateOpener:
            return "gate_opener";
        case IoHomeDeviceType::RollingDoorOpener:
            return "rolling_door_opener";
        case IoHomeDeviceType::Lock:
            return "lock";
        case IoHomeDeviceType::Blind:
            return "blind";
        case IoHomeDeviceType::Unknown0B:
            return "unknown_0b";
        case IoHomeDeviceType::Beacon:
            return "beacon";
        case IoHomeDeviceType::DualShutter:
            return "dual_shutter";
        case IoHomeDeviceType::HeatingTempInterface:
            return "heating_temp_interface";
        case IoHomeDeviceType::OnOffSwitch:
            return "on_off_switch";
        case IoHomeDeviceType::HorizontalAwning:
            return "horizontal_awning";
        case IoHomeDeviceType::ExternalVenetianBlind:
            return "external_venetian_blind";
        case IoHomeDeviceType::LouvrBlind:
            return "louvr_blind";
        case IoHomeDeviceType::CurtainTrack:
            return "curtain_track";
        case IoHomeDeviceType::VentilationPoint:
            return "ventilation_point";
        case IoHomeDeviceType::ExteriorHeating:
            return "exterior_heating";
        case IoHomeDeviceType::HeatPump:
            return "heat_pump";
        case IoHomeDeviceType::IntrusionAlarm:
            return "intrusion_alarm";
        case IoHomeDeviceType::SwingingShutter:
            return "swinging_shutter";
        case IoHomeDeviceType::Unknown:
        default:
            return "unknown";
        }
    }
}

IoHomecontrolChannel::IoHomecontrolChannel(uint8_t iIndex, IoHomeController &iController)
    : mController(iController)
{
    _channelIndex = iIndex;
}

const std::string IoHomecontrolChannel::name()
{
    return "IoHomecontrol";
}

const std::string IoHomecontrolChannel::logPrefix()
{
    std::string lPrefix("IoHC[");
    lPrefix += std::to_string(_channelIndex + 1);
    lPrefix += "]";
    return lPrefix;
}

void IoHomecontrolChannel::setup()
{
    const uint8_t lProtocolMode = static_cast<uint8_t>(ParamIOHC_IOHCProtocolMode);
    const uint32_t lOneWayTargetNodeId = static_cast<uint32_t>(ParamIOHC_IOHCOneWayTargetNodeId) & 0x00FFFFFF;
    const uint8_t lOneWayBroadcastType = static_cast<uint8_t>(ParamIOHC_IOHCOneWayBroadcastType);
    const uint8_t lOneWayProfileChannel = static_cast<uint8_t>(ParamIOHC_IOHCOneWayProfileChannel);
    const uint8_t lOneWayManufacturer = static_cast<uint8_t>(ParamIOHC_IOHCOneWayManufacturer);

    logInfoP("ETS config: active=%u protocol=%u (%s) oneWayTarget=0x%06X oneWayType=%u oneWayProfile=%u oneWayMfg=0x%02X",
             ParamIOHC_IOHCActive ? 1U : 0U,
             static_cast<unsigned>(lProtocolMode),
             lProtocolMode == 1 ? "1W" : "2W",
             static_cast<unsigned long>(lOneWayTargetNodeId),
             static_cast<unsigned>(lOneWayBroadcastType),
             static_cast<unsigned>(lOneWayProfileChannel),
             static_cast<unsigned>(lOneWayManufacturer));

    // Check if channel is active in ETS
    if (!ParamIOHC_IOHCActive)
    {
        logInfoP("Channel disabled in ETS - protocol and 1W settings will not be applied");
        return;
    }

    if (lProtocolMode != 0 && lProtocolMode != 1)
        logInfoP("Unexpected ETS protocol mode %u - treating as 2W", static_cast<unsigned>(lProtocolMode));

    mStatusPollTimer = 0;
    mNextStatusPollMs = 0;
    mPollTrackingDeadlineMs = 0;
    mSingleFollowUpPollPending = false;
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;
    mStatusExpected = false;

    loadSceneConfiguration();

    // Apply protocol mode from ETS (Feature 4: 1W/2W per channel)
    setIs1W(lProtocolMode == 1);
    setConfigured1WTargetNodeId(lOneWayTargetNodeId);
    setConfigured1WBroadcastType(resolveOneWayBroadcastType(
        lOneWayBroadcastType,
        static_cast<uint8_t>(ParamIOHC_IOHCDeviceType)));
    const uint8_t lProfileChannel = lOneWayProfileChannel;
    setConfigured1WProfileChannel(lProfileChannel == 0 ? 0xFF : static_cast<uint8_t>(lProfileChannel - 1));
    mConfigured1WManufacturer = lOneWayManufacturer;
    if (mConfigured1WManufacturer != 0)
        setOneWayControllerManufacturer(mConfigured1WManufacturer);

    if (mIs1W && mConfigured1WTargetNodeId == 0)
        logInfoP("Channel is configured as 1W but has no ETS 1W target node; pairing must provide a target node explicitly");

    logInfoP("Applied protocol config: %s target=0x%06X broadcastType=%u profile=%s manufacturer=0x%02X",
             mIs1W ? "1W" : "2W",
             static_cast<unsigned long>(mConfigured1WTargetNodeId),
             static_cast<unsigned>(mConfigured1WBroadcastType),
             mConfigured1WProfileChannel == 0xFF ? "own" : "linked",
             static_cast<unsigned>(mOneWayControllerManufacturer));

    logDebugP("Setup (type=%d, poll=%ds, open=%.1fs, close=%.1fs, invert=%d, powerOn=%d, scenes=%d, 1w=%d, 1wTarget=%06X, 1wType=%u, 1wProfile=%u)",
              ParamIOHC_IOHCDeviceType, ParamIOHC_IOHCPollInterval,
              ParamIOHC_IOHCOpeningTime, ParamIOHC_IOHCClosingTime,
              ParamIOHC_IOHCInvertDir, ParamIOHC_IOHCPowerOnBeh,
              getConfiguredSceneCount(), mIs1W ? 1 : 0,
              mConfigured1WTargetNodeId, static_cast<unsigned>(mConfigured1WBroadcastType),
              mConfigured1WProfileChannel == 0xFF ? 0U : static_cast<unsigned>(mConfigured1WProfileChannel + 1));
}

void IoHomecontrolChannel::loop()
{
    if (!mPaired)
        return;

    if (!ParamIOHC_IOHCActive)
        return;

    const uint32_t lNow = millis();

    updateEstimatedPosition();

    if (mPollTrackingDeadlineMs != 0 &&
        timeReached(lNow, mPollTrackingDeadlineMs) &&
        mNextStatusPollMs == 0 &&
        !mSingleFollowUpPollPending)
    {
        clearStatusPollTracking();
    }

    if (mNextStatusPollMs != 0 && timeReached(lNow, mNextStatusPollMs))
    {
        if (requestStatus())
        {
            mNextStatusPollMs = 0;
            mSingleFollowUpPollPending = false;
            mStatusExpected = false;
        }
        else
        {
            onStatusPollFailed(false);
        }
    }

    // Periodic status polling based on ETS config
    uint16_t lPollSec = ParamIOHC_IOHCPollInterval;
    if (lPollSec > 0 && !isStatusPollTrackingActive(lNow))
    {
        uint32_t lPollMs = (uint32_t)lPollSec * 1000;
        if (delayCheck(mStatusPollTimer, lPollMs))
        {
            requestStatus();
            mStatusPollTimer = delayTimerInit();
        }
    }
}

void IoHomecontrolChannel::processInputKo(uint8_t iIoIndex, GroupObject &iKo)
{
    if (!mPaired)
    {
        logDebugP("Ignoring KO %d - not paired", iIoIndex);
        return;
    }

    // P2: Lock check — only Lock and WindAlarm KOs bypass the lock
    if (mLocked && iIoIndex != IOHC_KoCHLock && iIoIndex != IOHC_KoCHWindAlarm)
    {
        logDebugP("Channel locked, ignoring KO %d", iIoIndex);
        return;
    }

    switch (iIoIndex)
    {
    case IOHC_KoCHPosition:
    {
        float lPercent = (float)iKo.value(DPT_Scaling);
        if (ParamIOHC_IOHCInvertDir)
            lPercent = 100.0f - lPercent;
        sendPositionCommand(lPercent);
        break;
    }
    case IOHC_KoCHUpDown:
    {
        bool lDown = iKo.value(DPT_UpDown);
        if (ParamIOHC_IOHCInvertDir)
            lDown = !lDown;
        sendUpDown(lDown);
        break;
    }
    case IOHC_KoCHOnOff:
    {
        bool lOn = (bool)iKo.value(DPT_Switch);
        sendPositionCommand(lOn ? 100.0f : 0.0f);
        break;
    }
    case IOHC_KoCHStop:
    {
        sendStop();
        break;
    }
    case IOHC_KoCHSlat:
    {
        float lPercent = (float)iKo.value(DPT_Scaling);
        sendSlatCommand(lPercent);
        break;
    }
    // P1: Favorite position trigger
    case IOHC_KoCHFavorite:
    {
        if ((bool)iKo.value(DPT_Switch))
            sendFavorite();
        break;
    }
    // P1: Ventilation position trigger
    case IOHC_KoCHVentilation:
    {
        if ((bool)iKo.value(DPT_Switch))
            sendVentilationPosition();
        break;
    }
    // P2: Lock/Unlock
    case IOHC_KoCHLock:
    {
        mLocked = (bool)iKo.value(DPT_Switch);
        logDebugP("Channel %s", mLocked ? "LOCKED" : "unlocked");
        break;
    }
    // P3: Scene recall (DPT 17.001)
    case IOHC_KoCHScene:
    {
        uint8_t lScene = (iKo.valueRef()[0] & 0x3F) + 1;
        handleSceneRecall(lScene);
        break;
    }
    // P3: Scene control — store/recall (DPT 18.001)
    case IOHC_KoCHSceneControl:
    {
        uint8_t lControl = iKo.valueRef()[0];
        handleSceneControl(lControl);
        break;
    }
    // P3: Wind/Rain alarm
    case IOHC_KoCHWindAlarm:
    {
        bool lAlarm = (bool)iKo.value(DPT_Alarm);
        handleWindAlarm(lAlarm);
        break;
    }
    // P3: Step-Stop / Long operation
    case IOHC_KoCHStepStop:
    {
        bool lDown = (bool)iKo.value(DPT_UpDown);
        handleStepStop(lDown);
        break;
    }
    // Cozy thermostat KOs (Feature 3: visible when DeviceType=5)
    case IOHC_KoCHCozyTemp:
    {
        float lTempC = (float)iKo.value(Dpt(9, 1));
        // Convert DPT 9.001 (°C) to io-homecontrol tenths (70=7.0°C, 280=28.0°C)
        uint16_t lTenths = (uint16_t)(lTempC * 10.0f + 0.5f);
        if (lTenths < 70)
            lTenths = 70;
        if (lTenths > 280)
            lTenths = 280;
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x03, (uint8_t)lTenths);
        logDebugP("Cozy temp: %.1f°C (%d tenths)", lTempC, lTenths);
        break;
    }
    case IOHC_KoCHCozyMode:
    {
        uint8_t lMode = iKo.valueRef()[0];
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x04, lMode);
        logDebugP("Cozy mode: %d", lMode);
        break;
    }
    case IOHC_KoCHCozyPresence:
    {
        bool lPresent = (bool)iKo.value(DPT_Switch);
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x10, lPresent ? 1 : 0);
        logDebugP("Cozy presence: %s", lPresent ? "ON" : "OFF");
        break;
    }
    case IOHC_KoCHCozyWindow:
    {
        bool lOpen = (bool)iKo.value(DPT_Switch);
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x0E, lOpen ? 1 : 0);
        logDebugP("Cozy window: %s", lOpen ? "OPEN" : "CLOSED");
        break;
    }
    default:
        break;
    }
}

// --- Callbacks from controller ---

void IoHomecontrolChannel::onPositionFeedback(float iPositionPercent)
{
    publishPositionFeedback(iPositionPercent, true);
}

void IoHomecontrolChannel::onTargetPositionFeedback(float iTargetPositionPercent)
{
    mTargetPosition = clampPercent(iTargetPositionPercent);
}

void IoHomecontrolChannel::onStatusUpdate(bool iIsMoving)
{
    mStatusPollTimer = delayTimerInit();
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;

    if (!iIsMoving)
    {
        clearStatusPollTracking();
        stopTravelEstimation(false);
    }
    else if (mTravelDurationMs == 0 && mTargetPosition != mCurrentPosition)
        startTravelEstimation(mTargetPosition);

    if (iIsMoving)
    {
        const uint32_t lDelayMs = defaultTrackedStatusPollDelayMs();
        const uint32_t lNow = millis();
        if (mPollTrackingDeadlineMs == 0)
            mPollTrackingDeadlineMs = lNow + lDelayMs;
        if (mNextStatusPollMs == 0 && (mSingleFollowUpPollPending || mStatusExpected))
            mNextStatusPollMs = lNow + lDelayMs;
    }

    mIsMoving = iIsMoving;
    if (!isBinaryDeviceType())
        getKo(IOHC_KoCHMovementStatus).value(iIsMoving, Dpt(1, 11));
    logDebugP("Status: %s", iIsMoving ? "moving" : "idle");
}

void IoHomecontrolChannel::logStatusSummary(float iCurrentPositionPercent, bool iHasCurrentPosition,
                                            float iTargetPositionPercent, bool iHasTargetPosition,
                                            bool iIsMoving)
{
    if (mDeviceName[0] != '\0')
    {
        if (iHasCurrentPosition && iHasTargetPosition)
        {
            logDebugP("Received device status for %06X: %s (0x%02X/0x%02X) / Position %.1f / Target %.1f / Moving: %s / Deleted: %s",
                      mNodeId, mDeviceName,
                      static_cast<unsigned>(mDeviceType & 0xFF),
                      static_cast<unsigned>(mDeviceSubtype),
                      iCurrentPositionPercent, iTargetPositionPercent,
                      iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
        }
        else if (iHasCurrentPosition)
        {
            logDebugP("Received device status for %06X: %s (0x%02X/0x%02X) / Position %.1f / Moving: %s / Deleted: %s",
                      mNodeId, mDeviceName,
                      static_cast<unsigned>(mDeviceType & 0xFF),
                      static_cast<unsigned>(mDeviceSubtype),
                      iCurrentPositionPercent, iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
        }
        else
        {
            logDebugP("Received device status for %06X: %s (0x%02X/0x%02X) / Moving: %s / Deleted: %s",
                      mNodeId, mDeviceName,
                      static_cast<unsigned>(mDeviceType & 0xFF),
                      static_cast<unsigned>(mDeviceSubtype),
                      iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
        }
        return;
    }

    if (iHasCurrentPosition && iHasTargetPosition)
    {
        logDebugP("Received device status for %06X: %s (0x%02X/0x%02X) / Position %.1f / Target %.1f / Moving: %s / Deleted: %s",
                  mNodeId, iohcDeviceTypeLabel(mDeviceType),
                  static_cast<unsigned>(mDeviceType & 0xFF),
                  static_cast<unsigned>(mDeviceSubtype),
                  iCurrentPositionPercent, iTargetPositionPercent,
                  iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
    }
    else if (iHasCurrentPosition)
    {
        logDebugP("Received device status for %06X: %s (0x%02X/0x%02X) / Position %.1f / Moving: %s / Deleted: %s",
                  mNodeId, iohcDeviceTypeLabel(mDeviceType),
                  static_cast<unsigned>(mDeviceType & 0xFF),
                  static_cast<unsigned>(mDeviceSubtype),
                  iCurrentPositionPercent, iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
    }
    else
    {
        logDebugP("Received device status for %06X: %s (0x%02X/0x%02X) / Moving: %s / Deleted: %s",
                  mNodeId, iohcDeviceTypeLabel(mDeviceType),
                  static_cast<unsigned>(mDeviceType & 0xFF),
                  static_cast<unsigned>(mDeviceSubtype),
                  iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
    }
}

void IoHomecontrolChannel::onSlatFeedback(float iSlatPercent)
{
    mCurrentSlat = iSlatPercent;
    getKo(IOHC_KoCHSlatFeedback).value((uint8_t)(iSlatPercent + 0.5f), DPT_Scaling);
    logDebugP("Slat feedback: %.1f%%", iSlatPercent);
}

void IoHomecontrolChannel::onDeviceName(const char *iName, uint8_t iLen)
{
    // Strip leading control characters (bytes <= 0x20)
    uint8_t lStart = 0;
    while (lStart < iLen && (uint8_t)iName[lStart] <= 0x20)
        lStart++;

    // Strip trailing null/space bytes
    uint8_t lEnd = iLen;
    while (lEnd > lStart && ((uint8_t)iName[lEnd - 1] <= 0x20 || iName[lEnd - 1] == '\0'))
        lEnd--;

    // Convert Latin-1 to UTF-8, truncate to fit buffer (20 chars + null)
    uint8_t lOutPos = 0;
    for (uint8_t i = lStart; i < lEnd && lOutPos < sizeof(mDeviceName) - 1; i++)
    {
        uint8_t lByte = (uint8_t)iName[i];
        if (lByte < 0x80)
        {
            mDeviceName[lOutPos++] = lByte;
        }
        else
        {
            // Latin-1 codepoints 0x80-0xFF → UTF-8 two-byte sequence
            if (lOutPos + 1 >= sizeof(mDeviceName) - 1)
                break; // not enough room for 2-byte sequence
            mDeviceName[lOutPos++] = 0xC0 | (lByte >> 6);
            mDeviceName[lOutPos++] = 0x80 | (lByte & 0x3F);
        }
    }
    mDeviceName[lOutPos] = '\0';
    logDebugP("Device name: %s", mDeviceName);
}

void IoHomecontrolChannel::onDeviceInfo(uint16_t iType, uint8_t iSubtype, uint8_t iManufacturer)
{
    mDeviceType = iType;
    mDeviceSubtype = iSubtype;
    mManufacturer = iManufacturer;
    logDebugP("Device info: type=0x%04X subtype=0x%02X mfg=0x%02X", iType, iSubtype, iManufacturer);
}

void IoHomecontrolChannel::onBatteryLevel(uint8_t iPercent)
{
    mBatteryLevel = iPercent;
    getKo(IOHC_KoCHBattery).value(iPercent, DPT_Scaling);
    logDebugP("Battery level: %d%%", iPercent);
}

void IoHomecontrolChannel::onEstimate(uint8_t iSeconds)
{
    // Estimate byte from PrivateResponse (data[7]): travel time remaining in seconds
    // 0xFF or 0x00 = unknown; reference uses this to schedule next status poll
    if (iSeconds != 0xFF && iSeconds != 0x00)
    {
        logDebugP("Estimate: %d seconds remaining", iSeconds);
        mStatusPollTimer = delayTimerInit();

        uint32_t lDelayMs = static_cast<uint32_t>(iSeconds) * 1000UL + kTrackedStatusEstimateBiasMs;
        const uint32_t lConfiguredPollMs = configuredStatusPollIntervalMs();
        if (lConfiguredPollMs > 0 && lConfiguredPollMs < lDelayMs)
            lDelayMs = lConfiguredPollMs;

        mSingleFollowUpPollPending = true;
        mStatusPollFailures = 0;
        mAuthPollFailures = 0;
        mNextStatusPollMs = millis() + lDelayMs;
        mPollTrackingDeadlineMs = millis() + lDelayMs;

        // Restart travel-time position estimation with the device-provided remaining time.
        mCurrentPosition = clampPercent(estimateCurrentPosition());
        mTravelStartPosition = mCurrentPosition;
        mTravelDurationMs = (uint32_t)iSeconds * 1000;
        mTravelStartTime = millis();
    }
}

float IoHomecontrolChannel::estimateCurrentPosition() const
{
    if (mTravelDurationMs == 0 || !mIsMoving)
        return mCurrentPosition;

    uint32_t lElapsed = millis() - mTravelStartTime;
    return snapPositionBoundary(clampPercent(interpolatePosition(mTravelStartPosition, mTargetPosition, lElapsed, mTravelDurationMs)));
}

void IoHomecontrolChannel::onStatusExpected()
{
    const uint32_t lDelayMs = defaultTrackedStatusPollDelayMs();
    if (mPollTrackingDeadlineMs == 0)
        mPollTrackingDeadlineMs = millis() + lDelayMs;
    if (mNextStatusPollMs == 0)
        mNextStatusPollMs = millis() + lDelayMs;

    mStatusExpected = true;
    logDebugP("Device will auto-send status update");
}

void IoHomecontrolChannel::onStatusPollFailed(bool iAfterChallenge)
{
    const uint32_t lBaseDelayMs = defaultTrackedStatusPollDelayMs();
    const uint32_t lNow = millis();
    uint32_t lBackoffFactor = 1;

    if (iAfterChallenge)
    {
        if (mAuthPollFailures < 0xFF)
            mAuthPollFailures++;
        lBackoffFactor = 2U + (static_cast<uint32_t>(mAuthPollFailures) * 2U);
        if (lBackoffFactor > 8U)
            lBackoffFactor = 8U;
    }
    else
    {
        if (mStatusPollFailures < 0xFF)
            mStatusPollFailures++;
        lBackoffFactor = 1U + static_cast<uint32_t>(mStatusPollFailures);
        if (lBackoffFactor > 4U)
            lBackoffFactor = 4U;
    }

    mStatusExpected = false;
    mSingleFollowUpPollPending = true;
    mNextStatusPollMs = lNow + (lBaseDelayMs * lBackoffFactor);
    if (mPollTrackingDeadlineMs == 0 || timeReached(mNextStatusPollMs, mPollTrackingDeadlineMs))
        mPollTrackingDeadlineMs = mNextStatusPollMs;

    logDebugP("Status poll failed%s, retry in %lu ms",
              iAfterChallenge ? " after challenge" : "",
              static_cast<unsigned long>(lBaseDelayMs * lBackoffFactor));
}

// --- Pairing data ---

bool IoHomecontrolChannel::isPaired() const
{
    return mPaired;
}

void IoHomecontrolChannel::setNodeId(uint32_t iNodeId)
{
    mNodeId = iNodeId & 0x00FFFFFF; // 24-bit
    mPaired = (mNodeId != 0);
}

uint32_t IoHomecontrolChannel::getNodeId() const
{
    return mNodeId;
}

void IoHomecontrolChannel::setEncryptionKey(const uint8_t *iKey)
{
    memcpy(mEncKey, iKey, 16);
}

const uint8_t *IoHomecontrolChannel::getEncryptionKey() const
{
    return mEncKey;
}

void IoHomecontrolChannel::setLowPower2W(bool iLowPower)
{
    mLowPower2W = iLowPower;
}

bool IoHomecontrolChannel::isLowPower2W() const
{
    return mLowPower2W;
}

void IoHomecontrolChannel::setLastChallenge(const uint8_t *iChallenge)
{
    memcpy(mLastChallenge, iChallenge, 6);
}

const uint8_t *IoHomecontrolChannel::getLastChallenge() const
{
    return mLastChallenge;
}

uint16_t IoHomecontrolChannel::getSequence1W() const { return mSequence1W; }
uint16_t IoHomecontrolChannel::getReservedSequence1W() const { return mReservedSequence1W; }
void IoHomecontrolChannel::setSequence1W(uint16_t iSeq)
{
    mSequence1W = iSeq;
    mReservedSequence1W = iSeq;
}
void IoHomecontrolChannel::setReservedSequence1W(uint16_t iSeq)
{
    mReservedSequence1W = iSeq;
}
uint16_t IoHomecontrolChannel::incrementSequence1W()
{
    bool lSaveRequired = false;
    return incrementSequence1W(false, lSaveRequired);
}
uint16_t IoHomecontrolChannel::incrementSequence1W(bool iForceReserve, bool &oFlashSaveRequired)
{
    const uint16_t lNext = static_cast<uint16_t>(mSequence1W + 1U);
    mSequence1W = lNext;

    // mReservedSequence1W is the highest sequence value already made safe in
    // flash. Reserve another small window before using a value at/above that
    // watermark, so a power loss cannot replay the just-transmitted sequence.
    const int16_t lRemainingReserved = static_cast<int16_t>(mReservedSequence1W - mSequence1W);
    oFlashSaveRequired = iForceReserve || mReservedSequence1W == 0 || lRemainingReserved <= 0;
    if (oFlashSaveRequired)
        mReservedSequence1W = static_cast<uint16_t>(mSequence1W + IOHC_1W_SEQUENCE_RESERVE_WINDOW);

    return mSequence1W;
}
void IoHomecontrolChannel::setOneWayControllerNodeId(uint32_t iNodeId) { mOneWayControllerNodeId = iNodeId & 0x00FFFFFF; }
uint32_t IoHomecontrolChannel::getOneWayControllerNodeId() const { return mOneWayControllerNodeId; }
void IoHomecontrolChannel::setOneWayControllerKey(const uint8_t *iKey)
{
    if (iKey)
        memcpy(mOneWayControllerKey, iKey, sizeof(mOneWayControllerKey));
}
const uint8_t *IoHomecontrolChannel::getOneWayControllerKey() const { return mOneWayControllerKey; }
void IoHomecontrolChannel::setOneWayControllerManufacturer(uint8_t iManufacturer) { mOneWayControllerManufacturer = iManufacturer; }
uint8_t IoHomecontrolChannel::getOneWayControllerManufacturer() const { return mOneWayControllerManufacturer; }
uint8_t IoHomecontrolChannel::getConfigured1WManufacturer() const { return mConfigured1WManufacturer; }
bool IoHomecontrolChannel::hasOneWayControllerIdentity() const
{
    if (mOneWayControllerNodeId == 0)
        return false;
    for (uint8_t i = 0; i < sizeof(mOneWayControllerKey); i++)
    {
        if (mOneWayControllerKey[i] != 0)
            return true;
    }
    return false;
}
void IoHomecontrolChannel::setConfigured1WProfileChannel(uint8_t iChannelIndex) { mConfigured1WProfileChannel = iChannelIndex; }
uint8_t IoHomecontrolChannel::getConfigured1WProfileChannel() const { return mConfigured1WProfileChannel; }
bool IoHomecontrolChannel::is1W() const { return mIs1W; }
void IoHomecontrolChannel::setIs1W(bool iIs1W) { mIs1W = iIs1W; }
void IoHomecontrolChannel::setConfigured1WTargetNodeId(uint32_t iNodeId) { mConfigured1WTargetNodeId = iNodeId & 0x00FFFFFF; }
uint32_t IoHomecontrolChannel::getConfigured1WTargetNodeId() const { return mConfigured1WTargetNodeId; }
void IoHomecontrolChannel::setConfigured1WBroadcastType(uint8_t iBroadcastType) { mConfigured1WBroadcastType = iBroadcastType & 0x3F; }
uint8_t IoHomecontrolChannel::getConfigured1WBroadcastType() const { return mConfigured1WBroadcastType; }

// --- Private command methods ---

void IoHomecontrolChannel::sendPositionCommand(float iPercent, uint8_t iSlatPercent)
{
    logDebugP("Send position %.1f%%", iPercent);
    mTargetPosition = clampPercent(iPercent);
    uint8_t lParam = (uint8_t)(iPercent + 0.5f);
    const bool lQueued = mIs1W
                             ? mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, lParam, iSlatPercent)
                             : mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, lParam);
    if (!lQueued)
        return;

    startTravelEstimation(mTargetPosition);
    startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

void IoHomecontrolChannel::sendUpDown(bool iDown)
{
    logDebugP("Send %s", iDown ? "DOWN" : "UP");
    uint8_t lPercent = iDown ? 100 : 0;
    if (!mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, lPercent))
        return;

    startTravelEstimation((float)lPercent);
    startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

void IoHomecontrolChannel::sendStop()
{
    logDebugP("Send STOP");
    stopTravelEstimation(true);
    if (!mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, 0xD2))
        return;

    startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

void IoHomecontrolChannel::sendFavorite()
{
    logDebugP("Send FAVORITE");
    if (!mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, 0xD8))
        return;

    startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

void IoHomecontrolChannel::sendSlatCommand(float iPercent)
{
    const float lSlatPercent = clampPercent(iPercent);
    logDebugP("Send slat %.1f%% (with current position %.1f%%)", lSlatPercent, mCurrentPosition);

    if (!mIs1W && isTiltCapableDeviceType())
    {
        if (mController.sendTiltCommand(mNodeId, mEncKey, static_cast<uint8_t>(lSlatPercent + 0.5f)))
            startStatusPollTracking(defaultTrackedStatusPollDelayMs());
        return;
    }

    uint8_t lPosParam = (uint8_t)(mCurrentPosition + 0.5f);
    uint8_t lSlatParam = (uint8_t)(lSlatPercent + 0.5f);
    if (mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, lPosParam, lSlatParam))
        startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

bool IoHomecontrolChannel::requestStatus()
{
    logDebugP("Request status");

    if (!mIs1W && isTiltCapableDeviceType())
    {
        const bool lQueued = mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Private, 0x03, 0x20, 0x01);
        if (lQueued)
            mStatusPollTimer = delayTimerInit();
        return lQueued;
    }

    const bool lQueued = mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Private, 0x03);
    if (lQueued)
        mStatusPollTimer = delayTimerInit();
    return lQueued;
}

void IoHomecontrolChannel::scheduleStatusPoll(uint32_t iDelayMs)
{
    if (!mPaired)
        return;

    const uint32_t lDelayMs = (iDelayMs > 0) ? iDelayMs : defaultTrackedStatusPollDelayMs();
    const uint32_t lNow = millis();
    const uint32_t lRequestedPollMs = lNow + lDelayMs;

    mSingleFollowUpPollPending = true;
    mStatusExpected = false;
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;

    if (mNextStatusPollMs == 0 || static_cast<int32_t>(lRequestedPollMs - mNextStatusPollMs) < 0)
        mNextStatusPollMs = lRequestedPollMs;

    if (mPollTrackingDeadlineMs == 0 || timeReached(lRequestedPollMs, mPollTrackingDeadlineMs))
        mPollTrackingDeadlineMs = lRequestedPollMs;
}

void IoHomecontrolChannel::requestStatusPrivate()
{
    logDebugP("Request status (Private 0x03)");
    mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Private, 0x03);
}

void IoHomecontrolChannel::startStatusPollTracking(uint32_t iDelayMs)
{
    const uint32_t lNow = millis();
    const uint32_t lTrackingWindowMs = (mTravelDurationMs > 0)
                                           ? (mTravelDurationMs + kTrackedStatusEstimateBiasMs)
                                           : iDelayMs;

    mSingleFollowUpPollPending = true;
    mStatusExpected = false;
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;
    mNextStatusPollMs = lNow + iDelayMs;
    mPollTrackingDeadlineMs = lNow + ((lTrackingWindowMs > iDelayMs) ? lTrackingWindowMs : iDelayMs);
}

void IoHomecontrolChannel::clearStatusPollTracking()
{
    mNextStatusPollMs = 0;
    mPollTrackingDeadlineMs = 0;
    mSingleFollowUpPollPending = false;
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;
    mStatusExpected = false;
}

uint32_t IoHomecontrolChannel::configuredStatusPollIntervalMs() const
{
    const uint16_t lPollSec = ParamIOHC_IOHCPollInterval;
    return (lPollSec > 0) ? (static_cast<uint32_t>(lPollSec) * 1000UL) : 0UL;
}

uint32_t IoHomecontrolChannel::defaultTrackedStatusPollDelayMs() const
{
    const uint32_t lConfiguredPollMs = configuredStatusPollIntervalMs();
    if (lConfiguredPollMs == 0 || lConfiguredPollMs > kTrackedStatusPollDefaultMs)
        return kTrackedStatusPollDefaultMs;
    return lConfiguredPollMs;
}

bool IoHomecontrolChannel::isStatusPollTrackingActive(uint32_t iNowMs) const
{
    if (mNextStatusPollMs != 0 || mSingleFollowUpPollPending || mStatusExpected)
        return true;

    return mPollTrackingDeadlineMs != 0 && !timeReached(iNowMs, mPollTrackingDeadlineMs);
}

bool IoHomecontrolChannel::isOnOffDeviceType() const
{
    return ParamIOHC_IOHCDeviceType == 6 || ParamIOHC_IOHCDeviceType == 12;
}

bool IoHomecontrolChannel::isLockDeviceType() const
{
    return ParamIOHC_IOHCDeviceType == 8;
}

bool IoHomecontrolChannel::isTiltCapableDeviceType() const
{
    uint16_t lType = mDeviceType;
    if (lType == 0)
        lType = static_cast<uint16_t>(ParamIOHC_IOHCDeviceType);

    switch (static_cast<IoHomeDeviceType>(lType))
    {
    case IoHomeDeviceType::VenetianBlind:
    case IoHomeDeviceType::ExternalVenetianBlind:
    case IoHomeDeviceType::LouvrBlind:
    case IoHomeDeviceType::Blind:
        return true;
    default:
        return false;
    }
}

bool IoHomecontrolChannel::isBinaryDeviceType() const
{
    return isOnOffDeviceType() || isLockDeviceType();
}

void IoHomecontrolChannel::publishBinaryStatus()
{
    if (!isBinaryDeviceType())
        return;

    bool lActive = mCurrentPosition >= 50.0f;
    if (isOnOffDeviceType())
        getKo(IOHC_KoCHOnOffStatus).value(lActive, DPT_Switch);
    else
        getKo(IOHC_KoCHLockStatus).value(lActive, Dpt(1, 11));
}

bool IoHomecontrolChannel::restoreLastKnownStateAfterStartup()
{
    bool lBinaryState = false;

    switch (ParamIOHC_IOHCDeviceType)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 7:
    case 9:
    case 10:
    case 11:
        break;
    case 6:
    case 8:
    case 12:
        lBinaryState = true;
        break;
    default:
        return false;
    }

    if (lBinaryState)
    {
        bool lOn = isOnOffDeviceType()
                       ? (bool)getKo(IOHC_KoCHOnOffStatus).value(DPT_Switch)
                       : (bool)getKo(IOHC_KoCHLockStatus).value(Dpt(1, 11));
        mCurrentPosition = lOn ? 100.0f : 0.0f;
        mTargetPosition = mCurrentPosition;
        mTravelDurationMs = 0;
        mTravelStartTime = 0;
        mTravelStartPosition = mCurrentPosition;

        logDebugP("Restore last state: %s", lOn ? "ON" : "OFF");
        sendPositionCommand(mCurrentPosition);
        return true;
    }

    uint8_t lReportedPosition = (uint8_t)getKo(IOHC_KoCHPositionFeedback).value(DPT_Scaling);
    if (lReportedPosition > 100)
        return false;

    float lDevicePosition = ParamIOHC_IOHCInvertDir ? (100.0f - lReportedPosition) : lReportedPosition;
    mCurrentPosition = clampPercent(lDevicePosition);
    mTargetPosition = mCurrentPosition;
    mTravelDurationMs = 0;
    mTravelStartTime = 0;
    mTravelStartPosition = mCurrentPosition;

    logDebugP("Restore last position: %d%%", lReportedPosition);
    sendPositionCommand(mCurrentPosition);
    return true;
}

void IoHomecontrolChannel::publishPositionFeedback(float iPositionPercent, bool iLogMessage)
{
    mCurrentPosition = clampPercent(iPositionPercent);

    float lReportPos = ParamIOHC_IOHCInvertDir ? (100.0f - mCurrentPosition) : mCurrentPosition;
    getKo(IOHC_KoCHPositionFeedback).value((uint8_t)(lReportPos + 0.5f), DPT_Scaling);
    publishBinaryStatus();

    if (iLogMessage)
        logDebugP("Position feedback: %.1f%%", mCurrentPosition);
}

void IoHomecontrolChannel::startTravelEstimation(float iTargetPositionPercent)
{
    float lCurrentPosition = clampPercent(estimateCurrentPosition());
    float lTargetPosition = clampPercent(iTargetPositionPercent);

    mCurrentPosition = lCurrentPosition;
    mTravelStartPosition = lCurrentPosition;
    mTargetPosition = lTargetPosition;
    mTravelDurationMs = estimateTravelDurationMs(lCurrentPosition, lTargetPosition,
                                                 configuredOpeningTimeSeconds(),
                                                 configuredClosingTimeSeconds());
    mTravelStartTime = millis();
}

void IoHomecontrolChannel::stopTravelEstimation(bool iPublishPosition)
{
    float lCurrentPosition = snapPositionBoundary(clampPercent(estimateCurrentPosition()));
    if (iPublishPosition)
        publishPositionFeedback(lCurrentPosition, false);
    else
        mCurrentPosition = lCurrentPosition;

    mTravelDurationMs = 0;
    mTravelStartTime = 0;
    mTravelStartPosition = mCurrentPosition;
    mTargetPosition = mCurrentPosition;
}

void IoHomecontrolChannel::updateEstimatedPosition()
{
    if (mTravelDurationMs == 0 || !mIsMoving)
        return;

    float lPreviousPosition = mCurrentPosition;
    float lEstimatedPosition = snapPositionBoundary(clampPercent(estimateCurrentPosition()));
    float lPreviousReported = ParamIOHC_IOHCInvertDir ? (100.0f - lPreviousPosition) : lPreviousPosition;
    float lEstimatedReported = ParamIOHC_IOHCInvertDir ? (100.0f - lEstimatedPosition) : lEstimatedPosition;

    mCurrentPosition = lEstimatedPosition;
    if ((uint8_t)(lEstimatedReported + 0.5f) != (uint8_t)(lPreviousReported + 0.5f))
    {
        getKo(IOHC_KoCHPositionFeedback).value((uint8_t)(lEstimatedReported + 0.5f), DPT_Scaling);
        publishBinaryStatus();
    }

    if (millis() - mTravelStartTime >= mTravelDurationMs)
    {
        mTravelDurationMs = 0;
        mTravelStartTime = 0;
        mTravelStartPosition = mCurrentPosition;
    }
}

float IoHomecontrolChannel::configuredOpeningTimeSeconds() const
{
    return ParamIOHC_IOHCOpeningTime > 0.0f ? ParamIOHC_IOHCOpeningTime : 0.0f;
}

float IoHomecontrolChannel::configuredClosingTimeSeconds() const
{
    return ParamIOHC_IOHCClosingTime > 0.0f ? ParamIOHC_IOHCClosingTime : 0.0f;
}

// --- P1: RSSI callback ---

void IoHomecontrolChannel::onRssiUpdate(uint8_t iScaledPercent)
{
    mLastRssi = iScaledPercent;
    getKo(IOHC_KoCHRssi).value(iScaledPercent, DPT_Scaling);
    logDebugP("RSSI: %d%%", iScaledPercent);
}

// --- P2: Lock control ---

void IoHomecontrolChannel::setLocked(bool iLocked)
{
    mLocked = iLocked;
}

bool IoHomecontrolChannel::isLocked() const
{
    return mLocked;
}

// --- P2: Error status ---

void IoHomecontrolChannel::setErrorStatus(uint8_t iStatus)
{
    if (mErrorStatus != iStatus)
    {
        mErrorStatus = iStatus;
        getKo(IOHC_KoCHErrorStatus).value(iStatus, DPT_DecimalFactor);
        logDebugP("Error status: %d", iStatus);
    }
}

uint8_t IoHomecontrolChannel::getErrorStatus() const
{
    return mErrorStatus;
}

// --- P3: Scene data ---

void IoHomecontrolChannel::setScenePosition(uint8_t iScene, uint8_t iPosition)
{
    if (iScene < kMaxSceneCount)
        mScenePositions[iScene] = iPosition;
}

uint8_t IoHomecontrolChannel::getScenePosition(uint8_t iScene) const
{
    if (iScene < kMaxSceneCount)
        return mScenePositions[iScene];
    return 0xFF;
}

void IoHomecontrolChannel::setSceneAction(uint8_t iScene, SceneAction iAction)
{
    if (iScene < kMaxSceneCount)
        mSceneActions[iScene] = iAction;
}

IoHomecontrolChannel::SceneAction IoHomecontrolChannel::getSceneAction(uint8_t iScene) const
{
    if (iScene < kMaxSceneCount)
        return mSceneActions[iScene];
    return SceneAction::Position;
}

void IoHomecontrolChannel::setSceneSlat(uint8_t iScene, uint8_t iSlatPosition)
{
    if (iScene < kMaxSceneCount)
        mSceneSlats[iScene] = iSlatPosition;
}

uint8_t IoHomecontrolChannel::getSceneSlat(uint8_t iScene) const
{
    if (iScene < kMaxSceneCount)
        return mSceneSlats[iScene];
    return 0;
}

uint8_t IoHomecontrolChannel::getConfiguredSceneCount() const
{
    uint8_t lSceneCount = ParamIOHC_IOHCSceneCount;
    return lSceneCount > kMaxSceneCount ? kMaxSceneCount : lSceneCount;
}

void IoHomecontrolChannel::loadSceneConfiguration()
{
    memset(mScenePositions, 0xFF, sizeof(mScenePositions));
    memset(mSceneSlats, 0, sizeof(mSceneSlats));
    for (uint8_t i = 0; i < kMaxSceneCount; i++)
        mSceneActions[i] = SceneAction::Position;

    uint8_t lSceneCount = getConfiguredSceneCount();
    for (uint8_t i = 0; i < lSceneCount; i++)
    {
        mScenePositions[i] = knx.paramByte(IOHC_ParamCalcIndex(IOHC_IOHCScene1Position + i));
        mSceneActions[i] = static_cast<SceneAction>(knx.paramByte(IOHC_ParamCalcIndex(IOHC_IOHCScene1Action + i)));
        mSceneSlats[i] = knx.paramByte(IOHC_ParamCalcIndex(IOHC_IOHCScene1Slat + i));
    }
}

float IoHomecontrolChannel::sceneToDevicePosition(uint8_t iScenePosition) const
{
    float lPosition = (float)iScenePosition;
    if (ParamIOHC_IOHCInvertDir)
        lPosition = 100.0f - lPosition;
    return lPosition;
}

uint8_t IoHomecontrolChannel::sceneToDeviceSlat(uint8_t iSceneSlat) const
{
    return iSceneSlat > 100 ? 100 : iSceneSlat;
}

uint8_t IoHomecontrolChannel::currentPositionToSceneValue() const
{
    float lPosition = ParamIOHC_IOHCInvertDir ? (100.0f - mCurrentPosition) : mCurrentPosition;
    if (lPosition < 0.0f)
        lPosition = 0.0f;
    if (lPosition > 100.0f)
        lPosition = 100.0f;
    return (uint8_t)(lPosition + 0.5f);
}

uint8_t IoHomecontrolChannel::currentSlatToSceneValue() const
{
    float lSlat = mCurrentSlat;
    if (lSlat < 0.0f)
        lSlat = 0.0f;
    if (lSlat > 100.0f)
        lSlat = 100.0f;
    return (uint8_t)(lSlat + 0.5f);
}

bool IoHomecontrolChannel::storeSceneStateToEts(uint8_t iSceneIndex, uint8_t iScenePosition, uint8_t iSceneSlat)
{
    if (iSceneIndex >= getConfiguredSceneCount())
        return false;

    uint8_t *lPositionData = knx.paramData(IOHC_ParamCalcIndex(IOHC_IOHCScene1Position + iSceneIndex));
    uint8_t *lSlatData = knx.paramData(IOHC_ParamCalcIndex(IOHC_IOHCScene1Slat + iSceneIndex));
    if (lPositionData == nullptr || lSlatData == nullptr)
        return false;

    *lPositionData = iScenePosition;
    *lSlatData = iSceneSlat;
    mScenePositions[iSceneIndex] = iScenePosition;
    mSceneSlats[iSceneIndex] = iSceneSlat;
    knx.writeMemory();
    return true;
}

// --- P1: Ventilation position ---

void IoHomecontrolChannel::sendVentilationPosition()
{
    // 2W ventilation has not been validated from a real capture yet.
    // Do not overload normal 2W Execute(0xD8, 0x03), because this may be
    // interpreted as favorite/special execute depending on the device.
    if (!mIs1W)
    {
        logDebugP("VENTILATION disabled for 2W: no validated capture available");
        return;
    }

    logDebugP("Send 1W VENTILATION");

    // 1W path: controller maps Execute(0xD8, 0x03) to IOHC_POSITION_VENT.
    if (!mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, 0xD8, 0x03))
        logDebugP("VENTILATION queue failed");
    else
        startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

// --- P3: Scene handling ---

void IoHomecontrolChannel::handleSceneRecall(uint8_t iScene)
{
    uint8_t lSceneCount = getConfiguredSceneCount();
    if (iScene < 1 || iScene > lSceneCount)
        return;

    uint8_t lSceneIndex = iScene - 1;
    uint8_t lDeviceType = ParamIOHC_IOHCDeviceType;

    // Thermostat scenes: temperature + cozy mode
    if (lDeviceType == 5)
    {
        uint8_t lTempC = mScenePositions[lSceneIndex];                    // 7-28 °C (overlaid param)
        uint8_t lMode = static_cast<uint8_t>(mSceneActions[lSceneIndex]); // cozy mode (overlaid param)
        uint16_t lTenths = (uint16_t)lTempC * 10;
        if (lTenths < 70)
            lTenths = 70;
        if (lTenths > 280)
            lTenths = 280;
        logDebugP("Scene %d recall -> thermostat: %d°C, mode %d", iScene, lTempC, lMode);
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x03, (uint8_t)lTenths);
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x04, lMode);
        return;
    }

    // Licht/Schloss/Schalter scenes: on/off
    if (lDeviceType == 6 || lDeviceType == 8 || lDeviceType == 12)
    {
        uint8_t lOnOff = mScenePositions[lSceneIndex]; // 0=off, 1=on (overlaid param)
        logDebugP("Scene %d recall -> %s", iScene, lOnOff ? "on" : "off");
        sendPositionCommand(lOnOff ? 100.0f : 0.0f);
        return;
    }

    // Position-type scenes (original logic)
    SceneAction lAction = mSceneActions[lSceneIndex];

    if (lAction == SceneAction::Favorite)
    {
        logDebugP("Scene %d recall -> favorite", iScene);
        sendFavorite();
        return;
    }

    if (lAction == SceneAction::Ventilation)
    {
        logDebugP("Scene %d recall -> ventilation", iScene);
        sendVentilationPosition();
        return;
    }

    uint8_t lPos = mScenePositions[lSceneIndex];
    if (lPos == 0xFF)
    {
        logDebugP("Scene %d not configured", iScene);
        return;
    }

    uint8_t lSlat = mSceneSlats[lSceneIndex];
    bool lUseSlat = lDeviceType == 1;

    if (lUseSlat)
        logDebugP("Scene %d recall -> %d%%, lamella %d%%", iScene, lPos, lSlat);
    else
        logDebugP("Scene %d recall -> %d%%", iScene, lPos);

    sendPositionCommand(sceneToDevicePosition(lPos), lUseSlat ? sceneToDeviceSlat(lSlat) : 0xFF);
}

void IoHomecontrolChannel::handleSceneControl(uint8_t iControl)
{
    uint8_t lScene = (iControl & 0x3F) + 1;
    bool lStore = (iControl & 0x80) != 0;
    uint8_t lSceneCount = getConfiguredSceneCount();

    if (lScene < 1 || lScene > lSceneCount)
        return;

    uint8_t lSceneIndex = lScene - 1;
    uint8_t lDeviceType = ParamIOHC_IOHCDeviceType;

    // Thermostat/Licht/Schloss/Schalter: store not supported (ETS-configured only)
    if (lDeviceType == 5 || lDeviceType == 6 || lDeviceType == 8 || lDeviceType == 12)
    {
        if (lStore)
            logDebugP("Scene %d learn ignored: not supported for device type %d", lScene, lDeviceType);
        else
            handleSceneRecall(lScene);
        return;
    }

    // Position-type scene store/recall
    if (mSceneActions[lSceneIndex] != SceneAction::Position)
    {
        if (lStore)
            logDebugP("Scene %d learn ignored: only Position-Szenen sind lernbar", lScene);
        else
            handleSceneRecall(lScene);
        return;
    }

    if (lStore)
    {
        uint8_t lPos = currentPositionToSceneValue();
        uint8_t lSlat = lDeviceType == 1 ? currentSlatToSceneValue() : 0;
        if (storeSceneStateToEts(lSceneIndex, lPos, lSlat))
        {
            if (lDeviceType == 1)
                logDebugP("Scene %d store: %d%%, lamella %d%%", lScene, lPos, lSlat);
            else
                logDebugP("Scene %d store: %d%%", lScene, lPos);
        }
    }
    else
    {
        handleSceneRecall(lScene);
    }
}

// --- P3: Wind/Rain alarm ---

void IoHomecontrolChannel::handleWindAlarm(bool iAlarm)
{
    if (iAlarm)
    {
        logDebugP("WIND/RAIN ALARM — safety action");
        uint8_t lDevType = ParamIOHC_IOHCDeviceType;
        switch (lDevType)
        {
        case 2: // Fenster — close
            sendPositionCommand(0.0f);
            break;
        case 3: // Markise — retract
            sendPositionCommand(0.0f);
            break;
        default: // Jalousie/Generic/Garage — move up
            sendUpDown(false);
            break;
        }
    }
    else
    {
        logDebugP("Wind/Rain alarm cleared");
    }
}

// --- P3: Step-Stop ---

void IoHomecontrolChannel::handleStepStop(bool iDown)
{
    if (mIsMoving)
    {
        // If moving, stop
        sendStop();
    }
    else
    {
        // If idle, start moving
        if (ParamIOHC_IOHCInvertDir)
            iDown = !iDown;
        sendUpDown(iDown);
    }
}

GroupObject &IoHomecontrolChannel::getKo(uint8_t iIoIndex)
{
    // Use the generated IOHC_KoCalcNumber macro from knxprod.h
    // This requires _channelIndex to be set (it is, from constructor)
    return knx.getGroupObject(IOHC_KoCalcNumber(iIoIndex));
}
