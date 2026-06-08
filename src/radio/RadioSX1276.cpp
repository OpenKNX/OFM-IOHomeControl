#include "RadioSX1276.h"
#include "sx1276Regs-Fsk.h"
#include "../protocol/IoHomeCommands.h"
#include "../protocol/IoHomeFrame.h"
#include "OpenKNX.h"

#ifdef ESP32
#include <SPI.h>
#include <Arduino.h>
#else
// Stubs for non-ESP32 compilation
#include <stdint.h>
#include <string.h>
static void pinMode(uint8_t, uint8_t) {}
static void digitalWrite(uint8_t, uint8_t) {}
static int digitalRead(uint8_t) { return 0; }
static void delay(unsigned long) {}
static unsigned long millis() { return 0; }
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
#endif

static constexpr uint8_t kMaxSx1276PayloadLen = IOHC_FRAME_MAX_SIZE + IOHC_CRC_SIZE;

// SX1276 operating modes (RegOpMode)
#define RF_OPMODE_SLEEP 0x00
#define RF_OPMODE_STANDBY 0x01
#define RF_OPMODE_FSTX 0x02
#define RF_OPMODE_TX 0x03
#define RF_OPMODE_FSRX 0x04
#define RF_OPMODE_RX 0x05

// SX1276 RegOpMode flags
#define RF_OPMODE_LONGRANGEMODE_LORA 0x80
#define RF_OPMODE_LOWFREQUENCYMODE_ON 0x08

// SPI read/write masks
#define SPI_WRITE_MASK 0x80
#define SPI_READ_MASK 0x7F

RadioSX1276::RadioSX1276()
    : mCsPin(0), mResetPin(0), mDio0Pin(0), mDio4Pin(PIN_NOT_CONNECTED),
      mInitialized(false), mState(RadioState::Idle),
      mLastRssi(0), mCurrentFreq(0), mEms2Mode(false),
      mTxStartCount(0), mTxDoneCount(0), mRxStartCount(0), mIrqCount(0),
      mLastIrqStatus(0), mLastOpStatusBefore(0), mLastOpStatusAfter(0),
      mLastTxSetStatus(0), mLastTxIrqImmediate(0), mDio0Fired(false)
{
}

#ifdef ESP32
void IRAM_ATTR RadioSX1276::dio0Isr(void *arg)
{
    RadioSX1276 *self = static_cast<RadioSX1276 *>(arg);
    self->mDio0Fired = true;
    self->mIrqCount++;
}
#endif

void RadioSX1276::init(uint8_t iCsPin, uint8_t iResetPin, uint8_t iDio0Pin, uint8_t iDio4Pin)
{
    mCsPin = iCsPin;
    mResetPin = iResetPin;
    mDio0Pin = iDio0Pin;
    mDio4Pin = iDio4Pin;

#ifdef ESP32
    // Configure pins
    pinMode(mCsPin, OUTPUT);
    digitalWrite(mCsPin, HIGH);
    pinMode(mResetPin, OUTPUT);
    pinMode(mDio0Pin, INPUT);
    if (mDio4Pin != PIN_NOT_CONNECTED)
        pinMode(mDio4Pin, INPUT);

    // Initialize SPI with explicit board pins to avoid default-bus pin reconfiguration.
    SPI.begin(IOHC_SPI_SCK, IOHC_SPI_MISO, IOHC_SPI_MOSI, mCsPin);

    // Attach DIO0 interrupt for packet detection
    attachInterruptArg(digitalPinToInterrupt(mDio0Pin), dio0Isr, this, RISING);
#endif

    // Hardware reset
    resetChip();

    // Verify chip presence by reading version register
    uint8_t lVersion = readRegister(REG_VERSION);
    if (lVersion == 0x12) // SX1276
    {
        mInitialized = true;
    }
    else
    {
        mInitialized = false;
        return;
    }

    // Calibrate first (before register configuration, per nicolas5000)
    calibrate();

    // Configure for io-homecontrol FSK mode
    configure();
}

void RadioSX1276::resetChip()
{
#ifdef ESP32
    digitalWrite(mResetPin, LOW);
    delay(1);
    digitalWrite(mResetPin, HIGH);
    delay(10);
#endif
}

std::string RadioSX1276::logPrefix()
{
    return openknx.logger.buildPrefix("RadioSX1276", 0);
}

