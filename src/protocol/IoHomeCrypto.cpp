#include "IoHomeCrypto.h"
#include <string.h>

#ifdef ESP32
#include "mbedtls/aes.h"
#elif defined(TEST_NATIVE)
// Real AES implementation provided by test harness
#include "mbedtls/aes.h"
#else
// Stub for non-ESP32 compilation (passthrough - not suitable for protocol testing)
struct mbedtls_aes_context
{
    uint8_t key[16];
};
static void mbedtls_aes_init(mbedtls_aes_context *) {}
static void mbedtls_aes_free(mbedtls_aes_context *) {}
static int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const uint8_t *key, unsigned int)
{
    memcpy(ctx->key, key, 16);
    return 0;
}
static int mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx, const uint8_t *key, unsigned int)
{
    memcpy(ctx->key, key, 16);
    return 0;
}
#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0
static int mbedtls_aes_crypt_ecb(mbedtls_aes_context *, int, const uint8_t *input, uint8_t *output)
{
    memcpy(output, input, 16); // passthrough for testing
    return 0;
}
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

    bool createHmac1W(const uint8_t *iFrameData, size_t iDataLen,
                      uint16_t iSequenceNum, const uint8_t iControllerKey[16],
                      uint8_t oHmac[6])
    {
        uint8_t lIv[16];
        constructIv1W(iFrameData, iDataLen, iSequenceNum, lIv);

        uint8_t lEncrypted[16];
        if (!aes128Encrypt(lIv, iControllerKey, lEncrypted))
            return false;

        memcpy(oHmac, lEncrypted, 6);
        return true;
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
        // Build IV: repeat 3-byte node address to fill 16 bytes
        // {n0,n1,n2, n0,n1,n2, n0,n1,n2, n0,n1,n2, n0,n1,n2, n0}
        uint8_t lIv[16];
        for (int i = 0; i < 16; i++)
            lIv[i] = iNodeAddress[i % 3];

        // CFB128: encrypt IV, XOR with key
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

} // namespace IoHomeCrypto
