#pragma once
#include "RadioTypes.h"
#include "../protocol/IoHomeFrame.h"
#include <stdint.h>

#ifdef ESP32
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

// SX1262 radio driver for io-homecontrol
// Command-based SPI interface (opcode + params), requires BUSY pin
// Non-blocking design: TX/RX state polled via DIO1 IRQ flag
//
// SX1262 DIO usage. Unlike the SX1276, the SX1262 has no fixed-function DIO
// mapping table: any interrupt (TxDone, RxDone, PreambleDetected, Timeout, ...)
// can be routed to any DIO via the IRQ mask (SetDioIrqParams). DIO2 and DIO3
// double as dedicated control outputs. BUSY is mandatory and signals that the
// chip is processing a command.
//   DIO1 (used):     general IRQ line - TxDone / RxDone / Timeout via IRQ mask
//   BUSY (used):     command-busy flag, polled before each SPI transaction
//   DIO2 (optional): RF switch control (SetDIO2AsRfSwitchCtrl) - drives an
//                    external antenna TX/RX switch automatically
//   DIO3 (optional): TCXO supply control (SetDIO3AsTCXOCtrl) - powers/sequences
//                    a temperature-compensated oscillator
// This driver wires DIO1 + BUSY; DIO2/DIO3 control depends on board hardware.

enum class RadioSX1262InitError : uint8_t
{
  None = 0,
  BusyTimeout,
  StatusInvalid,
  ConfigureFailed
};

enum : uint8_t
{
  RADIO_SX1262_RF_SWITCH_SHARED = 0x01,
  RADIO_SX1262_RF_SWITCH_DIO2 = 0x02,
  RADIO_SX1262_RF_SWITCH_RX_EN = 0x04,
  RADIO_SX1262_RF_SWITCH_TX_EN = 0x08,
};

struct RadioSX1262TxBusyTrace
{
  uint32_t irqHits;
  uint32_t irqTotalUs;
  uint32_t statusHits;
  uint32_t statusTotalUs;
  uint32_t clearIrqHits;
  uint32_t clearIrqTotalUs;
  uint32_t standbyHits;
  uint32_t standbyTotalUs;
  uint32_t rxHits;
  uint32_t rxTotalUs;
  uint32_t otherHits;
  uint32_t otherTotalUs;
};

class RadioSX1262
{
public:
  RadioSX1262();

  void init(uint8_t iCsPin, uint8_t iResetPin, uint8_t iDio1Pin, uint8_t iBusyPin);
  RadioError configure();
  RadioError setFrequency(uint32_t iFreqHz);
  RadioError setFrequencyBlocking(uint32_t iFreqHz);
  RadioError setOutputPower(uint8_t iPower);
  RadioError setPreambleLength(uint16_t iSymbols);
  RadioError setPreambleLengthBlocking(uint16_t iSymbols);
  RadioError startTransmit(const uint8_t *iData, uint8_t iLen);
  RadioError startTransmitBlocking(const uint8_t *iData, uint8_t iLen);
  RadioError startReceive();
  RadioError startReceiveBlocking();
  bool isTxDone();
  bool isTxDoneBlocking();
  bool isPacketAvailable();
  bool isPreambleDetected() const;
  bool isSyncDetected() const;
  uint8_t readPacket(uint8_t *oBuffer, uint8_t iMaxLen);
  int16_t lastRssi() const;
  bool currentRssi(int16_t &oRssi);
  void sleep();
  void standby();
  RadioState state() const;
  bool isInitialized() const;
  RadioSX1262InitError initError() const;
  uint8_t initStatusByte() const;
  uint8_t initCommandStatus() const;
  bool busyTimedOut() const;
  uint32_t txStartCount() const;
  uint32_t txDoneCount() const;
  uint32_t rxStartCount() const;
  uint32_t irqCount() const;
  uint32_t preambleIrqCount() const;
  uint32_t syncWordIrqCount() const;
  uint32_t rxDoneCount() const;
  uint32_t crcErrorCount() const;
  uint32_t timeoutCount() const;
  uint32_t irqPollHitCount() const;
  uint32_t preambleOnlyIrqCount() const;
  uint32_t rxReadFailCount() const;
  uint32_t txBusyHighHitCount() const;
  uint32_t txBusyHighTotalUs() const;
  uint32_t txBusyHighMaxUs() const;
  RadioSX1262TxBusyTrace txBusyTrace() const;
  uint16_t lastIrqStatus() const;
  uint8_t lastOpStatusBefore() const;
  uint8_t lastOpStatusAfter() const;
  uint8_t lastTxSetStatus() const;
  uint16_t lastTxIrqImmediate() const;
  uint16_t initDeviceErrors() const;
  uint16_t lastDeviceErrors() const;
  uint32_t tcxoStartupDelayUs() const;
  uint8_t tcxoStartupAttempts() const;
  uint8_t rfSwitchConfig() const;
  uint8_t debugReadRegister(uint16_t iAddr);
  uint8_t debugReadStatus();
  uint16_t debugReadIrqStatus();
  uint16_t debugReadDeviceErrors();
  void debugClearDeviceErrors();
  int debugReadDio1Level() const;
  int debugReadBusyLevel() const;