RadioError RadioSX1276::configure()
{
    if (!mInitialized)
        return RadioError::NotInitialized;

    // Enter standby mode for configuration
    setMode(RF_OPMODE_STANDBY);

    // Set FSK mode (not LoRa) and force HF band.
    // Bit 7 = LoRa mode, bit 3 = LowFrequencyModeOn.
    // io-homecontrol runs at 868/869 MHz, so LowFrequencyModeOn must be 0.
    // Keeping bit 3 set made RegOpMode read back as 0x09 (standby + LF),
    // which can prevent the chip from actually entering TX at 868 MHz.
    uint8_t lOpMode = readRegister(REG_OPMODE);
    lOpMode &= ~RF_OPMODE_LONGRANGEMODE_LORA;
    lOpMode &= ~RF_OPMODE_LOWFREQUENCYMODE_ON;
    writeRegister(REG_OPMODE, lOpMode);

    // Disable clock output (save power)
    writeRegister(REG_OSC, RF_OSC_CLKOUT_OFF);

    // Bit rate: 38400 bps
    // BitRate = FXOSC / BitRateReg = 32000000 / 833 = 38400
    uint16_t lBitRate = 833;
    writeRegister(REG_BITRATEMSB, (lBitRate >> 8) & 0xFF);
    writeRegister(REG_BITRATELSB, lBitRate & 0xFF);

    // Frequency deviation: 19200 Hz
    // Fdev = Fstep * FdevReg = 61.035 * 314 ≈ 19165 Hz (per nicolas5000)
    uint16_t lFdev = 314;
    writeRegister(REG_FDEVMSB, (lFdev >> 8) & 0xFF);
    writeRegister(REG_FDEVLSB, lFdev & 0xFF);

    // PA ramp: no shaping, 12 us ramp (per nicolas5000)
    writeRegister(REG_PARAMP, RF_PARAMP_MODULATIONSHAPING_00 | RF_PARAMP_0012_US);

    // Over-current protection: 240 mA (required for PA_BOOST at higher power)
    writeRegister(REG_OCP, RF_OCP_ON | RF_OCP_TRIM_240_MA);

    // LNA: maximum gain (G1) + boost
    writeRegister(REG_LNA, 0x23);

    // RX bandwidth: 250 kHz (Mant=00, Exp=1)
    writeRegister(REG_RXBW, 0x01);

    // AFC bandwidth: 250 kHz (match RX bandwidth, per nicolas5000)
    writeRegister(REG_AFCBW, 0x01);

    // RSSI smoothing: 8-sample averaging
    writeRegister(REG_RSSICONFIG, 0x02);

    // RX config: RestartRxOnCollision ON, AFC auto ON, AGC auto ON,
    // trigger on preamble detect (per nicolas5000)
    writeRegister(REG_RXCONFIG, RF_RXCONFIG_RESTARTRXONCOLLISION_ON |
                                    RF_RXCONFIG_AFCAUTO_ON |
                                    RF_RXCONFIG_AGCAUTO_ON |
                                    RF_RXCONFIG_RXTRIGER_PREAMBLEDETECT);

    // AFC auto-clear between packets
    writeRegister(REG_AFCFEI, RF_AFCFEI_AFCAUTOCLEAR_ON);

    // Preamble detector: ON, 2-byte window, tolerance=10 bits
    writeRegister(REG_PREAMBLEDETECT, 0xAA);

    // Preamble: 8 symbols default (SHORT_PREAMBLE_LENGTH)
    writeRegister(REG_PREAMBLEMSB, 0x00);
    writeRegister(REG_PREAMBLELSB, 0x08);

    // Sync word: 0x55 0xFF 0x33 (3-byte on-air sync, per nicolas5000)
    // With IoHomeOn=1, SyncSize = number of sync bytes (not SyncSize+1)
    // So SyncSize=2 means 2 bytes (0xFF 0x33); the 0x55 is the last preamble byte
    writeRegister(REG_SYNCCONFIG, 0x52); // AutoRestart=WaitPLL_Off, Sync on, SyncSize=2
    writeRegister(REG_SYNCVALUE1, IOHC_SYNC_WORD[0]);
    writeRegister(REG_SYNCVALUE2, IOHC_SYNC_WORD[1]);
    writeRegister(REG_SYNCVALUE3, IOHC_SYNC_WORD[2]);

    // Packet config 1: variable length, CRC on, auto-clear on bad CRC, CCITT type
    writeRegister(REG_PACKETCONFIG1, 0x90);

    // Packet config 2: io-homecontrol mode ON + PowerFrame
    // IoHomeOn changes CRC to include length byte and adjusts sync word size interpretation
    // PowerFrame prevents false preamble re-detection during TX ramp-down
    writeRegister(REG_PACKETCONFIG2, RF_PACKETCONFIG2_DATAMODE_PACKET |
                                         RF_PACKETCONFIG2_IOHOME_ON |
                                         RF_PACKETCONFIG2_IOHOME_POWERFRAME);

    // Max payload length: 255 (avoid blocking PayloadReady, per nicolas5000)
    writeRegister(REG_PAYLOADLENGTH, 0xFF);

    // FIFO threshold: TX start as soon as FIFO is not empty (per nicolas5000)
    writeRegister(REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTARTCONDITION_FIFONOTEMPTY);

    // DIO0 mapping: PacketSent in TX, PayloadReady in RX
    writeRegister(REG_DIOMAPPING1, 0x00);
    // DIO4 mapping: PreambleDetect (only if DIO4 pin is connected)
    if (mDio4Pin != PIN_NOT_CONNECTED)
        writeRegister(REG_DIOMAPPING2, 0x01);

    // Set default frequency (CH2 = 868.95 MHz, the shared 1W/2W channel)
    setFrequency(IOHC_FREQ_2);

    // Set default power
    setOutputPower(14);

    // Enable fast frequency hopping (REG_PLLHOP: FastHopOn = 1)
    uint8_t lPllHop = readRegister(REG_PLLHOP);
    writeRegister(REG_PLLHOP, lPllHop | RF_PLLHOP_FASTHOP_ON);

    return RadioError::None;
}

