#include "IoHomecontrol.h"
#include "ModuleVersionCheck.h"
#include "OpenKNX.h"
#include "knxprod.h"
#include "protocol/IoHomeFrame.h"
#include "protocol/IoHomeCrypto.h"
#if defined(RADIO_SX1262)
#include "radio/SX1262DeviceErrors.h"
#include "radio/sx1262Regs-Fsk.h"
#endif
#if defined(RADIO_SX1276)
#include "radio/sx1276Regs-Fsk.h"
#endif

#ifdef ESP32
#include <esp_mac.h>
#endif

namespace
{
    constexpr uint8_t kRadioDiagSweepChannelCount = 3;
    constexpr uint8_t kRadioDiagPayload[] = {0xAA, 0x55, 0x12, 0x34};
    constexpr uint32_t kRadioDiagSweepFreqs[kRadioDiagSweepChannelCount] = {IOHC_FREQ_2, IOHC_FREQ_3, IOHC_FREQ_1};
    constexpr uint8_t kRadioDiagSweepChannels[kRadioDiagSweepChannelCount] = {2, 3, 1};
    constexpr uint32_t kRadioDiagTxTestTimeoutMs = 250UL;
    constexpr uint32_t kRadioDiagSweepTimeoutMs = 100UL;
    constexpr uint32_t kRadioDiagSweepFinalTimeoutMs = 250UL;
    constexpr uint32_t kRadioDiagBusyRetryTimeoutMs = 250UL;

#if defined(RADIO_SX1262)
    const char *describeSX1262DeviceErrors(uint16_t iDeviceErrors, char *oBuffer, size_t iBufferSize)
    {
        formatSX1262DeviceErrors(iDeviceErrors, oBuffer, iBufferSize);
        return oBuffer;
    }
#endif

    bool isSpace(char iChar)
    {
        return iChar == ' ' || iChar == '\t';
    }

    bool parseUnsignedDecimal(const std::string &iText, uint32_t &oValue)
    {
        size_t lPos = 0;
        while (lPos < iText.length() && isSpace(iText[lPos]))
            lPos++;

        if (lPos >= iText.length() || iText[lPos] < '0' || iText[lPos] > '9')
            return false;

        uint32_t lValue = 0;
        while (lPos < iText.length() && iText[lPos] >= '0' && iText[lPos] <= '9')
        {
            const uint32_t lDigit = static_cast<uint32_t>(iText[lPos] - '0');
            if (lValue > (0xFFFFFFFFUL - lDigit) / 10UL)
                return false;
            lValue = (lValue * 10UL) + lDigit;
            lPos++;
        }

        while (lPos < iText.length() && isSpace(iText[lPos]))
            lPos++;

        if (lPos != iText.length())
            return false;

        oValue = lValue;
        return true;
    }

    // Parse a hex string into a byte array
    // Returns true if all chars were valid hex and exactly iExpectedBytes bytes were parsed
    bool parseHexBytes(const std::string &iText, uint8_t *oBytes, size_t iExpectedBytes)
    {
        size_t lPos = 0;
        while (lPos < iText.length() && isSpace(iText[lPos]))
            lPos++;

        // Skip "0x" prefix if present
        if (lPos + 2 < iText.length() && iText[lPos] == '0' && (iText[lPos + 1] == 'x' || iText[lPos + 1] == 'X'))
            lPos += 2;

        size_t lParsed = 0;
        uint8_t lByte = 0;
        bool lGotNibble = false;

        while (lPos < iText.length() && lParsed < iExpectedBytes)
        {
            const char lChar = iText[lPos];
            uint8_t lNibble = 0;
            if (lChar >= '0' && lChar <= '9')
                lNibble = static_cast<uint8_t>(lChar - '0');
            else if (lChar >= 'a' && lChar <= 'f')
                lNibble = static_cast<uint8_t>(lChar - 'a' + 10);
            else if (lChar >= 'A' && lChar <= 'F')
                lNibble = static_cast<uint8_t>(lChar - 'A' + 10);
            else
                break;

            if (!lGotNibble)
            {
                lByte = lNibble;
                lGotNibble = true;
            }
            else
            {
                oBytes[lParsed++] = (lByte << 4) | lNibble;
                lGotNibble = false;
            }
            lPos++;
        }

        // If we got an odd number of nibbles, the last nibble is still in lByte but not stored
        // This is an error — reject incomplete bytes
        if (lGotNibble)
            return false;

        return lParsed == iExpectedBytes;
    }

    bool parseChannelIndex(const std::string &iText, uint8_t iNumChannels, uint8_t &oIndex)
    {
        uint32_t lValue = 0;
        if (!parseUnsignedDecimal(iText, lValue) || lValue < 1 || lValue > iNumChannels)
            return false;

        oIndex = static_cast<uint8_t>(lValue - 1);
        return true;
    }

    bool parseHex24(const std::string &iText, uint32_t &oValue)
    {
        size_t lPos = 0;
        while (lPos < iText.length() && isSpace(iText[lPos]))
            lPos++;

        if (lPos + 2 < iText.length() && iText[lPos] == '0' && (iText[lPos + 1] == 'x' || iText[lPos + 1] == 'X'))
            lPos += 2;

        uint32_t lValue = 0;
        uint8_t lDigits = 0;
        while (lPos < iText.length())
        {
            const char lChar = iText[lPos];
            uint8_t lNibble = 0;
            if (lChar >= '0' && lChar <= '9')
                lNibble = static_cast<uint8_t>(lChar - '0');
            else if (lChar >= 'a' && lChar <= 'f')
                lNibble = static_cast<uint8_t>(lChar - 'a' + 10);
            else if (lChar >= 'A' && lChar <= 'F')
                lNibble = static_cast<uint8_t>(lChar - 'A' + 10);
            else
                break;

            if (lDigits >= 6)
                return false;
            lValue = (lValue << 4) | lNibble;
            lDigits++;
            lPos++;
        }

        if (lDigits == 0)
            return false;

        while (lPos < iText.length() && isSpace(iText[lPos]))
            lPos++;

        if (lPos != iText.length())
            return false;

        oValue = lValue & 0x00FFFFFF;
        return true;
    }

    uint8_t clampFlashRecordCount(uint8_t iStoredCount, uint8_t iConfiguredCount, uint16_t iSize, uint16_t iHeaderSize, uint16_t iRecordSize)
    {
        if (iSize < iHeaderSize || iRecordSize == 0)
            return 0;

        const uint16_t lMaxRecordsBySize = (iSize - iHeaderSize) / iRecordSize;
        uint8_t lReadCount = iStoredCount;
        if (lReadCount > iConfiguredCount)
            lReadCount = iConfiguredCount;
        if (lReadCount > lMaxRecordsBySize)
            lReadCount = static_cast<uint8_t>(lMaxRecordsBySize);
        return lReadCount;
    }
}

IoHomecontrol openknxIoHomecontrol;

const std::string IoHomecontrol::name()
{
    return "IoHomecontrol";
}

const std::string IoHomecontrol::version()
{
    return MODULE_IoHomecontrol_Version;
}

IoHomecontrol::IoHomecontrol() {}

IoHomecontrol::~IoHomecontrol()
{
    for (uint8_t i = 0; i < mNumChannels; i++)
        delete mChannels[i];
}

IoHomeController &IoHomecontrol::controller()
{
    return mController;
}

IoHomeRemoteMap &IoHomecontrol::remoteMap()
{
    return mRemoteMap;
}

IoHomecontrolChannel *IoHomecontrol::getChannel(uint8_t iIndex)
{
    if (iIndex < mNumChannels)
        return mChannels[iIndex];
    return nullptr;
}

// --- Own node ID and system key initialization ---

void IoHomecontrol::deriveOwnNodeId()
{
    // Derive a 3-byte (24-bit) io-homecontrol node ID from the ESP32 MAC address.
    // Use the last 3 bytes of the base MAC, OR'd with 0x800000 to mark as locally administered.
    uint32_t lNodeId = 0x800001; // fallback
#ifdef ESP32
    uint8_t lMac[6] = {};
    if (esp_base_mac_addr_get(lMac) == ESP_OK)
    {
        lNodeId = ((uint32_t)lMac[3] << 16) | ((uint32_t)lMac[4] << 8) | lMac[5];
        lNodeId |= 0x800000; // set high bit to mark locally administered
    }
#endif
    mController.setOwnNodeId(lNodeId);
    logDebugP("Own node ID: %06X", lNodeId);
}

void IoHomecontrol::initSystemKey()
{
    // The system key is generated once and persisted in flash alongside pairing data.
    // If no key exists yet (all zeros), generate a random one.
    // The key is loaded by readFlash() before this point, so we only generate if still empty.
    const uint8_t *lExisting = mController.getSystemKey();
    bool lAllZero = true;
    for (uint8_t i = 0; i < 16; i++)
    {
        if (lExisting[i] != 0)
        {
            lAllZero = false;
            break;
        }
    }

    if (lAllZero)
    {
        uint8_t lKey[16];
#ifdef ESP32
        for (uint8_t i = 0; i < 16; i += 4)
        {
            uint32_t r = esp_random();
            lKey[i] = (r >> 0) & 0xFF;
            lKey[i + 1] = (r >> 8) & 0xFF;
            lKey[i + 2] = (r >> 16) & 0xFF;
            lKey[i + 3] = (r >> 24) & 0xFF;
        }
#else
        for (uint8_t i = 0; i < 16; i++)
            lKey[i] = rand() & 0xFF;
#endif
        mController.setSystemKey(lKey);
        logDebugP("Generated new system key");
        openknx.flash.save(); // persist immediately
    }
}

uint8_t IoHomecontrol::countPairedChannels() const
{
    uint8_t lCount = 0;
    for (uint8_t i = 0; i < mNumChannels; i++)
    {
        if (mChannels[i] != nullptr && mChannels[i]->isPaired())
            lCount++;
    }
    return lCount;
}

bool IoHomecontrol::isPairingState(ControllerState iState) const
{
    return iState >= ControllerState::PairSendDiscovery && iState <= ControllerState::PairWaitSetConfig1FinalResponse;
}

OpenKNX::Led::FunctionGroup *IoHomecontrol::statusLedFunction()
{
    return openknx.ledFunctions.get(OPENKNX_LEDFUNC_IOHC_STATE);
}

void IoHomecontrol::applyStatusLedMode(StatusLedMode iMode)
{
    auto *lLed = statusLedFunction();
    switch (iMode)
    {
    case StatusLedMode::IdleUnpaired:
        lLed->pulsing(1400);
        break;
    case StatusLedMode::IdlePaired:
        lLed->on();
        break;
    case StatusLedMode::Pairing:
        lLed->blinking(140);
        break;
    case StatusLedMode::Unknown:
    default:
        lLed->off();
        break;
    }
    mStatusLedMode = iMode;
}

void IoHomecontrol::updateStatusLed()
{
    auto *lLed = statusLedFunction();
    const uint32_t lNow = millis();
    const ControllerState lState = mController.state();

    // Transition-triggered signals for pairing end states.
    if (lState != mLastControllerState)
    {
        if (lState == ControllerState::PairComplete)
        {
            lLed->flash(220);
            mLedEffectUntil = lNow + 450;
            mLedEffectIsError = false;
            if (mAutoSpeDiscoveryAfterPairing && !mPendingPostPairSpeDiscovery)
            {
                mPendingPostPairSpeDiscovery = true;
                logInfoP("Auto-SPE: queued encrypted post-pairing discovery");
            }
        }
        else if (lState == ControllerState::PairFailed)
        {
            lLed->errorCode(2);
            mLedEffectUntil = lNow + 3500;
            mLedEffectIsError = true;
            mPendingPostPairSpeDiscovery = false;
        }
        mLastControllerState = lState;
    }

    // Short visual acknowledge if number of paired channels changed.
    const uint8_t lPairedCount = countPairedChannels();
    if (mLastPairedCount != 0xFF && lPairedCount != mLastPairedCount)
    {
        lLed->flash(120);
        mLedEffectUntil = lNow + 260;
        mLedEffectIsError = false;
        if (mAutoSpeDiscoveryAfterPairing && !mPendingPostPairSpeDiscovery && lPairedCount > mLastPairedCount)
        {
            mPendingPostPairSpeDiscovery = true;
            logInfoP("Auto-SPE: queued encrypted post-pairing discovery");
        }
    }
    mLastPairedCount = lPairedCount;

    // Keep temporary effects active, then return to base mode.
    if (mLedEffectUntil != 0)
    {
        if (lNow < mLedEffectUntil)
            return;

        if (mLedEffectIsError)
            lLed->errorCode(0);

        mLedEffectUntil = 0;
        mLedEffectIsError = false;
        mStatusLedMode = StatusLedMode::Unknown;
    }

    StatusLedMode lTargetMode;
    if (isPairingState(lState))
        lTargetMode = StatusLedMode::Pairing;
    else if (lPairedCount > 0)
        lTargetMode = StatusLedMode::IdlePaired;
    else
        lTargetMode = StatusLedMode::IdleUnpaired;

    if (lTargetMode != mStatusLedMode)
        applyStatusLedMode(lTargetMode);
}

// --- OpenKNX::Module interface ---

void IoHomecontrol::setup()
{
    logDebugP("setup");

    // Wire controller back-pointer
    mController.setModule(this);

    // Initialize radio hardware
    mController.init();

    // Derive own node ID from MAC
    deriveOwnNodeId();

    // Create channels from ETS configuration
    mNumChannels = MIN(ParamIOHC_IOHCVisibleChannels, IOHC_ChannelCount);
    logDebugP("Visible channels: %d", mNumChannels);

    for (uint8_t i = 0; i < mNumChannels; i++)
    {
        mChannels[i] = new IoHomecontrolChannel(i, mController);
        mChannels[i]->setup();
    }

    // Initialize system key (may generate if first boot)
    initSystemKey();

    // Report module status OK
    KoIOHC_IOHC_Modulstatus.value(true, DPT_Switch);

    mLastControllerState = mController.state();
    mLastPairedCount = countPairedChannels();
    mStatusLedMode = StatusLedMode::Unknown;
    updateStatusLed();
}

void IoHomecontrol::loop()
{
    if (!openknx.afterStartupDelay())
        return;

    // Radio diagnostics take exclusive ownership of the radio so the controller
    // loop cannot consume TX completion and restart RX underneath them.
    if (mRadioDiagnostic.active)
        processRadioDiagnostic();
    else
        mController.loop();

    if (mPendingPostPairSpeDiscovery && !mRadioDiagnostic.active &&
        mController.state() == ControllerState::Idle)
    {
        mPendingPostPairSpeDiscovery = false;
        logInfoP("Auto-SPE: starting encrypted sub-device discovery");
        mController.startDiscovery(true);
    }

    // Update discovery status feedback (KO#22)
    ControllerState lState = mController.state();
    bool lDiscoveryActive = (lState == ControllerState::DiscoverySending ||
                             lState == ControllerState::DiscoveryListening);
    if (lDiscoveryActive != mLastDiscoveryActive)
    {
        KoIOHC_IOHC_DiscoveryAktiv.value(lDiscoveryActive, DPT_Switch);
        mLastDiscoveryActive = lDiscoveryActive;
    }

    // Update network scan status feedback (KO#24)
    bool lScanActive = mController.isNetworkScanActive();
    if (lScanActive != mLastScanActive)
    {
        KoIOHC_IOHC_NetzwerkScanAktiv.value(lScanActive, DPT_Switch);
        mLastScanActive = lScanActive;
    }

    // Remote observation: report observed remote addresses (KO#25, Feature 5)
    if (ParamIOHC_IOHCRemoteObserve)
    {
        uint8_t lObservedCount = mRemoteMap.observedCount();
        if (lObservedCount > mLastObservedCount)
        {
            uint32_t lAddr = mRemoteMap.observedAddress(lObservedCount - 1);
            KoIOHC_IOHC_BeobachteteFernbedienung.value(lAddr, Dpt(12, 1));
            logDebugP("Observed remote: 0x%06X", lAddr);
        }
        mLastObservedCount = lObservedCount;
    }

    // Process all channels
    for (uint8_t i = 0; i < mNumChannels; i++)
    {
        mChannels[i]->loop();
    }

    updateStatusLed();
}

