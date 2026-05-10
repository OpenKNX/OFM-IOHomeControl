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
