#pragma once
#include <stdint.h>

// Shared radio types for all radio drivers (SX1276, SX1262, etc.)

static constexpr uint8_t RADIO_PIN_NOT_CONNECTED = 0xFF;

enum class RadioState : uint8_t
{
  Idle,
  Transmitting,
  Receiving,
  Sleep
};

enum class RadioError : int8_t
{
  None = 0,
  NotInitialized = -1,
  HardwareError = -2,
  Busy = -3,
  InvalidParam = -4
};

// Evidence captured before RX restart. These are radio observations, not
// protocol authentication or proof that a peer accepted a command.
struct RadioReceiveEvidence {
  uint32_t readTimestampUs = 0, frequencyHz = 0;
  int16_t rssiDbm = 0, afcRaw = 0, feiRaw = 0;
  uint16_t irq = 0;
  uint8_t length = 0;
  bool hardwareCrcChecked = false, hardwareCrcValid = false;
  bool hardwareCrcConsumed = false, fifoOverrun = false, truncated = false;
  bool timestampValid = false, frequencyErrorRegistersPresent = false;
  // SX1276 IoHome mode removes the length byte; controller reconstructs it for
  // the common controller frame path. Evidence refers to FIFO bytes.
  bool fifoLengthByteRemoved = false;
  bool admissible() const {
    return length != 0 && !fifoOverrun && !truncated &&
           (!hardwareCrcChecked || hardwareCrcValid);
  }
};
