#include "RadioSX1262.h"
#include "SX1262IoHomePhy.h"
#include "sx1262Regs-Fsk.h"
#include "../protocol/IoHomeCommands.h"
#include "../protocol/IoHomeFrame.h"
#include "HardwareConfig.h"
#include "OpenKNX.h"

#ifdef ESP32
#include <SPI.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
static SPISettings sSx1262SpiSettings(2000000, MSBFIRST, SPI_MODE0);
#else
// Stubs for non-ESP32 compilation
#include <stdint.h>
#include <string.h>
static void pinMode(uint8_t, uint8_t) {}
static void digitalWrite(uint8_t, uint8_t) {}
static int digitalRead(uint8_t) { return 0; }
static void delay(unsigned long) {}
static void delayMicroseconds(unsigned int) {}
static unsigned long millis() { return 0; }
static unsigned long micros() { return 0; }
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
#endif

// BUSY pin timeout (ms)
#define SX1262_BUSY_TIMEOUT_MS 100
#define SX1262_MUTEX_TIMEOUT_MS 50
#define SX1262_DIO1_REQUEUE_DELAY_MS 2

static constexpr uint16_t kCachedIrqMask = SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_DONE |
                                           SX1262_IRQ_SYNC_WORD_VALID | SX1262_IRQ_TIMEOUT |
                                           SX1262_IRQ_CRC_ERR;

static constexpr uint8_t kMaxSx1262PayloadLen = IOHC_FRAME_BUFFER_SIZE + IOHC_CRC_SIZE;
static constexpr uint8_t kSx1262RxBufferBase = 0x80;
static constexpr uint32_t kIoHomeLineRateBps = 38400UL;
static constexpr uint32_t kTxToRxSettleUs = 500UL;
static constexpr uint32_t kRxDiscardLogIntervalMs = 1000UL;
static constexpr size_t kRxDiscardDumpLen = 24;

#if defined(IOHC_RADIO_TCXO_VOLTAGE) && !defined(IOHC_RADIO_TCXO_DELAY_US)
#define IOHC_RADIO_TCXO_DELAY_US 5000UL
#endif

#ifndef IOHC_RADIO_DIO2_RF_SW
#define IOHC_RADIO_DIO2_RF_SW 0
#endif

#ifndef IOHC_RADIO_RF_SW
#define IOHC_RADIO_RF_SW RADIO_PIN_NOT_CONNECTED
#endif

#ifndef IOHC_RADIO_RX_EN
#define IOHC_RADIO_RX_EN RADIO_PIN_NOT_CONNECTED
#endif

#ifndef IOHC_RADIO_TX_EN
#define IOHC_RADIO_TX_EN RADIO_PIN_NOT_CONNECTED
#endif

namespace
{
    std::string logPrefix()
    {
        return openknx.logger.buildPrefix("RadioSX1262", 0);
    }

    void logDiscardedSoftwareCapture(uint8_t iPayloadLen, uint8_t iReadLen, uint8_t iStartOffset, int16_t iRssi, const uint8_t *iRawBuf)
    {
        static uint32_t sSuppressedCount = 0;
        static unsigned long sLastLogAt = 0;

        const unsigned long lNow = millis();
        if (sLastLogAt != 0 && static_cast<unsigned long>(lNow - sLastLogAt) < kRxDiscardLogIntervalMs)
        {
            sSuppressedCount++;
            return;
        }

        logDebugP("Discarded software-PHY RX capture: payload=%u read=%u start=0x%02X rssi=%ddBm suppressed=%lu",
                  static_cast<unsigned int>(iPayloadLen),
                  static_cast<unsigned int>(iReadLen),
                  static_cast<unsigned int>(iStartOffset),
                  static_cast<int>(iRssi),
                  static_cast<unsigned long>(sSuppressedCount));

        size_t lDumpLen = iReadLen;
        if (lDumpLen > kRxDiscardDumpLen)
            lDumpLen = kRxDiscardDumpLen;
        if (lDumpLen > 0)
            logHexDebugP(iRawBuf, lDumpLen);

        sLastLogAt = lNow;
        sSuppressedCount = 0;
    }
}

RadioSX1262::RadioSX1262()
    : mCsPin(0), mResetPin(0), mDio1Pin(0), mBusyPin(0), mRfSwitchPin(IOHC_RADIO_RF_SW),
      mRfSwitchRxPin(IOHC_RADIO_RX_EN), mRfSwitchTxPin(IOHC_RADIO_TX_EN), mUseDio2RfSwitch(IOHC_RADIO_DIO2_RF_SW != 0),
      mInitialized(false), mInitError(RadioSX1262InitError::None), mInitStatusByte(0), mInitDeviceErrors(0), mLastDeviceErrors(0), mTcxoStartupDelayUs(0), mTcxoStartupAttempts(0), mBusyTimedOut(false), mReceiveRestartPending(false), mState(RadioState::Idle),
      mLastRssi(0), mCurrentFreq(0), mStandbyMode(SX1262_STDBY_RC), mPreambleLength(8),
      mSyncWord{}, mSyncWordBits(0), mPacketPayloadLen(SX1262_IOHOME_RX_FIXED_LEN), mSoftwarePhyMode(false), mEms2Mode(false),
      mTxToRxSettlePending(false), mSoftwarePhySyncAtUs(0), mEarlyRxFrame{}, mEarlyRxFrameLen(0),
#ifdef ESP32
      mChipMutex(nullptr), mChipMutexBuffer{}, mDio1EventQueue(nullptr), mDio1EventQueueBuffer{}, mDio1EventQueueStorage{}, mDio1TaskHandle(nullptr),
#endif
      mIrqFired(false), mPreambleFlag(false), mSyncFlag(false), mPendingIrqStickyMask(0), mPendingIrqQueue{}, mPendingIrqHead(0), mPendingIrqTail(0), mPendingIrqOverflowCount(0),
      mTxStartCount(0), mTxDoneCount(0), mRxStartCount(0), mIrqCount(0),
      mPreambleIrqCount(0), mSyncWordIrqCount(0), mRxDoneCount(0), mCrcErrorCount(0),
      mTimeoutCount(0), mIrqPollHitCount(0), mPreambleOnlyIrqCount(0), mRxReadFailCount(0), mTxBusyHighHitCount(0), mTxBusyHighTotalUs(0), mTxBusyHighMaxUs(0), mTxBusyHighStartUs(0), mTxBusyTraceActive(false), mTxBusyTraceOp(TxBusyTraceOp::None), mTxBusyTrace{}, mLastIrqStatus(0), mLastOpStatusBefore(0), mLastOpStatusAfter(0),
      mLastTxSetStatus(0), mLastTxIrqImmediate(0), mTxCompletionPhase(TxCompletionPhase::WaitingForIrq)
{
}

#ifdef ESP32
void IRAM_ATTR RadioSX1262::dio1Isr(void *arg)
{
    RadioSX1262 *self = static_cast<RadioSX1262 *>(arg);
    self->mIrqFired = true;
    if (self->mDio1EventQueue != nullptr)
    {
        const uint32_t lEvent = self->mDio1Pin;
        BaseType_t lHigherPriorityTaskWoken = pdFALSE;
        if (xQueueSendFromISR(self->mDio1EventQueue, &lEvent, &lHigherPriorityTaskWoken) == pdPASS)
        {
            if (lHigherPriorityTaskWoken == pdTRUE)
                portYIELD_FROM_ISR();
        }
    }
}

void RadioSX1262::dio1Task(void *arg)
{
    RadioSX1262 *lSelf = static_cast<RadioSX1262 *>(arg);
    if (lSelf == nullptr || lSelf->mDio1EventQueue == nullptr)
        vTaskDelete(nullptr);

    uint32_t lEvent = 0;
    for (;;)
    {
        if (xQueueReceive(lSelf->mDio1EventQueue, &lEvent, portMAX_DELAY) == pdTRUE)
            lSelf->handleQueuedDio1();
    }
}
#endif