bool IoHomecontrol::startRadioDiagnostic(RadioDiagnosticKind iKind, uint8_t iValue)
{
    if (mRadioDiagnostic.active)
    {
        logInfoP("RadioDiag: another radio diagnostic is already running");
        return false;
    }

    auto lHealth = mController.radioHealth();
    mRadioDiagnostic = RadioDiagnosticState();
    mRadioDiagnostic.kind = iKind;
    mRadioDiagnostic.active = true;
    mRadioDiagnostic.prevRxScanEnabled = mController.isRxScanEnabled();
    mRadioDiagnostic.restoreFreqIdx = lHealth.currentFreqIdx;
    mRadioDiagnostic.startedAtMs = millis();

    for (uint8_t i = 0; i < kRadioDiagSweepChannelCount; i++)
        mRadioDiagnostic.sweepStats[i] = RadioSweepStat();

    if (iKind == RadioDiagnosticKind::Sweep)
    {
        if (iValue < 1)
            iValue = 1;
        mRadioDiagnostic.requestedRounds = iValue;
        logInfoP("RadioSweep: started rounds=%u", static_cast<unsigned int>(mRadioDiagnostic.requestedRounds));
    }
    else if (iKind == RadioDiagnosticKind::Soak)
    {
        if (iValue < 1)
            iValue = 1;
        mRadioDiagnostic.soakBudgetMs = static_cast<uint32_t>(iValue) * 1000UL;
        logInfoP("RadioSoak: started seconds=%u", static_cast<unsigned int>(iValue));
    }
    else
    {
        logInfoP("RadioTxTest: started");
    }

    mController.setRxScanEnabled(false);
    mController.radio().standby();
    return true;
}

void IoHomecontrol::advanceRadioDiagnosticFrequency()
{
    mRadioDiagnostic.currentFreqPos++;
    if (mRadioDiagnostic.currentFreqPos >= kRadioDiagSweepChannelCount)
    {
        mRadioDiagnostic.currentFreqPos = 0;
        mRadioDiagnostic.completedRounds++;
    }
}

bool IoHomecontrol::deferRadioDiagnosticBusy()
{
    if (mRadioDiagnostic.busySinceMs == 0)
        mRadioDiagnostic.busySinceMs = millis();

    return millis() - mRadioDiagnostic.busySinceMs < kRadioDiagBusyRetryTimeoutMs;
}

void IoHomecontrol::clearRadioDiagnosticBusy()
{
    mRadioDiagnostic.busySinceMs = 0;
}

bool IoHomecontrol::restoreRadioDiagnosticReceive()
{
#if defined(RADIO_SX1262)
    if (mRadioDiagnostic.restoreFreqIdx < IOHC_NUM_FREQUENCIES)
    {
        if (mController.radio().setFrequencyBlocking(IOHC_FREQUENCIES[mRadioDiagnostic.restoreFreqIdx]) == RadioError::Busy)
            return false;
    }
    else if (mController.radio().setFrequencyBlocking(IOHC_FREQ_2) == RadioError::Busy)
    {
        return false;
    }

    if (mController.radio().startReceiveBlocking() == RadioError::Busy)
        return false;
#else
    const uint32_t lRestoreFreq = (mRadioDiagnostic.restoreFreqIdx < IOHC_NUM_FREQUENCIES)
                                      ? IOHC_FREQUENCIES[mRadioDiagnostic.restoreFreqIdx]
                                      : IOHC_FREQ_2;
    if (mController.radio().setFrequency(lRestoreFreq) != RadioError::None)
        return false;
    if (mController.radio().startReceive() != RadioError::None)
        return false;
#endif
    mController.setRxScanEnabled(mRadioDiagnostic.prevRxScanEnabled);
    return true;
}

bool IoHomecontrol::forceRestoreRadioDiagnosticReceive()
{
    mController.radio().standby();
#if defined(RADIO_SX1262)
    const RadioError lErr = mController.radio().startReceiveBlocking();
#else
    const RadioError lErr = mController.radio().startReceive();
#endif
    if (lErr == RadioError::None)
    {
        mController.setRxScanEnabled(mRadioDiagnostic.prevRxScanEnabled);
        return true;
    }

    mRadioDiagnostic.lastError = lErr;
    logInfoP("RadioDiag: forced RX restore failed err=%d", static_cast<int>(lErr));
    return false;
}

void IoHomecontrol::recordRadioDiagnosticSweepSuccess(RadioSweepStat &iStat, uint32_t iWaitedMs, uint16_t iIrq)
{
    clearRadioDiagnosticBusy();
    if (iWaitedMs > iStat.maxWaitMs)
        iStat.maxWaitMs = iWaitedMs;
    iStat.okCount++;
    mRadioDiagnostic.totalOk++;
    iStat.lastIrq = iIrq;
    iStat.lastTxStatus = mController.radio().lastTxSetStatus();
    iStat.lastDevErr = mController.radio().debugReadDeviceErrors();
    mRadioDiagnostic.txInProgress = false;
    advanceRadioDiagnosticFrequency();
}

void IoHomecontrol::recordRadioDiagnosticSweepFailure(RadioSweepStat &iStat, bool iTimeout)
{
    if (iTimeout)
    {
        iStat.timeoutCount++;
        mRadioDiagnostic.totalTimeout++;
    }
    else
    {
        iStat.failCount++;
        mRadioDiagnostic.totalFail++;
    }

    iStat.lastIrq = mController.radio().lastIrqStatus();
    iStat.lastTxStatus = mController.radio().lastTxSetStatus();
    iStat.lastDevErr = mController.radio().debugReadDeviceErrors();
    mController.radio().standby();
    mRadioDiagnostic.txInProgress = false;
    clearRadioDiagnosticBusy();
    advanceRadioDiagnosticFrequency();
}

void IoHomecontrol::finishRadioTxTest()
{
    const uint16_t lLastIrq = mController.radio().lastIrqStatus();
    const uint8_t lLastTxStatus = mController.radio().lastTxSetStatus();

    logInfoP("RadioTxTest: err=%d len=%d done=%d wait=%lu irq=0x%04X txSt=0x%02X stNow=0x%02X mode=%u cmd=%u irqNow=0x%04X devErrB=0x%04X devErrC=0x%04X devErrNow=0x%04X dio1=%d busy=%d",
             static_cast<int>(mRadioDiagnostic.lastError),
             static_cast<int>(sizeof(kRadioDiagPayload)),
             mRadioDiagnostic.txDone ? 1 : 0,
             static_cast<unsigned long>(mRadioDiagnostic.txWaitedMs),
             static_cast<unsigned int>(lLastIrq),
             static_cast<unsigned int>(lLastTxStatus),
             static_cast<unsigned int>(mRadioDiagnostic.statusNow),
             static_cast<unsigned int>(mRadioDiagnostic.modeNow),
             static_cast<unsigned int>(mRadioDiagnostic.cmdNow),
             static_cast<unsigned int>(mRadioDiagnostic.irqNow),
             static_cast<unsigned int>(mRadioDiagnostic.devErrBefore),
             static_cast<unsigned int>(mRadioDiagnostic.devErrCleared),
             static_cast<unsigned int>(mRadioDiagnostic.devErrNow),
             mRadioDiagnostic.dio1Now,
             mRadioDiagnostic.busyNow);

    mRadioDiagnostic = RadioDiagnosticState();
}

void IoHomecontrol::finishRadioSweep()
{
    if (mRadioDiagnostic.kind == RadioDiagnosticKind::Soak)
    {
        logInfoP("RadioSoak: seconds=%lu rounds=%lu tx=%u ok=%u fail=%u timeout=%u rxScan=%d",
                 static_cast<unsigned long>(mRadioDiagnostic.soakBudgetMs / 1000UL),
                 static_cast<unsigned long>(mRadioDiagnostic.completedRounds),
                 static_cast<unsigned int>(mRadioDiagnostic.completedRounds * kRadioDiagSweepChannelCount),
                 static_cast<unsigned int>(mRadioDiagnostic.totalOk),
                 static_cast<unsigned int>(mRadioDiagnostic.totalFail),
                 static_cast<unsigned int>(mRadioDiagnostic.totalTimeout),
                 mRadioDiagnostic.prevRxScanEnabled ? 1 : 0);
    }
    else
    {
        logInfoP("RadioSweep: rounds=%lu tx=%u ok=%u fail=%u timeout=%u rxScan=%d",
                 static_cast<unsigned long>(mRadioDiagnostic.completedRounds),
                 static_cast<unsigned int>(mRadioDiagnostic.completedRounds * kRadioDiagSweepChannelCount),
                 static_cast<unsigned int>(mRadioDiagnostic.totalOk),
                 static_cast<unsigned int>(mRadioDiagnostic.totalFail),
                 static_cast<unsigned int>(mRadioDiagnostic.totalTimeout),
                 mRadioDiagnostic.prevRxScanEnabled ? 1 : 0);
    }

    for (uint8_t i = 0; i < kRadioDiagSweepChannelCount; i++)
    {
        const auto &lStat = mRadioDiagnostic.sweepStats[i];
        logInfoP("  CH%u %luHz: ok=%u fail=%u timeout=%u maxWait=%lums txSt=0x%02X irq=0x%04X devErr=0x%04X",
                 static_cast<unsigned int>(kRadioDiagSweepChannels[i]),
                 static_cast<unsigned long>(kRadioDiagSweepFreqs[i]),
                 static_cast<unsigned int>(lStat.okCount),
                 static_cast<unsigned int>(lStat.failCount),
                 static_cast<unsigned int>(lStat.timeoutCount),
                 static_cast<unsigned long>(lStat.maxWaitMs),
                 static_cast<unsigned int>(lStat.lastTxStatus),
                 static_cast<unsigned int>(lStat.lastIrq),
                 static_cast<unsigned int>(lStat.lastDevErr));
    }

    mRadioDiagnostic = RadioDiagnosticState();
}

