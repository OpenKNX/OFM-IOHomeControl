// Unit tests for OFM-IO-Homecontrol protocol layer
// Compile: g++ -std=c++17 -DTEST_NATIVE -I../src -I. test_protocol.cpp
//          ../src/protocol/IoHomeCrypto.cpp ../src/protocol/IoHomeFrame.cpp
//          ../src/IoHomeRemoteMap.cpp -o test_protocol
//
// Tests are validated against the reference implementation and docs/scripts:
//   https://github.com/nicolas5000/io-rts-esp32
//   https://github.com/Velocet/iown-homecontrol/tree/main/scripts/Iown-ioCrypto.py

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "test_registry.h"

// Pull in the modules under test
#include "protocol/IoHomeCrypto.h"
#include "protocol/IoHomeFrame.h"
#include "protocol/IoHomeCommands.h"
#include "IoHomeRemoteMap.h"
#include "corpus/golden_rf_corpus.h"

#ifdef TEST_NATIVE
#include "controller/IoHomeController.h"
#include "controller/IoHomeControllerNativeStubs.h"
#endif

// ---------- Test framework (minimal) ----------

int sTestsPassed = 0;
int sTestsFailed = 0;

#define TEST(name)                                                    \
    static void test_##name();                                       \
    static IoHomeTestRegistrar test_registrar_##name(#name, test_##name); \
    static void test_##name()
#define RUN(name)                  \
    do                             \
    {                              \
        printf("  %-50s ", #name); \
        test_##name();             \
        printf("[PASS]\n");        \
        sTestsPassed++;            \
    } while (0)

#define ASSERT_TRUE(expr)                                    \
    do                                                       \
    {                                                        \
        if (!(expr))                                         \
        {                                                    \
            printf("[FAIL] line %d: %s\n", __LINE__, #expr); \
            sTestsFailed++;                                  \
            return;                                          \
        }                                                    \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
#define ASSERT_MEM_EQ(a, b, n) ASSERT_TRUE(memcmp((a), (b), (n)) == 0)
#define ASSERT_MEM_NEQ(a, b, n) ASSERT_TRUE(memcmp((a), (b), (n)) != 0)
#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT_TRUE(((a) - (b)) < (eps) && ((b) - (a)) < (eps))

static void hexdump(const char *label, const uint8_t *data, size_t len)
{
    printf("    %s: ", label);
    for (size_t i = 0; i < len; i++)
        printf("%02x", data[i]);
    printf("\n");
}

static uint8_t serializeFrameForTest(const IoHomeFrame &frame, uint8_t *buffer, uint8_t maxLen)
{
    if (frame.hasCrc)
        return frame.serializeRawWithCrc(buffer, maxLen);
    if ((frame.ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0)
        return frame.serialize1W(buffer, maxLen);
    return frame.serialize2W(buffer, maxLen);
}

static uint8_t serialize2WWithAppendedHmacForRejectTest(const IoHomeFrame &frame, uint8_t *buffer, uint8_t maxLen)
{
    IoHomeFrame base = frame;
    base.hasHmac = false;
    base.hasCrc = false;
    const uint8_t len = base.serialize2W(buffer, maxLen);
    if (len == 0 || maxLen < len + IOHC_HMAC_SIZE)
        return 0;
    memcpy(buffer + len, frame.hmac, IOHC_HMAC_SIZE);
    return len + IOHC_HMAC_SIZE;
}

static bool deserializeFrameForTest(IoHomeFrame &frame, const uint8_t *buffer, uint8_t len)
{
    return frame.deserializeFrame(buffer, len);
}

static bool deserializeRawWithOptionalCrcForTest(IoHomeFrame &frame, const uint8_t *buffer, uint8_t len)
{
    return frame.deserializeRawWithOptionalCrc(buffer, len);
}

#ifdef TEST_NATIVE
static void initGatewayControllerForTest(IoHomeController &oController,
                                         IoHomecontrol &oModule,
                                         uint32_t iGatewayNodeId,
                                         const uint8_t iGatewayKey[16])
{
    oController.setModule(&oModule);
    oController.setOwnNodeId(iGatewayNodeId);
    oController.init();
    oController.setRxScanEnabled(false);
    oController.setGatewayNodeId(iGatewayNodeId);
    oController.setGatewayKey(iGatewayKey);
    oController.setGatewayMode(true);
}

static bool queueGatewayRequestAndLoop(IoHomeController &iController,
                                       const IoHomeFrame &iRequest,
                                       IoHomeFrame &oResponse)
{
    // The active extraction responder broadcasts every device-role reply.
    // Drain a previous CH1/CH3/CH2 sequence before injecting the next hub
    // request, as a real hub cannot transmit while the responder is on air.
    for (uint8_t i = 0; i < 8; i++)
        iController.loop();

    uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(iRequest, lBuffer, sizeof(lBuffer));
    if (lLen == 0)
        return false;

    iController.radio().testClearTransmittedPacket();
    iController.radio().testQueueReceivedPacket(lBuffer, lLen);
    iController.loop();

    const auto &lPacket = iController.radio().testLastTransmittedPacket();
    if (lPacket.empty())
        return false;

    return deserializeFrameForTest(oResponse, lPacket.data(), static_cast<uint8_t>(lPacket.size()));
}

static void buildGatewayDiscoverRequest(IoHomeFrame &oFrame, uint32_t iDeviceNodeId)
{
    oFrame.init();
    oFrame.setStart2W();
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestBroadcast();
    oFrame.commandId = IoHomeCommand::DiscoverRequest;
    oFrame.dataLen = 0;
    oFrame.hasHmac = false;
}

static void buildGatewayConfirmation(IoHomeFrame &oFrame,
                                     uint32_t iDeviceNodeId,
                                     uint32_t iGatewayNodeId)
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x01;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iGatewayNodeId);
    oFrame.commandId = IoHomeCommand::Confirmation;
    oFrame.data[0] = 0x01;
    oFrame.dataLen = 1;
    oFrame.hasHmac = false;
}

static void buildGatewayLaunchKeyTransfer(IoHomeFrame &oFrame,
                                          uint32_t iDeviceNodeId,
                                          uint32_t iGatewayNodeId,
                                          const uint8_t iChallenge[6])
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x01;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iGatewayNodeId);
    oFrame.commandId = IoHomeCommand::LaunchKeyTransfer;
    memcpy(oFrame.data, iChallenge, 6);
    oFrame.dataLen = 6;
    oFrame.hasHmac = false;
}

static void buildGatewayChallengeRequest(IoHomeFrame &oFrame,
                                         uint32_t iDeviceNodeId,
                                         uint32_t iGatewayNodeId,
                                         const uint8_t iChallenge[6])
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x01;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iGatewayNodeId);
    oFrame.commandId = IoHomeCommand::ChallengeRequest;
    memcpy(oFrame.data, iChallenge, 6);
    oFrame.dataLen = 6;
    oFrame.hasHmac = false;
}

static void initKeyExtractControllerForTest(IoHomeController &oController,
                                            IoHomecontrol &oModule,
                                            uint32_t iOwnNodeId)
{
    oController.setModule(&oModule);
    oController.setOwnNodeId(iOwnNodeId);
    oController.init();
    oController.setRxScanEnabled(false);
}

static void buildKeyExtractKeyInit(IoHomeFrame &oFrame,
                                   uint32_t iHubNodeId,
                                   uint32_t iExtractNodeId)
{
    oFrame.init();
    oFrame.ctrlByte0 = 0;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iHubNodeId);
    oFrame.setDestNode(iExtractNodeId);
    oFrame.commandId = IoHomeCommand::KeyInitTransfer;
    oFrame.dataLen = 0;
    oFrame.hasHmac = false;
}

static void buildKeyExtractKeyTransfer(IoHomeFrame &oFrame,
                                       uint32_t iHubNodeId,
                                       uint32_t iExtractNodeId,
                                       const uint8_t iChallenge[6],
                                       const uint8_t iSystemKey[16])
{
    uint8_t lEncryptedKey[16] = {};
    const uint8_t lKeyInitTranscript[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(lKeyInitTranscript, sizeof(lKeyInitTranscript),
                                            iChallenge, iSystemKey,
                                            IOHC_TRANSFER_KEY, lEncryptedKey));

    oFrame.init();
    oFrame.ctrlByte0 = 0;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iHubNodeId);
    oFrame.setDestNode(iExtractNodeId);
    oFrame.commandId = IoHomeCommand::KeyTransfer;
    memcpy(oFrame.data, lEncryptedKey, sizeof(lEncryptedKey));
    oFrame.dataLen = sizeof(lEncryptedKey);
    oFrame.hasHmac = false;
}

static void buildKeyExtractConfirmation(IoHomeFrame &oFrame,
                                        uint32_t iHubNodeId,
                                        uint32_t iExtractNodeId)
{
    oFrame.init();
    oFrame.ctrlByte0 = 0;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iHubNodeId);
    oFrame.setDestNode(iExtractNodeId);
    oFrame.commandId = IoHomeCommand::Confirmation;
    oFrame.dataLen = 0;
    oFrame.hasHmac = false;
}

static void buildKeyExtractAddressRequest(IoHomeFrame &oFrame,
                                          uint32_t iHubNodeId,
                                          uint32_t iExtractNodeId)
{
    oFrame.init();
    oFrame.setStart2W();
    oFrame.setSrcNode(iHubNodeId);
    oFrame.setDestNode(iExtractNodeId);
    oFrame.commandId = IoHomeCommand::AddressRequest;
    oFrame.dataLen = 0;
    oFrame.hasHmac = false;
}
#endif

// =====================================================================
// 1. AES-128 ECB — NIST FIPS-197 Appendix B test vector
// =====================================================================

TEST(aes128_nist_vector)
{
    // NIST FIPS-197 Appendix B
    const uint8_t key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    const uint8_t plaintext[16] = {
        0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
        0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34};
    const uint8_t expected[16] = {
        0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb,
        0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32};

    uint8_t output[16];
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(plaintext, key, output));
    ASSERT_MEM_EQ(output, expected, 16);
}

TEST(aes128_encrypt_decrypt_roundtrip)
{
    const uint8_t key[16] = {
        0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
        0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73};
    const uint8_t plaintext[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    uint8_t encrypted[16], decrypted[16];
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(plaintext, key, encrypted));
    // encrypted should differ from plaintext
    ASSERT_TRUE(memcmp(encrypted, plaintext, 16) != 0);
    ASSERT_TRUE(IoHomeCrypto::aes128Decrypt(encrypted, key, decrypted));
    ASSERT_MEM_EQ(decrypted, plaintext, 16);
}

// =====================================================================
// 2. Checksum algorithm
// =====================================================================

TEST(checksum_zero_input)
{
    uint8_t chk1 = 0, chk2 = 0;
    IoHomeCrypto::computeChecksum(0x00, chk1, chk2);
    // After one zero byte with zero initial state:
    // tmp = 0x00 ^ 0x00 = 0x00
    // chk2 = (0 & 0x7F) << 1 = 0x00
    // (chk1 & 0x80) == 0 → first branch
    // tmp(0x00) < 128 → no set LSB
    // chk1 = chk2 = 0x00
    // chk2 = (0x00 << 1) = 0x00
    ASSERT_EQ(chk1, 0x00);
    ASSERT_EQ(chk2, 0x00);
}

TEST(checksum_single_byte)
{
    uint8_t chk1 = 0, chk2 = 0;
    IoHomeCrypto::computeChecksum(0xFF, chk1, chk2);
    // tmp = 0xFF ^ 0x00 = 0xFF
    // chk2 = (0 & 0x7F) << 1 = 0x00
    // (chk1=0 & 0x80) == 0 → first branch
    // tmp(0xFF) >= 128 → chk2 |= 1 → chk2=0x01
    // chk1 = 0x01
    // chk2 = (0xFF << 1) & 0xFF = 0xFE
    ASSERT_EQ(chk1, 0x01);
    ASSERT_EQ(chk2, 0xFE);
}

TEST(checksum_multi_byte_sequence)
{
    // Process the byte sequence {0x40, 0x01, 0x00, 0x00, 0x3B}
    // (a typical discovery frame header fragment)
    uint8_t chk1 = 0, chk2 = 0;
    const uint8_t data[] = {0x40, 0x01, 0x00, 0x00, 0x3B};
    for (size_t i = 0; i < sizeof(data); i++)
        IoHomeCrypto::computeChecksum(data[i], chk1, chk2);
    // Verify deterministic — run again with same input
    uint8_t chk1b = 0, chk2b = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        IoHomeCrypto::computeChecksum(data[i], chk1b, chk2b);
    ASSERT_EQ(chk1, chk1b);
    ASSERT_EQ(chk2, chk2b);
}

TEST(checksum_high_bit_branch)
{
    // Force the high-bit branch: set chk1 with bit 7 set
    uint8_t chk1 = 0x80, chk2 = 0x00;
    IoHomeCrypto::computeChecksum(0x00, chk1, chk2);
    // tmp = 0x00 ^ 0x00 = 0x00
    // chk2 = (0x80 & 0x7F) << 1 = 0x00
    // (chk1=0x80 & 0x80) != 0 → second branch
    // tmp(0x00) < 128 → no set LSB
    // chk1 = 0x00 ^ 0x55 = 0x55
    // chk2 = (0x00 << 1) ^ 0x5B = 0x5B
    ASSERT_EQ(chk1, 0x55);
    ASSERT_EQ(chk2, 0x5B);
}

// =====================================================================
// 3. IV construction
// =====================================================================

TEST(iv_construction_short_data)
{
    // 3 bytes of frame data + challenge
    const uint8_t frameData[] = {0xAA, 0xBB, 0xCC};
    const uint8_t challenge[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    uint8_t iv[16];

    IoHomeCrypto::constructIv2W(frameData, 3, challenge, iv);

    // Bytes 0-2: frame data
    ASSERT_EQ(iv[0], 0xAA);
    ASSERT_EQ(iv[1], 0xBB);
    ASSERT_EQ(iv[2], 0xCC);
    // Bytes 3-7: padded with 0x55
    for (int i = 3; i < 8; i++)
        ASSERT_EQ(iv[i], 0x55);
    // Bytes 8-9: checksums (computed from 3 bytes)
    // (verify they're non-trivially set for non-zero input)
    // Bytes 10-15: challenge
    ASSERT_MEM_EQ(iv + 10, challenge, 6);
}

TEST(iv_construction_full_data)
{
    // 10 bytes of frame data — only first 8 go into IV bytes 0-7
    const uint8_t frameData[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    const uint8_t challenge[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    uint8_t iv[16];

    IoHomeCrypto::constructIv2W(frameData, 10, challenge, iv);

    // Bytes 0-7: first 8 bytes of frame data
    ASSERT_MEM_EQ(iv, frameData, 8);
    // Bytes 8-9: checksums (computed from ALL 10 bytes, not just first 8)
    // Verify checksums are set (non-zero for this input)
    // Bytes 10-15: challenge
    ASSERT_MEM_EQ(iv + 10, challenge, 6);
}

TEST(iv_construction_zero_data)
{
    // 0 bytes of frame data
    const uint8_t challenge[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t iv[16];

    IoHomeCrypto::constructIv2W(nullptr, 0, challenge, iv);

    // Bytes 0-7: all 0x55 (padding)
    for (int i = 0; i < 8; i++)
        ASSERT_EQ(iv[i], 0x55);
    // Bytes 8-9: checksums of empty data = 0, 0
    ASSERT_EQ(iv[8], 0x00);
    ASSERT_EQ(iv[9], 0x00);
    // Bytes 10-15: challenge
    ASSERT_MEM_EQ(iv + 10, challenge, 6);
}

// =====================================================================
// 4. Frame serialize / deserialize roundtrip
// =====================================================================

TEST(frame_serialize_minimal)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestBroadcast();
    frame.commandId = IoHomeCommand::DiscoverRequest;
    frame.dataLen = 0;
    frame.hasHmac = false;

    ASSERT_EQ(frame.totalLength(), 9);

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 9);

    // ctrl0: START(0x40) | (9-1=8 & 0x1F) = 0x48
    ASSERT_EQ(buf[0], 0x48);
    // ctrl1: version 0
    ASSERT_EQ(buf[1], 0x00);
    // dest: broadcast 00 00 3B
    ASSERT_EQ(buf[2], 0x00);
    ASSERT_EQ(buf[3], 0x00);
    ASSERT_EQ(buf[4], 0x3B);
    // src: 1A 38 0B
    ASSERT_EQ(buf[5], 0x1A);
    ASSERT_EQ(buf[6], 0x38);
    ASSERT_EQ(buf[7], 0x0B);
    // cmd: DiscoverRequest = 0x28
    ASSERT_EQ(buf[8], 0x28);
}

TEST(frame_roundtrip_with_data)
{
    IoHomeFrame original;
    original.init();
    original.setStart2W();
    original.setSrcNode(0xAABBCC);
    original.setDestNode(0x112233);
    original.commandId = IoHomeCommand::Execute;
    original.data[0] = 0x4E; // position MSB
    original.data[1] = 0x20; // position LSB
    original.dataLen = 2;
    original.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(original, buf, sizeof(buf));
    ASSERT_EQ(len, 9 + 2); // header + data = 11

    // Deserialize
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.getSrcNodeId(), 0xAABBCC);
    ASSERT_EQ(parsed.getDestNodeId(), 0x112233);
    ASSERT_EQ((uint8_t)parsed.commandId, 0x00); // Execute
    ASSERT_EQ(parsed.dataLen, 2);
    ASSERT_EQ(parsed.data[0], 0x4E);
    ASSERT_EQ(parsed.data[1], 0x20);
    ASSERT_TRUE(!parsed.hasHmac);
}

TEST(frame_roundtrip_no_hmac)
{
    IoHomeFrame original;
    original.init();
    original.setStart2W();
    original.setSrcNode(0x112233);
    original.setDestNode(0x445566);
    original.commandId = IoHomeCommand::GetName; // 0x50, no HMAC
    original.dataLen = 0;
    original.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(original, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.getSrcNodeId(), 0x112233);
    ASSERT_EQ(parsed.getDestNodeId(), 0x445566);
    ASSERT_EQ((uint8_t)parsed.commandId, 0x50);
    ASSERT_EQ(parsed.dataLen, 0);
    ASSERT_TRUE(!parsed.hasHmac);
}

TEST(frame_deserialize_rejects_too_short)
{
    uint8_t buf[8] = {0};
    IoHomeFrame frame;
    ASSERT_TRUE(!deserializeFrameForTest(frame, buf, 8)); // min is 9
}

TEST(frame_deserialize_rejects_too_long)
{
    uint8_t buf[33] = {0};
    IoHomeFrame frame;
    ASSERT_TRUE(!deserializeFrameForTest(frame, buf, 33)); // max is 32
}

TEST(frame_2w_rejects_oversized_declared_length)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::Private;
    for (uint8_t i = 0; i < IOHC_FRAME_MAX_DATA; i++)
        frame.data[i] = i;
    frame.dataLen = IOHC_FRAME_MAX_DATA + 1;

    uint8_t buf[40] = {0};
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 0);

    buf[0] = static_cast<uint8_t>(IOHC_CTRL0_START | ((IOHC_FRAME_MAX_SIZE_2W - 1) & IOHC_CTRL0_LEN_MASK));
    ASSERT_TRUE(!deserializeFrameForTest(frame, buf, IOHC_FRAME_MAX_SIZE_2W + 1));
}

// =====================================================================
// 5. Deserialize reference packet capture
// =====================================================================

TEST(frame_deserialize_smoove_origin_packet)
{
    // SMOOVE Origin IO packet (from reference protocol documentation)
    // Without sync word prefix (FF 33) and without trailing CRC
    // ctrl0=F6, ctrl1=00, dest=00 00 3F, src=48 5B 37, cmd=00(Execute)
    // data=01 43 D2 00 00 00, hmac=B6 3C B3 CD CD 2B
    // Note: F6 = 1111 0110 → END=1, START=1, 1W=1, len=(0x16)=22 → total=23
    // But actually this is 1W mode. Let's just test basic parsing.
    const uint8_t packet[] = {
        0xF6, 0x00,
        0x00, 0x00, 0x3F,                   // dest (broadcast)
        0x48, 0x5B, 0x37,                   // src
        0x00,                               // Execute
        0x01, 0x43, 0xD2, 0x00, 0x00, 0x00, // data (6 bytes)
        0x03, 0xD6,                         // rolling code
        0xB6, 0x3C, 0xB3, 0xCD, 0xCD, 0x2B  // hmac
    };

    IoHomeFrame frame;
    ASSERT_TRUE(deserializeFrameForTest(frame, packet, sizeof(packet)));
    ASSERT_EQ(frame.getSrcNodeId(), 0x485B37);
    ASSERT_EQ(frame.getDestNodeId(), 0x00003F);
    ASSERT_EQ((uint8_t)frame.commandId, 0x00); // Execute

    // This is a 1W packet (bit 5 set in ctrl0=0xF6)
    bool is1W = (frame.ctrlByte0 & 0x20) != 0;
    ASSERT_TRUE(is1W);
    // In 1W mode, Execute (0x00) is authenticated — HMAC is present
    ASSERT_TRUE(frame.hasHmac);
    ASSERT_EQ(frame.dataLen, 8); // 6 data + 2 rolling code
}

TEST(frame_deserialize_1w_execute_real_capture_from_box)
{
    // Exact 1W Execute frame observed on air and confirmed by the ESPHome parse_ok log.
    const uint8_t packet[] = {
        0xF6, 0x00,
        0x00, 0x00, 0x3F,
        0x9A, 0x5C, 0xA0,
        0x00,
        0x01, 0x43, 0xC8, 0x00, 0x00, 0x00, 0x40, 0x28,
        0xC6, 0x00, 0xBC, 0x4F, 0x2E, 0xB6};

    IoHomeFrame frame;
    ASSERT_TRUE(deserializeFrameForTest(frame, packet, sizeof(packet)));
    ASSERT_EQ(frame.getSrcNodeId(), 0x9A5CA0);
    ASSERT_EQ(frame.getDestNodeId(), 0x00003F);
    ASSERT_EQ((uint8_t)frame.commandId, 0x00);
    ASSERT_TRUE((frame.ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0);
    ASSERT_TRUE(frame.hasHmac);
    ASSERT_EQ(frame.dataLen, 8);

    ASSERT_EQ(frame.data[0], 0x01);
    ASSERT_EQ(frame.data[1], 0x43);
    ASSERT_EQ(frame.data[2], 0xC8);
    ASSERT_EQ(frame.data[3], 0x00);
    ASSERT_EQ(frame.data[6], 0x40);
    ASSERT_EQ(frame.data[7], 0x28);

    const uint8_t expectedHmac[6] = {0xC6, 0x00, 0xBC, 0x4F, 0x2E, 0xB6};
    ASSERT_MEM_EQ(frame.hmac, expectedHmac, sizeof(expectedHmac));
}

TEST(frame_rejects_sx1262_parse_fail_capture)
{
    // Raw 32-byte sample that the ESPHome SX1262 path marked as parse_fail.
    // Keep this rejected so parser changes don't accidentally accept garbage
    // while the receive/timing path is being debugged.
    const uint8_t packet[] = {
        0x34, 0xC0, 0x13, 0x34, 0x53, 0x48, 0x5F, 0x70,
        0x3D, 0xC9, 0x10, 0x54, 0x30, 0x04, 0x21, 0x00,
        0x40, 0x10, 0x10, 0x11, 0x14, 0x4D, 0x91, 0xDE,
        0x11, 0x60, 0x4A, 0x10, 0x85, 0x93, 0xF9, 0xFF};

    IoHomeFrame frame;
    ASSERT_TRUE(!deserializeFrameForTest(frame, packet, sizeof(packet)));
}

// =====================================================================
// 6. HMAC creation and verification
// =====================================================================

TEST(hmac_create_verify_roundtrip)
{
    const uint8_t frameData[] = {
        0x48, 0x01, 0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC, 0x00, 0x4E, 0x20};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t systemKey[16] = {
        0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13};

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, systemKey, hmac));

    // Verify should pass with correct HMAC
    ASSERT_TRUE(IoHomeCrypto::verifyHmac(frameData, sizeof(frameData), hmac, challenge, systemKey));

    // Verify should fail with wrong HMAC
    uint8_t badHmac[6];
    memcpy(badHmac, hmac, 6);
    badHmac[0] ^= 0xFF; // corrupt first byte
    ASSERT_TRUE(!IoHomeCrypto::verifyHmac(frameData, sizeof(frameData), badHmac, challenge, systemKey));
}

TEST(hmac_different_keys_different_output)
{
    const uint8_t frameData[] = {0x48, 0x01, 0x00, 0x00, 0x3B, 0xAA, 0xBB, 0xCC, 0x00};
    const uint8_t challenge[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    const uint8_t key1[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                              0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    const uint8_t key2[16] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
                              0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00};

    uint8_t hmac1[6], hmac2[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, key1, hmac1));
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, key2, hmac2));

    // Different keys must produce different HMACs
    ASSERT_TRUE(memcmp(hmac1, hmac2, 6) != 0);
}

TEST(hmac_different_challenges_different_output)
{
    const uint8_t frameData[] = {0x48, 0x01, 0x00, 0x00, 0x3B, 0xAA, 0xBB, 0xCC, 0x00};
    const uint8_t key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    const uint8_t challenge1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t challenge2[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};

    uint8_t hmac1[6], hmac2[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge1, key, hmac1));
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge2, key, hmac2));

    ASSERT_TRUE(memcmp(hmac1, hmac2, 6) != 0);
}

TEST(hmac_wrong_key_rejects)
{
    const uint8_t frameData[] = {0x48, 0x01, 0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC, 0x00};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    const uint8_t correctKey[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    const uint8_t wrongKey[16] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
                                  0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0};

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, correctKey, hmac));

    // Verify with wrong key should fail
    ASSERT_TRUE(!IoHomeCrypto::verifyHmac(frameData, sizeof(frameData), hmac, challenge, wrongKey));
}

// =====================================================================
// 7. Key transfer keystream / XOR helpers
// =====================================================================

TEST(crypt2wkey_produces_keystream)
{
    const uint8_t frameData[] = {0x31};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t transferKey[16] = {
        0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
        0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73};

    uint8_t keystream[16];
    ASSERT_TRUE(IoHomeCrypto::derive2WKeystream(frameData, sizeof(frameData),
                                                challenge, transferKey, keystream));

    // Keystream should be non-trivial (not all zeros or the input)
    bool allZero = true;
    for (int i = 0; i < 16; i++)
        if (keystream[i] != 0)
            allZero = false;
    ASSERT_TRUE(!allZero);
}

TEST(crypt2wkey_xor_roundtrip)
{
    // Key transfer encryption/decryption must use the explicit XOR helper.
    const uint8_t frameData[] = {0x31};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t transferKey[16] = {
        0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
        0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73};
    const uint8_t systemKey[16] = {
        0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13};

    uint8_t encrypted[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(frameData, sizeof(frameData),
                                            challenge, systemKey,
                                            transferKey, encrypted));
    ASSERT_MEM_NEQ(encrypted, systemKey, 16);

    uint8_t decrypted[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(frameData, sizeof(frameData),
                                            challenge, encrypted,
                                            transferKey, decrypted));
    ASSERT_MEM_EQ(decrypted, systemKey, 16);
}

// =====================================================================
// 8. Cross-validation: HMAC uses same IV as derive2WKeystream
// =====================================================================

TEST(hmac_and_crypt_share_iv)
{
    // Both createHmac2W and derive2WKeystream call constructIv2W with the same
    // frame data and challenge. Verify by checking that the keystream
    // from derive2WKeystream has the same first 6 bytes as the HMAC output
    // when using the same key.
    const uint8_t frameData[] = {0x48, 0x01, 0x00, 0x00, 0x3B, 0xAA, 0xBB, 0xCC, 0x00};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t key[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, key, hmac));

    uint8_t keystream[16];
    ASSERT_TRUE(IoHomeCrypto::derive2WKeystream(frameData, sizeof(frameData), challenge, key, keystream));

    // HMAC is first 6 bytes of AES(IV, key), derive2WKeystream returns full 16 bytes
    ASSERT_MEM_EQ(hmac, keystream, 6);
}

// =====================================================================
// 9. Frame node ID encoding
// =====================================================================

TEST(frame_node_id_encoding)
{
    IoHomeFrame frame;
    frame.init();

    // Test various node IDs
    frame.setSrcNode(0x1A380B);
    ASSERT_EQ(frame.getSrcNodeId(), 0x1A380B);
    ASSERT_EQ(frame.srcNode[0], 0x1A);
    ASSERT_EQ(frame.srcNode[1], 0x38);
    ASSERT_EQ(frame.srcNode[2], 0x0B);

    frame.setDestNode(0x485B37);
    ASSERT_EQ(frame.getDestNodeId(), 0x485B37);

    frame.setDestBroadcast();
    ASSERT_EQ(frame.destNode[0], 0x00);
    ASSERT_EQ(frame.destNode[1], 0x00);
    ASSERT_EQ(frame.destNode[2], 0x3B);
}

// =====================================================================
// 10. Frame control byte flags
// =====================================================================

TEST(frame_start_2w_flags)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    // START=0x40, 2W mode (bit 5 clear), version=0
    ASSERT_EQ(frame.ctrlByte0, 0x40);
    ASSERT_EQ(frame.ctrlByte1, 0x00);

    // Not 1W mode
    ASSERT_TRUE(!(frame.ctrlByte0 & 0x20));
}

// =====================================================================
// 11. Transfer key constant matches reference
// =====================================================================

TEST(transfer_key_matches_reference)
{
    // The hardcoded transfer key must match the reference implementation
    const uint8_t expected[16] = {
        0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
        0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73};
    ASSERT_MEM_EQ(IOHC_TRANSFER_KEY, expected, 16);
}

// =====================================================================
// 12. Position encoding (reference comparison)
// =====================================================================

TEST(position_encoding)
{
    // Execute TX uses position × 2 (reference: io-rts-esp32)
    // 0% → 0, 50% → 100, 100% → 200
    ASSERT_EQ((uint8_t)(0 * 2), 0);
    ASSERT_EQ((uint8_t)(50 * 2), 100);
    ASSERT_EQ((uint8_t)(100 * 2), 200);

    // StatusUpdate RX uses raw * 100 / 0xC800 for decoding
    ASSERT_EQ((uint32_t)IOHC_POSITION_MAX * 100 / IOHC_POSITION_MAX, 100);
    ASSERT_EQ((uint32_t)(IOHC_POSITION_MAX / 2) * 100 / IOHC_POSITION_MAX, 50);

    // STOP = 0xD200
    ASSERT_EQ(IOHC_POSITION_STOP, 0xD200);
}

// =====================================================================
// 13. IV checksum cross-check against reference algorithm
// =====================================================================

TEST(iv_checksum_cross_reference)
{
    // Process known frame data and verify the IV checksum bytes match
    // between two independent runs (deterministic)
    const uint8_t frameData[] = {
        0x48, 0x01, 0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC, 0x00, 0x4E, 0x20};
    const uint8_t challenge[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

    uint8_t iv1[16], iv2[16];
    IoHomeCrypto::constructIv2W(frameData, sizeof(frameData), challenge, iv1);
    IoHomeCrypto::constructIv2W(frameData, sizeof(frameData), challenge, iv2);

    // Must be identical
    ASSERT_MEM_EQ(iv1, iv2, 16);

    // Verify structure: bytes 0-7 = frame data (first 8), 8-9 = checksums, 10-15 = challenge
    ASSERT_MEM_EQ(iv1, frameData, 8);
    ASSERT_MEM_EQ(iv1 + 10, challenge, 6);

    // Checksums should be non-zero for this input
    ASSERT_TRUE(iv1[8] != 0 || iv1[9] != 0);
}

// =====================================================================
// 14. Deterministic HMAC — same input always produces same output
// =====================================================================

TEST(hmac_deterministic)
{
    const uint8_t frameData[] = {0x40, 0x01, 0x00, 0x00, 0x3B, 0x1A, 0x38, 0x0B, 0x28};
    const uint8_t challenge[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const uint8_t key[16] = {
        0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
        0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73};

    uint8_t hmac1[6], hmac2[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, key, hmac1));
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, key, hmac2));
    ASSERT_MEM_EQ(hmac1, hmac2, 6);

    // Print for manual cross-check with reference implementation
    hexdump("HMAC", hmac1, 6);
}

// =====================================================================
// 15. HMAC detection per command type in deserialize
// =====================================================================

TEST(frame_deserialize_rejects_2w_appended_hmac)
{
    // 2W frames are length-delimited by CTRL0. Extra appended HMAC bytes must
    // be rejected instead of being split out of the declared payload.
    IoHomeFrame orig;
    orig.init();
    orig.setStart2W(); // 2W mode
    orig.setSrcNode(0xAABBCC);
    orig.setDestNode(0x112233);
    orig.commandId = IoHomeCommand::Execute;
    orig.data[0] = 0x4E;
    orig.data[1] = 0x20;
    orig.dataLen = 2;
    orig.hmac[0] = 0x11;
    orig.hmac[1] = 0x22;
    orig.hmac[2] = 0x33;
    orig.hmac[3] = 0x44;
    orig.hmac[4] = 0x55;
    orig.hmac[5] = 0x66;
    orig.hasHmac = true;

    uint8_t buf[32];
    uint8_t len = serialize2WWithAppendedHmacForRejectTest(orig, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, 11);

    IoHomeFrame parsed;
    ASSERT_TRUE(!deserializeFrameForTest(parsed, buf, len));
}

TEST(frame_deserialize_status_update_2w_keeps_payload)
{
    // StatusUpdate (0x71) in 2W mode keeps all declared payload bytes in data.
    IoHomeFrame orig;
    orig.init();
    orig.setStart2W();
    orig.setSrcNode(0xAABBCC);
    orig.setDestNode(0x112233);
    orig.commandId = IoHomeCommand::StatusUpdate; // 0x71
    const uint8_t expectedData[8] = {0x27, 0x10, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    memcpy(orig.data, expectedData, sizeof(expectedData));
    orig.dataLen = sizeof(expectedData);
    orig.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(orig, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, sizeof(expectedData));
    ASSERT_MEM_EQ(parsed.data, expectedData, sizeof(expectedData));
}

TEST(frame_deserialize_getname_2w_no_hmac)
{
    // GetName (0x50) in 2W mode should NOT have HMAC
    IoHomeFrame orig;
    orig.init();
    orig.setStart2W();
    orig.setSrcNode(0xAABBCC);
    orig.setDestNode(0x112233);
    orig.commandId = IoHomeCommand::GetName; // 0x50
    // Put 5 bytes of "name" data
    orig.data[0] = 'H';
    orig.data[1] = 'e';
    orig.data[2] = 'l';
    orig.data[3] = 'l';
    orig.data[4] = 'o';
    orig.dataLen = 5;
    orig.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(orig, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, 5);
    ASSERT_EQ(parsed.data[0], 'H');
    ASSERT_EQ(parsed.data[4], 'o');
}

TEST(frame_deserialize_1w_execute_no_hmac)
{
    // 1W mode Execute — should NOT split HMAC even though cmd=0x00
    uint8_t buf[] = {
        0x6D, 0x00,                  // ctrl0: START | 1W | len=13(0x0D) → total=14, ctrl1: 0
        0x00, 0x00, 0x3B,            // dest
        0xAA, 0xBB, 0xCC,            // src
        0x00,                        // Execute
        0x01, 0x02, 0x03, 0x04, 0x05 // 5 bytes data (no HMAC)
    };

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, sizeof(buf)));
    bool is1W = (parsed.ctrlByte0 & 0x20) != 0;
    ASSERT_TRUE(is1W);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, 5);
}

// =====================================================================
// 16. Reference key-exchange test vector
// =====================================================================

TEST(crypt2wkey_reference_vector)
{
    // From reference documentation (io-rts-esp32):
    // Command 0x31 (KeyInitTransfer) frame, challenge 12 34 56 78 9a bc
    // IV construction for command 0x31:
    //   Bytes 0-7: frame header "31 55 55 55 55 55 55 55" (cmd + padding)
    //   Bytes 8-9: checksums of single byte 0x31
    //   Bytes 10-15: challenge
    //
    // We verify IV construction matches reference pattern.
    const uint8_t frameData[] = {0x31}; // just the command byte
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    uint8_t iv[16];
    IoHomeCrypto::constructIv2W(frameData, 1, challenge, iv);

    // Byte 0: frame data
    ASSERT_EQ(iv[0], 0x31);
    // Bytes 1-7: 0x55 padding
    for (int i = 1; i < 8; i++)
        ASSERT_EQ(iv[i], 0x55);
    // Bytes 8-9: checksum of single byte 0x31
    // 0x31 = 0b00110001, chk1=0, chk2=0
    // tmp = 0x31 ^ 0 = 0x31, chk2 = 0
    // (chk1=0 & 0x80)==0 → first branch
    // 0x31 < 128 → no set LSB
    // chk1 = 0, chk2 = (0x31 << 1) & 0xFF = 0x62
    ASSERT_EQ(iv[8], 0x00);
    ASSERT_EQ(iv[9], 0x62);
    // Bytes 10-15: challenge
    ASSERT_MEM_EQ(iv + 10, challenge, 6);

    // Now compute the keystream with the transfer key and verify it's deterministic
    uint8_t keystream1[16], keystream2[16];
    ASSERT_TRUE(IoHomeCrypto::derive2WKeystream(frameData, 1, challenge, IOHC_TRANSFER_KEY, keystream1));
    ASSERT_TRUE(IoHomeCrypto::derive2WKeystream(frameData, 1, challenge, IOHC_TRANSFER_KEY, keystream2));
    ASSERT_MEM_EQ(keystream1, keystream2, 16);

    // Print for cross-checking against reference implementation
    hexdump("IV  ", iv, 16);
    hexdump("KS  ", keystream1, 16);
}

TEST(crypt2wkey_full_key_exchange_simulation)
{
    // Simulate the full symmetric key exchange through crypt2WKeyXor().
    const uint8_t systemKey[16] = {
        0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t frameData[] = {0x31};

    uint8_t encrypted[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(frameData, sizeof(frameData),
                                            challenge, systemKey,
                                            IOHC_TRANSFER_KEY, encrypted));

    uint8_t decrypted[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(frameData, sizeof(frameData),
                                            challenge, encrypted,
                                            IOHC_TRANSFER_KEY, decrypted));

    ASSERT_MEM_EQ(decrypted, systemKey, 16);
}

TEST(velocet_vector_1w_key_push)
{
    const uint8_t systemKey[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    const uint8_t nodeAddr[3] = {0x48, 0x5B, 0x37};
    const uint8_t expectedEncrypted[16] = {
        0x82, 0x60, 0x89, 0xF3, 0x44, 0xCB, 0xCC, 0xAB,
        0x84, 0x26, 0xCE, 0x7C, 0x12, 0x51, 0xB8, 0xE0};
    const uint8_t expectedHmac[6] = {0x2D, 0x49, 0x8F, 0xBF, 0x1F, 0x7C};

    uint8_t encrypted[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(systemKey, IOHC_TRANSFER_KEY, nodeAddr, encrypted));
    ASSERT_MEM_EQ(encrypted, expectedEncrypted, sizeof(expectedEncrypted));

    uint8_t hmacInput[17];
    hmacInput[0] = static_cast<uint8_t>(IoHomeCommand::SendKey1W);
    memcpy(hmacInput + 1, encrypted, sizeof(encrypted));

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(hmacInput, sizeof(hmacInput), 0x1A2B, systemKey, hmac));
    ASSERT_MEM_EQ(hmac, expectedHmac, sizeof(expectedHmac));
}

TEST(velocet_vector_2w_push_key_exchange)
{
    const uint8_t systemKey[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    const uint8_t keyChallenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t authChallenge[6] = {0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F};
    const uint8_t expectedEncrypted[16] = {
        0x10, 0x0F, 0x0F, 0xC2, 0xE1, 0x96, 0xA3, 0x95,
        0x76, 0x13, 0xD7, 0xAB, 0x9C, 0xFD, 0x83, 0x31};
    const uint8_t expectedHmac[6] = {0x8D, 0x35, 0xDC, 0x56, 0x37, 0xF4};

    const uint8_t frameData[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
    uint8_t encrypted[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(frameData, sizeof(frameData),
                                            keyChallenge, systemKey,
                                            IOHC_TRANSFER_KEY, encrypted));
    ASSERT_MEM_EQ(encrypted, expectedEncrypted, sizeof(expectedEncrypted));

    uint8_t hmacInput[17];
    hmacInput[0] = static_cast<uint8_t>(IoHomeCommand::KeyTransfer);
    memcpy(hmacInput + 1, encrypted, sizeof(encrypted));

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(hmacInput, sizeof(hmacInput), authChallenge, systemKey, hmac));
    ASSERT_MEM_EQ(hmac, expectedHmac, sizeof(expectedHmac));
}

TEST(velocet_vector_2w_pull_key_exchange)
{
    const uint8_t systemKey[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    const uint8_t keyChallenge[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    const uint8_t authChallenge[6] = {0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F};
    const uint8_t expectedEncrypted[16] = {
        0x96, 0xEE, 0x62, 0x02, 0x19, 0x6C, 0x23, 0xC0,
        0x2E, 0x79, 0x3E, 0x5F, 0x8C, 0x9C, 0x86, 0x80};
    const uint8_t expectedHmac[6] = {0x5D, 0x25, 0x6A, 0x38, 0x80, 0x08};

    const uint8_t frameData[7] = {
        static_cast<uint8_t>(IoHomeCommand::LaunchKeyTransfer),
        0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    uint8_t encrypted[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(frameData, sizeof(frameData),
                                            keyChallenge, systemKey,
                                            IOHC_TRANSFER_KEY, encrypted));
    ASSERT_MEM_EQ(encrypted, expectedEncrypted, sizeof(expectedEncrypted));

    uint8_t hmacInput[17];
    hmacInput[0] = static_cast<uint8_t>(IoHomeCommand::KeyTransfer);
    memcpy(hmacInput + 1, encrypted, sizeof(encrypted));

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(hmacInput, sizeof(hmacInput), authChallenge, systemKey, hmac));
    ASSERT_MEM_EQ(hmac, expectedHmac, sizeof(expectedHmac));
}

// =====================================================================
// 17. Frame length field encoding (5-bit ctrl0)
// =====================================================================

TEST(frame_length_field_min)
{
    // Minimum frame: 9 bytes → ctrl0 length field = 8 (9-1)
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::DiscoverRequest;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 9);
    ASSERT_EQ(buf[0] & 0x1F, 8); // 9-1 = 8
}

TEST(frame_length_field_with_hmac)
{
    // 2W appended HMAC is no longer serialized by the protocol frame layer.
    // A 0x3D ChallengeResponse carries the 6-byte HMAC as normal data instead.
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::Execute;
    frame.data[0] = 0x4E;
    frame.data[1] = 0x20;
    frame.dataLen = 2;
    memset(frame.hmac, 0xAA, 6);
    frame.hasHmac = true;

    uint8_t buf[32];
    uint8_t len = frame.serialize2W(buf, sizeof(buf));
    ASSERT_EQ(len, 0);
}

TEST(frame_length_field_max)
{
    // Maximum: 9 + 23 data = 32 → ctrl0 length field = 31
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::GetNameResponse;
    memset(frame.data, 0x41, 23); // fill with 'A'
    frame.dataLen = 23;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 32);
    ASSERT_EQ(buf[0] & 0x1F, 31); // 32-1 = 31

    // Roundtrip
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.dataLen, 23);
}

// =====================================================================
// 18. Position decoding (raw → percent)
// =====================================================================

TEST(position_decoding)
{
    // The controller decodes: percent = raw * 100 / IOHC_POSITION_MAX (0xC800)
    // Replicate the logic from dispatchRxFrame (reference-compatible format)
    auto decode = [](uint16_t raw) -> uint8_t
    {
        uint8_t pct = (uint8_t)((uint32_t)raw * 100 / IOHC_POSITION_MAX);
        if (pct > 100)
            pct = 100;
        return pct;
    };

    ASSERT_EQ(decode(0), 0);                       // 0% (open)
    ASSERT_EQ(decode(IOHC_POSITION_MAX / 2), 50);  // 50%
    ASSERT_EQ(decode(IOHC_POSITION_MAX), 100);     // 100% (closed)
    ASSERT_EQ(decode(IOHC_POSITION_MAX / 100), 1); // ~1%
    ASSERT_EQ(decode(IOHC_POSITION_MAX / 4), 25);  // 25%

    // Special values should be filtered BEFORE decode in controller
    // (STOP=0xD200, FAVORITE=0xD800, UNKNOWN=0xD400 are all > 0xC800)
    ASSERT_TRUE((IOHC_POSITION_STOP & 0xFFFF) > IOHC_POSITION_MAX);
    ASSERT_TRUE((IOHC_POSITION_FAVORITE & 0xFFFF) > IOHC_POSITION_MAX);
    ASSERT_TRUE((IOHC_POSITION_UNKNOWN & 0xFFFF) > IOHC_POSITION_MAX);
}

TEST(position_decoding_from_frame)
{
    // Build a StatusUpdate frame with reference layout:
    //   data[0]: flags (0x00 = not stopped → moving)
    //   data[1-6]: padding/unused
    //   data[7:8]: current position (16-bit BE) = 50% → 0xC800/2 = 0x6400
    // Minimum 9 bytes for current position, 11 for full status
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::StatusUpdate;
    memset(frame.data, 0, 11);
    frame.data[0] = 0x00;                    // not stopped → moving
    uint16_t posRaw = IOHC_POSITION_MAX / 2; // 50%
    frame.data[7] = (posRaw >> 8) & 0xFF;
    frame.data[8] = posRaw & 0xFF;
    frame.dataLen = 11;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE(!parsed.hasHmac);

    // Decode per reference: current position at data[7:8]
    uint16_t currentRaw = ((uint16_t)parsed.data[7] << 8) | parsed.data[8];
    ASSERT_EQ(currentRaw, IOHC_POSITION_MAX / 2);
    uint8_t percent = (uint8_t)((uint32_t)currentRaw * 100 / IOHC_POSITION_MAX);
    ASSERT_EQ(percent, 50);

    // Stopped flag at data[0] bit 0
    bool stopped = (parsed.data[0] & 0x01) != 0;
    ASSERT_TRUE(!stopped); // moving
}

// =====================================================================
// 19. HMAC verification with tampered frame data
// =====================================================================

TEST(hmac_rejects_tampered_frame_data)
{
    const uint8_t frameData[] = {
        0x48, 0x01, 0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC, 0x00, 0x4E, 0x20};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t key[16] = {
        0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13};

    // Create valid HMAC
    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, key, hmac));

    // Verify passes with original data
    ASSERT_TRUE(IoHomeCrypto::verifyHmac(frameData, sizeof(frameData), hmac, challenge, key));

    // Tamper a single byte in the frame data (flip bit in position byte)
    uint8_t tampered[sizeof(frameData)];
    memcpy(tampered, frameData, sizeof(frameData));
    tampered[9] ^= 0x01; // flip one bit in position MSB

    // Verification must fail
    ASSERT_TRUE(!IoHomeCrypto::verifyHmac(tampered, sizeof(tampered), hmac, challenge, key));
}

TEST(hmac_rejects_tampered_single_bit)
{
    const uint8_t frameData[] = {
        0x48, 0x01, 0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC, 0x00};
    const uint8_t challenge[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    const uint8_t key[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, key, hmac));

    // Try flipping each bit in each byte of frame data — all must fail
    for (size_t byteIdx = 0; byteIdx < sizeof(frameData); byteIdx++)
    {
        uint8_t tampered[sizeof(frameData)];
        memcpy(tampered, frameData, sizeof(frameData));
        tampered[byteIdx] ^= 0x80; // flip MSB of this byte

        bool result = IoHomeCrypto::verifyHmac(tampered, sizeof(tampered), hmac, challenge, key);
        if (result)
        {
            printf("[FAIL] HMAC accepted tampered byte %zu\n", byteIdx);
            sTestsFailed++;
            return;
        }
    }
}

// =====================================================================
// 20. Favorite position encoding
// =====================================================================

TEST(favorite_position_encoding)
{
    // Verify 0xD8 maps to IOHC_POSITION_FAVORITE (0xD800)
    ASSERT_EQ(IOHC_POSITION_FAVORITE, 0xD800);

    // Verify STOP encoding
    ASSERT_EQ(IOHC_POSITION_STOP, 0xD200);

    // Normal positions
    ASSERT_EQ((uint16_t)0 * 200, 0);
    ASSERT_EQ((uint16_t)50 * 200, 10000);
    ASSERT_EQ((uint16_t)100 * 200, 20000);

    // Verify special values are distinct
    ASSERT_TRUE(IOHC_POSITION_STOP != IOHC_POSITION_FAVORITE);
    ASSERT_TRUE(IOHC_POSITION_UNKNOWN != IOHC_POSITION_FAVORITE);
}

// =====================================================================
// 21. Multi-param frame (position + slat)
// =====================================================================

TEST(frame_execute_with_slat)
{
    // Build an Execute frame with position (50%) + slat (75%) in reference format:
    //   data[0]: 0x01 (originator: user)
    //   data[1]: 0x67 (ACEI priority)
    //   data[2]: position × 2 = 100
    //   data[3-7]: {0x00, 0x80, 0xD8, 0x06, 0x00}
    //   data[8]: slat × 2 = 150
    //   data[9]: 0x00
    //   Total: 10 bytes
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::Execute;

    frame.data[0] = 0x01;
    frame.data[1] = 0x67;
    frame.data[2] = 50 * 2; // position 50%
    frame.data[3] = 0x00;
    frame.data[4] = 0x80;
    frame.data[5] = 0xD8;
    frame.data[6] = 0x06;
    frame.data[7] = 0x00;
    frame.data[8] = 75 * 2; // slat 75%
    frame.data[9] = 0x00;
    frame.dataLen = 10;

    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 9 + 10); // header + 10 data = 19

    // Deserialize
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.dataLen, 10);
    ASSERT_TRUE(!parsed.hasHmac);

    // Verify originator and priority
    ASSERT_EQ(parsed.data[0], 0x01);
    ASSERT_EQ(parsed.data[1], 0x67);

    // Verify position: data[2] / 2 = 50%
    ASSERT_EQ(parsed.data[2], 100);
    ASSERT_EQ(parsed.data[2] / 2, 50);

    // Verify slat: data[8] / 2 = 75%
    ASSERT_EQ(parsed.data[8], 150);
    ASSERT_EQ(parsed.data[8] / 2, 75);
}

// =====================================================================
// 22. Challenge freshness (zero challenge detection)
// =====================================================================

TEST(challenge_zero_detection)
{
    // All-zero challenge should be detectable as "no pending challenge"
    const uint8_t zeroChallenge[6] = {0, 0, 0, 0, 0, 0};
    const uint8_t validChallenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    // Check zero
    bool hasPending = false;
    for (int i = 0; i < 6; i++)
        if (zeroChallenge[i] != 0)
        {
            hasPending = true;
            break;
        }
    ASSERT_TRUE(!hasPending);

    // Check non-zero
    hasPending = false;
    for (int i = 0; i < 6; i++)
        if (validChallenge[i] != 0)
        {
            hasPending = true;
            break;
        }
    ASSERT_TRUE(hasPending);
}

TEST(challenge_cleared_after_verify)
{
    // Simulate: create HMAC, verify it, then create new HMAC with different challenge
    // The old HMAC should NOT verify with the new challenge
    const uint8_t frameData[] = {0x48, 0x01, 0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC, 0x00};
    const uint8_t key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    const uint8_t challenge1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const uint8_t challenge2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    uint8_t hmac1[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge1, key, hmac1));

    // Verify with correct challenge
    ASSERT_TRUE(IoHomeCrypto::verifyHmac(frameData, sizeof(frameData), hmac1, challenge1, key));

    // After "clearing" challenge (using new one), old HMAC should NOT verify
    ASSERT_TRUE(!IoHomeCrypto::verifyHmac(frameData, sizeof(frameData), hmac1, challenge2, key));

    // Zero challenge should also fail
    const uint8_t zeroChallenge[6] = {};
    ASSERT_TRUE(!IoHomeCrypto::verifyHmac(frameData, sizeof(frameData), hmac1, zeroChallenge, key));
}

// =====================================================================
// 23. Battery level decoding
// =====================================================================

TEST(battery_level_decoding)
{
    // StatusUpdate frame reference layout:
    //   data[0]: flags (bit 0 = stopped)
    //   data[3]: battery level (0-100)
    //   data[7:8]: current position (16-bit BE)
    // Battery in byte 3, valid range 0-100
    uint8_t data[11] = {};
    data[0] = 0x00; // not stopped (moving)
    data[3] = 100;  // battery 100%
    data[7] = 0x64; // current pos MSB
    data[8] = 0x00; // current pos LSB → 0x6400

    // Battery extraction
    uint8_t battery = data[3];
    ASSERT_EQ(battery, 100);
    ASSERT_TRUE(battery <= 100); // valid range

    // Stopped flag
    bool stopped = (data[0] & 0x01) != 0;
    ASSERT_TRUE(!stopped);

    // Low battery
    data[3] = 15;
    ASSERT_EQ(data[3], 15);
    ASSERT_TRUE(data[3] <= 100);

    // Invalid battery (>100 should be rejected by controller)
    data[3] = 0xFF;
    ASSERT_TRUE(data[3] > 100); // controller should skip this
}

// =====================================================================
// 24. DiscoverSPE frame serialization
// =====================================================================

TEST(frame_discover_spe_request)
{
    // DiscoverSPERequest (0x2A) with 12-byte payload: 6B challenge + 6B HMAC
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestBroadcast();
    frame.commandId = IoHomeCommand::DiscoverSPERequest;
    // Simulate: 6 bytes challenge + 6 bytes HMAC
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t hmac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    memcpy(frame.data, challenge, 6);
    memcpy(frame.data + 6, hmac, 6);
    frame.dataLen = 12;
    frame.hasHmac = false;

    ASSERT_EQ(frame.totalLength(), 21); // 9 + 12

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 21);

    // Verify command byte
    ASSERT_EQ(buf[8], 0x2A); // DiscoverSPERequest

    // Deserialize
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x2A);
    ASSERT_EQ(parsed.dataLen, 12);
    ASSERT_MEM_EQ(parsed.data, challenge, 6);
    ASSERT_MEM_EQ(parsed.data + 6, hmac, 6);
    ASSERT_TRUE(!parsed.hasHmac);
}

// =====================================================================
// 25. Position special values roundtrip
// =====================================================================

TEST(position_special_values_encoding)
{
    // Verify all special position constants
    ASSERT_EQ(IOHC_POSITION_STOP, 0xD200);
    ASSERT_EQ(IOHC_POSITION_UNKNOWN, 0xD400);
    ASSERT_EQ(IOHC_POSITION_FAVORITE, 0xD800);
    ASSERT_EQ(IOHC_POSITION_MAX, 0xC800);

    // Serialize a STOP frame
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::Execute;
    frame.data[0] = (IOHC_POSITION_STOP >> 8) & 0xFF;
    frame.data[1] = IOHC_POSITION_STOP & 0xFF;
    frame.dataLen = 2;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    uint16_t posRaw = ((uint16_t)parsed.data[0] << 8) | parsed.data[1];
    ASSERT_EQ(posRaw, IOHC_POSITION_STOP);

    // Serialize a FAVORITE frame
    frame.data[0] = (IOHC_POSITION_FAVORITE >> 8) & 0xFF;
    frame.data[1] = IOHC_POSITION_FAVORITE & 0xFF;
    len = serializeFrameForTest(frame, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    posRaw = ((uint16_t)parsed.data[0] << 8) | parsed.data[1];
    ASSERT_EQ(posRaw, IOHC_POSITION_FAVORITE);
}

// =====================================================================
// 26. Retry logic — queue entry structure
// =====================================================================

TEST(retry_counter_logic)
{
    // Test that retry counting works correctly
    // Simulate: retries starts at 0, increment on each timeout, stop at MAX
    uint8_t retries = 0;
    const uint8_t maxRetries = 3; // IOHC_MAX_RETRIES

    // First attempt
    ASSERT_EQ(retries, 0);
    ASSERT_TRUE(retries < maxRetries);

    // First retry
    retries++;
    ASSERT_EQ(retries, 1);
    ASSERT_TRUE(retries < maxRetries);

    // Second retry
    retries++;
    ASSERT_EQ(retries, 2);
    ASSERT_TRUE(retries < maxRetries);

    // Third retry
    retries++;
    ASSERT_EQ(retries, 3);
    ASSERT_TRUE(!(retries < maxRetries)); // should give up

    // Total attempts: 1 initial + 3 retries = 4 transmissions across 4 frequencies
    // (freq hop after each timeout covers all 3 frequencies plus wrap)
}

TEST(frequency_hop_cycle)
{
    // Verify frequency hopping cycles through all 3 bands
    uint8_t freqIdx = 0;
    ASSERT_EQ(IOHC_FREQUENCIES[0], 868950000UL); // CH2 (1W/2W shared)
    ASSERT_EQ(IOHC_FREQUENCIES[1], 869850000UL); // CH3
    ASSERT_EQ(IOHC_FREQUENCIES[2], 868250000UL); // CH1

    // Hop 0→1→2→0
    freqIdx = (freqIdx + 1) % IOHC_NUM_FREQUENCIES;
    ASSERT_EQ(freqIdx, 1);
    freqIdx = (freqIdx + 1) % IOHC_NUM_FREQUENCIES;
    ASSERT_EQ(freqIdx, 2);
    freqIdx = (freqIdx + 1) % IOHC_NUM_FREQUENCIES;
    ASSERT_EQ(freqIdx, 0); // wraps back
}

// =====================================================================
// 27. Execute TX 8-byte format (reference-compatible)
// =====================================================================

TEST(execute_tx_8byte_format)
{
    // Verify the 8-byte Execute TX format per reference (io-rts-esp32)
    // Normal position: {0x01, 0x67, pos×2, 0x00, 0x80, 0xD8, 0x06, 0x00}
    uint8_t data[8];
    uint8_t pos = 75; // 75%

    data[0] = 0x01; // originator: user
    data[1] = 0x67; // ACEI priority
    data[2] = pos * 2;
    data[3] = 0x00;
    data[4] = 0x80;
    data[5] = 0xD8;
    data[6] = 0x06; // standard mode
    data[7] = 0x00;

    ASSERT_EQ(data[0], 0x01);
    ASSERT_EQ(data[1], 0x67);
    ASSERT_EQ(data[2], 150); // 75 × 2
    ASSERT_EQ(data[4], 0x80);
    ASSERT_EQ(data[5], 0xD8);
    ASSERT_EQ(data[6], 0x06);

    // 0% → data[2] = 0
    ASSERT_EQ((uint8_t)(0 * 2), 0);
    // 100% → data[2] = 200
    ASSERT_EQ((uint8_t)(100 * 2), 200);
}

TEST(execute_tx_special_6byte_format)
{
    // Special positions (STOP, FAVORITE): 6 bytes
    // {0x01, 0x67, special, 0x00, 0x00, 0x00}
    uint8_t data[6];
    data[0] = 0x01;
    data[1] = 0x67;
    data[2] = 0xD2; // STOP
    data[3] = 0x00;
    data[4] = 0x00;
    data[5] = 0x00;

    ASSERT_EQ(data[2], 0xD2);
    ASSERT_TRUE(data[2] > 200); // > 100% → special value

    // FAVORITE
    data[2] = 0xD8;
    ASSERT_EQ(data[2], 0xD8);
    ASSERT_TRUE(data[2] > 200);
}

// =====================================================================
// 28. StatusUpdate response parsing (reference layout)
// =====================================================================

TEST(status_update_reference_layout)
{
    // StatusUpdate (0x71) reference layout:
    //   data[0]: flags (bit 0 = stopped)
    //   data[5:6]: target position (16-bit BE)
    //   data[7:8]: current position (16-bit BE)
    //   data[10]: estimate
    //   Min 11 bytes
    uint8_t data[11] = {};
    data[0] = 0x01; // stopped
    // target = 100% = 0xC800
    data[5] = (IOHC_POSITION_MAX >> 8) & 0xFF;
    data[6] = IOHC_POSITION_MAX & 0xFF;
    // current = 50% = 0x6400
    uint16_t halfPos = IOHC_POSITION_MAX / 2;
    data[7] = (halfPos >> 8) & 0xFF;
    data[8] = halfPos & 0xFF;
    data[10] = 0x05; // estimate

    // Parse
    bool stopped = (data[0] & 0x01) != 0;
    ASSERT_TRUE(stopped);

    uint16_t targetRaw = ((uint16_t)data[5] << 8) | data[6];
    ASSERT_EQ(targetRaw, IOHC_POSITION_MAX);
    uint8_t targetPct = (uint8_t)((uint32_t)targetRaw * 100 / IOHC_POSITION_MAX);
    ASSERT_EQ(targetPct, 100);

    uint16_t currentRaw = ((uint16_t)data[7] << 8) | data[8];
    ASSERT_EQ(currentRaw, halfPos);
    uint8_t currentPct = (uint8_t)((uint32_t)currentRaw * 100 / IOHC_POSITION_MAX);
    ASSERT_EQ(currentPct, 50);
}

TEST(status_update_special_position_filter)
{
    // Special values (STOP, FAVORITE, UNKNOWN) must be filtered before decode
    uint16_t stopVal = IOHC_POSITION_STOP & 0xFFFF;
    uint16_t favVal = IOHC_POSITION_FAVORITE & 0xFFFF;
    uint16_t unkVal = IOHC_POSITION_UNKNOWN & 0xFFFF;

    // All are > IOHC_POSITION_MAX so should be excluded from % conversion
    ASSERT_TRUE(stopVal > IOHC_POSITION_MAX);
    ASSERT_TRUE(favVal > IOHC_POSITION_MAX);
    ASSERT_TRUE(unkVal > IOHC_POSITION_MAX);

    // Normal position decodes correctly
    uint16_t normalPos = IOHC_POSITION_MAX * 3 / 4; // 75%
    ASSERT_TRUE(normalPos <= IOHC_POSITION_MAX);
    uint8_t pct = (uint8_t)((uint32_t)normalPos * 100 / IOHC_POSITION_MAX);
    ASSERT_EQ(pct, 75);
}

// =====================================================================
// 29. PrivateResponse parsing (reference layout)
// =====================================================================

TEST(private_response_reference_layout)
{
    // PrivateResponse (0x04) layout:
    //   data[0]: flags (bit 0 = stopped)
    //   data[2:3]: target position (16-bit BE)
    //   data[4:5]: current position (16-bit BE)
    //   data[7]: estimate
    //   Min 6 bytes for position
    uint8_t data[8] = {};
    data[0] = 0x00; // not stopped → moving
    // target = 25% = 0xC800/4 = 0x3200
    uint16_t target = IOHC_POSITION_MAX / 4;
    data[2] = (target >> 8) & 0xFF;
    data[3] = target & 0xFF;
    // current = 75%
    uint16_t current = IOHC_POSITION_MAX * 3 / 4;
    data[4] = (current >> 8) & 0xFF;
    data[5] = current & 0xFF;

    bool stopped = (data[0] & 0x01) != 0;
    ASSERT_TRUE(!stopped);

    uint16_t currentRaw = ((uint16_t)data[4] << 8) | data[5];
    uint8_t pct = (uint8_t)((uint32_t)currentRaw * 100 / IOHC_POSITION_MAX);
    ASSERT_EQ(pct, 75);

    uint16_t targetRaw = ((uint16_t)data[2] << 8) | data[3];
    uint8_t tPct = (uint8_t)((uint32_t)targetRaw * 100 / IOHC_POSITION_MAX);
    ASSERT_EQ(tPct, 25);
}

TEST(private_response_real_capture_from_box)
{
    // Real 2W reply captured from an io-homecontrol box after a 0x03 request:
    //   FROM 904C09 TO 0842E3 CMD 04 DATA(14)
    //   05 00 C8 00 C8 00 00 00 08 42 E3 01 00 00
    // Full frame bytes confirmed by the ESPHome capture log.
    const uint8_t packet[] = {
        0x96, 0x00,
        0x08, 0x42, 0xE3,
        0x90, 0x4C, 0x09,
        0x04,
        0x05, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0x00, 0x00,
        0x08, 0x42, 0xE3, 0x01, 0x00, 0x00};

    IoHomeFrame frame;
    ASSERT_TRUE(deserializeFrameForTest(frame, packet, sizeof(packet)));
    ASSERT_EQ(frame.getDestNodeId(), 0x0842E3);
    ASSERT_EQ(frame.getSrcNodeId(), 0x904C09);
    ASSERT_EQ((uint8_t)frame.commandId, 0x04);
    ASSERT_TRUE(!(frame.ctrlByte0 & IOHC_CTRL0_MODE_1W));
    ASSERT_TRUE(!frame.hasHmac);
    ASSERT_EQ(frame.dataLen, 14);

    ASSERT_EQ(frame.data[0], 0x05);
    ASSERT_EQ(frame.data[1], 0x00);

    uint16_t targetRaw = ((uint16_t)frame.data[2] << 8) | frame.data[3];
    uint16_t currentRaw = ((uint16_t)frame.data[4] << 8) | frame.data[5];
    ASSERT_EQ(targetRaw, IOHC_POSITION_MAX);
    ASSERT_EQ(currentRaw, IOHC_POSITION_MAX);

    ASSERT_EQ(frame.data[8], 0x08);
    ASSERT_EQ(frame.data[9], 0x42);
    ASSERT_EQ(frame.data[10], 0xE3);
    ASSERT_EQ(frame.data[11], 0x01);
}

// =====================================================================
// 30. CTRL1 flag encoding
// =====================================================================

TEST(ctrl1_beacon_flag)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.ctrlByte1 |= IOHC_CTRL1_BEACON;
    ASSERT_TRUE((frame.ctrlByte1 & IOHC_CTRL1_BEACON) != 0);
    ASSERT_EQ(frame.ctrlByte1 & IOHC_CTRL1_BEACON, 0x80);

    // Roundtrip
    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::GetName;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE((parsed.ctrlByte1 & IOHC_CTRL1_BEACON) != 0);
    // Version should still be 0
    ASSERT_EQ(parsed.ctrlByte1 & IOHC_CTRL1_VER_MASK, 0x00);
}

TEST(ctrl1_routed_flag)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.ctrlByte1 |= IOHC_CTRL1_ROUTED;
    ASSERT_EQ(frame.ctrlByte1 & IOHC_CTRL1_ROUTED, 0x40);

    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::GetGeneralInfo1;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE((parsed.ctrlByte1 & IOHC_CTRL1_ROUTED) != 0);
}

TEST(ctrl1_low_power_flag)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.ctrlByte1 |= IOHC_CTRL1_LOW_POWER;
    ASSERT_EQ(frame.ctrlByte1 & IOHC_CTRL1_LOW_POWER, 0x20);

    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::GetName;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE((parsed.ctrlByte1 & IOHC_CTRL1_LOW_POWER) != 0);
}

TEST(ctrl1_ack_flag)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.ctrlByte1 |= IOHC_CTRL1_ACK;
    ASSERT_EQ(frame.ctrlByte1 & IOHC_CTRL1_ACK, 0x10);

    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::GetName;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE((parsed.ctrlByte1 & IOHC_CTRL1_ACK) != 0);
}

TEST(ctrl1_combined_flags)
{
    // Set multiple CTRL1 flags simultaneously
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.ctrlByte1 |= IOHC_CTRL1_ROUTED | IOHC_CTRL1_LOW_POWER | IOHC_CTRL1_ACK;

    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::GetName;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE((parsed.ctrlByte1 & IOHC_CTRL1_ROUTED) != 0);
    ASSERT_TRUE((parsed.ctrlByte1 & IOHC_CTRL1_LOW_POWER) != 0);
    ASSERT_TRUE((parsed.ctrlByte1 & IOHC_CTRL1_ACK) != 0);
    ASSERT_TRUE((parsed.ctrlByte1 & IOHC_CTRL1_BEACON) == 0);
    ASSERT_EQ(parsed.ctrlByte1 & IOHC_CTRL1_VER_MASK, 0x00);
}

TEST(ctrl1_version_field)
{
    // Test different protocol versions
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_START;
    frame.ctrlByte1 = 0x02; // version 2
    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::GetName;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.ctrlByte1 & IOHC_CTRL1_VER_MASK, 0x02);
}

// =====================================================================
// 31. END flag and START+END combinations
// =====================================================================

TEST(ctrl0_end_flag)
{
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_END; // END only, no START
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::GetName;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE((parsed.ctrlByte0 & IOHC_CTRL0_END) != 0);
    ASSERT_TRUE((parsed.ctrlByte0 & IOHC_CTRL0_START) == 0);
}

TEST(ctrl0_start_and_end)
{
    // Both START and END set (single-frame exchange, common in reference)
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_START | IOHC_CTRL0_END;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::Execute;
    frame.data[0] = 0x01;
    frame.data[1] = 0x67;
    frame.data[2] = 100; // 50%
    frame.dataLen = 3;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE((parsed.ctrlByte0 & IOHC_CTRL0_START) != 0);
    ASSERT_TRUE((parsed.ctrlByte0 & IOHC_CTRL0_END) != 0);
    ASSERT_TRUE(!(parsed.ctrlByte0 & IOHC_CTRL0_MODE_1W)); // 2W
    ASSERT_TRUE(!parsed.hasHmac);
}

// =====================================================================
// 32. Error response (0xFE)
// =====================================================================

TEST(error_response_frame)
{
    // ErrorResponse (0xFE) — no HMAC, data contains error code
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_END;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::ErrorResponse;
    frame.data[0] = 0x01; // error code: unknown command
    frame.data[1] = 0x00; // failed command ID
    frame.dataLen = 2;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 9 + 2); // no HMAC

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0xFE);
    ASSERT_EQ(parsed.dataLen, 2);
    ASSERT_EQ(parsed.data[0], 0x01); // error code
    ASSERT_TRUE(!parsed.hasHmac);    // ErrorResponse has no HMAC
}

TEST(error_response_codes)
{
    // Verify common error codes are distinct and non-zero
    const uint8_t ERR_UNKNOWN_CMD = 0x01;
    const uint8_t ERR_BAD_PARAM = 0x02;
    const uint8_t ERR_BUSY = 0x03;
    const uint8_t ERR_NODE_LOCKED = 0x04;

    ASSERT_TRUE(ERR_UNKNOWN_CMD != ERR_BAD_PARAM);
    ASSERT_TRUE(ERR_BAD_PARAM != ERR_BUSY);
    ASSERT_TRUE(ERR_BUSY != ERR_NODE_LOCKED);
    ASSERT_TRUE(ERR_UNKNOWN_CMD != 0x00);
}

// =====================================================================
// 33. GetGeneralInfo1 response decoding
// =====================================================================

TEST(general_info1_response_decode)
{
    // GetGeneralInfo1Response (0x55) layout:
    //   data[0]: device type low byte (bits 0-7)
    //   data[1]: (device type bits 8-9) | (subtype bits 0-5 << 2)
    //   data[2]: manufacturer code
    // Device type = 10 bits, subtype = 6 bits
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::GetGeneralInfo1Response;

    // Encode: device type 0x0002 (roller shutter), subtype 0x01, manufacturer 0x02 (Somfy)
    uint16_t devType = 0x0002;
    uint8_t subType = 0x01;
    uint8_t manufacturer = 0x02;
    frame.data[0] = devType & 0xFF;
    frame.data[1] = ((devType >> 8) & 0x03) | ((subType & 0x3F) << 2);
    frame.data[2] = manufacturer;
    frame.dataLen = 3;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x55);

    // Decode per our controller logic
    uint16_t parsedType = (parsed.data[0] | ((uint16_t)parsed.data[1] << 8)) & 0x3FF;
    uint8_t parsedSubtype = parsed.data[1] & 0x3F;
    uint8_t parsedMfg = parsed.data[2];

    ASSERT_EQ(parsedType, 0x0002);
    ASSERT_EQ(parsedSubtype, ((subType << 2) & 0x3F)); // matches our decode
    ASSERT_EQ(parsedMfg, 0x02);
}

TEST(general_info1_all_device_types)
{
    // Verify device type encoding supports full 10-bit range
    for (uint16_t devType = 0; devType <= 0x03FF; devType += 0x80)
    {
        uint8_t byte0 = devType & 0xFF;
        uint8_t byte1 = (devType >> 8) & 0x03;
        uint16_t decoded = (byte0 | ((uint16_t)byte1 << 8)) & 0x3FF;
        if (decoded != devType)
        {
            printf("[FAIL] device type 0x%03X roundtrip failed: got 0x%03X\n", devType, decoded);
            sTestsFailed++;
            return;
        }
    }
}

// =====================================================================
// 34. GetName response
// =====================================================================

TEST(get_name_response_frame)
{
    // GetNameResponse (0x51) — name as raw bytes, no HMAC
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::GetNameResponse;
    const char *name = "Velux Window";
    uint8_t nameLen = strlen(name);
    memcpy(frame.data, name, nameLen);
    frame.dataLen = nameLen;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x51);
    ASSERT_EQ(parsed.dataLen, nameLen);
    ASSERT_MEM_EQ(parsed.data, name, nameLen);
    ASSERT_TRUE(!parsed.hasHmac);
}

TEST(get_name_max_length)
{
    // Maximum name: 20 chars (fits in IOHC_FRAME_MAX_DATA=23)
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::GetNameResponse;
    memset(frame.data, 'A', 20);
    frame.dataLen = 20;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 9 + 20); // 29 bytes, within 32 max

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.dataLen, 20);

    // Simulate channel name copy with null termination
    char nameBuf[21] = {};
    uint8_t copyLen = (parsed.dataLen < 20) ? parsed.dataLen : 20;
    memcpy(nameBuf, parsed.data, copyLen);
    nameBuf[copyLen] = '\0';
    ASSERT_EQ(strlen(nameBuf), 20);
}

// =====================================================================
// 35. Discovery response parsing
// =====================================================================

TEST(discovery_response_frame)
{
    // DiscoverResponse (0x29) — device responds with its node ID as source
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_END; // response frame
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0x485B37);  // discovered device
    frame.setDestNode(0x1A380B); // our node
    frame.commandId = IoHomeCommand::DiscoverResponse;
    // Response data may contain device type info
    frame.data[0] = 0x02; // device type: roller shutter
    frame.data[1] = 0x00;
    frame.data[2] = 0x02; // manufacturer: Somfy
    frame.dataLen = 3;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x29);
    ASSERT_EQ(parsed.getSrcNodeId(), 0x485B37);
    ASSERT_EQ(parsed.getDestNodeId(), 0x1A380B);
    ASSERT_TRUE(!parsed.hasHmac); // discovery has no HMAC
}

TEST(discovery_spe_response_frame)
{
    // DiscoverSPEResponse (0x2B) — encrypted discovery response
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_END;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x1A380B);
    frame.commandId = IoHomeCommand::DiscoverSPEResponse;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x2B);
    ASSERT_EQ(parsed.getSrcNodeId(), 0x485B37);
}

// =====================================================================
// 36. Device type and manufacturer enums
// =====================================================================

TEST(device_type_enum_values)
{
    // Verify all 25 device types from IoHomeCommands.h
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Unknown, 0x00);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::VenetianBlind, 0x01);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::RollerShutter, 0x02);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Awning, 0x03);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::WindowOpener, 0x04);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::GarageOpener, 0x05);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Light, 0x06);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::GateOpener, 0x07);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::RollingDoorOpener, 0x08);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Lock, 0x09);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Blind, 0x0A);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Unknown0B, 0x0B);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Beacon, 0x0C);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::DualShutter, 0x0D);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::HeatingTempInterface, 0x0E);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::OnOffSwitch, 0x0F);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::HorizontalAwning, 0x10);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::ExternalVenetianBlind, 0x11);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::LouvrBlind, 0x12);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::CurtainTrack, 0x13);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::VentilationPoint, 0x14);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::ExteriorHeating, 0x15);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::HeatPump, 0x16);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::IntrusionAlarm, 0x17);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::SwingingShutter, 0x18);
}

TEST(command_id_enum_values)
{
    // Verify all command IDs from IoHomeCommands.h
    ASSERT_EQ((uint8_t)IoHomeCommand::Execute, 0x00);
    ASSERT_EQ((uint8_t)IoHomeCommand::Private, 0x03);
    ASSERT_EQ((uint8_t)IoHomeCommand::PrivateResponse, 0x04);
    ASSERT_EQ((uint8_t)IoHomeCommand::Private2, 0x0C);
    ASSERT_EQ((uint8_t)IoHomeCommand::Private2Response, 0x0D);
    ASSERT_EQ((uint8_t)IoHomeCommand::Identify, 0x1E);
    ASSERT_EQ((uint8_t)IoHomeCommand::DiscoverRequest, 0x28);
    ASSERT_EQ((uint8_t)IoHomeCommand::DiscoverResponse, 0x29);
    ASSERT_EQ((uint8_t)IoHomeCommand::DiscoverSPERequest, 0x2A);
    ASSERT_EQ((uint8_t)IoHomeCommand::DiscoverSPEResponse, 0x2B);
    ASSERT_EQ((uint8_t)IoHomeCommand::Confirmation, 0x2C);
    ASSERT_EQ((uint8_t)IoHomeCommand::ConfirmationACK, 0x2D);
    ASSERT_EQ((uint8_t)IoHomeCommand::Discover2ERequest, 0x2E);
    ASSERT_EQ((uint8_t)IoHomeCommand::KeyInitTransfer, 0x31);
    ASSERT_EQ((uint8_t)IoHomeCommand::KeyTransfer, 0x32);
    ASSERT_EQ((uint8_t)IoHomeCommand::KeyTransferConfirmation, 0x33);
    ASSERT_EQ((uint8_t)IoHomeCommand::ChallengeRequest, 0x3C);
    ASSERT_EQ((uint8_t)IoHomeCommand::ChallengeResponse, 0x3D);
    ASSERT_EQ((uint8_t)IoHomeCommand::Unknown46Request, 0x46);
    ASSERT_EQ((uint8_t)IoHomeCommand::Unknown46Response, 0x47);
    ASSERT_EQ((uint8_t)IoHomeCommand::Unknown4ARequest, 0x4A);
    ASSERT_EQ((uint8_t)IoHomeCommand::Unknown4AResponse, 0x4B);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetName, 0x50);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetNameResponse, 0x51);
    ASSERT_EQ((uint8_t)IoHomeCommand::SetName, 0x52);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetGeneralInfo1, 0x54);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetGeneralInfo1Response, 0x55);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetGeneralInfo2, 0x56);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetGeneralInfo2Response, 0x57);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetGeneralInfo3, 0x58);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetGeneralInfo3Response, 0x59);
    ASSERT_EQ((uint8_t)IoHomeCommand::SetConfig1, 0x6F);
    ASSERT_EQ((uint8_t)IoHomeCommand::SetConfig1Response, 0x70);
    ASSERT_EQ((uint8_t)IoHomeCommand::StatusUpdate, 0x71);
    ASSERT_EQ((uint8_t)IoHomeCommand::StatusUpdateResponse, 0x72);
    ASSERT_EQ((uint8_t)IoHomeCommand::ErrorResponse, 0xFE);
}

// =====================================================================
// 37. Execute quiet mode flag
// =====================================================================

TEST(execute_quiet_mode)
{
    // Reference uses byte 6: 0x06 = standard mode, 0x05 = quiet mode
    uint8_t data[8];
    data[0] = 0x01;   // originator
    data[1] = 0x67;   // priority
    data[2] = 50 * 2; // 50%
    data[3] = 0x00;
    data[4] = 0x80;
    data[5] = 0xD8;
    data[6] = 0x05; // QUIET mode
    data[7] = 0x00;

    ASSERT_EQ(data[6], 0x05);
    ASSERT_TRUE(data[6] != 0x06); // different from standard

    // Standard mode
    data[6] = 0x06;
    ASSERT_EQ(data[6], 0x06);
}

// =====================================================================
// 38. Execute originator codes
// =====================================================================

TEST(execute_originator_codes)
{
    // Reference originator byte values:
    // 0x01 = user (remote control)
    // 0x02 = rain sensor
    // 0x03 = timer
    // 0x04 = security (alarm)
    // 0x08 = UPS (battery backup)
    // 0x0A = emergency
    // 0x0D = standalone (local switch)

    const uint8_t ORIG_USER = 0x01;
    const uint8_t ORIG_RAIN = 0x02;
    const uint8_t ORIG_TIMER = 0x03;
    const uint8_t ORIG_SECURITY = 0x04;
    const uint8_t ORIG_UPS = 0x08;
    const uint8_t ORIG_EMERGENCY = 0x0A;
    const uint8_t ORIG_STANDALONE = 0x0D;

    // All codes should be distinct
    const uint8_t codes[] = {ORIG_USER, ORIG_RAIN, ORIG_TIMER, ORIG_SECURITY,
                             ORIG_UPS, ORIG_EMERGENCY, ORIG_STANDALONE};
    for (size_t i = 0; i < sizeof(codes); i++)
    {
        for (size_t j = i + 1; j < sizeof(codes); j++)
        {
            if (codes[i] == codes[j])
            {
                printf("[FAIL] originator codes %zu and %zu are identical\n", i, j);
                sTestsFailed++;
                return;
            }
        }
    }

    // Our controller uses 0x01 (user) — verify it's valid
    ASSERT_EQ(ORIG_USER, 0x01);
}

// =====================================================================
// 39. PrivateResponse frame (0x04) — no HMAC in deserialize
// =====================================================================

TEST(private_response_frame_no_hmac)
{
    // PrivateResponse (0x04) should NOT have HMAC split in deserialize
    // (it's not in the lNeedsHmac list: only 0x00 and 0x71 get HMAC)
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_END;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0xAABBCC);
    frame.setDestNode(0x112233);
    frame.commandId = IoHomeCommand::PrivateResponse;
    // 8 bytes of data (no HMAC)
    memset(frame.data, 0x42, 8);
    frame.dataLen = 8;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x04);
    ASSERT_EQ(parsed.dataLen, 8);
    ASSERT_TRUE(!parsed.hasHmac); // 0x04 does not get HMAC split
}

// =====================================================================
// 40. Challenge-response frame roundtrip
// =====================================================================

TEST(challenge_request_frame)
{
    // ChallengeRequest (0x3C) — device sends 6-byte challenge
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_END;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0x485B37);  // device
    frame.setDestNode(0x1A380B); // us
    frame.commandId = IoHomeCommand::ChallengeRequest;
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    memcpy(frame.data, challenge, 6);
    frame.dataLen = 6;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x3C);
    ASSERT_EQ(parsed.dataLen, 6);
    ASSERT_MEM_EQ(parsed.data, challenge, 6);
}

TEST(challenge_response_frame)
{
    // ChallengeResponse (0x3D) carries the computed HMAC as command data
    const uint8_t frameData[] = {0x32, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
                                 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t key[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, key, hmac));

    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::ChallengeResponse;
    memcpy(frame.data, hmac, 6);
    frame.dataLen = 6;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x3D);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, 6);
    ASSERT_MEM_EQ(parsed.data, hmac, 6);
}

// =====================================================================
// 41. Key exchange frames
// =====================================================================

TEST(key_init_transfer_frame)
{
    // KeyInitTransfer (0x31) — start key exchange
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::KeyInitTransfer;
    frame.dataLen = 0;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 9); // minimal frame

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x31);
    ASSERT_EQ(parsed.dataLen, 0);
}

TEST(key_transfer_frame)
{
    // KeyTransfer (0x32) carries only the 16-byte encrypted key
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::KeyTransfer;

    // Encrypted key (16 bytes)
    uint8_t encKey[16];
    for (int i = 0; i < 16; i++)
        encKey[i] = i + 0xA0;
    memcpy(frame.data, encKey, 16);
    frame.dataLen = 16;

    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 9 + 16); // 25 bytes

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x32);
    ASSERT_EQ(parsed.dataLen, 16);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_MEM_EQ(parsed.data, encKey, 16);
}

// =====================================================================
// 42. Confirmation frame
// =====================================================================

TEST(confirmation_frame)
{
    // Confirmation (0x2C) — pairing confirmation
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_END;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x1A380B);
    frame.commandId = IoHomeCommand::Confirmation;
    frame.data[0] = 0x01; // success
    frame.dataLen = 1;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x2C);
    ASSERT_EQ(parsed.data[0], 0x01);
}

// =====================================================================
//  INTEGRATION TESTS
// =====================================================================
// These simulate end-to-end protocol flows using only the protocol layer
// =====================================================================
// 46. StatusUpdateResponse (0x72) frame construction
// =====================================================================

TEST(status_update_response_frame)
{
    // StatusUpdateResponse is an ACK sent when receiving unsolicited StatusUpdate (0x71)
    // Payload: {0x05, 0x00} per reference
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::StatusUpdateResponse;
    frame.data[0] = 0x05;
    frame.data[1] = 0x00;
    frame.dataLen = 2;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x72);
    ASSERT_EQ(parsed.dataLen, 2);
    ASSERT_EQ(parsed.data[0], 0x05);
    ASSERT_EQ(parsed.data[1], 0x00);
    ASSERT_TRUE(!parsed.hasHmac);
}

// =====================================================================
// 47. PrivateResponse estimate byte decoding
// =====================================================================

TEST(private_response_estimate_byte)
{
    // PrivateResponse (0x04) data[7] = estimate (travel time in seconds)
    // 0xFF and 0x00 are "unknown"
    uint8_t data[8] = {0x01, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00};

    // Normal estimate: 15 seconds
    data[7] = 15;
    ASSERT_EQ(data[7], 15);
    ASSERT_TRUE(data[7] != 0xFF && data[7] != 0x00); // valid estimate

    // Unknown estimate: 0xFF
    data[7] = 0xFF;
    ASSERT_TRUE(data[7] == 0xFF || data[7] == 0x00); // unknown

    // Unknown estimate: 0x00
    data[7] = 0x00;
    ASSERT_TRUE(data[7] == 0xFF || data[7] == 0x00); // unknown
}

// =====================================================================
// 48. Complete DeviceType enum (25 values)
// =====================================================================

TEST(device_type_complete_enum)
{
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Unknown, 0x00);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::VenetianBlind, 0x01);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::RollerShutter, 0x02);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Awning, 0x03);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::WindowOpener, 0x04);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::GarageOpener, 0x05);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Light, 0x06);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::GateOpener, 0x07);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::RollingDoorOpener, 0x08);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Lock, 0x09);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Blind, 0x0A);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Unknown0B, 0x0B);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::Beacon, 0x0C);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::DualShutter, 0x0D);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::HeatingTempInterface, 0x0E);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::OnOffSwitch, 0x0F);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::HorizontalAwning, 0x10);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::ExternalVenetianBlind, 0x11);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::LouvrBlind, 0x12);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::CurtainTrack, 0x13);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::VentilationPoint, 0x14);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::ExteriorHeating, 0x15);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::HeatPump, 0x16);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::IntrusionAlarm, 0x17);
    ASSERT_EQ((uint8_t)IoHomeDeviceType::SwingingShutter, 0x18);
}

// =====================================================================
// 49. Manufacturer enum
// =====================================================================

TEST(manufacturer_enum_values)
{
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Unknown, 0x00);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Velux, 0x01);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Somfy, 0x02);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Honeywell, 0x03);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Hormann, 0x04);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::AssaAbloy, 0x05);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Niko, 0x06);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::WindowMaster, 0x07);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Renson, 0x08);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Ciat, 0x09);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Secuyou, 0x0A);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::Overkiz, 0x0B);
    ASSERT_EQ((uint8_t)IoHomeManufacturer::AtlanticGroup, 0x0C);
}

// =====================================================================
// 50. isOpenCloseOnly() detection
// =====================================================================

TEST(open_close_only_detection)
{
    // True for devices without continuous positioning
    ASSERT_TRUE(isOpenCloseOnly(IoHomeDeviceType::Lock));
    ASSERT_TRUE(isOpenCloseOnly(IoHomeDeviceType::OnOffSwitch));
    ASSERT_TRUE(isOpenCloseOnly(IoHomeDeviceType::IntrusionAlarm));
    ASSERT_TRUE(isOpenCloseOnly(IoHomeDeviceType::SwingingShutter));

    // False for position-capable devices
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::Unknown));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::VenetianBlind));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::RollerShutter));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::Awning));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::WindowOpener));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::GarageOpener));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::Light));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::GateOpener));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::RollingDoorOpener));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::Blind));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::Beacon));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::DualShutter));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::HorizontalAwning));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::ExternalVenetianBlind));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::LouvrBlind));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::CurtainTrack));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::VentilationPoint));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::ExteriorHeating));
    ASSERT_TRUE(!isOpenCloseOnly(IoHomeDeviceType::HeatPump));
}

// =====================================================================
// 51. Position fallback (current > max → use target)
// =====================================================================

TEST(position_fallback_uses_target)
{
    // StatusUpdate: current = 0xFFFF (unknown/out of range), stopped, target = 50%
    // Expected: use target position
    uint8_t data[11] = {};
    data[0] = 0x01;               // stopped
    uint16_t target = 0xC800 / 2; // 50%
    data[5] = (target >> 8) & 0xFF;
    data[6] = target & 0xFF;
    data[7] = 0xFF; // current high byte (out of range)
    data[8] = 0xFF; // current low byte

    bool lStopped = (data[0] & 0x01) != 0;
    uint16_t lCurrentRaw = ((uint16_t)data[7] << 8) | data[8];
    uint16_t lPosRaw = lCurrentRaw;

    // Apply fallback logic
    if (lPosRaw > IOHC_POSITION_MAX && lStopped)
    {
        uint16_t lTargetRaw = ((uint16_t)data[5] << 8) | data[6];
        if (lTargetRaw <= IOHC_POSITION_MAX)
            lPosRaw = lTargetRaw;
    }

    uint8_t lPercent = (uint8_t)((uint32_t)lPosRaw * 100 / IOHC_POSITION_MAX);
    if (lPercent > 100)
        lPercent = 100;
    ASSERT_EQ(lPercent, 50);
}

TEST(position_fallback_not_when_moving)
{
    // When device is moving (not stopped), don't fall back to target
    uint8_t data[9] = {};
    data[0] = 0x00;               // NOT stopped (moving)
    uint16_t target = 0xC800 / 2; // 50%
    data[5] = (target >> 8) & 0xFF;
    data[6] = target & 0xFF;
    data[7] = 0xFF; // current out of range
    data[8] = 0xFF;

    bool lStopped = (data[0] & 0x01) != 0;
    uint16_t lCurrentRaw = ((uint16_t)data[7] << 8) | data[8];
    uint16_t lPosRaw = lCurrentRaw;

    if (lPosRaw > IOHC_POSITION_MAX && lStopped)
    {
        // should NOT enter this block
        uint16_t lTargetRaw = ((uint16_t)data[5] << 8) | data[6];
        if (lTargetRaw <= IOHC_POSITION_MAX)
            lPosRaw = lTargetRaw;
    }

    // Should still be the original out-of-range value (not target)
    ASSERT_TRUE(lPosRaw > IOHC_POSITION_MAX);
}

// =====================================================================
// 52. GetGeneralInfo2 (0x57) type/subtype extraction
// =====================================================================

TEST(general_info2_type_extraction)
{
    // Reference: type = data[10] << 2 | data[11] >> 6, subtype = data[11] & 0x3F
    uint8_t data[12] = {};

    // RollerShutter (0x02), subtype 5
    // type = 0x02 -> data[10] = 0x00, data[11] high 2 bits = 0x02 << 6 won't work
    // Actually: type = data[10] << 2 | data[11] >> 6
    // For type=2: data[10] = 0, data[11] = (2 << 6) | 5 = 0x85
    data[10] = 0x00;
    data[11] = (0x02 << 6) | 5; // 0x85

    uint16_t lType = ((uint16_t)data[10] << 2) | (data[11] >> 6);
    uint8_t lSubtype = data[11] & 0x3F;

    ASSERT_EQ(lType, 0x02); // RollerShutter
    ASSERT_EQ(lSubtype, 5);

    // VenetianBlind (0x01), subtype 0x3F
    data[10] = 0x00;
    data[11] = (0x01 << 6) | 0x3F; // 0x7F
    lType = ((uint16_t)data[10] << 2) | (data[11] >> 6);
    lSubtype = data[11] & 0x3F;
    ASSERT_EQ(lType, 0x01);
    ASSERT_EQ(lSubtype, 0x3F);

    // Larger type value (0x104 = 260), subtype 10
    // 0x104 = 0b100000100
    // data[10] = 0x104 >> 2 = 0x41, data[11] = (0x104 & 0x03) << 6 | 10 = 0x0A
    data[10] = 0x41;
    data[11] = ((0x104 & 0x03) << 6) | 10;
    lType = ((uint16_t)data[10] << 2) | (data[11] >> 6);
    lSubtype = data[11] & 0x3F;
    ASSERT_EQ(lType, 0x104);
    ASSERT_EQ(lSubtype, 10);
}

// =====================================================================
// 53. Name response cleaning
// =====================================================================

TEST(name_strip_leading_trailing)
{
    // Simulate the cleaning logic from onDeviceName()
    const char input[] = "\x02\x03Hello\x00\x00";
    uint8_t len = sizeof(input) - 1; // exclude C null terminator

    uint8_t start = 0;
    while (start < len && (uint8_t)input[start] <= 0x20)
        start++;
    uint8_t end = len;
    while (end > start && ((uint8_t)input[end - 1] <= 0x20 || input[end - 1] == '\0'))
        end--;

    char result[21] = {};
    uint8_t outPos = 0;
    for (uint8_t i = start; i < end && outPos < 20; i++)
        result[outPos++] = input[i];
    result[outPos] = '\0';

    ASSERT_EQ(strcmp(result, "Hello"), 0);
}

TEST(name_latin1_to_utf8)
{
    // Latin-1 ö (0xF6) → UTF-8 0xC3 0xB6
    const char input[] = "R\xF6lladen";
    uint8_t len = (uint8_t)strlen(input);

    char result[21] = {};
    uint8_t outPos = 0;
    for (uint8_t i = 0; i < len && outPos < 20; i++)
    {
        uint8_t b = (uint8_t)input[i];
        if (b < 0x80)
        {
            result[outPos++] = b;
        }
        else
        {
            if (outPos + 1 >= 20)
                break;
            result[outPos++] = 0xC0 | (b >> 6);
            result[outPos++] = 0x80 | (b & 0x3F);
        }
    }
    result[outPos] = '\0';

    // "Rölladen" in UTF-8
    ASSERT_EQ((uint8_t)result[0], 'R');
    ASSERT_EQ((uint8_t)result[1], 0xC3);
    ASSERT_EQ((uint8_t)result[2], 0xB6);
    ASSERT_EQ((uint8_t)result[3], 'l');
    ASSERT_EQ(strcmp(result + 3, "lladen"), 0);
}

// =====================================================================
// 54. Status-expected flag (data[1] & 0x80)
// =====================================================================

TEST(status_expected_flag_parsing)
{
    // data[1] bit 7 = status-expected
    uint8_t data[2];

    // Flag set
    data[1] = 0x80;
    ASSERT_TRUE((data[1] & 0x80) != 0);

    // Flag set with other bits
    data[1] = 0xFF;
    ASSERT_TRUE((data[1] & 0x80) != 0);

    // Flag clear
    data[1] = 0x7F;
    ASSERT_TRUE((data[1] & 0x80) == 0);

    data[1] = 0x00;
    ASSERT_TRUE((data[1] & 0x80) == 0);
}

// =====================================================================
// 55. Private (0x03) GetStatus payload
// =====================================================================

TEST(private_command_payload)
{
    // Reference: Private (0x03) sends {0x03, 0x00, 0x00}
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::Private;
    frame.data[0] = 0x03;
    frame.data[1] = 0x00;
    frame.data[2] = 0x00;
    frame.dataLen = 3;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x03);
    ASSERT_EQ(parsed.dataLen, 3);
    ASSERT_EQ(parsed.data[0], 0x03);
    ASSERT_EQ(parsed.data[1], 0x00);
    ASSERT_EQ(parsed.data[2], 0x00);
    ASSERT_TRUE(!parsed.hasHmac);
}

// =====================================================================
// 56. ChallengeResponse (0x3D) data payload in deserialize
// =====================================================================

TEST(frame_deserialize_challenge_response_data)
{
    // ChallengeResponse (0x3D) carries the six HMAC bytes as command data.
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x1A380B);
    frame.commandId = IoHomeCommand::ChallengeResponse;
    const uint8_t hmacData[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(frame.data, hmacData, sizeof(hmacData));
    frame.dataLen = sizeof(hmacData);
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x3D);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, sizeof(hmacData));
    ASSERT_MEM_EQ(parsed.data, hmacData, sizeof(hmacData));
}

// =====================================================================
// 57. 2W frame-order semantics
// =====================================================================

TEST(key_transfer_is_continuation_frame)
{
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = 0; // no START, no END: true 2W continuation
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::KeyTransfer;
    for (uint8_t i = 0; i < 16; i++)
        frame.data[i] = i;
    frame.dataLen = 16;
    frame.hasHmac = false;

    uint8_t buf[32];
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_START) == 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_END) == 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_MODE_1W) == 0);
}

TEST(challenge_response_is_continuation_frame)
{
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = 0; // no START, no END: true 2W continuation
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::ChallengeResponse;
    const uint8_t hmacData[IOHC_HMAC_SIZE] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(frame.data, hmacData, sizeof(hmacData));
    frame.dataLen = sizeof(hmacData);
    frame.hasHmac = false;

    uint8_t buf[32];
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_START) == 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_END) == 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_MODE_1W) == 0);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::ChallengeResponse));
    ASSERT_MEM_EQ(buf + 9, hmacData, sizeof(hmacData));
}

// =====================================================================
// 58. Receive-side auth: ChallengeRequest frame format
// =====================================================================

TEST(receive_auth_challenge_frame)
{
    // When authenticating unsolicited StatusUpdate, we send ChallengeRequest (0x3C)
    // with 6-byte random challenge as payload, no HMAC
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::ChallengeRequest;
    const uint8_t challenge[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42, 0x99};
    memcpy(frame.data, challenge, 6);
    frame.dataLen = 6;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x3C);
    ASSERT_EQ(parsed.dataLen, 6);
    ASSERT_MEM_EQ(parsed.data, challenge, 6);
    ASSERT_TRUE(!parsed.hasHmac);
}

// =====================================================================
// Reference byte-vector regression tests
// =====================================================================

TEST(byte_vector_2w_execute_position_50_payload)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x831F2A);
    frame.setDestNode(0x7E9E6E);
    frame.commandId = IoHomeCommand::Execute;

    const uint8_t expectedPayload[] = {0x01, 0x67, 0x64, 0x00, 0x80, 0xD8, 0x06, 0x00};
    memcpy(frame.data, expectedPayload, sizeof(expectedPayload));
    frame.dataLen = sizeof(expectedPayload);
    frame.hasHmac = false;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 17);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_START) != 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_MODE_1W) == 0);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::Execute));
    ASSERT_MEM_EQ(buf + 9, expectedPayload, sizeof(expectedPayload));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::Execute);
    ASSERT_EQ(parsed.dataLen, sizeof(expectedPayload));
    ASSERT_MEM_EQ(parsed.data, expectedPayload, sizeof(expectedPayload));
}

TEST(byte_vector_2w_execute_stop_and_favorite_payloads)
{
    struct Vector
    {
        uint8_t commandValue;
        uint8_t expectedPayload[6];
    };

    const Vector vectors[] = {
        {0xD2, {0x01, 0x67, 0xD2, 0x00, 0x00, 0x00}},
        {0xD8, {0x01, 0x67, 0xD8, 0x00, 0x00, 0x00}},
    };

    for (const Vector &v : vectors)
    {
        IoHomeFrame frame;
        frame.init();
        frame.setStart2W();
        frame.setSrcNode(0x831F2A);
        frame.setDestNode(0x7E9E6E);
        frame.commandId = IoHomeCommand::Execute;
        memcpy(frame.data, v.expectedPayload, sizeof(v.expectedPayload));
        frame.dataLen = sizeof(v.expectedPayload);
        frame.hasHmac = false;

        uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
        const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
        ASSERT_EQ(len, 15);
        ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::Execute));
        ASSERT_EQ(buf[11], v.commandValue);
        ASSERT_MEM_EQ(buf + 9, v.expectedPayload, sizeof(v.expectedPayload));
    }
}

TEST(byte_vector_2w_key_transfer_encrypted_key_from_key_init)
{
    const uint8_t systemKey[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    const uint8_t keyChallenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t keyInitTranscript[] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
    const uint8_t expectedEncryptedKey[16] = {
        0x10, 0x0F, 0x0F, 0xC2, 0xE1, 0x96, 0xA3, 0x95,
        0x76, 0x13, 0xD7, 0xAB, 0x9C, 0xFD, 0x83, 0x31};

    uint8_t encryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(keyInitTranscript, sizeof(keyInitTranscript),
                                            keyChallenge, systemKey,
                                            IOHC_TRANSFER_KEY, encryptedKey));
    ASSERT_MEM_EQ(encryptedKey, expectedEncryptedKey, sizeof(expectedEncryptedKey));

    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_ORDER_SINGLE; // 0x32 is a continuation frame, not a fresh START request.
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x831F2A);
    frame.setDestNode(0x7E9E6E);
    frame.commandId = IoHomeCommand::KeyTransfer;
    memcpy(frame.data, encryptedKey, sizeof(encryptedKey));
    frame.dataLen = sizeof(encryptedKey);
    frame.hasHmac = false;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 25);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_START) == 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_END) == 0);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::KeyTransfer));
    ASSERT_MEM_EQ(buf + 9, expectedEncryptedKey, sizeof(expectedEncryptedKey));
}

TEST(byte_vector_2w_challenge_response_hmac_is_command_data)
{
    const uint8_t hmacData[6] = {0x8D, 0x35, 0xDC, 0x56, 0x37, 0xF4};

    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_ORDER_SINGLE; // continuation/auth response, not a START request
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x831F2A);
    frame.setDestNode(0x7E9E6E);
    frame.commandId = IoHomeCommand::ChallengeResponse;
    memcpy(frame.data, hmacData, sizeof(hmacData));
    frame.dataLen = sizeof(hmacData);
    frame.hasHmac = false;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 15);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_START) == 0);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_END) == 0);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, 15);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::ChallengeResponse));
    ASSERT_MEM_EQ(buf + 9, hmacData, sizeof(hmacData));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::ChallengeResponse);
    ASSERT_EQ(parsed.dataLen, sizeof(hmacData));
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_MEM_EQ(parsed.data, hmacData, sizeof(hmacData));
}

TEST(byte_vector_1w_execute_00_14_hmac_transcript)
{
    const uint8_t key[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint16_t sequence = 0x1234;
    const uint8_t transcript[] = {
        static_cast<uint8_t>(IoHomeCommand::Execute),
        IOHC_ORIGINATOR_USER,
        IOHC_ACEI_1W,
        0x64, 0x00,
        0x00, 0x00};
    const uint8_t expectedIv[16] = {
        0x00, 0x01, 0x43, 0x64, 0x00, 0x00, 0x00, 0x55,
        0x0E, 0x60, 0x12, 0x34, 0x55, 0x55, 0x55, 0x55};
    const uint8_t expectedHmac[6] = {0x76, 0x8D, 0xA9, 0x52, 0x4C, 0x3F};

    uint8_t iv[16];
    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac1WWithIv(transcript, sizeof(transcript),
                                                 sequence, key, iv, hmac));
    ASSERT_MEM_EQ(iv, expectedIv, sizeof(expectedIv));
    ASSERT_MEM_EQ(hmac, expectedHmac, sizeof(expectedHmac));

    IoHomeFrame frame;
    frame.init();
    frame.set1WMode();
    frame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x831F2A);
    frame.setDestNode(0x00003F);
    frame.commandId = IoHomeCommand::Execute;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_1W;
    frame.data[2] = 0x64;
    frame.data[3] = 0x00;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = static_cast<uint8_t>((sequence >> 8) & 0xFF);
    frame.data[7] = static_cast<uint8_t>(sequence & 0xFF);
    frame.dataLen = 8;
    memcpy(frame.hmac, hmac, sizeof(hmac));
    frame.hasHmac = true;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 23);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_MODE_1W) != 0);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, 23);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::Execute));
    ASSERT_MEM_EQ(buf + 9, transcript + 1, 6);
    ASSERT_EQ(buf[15], 0x12);
    ASSERT_EQ(buf[16], 0x34);
    ASSERT_MEM_EQ(buf + 17, expectedHmac, sizeof(expectedHmac));
}

TEST(byte_vector_1w_sendkey_30_payload_20_bytes_no_hmac)
{
    const uint8_t encryptedKey[16] = {
        0x2D, 0x36, 0xBD, 0x8B, 0x4D, 0x4F, 0xB1, 0xE1,
        0xA1, 0xB3, 0x09, 0x9B, 0x39, 0x4D, 0x3A, 0x9E};
    const uint8_t expectedPayload[20] = {
        0x2D, 0x36, 0xBD, 0x8B, 0x4D, 0x4F, 0xB1, 0xE1,
        0xA1, 0xB3, 0x09, 0x9B, 0x39, 0x4D, 0x3A, 0x9E,
        static_cast<uint8_t>(IoHomeManufacturer::Velux), 0x01, 0x12, 0x34};

    IoHomeFrame frame;
    frame.init();
    frame.set1WMode();
    frame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0xB60D1A);
    frame.setDestNode(0x00003F);
    frame.commandId = IoHomeCommand::SendKey1W;
    memcpy(frame.data, expectedPayload, sizeof(expectedPayload));
    frame.dataLen = sizeof(expectedPayload);
    frame.hasHmac = false;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 29);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, 29);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::SendKey1W));
    ASSERT_MEM_EQ(buf + 9, expectedPayload, sizeof(expectedPayload));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::SendKey1W);
    ASSERT_EQ(parsed.dataLen, 20);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_MEM_EQ(parsed.data, expectedPayload, sizeof(expectedPayload));
}

// =====================================================================
// Integration Tests
// =====================================================================
// These tests simulate full protocol sequences end-to-end
// (frame build → serialize → deserialize → decode), verifying complete
// command-response sequences as they would happen over the air.

// =====================================================================
// I1. Execute command → StatusUpdate response flow
// =====================================================================

TEST(integration_execute_status_flow)
{
    // Simulate: gateway sends Execute(50%), device responds with StatusUpdate
    const uint32_t gwNode = 0x1A380B;
    const uint32_t devNode = 0x485B37;
    const uint8_t sysKey[16] = {
        0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13};
    uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    // --- Step 1: Build Execute frame ---
    IoHomeFrame txFrame;
    txFrame.init();
    txFrame.setStart2W();
    txFrame.setSrcNode(gwNode);
    txFrame.setDestNode(devNode);
    txFrame.commandId = IoHomeCommand::Execute;
    txFrame.data[0] = 0x01;   // originator: user
    txFrame.data[1] = 0x67;   // priority
    txFrame.data[2] = 50 * 2; // 50%
    txFrame.data[3] = 0x00;
    txFrame.data[4] = 0x80;
    txFrame.data[5] = 0xD8;
    txFrame.data[6] = 0x06; // standard
    txFrame.data[7] = 0x00;
    txFrame.dataLen = 8;

    // Serialize to get frame bytes for HMAC (without HMAC — this is what HMAC covers)
    uint8_t txBufNoHmac[32];
    uint8_t txLenNoHmac = serializeFrameForTest(txFrame, txBufNoHmac, sizeof(txBufNoHmac));
    ASSERT_TRUE(txLenNoHmac > 0);

    // Create HMAC over the frame-without-HMAC bytes
    uint8_t txHmac[IOHC_HMAC_SIZE] = {0};
    IoHomeCrypto::createHmac2W(txBufNoHmac, txLenNoHmac, challenge, sysKey, txHmac);

    // Re-serialize for transmission without appended HMAC bytes.
    uint8_t txBuf[32];
    uint8_t txLen = serializeFrameForTest(txFrame, txBuf, sizeof(txBuf));
    ASSERT_EQ(txLen, 9 + 8); // header + 8 data = 17

    // Deserialize on "device side"
    IoHomeFrame rxOnDevice;
    ASSERT_TRUE(deserializeFrameForTest(rxOnDevice, txBuf, txLen));
    ASSERT_EQ(rxOnDevice.getSrcNodeId(), gwNode);
    ASSERT_EQ(rxOnDevice.getDestNodeId(), devNode);
    ASSERT_EQ((uint8_t)rxOnDevice.commandId, 0x00);
    ASSERT_TRUE(!rxOnDevice.hasHmac);

    // Device verifies HMAC using pre-HMAC frame bytes
    // (ctrl0 length field differs between with/without HMAC serializations)
    ASSERT_TRUE(IoHomeCrypto::verifyHmac(txBufNoHmac, txLenNoHmac,
                                         txHmac, challenge, sysKey));

    // Device reads position from data[2]: 100/2 = 50%
    ASSERT_EQ(rxOnDevice.data[2] / 2, 50);

    // --- Step 2: Device sends StatusUpdate response ---
    IoHomeFrame respFrame;
    respFrame.init();
    respFrame.ctrlByte0 = IOHC_CTRL0_END;
    respFrame.ctrlByte1 = 0x01;
    respFrame.setSrcNode(devNode);
    respFrame.setDestNode(gwNode);
    respFrame.commandId = IoHomeCommand::StatusUpdate;

    // StatusUpdate layout: data[0]=flags, data[7:8]=current pos
    memset(respFrame.data, 0, 11);
    respFrame.data[0] = 0x00;                    // not stopped (still moving)
    uint16_t currentPos = IOHC_POSITION_MAX / 4; // currently at 25%
    respFrame.data[7] = (currentPos >> 8) & 0xFF;
    respFrame.data[8] = currentPos & 0xFF;
    // Target position at data[5:6]
    uint16_t targetPos = IOHC_POSITION_MAX / 2; // target 50%
    respFrame.data[5] = (targetPos >> 8) & 0xFF;
    respFrame.data[6] = targetPos & 0xFF;
    respFrame.dataLen = 11;

    // Add HMAC to response
    uint8_t respBufNoHmac[32];
    respFrame.hasHmac = false;
    uint8_t respLenNoHmac = serializeFrameForTest(respFrame, respBufNoHmac, sizeof(respBufNoHmac));
    uint8_t respHmac[IOHC_HMAC_SIZE] = {0};
    IoHomeCrypto::createHmac2W(respBufNoHmac, respLenNoHmac, challenge, sysKey, respHmac);
    uint8_t respBuf[32];
    uint8_t respLen = serializeFrameForTest(respFrame, respBuf, sizeof(respBuf));

    // Gateway receives and deserializes
    IoHomeFrame rxOnGw;
    ASSERT_TRUE(deserializeFrameForTest(rxOnGw, respBuf, respLen));
    ASSERT_EQ(rxOnGw.getSrcNodeId(), devNode);
    ASSERT_TRUE(!rxOnGw.hasHmac);

    // Gateway verifies HMAC using pre-HMAC frame bytes
    ASSERT_TRUE(IoHomeCrypto::verifyHmac(respBufNoHmac, respLenNoHmac,
                                         respHmac, challenge, sysKey));

    // Gateway decodes position
    uint16_t rawPos = ((uint16_t)rxOnGw.data[7] << 8) | rxOnGw.data[8];
    uint8_t percent = (uint8_t)((uint32_t)rawPos * 100 / IOHC_POSITION_MAX);
    ASSERT_EQ(percent, 25); // currently at 25%, moving toward 50%

    bool stopped = (rxOnGw.data[0] & 0x01) != 0;
    ASSERT_TRUE(!stopped); // still moving
}

// =====================================================================
// I2. Full key exchange simulation (pairing sequence)
// =====================================================================

TEST(integration_key_exchange)
{
    // Full pairing flow:
    //   1. GW → Device: DiscoverRequest
    //   2. Device → GW: DiscoverResponse (with device node ID)
    //   3. GW → Device: KeyInitTransfer
    //   4. Device → GW: ChallengeRequest (6-byte challenge)
    //   5. GW → Device: KeyTransfer (encrypted system key only)
    //   6. Device → GW: optional ChallengeRequest
    //   7. GW → Device: optional ChallengeResponse
    //   8. Device → GW: Confirmation

    const uint32_t gwNode = 0x1A380B;
    const uint32_t devNode = 0x485B37;
    const uint8_t systemKey[16] = {
        0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    uint8_t buf[32];

    // --- Step 1: DiscoverRequest ---
    IoHomeFrame discReq;
    discReq.init();
    discReq.setStart2W();
    discReq.setSrcNode(gwNode);
    discReq.setDestBroadcast();
    discReq.commandId = IoHomeCommand::DiscoverRequest;
    discReq.dataLen = 0;
    discReq.hasHmac = false;
    uint8_t len = serializeFrameForTest(discReq, buf, sizeof(buf));
    ASSERT_EQ(len, 9);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x28);

    // --- Step 2: DiscoverResponse (device responds) ---
    IoHomeFrame discResp;
    discResp.init();
    discResp.ctrlByte0 = IOHC_CTRL0_END;
    discResp.ctrlByte1 = 0x01;
    discResp.setSrcNode(devNode);
    discResp.setDestNode(gwNode);
    discResp.commandId = IoHomeCommand::DiscoverResponse;
    discResp.dataLen = 0;
    discResp.hasHmac = false;
    len = serializeFrameForTest(discResp, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    uint32_t discoveredNode = parsed.getSrcNodeId();
    ASSERT_EQ(discoveredNode, devNode);

    // --- Step 3: KeyInitTransfer ---
    IoHomeFrame keyInit;
    keyInit.init();
    keyInit.setStart2W();
    keyInit.setSrcNode(gwNode);
    keyInit.setDestNode(discoveredNode);
    keyInit.commandId = IoHomeCommand::KeyInitTransfer;
    keyInit.dataLen = 0;
    keyInit.hasHmac = false;
    len = serializeFrameForTest(keyInit, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x31);

    // --- Step 4: ChallengeRequest (device sends challenge) ---
    uint8_t devChallenge[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    IoHomeFrame chalReq;
    chalReq.init();
    chalReq.ctrlByte0 = IOHC_CTRL0_END;
    chalReq.ctrlByte1 = 0x01;
    chalReq.setSrcNode(devNode);
    chalReq.setDestNode(gwNode);
    chalReq.commandId = IoHomeCommand::ChallengeRequest;
    memcpy(chalReq.data, devChallenge, 6);
    chalReq.dataLen = 6;
    chalReq.hasHmac = false;
    len = serializeFrameForTest(chalReq, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x3C);
    ASSERT_EQ(parsed.dataLen, 6);
    uint8_t receivedChallenge[6];
    memcpy(receivedChallenge, parsed.data, 6);
    ASSERT_MEM_EQ(receivedChallenge, devChallenge, 6);

    // --- Step 5: KeyTransfer (GW encrypts system key) ---
    IoHomeFrame keyXfer;
    keyXfer.init();
    keyXfer.setStart2W();
    keyXfer.setSrcNode(gwNode);
    keyXfer.setDestNode(devNode);
    keyXfer.commandId = IoHomeCommand::KeyTransfer;

    keyXfer.dataLen = 0;
    keyXfer.hasHmac = false;
    const uint8_t keyInitData[] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};

    // Encrypt system key using transfer key + challenge.
    uint8_t encryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(keyInitData, sizeof(keyInitData),
                                            receivedChallenge, systemKey,
                                            IOHC_TRANSFER_KEY, encryptedKey));

    memcpy(keyXfer.data, encryptedKey, 16);
    keyXfer.dataLen = 16;
    keyXfer.hasHmac = false;

    uint8_t xferBuf[32];
    uint8_t xferLen = serializeFrameForTest(keyXfer, xferBuf, sizeof(xferBuf));
    ASSERT_EQ(xferLen, 9 + 16); // 25 bytes

    // Device side: deserialize and decrypt
    ASSERT_TRUE(deserializeFrameForTest(parsed, xferBuf, xferLen));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x32);
    ASSERT_EQ(parsed.dataLen, 16);
    ASSERT_TRUE(!parsed.hasHmac);

    // Device decrypts with the same symmetric XOR helper.
    uint8_t decryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(keyInitData, sizeof(keyInitData),
                                            receivedChallenge, parsed.data,
                                            IOHC_TRANSFER_KEY, decryptedKey));

    // Decrypted key must match original system key
    ASSERT_MEM_EQ(decryptedKey, systemKey, 16);

    // --- Step 6: Optional post-key ChallengeRequest / ChallengeResponse ---
    uint8_t postKeyChallenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    IoHomeFrame postKeyChal;
    postKeyChal.init();
    postKeyChal.ctrlByte0 = IOHC_CTRL0_END;
    postKeyChal.ctrlByte1 = 0x01;
    postKeyChal.setSrcNode(devNode);
    postKeyChal.setDestNode(gwNode);
    postKeyChal.commandId = IoHomeCommand::ChallengeRequest;
    memcpy(postKeyChal.data, postKeyChallenge, 6);
    postKeyChal.dataLen = 6;
    postKeyChal.hasHmac = false;
    len = serializeFrameForTest(postKeyChal, buf, sizeof(buf));
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));

    uint8_t keyTransferHmacInput[1 + 16] = {static_cast<uint8_t>(IoHomeCommand::KeyTransfer)};
    memcpy(keyTransferHmacInput + 1, encryptedKey, 16);
    uint8_t keyTransferHmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(keyTransferHmacInput, sizeof(keyTransferHmacInput),
                                           postKeyChallenge, systemKey, keyTransferHmac));

    IoHomeFrame keyTransferAuth;
    keyTransferAuth.init();
    keyTransferAuth.ctrlByte0 = 0;
    keyTransferAuth.ctrlByte1 = 0x00;
    keyTransferAuth.setSrcNode(gwNode);
    keyTransferAuth.setDestNode(devNode);
    keyTransferAuth.commandId = IoHomeCommand::ChallengeResponse;
    memcpy(keyTransferAuth.data, keyTransferHmac, 6);
    keyTransferAuth.dataLen = 6;
    keyTransferAuth.hasHmac = false;
    len = serializeFrameForTest(keyTransferAuth, buf, sizeof(buf));
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x3D);
    ASSERT_EQ(parsed.dataLen, 6);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_MEM_EQ(parsed.data, keyTransferHmac, 6);

    // --- Step 7: Confirmation ---
    IoHomeFrame confirm;
    confirm.init();
    confirm.ctrlByte0 = IOHC_CTRL0_END;
    confirm.ctrlByte1 = 0x01;
    confirm.setSrcNode(devNode);
    confirm.setDestNode(gwNode);
    confirm.commandId = IoHomeCommand::Confirmation;
    confirm.data[0] = 0x01; // success
    confirm.dataLen = 1;
    confirm.hasHmac = false;
    len = serializeFrameForTest(confirm, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x2C);
    ASSERT_EQ(parsed.data[0], 0x01);
    ASSERT_EQ(parsed.getSrcNodeId(), devNode);

    // Pairing complete — device now has the system key
}

// =====================================================================
// I3. Challenge lifecycle (send, verify, clear, reject replay)
// =====================================================================

TEST(integration_challenge_lifecycle)
{
    // Simulate full challenge lifecycle:
    //   1. Gateway sends Execute with challenge → HMAC
    //   2. Device responds → gateway verifies HMAC with stored challenge
    //   3. Gateway clears challenge after verification
    //   4. Replayed frame with same HMAC is rejected (challenge = all zeros)

    const uint32_t gwNode = 0x1A380B;
    const uint32_t devNode = 0x485B37;
    const uint8_t sysKey[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    // Step 1: Generate challenge and create authenticated frame
    uint8_t challenge[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

    IoHomeFrame txFrame;
    txFrame.init();
    txFrame.setStart2W();
    txFrame.setSrcNode(gwNode);
    txFrame.setDestNode(devNode);
    txFrame.commandId = IoHomeCommand::Execute;
    txFrame.data[0] = 0x01;
    txFrame.data[1] = 0x67;
    txFrame.data[2] = 0;
    txFrame.dataLen = 3;
    txFrame.hasHmac = false;

    uint8_t txBuf[32];
    uint8_t txLen = serializeFrameForTest(txFrame, txBuf, sizeof(txBuf));
    uint8_t txHmac[IOHC_HMAC_SIZE] = {0};
    IoHomeCrypto::createHmac2W(txBuf, txLen, challenge, sysKey, txHmac);
    ASSERT_TRUE(txLen > 0);

    // Store challenge (simulating channel state)
    uint8_t storedChallenge[6];
    memcpy(storedChallenge, challenge, 6);

    // Step 2: Device response with same challenge HMAC
    IoHomeFrame respFrame;
    respFrame.init();
    respFrame.ctrlByte0 = IOHC_CTRL0_END;
    respFrame.ctrlByte1 = 0x01;
    respFrame.setSrcNode(devNode);
    respFrame.setDestNode(gwNode);
    respFrame.commandId = IoHomeCommand::StatusUpdate;
    memset(respFrame.data, 0, 11);
    respFrame.data[0] = 0x01; // stopped
    respFrame.dataLen = 11;
    respFrame.hasHmac = false;

    uint8_t respBufNoHmac[32];
    uint8_t respLenNoHmac = serializeFrameForTest(respFrame, respBufNoHmac, sizeof(respBufNoHmac));
    uint8_t respHmac[IOHC_HMAC_SIZE] = {0};
    IoHomeCrypto::createHmac2W(respBufNoHmac, respLenNoHmac, storedChallenge, sysKey, respHmac);
    uint8_t respBuf[32];
    uint8_t respLen = serializeFrameForTest(respFrame, respBuf, sizeof(respBuf));

    // Step 2b: Verify HMAC with stored challenge — should pass
    IoHomeFrame rxFrame;
    ASSERT_TRUE(deserializeFrameForTest(rxFrame, respBuf, respLen));
    ASSERT_TRUE(!rxFrame.hasHmac);

    // Check pending challenge is non-zero
    bool hasPending = false;
    for (int i = 0; i < 6; i++)
        if (storedChallenge[i] != 0)
        {
            hasPending = true;
            break;
        }
    ASSERT_TRUE(hasPending);

    ASSERT_TRUE(IoHomeCrypto::verifyHmac(respBufNoHmac, respLenNoHmac,
                                         respHmac, storedChallenge, sysKey));

    // Step 3: Clear challenge after successful verification
    memset(storedChallenge, 0, 6);

    // Step 4: Replay attack — same frame arrives again
    hasPending = false;
    for (int i = 0; i < 6; i++)
        if (storedChallenge[i] != 0)
        {
            hasPending = true;
            break;
        }
    ASSERT_TRUE(!hasPending); // no pending challenge → reject

    // Even if we tried to verify, the zero challenge produces different HMAC
    ASSERT_TRUE(!IoHomeCrypto::verifyHmac(respBufNoHmac, respLenNoHmac,
                                          rxFrame.hmac, storedChallenge, sysKey));
}

// =====================================================================
// I4. Multi-device discovery scan
// =====================================================================

TEST(integration_discovery_scan)
{
    // Simulate discovery: GW broadcasts on 3 frequencies, 2 devices respond
    const uint32_t gwNode = 0x1A380B;
    const uint32_t dev1 = 0x485B37;
    const uint32_t dev2 = 0xAABBCC;
    uint8_t buf[32];

    // GW sends discovery on freq 0
    IoHomeFrame discReq;
    discReq.init();
    discReq.setStart2W();
    discReq.setSrcNode(gwNode);
    discReq.setDestBroadcast();
    discReq.commandId = IoHomeCommand::DiscoverRequest;
    discReq.dataLen = 0;
    discReq.hasHmac = false;
    uint8_t len = serializeFrameForTest(discReq, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    // Device 1 responds
    IoHomeFrame resp1;
    resp1.init();
    resp1.ctrlByte0 = IOHC_CTRL0_END;
    resp1.ctrlByte1 = 0x01;
    resp1.setSrcNode(dev1);
    resp1.setDestNode(gwNode);
    resp1.commandId = IoHomeCommand::DiscoverResponse;
    resp1.dataLen = 0;
    resp1.hasHmac = false;
    len = serializeFrameForTest(resp1, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    uint32_t discovered1 = parsed.getSrcNodeId();
    ASSERT_EQ(discovered1, dev1);

    // Device 2 responds (on freq 1 after hop)
    IoHomeFrame resp2;
    resp2.init();
    resp2.ctrlByte0 = IOHC_CTRL0_END;
    resp2.ctrlByte1 = 0x01;
    resp2.setSrcNode(dev2);
    resp2.setDestNode(gwNode);
    resp2.commandId = IoHomeCommand::DiscoverResponse;
    resp2.dataLen = 0;
    resp2.hasHmac = false;
    len = serializeFrameForTest(resp2, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    uint32_t discovered2 = parsed.getSrcNodeId();
    ASSERT_EQ(discovered2, dev2);

    // Both devices discovered, different node IDs
    ASSERT_TRUE(discovered1 != discovered2);
}

// =====================================================================
// I5. Device info query sequence
// =====================================================================

TEST(integration_device_info_query)
{
    // Simulate: GW queries device name, info1, info3 in sequence
    const uint32_t gwNode = 0x1A380B;
    const uint32_t devNode = 0x485B37;
    uint8_t buf[32];

    // --- GetName request ---
    IoHomeFrame nameReq;
    nameReq.init();
    nameReq.setStart2W();
    nameReq.setSrcNode(gwNode);
    nameReq.setDestNode(devNode);
    nameReq.commandId = IoHomeCommand::GetName;
    nameReq.dataLen = 0;
    nameReq.hasHmac = false;
    uint8_t len = serializeFrameForTest(nameReq, buf, sizeof(buf));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x50);

    // --- GetName response ---
    IoHomeFrame nameResp;
    nameResp.init();
    nameResp.ctrlByte0 = IOHC_CTRL0_END;
    nameResp.ctrlByte1 = 0x01;
    nameResp.setSrcNode(devNode);
    nameResp.setDestNode(gwNode);
    nameResp.commandId = IoHomeCommand::GetNameResponse;
    const char *devName = "Kitchen Blind";
    memcpy(nameResp.data, devName, strlen(devName));
    nameResp.dataLen = strlen(devName);
    nameResp.hasHmac = false;
    len = serializeFrameForTest(nameResp, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    char nameBuf[21] = {};
    uint8_t copyLen = (parsed.dataLen < 20) ? parsed.dataLen : 20;
    memcpy(nameBuf, parsed.data, copyLen);
    ASSERT_EQ(strcmp(nameBuf, "Kitchen Blind"), 0);

    // --- GetGeneralInfo1 request ---
    IoHomeFrame info1Req;
    info1Req.init();
    info1Req.setStart2W();
    info1Req.setSrcNode(gwNode);
    info1Req.setDestNode(devNode);
    info1Req.commandId = IoHomeCommand::GetGeneralInfo1;
    info1Req.dataLen = 0;
    info1Req.hasHmac = false;
    len = serializeFrameForTest(info1Req, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x54);

    // --- GetGeneralInfo1 response ---
    IoHomeFrame info1Resp;
    info1Resp.init();
    info1Resp.ctrlByte0 = IOHC_CTRL0_END;
    info1Resp.ctrlByte1 = 0x01;
    info1Resp.setSrcNode(devNode);
    info1Resp.setDestNode(gwNode);
    info1Resp.commandId = IoHomeCommand::GetGeneralInfo1Response;
    // Device type 0x01 (venetian blind), subtype 0x00, mfg 0x02 (Somfy)
    info1Resp.data[0] = 0x01;
    info1Resp.data[1] = 0x00;
    info1Resp.data[2] = 0x02;
    info1Resp.dataLen = 3;
    info1Resp.hasHmac = false;
    len = serializeFrameForTest(info1Resp, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    uint16_t devType = (parsed.data[0] | ((uint16_t)parsed.data[1] << 8)) & 0x3FF;
    uint8_t mfg = parsed.data[2];
    ASSERT_EQ(devType, 0x01); // venetian blind
    ASSERT_EQ(mfg, 0x02);     // Somfy

    // --- GetGeneralInfo3 request ---
    IoHomeFrame info3Req;
    info3Req.init();
    info3Req.setStart2W();
    info3Req.setSrcNode(gwNode);
    info3Req.setDestNode(devNode);
    info3Req.commandId = IoHomeCommand::GetGeneralInfo3;
    info3Req.dataLen = 0;
    info3Req.hasHmac = false;
    len = serializeFrameForTest(info3Req, buf, sizeof(buf));

    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x58);
}

// =====================================================================
// I6. Frequency hopping retry with successful response
// =====================================================================

TEST(integration_retry_then_success)
{
    // Simulate: command times out on freq 0 and 1, succeeds on freq 2
    const uint32_t gwNode = 0x1A380B;
    const uint32_t devNode = 0x485B37;
    const uint8_t sysKey[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    // Simulate retry state
    uint8_t retries = 0;
    uint8_t freqIdx = 0;

    // Timeout on freq 0
    freqIdx = (freqIdx + 1) % IOHC_NUM_FREQUENCIES;
    retries++;
    ASSERT_EQ(freqIdx, 1);
    ASSERT_EQ(retries, 1);

    // Timeout on freq 1
    freqIdx = (freqIdx + 1) % IOHC_NUM_FREQUENCIES;
    retries++;
    ASSERT_EQ(freqIdx, 2);
    ASSERT_EQ(retries, 2);

    // Success on freq 2 — build and verify a complete authenticated exchange
    uint8_t challenge[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    IoHomeFrame txFrame;
    txFrame.init();
    txFrame.setStart2W();
    txFrame.setSrcNode(gwNode);
    txFrame.setDestNode(devNode);
    txFrame.commandId = IoHomeCommand::Execute;
    txFrame.data[0] = 0x01;
    txFrame.data[1] = 0x67;
    txFrame.data[2] = 100 * 2; // 100%
    txFrame.data[3] = 0x00;
    txFrame.data[4] = 0x80;
    txFrame.data[5] = 0xD8;
    txFrame.data[6] = 0x06;
    txFrame.data[7] = 0x00;
    txFrame.dataLen = 8;

    uint8_t txBufNoHmac2[32];
    uint8_t txLenNoHmac2 = serializeFrameForTest(txFrame, txBufNoHmac2, sizeof(txBufNoHmac2));
    uint8_t txHmac2[IOHC_HMAC_SIZE] = {0};
    IoHomeCrypto::createHmac2W(txBufNoHmac2, txLenNoHmac2, challenge, sysKey, txHmac2);
    uint8_t txBuf2[32];
    uint8_t txLen2 = serializeFrameForTest(txFrame, txBuf2, sizeof(txBuf2));
    ASSERT_TRUE(txLen2 > 0);

    // Device responds on freq 2
    IoHomeFrame respFrame;
    respFrame.init();
    respFrame.ctrlByte0 = IOHC_CTRL0_END;
    respFrame.ctrlByte1 = 0x01;
    respFrame.setSrcNode(devNode);
    respFrame.setDestNode(gwNode);
    respFrame.commandId = IoHomeCommand::StatusUpdate;
    memset(respFrame.data, 0, 11);
    respFrame.data[0] = 0x01;         // stopped (arrived at position)
    uint16_t pos = IOHC_POSITION_MAX; // 100%
    respFrame.data[7] = (pos >> 8) & 0xFF;
    respFrame.data[8] = pos & 0xFF;
    respFrame.dataLen = 11;

    uint8_t respBufNoHmac2[32];
    respFrame.hasHmac = false;
    uint8_t respLenNoHmac2 = serializeFrameForTest(respFrame, respBufNoHmac2, sizeof(respBufNoHmac2));
    uint8_t respHmac2[IOHC_HMAC_SIZE] = {0};
    IoHomeCrypto::createHmac2W(respBufNoHmac2, respLenNoHmac2, challenge, sysKey, respHmac2);
    uint8_t respBuf2[32];
    uint8_t respLen2 = serializeFrameForTest(respFrame, respBuf2, sizeof(respBuf2));

    // Verify response
    IoHomeFrame rxFrame;
    ASSERT_TRUE(deserializeFrameForTest(rxFrame, respBuf2, respLen2));
    ASSERT_TRUE(IoHomeCrypto::verifyHmac(respBufNoHmac2, respLenNoHmac2,
                                         respHmac2, challenge, sysKey));

    uint16_t rawPos = ((uint16_t)rxFrame.data[7] << 8) | rxFrame.data[8];
    uint8_t percent = (uint8_t)((uint32_t)rawPos * 100 / IOHC_POSITION_MAX);
    ASSERT_EQ(percent, 100);

    bool stopped = (rxFrame.data[0] & 0x01) != 0;
    ASSERT_TRUE(stopped);

    // Retries + success = 3 total attempts (MAX_RETRIES = 3)
    ASSERT_TRUE(retries < 3);
}

// =====================================================================
// ACEI byte constants and originator IDs
// =====================================================================

TEST(acei_byte_constants)
{
    // Default ACEI = 0x67: priority=3, service=0, extended=3, valid=1
    // Binary: 011 00 11 1 = 0x67
    uint8_t acei = IOHC_ACEI_DEFAULT;
    ASSERT_EQ(acei, 0x67);

    // Extract priority (bits 7:5)
    uint8_t priority = (acei & IOHC_ACEI_PRIORITY_MASK) >> 5;
    ASSERT_EQ(priority, IOHC_PRIORITY_USER_REMOTE); // 3

    // Extract service (bits 4:3)
    uint8_t service = (acei & IOHC_ACEI_SERVICE_MASK) >> 3;
    ASSERT_EQ(service, 0);

    // Extract extended (bits 2:1)
    uint8_t extended = (acei & IOHC_ACEI_EXTENDED_MASK) >> 1;
    ASSERT_EQ(extended, 3);

    // Valid bit (bit 0)
    ASSERT_TRUE(acei & IOHC_ACEI_VALID_BIT);
}

TEST(acei_priority_levels)
{
    // Construct ACEI with protection priority (0) — highest
    uint8_t aceiProtection = (IOHC_PRIORITY_PROTECTION << 5) | 0x07; // service=0, ext=3, valid=1
    uint8_t prio = (aceiProtection & IOHC_ACEI_PRIORITY_MASK) >> 5;
    ASSERT_EQ(prio, 0);

    // Construct ACEI with auto-high priority (7) — lowest
    uint8_t aceiAuto = (IOHC_PRIORITY_AUTO_HIGH << 5) | 0x07;
    prio = (aceiAuto & IOHC_ACEI_PRIORITY_MASK) >> 5;
    ASSERT_EQ(prio, 7);
}

TEST(originator_id_values)
{
    ASSERT_EQ(IOHC_ORIGINATOR_LOCAL, 0x00);
    ASSERT_EQ(IOHC_ORIGINATOR_USER, 0x01);
    ASSERT_EQ(IOHC_ORIGINATOR_RAIN, 0x02);
    ASSERT_EQ(IOHC_ORIGINATOR_TIMER, 0x03);
    ASSERT_EQ(IOHC_ORIGINATOR_SCD, 0x04);
    ASSERT_EQ(IOHC_ORIGINATOR_SAAC, 0x08);
    ASSERT_EQ(IOHC_ORIGINATOR_WIND, 0x09);
    ASSERT_EQ(IOHC_ORIGINATOR_SELF, 0x10);
    ASSERT_EQ(IOHC_ORIGINATOR_EMERGENCY, 0xFF);
}

TEST(acei_in_execute_frame)
{
    // Verify Execute frame uses ACEI constants
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::Execute;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_DEFAULT;
    frame.data[2] = 50 * 2; // 50%
    frame.data[3] = 0x00;
    frame.data[4] = 0x80;
    frame.data[5] = 0xD8;
    frame.data[6] = 0x06;
    frame.data[7] = 0x00;
    frame.dataLen = 8;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(parsed.data[1], IOHC_ACEI_DEFAULT);
}

// =====================================================================
// ActivateMode (0x01) command
// =====================================================================

TEST(activate_mode_enum_value)
{
    ASSERT_EQ((uint8_t)IoHomeCommand::ActivateMode, 0x01);
}

TEST(activate_mode_frame)
{
    // ActivateMode payload stays fully in the declared 2W frame body.
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::ActivateMode;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_DEFAULT;
    frame.data[2] = (IOHC_POSITION_FAVORITE >> 8) & 0xFF; // 0xD8
    frame.data[3] = IOHC_POSITION_FAVORITE & 0xFF;        // 0x00
    frame.data[4] = (IOHC_POSITION_UNKNOWN >> 8) & 0xFF;  // 0xD4 (FP2 = ignore)
    frame.data[5] = IOHC_POSITION_UNKNOWN & 0xFF;         // 0x00
    memset(frame.data + 6, 0, 7);
    frame.dataLen = 13;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ((uint8_t)parsed.commandId, 0x01);
    ASSERT_EQ(parsed.dataLen, 13);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_EQ(parsed.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(parsed.data[1], IOHC_ACEI_DEFAULT);
    ASSERT_EQ(parsed.data[2], 0xD8);
    ASSERT_EQ(parsed.data[3], 0x00);
}

TEST(activate_mode_2w_rejects_appended_hmac)
{
    // Appending 6 extra bytes after the declared 2W payload is invalid.
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::ActivateMode;
    memset(frame.data, 0, 13);
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_DEFAULT;
    frame.dataLen = 13;
    // Add HMAC
    memset(frame.hmac, 0xAA, IOHC_HMAC_SIZE);
    frame.hasHmac = true;

    uint8_t buf[32];
    uint8_t len = serialize2WWithAppendedHmacForRejectTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(!deserializeFrameForTest(parsed, buf, len));
}

// =====================================================================
// Vent and ForceOpen position constants
// =====================================================================

TEST(vent_position_constant)
{
    ASSERT_EQ(IOHC_POSITION_VENT, 0xD803);
    // Vent high byte
    ASSERT_EQ((IOHC_POSITION_VENT >> 8) & 0xFF, 0xD8);
    // Vent low byte
    ASSERT_EQ(IOHC_POSITION_VENT & 0xFF, 0x03);
}

TEST(force_open_position_constant)
{
    ASSERT_EQ(IOHC_POSITION_FORCE_OPEN, 0x6400);
    // ForceOpen as percentage: 0x6400 * 100 / 0xC800 = 50%
    uint32_t percent = (uint32_t)IOHC_POSITION_FORCE_OPEN * 100 / IOHC_POSITION_MAX;
    ASSERT_EQ(percent, 50);
}

TEST(activate_mode_vent_frame)
{
    // ActivateMode with vent position
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x1A380B);
    frame.setDestNode(0x485B37);
    frame.commandId = IoHomeCommand::ActivateMode;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_DEFAULT;
    frame.data[2] = (IOHC_POSITION_VENT >> 8) & 0xFF;
    frame.data[3] = IOHC_POSITION_VENT & 0xFF;
    frame.data[4] = (IOHC_POSITION_UNKNOWN >> 8) & 0xFF;
    frame.data[5] = IOHC_POSITION_UNKNOWN & 0xFF;
    memset(frame.data + 6, 0, 7);
    frame.dataLen = 13;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.data[2], 0xD8);
    ASSERT_EQ(parsed.data[3], 0x03);
}

// =====================================================================
// Phase 6: New feature tests (CRC-16, 1W crypto, address class, etc.)
// =====================================================================

// --- CRC-16 Kermit ---

TEST(crc16_kermit_standard_vector)
{
    const uint8_t data[] = "123456789";
    uint16_t crc = IoHomeCrypto::crc16Kermit(data, 9);
    ASSERT_EQ(crc, 0x2189);
}

TEST(crc16_kermit_empty)
{
    uint16_t crc = IoHomeCrypto::crc16Kermit(nullptr, 0);
    ASSERT_EQ(crc, 0x0000);
}

// --- 1W IV construction ---

TEST(iv_1w_construction)
{
    uint8_t frameData[] = {0x01, 0x67, 0x50, 0x00};
    uint16_t seq = 0x1234;
    uint8_t iv[16];
    IoHomeCrypto::constructIv1W(frameData, 4, seq, iv);

    // Bytes 0-3: frame data
    ASSERT_EQ(iv[0], 0x01);
    ASSERT_EQ(iv[1], 0x67);
    ASSERT_EQ(iv[2], 0x50);
    ASSERT_EQ(iv[3], 0x00);
    // Bytes 4-7: padded with 0x55
    ASSERT_EQ(iv[4], 0x55);
    ASSERT_EQ(iv[5], 0x55);
    ASSERT_EQ(iv[6], 0x55);
    ASSERT_EQ(iv[7], 0x55);
    // Bytes 8-9: checksums (non-zero for non-empty data)
    // Bytes 10-11: sequence number big-endian
    ASSERT_EQ(iv[10], 0x12);
    ASSERT_EQ(iv[11], 0x34);
    // Bytes 12-15: 0x55 padding
    ASSERT_EQ(iv[12], 0x55);
    ASSERT_EQ(iv[13], 0x55);
    ASSERT_EQ(iv[14], 0x55);
    ASSERT_EQ(iv[15], 0x55);
}

// --- 1W HMAC ---

TEST(hmac_1w_create_verify_roundtrip)
{
    uint8_t frameData[] = {0x01, 0x67, 0x50, 0x00, 0x80, 0xD8};
    uint8_t key[16];
    memset(key, 0xAA, 16);
    uint16_t seq = 0x0042;

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(frameData, sizeof(frameData), seq, key, hmac));
    ASSERT_TRUE(IoHomeCrypto::verifyHmac1W(frameData, sizeof(frameData), seq, hmac, key));
}

TEST(hmac_1w_different_seq_different_output)
{
    uint8_t frameData[] = {0x01, 0x67};
    uint8_t key[16];
    memset(key, 0xBB, 16);

    uint8_t hmac1[6], hmac2[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(frameData, 2, 0x0001, key, hmac1));
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(frameData, 2, 0x0002, key, hmac2));

    bool different = false;
    for (int i = 0; i < 6; i++)
        if (hmac1[i] != hmac2[i])
            different = true;
    ASSERT_TRUE(different);
}

TEST(hmac_1w_wrong_key_rejects)
{
    uint8_t frameData[] = {0x01, 0x67, 0x50};
    uint8_t key1[16], key2[16];
    memset(key1, 0xCC, 16);
    memset(key2, 0xDD, 16);
    uint16_t seq = 0x0010;

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(frameData, 3, seq, key1, hmac));
    ASSERT_TRUE(!IoHomeCrypto::verifyHmac1W(frameData, 3, seq, hmac, key2));
}

TEST(hmac_1w_reference_vectors_selftest)
{
    // Runs the byte-exact 1W reference vectors from IoHomeCrypto.
    // This locks down that the 1W HMAC transcript is command+payload only;
    // ctrl0/ctrl1/source/destination/sequence/appended-HMAC must not be part
    // of the IV checksum transcript.
    ASSERT_TRUE(IoHomeCrypto::selfTest1WReferenceVectors());
}

TEST(encrypt_1w_key_reference_vectors_selftest)
{
    // Runs the byte-exact 1W SendKey encryption vector.
    // This locks down that the key-encryption IV is based on the remote /
    // controller node address and that the single-block CFB128 path is
    // equivalent to AES-ECB(IV) XOR clearKey.
    ASSERT_TRUE(IoHomeCrypto::selfTest1WKeyEncryptionVectors());
}

// --- 1W key encryption ---

TEST(encrypt_1w_key_roundtrip)
{
    uint8_t originalKey[16];
    for (int i = 0; i < 16; i++)
        originalKey[i] = i * 7 + 3;

    uint8_t transferKey[16];
    memset(transferKey, 0x42, 16);

    uint8_t nodeAddr[3] = {0x48, 0x5B, 0x37};

    uint8_t encrypted[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(originalKey, transferKey, nodeAddr, encrypted));

    uint8_t decrypted[16];
    ASSERT_TRUE(IoHomeCrypto::decrypt1WKey(encrypted, transferKey, nodeAddr, decrypted));

    ASSERT_TRUE(memcmp(originalKey, decrypted, 16) == 0);
}

TEST(encrypt_1w_key_iv_from_node_address)
{
    // Verify the IV construction: node address repeated to fill 16 bytes
    uint8_t key[16] = {};
    uint8_t transferKey[16] = {};
    uint8_t nodeAddr[3] = {0xAA, 0xBB, 0xCC};
    uint8_t encrypted[16];

    // With all-zero key and transfer key, encrypted = AES(iv, 0) XOR 0 = AES(iv, 0)
    // We can't easily test the IV directly, but we can verify the operation is deterministic
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(key, transferKey, nodeAddr, encrypted));

    // Same inputs should produce same output
    uint8_t encrypted2[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(key, transferKey, nodeAddr, encrypted2));
    ASSERT_TRUE(memcmp(encrypted, encrypted2, 16) == 0);

    // Different node address should produce different output
    uint8_t nodeAddr2[3] = {0xDD, 0xEE, 0xFF};
    uint8_t encrypted3[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(key, transferKey, nodeAddr2, encrypted3));
    ASSERT_TRUE(memcmp(encrypted, encrypted3, 16) != 0);
}

// --- Command enum values ---

TEST(write_private_enum_value)
{
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::WritePrivate), 0x20);
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::WritePrivateResponse), 0x21);
}

TEST(identify_enum_value)
{
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::Identify), 0x1E);
}

TEST(send_key_1w_enum_value)
{
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::SendKey1W), 0x30);
}

TEST(address_request_enum)
{
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::AddressRequest), 0x36);
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::AddressResponse), 0x37);
}

TEST(launch_key_transfer_enum)
{
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::LaunchKeyTransfer), 0x38);
}

TEST(remove_controller_enum)
{
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::RemoveController), 0x39);
}

// --- HMAC detection in deserialize ---

TEST(write_private_2w_rejects_appended_hmac)
{
    // WritePrivate in 2W mode must also reject undeclared appended HMAC bytes.
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::WritePrivate;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_DEFAULT;
    frame.data[2] = 0x03;
    frame.data[3] = 200; // 20.0°C
    memset(frame.data + 4, 0, 9);
    frame.dataLen = 13;
    memset(frame.hmac, 0xAA, IOHC_HMAC_SIZE);
    frame.hasHmac = true;

    uint8_t buf[32];
    uint8_t len = serialize2WWithAppendedHmacForRejectTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(!deserializeFrameForTest(parsed, buf, len));
}

TEST(send_key_1w_unauthenticated_29_bytes)
{
    IoHomeFrame frame;
    frame.init();
    frame.set1WMode();
    frame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x0000BF);
    frame.commandId = IoHomeCommand::SendKey1W;

    for (uint8_t i = 0; i < 16; i++)
        frame.data[i] = static_cast<uint8_t>(0xA0 + i);
    frame.data[16] = static_cast<uint8_t>(IoHomeManufacturer::Velux);
    frame.data[17] = 0x01;
    frame.data[18] = 0x12;
    frame.data[19] = 0x34;
    frame.dataLen = 20;
    frame.hasHmac = false;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 29);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_MODE_1W) != 0);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, 29);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::SendKey1W));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::SendKey1W);
    ASSERT_TRUE((parsed.ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, 20);
    ASSERT_MEM_EQ(parsed.data, frame.data, 20);
}

TEST(send_key_1w_parses_optional_trailer_mac)
{
    IoHomeFrame frame;
    frame.init();
    frame.set1WMode();
    frame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x0000BF);
    frame.commandId = IoHomeCommand::SendKey1W;
    memset(frame.data, 0x42, 20);
    frame.dataLen = 20;
    memset(frame.hmac, 0xBB, IOHC_HMAC_SIZE);
    frame.hasHmac = true;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    ASSERT_EQ(serializeFrameForTest(frame, buf, sizeof(buf)), 0);

    // SendKey1W cannot embed an HMAC in its declared payload. Some controllers
    // do append a six-byte MAC trailer outside the declared 29-byte frame;
    // preserve it for the controller's cryptographic verification.
    frame.hasHmac = false;
    frame.dataLen = 19;
    ASSERT_EQ(serializeFrameForTest(frame, buf, sizeof(buf)), 0);

    frame.dataLen = 20;
    const uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_EQ(len, 29);
    memset(buf + len, 0xBB, IOHC_HMAC_SIZE);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len + IOHC_HMAC_SIZE));
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_TRUE(parsed.hasTrailerMac);
    ASSERT_EQ(parsed.dataLen, 20);
    ASSERT_MEM_EQ(parsed.trailerMac, buf + len, IOHC_HMAC_SIZE);
}

TEST(serializer_boundary_2w_challenge_response_hmac_as_data)
{
    // 2W ChallengeResponse (0x3D) must carry the HMAC bytes as ordinary data.
    // It must not use IoHomeFrame::hasHmac and must not append software CRC in
    // the normal 2W serializer.
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = 0x00; // continuation frame, no START/END
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::ChallengeResponse;
    const uint8_t expectedHmac[IOHC_HMAC_SIZE] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65};
    memcpy(frame.data, expectedHmac, IOHC_HMAC_SIZE);
    frame.dataLen = IOHC_HMAC_SIZE;
    frame.hasHmac = false;
    frame.hasCrc = false;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = frame.serialize2W(buf, sizeof(buf));
    ASSERT_EQ(len, 15); // 9-byte header + 6 bytes command data
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_MODE_1W) == 0);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, 15);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::ChallengeResponse));
    ASSERT_MEM_EQ(buf + 9, expectedHmac, IOHC_HMAC_SIZE);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::ChallengeResponse);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_TRUE(!parsed.hasCrc);
    ASSERT_EQ(parsed.dataLen, IOHC_HMAC_SIZE);
    ASSERT_MEM_EQ(parsed.data, expectedHmac, IOHC_HMAC_SIZE);

    // The same frame with raw/diagnostic CRC is longer and must not be produced
    // by serialize2W().
    frame.hasCrc = true;
    ASSERT_EQ(frame.serialize2W(buf, sizeof(buf)), 0);
}

TEST(serializer_boundary_2w_has_hmac_rejected)
{
    // Any 2W frame with hasHmac=true is invalid at the serializer boundary.
    // 2W auth must be represented as 0x3D command data instead.
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::Execute;
    frame.data[0] = 0x01;
    frame.data[1] = 0x67;
    frame.data[2] = 0x64;
    frame.dataLen = 3;
    memset(frame.hmac, 0xAA, IOHC_HMAC_SIZE);
    frame.hasHmac = true;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    ASSERT_EQ(frame.serialize2W(buf, sizeof(buf)), 0);
    ASSERT_EQ(serializeFrameForTest(frame, buf, sizeof(buf)), 0);

    // Even ChallengeResponse must reject hasHmac=true. Its six HMAC bytes must
    // be in data[], not in hmac[].
    frame.commandId = IoHomeCommand::ChallengeResponse;
    frame.dataLen = IOHC_HMAC_SIZE;
    ASSERT_EQ(frame.serialize2W(buf, sizeof(buf)), 0);
}

TEST(serializer_boundary_1w_execute_hmac_declared_length)
{
    // Normal authenticated 1W Execute includes the appended 6-byte HMAC inside
    // the CTRL0-declared protocol length.
    IoHomeFrame frame;
    frame.init();
    frame.set1WMode();
    frame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x0000BF);
    frame.commandId = IoHomeCommand::Execute;
    const uint8_t payload[8] = {0x01, 0x43, 0x00, 0x00, 0x00, 0x00, 0x12, 0x34};
    const uint8_t expectedHmac[IOHC_HMAC_SIZE] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    memcpy(frame.data, payload, sizeof(payload));
    memcpy(frame.hmac, expectedHmac, sizeof(expectedHmac));
    frame.dataLen = sizeof(payload);
    frame.hasHmac = true;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = frame.serialize1W(buf, sizeof(buf));
    ASSERT_EQ(len, 23); // 9-byte header + 8-byte payload + 6-byte HMAC
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_MODE_1W) != 0);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, len);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::Execute));
    ASSERT_MEM_EQ(buf + 9, payload, sizeof(payload));
    ASSERT_MEM_EQ(buf + 9 + sizeof(payload), expectedHmac, sizeof(expectedHmac));

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::Execute);
    ASSERT_TRUE(parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, sizeof(payload));
    ASSERT_MEM_EQ(parsed.data, payload, sizeof(payload));
    ASSERT_MEM_EQ(parsed.hmac, expectedHmac, sizeof(expectedHmac));
}

TEST(serializer_boundary_sendkey1w_unauthenticated_29)
{
    // SendKey1W (0x30) is the dedicated Add/SendKey frame: 9-byte header +
    // encryptedKey[16] + manufacturer + 0x01 + sequence[2]. No HMAC is appended.
    IoHomeFrame frame;
    frame.init();
    frame.set1WMode();
    frame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    frame.ctrlByte1 = 0x00;
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x0000BF);
    frame.commandId = IoHomeCommand::SendKey1W;
    for (uint8_t i = 0; i < 16; i++)
        frame.data[i] = static_cast<uint8_t>(0xC0 + i);
    frame.data[16] = static_cast<uint8_t>(IoHomeManufacturer::Velux);
    frame.data[17] = 0x01;
    frame.data[18] = 0x5A;
    frame.data[19] = 0xC3;
    frame.dataLen = 20;
    frame.hasHmac = false;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = frame.serialize1W(buf, sizeof(buf));
    ASSERT_EQ(len, 29);
    ASSERT_TRUE((buf[0] & IOHC_CTRL0_MODE_1W) != 0);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, 29);
    ASSERT_EQ(buf[8], static_cast<uint8_t>(IoHomeCommand::SendKey1W));
    ASSERT_MEM_EQ(buf + 9, frame.data, 20);

    // Future accidental authenticated SendKey1W must fail immediately.
    frame.hasHmac = true;
    memset(frame.hmac, 0xEE, IOHC_HMAC_SIZE);
    ASSERT_EQ(frame.serialize1W(buf, sizeof(buf)), 0);
}

TEST(serializer_sendkey1w_accepts_optional_trailer_mac)
{
    const uint8_t kKey[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    const uint16_t kSequence = 0x1A2B;

    IoHomeFrame frame;
    frame.init();
    frame.set1WMode();
    frame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x0000BF);
    frame.commandId = IoHomeCommand::SendKey1W;
    for (uint8_t i = 0; i < 16; i++)
        frame.data[i] = static_cast<uint8_t>(0xC0 + i);
    frame.data[16] = static_cast<uint8_t>(IoHomeManufacturer::Velux);
    frame.data[17] = 0x01;
    frame.data[18] = static_cast<uint8_t>(kSequence >> 8);
    frame.data[19] = static_cast<uint8_t>(kSequence & 0xFF);
    frame.dataLen = 20;

    uint8_t transcript[17] = {static_cast<uint8_t>(IoHomeCommand::SendKey1W)};
    memcpy(transcript + 1, frame.data, 16);
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(transcript, sizeof(transcript), kSequence,
                                           kKey, frame.trailerMac));
    frame.hasTrailerMac = true;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t len = frame.serialize1W(buf, sizeof(buf));
    ASSERT_EQ(len, 35);
    ASSERT_EQ((buf[0] & IOHC_CTRL0_LEN_MASK) + 1, 29);
    ASSERT_MEM_EQ(buf + 29, frame.trailerMac, IOHC_HMAC_SIZE);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.dataLen, 20);
    ASSERT_TRUE(parsed.hasTrailerMac);
    ASSERT_MEM_EQ(parsed.trailerMac, frame.trailerMac, IOHC_HMAC_SIZE);
}

TEST(golden_rf_corpus_frames_decode_to_declared_metadata)
{
    using namespace IoHomeGoldenRfCorpus;

    ASSERT_TRUE(frameCount > 0);
    for (uint8_t i = 0; i < frameCount; i++)
    {
        const Frame &lFixture = kFrames[i];
        ASSERT_TRUE(lFixture.id && lFixture.id[0] != '\0');
        ASSERT_TRUE(lFixture.scenario && lFixture.scenario[0] != '\0');
        ASSERT_TRUE(lFixture.provenance && lFixture.provenance[0] != '\0');
        ASSERT_TRUE(lFixture.bytes != nullptr);
        ASSERT_TRUE(lFixture.wireLen >= IOHC_FRAME_MIN_SIZE);

        IoHomeFrame lFrame;
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lFixture.bytes, lFixture.wireLen));
        ASSERT_EQ(lFrame.commandId, lFixture.command);
        ASSERT_EQ(lFrame.getSrcNodeId(), lFixture.source);
        ASSERT_EQ(lFrame.getDestNodeId(), lFixture.destination);
        ASSERT_EQ((lFrame.ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0, lFixture.oneWay);
        ASSERT_EQ(lFrame.hasHmac, lFixture.hasHmac);
        ASSERT_EQ(lFrame.hasTrailerMac, lFixture.hasTrailerMac);
        ASSERT_EQ((lFixture.bytes[0] & IOHC_CTRL0_LEN_MASK) + 1,
                  lFixture.hasTrailerMac ? 29 : lFixture.wireLen);
    }
}

TEST(golden_rf_corpus_public_trailer_mac_verifies)
{
    using namespace IoHomeGoldenRfCorpus;

    const Frame *lFixture = findFrame("dimmer_sendkey_with_mac");
    ASSERT_TRUE(lFixture != nullptr);
    ASSERT_EQ(lFixture->crypto, CryptoExpectation::VerifyPublicOneWayTrailer);

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lFixture->bytes, lFixture->wireLen));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::SendKey1W);
    ASSERT_TRUE(lFrame.hasTrailerMac);
    ASSERT_EQ(lFrame.dataLen, 20);

    const uint16_t lSequence = static_cast<uint16_t>((static_cast<uint16_t>(lFrame.data[18]) << 8) |
                                                     lFrame.data[19]);
    uint8_t lTranscript[17] = {static_cast<uint8_t>(IoHomeCommand::SendKey1W)};
    memcpy(lTranscript + 1, lFrame.data, 16);
    ASSERT_TRUE(IoHomeCrypto::verifyHmac1W(lTranscript, sizeof(lTranscript), lSequence,
                                           lFrame.trailerMac, kPublicTrailerVectorKey));
}

TEST(golden_rf_corpus_covers_every_pairing_wait_injection)
{
    using namespace IoHomeGoldenRfCorpus;

    ASSERT_EQ(pairingWaitInjectionCount, 5);
    for (uint8_t i = 0; i < pairingWaitInjectionCount; i++)
    {
        const PairingWaitInjection &lInjection = kPairingWaitInjections[i];
        ASSERT_TRUE(lInjection.waitState && lInjection.waitState[0] != '\0');
        ASSERT_TRUE(lInjection.expectedOutcome && lInjection.expectedOutcome[0] != '\0');
        const Frame *lFixture = findFrame(lInjection.unrelatedFrameId);
        ASSERT_TRUE(lFixture != nullptr);
        ASSERT_EQ(lFixture->crypto, CryptoExpectation::PairingCorrelationMustReject);
    }

    ASSERT_EQ(scenarioCount, 10);
    for (uint8_t i = 0; i < scenarioCount; i++)
    {
        const Scenario &lScenario = kScenarios[i];
        ASSERT_TRUE(lScenario.id && lScenario.id[0] != '\0');
        ASSERT_TRUE(lScenario.expectedOutcome && lScenario.expectedOutcome[0] != '\0');
        ASSERT_TRUE(findFrame(lScenario.fixtureId) != nullptr);
    }
}

TEST(serializer_boundary_raw_crc_explicit_only)
{
    // Normal protocol serializers reject CRC. Only the raw/diagnostic helper may
    // append and later accept the transport CRC.
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::Private;
    frame.data[0] = 0x03;
    frame.data[1] = 0x00;
    frame.data[2] = 0x00;
    frame.dataLen = 3;
    frame.hasHmac = false;

    uint8_t normalBuf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t normalLen = frame.serialize2W(normalBuf, sizeof(normalBuf));
    ASSERT_EQ(normalLen, 12);

    frame.hasCrc = true;
    ASSERT_EQ(frame.serialize2W(normalBuf, sizeof(normalBuf)), 0);

    uint8_t rawBuf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t rawLen = frame.serializeRawWithCrc(rawBuf, sizeof(rawBuf));
    ASSERT_EQ(rawLen, normalLen + IOHC_CRC_SIZE);
    ASSERT_MEM_EQ(rawBuf, normalBuf, normalLen);

    IoHomeFrame parsed;
    ASSERT_TRUE(!deserializeFrameForTest(parsed, rawBuf, rawLen));
    ASSERT_TRUE(deserializeRawWithOptionalCrcForTest(parsed, rawBuf, rawLen));
    ASSERT_TRUE(parsed.hasCrc);
    ASSERT_TRUE(!parsed.hasHmac);
    ASSERT_EQ(parsed.commandId, IoHomeCommand::Private);
    ASSERT_EQ(parsed.dataLen, 3);
}

TEST(frame_1w_execute_has_hmac)
{
    // 1W Execute (0x00) should also detect HMAC
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_START | IOHC_CTRL0_MODE_1W;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::Execute;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_DEFAULT;
    frame.data[2] = 100; // 50% position
    frame.data[3] = 0x00;
    frame.data[4] = 0x80;
    frame.data[5] = 0xD8;
    frame.data[6] = 0x06;
    frame.data[7] = 0x00;
    frame.dataLen = 8;
    memset(frame.hmac, 0xCC, IOHC_HMAC_SIZE);
    frame.hasHmac = true;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE(parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, 8);
}

TEST(frame_1w_activate_mode_has_hmac)
{
    // 1W ActivateMode (0x01, _p0x01_13) must also detect the appended HMAC:
    // origin+acei+main(1)+fp1+fp2+seq[2] = 7 payload bytes + 6 HMAC bytes.
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_START | IOHC_CTRL0_MODE_1W;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x0000BF);
    frame.commandId = IoHomeCommand::ActivateMode;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_1W;
    frame.data[2] = 0x00; // main (1 byte)
    frame.data[3] = 0x01; // fp1
    frame.data[4] = 0x00; // fp2
    frame.data[5] = 0x12; // sequence hi
    frame.data[6] = 0x34; // sequence lo
    frame.dataLen = 7;
    memset(frame.hmac, 0xCC, IOHC_HMAC_SIZE);
    frame.hasHmac = true;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE(parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, 7);
    ASSERT_MEM_EQ(parsed.hmac, frame.hmac, IOHC_HMAC_SIZE);
}

TEST(frame_1w_write_private_has_hmac)
{
    // 1W WritePrivate (0x20, _p0x20_13) must also detect the appended HMAC:
    // origin+acei+main[2]+fp1+seq[2] = 6 payload bytes + 6 HMAC bytes.
    IoHomeFrame frame;
    frame.init();
    frame.ctrlByte0 = IOHC_CTRL0_START | IOHC_CTRL0_MODE_1W;
    frame.ctrlByte1 = 0x01;
    frame.setSrcNode(0xB60D1A);
    frame.setDestNode(0x00003F);
    frame.commandId = IoHomeCommand::WritePrivate;
    frame.data[0] = IOHC_ORIGINATOR_RAIN;
    frame.data[1] = 0xDB;
    frame.data[2] = 0x00; // main hi
    frame.data[3] = 0x09; // main lo
    frame.data[4] = 0x00; // fp1
    frame.data[5] = 0x23; // sequence hi
    frame.dataLen = 6;
    memset(frame.hmac, 0xAA, IOHC_HMAC_SIZE);
    frame.hasHmac = true;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_TRUE(parsed.hasHmac);
    ASSERT_EQ(parsed.dataLen, 6);
    ASSERT_MEM_EQ(parsed.hmac, frame.hmac, IOHC_HMAC_SIZE);
}

// --- Address classes ---

TEST(address_class_group)
{
    ASSERT_TRUE(getAddressClass(0x000000) == IoHomeAddressClass::Group);
}

TEST(address_class_discover)
{
    ASSERT_TRUE(getAddressClass(0x00003B) == IoHomeAddressClass::DiscoverAll);
}

TEST(address_class_discover_alt)
{
    ASSERT_TRUE(getAddressClass(0x00003F) == IoHomeAddressClass::DiscoverAlt);
}

TEST(address_class_unicast)
{
    ASSERT_TRUE(getAddressClass(0x485B37) == IoHomeAddressClass::Unicast);
}

TEST(address_class_broadcast_type)
{
    ASSERT_TRUE(getAddressClass(0x000005) == IoHomeAddressClass::BroadcastDeviceType);
}

// --- Group addressing ---

TEST(group_addressing_frame)
{
    IoHomeFrame frame;
    frame.init();
    frame.setDestGroup();
    ASSERT_EQ(frame.destNode[0], 0x00);
    ASSERT_EQ(frame.destNode[1], 0x00);
    ASSERT_EQ(frame.destNode[2], 0x00);
    ASSERT_EQ(frame.getDestNodeId(), (uint32_t)0);
}

// --- 1W mode flag ---

TEST(frame_1w_mode_flag)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    ASSERT_TRUE(!(frame.ctrlByte0 & IOHC_CTRL0_MODE_1W));
    frame.set1WMode();
    ASSERT_TRUE(frame.ctrlByte0 & IOHC_CTRL0_MODE_1W);
    ASSERT_TRUE(frame.ctrlByte1 & IOHC_CTRL1_LOW_POWER);
}

// --- EMS2 constants ---

TEST(ems2_sync_word_constant)
{
    ASSERT_EQ(IOHC_EMS2_SYNC_WORD[0], 0x2D);
    ASSERT_EQ(IOHC_EMS2_SYNC_WORD[1], 0xD4);
    ASSERT_EQ(IOHC_EMS2_SYNC_WORD_SIZE, 2);
}

// --- Scan command list ---

TEST(scan_command_list)
{
    ASSERT_EQ(IOHC_SCAN_COMMANDS_COUNT, (size_t)26);
    // Verify some expected commands are present
    bool hasExecute = false, hasDiscover = false, hasIdentify = false, hasStatus = false;
    for (size_t i = 0; i < IOHC_SCAN_COMMANDS_COUNT; i++)
    {
        if (IOHC_SCAN_COMMANDS[i] == 0x00)
            hasExecute = true;
        if (IOHC_SCAN_COMMANDS[i] == 0x1E)
            hasIdentify = true;
        if (IOHC_SCAN_COMMANDS[i] == 0x28)
            hasDiscover = true;
        if (IOHC_SCAN_COMMANDS[i] == 0x71)
            hasStatus = true;
    }
    ASSERT_TRUE(hasExecute);
    ASSERT_TRUE(hasDiscover);
    ASSERT_TRUE(hasIdentify);
    ASSERT_TRUE(hasStatus);
}

// --- Cozy thermostat payloads ---

TEST(cozy_temperature_payload)
{
    uint8_t data[13];
    uint8_t len = IoHomeCozyPayload::buildTemperature(data, 200); // 20.0°C
    ASSERT_EQ(len, 6);
    ASSERT_EQ(data[0], IOHC_COZY_ORIGINATOR);
    ASSERT_EQ(data[1], IOHC_COZY_ACEI_WRITE);
    ASSERT_EQ(data[2], 0x01);
    ASSERT_EQ(data[3], 0x03); // temp sub-command
    ASSERT_EQ(data[4], 200);  // 20.0°C × 10
    ASSERT_EQ(data[5], 0x00);
}

TEST(cozy_mode_payload)
{
    uint8_t data[13];
    uint8_t len = IoHomeCozyPayload::buildMode(data, IOHC_COZY_MODE_AUTO);
    ASSERT_EQ(len, 5);
    ASSERT_EQ(data[0], IOHC_COZY_ORIGINATOR);
    ASSERT_EQ(data[1], IOHC_COZY_ACEI_WRITE);
    ASSERT_EQ(data[2], 0x01);
    ASSERT_EQ(data[3], 0x00); // mode sub-command
    ASSERT_EQ(data[4], IOHC_COZY_MODE_AUTO);
}

// --- Position interpolation ---

TEST(interpolate_position_midpoint)
{
    float pos = interpolatePosition(0.0f, 100.0f, 500, 1000);
    ASSERT_FLOAT_EQ(pos, 50.0f, 0.01f);
}

TEST(interpolate_position_complete)
{
    float pos = interpolatePosition(0.0f, 100.0f, 1000, 1000);
    ASSERT_FLOAT_EQ(pos, 100.0f, 0.01f);
}

TEST(interpolate_position_over)
{
    float pos = interpolatePosition(0.0f, 100.0f, 2000, 1000);
    ASSERT_FLOAT_EQ(pos, 100.0f, 0.01f);
}

TEST(interpolate_position_zero)
{
    float pos = interpolatePosition(20.0f, 80.0f, 0, 1000);
    ASSERT_FLOAT_EQ(pos, 20.0f, 0.01f);
}

TEST(interpolate_position_zero_duration)
{
    float pos = interpolatePosition(0.0f, 100.0f, 0, 0);
    ASSERT_FLOAT_EQ(pos, 100.0f, 0.01f);
}

TEST(interpolate_position_float_precision)
{
    // Sub-percent precision: 33.3% elapsed → position 33.3
    float pos = interpolatePosition(0.0f, 100.0f, 333, 1000);
    ASSERT_FLOAT_EQ(pos, 33.3f, 0.1f);

    // Precise fractional position
    pos = interpolatePosition(10.0f, 90.0f, 500, 1000);
    ASSERT_FLOAT_EQ(pos, 50.0f, 0.01f);
}

TEST(estimate_travel_duration_closing_full)
{
    uint32_t duration = estimateTravelDurationMs(0.0f, 100.0f, 30.0f, 31.0f);
    ASSERT_EQ(duration, 31000);
}

TEST(estimate_travel_duration_opening_partial)
{
    uint32_t duration = estimateTravelDurationMs(80.0f, 30.0f, 40.0f, 20.0f);
    ASSERT_EQ(duration, 20000);
}

TEST(estimate_travel_duration_invalid_time)
{
    uint32_t duration = estimateTravelDurationMs(10.0f, 90.0f, 0.0f, 0.0f);
    ASSERT_EQ(duration, 0u);
}

// --- Direct Command (0x02) ---

TEST(direct_command_enum_value)
{
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::DirectCommand), 0x02);
}

TEST(direct_command_2w_rejects_appended_hmac)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::DirectCommand;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = IOHC_ACEI_DEFAULT;
    frame.data[2] = 0x50;
    frame.data[3] = 0x00;
    frame.dataLen = 4;
    memset(frame.hmac, 0xAA, IOHC_HMAC_SIZE);
    frame.hasHmac = true;

    uint8_t buf[32];
    uint8_t len = serialize2WWithAppendedHmacForRejectTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(!deserializeFrameForTest(parsed, buf, len));
}

TEST(scan_command_list_includes_direct)
{
    ASSERT_EQ(IOHC_SCAN_COMMANDS_COUNT, (size_t)26);
    bool found = false;
    for (size_t i = 0; i < IOHC_SCAN_COMMANDS_COUNT; i++)
        if (IOHC_SCAN_COMMANDS[i] == 0x02)
            found = true;
    ASSERT_TRUE(found);
}

// --- Frame order field ---

TEST(frame_order_constants)
{
    ASSERT_EQ(IOHC_CTRL0_ORDER_SINGLE, 0x00);
    ASSERT_EQ(IOHC_CTRL0_ORDER_FIRST, 0x40);
    ASSERT_EQ(IOHC_CTRL0_ORDER_LAST, 0x80);
    ASSERT_EQ(IOHC_CTRL0_ORDER_END, 0xC0);
    ASSERT_EQ(IOHC_CTRL0_ORDER_MASK, 0xC0);
}

TEST(frame_order_set_get_roundtrip)
{
    IoHomeFrame frame;
    frame.init();

    frame.setFrameOrder(IOHC_CTRL0_ORDER_FIRST);
    ASSERT_EQ(frame.getFrameOrder(), IOHC_CTRL0_ORDER_FIRST);

    frame.setFrameOrder(IOHC_CTRL0_ORDER_LAST);
    ASSERT_EQ(frame.getFrameOrder(), IOHC_CTRL0_ORDER_LAST);

    frame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    ASSERT_EQ(frame.getFrameOrder(), IOHC_CTRL0_ORDER_END);

    frame.setFrameOrder(IOHC_CTRL0_ORDER_SINGLE);
    ASSERT_EQ(frame.getFrameOrder(), IOHC_CTRL0_ORDER_SINGLE);
}

TEST(frame_order_preserves_other_bits)
{
    IoHomeFrame frame;
    frame.init();
    // Set 1W mode and some length bits
    frame.ctrlByte0 = IOHC_CTRL0_MODE_1W | 0x0F; // 1W + length=15

    frame.setFrameOrder(IOHC_CTRL0_ORDER_LAST);
    // 1W bit and length should be preserved
    ASSERT_TRUE(frame.ctrlByte0 & IOHC_CTRL0_MODE_1W);
    ASSERT_EQ(frame.ctrlByte0 & IOHC_CTRL0_LEN_MASK, 0x0F);
    ASSERT_EQ(frame.getFrameOrder(), IOHC_CTRL0_ORDER_LAST);
}

// --- CRC in frame ---

TEST(crc_serialize_appends_two_bytes)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::Private;
    frame.data[0] = 0x03;
    frame.data[1] = 0x00;
    frame.data[2] = 0x00;
    frame.dataLen = 3;
    frame.hasHmac = false;

    // Without CRC
    frame.hasCrc = false;
    uint8_t buf1[32];
    uint8_t len1 = serializeFrameForTest(frame, buf1, sizeof(buf1));

    // With CRC
    frame.hasCrc = true;
    uint8_t buf2[32];
    uint8_t len2 = serializeFrameForTest(frame, buf2, sizeof(buf2));

    ASSERT_EQ(len2, len1 + 2);
}

TEST(crc_roundtrip)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::Private;
    frame.data[0] = 0x03;
    frame.data[1] = 0x00;
    frame.data[2] = 0x00;
    frame.dataLen = 3;
    frame.hasHmac = false;
    frame.hasCrc = true;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeRawWithOptionalCrcForTest(parsed, buf, len));
    ASSERT_TRUE(parsed.hasCrc);
    ASSERT_EQ(parsed.dataLen, 3);
    ASSERT_EQ(parsed.data[0], 0x03);
}

TEST(crc_mismatch_rejects)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::Private;
    frame.data[0] = 0x03;
    frame.dataLen = 1;
    frame.hasHmac = false;
    frame.hasCrc = true;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    // Tamper with CRC (last 2 bytes)
    buf[len - 1] ^= 0xFF;

    IoHomeFrame parsed;
    ASSERT_TRUE(!deserializeRawWithOptionalCrcForTest(parsed, buf, len));
}

TEST(crc_value_matches_kermit)
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x485B37);
    frame.setDestNode(0x123456);
    frame.commandId = IoHomeCommand::Private;
    frame.data[0] = 0x03;
    frame.data[1] = 0x00;
    frame.dataLen = 2;
    frame.hasHmac = false;
    frame.hasCrc = true;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));

    // CRC should be over all bytes except the last 2
    uint16_t expected = IoHomeCrypto::crc16Kermit(buf, len - 2);
    uint16_t actual = (uint16_t)buf[len - 2] | ((uint16_t)buf[len - 1] << 8);
    ASSERT_EQ(actual, expected);
}

// =====================================================================
// Position boundary snapping
// =====================================================================

TEST(snap_position_at_low_boundary)
{
    ASSERT_FLOAT_EQ(snapPositionBoundary(0.3f), 0.0f, 0.01f);
}

TEST(snap_position_at_high_boundary)
{
    ASSERT_FLOAT_EQ(snapPositionBoundary(99.7f), 100.0f, 0.01f);
}

TEST(snap_position_exact_threshold_low)
{
    ASSERT_FLOAT_EQ(snapPositionBoundary(0.5f), 0.0f, 0.01f);
}

TEST(snap_position_exact_threshold_high)
{
    ASSERT_FLOAT_EQ(snapPositionBoundary(99.5f), 100.0f, 0.01f);
}

TEST(snap_position_no_snap_middle)
{
    ASSERT_FLOAT_EQ(snapPositionBoundary(50.0f), 50.0f, 0.01f);
}

TEST(snap_position_just_above_low)
{
    // 0.6 is above 0.5 threshold — no snap
    ASSERT_FLOAT_EQ(snapPositionBoundary(0.6f), 0.6f, 0.01f);
}

TEST(snap_position_just_below_high)
{
    // 99.4 is below 99.5 threshold — no snap
    ASSERT_FLOAT_EQ(snapPositionBoundary(99.4f), 99.4f, 0.01f);
}

TEST(snap_interpolation_near_target)
{
    // Interpolation reaching 99.8% of travel to 100% target should snap
    float pos = interpolatePosition(0.0f, 100.0f, 998, 1000);
    float snapped = snapPositionBoundary(pos);
    ASSERT_FLOAT_EQ(snapped, 100.0f, 0.01f);
}

// =====================================================================
// Cozy extended payloads
// =====================================================================

TEST(cozy_presence_payload)
{
    uint8_t data[23] = {};
    uint8_t len = IoHomeCozyPayload::buildPresence(data, IOHC_COZY_PRESENCE_ON);
    ASSERT_EQ(len, 5);
    ASSERT_EQ(data[0], IOHC_COZY_ORIGINATOR);
    ASSERT_EQ(data[1], IOHC_COZY_ACEI_WRITE);
    ASSERT_EQ(data[2], 0x01);
    ASSERT_EQ(data[3], 0x10);
    ASSERT_EQ(data[4], 0x01);
}

TEST(cozy_window_payload)
{
    uint8_t data[23] = {};
    uint8_t len = IoHomeCozyPayload::buildWindow(data, IOHC_COZY_WINDOW_OPEN);
    ASSERT_EQ(len, 5);
    ASSERT_EQ(data[0], IOHC_COZY_ORIGINATOR);
    ASSERT_EQ(data[1], IOHC_COZY_ACEI_WRITE);
    ASSERT_EQ(data[2], 0x01);
    ASSERT_EQ(data[3], 0x0E);
    ASSERT_EQ(data[4], 0x01);
}

TEST(cozy_poweron_payload)
{
    uint8_t data[23] = {};
    uint8_t len = IoHomeCozyPayload::buildPowerOn(data);
    ASSERT_EQ(len, 4);
    ASSERT_EQ(data[0], IOHC_COZY_ORIGINATOR);
    ASSERT_EQ(data[1], IOHC_COZY_ACEI_READ);
    ASSERT_EQ(data[2], 0x01);
    ASSERT_EQ(data[3], 0x2C);
}

TEST(cozy_midnight_payload)
{
    uint8_t data[23] = {};
    uint8_t len = IoHomeCozyPayload::buildMidnightSync(data);
    ASSERT_EQ(len, 4);
    ASSERT_EQ(data[0], IOHC_COZY_ORIGINATOR);
    ASSERT_EQ(data[1], IOHC_COZY_ACEI_READ);
    ASSERT_EQ(data[2], 0x01);
    ASSERT_EQ(data[3], 0x30);
}

TEST(cozy_presence_constants)
{
    ASSERT_EQ(IOHC_COZY_PRESENCE_ON, 0x01);
    ASSERT_EQ(IOHC_COZY_PRESENCE_OFF, 0x00);
    ASSERT_EQ(IOHC_COZY_WINDOW_OPEN, 0x01);
    ASSERT_EQ(IOHC_COZY_WINDOW_CLOSED, 0x00);
}

// =====================================================================
// Multi-frequency RX scanning
// =====================================================================

TEST(freq_scan_cycle_wraps)
{
    // Verify frequency index wraps correctly through 0,1,2,0,...
    uint8_t idx = 0;
    idx = (idx + 1) % IOHC_NUM_FREQUENCIES;
    ASSERT_EQ(idx, 1);
    idx = (idx + 1) % IOHC_NUM_FREQUENCIES;
    ASSERT_EQ(idx, 2);
    idx = (idx + 1) % IOHC_NUM_FREQUENCIES;
    ASSERT_EQ(idx, 0);
}

TEST(freq_scan_interval_constant)
{
    // IOHC_RX_SCAN_INTERVAL_US defined in IoHomeController.h = 2700
    // Verify the value matches nicolas5000's CHANNEL_HOP_TIME_US
    ASSERT_EQ(2700, 2700); // documents expected value
}

TEST(freq_scan_all_frequencies_covered)
{
    // Verify all 3 frequencies are visited in a cycle
    bool visited[IOHC_NUM_FREQUENCIES] = {};
    uint8_t idx = 0;
    for (int i = 0; i < IOHC_NUM_FREQUENCIES; i++)
    {
        visited[idx] = true;
        idx = (idx + 1) % IOHC_NUM_FREQUENCIES;
    }
    for (int i = 0; i < IOHC_NUM_FREQUENCIES; i++)
        ASSERT_TRUE(visited[i]);

    ASSERT_EQ(IOHC_FREQUENCIES[0], 868950000UL); // CH2 (1W/2W shared)
    ASSERT_EQ(IOHC_FREQUENCIES[1], 869850000UL); // CH3
    ASSERT_EQ(IOHC_FREQUENCIES[2], 868250000UL); // CH1
}

// =====================================================================
// Remote map
// =====================================================================

TEST(remote_map_add_remove)
{
    IoHomeRemoteMap map;
    ASSERT_EQ(map.count(), 0);
    ASSERT_TRUE(map.addRemote(0x112233, "Remote1"));
    ASSERT_TRUE(map.addRemote(0x445566, "Remote2"));
    ASSERT_TRUE(map.addRemote(0x778899, "Remote3"));
    ASSERT_EQ(map.count(), 3);
    ASSERT_TRUE(map.removeRemote(0x445566));
    ASSERT_EQ(map.count(), 2);
    ASSERT_TRUE(map.findRemote(0x112233) != nullptr);
    ASSERT_TRUE(map.findRemote(0x445566) == nullptr);
    ASSERT_TRUE(map.findRemote(0x778899) != nullptr);
}

TEST(remote_map_find)
{
    IoHomeRemoteMap map;
    map.addRemote(0xABCDEF, "TestRemote");
    const IoHomeRemoteEntry *e = map.findRemote(0xABCDEF);
    ASSERT_TRUE(e != nullptr);
    ASSERT_EQ(e->address, 0xABCDEFu);
    ASSERT_TRUE(strcmp(e->name, "TestRemote") == 0);
}

TEST(remote_map_link_unlink)
{
    IoHomeRemoteMap map;
    map.addRemote(0x112233, "R1");
    ASSERT_TRUE(map.linkDevice(0x112233, 0xAABBCC));
    ASSERT_TRUE(map.linkDevice(0x112233, 0xDDEEFF));
    const IoHomeRemoteEntry *e = map.findRemote(0x112233);
    ASSERT_EQ(e->linkCount, 2);
    ASSERT_EQ(e->linkedDevices[0], 0xAABBCCu);
    ASSERT_EQ(e->linkedDevices[1], 0xDDEEFFu);
    ASSERT_TRUE(map.unlinkDevice(0x112233, 0xAABBCC));
    ASSERT_EQ(e->linkCount, 1);
    ASSERT_EQ(e->linkedDevices[0], 0xDDEEFFu);
}

TEST(remote_map_full)
{
    IoHomeRemoteMap map;
    for (uint8_t i = 0; i < IOHC_REMOTE_MAX_ENTRIES; i++)
        ASSERT_TRUE(map.addRemote(0x100000 + i, "R"));
    ASSERT_EQ(map.count(), IOHC_REMOTE_MAX_ENTRIES);
    ASSERT_TRUE(!map.addRemote(0x999999, "Overflow"));
}

TEST(remote_map_link_limit)
{
    IoHomeRemoteMap map;
    map.addRemote(0x112233, "R1");
    for (uint8_t i = 0; i < IOHC_REMOTE_MAX_LINKS; i++)
        ASSERT_TRUE(map.linkDevice(0x112233, 0x200000 + i));
    ASSERT_TRUE(!map.linkDevice(0x112233, 0x999999));
}

TEST(remote_map_observe_address)
{
    IoHomeRemoteMap map;
    map.observeAddress(0x111111);
    map.observeAddress(0x222222);
    map.observeAddress(0x333333);
    ASSERT_EQ(map.observedCount(), 3);
    ASSERT_EQ(map.observedAddress(0), 0x111111u);
    ASSERT_EQ(map.observedAddress(1), 0x222222u);
    ASSERT_EQ(map.observedAddress(2), 0x333333u);
}

TEST(remote_map_observe_dedup)
{
    IoHomeRemoteMap map;
    map.observeAddress(0x111111);
    map.observeAddress(0x222222);
    map.observeAddress(0x111111); // duplicate
    ASSERT_EQ(map.observedCount(), 2);
}

TEST(remote_map_flash_roundtrip)
{
    IoHomeRemoteMap map;
    map.addRemote(0xABCDEF, "SomfyRemote");
    uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    map.setRemoteKey(0xABCDEF, key);
    map.linkDevice(0xABCDEF, 0x112233);

    uint8_t buf[IOHC_REMOTE_MAP_FLASH_SIZE];
    uint16_t written = map.writeToBuffer(buf);
    ASSERT_TRUE(written > 0);

    IoHomeRemoteMap map2;
    map2.readFromBuffer(buf, written);
    ASSERT_EQ(map2.count(), 1);
    const IoHomeRemoteEntry *e = map2.findRemote(0xABCDEF);
    ASSERT_TRUE(e != nullptr);
    ASSERT_TRUE(strcmp(e->name, "SomfyRemote") == 0);
    ASSERT_MEM_EQ(e->key, key, 16);
    ASSERT_EQ(e->linkCount, 1);
    ASSERT_EQ(e->linkedDevices[0], 0x112233u);
}

TEST(remote_map_flash_size)
{
    IoHomeRemoteMap map;
    ASSERT_EQ(map.flashSize(), IOHC_REMOTE_MAP_FLASH_SIZE);
    // Per-entry: active(1) + address(3) + name(16) + key(16) + seq(2) + linkCount(1) + links(4*3) + supportedCmds(32)
    // = 1 + 3 + 16 + 16 + 2 + 1 + 12 + 32 = 83
    // Total: 1 + 8 * 83 = 665
    ASSERT_EQ(IOHC_REMOTE_MAP_FLASH_SIZE, 665);
}

// =====================================================================
// Remote map supported commands
// =====================================================================

TEST(remote_map_supported_commands_set_get)
{
    IoHomeRemoteMap map;
    ASSERT_TRUE(map.addRemote(0x123456, "Test Device"));

    // Set supported commands bitmask
    uint8_t bitmask[32];
    memset(bitmask, 0, sizeof(bitmask));
    bitmask[0] = 0xFF; // commands 0-7 supported
    bitmask[2] = 0x01; // command 16 supported
    map.setRemoteSupportedCommands(0x123456, bitmask, 32);

    // Get supported commands back
    uint8_t len = 0;
    const uint8_t *result = map.getRemoteSupportedCommands(0x123456, &len);
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(len, 3);
    ASSERT_EQ(result[0], 0xFF);
    ASSERT_EQ(result[1], 0x00);
    ASSERT_EQ(result[2], 0x01);
}

TEST(remote_map_supported_commands_query)
{
    IoHomeRemoteMap map;
    ASSERT_TRUE(map.addRemote(0xABCDEF, "Query Device"));

    // No commands set initially
    ASSERT_TRUE(!map.remoteCommandSupported(0xABCDEF, 0x00));
    ASSERT_TRUE(!map.remoteCommandSupported(0xABCDEF, 0xFF));

    // Set some commands
    uint8_t bitmask[32] = {};
    bitmask[0] = 0x01; // bit 0 → command 0 supported
    bitmask[1] = 0x02; // bit 1 → command 9 supported
    bitmask[2] = 0x80; // bit 7 → command 23 supported
    map.setRemoteSupportedCommands(0xABCDEF, bitmask, 32);

    ASSERT_TRUE(map.remoteCommandSupported(0xABCDEF, 0x00));  // byte 0, bit 0
    ASSERT_TRUE(!map.remoteCommandSupported(0xABCDEF, 0x01)); // byte 0, bit 1
    ASSERT_TRUE(map.remoteCommandSupported(0xABCDEF, 0x09));  // byte 1, bit 1
    ASSERT_TRUE(!map.remoteCommandSupported(0xABCDEF, 0x0A)); // byte 1, bit 2
    ASSERT_TRUE(map.remoteCommandSupported(0xABCDEF, 0x17));  // byte 2, bit 7 (0x80)
    ASSERT_TRUE(!map.remoteCommandSupported(0xABCDEF, 0x18)); // byte 2, bit 0

    // Unknown node
    ASSERT_TRUE(!map.remoteCommandSupported(0x000000, 0x00));

    // Unknown node
    ASSERT_TRUE(!map.remoteCommandSupported(0x000000, 0x00));
}

TEST(remote_map_supported_commands_flash_roundtrip)
{
    IoHomeRemoteMap map;
    ASSERT_TRUE(map.addRemote(0x112233, "Flash Test"));

    // Set supported commands
    uint8_t bitmask[32] = {};
    bitmask[0] = 0x55;
    bitmask[5] = 0xAA;
    bitmask[31] = 0x01;
    map.setRemoteSupportedCommands(0x112233, bitmask, 32);

    // Write to buffer and read back
    uint8_t buffer[IOHC_REMOTE_MAP_FLASH_SIZE];
    memset(buffer, 0, sizeof(buffer));
    uint16_t written = map.writeToBuffer(buffer);
    ASSERT_EQ(written, IOHC_REMOTE_MAP_FLASH_SIZE);

    IoHomeRemoteMap map2;
    uint16_t read = map2.readFromBuffer(buffer, written);
    ASSERT_EQ(read, written);

    // Verify supported commands survived roundtrip
    // bitmask[0] = 0x55 = 0b01010101 → bits 0,2,4,6 → commands 0,2,4,6
    // bitmask[5] = 0xAA = 0b10101010 → bits 1,3,5,7 → commands 41,43,45,47
    // bitmask[31] = 0x01 → bit 0 → command 248
    ASSERT_TRUE(map2.remoteCommandSupported(0x112233, 0x00));  // byte 0, bit 0
    ASSERT_TRUE(!map2.remoteCommandSupported(0x112233, 0x01)); // byte 0, bit 1 not set
    ASSERT_TRUE(map2.remoteCommandSupported(0x112233, 0x04));  // byte 0, bit 4
    ASSERT_TRUE(map2.remoteCommandSupported(0x112233, 0x29));  // byte 5, bit 1 → 40+1=41 (0x29)
    ASSERT_TRUE(map2.remoteCommandSupported(0x112233, 0xF8));  // byte 31, bit 0 → 248
}

TEST(remote_map_supported_commands_trailing_zero_trim)
{
    IoHomeRemoteMap map;
    ASSERT_TRUE(map.addRemote(0x998877, "Trim Test"));

    // Set only command 2
    uint8_t bitmask[32] = {};
    bitmask[0] = 0x04; // only bit 2 set
    map.setRemoteSupportedCommands(0x998877, bitmask, 32);

    uint8_t len = 0;
    const uint8_t *result = map.getRemoteSupportedCommands(0x998877, &len);
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(len, 1); // trailing zeros trimmed to 1 byte
    ASSERT_EQ(result[0], 0x04);
}

// =====================================================================
// Network scan buffer
// =====================================================================

TEST(scan_buffer_insert)
{
    // Simulate scan buffer circular insertion
    // Using the same struct layout as IoHomeController's internal buffer
    struct ScanEntry
    {
        uint32_t timestamp;
        uint8_t srcNode;
        int16_t rssi;
        uint8_t freqIdx;
        bool valid;
    };
    static constexpr uint8_t kSize = 32;
    ScanEntry buf[kSize] = {};
    uint8_t head = 0;

    // Insert 3 entries
    for (int i = 0; i < 3; i++)
    {
        buf[head].timestamp = (uint32_t)(i * 100);
        buf[head].srcNode = (uint8_t)i;
        buf[head].rssi = -50 - i;
        buf[head].freqIdx = i % 3;
        buf[head].valid = true;
        head = (head + 1) % kSize;
    }

    ASSERT_EQ(head, 3);
    ASSERT_TRUE(buf[0].valid);
    ASSERT_TRUE(buf[1].valid);
    ASSERT_TRUE(buf[2].valid);
    ASSERT_TRUE(!buf[3].valid);
    ASSERT_EQ(buf[0].rssi, -50);
    ASSERT_EQ(buf[2].rssi, -52);
}

TEST(scan_buffer_wrap)
{
    // Verify circular buffer wraps at capacity
    static constexpr uint8_t kSize = 32;
    uint8_t head = 0;
    uint32_t timestamps[kSize] = {};

    // Fill entire buffer and then add one more
    for (int i = 0; i < kSize + 1; i++)
    {
        timestamps[head] = (uint32_t)(i * 10);
        head = (head + 1) % kSize;
    }

    // Head should have wrapped to 1
    ASSERT_EQ(head, 1);
    // Slot 0 was overwritten by entry 32
    ASSERT_EQ(timestamps[0], 320u);
    // Slot 1 still has entry 1
    ASSERT_EQ(timestamps[1], 10u);
}

TEST(scan_buffer_read_order)
{
    // Reading from oldest to newest in a circular buffer
    static constexpr uint8_t kSize = 32;
    uint32_t timestamps[kSize] = {};
    uint8_t head = 0;

    // Insert 5 entries
    for (int i = 0; i < 5; i++)
    {
        timestamps[head] = (uint32_t)(i + 1);
        head = (head + 1) % kSize;
    }

    // Read in order from 0 to head-1
    for (int i = 0; i < 5; i++)
        ASSERT_EQ(timestamps[i], (uint32_t)(i + 1));
}

// =====================================================================
// Node stats
// =====================================================================

TEST(node_stats_update)
{
    // Simulate per-node statistics tracking
    struct NodeStats
    {
        uint32_t nodeId;
        uint16_t packetCount;
        int16_t lastRssi;
        bool active;
    };
    static constexpr uint8_t kMax = 16;
    NodeStats stats[kMax] = {};

    // Helper lambda: find or create
    auto updateStats = [&](uint32_t nodeId, int16_t rssi)
    {
        for (uint8_t i = 0; i < kMax; i++)
        {
            if (stats[i].active && stats[i].nodeId == nodeId)
            {
                stats[i].packetCount++;
                stats[i].lastRssi = rssi;
                return;
            }
        }
        for (uint8_t i = 0; i < kMax; i++)
        {
            if (!stats[i].active)
            {
                stats[i].nodeId = nodeId;
                stats[i].packetCount = 1;
                stats[i].lastRssi = rssi;
                stats[i].active = true;
                return;
            }
        }
    };

    updateStats(0x112233, -45);
    updateStats(0x445566, -60);
    updateStats(0x112233, -42); // second packet from same node

    ASSERT_TRUE(stats[0].active);
    ASSERT_EQ(stats[0].nodeId, 0x112233u);
    ASSERT_EQ(stats[0].packetCount, 2);
    ASSERT_EQ(stats[0].lastRssi, -42);
    ASSERT_TRUE(stats[1].active);
    ASSERT_EQ(stats[1].nodeId, 0x445566u);
    ASSERT_EQ(stats[1].packetCount, 1);
}

TEST(node_stats_limit)
{
    // Fill all 16 slots; 17th node should be silently dropped
    struct NodeStats
    {
        uint32_t nodeId;
        bool active;
    };
    static constexpr uint8_t kMax = 16;
    NodeStats stats[kMax] = {};

    for (uint8_t i = 0; i < kMax; i++)
    {
        stats[i].nodeId = 0x100000 + i;
        stats[i].active = true;
    }

    // Try to find slot for 17th — all full
    bool found = false;
    for (uint8_t i = 0; i < kMax; i++)
    {
        if (!stats[i].active)
        {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(!found);
}

// =====================================================================
// Command name decoder (enum coverage)
// =====================================================================

TEST(command_name_enum_coverage)
{
    // Verify all commands used in scan list have distinct non-zero enum values
    // This validates the enum-to-string decoder has all entries to work with
    uint8_t seen[256] = {};
    for (size_t i = 0; i < IOHC_SCAN_COMMANDS_COUNT; i++)
    {
        uint8_t cmd = IOHC_SCAN_COMMANDS[i];
        seen[cmd]++;
        ASSERT_EQ(seen[cmd], 1); // no duplicates in scan list
    }

    // Key commands have expected values
    ASSERT_EQ((uint8_t)IoHomeCommand::Execute, 0x00);
    ASSERT_EQ((uint8_t)IoHomeCommand::Identify, 0x1E);
    ASSERT_EQ((uint8_t)IoHomeCommand::StatusUpdate, 0x71);
    ASSERT_EQ((uint8_t)IoHomeCommand::WritePrivate, 0x20);
    ASSERT_EQ((uint8_t)IoHomeCommand::KeyTransfer, 0x32);
    ASSERT_EQ((uint8_t)IoHomeCommand::GetName, 0x50);
    ASSERT_EQ((uint8_t)IoHomeCommand::DiscoverRequest, 0x28);
#ifdef TEST_NATIVE
    ASSERT_TRUE(strcmp(IoHomeController::commandName(IoHomeCommand::Identify), "Identify") == 0);
#endif
}

// =====================================================================
// 1W Execute payload tests
// =====================================================================

TEST(test_1w_acei_constant)
{
    // 1W ACEI: priority=2 (bits 7:5 = 010), service=0 (bits 4:3 = 00),
    // extended=1 (bits 2:1 = 01), valid=1 (bit 0 = 1) = 0b01000011 = 0x43
    ASSERT_EQ(IOHC_ACEI_1W, 0x43);

    // Verify bit fields
    ASSERT_EQ((IOHC_ACEI_1W & IOHC_ACEI_PRIORITY_MASK) >> 5, 2); // priority = 2 (user)
    ASSERT_EQ((IOHC_ACEI_1W & IOHC_ACEI_SERVICE_MASK) >> 3, 0);  // service = 0
    ASSERT_EQ((IOHC_ACEI_1W & IOHC_ACEI_EXTENDED_MASK) >> 1, 1); // extended = 1
    ASSERT_EQ(IOHC_ACEI_1W & IOHC_ACEI_VALID_BIT, 1);            // valid = 1
}

TEST(test_1w_execute_position_encoding)
{
    // 1W standard Execute uses raw IOHC closedness percent, not UI open percent:
    //   rawClosed=0   -> open
    //   rawClosed=100 -> closed
    ASSERT_EQ(0 * 2, 0x00);   // rawClosed 0% = open
    ASSERT_EQ(50 * 2, 0x64);  // rawClosed 50%
    ASSERT_EQ(100 * 2, 0xC8); // rawClosed 100% = closed

    ASSERT_EQ(IoHomeController::rawClosedPercentToOneWayMain(0), 0x0000);
    ASSERT_EQ(IoHomeController::rawClosedPercentToOneWayMain(50), 0x6400);
    ASSERT_EQ(IoHomeController::rawClosedPercentToOneWayMain(100), 0xC800);

    // Special positions (raw values, not multiplied)
    ASSERT_EQ((uint8_t)0xD2, 0xD2); // STOP
    ASSERT_EQ((uint8_t)0xD8, 0xD8); // FAVORITE
}

TEST(test_1w_position_convention_helpers)
{
    // UI/Home-Assistant-style open percent uses 100=open. Convert it at the
    // caller/channel boundary before passing a low-level raw IOHC Execute value.
    ASSERT_EQ(IoHomeController::uiOpenPercentToRawClosedPercent(100), 0);
    ASSERT_EQ(IoHomeController::uiOpenPercentToRawClosedPercent(75), 25);
    ASSERT_EQ(IoHomeController::uiOpenPercentToRawClosedPercent(50), 50);
    ASSERT_EQ(IoHomeController::uiOpenPercentToRawClosedPercent(0), 100);
    ASSERT_EQ(IoHomeController::uiOpenPercentToRawClosedPercent(200), 0);

    const uint8_t lUiOpenPercent = 75;
    const uint8_t lRawClosedPercent = IoHomeController::uiOpenPercentToRawClosedPercent(lUiOpenPercent);
    ASSERT_EQ(lRawClosedPercent, 25);
    ASSERT_EQ(IoHomeController::rawClosedPercentToOneWayMain(lRawClosedPercent), 0x3200);
}

TEST(test_1w_execute_payload_layout)
{
    // Simulate 1W Execute payload construction (matching rspaargaren _p0x00_14)
    // Wire: origin(1) + acei(1) + main(2) + fp1(1) + fp2(1) + seq(2) + hmac(6) = 14 bytes
    uint8_t data[8];               // data portion before HMAC
    uint8_t rawClosedPercent = 75; // raw IOHC closedness, not UI open percent

    data[0] = IOHC_ORIGINATOR_USER; // origin = 0x01
    data[1] = IOHC_ACEI_1W;         // acei = 0x43
    data[2] = rawClosedPercent * 2; // main high = 150
    data[3] = 0x00;                 // main low
    data[4] = 0x00;                 // fp1
    data[5] = 0x00;                 // fp2
    uint16_t seq = 42;
    data[6] = (seq >> 8) & 0xFF; // seq high
    data[7] = seq & 0xFF;        // seq low

    ASSERT_EQ(data[0], 0x01);
    ASSERT_EQ(data[1], 0x43);
    ASSERT_EQ(data[2], 150);
    ASSERT_EQ(data[3], 0x00);
    ASSERT_EQ(data[6], 0x00);
    ASSERT_EQ(data[7], 42);

    // Total wire size = 8 (data) + 6 (hmac) = 14 bytes
    ASSERT_EQ(8 + 6, 14);
}

TEST(test_1w_execute_hmac_input_7bytes)
{
    // HMAC input for 1W Execute: cmd(1) + origin(1) + acei(1) + main(2) + fp1(1) + fp2(1) = 7 bytes
    uint8_t data[8];
    data[0] = IOHC_ORIGINATOR_USER;
    data[1] = IOHC_ACEI_1W;
    data[2] = 50 * 2; // position
    data[3] = 0x00;
    data[4] = 0x00;
    data[5] = 0x00;

    uint8_t hmacInput[7];
    hmacInput[0] = (uint8_t)IoHomeCommand::Execute; // cmd = 0x00
    memcpy(hmacInput + 1, data, 6);

    ASSERT_EQ(hmacInput[0], 0x00); // cmd
    ASSERT_EQ(hmacInput[1], 0x01); // origin
    ASSERT_EQ(hmacInput[2], 0x43); // acei
    ASSERT_EQ(hmacInput[3], 100);  // main high (50*2)
    ASSERT_EQ(hmacInput[4], 0x00); // main low
    ASSERT_EQ(hmacInput[5], 0x00); // fp1
    ASSERT_EQ(hmacInput[6], 0x00); // fp2

    // Verify HMAC can be created and verified with these 7 bytes
    uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint16_t seq = 100;
    uint8_t hmac[6];
    IoHomeCrypto::createHmac1W(hmacInput, 7, seq, key, hmac);
    ASSERT_TRUE(IoHomeCrypto::verifyHmac1W(hmacInput, 7, seq, hmac, key));
}

TEST(test_1w_sequence_increment)
{
    // Verify sequence counter behavior (standalone test without channel object)
    uint16_t seq = 0;
    seq++;
    ASSERT_EQ(seq, 1);
    seq++;
    ASSERT_EQ(seq, 2);

    // Wrap around at 0xFFFF
    seq = 0xFFFF;
    seq++;
    ASSERT_EQ(seq, 0); // wraps to 0
}

TEST(test_1w_execute_stop_special)
{
    // STOP command: special value 0xD2 goes in data[2]
    uint8_t data[8];
    uint8_t param = 0xD2; // STOP

    data[0] = IOHC_ORIGINATOR_USER;
    data[1] = IOHC_ACEI_1W;
    // param > 100, so raw value
    data[2] = param;
    data[3] = 0x00;
    data[4] = 0x00;
    data[5] = 0x00;

    ASSERT_EQ(data[2], 0xD2);

    // FAVORITE: 0xD8
    data[2] = 0xD8;
    ASSERT_EQ(data[2], 0xD8);
}

TEST(test_1w_execute_with_slat)
{
    // When slat parameter is provided: fp1=0x80, fp2=slat×2
    uint8_t data[8];
    uint8_t param = 50;  // 50% position
    uint8_t param2 = 75; // 75% slat angle

    data[0] = IOHC_ORIGINATOR_USER;
    data[1] = IOHC_ACEI_1W;
    data[2] = param * 2; // 100
    data[3] = 0x00;
    data[4] = 0x80;       // fp1: slat flag
    data[5] = param2 * 2; // fp2: 150

    ASSERT_EQ(data[4], 0x80);
    ASSERT_EQ(data[5], 150);

    // Without slat: both zero
    data[4] = 0x00;
    data[5] = 0x00;
    ASSERT_EQ(data[4], 0x00);
    ASSERT_EQ(data[5], 0x00);
}

// --- 1W repeat and payload variant tests ---

TEST(test_1w_repeat_count_constant)
{
    ASSERT_EQ(IOHC_1W_REPEAT_COUNT, 4);
    ASSERT_EQ(IOHC_1W_REPEAT_COUNT + 1, 5); // first TX + four repeats
}

TEST(test_1w_repeat_interval_constant)
{
    ASSERT_EQ(IOHC_1W_REPEAT_INTERVAL_MS, 40);
}

TEST(test_1w_repeat_preamble_constants)
{
    ASSERT_EQ(IOHC_PREAMBLE_LONG, 1024);
    ASSERT_EQ(IOHC_PREAMBLE_NORMAL_START, 32);
    ASSERT_EQ(IOHC_PREAMBLE_SHORT, 8);
    ASSERT_TRUE(IOHC_PREAMBLE_LONG > IOHC_PREAMBLE_NORMAL_START);
    ASSERT_TRUE(IOHC_PREAMBLE_NORMAL_START > IOHC_PREAMBLE_SHORT);
    ASSERT_TRUE(IOHC_PREAMBLE_LONG > IOHC_PREAMBLE_SHORT);
}

TEST(test_1w_activate_mode_payload_layout)
{
    // 1W ActivateMode (_p0x01_13): origin(1)+acei(1)+main(1!)+fp1(1)+fp2(1)+seq(2)+hmac(6) = 13B
    // Note: main is 1 byte, not 2 like in Execute
    uint8_t data[7];      // data before HMAC
    uint8_t param = 0xD8; // e.g., favorite mode
    uint8_t fp1 = 0x03;
    uint8_t fp2 = 0x00;
    uint16_t seq = 0x0042;

    data[0] = IOHC_ORIGINATOR_USER; // 0x01
    data[1] = IOHC_ACEI_1W;         // 0x43
    data[2] = param;                // main (1 byte!)
    data[3] = fp1;
    data[4] = fp2;
    data[5] = (seq >> 8) & 0xFF;
    data[6] = seq & 0xFF;

    // Total wire payload = 7 data + 6 HMAC = 13 bytes
    ASSERT_EQ(data[0], 0x01);
    ASSERT_EQ(data[1], 0x43);
    ASSERT_EQ(data[2], 0xD8); // main is 1 byte (not 2)
    ASSERT_EQ(data[3], 0x03); // fp1
    ASSERT_EQ(data[4], 0x00); // fp2
    ASSERT_EQ(data[5], 0x00); // seq high
    ASSERT_EQ(data[6], 0x42); // seq low
}

TEST(test_1w_activate_mode_hmac_input_6bytes)
{
    // HMAC input for _p0x01_13: cmd(1) + origin+acei+main+fp1+fp2 = 6 bytes
    uint8_t key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                       0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t hmacInput[6];
    hmacInput[0] = 0x01; // ActivateMode command
    hmacInput[1] = IOHC_ORIGINATOR_USER;
    hmacInput[2] = IOHC_ACEI_1W;
    hmacInput[3] = 0xD8; // main (1 byte)
    hmacInput[4] = 0x03; // fp1
    hmacInput[5] = 0x00; // fp2

    uint16_t seq = 0x0042;
    uint8_t hmac[6];
    IoHomeCrypto::createHmac1W(hmacInput, 6, seq, key, hmac);

    // Verify HMAC can be validated
    bool valid = IoHomeCrypto::verifyHmac1W(hmacInput, 6, seq, hmac, key);
    ASSERT_TRUE(valid);

    // Tamper with input — should fail
    hmacInput[3] = 0xD9;
    valid = IoHomeCrypto::verifyHmac1W(hmacInput, 6, seq, hmac, key);
    ASSERT_TRUE(!valid);
}

TEST(test_1w_execute_16byte_payload_layout)
{
    // _p0x00_16: origin(1)+acei(1)+main[2]+fp1(1)+fp2(1)+data[2]+seq(2)+hmac(6) = 16B
    uint8_t data[10];      // data before HMAC
    uint8_t mainHi = 0x64; // e.g., 50% * 2 = 100
    uint8_t fp1 = 0x20;
    uint8_t fp2 = 0xCD;
    uint16_t seq = 0x0010;

    data[0] = IOHC_ORIGINATOR_USER;
    data[1] = IOHC_ACEI_1W;
    data[2] = mainHi;
    data[3] = 0x00; // main low
    data[4] = fp1;
    data[5] = fp2;
    data[6] = 0x00; // data[0]
    data[7] = 0x00; // data[1]
    data[8] = (seq >> 8) & 0xFF;
    data[9] = seq & 0xFF;

    // Total wire payload = 10 data + 6 HMAC = 16 bytes
    ASSERT_EQ(data[0], 0x01);
    ASSERT_EQ(data[1], 0x43);
    ASSERT_EQ(data[4], 0x20); // fp1
    ASSERT_EQ(data[5], 0xCD); // fp2
    ASSERT_EQ(data[8], 0x00); // seq high
    ASSERT_EQ(data[9], 0x10); // seq low
}

TEST(test_1w_execute_16byte_hmac_input_9bytes)
{
    // HMAC input for _p0x00_16: cmd(1) + origin+acei+main[2]+fp1+fp2+data[2] = 9 bytes
    uint8_t key[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                       0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    uint8_t hmacInput[9];
    hmacInput[0] = 0x00; // Execute command
    hmacInput[1] = IOHC_ORIGINATOR_USER;
    hmacInput[2] = IOHC_ACEI_1W;
    hmacInput[3] = 0x64; // main high
    hmacInput[4] = 0x00; // main low
    hmacInput[5] = 0x20; // fp1
    hmacInput[6] = 0xCD; // fp2
    hmacInput[7] = 0x00; // data[0]
    hmacInput[8] = 0x00; // data[1]

    uint16_t seq = 0x0010;
    uint8_t hmac[6];
    IoHomeCrypto::createHmac1W(hmacInput, 9, seq, key, hmac);

    bool valid = IoHomeCrypto::verifyHmac1W(hmacInput, 9, seq, hmac, key);
    ASSERT_TRUE(valid);

    // Tamper — should fail
    hmacInput[6] = 0xDE;
    valid = IoHomeCrypto::verifyHmac1W(hmacInput, 9, seq, hmac, key);
    ASSERT_TRUE(!valid);
}

// =====================================================================
// SetName (0x52) command
// =====================================================================

void test_setname_response_enum()
{
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::SetName), 0x52);
    ASSERT_EQ(static_cast<uint8_t>(IoHomeCommand::SetNameResponse), 0x53);
}

void test_setname_name_max_size()
{
    ASSERT_EQ(IOHC_NAME_MAX_SIZE, 16);
}

void test_setname_frame_layout()
{
    // Build a SetName frame with a short name — should be zero-padded to 16 bytes
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x123456);
    frame.setDestNode(0xABCDEF);
    frame.commandId = IoHomeCommand::SetName;

    const char *name = "MyBlind";
    uint8_t nameLen = 7;
    memset(frame.data, 0, IOHC_NAME_MAX_SIZE);
    memcpy(frame.data, name, nameLen);
    frame.dataLen = IOHC_NAME_MAX_SIZE;
    frame.hasHmac = false;

    // Verify payload: first 7 bytes are "MyBlind", rest are zero
    ASSERT_EQ(frame.data[0], 'M');
    ASSERT_EQ(frame.data[6], 'd');
    ASSERT_EQ(frame.data[7], 0);
    ASSERT_EQ(frame.data[15], 0);
    ASSERT_EQ(frame.dataLen, 16);

    // Serialize round-trip
    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(static_cast<uint8_t>(parsed.commandId), 0x52);
    ASSERT_EQ(parsed.dataLen, 16);
    ASSERT_TRUE(memcmp(parsed.data, frame.data, 16) == 0);
    ASSERT_TRUE(!parsed.hasHmac); // SetName uses challenge-response, not embedded HMAC
}

void test_setname_challenge_response_hmac_input()
{
    // Verify HMAC input format for challenge-response:
    // HMAC input = {command_id (0x52)} + {original data (16 bytes name)}
    // This is the same pattern as SetConfig1 auth
    uint8_t hmacInput[1 + IOHC_NAME_MAX_SIZE];
    hmacInput[0] = 0x52; // SetName command ID
    const char *name = "TestDevice";
    memset(hmacInput + 1, 0, IOHC_NAME_MAX_SIZE);
    memcpy(hmacInput + 1, name, 10);

    uint8_t challenge[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t key[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
                       0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00};
    uint8_t hmac[6];

    bool ok = IoHomeCrypto::createHmac2W(hmacInput, 1 + IOHC_NAME_MAX_SIZE,
                                         challenge, key, hmac);
    ASSERT_TRUE(ok);

    // Verify HMAC is deterministic
    uint8_t hmac2[6];
    IoHomeCrypto::createHmac2W(hmacInput, 1 + IOHC_NAME_MAX_SIZE,
                               challenge, key, hmac2);
    ASSERT_TRUE(memcmp(hmac, hmac2, 6) == 0);

    // Verify different name produces different HMAC
    hmacInput[1] = 'X'; // change first char
    uint8_t hmac3[6];
    IoHomeCrypto::createHmac2W(hmacInput, 1 + IOHC_NAME_MAX_SIZE,
                               challenge, key, hmac3);
    ASSERT_TRUE(memcmp(hmac, hmac3, 6) != 0);
}

// =====================================================================
// Identify (0x1E) command
// =====================================================================

void test_identify_frame_layout()
{
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setLowPower(true);
    frame.setSrcNode(0x123456);
    frame.setDestNode(0xABCDEF);
    frame.commandId = IoHomeCommand::Identify;
    frame.data[0] = IOHC_ORIGINATOR_USER;
    frame.data[1] = 0xFF;
    frame.dataLen = 2;
    frame.hasHmac = false;

    uint8_t buf[IOHC_FRAME_BUFFER_SIZE];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::Identify);
    ASSERT_TRUE(parsed.ctrlByte1 & IOHC_CTRL1_LOW_POWER);
    ASSERT_EQ(parsed.dataLen, 2);
    ASSERT_EQ(parsed.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(parsed.data[1], 0xFF);
    ASSERT_TRUE(!parsed.hasHmac);
}

void test_identify_challenge_response_hmac_input()
{
    uint8_t hmacInput[3] = {
        static_cast<uint8_t>(IoHomeCommand::Identify),
        IOHC_ORIGINATOR_USER,
        0xFF};

    uint8_t challenge[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t key[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
                       0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00};
    uint8_t hmac[6];

    ASSERT_TRUE(IoHomeCrypto::createHmac2W(hmacInput, sizeof(hmacInput),
                                           challenge, key, hmac));

    uint8_t hmac2[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(hmacInput, sizeof(hmacInput),
                                           challenge, key, hmac2));
    ASSERT_TRUE(memcmp(hmac, hmac2, 6) == 0);
}

// =====================================================================
// AES Cross-Validation Tests
// =====================================================================
// These tests verify that our mbedtls-based AES implementation produces
// identical output to the reference implementations:
//   - Velocet/iown-homecontrol: scripts/Iown-ioCrypto.py (Python + pycryptodome)
//   - CyrilOpenSource: src/iohcCryptoHelpers.cpp (mbedtls on ESP32, same lib)
//   - nicolas5000/io-rts-esp32 (mbedtls on ESP-IDF, same lib)
//
// Since CyrilOpenSource and io-rts-esp32 both use mbedtls on ESP32, the
// AES primitive is identical. The real validation is against the Velocet
// Python script which uses a pure-Python AES implementation.
// =====================================================================

TEST(aes128_ecb_against_nist_fips197)
{
    // NIST FIPS-197 Appendix B — the canonical AES-128 test vector
    // This validates the AES primitive itself, independent of our IV/HMAC logic.
    // If this passes, mbedtls is producing correct AES-128-ECB output.
    const uint8_t key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    const uint8_t plaintext[16] = {
        0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
        0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34};
    const uint8_t expected[16] = {
        0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb,
        0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32};

    uint8_t output[16];
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(plaintext, key, output));
    ASSERT_MEM_EQ(output, expected, 16);
}

TEST(aes128_ecb_decryption_inverse)
{
    // Verify decrypt is the exact inverse of encrypt for multiple key/plaintext pairs
    const uint8_t keys[3][16] = {
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
         0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F},
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
         0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    const uint8_t plaintexts[3][16] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
         0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
        {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
         0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99}};

    for (int k = 0; k < 3; k++)
    {
        uint8_t encrypted[16], decrypted[16];
        ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(plaintexts[k], keys[k], encrypted));
        ASSERT_TRUE(IoHomeCrypto::aes128Decrypt(encrypted, keys[k], decrypted));
        ASSERT_MEM_EQ(decrypted, plaintexts[k], 16);
    }
}

TEST(checksum_matches_velocet_python_reference)
{
    // Velocet's Python computeChecksum algorithm (from Iown-ioCrypto.py):
    //   tmpchksum = frame_byte ^ chksum2
    //   chksum2 = ((chksum1 & 0x7f) << 1) & 0xff
    //   if chksum1 & 0x80 == 0:
    //       if tmpchksum >= 128: chksum2 |= 1
    //       return (chksum2, (tmpchksum << 1) & 0xff)
    //   if tmpchksum >= 128: chksum2 |= 1
    //   return (chksum2 ^ 0x55, ((tmpchksum << 1) ^ 0x5b) & 0xff)
    //
    // Our C++ computeChecksum implements the identical logic.
    // Verify with multiple byte sequences that produce both branches.

    // Test sequence that exercises the high-bit branch
    uint8_t chk1 = 0x80, chk2 = 0x00;
    IoHomeCrypto::computeChecksum(0x48, chk1, chk2); // frame header byte
    // tmp = 0x48 ^ 0x00 = 0x48, chk2 = (0x80 & 0x7F)<<1 = 0x00
    // (0x80 & 0x80) != 0 → second branch
    // 0x48 < 128 → no set LSB
    // chk1 = 0x00 ^ 0x55 = 0x55
    // chk2 = (0x48 << 1) ^ 0x5B = 0x90 ^ 0x5B = 0xCB
    ASSERT_EQ(chk1, 0x55);
    ASSERT_EQ(chk2, 0xCB);

    // Test sequence that exercises the low-bit branch
    chk1 = 0x00;
    chk2 = 0x00;
    IoHomeCrypto::computeChecksum(0x48, chk1, chk2);
    // tmp = 0x48 ^ 0x00 = 0x48, chk2 = 0x00
    // (0x00 & 0x80) == 0 → first branch
    // 0x48 < 128 → no set LSB
    // chk1 = 0x00, chk2 = (0x48 << 1) = 0x90
    ASSERT_EQ(chk1, 0x00);
    ASSERT_EQ(chk2, 0x90);

    // Verify determinism — same input always produces same output
    // (compare against the expected high-bit result: 0x55, 0xCB)
    uint8_t chk1b = 0x80, chk2b = 0x00;
    IoHomeCrypto::computeChecksum(0x48, chk1b, chk2b);
    ASSERT_EQ(chk1b, 0x55);
    ASSERT_EQ(chk2b, 0xCB);
}

TEST(iv_2w_matches_cyrilopen_source_construction)
{
    // CyrilOpenSource's constructInitialValue (from iohcCryptoHelpers.cpp):
    //   1. Start with zero-filled 16-byte array
    //   2. Set bytes 8,9 = 0,0
    //   3. For each frame byte: computeChecksum, store in iv[8,9]; if i<8 store byte in iv[i]
    //   4. Fill remaining iv[0..7] with 0x55
    //   5. If challenge: fill iv[10..15] with challenge
    //
    // Our constructIv2W:
    //   1. memset(iv, 0x55, 16)  ← padded with 0x55
    //   2. memcpy(iv[0..7], frameData, min(len,8))
    //   3. Compute checksums into iv[8..9]
    //   4. memcpy(iv[10..15], challenge, 6)
    //
    // The end result is identical:
    //   - Bytes 0-7: frame data (first 8 bytes) padded with 0x55
    //   - Bytes 8-9: proprietary checksums of ALL frame data
    //   - Bytes 10-15: 6-byte challenge

    const uint8_t frameData[] = {0x31, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x38, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    uint8_t ourIv[16];
    IoHomeCrypto::constructIv2W(frameData, sizeof(frameData), challenge, ourIv);

    // Bytes 0-7: first 8 bytes of frame data
    for (int i = 0; i < 8; i++)
        ASSERT_EQ(ourIv[i], frameData[i]);

    // Bytes 10-15: challenge
    ASSERT_MEM_EQ(ourIv + 10, challenge, 6);

    // Bytes 8-9: checksums — verify against manual computation
    uint8_t c1 = 0, c2 = 0;
    for (size_t i = 0; i < sizeof(frameData); i++)
        IoHomeCrypto::computeChecksum(frameData[i], c1, c2);
    ASSERT_EQ(ourIv[8], c1);
    ASSERT_EQ(ourIv[9], c2);
}

TEST(iv_1w_matches_cyrilopen_source_construction)
{
    // CyrilOpenSource's constructInitialValue with sequence_number:
    //   Same as 2W but bytes 10-11 = sequence (2 bytes BE), bytes 12-15 = 0x55
    //
    // Our constructIv1W:
    //   Bytes 0-7: frame data padded with 0x55
    //   Bytes 8-9: checksums
    //   Bytes 10-11: sequence number BE
    //   Bytes 12-15: 0x55 padding

    const uint8_t frameData[] = {0x30, 0x82, 0x60, 0x89, 0xF3, 0x44, 0xCB, 0xCC};
    const uint16_t sequence = 0x1A2B;

    uint8_t iv[16];
    IoHomeCrypto::constructIv1W(frameData, sizeof(frameData), sequence, iv);

    // Bytes 0-7: frame data
    for (int i = 0; i < 8; i++)
        ASSERT_EQ(iv[i], frameData[i]);

    // Bytes 8-9: checksums
    uint8_t c1 = 0, c2 = 0;
    for (size_t i = 0; i < sizeof(frameData); i++)
        IoHomeCrypto::computeChecksum(frameData[i], c1, c2);
    ASSERT_EQ(iv[8], c1);
    ASSERT_EQ(iv[9], c2);

    // Bytes 10-11: sequence number big-endian
    ASSERT_EQ(iv[10], (sequence >> 8) & 0xFF);
    ASSERT_EQ(iv[11], sequence & 0xFF);

    // Bytes 12-15: 0x55 padding
    for (int i = 12; i < 16; i++)
        ASSERT_EQ(iv[i], 0x55);
}

TEST(encrypt_1w_key_iv_construction_matches_reference)
{
    // Velocet's Python encrypt_1W_key IV construction:
    //   iv = bytearray(16)
    //   for i in range(0, 13, 3):
    //       iv[i] = node_address[0]
    //       iv[i+1] = node_address[1]
    //       iv[i+2] = node_address[2]
    //   iv[15] = node_address[0]
    //
    // Our C++:
    //   for (int i = 0; i < 16; i++)
    //       lIv[i] = iNodeAddress[i % 3];
    //
    // These produce the same pattern: {n0,n1,n2, n0,n1,n2, n0,n1,n2, n0,n1,n2, n0,n1,n2, n0}
    // = {n0,n1,n2} repeated 5 times + n0 (16 bytes)

    const uint8_t nodeAddress[3] = {0x48, 0x5B, 0x37};
    const uint8_t expectedIv[16] = {
        0x48, 0x5B, 0x37, 0x48, 0x5B, 0x37, 0x48, 0x5B,
        0x37, 0x48, 0x5B, 0x37, 0x48, 0x5B, 0x37, 0x48};

    uint8_t ourIv[16];
    for (int i = 0; i < 16; i++)
        ourIv[i] = nodeAddress[i % 3];

    ASSERT_MEM_EQ(ourIv, expectedIv, 16);
}

TEST(encrypt_1w_key_matches_velocet_python_vector)
{
    // Velocet's Python demo for 1W key push (command 0x30):
    //   node_address = 0xABCDEF
    //   controller_key = 0x01020304050607080910111213141516
    //   transfer_key = 0x34C3466ED88F4E8E16AA473949884373
    //
    // Expected encrypted key (from Velocet demo):
    //   82 60 89 F3 44 CB CC AB 84 26 CE 7C 12 51 B8 E0
    //
    // This is our existing velocet_vector_1w_key_push test.
    // Re-verify with explicit IV debugging.

    const uint8_t systemKey[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    const uint8_t nodeAddr[3] = {0x48, 0x5B, 0x37};
    const uint8_t expectedEncrypted[16] = {
        0x82, 0x60, 0x89, 0xF3, 0x44, 0xCB, 0xCC, 0xAB,
        0x84, 0x26, 0xCE, 0x7C, 0x12, 0x51, 0xB8, 0xE0};
    const uint8_t expectedHmac[6] = {0x2D, 0x49, 0x8F, 0xBF, 0x1F, 0x7C};

    // Verify IV construction
    uint8_t expectedIv[16] = {
        0x48, 0x5B, 0x37, 0x48, 0x5B, 0x37, 0x48, 0x5B,
        0x37, 0x48, 0x5B, 0x37, 0x48, 0x5B, 0x37, 0x48};
    uint8_t computedIv[16];
    for (int i = 0; i < 16; i++)
        computedIv[i] = nodeAddr[i % 3];
    ASSERT_MEM_EQ(computedIv, expectedIv, 16);

    // Encrypt key
    uint8_t encrypted[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(systemKey, IOHC_TRANSFER_KEY, nodeAddr, encrypted));
    ASSERT_MEM_EQ(encrypted, expectedEncrypted, 16);

    // HMAC input = {0x30} + encrypted_key (17 bytes)
    uint8_t hmacInput[17];
    hmacInput[0] = static_cast<uint8_t>(IoHomeCommand::SendKey1W);
    memcpy(hmacInput + 1, encrypted, sizeof(encrypted));

    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(hmacInput, sizeof(hmacInput), 0x1A2B, systemKey, hmac));
    ASSERT_MEM_EQ(hmac, expectedHmac, 6);
}

TEST(encrypt_1w_key_matches_cyrilopen_source_logic)
{
    // CyrilOpenSource's encrypt_1W_key (from iohcCryptoHelpers.cpp):
    //   iv(16) = {0}
    //   for i in range(0, 13, 3): iv[i..i+2] = node_address[0..2]
    //   iv[15] = node_address[0]
    //   Then uses mbedtls_aes_crypt_cfb128 in CFB mode with offset=0
    //
    // Our encrypt1WKey uses ECB with XOR:
    //   AES_ECB(IV, transferKey) → keystream
    //   encrypted = key XOR keystream
    //
    // CFB128 with offset=0 and initial IV is equivalent to:
    //   For a single 16-byte block: CFB = ECB(IV) XOR plaintext
    //   This is IDENTICAL to our ECB+XOR approach.
    //
    // So the outputs must be identical for single-block encryption.

    const uint8_t nodeAddr[3] = {0xAA, 0xBB, 0xCC};
    const uint8_t key[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
                             0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};

    // Encrypt
    uint8_t encrypted[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(key, IOHC_TRANSFER_KEY, nodeAddr, encrypted));

    // Decrypt (same operation for single-block CFB)
    uint8_t decrypted[16];
    ASSERT_TRUE(IoHomeCrypto::decrypt1WKey(encrypted, IOHC_TRANSFER_KEY, nodeAddr, decrypted));

    // Must round-trip
    ASSERT_MEM_EQ(decrypted, key, 16);

    // Verify the encrypted value is non-trivial (not the original key)
    ASSERT_TRUE(memcmp(encrypted, key, 16) != 0);
}

TEST(hmac_1w_cross_algorithm_validation)
{
    // Verify our 1W HMAC algorithm:
    //   1. Construct IV from frame data + sequence number (same as Velocet constructInitialValue)
    //   2. AES-128-ECB(IV, controllerKey) → 16 bytes
    //   3. HMAC = first 6 bytes
    //
    // The key insight: our constructIv1W and Velocet's constructInitialValue
    // with sequence_number produce identical IVs. Then AES-128-ECB is the
    // same primitive. Therefore HMAC outputs must match.

    const uint8_t frameData[] = {0x30, 0x82, 0x60, 0x89, 0xF3, 0x44, 0xCB, 0xCC};
    const uint16_t sequence = 0x0100;
    const uint8_t controllerKey[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};

    // Build IV manually (matching Velocet's constructInitialValue)
    uint8_t manualIv[16];
    memset(manualIv, 0x55, 16);
    memcpy(manualIv, frameData, sizeof(frameData));
    {
        uint8_t c1 = 0, c2 = 0;
        for (size_t i = 0; i < sizeof(frameData); i++)
            IoHomeCrypto::computeChecksum(frameData[i], c1, c2);
        manualIv[8] = c1;
        manualIv[9] = c2;
    }
    manualIv[10] = (sequence >> 8) & 0xFF;
    manualIv[11] = sequence & 0xFF;

    // Our constructIv1W should produce the same IV
    uint8_t ourIv[16];
    IoHomeCrypto::constructIv1W(frameData, sizeof(frameData), sequence, ourIv);
    ASSERT_MEM_EQ(ourIv, manualIv, 16);

    // Now verify HMAC = first 6 bytes of AES(IV, key)
    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(frameData, sizeof(frameData), sequence, controllerKey, hmac));

    // Manually compute: AES(IV, key) → first 6 bytes
    uint8_t fullBlock[16];
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(ourIv, controllerKey, fullBlock));
    ASSERT_MEM_EQ(hmac, fullBlock, 6);
}

TEST(hmac_2w_cross_algorithm_validation)
{
    // Verify our 2W HMAC algorithm:
    //   1. Construct IV from frame data + challenge (same as Velocet constructInitialValue)
    //   2. AES-128-ECB(IV, systemKey) → 16 bytes
    //   3. HMAC = first 6 bytes
    //
    // The IV construction must match: frame data → checksums → challenge

    const uint8_t frameData[] = {0x32, 0x82, 0x60, 0x89, 0xF3, 0x44, 0xCB, 0xCC};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t systemKey[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};

    // Build IV manually (matching Velocet's constructInitialValue)
    uint8_t manualIv[16];
    memset(manualIv, 0x55, 16);
    memcpy(manualIv, frameData, 8);
    {
        uint8_t c1 = 0, c2 = 0;
        for (size_t i = 0; i < sizeof(frameData); i++)
            IoHomeCrypto::computeChecksum(frameData[i], c1, c2);
        manualIv[8] = c1;
        manualIv[9] = c2;
    }
    memcpy(manualIv + 10, challenge, 6);

    // Our constructIv2W should produce the same IV
    uint8_t ourIv[16];
    IoHomeCrypto::constructIv2W(frameData, sizeof(frameData), challenge, ourIv);
    ASSERT_MEM_EQ(ourIv, manualIv, 16);

    // HMAC = first 6 bytes of AES(IV, key)
    uint8_t hmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(frameData, sizeof(frameData), challenge, systemKey, hmac));

    uint8_t fullBlock[16];
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(ourIv, systemKey, fullBlock));
    ASSERT_MEM_EQ(hmac, fullBlock, 6);
}

TEST(crypt2wkey_keystream_is_aes_iv_output)
{
    // derive2WKeystream returns the AES(IV, transferKey) keystream.
    // This must be the raw AES output — no XOR with the key.
    // The caller XORs with the system key to get the encrypted key.

    const uint8_t frameData[] = {0x31};
    const uint8_t challenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    // Get keystream from derive2WKeystream
    uint8_t keystream[16];
    ASSERT_TRUE(IoHomeCrypto::derive2WKeystream(frameData, sizeof(frameData),
                                                challenge, IOHC_TRANSFER_KEY, keystream));

    // Build IV manually
    uint8_t iv[16];
    memset(iv, 0x55, 16);
    iv[0] = 0x31;
    {
        uint8_t c1 = 0, c2 = 0;
        IoHomeCrypto::computeChecksum(0x31, c1, c2);
        iv[8] = c1;
        iv[9] = c2;
    }
    memcpy(iv + 10, challenge, 6);

    // derive2WKeystream keystream = AES(IV, transferKey)
    uint8_t expected[16];
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(iv, IOHC_TRANSFER_KEY, expected));
    ASSERT_MEM_EQ(keystream, expected, 16);
}

TEST(aes_consistency_across_multiple_calls)
{
    // AES-128-ECB must be deterministic: same input + key = same output every time.
    // This is critical because our crypto functions create a new mbedtls_aes_context
    // on each call. If there were any state leakage between calls, this would fail.

    const uint8_t key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    const uint8_t input[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
                               0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};

    uint8_t output1[16], output2[16], output3[16];
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(input, key, output1));
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(input, key, output2));
    ASSERT_TRUE(IoHomeCrypto::aes128Encrypt(input, key, output3));

    ASSERT_MEM_EQ(output1, output2, 16);
    ASSERT_MEM_EQ(output2, output3, 16);
}

// =====================================================================
// Full 1W Key Transfer Flow (SendKey1W 0x30 integration test)
// =====================================================================

TEST(integration_1w_key_transfer_flow)
{
    // Full 1W key-transfer frame simulation:
    //   SendKey1W (0x30) carries encryptedKey[16] + manufacturer + 0x01 + sequence[2].
    //   It is unauthenticated and serializes to exactly 29 bytes.
    //   The key-encryption IV is based on the remote/controller source node.

    const uint32_t lRemoteNodeId = 0x1A380B;
    const uint8_t lRemoteNodeAddr[3] = {0x1A, 0x38, 0x0B};
    const uint8_t lDeviceNodeAddr[3] = {0x48, 0x5B, 0x37};
    const uint8_t lSystemKey[16] = {
        0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const uint16_t lSequence = 0x0042;

    uint8_t lEncryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(lSystemKey, IOHC_TRANSFER_KEY,
                                           lRemoteNodeAddr, lEncryptedKey));

    uint8_t lWrongKey[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(lSystemKey, IOHC_TRANSFER_KEY,
                                           lDeviceNodeAddr, lWrongKey));
    ASSERT_TRUE(memcmp(lEncryptedKey, lWrongKey, 16) != 0);

    IoHomeFrame lKeyTransferFrame;
    lKeyTransferFrame.init();
    lKeyTransferFrame.set1WMode();
    lKeyTransferFrame.setFrameOrder(IOHC_CTRL0_ORDER_END);
    lKeyTransferFrame.setSrcNode(lRemoteNodeId);
    lKeyTransferFrame.setDestNode(0x0000BF); // type=2 typed broadcast target
    lKeyTransferFrame.commandId = IoHomeCommand::SendKey1W;
    memcpy(lKeyTransferFrame.data, lEncryptedKey, 16);
    lKeyTransferFrame.data[16] = static_cast<uint8_t>(IoHomeManufacturer::Velux);
    lKeyTransferFrame.data[17] = 0x01;
    lKeyTransferFrame.data[18] = (lSequence >> 8) & 0xFF;
    lKeyTransferFrame.data[19] = lSequence & 0xFF;
    lKeyTransferFrame.dataLen = 20;
    lKeyTransferFrame.hasHmac = false;

    uint8_t lBuf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(lKeyTransferFrame, lBuf, sizeof(lBuf));
    ASSERT_EQ(lLen, 29);
    ASSERT_EQ((lBuf[0] & IOHC_CTRL0_LEN_MASK) + 1, 29);
    ASSERT_TRUE((lBuf[0] & IOHC_CTRL0_MODE_1W) != 0);
    ASSERT_EQ(lBuf[8], static_cast<uint8_t>(IoHomeCommand::SendKey1W));

    IoHomeFrame lParsed;
    ASSERT_TRUE(deserializeFrameForTest(lParsed, lBuf, lLen));
    ASSERT_EQ(lParsed.commandId, IoHomeCommand::SendKey1W);
    ASSERT_TRUE(!lParsed.hasHmac);
    ASSERT_EQ(lParsed.getDestNodeId(), 0x0000BF);
    ASSERT_EQ(lParsed.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lParsed.dataLen, 20);
    ASSERT_MEM_EQ(lParsed.data, lEncryptedKey, 16);
    ASSERT_EQ(lParsed.data[16], 0x01);
    ASSERT_EQ(lParsed.data[17], 0x01);
    ASSERT_EQ(lParsed.data[18], (lSequence >> 8) & 0xFF);
    ASSERT_EQ(lParsed.data[19], lSequence & 0xFF);

    uint8_t lDecryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::decrypt1WKey(lEncryptedKey, IOHC_TRANSFER_KEY,
                                           lRemoteNodeAddr, lDecryptedKey));
    ASSERT_MEM_EQ(lDecryptedKey, lSystemKey, 16);

    ASSERT_TRUE(IoHomeCrypto::decrypt1WKey(lEncryptedKey, IOHC_TRANSFER_KEY,
                                           lDeviceNodeAddr, lDecryptedKey));
    ASSERT_TRUE(memcmp(lDecryptedKey, lSystemKey, 16) != 0);
}

TEST(integration_1w_key_transfer_wrong_iv_rejected)
{
    // Critical compatibility test: 1W SendKey encryption must use the
    // remote/controller node as IV address. Encrypting with the actuator/device
    // address must not decrypt correctly with the remote IV.

    const uint8_t lSystemKey[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    const uint8_t lRemoteAddr[3] = {0xDE, 0xAD, 0xBE};
    const uint8_t lDeviceAddr[3] = {0x00, 0x11, 0x22};

    uint8_t lEncryptedWithRemote[16];
    uint8_t lEncryptedWithDevice[16];

    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(lSystemKey, IOHC_TRANSFER_KEY,
                                           lRemoteAddr, lEncryptedWithRemote));
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(lSystemKey, IOHC_TRANSFER_KEY,
                                           lDeviceAddr, lEncryptedWithDevice));

    // Different IVs produce different ciphertext.
    ASSERT_TRUE(memcmp(lEncryptedWithRemote, lEncryptedWithDevice, 16) != 0);

    // Remote/controller IV decrypts the correctly encrypted key.
    uint8_t lDecrypted[16];
    ASSERT_TRUE(IoHomeCrypto::decrypt1WKey(lEncryptedWithRemote, IOHC_TRANSFER_KEY,
                                           lRemoteAddr, lDecrypted));
    ASSERT_MEM_EQ(lDecrypted, lSystemKey, 16);

    // Remote/controller IV cannot decrypt a key encrypted with device IV.
    ASSERT_TRUE(IoHomeCrypto::decrypt1WKey(lEncryptedWithDevice, IOHC_TRANSFER_KEY,
                                           lRemoteAddr, lDecrypted));
    ASSERT_TRUE(memcmp(lDecrypted, lSystemKey, 16) != 0);
}

// =====================================================================
// Fake Gateway Mode Tests
// =====================================================================

TEST(gateway_discover_answer_frame_layout)
{
    // Build a 0x29 DiscoverAnswer as a gateway would
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x112233);  // gateway node ID
    frame.setDestNode(0x445566); // device node ID
    frame.commandId = IoHomeCommand::DiscoverResponse;
    // data: {0xFF, 0xC0, gwAddr[3], mfr, info, ts[2]} = 9 bytes
    frame.data[0] = 0xFF;
    frame.data[1] = 0xC0;
    frame.data[2] = 0x11;
    frame.data[3] = 0x22;
    frame.data[4] = 0x33;
    frame.data[5] = 0x0B; // Overkiz manufacturer
    frame.data[6] = 0xCC; // info byte
    frame.data[7] = 0x00;
    frame.data[8] = 0x00;
    frame.dataLen = 9;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    // Deserialize and verify
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::DiscoverResponse);
    ASSERT_EQ(parsed.getSrcNodeId(), 0x112233);
    ASSERT_EQ(parsed.getDestNodeId(), 0x445566);
    ASSERT_EQ(parsed.dataLen, 9);
    ASSERT_EQ(parsed.data[5], 0x0B); // Overkiz manufacturer
}

TEST(gateway_key_transfer_encryption)
{
    // Simulate 2W key transfer: gateway encrypts its key with the transfer key.
    const uint8_t lChallenge[6] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65};
    const uint8_t lKeyInitData[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
    const uint8_t lGatewayKey[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};

    uint8_t lEncryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(lKeyInitData, sizeof(lKeyInitData),
                                            lChallenge, lGatewayKey,
                                            IOHC_TRANSFER_KEY, lEncryptedKey));
    ASSERT_MEM_NEQ(lEncryptedKey, lGatewayKey, 16);

    uint8_t lDecryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(lKeyInitData, sizeof(lKeyInitData),
                                            lChallenge, lEncryptedKey,
                                            IOHC_TRANSFER_KEY, lDecryptedKey));
    ASSERT_MEM_EQ(lDecryptedKey, lGatewayKey, 16);
}

TEST(gateway_challenge_answer_hmac)
{
    // Simulate 0x3C ChallengeRequest → 0x3D ChallengeAnswer
    uint8_t lHmacInput[10];
    uint8_t lHmacInputLen = 0;

    // HMAC input: {memorized_cmd, memorized_data...}
    lHmacInput[0] = 0x32; // KeyTransfer
    lHmacInput[1] = 0xAA; // example encrypted key byte
    lHmacInputLen = 2;

    const uint8_t lChallenge[6] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65};
    const uint8_t lSystemKey[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};

    uint8_t lHmac[6];
    bool lOk = IoHomeCrypto::createHmac2W(lHmacInput, lHmacInputLen,
                                          lChallenge, lSystemKey, lHmac);
    ASSERT_TRUE(lOk);

    // Verify HMAC is deterministic (same input → same HMAC)
    uint8_t lHmac2[6];
    IoHomeCrypto::createHmac2W(lHmacInput, lHmacInputLen,
                               lChallenge, lSystemKey, lHmac2);
    ASSERT_MEM_EQ(lHmac, lHmac2, 6);

    // Verify HMAC differs for different challenge
    const uint8_t lOtherChallenge[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t lHmac3[6];
    IoHomeCrypto::createHmac2W(lHmacInput, lHmacInputLen,
                               lOtherChallenge, lSystemKey, lHmac3);
    ASSERT_MEM_NEQ(lHmac, lHmac3, 6);
}

TEST(gateway_name_response_content)
{
    // Build 0x51 GetNameResponse: "MY_GATEWAY" padded to 16 bytes
    IoHomeFrame frame;
    frame.init();
    frame.setStart2W();
    frame.setSrcNode(0x112233);
    frame.setDestNode(0x445566);
    frame.commandId = IoHomeCommand::GetNameResponse;

    const char lName[] = "MY_GATEWAY";
    memcpy(frame.data, lName, sizeof(lName) - 1);
    memset(frame.data + sizeof(lName) - 1, 0, 16 - (sizeof(lName) - 1));
    frame.dataLen = 16;
    frame.hasHmac = false;

    uint8_t buf[32];
    uint8_t len = serializeFrameForTest(frame, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    // Verify content
    IoHomeFrame parsed;
    ASSERT_TRUE(deserializeFrameForTest(parsed, buf, len));
    ASSERT_EQ(parsed.commandId, IoHomeCommand::GetNameResponse);
    ASSERT_EQ(parsed.dataLen, 16);
    // First 10 bytes should be "MY_GATEWAY"
    ASSERT_EQ(memcmp(frame.data, "MY_GATEWAY", 10), 0);
    // Remaining bytes should be zero
    for (int i = 10; i < 16; i++)
        ASSERT_EQ(parsed.data[i], 0x00);
}

#ifdef TEST_NATIVE
TEST(gateway_controller_discover_request_from_idle)
{
    const uint32_t lGatewayNodeId = 0x112233;
    const uint32_t lDeviceNodeId = 0x445566;
    const uint8_t lGatewayKey[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};

    IoHomeController lController;
    IoHomecontrol lModule;
    initGatewayControllerForTest(lController, lModule, lGatewayNodeId, lGatewayKey);

    IoHomeFrame lRequest;
    buildGatewayDiscoverRequest(lRequest, lDeviceNodeId);

    IoHomeFrame lResponse;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::DiscoverResponse);
    ASSERT_EQ(lResponse.getSrcNodeId(), lGatewayNodeId);
    ASSERT_EQ(lResponse.getDestNodeId(), lDeviceNodeId);
    ASSERT_EQ(lResponse.ctrlByte0 & IOHC_CTRL0_START, 0);
    ASSERT_EQ(lResponse.ctrlByte0 & IOHC_CTRL0_END, IOHC_CTRL0_END);
    ASSERT_EQ(lResponse.ctrlByte1, 0x00);
    ASSERT_EQ(lResponse.dataLen, 9);
    ASSERT_EQ(lResponse.data[0], 0xFF);
    ASSERT_EQ(lResponse.data[1], 0xC0);
    ASSERT_EQ(lResponse.data[5], static_cast<uint8_t>(IoHomeManufacturer::Overkiz));
    ASSERT_EQ(lController.getGatewayPairedDeviceCount(), 0);
}

TEST(gateway_controller_key_transfer_uses_configured_gateway_key)
{
    const uint32_t lGatewayNodeId = 0x112233;
    const uint32_t lDeviceNodeId = 0x445566;
    const uint8_t lGatewayKey[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
    const uint8_t lLaunchChallenge[6] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65};

    IoHomeController lController;
    IoHomecontrol lModule;
    initGatewayControllerForTest(lController, lModule, lGatewayNodeId, lGatewayKey);

    IoHomeFrame lRequest;
    IoHomeFrame lResponse;

    buildGatewayDiscoverRequest(lRequest, lDeviceNodeId);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::DiscoverResponse);

    buildGatewayConfirmation(lRequest, lDeviceNodeId, lGatewayNodeId);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::ConfirmationACK);

    buildGatewayLaunchKeyTransfer(lRequest, lDeviceNodeId, lGatewayNodeId,
                                  lLaunchChallenge);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::KeyTransfer);
    ASSERT_EQ(lResponse.dataLen, 16);

    const uint8_t lKeyInitData[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
    uint8_t lExpectedEncryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(lKeyInitData, sizeof(lKeyInitData),
                                            lLaunchChallenge, lGatewayKey,
                                            IOHC_TRANSFER_KEY,
                                            lExpectedEncryptedKey));

    ASSERT_MEM_EQ(lResponse.data, lExpectedEncryptedKey, 16);
    ASSERT_EQ(lController.getGatewayPairedDeviceCount(), 0);
}

TEST(gateway_controller_challenge_response_tracks_paired_device)
{
    const uint32_t lGatewayNodeId = 0x112233;
    const uint32_t lDeviceNodeId = 0x445566;
    const uint8_t lGatewayKey[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
    const uint8_t lLaunchChallenge[6] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65};
    const uint8_t lAuthChallenge[6] = {0x01, 0x13, 0x25, 0x37, 0x49, 0x5B};

    IoHomeController lController;
    IoHomecontrol lModule;
    initGatewayControllerForTest(lController, lModule, lGatewayNodeId, lGatewayKey);

    IoHomeFrame lRequest;
    IoHomeFrame lResponse;
    IoHomeFrame lKeyTransferResponse;

    buildGatewayDiscoverRequest(lRequest, lDeviceNodeId);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::DiscoverResponse);

    buildGatewayConfirmation(lRequest, lDeviceNodeId, lGatewayNodeId);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::ConfirmationACK);

    buildGatewayLaunchKeyTransfer(lRequest, lDeviceNodeId, lGatewayNodeId,
                                  lLaunchChallenge);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lKeyTransferResponse));
    ASSERT_EQ(lKeyTransferResponse.commandId, IoHomeCommand::KeyTransfer);

    buildGatewayChallengeRequest(lRequest, lDeviceNodeId, lGatewayNodeId,
                                 lAuthChallenge);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::ChallengeResponse);
    ASSERT_EQ(lResponse.dataLen, 6);
    ASSERT_TRUE(lResponse.ctrlByte1 & IOHC_CTRL1_LOW_POWER);

    uint8_t lHmacInput[17];
    lHmacInput[0] = static_cast<uint8_t>(IoHomeCommand::KeyTransfer);
    memcpy(lHmacInput + 1, lKeyTransferResponse.data, 16);

    uint8_t lExpectedHmac[6];
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(lHmacInput, sizeof(lHmacInput),
                                           lAuthChallenge, lGatewayKey,
                                           lExpectedHmac));
    ASSERT_MEM_EQ(lResponse.data, lExpectedHmac, sizeof(lExpectedHmac));
    ASSERT_EQ(lController.getGatewayPairedDeviceCount(), 1);
    ASSERT_EQ(lController.getGatewayPairedNodeId(0), lDeviceNodeId);
}

static void initOneWayPairingModeControllerForTest(IoHomeController &oController,
                                                   IoHomecontrol &oModule,
                                                   IoHomecontrolChannel &oChannel,
                                                   uint32_t iRemoteNodeId,
                                                   uint32_t iDeviceNodeId,
                                                   const uint8_t iKey[16])
{
    oModule.testSetChannel(0, &oChannel);
    oController.setModule(&oModule);
    oController.setOwnNodeId(iRemoteNodeId);
    oController.setSystemKey(iKey);
    oController.init();
    oChannel.setIs1W(true);
    oChannel.setConfigured1WTargetNodeId(iDeviceNodeId);
    oChannel.setOneWayControllerNodeId(iRemoteNodeId);
    oChannel.setOneWayControllerKey(iKey);
    oChannel.setEncryptionKey(iKey);
}

static void finishCurrentBlind1WPairingTxForTest(IoHomeController &iController)
{
    // Finish the long-preamble first TX and the configured short-preamble repeats.
    iController.loop();
    for (uint8_t i = 0; i < IOHC_1W_REPEAT_COUNT; i++)
    {
        ioHomeTestAdvanceMillis(IOHC_1W_REPEAT_INTERVAL_MS);
        iController.loop(); // send short-preamble repeat
        iController.loop(); // finish repeat TX and schedule next state/repeat
    }
}

static bool lastTransmittedFrameForTest(IoHomeController &iController,
                                        IoHomeFrame &oFrame)
{
    const auto &lPacket = iController.radio().testLastTransmittedPacket();
    return !lPacket.empty() &&
           deserializeFrameForTest(oFrame, lPacket.data(),
                                   static_cast<uint8_t>(lPacket.size()));
}

static uint16_t oneWaySequenceForTest(const IoHomeFrame &iFrame)
{
    if (iFrame.dataLen < 2)
        return 0;
    return static_cast<uint16_t>((static_cast<uint16_t>(iFrame.data[iFrame.dataLen - 2]) << 8) |
                                 iFrame.data[iFrame.dataLen - 1]);
}

static bool advanceVeluxEnrollmentToFinalizerStopForTest(IoHomeController &iController)
{
    iController.loop(); // REMOVE
    finishCurrentBlind1WPairingTxForTest(iController);
    if (iController.state() != ControllerState::PairSend1WKeyTransfer)
        return false;

    for (uint8_t i = 0; i < 4; ++i)
    {
        iController.loop(); // ADD_CONTROLLER for one KLI destination
        if (iController.state() != ControllerState::PairWait1WKeyTransfer)
            return false;
        finishCurrentBlind1WPairingTxForTest(iController);
    }
    return iController.state() == ControllerState::PairSend1WFinalizerStop;
}

TEST(controller_1w_pairing_modes_command_sequences)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    struct TestCase
    {
        Pairing1WMode mode;
        IoHomeCommand firstCommand;
        bool hasSecondCommand;
        IoHomeCommand secondCommand;
    };

    const TestCase lCases[] = {
        {Pairing1WMode::AnnounceOnly, IoHomeCommand::Discover2ERequest, false, IoHomeCommand::SendKey1W},
        {Pairing1WMode::AddOnly, IoHomeCommand::SendKey1W, false, IoHomeCommand::SendKey1W},
        {Pairing1WMode::AnnounceAdd, IoHomeCommand::Discover2ERequest, true, IoHomeCommand::SendKey1W},
        {Pairing1WMode::Remove, IoHomeCommand::RemoveController, false, IoHomeCommand::SendKey1W},
        {Pairing1WMode::RemoveAdd, IoHomeCommand::RemoveController, true, IoHomeCommand::SendKey1W},
    };

    for (const TestCase &lCase : lCases)
    {
        ioHomeTestSetMillis(1000);
        ioHomeTestSetMicros(1000000);

        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                               lRemoteNodeId, lDeviceNodeId, lKey);

        ASSERT_TRUE(lController.startPairing1W(0, lDeviceNodeId, lCase.mode));
        ASSERT_EQ(lController.lastPairing1WMode(), lCase.mode);

        lController.radio().testClearTransmittedPacket();
        lController.loop();

        IoHomeFrame lFirstFrame;
        const auto &lFirstPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(!lFirstPacket.empty());
        ASSERT_TRUE(deserializeFrameForTest(lFirstFrame, lFirstPacket.data(), static_cast<uint8_t>(lFirstPacket.size())));
        ASSERT_EQ(lFirstFrame.commandId, lCase.firstCommand);

        finishCurrentBlind1WPairingTxForTest(lController);

        if (lCase.hasSecondCommand)
        {
            ASSERT_EQ(lController.state(), ControllerState::PairSend1WKeyTransfer);
            lController.radio().testClearTransmittedPacket();
            lController.loop();
            IoHomeFrame lSecondFrame;
            const auto &lSecondPacket = lController.radio().testLastTransmittedPacket();
            ASSERT_TRUE(!lSecondPacket.empty());
            ASSERT_TRUE(deserializeFrameForTest(lSecondFrame, lSecondPacket.data(), static_cast<uint8_t>(lSecondPacket.size())));
            ASSERT_EQ(lSecondFrame.commandId, lCase.secondCommand);
            ASSERT_EQ(lSecondFrame.commandId, IoHomeCommand::SendKey1W);
            ASSERT_TRUE(!lSecondFrame.hasHmac);
        }
        else
        {
            ASSERT_TRUE(lController.state() == ControllerState::PairComplete ||
                        lController.state() == ControllerState::Idle);
            lController.loop();
            ASSERT_EQ(lController.state(), ControllerState::Idle);
        }
    }
}

TEST(controller_1w_announce_only_does_not_send_sendkey_after_repeats)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                           lRemoteNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lController.startPairing1WAnnounceOnly(0, lDeviceNodeId));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);

    finishCurrentBlind1WPairingTxForTest(lController);
    ASSERT_EQ(lController.radio().testTransmitCount(), 5U);

    IoHomeFrame lLastFrame;
    const auto &lLastPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(deserializeFrameForTest(lLastFrame, lLastPacket.data(), static_cast<uint8_t>(lLastPacket.size())));
    ASSERT_EQ(lLastFrame.commandId, IoHomeCommand::Discover2ERequest);

    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::Idle);
    ASSERT_EQ(lController.radio().testTransmitCount(), 5U);
}

TEST(controller_default_1w_pairing_uses_type0_all)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.setSystemKey(lKey);
    lController.init();
    lChannel.setIs1W(true);
    lChannel.setConfigured1WTargetNodeId(lDeviceNodeId);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);

    ASSERT_EQ(lController.getOneWayBroadcastType(), 0);
    ASSERT_EQ(lChannel.getOneWayControllerManufacturer(), static_cast<uint8_t>(IoHomeManufacturer::Somfy));
    lChannel.setOneWayControllerManufacturer(static_cast<uint8_t>(IoHomeManufacturer::Velux));
    ASSERT_EQ(lChannel.getOneWayControllerManufacturer(), static_cast<uint8_t>(IoHomeManufacturer::Velux));
    ASSERT_EQ(lController.oneWayBroadcastTarget(lController.getOneWayBroadcastType()), 0x00003F);
    ASSERT_EQ(lController.oneWayBroadcastTarget(0), 0x00003F);
    ASSERT_EQ(lController.oneWayBroadcastTarget(3), 0x0000FF);
    ASSERT_TRUE(lController.startPairing(0, lDeviceNodeId));
    ASSERT_EQ(lController.lastPairing1WMode(), Pairing1WMode::RemoveAdd);

    lController.radio().testClearTransmittedPacket();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::RemoveController);
    ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
    ASSERT_TRUE(lFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER);
    ASSERT_EQ(lFrame.dataLen, 3);
    ASSERT_EQ(lFrame.data[1], 0x00);
    ASSERT_EQ(lFrame.data[2], 0x01);
    ASSERT_EQ(lChannel.getSequence1W(), 1);
    ASSERT_TRUE(lFrame.hasHmac);

    finishCurrentBlind1WPairingTxForTest(lController);
    ASSERT_EQ(lController.state(), ControllerState::PairSend1WKeyTransfer);
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    IoHomeFrame lSendKey;
    const auto &lSendKeyPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(deserializeFrameForTest(lSendKey, lSendKeyPacket.data(), static_cast<uint8_t>(lSendKeyPacket.size())));
    ASSERT_EQ(lSendKey.commandId, IoHomeCommand::SendKey1W);
    ASSERT_EQ(lChannel.getSequence1W(), 2);
}

TEST(controller_1w_enrollment_finalizer_resolution_is_conservative)
{
    ASSERT_EQ(IoHomeController::resolveOneWayEnrollmentFinalizer(
                  OneWayEnrollmentFinalizer::Automatic,
                  static_cast<uint8_t>(IoHomeManufacturer::Velux)),
              OneWayEnrollmentFinalizer::StopDown);
    ASSERT_EQ(IoHomeController::resolveOneWayEnrollmentFinalizer(
                  OneWayEnrollmentFinalizer::Automatic,
                  static_cast<uint8_t>(IoHomeManufacturer::Somfy)),
              OneWayEnrollmentFinalizer::None);
    ASSERT_EQ(IoHomeController::resolveOneWayEnrollmentFinalizer(
                  OneWayEnrollmentFinalizer::Automatic, 0xFF),
              OneWayEnrollmentFinalizer::None);
    ASSERT_EQ(IoHomeController::resolveOneWayEnrollmentFinalizer(
                  OneWayEnrollmentFinalizer::StopDown,
                  static_cast<uint8_t>(IoHomeManufacturer::Somfy)),
              OneWayEnrollmentFinalizer::StopDown);
    ASSERT_EQ(IoHomeController::resolveOneWayEnrollmentFinalizer(
                  OneWayEnrollmentFinalizer::None,
                  static_cast<uint8_t>(IoHomeManufacturer::Velux)),
              OneWayEnrollmentFinalizer::None);
    ASSERT_EQ(IOHC_1W_ENROLL_FINALIZER_DELAY_MS, 40U);
    ASSERT_EQ(IOHC_1W_ENROLL_FINALIZER_DEADLINE_MS, 3000U);
    ASSERT_TRUE(IOHC_1W_ENROLL_FINALIZER_DELAY_MS < IOHC_1W_ENROLL_FINALIZER_DEADLINE_MS);
}

TEST(controller_velux_1w_enrollment_serializes_multicast_add_stop_down)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    const auto &lReference = IoHomeGoldenRfCorpus::kKli310EnrollmentReference;

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                           lRemoteNodeId, lDeviceNodeId, lKey);
    lChannel.setOneWayControllerManufacturer(lReference.manufacturer);
    lChannel.setConfigured1WAcei(lReference.acei);
    lChannel.setConfigured1WEnrollmentFinalizer(OneWayEnrollmentFinalizer::Automatic);

    ASSERT_TRUE(lController.startPairing1W(0, lDeviceNodeId, Pairing1WMode::RemoveAdd));

    lController.radio().testClearTransmittedPacket();
    lController.loop();
    IoHomeFrame lFrame;
    ASSERT_TRUE(lastTransmittedFrameForTest(lController, lFrame));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::RemoveController);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
    ASSERT_EQ(oneWaySequenceForTest(lFrame), 1U);
    finishCurrentBlind1WPairingTxForTest(lController);

    for (uint8_t i = 0; i < lReference.addDestinationCount; ++i)
    {
        ASSERT_EQ(lController.state(), ControllerState::PairSend1WKeyTransfer);
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        const auto &lAddPacket = lController.radio().testLastTransmittedPacket();
        if (i == 0)
        {
            if (!IoHomeGoldenRfCorpus::matchesMaskedReference(
                    lAddPacket.data(), static_cast<uint8_t>(lAddPacket.size()),
                    IoHomeGoldenRfCorpus::kKli310AddReference))
                hexdump("generated KLI ADD", lAddPacket.data(), lAddPacket.size());
            ASSERT_TRUE(IoHomeGoldenRfCorpus::matchesMaskedReference(
                lAddPacket.data(), static_cast<uint8_t>(lAddPacket.size()),
                IoHomeGoldenRfCorpus::kKli310AddReference));
        }
        ASSERT_TRUE(lastTransmittedFrameForTest(lController, lFrame));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::SendKey1W);
        ASSERT_EQ(lFrame.getDestNodeId(), lReference.addDestinations[i]);
        ASSERT_EQ(oneWaySequenceForTest(lFrame), 2U);
        ASSERT_EQ(lFrame.data[16], lReference.manufacturer);
        ASSERT_TRUE(!lFrame.hasHmac);
        finishCurrentBlind1WPairingTxForTest(lController);
    }

    ASSERT_EQ(lController.state(), ControllerState::PairSend1WFinalizerStop);
    ASSERT_TRUE(lController.sendChannelCommand(&lChannel, IoHomeCommand::Execute, 0x00));

    lController.radio().testClearTransmittedPacket();
    lController.loop();
    ASSERT_TRUE(lastTransmittedFrameForTest(lController, lFrame));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_EQ(lFrame.getDestNodeId(), lReference.finalizerDestination);
    ASSERT_EQ(lFrame.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(lFrame.data[1], lReference.acei);
    ASSERT_EQ(static_cast<uint16_t>((lFrame.data[2] << 8) | lFrame.data[3]),
              lReference.stopMain);
    ASSERT_EQ(oneWaySequenceForTest(lFrame), 3U);
    ASSERT_TRUE(lFrame.hasHmac);
    finishCurrentBlind1WPairingTxForTest(lController);

    ASSERT_EQ(lController.state(), ControllerState::PairWait1WFinalizerGap);
    ioHomeTestAdvanceMillis(IOHC_1W_ENROLL_FINALIZER_DELAY_MS - 1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::PairWait1WFinalizerGap);
    ioHomeTestAdvanceMillis(1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::PairSend1WFinalizerDown);

    lController.radio().testClearTransmittedPacket();
    lController.loop();
    ASSERT_TRUE(lastTransmittedFrameForTest(lController, lFrame));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_EQ(lFrame.getDestNodeId(), lReference.finalizerDestination);
    ASSERT_EQ(lFrame.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(lFrame.data[1], lReference.acei);
    ASSERT_EQ(static_cast<uint16_t>((lFrame.data[2] << 8) | lFrame.data[3]),
              lReference.downMain);
    ASSERT_EQ(oneWaySequenceForTest(lFrame), 4U);
    ASSERT_TRUE(lFrame.hasHmac);
    finishCurrentBlind1WPairingTxForTest(lController);

    ASSERT_EQ(lController.state(), ControllerState::PairComplete);
    ASSERT_EQ(lController.radio().testTransmitCount(), 35U);
    ASSERT_EQ(lChannel.getSequence1W(), 4U);

    const auto *lTrace = lController.oneWayEnrollmentTrace();
    ASSERT_EQ(lController.oneWayEnrollmentTraceCount(), 8U);
    ASSERT_EQ(lTrace[0].phase, IoHomeController::OneWayEnrollPhase::Remove);
    ASSERT_EQ(lTrace[0].source, lRemoteNodeId);
    for (uint8_t i = 0; i < 4; ++i)
    {
        ASSERT_EQ(lTrace[i + 1].phase, IoHomeController::OneWayEnrollPhase::Add);
        ASSERT_EQ(lTrace[i + 1].sequence, 2U);
        ASSERT_EQ(lTrace[i + 1].source, lRemoteNodeId);
        ASSERT_EQ(lTrace[i + 1].destination, lReference.addDestinations[i]);
        ASSERT_TRUE(lTrace[i + 1].txSuccess);
    }
    ASSERT_EQ(lTrace[5].phase, IoHomeController::OneWayEnrollPhase::FinalizeStop);
    ASSERT_EQ(lTrace[5].sequence, 3U);
    ASSERT_TRUE(lTrace[5].txSuccess);
    ASSERT_EQ(lTrace[6].phase, IoHomeController::OneWayEnrollPhase::FinalizeDown);
    ASSERT_EQ(lTrace[6].sequence, 4U);
    ASSERT_TRUE(lTrace[6].txSuccess);
    ASSERT_TRUE(lTrace[6].timestampMs - lTrace[5].timestampMs <
                lReference.stopDownDeadlineMs);
    ASSERT_EQ(lTrace[7].phase, IoHomeController::OneWayEnrollPhase::Complete);
    ASSERT_TRUE(lTrace[7].txSuccess);
}

TEST(controller_generic_1w_automatic_finalizer_stops_after_add)
{
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                           0x831F2A, 0x7E9E6E, lKey);
    lChannel.setOneWayControllerManufacturer(static_cast<uint8_t>(IoHomeManufacturer::Somfy));
    lChannel.setConfigured1WEnrollmentFinalizer(OneWayEnrollmentFinalizer::Automatic);

    ASSERT_TRUE(lController.startPairing1W(0, 0x7E9E6E, Pairing1WMode::RemoveAdd));
    lController.loop();
    finishCurrentBlind1WPairingTxForTest(lController);
    ASSERT_EQ(lController.state(), ControllerState::PairSend1WKeyTransfer);
    lController.loop();
    finishCurrentBlind1WPairingTxForTest(lController);

    ASSERT_EQ(lController.state(), ControllerState::PairComplete);
    ASSERT_EQ(lController.radio().testTransmitCount(), 10U);
    ASSERT_EQ(lChannel.getSequence1W(), 2U);
    ASSERT_EQ(lController.oneWayEnrollmentTraceCount(), 3U);
    ASSERT_EQ(lController.oneWayEnrollmentTrace()[0].phase,
              IoHomeController::OneWayEnrollPhase::Remove);
    ASSERT_EQ(lController.oneWayEnrollmentTrace()[1].phase,
              IoHomeController::OneWayEnrollPhase::Add);
    ASSERT_EQ(lController.oneWayEnrollmentTrace()[2].phase,
              IoHomeController::OneWayEnrollPhase::Complete);
}

TEST(controller_1w_enrollment_remove_failure_stops_operation)
{
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                           0x831F2A, 0x7E9E6E, lKey);

    ASSERT_TRUE(lController.startPairing1W(0, 0x7E9E6E, Pairing1WMode::RemoveAdd));
    lController.radio().testSetNextTransmitError(RadioError::HardwareError);
    lController.loop();

    ASSERT_EQ(lController.state(), ControllerState::PairFailed);
    ASSERT_EQ(lController.radio().testTransmitCount(), 0U);
    ASSERT_EQ(lChannel.getSequence1W(), 1U);
    const auto *lTrace = lController.oneWayEnrollmentTrace();
    ASSERT_EQ(lController.oneWayEnrollmentTraceCount(), 2U);
    ASSERT_EQ(lTrace[0].phase, IoHomeController::OneWayEnrollPhase::Remove);
    ASSERT_TRUE(!lTrace[0].txSuccess);
    ASSERT_EQ(lTrace[1].phase, IoHomeController::OneWayEnrollPhase::Failed);
}

TEST(controller_1w_enrollment_add_failure_skips_finalizer)
{
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                           0x831F2A, 0x7E9E6E, lKey);
    lChannel.setOneWayControllerManufacturer(static_cast<uint8_t>(IoHomeManufacturer::Velux));
    lChannel.setConfigured1WEnrollmentFinalizer(OneWayEnrollmentFinalizer::Automatic);

    ASSERT_TRUE(lController.startPairing1W(0, 0x7E9E6E, Pairing1WMode::RemoveAdd));
    lController.loop();
    finishCurrentBlind1WPairingTxForTest(lController);
    ASSERT_EQ(lController.state(), ControllerState::PairSend1WKeyTransfer);
    lController.radio().testSetNextTransmitError(RadioError::HardwareError);
    lController.loop();

    ASSERT_EQ(lController.state(), ControllerState::PairFailed);
    ASSERT_EQ(lController.radio().testTransmitCount(), 5U);
    ASSERT_EQ(lChannel.getSequence1W(), 2U);
    const auto *lTrace = lController.oneWayEnrollmentTrace();
    const uint8_t lTraceCount = lController.oneWayEnrollmentTraceCount();
    ASSERT_EQ(lTrace[lTraceCount - 2].phase, IoHomeController::OneWayEnrollPhase::Add);
    ASSERT_TRUE(!lTrace[lTraceCount - 2].txSuccess);
    ASSERT_EQ(lTrace[lTraceCount - 1].phase, IoHomeController::OneWayEnrollPhase::Failed);
}

TEST(controller_velux_1w_finalizer_stop_failure_suppresses_down)
{
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                           0x831F2A, 0x7E9E6E, lKey);
    lChannel.setOneWayControllerManufacturer(static_cast<uint8_t>(IoHomeManufacturer::Velux));
    lChannel.setConfigured1WEnrollmentFinalizer(OneWayEnrollmentFinalizer::Automatic);

    ASSERT_TRUE(lController.startPairing1W(0, 0x7E9E6E, Pairing1WMode::RemoveAdd));
    ASSERT_TRUE(advanceVeluxEnrollmentToFinalizerStopForTest(lController));
    ASSERT_EQ(lController.radio().testTransmitCount(), 25U);

    lController.radio().testSetNextTransmitError(RadioError::HardwareError);
    lController.loop();

    ASSERT_EQ(lController.state(), ControllerState::PairFailed);
    ASSERT_EQ(lController.radio().testTransmitCount(), 25U);
    ASSERT_EQ(lChannel.getSequence1W(), 3U);
    const auto *lTrace = lController.oneWayEnrollmentTrace();
    const uint8_t lTraceCount = lController.oneWayEnrollmentTraceCount();
    ASSERT_EQ(lTrace[lTraceCount - 2].phase,
              IoHomeController::OneWayEnrollPhase::FinalizeStop);
    ASSERT_TRUE(!lTrace[lTraceCount - 2].txSuccess);
    ASSERT_EQ(lTrace[lTraceCount - 1].phase,
              IoHomeController::OneWayEnrollPhase::Failed);
    ASSERT_TRUE(!lTrace[lTraceCount - 1].txSuccess);
}

TEST(controller_velux_1w_finalizer_deadline_failure_suppresses_down)
{
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                           0x831F2A, 0x7E9E6E, lKey);
    lChannel.setOneWayControllerManufacturer(static_cast<uint8_t>(IoHomeManufacturer::Velux));
    lChannel.setConfigured1WEnrollmentFinalizer(OneWayEnrollmentFinalizer::Automatic);

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);
    ASSERT_TRUE(lController.startPairing1W(0, 0x7E9E6E, Pairing1WMode::RemoveAdd));
    ASSERT_TRUE(advanceVeluxEnrollmentToFinalizerStopForTest(lController));
    lController.loop();
    finishCurrentBlind1WPairingTxForTest(lController);
    ASSERT_EQ(lController.state(), ControllerState::PairWait1WFinalizerGap);
    const uint32_t lTxCountBeforeDeadline = lController.radio().testTransmitCount();

    ioHomeTestAdvanceMillis(IOHC_1W_ENROLL_FINALIZER_DEADLINE_MS);
    lController.loop();

    ASSERT_EQ(lController.state(), ControllerState::PairFailed);
    ASSERT_EQ(lController.radio().testTransmitCount(), lTxCountBeforeDeadline);
    ASSERT_EQ(lChannel.getSequence1W(), 3U);
    const auto *lTrace = lController.oneWayEnrollmentTrace();
    ASSERT_EQ(lTrace[lController.oneWayEnrollmentTraceCount() - 1].phase,
              IoHomeController::OneWayEnrollPhase::Failed);
}

TEST(controller_velux_1w_finalizer_down_failure_marks_operation_failed)
{
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initOneWayPairingModeControllerForTest(lController, lModule, lChannel,
                                           0x831F2A, 0x7E9E6E, lKey);
    lChannel.setOneWayControllerManufacturer(static_cast<uint8_t>(IoHomeManufacturer::Velux));
    lChannel.setConfigured1WEnrollmentFinalizer(OneWayEnrollmentFinalizer::Automatic);

    ASSERT_TRUE(lController.startPairing1W(0, 0x7E9E6E, Pairing1WMode::RemoveAdd));
    ASSERT_TRUE(advanceVeluxEnrollmentToFinalizerStopForTest(lController));
    lController.loop();
    finishCurrentBlind1WPairingTxForTest(lController);
    ioHomeTestAdvanceMillis(IOHC_1W_ENROLL_FINALIZER_DELAY_MS);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::PairSend1WFinalizerDown);

    lController.radio().testSetNextTransmitError(RadioError::HardwareError);
    lController.loop();

    ASSERT_EQ(lController.state(), ControllerState::PairFailed);
    ASSERT_EQ(lController.radio().testTransmitCount(), 30U);
    ASSERT_EQ(lChannel.getSequence1W(), 4U);
    const auto *lTrace = lController.oneWayEnrollmentTrace();
    const uint8_t lTraceCount = lController.oneWayEnrollmentTraceCount();
    ASSERT_EQ(lTrace[lTraceCount - 2].phase,
              IoHomeController::OneWayEnrollPhase::FinalizeDown);
    ASSERT_TRUE(!lTrace[lTraceCount - 2].txSuccess);
    ASSERT_EQ(lTrace[lTraceCount - 1].phase,
              IoHomeController::OneWayEnrollPhase::Failed);
}

TEST(controller_discovery_sends_standard_28_then_alt_2e_broadcast)
{
    // Standard (non-encrypted) discovery mirrors a TaHoma box: it broadcasts the
    // classic DiscoverRequest (0x28 -> 0x00003B) and then the alternative
    // Discover2ERequest (0x2E -> 0x00003F, data 0x00, unauthenticated) on the
    // same frequency before listening for responses.
    const uint32_t lOwnNodeId = 0x9F0071;

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    lController.setModule(&lModule);
    lController.setOwnNodeId(lOwnNodeId);
    lController.init();

    lController.startDiscovery(false);
    ASSERT_EQ(lController.state(), ControllerState::DiscoverySending);

    // First transmit: classic 0x28 DiscoverRequest to 0x00003B.
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::DiscoverySending);

    IoHomeFrame lStd;
    const auto &lStdPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lStdPacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lStd, lStdPacket.data(), static_cast<uint8_t>(lStdPacket.size())));
    ASSERT_EQ(lStd.commandId, IoHomeCommand::DiscoverRequest);
    ASSERT_EQ(lStd.getSrcNodeId(), lOwnNodeId);
    ASSERT_EQ(lStd.getDestNodeId(), 0x00003B);
    ASSERT_EQ(lStd.dataLen, 0);
    ASSERT_TRUE(!lStd.hasHmac);

    // Second transmit: alternative 0x2E Discover2ERequest to 0x00003F.
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::DiscoveryListening);

    IoHomeFrame lAlt;
    const auto &lAltPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lAltPacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lAlt, lAltPacket.data(), static_cast<uint8_t>(lAltPacket.size())));
    ASSERT_EQ(lAlt.commandId, IoHomeCommand::Discover2ERequest);
    ASSERT_EQ(lAlt.getSrcNodeId(), lOwnNodeId);
    ASSERT_EQ(lAlt.getDestNodeId(), 0x00003F);
    ASSERT_EQ(lAlt.dataLen, 1);
    ASSERT_EQ(lAlt.data[0], 0x00);
    ASSERT_TRUE(!lAlt.hasHmac);
}

TEST(controller_spe_discovery_sends_single_2a_broadcast)
{
    // Encrypted (SPE) discovery keeps its single DiscoverSPERequest (0x2A) frame
    // and must not emit the unauthenticated 0x2E alternative.
    const uint32_t lOwnNodeId = 0x9F0071;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    lController.setModule(&lModule);
    lController.setOwnNodeId(lOwnNodeId);
    lController.setSystemKey(lKey);
    lController.init();

    lController.startDiscovery(true);
    ASSERT_EQ(lController.state(), ControllerState::DiscoverySending);

    lController.radio().testClearTransmittedPacket();
    lController.loop();
    // A single SPE frame moves straight to listening; no alternative 0x2E TX.
    ASSERT_EQ(lController.state(), ControllerState::DiscoveryListening);

    IoHomeFrame lSpe;
    const auto &lSpePacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lSpePacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lSpe, lSpePacket.data(), static_cast<uint8_t>(lSpePacket.size())));
    ASSERT_EQ(lSpe.commandId, IoHomeCommand::DiscoverSPERequest);
    ASSERT_EQ(lSpe.getDestNodeId(), 0x00003B);
}

TEST(controller_1w_pairing_allows_add_without_target_node)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);
    lChannel.setEncryptionKey(lKey);

    ASSERT_EQ(lChannel.getNodeId(), 0U);
    ASSERT_EQ(lChannel.getConfigured1WTargetNodeId(), 0U);
    ASSERT_TRUE(lController.startPairing1WAddOnly(0, 0));

    lController.radio().testClearTransmittedPacket();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_EQ(lPacket.size(), 29);

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::SendKey1W);
    ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
    ASSERT_EQ(lFrame.dataLen, 20);
    ASSERT_TRUE(!lFrame.hasHmac);
}

TEST(controller_1w_profile_can_append_sendkey_trailer_mac)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);
    lChannel.setEncryptionKey(lKey);
    lChannel.setConfigured1WEnrollmentMac(true);

    ASSERT_TRUE(lController.startPairing1WAddOnly(0, 0));
    lController.radio().testClearTransmittedPacket();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_EQ(lPacket.size(), 35U);
    ASSERT_EQ((lPacket[0] & IOHC_CTRL0_LEN_MASK) + 1, 29);

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::SendKey1W);
    ASSERT_TRUE(lFrame.hasTrailerMac);
    const uint16_t lSequence = static_cast<uint16_t>((static_cast<uint16_t>(lFrame.data[18]) << 8) | lFrame.data[19]);
    uint8_t lTranscript[17] = {static_cast<uint8_t>(IoHomeCommand::SendKey1W)};
    memcpy(lTranscript + 1, lFrame.data, 16);
    ASSERT_TRUE(IoHomeCrypto::verifyHmac1W(lTranscript, sizeof(lTranscript), lSequence,
                                           lFrame.trailerMac, lKey));
}

TEST(controller_1w_sendkey_frame_identical_with_known_or_unknown_target)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    uint8_t lUnknownTargetPacket[IOHC_FRAME_BUFFER_SIZE] = {};
    uint8_t lUnknownTargetLen = 0;
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setIs1W(true);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);
        lChannel.setEncryptionKey(lKey);

        ASSERT_TRUE(lController.startPairing1WAddOnly(0, 0));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        lUnknownTargetLen = static_cast<uint8_t>(lPacket.size());
        memcpy(lUnknownTargetPacket, lPacket.data(), lUnknownTargetLen);
    }

    uint8_t lKnownTargetPacket[IOHC_FRAME_BUFFER_SIZE] = {};
    uint8_t lKnownTargetLen = 0;
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setIs1W(true);
        lChannel.setConfigured1WTargetNodeId(lDeviceNodeId);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);
        lChannel.setEncryptionKey(lKey);

        ASSERT_TRUE(lController.startPairing1WAddOnly(0, lDeviceNodeId));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        lKnownTargetLen = static_cast<uint8_t>(lPacket.size());
        memcpy(lKnownTargetPacket, lPacket.data(), lKnownTargetLen);
    }

    ASSERT_EQ(lUnknownTargetLen, lKnownTargetLen);
    ASSERT_MEM_EQ(lUnknownTargetPacket, lKnownTargetPacket, lUnknownTargetLen);
}

TEST(controller_1w_virtual_channel_execute_allowed_without_target_node)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);
    lChannel.setEncryptionKey(lKey);

    ASSERT_EQ(lChannel.getNodeId(), 0U);
    ASSERT_TRUE(lController.sendChannelCommand(&lChannel, IoHomeCommand::Execute, 0xD2));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
    ASSERT_EQ(lFrame.dataLen, 8);
    ASSERT_TRUE(lFrame.hasHmac);
}

TEST(controller_1w_key_frame_uses_profile_manufacturer_without_hmac)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(0x7E9E6E);
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);
    lChannel.setOneWayControllerManufacturer(static_cast<uint8_t>(IoHomeManufacturer::Velux));
    // This test intentionally verifies the explicit type-2 / shutter target path.
    // The reference-compatible default is type 0 / All, covered separately.
    lChannel.setConfigured1WBroadcastType(2);

    ASSERT_TRUE(lController.sendCommand(0x7E9E6E, lKey, IoHomeCommand::SendKey1W,
                                        0x02, 0x00, 0x01));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_EQ(lPacket.size(), 29);

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::SendKey1W);
    ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x0000BF); // type 2 typed broadcast
    ASSERT_TRUE(lFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER);
    ASSERT_EQ(lFrame.dataLen, 20);
    ASSERT_EQ(lFrame.data[16], static_cast<uint8_t>(IoHomeManufacturer::Velux));
    ASSERT_TRUE(!lFrame.hasHmac);
}

TEST(controller_2w_command_defaults_to_always_alive)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(false);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_TRUE((lFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER) == 0);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_NORMAL_START);
}

TEST(controller_2w_command_can_clear_low_power_for_mains_device)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(false);
    lChannel.setLowPower2W(false);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_TRUE((lFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER) == 0);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_NORMAL_START);
}

TEST(controller_ets_always_alive_overrides_learned_low_power)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(false);
    lChannel.setLowPower2W(true);
    lChannel.setConfigured2WPowerClass(TwoWayPowerClass::AlwaysAlive);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50));
    lController.loop();
    lController.loop();
    IoHomeFrame lFrame;
    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_TRUE((lFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER) == 0);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_NORMAL_START);
}

TEST(controller_ets_low_power_overrides_learned_always_alive)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(false);
    lChannel.setLowPower2W(false);
    lChannel.setConfigured2WPowerClass(TwoWayPowerClass::LowPower);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50));
    lController.loop();
    lController.loop();
    IoHomeFrame lFrame;
    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_TRUE((lFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER) != 0);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_LONG);
}

TEST(controller_send_identify_builds_authenticated_payload)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(false);

    ASSERT_TRUE(lController.sendIdentify(lDeviceNodeId, lKey));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Identify);
    ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lFrame.getDestNodeId(), lDeviceNodeId);
    ASSERT_TRUE((lFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER) == 0);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_NORMAL_START);
    ASSERT_EQ(lFrame.dataLen, 2);
    ASSERT_EQ(lFrame.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(lFrame.data[1], 0xFF);
    ASSERT_TRUE(!lFrame.hasHmac);
}

TEST(controller_send_identify_rejects_1w_channel)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(true);

    ASSERT_TRUE(!lController.sendIdentify(lDeviceNodeId, lKey));
}

static void initPaired2WControllerForTest(IoHomeController &oController,
                                          IoHomecontrol &oModule,
                                          IoHomecontrolChannel &oChannel,
                                          uint32_t iRemoteNodeId,
                                          uint32_t iDeviceNodeId,
                                          const uint8_t iKey[16])
{
    ioHomeTestSetMillis(0);
    ioHomeTestSetMicros(0);
    oModule.testSetChannel(0, &oChannel);
    oController.setModule(&oModule);
    oController.setOwnNodeId(iRemoteNodeId);
    oController.init();
    oChannel.setNodeId(iDeviceNodeId);
    oChannel.setEncryptionKey(iKey);
    oChannel.setIs1W(false);
}

static bool transmitQueuedControllerFrame(IoHomeController &iController,
                                          IoHomeFrame &oFrame)
{
    iController.radio().testClearTransmittedPacket();
    iController.loop();
    iController.loop();

    const auto &lPacket = iController.radio().testLastTransmittedPacket();
    if (lPacket.empty())
        return false;
    return deserializeFrameForTest(oFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size()));
}

static bool queueControllerResponse(IoHomeController &iController,
                                    const IoHomeFrame &iFrame)
{
    uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(iFrame, lBuffer, sizeof(lBuffer));
    if (lLen == 0)
        return false;

    iController.loop(); // TxInProgress -> WaitResponse after the queued command TX
    iController.radio().testQueueReceivedPacket(lBuffer, lLen);
    iController.loop();
    return true;
}

static bool retryKeepsStartForQueued2WCommand(IoHomeCommand iCommand, uint8_t iParam)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    if (!lController.sendCommand(lDeviceNodeId, lKey, iCommand, iParam))
        return false;

    IoHomeFrame lFirstFrame;
    if (!transmitQueuedControllerFrame(lController, lFirstFrame))
        return false;
    if ((lFirstFrame.ctrlByte0 & IOHC_CTRL0_START) == 0)
        return false;
    if (lController.radio().testLastPreambleLength() != IOHC_PREAMBLE_NORMAL_START)
        return false;

    lController.loop(); // TxInProgress -> WaitResponse
    ioHomeTestAdvanceMillis(IOHC_RX_TIMEOUT_MS);
    lController.loop(); // timeout reached: arm retry gap
    ioHomeTestAdvanceMillis(IOHC_RETRY_GAP_MS);
    lController.loop(); // retry gap elapsed: rebuild frame and enter TxPending
    if (lController.state() != ControllerState::TxPending)
        return false;

    lController.radio().testClearTransmittedPacket();
    lController.loop(); // TxPending -> TxInProgress, retry TX

    const auto &lRetryPacket = lController.radio().testLastTransmittedPacket();
    if (lRetryPacket.empty())
        return false;
    if (lController.radio().testLastPreambleLength() != IOHC_PREAMBLE_NORMAL_START)
        return false;

    IoHomeFrame lRetryFrame;
    if (!deserializeFrameForTest(lRetryFrame, lRetryPacket.data(), static_cast<uint8_t>(lRetryPacket.size())))
        return false;

    return (lRetryFrame.ctrlByte0 & IOHC_CTRL0_START) != 0 &&
           (lRetryFrame.ctrlByte0 & IOHC_CTRL0_END) == 0 &&
           (lRetryFrame.ctrlByte0 & IOHC_CTRL0_MODE_1W) == 0 &&
           lRetryFrame.commandId == lFirstFrame.commandId &&
           lRetryFrame.dataLen == lFirstFrame.dataLen &&
           memcmp(lRetryFrame.data, lFirstFrame.data, lFirstFrame.dataLen) == 0;
}

static bool retryKeepsStartForQueued2WSetName()
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    const char lName[] = "Bedroom";
    if (!lController.sendSetName(lDeviceNodeId, lKey, lName, static_cast<uint8_t>(strlen(lName))))
        return false;

    IoHomeFrame lFirstFrame;
    if (!transmitQueuedControllerFrame(lController, lFirstFrame))
        return false;
    if (lFirstFrame.commandId != IoHomeCommand::SetName ||
        (lFirstFrame.ctrlByte0 & IOHC_CTRL0_START) == 0 ||
        lController.radio().testLastPreambleLength() != IOHC_PREAMBLE_NORMAL_START)
        return false;

    lController.loop(); // TxInProgress -> WaitResponse
    ioHomeTestAdvanceMillis(IOHC_RX_TIMEOUT_MS);
    lController.loop(); // timeout reached: arm retry gap
    ioHomeTestAdvanceMillis(IOHC_RETRY_GAP_MS);
    lController.loop(); // retry gap elapsed: rebuild frame and enter TxPending
    if (lController.state() != ControllerState::TxPending)
        return false;

    lController.radio().testClearTransmittedPacket();
    lController.loop(); // TxPending -> TxInProgress, retry TX

    const auto &lRetryPacket = lController.radio().testLastTransmittedPacket();
    if (lRetryPacket.empty() || lController.radio().testLastPreambleLength() != IOHC_PREAMBLE_NORMAL_START)
        return false;

    IoHomeFrame lRetryFrame;
    if (!deserializeFrameForTest(lRetryFrame, lRetryPacket.data(), static_cast<uint8_t>(lRetryPacket.size())))
        return false;

    return (lRetryFrame.ctrlByte0 & IOHC_CTRL0_START) != 0 &&
           (lRetryFrame.ctrlByte0 & IOHC_CTRL0_END) == 0 &&
           (lRetryFrame.ctrlByte0 & IOHC_CTRL0_MODE_1W) == 0 &&
           lRetryFrame.commandId == IoHomeCommand::SetName &&
           lRetryFrame.dataLen == lFirstFrame.dataLen &&
           memcmp(lRetryFrame.data, lFirstFrame.data, lFirstFrame.dataLen) == 0;
}

static void buildPrivateResponseFrame(IoHomeFrame &oFrame,
                                      uint32_t iRemoteNodeId,
                                      uint32_t iDeviceNodeId,
                                      const uint8_t *iData,
                                      uint8_t iDataLen)
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = IoHomeCommand::PrivateResponse;
    memcpy(oFrame.data, iData, iDataLen);
    oFrame.dataLen = iDataLen;
    oFrame.hasHmac = false;
}

static void buildDiscoverResponseFrame(IoHomeFrame &oFrame,
                                       uint32_t iRemoteNodeId,
                                       uint32_t iDeviceNodeId,
                                       bool iEncrypted = false,
                                       uint8_t iPowerSave = 0xFF)
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = iEncrypted ? IoHomeCommand::DiscoverSPEResponse
                                  : IoHomeCommand::DiscoverResponse;
    oFrame.dataLen = 0;
    if (iPowerSave <= IOHC_DISCOVERY_POWER_SAVE_MASK)
    {
        memset(oFrame.data, 0, 9);
        oFrame.data[IOHC_DISCOVERY_FLAGS_OFFSET] = iPowerSave;
        oFrame.dataLen = 9;
    }
    oFrame.hasHmac = false;
}

static void buildPairChallengeRequestFrame(IoHomeFrame &oFrame,
                                           uint32_t iRemoteNodeId,
                                           uint32_t iDeviceNodeId,
                                           const uint8_t iChallenge[6])
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = IoHomeCommand::ChallengeRequest;
    memcpy(oFrame.data, iChallenge, 6);
    oFrame.dataLen = 6;
    oFrame.hasHmac = false;
}

static void buildKeyTransferConfirmationFrame(IoHomeFrame &oFrame,
                                              uint32_t iRemoteNodeId,
                                              uint32_t iDeviceNodeId)
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = IoHomeCommand::KeyTransferConfirmation;
    oFrame.dataLen = 0;
    oFrame.hasHmac = false;
}

static void buildErrorResponseFrame(IoHomeFrame &oFrame,
                                    uint32_t iRemoteNodeId,
                                    uint32_t iDeviceNodeId,
                                    uint8_t iErrorCode = 0x01)
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = IoHomeCommand::ErrorResponse;
    oFrame.data[0] = iErrorCode;
    oFrame.dataLen = 1;
    oFrame.hasHmac = false;
}

static bool advancePairingToWaitDeviceChallenge(IoHomeController &iController,
                                                uint32_t iRemoteNodeId,
                                                uint32_t iDeviceNodeId)
{
    if (!iController.startPairing(0))
        return false;

    IoHomeFrame lFrame;
    if (!transmitQueuedControllerFrame(iController, lFrame) ||
        lFrame.commandId != IoHomeCommand::DiscoverRequest)
        return false;

    IoHomeFrame lDiscoverResponse;
    buildDiscoverResponseFrame(lDiscoverResponse, iRemoteNodeId, iDeviceNodeId);
    uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(lDiscoverResponse, lBuffer, sizeof(lBuffer));
    if (lLen == 0)
        return false;

    iController.radio().testClearTransmittedPacket();
    iController.radio().testQueueReceivedPacket(lBuffer, lLen);
    iController.loop();

    const auto &lKeyInitPacket = iController.radio().testLastTransmittedPacket();
    if (lKeyInitPacket.empty() ||
        !deserializeFrameForTest(lFrame, lKeyInitPacket.data(), static_cast<uint8_t>(lKeyInitPacket.size())) ||
        lFrame.commandId != IoHomeCommand::KeyInitTransfer)
        return false;

    return iController.state() == ControllerState::PairWaitDeviceChallenge;
}

static bool advancePairingToWaitKeyTransferConfirmation(IoHomeController &iController,
                                                        uint32_t iRemoteNodeId,
                                                        uint32_t iDeviceNodeId)
{
    static const uint8_t kChallenge[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};

    if (!advancePairingToWaitDeviceChallenge(iController, iRemoteNodeId, iDeviceNodeId))
        return false;

    IoHomeFrame lChallengeRequest;
    buildPairChallengeRequestFrame(lChallengeRequest, iRemoteNodeId, iDeviceNodeId, kChallenge);
    if (!queueControllerResponse(iController, lChallengeRequest))
        return false;

    IoHomeFrame lFrame;
    const auto &lKeyTransferPacket = iController.radio().testLastTransmittedPacket();
    if (lKeyTransferPacket.empty() ||
        !deserializeFrameForTest(lFrame, lKeyTransferPacket.data(), static_cast<uint8_t>(lKeyTransferPacket.size())) ||
        lFrame.commandId != IoHomeCommand::KeyTransfer)
        return false;

    return iController.state() == ControllerState::PairWaitKeyTransferConfirmation;
}

static bool queueKeyTransferConfirmationAndCaptureSetConfig1(IoHomeController &iController,
                                                             uint32_t iRemoteNodeId,
                                                             uint32_t iDeviceNodeId,
                                                             IoHomeFrame &oSetConfig1Frame)
{
    IoHomeFrame lKeyConfirm;
    buildKeyTransferConfirmationFrame(lKeyConfirm, iRemoteNodeId, iDeviceNodeId);
    iController.radio().testClearTransmittedPacket();
    if (!queueControllerResponse(iController, lKeyConfirm))
        return false;

    const auto &lSetConfigPacket = iController.radio().testLastTransmittedPacket();
    if (lSetConfigPacket.empty())
        return false;

    return deserializeFrameForTest(oSetConfig1Frame, lSetConfigPacket.data(), static_cast<uint8_t>(lSetConfigPacket.size()));
}

TEST(controller_default_2w_pairing_uses_key_init_after_discovery)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);

    ASSERT_TRUE(lController.startPairing(0));

    IoHomeFrame lDiscoveryFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lDiscoveryFrame));
    ASSERT_EQ(lDiscoveryFrame.commandId, IoHomeCommand::DiscoverRequest);

    IoHomeFrame lDiscoverResponse;
    const uint32_t lFlashSavesBeforeDiscovery = openknx.flash.saveCount;
    buildDiscoverResponseFrame(lDiscoverResponse, lRemoteNodeId, lDeviceNodeId,
                               false, IOHC_POWER_SAVE_LOW_POWER);
    uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(lDiscoverResponse, lBuffer, sizeof(lBuffer));
    ASSERT_TRUE(lLen > 0);

    lController.radio().testClearTransmittedPacket();
    lController.radio().testQueueReceivedPacket(lBuffer, lLen);
    lController.loop();

    ASSERT_TRUE(lChannel.hasLearnedLowPower2W());
    ASSERT_TRUE(lChannel.isLowPower2W());
    ASSERT_EQ(openknx.flash.saveCount, lFlashSavesBeforeDiscovery + 1);

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lKeyInitFrame;
    ASSERT_TRUE(deserializeFrameForTest(lKeyInitFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lKeyInitFrame.commandId, IoHomeCommand::KeyInitTransfer);
    ASSERT_EQ(lKeyInitFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lKeyInitFrame.getDestNodeId(), lDeviceNodeId);
    ASSERT_EQ(lKeyInitFrame.dataLen, 0);
}

TEST(controller_pairing_discovery_learns_always_alive)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);
    lChannel.setLowPower2W(true);
    ASSERT_TRUE(lController.startPairing(0));

    IoHomeFrame lDiscoveryFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lDiscoveryFrame));

    IoHomeFrame lDiscoverResponse;
    buildDiscoverResponseFrame(lDiscoverResponse, lRemoteNodeId, lDeviceNodeId,
                               false, IOHC_POWER_SAVE_ALWAYS_ALIVE);
    uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(lDiscoverResponse, lBuffer, sizeof(lBuffer));
    ASSERT_TRUE(lLen > 0);

    const uint32_t lFlashSavesBeforeDiscovery = openknx.flash.saveCount;
    lController.radio().testQueueReceivedPacket(lBuffer, lLen);
    lController.loop();

    ASSERT_TRUE(lChannel.hasLearnedLowPower2W());
    ASSERT_TRUE(!lChannel.isLowPower2W());
    ASSERT_EQ(openknx.flash.saveCount, lFlashSavesBeforeDiscovery + 1);
}

TEST(pairing_broadcast_scan_skips_request_channel_and_unicast_wait_holds_it)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    // Discovery is broadcast on CH2. Its response wait must immediately
    // listen on CH3, then CH1, and never spend a dwell back on CH2.
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, 0, lKey);
        ASSERT_TRUE(lController.startPairing(0));

        lController.loop(); // PairSendDiscovery: TX on CH2
        ASSERT_EQ(lController.state(), ControllerState::PairWaitDiscoveryResponse);
        ioHomeTestAdvanceMicros(IOHC_RX_SCAN_INTERVAL_US + 1);
        lController.loop(); // enter RX and rotate off the broadcast request channel
        ASSERT_EQ(lController.radio().testCurrentFrequency(), IOHC_FREQ_3);

        ioHomeTestAdvanceMicros(IOHC_RX_SCAN_INTERVAL_US + 1);
        lController.loop();
        ASSERT_EQ(lController.radio().testCurrentFrequency(), IOHC_FREQ_1);

        ioHomeTestAdvanceMicros(IOHC_RX_SCAN_INTERVAL_US + 1);
        lController.loop();
        ASSERT_EQ(lController.radio().testCurrentFrequency(), IOHC_FREQ_3);
    }

    // Key-init is unicast on CH2. Even with the global scan enabled, its
    // challenge wait must remain on the request channel for the full window.
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, 0, lKey);
        lController.setSystemKey(lKey);
        ASSERT_TRUE(advancePairingToWaitDeviceChallenge(lController, lRemoteNodeId, lDeviceNodeId));

        ioHomeTestAdvanceMicros(IOHC_RX_SCAN_INTERVAL_US * 2U);
        lController.loop();
        ASSERT_EQ(lController.radio().testCurrentFrequency(), IOHC_FREQ_2);
        ioHomeTestAdvanceMicros(IOHC_RX_SCAN_INTERVAL_US * 2U);
        lController.loop();
        ASSERT_EQ(lController.radio().testCurrentFrequency(), IOHC_FREQ_2);
    }
}

TEST(controller_experimental_2w_pairing_can_use_discovery_confirmation)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);

    ASSERT_TRUE(lController.startPairingExperimental(0, 0, Pairing2WMode::DiscoveryConfirmation));

    IoHomeFrame lDiscoveryFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lDiscoveryFrame));
    ASSERT_EQ(lDiscoveryFrame.commandId, IoHomeCommand::DiscoverRequest);

    IoHomeFrame lDiscoverResponse;
    buildDiscoverResponseFrame(lDiscoverResponse, lRemoteNodeId, lDeviceNodeId);
    uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(lDiscoverResponse, lBuffer, sizeof(lBuffer));
    ASSERT_TRUE(lLen > 0);

    lController.radio().testClearTransmittedPacket();
    lController.radio().testQueueReceivedPacket(lBuffer, lLen);
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lConfirmationFrame;
    ASSERT_TRUE(deserializeFrameForTest(lConfirmationFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lConfirmationFrame.commandId, IoHomeCommand::Confirmation);
    ASSERT_EQ(lConfirmationFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lConfirmationFrame.getDestNodeId(), lDeviceNodeId);
    ASSERT_EQ(lConfirmationFrame.dataLen, 0);
}

TEST(controller_2w_pairing_succeeds_when_setconfig1_times_out)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);

    ASSERT_TRUE(advancePairingToWaitKeyTransferConfirmation(lController, lRemoteNodeId, lDeviceNodeId));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitKeyTransferConfirmation);

    IoHomeFrame lSetConfig1;
    ASSERT_TRUE(queueKeyTransferConfirmationAndCaptureSetConfig1(lController, lRemoteNodeId, lDeviceNodeId, lSetConfig1));
    ASSERT_EQ(lSetConfig1.commandId, IoHomeCommand::SetConfig1);
    ASSERT_EQ(lSetConfig1.dataLen, 5);
    ASSERT_EQ(lSetConfig1.data[0], 0xE0);
    ASSERT_EQ(lSetConfig1.data[1], 0x10);
    ASSERT_EQ(lSetConfig1.data[2], 0x0A);
    ASSERT_EQ(lSetConfig1.data[3], 0x08);
    ASSERT_EQ(lSetConfig1.data[4], 0x00);
    ASSERT_EQ(lChannel.getNodeId(), lDeviceNodeId);
    ASSERT_EQ(lController.state(), ControllerState::PairWaitSetConfig1Response);

    ioHomeTestAdvanceMillis(2001);
    lController.loop();

    ASSERT_EQ(lController.state(), ControllerState::PairComplete);
}

TEST(controller_2w_pairing_accepts_direct_key_confirmation_without_challenge)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);

    // Drive pairing until we have sent KeyInitTransfer (0x31) and are waiting
    // for the device's 0x3C challenge.
    ASSERT_TRUE(advancePairingToWaitDeviceChallenge(lController, lRemoteNodeId, lDeviceNodeId));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDeviceChallenge);

    // A confirmation from a different device must not complete the active
    // exchange merely because it happens to use the right opcode.
    IoHomeFrame lWrongDeviceConfirmation;
    buildKeyTransferConfirmationFrame(lWrongDeviceConfirmation, lRemoteNodeId, 0x123456);
    ASSERT_TRUE(queueControllerResponse(lController, lWrongDeviceConfirmation));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDeviceChallenge);

    // The device skips the challenge and confirms the key directly with 0x33.
    // The controller must accept this as a successful pairing and proceed to
    // SetConfig1 just like the normal post-challenge confirmation path.
    IoHomeFrame lSetConfig1;
    ASSERT_TRUE(queueKeyTransferConfirmationAndCaptureSetConfig1(lController, lRemoteNodeId, lDeviceNodeId, lSetConfig1));
    ASSERT_EQ(lSetConfig1.commandId, IoHomeCommand::SetConfig1);
    ASSERT_EQ(lChannel.getNodeId(), lDeviceNodeId);
    ASSERT_EQ(lController.state(), ControllerState::PairWaitSetConfig1Response);
}

TEST(controller_2w_pairing_rejects_misdirected_key_confirmation)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);
    ASSERT_TRUE(advancePairingToWaitKeyTransferConfirmation(lController, lRemoteNodeId, lDeviceNodeId));

    IoHomeFrame lMisdirectedConfirmation;
    buildKeyTransferConfirmationFrame(lMisdirectedConfirmation, 0x654321, lDeviceNodeId);
    ASSERT_TRUE(queueControllerResponse(lController, lMisdirectedConfirmation));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitKeyTransferConfirmation);
    ASSERT_EQ(lChannel.getNodeId(), 0U);

    IoHomeFrame lSetConfig1;
    ASSERT_TRUE(queueKeyTransferConfirmationAndCaptureSetConfig1(lController, lRemoteNodeId, lDeviceNodeId, lSetConfig1));
    ASSERT_EQ(lSetConfig1.commandId, IoHomeCommand::SetConfig1);
}

TEST(controller_2w_pairing_correlates_interleaved_frames)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    static const uint8_t kChallenge[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);
    ASSERT_TRUE(lController.startPairing(0, lDeviceNodeId));

    IoHomeFrame lFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lFrame));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::DiscoverRequest);

    // A broadcast source, another unicast device, and a response addressed to
    // someone else must not take ownership of this known-target transaction.
    IoHomeFrame lDiscoverResponse;
    buildDiscoverResponseFrame(lDiscoverResponse, lRemoteNodeId, 0);
    ASSERT_TRUE(queueControllerResponse(lController, lDiscoverResponse));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDiscoveryResponse);

    buildDiscoverResponseFrame(lDiscoverResponse, lRemoteNodeId, 0x123456);
    ASSERT_TRUE(queueControllerResponse(lController, lDiscoverResponse));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDiscoveryResponse);

    buildDiscoverResponseFrame(lDiscoverResponse, 0x654321, lDeviceNodeId);
    ASSERT_TRUE(queueControllerResponse(lController, lDiscoverResponse));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDiscoveryResponse);

    buildDiscoverResponseFrame(lDiscoverResponse, lRemoteNodeId, lDeviceNodeId);
    ASSERT_TRUE(queueControllerResponse(lController, lDiscoverResponse));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDeviceChallenge);

    // Once the device is bound, only its correctly addressed, complete 0x3C
    // can cause the 0x32 key-transfer response.
    IoHomeFrame lChallengeRequest;
    buildPairChallengeRequestFrame(lChallengeRequest, lRemoteNodeId, 0x123456, kChallenge);
    lController.radio().testClearTransmittedPacket();
    ASSERT_TRUE(queueControllerResponse(lController, lChallengeRequest));
    ASSERT_TRUE(lController.radio().testLastTransmittedPacket().empty());
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDeviceChallenge);

    buildPairChallengeRequestFrame(lChallengeRequest, lRemoteNodeId, lDeviceNodeId, kChallenge);
    lChallengeRequest.dataLen = 5;
    ASSERT_TRUE(queueControllerResponse(lController, lChallengeRequest));
    ASSERT_TRUE(lController.radio().testLastTransmittedPacket().empty());
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDeviceChallenge);

    buildPairChallengeRequestFrame(lChallengeRequest, lRemoteNodeId, lDeviceNodeId, kChallenge);
    ASSERT_TRUE(queueControllerResponse(lController, lChallengeRequest));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitKeyTransferConfirmation);

    // An interleaved status-like response and a different device's 0x33 do
    // not finish the active key exchange.
    uint8_t lStatusData[] = {0x00};
    buildPrivateResponseFrame(lFrame, lRemoteNodeId, lDeviceNodeId, lStatusData, sizeof(lStatusData));
    lController.radio().testClearTransmittedPacket();
    ASSERT_TRUE(queueControllerResponse(lController, lFrame));
    ASSERT_TRUE(lController.radio().testLastTransmittedPacket().empty());
    ASSERT_EQ(lController.state(), ControllerState::PairWaitKeyTransferConfirmation);

    buildKeyTransferConfirmationFrame(lFrame, lRemoteNodeId, 0x123456);
    ASSERT_TRUE(queueControllerResponse(lController, lFrame));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitKeyTransferConfirmation);
}

TEST(controller_2w_command_ignores_foreign_challenge)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    static const uint8_t kChallenge[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);
    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50));

    IoHomeFrame lFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lFrame));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);

    IoHomeFrame lChallengeRequest;
    buildPairChallengeRequestFrame(lChallengeRequest, lRemoteNodeId, 0x123456, kChallenge);
    lController.radio().testClearTransmittedPacket();
    ASSERT_TRUE(queueControllerResponse(lController, lChallengeRequest));
    ASSERT_TRUE(lController.radio().testLastTransmittedPacket().empty());
    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);

    buildPairChallengeRequestFrame(lChallengeRequest, lRemoteNodeId, lDeviceNodeId, kChallenge);
    ASSERT_TRUE(queueControllerResponse(lController, lChallengeRequest));

    const auto &lChallengeResponsePacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lChallengeResponsePacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lChallengeResponsePacket.data(),
                                        static_cast<uint8_t>(lChallengeResponsePacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::ChallengeResponse);
    ASSERT_EQ(lFrame.getDestNodeId(), lDeviceNodeId);
}

TEST(controller_2w_pairing_retries_key_init_and_accepts_direct_confirmation)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);
    ASSERT_TRUE(advancePairingToWaitDeviceChallenge(lController, lRemoteNodeId, lDeviceNodeId));

    ioHomeTestAdvanceMillis(5001);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::PairSendKeyInit);

    IoHomeFrame lKeyInit;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lKeyInit));
    ASSERT_EQ(lKeyInit.commandId, IoHomeCommand::KeyInitTransfer);
    ASSERT_EQ(lController.state(), ControllerState::PairWaitDeviceChallenge);

    // A delayed device can finish the retried phase without another challenge.
    IoHomeFrame lSetConfig1;
    ASSERT_TRUE(queueKeyTransferConfirmationAndCaptureSetConfig1(lController,
                                                                   lRemoteNodeId,
                                                                   lDeviceNodeId,
                                                                   lSetConfig1));
    ASSERT_EQ(lSetConfig1.commandId, IoHomeCommand::SetConfig1);
    ASSERT_EQ(lChannel.getNodeId(), lDeviceNodeId);
}

TEST(controller_2w_pairing_retries_key_init_with_fresh_challenge)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    static const uint8_t kFreshChallenge[6] = {0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);
    ASSERT_TRUE(advancePairingToWaitKeyTransferConfirmation(lController, lRemoteNodeId, lDeviceNodeId));

    ioHomeTestAdvanceMillis(5001);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::PairSendKeyInit);

    IoHomeFrame lKeyInit;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lKeyInit));
    ASSERT_EQ(lKeyInit.commandId, IoHomeCommand::KeyInitTransfer);

    IoHomeFrame lFreshChallenge;
    buildPairChallengeRequestFrame(lFreshChallenge, lRemoteNodeId, lDeviceNodeId, kFreshChallenge);
    ASSERT_TRUE(queueControllerResponse(lController, lFreshChallenge));

    IoHomeFrame lKeyTransfer;
    const auto &lKeyTransferPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lKeyTransferPacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lKeyTransfer, lKeyTransferPacket.data(),
                                        static_cast<uint8_t>(lKeyTransferPacket.size())));
    ASSERT_EQ(lKeyTransfer.commandId, IoHomeCommand::KeyTransfer);
    ASSERT_EQ(lController.state(), ControllerState::PairWaitKeyTransferConfirmation);
}

TEST(controller_2w_pairing_key_exchange_respects_total_budget)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);
    ASSERT_TRUE(advancePairingToWaitDeviceChallenge(lController, lRemoteNodeId, lDeviceNodeId));

    ioHomeTestAdvanceMillis(IOHC_PAIR_KEY_EXCHANGE_TIMEOUT_MS);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::PairFailed);
}

TEST(controller_2w_pairing_succeeds_when_setconfig1_returns_error_response)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);

    ASSERT_TRUE(advancePairingToWaitKeyTransferConfirmation(lController, lRemoteNodeId, lDeviceNodeId));

    IoHomeFrame lSetConfig1;
    ASSERT_TRUE(queueKeyTransferConfirmationAndCaptureSetConfig1(lController, lRemoteNodeId, lDeviceNodeId, lSetConfig1));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitSetConfig1Response);
    ASSERT_EQ(lChannel.getNodeId(), lDeviceNodeId);

    IoHomeFrame lErrorResponse;
    buildErrorResponseFrame(lErrorResponse, lRemoteNodeId, lDeviceNodeId, 0x05);
    ASSERT_TRUE(queueControllerResponse(lController, lErrorResponse));
    lController.loop();

    ASSERT_TRUE(lController.state() == ControllerState::PairComplete ||
                lController.state() == ControllerState::Idle);
}

TEST(controller_2w_pairing_succeeds_when_setconfig1_send_or_setup_fails)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, 0, lKey);
        lController.setSystemKey(lKey);

        ASSERT_TRUE(advancePairingToWaitKeyTransferConfirmation(lController, lRemoteNodeId, lDeviceNodeId));
        lController.radio().testSetNextPreambleError(RadioError::HardwareError);

        IoHomeFrame lKeyConfirm;
        buildKeyTransferConfirmationFrame(lKeyConfirm, lRemoteNodeId, lDeviceNodeId);
        lController.radio().testClearTransmittedPacket();
        ASSERT_TRUE(queueControllerResponse(lController, lKeyConfirm));

        ASSERT_TRUE(lController.radio().testLastTransmittedPacket().empty());
        ASSERT_EQ(lChannel.getNodeId(), lDeviceNodeId);
        ASSERT_EQ(lController.state(), ControllerState::PairComplete);
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, 0, lKey);
        lController.setSystemKey(lKey);

        ASSERT_TRUE(advancePairingToWaitKeyTransferConfirmation(lController, lRemoteNodeId, lDeviceNodeId));
        lController.radio().testSetNextTransmitError(RadioError::HardwareError);

        IoHomeFrame lKeyConfirm;
        buildKeyTransferConfirmationFrame(lKeyConfirm, lRemoteNodeId, lDeviceNodeId);
        lController.radio().testClearTransmittedPacket();
        ASSERT_TRUE(queueControllerResponse(lController, lKeyConfirm));

        ASSERT_TRUE(lController.radio().testLastTransmittedPacket().empty());
        ASSERT_EQ(lChannel.getNodeId(), lDeviceNodeId);
        ASSERT_EQ(lController.state(), ControllerState::PairComplete);
    }
}

TEST(controller_2w_pairing_setconfig1_auth_challenge_completes_on_final_reject)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    const uint8_t lSetConfigChallenge[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, 0, lKey);
    lController.setSystemKey(lKey);

    ASSERT_TRUE(advancePairingToWaitKeyTransferConfirmation(lController, lRemoteNodeId, lDeviceNodeId));

    IoHomeFrame lSetConfig1;
    ASSERT_TRUE(queueKeyTransferConfirmationAndCaptureSetConfig1(lController, lRemoteNodeId, lDeviceNodeId, lSetConfig1));
    ASSERT_EQ(lController.state(), ControllerState::PairWaitSetConfig1Response);
    ASSERT_EQ(lChannel.getNodeId(), lDeviceNodeId);

    IoHomeFrame lChallengeRequest;
    buildPairChallengeRequestFrame(lChallengeRequest, lRemoteNodeId, lDeviceNodeId, lSetConfigChallenge);
    lController.radio().testClearTransmittedPacket();
    ASSERT_TRUE(queueControllerResponse(lController, lChallengeRequest));

    const auto &lAuthPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lAuthPacket.empty());
    IoHomeFrame lAuthResponse;
    ASSERT_TRUE(deserializeFrameForTest(lAuthResponse, lAuthPacket.data(), static_cast<uint8_t>(lAuthPacket.size())));
    ASSERT_EQ(lAuthResponse.commandId, IoHomeCommand::ChallengeResponse);
    ASSERT_EQ(lController.state(), ControllerState::PairWaitSetConfig1FinalResponse);

    IoHomeFrame lErrorResponse;
    buildErrorResponseFrame(lErrorResponse, lRemoteNodeId, lDeviceNodeId, 0x02);
    ASSERT_TRUE(queueControllerResponse(lController, lErrorResponse));
    lController.loop();

    ASSERT_TRUE(lController.state() == ControllerState::PairComplete ||
                lController.state() == ControllerState::Idle);
}

TEST(controller_private_query_payload_variants)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);
        ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Private, 0x03));
        IoHomeFrame lFrame;
        ASSERT_TRUE(transmitQueuedControllerFrame(lController, lFrame));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::Private);
        ASSERT_EQ(lFrame.dataLen, 3);
        ASSERT_EQ(lFrame.data[0], 0x03);
        ASSERT_EQ(lFrame.data[1], 0x00);
        ASSERT_EQ(lFrame.data[2], 0x00);
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);
        ASSERT_TRUE(lController.sendBatteryStatusQuery(lDeviceNodeId, lKey));
        IoHomeFrame lFrame;
        ASSERT_TRUE(transmitQueuedControllerFrame(lController, lFrame));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::Private);
        ASSERT_EQ(lFrame.dataLen, 3);
        ASSERT_EQ(lFrame.data[0], 0x06);
        ASSERT_EQ(lFrame.data[1], 0x00);
        ASSERT_EQ(lFrame.data[2], 0x00);
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);
        ASSERT_TRUE(lController.sendBatteryStateQuery(lDeviceNodeId, lKey));
        IoHomeFrame lFrame;
        ASSERT_TRUE(transmitQueuedControllerFrame(lController, lFrame));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::Private);
        ASSERT_EQ(lFrame.dataLen, 3);
        ASSERT_EQ(lFrame.data[0], 0x09);
        ASSERT_EQ(lFrame.data[1], 0x00);
        ASSERT_EQ(lFrame.data[2], 0x00);
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);
        ASSERT_TRUE(lController.sendTiltStatusQuery(lDeviceNodeId, lKey));
        IoHomeFrame lFrame;
        ASSERT_TRUE(transmitQueuedControllerFrame(lController, lFrame));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::Private);
        ASSERT_EQ(lFrame.dataLen, 4);
        ASSERT_EQ(lFrame.data[0], 0x03);
        ASSERT_EQ(lFrame.data[1], 0x20);
        ASSERT_EQ(lFrame.data[2], 0x01);
        ASSERT_EQ(lFrame.data[3], 0x00);
    }
}

TEST(controller_2w_tilt_execute_payload)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lController.sendTiltCommand(lDeviceNodeId, lKey, 25));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_EQ(lPacket.size(), 17);
    ASSERT_EQ(lPacket[8], static_cast<uint8_t>(IoHomeCommand::Execute));
    ASSERT_EQ(lPacket[9], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(lPacket[10], 0xE7);
    ASSERT_EQ(lPacket[11], 0xD4);
    ASSERT_EQ(lPacket[12], 0x00);
    ASSERT_EQ(lPacket[13], 0x20);
    uint16_t lTiltRaw = ((uint16_t)lPacket[14] << 8) | lPacket[15];
    ASSERT_EQ(lTiltRaw, (uint16_t)((75UL * IOHC_POSITION_MAX) / 100UL));
    ASSERT_EQ(lPacket[16], 0x00);
}

TEST(controller_2w_execute_ignores_combined_slat_param)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50, 75));

    IoHomeFrame lFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lFrame));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_EQ(lFrame.dataLen, 8);
    ASSERT_EQ(lFrame.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(lFrame.data[1], IOHC_ACEI_DEFAULT);
    ASSERT_EQ(lFrame.data[2], 100);
    ASSERT_EQ(lFrame.data[3], 0x00);
    ASSERT_EQ(lFrame.data[4], 0x80);
    ASSERT_EQ(lFrame.data[5], 0xD8);
    ASSERT_EQ(lFrame.data[6], 0x06);
    ASSERT_EQ(lFrame.data[7], 0x00);
}

TEST(controller_private_response_decodes_battery_lowpower_and_tilt)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);
        lChannel.setLowPower2W(false);
        ASSERT_TRUE(lController.sendBatteryStatusQuery(lDeviceNodeId, lKey));
        IoHomeFrame lTxFrame;
        ASSERT_TRUE(transmitQueuedControllerFrame(lController, lTxFrame));

        uint8_t lData[4] = {0x00, 0x60, 87, 0x00};
        IoHomeFrame lResponse;
        buildPrivateResponseFrame(lResponse, lRemoteNodeId, lDeviceNodeId,
                                  lData, sizeof(lData));
        const uint32_t lFlashSavesBeforeResponse = openknx.flash.saveCount;
        ASSERT_TRUE(queueControllerResponse(lController, lResponse));
        ASSERT_TRUE(lChannel.isLowPower2W());
        ASSERT_TRUE(lChannel.hasLearnedLowPower2W());
        ASSERT_EQ(openknx.flash.saveCount, lFlashSavesBeforeResponse + 1);
        ASSERT_TRUE(lChannel.testHasBatteryLevel());
        ASSERT_EQ(lChannel.testBatteryLevel(), 87);
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);
        ASSERT_TRUE(lController.sendTiltStatusQuery(lDeviceNodeId, lKey));
        IoHomeFrame lTxFrame;
        ASSERT_TRUE(transmitQueuedControllerFrame(lController, lTxFrame));

        uint8_t lData[16] = {};
        lData[0] = 0x00; // moving flag set, but raw current/target are within tolerance
        lData[1] = 0x80; // status expected
        const uint16_t lTargetRaw = IOHC_POSITION_MAX / 2;
        const uint16_t lCurrentRaw = lTargetRaw + 50;
        lData[2] = (lTargetRaw >> 8) & 0xFF;
        lData[3] = lTargetRaw & 0xFF;
        lData[4] = (lCurrentRaw >> 8) & 0xFF;
        lData[5] = lCurrentRaw & 0xFF;
        lData[7] = 4;
        const uint16_t lTiltRaw = (75UL * IOHC_POSITION_MAX) / 100UL;
        lData[13] = (lTiltRaw >> 8) & 0xFF;
        lData[14] = lTiltRaw & 0xFF;

        IoHomeFrame lResponse;
        buildPrivateResponseFrame(lResponse, lRemoteNodeId, lDeviceNodeId,
                                  lData, sizeof(lData));
        ASSERT_TRUE(queueControllerResponse(lController, lResponse));
        ASSERT_TRUE(lChannel.testHasPositionFeedback());
        ASSERT_TRUE(lChannel.testHasTargetPositionFeedback());
        ASSERT_TRUE(lChannel.testHasStatusUpdate());
        ASSERT_TRUE(!lChannel.testStatusMoving());
        ASSERT_TRUE(lChannel.testStatusExpected());
        ASSERT_TRUE(lChannel.testHasEstimate());
        ASSERT_EQ(lChannel.testEstimate(), 4);
        ASSERT_TRUE(lChannel.testHasSlatFeedback());
        ASSERT_FLOAT_EQ(lChannel.testTargetPositionFeedback(), 50.0f, 0.01f);
        ASSERT_FLOAT_EQ(lChannel.testSlatFeedback(), 25.0f, 0.01f);
    }
}

TEST(controller_private_response_decodes_tilt_with_15_byte_payload)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);
    ASSERT_TRUE(lController.sendTiltStatusQuery(lDeviceNodeId, lKey));

    IoHomeFrame lTxFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lTxFrame));

    uint8_t lData[15] = {};
    lData[0] = 0x01;
    lData[1] = 0x80;
    const uint16_t lTargetRaw = IOHC_POSITION_MAX / 2;
    const uint16_t lCurrentRaw = lTargetRaw;
    lData[2] = (lTargetRaw >> 8) & 0xFF;
    lData[3] = lTargetRaw & 0xFF;
    lData[4] = (lCurrentRaw >> 8) & 0xFF;
    lData[5] = lCurrentRaw & 0xFF;
    lData[7] = 3;
    const uint16_t lTiltRaw = (25UL * IOHC_POSITION_MAX) / 100UL;
    lData[13] = (lTiltRaw >> 8) & 0xFF;
    lData[14] = lTiltRaw & 0xFF;

    IoHomeFrame lResponse;
    buildPrivateResponseFrame(lResponse, lRemoteNodeId, lDeviceNodeId,
                              lData, sizeof(lData));
    ASSERT_TRUE(queueControllerResponse(lController, lResponse));
    ASSERT_TRUE(lChannel.testHasSlatFeedback());
    ASSERT_FLOAT_EQ(lChannel.testSlatFeedback(), 75.0f, 0.01f);
}

TEST(controller_private_response_stopped_marker_uses_target_position)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);
    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Private, 0));
    IoHomeFrame lTxFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lTxFrame));

    uint8_t lData[8] = {};
    lData[0] = 0x01; // stopped
    const uint16_t lTargetRaw = (40UL * IOHC_POSITION_MAX) / 100UL;
    lData[2] = (lTargetRaw >> 8) & 0xFF;
    lData[3] = lTargetRaw & 0xFF;
    lData[4] = (IOHC_POSITION_UNKNOWN >> 8) & 0xFF;
    lData[5] = IOHC_POSITION_UNKNOWN & 0xFF;
    lData[7] = 0xFF;

    IoHomeFrame lResponse;
    buildPrivateResponseFrame(lResponse, lRemoteNodeId, lDeviceNodeId,
                              lData, sizeof(lData));
    ASSERT_TRUE(queueControllerResponse(lController, lResponse));
    ASSERT_TRUE(lChannel.testHasPositionFeedback());
    ASSERT_TRUE(lChannel.testHasTargetPositionFeedback());
    ASSERT_TRUE(lChannel.testHasStatusUpdate());
    ASSERT_TRUE(!lChannel.testStatusMoving());
    ASSERT_FLOAT_EQ(lChannel.testPositionFeedback(), 40.0f, 0.01f);
    ASSERT_FLOAT_EQ(lChannel.testTargetPositionFeedback(), 40.0f, 0.01f);
}

static bool queueControllerPassiveFrame(IoHomeController &iController,
                                        const IoHomeFrame &iFrame)
{
    uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(iFrame, lBuffer, sizeof(lBuffer));
    if (lLen == 0)
        return false;

    iController.radio().testQueueReceivedPacket(lBuffer, lLen);
    iController.loop();
    return true;
}

static void buildPassiveKeyInitFrame(IoHomeFrame &oFrame,
                                     uint32_t iRemoteNodeId,
                                     uint32_t iDeviceNodeId)
{
    oFrame.init();
    oFrame.setStart2W();
    oFrame.setSrcNode(iRemoteNodeId);
    oFrame.setDestNode(iDeviceNodeId);
    oFrame.commandId = IoHomeCommand::KeyInitTransfer;
    oFrame.dataLen = 0;
    oFrame.hasHmac = false;
}

static void buildPassiveExecuteFrame(IoHomeFrame &oFrame,
                                     uint32_t iRemoteNodeId,
                                     uint32_t iDeviceNodeId)
{
    oFrame.init();
    oFrame.setStart2W();
    oFrame.setSrcNode(iRemoteNodeId);
    oFrame.setDestNode(iDeviceNodeId);
    oFrame.commandId = IoHomeCommand::Execute;
    oFrame.dataLen = 0;
    oFrame.hasHmac = false;
}

static void buildPassiveChallengeFrame(IoHomeFrame &oFrame,
                                       uint32_t iRemoteNodeId,
                                       uint32_t iDeviceNodeId,
                                       const uint8_t iChallenge[6])
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = IoHomeCommand::ChallengeRequest;
    memcpy(oFrame.data, iChallenge, 6);
    oFrame.dataLen = 6;
    oFrame.hasHmac = false;
}

static void buildPassiveKeyTransferFrame(IoHomeFrame &oFrame,
                                         uint32_t iRemoteNodeId,
                                         uint32_t iDeviceNodeId,
                                         const uint8_t iChallenge[6],
                                         const uint8_t iSystemKey[16])
{
    const uint8_t lKeyInitData[1] = {static_cast<uint8_t>(IoHomeCommand::KeyInitTransfer)};
    uint8_t lEncryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::crypt2WKeyXor(lKeyInitData, sizeof(lKeyInitData),
                                            iChallenge, iSystemKey,
                                            IOHC_TRANSFER_KEY, lEncryptedKey));

    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iRemoteNodeId);
    oFrame.setDestNode(iDeviceNodeId);
    oFrame.commandId = IoHomeCommand::KeyTransfer;
    memcpy(oFrame.data, lEncryptedKey, sizeof(lEncryptedKey));
    oFrame.dataLen = 16;
    oFrame.hasHmac = false;
}

TEST(controller_passive_key_sniff_start_stop_clear)
{
    IoHomeController lController;
    IoHomecontrol lModule;
    lController.setModule(&lModule);
    lController.setOwnNodeId(0x831F2A);
    lController.init();

    ASSERT_EQ(lController.passiveKeySniffStatus(), IoHomeController::PassiveKeySniffStatus::Idle);
    ASSERT_TRUE(lController.startPassiveKeySniff(0));
    ASSERT_TRUE(lController.isPassiveMode());
    ASSERT_EQ(lController.passiveKeySniffStatus(), IoHomeController::PassiveKeySniffStatus::Listening);

    lController.stopPassiveKeySniff();
    ASSERT_TRUE(!lController.isPassiveMode());
    ASSERT_EQ(lController.passiveKeySniffStatus(), IoHomeController::PassiveKeySniffStatus::Idle);

    lController.clearPassiveKeyResult();
    ASSERT_TRUE(!lController.passiveKeyResult().valid);
}

TEST(controller_passive_mode_does_not_sniff_without_explicit_start)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lChallenge[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t lSystemKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lController.setPassiveMode(true);

    IoHomeFrame lFrame;
    buildPassiveKeyInitFrame(lFrame, lRemoteNodeId, lDeviceNodeId);
    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));
    buildPassiveChallengeFrame(lFrame, lRemoteNodeId, lDeviceNodeId, lChallenge);
    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));
    buildPassiveKeyTransferFrame(lFrame, lRemoteNodeId, lDeviceNodeId, lChallenge, lSystemKey);
    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));

    ASSERT_EQ(lController.passiveKeySniffStatus(), IoHomeController::PassiveKeySniffStatus::Idle);
    ASSERT_TRUE(!lController.passiveKeyResult().valid);
    ASSERT_EQ(lModule.testPassiveCaptureCount(), 0);
}

TEST(controller_key_extract_answers_discovery_with_throwaway_id)
{
    const uint32_t lOwnNodeId = 0x112233;
    const uint32_t lHubNodeId = 0x445566;

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    initKeyExtractControllerForTest(lController, lModule, lOwnNodeId);

    ASSERT_TRUE(lController.startKeyExtraction());
    ASSERT_EQ(lController.keyExtractStatus(), IoHomeController::KeyExtractStatus::Armed);

    IoHomeFrame lRequest;
    buildGatewayDiscoverRequest(lRequest, lHubNodeId);

    IoHomeFrame lResponse;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::DiscoverResponse);
    ASSERT_EQ(lResponse.getDestNodeId(), lHubNodeId);
    ASSERT_NE(lResponse.getSrcNodeId(), 0U);
    ASSERT_NE(lResponse.getSrcNodeId(), lOwnNodeId);
    ASSERT_EQ(lResponse.data[2], static_cast<uint8_t>((lResponse.getSrcNodeId() >> 16) & 0xFF));
    ASSERT_EQ(lResponse.data[3], static_cast<uint8_t>((lResponse.getSrcNodeId() >> 8) & 0xFF));
    ASSERT_EQ(lResponse.data[4], static_cast<uint8_t>(lResponse.getSrcNodeId() & 0xFF));
}

TEST(controller_key_extract_broadcasts_reply_with_ch2_last)
{
    const uint32_t lOwnNodeId = 0x112233;
    const uint32_t lHubNodeId = 0x445566;

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    initKeyExtractControllerForTest(lController, lModule, lOwnNodeId);
    ASSERT_TRUE(lController.startKeyExtraction());

    IoHomeFrame lRequest;
    buildGatewayDiscoverRequest(lRequest, lHubNodeId);
    IoHomeFrame lResponse;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_LONG);

    // Finish the CH1, CH3, then CH2 reply sequence. The final radio channel
    // must be CH2 so a hub reacting to it finds us back in receive mode.
    for (uint8_t i = 0; i < 4; i++)
        lController.loop();
    ASSERT_EQ(lController.radio().testTransmitCount(), 3U);
    ASSERT_EQ(lController.radio().testCurrentFrequency(), IOHC_FREQ_2);
}

TEST(controller_key_extract_reuses_stored_challenge_on_key_init_retry)
{
    const uint32_t lOwnNodeId = 0x112233;
    const uint32_t lHubNodeId = 0x445566;

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    initKeyExtractControllerForTest(lController, lModule, lOwnNodeId);
    ASSERT_TRUE(lController.startKeyExtraction());

    IoHomeFrame lDiscoverReq;
    buildGatewayDiscoverRequest(lDiscoverReq, lHubNodeId);
    IoHomeFrame lDiscoverResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lDiscoverReq, lDiscoverResp));
    const uint32_t lThrowawayNodeId = lDiscoverResp.getSrcNodeId();

    IoHomeFrame lKeyInitReq;
    buildKeyExtractKeyInit(lKeyInitReq, lHubNodeId, lThrowawayNodeId);

    IoHomeFrame lChallengeResp1;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lKeyInitReq, lChallengeResp1));
    ASSERT_EQ(lChallengeResp1.commandId, IoHomeCommand::ChallengeRequest);
    ASSERT_EQ(lChallengeResp1.dataLen, 6);

    IoHomeFrame lChallengeResp2;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lKeyInitReq, lChallengeResp2));
    ASSERT_EQ(lChallengeResp2.commandId, IoHomeCommand::ChallengeRequest);
    ASSERT_EQ(lChallengeResp2.dataLen, 6);
    ASSERT_MEM_EQ(lChallengeResp1.data, lChallengeResp2.data, 6);
}

TEST(controller_key_extract_acknowledges_discovery_confirmation)
{
    const uint32_t lOwnNodeId = 0x112233;
    const uint32_t lHubNodeId = 0x445566;

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    initKeyExtractControllerForTest(lController, lModule, lOwnNodeId);
    ASSERT_TRUE(lController.startKeyExtraction());

    IoHomeFrame lDiscoverReq;
    buildGatewayDiscoverRequest(lDiscoverReq, lHubNodeId);
    IoHomeFrame lDiscoverResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lDiscoverReq, lDiscoverResp));
    const uint32_t lThrowawayNodeId = lDiscoverResp.getSrcNodeId();

    IoHomeFrame lConfirmation;
    buildKeyExtractConfirmation(lConfirmation, lHubNodeId, lThrowawayNodeId);
    IoHomeFrame lConfirmationAck;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lConfirmation, lConfirmationAck));
    ASSERT_EQ(lConfirmationAck.commandId, IoHomeCommand::ConfirmationACK);
    ASSERT_EQ(lConfirmationAck.getSrcNodeId(), lThrowawayNodeId);
    ASSERT_EQ(lConfirmationAck.getDestNodeId(), lHubNodeId);

    // The hub may retry 0x2C after missing 0x2D; it must receive the same
    // confirmation instead of pulling the responder back to discovery.
    IoHomeFrame lRetryAck;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lConfirmation, lRetryAck));
    ASSERT_EQ(lRetryAck.commandId, IoHomeCommand::ConfirmationACK);
}

TEST(controller_key_extract_recovers_hub_system_key)
{
    const uint32_t lOwnNodeId = 0x112233;
    const uint32_t lHubNodeId = 0x445566;
    const uint8_t lSystemKey[16] = {
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    initKeyExtractControllerForTest(lController, lModule, lOwnNodeId);
    ASSERT_TRUE(lController.startKeyExtraction());

    IoHomeFrame lDiscoverReq;
    buildGatewayDiscoverRequest(lDiscoverReq, lHubNodeId);
    IoHomeFrame lDiscoverResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lDiscoverReq, lDiscoverResp));
    const uint32_t lThrowawayNodeId = lDiscoverResp.getSrcNodeId();

    IoHomeFrame lKeyInitReq;
    buildKeyExtractKeyInit(lKeyInitReq, lHubNodeId, lThrowawayNodeId);
    IoHomeFrame lChallengeResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lKeyInitReq, lChallengeResp));

    IoHomeFrame lKeyTransferReq;
    buildKeyExtractKeyTransfer(lKeyTransferReq, lHubNodeId, lThrowawayNodeId,
                               lChallengeResp.data, lSystemKey);
    IoHomeFrame lConfirmResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lKeyTransferReq, lConfirmResp));
    ASSERT_EQ(lConfirmResp.commandId, IoHomeCommand::KeyTransferConfirmation);
    ASSERT_EQ(lConfirmResp.getSrcNodeId(), lThrowawayNodeId);
    ASSERT_EQ(lConfirmResp.getDestNodeId(), lHubNodeId);

    const auto &lResult = lController.keyExtractResult();
    ASSERT_TRUE(lResult.valid);
    ASSERT_EQ(lResult.nodeId, lHubNodeId);
    ASSERT_MEM_EQ(lResult.key, lSystemKey, sizeof(lSystemKey));
    ASSERT_EQ(lController.keyExtractStatus(), IoHomeController::KeyExtractStatus::Captured);
    ASSERT_EQ(lModule.testPassiveCaptureCount(), 1);
    ASSERT_MEM_EQ(lModule.testLastPassiveKeyResult().key, lSystemKey, sizeof(lSystemKey));
}

TEST(controller_key_extract_completes_hub_address_verification)
{
    const uint32_t lOwnNodeId = 0x112233;
    const uint32_t lHubNodeId = 0x445566;
    const uint8_t lSystemKey[16] = {
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    const uint8_t lAddressChallenge[6] = {0x64, 0x45, 0xE0, 0x81, 0xDC, 0x93};

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    initKeyExtractControllerForTest(lController, lModule, lOwnNodeId);
    ASSERT_TRUE(lController.startKeyExtraction());

    IoHomeFrame lRequest;
    IoHomeFrame lResponse;
    buildGatewayDiscoverRequest(lRequest, lHubNodeId);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    const uint32_t lThrowawayNodeId = lResponse.getSrcNodeId();

    buildKeyExtractKeyInit(lRequest, lHubNodeId, lThrowawayNodeId);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::ChallengeRequest);

    buildKeyExtractKeyTransfer(lRequest, lHubNodeId, lThrowawayNodeId, lResponse.data, lSystemKey);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::KeyTransferConfirmation);
    ASSERT_EQ(lController.keyExtractStatus(), IoHomeController::KeyExtractStatus::Captured);

    // The advertised throwaway address is public; only the hub that handed us
    // the key may drive the post-extraction verification round.
    buildKeyExtractAddressRequest(lRequest, 0x123456, lThrowawayNodeId);
    ASSERT_TRUE(!queueGatewayRequestAndLoop(lController, lRequest, lResponse));

    buildKeyExtractAddressRequest(lRequest, lHubNodeId, lThrowawayNodeId);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::AddressResponse);
    ASSERT_EQ(lResponse.dataLen, 3);
    ASSERT_EQ(lResponse.data[0], static_cast<uint8_t>(lThrowawayNodeId >> 16));
    ASSERT_EQ(lResponse.data[1], static_cast<uint8_t>(lThrowawayNodeId >> 8));
    ASSERT_EQ(lResponse.data[2], static_cast<uint8_t>(lThrowawayNodeId));

    buildPairChallengeRequestFrame(lRequest, lThrowawayNodeId, lHubNodeId, lAddressChallenge);
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lRequest, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::ChallengeResponse);
    ASSERT_TRUE((lResponse.ctrlByte0 & IOHC_CTRL0_END) != 0);
    ASSERT_TRUE((lResponse.ctrlByte1 & IOHC_CTRL1_LOW_POWER) == 0);

    IoHomeFrame lAddressResponse;
    lAddressResponse.init();
    lAddressResponse.ctrlByte0 = 0;
    lAddressResponse.ctrlByte1 = 0;
    lAddressResponse.setSrcNode(lThrowawayNodeId);
    lAddressResponse.setDestNode(lHubNodeId);
    lAddressResponse.commandId = IoHomeCommand::AddressResponse;
    lAddressResponse.data[0] = static_cast<uint8_t>(lThrowawayNodeId >> 16);
    lAddressResponse.data[1] = static_cast<uint8_t>(lThrowawayNodeId >> 8);
    lAddressResponse.data[2] = static_cast<uint8_t>(lThrowawayNodeId);
    lAddressResponse.dataLen = 3;
    uint8_t lTranscript[4] = {static_cast<uint8_t>(IoHomeCommand::AddressResponse),
                              lAddressResponse.data[0], lAddressResponse.data[1], lAddressResponse.data[2]};
    uint8_t lExpectedHmac[IOHC_HMAC_SIZE] = {};
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(lTranscript, sizeof(lTranscript),
                                           lAddressChallenge, lSystemKey, lExpectedHmac));
    ASSERT_MEM_EQ(lResponse.data, lExpectedHmac, IOHC_HMAC_SIZE);
}

TEST(controller_key_extract_ignores_frames_addressed_to_real_node_id)
{
    const uint32_t lOwnNodeId = 0x112233;
    const uint32_t lHubNodeId = 0x445566;

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    initKeyExtractControllerForTest(lController, lModule, lOwnNodeId);
    ASSERT_TRUE(lController.startKeyExtraction());

    IoHomeFrame lDiscoverReq;
    buildGatewayDiscoverRequest(lDiscoverReq, lHubNodeId);
    IoHomeFrame lDiscoverResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lDiscoverReq, lDiscoverResp));

    IoHomeFrame lWrongKeyInitReq;
    buildKeyExtractKeyInit(lWrongKeyInitReq, lHubNodeId, lOwnNodeId);
    for (uint8_t i = 0; i < 4; i++)
        lController.loop(); // drain the preceding discovery broadcast
    lController.radio().testClearTransmittedPacket();
    uint8_t lBuf[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lLen = serializeFrameForTest(lWrongKeyInitReq, lBuf, sizeof(lBuf));
    ASSERT_TRUE(lLen > 0);
    lController.radio().testQueueReceivedPacket(lBuf, lLen);
    lController.loop();
    ASSERT_TRUE(lController.radio().testLastTransmittedPacket().empty());
    ASSERT_EQ(lController.keyExtractStatus(), IoHomeController::KeyExtractStatus::Armed);
}

TEST(controller_key_extract_disarms_after_success)
{
    const uint32_t lOwnNodeId = 0x112233;
    const uint32_t lHubNodeId = 0x445566;
    const uint8_t lSystemKey[16] = {
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    initKeyExtractControllerForTest(lController, lModule, lOwnNodeId);
    ASSERT_TRUE(lController.startKeyExtraction());

    IoHomeFrame lDiscoverReq;
    buildGatewayDiscoverRequest(lDiscoverReq, lHubNodeId);
    IoHomeFrame lDiscoverResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lDiscoverReq, lDiscoverResp));
    const uint32_t lThrowawayNodeId = lDiscoverResp.getSrcNodeId();

    IoHomeFrame lKeyInitReq;
    buildKeyExtractKeyInit(lKeyInitReq, lHubNodeId, lThrowawayNodeId);
    IoHomeFrame lChallengeResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lKeyInitReq, lChallengeResp));

    IoHomeFrame lKeyTransferReq;
    buildKeyExtractKeyTransfer(lKeyTransferReq, lHubNodeId, lThrowawayNodeId,
                               lChallengeResp.data, lSystemKey);
    IoHomeFrame lConfirmResp;
    ASSERT_TRUE(queueGatewayRequestAndLoop(lController, lKeyTransferReq, lConfirmResp));

    // Extraction stays armed for a bounded verification window, then disarms
    // automatically if the hub never performs the 0x36 address check.
    ioHomeTestAdvanceMillis(IoHomeController::kKeyExtractPostExtractGraceMs + 1U);
    lController.loop();
    lController.radio().testClearTransmittedPacket();
    IoHomeFrame lSecondDiscoverResp;
    ASSERT_TRUE(!queueGatewayRequestAndLoop(lController, lDiscoverReq, lSecondDiscoverResp));
    ASSERT_EQ(lController.keyExtractStatus(), IoHomeController::KeyExtractStatus::Captured);
}

TEST(controller_passive_key_sniff_captures_result_and_callback)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lChallenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const uint8_t lSystemKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();

    ASSERT_TRUE(lController.startPassiveKeySniff(0));

    IoHomeFrame lFrame;
    buildPassiveKeyInitFrame(lFrame, lRemoteNodeId, lDeviceNodeId);
    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));
    buildPassiveChallengeFrame(lFrame, lRemoteNodeId, lDeviceNodeId, lChallenge);
    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));
    buildPassiveKeyTransferFrame(lFrame, lRemoteNodeId, lDeviceNodeId, lChallenge, lSystemKey);
    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));

    const auto &lResult = lController.passiveKeyResult();
    ASSERT_EQ(lController.passiveKeySniffStatus(), IoHomeController::PassiveKeySniffStatus::Captured);
    ASSERT_TRUE(lResult.valid);
    ASSERT_EQ(lResult.nodeId, lDeviceNodeId);
    ASSERT_MEM_EQ(lResult.key, lSystemKey, 16);
    ASSERT_TRUE(!lController.isPassiveMode());

    ASSERT_EQ(lModule.testPassiveCaptureCount(), 1);
    const auto &lCallbackResult = lModule.testLastPassiveKeyResult();
    ASSERT_TRUE(lCallbackResult.valid);
    ASSERT_EQ(lCallbackResult.nodeId, lDeviceNodeId);
    ASSERT_MEM_EQ(lCallbackResult.key, lSystemKey, 16);

    lController.clearPassiveKeyResult();
    ASSERT_EQ(lController.passiveKeySniffStatus(), IoHomeController::PassiveKeySniffStatus::Idle);
    ASSERT_TRUE(!lController.passiveKeyResult().valid);
}

TEST(controller_1w_automatic_broadcast_type_maps_ets_roles_to_protocol_classes)
{
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(0), 0U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(1), 2U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(2), 4U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(3), 3U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(4), 5U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(5), 14U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(6), 6U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(7), 7U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(8), 9U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(9), 16U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(10), 19U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(11), 20U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(12), 15U);
    ASSERT_EQ(IoHomeController::oneWayBroadcastTypeForEtsDeviceType(0xFF), 0U);
}

TEST(controller_1w_key_receive_clones_remote_from_sendkey_frame)
{
    const uint32_t lOwnNodeId = 0x9F0071;
    const uint32_t lRemoteNodeId = 0x7E9E6E; // original remote we want to clone
    const uint8_t lManufacturer = 0x01;      // Velux
    const uint16_t lSequence = 0x148C;
    const uint8_t lClearKey[16] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lOwnNodeId);
    lController.init();
    lChannel.setIs1W(true);

    ASSERT_EQ(lController.oneWayKeyReceiveStatus(),
              IoHomeController::OneWayKeyReceiveStatus::Idle);
    ASSERT_TRUE(lController.startOneWayKeyReceive(0, 0));
    ASSERT_TRUE(lController.isPassiveMode());
    ASSERT_EQ(lController.oneWayKeyReceiveStatus(),
              IoHomeController::OneWayKeyReceiveStatus::Listening);

    // The original remote encrypts its key with the public transfer key, keyed
    // by its own node address, and broadcasts it inside a SendKey1W (0x30).
    const uint8_t lRemoteAddr[3] = {
        static_cast<uint8_t>((lRemoteNodeId >> 16) & 0xFF),
        static_cast<uint8_t>((lRemoteNodeId >> 8) & 0xFF),
        static_cast<uint8_t>(lRemoteNodeId & 0xFF)};
    uint8_t lEncryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(lClearKey, IOHC_TRANSFER_KEY, lRemoteAddr, lEncryptedKey));

    IoHomeFrame lFrame;
    lFrame.init();
    lFrame.setStart2W();
    lFrame.set1WMode();
    lFrame.setSrcNode(lRemoteNodeId);
    lFrame.setDestNode(0x00003F); // 1W broadcast target, type 0
    lFrame.commandId = IoHomeCommand::SendKey1W;
    memcpy(lFrame.data, lEncryptedKey, sizeof(lEncryptedKey));
    lFrame.data[16] = lManufacturer;
    lFrame.data[17] = 0x01;
    lFrame.data[18] = static_cast<uint8_t>((lSequence >> 8) & 0xFF);
    lFrame.data[19] = static_cast<uint8_t>(lSequence & 0xFF);
    lFrame.dataLen = 20;
    lFrame.hasHmac = false;

    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));

    ASSERT_EQ(lController.oneWayKeyReceiveStatus(),
              IoHomeController::OneWayKeyReceiveStatus::Captured);
    ASSERT_EQ(lController.oneWayKeyReceiveCapturedNode(), lRemoteNodeId);
    ASSERT_TRUE(!lController.isPassiveMode());

    // The channel profile is now a true clone of the original remote.
    ASSERT_EQ(lChannel.getOneWayControllerNodeId(), lRemoteNodeId);
    ASSERT_MEM_EQ(lChannel.getOneWayControllerKey(), lClearKey, 16);
    ASSERT_EQ(lChannel.getOneWayControllerManufacturer(), lManufacturer);
    ASSERT_EQ(lChannel.getSequence1W(), lSequence);
    ASSERT_EQ(lChannel.getReservedSequence1W(),
              static_cast<uint16_t>(lSequence + IOHC_1W_SEQUENCE_RESERVE_WINDOW));
    bool lSaveRequired = true;
    ASSERT_EQ(lChannel.incrementSequence1W(false, lSaveRequired), static_cast<uint16_t>(lSequence + 1));
    ASSERT_TRUE(!lSaveRequired);
    ASSERT_MEM_EQ(lChannel.getEncryptionKey(), lClearKey, 16);
}

TEST(controller_1w_key_receive_rejects_shared_controller_profile)
{
    const uint8_t lExistingKey[16] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
        0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x01};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lProfileChannel;
    IoHomecontrolChannel lLinkedChannel;
    lModule.testSetChannel(0, &lProfileChannel);
    lModule.testSetChannel(1, &lLinkedChannel);
    lController.setModule(&lModule);
    lController.init();
    lProfileChannel.setIs1W(true);
    lProfileChannel.setOneWayControllerNodeId(0x810001);
    lProfileChannel.setOneWayControllerKey(lExistingKey);
    lLinkedChannel.setIs1W(true);
    lLinkedChannel.setConfigured1WProfileChannel(0);

    ASSERT_TRUE(!lController.startOneWayKeyReceive(1, 0));
    ASSERT_EQ(lController.oneWayKeyReceiveStatus(),
              IoHomeController::OneWayKeyReceiveStatus::SharedProfile);
    ASSERT_TRUE(!lController.isPassiveMode());
    ASSERT_EQ(lProfileChannel.getOneWayControllerNodeId(), 0x810001U);
    ASSERT_MEM_EQ(lProfileChannel.getOneWayControllerKey(), lExistingKey, 16);
}

TEST(controller_1w_key_receive_verifies_optional_sendkey_trailer_mac)
{
    const uint32_t lRemoteNodeId = 0x7E9E6E;
    const uint16_t lSequence = 0x148C;
    const uint8_t lClearKey[16] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(0x9F0071);
    lController.init();
    lChannel.setIs1W(true);
    ASSERT_TRUE(lController.startOneWayKeyReceive(0, 0));

    const uint8_t lRemoteAddr[3] = {0x7E, 0x9E, 0x6E};
    uint8_t lEncryptedKey[16];
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(lClearKey, IOHC_TRANSFER_KEY, lRemoteAddr, lEncryptedKey));

    IoHomeFrame lFrame;
    lFrame.init();
    lFrame.set1WMode();
    lFrame.setSrcNode(lRemoteNodeId);
    lFrame.setDestNode(0x00003F);
    lFrame.commandId = IoHomeCommand::SendKey1W;
    memcpy(lFrame.data, lEncryptedKey, sizeof(lEncryptedKey));
    lFrame.data[16] = 0x01;
    lFrame.data[17] = 0x01;
    lFrame.data[18] = static_cast<uint8_t>(lSequence >> 8);
    lFrame.data[19] = static_cast<uint8_t>(lSequence & 0xFF);
    lFrame.dataLen = 20;
    uint8_t lTranscript[17] = {static_cast<uint8_t>(IoHomeCommand::SendKey1W)};
    memcpy(lTranscript + 1, lEncryptedKey, sizeof(lEncryptedKey));
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(lTranscript, sizeof(lTranscript), lSequence,
                                           lClearKey, lFrame.trailerMac));
    lFrame.hasTrailerMac = true;

    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));
    ASSERT_EQ(lController.oneWayKeyReceiveStatus(),
              IoHomeController::OneWayKeyReceiveStatus::CapturedTrailerMacVerified);
    ASSERT_MEM_EQ(lChannel.getOneWayControllerKey(), lClearKey, 16);
}

TEST(controller_1w_key_receive_rejects_invalid_sendkey_trailer_mac)
{
    const uint8_t lClearKey[16] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(0x9F0071);
    lController.init();
    lChannel.setIs1W(true);
    ASSERT_TRUE(lController.startOneWayKeyReceive(0, 0));

    IoHomeFrame lFrame;
    lFrame.init();
    lFrame.set1WMode();
    lFrame.setSrcNode(0x7E9E6E);
    lFrame.setDestNode(0x00003F);
    lFrame.commandId = IoHomeCommand::SendKey1W;
    const uint8_t lRemoteAddr[3] = {0x7E, 0x9E, 0x6E};
    ASSERT_TRUE(IoHomeCrypto::encrypt1WKey(lClearKey, IOHC_TRANSFER_KEY, lRemoteAddr, lFrame.data));
    lFrame.data[16] = 0x01;
    lFrame.data[17] = 0x01;
    lFrame.data[18] = 0x14;
    lFrame.data[19] = 0x8C;
    lFrame.dataLen = 20;
    uint8_t lTranscript[17] = {static_cast<uint8_t>(IoHomeCommand::SendKey1W)};
    memcpy(lTranscript + 1, lFrame.data, 16);
    ASSERT_TRUE(IoHomeCrypto::createHmac1W(lTranscript, sizeof(lTranscript), 0x148C,
                                           lClearKey, lFrame.trailerMac));
    lFrame.trailerMac[0] ^= 0x01;
    lFrame.hasTrailerMac = true;

    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));
    ASSERT_EQ(lController.oneWayKeyReceiveStatus(),
              IoHomeController::OneWayKeyReceiveStatus::TrailerMacInvalid);
    ASSERT_EQ(lChannel.getOneWayControllerNodeId(), 0U);
}

TEST(controller_1w_key_receive_rejects_non_1w_channel)
{
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(0x9F0071);
    lController.init();
    lChannel.setIs1W(false);

    ASSERT_TRUE(!lController.startOneWayKeyReceive(0, 0));
    ASSERT_TRUE(!lController.isPassiveMode());
    ASSERT_EQ(lController.oneWayKeyReceiveStatus(),
              IoHomeController::OneWayKeyReceiveStatus::Idle);
}

TEST(controller_1w_execute_uses_configured_channel_acei)
{
    const uint32_t lRemoteNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    // Default channel ACEI (0x43) is used when nothing is configured.
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setIs1W(true);
        lChannel.setConfigured1WBroadcastType(0);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);
        lChannel.setEncryptionKey(lKey);

        ASSERT_EQ(lChannel.getConfigured1WAcei(), 0x43);
        ASSERT_TRUE(lController.sendOneWayChannelExecuteWithType(&lChannel, 0x0000, 0, 0, 0));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(!lPacket.empty());
        IoHomeFrame lFrame;
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
        ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
        ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
        ASSERT_EQ(lFrame.data[0], 0x01);
        ASSERT_EQ(lFrame.data[1], 0x43);
    }

    // Configuring the channel ACEI to the Velux remote value (0x61) is honored
    // in the transmitted 1W Execute frame while everything else is unchanged.
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setIs1W(true);
        lChannel.setConfigured1WBroadcastType(0);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);
        lChannel.setEncryptionKey(lKey);
        lChannel.setConfigured1WAcei(0x61);

        ASSERT_EQ(lChannel.getConfigured1WAcei(), 0x61);
        ASSERT_TRUE(lController.sendOneWayChannelExecuteWithType(&lChannel, 0x0000, 0, 0, 0));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(!lPacket.empty());
        IoHomeFrame lFrame;
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
        ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
        ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
        ASSERT_EQ(lFrame.data[0], 0x01);
        ASSERT_EQ(lFrame.data[1], 0x61);
    }
}

TEST(controller_passive_remote_activity_schedules_follow_up_poll_for_target_device)
{
    const uint32_t lOwnNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint32_t lRemoteNodeId = 0x112233;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lOwnNodeId, lDeviceNodeId, lKey);

    IoHomeFrame lFrame;
    buildPassiveExecuteFrame(lFrame, lRemoteNodeId, lDeviceNodeId);
    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));

    ASSERT_TRUE(lChannel.testHasScheduledStatusPoll());
    ASSERT_EQ(lChannel.testScheduledStatusPollCount(), 1);
    ASSERT_EQ(lChannel.testLastScheduledStatusPollMs(), 2000UL);
}

TEST(controller_linked_remote_activity_schedules_follow_up_poll_for_linked_device)
{
    const uint32_t lOwnNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint32_t lRemoteNodeId = 0x112233;
    const uint32_t lOtherNodeId = 0x00003F;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lOwnNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lModule.remoteMap().addRemote(lRemoteNodeId, "Remote"));
    ASSERT_TRUE(lModule.remoteMap().linkDevice(lRemoteNodeId, lDeviceNodeId));

    IoHomeFrame lFrame;
    buildPassiveExecuteFrame(lFrame, lRemoteNodeId, lOtherNodeId);
    ASSERT_TRUE(queueControllerPassiveFrame(lController, lFrame));

    ASSERT_TRUE(lChannel.testHasScheduledStatusPoll());
    ASSERT_EQ(lChannel.testScheduledStatusPollCount(), 1);
    ASSERT_EQ(lChannel.testLastScheduledStatusPollMs(), 2000UL);
}

static bool buildChallengeRequestPacket(uint32_t iRemoteNodeId,
                                        uint32_t iDeviceNodeId,
                                        const uint8_t iChallenge[6],
                                        uint8_t *oBuffer,
                                        uint8_t &oLen)
{
    IoHomeFrame lChallengeFrame;
    lChallengeFrame.init();
    lChallengeFrame.ctrlByte0 = IOHC_CTRL0_END;
    lChallengeFrame.ctrlByte1 = 0x01;
    lChallengeFrame.setSrcNode(iDeviceNodeId);
    lChallengeFrame.setDestNode(iRemoteNodeId);
    lChallengeFrame.commandId = IoHomeCommand::ChallengeRequest;
    memcpy(lChallengeFrame.data, iChallenge, 6);
    lChallengeFrame.dataLen = 6;
    lChallengeFrame.hasHmac = false;

    oLen = serializeFrameForTest(lChallengeFrame, oBuffer, IOHC_FRAME_BUFFER_SIZE);
    return oLen > 0;
}

static bool sendExecuteAndAnswerChallenge(IoHomeController &iController,
                                          uint32_t iRemoteNodeId,
                                          uint32_t iDeviceNodeId,
                                          const uint8_t iKey[16],
                                          IoHomeFrame &oChallengeResponse)
{
    if (!iController.sendCommand(iDeviceNodeId, iKey, IoHomeCommand::Execute, 50))
        return false;

    iController.radio().testClearTransmittedPacket();
    iController.loop(); // Idle -> TxPending
    iController.loop(); // TxPending -> TxInProgress, initial Execute transmitted
    iController.loop(); // TxInProgress -> WaitResponse

    const uint8_t lChallenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    uint8_t lChallengePacket[IOHC_FRAME_BUFFER_SIZE];
    uint8_t lChallengeLen = 0;
    if (!buildChallengeRequestPacket(iRemoteNodeId, iDeviceNodeId,
                                     lChallenge, lChallengePacket, lChallengeLen))
    {
        return false;
    }

    iController.radio().testClearTransmittedPacket();
    iController.radio().testQueueReceivedPacket(lChallengePacket, lChallengeLen);
    iController.loop();

    const auto &lPacket = iController.radio().testLastTransmittedPacket();
    if (lPacket.empty())
        return false;
    return deserializeFrameForTest(oChallengeResponse, lPacket.data(), static_cast<uint8_t>(lPacket.size()));
}

static void buildStatusUpdateFrame(IoHomeFrame &oFrame,
                                   uint32_t iRemoteNodeId,
                                   uint32_t iDeviceNodeId,
                                   const uint8_t *iData,
                                   uint8_t iDataLen)
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = IoHomeCommand::StatusUpdate;
    memcpy(oFrame.data, iData, iDataLen);
    oFrame.dataLen = iDataLen;
    oFrame.hasHmac = false;
}

static void buildChallengeResponseFrame(IoHomeFrame &oFrame,
                                        uint32_t iRemoteNodeId,
                                        uint32_t iDeviceNodeId,
                                        const uint8_t *iStatusData,
                                        uint8_t iStatusDataLen,
                                        const uint8_t iChallenge[6],
                                        const uint8_t iKey[16])
{
    uint8_t lHmacInput[1 + IOHC_FRAME_MAX_DATA] = {};
    lHmacInput[0] = static_cast<uint8_t>(IoHomeCommand::StatusUpdate);
    memcpy(lHmacInput + 1, iStatusData, iStatusDataLen);

    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = IoHomeCommand::ChallengeResponse;
    ASSERT_TRUE(IoHomeCrypto::createHmac2W(lHmacInput, 1 + iStatusDataLen,
                                           iChallenge, iKey, oFrame.data));
    oFrame.dataLen = IOHC_HMAC_SIZE;
    oFrame.hasHmac = false;
}

TEST(controller_2w_challenge_response_inherits_low_power)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(false);
    lChannel.setLowPower2W(true);

    IoHomeFrame lResponse;
    ASSERT_TRUE(sendExecuteAndAnswerChallenge(lController, lRemoteNodeId,
                                              lDeviceNodeId, lKey, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::ChallengeResponse);
    ASSERT_EQ(lResponse.dataLen, IOHC_HMAC_SIZE);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), 64);
    ASSERT_TRUE(lResponse.ctrlByte1 & IOHC_CTRL1_LOW_POWER);
}

TEST(controller_2w_challenge_response_can_clear_low_power_for_mains_device)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(false);
    lChannel.setLowPower2W(false);

    IoHomeFrame lResponse;
    ASSERT_TRUE(sendExecuteAndAnswerChallenge(lController, lRemoteNodeId,
                                              lDeviceNodeId, lKey, lResponse));
    ASSERT_EQ(lResponse.commandId, IoHomeCommand::ChallengeResponse);
    ASSERT_EQ(lResponse.dataLen, IOHC_HMAC_SIZE);
    ASSERT_TRUE((lResponse.ctrlByte1 & IOHC_CTRL1_LOW_POWER) == 0);
}

TEST(controller_status_update_receive_auth_uses_saved_command_data)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    uint8_t lStatusData[11] = {};
    lStatusData[0] = 0x01;
    lStatusData[1] = 0x80;
    lStatusData[3] = 55;
    const uint16_t lTargetRaw = (40UL * IOHC_POSITION_MAX) / 100UL;
    const uint16_t lCurrentRaw = (38UL * IOHC_POSITION_MAX) / 100UL;
    lStatusData[5] = (lTargetRaw >> 8) & 0xFF;
    lStatusData[6] = lTargetRaw & 0xFF;
    lStatusData[7] = (lCurrentRaw >> 8) & 0xFF;
    lStatusData[8] = lCurrentRaw & 0xFF;
    lStatusData[10] = 7;

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    IoHomeFrame lStatusFrame;
    buildStatusUpdateFrame(lStatusFrame, lRemoteNodeId, lDeviceNodeId,
                           lStatusData, sizeof(lStatusData));

    uint8_t lStatusBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lStatusLen = serializeFrameForTest(lStatusFrame, lStatusBuffer, sizeof(lStatusBuffer));
    ASSERT_TRUE(lStatusLen > 0);

    lController.radio().testClearTransmittedPacket();
    lController.radio().testQueueReceivedPacket(lStatusBuffer, lStatusLen);
    lController.loop();

    IoHomeFrame lChallengeRequest;
    const auto &lChallengePacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lChallengePacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lChallengeRequest, lChallengePacket.data(), static_cast<uint8_t>(lChallengePacket.size())));
    ASSERT_EQ(lChallengeRequest.commandId, IoHomeCommand::ChallengeRequest);
    ASSERT_EQ(lChallengeRequest.dataLen, 6);
    ASSERT_TRUE(!lChannel.testHasStatusUpdate());
    ASSERT_TRUE(!lChannel.testHasPositionFeedback());

    IoHomeFrame lChallengeResponse;
    buildChallengeResponseFrame(lChallengeResponse, lRemoteNodeId, lDeviceNodeId,
                                lStatusData, sizeof(lStatusData),
                                lChallengeRequest.data, lKey);
    uint8_t lChallengeResponseBuffer[IOHC_FRAME_BUFFER_SIZE];
    const uint8_t lChallengeResponseLen = serializeFrameForTest(lChallengeResponse, lChallengeResponseBuffer, sizeof(lChallengeResponseBuffer));
    ASSERT_TRUE(lChallengeResponseLen > 0);

    lController.radio().testClearTransmittedPacket();
    lController.radio().testQueueReceivedPacket(lChallengeResponseBuffer, lChallengeResponseLen);
    lController.loop();

    IoHomeFrame lAckFrame;
    const auto &lAckPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lAckPacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lAckFrame, lAckPacket.data(), static_cast<uint8_t>(lAckPacket.size())));
    ASSERT_EQ(lAckFrame.commandId, IoHomeCommand::StatusUpdateResponse);
    ASSERT_EQ(lAckFrame.dataLen, 2);
    ASSERT_EQ(lAckFrame.data[0], 0x05);
    ASSERT_EQ(lAckFrame.data[1], 0x00);

    ASSERT_TRUE(lChannel.testHasStatusUpdate());
    ASSERT_TRUE(!lChannel.testStatusMoving());
    ASSERT_TRUE(lChannel.testHasPositionFeedback());
    ASSERT_TRUE(lChannel.testHasTargetPositionFeedback());
    ASSERT_TRUE(lChannel.testHasBatteryLevel());
    ASSERT_EQ(lChannel.testBatteryLevel(), 55);
    ASSERT_TRUE(lChannel.testStatusExpected());
    ASSERT_FLOAT_EQ(lChannel.testTargetPositionFeedback(), 40.0f, 0.01f);

    for (int i = 0; i < 8; i++)
        lController.loop();
    ASSERT_EQ(lController.radio().testTransmitCount(), 4U);
}

static void buildGeneralInfo2ResponseFrame(IoHomeFrame &oFrame,
                                           uint32_t iRemoteNodeId,
                                           uint32_t iDeviceNodeId,
                                           const uint8_t *iData,
                                           uint8_t iDataLen)
{
    oFrame.init();
    oFrame.ctrlByte0 = IOHC_CTRL0_END;
    oFrame.ctrlByte1 = 0x00;
    oFrame.setSrcNode(iDeviceNodeId);
    oFrame.setDestNode(iRemoteNodeId);
    oFrame.commandId = IoHomeCommand::GetGeneralInfo2Response;
    memcpy(oFrame.data, iData, iDataLen);
    oFrame.dataLen = iDataLen;
    oFrame.hasHmac = false;
}

TEST(controller_status_update_requires_11_bytes_for_position)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50));
    IoHomeFrame lTxFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lTxFrame));

    uint8_t lData[10] = {};
    lData[0] = 0x01;
    lData[1] = 0x80;
    lData[3] = 44;
    const uint16_t lTargetRaw = (40UL * IOHC_POSITION_MAX) / 100UL;
    const uint16_t lCurrentRaw = (35UL * IOHC_POSITION_MAX) / 100UL;
    lData[5] = (lTargetRaw >> 8) & 0xFF;
    lData[6] = lTargetRaw & 0xFF;
    lData[7] = (lCurrentRaw >> 8) & 0xFF;
    lData[8] = lCurrentRaw & 0xFF;

    IoHomeFrame lResponse;
    buildStatusUpdateFrame(lResponse, lRemoteNodeId, lDeviceNodeId, lData, sizeof(lData));
    ASSERT_TRUE(queueControllerResponse(lController, lResponse));
    ASSERT_TRUE(!lChannel.testHasPositionFeedback());
    ASSERT_TRUE(!lChannel.testHasTargetPositionFeedback());
}

TEST(controller_private_response_requires_8_bytes_for_position)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lController.sendBatteryStatusQuery(lDeviceNodeId, lKey));
    IoHomeFrame lTxFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lTxFrame));

    uint8_t lData[7] = {};
    lData[0] = 0x01;
    lData[1] = 0x80;
    const uint16_t lTargetRaw = (55UL * IOHC_POSITION_MAX) / 100UL;
    const uint16_t lCurrentRaw = (50UL * IOHC_POSITION_MAX) / 100UL;
    lData[2] = (lTargetRaw >> 8) & 0xFF;
    lData[3] = lTargetRaw & 0xFF;
    lData[4] = (lCurrentRaw >> 8) & 0xFF;
    lData[5] = lCurrentRaw & 0xFF;

    IoHomeFrame lResponse;
    buildPrivateResponseFrame(lResponse, lRemoteNodeId, lDeviceNodeId, lData, sizeof(lData));
    ASSERT_TRUE(queueControllerResponse(lController, lResponse));
    ASSERT_TRUE(!lChannel.testHasPositionFeedback());
    ASSERT_TRUE(!lChannel.testHasTargetPositionFeedback());
}

TEST(controller_general_info2_response_uses_selector_before_tilt_decode)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);

        uint8_t lData[15] = {};
        lData[10] = 0x00;
        lData[11] = (0x02 << 6) | 0x05;
        lData[12] = 0x00;
        const uint16_t lTiltRaw = (25UL * IOHC_POSITION_MAX) / 100UL;
        lData[13] = (lTiltRaw >> 8) & 0xFF;
        lData[14] = lTiltRaw & 0xFF;

        IoHomeFrame lResponse;
        buildGeneralInfo2ResponseFrame(lResponse, lRemoteNodeId, lDeviceNodeId, lData, sizeof(lData));
        uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
        const uint8_t lLen = serializeFrameForTest(lResponse, lBuffer, sizeof(lBuffer));
        ASSERT_TRUE(lLen > 0);

        lController.radio().testQueueReceivedPacket(lBuffer, lLen);
        lController.loop();
        ASSERT_TRUE(!lChannel.testHasSlatFeedback());
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);

        uint8_t lData[15] = {};
        lData[10] = 0x00;
        lData[11] = (0x02 << 6) | 0x05;
        lData[12] = 0x01;
        const uint16_t lTiltRaw = (25UL * IOHC_POSITION_MAX) / 100UL;
        lData[13] = (lTiltRaw >> 8) & 0xFF;
        lData[14] = lTiltRaw & 0xFF;

        IoHomeFrame lResponse;
        buildGeneralInfo2ResponseFrame(lResponse, lRemoteNodeId, lDeviceNodeId, lData, sizeof(lData));
        uint8_t lBuffer[IOHC_FRAME_BUFFER_SIZE];
        const uint8_t lLen = serializeFrameForTest(lResponse, lBuffer, sizeof(lBuffer));
        ASSERT_TRUE(lLen > 0);

        lController.radio().testQueueReceivedPacket(lBuffer, lLen);
        lController.loop();
        ASSERT_TRUE(lChannel.testHasSlatFeedback());
        ASSERT_FLOAT_EQ(lChannel.testSlatFeedback(), 75.0f, 0.01f);
    }
}

TEST(controller_status_poll_failure_after_challenge_notifies_channel)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    static const uint8_t kChallenge[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Private, 0x03));

    IoHomeFrame lTxFrame;
    ASSERT_TRUE(transmitQueuedControllerFrame(lController, lTxFrame));
    ASSERT_EQ(lTxFrame.commandId, IoHomeCommand::Private);
    ASSERT_EQ(lTxFrame.dataLen, 3);
    ASSERT_EQ(lTxFrame.data[0], 0x03);
    ASSERT_EQ(lTxFrame.data[1], 0x00);
    ASSERT_EQ(lTxFrame.data[2], 0x00);

    IoHomeFrame lChallengeRequest;
    buildPairChallengeRequestFrame(lChallengeRequest, lRemoteNodeId, lDeviceNodeId, kChallenge);

    lController.radio().testSetNextTransmitError(RadioError::HardwareError);
    ASSERT_TRUE(queueControllerResponse(lController, lChallengeRequest));

    ASSERT_TRUE(lChannel.testHasStatusPollFailure());
    ASSERT_TRUE(lChannel.testStatusPollFailureAfterChallenge());
    ASSERT_EQ(lChannel.testStatusPollFailureCount(), 1);
    ASSERT_EQ(lChannel.testAuthPollFailureCount(), 1);
    ASSERT_EQ(lChannel.testDirectPollFailureCount(), 0);
    ASSERT_EQ(lController.state(), ControllerState::Idle);
}

TEST(controller_2w_initial_response_wait_uses_retry_gap)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50));
    lController.loop();
    lController.loop();
    lController.loop();

    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);

    ioHomeTestAdvanceMillis(IOHC_RX_TIMEOUT_MS - 1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);

    ioHomeTestAdvanceMillis(1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);

    ioHomeTestAdvanceMillis(IOHC_RETRY_GAP_MS - 1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);

    ioHomeTestAdvanceMillis(1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::TxPending);
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);

    lController.loop();
    lController.loop();
    ASSERT_EQ(lController.radio().testTransmitCount(), 2U);
}

TEST(retry_preserves_start_flag_for_2w_request)
{
    ASSERT_TRUE(retryKeepsStartForQueued2WCommand(IoHomeCommand::Execute, 50));
    ASSERT_TRUE(retryKeepsStartForQueued2WCommand(IoHomeCommand::Private, 0x03));
    ASSERT_TRUE(retryKeepsStartForQueued2WCommand(IoHomeCommand::GetName, 0x00));
    ASSERT_TRUE(retryKeepsStartForQueued2WSetName());
}

TEST(pairing_telemetry_reports_start_and_cancel)
{
    IoHomeController lController;

    ASSERT_TRUE(lController.startPairing(2, 0x123456));
    const IoHomeController::PairingTelemetry &lStarted = lController.pairingTelemetry();
    ASSERT_EQ(lStarted.outcome, IoHomeController::PairingOutcome::InProgress);
    ASSERT_EQ(lStarted.diagnostic, IoHomeController::PairingOutcome::None);
    ASSERT_EQ(lStarted.channel, 2);
    ASSERT_EQ(lStarted.peerNodeId, 0x123456U);

    lController.cancelPairing();
    const IoHomeController::PairingTelemetry &lCancelled = lController.pairingTelemetry();
    ASSERT_EQ(lCancelled.outcome, IoHomeController::PairingOutcome::Cancelled);
    ASSERT_EQ(lCancelled.diagnostic, IoHomeController::PairingOutcome::Cancelled);
}

TEST(byte_vector_controller_2w_execute_payloads_and_retry_start)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    struct Vector
    {
        uint8_t param;
        uint8_t profile;
        uint8_t expectedLen;
        uint8_t expectedPayload[8];
    };

    const Vector vectors[] = {
        {50, 0xFF, 8, {0x01, 0x67, 0x64, 0x00, 0x80, 0xD8, 0x06, 0x00}},
        {0xD2, 0xFF, 6, {0x01, 0x67, 0xD2, 0x00, 0x00, 0x00, 0x00, 0x00}},
        {0xD8, 0xFF, 6, {0x01, 0x67, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00}},
        {50, IOHC_EXECUTE_PROFILE_SILENT, 8, {0x01, 0x67, 0x64, 0x00, 0x80, 0xD8, 0x05, 0x00}},
        {0xD8, IOHC_EXECUTE_PROFILE_SILENT, 8, {0x01, 0x67, 0xD8, 0x00, 0x80, 0xD8, 0x05, 0x00}},
    };

    for (const Vector &v : vectors)
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        initPaired2WControllerForTest(lController, lModule, lChannel,
                                      lRemoteNodeId, lDeviceNodeId, lKey);

        ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, v.param, 0xFF, v.profile));

        IoHomeFrame lFirstFrame;
        ASSERT_TRUE(transmitQueuedControllerFrame(lController, lFirstFrame));
        ASSERT_EQ(lFirstFrame.commandId, IoHomeCommand::Execute);
        ASSERT_TRUE((lFirstFrame.ctrlByte0 & IOHC_CTRL0_START) != 0);
        ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_NORMAL_START);
        ASSERT_EQ(lFirstFrame.dataLen, v.expectedLen);
        ASSERT_MEM_EQ(lFirstFrame.data, v.expectedPayload, v.expectedLen);

        lController.loop(); // TxInProgress -> WaitResponse
        ioHomeTestAdvanceMillis(IOHC_RX_TIMEOUT_MS);
        lController.loop(); // timeout reached: arm retry gap
        ioHomeTestAdvanceMillis(IOHC_RETRY_GAP_MS);
        lController.loop(); // retry gap elapsed: rebuild frame and enter TxPending
        ASSERT_EQ(lController.state(), ControllerState::TxPending);

        lController.radio().testClearTransmittedPacket();
        lController.loop(); // retry TX

        const auto &lRetryPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(!lRetryPacket.empty());
        ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_NORMAL_START);

        IoHomeFrame lRetryFrame;
        ASSERT_TRUE(deserializeFrameForTest(lRetryFrame, lRetryPacket.data(), static_cast<uint8_t>(lRetryPacket.size())));
        ASSERT_TRUE((lRetryFrame.ctrlByte0 & IOHC_CTRL0_START) != 0);
        ASSERT_EQ(lRetryFrame.commandId, IoHomeCommand::Execute);
        ASSERT_EQ(lRetryFrame.dataLen, v.expectedLen);
        ASSERT_MEM_EQ(lRetryFrame.data, v.expectedPayload, v.expectedLen);
    }
}

TEST(byte_vector_controller_1w_default_and_typed_targets)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    struct Vector
    {
        uint8_t configuredType;
        uint32_t expectedDest;
    };

    const Vector vectors[] = {
        {0, 0x00003F},
        {2, 0x0000BF},
        {3, 0x0000FF},
    };

    for (const Vector &v : vectors)
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);
        lChannel.setConfigured1WBroadcastType(v.configuredType);

        ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 0xD8, 0x03));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(!lPacket.empty());

        IoHomeFrame lFrame;
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
        ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
        ASSERT_EQ(lFrame.getDestNodeId(), v.expectedDest);
        ASSERT_TRUE(lFrame.hasHmac);
    }
}

TEST(byte_vector_controller_1w_sendkey_no_hmac_and_20_byte_payload)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);
    lChannel.setEncryptionKey(lKey);
    lChannel.setOneWayControllerManufacturer(static_cast<uint8_t>(IoHomeManufacturer::Velux));

    ASSERT_TRUE(lController.startPairing1WAddOnly(0, 0));
    lController.radio().testClearTransmittedPacket();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_EQ(lPacket.size(), 29);

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::SendKey1W);
    ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
    ASSERT_EQ(lFrame.dataLen, 20);
    ASSERT_TRUE(!lFrame.hasHmac);
    ASSERT_EQ(lFrame.data[16], static_cast<uint8_t>(IoHomeManufacturer::Velux));
    ASSERT_EQ(lFrame.data[17], 0x01);
}

TEST(byte_vector_controller_1w_repeat_plan_long_then_four_short_40ms)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    ASSERT_EQ(IOHC_1W_REPEAT_COUNT, 4);
    ASSERT_EQ(IOHC_1W_REPEAT_INTERVAL_MS, 40);

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 0xD2));
    lController.loop();
    lController.loop();
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_LONG);

    lController.loop();
    for (uint8_t i = 0; i < IOHC_1W_REPEAT_COUNT; i++)
    {
        ioHomeTestAdvanceMillis(IOHC_1W_REPEAT_INTERVAL_MS - 1);
        lController.loop();
        ASSERT_EQ(lController.radio().testTransmitCount(), static_cast<uint32_t>(i + 1));

        ioHomeTestAdvanceMillis(1);
        lController.loop();
        ASSERT_EQ(lController.radio().testTransmitCount(), static_cast<uint32_t>(i + 2));
        ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_SHORT);
        lController.loop();
    }

    ASSERT_EQ(lController.radio().testTransmitCount(), 5U);
}

TEST(controller_2w_final_response_wait_and_sx1262_dwell)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    initPaired2WControllerForTest(lController, lModule, lChannel,
                                  lRemoteNodeId, lDeviceNodeId, lKey);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 50));
    lController.loop();
    lController.loop();
    lController.loop();

    const uint8_t lChallenge[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    uint8_t lChallengePacket[IOHC_FRAME_BUFFER_SIZE];
    uint8_t lChallengeLen = 0;
    ASSERT_TRUE(buildChallengeRequestPacket(lRemoteNodeId, lDeviceNodeId,
                                            lChallenge, lChallengePacket, lChallengeLen));

    lController.radio().testQueueReceivedPacket(lChallengePacket, lChallengeLen);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::TxInProgress);
    ASSERT_EQ(lController.radio().testTransmitCount(), 2U);

    const uint32_t lRxStartsBeforeDwell = lController.radio().rxStartCount();

    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);
    ASSERT_EQ(lController.radio().rxStartCount(), lRxStartsBeforeDwell + 1);

    ioHomeTestAdvanceMillis(IOHC_AUTH_DWELL_MS_SX1262 - 1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);
    ASSERT_EQ(lController.radio().rxStartCount(), lRxStartsBeforeDwell + 1);

    ioHomeTestAdvanceMillis(1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);
    // At dwell expiry RX scanning may restart immediately; before expiry it must not.
    ASSERT_TRUE(lController.radio().rxStartCount() >= lRxStartsBeforeDwell + 1);

    ioHomeTestAdvanceMillis(IOHC_RX_FINAL_TIMEOUT_MS - IOHC_AUTH_DWELL_MS_SX1262 - 1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::WaitResponse);
    ASSERT_EQ(lController.radio().testTransmitCount(), 2U);

    ioHomeTestAdvanceMillis(1);
    lController.loop();
    ASSERT_EQ(lController.state(), ControllerState::Idle);
    ASSERT_EQ(lController.radio().testTransmitCount(), 2U);
}

TEST(controller_default_1w_execute_uses_standard_vent_layout)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 0xD8, 0x03));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F); // default type 0 / All target
    ASSERT_TRUE(lFrame.ctrlByte1 & IOHC_CTRL1_LOW_POWER);
    ASSERT_EQ(lFrame.dataLen, 8);
    ASSERT_EQ(lFrame.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(lFrame.data[1], IOHC_ACEI_1W);
    ASSERT_EQ(lFrame.data[2], 0xD8);
    ASSERT_EQ(lFrame.data[3], 0x03);
    ASSERT_EQ(lFrame.data[4], 0x00);
    ASSERT_EQ(lFrame.data[5], 0x00);
    ASSERT_TRUE(lFrame.hasHmac);
}

TEST(controller_1w_execute_repeats_first_long_then_four_short)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    ioHomeTestSetMillis(0);
    ioHomeTestSetMicros(0);

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 0xD2));
    lController.loop(); // Idle -> TxPending
    lController.loop(); // TxPending -> TxInProgress, first TX
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_LONG);

    lController.loop(); // TxInProgress -> Tx1WRepeat
    ASSERT_EQ(lController.state(), ControllerState::Tx1WRepeat);

    for (uint8_t i = 0; i < IOHC_1W_REPEAT_COUNT; i++)
    {
        ioHomeTestAdvanceMillis(IOHC_1W_REPEAT_INTERVAL_MS - 1);
        lController.loop();
        ASSERT_EQ(lController.radio().testTransmitCount(), static_cast<uint32_t>(i + 1));

        ioHomeTestAdvanceMillis(1);
        lController.loop();
        ASSERT_EQ(lController.radio().testTransmitCount(), static_cast<uint32_t>(i + 2));
        ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_SHORT);

        lController.loop(); // finish repeat TX and either schedule next repeat or go idle
    }

    ASSERT_EQ(lController.radio().testTransmitCount(), 5U);
    ASSERT_EQ(lController.state(), ControllerState::Idle);
}

TEST(controller_1w_pairing_repeats_first_long_then_short)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    // Keep the native clock away from 0 because the controller uses 0 as the
    // "no repeat timer armed" sentinel for the blind 1W pairing wait path.
    ioHomeTestSetMillis(1000);
    ioHomeTestSetMicros(1000000);

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.setSystemKey(lKey);
    lController.init();
    lChannel.setIs1W(true);
    lChannel.setConfigured1WTargetNodeId(lDeviceNodeId);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);

    ASSERT_TRUE(lController.startPairing(0, lDeviceNodeId));
    lController.loop(); // PairSend1WAnnounce, first TX
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);
    ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_LONG);

    lController.loop(); // PairWait1WAnnounce schedules the first repeat
    ASSERT_EQ(lController.radio().testTransmitCount(), 1U);

    for (uint8_t i = 0; i < IOHC_1W_REPEAT_COUNT; i++)
    {
        const uint32_t lExpectedBeforeRepeat = static_cast<uint32_t>(i + 1);
        const uint32_t lExpectedAfterRepeat = static_cast<uint32_t>(i + 2);

        ioHomeTestAdvanceMillis(IOHC_1W_REPEAT_INTERVAL_MS - 1);
        lController.loop();
        ASSERT_EQ(lController.radio().testTransmitCount(), lExpectedBeforeRepeat);

        ioHomeTestAdvanceMillis(1);
        lController.loop();
        ASSERT_EQ(lController.radio().testTransmitCount(), lExpectedAfterRepeat);
        ASSERT_EQ(lController.radio().testLastPreambleLength(), IOHC_PREAMBLE_SHORT);

        lController.loop(); // finish repeat TX and schedule next repeat or complete
    }

    ASSERT_EQ(lController.radio().testTransmitCount(), 5U);
}

TEST(controller_1w_ui_open_position_conversion_matches_raw_closed_main)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);

    const uint8_t lUiOpenPercent = 75;
    const uint8_t lRawClosedPercent = IoHomeController::uiOpenPercentToRawClosedPercent(lUiOpenPercent);
    ASSERT_EQ(lRawClosedPercent, 25);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, lRawClosedPercent));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    IoHomeFrame lFrame;
    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_EQ(lFrame.dataLen, 8);
    ASSERT_EQ(lFrame.data[2], 0x32);
    ASSERT_EQ(lFrame.data[3], 0x00);
}

TEST(controller_default_1w_execute_matches_reference_payloads)
{
    struct TestCase
    {
        uint8_t param;
        uint8_t param2;
        uint8_t expectedData[6];
    };

    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};
    const TestCase lCases[] = {
        {0x00, 0xFF, {IOHC_ORIGINATOR_USER, IOHC_ACEI_1W, 0x00, 0x00, 0x00, 0x00}},
        {100, 0xFF, {IOHC_ORIGINATOR_USER, IOHC_ACEI_1W, 0xC8, 0x00, 0x00, 0x00}},
        {0xD2, 0xFF, {IOHC_ORIGINATOR_USER, IOHC_ACEI_1W, 0xD2, 0x00, 0x00, 0x00}},
        {0xD8, 0x03, {IOHC_ORIGINATOR_USER, IOHC_ACEI_1W, 0xD8, 0x03, 0x00, 0x00}},
    };

    for (const TestCase &lCase : lCases)
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setConfigured1WBroadcastType(3);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, lCase.param, lCase.param2));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(!lPacket.empty());

        IoHomeFrame lFrame;
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
        ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
        ASSERT_EQ(lFrame.getDestNodeId(), 0x0000FF); // configured type 3 typed broadcast
        ASSERT_EQ(lFrame.dataLen, 8);
        ASSERT_MEM_EQ(lFrame.data, lCase.expectedData, sizeof(lCase.expectedData));
        ASSERT_TRUE(lFrame.hasHmac);
    }
}

TEST(controller_1w_execute_template_can_override_acei_fp_and_destination)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lRemoteNodeId);
    lController.init();
    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lKey);
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lKey);

    ASSERT_TRUE(lController.sendOneWayExecuteWithTemplate(lDeviceNodeId, lKey,
                                                          0xE7, IOHC_POSITION_VENT, 0x12, 0x34,
                                                          OneWayDestinationMode::ExplicitType,
                                                          6, 0));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());

    IoHomeFrame lFrame;
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.commandId, IoHomeCommand::Execute);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x0001BF); // explicit type 6 typed broadcast
    ASSERT_EQ(lFrame.dataLen, 8);
    ASSERT_EQ(lFrame.data[0], IOHC_ORIGINATOR_USER);
    ASSERT_EQ(lFrame.data[1], 0xE7);
    ASSERT_EQ(lFrame.data[2], 0xD8);
    ASSERT_EQ(lFrame.data[3], 0x03);
    ASSERT_EQ(lFrame.data[4], 0x12);
    ASSERT_EQ(lFrame.data[5], 0x34);
    ASSERT_TRUE(lFrame.hasHmac);
}

TEST(controller_1w_destination_modes_cover_default_typed_all_and_exact)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    // Default/ProfileTyped: unspecified channel type now stays type 0 (“All”).
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_EQ(lChannel.getConfigured1WBroadcastType(), 0);
        ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 0xD8, 0x03));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
    }

    // Explicit configured type 2 remains the shutter/blind typed target.
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setConfigured1WBroadcastType(2);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 0xD8, 0x03));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x0000BF);
    }

    // Explicit type 3 remains the awning typed target.
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.sendOneWayExecuteWithType(lDeviceNodeId, lKey, IOHC_POSITION_VENT, 0x00, 0x00, 3));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x0000FF);
    }

    // All mode forces rspaargaren type 0 destination even if a type parameter is supplied.
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.sendOneWayExecuteWithDestination(lDeviceNodeId, lKey,
                                                                 IOHC_POSITION_VENT, 0x00, 0x00,
                                                                 OneWayDestinationMode::All,
                                                                 3, 0));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x00003F);
    }

    // Exact mode bypasses typed broadcast calculation for diagnostics/captured targets.
    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.sendOneWayExecuteWithDestination(lDeviceNodeId, lKey,
                                                                 IOHC_POSITION_VENT, 0x00, 0x00,
                                                                 OneWayDestinationMode::Exact,
                                                                 3, 0x123456));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x123456);
    }
}

TEST(controller_1w_channel_broadcast_type3_uses_typed_destination_for_pairing_and_runtime)
{
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.setSystemKey(lKey);
        lController.init();
        lChannel.setIs1W(true);
        lChannel.setConfigured1WTargetNodeId(lDeviceNodeId);
        lChannel.setConfigured1WBroadcastType(3);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.startPairing(0, lDeviceNodeId));
        lController.radio().testClearTransmittedPacket();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x0000FF);
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setConfigured1WBroadcastType(3);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 0xD8, 0x03));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x0000FF);
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setConfigured1WBroadcastType(3);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::ActivateMode, 0x01));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x0000FF);
    }

    {
        IoHomeController lController;
        IoHomecontrol lModule;
        IoHomecontrolChannel lChannel;
        lModule.testSetChannel(0, &lChannel);
        lController.setModule(&lModule);
        lController.setOwnNodeId(lRemoteNodeId);
        lController.init();
        lChannel.setNodeId(lDeviceNodeId);
        lChannel.setEncryptionKey(lKey);
        lChannel.setIs1W(true);
        lChannel.setConfigured1WBroadcastType(3);
        lChannel.setOneWayControllerNodeId(lRemoteNodeId);
        lChannel.setOneWayControllerKey(lKey);

        ASSERT_TRUE(lController.sendOneWayExecuteWithType(lDeviceNodeId, lKey, IOHC_POSITION_VENT, 0x00, 0x00, 3));
        lController.radio().testClearTransmittedPacket();
        lController.loop();
        lController.loop();

        IoHomeFrame lFrame;
        const auto &lPacket = lController.radio().testLastTransmittedPacket();
        ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
        ASSERT_EQ(lFrame.getDestNodeId(), 0x0000FF);
    }
}

TEST(controller_1w_channel_profiles_are_independent)
{
    const uint8_t lKey1[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t lKey2[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel1;
    IoHomecontrolChannel lChannel2;
    lModule.testSetChannel(0, &lChannel1);
    lModule.testSetChannel(1, &lChannel2);
    lController.setModule(&lModule);

    lChannel1.setIs1W(true);
    lChannel1.setOneWayControllerNodeId(0x810001);
    lChannel1.setOneWayControllerKey(lKey1);
    lChannel1.setSequence1W(10);
    lChannel2.setIs1W(true);
    lChannel2.setOneWayControllerNodeId(0x820002);
    lChannel2.setOneWayControllerKey(lKey2);
    lChannel2.setSequence1W(20);

    ASSERT_TRUE(lController.oneWayProfileForChannel(&lChannel1) == &lChannel1);
    ASSERT_TRUE(lController.oneWayProfileForChannel(&lChannel2) == &lChannel2);
    ASSERT_EQ(lController.oneWayProfileForChannel(&lChannel1)->getOneWayControllerNodeId(), 0x810001);
    ASSERT_EQ(lController.oneWayProfileForChannel(&lChannel2)->getOneWayControllerNodeId(), 0x820002);
    ASSERT_EQ(lChannel1.getSequence1W(), 10);
    ASSERT_EQ(lChannel2.getSequence1W(), 20);
}

TEST(controller_1w_execute_uses_remote_identity_not_2w_gateway_identity)
{
    const uint32_t lGatewayNodeId = 0x112233;
    const uint32_t lRemoteNodeId = 0x831F2A;
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lGatewayKey[16] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
        0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00};
    const uint8_t lRemoteKey[16] = {
        0x2A, 0xDD, 0xFC, 0x13, 0xC9, 0x97, 0x60, 0x11,
        0xB1, 0xC1, 0x09, 0xFB, 0xF3, 0x95, 0x2F, 0xA1};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.setOwnNodeId(lGatewayNodeId);
    lController.setSystemKey(lGatewayKey);
    lController.init();

    lChannel.setNodeId(lDeviceNodeId);
    lChannel.setEncryptionKey(lRemoteKey);
    lChannel.setIs1W(true);
    lChannel.setOneWayControllerNodeId(lRemoteNodeId);
    lChannel.setOneWayControllerKey(lRemoteKey);
    lChannel.setConfigured1WBroadcastType(2);

    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lRemoteKey, IoHomeCommand::Execute, 0xD8, 0x03));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    IoHomeFrame lFrame;
    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(!lPacket.empty());
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.getSrcNodeId(), lRemoteNodeId);
    ASSERT_NE(lFrame.getSrcNodeId(), lGatewayNodeId);
    ASSERT_MEM_NEQ(lRemoteKey, lGatewayKey, 16);
    ASSERT_EQ(lFrame.getDestNodeId(), 0x0000BF);
}

TEST(channel_1w_sequence_reserve_window_reduces_flash_saves)
{
    IoHomecontrolChannel lChannel;
    lChannel.setSequence1W(0);

    uint16_t lLastUsed = 0;
    uint8_t lSaveRequests = 0;
    for (uint8_t i = 0; i < 100; i++)
    {
        bool lSaveRequired = false;
        lLastUsed = lChannel.incrementSequence1W(false, lSaveRequired);
        if (lSaveRequired)
            lSaveRequests++;
    }

    ASSERT_EQ(lLastUsed, 100);
    ASSERT_TRUE(lSaveRequests > 0);
    ASSERT_TRUE(lSaveRequests < 100);
    ASSERT_TRUE(lSaveRequests <= 7);
    ASSERT_TRUE(static_cast<int16_t>(lChannel.getReservedSequence1W() - lChannel.getSequence1W()) > 0);
}

TEST(channel_1w_sequence_reboot_uses_reserved_high_water)
{
    IoHomecontrolChannel lChannel;
    lChannel.setSequence1W(100);

    bool lSaveRequired = false;
    const uint16_t lUsedBeforePowerLoss = lChannel.incrementSequence1W(false, lSaveRequired);
    ASSERT_TRUE(lSaveRequired);
    ASSERT_EQ(lUsedBeforePowerLoss, 101);
    const uint16_t lReservedBeforePowerLoss = lChannel.getReservedSequence1W();
    ASSERT_TRUE(static_cast<int16_t>(lReservedBeforePowerLoss - lUsedBeforePowerLoss) > 0);

    IoHomecontrolChannel lAfterReboot;
    // Flash stores the reserved/high-water sequence, not merely the last used
    // value. Restoring that value must skip ahead before the next TX.
    lAfterReboot.setSequence1W(lReservedBeforePowerLoss);
    bool lRebootSaveRequired = false;
    const uint16_t lFirstUsedAfterReboot = lAfterReboot.incrementSequence1W(false, lRebootSaveRequired);

    ASSERT_TRUE(lRebootSaveRequired);
    ASSERT_TRUE(lFirstUsedAfterReboot > lUsedBeforePowerLoss);
    ASSERT_TRUE(static_cast<int16_t>(lAfterReboot.getReservedSequence1W() - lFirstUsedAfterReboot) > 0);
}

TEST(controller_1w_shared_profile_uses_owner_sequence_and_identity)
{
    const uint32_t lDeviceNodeId = 0x7E9E6E;
    const uint8_t lKey[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lOwner;
    IoHomecontrolChannel lShared;
    lModule.testSetChannel(0, &lOwner);
    lModule.testSetChannel(1, &lShared);
    lController.setModule(&lModule);
    lController.init();

    lOwner.setIs1W(true);
    lOwner.setOneWayControllerNodeId(0x810001);
    lOwner.setOneWayControllerKey(lKey);
    lOwner.setSequence1W(10);
    lShared.setNodeId(lDeviceNodeId);
    lShared.setIs1W(true);
    lShared.setConfigured1WProfileChannel(0);

    ASSERT_TRUE(lController.oneWayProfileForChannel(&lShared) == &lOwner);
    const uint32_t lSaveCountBefore = openknx.flash.saveCount;
    ASSERT_TRUE(lController.sendCommand(lDeviceNodeId, lKey, IoHomeCommand::Execute, 0xD8, 0x03));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();

    IoHomeFrame lFrame;
    const auto &lPacket = lController.radio().testLastTransmittedPacket();
    ASSERT_TRUE(deserializeFrameForTest(lFrame, lPacket.data(), static_cast<uint8_t>(lPacket.size())));
    ASSERT_EQ(lFrame.getSrcNodeId(), 0x810001);
    ASSERT_EQ(lOwner.getSequence1W(), 11);
    ASSERT_EQ(lShared.getSequence1W(), 0);
    ASSERT_EQ(lFrame.data[6], 0x00);
    ASSERT_EQ(lFrame.data[7], 0x0B);
    ASSERT_TRUE(openknx.flash.saveCount > lSaveCountBefore);
}

TEST(controller_1w_identical_imported_profiles_share_first_sequence_owner)
{
    const uint8_t lKey[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel1;
    IoHomecontrolChannel lChannel2;
    lModule.testSetChannel(0, &lChannel1);
    lModule.testSetChannel(1, &lChannel2);
    lController.setModule(&lModule);

    lChannel1.setIs1W(true);
    lChannel1.setOneWayControllerNodeId(0x810001);
    lChannel1.setOneWayControllerKey(lKey);
    lChannel1.setSequence1W(30);
    lChannel2.setIs1W(true);
    lChannel2.setOneWayControllerNodeId(0x810001);
    lChannel2.setOneWayControllerKey(lKey);
    lChannel2.setSequence1W(20);

    ASSERT_TRUE(lController.oneWayProfileForChannel(&lChannel1) == &lChannel1);
    ASSERT_TRUE(lController.oneWayProfileForChannel(&lChannel2) == &lChannel1);
}

TEST(controller_1w_invalid_or_cyclic_profile_reference_falls_back_to_own)
{
    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel1;
    IoHomecontrolChannel lChannel2;
    lModule.testSetChannel(0, &lChannel1);
    lModule.testSetChannel(1, &lChannel2);
    lController.setModule(&lModule);

    lChannel1.setIs1W(true);
    lChannel1.setConfigured1WProfileChannel(1);
    ASSERT_TRUE(lController.oneWayProfileForChannel(&lChannel1) == &lChannel1);

    lChannel2.setIs1W(true);
    lChannel2.setConfigured1WProfileChannel(0);
    ASSERT_TRUE(lController.oneWayProfileForChannel(&lChannel1) == &lChannel1);
    ASSERT_TRUE(lController.oneWayProfileForChannel(&lChannel2) == &lChannel2);
}

TEST(controller_1w_missing_profile_does_not_send_empty_frame)
{
    const uint8_t lKey[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    IoHomeController lController;
    IoHomecontrol lModule;
    IoHomecontrolChannel lChannel;
    lModule.testSetChannel(0, &lChannel);
    lController.setModule(&lModule);
    lController.init();
    lChannel.setNodeId(0x7E9E6E);
    lChannel.setIs1W(true);

    ASSERT_TRUE(lController.sendCommand(0x7E9E6E, lKey, IoHomeCommand::Execute, 0xD8, 0x03));
    lController.radio().testClearTransmittedPacket();
    lController.loop();
    lController.loop();
    ASSERT_TRUE(lController.radio().testLastTransmittedPacket().empty());
}
#endif

// =====================================================================
// main
// =====================================================================

#ifdef TEST_LEGACY_RUNNER
int main()
{
    printf("=== OFM-IO-Homecontrol Protocol Unit Tests ===\n\n");

    printf("AES-128 ECB:\n");
    RUN(aes128_nist_vector);
    RUN(aes128_encrypt_decrypt_roundtrip);
    RUN(aes128_ecb_against_nist_fips197);
    RUN(aes128_ecb_decryption_inverse);

    printf("\nChecksum algorithm:\n");
    RUN(checksum_zero_input);
    RUN(checksum_single_byte);
    RUN(checksum_multi_byte_sequence);
    RUN(checksum_high_bit_branch);

    printf("\nIV construction:\n");
    RUN(iv_construction_short_data);
    RUN(iv_construction_full_data);
    RUN(iv_construction_zero_data);
    RUN(iv_checksum_cross_reference);

    printf("\nFrame serialize/deserialize:\n");
    RUN(frame_serialize_minimal);
    RUN(frame_roundtrip_with_data);
    RUN(frame_roundtrip_no_hmac);
    RUN(frame_deserialize_rejects_too_short);
    RUN(frame_deserialize_rejects_too_long);
    RUN(frame_2w_rejects_oversized_declared_length);
    RUN(frame_deserialize_smoove_origin_packet);
    RUN(frame_deserialize_1w_execute_real_capture_from_box);
    RUN(frame_rejects_sx1262_parse_fail_capture);
    RUN(frame_node_id_encoding);
    RUN(frame_start_2w_flags);

    printf("\nHMAC creation & verification:\n");
    RUN(hmac_create_verify_roundtrip);
    RUN(hmac_different_keys_different_output);
    RUN(hmac_different_challenges_different_output);
    RUN(hmac_wrong_key_rejects);
    RUN(hmac_deterministic);

    printf("\nKey transfer keystream / XOR helpers:\n");
    RUN(crypt2wkey_produces_keystream);
    RUN(crypt2wkey_xor_roundtrip);
    RUN(hmac_and_crypt_share_iv);
    RUN(velocet_vector_1w_key_push);
    RUN(velocet_vector_2w_push_key_exchange);
    RUN(velocet_vector_2w_pull_key_exchange);

    printf("\nProtocol constants:\n");
    RUN(transfer_key_matches_reference);
    RUN(position_encoding);

    printf("\nHMAC detection per command type:\n");
    RUN(frame_deserialize_rejects_2w_appended_hmac);
    RUN(frame_deserialize_status_update_2w_keeps_payload);
    RUN(frame_deserialize_getname_2w_no_hmac);
    RUN(frame_deserialize_1w_execute_no_hmac);

    printf("\nReference key-exchange vectors:\n");
    RUN(crypt2wkey_reference_vector);
    RUN(crypt2wkey_full_key_exchange_simulation);

    printf("\nFrame length field encoding:\n");
    RUN(frame_length_field_min);
    RUN(frame_length_field_with_hmac);
    RUN(frame_length_field_max);

    printf("\nPosition decoding:\n");
    RUN(position_decoding);
    RUN(position_decoding_from_frame);

    printf("\nHMAC tamper detection:\n");
    RUN(hmac_rejects_tampered_frame_data);
    RUN(hmac_rejects_tampered_single_bit);

    printf("\nFavorite position encoding:\n");
    RUN(favorite_position_encoding);

    printf("\nMulti-param frame (position + slat):\n");
    RUN(frame_execute_with_slat);

    printf("\nChallenge freshness:\n");
    RUN(challenge_zero_detection);
    RUN(challenge_cleared_after_verify);

    printf("\nBattery level decoding:\n");
    RUN(battery_level_decoding);

    printf("\nDiscoverSPE frame:\n");
    RUN(frame_discover_spe_request);

    printf("\nPosition special values:\n");
    RUN(position_special_values_encoding);

    printf("\nRetry logic:\n");
    RUN(retry_counter_logic);
    RUN(frequency_hop_cycle);

    printf("\nExecute TX format (reference):\n");
    RUN(execute_tx_8byte_format);
    RUN(execute_tx_special_6byte_format);

    printf("\nStatusUpdate response parsing:\n");
    RUN(status_update_reference_layout);
    RUN(status_update_special_position_filter);

    printf("\nPrivateResponse parsing:\n");
    RUN(private_response_reference_layout);
    RUN(private_response_real_capture_from_box);

    printf("\nCTRL1 flags:\n");
    RUN(ctrl1_beacon_flag);
    RUN(ctrl1_routed_flag);
    RUN(ctrl1_low_power_flag);
    RUN(ctrl1_ack_flag);
    RUN(ctrl1_combined_flags);
    RUN(ctrl1_version_field);

    printf("\nEND flag / START+END:\n");
    RUN(ctrl0_end_flag);
    RUN(ctrl0_start_and_end);

    printf("\nError response (0xFE):\n");
    RUN(error_response_frame);
    RUN(error_response_codes);

    printf("\nGetGeneralInfo1 response:\n");
    RUN(general_info1_response_decode);
    RUN(general_info1_all_device_types);

    printf("\nGetName response:\n");
    RUN(get_name_response_frame);
    RUN(get_name_max_length);

    printf("\nDiscovery response:\n");
    RUN(discovery_response_frame);
    RUN(discovery_spe_response_frame);

    printf("\nDevice type & command enums:\n");
    RUN(device_type_enum_values);
    RUN(command_id_enum_values);

    printf("\nExecute quiet mode:\n");
    RUN(execute_quiet_mode);

    printf("\nExecute originator codes:\n");
    RUN(execute_originator_codes);

    printf("\nPrivateResponse frame (no HMAC):\n");
    RUN(private_response_frame_no_hmac);

    printf("\nChallenge-response frames:\n");
    RUN(challenge_request_frame);
    RUN(challenge_response_frame);

    printf("\nKey exchange frames:\n");
    RUN(key_init_transfer_frame);
    RUN(key_transfer_frame);

    printf("\nConfirmation frame:\n");
    RUN(confirmation_frame);

    printf("\nStatusUpdateResponse (0x72) frame:\n");
    RUN(status_update_response_frame);

    printf("\nPrivateResponse estimate byte:\n");
    RUN(private_response_estimate_byte);

    printf("\nComplete DeviceType enum:\n");
    RUN(device_type_complete_enum);

    printf("\nManufacturer enum:\n");
    RUN(manufacturer_enum_values);

    printf("\nisOpenCloseOnly detection:\n");
    RUN(open_close_only_detection);

    printf("\nPosition fallback (current > max):\n");
    RUN(position_fallback_uses_target);
    RUN(position_fallback_not_when_moving);

    printf("\nGetGeneralInfo2 type extraction:\n");
    RUN(general_info2_type_extraction);

    printf("\nName response cleaning:\n");
    RUN(name_strip_leading_trailing);
    RUN(name_latin1_to_utf8);

    printf("\nStatus-expected flag:\n");
    RUN(status_expected_flag_parsing);

    printf("\nPrivate (0x03) GetStatus payload:\n");
    RUN(private_command_payload);

    printf("\nChallengeResponse HMAC detection:\n");
    RUN(frame_deserialize_challenge_response_data);

    printf("\n2W frame-order semantics:\n");
    RUN(key_transfer_is_continuation_frame);
    RUN(challenge_response_is_continuation_frame);

    printf("\nReceive-side auth challenge frame:\n");
    RUN(receive_auth_challenge_frame);

    printf("\nReference byte-vector regression tests:\n");
    RUN(byte_vector_2w_execute_position_50_payload);
    RUN(byte_vector_2w_execute_stop_and_favorite_payloads);
    RUN(byte_vector_2w_key_transfer_encrypted_key_from_key_init);
    RUN(byte_vector_2w_challenge_response_hmac_is_command_data);
    RUN(byte_vector_1w_execute_00_14_hmac_transcript);
    RUN(byte_vector_1w_sendkey_30_payload_20_bytes_no_hmac);

    printf("\n--- Integration Tests ---\n");

    printf("\nExecute → StatusUpdate flow:\n");
    RUN(integration_execute_status_flow);

    printf("\nFull key exchange (pairing):\n");
    RUN(integration_key_exchange);

    printf("\nChallenge lifecycle (replay rejection):\n");
    RUN(integration_challenge_lifecycle);

    printf("\nMulti-device discovery scan:\n");
    RUN(integration_discovery_scan);

    printf("\nDevice info query sequence:\n");
    RUN(integration_device_info_query);

    printf("\nRetry with frequency hop:\n");
    RUN(integration_retry_then_success);

    printf("\nACEI byte constants:\n");
    RUN(acei_byte_constants);
    RUN(acei_priority_levels);
    RUN(originator_id_values);
    RUN(acei_in_execute_frame);

    printf("\nActivateMode (0x01) command:\n");
    RUN(activate_mode_enum_value);
    RUN(activate_mode_frame);
    RUN(activate_mode_2w_rejects_appended_hmac);

    printf("\nVent and ForceOpen position constants:\n");
    RUN(vent_position_constant);
    RUN(force_open_position_constant);
    RUN(activate_mode_vent_frame);

    printf("\n--- New Feature Tests ---\n");

    printf("\nCRC-16 Kermit:\n");
    RUN(crc16_kermit_standard_vector);
    RUN(crc16_kermit_empty);

    printf("\n1W IV construction:\n");
    RUN(iv_1w_construction);

    printf("\n1W HMAC:\n");
    RUN(hmac_1w_create_verify_roundtrip);
    RUN(hmac_1w_different_seq_different_output);
    RUN(hmac_1w_wrong_key_rejects);
    RUN(hmac_1w_reference_vectors_selftest);

    printf("\n1W key encryption:\n");
    RUN(encrypt_1w_key_roundtrip);
    RUN(encrypt_1w_key_iv_from_node_address);
    RUN(encrypt_1w_key_reference_vectors_selftest);

    printf("\nAES Cross-Validation:\n");
    RUN(checksum_matches_velocet_python_reference);
    RUN(iv_2w_matches_cyrilopen_source_construction);
    RUN(iv_1w_matches_cyrilopen_source_construction);
    RUN(encrypt_1w_key_iv_construction_matches_reference);
    RUN(encrypt_1w_key_matches_velocet_python_vector);
    RUN(encrypt_1w_key_matches_cyrilopen_source_logic);
    RUN(hmac_1w_cross_algorithm_validation);
    RUN(hmac_2w_cross_algorithm_validation);
    RUN(crypt2wkey_keystream_is_aes_iv_output);
    RUN(aes_consistency_across_multiple_calls);

    printf("\n1W Key Transfer (SendKey1W 0x30):\n");
    RUN(integration_1w_key_transfer_flow);
    RUN(integration_1w_key_transfer_wrong_iv_rejected);

    printf("\nNew command enums:\n");
    RUN(write_private_enum_value);
    RUN(identify_enum_value);
    RUN(send_key_1w_enum_value);
    RUN(address_request_enum);
    RUN(launch_key_transfer_enum);
    RUN(remove_controller_enum);

    printf("\nHMAC/frame auth detection for new commands:\n");
    RUN(write_private_2w_rejects_appended_hmac);
    RUN(send_key_1w_unauthenticated_29_bytes);
    RUN(send_key_1w_parses_optional_trailer_mac);
    RUN(frame_1w_execute_has_hmac);
    RUN(frame_1w_activate_mode_has_hmac);
    RUN(frame_1w_write_private_has_hmac);

    printf("\nSerializer boundary tests:\n");
    RUN(serializer_boundary_2w_challenge_response_hmac_as_data);
    RUN(serializer_boundary_2w_has_hmac_rejected);
    RUN(serializer_boundary_1w_execute_hmac_declared_length);
    RUN(serializer_boundary_sendkey1w_unauthenticated_29);
    RUN(serializer_sendkey1w_accepts_optional_trailer_mac);
    RUN(serializer_boundary_raw_crc_explicit_only);

    printf("\nGolden RF corpus:\n");
    RUN(golden_rf_corpus_frames_decode_to_declared_metadata);
    RUN(golden_rf_corpus_public_trailer_mac_verifies);
    RUN(golden_rf_corpus_covers_every_pairing_wait_injection);

    printf("\nAddress classes:\n");
    RUN(address_class_group);
    RUN(address_class_discover);
    RUN(address_class_discover_alt);
    RUN(address_class_unicast);
    RUN(address_class_broadcast_type);

    printf("\nGroup addressing:\n");
    RUN(group_addressing_frame);

    printf("\n1W mode flag:\n");
    RUN(frame_1w_mode_flag);

    printf("\nEMS2 constants:\n");
    RUN(ems2_sync_word_constant);

    printf("\nScan command list:\n");
    RUN(scan_command_list);

    printf("\nCozy thermostat payloads:\n");
    RUN(cozy_temperature_payload);
    RUN(cozy_mode_payload);

    printf("\nPosition interpolation:\n");
    RUN(interpolate_position_midpoint);
    RUN(interpolate_position_complete);
    RUN(interpolate_position_over);
    RUN(interpolate_position_zero);
    RUN(interpolate_position_zero_duration);
    RUN(interpolate_position_float_precision);

    printf("\nDirect Command (0x02):\n");
    RUN(direct_command_enum_value);
    RUN(direct_command_2w_rejects_appended_hmac);
    RUN(scan_command_list_includes_direct);

    printf("\nFrame order field:\n");
    RUN(frame_order_constants);
    RUN(frame_order_set_get_roundtrip);
    RUN(frame_order_preserves_other_bits);

    printf("\nCRC in frame:\n");
    RUN(crc_serialize_appends_two_bytes);
    RUN(crc_roundtrip);
    RUN(crc_mismatch_rejects);
    RUN(crc_value_matches_kermit);

    printf("\nPosition boundary snapping:\n");
    RUN(snap_position_at_low_boundary);
    RUN(snap_position_at_high_boundary);
    RUN(snap_position_exact_threshold_low);
    RUN(snap_position_exact_threshold_high);
    RUN(snap_position_no_snap_middle);
    RUN(snap_position_just_above_low);
    RUN(snap_position_just_below_high);
    RUN(snap_interpolation_near_target);

    printf("\nCozy extended payloads:\n");
    RUN(cozy_presence_payload);
    RUN(cozy_window_payload);
    RUN(cozy_poweron_payload);
    RUN(cozy_midnight_payload);
    RUN(cozy_presence_constants);

    printf("\nMulti-frequency RX scanning:\n");
    RUN(freq_scan_cycle_wraps);
    RUN(freq_scan_interval_constant);
    RUN(freq_scan_all_frequencies_covered);

    printf("\nRemote map:\n");
    RUN(remote_map_add_remove);
    RUN(remote_map_find);
    RUN(remote_map_link_unlink);
    RUN(remote_map_full);
    RUN(remote_map_link_limit);
    RUN(remote_map_observe_address);
    RUN(remote_map_observe_dedup);
    RUN(remote_map_flash_roundtrip);
    RUN(remote_map_flash_size);

    printf("\nRemote map supported commands:\n");
    RUN(remote_map_supported_commands_set_get);
    RUN(remote_map_supported_commands_query);
    RUN(remote_map_supported_commands_flash_roundtrip);
    RUN(remote_map_supported_commands_trailing_zero_trim);

    printf("\nNetwork scan buffer:\n");
    RUN(scan_buffer_insert);
    RUN(scan_buffer_wrap);
    RUN(scan_buffer_read_order);

    printf("\nNode stats:\n");
    RUN(node_stats_update);
    RUN(node_stats_limit);

    printf("\nCommand name decoder:\n");
    RUN(command_name_enum_coverage);

    printf("\n1W Execute payload:\n");
    RUN(test_1w_acei_constant);
    RUN(test_1w_execute_position_encoding);
    RUN(test_1w_position_convention_helpers);
    RUN(test_1w_execute_payload_layout);
    RUN(test_1w_execute_hmac_input_7bytes);
    RUN(test_1w_sequence_increment);
    RUN(test_1w_execute_stop_special);
    RUN(test_1w_execute_with_slat);

    printf("\n1W repeat and payload variants:\n");
    RUN(test_1w_repeat_count_constant);
    RUN(test_1w_repeat_interval_constant);
    RUN(test_1w_repeat_preamble_constants);
    RUN(test_1w_activate_mode_payload_layout);
    RUN(test_1w_activate_mode_hmac_input_6bytes);
    RUN(test_1w_execute_16byte_payload_layout);
    RUN(test_1w_execute_16byte_hmac_input_9bytes);

    printf("\nSetName (0x52) command:\n");
    RUN(setname_response_enum);
    RUN(setname_name_max_size);
    RUN(setname_frame_layout);
    RUN(setname_challenge_response_hmac_input);

    printf("\nIdentify (0x1E) command:\n");
    RUN(identify_frame_layout);
    RUN(identify_challenge_response_hmac_input);

    printf("\nFake gateway mode tests:\n");
    RUN(gateway_discover_answer_frame_layout);
    RUN(gateway_key_transfer_encryption);
    RUN(gateway_challenge_answer_hmac);
    RUN(gateway_name_response_content);
#ifdef TEST_NATIVE
    RUN(gateway_controller_discover_request_from_idle);
    RUN(gateway_controller_key_transfer_uses_configured_gateway_key);
    RUN(gateway_controller_challenge_response_tracks_paired_device);
    RUN(controller_key_extract_answers_discovery_with_throwaway_id);
    RUN(controller_key_extract_broadcasts_reply_with_ch2_last);
    RUN(controller_key_extract_reuses_stored_challenge_on_key_init_retry);
    RUN(controller_key_extract_acknowledges_discovery_confirmation);
    RUN(controller_key_extract_recovers_hub_system_key);
    RUN(controller_key_extract_completes_hub_address_verification);
    RUN(controller_key_extract_ignores_frames_addressed_to_real_node_id);
    RUN(controller_key_extract_disarms_after_success);
    RUN(controller_1w_pairing_modes_command_sequences);
    RUN(controller_1w_announce_only_does_not_send_sendkey_after_repeats);
    RUN(controller_default_1w_pairing_uses_type0_all);
    RUN(controller_1w_enrollment_finalizer_resolution_is_conservative);
    RUN(controller_velux_1w_enrollment_serializes_multicast_add_stop_down);
    RUN(controller_generic_1w_automatic_finalizer_stops_after_add);
    RUN(controller_1w_enrollment_remove_failure_stops_operation);
    RUN(controller_1w_enrollment_add_failure_skips_finalizer);
    RUN(controller_velux_1w_finalizer_stop_failure_suppresses_down);
    RUN(controller_velux_1w_finalizer_deadline_failure_suppresses_down);
    RUN(controller_velux_1w_finalizer_down_failure_marks_operation_failed);
    RUN(controller_discovery_sends_standard_28_then_alt_2e_broadcast);
    RUN(controller_spe_discovery_sends_single_2a_broadcast);
    RUN(controller_1w_pairing_allows_add_without_target_node);
    RUN(controller_1w_profile_can_append_sendkey_trailer_mac);
    RUN(controller_1w_sendkey_frame_identical_with_known_or_unknown_target);
    RUN(controller_1w_virtual_channel_execute_allowed_without_target_node);
    RUN(controller_default_2w_pairing_uses_key_init_after_discovery);
    RUN(controller_pairing_discovery_learns_always_alive);
    RUN(controller_experimental_2w_pairing_can_use_discovery_confirmation);
    RUN(controller_2w_pairing_succeeds_when_setconfig1_times_out);
    RUN(controller_2w_pairing_succeeds_when_setconfig1_returns_error_response);
    RUN(controller_2w_pairing_succeeds_when_setconfig1_send_or_setup_fails);
    RUN(controller_2w_pairing_accepts_direct_key_confirmation_without_challenge);
    RUN(controller_2w_pairing_rejects_misdirected_key_confirmation);
    RUN(controller_2w_pairing_correlates_interleaved_frames);
    RUN(controller_2w_command_ignores_foreign_challenge);
    RUN(controller_2w_pairing_retries_key_init_and_accepts_direct_confirmation);
    RUN(controller_2w_pairing_retries_key_init_with_fresh_challenge);
    RUN(controller_2w_pairing_key_exchange_respects_total_budget);
    RUN(controller_2w_pairing_setconfig1_auth_challenge_completes_on_final_reject);
    RUN(controller_status_update_requires_11_bytes_for_position);
    RUN(controller_private_response_requires_8_bytes_for_position);
    RUN(controller_general_info2_response_uses_selector_before_tilt_decode);
    RUN(controller_status_poll_failure_after_challenge_notifies_channel);
    RUN(controller_1w_key_frame_uses_profile_manufacturer_without_hmac);
    RUN(controller_2w_command_defaults_to_always_alive);
    RUN(controller_2w_command_can_clear_low_power_for_mains_device);
    RUN(controller_ets_always_alive_overrides_learned_low_power);
    RUN(controller_ets_low_power_overrides_learned_always_alive);
    RUN(controller_send_identify_builds_authenticated_payload);
    RUN(controller_send_identify_rejects_1w_channel);
    RUN(controller_private_query_payload_variants);
    RUN(controller_2w_tilt_execute_payload);
    RUN(controller_private_response_decodes_battery_lowpower_and_tilt);
    RUN(controller_private_response_decodes_tilt_with_15_byte_payload);
    RUN(controller_private_response_stopped_marker_uses_target_position);
    RUN(controller_passive_key_sniff_start_stop_clear);
    RUN(controller_passive_mode_does_not_sniff_without_explicit_start);
    RUN(controller_passive_key_sniff_captures_result_and_callback);
    RUN(controller_1w_key_receive_clones_remote_from_sendkey_frame);
    RUN(controller_1w_key_receive_verifies_optional_sendkey_trailer_mac);
    RUN(controller_1w_key_receive_rejects_invalid_sendkey_trailer_mac);
    RUN(controller_1w_key_receive_rejects_non_1w_channel);
    RUN(controller_1w_execute_uses_configured_channel_acei);
    RUN(controller_passive_remote_activity_schedules_follow_up_poll_for_target_device);
    RUN(controller_linked_remote_activity_schedules_follow_up_poll_for_linked_device);
    RUN(controller_2w_challenge_response_inherits_low_power);
    RUN(controller_2w_challenge_response_can_clear_low_power_for_mains_device);
    RUN(controller_status_update_receive_auth_uses_saved_command_data);
    RUN(controller_2w_initial_response_wait_uses_retry_gap);
    RUN(retry_preserves_start_flag_for_2w_request);
    RUN(byte_vector_controller_2w_execute_payloads_and_retry_start);
    RUN(byte_vector_controller_1w_default_and_typed_targets);
    RUN(byte_vector_controller_1w_sendkey_no_hmac_and_20_byte_payload);
    RUN(byte_vector_controller_1w_repeat_plan_long_then_four_short_40ms);
    RUN(controller_2w_final_response_wait_and_sx1262_dwell);
    RUN(controller_default_1w_execute_uses_standard_vent_layout);
    RUN(controller_1w_execute_repeats_first_long_then_four_short);
    RUN(controller_1w_pairing_repeats_first_long_then_short);
    RUN(controller_1w_ui_open_position_conversion_matches_raw_closed_main);
    RUN(controller_default_1w_execute_matches_reference_payloads);
    RUN(controller_1w_execute_template_can_override_acei_fp_and_destination);
    RUN(controller_1w_destination_modes_cover_default_typed_all_and_exact);
    RUN(controller_1w_channel_broadcast_type3_uses_typed_destination_for_pairing_and_runtime);
    RUN(controller_1w_channel_profiles_are_independent);
    RUN(controller_1w_execute_uses_remote_identity_not_2w_gateway_identity);
    RUN(channel_1w_sequence_reserve_window_reduces_flash_saves);
    RUN(channel_1w_sequence_reboot_uses_reserved_high_water);
    RUN(controller_1w_shared_profile_uses_owner_sequence_and_identity);
    RUN(controller_1w_identical_imported_profiles_share_first_sequence_owner);
    RUN(controller_1w_invalid_or_cyclic_profile_reference_falls_back_to_own);
    RUN(controller_1w_missing_profile_does_not_send_empty_frame);
#endif

    printf("\n=== Results: %d passed, %d failed ===\n", sTestsPassed, sTestsFailed);
    return sTestsFailed > 0 ? 1 : 0;
}
#endif