void RadioSX1262::init(uint8_t iCsPin, uint8_t iResetPin, uint8_t iDio1Pin, uint8_t iBusyPin)
{
    mCsPin = iCsPin;
    mResetPin = iResetPin;
    mDio1Pin = iDio1Pin;
    mBusyPin = iBusyPin;
    mInitialized = false;
    mInitError = RadioSX1262InitError::None;
    mInitStatusByte = 0;
    mInitDeviceErrors = 0;
    mLastDeviceErrors = 0;
    mTcxoStartupDelayUs = 0;
    mTcxoStartupAttempts = 0;
    mBusyTimedOut = false;
    mReceiveRestartPending = false;
    mStandbyMode = SX1262_STDBY_RC;
    memset(mSyncWord, 0, sizeof(mSyncWord));
    mSyncWordBits = 0;
    mPacketPayloadLen = SX1262_IOHOME_RX_FIXED_LEN;
    mSoftwarePhyMode = false;
    mTxToRxSettlePending = false;
    mSoftwarePhySyncAtUs = 0;
    memset(mEarlyRxFrame, 0, sizeof(mEarlyRxFrame));
    mEarlyRxFrameLen = 0;
    mPendingIrqStickyMask = 0;
    memset((void *)mPendingIrqQueue, 0, sizeof(mPendingIrqQueue));
    mPendingIrqHead = 0;
    mPendingIrqTail = 0;
    mPendingIrqOverflowCount = 0;
    mTxStartCount = 0;
    mTxDoneCount = 0;
    mRxStartCount = 0;
    mIrqCount = 0;
    mPreambleIrqCount = 0;
    mSyncWordIrqCount = 0;
    mRxDoneCount = 0;
    mCrcErrorCount = 0;
    mTimeoutCount = 0;
    mIrqPollHitCount = 0;
    mPreambleOnlyIrqCount = 0;
    mRxReadFailCount = 0;
    mTxBusyHighHitCount = 0;
    mTxBusyHighTotalUs = 0;
    mTxBusyHighMaxUs = 0;
    mTxBusyHighStartUs = 0;
    mTxBusyTraceActive = false;
    mTxBusyTraceOp = TxBusyTraceOp::None;
    mTxBusyTrace = {};
    mLastIrqStatus = 0;
    mLastOpStatusBefore = 0;
    mLastOpStatusAfter = 0;
    mLastTxSetStatus = 0;
    mLastTxIrqImmediate = 0;
    mTxCompletionPhase = TxCompletionPhase::WaitingForIrq;

#ifdef ESP32
    // Configure pins
    pinMode(mCsPin, OUTPUT);
    digitalWrite(mCsPin, HIGH);
    pinMode(mResetPin, OUTPUT);
    pinMode(mDio1Pin, INPUT);
    pinMode(mBusyPin, INPUT);
    if (mRfSwitchPin != RADIO_PIN_NOT_CONNECTED)
        pinMode(mRfSwitchPin, OUTPUT);
    if (mRfSwitchRxPin != RADIO_PIN_NOT_CONNECTED)
        pinMode(mRfSwitchRxPin, OUTPUT);
    if (mRfSwitchTxPin != RADIO_PIN_NOT_CONNECTED)
        pinMode(mRfSwitchTxPin, OUTPUT);
    setRfSwitchIdle();

    // Initialize SPI with board-specific pins
    SPI.begin(IOHC_SPI_SCK, IOHC_SPI_MISO, IOHC_SPI_MOSI, mCsPin);

    // Attach DIO1 interrupt for TX/RX/preamble detection
    mChipMutex = xSemaphoreCreateRecursiveMutexStatic(&mChipMutexBuffer);
    mDio1EventQueue = xQueueCreateStatic(kDio1EventQueueLen,
                                         sizeof(uint32_t),
                                         mDio1EventQueueStorage,
                                         &mDio1EventQueueBuffer);
    if (mDio1EventQueue != nullptr)
        xTaskCreate(dio1Task, "sx1262-dio1", 4096, this, 10, &mDio1TaskHandle);
    attachInterruptArg(digitalPinToInterrupt(mDio1Pin), dio1Isr, this, RISING);
#endif

    // Hardware reset
    resetChip();
    if (mBusyTimedOut)
    {
        mInitError = RadioSX1262InitError::BusyTimeout;
        return;
    }

    // After reset, chip is in STDBY_RC mode. Verify by reading status.
    uint8_t lStatus = 0;
    readCommand(SX1262_CMD_GET_STATUS, &lStatus, 1);
    mInitStatusByte = lStatus;
    if (mBusyTimedOut)
    {
        mInitError = RadioSX1262InitError::BusyTimeout;
        return;
    }

    // Status byte: bits 6:4 = chip mode.
    // Valid SX1262 modes reported by GET_STATUS are:
    // 0x2 = STDBY_RC, 0x3 = STDBY_XOSC, 0x4 = FS, 0x5 = RX, 0x6 = TX.
    uint8_t lChipMode = (lStatus >> 4) & 0x07;
    if (lChipMode < 0x02 || lChipMode > 0x06)
    {
        mInitError = RadioSX1262InitError::StatusInvalid;
        mInitialized = false;
        return;
    }

    mInitialized = true;

    // Configure for io-homecontrol FSK mode
    if (configure() != RadioError::None)
    {
        mInitError = RadioSX1262InitError::ConfigureFailed;
        mInitialized = false;
        return;
    }
}

void RadioSX1262::resetChip()
{
#ifdef ESP32
    digitalWrite(mResetPin, LOW);
    delay(1);
    digitalWrite(mResetPin, HIGH);
    delay(10); // Wait for chip to be ready after reset
    // Wait for BUSY to go low (chip ready)
    waitBusy(true);
#endif
}

RadioError RadioSX1262::configure()
{
    if (!mInitialized)
        return RadioError::NotInitialized;
    if (mBusyTimedOut)
        return RadioError::HardwareError;

    // Enter standby mode for configuration
    standby();

    // Use DC-DC regulator for better efficiency (if board supports it)
    uint8_t lRegMode = SX1262_REGULATOR_DC_DC;
    if (!sendCommand(SX1262_CMD_SET_REGULATOR_MODE, &lRegMode, 1))
        return RadioError::HardwareError;

#ifdef IOHC_RADIO_TCXO_VOLTAGE
    if (!configureTcxo(true))
        return RadioError::HardwareError;
#endif

    // Boards can use DIO2 RF switching, a shared external RF switch GPIO, dedicated RX/TX enables, or a mix.
    uint8_t lDio2RfSwitch = mUseDio2RfSwitch ? 0x01 : 0x00;
    if (!sendCommand(SX1262_CMD_SET_DIO2_AS_RF_SWITCH, &lDio2RfSwitch, 1))
        return RadioError::HardwareError;

    // Set packet type to GFSK/FSK
    uint8_t lPacketType = SX1262_PACKET_TYPE_GFSK;
    if (!sendCommand(SX1262_CMD_SET_PACKET_TYPE, &lPacketType, 1))
        return RadioError::HardwareError;

    if (!applyStandardModulationParams())
        return RadioError::HardwareError;

    // Set packet parameters
    mPreambleLength = 8; // default short preamble (same as SX1276)
    if (!applySyncWord(IOHC_SYNC_WORD, IOHC_SYNC_WORD_SIZE))
        return RadioError::HardwareError;

    mPacketPayloadLen = mSoftwarePhyMode ? SX1262_IOHOME_RX_FIXED_LEN : IOHC_FRAME_BUFFER_SIZE;
    if (!applyPacketParams())
        return RadioError::HardwareError;

    if (!mSoftwarePhyMode)
    {
        // Non-io-home sync patterns keep using the chip packet engine with hardware CRC.
        if (!writeRegister(SX1262_REG_CRC_INIT_MSB, 0x1D) ||
            !writeRegister(SX1262_REG_CRC_INIT_LSB, 0x0F))
            return RadioError::HardwareError;
        if (!writeRegister(SX1262_REG_CRC_POLY_MSB, 0x10) ||
            !writeRegister(SX1262_REG_CRC_POLY_LSB, 0x21))
            return RadioError::HardwareError;
    }

    if (!configureRxIrqs())
        return RadioError::HardwareError;

    // Set buffer base addresses (TX at 0x00, RX at 0x80)
    uint8_t lBufBase[2] = {0x00, 0x80};
    if (!sendCommand(SX1262_CMD_SET_BUFFER_BASE_ADDR, lBufBase, 2))
        return RadioError::HardwareError;

    // Set default frequency (CH2 = 868.95 MHz)
    if (setFrequencyInternal(IOHC_FREQ_2, true) != RadioError::None)
        return RadioError::HardwareError;

    // Set default power
    if (setOutputPowerInternal(14, true) != RadioError::None)
        return RadioError::HardwareError;

    // Set RX gain to boosted for better sensitivity
    if (!writeRegister(SX1262_REG_RX_GAIN, SX1262_RX_GAIN_BOOSTED))
        return RadioError::HardwareError;

    // Beegee/Semtech SX1262 path uses the higher SX1262 current limit and
    // the TX clamp workaround for better resistance to antenna mismatch.
    uint8_t lTxClampCfg = 0;
    if (!readRegister(SX1262_REG_TX_CLAMP_CFG, lTxClampCfg) ||
        !writeRegister(SX1262_REG_TX_CLAMP_CFG, lTxClampCfg | (0x0F << 1)) ||
        !writeRegister(SX1262_REG_OCP, 0x38))
        return RadioError::HardwareError;

    // SX126x datasheet known limitation: for any (G)FSK transmission,
    // bit 2 of register 0x0889 must be set before packet transmission.
    uint8_t lTxModulation = 0;
    if (!readRegister(0x0889, lTxModulation) ||
        !writeRegister(0x0889, lTxModulation | 0x04))
        return RadioError::HardwareError;

    // Keep the oscillator alive after TX/RX completion so short response windows do not pay a cold-start penalty.
    mStandbyMode = SX1262_STDBY_XOSC;
    if (!applyRxTxFallbackMode(SX1262_FALLBACK_STDBY_XOSC, true))
        return RadioError::HardwareError;

    // Calibrate for 868 MHz
    calibrate();

    if (mBusyTimedOut)
        return RadioError::HardwareError;

    uint16_t lDeviceErrors = 0;
    if (!readDeviceErrors(lDeviceErrors, true))
        return RadioError::HardwareError;
    mInitDeviceErrors = lDeviceErrors;
    if (lDeviceErrors != 0 && !clearDeviceErrors(true))
        return RadioError::HardwareError;

    return RadioError::None;
}

RadioError RadioSX1262::setFrequency(uint32_t iFreqHz)
{
    return setFrequencyInternal(iFreqHz, false);
}

RadioError RadioSX1262::setFrequencyBlocking(uint32_t iFreqHz)
{
    return setFrequencyInternal(iFreqHz, true);
}

RadioError RadioSX1262::setFrequencyInternal(uint32_t iFreqHz, bool iBlocking)
{
    if (!mInitialized)
        return RadioError::NotInitialized;
    if (mState == RadioState::Transmitting)
        return RadioError::Busy;
    if (mState == RadioState::Receiving && !tryStandby(iBlocking))
        return RadioError::Busy;

    // SX1262: RfFreq = Freq * 2^25 / Fxosc = Freq * 33554432 / 32000000
    // Simplified: RfFreq = (uint32_t)(Freq * 1.048576)
    uint32_t lRfFreq = (uint32_t)((double)iFreqHz / 32000000.0 * 33554432.0);
    uint8_t lParams[4];
    lParams[0] = (lRfFreq >> 24) & 0xFF;
    lParams[1] = (lRfFreq >> 16) & 0xFF;
    lParams[2] = (lRfFreq >> 8) & 0xFF;
    lParams[3] = lRfFreq & 0xFF;
    if (!sendCommand(SX1262_CMD_SET_RF_FREQUENCY, lParams, 4, iBlocking))
        return RadioError::Busy;
    mCurrentFreq = iFreqHz;

    return RadioError::None;
}