void IoHomecontrol::processRadioDiagnostic()
{
    if (!mRadioDiagnostic.active)
        return;

#if defined(RADIO_SX1262)
    if (mRadioDiagnostic.kind == RadioDiagnosticKind::TxTest)
    {
        if (!mRadioDiagnostic.txInProgress)
        {
            mRadioDiagnostic.devErrBefore = mController.radio().debugReadDeviceErrors();
            mController.radio().debugClearDeviceErrors();
            mRadioDiagnostic.devErrCleared = mController.radio().debugReadDeviceErrors();

            const auto lErr = mController.radio().startTransmitBlocking(kRadioDiagPayload, sizeof(kRadioDiagPayload));
            if (lErr == RadioError::Busy)
            {
                if (deferRadioDiagnosticBusy())
                    return;

                mRadioDiagnostic.lastError = RadioError::Busy;
                forceRestoreRadioDiagnosticReceive();
                clearRadioDiagnosticBusy();
                finishRadioTxTest();
                return;
            }

            clearRadioDiagnosticBusy();
            mRadioDiagnostic.lastError = lErr;
            if (lErr != RadioError::None)
            {
                if (!restoreRadioDiagnosticReceive())
                {
                    if (deferRadioDiagnosticBusy())
                        return;

                    forceRestoreRadioDiagnosticReceive();
                }

                clearRadioDiagnosticBusy();
                finishRadioTxTest();
                return;
            }

            mRadioDiagnostic.txInProgress = true;
            mRadioDiagnostic.txStartedAtMs = millis();
            return;
        }

        if (!mController.radio().isTxDoneBlocking())
        {
            if (millis() - mRadioDiagnostic.txStartedAtMs < kRadioDiagTxTestTimeoutMs)
                return;

            mController.radio().standby();
        }

        clearRadioDiagnosticBusy();
        mRadioDiagnostic.txDone = (mController.radio().state() != RadioState::Transmitting);
        mRadioDiagnostic.txWaitedMs = millis() - mRadioDiagnostic.txStartedAtMs;
        mRadioDiagnostic.statusNow = mController.radio().debugReadStatus();
        mRadioDiagnostic.modeNow = (mRadioDiagnostic.statusNow >> 4) & 0x07;
        mRadioDiagnostic.cmdNow = (mRadioDiagnostic.statusNow >> 1) & 0x07;
        mRadioDiagnostic.irqNow = mController.radio().debugReadIrqStatus();
        mRadioDiagnostic.devErrNow = mController.radio().debugReadDeviceErrors();
        mRadioDiagnostic.dio1Now = mController.radio().debugReadDio1Level();
        mRadioDiagnostic.busyNow = mController.radio().debugReadBusyLevel();

        if (!restoreRadioDiagnosticReceive())
        {
            if (deferRadioDiagnosticBusy())
                return;

            forceRestoreRadioDiagnosticReceive();
        }

        clearRadioDiagnosticBusy();
        finishRadioTxTest();
        return;
    }

    if (!mRadioDiagnostic.txInProgress)
    {
        if (mRadioDiagnostic.currentFreqPos == 0)
        {
            if (mRadioDiagnostic.kind == RadioDiagnosticKind::Sweep &&
                mRadioDiagnostic.completedRounds >= mRadioDiagnostic.requestedRounds)
            {
                if (!restoreRadioDiagnosticReceive())
                {
                    if (deferRadioDiagnosticBusy())
                        return;

                    forceRestoreRadioDiagnosticReceive();
                }

                clearRadioDiagnosticBusy();
                finishRadioSweep();
                return;
            }

            if (mRadioDiagnostic.kind == RadioDiagnosticKind::Soak &&
                mRadioDiagnostic.completedRounds > 0 &&
                millis() - mRadioDiagnostic.startedAtMs >= mRadioDiagnostic.soakBudgetMs)
            {
                if (!restoreRadioDiagnosticReceive())
                {
                    if (deferRadioDiagnosticBusy())
                        return;

                    forceRestoreRadioDiagnosticReceive();
                }

                clearRadioDiagnosticBusy();
                finishRadioSweep();
                return;
            }
        }

        const uint8_t lFreqPos = mRadioDiagnostic.currentFreqPos;
        auto &lStat = mRadioDiagnostic.sweepStats[lFreqPos];
        const auto lFreqErr = mController.radio().setFrequencyBlocking(kRadioDiagSweepFreqs[lFreqPos]);
        if (lFreqErr == RadioError::Busy)
        {
            if (deferRadioDiagnosticBusy())
                return;

            recordRadioDiagnosticSweepFailure(lStat, true);
            return;
        }

        clearRadioDiagnosticBusy();
        if (lFreqErr != RadioError::None)
        {
            recordRadioDiagnosticSweepFailure(lStat, false);
            return;
        }

        mController.radio().debugClearDeviceErrors();
        const auto lErr = mController.radio().startTransmitBlocking(kRadioDiagPayload, sizeof(kRadioDiagPayload));
        if (lErr == RadioError::Busy)
        {
            if (deferRadioDiagnosticBusy())
                return;

            recordRadioDiagnosticSweepFailure(lStat, true);
            return;
        }

        clearRadioDiagnosticBusy();
        if (lErr != RadioError::None)
        {
            recordRadioDiagnosticSweepFailure(lStat, false);
            return;
        }

        mRadioDiagnostic.txInProgress = true;
        mRadioDiagnostic.txStartedAtMs = millis();
        return;
    }

    const uint8_t lFreqPos = mRadioDiagnostic.currentFreqPos;
    auto &lStat = mRadioDiagnostic.sweepStats[lFreqPos];
    if (mController.radio().isTxDoneBlocking())
    {
        recordRadioDiagnosticSweepSuccess(lStat,
                                          millis() - mRadioDiagnostic.txStartedAtMs,
                                          mController.radio().lastIrqStatus());
        return;
    }

    const uint32_t lWaitedMs = millis() - mRadioDiagnostic.txStartedAtMs;
    if (lWaitedMs < kRadioDiagSweepTimeoutMs)
        return;

    const uint16_t lLastIrq = mController.radio().lastIrqStatus();
    if ((lLastIrq & SX1262_IRQ_TX_DONE) != 0)
    {
        mController.radio().standby();
        recordRadioDiagnosticSweepSuccess(lStat, lWaitedMs, lLastIrq);
        return;
    }

    const uint16_t lIrqNow = mController.radio().debugReadIrqStatus();
    if ((lIrqNow & SX1262_IRQ_TX_DONE) != 0)
    {
        mController.radio().standby();
        recordRadioDiagnosticSweepSuccess(lStat, lWaitedMs, lIrqNow);
        return;
    }

    if (mController.radio().debugReadDio1Level() > 0)
    {
        mController.radio().standby();
        recordRadioDiagnosticSweepSuccess(lStat, lWaitedMs, SX1262_IRQ_TX_DONE);
        return;
    }

    if (lWaitedMs < kRadioDiagSweepFinalTimeoutMs)
        return;

    if (lWaitedMs > lStat.maxWaitMs)
        lStat.maxWaitMs = lWaitedMs;
    recordRadioDiagnosticSweepFailure(lStat, true);
#elif defined(RADIO_SX1276)
    if (mRadioDiagnostic.kind == RadioDiagnosticKind::TxTest)
    {
        if (!mRadioDiagnostic.txInProgress)
        {
            // Start a non-blocking TX
            const auto lErr = mController.radio().startTransmit(kRadioDiagPayload, sizeof(kRadioDiagPayload));
            if (lErr == RadioError::Busy)
            {
                if (deferRadioDiagnosticBusy())
                    return;

                mRadioDiagnostic.lastError = RadioError::Busy;
                forceRestoreRadioDiagnosticReceive();
                clearRadioDiagnosticBusy();
                finishRadioTxTest();
                return;
            }

            clearRadioDiagnosticBusy();
            mRadioDiagnostic.lastError = lErr;
            if (lErr != RadioError::None)
            {
                if (!restoreRadioDiagnosticReceive())
                {
                    if (deferRadioDiagnosticBusy())
                        return;

                    forceRestoreRadioDiagnosticReceive();
                }

                clearRadioDiagnosticBusy();
                finishRadioTxTest();
                return;
            }

            mRadioDiagnostic.txInProgress = true;
            mRadioDiagnostic.txStartedAtMs = millis();
            return;
        }

        if (!mController.radio().isTxDone())
        {
            if (millis() - mRadioDiagnostic.txStartedAtMs < kRadioDiagTxTestTimeoutMs)
                return;

            mController.radio().standby();
        }

        clearRadioDiagnosticBusy();
        mRadioDiagnostic.txDone = (mController.radio().state() != RadioState::Transmitting);
        mRadioDiagnostic.txWaitedMs = millis() - mRadioDiagnostic.txStartedAtMs;
        mRadioDiagnostic.statusNow = mController.radio().debugReadRegister(REG_OPMODE);
        mRadioDiagnostic.modeNow = (mRadioDiagnostic.statusNow >> 4) & 0x07;
        mRadioDiagnostic.cmdNow = 0;
        const uint8_t lIrq1 = mController.radio().debugReadRegister(REG_IRQFLAGS1);
        const uint8_t lIrq2 = mController.radio().debugReadRegister(REG_IRQFLAGS2);
        mRadioDiagnostic.irqNow = (static_cast<uint16_t>(lIrq1) << 8) | lIrq2;
        mRadioDiagnostic.devErrNow = mController.radio().debugReadDeviceErrors();
        mRadioDiagnostic.dio1Now = 0;
        mRadioDiagnostic.busyNow = 0;

        if (!restoreRadioDiagnosticReceive())
        {
            if (deferRadioDiagnosticBusy())
                return;

            forceRestoreRadioDiagnosticReceive();
        }

        clearRadioDiagnosticBusy();
        finishRadioTxTest();
        return;
    }

    if (!mRadioDiagnostic.txInProgress)
    {
        if (mRadioDiagnostic.currentFreqPos == 0)
        {
            if (mRadioDiagnostic.kind == RadioDiagnosticKind::Sweep &&
                mRadioDiagnostic.completedRounds >= mRadioDiagnostic.requestedRounds)
            {
                if (!restoreRadioDiagnosticReceive())
                {
                    if (deferRadioDiagnosticBusy())
                        return;

                    forceRestoreRadioDiagnosticReceive();
                }

                clearRadioDiagnosticBusy();
                finishRadioSweep();
                return;
            }

            if (mRadioDiagnostic.kind == RadioDiagnosticKind::Soak &&
                mRadioDiagnostic.completedRounds > 0 &&
                millis() - mRadioDiagnostic.startedAtMs >= mRadioDiagnostic.soakBudgetMs)
            {
                if (!restoreRadioDiagnosticReceive())
                {
                    if (deferRadioDiagnosticBusy())
                        return;

                    forceRestoreRadioDiagnosticReceive();
                }

                clearRadioDiagnosticBusy();
                finishRadioSweep();
                return;
            }
        }

        const uint8_t lFreqPos = mRadioDiagnostic.currentFreqPos;
        auto &lStat = mRadioDiagnostic.sweepStats[lFreqPos];
        const auto lFreqErr = mController.radio().setFrequency(kRadioDiagSweepFreqs[lFreqPos]);
        if (lFreqErr == RadioError::Busy)
        {
            if (deferRadioDiagnosticBusy())
                return;

            recordRadioDiagnosticSweepFailure(lStat, true);
            return;
        }

        clearRadioDiagnosticBusy();
        if (lFreqErr != RadioError::None)
        {
            recordRadioDiagnosticSweepFailure(lStat, false);
            return;
        }

        const auto lErr = mController.radio().startTransmit(kRadioDiagPayload, sizeof(kRadioDiagPayload));
        if (lErr == RadioError::Busy)
        {
            if (deferRadioDiagnosticBusy())
                return;

            recordRadioDiagnosticSweepFailure(lStat, true);
            return;
        }

        clearRadioDiagnosticBusy();
        if (lErr != RadioError::None)
        {
            recordRadioDiagnosticSweepFailure(lStat, false);
            return;
        }

        mRadioDiagnostic.txInProgress = true;
        mRadioDiagnostic.txStartedAtMs = millis();
        return;
    }

    const uint8_t lFreqPos = mRadioDiagnostic.currentFreqPos;
    auto &lStat = mRadioDiagnostic.sweepStats[lFreqPos];
    if (mController.radio().isTxDone())
    {
        const uint16_t lIrq = mController.radio().lastIrqStatus();
        recordRadioDiagnosticSweepSuccess(lStat, millis() - mRadioDiagnostic.txStartedAtMs, lIrq);
        return;
    }

    const uint32_t lWaitedMs = millis() - mRadioDiagnostic.txStartedAtMs;
    if (lWaitedMs < kRadioDiagSweepTimeoutMs)
        return;

    const uint8_t lIrq1 = mController.radio().debugReadRegister(REG_IRQFLAGS1);
    const uint8_t lIrq2 = mController.radio().debugReadRegister(REG_IRQFLAGS2);
    const uint16_t lLastIrq = (static_cast<uint16_t>(lIrq1) << 8) | lIrq2;
    if ((lLastIrq & 0x0008) != 0)
    {
        mController.radio().standby();
        recordRadioDiagnosticSweepSuccess(lStat, lWaitedMs, lLastIrq);
        return;
    }

    if (lWaitedMs < kRadioDiagSweepFinalTimeoutMs)
        return;

    if (lWaitedMs > lStat.maxWaitMs)
        lStat.maxWaitMs = lWaitedMs;
    recordRadioDiagnosticSweepFailure(lStat, true);
#else
    mRadioDiagnostic = RadioDiagnosticState();
    return;
#endif
}

void IoHomecontrol::processAfterStartupDelay()
{
    logDebugP("afterStartupDelay");
    mController.startReceive();

    // P2: Power-on behavior per channel
    for (uint8_t i = 0; i < mNumChannels; i++)
    {
        if (!mChannels[i]->isPaired())
            continue;

        uint8_t _channelIndex = i; // needed by ParamIOHC_* macros
        uint8_t lBehavior = ParamIOHC_IOHCPowerOnBeh;
        if (lBehavior == 1) // Status abfragen
        {
            logDebugP("Ch%d: power-on -> request status", i + 1);
            mController.sendCommand(mChannels[i]->getNodeId(),
                                    mChannels[i]->getEncryptionKey(),
                                    IoHomeCommand::GetGeneralInfo3, 0);
        }
        else if (lBehavior == 2)
        {
            if (mChannels[i]->restoreLastKnownStateAfterStartup())
                logDebugP("Ch%d: power-on -> restore last state", i + 1);
            else
                logDebugP("Ch%d: power-on restore skipped", i + 1);
        }
    }
}

void IoHomecontrol::processInputKo(GroupObject &iKo)
{
    uint16_t lAsap = iKo.asap();

    // Global KO: Discovery trigger (KO#21)
    if (lAsap == IOHC_KoIOHC_Discovery)
    {
        if ((bool)iKo.value(DPT_Switch))
        {
            logDebugP("Discovery triggered via KNX");
            mController.startDiscovery();
            KoIOHC_IOHC_DiscoveryAktiv.value(true, DPT_Switch);
        }
        return;
    }

    // Global KO: Network Scan trigger (KO#23)
    if (lAsap == IOHC_KoIOHC_NetzwerkScan)
    {
        bool lStart = (bool)iKo.value(DPT_Switch);
        if (lStart)
        {
            logDebugP("Network scan started via KNX");
            mController.startNetworkScan();
        }
        else
        {
            logDebugP("Network scan stopped via KNX");
            mController.stopNetworkScan();
        }
        KoIOHC_IOHC_NetzwerkScanAktiv.value(lStart, DPT_Switch);
        return;
    }

    // Per-channel KO dispatch using knxprod.h macros
    int8_t lChannelId = IOHC_KoCalcChannel(lAsap);
    if (lChannelId >= 0 && lChannelId < mNumChannels)
    {
        // Temporarily set _channelIndex for the macro (the macro uses it internally)
        // We compute the IO index manually instead
        uint8_t lIoIndex = (lAsap - IOHC_KoBlockOffset) % IOHC_KoBlockSize;
        mChannels[lChannelId]->processInputKo(lIoIndex, iKo);
    }
}

bool IoHomecontrol::processFunctionProperty(uint8_t objectIndex, uint8_t propertyId,
                                            uint8_t length, uint8_t *data,
                                            uint8_t *resultData, uint8_t &resultLength)
{
    // io-homecontrol function properties: objectIndex=160, propertyId=10
    if (objectIndex != kFunctionPropertyObjectIndex || propertyId != kFunctionPropertyId || length < 1)
        return false;

    uint8_t lCmd = data[0];
    switch (lCmd)
    {
    case 0x10: // Start pairing
    {
        if (length < 2)
            break;
        uint8_t lChannel = data[1];
        if (lChannel < mNumChannels)
        {
            uint32_t lNodeId = 0;
            if (length >= 5)
            {
                lNodeId = ((uint32_t)data[2] << 16) |
                          ((uint32_t)data[3] << 8) |
                          (uint32_t)data[4];
            }

            bool lStarted = mController.startPairing(lChannel, lNodeId);
            resultData[0] = lStarted ? 0x00 : 0x02;
            if (!lStarted)
            {
                switch (mController.lastPairStartStatus())
                {
                case IoHomeController::PairStartStatus::Missing1WTarget:
                    resultData[0] = 0x03;
                    logInfoP("ETS: 1W pairing needs a target node ID on channel %d", lChannel + 1);
                    break;
                case IoHomeController::PairStartStatus::Busy:
                    resultData[0] = 0x04;
                    logInfoP("ETS: pairing blocked for channel %d, controller state=%s", lChannel + 1,
                             IoHomeController::stateName(mController.lastPairStartBlockedState()));
                    break;
                default:
                    logInfoP("ETS: pairing FAILED for channel %d", lChannel + 1);
                    break;
                }
            }

            if (lStarted)
            {
                if (mChannels[lChannel]->is1W())
                    logInfoP("ETS: 1W pairing started for channel %d", lChannel + 1);
                else
                    logInfoP("ETS: pairing started for channel %d", lChannel + 1);
            }

            resultLength = 1;
            return true;
        }
        break;
    }
    case 0x11: // Cancel pairing
    {
        mController.cancelPairing();
        logInfoP("ETS: pairing cancelled");
        resultData[0] = 0x00;
        resultLength = 1;
        return true;
    }
    case 0x12: // Query pairing status
    {
        if (length < 2)
            break;
        uint8_t lChannel = data[1];
        if (lChannel < mNumChannels)
        {
            resultData[0] = mChannels[lChannel]->isPaired() ? 0x01 : 0x00;
            uint32_t lNodeId = mChannels[lChannel]->getNodeId();
            resultData[1] = (lNodeId >> 16) & 0xFF;
            resultData[2] = (lNodeId >> 8) & 0xFF;
            resultData[3] = lNodeId & 0xFF;
            resultData[4] = static_cast<uint8_t>(mController.state());
            resultData[5] = static_cast<uint8_t>(mController.lastPairStartStatus());
            resultLength = 6;
            return true;
        }
        break;
    }
    case 0x13: // Unpair channel
    {
        if (length < 2)
            break;
        uint8_t lChannel = data[1];
        if (lChannel < mNumChannels)
        {
            mController.cancelPairing();
            mChannels[lChannel]->setNodeId(0);
            memset(const_cast<uint8_t *>(mChannels[lChannel]->getEncryptionKey()), 0, 16);
            resultData[0] = 0x00;
            resultLength = 1;
            openknx.flash.save();
            logInfoP("ETS: channel %d unpaired", lChannel + 1);
            return true;
        }
        break;
    }
    case 0x20: // Test: send position command
    {
        if (length < 3)
            break;
        uint8_t lChannel = data[1];
        uint8_t lPercent = data[2];
        if (lChannel < mNumChannels && mChannels[lChannel]->isPaired())
        {
            mController.sendCommand(mChannels[lChannel]->getNodeId(),
                                    mChannels[lChannel]->getEncryptionKey(),
                                    IoHomeCommand::Execute, lPercent);
            resultData[0] = 0x00;
            resultLength = 1;
            return true;
        }
        break;
    }
    }

    resultData[0] = 0xFF; // error
    resultLength = 1;
    return true;
}

// --- Flash persistence ---
// Layout v6: version(1) + systemKey(16) + numChannels(1) + per-channel: index(1) + flags(1) + nodeId(3) + key(16) + seq1W(2) + reserved(1) = 24 bytes + remoteMap
// flags: bit0=paired, bit1=is1W

uint16_t IoHomecontrol::flashSize()
{
    return 1 + 16 + 1 + (IOHC_ChannelCount * 24) + mRemoteMap.flashSize();
}

void IoHomecontrol::writeFlash()
{
    openknx.flash.writeByte(6); // format version 6 (added 1W sequence + mode)

    // Write system key
    const uint8_t *lSysKey = mController.getSystemKey();
    for (uint8_t k = 0; k < 16; k++)
        openknx.flash.writeByte(lSysKey[k]);

    openknx.flash.writeByte(mNumChannels);
    for (uint8_t i = 0; i < mNumChannels; i++)
    {
        openknx.flash.writeByte(i);
        uint8_t lFlags = 0;
        if (mChannels[i]->isPaired())
            lFlags |= 0x01;
        if (mChannels[i]->is1W())
            lFlags |= 0x02;
        openknx.flash.writeByte(lFlags);
        uint32_t lNodeId = mChannels[i]->getNodeId();
        openknx.flash.writeByte((lNodeId >> 16) & 0xFF);
        openknx.flash.writeByte((lNodeId >> 8) & 0xFF);
        openknx.flash.writeByte(lNodeId & 0xFF);
        const uint8_t *lKey = mChannels[i]->getEncryptionKey();
        for (uint8_t k = 0; k < 16; k++)
            openknx.flash.writeByte(lKey[k]);
        uint16_t lSeq = mChannels[i]->getSequence1W();
        openknx.flash.writeByte((lSeq >> 8) & 0xFF);
        openknx.flash.writeByte(lSeq & 0xFF);
        openknx.flash.writeByte(0x00); // reserved
    }

    // Write remote map
    uint8_t lRemoteBuf[IOHC_REMOTE_MAP_FLASH_SIZE];
    uint16_t lRemoteLen = mRemoteMap.writeToBuffer(lRemoteBuf);
    for (uint16_t i = 0; i < lRemoteLen; i++)
        openknx.flash.writeByte(lRemoteBuf[i]);
}

