#include "SX1262IoHomePhy.h"

#include "../protocol/IoHomeCrypto.h"

#include <cstring>

namespace
{
  constexpr uint8_t kIoHomeProtocolSync[] = {0x55, 0xFF, 0x33};
  constexpr uint8_t kIoHomeChipSync[] = {0x57, 0xFD, 0x99};

  uint8_t getBitMsb(const uint8_t *iData, size_t iBitPos)
  {
    return static_cast<uint8_t>((iData[iBitPos / 8U] >> (7U - (iBitPos % 8U))) & 0x01U);
  }

  void setBitMsb(uint8_t *oData, size_t iBitPos)
  {
    oData[iBitPos / 8U] |= static_cast<uint8_t>(1U << (7U - (iBitPos % 8U)));
  }

  void clearBitMsb(uint8_t *oData, size_t iBitPos)
  {
    oData[iBitPos / 8U] &= static_cast<uint8_t>(~(1U << (7U - (iBitPos % 8U))));
  }

  bool hasValidProtocolCrc(const uint8_t *iDecoded, size_t iDecodedLen,
                           size_t iStart, size_t iFrameLen)
  {
    if (iStart + iFrameLen + IOHC_CRC_SIZE > iDecodedLen)
      return false;

    const uint16_t lRxCrc = static_cast<uint16_t>(iDecoded[iStart + iFrameLen]) |
                            static_cast<uint16_t>(iDecoded[iStart + iFrameLen + 1U] << 8U);
    return lRxCrc == IoHomeCrypto::crc16Kermit(iDecoded + iStart, iFrameLen);
  }
}

SX1262IoHomePhySyncConfig sx1262ResolveIoHomeSyncWord(const uint8_t *iSyncWord, uint8_t iSyncWordLen)
{
  SX1262IoHomePhySyncConfig lConfig = {};
  if (iSyncWord == nullptr || iSyncWordLen == 0)
    return lConfig;

  if (iSyncWordLen >= sizeof(kIoHomeProtocolSync) &&
      std::memcmp(iSyncWord, kIoHomeProtocolSync, sizeof(kIoHomeProtocolSync)) == 0)
  {
    std::memcpy(lConfig.syncWord, kIoHomeChipSync, sizeof(kIoHomeChipSync));
    lConfig.syncWordBits = 24;
    lConfig.softwarePhyEnabled = true;
    return lConfig;
  }

  const uint8_t lCopyLen = (iSyncWordLen < sizeof(lConfig.syncWord)) ? iSyncWordLen : static_cast<uint8_t>(sizeof(lConfig.syncWord));
  std::memcpy(lConfig.syncWord, iSyncWord, lCopyLen);
  lConfig.syncWordBits = static_cast<uint8_t>(lCopyLen * 8U);
  lConfig.softwarePhyEnabled = false;
  return lConfig;
}

size_t sx1262EncodeIoHomeFrame(const uint8_t *iFrame, size_t iFrameLen, uint8_t *oEncoded, size_t iEncodedMaxLen)
{
  if (iFrame == nullptr || oEncoded == nullptr || iFrameLen == 0 || iFrameLen > IOHC_FRAME_BUFFER_SIZE)
    return 0;

  uint8_t lProtocolFrame[IOHC_FRAME_BUFFER_SIZE + IOHC_CRC_SIZE] = {};
  std::memcpy(lProtocolFrame, iFrame, iFrameLen);
  const uint16_t lCrc = IoHomeCrypto::crc16Kermit(iFrame, iFrameLen);
  lProtocolFrame[iFrameLen] = static_cast<uint8_t>(lCrc & 0xFFU);
  lProtocolFrame[iFrameLen + 1U] = static_cast<uint8_t>((lCrc >> 8U) & 0xFFU);

  const size_t lProtocolLen = iFrameLen + IOHC_CRC_SIZE;
  const size_t lTotalBits = lProtocolLen * SX1262_IOHOME_UART_BITS_PER_BYTE;
  const size_t lEncodedLen = (lTotalBits + 7U) / 8U;
  if (lEncodedLen > iEncodedMaxLen)
    return 0;

  // UART idles high. Mark unused bits in the final packed byte high too, so
  // they cannot look like a trailing start bit to the receiver.
  std::memset(oEncoded, 0xFF, lEncodedLen);

  size_t lBitPos = 0;
  for (size_t i = 0; i < lProtocolLen; i++)
  {
    const uint8_t lValue = lProtocolFrame[i];
    clearBitMsb(oEncoded, lBitPos); // start bit = 0
    lBitPos++;
    for (uint8_t lBit = 0; lBit < 8U; lBit++)
    {
      if ((lValue & (1U << lBit)) != 0)
        setBitMsb(oEncoded, lBitPos);
      else
        clearBitMsb(oEncoded, lBitPos);
      lBitPos++;
    }

    setBitMsb(oEncoded, lBitPos); // stop bit = 1
    lBitPos++;
  }

  return lEncodedLen;
}