RadioError RadioSX1262::setOutputPower(uint8_t iPower)
{
    return setOutputPowerInternal(iPower, false);
}

RadioError RadioSX1262::setOutputPowerInternal(uint8_t iPower, bool iBlocking)
{
    if (!mInitialized)
        return RadioError::NotInitialized;
    if (iPower < 2 || iPower > 22)
        return RadioError::InvalidParam;

    uint8_t lPaDutyCycle = SX1262_PA_DUTY_CYCLE_14DBM;
    uint8_t lPaHpMax = SX1262_PA_HP_MAX_14DBM;
    int8_t lPowerDbm = (int8_t)iPower;
    if (lPowerDbm > 22)
        lPowerDbm = 22;
    if (lPowerDbm < -9)
        lPowerDbm = -9;

    if (lPowerDbm >= 22)
    {
        lPaDutyCycle = SX1262_PA_DUTY_CYCLE_22DBM;
        lPaHpMax = SX1262_PA_HP_MAX_22DBM;
    }
    else if (lPowerDbm >= 20)
    {
        lPaDutyCycle = SX1262_PA_DUTY_CYCLE_20DBM;
        lPaHpMax = SX1262_PA_HP_MAX_20DBM;
    }
    else if (lPowerDbm >= 17)
    {
        lPaDutyCycle = SX1262_PA_DUTY_CYCLE_17DBM;
        lPaHpMax = SX1262_PA_HP_MAX_17DBM;
    }

    uint8_t lPaConfig[4] = {lPaDutyCycle, lPaHpMax,
                            SX1262_PA_DEVICE_SEL_SX1262, SX1262_PA_LUT_STANDARD};
    if (!sendCommand(SX1262_CMD_SET_PA_CONFIG, lPaConfig, 4, iBlocking))
        return RadioError::Busy;

    // Use the datasheet high-power PA table and keep the gentler 200 us ramp explicit.
    uint8_t lTxParams[2] = {static_cast<uint8_t>(lPowerDbm), SX1262_PA_RAMP_200U};
    if (!sendCommand(SX1262_CMD_SET_TX_PARAMS, lTxParams, 2, iBlocking))
        return RadioError::Busy;

    return RadioError::None;
}

RadioError RadioSX1262::setPreambleLength(uint16_t iSymbols)
{
    return setPreambleLengthInternal(iSymbols, false);
}

RadioError RadioSX1262::setPreambleLengthBlocking(uint16_t iSymbols)
{
    return setPreambleLengthInternal(iSymbols, true);
}

RadioError RadioSX1262::setPreambleLengthInternal(uint16_t iSymbols, bool iBlocking)
{
    if (!mInitialized)
        return RadioError::NotInitialized;
    if (mState == RadioState::Transmitting)
        return RadioError::Busy;

    mPreambleLength = iSymbols;
    if (mState == RadioState::Receiving && !tryStandby(iBlocking))
        return RadioError::Busy;
    if (!applyPacketParams(iBlocking))
        return RadioError::Busy;
    return RadioError::None;
}

RadioError RadioSX1262::startTransmit(const uint8_t *iData, uint8_t iLen)
{
    return startTransmitInternal(iData, iLen, false);
}

RadioError RadioSX1262::startTransmitBlocking(const uint8_t *iData, uint8_t iLen)
{
    return startTransmitInternal(iData, iLen, true);
}

RadioError RadioSX1262::startTransmitInternal(const uint8_t *iData, uint8_t iLen, bool iBlocking)
{
    if (iLen == 0 || iData == nullptr)
        return RadioError::InvalidParam;
    if (!mInitialized)
        return RadioError::NotInitialized;
    if (mState == RadioState::Transmitting)
        return RadioError::Busy;

    uint8_t lEncodedBuf[SX1262_IOHOME_MAX_ENCODED_FRAME_LEN] = {0};
    const uint8_t *lTxData = iData;
    uint8_t lTxLen = iLen;

    if (mSoftwarePhyMode)
    {
        if (iLen > IOHC_FRAME_BUFFER_SIZE)
            return RadioError::InvalidParam;

        const size_t lEncodedLen = sx1262EncodeIoHomeFrame(iData, iLen, lEncodedBuf, sizeof(lEncodedBuf));
        if (lEncodedLen == 0 || lEncodedLen > 0xFF)
            return RadioError::InvalidParam;

        lTxData = lEncodedBuf;
        lTxLen = static_cast<uint8_t>(lEncodedLen);
    }
    else if (iLen > kMaxSx1262PayloadLen)
    {
        return RadioError::InvalidParam;
    }

    // Enter standby to configure
    if (!tryStandby(iBlocking))
        return RadioError::Busy;
    setRfSwitchTx();

    // Move through FS explicitly before SET_TX, matching the Semtech/Beegee launch flow.
    if (!sendCommand(SX1262_CMD_SET_FS, nullptr, 0, iBlocking))
    {
        setRfSwitchRx();
        return RadioError::Busy;
    }

    mPacketPayloadLen = lTxLen;
    if (!applyPacketParams(iBlocking))
    {
        setRfSwitchRx();
        return RadioError::Busy;
    }

    // SX126x GFSK erratum: packet/modulation configuration can clear this
    // bit, so set it for every transmission rather than only at init time.
    uint8_t lTxModulation = 0;
    if (!readRegister(0x0889, lTxModulation) ||
        !writeRegister(0x0889, static_cast<uint8_t>(lTxModulation | 0x04)))
    {
        setRfSwitchRx();
        return RadioError::HardwareError;
    }

    if (!writeBuffer(0x00, lTxData, lTxLen, iBlocking))
    {
        setRfSwitchRx();
        return RadioError::Busy;
    }

    // Clear IRQ flags before TX
    mIrqFired = false;
    mPreambleFlag = false;
    mSyncFlag = false;
    mSoftwarePhySyncAtUs = 0;
    mEarlyRxFrameLen = 0;
#ifdef ESP32
    portENTER_CRITICAL(&mPendingIrqMux);
#endif
    mPendingIrqStickyMask = 0;
    mPendingIrqHead = 0;
    mPendingIrqTail = 0;
#ifdef ESP32
    portEXIT_CRITICAL(&mPendingIrqMux);
#endif
    if (!clearIrqStatus(SX1262_IRQ_ALL, iBlocking) || !configureTxIrqs(iBlocking))
    {
        setRfSwitchRx();
        return RadioError::Busy;
    }

    uint8_t lStatus = 0;
    if (!readCommand(SX1262_CMD_GET_STATUS, &lStatus, 1, iBlocking))
    {
        setRfSwitchRx();
        return RadioError::Busy;
    }
    mLastOpStatusBefore = lStatus;

    // Start TX with no timeout while debugging basic TX_DONE signalling.
    uint8_t lTxParams[3] = {0x00, 0x00, 0x00};
    if (!sendCommand(SX1262_CMD_SET_TX, lTxParams, 3, iBlocking))
    {
        setRfSwitchRx();
        return RadioError::Busy;
    }
    mTxStartCount++;
    resetTxBusyTrace();
    mTxBusyTraceActive = true;
    mTxCompletionPhase = TxCompletionPhase::WaitingForIrq;
    mState = RadioState::Transmitting;

    if (readCommand(SX1262_CMD_GET_STATUS, &lStatus, 1, iBlocking))
    {
        mLastTxSetStatus = lStatus;
        mLastOpStatusAfter = lStatus;
    }
    else
    {
        mLastTxSetStatus = 0;
        mLastOpStatusAfter = 0;
    }

    uint16_t lIrqImmediate = 0;
    mLastTxIrqImmediate = getIrqStatus(lIrqImmediate, iBlocking) ? lIrqImmediate : 0;

    return RadioError::None;
}

RadioError RadioSX1262::startReceive()
{
    return startReceiveInternal(false);
}

RadioError RadioSX1262::startReceiveBlocking()
{
    return startReceiveInternal(true);
}

RadioError RadioSX1262::startReceiveInternal(bool iBlocking)
{
    if (!mInitialized)
        return RadioError::NotInitialized;

    if (!tryStandby(iBlocking))
    {
        mReceiveRestartPending = true;
        return RadioError::Busy;
    }

    setRfSwitchRx();
    mPacketPayloadLen = mSoftwarePhyMode ? SX1262_IOHOME_RX_FIXED_LEN : IOHC_FRAME_BUFFER_SIZE;

    // Clear IRQ flags and preamble flag before RX
    mIrqFired = false;
    mPreambleFlag = false;
    mSyncFlag = false;
    mSoftwarePhySyncAtUs = 0;
#ifdef ESP32
    portENTER_CRITICAL(&mPendingIrqMux);
#endif
    mPendingIrqStickyMask = 0;
    mPendingIrqHead = 0;
    mPendingIrqTail = 0;
#ifdef ESP32
    portEXIT_CRITICAL(&mPendingIrqMux);
#endif
    if (!applyPacketParams(iBlocking) || !clearIrqStatus(SX1262_IRQ_ALL, iBlocking) || !configureRxIrqs(iBlocking))
    {
        mReceiveRestartPending = true;
        return RadioError::Busy;
    }

    uint8_t lStatus = 0;
    if (!readCommand(SX1262_CMD_GET_STATUS, &lStatus, 1, iBlocking))
    {
        mReceiveRestartPending = true;
        return RadioError::Busy;
    }
    mLastOpStatusBefore = lStatus;

    // Start RX (timeout = 0xFFFFFF for continuous RX)
    uint8_t lRxParams[3] = {0xFF, 0xFF, 0xFF}; // continuous RX
    if (!sendCommand(SX1262_CMD_SET_RX, lRxParams, 3, iBlocking))
    {
        mReceiveRestartPending = true;
        return RadioError::Busy;
    }
    mRxStartCount++;
    mState = RadioState::Receiving;
    mReceiveRestartPending = false;
    mTxBusyTraceActive = false;
    clearTxBusyTraceOp();
    mTxCompletionPhase = TxCompletionPhase::WaitingForIrq;

    if (mTxToRxSettlePending)
    {
        // The receiver is already armed, so an immediate peer reply is captured
        // while the GFSK frequency discriminator is given time to stabilize.
        delayMicroseconds(kTxToRxSettleUs);
        mTxToRxSettlePending = false;
    }

    if (readCommand(SX1262_CMD_GET_STATUS, &lStatus, 1, iBlocking))
        mLastOpStatusAfter = lStatus;
    else
        mLastOpStatusAfter = 0;

    return RadioError::None;
}

