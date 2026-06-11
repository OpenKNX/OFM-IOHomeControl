#include "IoHomeCrypto.h"
#include <string.h>

#if defined(ESP32) || defined(TEST_NATIVE)
// Real AES is mandatory for io-homecontrol crypto.
// ESP32 provides mbedTLS via the SDK; host protocol tests must define
// TEST_NATIVE and link against mbedTLS as well.
#include "mbedtls/aes.h"
#else
#error "Real AES required for IOHC protocol tests. Define TEST_NATIVE with mbedTLS or build for ESP32."
#endif

#ifdef ESP32
#include "esp_random.h"
#else
#include <stdlib.h>
#endif

namespace IoHomeCrypto
{

    uint16_t crc16Kermit(const uint8_t *iData, size_t iLen)
    {
        uint16_t lCrc = 0x0000;
        for (size_t i = 0; i < iLen; i++)
        {
            lCrc ^= iData[i];
            for (int j = 0; j < 8; j++)
            {
                if (lCrc & 1)
                    lCrc = (lCrc >> 1) ^ 0x8408;
                else
                    lCrc >>= 1;
            }
        }
        return lCrc;
    }

    void computeChecksum(uint8_t iFrameByte, uint8_t &ioChksum1, uint8_t &ioChksum2)
    {
        uint8_t lTmp = iFrameByte ^ ioChksum2;
        ioChksum2 = ((ioChksum1 & 0x7F) << 1) & 0xFF;

        if ((ioChksum1 & 0x80) == 0)
        {
            if (lTmp >= 128)
                ioChksum2 |= 1;
            ioChksum1 = ioChksum2;
            ioChksum2 = (lTmp << 1) & 0xFF;
            return;
        }

        if (lTmp >= 128)
            ioChksum2 |= 1;
        ioChksum1 = ioChksum2 ^ 0x55;
        ioChksum2 = ((lTmp << 1) ^ 0x5B) & 0xFF;
    }

    void generateChallenge(uint8_t oChallenge[6])
    {
#ifdef ESP32
        uint32_t r1 = esp_random();
        uint32_t r2 = esp_random();
        oChallenge[0] = (r1 >> 0) & 0xFF;
        oChallenge[1] = (r1 >> 8) & 0xFF;
        oChallenge[2] = (r1 >> 16) & 0xFF;
        oChallenge[3] = (r1 >> 24) & 0xFF;
        oChallenge[4] = (r2 >> 0) & 0xFF;
        oChallenge[5] = (r2 >> 8) & 0xFF;
#else
        for (int i = 0; i < 6; i++)
            oChallenge[i] = rand() & 0xFF;
#endif
    }

    void constructIv2W(const uint8_t *iFrameData, size_t iDataLen,
                       const uint8_t iChallenge[6], uint8_t oIv[16])
    {
        memset(oIv, 0x55, 16); // pad with 0x55

        // Bytes 0-7: frame data (up to 8 bytes)
        size_t lCopyLen = (iDataLen < 8) ? iDataLen : 8;
        memcpy(oIv, iFrameData, lCopyLen);

        // Bytes 8-9: proprietary checksums
        uint8_t lChk1 = 0, lChk2 = 0;
        for (size_t i = 0; i < iDataLen; i++)
            computeChecksum(iFrameData[i], lChk1, lChk2);
        oIv[8] = lChk1;
        oIv[9] = lChk2;

        // Bytes 10-15: 6-byte challenge
        memcpy(oIv + 10, iChallenge, 6);
    }

    bool aes128Encrypt(const uint8_t iInput[16], const uint8_t iKey[16], uint8_t oOutput[16])
    {
        mbedtls_aes_context lCtx;
        mbedtls_aes_init(&lCtx);
        int lRet = mbedtls_aes_setkey_enc(&lCtx, iKey, 128);
        if (lRet != 0)
        {
            mbedtls_aes_free(&lCtx);
            return false;
        }
        lRet = mbedtls_aes_crypt_ecb(&lCtx, MBEDTLS_AES_ENCRYPT, iInput, oOutput);
        mbedtls_aes_free(&lCtx);
        return (lRet == 0);
    }

    bool aes128Decrypt(const uint8_t iInput[16], const uint8_t iKey[16], uint8_t oOutput[16])
    {
        mbedtls_aes_context lCtx;
        mbedtls_aes_init(&lCtx);
        int lRet = mbedtls_aes_setkey_dec(&lCtx, iKey, 128);
        if (lRet != 0)
        {
            mbedtls_aes_free(&lCtx);
            return false;
        }
        lRet = mbedtls_aes_crypt_ecb(&lCtx, MBEDTLS_AES_DECRYPT, iInput, oOutput);
        mbedtls_aes_free(&lCtx);
        return (lRet == 0);
    }

