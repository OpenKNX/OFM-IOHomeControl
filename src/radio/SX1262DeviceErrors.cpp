#include "SX1262DeviceErrors.h"

#include "sx1262Regs-Fsk.h"

#include <cstdarg>
#include <cstdio>

namespace
{
  struct DeviceErrorName
  {
    uint16_t bit;
    const char *name;
  };

  constexpr DeviceErrorName kDeviceErrorNames[] = {
      {SX1262_DEVICE_ERROR_RC64K_CALIB, "RC64K_CALIB_ERR"},
      {SX1262_DEVICE_ERROR_RC13M_CALIB, "RC13M_CALIB_ERR"},
      {SX1262_DEVICE_ERROR_PLL_CALIB, "PLL_CALIB_ERR"},
      {SX1262_DEVICE_ERROR_ADC_CALIB, "ADC_CALIB_ERR"},
      {SX1262_DEVICE_ERROR_IMAGE_CALIB, "IMG_CALIB_ERR"},
      {SX1262_DEVICE_ERROR_XOSC_START, "XOSC_START_ERR"},
      {SX1262_DEVICE_ERROR_PLL_LOCK, "PLL_LOCK_ERR"},
      {SX1262_DEVICE_ERROR_PA_RAMP, "PA_RAMP_ERR"},
  };

  size_t appendText(char *iBuffer, size_t iBufferSize, size_t iUsed, const char *iFormat, ...)
  {
    if (iBuffer == nullptr || iBufferSize == 0)
      return 0;

    const size_t lWritePos = iUsed < iBufferSize ? iUsed : (iBufferSize - 1);
    const size_t lRemaining = lWritePos < iBufferSize ? (iBufferSize - lWritePos) : 0;

    va_list lArgs;
    va_start(lArgs, iFormat);
    const int lWritten = vsnprintf(iBuffer + lWritePos, lRemaining, iFormat, lArgs);
    va_end(lArgs);

    if (lWritten <= 0)
    {
      iBuffer[iBufferSize - 1] = '\0';
      return lWritePos;
    }

    const size_t lNextUsed = iUsed + static_cast<size_t>(lWritten);
    if (lNextUsed >= iBufferSize)
    {
      iBuffer[iBufferSize - 1] = '\0';
      return iBufferSize - 1;
    }

    return lNextUsed;
  }
}

size_t formatSX1262DeviceErrors(uint16_t iDeviceErrors, char *oBuffer, size_t iBufferSize)
{
  if (oBuffer == nullptr || iBufferSize == 0)
    return 0;

  oBuffer[0] = '\0';

  if (iDeviceErrors == 0)
    return appendText(oBuffer, iBufferSize, 0, "none");

  bool lFirst = true;
  size_t lUsed = 0;
  uint16_t lUnknownBits = iDeviceErrors;

  for (const auto &lEntry : kDeviceErrorNames)
  {
    if ((iDeviceErrors & lEntry.bit) == 0)
      continue;

    lUsed = appendText(oBuffer, iBufferSize, lUsed, lFirst ? "%s" : ", %s", lEntry.name);
    lFirst = false;
    lUnknownBits = static_cast<uint16_t>(lUnknownBits & static_cast<uint16_t>(~lEntry.bit));
  }

  if (lUnknownBits != 0)
    lUsed = appendText(oBuffer, iBufferSize, lUsed, lFirst ? "UNKNOWN_0x%04X" : ", UNKNOWN_0x%04X", lUnknownBits);

  return lUsed;
}