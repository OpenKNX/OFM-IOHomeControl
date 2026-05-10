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

    // Encrypt/decrypt using 2W key derivation (construct IV + AES-128 ECB)
    // Returns the keystream derived from frame data, challenge, and key
    bool crypt2WKey(const uint8_t *iFrameData, size_t iDataLen,
                    const uint8_t iChallenge[6], const uint8_t iKey[16], uint8_t oOutput[16]);

    // Create 6-byte HMAC for frame authentication
    bool createHmac2W(const uint8_t *iFrameData, size_t iDataLen,
                      const uint8_t iChallenge[6], const uint8_t iSystemKey[16],
                      uint8_t oHmac[6]);

    // Verify received HMAC against computed HMAC
    bool verifyHmac(const uint8_t *iFrameData, size_t iDataLen,
                    const uint8_t iReceivedHmac[6], const uint8_t *iChallenge,
                    const uint8_t iSystemKey[16]);

    // 1W HMAC: create and verify using sequence number instead of challenge
    bool createHmac1W(const uint8_t *iFrameData, size_t iDataLen,
                      uint16_t iSequenceNum, const uint8_t iControllerKey[16],
                      uint8_t oHmac[6]);

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