void IoHomecontrol::readFlash(const uint8_t *iBuffer, const uint16_t iSize)
{
    if (iSize == 0)
        return;

    constexpr uint16_t kFlashHeaderV6 = 1 + 16 + 1;
    constexpr uint16_t kFlashHeaderV5 = 1 + 16 + 1;
    constexpr uint16_t kFlashHeaderV1 = 1 + 1;
    constexpr uint16_t kFlashRecordV6 = 24;
    constexpr uint16_t kFlashRecordV5 = 21;
    constexpr uint16_t kFlashRecordV3 = 21 + 64;

    uint8_t lVersion = openknx.flash.readByte();

    if (lVersion == 6)
    {
        if (iSize < kFlashHeaderV6)
            return;

        uint8_t lSysKey[16];
        for (uint8_t k = 0; k < 16; k++)
            lSysKey[k] = openknx.flash.readByte();
        mController.setSystemKey(lSysKey);

        uint8_t lCount = openknx.flash.readByte();
        logDebugP("Reading %d channels from flash (v6)", lCount);

        const uint8_t lReadCount = clampFlashRecordCount(lCount, mNumChannels, iSize, kFlashHeaderV6, kFlashRecordV6);
        if (lReadCount != lCount)
            logDebugP("Flash v6 channel count clamped from %d to %d", lCount, lReadCount);

        for (uint8_t i = 0; i < lReadCount; i++)
        {
            uint8_t lIdx = openknx.flash.readByte();
            uint8_t lFlags = openknx.flash.readByte();
            uint32_t lNodeId = ((uint32_t)openknx.flash.readByte() << 16) |
                               ((uint32_t)openknx.flash.readByte() << 8) |
                               (uint32_t)openknx.flash.readByte();
            uint8_t lKey[16];
            for (uint8_t k = 0; k < 16; k++)
                lKey[k] = openknx.flash.readByte();
            uint16_t lSeq = ((uint16_t)openknx.flash.readByte() << 8) | openknx.flash.readByte();
            openknx.flash.readByte(); // reserved

            if ((lFlags & 0x01) && lIdx < mNumChannels)
            {
                mChannels[lIdx]->setNodeId(lNodeId);
                mChannels[lIdx]->setEncryptionKey(lKey);
                mChannels[lIdx]->setIs1W((lFlags & 0x02) != 0);
                mChannels[lIdx]->setSequence1W(lSeq);
            }
        }

        // Read remote map after channel data
        uint16_t lChannelDataSize = kFlashHeaderV6 + static_cast<uint16_t>(lReadCount) * kFlashRecordV6;
        uint16_t lRemaining = (iSize > lChannelDataSize) ? (iSize - lChannelDataSize) : 0;
        if (lRemaining > 0 && lRemaining <= IOHC_REMOTE_MAP_FLASH_SIZE)
        {
            const uint8_t *lRemoteData = iBuffer + lChannelDataSize;
            mRemoteMap.readFromBuffer(lRemoteData, lRemaining);
        }
    }
    else if (lVersion == 5 || lVersion == 4)
    {
        if (iSize < kFlashHeaderV5)
            return;

        uint8_t lSysKey[16];
        for (uint8_t k = 0; k < 16; k++)
            lSysKey[k] = openknx.flash.readByte();
        mController.setSystemKey(lSysKey);

        uint8_t lCount = openknx.flash.readByte();
        logDebugP("Reading %d channels from flash (v%d)", lCount, lVersion);

        const uint8_t lReadCount = clampFlashRecordCount(lCount, mNumChannels, iSize, kFlashHeaderV5, kFlashRecordV5);
        if (lReadCount != lCount)
            logDebugP("Flash v%d channel count clamped from %d to %d", lVersion, lCount, lReadCount);

        for (uint8_t i = 0; i < lReadCount; i++)
        {
            uint8_t lIdx = openknx.flash.readByte();
            uint8_t lFlags = openknx.flash.readByte();
            uint32_t lNodeId = ((uint32_t)openknx.flash.readByte() << 16) |
                               ((uint32_t)openknx.flash.readByte() << 8) |
                               (uint32_t)openknx.flash.readByte();
            uint8_t lKey[16];
            for (uint8_t k = 0; k < 16; k++)
                lKey[k] = openknx.flash.readByte();

            if ((lFlags & 0x01) && lIdx < mNumChannels)
            {
                mChannels[lIdx]->setNodeId(lNodeId);
                mChannels[lIdx]->setEncryptionKey(lKey);
            }
        }

        // v5: read remote map after channel data
        if (lVersion == 5)
        {
            uint16_t lChannelDataSize = kFlashHeaderV5 + static_cast<uint16_t>(lReadCount) * kFlashRecordV5;
            uint16_t lRemaining = (iSize > lChannelDataSize) ? (iSize - lChannelDataSize) : 0;
            if (lRemaining > 0 && lRemaining <= IOHC_REMOTE_MAP_FLASH_SIZE)
            {
                const uint8_t *lRemoteData = iBuffer + lChannelDataSize;
                mRemoteMap.readFromBuffer(lRemoteData, lRemaining);
            }
        }
    }
    else if (lVersion == 3)
    {
        if (iSize < kFlashHeaderV5)
            return;

        // Version 3: system key + channel data + scene data.
        // Scene positions are no longer restored from private flash. ETS parameters are the source of truth.
        uint8_t lSysKey[16];
        for (uint8_t k = 0; k < 16; k++)
            lSysKey[k] = openknx.flash.readByte();
        mController.setSystemKey(lSysKey);

        uint8_t lCount = openknx.flash.readByte();
        logDebugP("Reading %d channels from flash (v3, migrating scene config to ETS)", lCount);

        const uint8_t lReadCount = clampFlashRecordCount(lCount, mNumChannels, iSize, kFlashHeaderV5, kFlashRecordV3);
        if (lReadCount != lCount)
            logDebugP("Flash v3 channel count clamped from %d to %d", lCount, lReadCount);

        for (uint8_t i = 0; i < lReadCount; i++)
        {
            uint8_t lIdx = openknx.flash.readByte();
            uint8_t lFlags = openknx.flash.readByte();
            uint32_t lNodeId = ((uint32_t)openknx.flash.readByte() << 16) |
                               ((uint32_t)openknx.flash.readByte() << 8) |
                               (uint32_t)openknx.flash.readByte();
            uint8_t lKey[16];
            for (uint8_t k = 0; k < 16; k++)
                lKey[k] = openknx.flash.readByte();

            for (uint8_t s = 0; s < 64; s++)
                openknx.flash.readByte();

            if ((lFlags & 0x01) && lIdx < mNumChannels)
            {
                mChannels[lIdx]->setNodeId(lNodeId);
                mChannels[lIdx]->setEncryptionKey(lKey);
            }
        }
    }
    else if (lVersion == 2)
    {
        if (iSize < kFlashHeaderV5)
            return;

        // Version 2: system key + channel data (no scenes — migration)
        uint8_t lSysKey[16];
        for (uint8_t k = 0; k < 16; k++)
            lSysKey[k] = openknx.flash.readByte();
        mController.setSystemKey(lSysKey);

        uint8_t lCount = openknx.flash.readByte();
        logDebugP("Reading %d channels from flash (v2, migrating to v3)", lCount);

        const uint8_t lReadCount = clampFlashRecordCount(lCount, mNumChannels, iSize, kFlashHeaderV5, kFlashRecordV5);
        if (lReadCount != lCount)
            logDebugP("Flash v2 channel count clamped from %d to %d", lCount, lReadCount);

        for (uint8_t i = 0; i < lReadCount; i++)
        {
            uint8_t lIdx = openknx.flash.readByte();
            uint8_t lFlags = openknx.flash.readByte();
            uint32_t lNodeId = ((uint32_t)openknx.flash.readByte() << 16) |
                               ((uint32_t)openknx.flash.readByte() << 8) |
                               (uint32_t)openknx.flash.readByte();
            uint8_t lKey[16];
            for (uint8_t k = 0; k < 16; k++)
                lKey[k] = openknx.flash.readByte();

            if ((lFlags & 0x01) && lIdx < mNumChannels)
            {
                mChannels[lIdx]->setNodeId(lNodeId);
                mChannels[lIdx]->setEncryptionKey(lKey);
            }
        }
    }
    else if (lVersion == 1)
    {
        if (iSize < kFlashHeaderV1)
            return;

        // Version 1: legacy format without system key
        uint8_t lCount = openknx.flash.readByte();
        logDebugP("Reading %d channels from flash (v1, migrating)", lCount);

        const uint8_t lReadCount = clampFlashRecordCount(lCount, mNumChannels, iSize, kFlashHeaderV1, kFlashRecordV5);
        if (lReadCount != lCount)
            logDebugP("Flash v1 channel count clamped from %d to %d", lCount, lReadCount);

        for (uint8_t i = 0; i < lReadCount; i++)
        {
            uint8_t lIdx = openknx.flash.readByte();
            uint8_t lFlags = openknx.flash.readByte();
            uint32_t lNodeId = ((uint32_t)openknx.flash.readByte() << 16) |
                               ((uint32_t)openknx.flash.readByte() << 8) |
                               (uint32_t)openknx.flash.readByte();
            uint8_t lKey[16];
            for (uint8_t k = 0; k < 16; k++)
                lKey[k] = openknx.flash.readByte();

            if ((lFlags & 0x01) && lIdx < mNumChannels)
            {
                mChannels[lIdx]->setNodeId(lNodeId);
                mChannels[lIdx]->setEncryptionKey(lKey);
            }
        }
    }
    else
    {
        logDebugP("Unknown flash version %d", lVersion);
    }
}

// --- Serial console ---

void IoHomecontrol::showHelp()
{
    // Always show help (even when KNX not configured) so users can see
    // available IOHC commands on all builds.

    openknx.console.printHelpLine("iohc help", "Show io-homecontrol commands");
    openknx.console.printHelpLine("iohc status", "Show all channel status");
    openknx.console.printHelpLine("iohc status NN", "Show channel NN detail");
    openknx.console.printHelpLine("iohc pair NN [ADDR]", "Start pairing; 1W uses ETS target or hex ADDR override");
    openknx.console.printHelpLine("iohc pair cancel", "Cancel ongoing pairing");
    openknx.console.printHelpLine("iohc pairdiag on|off|status", "Verbose pairing/discovery diagnostics");
    openknx.console.printHelpLine("iohc unpair NN", "Remove pairing for channel NN");
    openknx.console.printHelpLine("iohc discover", "Broadcast discovery, list devices");
    openknx.console.printHelpLine("iohc discover spe", "Encrypted SPE/sub-device discovery");
    openknx.console.printHelpLine("iohc autospe on|off|status", "Runtime post-pair SPE discovery");
    openknx.console.printHelpLine("iohc send NN PP", "Send position PP% to channel NN");
    openknx.console.printHelpLine("iohc set1w NN", "Mark channel NN as 1W (one-way)");
    openknx.console.printHelpLine("iohc set2w NN", "Mark channel NN as 2W (two-way)");
    openknx.console.printHelpLine("iohc cozy temp NN TT", "Set thermostat temp (TT=tenths, 70-280)");
    openknx.console.printHelpLine("iohc cozy mode NN MM", "Set thermostat mode (0-3)");
    openknx.console.printHelpLine("iohc cozy presence NN 0/1", "Set presence on/off");
    openknx.console.printHelpLine("iohc cozy window NN 0/1", "Set window open/close");
    openknx.console.printHelpLine("iohc cozy poweron NN", "Send power on");
    openknx.console.printHelpLine("iohc cozy midnight NN", "Send midnight sync");
    openknx.console.printHelpLine("iohc remote list", "List tracked remotes");
    openknx.console.printHelpLine("iohc remote add ADDR NAME", "Add remote (hex addr)");
    openknx.console.printHelpLine("iohc remote del ADDR", "Remove remote");
    openknx.console.printHelpLine("iohc remote link ADDR DEV", "Link device to remote");
    openknx.console.printHelpLine("iohc remote unlink ADDR DEV", "Unlink device from remote");
    openknx.console.printHelpLine("iohc remote observed", "Show observed addresses");
    openknx.console.printHelpLine("iohc gateway on", "Enable fake gateway mode (respond to device pairing)");
    openknx.console.printHelpLine("iohc gateway off", "Disable fake gateway mode");
    openknx.console.printHelpLine("iohc gateway status", "Show gateway mode status and paired devices");
    openknx.console.printHelpLine("iohc gateway node ADDR", "Set gateway node ID (hex, default=0x112233)");
    openknx.console.printHelpLine("iohc gateway key HEX32", "Set gateway stack key (32 hex chars)");
    openknx.console.printHelpLine("iohc gateway clear", "Clear all paired devices in gateway mode");
    openknx.console.printHelpLine("iohc scan start", "Start passive network scan");
    openknx.console.printHelpLine("iohc scan stop", "Stop network scan");
    openknx.console.printHelpLine("iohc scan dump", "Dump captured packets");
    openknx.console.printHelpLine("iohc scan stats", "Show per-node statistics");
    openknx.console.printHelpLine("iohc radio", "Show radio health summary");
#if defined(RADIO_SX1262)
    openknx.console.printHelpLine("iohc radio txtest", "Send a minimal SX1262 TX test payload");
    openknx.console.printHelpLine("iohc radio sweep [N]", "TX self-test on all 3 io-homecontrol frequencies");
    openknx.console.printHelpLine("iohc radio soak [S]", "Time-budgeted TX soak on all 3 io-homecontrol frequencies");
    openknx.console.printHelpLine("iohc radio raw", "Show raw SX1262 register readback");
#else
    openknx.console.printHelpLine("iohc radio txtest", "Send a minimal SX1276 TX test payload (SPI TX)");
    openknx.console.printHelpLine("iohc radio sweep [N]", "TX self-test on all 3 io-homecontrol frequencies");
    openknx.console.printHelpLine("iohc radio soak [S]", "Time-budgeted TX soak on all 3 io-homecontrol frequencies");
    openknx.console.printHelpLine("iohc radio raw", "Show basic SX1276 register readback (SPI test)");
#endif
    openknx.console.printHelpLine("iohc proto selftest", "In-memory frame/crypto self-test");
}

