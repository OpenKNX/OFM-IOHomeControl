#include <cstdint>
#include <cstdio>
#include <cstring>

#include "radio/SX1262IoHomePhy.h"

static int sTestsPassed = 0;
static int sTestsFailed = 0;

#define TEST(name) static void test_##name()
#define RUN(name)                  \
  do                               \
  {                                \
    const int failed = sTestsFailed; \
    printf("  %-50s ", #name);     \
    test_##name();                 \
    if (sTestsFailed == failed)    \
    {                              \
      printf("[PASS]\n");          \
      sTestsPassed++;              \
    }                              \
  } while (0)

#define ASSERT_TRUE(expr)                              \
  do                                                   \
  {                                                    \
    if (!(expr))                                       \
    {                                                  \
      printf("[FAIL] line %d: %s\n", __LINE__, #expr); \
      sTestsFailed++;                                  \
      return;                                          \
    }                                                  \
  } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_MEM_EQ(a, b, n) ASSERT_TRUE(std::memcmp((a), (b), (n)) == 0)

namespace
{
  void shiftBitsMsb(const uint8_t *iSrc, size_t iSrcLen, uint8_t iShiftBits, uint8_t *oDst, size_t iDstLen)
  {
    std::memset(oDst, 0, iDstLen);
    for (size_t i = 0; i < iSrcLen * 8U; i++)
    {
      const uint8_t lBit = static_cast<uint8_t>((iSrc[i / 8U] >> (7U - (i % 8U))) & 0x01U);
      if (lBit == 0)
        continue;

      const size_t lDstBit = i + iShiftBits;
      oDst[lDstBit / 8U] |= static_cast<uint8_t>(1U << (7U - (lDstBit % 8U)));
    }
  }
}

TEST(resolve_sync_remaps_iohome_pattern)
{
  const uint8_t lSync[] = {0x55, 0xFF, 0x33};
  const auto lConfig = sx1262ResolveIoHomeSyncWord(lSync, sizeof(lSync));
  const uint8_t lExpected[] = {0x57, 0xFD, 0x99};
  ASSERT_TRUE(lConfig.softwarePhyEnabled);
  ASSERT_EQ(lConfig.syncWordBits, 24u);
  ASSERT_MEM_EQ(lConfig.syncWord, lExpected, sizeof(lExpected));
}

TEST(resolve_sync_keeps_non_iohome_pattern)
{
  const uint8_t lSync[] = {0x2D, 0xD4};
  const auto lConfig = sx1262ResolveIoHomeSyncWord(lSync, sizeof(lSync));
  ASSERT_TRUE(!lConfig.softwarePhyEnabled);
  ASSERT_EQ(lConfig.syncWordBits, 16u);
  ASSERT_MEM_EQ(lConfig.syncWord, lSync, sizeof(lSync));
}

TEST(encode_and_find_round_trip_frame)
{
  const uint8_t lFrame[] = {0x48, 0x00, 0x00, 0x00, 0x3F, 0x00, 0xFA, 0x34, 0x07};
  uint8_t lEncoded[SX1262_IOHOME_MAX_ENCODED_FRAME_LEN] = {};
  const size_t lEncodedLen = sx1262EncodeIoHomeFrame(lFrame, sizeof(lFrame), lEncoded, sizeof(lEncoded));
  ASSERT_TRUE(lEncodedLen > 0);

  uint8_t lDecoded[IOHC_FRAME_BUFFER_SIZE] = {};
  size_t lDecodedLen = 0;
  ASSERT_TRUE(sx1262FindIoHomeFrame(lEncoded, lEncodedLen, lDecoded, sizeof(lDecoded), lDecodedLen));
  ASSERT_EQ(lDecodedLen, sizeof(lFrame));
  ASSERT_MEM_EQ(lDecoded, lFrame, sizeof(lFrame));
}

TEST(find_frame_with_bit_offset)
{
  const uint8_t lFrame[] = {0x4C, 0x00, 0x00, 0x00, 0x3F, 0x00, 0xFA, 0x34, 0x28, 0x01, 0x00, 0x00, 0x00};
  uint8_t lEncoded[SX1262_IOHOME_MAX_ENCODED_FRAME_LEN] = {};
  const size_t lEncodedLen = sx1262EncodeIoHomeFrame(lFrame, sizeof(lFrame), lEncoded, sizeof(lEncoded));
  ASSERT_TRUE(lEncodedLen > 0);

  uint8_t lShifted[SX1262_IOHOME_MAX_ENCODED_FRAME_LEN + 2] = {};
  shiftBitsMsb(lEncoded, lEncodedLen, 3, lShifted, sizeof(lShifted));

  uint8_t lDecoded[IOHC_FRAME_BUFFER_SIZE] = {};
  size_t lDecodedLen = 0;
  ASSERT_TRUE(sx1262FindIoHomeFrame(lShifted, sizeof(lShifted), lDecoded, sizeof(lDecoded), lDecodedLen));
  ASSERT_EQ(lDecodedLen, sizeof(lFrame));
  ASSERT_MEM_EQ(lDecoded, lFrame, sizeof(lFrame));
}

TEST(rejects_crc_invalid_capture)
{
  const uint8_t lFrame[] = {0x48, 0x00, 0x00, 0x00, 0x3F, 0x00, 0xFA, 0x34, 0x07};
  uint8_t lEncoded[SX1262_IOHOME_MAX_ENCODED_FRAME_LEN] = {};
  const size_t lEncodedLen = sx1262EncodeIoHomeFrame(lFrame, sizeof(lFrame), lEncoded, sizeof(lEncoded));
  ASSERT_TRUE(lEncodedLen > 0);

  lEncoded[lEncodedLen / 2U] ^= 0x20U;

  uint8_t lDecoded[IOHC_FRAME_BUFFER_SIZE] = {};
  size_t lDecodedLen = 0;
  ASSERT_TRUE(!sx1262FindIoHomeFrame(lEncoded, lEncodedLen, lDecoded, sizeof(lDecoded), lDecodedLen));
  ASSERT_EQ(lDecodedLen, 0u);
}

TEST(encode_respects_max_output_size)
{
  const uint8_t lFrame[] = {0x48, 0x00, 0x00, 0x00, 0x3F, 0x00, 0xFA, 0x34, 0x07};
  uint8_t lTiny[2] = {};
  ASSERT_EQ(sx1262EncodeIoHomeFrame(lFrame, sizeof(lFrame), lTiny, sizeof(lTiny)), 0u);
}

TEST(encode_pads_partial_uart_byte_high)
{
  // 11 protocol bytes (frame + CRC) use 110 UART bits, leaving two unused
  // bits in the final encoded byte. UART mark/idle is high.
  const uint8_t lFrame[] = {0x48, 0x00, 0x00, 0x00, 0x3F, 0x00, 0xFA, 0x34, 0x07};
  uint8_t lEncoded[SX1262_IOHOME_MAX_ENCODED_FRAME_LEN] = {};
  const size_t lEncodedLen = sx1262EncodeIoHomeFrame(lFrame, sizeof(lFrame), lEncoded, sizeof(lEncoded));
  ASSERT_EQ(lEncodedLen, 14u);
  ASSERT_EQ(static_cast<uint8_t>(lEncoded[lEncodedLen - 1U] & 0x03U), 0x03U);
}

int main()
{
  printf("SX1262 io-home PHY tests\n");
  RUN(resolve_sync_remaps_iohome_pattern);
  RUN(resolve_sync_keeps_non_iohome_pattern);
  RUN(encode_and_find_round_trip_frame);
  RUN(find_frame_with_bit_offset);
  RUN(rejects_crc_invalid_capture);
  RUN(encode_respects_max_output_size);
  RUN(encode_pads_partial_uart_byte_high);

  printf("\nPassed: %d\n", sTestsPassed);
  if (sTestsFailed != 0)
  {
    printf("Failed: %d\n", sTestsFailed);
    return 1;
  }

  return 0;
}