RadioError RadioSX1276::setFrequency(uint32_t iFreqHz)
{
    if (!mInitialized)
        return RadioError::NotInitialized;

    // Frf = Fstep * FrfReg, Fstep = FXOSC / 2^19 = 32000000 / 524288 ≈ 61.035 Hz
    uint32_t lFrf = (uint32_t)((double)iFreqHz / 61.03515625);
    writeRegister(REG_FRFMSB, (lFrf >> 16) & 0xFF);
    writeRegister(REG_FRFMID, (lFrf >> 8) & 0xFF);
    writeRegister(REG_FRFLSB, lFrf & 0xFF);
    mCurrentFreq = iFreqHz;

    return RadioError::None;
}

RadioError RadioSX1276::setOutputPower(uint8_t iPower)
{
    if (!mInitialized)
        return RadioError::NotInitialized;
    if (iPower < 2 || iPower > 20)
        return RadioError::InvalidParam;

    // PA_BOOST pin: Pout = 17 - (15 - OutputPower) = OutputPower + 2
    uint8_t lPaConfig = 0x80; // PA_BOOST selected
    if (iPower <= 17)
    {
        lPaConfig |= (iPower - 2);
    }
    else
    {
        // +20 dBm mode: enable via PADAC register
        lPaConfig |= 15;
        writeRegister(REG_PADAC, 0x87); // enable +20 dBm
    }

    writeRegister(REG_PACONFIG, lPaConfig);
    return RadioError::None;
}

RadioError RadioSX1276::setPreambleLength(uint16_t iSymbols)
{
    if (!mInitialized)
        return RadioError::NotInitialized;

    writeRegister(REG_PREAMBLEMSB, (iSymbols >> 8) & 0xFF);
    writeRegister(REG_PREAMBLELSB, iSymbols & 0xFF);
    return RadioError::None;
}

RadioError RadioSX1276::startTransmit(const uint8_t *iData, uint8_t iLen)
{
    if (iLen == 0 || iLen > kMaxSx1276PayloadLen || iData == nullptr)
        return RadioError::InvalidParam;
    if (!mInitialized)
        return RadioError::NotInitialized;
    if (mState == RadioState::Transmitting)
        return RadioError::Busy;

    // Enter standby to load FIFO
    setMode(RF_OPMODE_STANDBY);

    // Keep packet mode configured for the maximum variable-length frame size.
    // In io-homecontrol mode (IoHomeOn=1) the SX1276 handles the length byte
    // internally. Leaving PayloadLength at 0xFF avoids blocking larger 1W frames
    // such as SendKey1W (35 bytes).
    writeRegister(REG_PAYLOADLENGTH, 0xFF);

    // Write data to FIFO (with IoHomeOn=1, radio handles length byte internally)
    writeFifo(iData, iLen);

    // Start TX
    mDio0Fired = false; // clear stale interrupt before TX
    setMode(RF_OPMODE_TX);
    mState = RadioState::Transmitting;
    mTxStartCount++;
    mLastTxSetStatus = mLastOpStatusAfter;
    mLastTxIrqImmediate = readIrqStatus();
    mLastIrqStatus = mLastTxIrqImmediate;

    return RadioError::None;
}

