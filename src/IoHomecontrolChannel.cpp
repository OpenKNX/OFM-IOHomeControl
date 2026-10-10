#include "protocol/IoHomeStandaloneDiag.h"
#include "protocol/IoHomeProfileObjects.h"
#include "IoHomecontrolChannel.h"
#include "IoHomecontrol.h"
#include "controller/IoHomeController.h"
#include "protocol/IoHomeCommands.h"
#include "protocol/IoHomeProfileRegistry.h"
#include "knxprod.h"
#include "OpenKNX.h"

#ifndef ParamIOHC_cDimmable
#define ParamIOHC_cDimmable 0
#endif

#ifndef ParamIOHC_cSilentOperation
#define ParamIOHC_cSilentOperation 0
#endif

#ifndef ParamIOHC_cOneWayEnrollmentFinalizer
#define ParamIOHC_cOneWayEnrollmentFinalizer 0
#endif

#ifndef ParamIOHC_cTwoWayPowerClass
#define ParamIOHC_cTwoWayPowerClass 0
#endif
#ifndef ParamIOHC_cTwoWayDiscoverConfirmMode
#define ParamIOHC_cTwoWayDiscoverConfirmMode 1
#endif
#ifndef ParamIOHC_cTwoWayKeyInitDelay
#define ParamIOHC_cTwoWayKeyInitDelay 300
#endif

#ifndef ParamIOHC_cTwoWayDiscoveryCommand
#define ParamIOHC_cTwoWayDiscoveryCommand 0
#endif
#ifndef ParamIOHC_cTwoWayDiscoveryDestination
#define ParamIOHC_cTwoWayDiscoveryDestination 0
#endif
#ifndef ParamIOHC_cTwoWayDiscoveryAck
#define ParamIOHC_cTwoWayDiscoveryAck 0
#endif
#ifndef ParamIOHC_cTwoWayDiscoveryLowPower
#define ParamIOHC_cTwoWayDiscoveryLowPower 0
#endif
#ifndef ParamIOHC_cTwoWayDiscoveryPreamble
#define ParamIOHC_cTwoWayDiscoveryPreamble 0
#endif
#ifndef ParamIOHC_cTwoWayDiscoveryListenChannels
#define ParamIOHC_cTwoWayDiscoveryListenChannels 0
#endif
#ifndef ParamIOHC_cOneWayExecuteDestination
#define ParamIOHC_cOneWayExecuteDestination 0
#endif
#ifndef ParamIOHC_cOneWayEnrollmentDestination
#define ParamIOHC_cOneWayEnrollmentDestination 0
#endif
#ifndef ParamIOHC_cOneWayEnrollmentClasses
#define ParamIOHC_cOneWayEnrollmentClasses 0
#endif
#ifndef ParamIOHC_cOneWayPowerClass
#define ParamIOHC_cOneWayPowerClass 0
#endif
#ifndef ParamIOHC_cTwoWayAcei
#define ParamIOHC_cTwoWayAcei IOHC_ACEI_DEFAULT
#endif

#ifndef ParamIOHC_cProfileOverride
#define ParamIOHC_cProfileOverride 0
#endif