  // EMS2 protocol mode (building-wide wake)
  void configureEms2Mode();
  void configureStandardMode();
  RadioError sendEms2Wake();

private:
  enum class TxBusyTraceOp : uint8_t
  {
    None,
    GetIrqStatus,
    GetStatus,
    ClearIrqStatus,
    SetStandby,
    SetRx,
    Other
  };

  enum class TxCompletionPhase : uint8_t
  {
    WaitingForIrq,
    ClearIrqPending
  };

  static constexpr uint8_t kDio1EventQueueLen = 10;
  static constexpr uint8_t kPendingIrqDepth = 8;

  uint8_t mCsPin;
  uint8_t mResetPin;
  uint8_t mDio1Pin;
  uint8_t mBusyPin;
  uint8_t mRfSwitchPin;
  uint8_t mRfSwitchRxPin;
  uint8_t mRfSwitchTxPin;
  bool mUseDio2RfSwitch;
  bool mInitialized;
  RadioSX1262InitError mInitError;
  uint8_t mInitStatusByte;
  uint16_t mInitDeviceErrors;
  uint16_t mLastDeviceErrors;
  uint32_t mTcxoStartupDelayUs;
  uint8_t mTcxoStartupAttempts;
  bool mBusyTimedOut;
  bool mReceiveRestartPending;
  RadioState mState;
  int16_t mLastRssi;
  uint32_t mCurrentFreq;
  uint8_t mStandbyMode;
  uint16_t mPreambleLength; // cached for SetPacketParams (SX1262 sets all params at once)
  uint8_t mSyncWord[8];
  uint8_t mSyncWordBits;
  uint8_t mPacketPayloadLen;
  bool mSoftwarePhyMode;
  bool mEms2Mode;
  bool mTxToRxSettlePending;
  volatile uint32_t mSoftwarePhySyncAtUs;
  uint8_t mEarlyRxFrame[IOHC_FRAME_BUFFER_SIZE];
  uint8_t mEarlyRxFrameLen;

#ifdef ESP32
  SemaphoreHandle_t mChipMutex;
  StaticSemaphore_t mChipMutexBuffer;
  QueueHandle_t mDio1EventQueue;
  StaticQueue_t mDio1EventQueueBuffer;
  uint8_t mDio1EventQueueStorage[kDio1EventQueueLen * sizeof(uint32_t)];
  TaskHandle_t mDio1TaskHandle;
  portMUX_TYPE mPendingIrqMux = portMUX_INITIALIZER_UNLOCKED;
#endif

  // Interrupt-driven DIO1 detection
  volatile bool mIrqFired;
  volatile bool mPreambleFlag; // latched preamble detection from IRQ
  volatile bool mSyncFlag;     // latched sync-word detection from IRQ
  volatile uint16_t mPendingIrqStickyMask;
  volatile uint16_t mPendingIrqQueue[kPendingIrqDepth];
  volatile uint8_t mPendingIrqHead;
  volatile uint8_t mPendingIrqTail;
  uint32_t mPendingIrqOverflowCount;
  uint32_t mTxStartCount;
  uint32_t mTxDoneCount;
  uint32_t mRxStartCount;
  uint32_t mIrqCount;
  uint32_t mPreambleIrqCount;
  uint32_t mSyncWordIrqCount;
  uint32_t mRxDoneCount;
  uint32_t mCrcErrorCount;
  uint32_t mTimeoutCount;
  uint32_t mIrqPollHitCount;
  uint32_t mPreambleOnlyIrqCount;
  uint32_t mRxReadFailCount;
  uint32_t mTxBusyHighHitCount;
  uint32_t mTxBusyHighTotalUs;
  uint32_t mTxBusyHighMaxUs;
  uint32_t mTxBusyHighStartUs;
  bool mTxBusyTraceActive;
  TxBusyTraceOp mTxBusyTraceOp;
  RadioSX1262TxBusyTrace mTxBusyTrace;
  uint16_t mLastIrqStatus;
  uint8_t mLastOpStatusBefore;
  uint8_t mLastOpStatusAfter;
  uint8_t mLastTxSetStatus;
  uint16_t mLastTxIrqImmediate;
  TxCompletionPhase mTxCompletionPhase;
  static void IRAM_ATTR dio1Isr(void *arg);
#ifdef ESP32
  static void dio1Task(void *arg);
#endif