    bool derive2WKeystream(const uint8_t *iFrameData, size_t iDataLen,
                           const uint8_t iChallenge[6], const uint8_t iAesKey[16],
                           uint8_t oKeystream[16])
    {
        if (!iFrameData || !iChallenge || !iAesKey || !oKeystream)
            return false;

        // Construct IV from frame data and challenge.
        uint8_t lIv[16];
        constructIv2W(iFrameData, iDataLen, iChallenge, lIv);

        // Encrypt IV with the selected AES key to get the 2W keystream.
        return aes128Encrypt(lIv, iAesKey, oKeystream);
    }

    bool crypt2WKeyXor(const uint8_t *iFrameData, size_t iDataLen,
                       const uint8_t iChallenge[6], const uint8_t iInputKey[16],
                       const uint8_t iXorAesKey[16], uint8_t oOutputKey[16])
    {
        if (!iInputKey || !oOutputKey)
            return false;

        uint8_t lKeystream[16];
        if (!derive2WKeystream(iFrameData, iDataLen, iChallenge, iXorAesKey, lKeystream))
            return false;

        for (uint8_t i = 0; i < 16; i++)
            oOutputKey[i] = iInputKey[i] ^ lKeystream[i];

        return true;
    }

    bool createHmac2W(const uint8_t *iFrameData, size_t iDataLen,
                      const uint8_t iChallenge[6], const uint8_t iSystemKey[16],
                      uint8_t oHmac[6])
    {
        // Construct IV
        uint8_t lIv[16];
        constructIv2W(iFrameData, iDataLen, iChallenge, lIv);

        // Encrypt IV with system key
        uint8_t lEncrypted[16];
        if (!aes128Encrypt(lIv, iSystemKey, lEncrypted))
            return false;

        // HMAC is first 6 bytes of encrypted IV
        memcpy(oHmac, lEncrypted, 6);
        return true;
    }

    bool verifyHmac(const uint8_t *iFrameData, size_t iDataLen,
                    const uint8_t iReceivedHmac[6], const uint8_t *iChallenge,
                    const uint8_t iSystemKey[16])
    {
        uint8_t lComputed[6];
        if (!createHmac2W(iFrameData, iDataLen, iChallenge, iSystemKey, lComputed))
            return false;

        // Constant-time comparison to prevent timing attacks
        uint8_t lDiff = 0;
        for (int i = 0; i < 6; i++)
            lDiff |= lComputed[i] ^ iReceivedHmac[i];
        return (lDiff == 0);
    }

    // --- 1W protocol functions ---

    void constructIv1W(const uint8_t *iFrameData, size_t iDataLen,
                       uint16_t iSequenceNum, uint8_t oIv[16])
    {
        memset(oIv, 0x55, 16); // pad with 0x55

        // Bytes 0-7: frame data (up to 8 bytes)
        size_t lCopyLen = (iDataLen < 8) ? iDataLen : 8;
        memcpy(oIv, iFrameData, lCopyLen);

        // Bytes 8-9: proprietary checksums
        uint8_t lChk1 = 0, lChk2 = 0;
        for (size_t i = 0; i < iDataLen; i++)
            computeChecksum(iFrameData[i], lChk1, lChk2);
        oIv[8] = lChk1;
        oIv[9] = lChk2;

        // Bytes 10-11: sequence number big-endian
        oIv[10] = (iSequenceNum >> 8) & 0xFF;
        oIv[11] = iSequenceNum & 0xFF;

        // Bytes 12-15: remain 0x55 padding
    }

    bool createHmac1WWithIv(const uint8_t *iFrameData, size_t iDataLen,
                            uint16_t iSequenceNum, const uint8_t iControllerKey[16],
                            uint8_t oIv[16], uint8_t oHmac[6])
    {
        if (!iFrameData || !iControllerKey || !oIv || !oHmac)
            return false;

        constructIv1W(iFrameData, iDataLen, iSequenceNum, oIv);

        uint8_t lEncrypted[16];
        if (!aes128Encrypt(oIv, iControllerKey, lEncrypted))
            return false;

        memcpy(oHmac, lEncrypted, 6);
        return true;
    }