// ---------------------------------------------------------------------------
// Scene parameter access
//
// The knxprod generator emits the per-scene ETS parameters as a flat block in
// which each field (Position, Action, Slat) keeps a fixed stride of one byte
// between consecutive scenes:
//
//   Scene1Position = 14, Scene2Position = 15, ... Scene10Position = 23
//   Scene1Action   = 24, Scene2Action   = 25, ... Scene10Action   = 33
//   Scene1Slat     = 34, Scene2Slat     = 35, ... Scene10Slat     = 43
//
// loadSceneConfiguration() and storeSceneStateToEts() exploit this layout by
// indexing the first-scene constant with the (0-based) scene number. The macro
// below makes that access explicit; the static_asserts pin the layout so that a
// future change of the ETS parameter structure breaks the build at compile time
// instead of silently reading or writing the wrong bytes.
#define IOHC_SCENE_PARAM(field, sceneIndex) \
    IOHC_ParamCalcIndex(IOHC_cScene1##field + (sceneIndex))

#define IOHC_ASSERT_SCENE_LAYOUT(scene)                                                 \
    static_assert(IOHC_cScene##scene##Position - IOHC_cScene1Position == ((scene) - 1), \
                  "IO-Homecontrol scene Position parameters must keep a stride of 1");  \
    static_assert(IOHC_cScene##scene##Action - IOHC_cScene1Action == ((scene) - 1),     \
                  "IO-Homecontrol scene Action parameters must keep a stride of 1");    \
    static_assert(IOHC_cScene##scene##Slat - IOHC_cScene1Slat == ((scene) - 1),         \
                  "IO-Homecontrol scene Slat parameters must keep a stride of 1")

static_assert(IoHomecontrolChannel::kMaxSceneCount == 10,
              "Scene layout asserts below cover scenes 1..10; update them when kMaxSceneCount changes");

IOHC_ASSERT_SCENE_LAYOUT(2);
IOHC_ASSERT_SCENE_LAYOUT(3);
IOHC_ASSERT_SCENE_LAYOUT(4);
IOHC_ASSERT_SCENE_LAYOUT(5);
IOHC_ASSERT_SCENE_LAYOUT(6);
IOHC_ASSERT_SCENE_LAYOUT(7);
IOHC_ASSERT_SCENE_LAYOUT(8);
IOHC_ASSERT_SCENE_LAYOUT(9);
IOHC_ASSERT_SCENE_LAYOUT(10);

#undef IOHC_ASSERT_SCENE_LAYOUT
// ---------------------------------------------------------------------------

namespace
{
    void cancelPendingBatteryWrite(GroupObject &ko)
    {
        // A valid KO stays readable until reboot. Only cancel an unsent write;
        // do not initialize an unknown KO or alter an in-flight telegram.
        if (ko.commFlag() == WriteRequest) ko.commFlag(Ok);
    }

    constexpr uint32_t kTrackedStatusPollDefaultMs = 2000UL;
    constexpr uint32_t kTrackedStatusEstimateBiasMs = 1000UL;
    constexpr uint32_t kTrackedStatusPollWindowMs = 10UL * 60UL * 1000UL;
    constexpr uint32_t kLowPowerTrackedPollMinMs = 10UL * 1000UL;
    constexpr uint32_t kLowPowerBackgroundPollMinMs = 60UL * 1000UL;

    uint8_t resolveOneWayBroadcastType(uint8_t iConfiguredType, uint8_t iDeviceType)
    {
        if (iConfiguredType != 0xFF)
            return iConfiguredType & 0x3F;

        return IoHomeController::oneWayBroadcastTypeForEtsDeviceType(iDeviceType);
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
        case IoHomeDeviceType::Screen:
            return "screen";
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
    if(ioHomeStandaloneChannel(_channelIndex)) {
        setIs1W(false);setManualProfileOverride(0);
        setConfigured2WPowerClass(TwoWayPowerClass::Automatic);
        setConfigured2WDiscoverConfirmMode(PairingDiscoverConfirmMode::Send);
        setConfigured2WKeyInitDelay(300);setConfigured2WAcei(IOHC_ACEI_DEFAULT);
        setConfigured2WDiscoverySettings(TwoWayDiscoverySettings{});
        logInfoP("Standalone diagnostic channel: active, 2W, automatic power, default discovery; no ETS required");
        return;
    }
    if(!knx.configured())return;

    const uint8_t lProtocolMode = static_cast<uint8_t>(ParamIOHC_cProtocolMode);
    const uint32_t lOneWayTargetNodeId = static_cast<uint32_t>(ParamIOHC_cOneWayTargetNodeId) & 0x00FFFFFF;
    const uint8_t lOneWayBroadcastType = static_cast<uint8_t>(ParamIOHC_cOneWayBroadcastType);
    const uint8_t lOneWayProfileChannel = static_cast<uint8_t>(ParamIOHC_cOneWayProfileChannel);
    const uint8_t lOneWayManufacturer = static_cast<uint8_t>(ParamIOHC_cOneWayManufacturer);
    const uint8_t lOneWayAcei = static_cast<uint8_t>(ParamIOHC_cOneWayAcei);
    const bool lOneWayEnrollmentMac = ParamIOHC_cOneWayEnrollmentMac != 0;
    const uint8_t lOneWayEnrollmentFinalizer = static_cast<uint8_t>(ParamIOHC_cOneWayEnrollmentFinalizer);
    const uint8_t lOneWayExecuteDestination = static_cast<uint8_t>(ParamIOHC_cOneWayExecuteDestination);
    const uint8_t lOneWayEnrollmentDestination = static_cast<uint8_t>(ParamIOHC_cOneWayEnrollmentDestination);
    const uint8_t lOneWayEnrollmentClasses = static_cast<uint8_t>(ParamIOHC_cOneWayEnrollmentClasses);
    const uint8_t lOneWayPowerClass = static_cast<uint8_t>(ParamIOHC_cOneWayPowerClass);
    const uint8_t lTwoWayPowerClass = static_cast<uint8_t>(ParamIOHC_cTwoWayPowerClass);
    const uint8_t lTwoWayDiscoverConfirmMode = static_cast<uint8_t>(ParamIOHC_cTwoWayDiscoverConfirmMode);
    const uint16_t lTwoWayKeyInitDelay = static_cast<uint16_t>(ParamIOHC_cTwoWayKeyInitDelay);
    const uint8_t lTwoWayDiscoveryCommand = static_cast<uint8_t>(ParamIOHC_cTwoWayDiscoveryCommand);
    const uint8_t lTwoWayDiscoveryDestination = static_cast<uint8_t>(ParamIOHC_cTwoWayDiscoveryDestination);
    const uint8_t lTwoWayDiscoveryAck = static_cast<uint8_t>(ParamIOHC_cTwoWayDiscoveryAck);
    const uint8_t lTwoWayDiscoveryLowPower = static_cast<uint8_t>(ParamIOHC_cTwoWayDiscoveryLowPower);
    const uint8_t lTwoWayDiscoveryPreamble = static_cast<uint8_t>(ParamIOHC_cTwoWayDiscoveryPreamble);
    const uint8_t lTwoWayDiscoveryListenChannels = static_cast<uint8_t>(ParamIOHC_cTwoWayDiscoveryListenChannels);
    const uint8_t lTwoWayAcei = static_cast<uint8_t>(ParamIOHC_cTwoWayAcei);


    // The visible combined channel selector is synchronized to these historic
    // fields so existing ETS projects retain their activation state and type.
    const bool lChannelActive = ParamIOHC_cActive && !ParamIOHC_cSuspend;

    logInfoP("ETS config: active=%u protocol=%u (%s) oneWayTarget=0x%06X oneWayType=%u oneWayProfile=%u oneWayMfg=0x%02X",
             lChannelActive ? 1U : 0U,
             static_cast<unsigned>(lProtocolMode),
             lProtocolMode == 1 ? "1W" : "2W",
             static_cast<unsigned long>(lOneWayTargetNodeId),
             static_cast<unsigned>(lOneWayBroadcastType),
             static_cast<unsigned>(lOneWayProfileChannel),
             static_cast<unsigned>(lOneWayManufacturer));

    // Check if channel is active in ETS (activated and not suspended)
    if (!lChannelActive)
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
    setConfigured2WPowerClass(
        lTwoWayPowerClass <= static_cast<uint8_t>(TwoWayPowerClass::LowPower)
            ? static_cast<TwoWayPowerClass>(lTwoWayPowerClass)
            : TwoWayPowerClass::Automatic);
    setConfigured2WDiscoverConfirmMode(
        lTwoWayDiscoverConfirmMode <= static_cast<uint8_t>(PairingDiscoverConfirmMode::SendWithAck)
            ? static_cast<PairingDiscoverConfirmMode>(lTwoWayDiscoverConfirmMode)
            : PairingDiscoverConfirmMode::Send);
    setConfigured2WKeyInitDelay(lTwoWayKeyInitDelay <= 10000 ? lTwoWayKeyInitDelay : 300);
    setConfigured2WAcei(lTwoWayAcei == 0x63 ? 0x63 : IOHC_ACEI_DEFAULT);
    setManualProfileOverride(static_cast<uint16_t>(ParamIOHC_cProfileOverride));
    TwoWayDiscoverySettings lDiscoverySettings;
    if (lTwoWayDiscoveryCommand <= static_cast<uint8_t>(TwoWayDiscoveryCommandMode::DiscoverSPE))
        lDiscoverySettings.command = static_cast<TwoWayDiscoveryCommandMode>(lTwoWayDiscoveryCommand);
    if (lTwoWayDiscoveryDestination <= static_cast<uint8_t>(TwoWayDiscoveryDestinationMode::LightingDiscoverAlt))
        lDiscoverySettings.destination = static_cast<TwoWayDiscoveryDestinationMode>(lTwoWayDiscoveryDestination);
    if (lTwoWayDiscoveryAck <= static_cast<uint8_t>(TwoWayDiscoveryFlagMode::On))
        lDiscoverySettings.ack = static_cast<TwoWayDiscoveryFlagMode>(lTwoWayDiscoveryAck);
    if (lTwoWayDiscoveryLowPower <= static_cast<uint8_t>(TwoWayDiscoveryFlagMode::On))
        lDiscoverySettings.lowPower = static_cast<TwoWayDiscoveryFlagMode>(lTwoWayDiscoveryLowPower);
    if (lTwoWayDiscoveryPreamble <= static_cast<uint8_t>(TwoWayDiscoveryPreambleMode::Short))
        lDiscoverySettings.preamble = static_cast<TwoWayDiscoveryPreambleMode>(lTwoWayDiscoveryPreamble);
    lDiscoverySettings.listenChannels = lTwoWayDiscoveryListenChannels == 1
                                            ? TwoWayDiscoveryListenChannels::All
                                            : TwoWayDiscoveryListenChannels::PreferAlternateRecentAware;
    setConfigured2WDiscoverySettings(lDiscoverySettings);
    setConfigured1WTargetNodeId(lOneWayTargetNodeId);
    setConfigured1WBroadcastType(resolveOneWayBroadcastType(
        lOneWayBroadcastType,
        static_cast<uint8_t>(ParamIOHC_cDeviceType)));
    const uint8_t lProfileChannel = lOneWayProfileChannel;
    setConfigured1WProfileChannel(lProfileChannel == 0 ? 0xFF : static_cast<uint8_t>(lProfileChannel - 1));
    setConfigured1WAcei(lOneWayAcei);
    setConfigured1WEnrollmentMac(lOneWayEnrollmentMac);
    setConfigured1WEnrollmentFinalizer(
        lOneWayEnrollmentFinalizer <= static_cast<uint8_t>(OneWayEnrollmentFinalizer::StopDown)
            ? static_cast<OneWayEnrollmentFinalizer>(lOneWayEnrollmentFinalizer)
            : OneWayEnrollmentFinalizer::Automatic);
    setConfigured1WExecuteDestinationPolicy(
        lOneWayExecuteDestination <= static_cast<uint8_t>(OneWayExecuteDestinationPolicy::All)
            ? static_cast<OneWayExecuteDestinationPolicy>(lOneWayExecuteDestination)
            : OneWayExecuteDestinationPolicy::Automatic);
    setConfigured1WEnrollmentDestinationPolicy(
        lOneWayEnrollmentDestination <= static_cast<uint8_t>(OneWayEnrollmentDestinationPolicy::Typed)
            ? static_cast<OneWayEnrollmentDestinationPolicy>(lOneWayEnrollmentDestination)
            : OneWayEnrollmentDestinationPolicy::Automatic);
    setConfigured1WEnrollmentClassMask(lOneWayEnrollmentClasses);
    setConfigured1WPowerClass(
        lOneWayPowerClass <= static_cast<uint8_t>(OneWayPowerClass::LowPower)
            ? static_cast<OneWayPowerClass>(lOneWayPowerClass)
            : OneWayPowerClass::Automatic);
#ifdef MVS_ParamBlockOffset
    mMovementMode = ParamMVS_cMovementMode <= 2 ? ParamMVS_cMovementMode : 0;
    if (ParamBASE_ModuleEnabled_MVS && ParamMVS_cMovementModeObject)
        knx.getGroupObject(MVS_KoCalcNumber(MVS_KocMovementMode)).valueNoSend(mMovementMode, Dpt(5, 10));
#else
    mMovementMode = ParamIOHC_cSilentOperation ? 1 : 0;
#endif
    mConfigured1WManufacturer = lOneWayManufacturer;
    if (mConfigured1WManufacturer != 0)
        setOneWayControllerManufacturer(mConfigured1WManufacturer);

    if (mIs1W && mConfigured1WTargetNodeId == 0)
        logInfoP("Channel is configured as 1W but has no ETS 1W target node; pairing must provide a target node explicitly");

    logInfoP("Applied protocol config: %s target=0x%06X broadcastType=%u acei=0x%02X profile=%s manufacturer=0x%02X enrollFinalizer=%u power1W=%s movementMode=%u power2W=%s",
             mIs1W ? "1W" : "2W",
             static_cast<unsigned long>(mConfigured1WTargetNodeId),
             static_cast<unsigned>(mConfigured1WBroadcastType),
             static_cast<unsigned>(mConfigured1WAcei),
             mConfigured1WProfileChannel == 0xFF ? "own" : "linked",
             static_cast<unsigned>(mOneWayControllerManufacturer),
             static_cast<unsigned>(mConfigured1WEnrollmentFinalizer),
             IoHomeController::oneWayPowerClassName(mConfigured1WPowerClass),
             static_cast<unsigned>(mMovementMode),
             twoWayPowerClassName(mConfigured2WPowerClass));

    logDebugP("Setup (type=%d, poll=%ds, open=%.1fs, close=%.1fs, invert=%d, powerOn=%d, scenes=%d, 1w=%d, 1wTarget=%06X, 1wType=%u, 1wProfile=%u)",
              ParamIOHC_cDeviceType, ParamIOHC_cPollInterval,
              ParamIOHC_cOpeningTime, ParamIOHC_cClosingTime,
              ParamIOHC_cInvertDir, ParamIOHC_cPowerOnBeh,
              getConfiguredSceneCount(), mIs1W ? 1 : 0,
              mConfigured1WTargetNodeId, static_cast<unsigned>(mConfigured1WBroadcastType),
              mConfigured1WProfileChannel == 0xFF ? 0U : static_cast<unsigned>(mConfigured1WProfileChannel + 1));
}

void IoHomecontrolChannel::loop()
{
    if(!knx.configured())return; // RF callbacks/console remain active; no ETS polling or KOs

    updateLimitationStatus();
    updateBatteryKo();
    updateProfileParameterValidity();
    publishProductState();
    if (!isOperational())
        return;

    if (!ParamIOHC_cActive || ParamIOHC_cSuspend)
        return;

    const uint32_t lNow = millis();

    updateEstimatedPosition();

    // 1W (one-way) channels have no return path: the device never sends an
    // authenticated status back, so any 2W status poll would fail forever and
    // reschedule itself. Skip all status-poll handling for 1W; position is
    // tracked purely by travel-time estimation above.
    if (mIs1W)
        return;

    if (mPollTrackingDeadlineMs != 0 &&
        timeReached(lNow, mPollTrackingDeadlineMs) &&
        mNextStatusPollMs == 0 &&
        !mSingleFollowUpPollPending)
    {
        clearStatusPollTracking();
    }

    if (mNextStatusPollMs != 0 && timeReached(lNow, mNextStatusPollMs))
    {
        if (requestStatus(true))
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
    uint16_t lPollSec = ParamIOHC_cPollInterval;
    if (lPollSec > 0 && !isStatusPollTrackingActive(lNow))
    {
        uint32_t lPollMs = (uint32_t)lPollSec * 1000;
        if (effectiveLowPower2W() && lPollMs < kLowPowerBackgroundPollMinMs)
            lPollMs = kLowPowerBackgroundPollMinMs;
        if (delayCheck(mStatusPollTimer, lPollMs))
        {
            requestStatus();
            mStatusPollTimer = delayTimerInit();
        }
    }
}

void IoHomecontrolChannel::processInputKo(uint8_t iIoIndex, GroupObject &iKo)
{
    if (!isOperational())
    {
        logDebugP("Ignoring KO %d - %s", iIoIndex, mIs1W ? "1W not enrolled" : "not paired");
        return;
    }

    if (!allowsActuatorControls() && iIoIndex != IOHC_KoCHLock)
    {
        logDebugP("Ignoring actuator KO %d for known %s node", iIoIndex,
                  ioHomeNodeClassName(mProtocolIdentity.nodeClass));
        return;
    }

    // P2: Lock check — only Lock and WindAlarm KOs bypass the lock
    if (mLocked && iIoIndex != IOHC_KoCHLock && iIoIndex != IOHC_KoCHWindAlarm)
    {
        logDebugP("Channel locked, ignoring KO %d", iIoIndex);
        return;
    }

    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
    {
        const IoHomeGenericCapabilities lCaps = ioHomeProfileCapabilities(lDescriptor);
        const bool lBinaryOnly = (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly) != 0;
        bool lSupported = true;
        switch (iIoIndex)
        {
        case IOHC_KoCHPosition:
            lSupported = !lBinaryOnly &&
                         (lCaps.position || lCaps.light || lCaps.ventilation || lCaps.heating);
            break;
        case IOHC_KoCHUpDown:
        case IOHC_KoCHStepStop:
        case IOHC_KoCHStop:
            lSupported = !lBinaryOnly && lCaps.position;
            break;
        case IOHC_KoCHOnOff:
            lSupported = lCaps.onOff || lCaps.light || lCaps.lock || lCaps.heating;
            break;
        case IOHC_KoCHSlat:
            lSupported = lCaps.tilt;
            break;
        case IOHC_KoCHFavorite:
            lSupported = !lBinaryOnly && lCaps.position;
            break;
        case IOHC_KoCHVentilation:
            lSupported = lDescriptor->securedVentilation;
            break;
        default:
            break;
        }
        if (!lSupported)
        {
            logDebugP("Ignoring KO %u unsupported by profile %s",
                      static_cast<unsigned>(iIoIndex), lDescriptor->label);
            return;
        }
    }

    switch (iIoIndex)
    {
    case IOHC_KoCHPosition:
    {
        float lPercent = (float)iKo.value(DPT_Scaling);
        if (ParamIOHC_cInvertDir)
            lPercent = 100.0f - lPercent;
        sendPositionCommand(lPercent);
        break;
    }
    case IOHC_KoCHUpDown:
    {
        bool lDown = iKo.value(DPT_UpDown);
        if (ParamIOHC_cInvertDir)
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
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x03, lTenths);
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

void IoHomecontrolChannel::observe2WMovingStatus(bool iIsMoving)
{
    const uint32_t lNow = millis();
    mHas2WHeardEvidence = true;
    mLast2WHeardMs = lNow;
    ++m2WMovingEvidenceGeneration;
    mHas2WMovingEvidence = iIsMoving;
    if (iIsMoving)
        mLast2WMovingEvidenceMs = lNow;
}

void IoHomecontrolChannel::onStatusUpdate(bool iIsMoving)
{
    observe2WMovingStatus(iIsMoving);

    mStatusPollTimer = delayTimerInit();
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;

    if (!iIsMoving)
    {
        clearStopTravelSnapshot();
        clearStatusPollTracking();
        stopTravelEstimation(false);
    }
    else if (mStopTravelSnapshot.valid)
    {
        // A real moving status resolves an ambiguous STOP in favour of the
        // pre-STOP trajectory.
        restoreStopTravelSnapshot();
    }
    else if (mTravelDurationMs == 0 && mTargetPosition != mCurrentPosition)
        startTravelEstimation(mTargetPosition);

    if (iIsMoving)
    {
        const uint32_t lDelayMs = defaultTrackedStatusPollDelayMs();
        const uint32_t lNow = millis();
        if (mPollTrackingDeadlineMs == 0)
            mPollTrackingDeadlineMs = lNow + kTrackedStatusPollWindowMs;
        if (mNextStatusPollMs == 0 && (mSingleFollowUpPollPending || mStatusExpected))
            mNextStatusPollMs = lNow + lDelayMs;
    }

    mIsMoving = iIsMoving;
    if (!isBinaryDeviceType())
        if(knx.configured())getKo(IOHC_KoCHMovementStatus).value(iIsMoving, Dpt(1, 11));
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
            logDebugP("Received device status for %06X: %s (0x%03X/0x%02X) / Position %.1f / Target %.1f / Moving: %s / Deleted: %s",
                      mNodeId, mDeviceName,
                      static_cast<unsigned>(mProfile),
                      static_cast<unsigned>(mSubProfile),
                      iCurrentPositionPercent, iTargetPositionPercent,
                      iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
        }
        else if (iHasCurrentPosition)
        {
            logDebugP("Received device status for %06X: %s (0x%03X/0x%02X) / Position %.1f / Moving: %s / Deleted: %s",
                      mNodeId, mDeviceName,
                      static_cast<unsigned>(mProfile),
                      static_cast<unsigned>(mSubProfile),
                      iCurrentPositionPercent, iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
        }
        else
        {
            logDebugP("Received device status for %06X: %s (0x%03X/0x%02X) / Moving: %s / Deleted: %s",
                      mNodeId, mDeviceName,
                      static_cast<unsigned>(mProfile),
                      static_cast<unsigned>(mSubProfile),
                      iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
        }
        return;
    }

    if (iHasCurrentPosition && iHasTargetPosition)
    {
        logDebugP("Received device status for %06X: %s (0x%03X/0x%02X) / Position %.1f / Target %.1f / Moving: %s / Deleted: %s",
                  mNodeId, iohcDeviceTypeLabel(mProfile),
                  static_cast<unsigned>(mProfile),
                  static_cast<unsigned>(mSubProfile),
                  iCurrentPositionPercent, iTargetPositionPercent,
                  iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
    }
    else if (iHasCurrentPosition)
    {
        logDebugP("Received device status for %06X: %s (0x%03X/0x%02X) / Position %.1f / Moving: %s / Deleted: %s",
                  mNodeId, iohcDeviceTypeLabel(mProfile),
                  static_cast<unsigned>(mProfile),
                  static_cast<unsigned>(mSubProfile),
                  iCurrentPositionPercent, iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
    }
    else
    {
        logDebugP("Received device status for %06X: %s (0x%03X/0x%02X) / Moving: %s / Deleted: %s",
                  mNodeId, iohcDeviceTypeLabel(mProfile),
                  static_cast<unsigned>(mProfile),
                  static_cast<unsigned>(mSubProfile),
                  iIsMoving ? "Yes" : "No", mPaired ? "No" : "Yes");
    }
}

void IoHomecontrolChannel::onSlatFeedback(float iSlatPercent)
{
    mCurrentSlat = iSlatPercent;
    if(knx.configured())getKo(IOHC_KoCHSlatFeedback).value((uint8_t)(iSlatPercent + 0.5f), DPT_Scaling);
    logDebugP("Slat feedback: %.1f%%", iSlatPercent);
}

void IoHomecontrolChannel::onScalarFeedback(float iPercent)
{
    // Binary/light/heating status shares the channel's numeric state but is
    // not published as a cover-position KO.
    const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor();
    mCurrentPosition = lDescriptor && (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly)
                           ? (iPercent >= 50.0f ? 100.0f : 0.0f)
                           : clampPercent(iPercent);
    mTargetPosition = mCurrentPosition;
    publishBinaryStatus();
    logDebugP("Scalar feedback: %.1f%%", mCurrentPosition);
}

void IoHomecontrolChannel::onVelocityFeedback(ParameterSemantic iSemantic,
                                               float iPercent)
{
    mCurrentVelocity = clampPercent(iPercent);
    logDebugP("%s feedback: %.1f%%",
              ioHomeParameterSemanticName(iSemantic), mCurrentVelocity);
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

namespace
{
    uint8_t copyEnrichmentPayload(uint8_t *oTarget, const uint8_t *iData,
                                  uint8_t iDataLen)
    {
        const uint8_t lLength = iDataLen < IOHC_DEVICE_INFO_RAW_MAX_SIZE
                                    ? iDataLen
                                    : IOHC_DEVICE_INFO_RAW_MAX_SIZE;
        memset(oTarget, 0, IOHC_DEVICE_INFO_RAW_MAX_SIZE);
        if (iData && lLength > 0)
            memcpy(oTarget, iData, lLength);
        return lLength;
    }

    std::string enrichmentHex(const uint8_t *iData, uint8_t iDataLen)
    {
        static const char kHex[] = "0123456789ABCDEF";
        std::string lResult;
        lResult.reserve(static_cast<size_t>(iDataLen) * 2U);
        for (uint8_t i = 0; i < iDataLen; i++)
        {
            lResult.push_back(kHex[(iData[i] >> 4) & 0x0F]);
            lResult.push_back(kHex[iData[i] & 0x0F]);
        }
        return lResult;
    }

    std::string enrichmentAscii(const uint8_t *iData, uint8_t iDataLen)
    {
        std::string lResult;
        lResult.reserve(iDataLen);
        for (uint8_t i = 0; i < iDataLen; i++)
            lResult.push_back(iData[i] >= 0x20 && iData[i] <= 0x7E
                                  ? static_cast<char>(iData[i])
                                  : '.');
        return lResult;
    }
}

void IoHomecontrolChannel::onPostPairEnrichmentResponse(
    IoHomeCommand iResponse, const uint8_t *iData, uint8_t iDataLen)
{
    switch (iResponse)
    {
    case IoHomeCommand::GetNameResponse:
        mProductIdentityEvidence.nameResponseLen = copyEnrichmentPayload(
            mProductIdentityEvidence.nameResponse, iData, iDataLen);
        onDeviceName(
            reinterpret_cast<const char *>(mProductIdentityEvidence.nameResponse),
            mProductIdentityEvidence.nameResponseLen);
        break;

    case IoHomeCommand::GetGeneralInfo1Response:
        invalidateProductContext();
        mProductIdentityEvidence.generalInfo1Len = copyEnrichmentPayload(
            mProductIdentityEvidence.generalInfo1, iData, iDataLen);
        logDebugP("GeneralInfo1 raw=%s ascii=%s",
                  enrichmentHex(mProductIdentityEvidence.generalInfo1,
                                mProductIdentityEvidence.generalInfo1Len).c_str(),
                  enrichmentAscii(mProductIdentityEvidence.generalInfo1,
                                  mProductIdentityEvidence.generalInfo1Len).c_str());
        break;

    case IoHomeCommand::GetGeneralInfo2Response:
        invalidateProductContext();
        mProductIdentityEvidence.generalInfo2Len = copyEnrichmentPayload(
            mProductIdentityEvidence.generalInfo2, iData, iDataLen);
        mProductIdentityEvidence.generalInfo2TypeValid = iDataLen >= 12;
        if (mProductIdentityEvidence.generalInfo2TypeValid)
        {
            mProductIdentityEvidence.generalInfo2Profile =
                decodePackedProfile(iData[10], iData[11]);
            mProductIdentityEvidence.generalInfo2SubProfile =
                decodePackedSubProfile(iData[11]);
            mProductIdentityEvidence.generalInfo2MatchesDiscovery =
                mProtocolIdentity.valid &&
                mProductIdentityEvidence.generalInfo2Profile == mProtocolIdentity.profile &&
                mProductIdentityEvidence.generalInfo2SubProfile == mProtocolIdentity.subProfile;
            if (mProtocolIdentity.valid)
            {
                logInfoP("GI2 profile validation: %s discovery=%u/%u raw=%s GI2=%u/%u raw=%s (discovery retained)",
                         mProductIdentityEvidence.generalInfo2MatchesDiscovery ? "exact-match" : "mismatch",
                         static_cast<unsigned>(mProtocolIdentity.profile),
                         static_cast<unsigned>(mProtocolIdentity.subProfile),
                         enrichmentHex(mProtocolIdentity.rawData, mProtocolIdentity.rawDataLen).c_str(),
                         static_cast<unsigned>(mProductIdentityEvidence.generalInfo2Profile),
                         static_cast<unsigned>(mProductIdentityEvidence.generalInfo2SubProfile),
                         enrichmentHex(mProductIdentityEvidence.generalInfo2,
                                       mProductIdentityEvidence.generalInfo2Len).c_str());
            }
        }
        break;

    case IoHomeCommand::GetGeneralInfo3Response:
        mProductIdentityEvidence.generalInfo3Len = copyEnrichmentPayload(
            mProductIdentityEvidence.generalInfo3, iData, iDataLen);
        mProductIdentityEvidence.generalInfo3Outcome =
            IoHomeGeneralInfo3Outcome::Response;
        memset(mProductIdentityEvidence.generalInfo3ErrorResponse, 0,
               sizeof(mProductIdentityEvidence.generalInfo3ErrorResponse));
        mProductIdentityEvidence.generalInfo3ErrorResponseLen = 0;
        break;

    default:
        break;
    }
    ioHomeUpdateVendorProductEvidence(mProtocolIdentity, mProductIdentityEvidence);
    if (mProductIdentityEvidence.manufacturerSignatureInconsistent &&
        (iResponse == IoHomeCommand::GetGeneralInfo1Response ||
         iResponse == IoHomeCommand::GetGeneralInfo2Response))
        logInfoP("Product signature manufacturer conflict: discovery=%u signatureDatabase=%u; discovery retained",
                 static_cast<unsigned>(mProtocolIdentity.manufacturerId),
                 static_cast<unsigned>(mProductIdentityEvidence.signatureManufacturerId));
}

void IoHomecontrolChannel::onGeneralInfo3Requested()
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

void IoHomecontrolChannel::onGeneralInfo3Failure(
    IoHomeGeneralInfo3Outcome iOutcome, const uint8_t *iData, uint8_t iDataLen)
{
    if (iOutcome != IoHomeGeneralInfo3Outcome::ErrorResponse &&
        iOutcome != IoHomeGeneralInfo3Outcome::Timeout &&
        iOutcome != IoHomeGeneralInfo3Outcome::TransportFailure)
        return;

    mProductIdentityEvidence.generalInfo3Outcome = iOutcome;
    mProductIdentityEvidence.generalInfo3ErrorResponseLen = copyEnrichmentPayload(
        mProductIdentityEvidence.generalInfo3ErrorResponse, iData, iDataLen);
}

void IoHomecontrolChannel::clearProductIdentityEvidence()
{
    invalidateProductContext();
    memset(mDeviceName, 0, sizeof(mDeviceName));
    mProductIdentityEvidence = IoHomeProductIdentityEvidence{};
}

void IoHomecontrolChannel::restoreProductIdentityEvidence(
    const IoHomeProductIdentityEvidence &iEvidence)
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

const IoHomeProductIdentityEvidence &IoHomecontrolChannel::getProductIdentityEvidence() const
{
    return mProductIdentityEvidence;
}

IoHomeProductSignature IoHomecontrolChannel::getGeneralInfo1ProductSignature() const
{
    return ioHomeGeneralInfo1ProductSignature(mProductIdentityEvidence);
}

const char *IoHomecontrolChannel::getDeviceName() const
{
    return mDeviceName;
}

void IoHomecontrolChannel::onProtocolIdentity(
    uint32_t iIoAddress, const IoHomeProtocolIdentity &iIdentity)
{
    if (!iIdentity.valid)
        return;

    // The RF source is ioAddress. Discovery data[2..4] is the independent
    // ioBackboneAddress and may legitimately be zero.
    mIoAddress = iIoAddress & 0x00FFFFFF;
    IoHomeProtocolIdentity lIdentity = iIdentity;
    lIdentity.ioAddress = mIoAddress;
    if (!ioHomeShouldAcceptProtocolIdentity(mProtocolIdentity, lIdentity))
        return;
    if (lIdentity.nodeClass == IoHomeNodeClass::Unknown &&
        mProtocolIdentity.valid && mProtocolIdentity.ioAddress == mIoAddress)
        lIdentity.nodeClass = mProtocolIdentity.nodeClass;
    // Hardware protocol identity is immutable with respect to ETS choices.
    // ETS device-role and command-shape parameters remain separate and never
    // write back into this discovery-derived object.
    if(ioHomeProductSemanticIdentityChanged(mProtocolIdentity,lIdentity))invalidateProductContext();
    mProtocolIdentity = lIdentity;
    ioHomeUpdateVendorProductEvidence(mProtocolIdentity, mProductIdentityEvidence);
    mProfile = lIdentity.profile;
    mSubProfile = lIdentity.subProfile;
    if (mManualPackedProfile != 0 &&
        mManualPackedProfile != ((lIdentity.profile << 6) | lIdentity.subProfile))
        logInfoP("Manual profile 0x%04X overrides discovered profile 0x%04X for behavior only",
                 mManualPackedProfile,
                 static_cast<unsigned>((lIdentity.profile << 6) | lIdentity.subProfile));
    logDebugP("Protocol identity: ioAddress=0x%06X profile=0x%04X subProfile=0x%02X manufacturerId=0x%02X ioBackboneAddress=%s MIB=%s",
              lIdentity.ioAddress, lIdentity.profile, lIdentity.subProfile,
              lIdentity.manufacturerId,
              lIdentity.hasIoBackboneAddress ? "present" : "n/a",
              lIdentity.hasMib ? "present" : "n/a");
}

void IoHomecontrolChannel::clearProtocolIdentity()
{
    invalidateProductContext();
    mIoAddress = 0;
    mProtocolIdentity = IoHomeProtocolIdentity{};
}

bool IoHomecontrolChannel::hasProtocolIdentity() const
{
    return mProtocolIdentity.valid;
}

uint32_t IoHomecontrolChannel::getIoAddress() const
{
    return mProtocolIdentity.valid ? mProtocolIdentity.ioAddress : mIoAddress;
}

const IoHomeProtocolIdentity &IoHomecontrolChannel::getProtocolIdentity() const
{
    return mProtocolIdentity;
}

IoHomeGenericCapabilities IoHomecontrolChannel::getProfileCapabilities() const
{
    return ioHomeProfileCapabilities(getEffectiveProfileDescriptor());
}

const IoHomeProfileDescriptor *IoHomecontrolChannel::getEffectiveProfileDescriptor() const
{
    if (mManualPackedProfile != 0)
        return ioHomeProfileDescriptor(mManualPackedProfile >> 6,
                                       mManualPackedProfile & 0x3F);
    return ioHomeProfileDescriptor(mProtocolIdentity);
}

void IoHomecontrolChannel::setManualProfileOverride(uint16_t iPackedType)
{
    if (iPackedType != 0 &&
        !ioHomeProfileDescriptor(iPackedType >> 6, iPackedType & 0x3F))
    {
        logInfoP("Ignoring unsupported manual profile 0x%04X", iPackedType);
        iPackedType = 0;
    }
    if (mManualPackedProfile != iPackedType) invalidateProductContext();
    mManualPackedProfile = iPackedType;
    if (mProtocolIdentity.valid && iPackedType != 0 &&
        iPackedType != ((mProtocolIdentity.profile << 6) | mProtocolIdentity.subProfile))
        logInfoP("Manual profile 0x%04X overrides discovered profile 0x%04X for behavior only",
                 iPackedType,
                 static_cast<unsigned>((mProtocolIdentity.profile << 6) | mProtocolIdentity.subProfile));
}

bool IoHomecontrolChannel::allowsActuatorControls() const
{
    // Detailed diagnosis-only selections occupy ETS categories 15 and above.
    // A restored actuator identity must not enable movement on these channels.
    if (ParamIOHC_cDeviceType >= 15) return false;
    return mProtocolIdentity.nodeClass == IoHomeNodeClass::Unknown ||
           mProtocolIdentity.nodeClass == IoHomeNodeClass::Actuator;
}

void IoHomecontrolChannel::onBatteryLevel(uint8_t iPercent)
{
    if(!mBatteryInfo.percentValid||iPercent>100||mBatteryInfo.percent!=iPercent)return;
    mBatteryLevel = iPercent;
    if(knx.configured())getKo(IOHC_KoCHBattery).value(iPercent, DPT_Scaling);
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
        if (effectiveLowPower2W() && lDelayMs < kLowPowerTrackedPollMinMs)
            lDelayMs = kLowPowerTrackedPollMinMs;

        mSingleFollowUpPollPending = true;
        mStatusPollFailures = 0;
        mAuthPollFailures = 0;
        const uint32_t lNow = millis();
        mNextStatusPollMs = lNow + lDelayMs;
        if (mPollTrackingDeadlineMs == 0)
            mPollTrackingDeadlineMs = lNow + kTrackedStatusPollWindowMs;

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
        mPollTrackingDeadlineMs = millis() + kTrackedStatusPollWindowMs;
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
    if (mPollTrackingDeadlineMs == 0)
        mPollTrackingDeadlineMs = lNow + kTrackedStatusPollWindowMs;

    const uint32_t lRetryDelayMs = lBaseDelayMs * lBackoffFactor;
    const uint32_t lRetryAtMs = lNow + lRetryDelayMs;
    if (timeReached(lRetryAtMs, mPollTrackingDeadlineMs))
    {
        // The bounded movement/remote-activity tracking window is over. Do
        // not keep extending it after every failed poll; normal ETS periodic
        // polling resumes once the original deadline is reached.
        mSingleFollowUpPollPending = false;
        mNextStatusPollMs = 0;
        logDebugP("Status poll failed%s, tracking window exhausted",
                  iAfterChallenge ? " after challenge" : "");
    }
    else
    {
        mSingleFollowUpPollPending = true;
        mNextStatusPollMs = lRetryAtMs;
        logDebugP("Status poll failed%s, retry in %lu ms",
                  iAfterChallenge ? " after challenge" : "",
                  static_cast<unsigned long>(lRetryDelayMs));
    }
}

// --- Pairing data ---

bool IoHomecontrolChannel::isPaired() const
{
    return mPaired;
}

bool IoHomecontrolChannel::isOneWayEnrolled() const
{
    return mOneWayEnrolled;
}

void IoHomecontrolChannel::setOneWayEnrolled(bool iEnrolled)
{
    mOneWayEnrolled = iEnrolled;
}

bool IoHomecontrolChannel::isOperational() const
{
    return mIs1W ? mOneWayEnrolled : mPaired;
}

void IoHomecontrolChannel::setNodeId(uint32_t iNodeId)
{
    mNodeId = iNodeId & 0x00FFFFFF; // 24-bit
    mPaired = (mNodeId != 0);
    mProductRuntime.bind(mNodeId,mEncKey);
}

uint32_t IoHomecontrolChannel::getNodeId() const
{
    return mNodeId;
}

void IoHomecontrolChannel::setEncryptionKey(const uint8_t *iKey)
{
    memcpy(mEncKey, iKey, 16);
    mProductRuntime.bind(mNodeId,mEncKey);
}

const uint8_t *IoHomecontrolChannel::getEncryptionKey() const
{
    return mEncKey;
}

void IoHomecontrolChannel::setLowPower2W(bool iLowPower)
{
    mLowPower2W = iLowPower;
    mHasLearnedLowPower2W = true;
}

void IoHomecontrolChannel::clearLearnedLowPower2W()
{
    mLowPower2W = false;
    mHasLearnedLowPower2W = false;
}

bool IoHomecontrolChannel::isLowPower2W() const
{
    return mLowPower2W;
}

bool IoHomecontrolChannel::hasLearnedLowPower2W() const
{
    return mHasLearnedLowPower2W;
}

void IoHomecontrolChannel::setConfigured2WPowerClass(TwoWayPowerClass iPowerClass)
{
    mConfigured2WPowerClass = iPowerClass;
}

TwoWayPowerClass IoHomecontrolChannel::getConfigured2WPowerClass() const
{
    return mConfigured2WPowerClass;
}

void IoHomecontrolChannel::setConfigured2WDiscoverConfirmMode(PairingDiscoverConfirmMode iMode)
{
    mConfigured2WDiscoverConfirmMode = iMode;
}

PairingDiscoverConfirmMode IoHomecontrolChannel::getConfigured2WDiscoverConfirmMode() const
{
    return mConfigured2WDiscoverConfirmMode;
}

void IoHomecontrolChannel::setConfigured2WKeyInitDelay(uint16_t iDelayMs)
{
    mConfigured2WKeyInitDelay = iDelayMs > 10000 ? 10000 : iDelayMs;
}

uint16_t IoHomecontrolChannel::getConfigured2WKeyInitDelay() const
{
    return mConfigured2WKeyInitDelay;
}

bool IoHomecontrolChannel::effectiveLowPower2W() const
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

const char *IoHomecontrolChannel::twoWayPowerClassName(TwoWayPowerClass iPowerClass)
{
    switch (iPowerClass)
    {
    case TwoWayPowerClass::Automatic:
        return "automatic";
    case TwoWayPowerClass::AlwaysAlive:
        return "always-alive";
    case TwoWayPowerClass::LowPower:
        return "low-power";
    default:
        return "automatic";
    }
}

void IoHomecontrolChannel::setConfigured2WDiscoverySettings(const TwoWayDiscoverySettings &iSettings)
{
    mConfigured2WDiscoverySettings = iSettings;
}

const TwoWayDiscoverySettings &IoHomecontrolChannel::getConfigured2WDiscoverySettings() const
{
    return mConfigured2WDiscoverySettings;
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
    return ioHomeAllocateSequence1W(mSequence1W, mReservedSequence1W,
                                     iForceReserve, oFlashSaveRequired);
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
void IoHomecontrolChannel::setConfigured1WAcei(uint8_t iAcei) { mConfigured1WAcei = iAcei; }
uint8_t IoHomecontrolChannel::getConfigured1WAcei() const { return mConfigured1WAcei; }
void IoHomecontrolChannel::setConfigured2WAcei(uint8_t iAcei) { mConfigured2WAcei = iAcei; }
uint8_t IoHomecontrolChannel::getConfigured2WAcei() const { return mConfigured2WAcei; }
void IoHomecontrolChannel::setConfigured1WEnrollmentMac(bool iEnabled) { mConfigured1WEnrollmentMac = iEnabled; }
bool IoHomecontrolChannel::getConfigured1WEnrollmentMac() const { return mConfigured1WEnrollmentMac; }
void IoHomecontrolChannel::setConfigured1WEnrollmentFinalizer(OneWayEnrollmentFinalizer iFinalizer) { mConfigured1WEnrollmentFinalizer = iFinalizer; }
OneWayEnrollmentFinalizer IoHomecontrolChannel::getConfigured1WEnrollmentFinalizer() const { return mConfigured1WEnrollmentFinalizer; }
void IoHomecontrolChannel::setConfigured1WExecuteDestinationPolicy(OneWayExecuteDestinationPolicy iPolicy) { mConfigured1WExecuteDestinationPolicy = iPolicy; }
OneWayExecuteDestinationPolicy IoHomecontrolChannel::getConfigured1WExecuteDestinationPolicy() const { return mConfigured1WExecuteDestinationPolicy; }
void IoHomecontrolChannel::setConfigured1WEnrollmentDestinationPolicy(OneWayEnrollmentDestinationPolicy iPolicy) { mConfigured1WEnrollmentDestinationPolicy = iPolicy; }
OneWayEnrollmentDestinationPolicy IoHomecontrolChannel::getConfigured1WEnrollmentDestinationPolicy() const { return mConfigured1WEnrollmentDestinationPolicy; }
void IoHomecontrolChannel::setConfigured1WEnrollmentClassMask(uint8_t iMask) { mConfigured1WEnrollmentClassMask = iMask <= IOHC_1W_ENROLL_CLASS_INTERIOR ? iMask : 0; }
uint8_t IoHomecontrolChannel::getConfigured1WEnrollmentClassMask() const { return mConfigured1WEnrollmentClassMask; }
void IoHomecontrolChannel::setConfigured1WPowerClass(OneWayPowerClass iPowerClass) { mConfigured1WPowerClass = iPowerClass; }
OneWayPowerClass IoHomecontrolChannel::getConfigured1WPowerClass() const { return mConfigured1WPowerClass; }

// --- Private command methods ---

void IoHomecontrolChannel::sendPositionCommand(float iPercent, uint8_t iSlatPercent)
{
    if (!allowsActuatorControls())
        return;
    const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor();
    if (lDescriptor && ioHomeParameterSemantic(lDescriptor, 0) == ParameterSemantic::Unsupported)
        return;
    if (lDescriptor && (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly) &&
        (iSlatPercent != 0xFF || (iPercent != 0.0f && iPercent != 100.0f)))
    {
        logDebugP("Binary profile accepts only On/Off endpoints");
        return;
    }
    if (iSlatPercent != 0xFF && mIs1W && lDescriptor)
    {
        logDebugP("Position+slat command has no validated FP mapping for %s",
                  lDescriptor ? lDescriptor->label : "unknown profile");
        return;
    }
    logDebugP("Send position %.1f%%", iPercent);
    const float lTargetPosition = clampPercent(iPercent);
    const float lWirePercent = lDescriptor &&
                                       lDescriptor->mpPolarity == ParameterPolarity::Reversed
                                   ? 100.0f - lTargetPosition : lTargetPosition;
    uint8_t lParam = static_cast<uint8_t>(lWirePercent + 0.5f);
    const bool lQueued = mIs1W
                             ? mController.sendChannelCommand(this, IoHomeCommand::Execute, lParam, iSlatPercent)
                             : sendTwoWayMovement(lParam);
    if (!lQueued)
        return;

    if (iSlatPercent != 0xFF && !mIs1W && isTiltCapableDeviceType())
        sendSlatCommand(iSlatPercent);

    clearStopTravelSnapshot();
    if (lDescriptor && (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly))
        mTargetPosition = lTargetPosition;
    else
        startTravelEstimation(lTargetPosition);
    if (!mIs1W || mNodeId != 0)
        startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

void IoHomecontrolChannel::sendUpDown(bool iDown)
{
    if (!allowsActuatorControls())
        return;
    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
        if (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly) return;
    logDebugP("Send %s", iDown ? "DOWN" : "UP");
    sendPositionCommand(iDown ? 100.0f : 0.0f);
}

void IoHomecontrolChannel::sendStop()
{
    if (!allowsActuatorControls())
        return;
    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
        if (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly) return;
    logDebugP("Send STOP");
    const bool lQueued = mIs1W
                             ? mController.sendChannelCommand(this, IoHomeCommand::Execute, 0xD2)
                             : mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, 0xD2);
    if (!lQueued)
        return;

    mStopTravelSnapshot.valid = mTravelDurationMs != 0 || mIsMoving;
    mStopTravelSnapshot.moving = mIsMoving;
    mStopTravelSnapshot.targetPosition = mTargetPosition;
    mStopTravelSnapshot.travelStartPosition = mTravelStartPosition;
    mStopTravelSnapshot.travelStartTime = mTravelStartTime;
    mStopTravelSnapshot.travelDurationMs = mTravelDurationMs;
    stopTravelEstimation(true);

    if (!mIs1W || mNodeId != 0)
        startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

void IoHomecontrolChannel::sendFavorite()
{
    if (!allowsActuatorControls())
        return;
    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
        if (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly) return;
    logDebugP("Send FAVORITE");
    const bool lQueued = mIs1W
                             ? mController.sendChannelCommand(this, IoHomeCommand::Execute, 0xD8)
                             : sendTwoWayMovement(0xD8);
    if (!lQueued)
        return;

    clearStopTravelSnapshot();
    if (!mIs1W || mNodeId != 0)
        startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

void IoHomecontrolChannel::sendSlatCommand(float iPercent)
{
    if (!allowsActuatorControls())
        return;
    const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor();
    if (mProtocolIdentity.valid && !lDescriptor)
        return;
    // The legacy 1W combined Execute carries a capture-specific FP shape.
    // Its parameter index cannot be asserted for a discovered profile yet.
    if (mIs1W && lDescriptor)
        return;
    if (lDescriptor &&
        (lDescriptor->capabilityFlags & IoHomeCapabilityOrientation) == 0)
    {
        logDebugP("No orientation parameter for %s", lDescriptor->label);
        return;
    }
    const float lSlatPercent = clampPercent(iPercent);
    logDebugP("Send slat %.1f%% (with current position %.1f%%)", lSlatPercent, mCurrentPosition);

    if (!mIs1W && isTiltCapableDeviceType())
    {
        const ParameterSemantic lOrientation = lDescriptor &&
            ioHomeParameterIndex(lDescriptor, ParameterSemantic::SlatOrientation) == 0xFF
                ? ParameterSemantic::HangerOrientation
                : ParameterSemantic::SlatOrientation;
        if (mController.sendProfileParameterCommand(
                mNodeId, mEncKey, lOrientation,
                static_cast<uint8_t>(lSlatPercent + 0.5f)))
            startStatusPollTracking(defaultTrackedStatusPollDelayMs());
        return;
    }

    uint8_t lPosParam = (uint8_t)(mCurrentPosition + 0.5f);
    uint8_t lSlatParam = (uint8_t)(lSlatPercent + 0.5f);
    const bool lQueued = mIs1W
                             ? mController.sendChannelCommand(this, IoHomeCommand::Execute, lPosParam, lSlatParam)
                             : mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Execute, lPosParam, lSlatParam);
    if (lQueued && (!mIs1W || mNodeId != 0))
        startStatusPollTracking(defaultTrackedStatusPollDelayMs());
}

bool IoHomecontrolChannel::requestStatus()
{
    return requestStatus(false);
}

bool IoHomecontrolChannel::requestStatus(bool iTrackedPoll)
{
    // 1W is one-way: the device cannot answer a 2W status request, so never
    // emit one (it would only trigger an endless failed-poll/retry loop).
    if (mIs1W || !allowsActuatorControls())
        return false;

    logDebugP("Request status");

    // Scheduler-owned status polls normally get one in-exchange attempt and
    // spread retries across the channel backoff policy. A STOP-settle check is
    // deliberately allowed the full exchange budget because it resolves an
    // ambiguous safety-relevant command outcome.
    const bool lStopSettlePoll = iTrackedPoll && mStopSettlePollPending;
    const uint8_t lMaxAttempts = lStopSettlePoll ? IOHC_EXCHANGE_MAX_ATTEMPTS : 1U;
    const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor();
    const ParameterSemantic lOrientation = lDescriptor &&
        ioHomeParameterIndex(lDescriptor, ParameterSemantic::SlatOrientation) == 0xFF
            ? ParameterSemantic::HangerOrientation
            : ParameterSemantic::SlatOrientation;
    const uint8_t lOrientationIndex = lDescriptor
        ? ioHomeParameterIndex(lDescriptor, lOrientation) : 3;
    if (isTiltCapableDeviceType() && lOrientationIndex >= 1 &&
        lOrientationIndex <= 3)
    {
        const bool lQueued = mController.sendBackgroundCommand(
            mNodeId, mEncKey, IoHomeCommand::Private,
            0x03, static_cast<uint8_t>(0x80U >> (lOrientationIndex - 1)),
            0x01, lMaxAttempts);
        if (lQueued)
        {
            mStatusPollTimer = delayTimerInit();
            if (iTrackedPoll)
                mStopSettlePollPending = false;
        }
        return lQueued;
    }

    const bool lQueued = mController.sendBackgroundCommand(
        mNodeId, mEncKey, IoHomeCommand::Private,
        0x03, 0xFF, 0xFF, lMaxAttempts);
    if (lQueued)
    {
        mStatusPollTimer = delayTimerInit();
        if (iTrackedPoll)
            mStopSettlePollPending = false;
    }
    return lQueued;
}

void IoHomecontrolChannel::scheduleStatusPoll(uint32_t iDelayMs)
{
    if (!mPaired)
        return;

    uint32_t lDelayMs = (iDelayMs > 0) ? iDelayMs : defaultTrackedStatusPollDelayMs();
    if (effectiveLowPower2W() && lDelayMs < kLowPowerTrackedPollMinMs)
        lDelayMs = kLowPowerTrackedPollMinMs;
    const uint32_t lNow = millis();
    const uint32_t lRequestedPollMs = lNow + lDelayMs;

    mSingleFollowUpPollPending = true;
    mStatusExpected = false;
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;

    if (mNextStatusPollMs == 0 || static_cast<int32_t>(lRequestedPollMs - mNextStatusPollMs) < 0)
        mNextStatusPollMs = lRequestedPollMs;

    if (mPollTrackingDeadlineMs == 0)
        mPollTrackingDeadlineMs = lNow + kTrackedStatusPollWindowMs;
}

void IoHomecontrolChannel::requestStatusPrivate()
{
    requestProfileParameterStatus();
    logDebugP("Request status (Private 0x03)");
    mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::Private, 0x03);
}

void IoHomecontrolChannel::startStatusPollTracking(uint32_t iDelayMs)
{
    if (effectiveLowPower2W() && iDelayMs < kLowPowerTrackedPollMinMs)
        iDelayMs = kLowPowerTrackedPollMinMs;
    mStopSettlePollPending = false;
    const uint32_t lNow = millis();
    const uint32_t lTrackingWindowMs = iDelayMs > kTrackedStatusPollWindowMs
                                           ? iDelayMs
                                           : kTrackedStatusPollWindowMs;

    mSingleFollowUpPollPending = true;
    mStatusExpected = false;
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;
    mNextStatusPollMs = lNow + iDelayMs;
    mPollTrackingDeadlineMs = lNow + ((lTrackingWindowMs > iDelayMs) ? lTrackingWindowMs : iDelayMs);
}

void IoHomecontrolChannel::clearStatusPollTracking()
{
    mStopSettlePollPending = false;
    mNextStatusPollMs = 0;
    mPollTrackingDeadlineMs = 0;
    mSingleFollowUpPollPending = false;
    mStatusPollFailures = 0;
    mAuthPollFailures = 0;
    mStatusExpected = false;
}

uint32_t IoHomecontrolChannel::configuredStatusPollIntervalMs() const
{
    const uint16_t lPollSec = ParamIOHC_cPollInterval;
    return (lPollSec > 0) ? (static_cast<uint32_t>(lPollSec) * 1000UL) : 0UL;
}

uint32_t IoHomecontrolChannel::defaultTrackedStatusPollDelayMs() const
{
    if (effectiveLowPower2W())
        return kLowPowerTrackedPollMinMs;
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
    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
    {
        const IoHomeGenericCapabilities lCaps = ioHomeProfileCapabilities(lDescriptor);
        return lCaps.light || lCaps.onOff;
    }
    return ParamIOHC_cDeviceType == 6 || ParamIOHC_cDeviceType == 12;
}

bool IoHomecontrolChannel::isDimmableLight() const
{
    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
        return ioHomeParameterIndex(lDescriptor,
                                    ParameterSemantic::LightIntensityGradient) != 0xFF;
    return ParamIOHC_cDeviceType == 6 && ParamIOHC_cDimmable;
}

bool IoHomecontrolChannel::isLockDeviceType() const
{
    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
        return ioHomeProfileCapabilities(lDescriptor).lock;
    return ParamIOHC_cDeviceType == 8;
}

uint8_t IoHomecontrolChannel::effectiveDeviceType() const
{
    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
    {
        const IoHomeGenericCapabilities lCaps = ioHomeProfileCapabilities(lDescriptor);
        if (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly) return 12;
        if (lCaps.light) return 6;
        if (lCaps.lock) return 8;
        if (lCaps.onOff) return 12;
        if (lCaps.ventilation) return 11;
        if (lCaps.position) return 1;
    }
    return ParamIOHC_cDeviceType;
}

bool IoHomecontrolChannel::isTiltCapableDeviceType() const
{
    if (const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor())
        return ioHomeProfileCapabilities(lDescriptor).tilt;
    if (mProtocolIdentity.valid)
        return false;

    uint16_t lProfile = mProfile;
    if (lProfile == 0)
        lProfile = static_cast<uint16_t>(ParamIOHC_cDeviceType);

    switch (static_cast<IoHomeDeviceType>(lProfile))
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
    if(!knx.configured())return;
    if (!isBinaryDeviceType())
        return;

    bool lActive = isDimmableLight() ? (mCurrentPosition > 0.0f) : (mCurrentPosition >= 50.0f);
    if (isOnOffDeviceType())
        getKo(IOHC_KoCHOnOffStatus).value(lActive, DPT_Switch);
    else
        getKo(IOHC_KoCHLockStatus).value(lActive, Dpt(1, 11));
}

bool IoHomecontrolChannel::restoreLastKnownStateAfterStartup()
{
    if(!knx.configured())return false;
    bool lBinaryState = false;

    switch (effectiveDeviceType())
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
        if (!isDimmableLight())
            lBinaryState = true;
        break;
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

    float lDevicePosition = ParamIOHC_cInvertDir ? (100.0f - lReportedPosition) : lReportedPosition;
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
    const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor();
    const bool lBinaryOnly = lDescriptor &&
                             (lDescriptor->capabilityFlags & IoHomeCapabilityBinaryOnly);
    mCurrentPosition = lBinaryOnly ? (iPositionPercent >= 50.0f ? 100.0f : 0.0f)
                                   : clampPercent(iPositionPercent);

    float lReportPos = ParamIOHC_cInvertDir ? (100.0f - mCurrentPosition) : mCurrentPosition;
    if (!lBinaryOnly)
        if(knx.configured())getKo(IOHC_KoCHPositionFeedback).value((uint8_t)(lReportPos + 0.5f), DPT_Scaling);
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

void IoHomecontrolChannel::clearStopTravelSnapshot()
{
    mStopTravelSnapshot = StopTravelSnapshot{};
}

void IoHomecontrolChannel::restoreStopTravelSnapshot()
{
    if (!mStopTravelSnapshot.valid)
        return;

    const StopTravelSnapshot lSnapshot = mStopTravelSnapshot;
    clearStopTravelSnapshot();
    mIsMoving = lSnapshot.moving;
    mTargetPosition = lSnapshot.targetPosition;
    mTravelStartPosition = lSnapshot.travelStartPosition;
    mTravelStartTime = lSnapshot.travelStartTime;
    mTravelDurationMs = lSnapshot.travelDurationMs;
    if (!isBinaryDeviceType())
        if(knx.configured())getKo(IOHC_KoCHMovementStatus).value(mIsMoving, Dpt(1, 11));
    logInfoP("STOP command was rejected or failed locally; restored travel target %.1f%%", mTargetPosition);
}

void IoHomecontrolChannel::onCommandExchangeResult(IoHomeCommand iCommand, uint8_t iParam,
                                                    IoHomeCommandExchangeResult iResult)
{
    if (iCommand == IoHomeCommand::Execute &&
        (iResult == IoHomeCommandExchangeResult::Completed ||
         iResult == IoHomeCommandExchangeResult::ExplicitlyRejected))
    {
        mConfirmsExecute = true;
    }

    if (iResult == IoHomeCommandExchangeResult::Completed ||
        iResult == IoHomeCommandExchangeResult::AuthenticatedUnconfirmed ||
        iResult == IoHomeCommandExchangeResult::ExplicitlyRejected)
    {
        mHas2WHeardEvidence = true;
        mLast2WHeardMs = millis();
    }

    if (iCommand != IoHomeCommand::Execute || iParam != 0xD2)
        return;

    switch (iResult)
    {
    case IoHomeCommandExchangeResult::Completed:
        mStopSettlePollPending = true;
        clearStopTravelSnapshot();
        break;
    case IoHomeCommandExchangeResult::MediaAccessFailed:
    case IoHomeCommandExchangeResult::FailedBeforeAuthentication:
        restoreStopTravelSnapshot();
        break;
    case IoHomeCommandExchangeResult::AuthenticatedUnconfirmed:
        // The actuator authenticated the STOP, so replay/rollback is unsafe.
        // Keep the snapshot until a status update or a new movement resolves it.
        mStopSettlePollPending = true;
        scheduleStatusPoll(defaultTrackedStatusPollDelayMs());
        logInfoP("STOP exchange authenticated but unconfirmed; waiting for status verification");
        break;
    case IoHomeCommandExchangeResult::SessionExhausted:
    case IoHomeCommandExchangeResult::Unknown:
        // Silence is not proof that the motor missed STOP. Preserve the
        // pre-STOP trajectory only as a snapshot until a real status resolves
        // whether it stopped or kept moving.
        mStopSettlePollPending = true;
        scheduleStatusPoll(defaultTrackedStatusPollDelayMs());
        logInfoP("STOP exchange not confirmed; waiting for status verification");
        break;
    case IoHomeCommandExchangeResult::ExplicitlyRejected:
        restoreStopTravelSnapshot();
        break;
    }
}

void IoHomecontrolChannel::onUnanswered2WWake(uint32_t iExchangeStartMs, uint32_t iGeneration)
{
    // Do not consume moving evidence received during the unanswered exchange.
    if (mHas2WMovingEvidence && iGeneration == m2WMovingEvidenceGeneration &&
        static_cast<int32_t>(mLast2WMovingEvidenceMs - iExchangeStartMs) <= 0)
        mHas2WMovingEvidence = false;
}

bool IoHomecontrolChannel::confirmsExecute() const
{
    return mConfirmsExecute;
}

TwoWayWakeBelief IoHomecontrolChannel::twoWayWakeBeliefAt(uint32_t iNowMs, bool iStopCommand) const
{
    return ::twoWayWakeBelief(mHas2WMovingEvidence, mLast2WMovingEvidenceMs,
                              mHas2WHeardEvidence, mLast2WHeardMs,
                              iNowMs, iStopCommand);
}

bool IoHomecontrolChannel::twoWayLastHeardAgeAt(uint32_t iNowMs, uint32_t &oAgeMs) const
{
    if (!mHas2WHeardEvidence)
        return false;
    oAgeMs = static_cast<uint32_t>(iNowMs - mLast2WHeardMs);
    return true;
}

bool IoHomecontrolChannel::hasStopSettlePollPending() const
{
    return mStopSettlePollPending;
}

void IoHomecontrolChannel::onExchangeTimeout(bool iAuthenticatedUnconfirmed)
{
    uint16_t &lCounter = iAuthenticatedUnconfirmed
                             ? mUnconfirmedExchangeCount
                             : mExchangeTimeoutCount;
    if (lCounter != UINT16_MAX)
        ++lCounter;
}

uint16_t IoHomecontrolChannel::exchangeTimeoutCount() const
{
    return mExchangeTimeoutCount;
}

uint16_t IoHomecontrolChannel::unconfirmedExchangeCount() const
{
    return mUnconfirmedExchangeCount;
}

void IoHomecontrolChannel::updateEstimatedPosition()
{
    if (mTravelDurationMs == 0 || !mIsMoving)
        return;

    float lPreviousPosition = mCurrentPosition;
    float lEstimatedPosition = snapPositionBoundary(clampPercent(estimateCurrentPosition()));
    float lPreviousReported = ParamIOHC_cInvertDir ? (100.0f - lPreviousPosition) : lPreviousPosition;
    float lEstimatedReported = ParamIOHC_cInvertDir ? (100.0f - lEstimatedPosition) : lEstimatedPosition;

    mCurrentPosition = lEstimatedPosition;
    if ((uint8_t)(lEstimatedReported + 0.5f) != (uint8_t)(lPreviousReported + 0.5f))
    {
        if(knx.configured())getKo(IOHC_KoCHPositionFeedback).value((uint8_t)(lEstimatedReported + 0.5f), DPT_Scaling);
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
    return ParamIOHC_cOpeningTime > 0.0f ? ParamIOHC_cOpeningTime : 0.0f;
}

float IoHomecontrolChannel::configuredClosingTimeSeconds() const
{
    return ParamIOHC_cClosingTime > 0.0f ? ParamIOHC_cClosingTime : 0.0f;
}

// --- P1: RSSI callback ---

void IoHomecontrolChannel::onRssiUpdate(uint8_t iScaledPercent)
{
    mLastRssi = iScaledPercent;
    if(knx.configured())getKo(IOHC_KoCHRssi).value(iScaledPercent, DPT_Scaling);
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
        if(knx.configured())getKo(IOHC_KoCHErrorStatus).value(iStatus, DPT_DecimalFactor);
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
    uint8_t lSceneCount = ParamIOHC_cSceneCount;
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
        mScenePositions[i] = knx.paramByte(IOHC_SCENE_PARAM(Position, i));
        mSceneActions[i] = static_cast<SceneAction>(knx.paramByte(IOHC_SCENE_PARAM(Action, i)));
        mSceneSlats[i] = knx.paramByte(IOHC_SCENE_PARAM(Slat, i));
    }
}

float IoHomecontrolChannel::sceneToDevicePosition(uint8_t iScenePosition) const
{
    float lPosition = (float)iScenePosition;
    if (ParamIOHC_cInvertDir)
        lPosition = 100.0f - lPosition;
    return lPosition;
}

uint8_t IoHomecontrolChannel::sceneToDeviceSlat(uint8_t iSceneSlat) const
{
    return iSceneSlat > 100 ? 100 : iSceneSlat;
}

uint8_t IoHomecontrolChannel::currentPositionToSceneValue() const
{
    float lPosition = ParamIOHC_cInvertDir ? (100.0f - mCurrentPosition) : mCurrentPosition;
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

    uint8_t *lPositionData = knx.paramData(IOHC_SCENE_PARAM(Position, iSceneIndex));
    uint8_t *lSlatData = knx.paramData(IOHC_SCENE_PARAM(Slat, iSceneIndex));
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
    const IoHomeProfileDescriptor *lDescriptor = getEffectiveProfileDescriptor();
    if (mProtocolIdentity.valid && (!lDescriptor || !lDescriptor->securedVentilation))
    {
        logDebugP("Secured ventilation is not defined for this profile");
        return;
    }
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
    uint8_t lDeviceType = effectiveDeviceType();

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
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x03, lTenths);
        mController.sendCommand(mNodeId, mEncKey, IoHomeCommand::WritePrivate, 0x04, lMode);
        return;
    }

    if (lDeviceType == 6 && isDimmableLight())
    {
        uint8_t lPos = mScenePositions[lSceneIndex];
        logDebugP("Scene %d recall -> brightness %d%%", iScene, lPos);
        sendPositionCommand(sceneToDevicePosition(lPos));
        return;
    }

    // Licht/Schloss/Schalter scenes: on/off
    if ((lDeviceType == 6 && !isDimmableLight()) || lDeviceType == 8 || lDeviceType == 12)
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
    bool lUseSlat = lDeviceType == 1 && isTiltCapableDeviceType();

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
    uint8_t lDeviceType = effectiveDeviceType();

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
        uint8_t lDevType = effectiveDeviceType();
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
        if (ParamIOHC_cInvertDir)
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

void IoHomecontrolChannel::processProductInputKo(uint8_t index,GroupObject &ko)
{
#ifdef PIC_KoBlockOffset
    if(!ParamBASE_ModuleEnabled_PIC||!isOperational()||is1W()||!ParamIOHC_cActive||ParamIOHC_cSuspend||mLocked)return;
    const auto family=ioHomeBindProductFamily(mProtocolIdentity,mProductIdentityEvidence);
    if(index==PIC_KocRgb) {
        const uint32_t rgb=ko.value(DPT_Colour_RGB);
        if(!mController.requestProductRgb(this,(rgb>>16)&255,(rgb>>8)&255,rgb&255))logInfoP("Product RGB write blocked: binding/qualification/ownership");
    } else if(index==PIC_KocWhite) {
        const uint16_t kelvin=ko.value(Dpt(7,600));
        if(!mController.requestProductWhite(this,kelvin))logInfoP("Product white write blocked: binding/qualification/ownership");
    } else if(index==PIC_KocRead&&bool(ko.value(DPT_Switch))) {
        if(family==IoHomeBoundProductFamily::RgbLight) {
            if(!mController.requestMpFpMaskRead(this,(uint16_t(1)<<9)|(uint16_t(1)<<10)))logInfoP("Product snapshot read blocked");
        } else if(family==IoHomeBoundProductFamily::TunableWhiteLight) {
            if(!mController.requestMpFpRead(this,14))logInfoP("Product white read blocked");
        }
    }
#endif
}
void IoHomecontrolChannel::publishProductState()
{
    if(!knx.configured())return;
#ifdef PVX_KoBlockOffset
    if(ParamBASE_ModuleEnabled_PVX){auto &valid=knx.getGroupObject(PVX_KoCalcNumber(PVX_KocValid));if(!valid.initialized()||bool(valid.value(Dpt(1,2))))valid.value(false,Dpt(1,2));}
#endif
#ifdef PIC_KoBlockOffset
    const auto family=ioHomeBindProductFamily(mProtocolIdentity,mProductIdentityEvidence);
    if(!ParamBASE_ModuleEnabled_PIC)return;
    const bool operational=isOperational()&&!is1W()&&ParamIOHC_cActive&&!ParamIOHC_cSuspend;
    uint8_t red=0,green=0,blue=0;uint16_t kelvin=0;bool valid=false;uint32_t generation=0;
    if(operational&&family==IoHomeBoundProductFamily::RgbLight&&ioHomeProductAccess(family,10).knxPublish&&ioHomeProductAccess(family,11).knxPublish) {
        valid=mProductRuntime.rgb(family,millis(),5000,red,green,blue);generation=mProductRuntime.sample(0)->generation;
    } else if(operational&&family==IoHomeBoundProductFamily::TunableWhiteLight&&ioHomeProductAccess(family,14).knxPublish) {
        valid=mProductRuntime.white(family,millis(),5000,kelvin);generation=mProductRuntime.sample(14)->generation;
    }
    auto &validKo=knx.getGroupObject(PIC_KoCalcNumber(PIC_KocValid));
    if(!validKo.initialized()||valid!=mProductPublishedValid)validKo.value(valid,Dpt(1,2));
    if(valid&&(!mProductPublishedValid||generation!=mProductPublishedGeneration)) {
        if(family==IoHomeBoundProductFamily::RgbLight)knx.getGroupObject(PIC_KoCalcNumber(PIC_KocRgbFeedback)).value(uint32_t(red)<<16|uint32_t(green)<<8|blue,DPT_Colour_RGB);
        else knx.getGroupObject(PIC_KoCalcNumber(PIC_KocWhiteFeedback)).value(kelvin,Dpt(7,600));
        mProductPublishedGeneration=generation;
    }
    mProductPublishedValid=valid;
#endif
}

void IoHomecontrolChannel::processProductValueInputKo(uint8_t index,GroupObject &ko)
{
#ifdef PVX_KoBlockOffset
    if(!ParamBASE_ModuleEnabled_PVX||!isOperational()||is1W()||!ParamIOHC_cActive||ParamIOHC_cSuspend||mLocked)return;
    // Explicit selection grants diagnostic reads only. All value writes remain gated.
    if(index!=PVX_KocRead||!bool(ko.value(DPT_Switch)))return;
    const uint8_t product=ParamPVX_cProductDefinition,fp=ParamPVX_cValueIndex;
    if(product<1||product>14||fp>16)return;
    uint16_t mask=fp?uint16_t(1)<<(fp-1):0;
    if(product==3&&fp==13)mask|=uint16_t(1)<<11;
    if(product==7&&fp>=9&&fp<=14)mask|=uint16_t(1)<<((fp%2?fp+1:fp-1)-1);
    if((product==8||product==9)&&(fp==15||fp==16))mask|=0xC000;
    if(!mController.requestMpFpMaskRead(this,mask))logInfoP("Selected product diagnostic GET blocked");
#endif
}

// Select a speed only for roller-shutter FP1; other profiles use FP1 differently.
uint8_t IoHomecontrolChannel::movementExecuteProfile() const
{
    if (mIs1W) return 0xFF;
#ifdef MVS_ParamBlockOffset
    if (!ParamBASE_ModuleEnabled_MVS) return 0xFF;
#endif
    const auto *profile = getEffectiveProfileDescriptor();
    if (profile && (ioHomeParameterSemantic(profile, 0) != ParameterSemantic::Position ||
                    ioHomeParameterSemantic(profile, 1) != ParameterSemantic::LinearSpeed)) return 0xFF;
    if (!profile && ParamIOHC_cDeviceType != 1) return 0xFF;
    return mMovementMode == 1 ? IOHC_EXECUTE_PROFILE_SILENT :
           mMovementMode == 2 ? IOHC_EXECUTE_PROFILE_FAST : IOHC_EXECUTE_PROFILE_NORMAL;
}

void IoHomecontrolChannel::processMovementModeInputKo(GroupObject &ko)
{
#ifdef MVS_KoBlockOffset
    if (!ParamBASE_ModuleEnabled_MVS || !ParamMVS_cMovementModeObject ||
        !isOperational() || mIs1W || !ParamIOHC_cActive || ParamIOHC_cSuspend) return;
    const uint8_t mode = ko.value(Dpt(5, 10));
    if (mode > 2) {
        // Restore the selected value: invalid telegrams must not become readable state.
        ko.value(mMovementMode, Dpt(5, 10));
        return;
    }
    mMovementMode = mode;
    mRequestedSpeedIndex = 0;
    ko.value(mMovementMode, Dpt(5, 10));
#endif
}


bool IoHomecontrolChannel::profileObjectsActive() const
{
#ifdef PRF_ParamBlockOffset
    const auto *profile = getEffectiveProfileDescriptor();
    return mProductContextRevision && ParamBASE_ModuleEnabled_PRF && ParamPRF_cEnabled && isOperational() && !mIs1W &&
           ParamIOHC_cActive && !ParamIOHC_cSuspend && allowsActuatorControls() && profile &&
           ParamPRF_cProfileCode == ((profile->profile << 6) | profile->subProfile);
#else
    return false;
#endif
}

void IoHomecontrolChannel::requestProfileParameterStatus()
{
    if (!profileObjectsActive() || mLocked) return;
    mController.requestProfileParameterRead(this);
}

void IoHomecontrolChannel::processProfileInputKo(uint8_t object, GroupObject &ko)
{
    if (!profileObjectsActive() || mLocked) return;
    const auto *profile = getEffectiveProfileDescriptor();
    if (object == 15) {if (bool(ko.value(DPT_Switch))) requestProfileParameterStatus();return;}
    if (object == 16) {
        if (ioHomeProfileObjectBinary(profile)) {sendPositionCommand(bool(ko.value(DPT_Switch))?100.0f:0.0f);requestProfileParameterStatus();}
        return;
    }
    if (object >= 15 || object % 3) return; // feedback / validity telegrams are never writes.
    const uint8_t index = ioHomeProfileObjectIndex(object / 3);
    const auto descriptor = ioHomeParameterDescriptor(profile,index);
    if (descriptor.encoding != ParameterEncoding::Relative) return;
    float value = ko.value(DPT_Scaling);
    if (index == 0) {
        if (ParamIOHC_cInvertDir && ioHomeIsPositionSemantic(profile->mp)) value=100-value;
        sendPositionCommand(value);
    } else if (descriptor.writable) {
        const uint8_t percent = static_cast<uint8_t>(value + 0.5f);
        if (!mController.sendProfileParameterCommand(mNodeId,mEncKey,descriptor.semantic,percent)) return;
        if (ioHomeIsPositionSemantic(descriptor.semantic) || ioHomeIsOrientationSemantic(descriptor.semantic))
            startStatusPollTracking(defaultTrackedStatusPollDelayMs());
        // Preserve explicitly selected travel speed on subsequent movement commands.
        if (descriptor.semantic == ParameterSemantic::LinearSpeed || descriptor.semantic == ParameterSemantic::AngularSpeed ||
            descriptor.semantic == ParameterSemantic::LightIntensityGradient || descriptor.semantic == ParameterSemantic::EnergyGradient) {
            mRequestedSpeedIndex=index;mRequestedSpeedRaw=ioHomePercentToRaw(percent,ParameterPolarity::Normal);
        }
    } else return;
    requestProfileParameterStatus();
}

void IoHomecontrolChannel::onProfileParameterFeedback(uint8_t index, uint16_t raw)
{
#ifdef PRF_KoBlockOffset
    const uint8_t slot=ioHomeProfileObjectSlot(index);
    if (slot==0xFF) return;
    IoHomeProfileObjectValue value;
    const auto *profile=getEffectiveProfileDescriptor();
    if (!profileObjectsActive() || !ioHomeDecodeProfileObject(profile,index,raw,value)) {
        mProfileReceivedMask &= ~(1U<<slot);
        updateProfileParameterValidity();return;
    }
    if(!knx.configured())return;
    auto &feedback=knx.getGroupObject(PRF_KoCalcNumber(slot*3+1));
    const auto semantic=ioHomeResolvedParameterSemantic(profile,index);
    if (index==0 && ioHomeProfileObjectBinary(profile))
        knx.getGroupObject(PRF_KoCalcNumber(17)).value(value.binary,DPT_Switch);
    else if (semantic==ParameterSemantic::ProjectionAngle) feedback.value(raw,Dpt(7,1));
    else if (semantic==ParameterSemantic::WindowSecurityMode) feedback.value(static_cast<uint8_t>(raw),Dpt(5,10));
    else {
        if (index==0 && ParamIOHC_cInvertDir && ioHomeIsPositionSemantic(profile->mp)) value.percent=100-value.percent;
        feedback.value(static_cast<uint8_t>(value.percent+0.5f),DPT_Scaling);
    }
    mProfileReceivedAt[slot]=millis();mProfileReceivedMask |= 1U<<slot;
    updateProfileParameterValidity();
#endif
}

void IoHomecontrolChannel::updateProfileParameterValidity()
{
    if(!knx.configured())return;
#ifdef PRF_KoBlockOffset
    if (!ParamBASE_ModuleEnabled_PRF) return;
    const bool active=profileObjectsActive();
    const uint32_t ttl=std::max<uint32_t>(30000UL,static_cast<uint32_t>(ParamIOHC_cPollInterval)*3000UL);
    for (uint8_t slot=0;slot<5;++slot) {
        const uint8_t bit=1U<<slot;
        const bool valid=active && (mProfileReceivedMask&bit) && uint32_t(millis()-mProfileReceivedAt[slot])<=ttl;
        auto &ko=knx.getGroupObject(PRF_KoCalcNumber(slot*3+2));
        if (!ko.initialized() || valid!=bool(mProfilePublishedMask&bit)) ko.value(valid,Dpt(1,2));
        if (valid) mProfilePublishedMask|=bit;else mProfilePublishedMask&=~bit;
    }
#endif
}

bool IoHomecontrolChannel::sendTwoWayMovement(uint8_t position)
{
    const auto *profile=getEffectiveProfileDescriptor();
    uint8_t index=mRequestedSpeedIndex;
    uint16_t speed=mRequestedSpeedRaw;
#ifdef MVS_ParamBlockOffset
    if (!index && ParamBASE_ModuleEnabled_MVS && profile) {
        index=ioHomeParameterIndex(profile,ParameterSemantic::LinearSpeed);
        if (index>3) index=0;
        speed=mMovementMode==1?0xD805:mMovementMode==2?IOHC_POSITION_MAX:IOHC_PARAMETER_DEFAULT;
    }
#endif
    if (!index && profile) {
        index=ioHomeParameterIndex(profile,ParameterSemantic::LightIntensityGradient);
        if (index>3) index=ioHomeParameterIndex(profile,ParameterSemantic::EnergyGradient);
        if (index>3) index=0;
        speed=IOHC_PARAMETER_DEFAULT;
    }
    if (index && profile && !(profile->capabilityFlags&IoHomeCapabilityBinaryOnly))
        return mController.sendProfileMovementCommand(mNodeId,mEncKey,position,index,speed);
    return mController.sendCommand(mNodeId,mEncKey,IoHomeCommand::Execute,position,0xFF,movementExecuteProfile());
}

bool IoHomecontrolChannel::limitationEnabled() const {
    return ParamLIM_cEnabled&&ParamIOHC_cActive&&!ParamIOHC_cSuspend&&!mIs1W&&mPaired&&
        mProtocolIdentity.valid&&mProtocolIdentity.nodeClass==IoHomeNodeClass::Actuator&&allowsActuatorControls();
}
bool IoHomecontrolChannel::limitationKoValid() const {
    return limitationEnabled()&&mController.limitationDecision(_channelIndex).valid;
}
void IoHomecontrolChannel::updateLimitationStatus() {
    if(!knx.configured())return;
    auto &ko=knx.getGroupObject(LIM_KoCalcNumber(LIM_KocActive));
    const auto &state=mController.limitationState(_channelIndex);
    const auto decision=mController.limitationDecision(_channelIndex);
    auto publication=state.snapshot;publication.node=mNodeId;publication.revision=productContextRevision();
    publication.limitationActive=decision.active;
    const auto action=mLimitationPublication.update(publication,limitationKoValid());
    if(action==IoHomeLimitationPublication::Action::Transmit)ko.value(decision.active,Dpt(1,2));
    else if(action==IoHomeLimitationPublication::Action::Cache)ko.valueNoSend(decision.active,Dpt(1,2));
    if(!limitationEnabled()){mLimitationPolled=false;return;}
    if(mLimitationPollRevision!=productContextRevision()) {
        mLimitationPolled=false;mLimitationPollRevision=productContextRevision();
    }
    const uint32_t now=millis(),interval=uint32_t(ParamLIM_cInterval)*1000;
    if(!mLimitationPolled||(interval&&uint32_t(now-mLimitationPollMs)>=interval)) {
        if(mController.refreshLimitationStatus(this)){mLimitationPolled=true;mLimitationPollMs=now;}
    }
}
void IoHomecontrolChannel::printLimitationStatus() {
    const auto &s=mController.limitationState(_channelIndex);const auto &v=s.snapshot;
    const auto decision=mController.limitationDecision(_channelIndex);
    const auto &rain=mController.rainState(_channelIndex);
    const auto &e=rain.evidence;
    logInfoP("  source=%s rain=%s rule=%s error-valid=%u code=%02X",
        ioHomeLimitationSourceName(decision.source),e.active(millis())?"yes":"no",ioHomeRainRuleName(e.rule),
        unsigned(rain.errorValid),unsigned(rain.errorCode));
    logInfoP("  commander=%06X originator=%02X last-command-valid=%u age=%lums rain-evidence=%u age=%lums",
        unsigned(e.lastCommand.node),unsigned(e.lastCommand.originator),unsigned(e.lastCommand.valid),
        static_cast<unsigned long>(millis()-e.lastCommandMs),unsigned(e.hasRainEvidence),
        static_cast<unsigned long>(millis()-e.lastRainEvidenceMs));
    logInfoP("  predicted-valid=%u target=%.2f observed-valid=%u target=%.2f stopped=%u (wire percent)",
        unsigned(e.input.predictedTargetValid),e.input.predictedTarget,
        unsigned(e.input.observedTargetValid),e.input.observedTarget,unsigned(e.input.stopped));
    const bool valid=limitationKoValid();
    logInfoP("Limitation ch=%u node=%06X sample-node=%06X coherent=%s active=%s revision=%lu current-revision=%lu token=%lu pending=%s result=%s/%u (5-byte layout provisional)",
        unsigned(_channelIndex+1),unsigned(mNodeId),unsigned(v.node),mController.limitationValid(_channelIndex)?"yes":"no",valid?(decision.active?"yes":"no"):"unknown",
        (unsigned long)v.revision,(unsigned long)productContextRevision(),(unsigned long)v.refreshToken,s.active?"yes":"no",s.resultValid?"known":"none",unsigned(s.result));
    for(uint8_t i=0;i<2;++i) {
        const auto &sample=i?v.maximum:v.minimum;const uint32_t at=i?v.maximumTimestampMs:v.minimumTimestampMs;
        if(!sample.valid){logInfoP("  %s: no sample",i?"max":"min");continue;}
        logInfoP("  %s: param=%02X raw=%04X percent=%.2f (-1=not-percent) originator=%02X(%s) time=%02X age=%lums bytes=%02X %02X %02X %02X %02X",
            i?"max":"min",sample.parameterId,sample.valueRaw,sample.percent(),sample.originator,ioHomeOriginatorName(sample.originator),
            sample.timeRaw,(unsigned long)(millis()-at),sample.raw[0],sample.raw[1],sample.raw[2],sample.raw[3],sample.raw[4]);
        const auto timer=ioHomeDecodeLimitationTimer(sample.timeRaw);
        logInfoP("    timer provisional KLF/API-derived: kind=%u seconds=%u; not used for automation",unsigned(timer.kind),timer.seconds);
    }
}

uint8_t IoHomecontrolChannel::batteryDiagnosticMode()const {
    if(!knx.configured())return ioHomeStandaloneChannel(_channelIndex)?2:0;
    return ParamBAT_cMode<=2?ParamBAT_cMode:0;
}
uint8_t IoHomecontrolChannel::batteryMonitoring()const {
    if(!mPaired||mIs1W)return 0;
    if(!knx.configured())return batteryDiagnosticMode();
    // BAT is an embedded per-channel KO bank, not an independently enabled
    // module. The visible channel mode is authoritative, including partial downloads.
    return ParamIOHC_cActive&&!ParamIOHC_cSuspend?batteryDiagnosticMode():0;
}
void IoHomecontrolChannel::invalidateBattery() {
    mBatteryAlarmPublished=false;
    mBatteryInfo={};for(auto &v:mBatteryObjects)v={};mBatteryPrivate[0]={};mBatteryPrivate[1]={};mBatteryEvent={};mBatteryLevel=0xFF;
    if(knx.configured()) {
        cancelPendingBatteryWrite(knx.getGroupObject(BAT_KoCalcNumber(BAT_KocLow)));
        cancelPendingBatteryWrite(getKo(IOHC_KoCHBattery));
    }
}
void IoHomecontrolChannel::updateBatteryKo() {
    if(!knx.configured())return;
    auto &ko=knx.getGroupObject(BAT_KoCalcNumber(BAT_KocLow));
    if(!batteryMonitoring()||!mBatteryInfo.selected().valid){mBatteryAlarmPublished=false;cancelPendingBatteryWrite(ko);return;}
    // Send the first confirmed state after reset even if it equals the old cache.
    if(!mBatteryAlarmPublished)mBatteryAlarmPublished=ko.value(mBatteryInfo.selected().low,Dpt(1,5));
    else ko.valueCompare(mBatteryInfo.selected().low,Dpt(1,5));
}
void IoHomecontrolChannel::onBatteryStatus(uint8_t status,uint8_t command) {
    if(!batteryMonitoring())return;
    mBatteryInfo.observeStatus(status,command,millis());
    if(mBatteryInfo.conflict())logInfoP("Battery conflict: selected=%s low=%u status=%u A601=%u",ioHomeBatterySourceName(mBatteryInfo.source()),unsigned(mBatteryInfo.selected().low),mBatteryInfo.coarse.raw,mBatteryInfo.somfy.raw);
    updateBatteryKo();
}
void IoHomecontrolChannel::onBatteryError(uint8_t code,uint8_t command) {
    if(!batteryMonitoring())return;mBatteryInfo.observeError(code,command,millis());updateBatteryKo();
}
void IoHomecontrolChannel::onBatteryPrivate(uint8_t function,const IoHomeFrame &frame,int rssi,uint8_t frequency,uint32_t responseUs) {
    if(batteryMonitoring()!=2||(function!=6&&function!=9))return;
    auto &sample=mBatteryPrivate[function==9];sample.valid=true;sample.command=uint8_t(frame.commandId);
    sample.length=frame.dataLen;sample.timestampMs=millis();std::memcpy(sample.data,frame.data,frame.dataLen);
    std::string raw;char byte[4];for(unsigned i=0;i<frame.dataLen;++i){snprintf(byte,sizeof(byte),"%02X ",frame.data[i]);raw+=byte;}
    const auto &id=getProtocolIdentity();
    logInfoP("Battery Private%02X RAW / UNIT UNKNOWN src=%06lX dst=%06lX cmd=%02X payload=%s",
        function,(unsigned long)frame.getSrcNodeId(),(unsigned long)frame.getDestNodeId(),unsigned(frame.commandId),raw.c_str());
    logInfoP("  RSSI=%d RF-index=%u frequency=%lu response-us=%lu time-ms=%lu profile=%u subtype=%u manufacturer=%u",rssi,frequency,
        (unsigned long)(frequency<IOHC_NUM_FREQUENCIES?IOHC_FREQUENCIES[frequency]:0),(unsigned long)responseUs,(unsigned long)millis(),id.profile,id.subProfile,id.manufacturerId);
}
void IoHomecontrolChannel::onBatteryObject(uint8_t provider,uint16_t object,const uint8_t *data,unsigned length) {
    if(batteryMonitoring()!=2||!ioHomeBatteryObject(provider,object))return;
    bool valid=ioHomeBatteryFields(data,length,object==0xA607,[](const IoHomeBatteryField &){});
    if(!data||!length||length>1024)return;
    const unsigned slot=object==0xA601?0:object==0xA607?1:object==0xA60E?2:object==9?3:4;
    mBatteryObjects[slot].data.assign(data,data+length);mBatteryObjects[slot].timestampMs=millis();
    if(!valid){logInfoP("Battery object %04X: malformed/duplicate TLV; no semantic update",object);printBatteryStatus();return;}
    ioHomeBatteryFields(data,length,object==0xA607,[&](const IoHomeBatteryField &field){
        uint32_t value=0;
        if(object==0xA601&&field.pid==1&&field.length<=2&&ioHomeBatteryUnsigned(field.data,field.length,value))
            mBatteryInfo.observeSomfy(value,0x4B,millis());
    });
    if(mBatteryInfo.conflict())logInfoP("Battery conflict: selected=%s low=%u status=%u A601=%u",ioHomeBatterySourceName(mBatteryInfo.source()),unsigned(mBatteryInfo.selected().low),mBatteryInfo.coarse.raw,mBatteryInfo.somfy.raw);
    printBatteryStatus();updateBatteryKo();
}
void IoHomecontrolChannel::printBatteryStatus() {
    const auto &id=getProtocolIdentity();const auto &selected=mBatteryInfo.selected();
    logInfoP("Battery ch=%u node=%06lX profile=%u subtype=%u manufacturer=%u monitor=%u percent=unknown",
        unsigned(_channelIndex+1),(unsigned long)mNodeId,id.profile,id.subProfile,id.manufacturerId,batteryMonitoring());
    logInfoP("  diagnostics=%u raw-mode=%u bank-enabled=%u active=%u suspend=%u paired=%u protocol=%s; monitor is effective channel mode, no periodic probes",
        batteryDiagnosticMode(),knx.configured()?unsigned(ParamBAT_cMode):0,
        knx.configured()?unsigned(ParamBASE_ModuleEnabled_BAT):0,
        knx.configured()?unsigned(ParamIOHC_cActive):1,knx.configured()?unsigned(ParamIOHC_cSuspend):0,
        unsigned(mPaired),mIs1W?"1W":"2W");
    logInfoP("  support=unverified-until-valid-response; unknown is neither zero-percent nor communication-error");
    logInfoP("  selected=%s source=%s age-ms=%lu conflict=%u",selected.valid?(selected.low?"yes":"no"):"unknown",
        ioHomeBatterySourceName(mBatteryInfo.source()),selected.valid?(unsigned long)(millis()-selected.timestampMs):0,mBatteryInfo.conflict());
    static const char *coarseNames[]={"unknown","low","normal","full"};
    static const char *somfyNames[]={"very-low","low","mid","high","unknown"};
    logInfoP("  coarse=%s age-ms=%lu",mBatteryInfo.coarse.valid?coarseNames[mBatteryInfo.coarse.raw]:"unknown",
        mBatteryInfo.coarse.valid?(unsigned long)(millis()-mBatteryInfo.coarse.timestampMs):0);
    logInfoP("  A601=%s age-ms=%lu",mBatteryInfo.somfy.valid?somfyNames[mBatteryInfo.somfy.raw]:"unknown",
        mBatteryInfo.somfy.valid?(unsigned long)(millis()-mBatteryInfo.somfy.timestampMs):0);
    logInfoP("  coarse-valid=%u raw=%u cmd=%02X A601-valid=%u raw=%u error-valid=%u code=%02X",
        mBatteryInfo.coarse.valid,mBatteryInfo.coarse.raw,mBatteryInfo.coarse.command,mBatteryInfo.somfy.valid,mBatteryInfo.somfy.raw,mBatteryInfo.error.valid,mBatteryInfo.error.raw);
    for(const auto *evidence:{&mBatteryInfo.coarse,&mBatteryInfo.somfy,&mBatteryInfo.error})
        logInfoP("  evidence valid=%u raw=%u command=%02X timestamp-ms=%lu age-ms=%lu",evidence->valid,evidence->raw,evidence->command,
            (unsigned long)evidence->timestampMs,evidence->valid?(unsigned long)(millis()-evidence->timestampMs):0);
    const uint16_t objects[]={0xA601,0xA607,0xA60E,9,0x4003};
    for(unsigned slot=0;slot<5;++slot){
    const auto &snapshot=mBatteryObjects[slot];if(snapshot.data.empty())continue;
    const auto *mBatteryObjectData=snapshot.data.data();const unsigned mBatteryObjectLength=snapshot.data.size();
    const uint16_t mBatteryObjectId=objects[slot];const uint8_t mBatteryObjectProvider=slot<3?2:0;const uint32_t mBatteryObjectMs=snapshot.timestampMs;
    const auto logBytes=[&](const uint8_t *data,unsigned length){
        // Bounded lines preserve long fields through the console formatter.
        for(unsigned offset=0;offset<length;offset+=16){
            char text[49]{};const unsigned count=std::min(16U,length-offset);
            for(unsigned i=0;i<count;++i)snprintf(text+i*3,4,"%02X ",data[offset+i]);
            logInfoP("    raw offset=%u bytes=%s",offset,text);
        }
    };
    const bool grammar=ioHomeBatteryFields(mBatteryObjectData,mBatteryObjectLength,mBatteryObjectId==0xA607,[](const IoHomeBatteryField &){});
    if(!grammar){
        logInfoP("  object=%04X len=%u unparsed RAW / UNIT UNKNOWN age-ms=%lu",mBatteryObjectId,mBatteryObjectLength,(unsigned long)(millis()-mBatteryObjectMs));
        if(mBatteryObjectId!=0xA607)logBytes(mBatteryObjectData,mBatteryObjectLength);
        else logInfoP("    A607 unparsed content withheld because it can contain one-way keys");
        continue;
    }
    ioHomeBatteryFields(mBatteryObjectData,mBatteryObjectLength,mBatteryObjectId==0xA607,[&](const IoHomeBatteryField &field){
        const bool relevant=mBatteryObjectId==0xA601?(field.pid==1||field.pid==134||field.pid==146||field.pid==147||field.pid==148):
            mBatteryObjectId==9?field.pid<=1:mBatteryObjectId==0x4003?field.pid==128:
            mBatteryObjectId==0xA607?(field.pid==2||field.pid==7||field.pid==8||field.pid==9):(field.pid==0||field.pid==1||field.pid==2);
        if(!relevant)return; // never log A607 one-way keys (PID 3)
        uint32_t value=0;const bool integer=ioHomeBatteryUnsigned(field.data,field.length,value);
        const char *label=mBatteryObjectId==0xA601&&field.pid==146?"LastBatteryVoltageRaw":"RAW / UNIT UNKNOWN";
        logInfoP("  %s object=%04X provider=%u record=%u PID=%u format=%02X len=%u age-ms=%lu",
            label,mBatteryObjectId,mBatteryObjectProvider,field.record,field.pid,field.format,field.length,(unsigned long)(millis()-mBatteryObjectMs));
        logBytes(field.data,field.length);
        if(integer)logInfoP("    BE unsigned-candidate=%lu signed-candidate=%ld (unit/scale unknown)",(unsigned long)value,(long)ioHomeBatterySigned(value,field.length));
        if(mBatteryObjectId==0xA607&&field.pid==8&&integer&&field.length<=2&&value<=5){
            static const char *states[]={"critical","low","medium","high","unknown","notSupported"};
            uint32_t address=0,role=255;
            ioHomeBatteryFields(mBatteryObjectData,mBatteryObjectLength,true,[&](const IoHomeBatteryField &other){
                if(other.record==field.record&&other.pid==2&&other.length==3)ioHomeBatteryUnsigned(other.data,3,address);
                if(other.record==field.record&&other.pid==7&&other.length<=2)ioHomeBatteryUnsigned(other.data,other.length,role);
            });
            logInfoP("    paired-controller=%06lX controller-type=%lu battery-state=%s micromodule-low=%u; separate from actuator battery",
                (unsigned long)address,(unsigned long)role,states[value],role==0&&value<=1);
        }
    });
    }
    for(unsigned i=0;i<3;++i){const auto &v=i<2?mBatteryPrivate[i]:mBatteryEvent;if(!v.valid)continue;
        std::string raw;char byte[4];for(unsigned j=0;j<v.length;++j){snprintf(byte,sizeof(byte),"%02X ",v.data[j]);raw+=byte;}
        logInfoP("  %s%02X cmd=%02X payload=%s age-ms=%lu RAW / UNIT UNKNOWN",i==2?"UnclassifiedRx":"Private",i==2?0:i?9:6,v.command,raw.c_str(),(unsigned long)(millis()-v.timestampMs));
    }
    logInfoP("  Dynamic2001 numeric source/encoding unresolved; no percentage converter, no automatic battery polling");
}

void IoHomecontrolChannel::onBatteryEventRaw(const IoHomeFrame &frame) {
    if(batteryMonitoring()!=2)return;
    mBatteryEvent.valid=true;mBatteryEvent.command=uint8_t(frame.commandId);mBatteryEvent.length=frame.dataLen;
    mBatteryEvent.timestampMs=millis();std::memcpy(mBatteryEvent.data,frame.data,frame.dataLen);
    std::string raw;char byte[4];for(unsigned i=0;i<frame.dataLen;++i){snprintf(byte,sizeof(byte),"%02X ",frame.data[i]);raw+=byte;}
    logInfoP("Battery unclassified RX src=%06lX dst=%06lX cmd=%02X payload=%s time-ms=%lu",
        (unsigned long)frame.getSrcNodeId(),(unsigned long)frame.getDestNodeId(),unsigned(frame.commandId),raw.c_str(),(unsigned long)millis());
    logInfoP("  RAW / UNIT UNKNOWN; no event2001 attribution");
}