bool IoHomecontrol::processCommand(const std::string iCmd, bool iDebugKo)
{
    // Allow the help command even when KNX is not configured so users
    // can discover IOHC commands on any build (SX1276 or SX1262).
    if (iCmd.substr(0, 5) != "iohc " || iCmd.length() < 6)
        return false;

    std::string lSub = iCmd.substr(5);

    if (lSub.substr(0, 1) == "h") // help
    {
        showHelp();
        return true;
    }

    // Allow setting radio frequency even when KNX is not configured.
    // Usage: `iohc radio freq 1|2|3` (channel index) or `iohc radio freq 868950000` (Hz)
    if (lSub.rfind("radio freq", 0) == 0)
    {
        std::string lArg;
        if (lSub.length() > strlen("radio freq"))
            lArg = lSub.substr(strlen("radio freq"));

        // trim leading spaces
        size_t lPos = 0;
        while (lPos < lArg.length() && isSpace(lArg[lPos]))
            lPos++;
        if (lPos > 0)
            lArg = lArg.substr(lPos);

        if (lArg.empty())
        {
            openknx.console.printHelpLine("iohc radio freq <1|2|3|Hz>", "Set radio frequency (channel index or Hz)");
            return true;
        }

        uint32_t lFreqHz = 0;
        bool lParsed = false;
        uint8_t lChanIdx = 0;
        if (parseChannelIndex(lArg, 3, lChanIdx))
        {
            const uint32_t lFreqs[3] = {IOHC_FREQ_1, IOHC_FREQ_2, IOHC_FREQ_3};
            lFreqHz = lFreqs[lChanIdx];
            lParsed = true;
        }
        else if (parseUnsignedDecimal(lArg, lFreqHz))
        {
            lParsed = true;
        }

        if (!lParsed)
        {
            openknx.console.printHelpLine("iohc radio freq <1|2|3|Hz>", "Set radio frequency (channel index or Hz)");
            return true;
        }

        const RadioError lErr = mController.radio().setFrequency(lFreqHz);
        if (lErr == RadioError::None)
        {
            logInfoP("Radio: setFrequency %lu Hz", lFreqHz);
            if (mController.isRxScanEnabled())
            {
                mController.setRxScanEnabled(false);
                logInfoP("Radio: rxScan disabled for manual frequency pinning");
            }
            mController.updateCurrentFrequencyIndex(lFreqHz);
            if (mController.startReceive() != RadioError::None)
                logInfoP("Radio: startReceive failed after setFrequency");
        }
        else
        {
            logInfoP("Radio: setFrequency err=%d", static_cast<int>(lErr));
        }
        return true;
    }

    if (!knx.configured())
    {
        openknx.console.printHelpLine("iohc", "Device is not Configured! Please configure KNX settings in ETS and power cycle the device.");
        return false;
    }
    if (lSub.substr(0, 6) == "status")
    {
        if (lSub.length() > 7)
        {
            uint8_t lIdx = 0;
            if (parseChannelIndex(lSub.substr(7, 2), mNumChannels, lIdx))
            {
                IoHomecontrolChannel *lCh = mChannels[lIdx];
                logInfoP("Ch%02d: %s NodeId=%06X%s", lIdx + 1,
                         lCh->isPaired() ? "PAIRED" : "unpaired",
                         lCh->getNodeId(),
                         lCh->is1W() ? " [1W]" : "");
                if (lCh->is1W())
                {
                    logInfoP("  1W seq=%d", lCh->getSequence1W());
                    if (!lCh->isPaired() && lCh->getConfigured1WTargetNodeId() != 0)
                        logInfoP("  1W target=%06X", lCh->getConfigured1WTargetNodeId());
                }
                if (iDebugKo)
                    openknx.console.writeDiagnoseKo("Ch%02d %s %06X", lIdx + 1,
                                                    lCh->isPaired() ? "P" : "-",
                                                    lCh->getNodeId());
            }
            else
            {
                logInfoP("Invalid channel: %s", lSub.substr(7).c_str());
            }
        }
        else
        {
            logInfoP("Auto-SPE post-pair discovery: %s%s",
                     mAutoSpeDiscoveryAfterPairing ? "on" : "off",
                     mPendingPostPairSpeDiscovery ? " (pending)" : "");
            for (uint8_t i = 0; i < mNumChannels; i++)
            {
                IoHomecontrolChannel *lCh = mChannels[i];
                if (lCh->is1W() && !lCh->isPaired() && lCh->getConfigured1WTargetNodeId() != 0)
                {
                    logInfoP("Ch%02d: %s NodeId=%06X [1W target=%06X]", i + 1,
                             lCh->isPaired() ? "PAIRED" : "unpaired",
                             lCh->getNodeId(),
                             lCh->getConfigured1WTargetNodeId());
                }
                else
                {
                    logInfoP("Ch%02d: %s NodeId=%06X%s", i + 1,
                             lCh->isPaired() ? "PAIRED" : "unpaired",
                             lCh->getNodeId(),
                             lCh->is1W() ? " [1W]" : "");
                }
            }
        }
        return true;
    }

    if (lSub.substr(0, 8) == "pairdiag")
    {
        std::string lPairDiagCmd = (lSub.length() > 9) ? lSub.substr(9) : "status";
        if (lPairDiagCmd.substr(0, 2) == "on" || lPairDiagCmd.substr(0, 6) == "enable" || lPairDiagCmd.substr(0, 1) == "1")
        {
            mController.setPairDiagnosticTraceEnabled(true);
            logInfoP("PairDiag: verbose pairing/discovery trace enabled");
        }
        else if (lPairDiagCmd.substr(0, 3) == "off" || lPairDiagCmd.substr(0, 7) == "disable" || lPairDiagCmd.substr(0, 1) == "0")
        {
            mController.setPairDiagnosticTraceEnabled(false);
            logInfoP("PairDiag: verbose pairing/discovery trace disabled");
        }
        else if (!(lPairDiagCmd.substr(0, 6) == "status" || lPairDiagCmd.substr(0, 4) == "dump"))
        {
            logInfoP("Usage: iohc pairdiag on|off|status");
        }

        mController.logPairDiagnosticStatus();
        return true;
    }

    if (lSub.substr(0, 4) == "pair")
    {
        if (lSub.length() > 5 && lSub.substr(5, 1) == "c")
        {
            mController.cancelPairing();
            logInfoP("Pairing cancelled");
            return true;
        }
        if (lSub.length() > 5)
        {
            uint8_t lIdx = 0;
            if (parseChannelIndex(lSub.substr(5, 2), mNumChannels, lIdx))
            {
                uint32_t lNodeId = 0;
                if (lSub.length() > 8 && !parseHex24(lSub.substr(8), lNodeId))
                {
                    logInfoP("Invalid node address: %s", lSub.substr(8).c_str());
                    return true;
                }

                bool lOk = mController.startPairing(lIdx, lNodeId);
                if (lOk)
                {
                    if (mChannels[lIdx]->is1W())
                        logInfoP("1W pairing started for channel %d", lIdx + 1);
                    else
                        logInfoP("Pairing started for channel %d", lIdx + 1);
                }
                else if (mChannels[lIdx]->is1W() && lNodeId == 0 && !mChannels[lIdx]->isPaired() &&
                         mChannels[lIdx]->getConfigured1WTargetNodeId() == 0)
                {
                    logInfoP("1W pairing needs an ETS target node ID or: iohc pair %02d AABBCC", lIdx + 1);
                }
                else
                {
                    logInfoP("Pairing FAILED for channel %d", lIdx + 1);
                }
            }
            else
            {
                logInfoP("Invalid channel: %s", lSub.substr(5).c_str());
            }
        }
        return true;
    }

    if (lSub.substr(0, 6) == "unpair")
    {
        if (lSub.length() > 7)
        {
            uint8_t lIdx = 0;
            if (parseChannelIndex(lSub.substr(7, 2), mNumChannels, lIdx))
            {
                mController.cancelPairing();
                mChannels[lIdx]->setNodeId(0);
                memset(const_cast<uint8_t *>(mChannels[lIdx]->getEncryptionKey()), 0, 16);
                openknx.flash.save();
                logInfoP("Channel %d unpaired", lIdx + 1);
            }
            else
            {
                logInfoP("Invalid channel: %s", lSub.substr(7).c_str());
            }
        }
        return true;
    }

    if (lSub.substr(0, 8) == "discover")
    {
        if (lSub.length() > 9 && lSub.substr(9, 3) == "spe")
        {
            logInfoP("Starting encrypted SPE discovery broadcast...");
            mController.startDiscovery(true);
        }
        else
        {
            logInfoP("Starting discovery broadcast...");
            mController.startDiscovery();
        }
        return true;
    }

    if (lSub.substr(0, 7) == "autospe")
    {
        std::string lAutoSpeCmd = (lSub.length() > 8) ? lSub.substr(8) : "status";
        if (lAutoSpeCmd.substr(0, 2) == "on" || lAutoSpeCmd.substr(0, 6) == "enable" || lAutoSpeCmd.substr(0, 1) == "1")
        {
            mAutoSpeDiscoveryAfterPairing = true;
            logInfoP("Auto-SPE post-pair discovery: on");
        }
        else if (lAutoSpeCmd.substr(0, 3) == "off" || lAutoSpeCmd.substr(0, 7) == "disable" || lAutoSpeCmd.substr(0, 1) == "0")
        {
            mAutoSpeDiscoveryAfterPairing = false;
            mPendingPostPairSpeDiscovery = false;
            logInfoP("Auto-SPE post-pair discovery: off");
        }
        else
        {
            logInfoP("Auto-SPE post-pair discovery: %s%s",
                     mAutoSpeDiscoveryAfterPairing ? "on" : "off",
                     mPendingPostPairSpeDiscovery ? " (pending)" : "");
        }
        return true;
    }

    if (lSub.substr(0, 4) == "send")
    {
        if (lSub.length() > 8)
        {
            uint8_t lIdx = 0;
            uint32_t lPosValue = 0;
            if (parseChannelIndex(lSub.substr(5, 2), mNumChannels, lIdx) &&
                parseUnsignedDecimal(lSub.substr(8), lPosValue) && lPosValue <= 100)
            {
                const uint8_t lPos = static_cast<uint8_t>(lPosValue);
                if (mChannels[lIdx]->isPaired())
                {
                    mController.sendCommand(mChannels[lIdx]->getNodeId(),
                                            mChannels[lIdx]->getEncryptionKey(),
                                            IoHomeCommand::Execute, lPos);
                    logInfoP("Sent position %d%% to channel %d", lPos, lIdx + 1);
                }
                else
                {
                    logInfoP("Channel %d not paired", lIdx + 1);
                }
            }
            else
            {
                logInfoP("Invalid send command: %s", lSub.c_str());
            }
        }
        return true;
    }

    if (lSub.substr(0, 5) == "set1w" && lSub.length() > 6)
    {
        uint8_t lIdx = 0;
        if (parseChannelIndex(lSub.substr(6, 2), mNumChannels, lIdx))
        {
            mChannels[lIdx]->setIs1W(true);
            if (mChannels[lIdx]->isPaired())
                openknx.flash.save();
            logInfoP("Channel %d set to 1W mode (seq=%d)", lIdx + 1, mChannels[lIdx]->getSequence1W());
        }
        else
            logInfoP("Invalid channel: %s", lSub.substr(6).c_str());
        return true;
    }

    if (lSub.substr(0, 5) == "set2w" && lSub.length() > 6)
    {
        uint8_t lIdx = 0;
        if (parseChannelIndex(lSub.substr(6, 2), mNumChannels, lIdx))
        {
            mChannels[lIdx]->setIs1W(false);
            if (mChannels[lIdx]->isPaired())
                openknx.flash.save();
            logInfoP("Channel %d set to 2W mode", lIdx + 1);
        }
        else
            logInfoP("Invalid channel: %s", lSub.substr(6).c_str());
        return true;
    }

    if (lSub.substr(0, 4) == "cozy")
    {
        if (lSub.length() > 5)
        {
            std::string lCozyCmd = lSub.substr(5);

            auto parseChannelIdx = [&](const std::string &iStr, size_t iOffset) -> int8_t
            {
                if (iStr.length() <= iOffset)
                    return -1;
                uint8_t idx = 0;
                if (!parseChannelIndex(iStr.substr(iOffset, 2), mNumChannels, idx))
                    return -1;
                return (idx < mNumChannels && mChannels[idx]->isPaired()) ? idx : -1;
            };

            if (lCozyCmd.substr(0, 4) == "temp" && lCozyCmd.length() > 7)
            {
                int8_t lIdx = parseChannelIdx(lCozyCmd, 5);
                uint32_t lTempValue = 0;
                if (lIdx >= 0 && parseUnsignedDecimal(lCozyCmd.substr(8), lTempValue))
                {
                    uint8_t lTemp = static_cast<uint8_t>(lTempValue);
                    mController.sendCommand(mChannels[lIdx]->getNodeId(),
                                            mChannels[lIdx]->getEncryptionKey(),
                                            IoHomeCommand::WritePrivate, 0x03, lTemp);
                    logInfoP("Cozy: set temperature %d (tenths) on channel %d", lTemp, lIdx + 1);
                }
                else
                    logInfoP("Invalid cozy temp command: %s", lCozyCmd.c_str());
            }
            else if (lCozyCmd.substr(0, 4) == "mode" && lCozyCmd.length() > 7)
            {
                int8_t lIdx = parseChannelIdx(lCozyCmd, 5);
                uint32_t lModeValue = 0;
                if (lIdx >= 0 && parseUnsignedDecimal(lCozyCmd.substr(8), lModeValue))
                {
                    uint8_t lMode = static_cast<uint8_t>(lModeValue);
                    mController.sendCommand(mChannels[lIdx]->getNodeId(),
                                            mChannels[lIdx]->getEncryptionKey(),
                                            IoHomeCommand::WritePrivate, 0x04, lMode);
                    logInfoP("Cozy: set mode %d on channel %d", lMode, lIdx + 1);
                }
                else
                    logInfoP("Invalid cozy mode command: %s", lCozyCmd.c_str());
            }
            else if (lCozyCmd.substr(0, 8) == "presence" && lCozyCmd.length() > 11)
            {
                int8_t lIdx = parseChannelIdx(lCozyCmd, 9);
                uint32_t lValValue = 0;
                if (lIdx >= 0 && parseUnsignedDecimal(lCozyCmd.substr(12), lValValue))
                {
                    uint8_t lVal = static_cast<uint8_t>(lValValue);
                    mController.sendCommand(mChannels[lIdx]->getNodeId(),
                                            mChannels[lIdx]->getEncryptionKey(),
                                            IoHomeCommand::WritePrivate, 0x10, lVal);
                    logInfoP("Cozy: set presence %s on channel %d", lVal ? "ON" : "OFF", lIdx + 1);
                }
                else
                    logInfoP("Invalid cozy presence command: %s", lCozyCmd.c_str());
            }
            else if (lCozyCmd.substr(0, 6) == "window" && lCozyCmd.length() > 9)
            {
                int8_t lIdx = parseChannelIdx(lCozyCmd, 7);
                uint32_t lValValue = 0;
                if (lIdx >= 0 && parseUnsignedDecimal(lCozyCmd.substr(10), lValValue))
                {
                    uint8_t lVal = static_cast<uint8_t>(lValValue);
                    mController.sendCommand(mChannels[lIdx]->getNodeId(),
                                            mChannels[lIdx]->getEncryptionKey(),
                                            IoHomeCommand::WritePrivate, 0x0E, lVal);
                    logInfoP("Cozy: set window %s on channel %d", lVal ? "OPEN" : "CLOSED", lIdx + 1);
                }
                else
                    logInfoP("Invalid cozy window command: %s", lCozyCmd.c_str());
            }
            else if (lCozyCmd.substr(0, 7) == "poweron" && lCozyCmd.length() > 8)
            {
                int8_t lIdx = parseChannelIdx(lCozyCmd, 8);
                if (lIdx >= 0)
                {
                    mController.sendCommand(mChannels[lIdx]->getNodeId(),
                                            mChannels[lIdx]->getEncryptionKey(),
                                            IoHomeCommand::WritePrivate, 0x0C, 0);
                    logInfoP("Cozy: power on channel %d", lIdx + 1);
                }
            }
            else if (lCozyCmd.substr(0, 8) == "midnight" && lCozyCmd.length() > 9)
            {
                int8_t lIdx = parseChannelIdx(lCozyCmd, 9);
                if (lIdx >= 0)
                {
                    mController.sendCommand(mChannels[lIdx]->getNodeId(),
                                            mChannels[lIdx]->getEncryptionKey(),
                                            IoHomeCommand::WritePrivate, 0x00, 0);
                    logInfoP("Cozy: midnight sync on channel %d", lIdx + 1);
                }
            }
        }
        return true;
    }

    if (lSub.substr(0, 6) == "remote")
    {
        if (lSub.length() > 7)
        {
            std::string lRemoteCmd = lSub.substr(7);

            if (lRemoteCmd.substr(0, 4) == "list")
            {
                uint8_t lCount = mRemoteMap.count();
                logInfoP("Tracked remotes: %d", lCount);
                for (uint8_t i = 0; i < IOHC_REMOTE_MAX_ENTRIES; i++)
                {
                    const IoHomeRemoteEntry *lE = mRemoteMap.entry(i);
                    if (lE && lE->active)
                    {
                        logInfoP("  0x%06X \"%s\" links=%d seq=%d",
                                 lE->address, lE->name, lE->linkCount, lE->sequenceNum);
                        for (uint8_t j = 0; j < lE->linkCount; j++)
                            logInfoP("    -> 0x%06X", lE->linkedDevices[j]);
                    }
                }
            }
            else if (lRemoteCmd.substr(0, 3) == "add" && lRemoteCmd.length() > 10)
            {
                uint32_t lAddr = 0;
                if (parseHex24(lRemoteCmd.substr(4, 6), lAddr))
                {
                    std::string lName = lRemoteCmd.substr(11);
                    if (mRemoteMap.addRemote(lAddr, lName.c_str()))
                        logInfoP("Remote 0x%06X added", lAddr);
                    else
                        logInfoP("Failed to add remote 0x%06X", lAddr);
                }
                else
                    logInfoP("Invalid remote address: %s", lRemoteCmd.substr(4, 6).c_str());
            }
            else if (lRemoteCmd.substr(0, 3) == "del" && lRemoteCmd.length() > 4)
            {
                uint32_t lAddr = 0;
                if (parseHex24(lRemoteCmd.substr(4, 6), lAddr))
                {
                    if (mRemoteMap.removeRemote(lAddr))
                        logInfoP("Remote 0x%06X removed", lAddr);
                    else
                        logInfoP("Remote 0x%06X not found", lAddr);
                }
                else
                    logInfoP("Invalid remote address: %s", lRemoteCmd.substr(4).c_str());
            }
            else if (lRemoteCmd.substr(0, 4) == "link" && lRemoteCmd.length() > 17)
            {
                uint32_t lAddr = 0;
                uint32_t lDev = 0;
                if (parseHex24(lRemoteCmd.substr(5, 6), lAddr) &&
                    parseHex24(lRemoteCmd.substr(12, 6), lDev) &&
                    mRemoteMap.linkDevice(lAddr, lDev))
                    logInfoP("Linked 0x%06X -> 0x%06X", lAddr, lDev);
                else
                    logInfoP("Failed to link");
            }
            else if (lRemoteCmd.substr(0, 6) == "unlink" && lRemoteCmd.length() > 19)
            {
                uint32_t lAddr = 0;
                uint32_t lDev = 0;
                if (parseHex24(lRemoteCmd.substr(7, 6), lAddr) &&
                    parseHex24(lRemoteCmd.substr(14, 6), lDev) &&
                    mRemoteMap.unlinkDevice(lAddr, lDev))
                    logInfoP("Unlinked 0x%06X -/-> 0x%06X", lAddr, lDev);
                else
                    logInfoP("Failed to unlink");
            }
            else if (lRemoteCmd.substr(0, 8) == "observed")
            {
                uint8_t lCount = mRemoteMap.observedCount();
                logInfoP("Observed addresses: %d", lCount);
                for (uint8_t i = 0; i < lCount; i++)
                    logInfoP("  0x%06X", mRemoteMap.observedAddress(i));
            }
        }
        return true;
    }

    if (lSub.substr(0, 4) == "scan")
    {
        if (lSub.length() > 5)
        {
            std::string lScanCmd = lSub.substr(5);

            if (lScanCmd.substr(0, 5) == "start")
            {
                mController.startNetworkScan();
                logInfoP("Network scan started (multi-freq passive capture)");
            }
            else if (lScanCmd.substr(0, 4) == "stop")
            {
                mController.stopNetworkScan();
                logInfoP("Network scan stopped");
            }
            else if (lScanCmd.substr(0, 4) == "dump")
            {
                const auto *lBuf = mController.scanBuffer();
                uint8_t lHead = mController.scanBufferHead();
                logInfoP("Scan buffer (newest first):");
                for (uint8_t n = 0; n < 32; n++)
                {
                    uint8_t lIdx = (lHead + 32 - 1 - n) % 32;
                    if (!lBuf[lIdx].valid)
                        continue;
                    const auto &lEntry = lBuf[lIdx];
                    logInfoP("  [%lu] freq=%d src=%06X dst=%06X cmd=%s(0x%02X) len=%d rssi=%ddBm",
                             lEntry.timestamp, lEntry.freqIdx,
                             lEntry.frame.getSrcNodeId(), lEntry.frame.getDestNodeId(),
                             IoHomeController::commandName(lEntry.frame.commandId),
                             static_cast<uint8_t>(lEntry.frame.commandId),
                             lEntry.frame.dataLen, lEntry.rssi);
                }
            }
            else if (lScanCmd.substr(0, 5) == "stats")
            {
                const auto *lStats = mController.nodeStats();
                logInfoP("Node statistics:");
                for (uint8_t i = 0; i < 16; i++)
                {
                    if (!lStats[i].active)
                        continue;
                    logInfoP("  0x%06X: %d pkts, lastRssi=%ddBm, lastCmd=%s(0x%02X)",
                             lStats[i].nodeId, lStats[i].packetCount,
                             lStats[i].lastRssi,
                             IoHomeController::commandName(lStats[i].lastCommand),
                             static_cast<uint8_t>(lStats[i].lastCommand));
                }
            }
        }
        return true;
    }

    if (lSub == "radio raw")
    {
        handleRadioRaw(iDebugKo);
        return true;
    }

    if (lSub == "radio txtest")
    {
#if defined(RADIO_SX1262)
        const bool lPrevRxScan = mController.isRxScanEnabled();
        mController.setRxScanEnabled(false);

        const uint16_t lDevErrBefore = mController.radio().debugReadDeviceErrors();
        mController.radio().debugClearDeviceErrors();
        const uint16_t lDevErrCleared = mController.radio().debugReadDeviceErrors();

        mController.radio().standby();

        const auto lErr = mController.radio().startTransmitBlocking(kRadioDiagPayload, sizeof(kRadioDiagPayload));
        bool lDone = false;
        uint32_t lWaitedMs = 0;
        uint8_t lStatusNow = 0;
        uint8_t lModeNow = 0;
        uint8_t lCmdNow = 0;
        uint16_t lIrqNow = 0;
        uint16_t lDevErrNow = 0;
        int lDio1Now = 0;
        int lBusyNow = 0;

        if (lErr == RadioError::None)
        {
            const uint32_t lStart = millis();
            while (millis() - lStart < kRadioDiagTxTestTimeoutMs)
            {
                if (mController.radio().isTxDoneBlocking())
                {
                    lDone = true;
                    break;
                }
                delay(1);
            }
            lWaitedMs = millis() - lStart;
            lStatusNow = mController.radio().debugReadStatus();
            lModeNow = (lStatusNow >> 4) & 0x07;
            lCmdNow = (lStatusNow >> 1) & 0x07;
            lIrqNow = mController.radio().debugReadIrqStatus();
            lDevErrNow = mController.radio().debugReadDeviceErrors();
            lDio1Now = mController.radio().debugReadDio1Level();
            lBusyNow = mController.radio().debugReadBusyLevel();
        }

        mController.radio().startReceiveBlocking();
        mController.setRxScanEnabled(lPrevRxScan);

        logInfoP("RadioTxTest: err=%d len=%d done=%d wait=%lu irq=0x%04X txSt=0x%02X stNow=0x%02X mode=%u cmd=%u irqNow=0x%04X devErrB=0x%04X devErrC=0x%04X devErrNow=0x%04X dio1=%d busy=%d",
                 static_cast<int>(lErr),
                 static_cast<int>(sizeof(kRadioDiagPayload)),
                 lDone ? 1 : 0,
                 static_cast<unsigned long>(lWaitedMs),
                 static_cast<unsigned int>(mController.radio().lastIrqStatus()),
                 static_cast<unsigned int>(mController.radio().lastTxSetStatus()),
                 static_cast<unsigned int>(lStatusNow),
                 static_cast<unsigned int>(lModeNow),
                 static_cast<unsigned int>(lCmdNow),
                 static_cast<unsigned int>(lIrqNow),
                 static_cast<unsigned int>(lDevErrBefore),
                 static_cast<unsigned int>(lDevErrCleared),
                 static_cast<unsigned int>(lDevErrNow),
                 lDio1Now,
                 lBusyNow);
#else
        // SX1276 path: perform a minimal non-blocking TX test using available APIs
        const bool lPrevRxScan = mController.isRxScanEnabled();
        mController.setRxScanEnabled(false);

        mController.radio().standby();

        const auto lErr = mController.radio().startTransmit(kRadioDiagPayload, sizeof(kRadioDiagPayload));
        bool lDone = false;
        uint32_t lWaitedMs = 0;

        if (lErr == RadioError::None)
        {
            const uint32_t lStart = millis();
            while (millis() - lStart < kRadioDiagTxTestTimeoutMs)
            {
                if (mController.radio().isTxDone())
                {
                    lDone = true;
                    break;
                }
                delay(1);
            }
            lWaitedMs = millis() - lStart;
        }

        // Read basic IRQ/status registers for diagnostics
        const uint8_t lIrq1 = mController.radio().debugReadRegister(REG_IRQFLAGS1);
        const uint8_t lIrq2 = mController.radio().debugReadRegister(REG_IRQFLAGS2);
        const uint8_t lOp = mController.radio().debugReadRegister(REG_OPMODE);

        // Restore RX and rx-scan
        mController.radio().startReceive();
        mController.setRxScanEnabled(lPrevRxScan);

        logInfoP("RadioTxTest: err=%d len=%d done=%d wait=%lu irq1=0x%02X irq2=0x%02X op=0x%02X",
                 static_cast<int>(lErr),
                 static_cast<int>(sizeof(kRadioDiagPayload)),
                 lDone ? 1 : 0,
                 static_cast<unsigned long>(lWaitedMs),
                 static_cast<unsigned int>(lIrq1),
                 static_cast<unsigned int>(lIrq2),
                 static_cast<unsigned int>(lOp));
#endif
        return true;
    }

    if (lSub.substr(0, 11) == "radio sweep" || lSub.substr(0, 10) == "radio soak")
    {
#if defined(RADIO_SX1262)
        const bool lIsSoak = (lSub.substr(0, 10) == "radio soak");
        const size_t lPrefixLen = lIsSoak ? 10 : 11;
        uint8_t lRounds = 1;
        uint32_t lBudgetSeconds = 10;
        if (lSub.length() > lPrefixLen)
        {
            const size_t lArgPos = lSub.find_first_not_of(' ', lPrefixLen);
            if (lArgPos != std::string::npos)
            {
                uint32_t lParsedValue = 0;
                if (!parseUnsignedDecimal(lSub.substr(lArgPos), lParsedValue))
                {
                    logInfoP("Invalid radio diagnostic argument: %s", lSub.substr(lArgPos).c_str());
                    return true;
                }
                if (lIsSoak)
                {
                    if (lParsedValue < 1)
                        lParsedValue = 1;
                    if (lParsedValue > 300)
                        lParsedValue = 300;
                    lBudgetSeconds = static_cast<uint32_t>(lParsedValue);
                }
                else
                {
                    if (lParsedValue < 1)
                        lParsedValue = 1;
                    if (lParsedValue > 10)
                        lParsedValue = 10;
                    lRounds = static_cast<uint8_t>(lParsedValue);
                }
            }
        }
        if (lIsSoak)
            startRadioDiagnostic(RadioDiagnosticKind::Soak, static_cast<uint8_t>(lBudgetSeconds));
        else
            startRadioDiagnostic(RadioDiagnosticKind::Sweep, lRounds);
#else
        {
            const bool lIsSoak = (lSub.substr(0, 10) == "radio soak");
            const size_t lPrefixLen = lIsSoak ? 10 : 11;
            uint8_t lRounds = 1;
            uint32_t lBudgetSeconds = 10;
            if (lSub.length() > lPrefixLen)
            {
                const size_t lArgPos = lSub.find_first_not_of(' ', lPrefixLen);
                if (lArgPos != std::string::npos)
                {
                    uint32_t lParsedValue = 0;
                    if (!parseUnsignedDecimal(lSub.substr(lArgPos), lParsedValue))
                    {
                        logInfoP("Invalid radio diagnostic argument: %s", lSub.substr(lArgPos).c_str());
                        return true;
                    }
                    if (lIsSoak)
                    {
                        if (lParsedValue < 1)
                            lParsedValue = 1;
                        if (lParsedValue > 300)
                            lParsedValue = 300;
                        lBudgetSeconds = static_cast<uint32_t>(lParsedValue);
                    }
                    else
                    {
                        if (lParsedValue < 1)
                            lParsedValue = 1;
                        if (lParsedValue > 10)
                            lParsedValue = 10;
                        lRounds = static_cast<uint8_t>(lParsedValue);
                    }
                }
            }
            if (lIsSoak)
                startRadioDiagnostic(RadioDiagnosticKind::Soak, static_cast<uint8_t>(lBudgetSeconds));
            else
                startRadioDiagnostic(RadioDiagnosticKind::Sweep, lRounds);
        }
#endif
        return true;
    }

    if (lSub == "proto selftest")
    {
        struct ProtoCheck
        {
            const char *name;
            bool ok;
        };

        constexpr uint32_t kSrcNodeId = 0x123456;
        constexpr uint32_t kDestNodeId = 0x654321;
        constexpr uint16_t kSeq1W = 0x3456;
        const uint8_t kChallenge[6] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65};
        const uint8_t kChallengeResponseData[IOHC_HMAC_SIZE] = {0xA0, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5};
        const uint8_t kSystemKey[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                        0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF0, 0x0F};
        const uint8_t kTransferKey[16] = {0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5, 0x96, 0x87,
                                          0x78, 0x69, 0x5A, 0x4B, 0x3C, 0x2D, 0x1E, 0x0F};
        const uint8_t kFrame2WData[4] = {0x01, 0x67, 0x64, 0x00};
        const uint8_t kEncrypted2WKey[16] = {0xF8, 0x49, 0x58, 0x4F, 0xFC, 0xFC, 0x44, 0x2B,
                                             0x1E, 0x97, 0xE4, 0xC3, 0x8D, 0xF7, 0xB1, 0x43};
        const uint8_t kFrame1WData[8] = {IOHC_ORIGINATOR_USER, IOHC_ACEI_1W, 0x32, 0x00, 0x00, 0x00,
                                         static_cast<uint8_t>((kSeq1W >> 8) & 0xFF), static_cast<uint8_t>(kSeq1W & 0xFF)};
        const uint8_t kNodeAddress[3] = {0x65, 0x43, 0x21};

        ProtoCheck lChecks[] = {
            {"2W serialize/deserialize", false},
            {"2W HMAC verify", false},
            {"1W serialize/deserialize", false},
            {"1W HMAC verify", false},
            {"1W key encrypt/decrypt", false},
            {"0x3D data payload", false},
            {"0x3D exact no CRC", false},
            {"max CRC frame", false},
            {"length mismatch reject", false},
            {"malformed length reject", false},
            {"CRC+HMAC decoded verify", false},
            {"invalid console input", false},
            {"corrupt flash count", false},
            {"CRC tamper reject", false},
            {"HMAC tamper reject", false},
            {"0x32 encrypted key only", false},
            {"0x32 auth via 0x3D", false}};
        uint8_t lOkCount = 0;

        IoHomeFrame lTx2W;
        lTx2W.init();
        lTx2W.setStart2W();
        lTx2W.setFrameOrder(IOHC_CTRL0_ORDER_END);
        lTx2W.setSrcNode(kSrcNodeId);
        lTx2W.setDestNode(kDestNodeId);
        lTx2W.commandId = IoHomeCommand::Execute;
        memcpy(lTx2W.data, kFrame2WData, sizeof(kFrame2WData));
        lTx2W.dataLen = sizeof(kFrame2WData);
        lTx2W.hasHmac = true;
        lTx2W.hasCrc = true;

        uint8_t lHmacInput2W[1 + sizeof(kFrame2WData)] = {static_cast<uint8_t>(IoHomeCommand::Execute), 0, 0, 0, 0};
        memcpy(lHmacInput2W + 1, kFrame2WData, sizeof(kFrame2WData));
        IoHomeCrypto::createHmac2W(lHmacInput2W, sizeof(lHmacInput2W), kChallenge, kSystemKey, lTx2W.hmac);

        uint8_t lBuffer2W[IOHC_FRAME_MAX_SIZE + IOHC_CRC_SIZE] = {0};
        const uint8_t lLen2W = lTx2W.serialize(lBuffer2W, sizeof(lBuffer2W));
        IoHomeFrame lRx2W;
        lRx2W.init();
        const bool lRoundTrip2W = (lLen2W > 0) && lRx2W.deserialize(lBuffer2W, lLen2W) &&
                                  lRx2W.getSrcNodeId() == kSrcNodeId &&
                                  lRx2W.getDestNodeId() == kDestNodeId &&
                                  lRx2W.commandId == IoHomeCommand::Execute &&
                                  lRx2W.dataLen == sizeof(kFrame2WData) &&
                                  memcmp(lRx2W.data, kFrame2WData, sizeof(kFrame2WData)) == 0 &&
                                  lRx2W.hasHmac && lRx2W.hasCrc &&
                                  memcmp(lRx2W.hmac, lTx2W.hmac, IOHC_HMAC_SIZE) == 0;
        lChecks[0].ok = lRoundTrip2W;
        lChecks[1].ok = lRoundTrip2W && IoHomeCrypto::verifyHmac(lHmacInput2W, sizeof(lHmacInput2W), lRx2W.hmac, kChallenge, kSystemKey);

        uint8_t lParsedHmacInput[1 + IOHC_FRAME_MAX_DATA] = {0};
        lParsedHmacInput[0] = static_cast<uint8_t>(lRx2W.commandId);
        if (lRx2W.dataLen > 0)
            memcpy(lParsedHmacInput + 1, lRx2W.data, lRx2W.dataLen);
        const uint8_t lParsedHmacInputLen = 1 + lRx2W.dataLen;
        const uint8_t lLegacyRawLen = (lLen2W > IOHC_HMAC_SIZE + IOHC_CRC_SIZE) ? (lLen2W - IOHC_HMAC_SIZE - IOHC_CRC_SIZE) : 0;
        lChecks[10].ok = lRoundTrip2W &&
                         IoHomeCrypto::verifyHmac(lParsedHmacInput, lParsedHmacInputLen, lRx2W.hmac, kChallenge, kSystemKey) &&
                         lLegacyRawLen > 0 &&
                         !IoHomeCrypto::verifyHmac(lBuffer2W, lLegacyRawLen, lRx2W.hmac, kChallenge, kSystemKey);

        IoHomeFrame lTx1W;
        lTx1W.init();
        lTx1W.setStart2W();
        lTx1W.set1WMode();
        lTx1W.setFrameOrder(IOHC_CTRL0_ORDER_END);
        lTx1W.setSrcNode(kSrcNodeId);
        lTx1W.setDestNode(kDestNodeId);
        lTx1W.commandId = IoHomeCommand::Execute;
        memcpy(lTx1W.data, kFrame1WData, sizeof(kFrame1WData));
        lTx1W.dataLen = sizeof(kFrame1WData);
        lTx1W.hasHmac = true;
        lTx1W.hasCrc = true;

        uint8_t lHmacInput1W[7] = {static_cast<uint8_t>(IoHomeCommand::Execute), IOHC_ORIGINATOR_USER, IOHC_ACEI_1W, 0x32, 0x00, 0x00, 0x00};
        IoHomeCrypto::createHmac1W(lHmacInput1W, sizeof(lHmacInput1W), kSeq1W, kSystemKey, lTx1W.hmac);

        uint8_t lBuffer1W[IOHC_FRAME_MAX_SIZE + IOHC_CRC_SIZE] = {0};
        const uint8_t lLen1W = lTx1W.serialize(lBuffer1W, sizeof(lBuffer1W));
        IoHomeFrame lRx1W;
        lRx1W.init();
        const bool lRoundTrip1W = (lLen1W > 0) && lRx1W.deserialize(lBuffer1W, lLen1W) &&
                                  lRx1W.getSrcNodeId() == kSrcNodeId &&
                                  lRx1W.getDestNodeId() == kDestNodeId &&
                                  lRx1W.commandId == IoHomeCommand::Execute &&
                                  lRx1W.dataLen == sizeof(kFrame1WData) &&
                                  memcmp(lRx1W.data, kFrame1WData, sizeof(kFrame1WData)) == 0 &&
                                  lRx1W.hasHmac && lRx1W.hasCrc &&
                                  memcmp(lRx1W.hmac, lTx1W.hmac, IOHC_HMAC_SIZE) == 0;
        lChecks[2].ok = lRoundTrip1W;
        lChecks[3].ok = lRoundTrip1W && IoHomeCrypto::verifyHmac1W(lHmacInput1W, sizeof(lHmacInput1W), kSeq1W, lRx1W.hmac, kSystemKey);

        uint8_t lEncryptedKey[16] = {0};
        uint8_t lDecryptedKey[16] = {0};
        lChecks[4].ok = IoHomeCrypto::encrypt1WKey(kSystemKey, kTransferKey, kNodeAddress, lEncryptedKey) &&
                        IoHomeCrypto::decrypt1WKey(lEncryptedKey, kTransferKey, kNodeAddress, lDecryptedKey) &&
                        memcmp(lDecryptedKey, kSystemKey, sizeof(kSystemKey)) == 0;

        IoHomeFrame lChallengeResponse;
        lChallengeResponse.init();
        lChallengeResponse.setStart2W();
        lChallengeResponse.setFrameOrder(IOHC_CTRL0_ORDER_END);
        lChallengeResponse.setSrcNode(kSrcNodeId);
        lChallengeResponse.setDestNode(kDestNodeId);
        lChallengeResponse.commandId = IoHomeCommand::ChallengeResponse;
        memcpy(lChallengeResponse.data, kChallengeResponseData, sizeof(kChallengeResponseData));
        lChallengeResponse.dataLen = sizeof(kChallengeResponseData);
        lChallengeResponse.hasHmac = false;
        lChallengeResponse.hasCrc = true;

        uint8_t lChallengeResponseBuffer[IOHC_FRAME_MAX_SIZE + IOHC_CRC_SIZE] = {0};
        const uint8_t lChallengeResponseLen = lChallengeResponse.serialize(lChallengeResponseBuffer, sizeof(lChallengeResponseBuffer));
        IoHomeFrame lParsedChallengeResponse;
        lChecks[5].ok = (lChallengeResponseLen > 0) &&
                        lParsedChallengeResponse.deserialize(lChallengeResponseBuffer, lChallengeResponseLen) &&
                        lParsedChallengeResponse.commandId == IoHomeCommand::ChallengeResponse &&
                        lParsedChallengeResponse.dataLen == sizeof(kChallengeResponseData) &&
                        !lParsedChallengeResponse.hasHmac && lParsedChallengeResponse.hasCrc &&
                        memcmp(lParsedChallengeResponse.data, kChallengeResponseData, sizeof(kChallengeResponseData)) == 0;

        lChallengeResponse.hasCrc = false;
        uint8_t lChallengeResponseNoCrcBuffer[IOHC_FRAME_MAX_SIZE] = {0};
        const uint8_t lChallengeResponseNoCrcLen = lChallengeResponse.serialize(lChallengeResponseNoCrcBuffer, sizeof(lChallengeResponseNoCrcBuffer));
        IoHomeFrame lParsedChallengeResponseNoCrc;
        lChecks[6].ok = (lChallengeResponseNoCrcLen == IOHC_FRAME_MIN_SIZE + IOHC_HMAC_SIZE) &&
                        lParsedChallengeResponseNoCrc.deserialize(lChallengeResponseNoCrcBuffer, lChallengeResponseNoCrcLen) &&
                        lParsedChallengeResponseNoCrc.commandId == IoHomeCommand::ChallengeResponse &&
                        lParsedChallengeResponseNoCrc.dataLen == sizeof(kChallengeResponseData) &&
                        !lParsedChallengeResponseNoCrc.hasHmac && !lParsedChallengeResponseNoCrc.hasCrc &&
                        memcmp(lParsedChallengeResponseNoCrc.data, kChallengeResponseData, sizeof(kChallengeResponseData)) == 0;

        IoHomeFrame lMaxCrc;
        lMaxCrc.init();
        lMaxCrc.setStart2W();
        lMaxCrc.setFrameOrder(IOHC_CTRL0_ORDER_END);
        lMaxCrc.setSrcNode(kSrcNodeId);
        lMaxCrc.setDestNode(kDestNodeId);
        lMaxCrc.commandId = IoHomeCommand::Private;
        for (uint8_t i = 0; i < IOHC_FRAME_MAX_DATA; i++)
            lMaxCrc.data[i] = i;
        lMaxCrc.dataLen = IOHC_FRAME_MAX_DATA;
        lMaxCrc.hasCrc = true;

        uint8_t lMaxCrcBuffer[IOHC_FRAME_MAX_SIZE + IOHC_CRC_SIZE] = {0};
        const uint8_t lMaxCrcLen = lMaxCrc.serialize(lMaxCrcBuffer, sizeof(lMaxCrcBuffer));
        IoHomeFrame lParsedMaxCrc;
        lChecks[7].ok = (lMaxCrcLen == IOHC_FRAME_MIN_SIZE + IOHC_FRAME_MAX_DATA + IOHC_CRC_SIZE) &&
                        lParsedMaxCrc.deserialize(lMaxCrcBuffer, lMaxCrcLen) &&
                        lParsedMaxCrc.commandId == IoHomeCommand::Private &&
                        lParsedMaxCrc.dataLen == IOHC_FRAME_MAX_DATA &&
                        lParsedMaxCrc.hasCrc &&
                        memcmp(lParsedMaxCrc.data, lMaxCrc.data, IOHC_FRAME_MAX_DATA) == 0;

        IoHomeFrame lWrongLengthFrame;
        lChecks[8].ok = (lLen2W > IOHC_CRC_SIZE) && !lWrongLengthFrame.deserialize(lBuffer2W, lLen2W - 1);

        uint8_t lMalformedDeclaredLen[sizeof(lBuffer2W)] = {0};
        memcpy(lMalformedDeclaredLen, lBuffer2W, lLen2W);
        lMalformedDeclaredLen[0] = (lMalformedDeclaredLen[0] & ~IOHC_CTRL0_LEN_MASK);
        IoHomeFrame lMalformedDeclaredFrame;
        lChecks[9].ok = (lLen2W > 0) && !lMalformedDeclaredFrame.deserialize(lMalformedDeclaredLen, lLen2W);

        uint32_t lParsedValue = 0;
        uint8_t lParsedChannel = 0;
        lChecks[11].ok = parseUnsignedDecimal(" 42 ", lParsedValue) && lParsedValue == 42 &&
                         !parseUnsignedDecimal("", lParsedValue) &&
                         !parseUnsignedDecimal("12x", lParsedValue) &&
                         !parseUnsignedDecimal("4294967296", lParsedValue) &&
                         parseChannelIndex("2", 2, lParsedChannel) && lParsedChannel == 1 &&
                         !parseChannelIndex("0", 2, lParsedChannel) &&
                         !parseChannelIndex("3", 2, lParsedChannel) &&
                         parseHex24("0x123456", lParsedValue) && lParsedValue == 0x123456 &&
                         !parseHex24("0x1000000", lParsedValue) &&
                         !parseHex24("12zz", lParsedValue);

        constexpr uint16_t kFlashSelftestHeader = 18;
        constexpr uint16_t kFlashSelftestRecord = 24;
        lChecks[12].ok = clampFlashRecordCount(255, 8, kFlashSelftestHeader + 2 * kFlashSelftestRecord, kFlashSelftestHeader, kFlashSelftestRecord) == 2 &&
                         clampFlashRecordCount(4, 1, kFlashSelftestHeader + 4 * kFlashSelftestRecord, kFlashSelftestHeader, kFlashSelftestRecord) == 1 &&
                         clampFlashRecordCount(4, 8, kFlashSelftestHeader - 1, kFlashSelftestHeader, kFlashSelftestRecord) == 0 &&
                         clampFlashRecordCount(4, 8, kFlashSelftestHeader, kFlashSelftestHeader, 0) == 0;

        uint8_t lCorruptCrc[sizeof(lBuffer2W)] = {0};
        memcpy(lCorruptCrc, lBuffer2W, lLen2W);
        if (lLen2W >= 2)
            lCorruptCrc[lLen2W - 1] ^= 0x01;
        IoHomeFrame lBadCrcFrame;
        lChecks[13].ok = (lLen2W >= 2) && !lBadCrcFrame.deserialize(lCorruptCrc, lLen2W);

        uint8_t lBadHmac[IOHC_HMAC_SIZE] = {0};
        memcpy(lBadHmac, lTx2W.hmac, IOHC_HMAC_SIZE);
        lBadHmac[0] ^= 0x80;
        lChecks[14].ok = !IoHomeCrypto::verifyHmac(lHmacInput2W, sizeof(lHmacInput2W), lBadHmac, kChallenge, kSystemKey);

        IoHomeFrame lKeyTransfer;
        lKeyTransfer.init();
        lKeyTransfer.ctrlByte0 = 0;
        lKeyTransfer.ctrlByte1 = 0x00;
        lKeyTransfer.setSrcNode(kSrcNodeId);
        lKeyTransfer.setDestNode(kDestNodeId);
        lKeyTransfer.commandId = IoHomeCommand::KeyTransfer;
        memcpy(lKeyTransfer.data, kEncrypted2WKey, sizeof(kEncrypted2WKey));
        lKeyTransfer.dataLen = sizeof(kEncrypted2WKey);
        lKeyTransfer.hasHmac = false;
        lKeyTransfer.hasCrc = true;

        uint8_t lKeyTransferBuffer[IOHC_FRAME_MAX_SIZE + IOHC_CRC_SIZE] = {0};
        const uint8_t lKeyTransferLen = lKeyTransfer.serialize(lKeyTransferBuffer, sizeof(lKeyTransferBuffer));
        IoHomeFrame lParsedKeyTransfer;
        lChecks[15].ok = (lKeyTransferLen == IOHC_FRAME_MIN_SIZE + sizeof(kEncrypted2WKey) + IOHC_CRC_SIZE) &&
                         lParsedKeyTransfer.deserialize(lKeyTransferBuffer, lKeyTransferLen) &&
                         lParsedKeyTransfer.commandId == IoHomeCommand::KeyTransfer &&
                         lParsedKeyTransfer.dataLen == sizeof(kEncrypted2WKey) &&
                         !lParsedKeyTransfer.hasHmac && lParsedKeyTransfer.hasCrc &&
                         memcmp(lParsedKeyTransfer.data, kEncrypted2WKey, sizeof(kEncrypted2WKey)) == 0;

        uint8_t lKeyTransferHmacInput[1 + sizeof(kEncrypted2WKey)] = {static_cast<uint8_t>(IoHomeCommand::KeyTransfer)};
        memcpy(lKeyTransferHmacInput + 1, kEncrypted2WKey, sizeof(kEncrypted2WKey));
        IoHomeFrame lKeyTransferAuth;
        lKeyTransferAuth.init();
        lKeyTransferAuth.ctrlByte0 = 0;
        lKeyTransferAuth.ctrlByte1 = 0x00;
        lKeyTransferAuth.setSrcNode(kSrcNodeId);
        lKeyTransferAuth.setDestNode(kDestNodeId);
        lKeyTransferAuth.commandId = IoHomeCommand::ChallengeResponse;
        const bool lKeyTransferHmacOk = IoHomeCrypto::createHmac2W(lKeyTransferHmacInput, sizeof(lKeyTransferHmacInput),
                                                                   kChallenge, kSystemKey, lKeyTransferAuth.data);
        lKeyTransferAuth.dataLen = IOHC_HMAC_SIZE;
        lKeyTransferAuth.hasHmac = false;
        lKeyTransferAuth.hasCrc = true;

        uint8_t lKeyTransferAuthBuffer[IOHC_FRAME_MAX_SIZE + IOHC_CRC_SIZE] = {0};
        const uint8_t lKeyTransferAuthLen = lKeyTransferAuth.serialize(lKeyTransferAuthBuffer, sizeof(lKeyTransferAuthBuffer));
        IoHomeFrame lParsedKeyTransferAuth;
        lChecks[16].ok = lChecks[15].ok && lKeyTransferHmacOk &&
                         (lKeyTransferAuthLen == IOHC_FRAME_MIN_SIZE + IOHC_HMAC_SIZE + IOHC_CRC_SIZE) &&
                         lParsedKeyTransferAuth.deserialize(lKeyTransferAuthBuffer, lKeyTransferAuthLen) &&
                         lParsedKeyTransferAuth.commandId == IoHomeCommand::ChallengeResponse &&
                         lParsedKeyTransferAuth.dataLen == IOHC_HMAC_SIZE &&
                         !lParsedKeyTransferAuth.hasHmac && lParsedKeyTransferAuth.hasCrc &&
                         memcmp(lParsedKeyTransferAuth.data, lKeyTransferAuth.data, IOHC_HMAC_SIZE) == 0;

        for (const auto &lCheck : lChecks)
        {
            if (lCheck.ok)
                lOkCount++;
            logInfoP("ProtoSelfTest: %-24s %s", lCheck.name, lCheck.ok ? "OK" : "FAIL");
        }
        logInfoP("ProtoSelfTest: %u/%u checks passed", static_cast<unsigned int>(lOkCount), static_cast<unsigned int>(sizeof(lChecks) / sizeof(lChecks[0])));
        return true;
    }

    // === Gateway mode commands ===
    if (lSub == "gateway on")
    {
        // Enable fake gateway mode with a default node ID
        if (mController.getGatewayNodeId() == 0)
            mController.setGatewayNodeId(0x112233);
        if (mController.getGatewayKey()[0] == 0)
        {
            // Generate a default random stack key
            uint8_t lDefaultKey[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
                                       0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
            mController.setGatewayKey(lDefaultKey);
        }
        mController.setGatewayMode(true);
        return true;
    }
    if (lSub == "gateway off")
    {
        mController.setGatewayMode(false);
        return true;
    }
    if (lSub == "gateway status")
    {
        const uint8_t lPairedDeviceCount = mController.getGatewayPairedDeviceCount();
        logInfoP("Gateway mode: %s", mController.isGatewayMode() ? "ON" : "OFF");
        logInfoP("Gateway node: 0x%06X", mController.getGatewayNodeId());
        logInfoP("Paired devices: %u", static_cast<unsigned int>(lPairedDeviceCount));
        if (lPairedDeviceCount > 0)
        {
            for (uint8_t i = 0; i < lPairedDeviceCount; i++)
            {
                const uint32_t lNodeId = mController.getGatewayPairedNodeId(i);
                logInfoP("  Device %u: 0x%06X", static_cast<unsigned int>(i + 1), lNodeId);
            }
        }
        return true;
    }
    if (lSub.substr(0, 10) == "gateway node")
    {
        std::string lArg = lSub.substr(10);
        // trim spaces
        size_t lPos = 0;
        while (lPos < lArg.length() && isSpace(lArg[lPos]))
            lPos++;
        if (lPos > 0)
            lArg = lArg.substr(lPos);
        if (lArg.empty())
        {
            openknx.console.printHelpLine("iohc gateway node ADDR", "Set gateway node ID (hex, e.g. AABBCC)");
            return true;
        }
        uint8_t lNodeBytes[3] = {};
        if (parseHexBytes(lArg, lNodeBytes, 3))
        {
            const uint32_t lNodeId = (static_cast<uint32_t>(lNodeBytes[0]) << 16) |
                                     (static_cast<uint32_t>(lNodeBytes[1]) << 8) |
                                     static_cast<uint32_t>(lNodeBytes[2]);
            mController.setGatewayNodeId(lNodeId);
            logInfoP("Gateway node ID set to 0x%06X", lNodeId);
        }
        else
        {
            logInfoP("Invalid hex address. Use 6 hex digits (e.g. AABBCC)");
        }
        return true;
    }
    if (lSub.substr(0, 9) == "gateway key")
    {
        std::string lArg = lSub.substr(9);
        size_t lPos = 0;
        while (lPos < lArg.length() && isSpace(lArg[lPos]))
            lPos++;
        if (lPos > 0)
            lArg = lArg.substr(lPos);
        if (lArg.empty())
        {
            openknx.console.printHelpLine("iohc gateway key HEX32", "Set gateway stack key (32 hex chars)");
            return true;
        }
        uint8_t lKey[16] = {};
        if (parseHexBytes(lArg, lKey, 16))
        {
            mController.setGatewayKey(lKey);
            logInfoP("Gateway stack key set (first 4 bytes: %02X%02X%02X%02X)",
                     lKey[0], lKey[1], lKey[2], lKey[3]);
        }
        else
        {
            logInfoP("Invalid hex key. Use 32 hex chars (16 bytes)");
        }
        return true;
    }
    if (lSub == "gateway clear")
    {
        mController.clearGatewayPairedDevices();
        return true;
    }

    if (lSub.substr(0, 5) == "radio")
    {
        const auto lHealth = mController.radioHealth();
        const int lInstantRssi = lHealth.currentRssiValid ? lHealth.currentRssi : 0;
        const char *lRadioState = "Unknown";
        switch (lHealth.radioState)
        {
        case RadioState::Idle:
            lRadioState = "Idle";
            break;
        case RadioState::Transmitting:
            lRadioState = "Tx";
            break;
        case RadioState::Receiving:
            lRadioState = "Rx";
            break;
        case RadioState::Sleep:
            lRadioState = "Sleep";
            break;
        }

#if defined(RADIO_SX1262)
        char lInitDevText[96];
        char lDevErrText[96];
        logInfoP("Radio: init=%d radio=%s ctrl=%d rxScan=%d passive=%d scan=%d preamble=%d q=%d duty=%u.%u%% lastRssi=%ddBm instRssiOk=%d instRssi=%ddBm freq=%d lastResp=%d initErr=%d status=0x%02X cmdStat=%d initDev=0x%04X[%s] devErr=0x%04X[%s] tcxo=%luus/%u rf=0x%02X busyTO=%d txS=%lu txD=%lu rxS=%lu irq=%lu poll=%lu pre=%lu preOnly=%lu sync=%lu rxD=%lu rxReadFail=%lu crc=%lu to=%lu lastIrq=0x%04X opB=0x%02X opA=0x%02X txSt=0x%02X txIrq=0x%04X",
                 lHealth.initialized ? 1 : 0,
                 lRadioState,
                 static_cast<int>(lHealth.controllerState),
                 lHealth.rxScanEnabled ? 1 : 0,
                 lHealth.passiveMode ? 1 : 0,
                 lHealth.networkScanActive ? 1 : 0,
                 lHealth.preambleDetected ? 1 : 0,
                 lHealth.queueDepth,
                 lHealth.dutyPermille / 10,
                 lHealth.dutyPermille % 10,
                 lHealth.lastRssi,
                 lHealth.currentRssiValid ? 1 : 0,
                 lInstantRssi,
                 lHealth.currentFreqIdx,
                 lHealth.lastResponseFreqIdx,
                 lHealth.initError,
                 lHealth.initStatusByte,
                 lHealth.initCommandStatus,
                 lHealth.initDeviceErrors,
                 describeSX1262DeviceErrors(lHealth.initDeviceErrors, lInitDevText, sizeof(lInitDevText)),
                 lHealth.lastDeviceErrors,
                 describeSX1262DeviceErrors(lHealth.lastDeviceErrors, lDevErrText, sizeof(lDevErrText)),
                 static_cast<unsigned long>(lHealth.tcxoStartupDelayUs),
                 lHealth.tcxoStartupAttempts,
                 lHealth.rfSwitchConfig,
                 lHealth.busyTimedOut ? 1 : 0,
                 static_cast<unsigned long>(lHealth.txStartCount),
                 static_cast<unsigned long>(lHealth.txDoneCount),
                 static_cast<unsigned long>(lHealth.rxStartCount),
                 static_cast<unsigned long>(lHealth.irqCount),
                 static_cast<unsigned long>(lHealth.irqPollHitCount),
                 static_cast<unsigned long>(lHealth.preambleIrqCount),
                 static_cast<unsigned long>(lHealth.preambleOnlyIrqCount),
                 static_cast<unsigned long>(lHealth.syncWordIrqCount),
                 static_cast<unsigned long>(lHealth.rxDoneCount),
                 static_cast<unsigned long>(lHealth.rxReadFailCount),
                 static_cast<unsigned long>(lHealth.crcErrorCount),
                 static_cast<unsigned long>(lHealth.timeoutCount),
                 lHealth.lastIrqStatus,
                 lHealth.lastOpStatusBefore,
                 lHealth.lastOpStatusAfter,
                 lHealth.lastTxSetStatus,
                 lHealth.lastTxIrqImmediate);
#else
        logInfoP("Radio: init=%d radio=%s ctrl=%d rxScan=%d passive=%d scan=%d preamble=%d q=%d duty=%u.%u%% lastRssi=%ddBm instRssiOk=%d instRssi=%ddBm freq=%d lastResp=%d initErr=%d status=0x%02X cmdStat=%d initDev=0x%04X devErr=0x%04X tcxo=%luus/%u rf=0x%02X busyTO=%d txS=%lu txD=%lu rxS=%lu irq=%lu poll=%lu pre=%lu preOnly=%lu sync=%lu rxD=%lu rxReadFail=%lu crc=%lu to=%lu lastIrq=0x%04X opB=0x%02X opA=0x%02X txSt=0x%02X txIrq=0x%04X",
                 lHealth.initialized ? 1 : 0,
                 lRadioState,
                 static_cast<int>(lHealth.controllerState),
                 lHealth.rxScanEnabled ? 1 : 0,
                 lHealth.passiveMode ? 1 : 0,
                 lHealth.networkScanActive ? 1 : 0,
                 lHealth.preambleDetected ? 1 : 0,
                 lHealth.queueDepth,
                 lHealth.dutyPermille / 10,
                 lHealth.dutyPermille % 10,
                 lHealth.lastRssi,
                 lHealth.currentRssiValid ? 1 : 0,
                 lInstantRssi,
                 lHealth.currentFreqIdx,
                 lHealth.lastResponseFreqIdx,
                 lHealth.initError,
                 lHealth.initStatusByte,
                 lHealth.initCommandStatus,
                 lHealth.initDeviceErrors,
                 lHealth.lastDeviceErrors,
                 static_cast<unsigned long>(lHealth.tcxoStartupDelayUs),
                 lHealth.tcxoStartupAttempts,
                 lHealth.rfSwitchConfig,
                 lHealth.busyTimedOut ? 1 : 0,
                 static_cast<unsigned long>(lHealth.txStartCount),
                 static_cast<unsigned long>(lHealth.txDoneCount),
                 static_cast<unsigned long>(lHealth.rxStartCount),
                 static_cast<unsigned long>(lHealth.irqCount),
                 static_cast<unsigned long>(lHealth.irqPollHitCount),
                 static_cast<unsigned long>(lHealth.preambleIrqCount),
                 static_cast<unsigned long>(lHealth.preambleOnlyIrqCount),
                 static_cast<unsigned long>(lHealth.syncWordIrqCount),
                 static_cast<unsigned long>(lHealth.rxDoneCount),
                 static_cast<unsigned long>(lHealth.rxReadFailCount),
                 static_cast<unsigned long>(lHealth.crcErrorCount),
                 static_cast<unsigned long>(lHealth.timeoutCount),
                 lHealth.lastIrqStatus,
                 lHealth.lastOpStatusBefore,
                 lHealth.lastOpStatusAfter,
                 lHealth.lastTxSetStatus,
                 lHealth.lastTxIrqImmediate);
#endif

        if (iDebugKo)
            openknx.console.writeDiagnoseKo("R %d %d %d %d %d",
                                            lHealth.initialized ? 1 : 0,
                                            lHealth.initError,
                                            lHealth.initStatusByte,
                                            lHealth.initCommandStatus,
                                            lHealth.busyTimedOut ? 1 : 0);
        return true;
    }

    return false;
}

