#pragma once
#include "RadioTypes.h"
#include <stdint.h>
#include <string>

#ifdef ESP32
#include <esp_attr.h>
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

// SX1276 radio driver for io-homecontrol
// Adapted from https://github.com/nicolas5000/io-rts-esp32 for Arduino SPI
// Non-blocking design: TX/RX state polled via DIO0 pin
//
// SX1276 DIO usage (FSK packet mode). Each DIO is software-mappable via
// RegDioMapping1/RegDioMapping2. This driver wires only DIO0 and DIO4; the
// remaining lines are optional status outputs that can also be polled via SPI.
//   DIO0 (used): PayloadReady -> RX packet-received interrupt
//   DIO4 (used): PreambleDetect -> early RX activity / optional IRQ
//   DIO1 (free): FIFO flow control - FifoLevel / FifoEmpty / FifoFull
//                (needed for streaming frames larger than the 64-byte FIFO)
//   DIO2 (free): FifoFull / RxReady(SyncAddress) / RxTimeout, or raw Data in
//                continuous mode. SyncAddress fires slightly before PayloadReady.
//   DIO3 (free): FifoEmpty / TxReady - precise TX timing and back-to-back TX
// For single io-homecontrol frames that fit in the FIFO, DIO0 + DIO4 suffice.

class RadioSX1276
{
public:
  static constexpr uint8_t PIN_NOT_CONNECTED = RADIO_PIN_NOT_CONNECTED;

  RadioSX1276();

  // Logging prefix required by log*P macros used in this class
  std::string logPrefix();

  void init(uint8_t iCsPin, uint8_t iResetPin, uint8_t iDio0Pin, uint8_t iDio4Pin = PIN_NOT_CONNECTED);
  RadioError configure();
  RadioError setFrequency(uint32_t iFreqHz);
  RadioError setOutputPower(uint8_t iPower);
  RadioError setPreambleLength(uint16_t iSymbols);
  RadioError startTransmit(const uint8_t *iData, uint8_t iLen);
  RadioError startReceive();
  bool isTxDone();
  bool isPacketAvailable();
  bool isPreambleDetected() const;
  uint8_t readPacket(uint8_t *oBuffer, uint8_t iMaxLen);
  int16_t lastRssi() const;
  bool currentRssi(int16_t &oRssi);
  void sleep();
  void standby();
  RadioState state() const;
  bool isInitialized() const;
  uint32_t txStartCount() const;
  uint32_t txDoneCount() const;
  uint32_t rxStartCount() const;
  uint32_t irqCount() const;
  uint32_t rxPayloadReadyPollCount() const;
  uint32_t rxFifoOverrunCount() const;
  uint32_t rxFifoEmptyCount() const;
  uint32_t rxCrcFailCount() const;
  uint8_t lastRxLen() const;
  uint16_t lastRxIrqStatus() const;
  uint16_t lastIrqStatus() const;
  uint8_t lastOpStatusBefore() const;
  uint8_t lastOpStatusAfter() const;
  uint8_t lastTxSetStatus() const;
  uint16_t lastTxIrqImmediate() const;

  /* Debug helpers (allow console to poke/read registers for diagnostics) */
  uint8_t debugReadRegister(uint8_t iAddr);
  void debugWriteRegister(uint8_t iAddr, uint8_t iVal);
  uint16_t debugReadDeviceErrors();

  // EMS2 protocol mode (building-wide wake)
  void configureEms2Mode();
  void configureStandardMode();
  RadioError sendEms2Wake();

private:
  uint8_t mCsPin;
  uint8_t mResetPin;
  uint8_t mDio0Pin;
  uint8_t mDio4Pin;
  bool mInitialized;
  RadioState mState;
  int16_t mLastRssi;
  uint32_t mCurrentFreq;
  uint32_t mPreviousStandardFrequency;
  bool mEms2Mode;
  uint32_t mTxStartCount;
  uint32_t mTxDoneCount;
  uint32_t mRxStartCount;
  uint32_t mIrqCount;
  uint32_t mRxPayloadReadyPollCount;
  uint32_t mRxFifoOverrunCount;
  uint32_t mRxFifoEmptyCount;
  uint32_t mRxCrcFailCount;
  uint8_t mLastRxLen;
  uint16_t mLastRxIrqStatus;
  uint16_t mLastIrqStatus;
  uint8_t mLastOpStatusBefore;
  uint8_t mLastOpStatusAfter;
  uint8_t mLastTxSetStatus;
  uint16_t mLastTxIrqImmediate;

  // Interrupt-driven DIO0 detection (ESP32 only)
  volatile bool mDio0Fired;
  static void IRAM_ATTR dio0Isr(void *arg);

  // SPI register access
  uint8_t readRegister(uint8_t iAddr);
  void writeRegister(uint8_t iAddr, uint8_t iVal);
  void writeFifo(const uint8_t *iData, uint8_t iLen);
  uint8_t readFifo(uint8_t *oData, uint8_t iMaxLen);
  uint16_t readIrqStatus();
  uint16_t synthesizeDeviceErrors(uint16_t iIrqStatus) const;

  // Internal mode switching
  void setMode(uint8_t iMode);

  // Hardware reset
  void resetChip();

  // Calibrate radio oscillator
  void calibrate();

  // Compact configuration read-back for pair/radio diagnostics.
  void logConfigDump();
};