bool RadioSX1262::isTxDone()
{
    return isTxDoneInternal(false);
}

bool RadioSX1262::isTxDoneBlocking()
{
    return isTxDoneInternal(true);
}

bool RadioSX1262::isTxDoneInternal(bool iBlocking)
{
    if (mState != RadioState::Transmitting)
        return false;

    if (mTxCompletionPhase != TxCompletionPhase::WaitingForIrq)
        return advanceTxDoneCleanup(iBlocking);

    uint16_t lIrq = 0;
    bool lNeedsClear = false;
    if (!consumeIrqStatus(lIrq, lNeedsClear, iBlocking))
        return false;

    if (lIrq & SX1262_IRQ_TX_DONE)
    {
        mTxCompletionPhase = TxCompletionPhase::ClearIrqPending;
        return advanceTxDoneCleanup(iBlocking);
    }

    if (lNeedsClear && !clearIrqStatus(SX1262_IRQ_ALL, iBlocking))
        return false;

    return false;
}

bool RadioSX1262::advanceTxDoneCleanup(bool iBlocking)
{
    if (mTxCompletionPhase == TxCompletionPhase::ClearIrqPending)
    {
        if (!clearIrqStatus(SX1262_IRQ_ALL, iBlocking))
            return false;
        setRfSwitchRx();
        mState = RadioState::Idle;
        mTxToRxSettlePending = true;
        mTxDoneCount++;
        mTxCompletionPhase = TxCompletionPhase::WaitingForIrq;
        return true;
    }

    return false;
}

bool RadioSX1262::isPacketAvailable()
{
    if (mEarlyRxFrameLen != 0)
        return true;

    if (mReceiveRestartPending)
    {
        if (startReceive() == RadioError::Busy)
            return false;
    }

    if (mState != RadioState::Receiving)
        return false;

    uint16_t lIrq = 0;
    bool lNeedsClear = false;
    if (!consumeIrqStatus(lIrq, lNeedsClear, false))
        return false;

    if (lIrq & SX1262_IRQ_RX_DONE)
    {
        if (lNeedsClear && !clearIrqStatus(SX1262_IRQ_ALL, false))
            return false;
        mPreambleFlag = false;
        mSyncFlag = false;

        if (lIrq & SX1262_IRQ_CRC_ERR)
            return false;

        return true;
    }

    if (mSoftwarePhyMode && (lIrq & SX1262_IRQ_SYNC_WORD_VALID))
    {
        if (lNeedsClear && !clearIrqStatus(lIrq & ~SX1262_IRQ_RX_DONE, false))
            return false;

        // RX_DONE is tied to the fixed raw capture size. Complete from CTRL0's
        // declared length instead, accepting the shortcut only after CRC validation.
        return tryCompleteSoftwarePhyFromLength();
    }

    if (lNeedsClear && !clearIrqStatus(lIrq & ~SX1262_IRQ_RX_DONE, false))
        return false;
    return false;
}

bool RadioSX1262::isPreambleDetected() const
{
    return mPreambleFlag;
}

bool RadioSX1262::isSyncDetected() const
{
    return mSyncFlag;
}

uint8_t RadioSX1262::readPacket(uint8_t *oBuffer, uint8_t iMaxLen)
{
    if (!mInitialized || oBuffer == nullptr || iMaxLen == 0)
        return 0;

    if (mEarlyRxFrameLen != 0)
    {
        uint8_t lLen = mEarlyRxFrameLen;
        if (lLen > iMaxLen)
            lLen = iMaxLen;
        memcpy(oBuffer, mEarlyRxFrame, lLen);
        mEarlyRxFrameLen = 0;
        return lLen;
    }

    // Read RSSI from GetPacketStatus
    // For GFSK: Status[0] = RxStatus, Status[1] = RssiSync, Status[2] = RssiAvg
    uint8_t lPktStatus[3] = {0};
    if (!readCommand(SX1262_CMD_GET_PACKET_STATUS, lPktStatus, 3, false))
    {
        mRxReadFailCount++;
        startReceive();
        return 0;
    }
    mLastRssi = -(lPktStatus[2] / 2); // RssiAvg in -dBm/2

    // Read RX buffer status to get payload length and start offset
    uint8_t lRxBufStatus[2] = {0};
    if (!readCommand(SX1262_CMD_GET_RX_BUFFER_STATUS, lRxBufStatus, 2, false))
    {
        mRxReadFailCount++;
        startReceive();
        return 0;
    }
    uint8_t lPayloadLen = lRxBufStatus[0];
    uint8_t lRxStartOffset = lRxBufStatus[1];

    if (mSoftwarePhyMode)
    {
        uint8_t lRawBuf[SX1262_IOHOME_RX_READ_LEN] = {0};
        uint8_t lReadLen = lPayloadLen;
        if (lReadLen == 0 || lReadLen < SX1262_IOHOME_RX_FIXED_LEN)
            lReadLen = SX1262_IOHOME_RX_READ_LEN;
        if (lReadLen > sizeof(lRawBuf))
            lReadLen = sizeof(lRawBuf);

        if (!readBuffer(lRxStartOffset, lRawBuf, lReadLen, false))
        {
            mRxReadFailCount++;
            startReceive();
            return 0;
        }

        size_t lFrameLen = 0;
        const bool lFound = sx1262FindIoHomeFrame(lRawBuf, lReadLen, oBuffer, iMaxLen, lFrameLen);
        if (!lFound)
            logDiscardedSoftwareCapture(lPayloadLen, lReadLen, lRxStartOffset, mLastRssi, lRawBuf);
        startReceive();
        return lFound ? static_cast<uint8_t>(lFrameLen) : 0;
    }

    // Match SX1276 io-homecontrol mode: RX payload length comes from the radio, not from a leading payload byte.
    uint8_t lLen = lPayloadLen;
    if (lLen > iMaxLen)
        lLen = iMaxLen;
    if (lLen > 0 && !readBuffer(lRxStartOffset, oBuffer, lLen, false))
    {
        mRxReadFailCount++;
        startReceive();
        return 0;
    }

    // Restart RX
    startReceive();

    return lLen;
}

int16_t RadioSX1262::lastRssi() const
{
    return mLastRssi;
}

bool RadioSX1262::currentRssi(int16_t &oRssi)
{
    oRssi = 0;
    if (!mInitialized || mState != RadioState::Receiving)
        return false;

    uint8_t lRssiRaw = 0;
    if (!readCommand(SX1262_CMD_GET_RSSI_INST, &lRssiRaw, 1))
        return false;

    oRssi = -(lRssiRaw / 2);
    return true;
}

void RadioSX1262::sleep()
{
    if (!mInitialized)
        return;
    // Sleep config: 0x00 = cold start (no retention), 0x04 = warm start (retention)
    uint8_t lSleepCfg = 0x04; // warm start for faster wake
    if (!sendCommand(SX1262_CMD_SET_SLEEP, &lSleepCfg, 1))
        return;
    setRfSwitchIdle();
    mTxCompletionPhase = TxCompletionPhase::WaitingForIrq;
    mState = RadioState::Sleep;
}

void RadioSX1262::standby()
{
    uint8_t lStdbyCfg = mStandbyMode;
    if (!sendCommand(SX1262_CMD_SET_STANDBY, &lStdbyCfg, 1))
        return;
    setRfSwitchRx();
    mTxCompletionPhase = TxCompletionPhase::WaitingForIrq;
    mState = RadioState::Idle;
}

RadioState RadioSX1262::state() const
{
    return mState;
}

bool RadioSX1262::isInitialized() const
{
    return mInitialized;
}

RadioSX1262InitError RadioSX1262::initError() const
{
    return mInitError;
}

uint8_t RadioSX1262::initStatusByte() const
{
    return mInitStatusByte;
}

uint8_t RadioSX1262::initCommandStatus() const
{
    return (mInitStatusByte >> 1) & 0x07;
}

bool RadioSX1262::busyTimedOut() const
{
    return mBusyTimedOut;
}

uint32_t RadioSX1262::txStartCount() const
{
    return mTxStartCount;
}

uint32_t RadioSX1262::txDoneCount() const
{
    return mTxDoneCount;
}

uint32_t RadioSX1262::rxStartCount() const
{
    return mRxStartCount;
}

uint32_t RadioSX1262::irqCount() const
{
    return mIrqCount;
}

uint32_t RadioSX1262::preambleIrqCount() const
{
    return mPreambleIrqCount;
}

uint32_t RadioSX1262::syncWordIrqCount() const
{
    return mSyncWordIrqCount;
}