bool IoHomecontrol::handleRadioRaw(bool iDebugKo)
{
#if defined(RADIO_SX1262)
    const uint8_t lCrcInitMsb = mController.radio().debugReadRegister(SX1262_REG_CRC_INIT_MSB);
    const uint8_t lCrcInitLsb = mController.radio().debugReadRegister(SX1262_REG_CRC_INIT_LSB);
    const uint8_t lCrcPolyMsb = mController.radio().debugReadRegister(SX1262_REG_CRC_POLY_MSB);
    const uint8_t lCrcPolyLsb = mController.radio().debugReadRegister(SX1262_REG_CRC_POLY_LSB);
    const uint8_t lRxGain = mController.radio().debugReadRegister(SX1262_REG_RX_GAIN);
    const uint8_t lSync0 = mController.radio().debugReadRegister(SX1262_REG_SYNC_WORD_0);
    const uint8_t lSync1 = mController.radio().debugReadRegister(SX1262_REG_SYNC_WORD_1);
    const uint8_t lSync2 = mController.radio().debugReadRegister(SX1262_REG_SYNC_WORD_2);
    const uint16_t lDevErr = mController.radio().debugReadDeviceErrors();
    const auto lHealth = mController.radioHealth();
    if (iDebugKo)
        openknx.console.writeDiagnoseKo("RadioRaw: crcInit=%02X%02X crcPoly=%02X%02X rxGain=%02X sync=%02X%02X%02X devErr=0x%04X initDev=0x%04X tcxo=%luus/%u rf=0x%02X",
                                        lCrcInitMsb, lCrcInitLsb, lCrcPolyMsb, lCrcPolyLsb, lRxGain, lSync0, lSync1, lSync2,
                                        lDevErr,
                                        lHealth.initDeviceErrors,
                                        static_cast<unsigned long>(lHealth.tcxoStartupDelayUs),
                                        lHealth.tcxoStartupAttempts,
                                        lHealth.rfSwitchConfig);
    else
        logInfoP("RadioRaw: crcInit=%02X%02X crcPoly=%02X%02X rxGain=%02X sync=%02X%02X%02X devErr=0x%04X initDev=0x%04X tcxo=%luus/%u rf=0x%02X",
                 lCrcInitMsb, lCrcInitLsb, lCrcPolyMsb, lCrcPolyLsb, lRxGain, lSync0, lSync1, lSync2,
                 lDevErr,
                 lHealth.initDeviceErrors,
                 static_cast<unsigned long>(lHealth.tcxoStartupDelayUs),
                 lHealth.tcxoStartupAttempts,
                 lHealth.rfSwitchConfig);
    return true;
#elif defined(RADIO_SX1276)
    const uint8_t lVersion = mController.radio().debugReadRegister(REG_VERSION);
    const uint8_t lOpMode = mController.radio().debugReadRegister(REG_OPMODE);
    const uint8_t lFrfMsb = mController.radio().debugReadRegister(REG_FRFMSB);
    const uint8_t lFrfMid = mController.radio().debugReadRegister(REG_FRFMID);
    const uint8_t lFrfLsb = mController.radio().debugReadRegister(REG_FRFLSB);
    const uint8_t lRssiRaw = mController.radio().debugReadRegister(REG_RSSIVALUE);
    const uint8_t lIrq1 = mController.radio().debugReadRegister(REG_IRQFLAGS1);
    const uint8_t lIrq2 = mController.radio().debugReadRegister(REG_IRQFLAGS2);
    if (iDebugKo)
        openknx.console.writeDiagnoseKo("RadioRaw: ver=0x%02X op=0x%02X frf=%02X%02X%02X rssiRaw=0x%02X irq1=0x%02X irq2=0x%02X",
                                        lVersion, lOpMode, lFrfMsb, lFrfMid, lFrfLsb, lRssiRaw, lIrq1, lIrq2);
    else
        logInfoP("RadioRaw: ver=0x%02X op=0x%02X frf=%02X%02X%02X rssiRaw=0x%02X irq1=0x%02X irq2=0x%02X",
                 lVersion, lOpMode, lFrfMsb, lFrfMid, lFrfLsb, lRssiRaw, lIrq1, lIrq2);
    return true;
#else
    if (iDebugKo)
        openknx.console.writeDiagnoseKo("RadioRaw: only available for SX1262/SX1276 builds");
    else
        logInfoP("RadioRaw: only available for SX1262/SX1276 builds");
    return false;
#endif
}

void IoHomecontrol::savePower()
{
    mController.sleep();
}

bool IoHomecontrol::restorePower()
{
    mController.init();
    mController.startReceive();
    return true;
}