  // SX1262 command-based SPI interface
  bool sendCommand(uint8_t iOpcode, const uint8_t *iParams, uint8_t iLen, bool iBlocking = true);
  bool readCommand(uint8_t iOpcode, uint8_t *oData, uint8_t iLen, bool iBlocking = true);
  bool writeRegister(uint16_t iAddr, uint8_t iVal, bool iBlocking = true);
  bool readRegister(uint16_t iAddr, uint8_t &oVal, bool iBlocking = true);
  bool writeRegisters(uint16_t iAddr, const uint8_t *iData, uint8_t iLen, bool iBlocking = true);
  bool writeBuffer(uint8_t iOffset, const uint8_t *iData, uint8_t iLen, bool iBlocking = true);
  bool readBuffer(uint8_t iOffset, uint8_t *oData, uint8_t iMaxLen, bool iBlocking = true);
  bool readDeviceErrors(uint16_t &oErrors, bool iBlocking = true);
  bool clearDeviceErrors(bool iBlocking = true);
  bool configureTcxo(bool iBlocking = true);
  bool applyRxTxFallbackMode(uint8_t iMode, bool iBlocking = true);

  // BUSY pin management (must wait for BUSY=LOW before SPI)
  bool lockChip(bool iBlocking = true);
  void unlockChip();
  bool waitBusy(bool iBlocking = true);
  void resetTxBusyTrace();
  void setTxBusyTraceOp(TxBusyTraceOp iOp);
  void clearTxBusyTraceOp();
  void accumulateTxBusyTrace(uint32_t iBusyDurationUs);
  bool advanceTxDoneCleanup(bool iBlocking);
  bool tryStandby(bool iBlocking = false);
  void handleQueuedDio1();
  void noteIrqStatus(uint16_t iIrq, bool iPolled);
  bool enqueuePendingIrq(uint16_t iIrq);
  bool dequeuePendingIrq(uint16_t &oIrq);
  bool consumeIrqStatus(uint16_t &oIrq, bool &oNeedsClear, bool iBlocking = false);

  // Hardware reset
  void resetChip();

  // Calibrate radio
  void calibrate();

  // Apply current packet params to chip
  bool applyStandardModulationParams(bool iBlocking = true);
  bool applyPacketParams(bool iBlocking = true);
  bool tryCompleteSoftwarePhyFromLength();

  RadioError setFrequencyInternal(uint32_t iFreqHz, bool iBlocking);
  RadioError setOutputPowerInternal(uint8_t iPower, bool iBlocking);
  RadioError setPreambleLengthInternal(uint16_t iSymbols, bool iBlocking);
  RadioError startTransmitInternal(const uint8_t *iData, uint8_t iLen, bool iBlocking);
  RadioError startReceiveInternal(bool iBlocking);
  bool isTxDoneInternal(bool iBlocking);

  // Set sync word in registers
  bool applySyncWord(const uint8_t *iSyncWord, uint8_t iLen, bool iBlocking = true);

  // Get IRQ status flags
  bool getIrqStatus(uint16_t &oIrq, bool iBlocking = true);
  bool clearIrqStatus(uint16_t iMask, bool iBlocking = true);
  void setRfSwitchIdle();
  void setRfSwitchRx();
  void setRfSwitchTx();
  uint8_t rfSwitchConfigFlags() const;
  bool configureTxIrqs(bool iBlocking = true);
  bool configureRxIrqs(bool iBlocking = true);
};
