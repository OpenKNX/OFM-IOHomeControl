#include <cstdint>
#include <cstdio>
#include <cstring>

#include "radio/SX1262DeviceErrors.h"
#include "radio/sx1262Regs-Fsk.h"

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
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)

TEST(format_none_for_zero_mask)
{
  char lBuffer[96];
  const size_t lLen = formatSX1262DeviceErrors(0, lBuffer, sizeof(lBuffer));
  ASSERT_EQ(lLen, 4u);
  ASSERT_STR_EQ(lBuffer, "none");
}

TEST(format_single_known_bit)
{
  char lBuffer[96];
  formatSX1262DeviceErrors(SX1262_DEVICE_ERROR_XOSC_START, lBuffer, sizeof(lBuffer));
  ASSERT_STR_EQ(lBuffer, "XOSC_START_ERR");
}

TEST(format_multiple_known_bits_in_datasheet_order)
{
  char lBuffer[96];
  formatSX1262DeviceErrors(static_cast<uint16_t>(SX1262_DEVICE_ERROR_XOSC_START | SX1262_DEVICE_ERROR_PLL_LOCK | SX1262_DEVICE_ERROR_PA_RAMP),
                           lBuffer,
                           sizeof(lBuffer));
  ASSERT_STR_EQ(lBuffer, "XOSC_START_ERR, PLL_LOCK_ERR, PA_RAMP_ERR");
}

TEST(format_unknown_bits)
{
  char lBuffer[96];
  formatSX1262DeviceErrors(0x0080, lBuffer, sizeof(lBuffer));
  ASSERT_STR_EQ(lBuffer, "UNKNOWN_0x0080");
}

TEST(format_mixed_known_and_unknown_bits)
{
  char lBuffer[96];
  formatSX1262DeviceErrors(static_cast<uint16_t>(SX1262_DEVICE_ERROR_RC64K_CALIB | SX1262_DEVICE_ERROR_PA_RAMP | 0x0080),
                           lBuffer,
                           sizeof(lBuffer));
  ASSERT_STR_EQ(lBuffer, "RC64K_CALIB_ERR, PA_RAMP_ERR, UNKNOWN_0x0080");
}

TEST(format_truncates_safely)
{
  char lBuffer[12];
  const size_t lLen = formatSX1262DeviceErrors(static_cast<uint16_t>(SX1262_DEVICE_ERROR_RC64K_CALIB | SX1262_DEVICE_ERROR_RC13M_CALIB),
                                               lBuffer,
                                               sizeof(lBuffer));
  ASSERT_TRUE(lBuffer[sizeof(lBuffer) - 1] == '\0');
  ASSERT_EQ(lLen, sizeof(lBuffer) - 1);
}

int main()
{
  printf("SX1262 device-error formatter tests\n");
  RUN(format_none_for_zero_mask);
  RUN(format_single_known_bit);
  RUN(format_multiple_known_bits_in_datasheet_order);
  RUN(format_unknown_bits);
  RUN(format_mixed_known_and_unknown_bits);
  RUN(format_truncates_safely);

  printf("\nPassed: %d\n", sTestsPassed);
  if (sTestsFailed != 0)
  {
    printf("Failed: %d\n", sTestsFailed);
    return 1;
  }

  return 0;
}