RadioError RadioSX1276::startReceive()
{
    if (!mInitialized)
        return RadioError::NotInitialized;

    // Restore the variable-length RX ceiling after TX-specific payload lengths.
    writeRegister(REG_PAYLOADLENGTH, 0xFF);

    mDio0Fired = false; // clear stale interrupt before RX
    setMode(RF_OPMODE_RX);
    mState = RadioState::Receiving;
    mRxStartCount++;
    mLastIrqStatus = readIrqStatus();

    return RadioError::None;
}

bool RadioSX1276::isTxDone()
{
    if (mState != RadioState::Transmitting)
        return false;

    const uint16_t lIrqStatus = readIrqStatus();
    if (mDio0Fired)
    {
        mDio0Fired = false;
        mLastIrqStatus = lIrqStatus;
        // TX complete, return to standby
        setMode(RF_OPMODE_STANDBY);
        mState = RadioState::Idle;
        mTxDoneCount++;
        return true;
    }

    if ((lIrqStatus & RF_IRQFLAGS2_PACKETSENT) != 0)
    {
        mLastIrqStatus = lIrqStatus;
        setMode(RF_OPMODE_STANDBY);
        mState = RadioState::Idle;
        mTxDoneCount++;
        return true;
    }

    return false;
}

bool RadioSX1276::isPacketAvailable()
{
    if (mState != RadioState::Receiving)
        return false;

    if (mDio0Fired)
    {
        mDio0Fired = false;
        mLastIrqStatus = readIrqStatus();
        return true;
    }
    return false;
}

bool RadioSX1276::isPreambleDetected() const
{
#ifdef ESP32
    if (mDio4Pin == PIN_NOT_CONNECTED)
        return false;
    return digitalRead(mDio4Pin);
#else
    return false;
#endif
}

uint8_t RadioSX1276::readPacket(uint8_t *oBuffer, uint8_t iMaxLen)
{
    if (!mInitialized)
        return 0;

    // Read RSSI
    mLastRssi = -(readRegister(REG_RSSIVALUE) / 2);

    // Read packet from FIFO (with IoHomeOn=1, read until FIFO empty)
    uint8_t lLen = readFifo(oBuffer, iMaxLen);

    // Restart RX
    startReceive();

    return lLen;
}

int16_t RadioSX1276::lastRssi() const
{
    return mLastRssi;
}

bool RadioSX1276::currentRssi(int16_t &oRssi)
{
    oRssi = 0;
    if (!mInitialized || mState != RadioState::Receiving)
        return false;

    oRssi = -(readRegister(REG_RSSIVALUE) / 2);
    return true;
}

void RadioSX1276::sleep()
{
    setMode(RF_OPMODE_SLEEP);
    mState = RadioState::Sleep;
}

void RadioSX1276::standby()
{
    setMode(RF_OPMODE_STANDBY);
    mState = RadioState::Idle;
}

RadioState RadioSX1276::state() const
{
    return mState;
}

bool RadioSX1276::isInitialized() const
{
    return mInitialized;
}

uint32_t RadioSX1276::txStartCount() const
{
    return mTxStartCount;
}

uint32_t RadioSX1276::txDoneCount() const
{
    return mTxDoneCount;
}

uint32_t RadioSX1276::rxStartCount() const
{
    return mRxStartCount;
}

uint32_t RadioSX1276::irqCount() const
{
    return mIrqCount;
}

uint16_t RadioSX1276::lastIrqStatus() const
{
    return mLastIrqStatus;
}

uint8_t RadioSX1276::lastOpStatusBefore() const
{
    return mLastOpStatusBefore;
}

uint8_t RadioSX1276::lastOpStatusAfter() const
{
    return mLastOpStatusAfter;
}

uint8_t RadioSX1276::lastTxSetStatus() const
{
    return mLastTxSetStatus;
}

