#pragma once
#include <stdint.h>
#include <stddef.h>

// io-homecontrol cryptography functions
// Adapted from https://github.com/nicolas5000/io-rts-esp32
// Uses mbedtls (bundled with ESP32 SDK) for AES-128 ECB

namespace IoHomeCrypto
{
    // CRC-16 Kermit (CCITT reflected, poly 0x8408, init 0)
    uint16_t crc16Kermit(const uint8_t *iData, size_t iLen);

    // Proprietary checksum used in IV construction
    void computeChecksum(uint8_t iFrameByte, uint8_t &ioChksum1, uint8_t &ioChksum2);

    // Generate 6-byte random challenge for authentication
    void generateChallenge(uint8_t oChallenge[6]);

    // Construct 16-byte IV for 2W encryption
    // Uses frame data (up to 8 bytes, padded with 0x55), checksums, and challenge
    void constructIv2W(const uint8_t *iFrameData, size_t iDataLen,
                       const uint8_t iChallenge[6], uint8_t oIv[16]);

    // Construct 16-byte IV for 1W encryption
    // Uses frame data (up to 8 bytes, padded with 0x55), checksums, and sequence number
    void constructIv1W(const uint8_t *iFrameData, size_t iDataLen,
                       uint16_t iSequenceNum, uint8_t oIv[16]);

    // AES-128 ECB encrypt a single 16-byte block
    bool aes128Encrypt(const uint8_t iInput[16], const uint8_t iKey[16], uint8_t oOutput[16]);

    // AES-128 ECB decrypt a single 16-byte block
    bool aes128Decrypt(const uint8_t iInput[16], const uint8_t iKey[16], uint8_t oOutput[16]);

    // Derive the 2W AES keystream from the auth/key transcript and peer challenge.
    // This returns only the AES-ECB(IV) keystream; it does not XOR payload/key bytes.
    bool derive2WKeystream(const uint8_t *iFrameData, size_t iDataLen,
                           const uint8_t iChallenge[6], const uint8_t iAesKey[16],
                           uint8_t oKeystream[16]);

    // Encrypt/decrypt a 16-byte 2W key by XORing it with the derived keystream.
    // io-homecontrol key transfer is symmetric: calling this again with the same
    // transcript/challenge/xor AES key recovers the original input key.
    bool crypt2WKeyXor(const uint8_t *iFrameData, size_t iDataLen,
                       const uint8_t iChallenge[6], const uint8_t iInputKey[16],
                       const uint8_t iXorAesKey[16], uint8_t oOutputKey[16]);

    // Create 6-byte HMAC for frame authentication
    bool createHmac2W(const uint8_t *iFrameData, size_t iDataLen,
                      const uint8_t iChallenge[6], const uint8_t iSystemKey[16],
                      uint8_t oHmac[6]);

    // Verify received HMAC against computed HMAC
    bool verifyHmac(const uint8_t *iFrameData, size_t iDataLen,
                    const uint8_t iReceivedHmac[6], const uint8_t *iChallenge,
                    const uint8_t iSystemKey[16]);

    // 1W HMAC: create and verify using sequence number instead of challenge.
    // iFrameData is the command transcript only: command byte + command payload
    // before the appended 1W sequence/HMAC. Do not include RF header bytes,
    // source/destination, appended sequence bytes, or appended HMAC bytes.
    bool createHmac1W(const uint8_t *iFrameData, size_t iDataLen,
                      uint16_t iSequenceNum, const uint8_t iControllerKey[16],
                      uint8_t oHmac[6]);

    // Same as createHmac1W(), but also returns the exact IV used for AES.
    // Useful for pairdiag/capture comparison.
    bool createHmac1WWithIv(const uint8_t *iFrameData, size_t iDataLen,
                            uint16_t iSequenceNum, const uint8_t iControllerKey[16],
                            uint8_t oIv[16], uint8_t oHmac[6]);

    // Byte-exact 1W crypto regression tests against fixed reference vectors.
    bool selfTest1WReferenceVectors();

    bool verifyHmac1W(const uint8_t *iFrameData, size_t iDataLen,
                      uint16_t iSequenceNum, const uint8_t iReceivedHmac[6],
                      const uint8_t iControllerKey[16]);

    // 1W key encryption: CFB128 built from AES-128-ECB
    // IV = 3-byte node address repeated to fill 16 bytes
    bool encrypt1WKey(const uint8_t iKey[16], const uint8_t iTransferKey[16],
                      const uint8_t iNodeAddress[3], uint8_t oEncrypted[16]);
    bool decrypt1WKey(const uint8_t iEncrypted[16], const uint8_t iTransferKey[16],
                      const uint8_t iNodeAddress[3], uint8_t oKey[16]);
} // namespace IoHomeCrypto