uint32_t RadioSX1262::rxDoneCount() const
{
    return mRxDoneCount;
}

uint32_t RadioSX1262::crcErrorCount() const
{
    return mCrcErrorCount;
}

uint32_t RadioSX1262::timeoutCount() const
{
    return mTimeoutCount;
}

uint32_t RadioSX1262::irqPollHitCount() const
{
    return mIrqPollHitCount;
}

uint32_t RadioSX1262::preambleOnlyIrqCount() const
{
    return mPreambleOnlyIrqCount;
}

uint32_t RadioSX1262::rxReadFailCount() const
{
    return mRxReadFailCount;
}

uint32_t RadioSX1262::txBusyHighHitCount() const
{
    return mTxBusyHighHitCount;
}

uint32_t RadioSX1262::txBusyHighTotalUs() const
{
    return mTxBusyHighTotalUs;
}

uint32_t RadioSX1262::txBusyHighMaxUs() const
{
    return mTxBusyHighMaxUs;
}

RadioSX1262TxBusyTrace RadioSX1262::txBusyTrace() const
{
    return mTxBusyTrace;
}

uint16_t RadioSX1262::lastIrqStatus() const
{
    return mLastIrqStatus;
}

uint8_t RadioSX1262::lastOpStatusBefore() const
{
    return mLastOpStatusBefore;
}

uint8_t RadioSX1262::lastOpStatusAfter() const
{
    return mLastOpStatusAfter;
}

uint8_t RadioSX1262::lastTxSetStatus() const
{
    return mLastTxSetStatus;
}

uint16_t RadioSX1262::lastTxIrqImmediate() const
{
    return mLastTxIrqImmediate;
}

uint16_t RadioSX1262::initDeviceErrors() const
{
    return mInitDeviceErrors;
}

uint16_t RadioSX1262::lastDeviceErrors() const
{
    return mLastDeviceErrors;
}

uint32_t RadioSX1262::tcxoStartupDelayUs() const
{
    return mTcxoStartupDelayUs;
}

uint8_t RadioSX1262::tcxoStartupAttempts() const
{
    return mTcxoStartupAttempts;
}

uint8_t RadioSX1262::rfSwitchConfig() const
{
    return rfSwitchConfigFlags();
}

uint8_t RadioSX1262::debugReadRegister(uint16_t iAddr)
{
    uint8_t lValue = 0;
    return readRegister(iAddr, lValue) ? lValue : 0;
}

uint8_t RadioSX1262::debugReadStatus()
{
    uint8_t lStatus = 0;
    readCommand(SX1262_CMD_GET_STATUS, &lStatus, 1);
    return lStatus;
}

uint16_t RadioSX1262::debugReadIrqStatus()
{
    uint16_t lIrq = 0;
    return getIrqStatus(lIrq) ? lIrq : 0;
}

uint16_t RadioSX1262::debugReadDeviceErrors()
{
    uint16_t lErrors = 0;
    readDeviceErrors(lErrors, true);
    return lErrors;
}

void RadioSX1262::debugClearDeviceErrors()
{
    clearDeviceErrors(true);
}

int RadioSX1262::debugReadDio1Level() const
{
#ifdef ESP32
    return digitalRead(mDio1Pin);
#else
    return 0;
#endif
}

int RadioSX1262::debugReadBusyLevel() const
{
#ifdef ESP32
    return digitalRead(mBusyPin);
#else
    return 0;
#endif
}

void RadioSX1262::configureEms2Mode()
{
    standby();

    // EMS2 uses 2-byte sync word {0x2D, 0xD4}
    applySyncWord(IOHC_EMS2_SYNC_WORD, IOHC_EMS2_SYNC_WORD_SIZE);
    mPacketPayloadLen = IOHC_FRAME_BUFFER_SIZE;
    applyPacketParams();

    // EMS2 uses 868.95 MHz
    setFrequencyInternal(IOHC_EMS2_FREQ, true);

    mEms2Mode = true;
}

void RadioSX1262::configureStandardMode()
{
    standby();

    applyStandardModulationParams();

    // Restore 3-byte io-homecontrol sync word
    applySyncWord(IOHC_SYNC_WORD, IOHC_SYNC_WORD_SIZE);
    mPacketPayloadLen = mSoftwarePhyMode ? SX1262_IOHOME_RX_FIXED_LEN : IOHC_FRAME_BUFFER_SIZE;
    applyPacketParams();

    // Restore default frequency
    setFrequencyInternal(IOHC_FREQ_1, true);

    mEms2Mode = false;
}

RadioError RadioSX1262::sendEms2Wake()
{
    if (!mInitialized)
        return RadioError::NotInitialized;

    // Set frequency deviation to 0 (unmodulated carrier) by reconfiguring modulation params
    uint8_t lModParams[8];
    uint32_t lBitRate = 26667;
    lModParams[0] = (lBitRate >> 16) & 0xFF;
    lModParams[1] = (lBitRate >> 8) & 0xFF;
    lModParams[2] = lBitRate & 0xFF;
    lModParams[3] = 0x0B; // Gaussian BT=1.0 (irrelevant for the unmodulated carrier)
    lModParams[4] = 0x0C; // 58.6 kHz (irrelevant for the TX-only carrier)
    // FreqDev = 0 (unmodulated carrier)
    lModParams[5] = 0x00;
    lModParams[6] = 0x00;
    lModParams[7] = 0x00;
    sendCommand(SX1262_CMD_SET_MODULATION_PARAMS, lModParams, 8);

    // Set EMS2 frequency
    setFrequencyInternal(IOHC_EMS2_FREQ, true);

    // Start TX continuous wave
    sendCommand(SX1262_CMD_SET_TX_CONTINUOUS, nullptr, 0);
    mState = RadioState::Transmitting;

#ifdef ESP32
    delay(IOHC_EMS2_WAKE_DURATION_MS);
#endif

    // Stop TX, return to standby
    standby();

    // Restore the complete tuned standard waveform, not just its deviation.
    applyStandardModulationParams();

    return RadioError::None;
}

// --- Private helpers ---

bool RadioSX1262::applyStandardModulationParams(bool iBlocking)
{
    // IO-homecontrol GFSK: 38.4 kbps, Gaussian BT=1.0, 58.6 kHz RX bandwidth,
    // and 19.2 kHz deviation. These are the hardware-validated reference values.
    const uint8_t lModParams[8] = {
        0x00, 0x68, 0x2B,
        0x0B,
        0x0C,
        0x00, 0x4E, 0xA5,
    };
    return sendCommand(SX1262_CMD_SET_MODULATION_PARAMS, lModParams, sizeof(lModParams), iBlocking);
}

bool RadioSX1262::applyPacketParams(bool iBlocking)
{
    // SetPacketParams for GFSK: 9 bytes
    // [0-1] PreambleLength (2 bytes, in bits: symbols * 8)
    // [2]   PreambleDetectorLength: 0x04 = 8 bits (1 byte)
    // [3]   SyncWordLength (in bits)
    // [4]   AddrComp: 0x00 = off
    // [5]   HeaderType: 0x00 = fixed length, 0x01 = variable length
    // [6]   PayloadLength: fixed length or max variable length
    // [7]   CrcType: 0x01 = off, 0x06 = 2-byte CRC
    // [8]   Whitening: 0x00 = off (io-homecontrol does not use whitening)
    uint8_t lParams[9];
    uint16_t lPreambleBits = mPreambleLength * 8;
    uint8_t lPayloadLen = mPacketPayloadLen;
    if (lPayloadLen == 0)
        lPayloadLen = mSoftwarePhyMode ? SX1262_IOHOME_RX_FIXED_LEN : IOHC_FRAME_BUFFER_SIZE;
    lParams[0] = (lPreambleBits >> 8) & 0xFF;
    lParams[1] = lPreambleBits & 0xFF;
    lParams[2] = 0x04;                           // preamble detector: 8 bits (1 byte)
    lParams[3] = mSyncWordBits;                  // sync word length is encoded in bits on SX126x GFSK packet params
    lParams[4] = 0x00;                           // no address filtering
    lParams[5] = mSoftwarePhyMode ? 0x00 : 0x01; // fixed length for software PHY, variable length otherwise
    lParams[6] = lPayloadLen;
    lParams[7] = mSoftwarePhyMode ? 0x01 : 0x06; // software CRC for io-home PHY, hardware CRC otherwise
    lParams[8] = 0x00;                           // no whitening
    return sendCommand(SX1262_CMD_SET_PACKET_PARAMS, lParams, 9, iBlocking);
}