uint16_t RadioSX1276::lastTxIrqImmediate() const
{
    return mLastTxIrqImmediate;
}

void RadioSX1276::configureEms2Mode()
{
    setMode(RF_OPMODE_STANDBY);

    // EMS2 uses 2-byte sync word {0x2D, 0xD4}
    writeRegister(REG_SYNCCONFIG, 0x51); // AutoRestart=WaitPLL_Off, Sync on, 2 bytes
    writeRegister(REG_SYNCVALUE1, IOHC_EMS2_SYNC_WORD[0]);
    writeRegister(REG_SYNCVALUE2, IOHC_EMS2_SYNC_WORD[1]);

    // EMS2 uses 868.95 MHz
    setFrequency(IOHC_EMS2_FREQ);

    mEms2Mode = true;
}

void RadioSX1276::configureStandardMode()
{
    setMode(RF_OPMODE_STANDBY);

    // Restore 3-byte io-homecontrol sync word
    writeRegister(REG_SYNCCONFIG, 0x52); // AutoRestart=WaitPLL_Off, Sync on, 3 bytes
    writeRegister(REG_SYNCVALUE1, IOHC_SYNC_WORD[0]);
    writeRegister(REG_SYNCVALUE2, IOHC_SYNC_WORD[1]);
    writeRegister(REG_SYNCVALUE3, IOHC_SYNC_WORD[2]);

    // Restore default frequency
    setFrequency(IOHC_FREQ_1);

    mEms2Mode = false;
}

RadioError RadioSX1276::sendEms2Wake()
{
    if (!mInitialized)
        return RadioError::NotInitialized;

    // Set frequency deviation to 0 (unmodulated carrier)
    writeRegister(REG_FDEVMSB, 0x00);
    writeRegister(REG_FDEVLSB, 0x00);

    // Set EMS2 frequency
    setFrequency(IOHC_EMS2_FREQ);

    // Transmit unmodulated carrier for IOHC_EMS2_WAKE_DURATION_MS
    setMode(RF_OPMODE_TX);
    mState = RadioState::Transmitting;

#ifdef ESP32
    delay(IOHC_EMS2_WAKE_DURATION_MS);
#endif

    // Stop TX
    setMode(RF_OPMODE_STANDBY);
    mState = RadioState::Idle;

    // Restore frequency deviation: 19200 Hz
    uint16_t lFdev = 314;
    writeRegister(REG_FDEVMSB, (lFdev >> 8) & 0xFF);
    writeRegister(REG_FDEVLSB, lFdev & 0xFF);

    return RadioError::None;
}

// --- SPI register access ---

uint8_t RadioSX1276::readRegister(uint8_t iAddr)
{
#ifdef ESP32
    uint8_t lVal;
    digitalWrite(mCsPin, LOW);
    SPI.transfer(iAddr & SPI_READ_MASK);
    lVal = SPI.transfer(0x00);
    digitalWrite(mCsPin, HIGH);
    return lVal;
#else
    return 0;
#endif
}

void RadioSX1276::writeRegister(uint8_t iAddr, uint8_t iVal)
{
#ifdef ESP32
    digitalWrite(mCsPin, LOW);
    SPI.transfer(iAddr | SPI_WRITE_MASK);
    SPI.transfer(iVal);
    digitalWrite(mCsPin, HIGH);
#endif
}

uint8_t RadioSX1276::debugReadRegister(uint8_t iAddr)
{
#ifdef ESP32
    return readRegister(iAddr);
#else
    return 0;
#endif
}

void RadioSX1276::debugWriteRegister(uint8_t iAddr, uint8_t iVal)
{
#ifdef ESP32
    writeRegister(iAddr, iVal);
#endif
}

uint16_t RadioSX1276::debugReadDeviceErrors()
{
    return synthesizeDeviceErrors(readIrqStatus());
}

void RadioSX1276::writeFifo(const uint8_t *iData, uint8_t iLen)
{
    if (iLen == 0 || iData == nullptr)
        return;

    // With IoHomeOn=1, the radio handles the length byte internally.
    // Write raw payload data to FIFO (per nicolas5000).
#ifdef ESP32
    digitalWrite(mCsPin, LOW);
    SPI.transfer(REG_FIFO | SPI_WRITE_MASK);
    for (uint8_t i = 0; i < iLen; i++)
        SPI.transfer(iData[i]);
    digitalWrite(mCsPin, HIGH);
#endif
}