    bool createHmac1W(const uint8_t *iFrameData, size_t iDataLen,
                      uint16_t iSequenceNum, const uint8_t iControllerKey[16],
                      uint8_t oHmac[6])
    {
        uint8_t lIv[16];
        return createHmac1WWithIv(iFrameData, iDataLen, iSequenceNum,
                                  iControllerKey, lIv, oHmac);
    }

    bool verifyHmac1W(const uint8_t *iFrameData, size_t iDataLen,
                      uint16_t iSequenceNum, const uint8_t iReceivedHmac[6],
                      const uint8_t iControllerKey[16])
    {
        uint8_t lComputed[6];
        if (!createHmac1W(iFrameData, iDataLen, iSequenceNum, iControllerKey, lComputed))
            return false;

        uint8_t lDiff = 0;
        for (int i = 0; i < 6; i++)
            lDiff |= lComputed[i] ^ iReceivedHmac[i];
        return (lDiff == 0);
    }

    bool encrypt1WKey(const uint8_t iKey[16], const uint8_t iTransferKey[16],
                      const uint8_t iNodeAddress[3], uint8_t oEncrypted[16])
    {
        // Build IV: repeat the 3-byte 1W remote/controller node address to fill 16 bytes.
        // {n0,n1,n2, n0,n1,n2, n0,n1,n2, n0,n1,n2, n0,n1,n2, n0}
        // This is the iv=remote-node rule used by rspaargaren's ESP32 AES-CFB128 path.
        // Do not pass the actuator/discovered node here for SendKey1W/Add.
        uint8_t lIv[16];
        for (int i = 0; i < 16; i++)
            lIv[i] = iNodeAddress[i % 3];

        // ESP32 AES-CFB128 with iv_offset=0 on exactly one 16-byte block is:
        // encryptedKey = clearKey XOR AES128_encrypt(repeatedNodeIv, transferKey).
        uint8_t lKeystream[16];
        if (!aes128Encrypt(lIv, iTransferKey, lKeystream))
            return false;

        for (int i = 0; i < 16; i++)
            oEncrypted[i] = iKey[i] ^ lKeystream[i];
        return true;
    }

    bool decrypt1WKey(const uint8_t iEncrypted[16], const uint8_t iTransferKey[16],
                      const uint8_t iNodeAddress[3], uint8_t oKey[16])
    {
        // Decrypt is identical to encrypt for single-block CFB128 (same XOR operation)
        return encrypt1WKey(iEncrypted, iTransferKey, iNodeAddress, oKey);
    }

    namespace
    {
        bool bytesEqual(const uint8_t *iA, const uint8_t *iB, size_t iLen)
        {
            uint8_t lDiff = 0;
            for (size_t i = 0; i < iLen; i++)
                lDiff |= iA[i] ^ iB[i];
            return lDiff == 0;
        }
    }

    bool selfTest1WKeyEncryptionVectors()
    {
        // Vector from rspaargaren extras/1W.json sample entry b60d1a.
        // It locks the SendKey1W/Add rule: IV address = 1W remote/controller node.
        static const uint8_t kTransferKey[16] = {
            0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
            0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73};
        static const uint8_t kRemoteNode[3] = {0xB6, 0x0D, 0x1A};
        static const uint8_t kClearKey[16] = {
            0x49, 0x56, 0x38, 0x73, 0x41, 0x7A, 0x63, 0x33,
            0x4E, 0x7A, 0x63, 0x6B, 0x52, 0x64, 0x48, 0x53};
        static const uint8_t kExpectedIv[16] = {
            0xB6, 0x0D, 0x1A, 0xB6, 0x0D, 0x1A, 0xB6, 0x0D,
            0x1A, 0xB6, 0x0D, 0x1A, 0xB6, 0x0D, 0x1A, 0xB6};
        static const uint8_t kExpectedEncryptedKey[16] = {
            0x2D, 0x36, 0xBD, 0x8B, 0x4D, 0x4F, 0xB1, 0xE1,
            0xA1, 0xB3, 0x09, 0x9B, 0x39, 0x4D, 0x3A, 0x9E};

        uint8_t lIv[16];
        for (int i = 0; i < 16; i++)
            lIv[i] = kRemoteNode[i % 3];
        if (!bytesEqual(lIv, kExpectedIv, sizeof(kExpectedIv)))
            return false;

        // Explicit CFB128(single-block) equivalence check: AES(IV) XOR clearKey.
        uint8_t lKeystream[16];
        if (!aes128Encrypt(lIv, kTransferKey, lKeystream))
            return false;
        uint8_t lCfbEquivalent[16];
        for (int i = 0; i < 16; i++)
            lCfbEquivalent[i] = kClearKey[i] ^ lKeystream[i];
        if (!bytesEqual(lCfbEquivalent, kExpectedEncryptedKey, sizeof(kExpectedEncryptedKey)))
            return false;

        uint8_t lEncrypted[16];
        if (!encrypt1WKey(kClearKey, kTransferKey, kRemoteNode, lEncrypted))
            return false;
        if (!bytesEqual(lEncrypted, kExpectedEncryptedKey, sizeof(kExpectedEncryptedKey)))
            return false;

        uint8_t lDecrypted[16];
        if (!decrypt1WKey(lEncrypted, kTransferKey, kRemoteNode, lDecrypted))
            return false;
        return bytesEqual(lDecrypted, kClearKey, sizeof(kClearKey));
    }