bool RadioSX1262::tryCompleteSoftwarePhyFromLength()
{
    uint32_t lSyncAtUs = mSoftwarePhySyncAtUs;
    if (lSyncAtUs == 0)
        lSyncAtUs = micros();

    const auto lWaitForRawBytes = [lSyncAtUs](size_t iRawBytes) {
#ifdef ESP32
        const uint32_t lRawBytesWithMargin = static_cast<uint32_t>(iRawBytes + SX1262_IOHOME_EARLY_READ_MARGIN);
        const uint32_t lNeededUs = static_cast<uint32_t>(
            (lRawBytesWithMargin * 8UL * 1000000UL + kIoHomeLineRateBps - 1UL) / kIoHomeLineRateBps);
        while (static_cast<uint32_t>(micros() - lSyncAtUs) < lNeededUs)
            delayMicroseconds(100);
#else
        (void)lSyncAtUs;
        (void)iRawBytes;
#endif
    };

    lWaitForRawBytes(SX1262_IOHOME_EARLY_HEADER_RAW_LEN);
    uint8_t lHeader[SX1262_IOHOME_EARLY_HEADER_RAW_LEN] = {0};
    if (!readBuffer(kSx1262RxBufferBase, lHeader, sizeof(lHeader), true))
        return false;

    const uint8_t lDeclaredFrameLen = sx1262PeekIoHomeFrameLength(lHeader, sizeof(lHeader));
    const size_t lFrameRawLen = sx1262IoHomeRawBytesForFrame(lDeclaredFrameLen);
    if (lFrameRawLen == 0 ||
        lFrameRawLen + SX1262_IOHOME_EARLY_READ_MARGIN > SX1262_IOHOME_RX_READ_LEN)
        return false;

    lWaitForRawBytes(lFrameRawLen);
    uint8_t lRawBuf[SX1262_IOHOME_RX_READ_LEN] = {0};
    const size_t lReadLen = lFrameRawLen + SX1262_IOHOME_EARLY_READ_MARGIN;
    if (!readBuffer(kSx1262RxBufferBase, lRawBuf, static_cast<uint8_t>(lReadLen), true))
        return false;

    size_t lFrameLen = 0;
    uint8_t lFrame[IOHC_FRAME_BUFFER_SIZE] = {0};
    if (!sx1262FindIoHomeFrame(lRawBuf, lReadLen, lFrame, sizeof(lFrame), lFrameLen))
        return false;

    uint8_t lPacketStatus[3] = {0};
    if (readCommand(SX1262_CMD_GET_PACKET_STATUS, lPacketStatus, sizeof(lPacketStatus), true))
        mLastRssi = -(lPacketStatus[2] / 2);

    memcpy(mEarlyRxFrame, lFrame, lFrameLen);
    mEarlyRxFrameLen = static_cast<uint8_t>(lFrameLen);

    // This fixed-length reception is deliberately complete before RX_DONE.
    // Tear it down and re-arm now; readPacket() consumes the cached validated frame.
    (void)clearIrqStatus(SX1262_IRQ_ALL, true);
    mPreambleFlag = false;
    mSyncFlag = false;
    mSoftwarePhySyncAtUs = 0;
    (void)startReceiveInternal(true);
    return true;
}

bool RadioSX1262::applyRxTxFallbackMode(uint8_t iMode, bool iBlocking)
{
    return sendCommand(SX1262_CMD_SET_RX_TX_FALLBACK_MODE, &iMode, 1, iBlocking);
}

bool RadioSX1262::applySyncWord(const uint8_t *iSyncWord, uint8_t iLen, bool iBlocking)
{
    const SX1262IoHomePhySyncConfig lSyncConfig = sx1262ResolveIoHomeSyncWord(iSyncWord, iLen);
    if (lSyncConfig.syncWordBits == 0)
        return false;

    memcpy(mSyncWord, lSyncConfig.syncWord, sizeof(mSyncWord));
    mSyncWordBits = lSyncConfig.syncWordBits;
    mSoftwarePhyMode = lSyncConfig.softwarePhyEnabled;
    return writeRegisters(SX1262_REG_SYNC_WORD_0, mSyncWord, sizeof(mSyncWord), iBlocking);
}

bool RadioSX1262::readDeviceErrors(uint16_t &oErrors, bool iBlocking)
{
    uint8_t lErrors[2] = {0, 0};
    oErrors = 0;
    if (!readCommand(SX1262_CMD_GET_DEVICE_ERRORS, lErrors, 2, iBlocking))
        return false;
    oErrors = static_cast<uint16_t>((static_cast<uint16_t>(lErrors[0]) << 8) | lErrors[1]);
    mLastDeviceErrors = oErrors;
    return true;
}

bool RadioSX1262::clearDeviceErrors(bool iBlocking)
{
    const uint8_t lClear[2] = {0x00, 0x00};
    const bool lOk = sendCommand(SX1262_CMD_CLEAR_DEVICE_ERRORS, lClear, 2, iBlocking);
    if (lOk)
        mLastDeviceErrors = 0;
    return lOk;
}

bool RadioSX1262::configureTcxo(bool iBlocking)
{
#ifdef IOHC_RADIO_TCXO_VOLTAGE
    const uint32_t lBaseDelayUs = static_cast<uint32_t>(IOHC_RADIO_TCXO_DELAY_US);
    const uint32_t lSecondDelayUs = (lBaseDelayUs < 10000UL) ? 10000UL : lBaseDelayUs * 2UL;
    const uint32_t lThirdDelayUs = (lSecondDelayUs < 50000UL) ? 50000UL : lSecondDelayUs;
    const uint32_t lTcxoDelayUs[3] = {lBaseDelayUs, lSecondDelayUs, lThirdDelayUs};

    for (uint8_t lAttempt = 0; lAttempt < 3; lAttempt++)
    {
        const uint32_t lDelayUs = lTcxoDelayUs[lAttempt];
        uint32_t lTcxoDelayTicks = static_cast<uint32_t>((static_cast<uint64_t>(lDelayUs) * 8ULL + 124ULL) / 125ULL);
        if (lTcxoDelayTicks == 0)
            lTcxoDelayTicks = 1;
        uint8_t lTcxoCtrl[4] = {
            static_cast<uint8_t>(IOHC_RADIO_TCXO_VOLTAGE & 0x07),
            static_cast<uint8_t>((lTcxoDelayTicks >> 16) & 0xFF),
            static_cast<uint8_t>((lTcxoDelayTicks >> 8) & 0xFF),
            static_cast<uint8_t>(lTcxoDelayTicks & 0xFF)};

        mTcxoStartupAttempts = static_cast<uint8_t>(lAttempt + 1);
        mTcxoStartupDelayUs = lDelayUs;

        if (!clearDeviceErrors(iBlocking))
            return false;
        if (!sendCommand(SX1262_CMD_SET_DIO3_AS_TCXO_CTRL, lTcxoCtrl, 4, iBlocking))
            return false;

#ifdef ESP32
        delay((lDelayUs + 999UL) / 1000UL + 2UL);
        if (!waitBusy(true))
            return false;
#endif

        uint8_t lCalMask = SX1262_CALIBRATE_ALL;
        if (!sendCommand(SX1262_CMD_CALIBRATE, &lCalMask, 1, true))
            return false;

#ifdef ESP32
        delay(10);
        if (!waitBusy(true))
            return false;
#endif

        uint16_t lDeviceErrors = 0;
        if (!readDeviceErrors(lDeviceErrors, true))
            return false;
        if ((lDeviceErrors & SX1262_DEVICE_ERROR_XOSC_START) == 0)
            break;
    }
#else
    (void)iBlocking;
#endif

    return true;
}

// --- SPI command interface ---

bool RadioSX1262::lockChip(bool iBlocking)
{
#ifdef ESP32
    if (mChipMutex == nullptr)
        return true;
    const TickType_t lTicks = iBlocking ? pdMS_TO_TICKS(SX1262_MUTEX_TIMEOUT_MS) : 0;
    return xSemaphoreTakeRecursive(mChipMutex, lTicks) == pdTRUE;
#else
    (void)iBlocking;
    return true;
#endif
}

void RadioSX1262::unlockChip()
{
#ifdef ESP32
    if (mChipMutex != nullptr)
        xSemaphoreGiveRecursive(mChipMutex);
#endif
}

bool RadioSX1262::waitBusy(bool iBlocking)
{
#ifdef ESP32
    if (digitalRead(mBusyPin) != HIGH)
    {
        if (mTxBusyHighStartUs != 0)
        {
            const uint32_t lBusyDurationUs = micros() - mTxBusyHighStartUs;
            mTxBusyHighTotalUs += lBusyDurationUs;
            if (lBusyDurationUs > mTxBusyHighMaxUs)
                mTxBusyHighMaxUs = lBusyDurationUs;
            accumulateTxBusyTrace(lBusyDurationUs);
            mTxBusyHighStartUs = 0;
        }
        return true;
    }

    if (mTxBusyTraceActive && mTxBusyHighStartUs == 0)
    {
        mTxBusyHighHitCount++;
        mTxBusyHighStartUs = micros();
    }

    if (!iBlocking)
        return false;

    unsigned long lStart = millis();
    while (digitalRead(mBusyPin) == HIGH)
    {
        if (millis() - lStart > SX1262_BUSY_TIMEOUT_MS)
        {
            mBusyTimedOut = true;
            return false;
        }
        yield();
    }
#endif
    return true;
}

void RadioSX1262::resetTxBusyTrace()
{
    mTxBusyHighHitCount = 0;
    mTxBusyHighTotalUs = 0;
    mTxBusyHighMaxUs = 0;
    mTxBusyHighStartUs = 0;
    mTxBusyTraceOp = TxBusyTraceOp::None;
    mTxBusyTrace = {};
}

void RadioSX1262::setTxBusyTraceOp(TxBusyTraceOp iOp)
{
    if (mTxBusyTraceActive)
        mTxBusyTraceOp = iOp;
}

void RadioSX1262::clearTxBusyTraceOp()
{
    mTxBusyTraceOp = TxBusyTraceOp::None;
}

void RadioSX1262::accumulateTxBusyTrace(uint32_t iBusyDurationUs)
{
    if (!mTxBusyTraceActive)
        return;

    uint32_t *lHits = &mTxBusyTrace.otherHits;
    uint32_t *lTotalUs = &mTxBusyTrace.otherTotalUs;
    switch (mTxBusyTraceOp)
    {
    case TxBusyTraceOp::GetIrqStatus:
        lHits = &mTxBusyTrace.irqHits;
        lTotalUs = &mTxBusyTrace.irqTotalUs;
        break;
    case TxBusyTraceOp::GetStatus:
        lHits = &mTxBusyTrace.statusHits;
        lTotalUs = &mTxBusyTrace.statusTotalUs;
        break;
    case TxBusyTraceOp::ClearIrqStatus:
        lHits = &mTxBusyTrace.clearIrqHits;
        lTotalUs = &mTxBusyTrace.clearIrqTotalUs;
        break;
    case TxBusyTraceOp::SetStandby:
        lHits = &mTxBusyTrace.standbyHits;
        lTotalUs = &mTxBusyTrace.standbyTotalUs;
        break;
    case TxBusyTraceOp::SetRx:
        lHits = &mTxBusyTrace.rxHits;
        lTotalUs = &mTxBusyTrace.rxTotalUs;
        break;
    case TxBusyTraceOp::Other:
    case TxBusyTraceOp::None:
        break;
    }

    (*lHits)++;
    (*lTotalUs) += iBusyDurationUs;
}