uint8_t RadioSX1276::readFifo(uint8_t *oData, uint8_t iMaxLen)
{
    if (iMaxLen == 0 || oData == nullptr)
        return 0;

    // With IoHomeOn=1, read bytes until FIFO is empty (per nicolas5000).
    // The radio strips the length byte; we get raw payload.
#ifdef ESP32
    uint8_t lLen = 0;
    while (!(readRegister(REG_IRQFLAGS2) & RF_IRQFLAGS2_FIFOEMPTY))
    {
        if (lLen >= iMaxLen)
            break;
        oData[lLen++] = readRegister(REG_FIFO);
    }
    return lLen;
#else
    return 0;
#endif
}

uint16_t RadioSX1276::readIrqStatus()
{
    const uint8_t lIrq1 = readRegister(REG_IRQFLAGS1);
    const uint8_t lIrq2 = readRegister(REG_IRQFLAGS2);
    return (static_cast<uint16_t>(lIrq1) << 8) | lIrq2;
}

uint16_t RadioSX1276::synthesizeDeviceErrors(uint16_t iIrqStatus) const
{
    uint16_t lErrors = 0;
    if ((iIrqStatus & (static_cast<uint16_t>(RF_IRQFLAGS1_TIMEOUT) << 8)) != 0)
        lErrors |= static_cast<uint16_t>(RF_IRQFLAGS1_TIMEOUT) << 8;
    if ((iIrqStatus & RF_IRQFLAGS2_FIFOOVERRUN) != 0)
        lErrors |= RF_IRQFLAGS2_FIFOOVERRUN;
    if ((iIrqStatus & RF_IRQFLAGS2_PAYLOADREADY) != 0 &&
        (iIrqStatus & RF_IRQFLAGS2_CRCOK) == 0)
        lErrors |= 0x0004;
    return lErrors;
}

void RadioSX1276::setMode(uint8_t iMode)
{
    uint8_t lOpMode = readRegister(REG_OPMODE);
    mLastOpStatusBefore = lOpMode;

    // Keep modulation/settings bits, but always force HF FSK for io-homecontrol.
    // The previous implementation preserved bit 3 (LowFrequencyModeOn). With that
    // bit set, a TX request could read back as 0x09 (standby + LF) instead of TX.
    lOpMode &= ~RF_OPMODE_LONGRANGEMODE_LORA;
    lOpMode &= ~RF_OPMODE_LOWFREQUENCYMODE_ON;
    lOpMode = (lOpMode & 0xF8) | (iMode & 0x07);

    writeRegister(REG_OPMODE, lOpMode);
    mLastOpStatusAfter = readRegister(REG_OPMODE);
}

void RadioSX1276::calibrate()
{
    // Calibration sequence per nicolas5000:
    // 1. Save PA config and set to RFO/off (safety during calibration)
    uint8_t lSavedPaConfig = readRegister(REG_PACONFIG);
    writeRegister(REG_PACONFIG, 0x00); // PA off during calibration

    // 2. RC oscillator calibration
    writeRegister(REG_OSC, 0x08); // trigger RC calibration

    // 3. Image calibration at current frequency (LF band from reset default)
    uint8_t lReg = readRegister(REG_IMAGECAL);
    writeRegister(REG_IMAGECAL, lReg | 0x40); // Start ImageCal
#ifdef ESP32
    unsigned long lStart = millis();
    while ((readRegister(REG_IMAGECAL) & 0x20) && (millis() - lStart < 100))
        delay(1);
#endif

    // 4. Set frequency to 868 MHz (HF band) and calibrate again.
    // Force LowFrequencyModeOn=0 before the HF image calibration.
    setFrequency(868000000UL);
    uint8_t lOpMode = readRegister(REG_OPMODE);
    lOpMode &= ~RF_OPMODE_LOWFREQUENCYMODE_ON;
    lOpMode &= ~RF_OPMODE_LONGRANGEMODE_LORA;
    writeRegister(REG_OPMODE, lOpMode);
    lReg = readRegister(REG_IMAGECAL);
    writeRegister(REG_IMAGECAL, lReg | 0x40); // Start ImageCal for HF
#ifdef ESP32
    lStart = millis();
    while ((readRegister(REG_IMAGECAL) & 0x20) && (millis() - lStart < 100))
        delay(1);
#endif

    // 5. Restore PA config
    writeRegister(REG_PACONFIG, lSavedPaConfig);
}
