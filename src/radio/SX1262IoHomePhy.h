#pragma once

#include "../protocol/IoHomeFrame.h"

#include <stddef.h>
#include <stdint.h>

struct SX1262IoHomePhySyncConfig
{
  uint8_t syncWord[8];
  uint8_t syncWordBits;
  bool softwarePhyEnabled;
};

inline constexpr uint8_t SX1262_IOHOME_UART_BITS_PER_BYTE = 10;
inline constexpr uint8_t SX1262_IOHOME_UART_PROBE_MAX_BIT_OFFSET = 10;
inline constexpr uint8_t SX1262_IOHOME_EARLY_HEADER_RAW_LEN = 3;
inline constexpr uint8_t SX1262_IOHOME_EARLY_READ_MARGIN = 2;
inline constexpr uint8_t SX1262_IOHOME_RX_FIXED_LEN = 48;
inline constexpr uint8_t SX1262_IOHOME_RX_READ_LEN = 64;
inline constexpr size_t SX1262_IOHOME_MAX_ENCODED_FRAME_LEN =
    ((IOHC_FRAME_BUFFER_SIZE + IOHC_CRC_SIZE) * SX1262_IOHOME_UART_BITS_PER_BYTE + 7U) / 8U;

SX1262IoHomePhySyncConfig sx1262ResolveIoHomeSyncWord(const uint8_t *iSyncWord, uint8_t iSyncWordLen);
size_t sx1262EncodeIoHomeFrame(const uint8_t *iFrame, size_t iFrameLen, uint8_t *oEncoded, size_t iEncodedMaxLen);
size_t sx1262DecodeIoHomeUart(const uint8_t *iRaw, size_t iRawLen, uint8_t iBitOffset, uint8_t *oDecoded, size_t iDecodedMaxLen);
uint8_t sx1262PeekIoHomeFrameLength(const uint8_t *iRaw, size_t iRawLen);
size_t sx1262IoHomeRawBytesForFrame(size_t iFrameLen);
bool sx1262FindIoHomeFrame(const uint8_t *iRaw, size_t iRawLen,
                           uint8_t *oFrame, size_t iFrameMaxLen, size_t &oFrameLen);