bool RadioSX1262::tryStandby(bool iBlocking)
{
    uint8_t lStdbyCfg = mStandbyMode;
    setTxBusyTraceOp(TxBusyTraceOp::SetStandby);
    const bool lOk = sendCommand(SX1262_CMD_SET_STANDBY, &lStdbyCfg, 1, iBlocking);
    clearTxBusyTraceOp();
    if (!lOk)
        return false;
    setRfSwitchRx();
    mState = RadioState::Idle;
    return true;
}

void RadioSX1262::handleQueuedDio1()
{
#ifdef ESP32
    if (!lockChip(true))
    {
        if (mDio1EventQueue != nullptr)
        {
            const uint32_t lEvent = mDio1Pin;
            (void)xQueueSend(mDio1EventQueue, &lEvent, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(SX1262_DIO1_REQUEUE_DELAY_MS));
        return;
    }
#endif

    uint16_t lIrq = 0;
    if (!getIrqStatus(lIrq, true))
    {
        unlockChip();
        return;
    }

    mIrqFired = false;
    if (lIrq == 0)
    {
        unlockChip();
        return;
    }

    noteIrqStatus(lIrq, false);
    if ((lIrq & kCachedIrqMask) != 0)
        enqueuePendingIrq(lIrq);
    (void)clearIrqStatus(lIrq, true);
    unlockChip();
}

void RadioSX1262::noteIrqStatus(uint16_t iIrq, bool iPolled)
{
    mLastIrqStatus = iIrq;
    mIrqCount++;
    if (iPolled)
        mIrqPollHitCount++;
    if (iIrq == SX1262_IRQ_PREAMBLE_DETECTED)
        mPreambleOnlyIrqCount++;
    if (iIrq & SX1262_IRQ_PREAMBLE_DETECTED)
    {
        mPreambleFlag = true;
        mPreambleIrqCount++;
    }
    if (iIrq & SX1262_IRQ_SYNC_WORD_VALID)
    {
        if (!mSyncFlag)
            mSoftwarePhySyncAtUs = micros();
        mSyncFlag = true;
        mSyncWordIrqCount++;
    }
    if (iIrq & SX1262_IRQ_RX_DONE)
        mRxDoneCount++;
    if (iIrq & SX1262_IRQ_CRC_ERR)
        mCrcErrorCount++;
    if (iIrq & SX1262_IRQ_TIMEOUT)
        mTimeoutCount++;
}

bool RadioSX1262::enqueuePendingIrq(uint16_t iIrq)
{
#ifdef ESP32
    portENTER_CRITICAL(&mPendingIrqMux);
#endif
    const uint8_t lNextHead = static_cast<uint8_t>((mPendingIrqHead + 1U) % kPendingIrqDepth);
    if (lNextHead == mPendingIrqTail)
    {
        mPendingIrqStickyMask = static_cast<uint16_t>(mPendingIrqStickyMask | iIrq);
        mPendingIrqOverflowCount++;
#ifdef ESP32
        portEXIT_CRITICAL(&mPendingIrqMux);
#endif
        return false;
    }
    mPendingIrqQueue[mPendingIrqHead] = iIrq;
    mPendingIrqHead = lNextHead;
#ifdef ESP32
    portEXIT_CRITICAL(&mPendingIrqMux);
#endif
    return true;
}

bool RadioSX1262::dequeuePendingIrq(uint16_t &oIrq)
{
    oIrq = 0;
#ifdef ESP32
    portENTER_CRITICAL(&mPendingIrqMux);
#endif
    if (mPendingIrqTail != mPendingIrqHead)
    {
        oIrq = mPendingIrqQueue[mPendingIrqTail];
        mPendingIrqTail = static_cast<uint8_t>((mPendingIrqTail + 1U) % kPendingIrqDepth);
#ifdef ESP32
        portEXIT_CRITICAL(&mPendingIrqMux);
#endif
        return true;
    }
    if (mPendingIrqStickyMask != 0)
    {
        oIrq = mPendingIrqStickyMask;
        mPendingIrqStickyMask = 0;
#ifdef ESP32
        portEXIT_CRITICAL(&mPendingIrqMux);
#endif
        return true;
    }
#ifdef ESP32
    portEXIT_CRITICAL(&mPendingIrqMux);
#endif
    return false;
}

bool RadioSX1262::consumeIrqStatus(uint16_t &oIrq, bool &oNeedsClear, bool iBlocking)
{
    oNeedsClear = false;
    if (dequeuePendingIrq(oIrq))
        return true;

    if (mIrqFired)
    {
        if (!getIrqStatus(oIrq, iBlocking) || oIrq == 0)
            return false;
        mIrqFired = false;
        noteIrqStatus(oIrq, false);
        oNeedsClear = true;
        return true;
    }

    if (!getIrqStatus(oIrq, iBlocking) || oIrq == 0)
        return false;
    noteIrqStatus(oIrq, true);
    oNeedsClear = true;
    return true;
}

bool RadioSX1262::sendCommand(uint8_t iOpcode, const uint8_t *iParams, uint8_t iLen, bool iBlocking)
{
    if (iLen > 0 && iParams == nullptr)
        return false;
#ifdef ESP32
    if (!lockChip(iBlocking))
        return false;
    const bool lHadTraceOp = (mTxBusyTraceOp != TxBusyTraceOp::None);
    if (!lHadTraceOp && mTxBusyTraceActive)
        setTxBusyTraceOp((iOpcode == SX1262_CMD_SET_RX) ? TxBusyTraceOp::SetRx : TxBusyTraceOp::Other);
    if (!waitBusy(iBlocking))
    {
        if (!lHadTraceOp)
            clearTxBusyTraceOp();
        unlockChip();
        return false;
    }
    digitalWrite(mCsPin, LOW);
    SPI.beginTransaction(sSx1262SpiSettings);
    SPI.transfer(iOpcode);
    for (uint8_t i = 0; i < iLen; i++)
        SPI.transfer(iParams[i]);
    SPI.endTransaction();
    digitalWrite(mCsPin, HIGH);
    if (iOpcode != SX1262_CMD_SET_SLEEP)
    {
        const bool lOk = waitBusy(iBlocking);
        if (!lHadTraceOp)
            clearTxBusyTraceOp();
        unlockChip();
        return lOk;
    }
    if (!lHadTraceOp)
        clearTxBusyTraceOp();
    unlockChip();
#endif
    return true;
}

bool RadioSX1262::readCommand(uint8_t iOpcode, uint8_t *oData, uint8_t iLen, bool iBlocking)
{
    if (iLen > 0 && oData == nullptr)
        return false;
#ifdef ESP32
    if (!lockChip(iBlocking))
        return false;
    const bool lHadTraceOp = (mTxBusyTraceOp != TxBusyTraceOp::None);
    if (!lHadTraceOp && mTxBusyTraceActive)
        setTxBusyTraceOp((iOpcode == SX1262_CMD_GET_STATUS) ? TxBusyTraceOp::GetStatus : TxBusyTraceOp::Other);
    if (!waitBusy(iBlocking))
    {
        for (uint8_t i = 0; i < iLen; i++)
            oData[i] = 0;
        if (!lHadTraceOp)
            clearTxBusyTraceOp();
        unlockChip();
        return false;
    }
    digitalWrite(mCsPin, LOW);
    SPI.beginTransaction(sSx1262SpiSettings);
    SPI.transfer(iOpcode);
    SPI.transfer(0x00);
    for (uint8_t i = 0; i < iLen; i++)
        oData[i] = SPI.transfer(0x00);
    SPI.endTransaction();
    digitalWrite(mCsPin, HIGH);
    const bool lOk = waitBusy(iBlocking);
    if (!lHadTraceOp)
        clearTxBusyTraceOp();
    unlockChip();
    return lOk;
#endif
    return true;
}

bool RadioSX1262::writeRegister(uint16_t iAddr, uint8_t iVal, bool iBlocking)
{
#ifdef ESP32
    if (!lockChip(iBlocking))
        return false;
    if (!waitBusy(iBlocking))
    {
        unlockChip();
        return false;
    }
    digitalWrite(mCsPin, LOW);
    SPI.beginTransaction(sSx1262SpiSettings);
    SPI.transfer(SX1262_CMD_WRITE_REGISTER);
    SPI.transfer((iAddr >> 8) & 0xFF);
    SPI.transfer(iAddr & 0xFF);
    SPI.transfer(iVal);
    SPI.endTransaction();
    digitalWrite(mCsPin, HIGH);
    const bool lOk = waitBusy(iBlocking);
    unlockChip();
    return lOk;
#endif
    return true;
}

bool RadioSX1262::readRegister(uint16_t iAddr, uint8_t &oVal, bool iBlocking)
{
    oVal = 0;
#ifdef ESP32
    if (!lockChip(iBlocking))
        return false;
    if (!waitBusy(iBlocking))
    {
        unlockChip();
        return false;
    }
    digitalWrite(mCsPin, LOW);
    SPI.beginTransaction(sSx1262SpiSettings);
    SPI.transfer(SX1262_CMD_READ_REGISTER);
    SPI.transfer((iAddr >> 8) & 0xFF);
    SPI.transfer(iAddr & 0xFF);
    SPI.transfer(0x00);
    oVal = SPI.transfer(0x00);
    SPI.endTransaction();
    digitalWrite(mCsPin, HIGH);
    const bool lOk = waitBusy(iBlocking);
    unlockChip();
    return lOk;
#endif
    return true;
}

bool RadioSX1262::writeRegisters(uint16_t iAddr, const uint8_t *iData, uint8_t iLen, bool iBlocking)
{
    if (iLen > 0 && iData == nullptr)
        return false;
#ifdef ESP32
    if (!lockChip(iBlocking))
        return false;
    if (!waitBusy(iBlocking))
    {
        unlockChip();
        return false;
    }
    digitalWrite(mCsPin, LOW);
    SPI.beginTransaction(sSx1262SpiSettings);
    SPI.transfer(SX1262_CMD_WRITE_REGISTER);
    SPI.transfer((iAddr >> 8) & 0xFF);
    SPI.transfer(iAddr & 0xFF);
    for (uint8_t i = 0; i < iLen; i++)
        SPI.transfer(iData[i]);
    SPI.endTransaction();
    digitalWrite(mCsPin, HIGH);
    const bool lOk = waitBusy(iBlocking);
    unlockChip();
    return lOk;
#endif
    return true;
}

bool RadioSX1262::writeBuffer(uint8_t iOffset, const uint8_t *iData, uint8_t iLen, bool iBlocking)
{
    if (iLen > 0 && iData == nullptr)
        return false;
#ifdef ESP32
    if (!lockChip(iBlocking))
        return false;
    if (!waitBusy(iBlocking))
    {
        unlockChip();
        return false;
    }
    digitalWrite(mCsPin, LOW);
    SPI.beginTransaction(sSx1262SpiSettings);
    SPI.transfer(SX1262_CMD_WRITE_BUFFER);
    SPI.transfer(iOffset);
    for (uint8_t i = 0; i < iLen; i++)
        SPI.transfer(iData[i]);
    SPI.endTransaction();
    digitalWrite(mCsPin, HIGH);
    const bool lOk = waitBusy(iBlocking);
    unlockChip();
    return lOk;
#endif
    return true;
}

bool RadioSX1262::readBuffer(uint8_t iOffset, uint8_t *oData, uint8_t iMaxLen, bool iBlocking)
{
    if (iMaxLen > 0 && oData == nullptr)
        return false;
#ifdef ESP32
    if (!lockChip(iBlocking))
        return false;
    if (!waitBusy(iBlocking))
    {
        unlockChip();
        return false;
    }
    digitalWrite(mCsPin, LOW);
    SPI.beginTransaction(sSx1262SpiSettings);
    SPI.transfer(SX1262_CMD_READ_BUFFER);
    SPI.transfer(iOffset);
    SPI.transfer(0x00);
    for (uint8_t i = 0; i < iMaxLen; i++)
        oData[i] = SPI.transfer(0x00);
    SPI.endTransaction();
    digitalWrite(mCsPin, HIGH);
    const bool lOk = waitBusy(iBlocking);
    unlockChip();
    return lOk;
#endif
    return true;
}

void RadioSX1262::calibrate()
{
    // Image calibration for 868 MHz band
    uint8_t lCalFreq[2] = {SX1262_IMAGE_CAL_FREQ1_868, SX1262_IMAGE_CAL_FREQ2_868};
    sendCommand(SX1262_CMD_CALIBRATE_IMAGE, lCalFreq, 2);

    // Full calibration (all blocks)
    uint8_t lCalMask = SX1262_CALIBRATE_ALL;
    sendCommand(SX1262_CMD_CALIBRATE, &lCalMask, 1);

#ifdef ESP32
    // BUSY clears before the analog side is fully settled on this board.
    delay(10);
    waitBusy(true);
#endif

    uint16_t lDeviceErrors = 0;
    readDeviceErrors(lDeviceErrors, true);
}

bool RadioSX1262::getIrqStatus(uint16_t &oIrq, bool iBlocking)
{
    uint8_t lData[2] = {0};
    oIrq = 0;
#ifdef ESP32
    if (!lockChip(iBlocking))
        return false;
    setTxBusyTraceOp(TxBusyTraceOp::GetIrqStatus);
    if (!waitBusy(iBlocking))
    {
        clearTxBusyTraceOp();
        unlockChip();
        return false;
    }
    digitalWrite(mCsPin, LOW);
    SPI.beginTransaction(sSx1262SpiSettings);
    SPI.transfer(SX1262_CMD_GET_IRQ_STATUS);
    SPI.transfer(0x00);
    lData[0] = SPI.transfer(0x00);
    lData[1] = SPI.transfer(0x00);
    SPI.endTransaction();
    digitalWrite(mCsPin, HIGH);
    if (!waitBusy(iBlocking))
    {
        clearTxBusyTraceOp();
        unlockChip();
        return false;
    }
    clearTxBusyTraceOp();
    unlockChip();
#else
    if (!readCommand(SX1262_CMD_GET_IRQ_STATUS, lData, 2, iBlocking))
        return false;
#endif
    oIrq = ((uint16_t)lData[0] << 8) | lData[1];
    return true;
}

bool RadioSX1262::clearIrqStatus(uint16_t iMask, bool iBlocking)
{
    uint8_t lParams[2];
    lParams[0] = (iMask >> 8) & 0xFF;
    lParams[1] = iMask & 0xFF;
    setTxBusyTraceOp(TxBusyTraceOp::ClearIrqStatus);
    const bool lOk = sendCommand(SX1262_CMD_CLEAR_IRQ_STATUS, lParams, 2, iBlocking);
    clearTxBusyTraceOp();
    return lOk;
}

bool RadioSX1262::configureTxIrqs(bool iBlocking)
{
    uint8_t lDioParams[8];
    uint16_t lIrqMask = SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT;
    uint16_t lDio1Mask = SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT;
    uint16_t lDio2Mask = 0x0000;
    uint16_t lDio3Mask = 0x0000;
    lDioParams[0] = (lIrqMask >> 8) & 0xFF;
    lDioParams[1] = lIrqMask & 0xFF;
    lDioParams[2] = (lDio1Mask >> 8) & 0xFF;
    lDioParams[3] = lDio1Mask & 0xFF;
    lDioParams[4] = (lDio2Mask >> 8) & 0xFF;
    lDioParams[5] = lDio2Mask & 0xFF;
    lDioParams[6] = (lDio3Mask >> 8) & 0xFF;
    lDioParams[7] = lDio3Mask & 0xFF;
    return sendCommand(SX1262_CMD_SET_DIO_IRQ_PARAMS, lDioParams, 8, iBlocking);
}

bool RadioSX1262::configureRxIrqs(bool iBlocking)
{
    uint8_t lDioParams[8];
    // Keep PreambleDetected visible through GetIrqStatus, but do not route it to
    // DIO1: it is not terminal and waking on it can tear down an active frame.
    uint16_t lIrqMask = SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_DONE |
                        SX1262_IRQ_PREAMBLE_DETECTED | SX1262_IRQ_SYNC_WORD_VALID |
                        SX1262_IRQ_CRC_ERR;
    uint16_t lDio1Mask = SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_DONE |
                         SX1262_IRQ_SYNC_WORD_VALID | SX1262_IRQ_CRC_ERR;
    uint16_t lDio2Mask = 0x0000;
    uint16_t lDio3Mask = 0x0000;
    lDioParams[0] = (lIrqMask >> 8) & 0xFF;
    lDioParams[1] = lIrqMask & 0xFF;
    lDioParams[2] = (lDio1Mask >> 8) & 0xFF;
    lDioParams[3] = lDio1Mask & 0xFF;
    lDioParams[4] = (lDio2Mask >> 8) & 0xFF;
    lDioParams[5] = lDio2Mask & 0xFF;
    lDioParams[6] = (lDio3Mask >> 8) & 0xFF;
    lDioParams[7] = lDio3Mask & 0xFF;
    return sendCommand(SX1262_CMD_SET_DIO_IRQ_PARAMS, lDioParams, 8, iBlocking);
}

void RadioSX1262::setRfSwitchIdle()
{
#ifdef ESP32
    if (mRfSwitchPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchPin, HIGH);
    if (mRfSwitchRxPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchRxPin, LOW);
    if (mRfSwitchTxPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchTxPin, LOW);
#endif
}

void RadioSX1262::setRfSwitchRx()
{
#ifdef ESP32
    if (mRfSwitchPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchPin, HIGH);
    if (mRfSwitchRxPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchRxPin, HIGH);
    if (mRfSwitchTxPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchTxPin, LOW);
#endif
}

void RadioSX1262::setRfSwitchTx()
{
#ifdef ESP32
    if (mRfSwitchPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchPin, LOW);
    if (mRfSwitchRxPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchRxPin, LOW);
    if (mRfSwitchTxPin != RADIO_PIN_NOT_CONNECTED)
        digitalWrite(mRfSwitchTxPin, HIGH);
#endif
}

uint8_t RadioSX1262::rfSwitchConfigFlags() const
{
    uint8_t lFlags = 0;
    if (mRfSwitchPin != RADIO_PIN_NOT_CONNECTED)
        lFlags |= RADIO_SX1262_RF_SWITCH_SHARED;
    if (mUseDio2RfSwitch)
        lFlags |= RADIO_SX1262_RF_SWITCH_DIO2;
    if (mRfSwitchRxPin != RADIO_PIN_NOT_CONNECTED)
        lFlags |= RADIO_SX1262_RF_SWITCH_RX_EN;
    if (mRfSwitchTxPin != RADIO_PIN_NOT_CONNECTED)
        lFlags |= RADIO_SX1262_RF_SWITCH_TX_EN;
    return lFlags;
}