size_t sx1262DecodeIoHomeUart(const uint8_t *iRaw, size_t iRawLen, uint8_t iBitOffset,
                              uint8_t *oDecoded, size_t iDecodedMaxLen)
{
  if (iRaw == nullptr || oDecoded == nullptr || iRawLen == 0 || iDecodedMaxLen == 0)
    return 0;

  size_t lDecodedLen = 0;
  size_t lBitPos = iBitOffset;
  const size_t lTotalBits = iRawLen * 8U;

  while (lBitPos + SX1262_IOHOME_UART_BITS_PER_BYTE <= lTotalBits && lDecodedLen < iDecodedMaxLen)
  {
    if (getBitMsb(iRaw, lBitPos) != 0 || getBitMsb(iRaw, lBitPos + 9U) != 1U)
      break;

    uint8_t lValue = 0;
    for (uint8_t lBit = 0; lBit < 8U; lBit++)
      lValue = static_cast<uint8_t>(lValue | (getBitMsb(iRaw, lBitPos + 1U + lBit) << lBit));

    oDecoded[lDecodedLen++] = lValue;
    lBitPos += SX1262_IOHOME_UART_BITS_PER_BYTE;
  }

  return lDecodedLen;
}

uint8_t sx1262PeekIoHomeFrameLength(const uint8_t *iRaw, size_t iRawLen)
{
  uint8_t lBestFrameLen = 0;
  for (uint8_t lBitOffset = 0; lBitOffset < SX1262_IOHOME_UART_PROBE_MAX_BIT_OFFSET; lBitOffset++)
  {
    uint8_t lCtrl0 = 0;
    if (sx1262DecodeIoHomeUart(iRaw, iRawLen, lBitOffset, &lCtrl0, 1) != 1)
      continue;

    const uint8_t lFrameLen = static_cast<uint8_t>((lCtrl0 & IOHC_CTRL0_LEN_MASK) + 1U);
    if (lFrameLen >= IOHC_FRAME_MIN_SIZE && lFrameLen <= IOHC_FRAME_BUFFER_SIZE &&
        lFrameLen > lBestFrameLen)
      lBestFrameLen = lFrameLen;
  }
  return lBestFrameLen;
}

size_t sx1262IoHomeRawBytesForFrame(size_t iFrameLen)
{
  if (iFrameLen < IOHC_FRAME_MIN_SIZE || iFrameLen > IOHC_FRAME_BUFFER_SIZE)
    return 0;

  const size_t lUartCells = iFrameLen + IOHC_CRC_SIZE;
  return (lUartCells * SX1262_IOHOME_UART_BITS_PER_BYTE + 7U) / 8U;
}

bool sx1262FindIoHomeFrame(const uint8_t *iRaw, size_t iRawLen,
                           uint8_t *oFrame, size_t iFrameMaxLen, size_t &oFrameLen)
{
  oFrameLen = 0;
  if (iRaw == nullptr || oFrame == nullptr || iRawLen == 0)
    return false;

  for (uint8_t lBitOffset = 0; lBitOffset < SX1262_IOHOME_UART_PROBE_MAX_BIT_OFFSET; lBitOffset++)
  {
    uint8_t lDecoded[IOHC_FRAME_BUFFER_SIZE + IOHC_CRC_SIZE + 8] = {};
    const size_t lDecodedLen = sx1262DecodeIoHomeUart(iRaw, iRawLen, lBitOffset, lDecoded, sizeof(lDecoded));
    if (lDecodedLen < IOHC_FRAME_MIN_SIZE + IOHC_CRC_SIZE)
      continue;

    for (size_t lStart = 0; lStart + IOHC_FRAME_MIN_SIZE + IOHC_CRC_SIZE <= lDecodedLen; lStart++)
    {
      const uint8_t lCtrl0 = lDecoded[lStart];
      const uint8_t lFrameLen = static_cast<uint8_t>((lCtrl0 & IOHC_CTRL0_LEN_MASK) + 1U);
      if (lFrameLen < IOHC_FRAME_MIN_SIZE || lFrameLen > IOHC_FRAME_BUFFER_SIZE)
        continue;

      // SendKey1W is the protocol's only legal out-of-length frame: CTRL0
      // declares 29 bytes, while some remotes append a six-byte MAC before
      // the transport CRC. Prefer that full candidate when its CRC validates,
      // then retain the normal declared-length path for no-MAC SendKey frames.
      const bool lMayHaveSendKeyTrailer =
          (lCtrl0 & IOHC_CTRL0_MODE_1W) != 0 &&
          lFrameLen == IOHC_FRAME_MIN_SIZE + 20U &&
          lDecoded[lStart + IOHC_FRAME_MIN_SIZE - 1U] ==
              static_cast<uint8_t>(IoHomeCommand::SendKey1W);
      const size_t lTrailerFrameLen = static_cast<size_t>(lFrameLen) + IOHC_HMAC_SIZE;
      if (lMayHaveSendKeyTrailer &&
          hasValidProtocolCrc(lDecoded, lDecodedLen, lStart, lTrailerFrameLen))
      {
        if (lTrailerFrameLen > iFrameMaxLen)
          return false;

        std::memcpy(oFrame, lDecoded + lStart, lTrailerFrameLen);
        oFrameLen = lTrailerFrameLen;
        return true;
      }

      if (!hasValidProtocolCrc(lDecoded, lDecodedLen, lStart, lFrameLen))
        continue;
      if (lFrameLen > iFrameMaxLen)
        return false;

      std::memcpy(oFrame, lDecoded + lStart, lFrameLen);
      oFrameLen = lFrameLen;
      return true;
    }
  }

  return false;
}