    bool selfTest1WReferenceVectors()
    {
        // These vectors lock the rspaargaren/Velocet 1W rule:
        // IV input is command transcript only.  It does not include ctrl0/ctrl1,
        // source, destination, appended sequence bytes or appended HMAC bytes.
        static const uint8_t kKey[16] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

        struct Vector
        {
            const char *name;
            const uint8_t *transcript;
            uint8_t transcriptLen;
            uint16_t sequence;
            uint8_t expectedIv[16];
            uint8_t expectedHmac[6];
        };

        static const uint8_t kPair2E[] = {0x2E, 0x00};
        static const uint8_t kRemove39[] = {0x39, 0x00};
        static const uint8_t kExecOpen[] = {0x00, 0x01, 0x43, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t kExecStop[] = {0x00, 0x01, 0x43, 0xD2, 0x00, 0x00, 0x00};
        static const uint8_t kMode1[] = {0x01, 0x01, 0x43, 0x05, 0x00, 0x11};

        static const Vector kVectors[] = {
            {"pair_2e00", kPair2E, sizeof(kPair2E), 0x1234,
             {0x2E, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x00, 0xB8, 0x12, 0x34, 0x55, 0x55, 0x55, 0x55},
             {0xDE, 0x69, 0x31, 0xC2, 0x49, 0x48}},
            {"remove_3900", kRemove39, sizeof(kRemove39), 0x1234,
             {0x39, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x00, 0xE4, 0x12, 0x34, 0x55, 0x55, 0x55, 0x55},
             {0x00, 0x00, 0x76, 0xFC, 0x28, 0x9C}},
            {"execute_open", kExecOpen, sizeof(kExecOpen), 0x1234,
             {0x00, 0x01, 0x43, 0x00, 0x00, 0x00, 0x00, 0x55, 0x08, 0x20, 0x12, 0x34, 0x55, 0x55, 0x55, 0x55},
             {0x55, 0x92, 0xF0, 0x26, 0xA2, 0xEC}},
            {"execute_stop", kExecStop, sizeof(kExecStop), 0x1234,
             {0x00, 0x01, 0x43, 0xD2, 0x00, 0x00, 0x00, 0x55, 0x05, 0x00, 0x12, 0x34, 0x55, 0x55, 0x55, 0x55},
             {0xFA, 0x9B, 0xD2, 0x86, 0xF0, 0x75}},
            {"mode1", kMode1, sizeof(kMode1), 0x1234,
             {0x01, 0x01, 0x43, 0x05, 0x00, 0x11, 0x55, 0x55, 0x04, 0x5A, 0x12, 0x34, 0x55, 0x55, 0x55, 0x55},
             {0x7A, 0x5A, 0xDB, 0x9E, 0x5B, 0xA0}},
        };

        for (const Vector &lVector : kVectors)
        {
            uint8_t lIv[16];
            uint8_t lHmac[6];

            constructIv1W(lVector.transcript, lVector.transcriptLen,
                          lVector.sequence, lIv);
            if (!bytesEqual(lIv, lVector.expectedIv, sizeof(lVector.expectedIv)))
                return false;

            if (!createHmac1WWithIv(lVector.transcript, lVector.transcriptLen,
                                    lVector.sequence, kKey, lIv, lHmac))
                return false;
            if (!bytesEqual(lIv, lVector.expectedIv, sizeof(lVector.expectedIv)))
                return false;
            if (!bytesEqual(lHmac, lVector.expectedHmac, sizeof(lVector.expectedHmac)))
                return false;
        }

        return selfTest1WKeyEncryptionVectors();
    }

} // namespace IoHomeCrypto
