#pragma once
#include <stdint.h>
#include "IoHomeCommands.h"
#include "IoHomeCrypto.h"

// Frame size limits
// 2W max: 32 bytes (standard io-homecontrol 2W limit)
// 1W SendKey1W max: 9 header + 20 data + 6 HMAC = 35 bytes
#define IOHC_FRAME_MIN_SIZE 9
#define IOHC_FRAME_MAX_SIZE_2W 32
#define IOHC_FRAME_MAX_SIZE_1W 36
#define IOHC_FRAME_BUFFER_SIZE IOHC_FRAME_MAX_SIZE_1W
#define IOHC_FRAME_MAX_DATA 23
#define IOHC_HMAC_SIZE 6
#define IOHC_CRC_SIZE 2
#define IOHC_NODE_ID_SIZE 3

// Control byte 0 flags
#define IOHC_CTRL0_END 0x80
#define IOHC_CTRL0_START 0x40
#define IOHC_CTRL0_MODE_1W 0x20  // 1-way mode (RTS-like)
#define IOHC_CTRL0_LEN_MASK 0x1F // frame length - 1

// Control byte 0: frame order field (bits[7:6])
#define IOHC_CTRL0_ORDER_MASK 0xC0
#define IOHC_CTRL0_ORDER_SINGLE 0x00 // single command (no START, no END)
#define IOHC_CTRL0_ORDER_FIRST 0x40  // first in series (= START)
#define IOHC_CTRL0_ORDER_LAST 0x80   // last in parallel (= END)
#define IOHC_CTRL0_ORDER_END 0xC0    // command group end (START + END)

// Control byte 1 flags
#define IOHC_CTRL1_BEACON 0x80
#define IOHC_CTRL1_ROUTED 0x40
#define IOHC_CTRL1_LOW_POWER 0x20
#define IOHC_CTRL1_ACK 0x10
#define IOHC_CTRL1_VER_MASK 0x0F // protocol version

struct IoHomeFrame
{
    uint8_t ctrlByte0;
    uint8_t ctrlByte1;
    uint8_t destNode[IOHC_NODE_ID_SIZE]; // 3-byte destination address
    uint8_t srcNode[IOHC_NODE_ID_SIZE];  // 3-byte source address
    IoHomeCommand commandId;
    uint8_t data[IOHC_FRAME_MAX_DATA];
    uint8_t dataLen;
    uint8_t hmac[IOHC_HMAC_SIZE];
    bool hasHmac;
    uint16_t crc;
    bool hasCrc;

    // Initialize a new frame
    void init();

    // Set frame as 2W (bidirectional) START frame
    void setStart2W();
    void setLowPower(bool iLowPower = true);

    // Set source and destination node IDs
    void setSrcNode(uint32_t iNodeId);
    void setDestNode(uint32_t iNodeId);
    void setDestBroadcast();
    void setDestGroup();

    // Set 1W mode (RTS-like, one-way)
    void set1WMode();

    // Get/set frame order field (bits[7:6] of ctrl byte 0)
    uint8_t getFrameOrder() const;
    void setFrameOrder(uint8_t iOrder);

    // Get node IDs as 24-bit integers
    uint32_t getSrcNodeId() const;
    uint32_t getDestNodeId() const;

    // Serialize a strict 2W protocol frame for normal TX.
    // 2W frames never append hasHmac and never append CRC here.
    // Returns number of bytes written, or 0 on error.
    uint8_t serialize2W(uint8_t *oBuffer, uint8_t iMaxLen) const;

    // Serialize a 1W protocol frame for normal TX.
    // Isolates 1W HMAC/length rules, including the SendKey1W special case.
    // Transport CRC is not appended here.
    uint8_t serialize1W(uint8_t *oBuffer, uint8_t iMaxLen) const;

    // Serialize a raw radio/diagnostic frame and append transport CRC.
    // This is intentionally explicit so normal protocol serializers stay CRC-free.
    uint8_t serializeRawWithCrc(uint8_t *oBuffer, uint8_t iMaxLen) const;

    // Legacy compatibility dispatcher for normal TX.
    // Dispatches by MODE_1W and does not append CRC.
    uint8_t serialize(uint8_t *oBuffer, uint8_t iMaxLen) const;

    // Deserialize an exact protocol frame from a received byte buffer.
    // Strict mode: no CRC bytes and no extra transport bytes are accepted.
    // 2W frames must match the CTRL0-declared length exactly.
    // 1W parsing keeps the existing SendKey1W HMAC handling so 1W behavior is unchanged.
    bool deserializeFrame(const uint8_t *iBuffer, uint8_t iLen);

    // Deserialize a raw radio/diagnostic buffer that may include a transport CRC.
    // If CRC bytes are present they are verified and stripped before protocol parsing.
    bool deserializeRawWithOptionalCrc(const uint8_t *iBuffer, uint8_t iLen);

    // Legacy compatibility wrapper. Normal RX paths should call deserializeFrame();
    // diagnostics/tests that intentionally pass raw buffers may keep using this.
    bool deserialize(const uint8_t *iBuffer, uint8_t iLen);

    // Get total frame length including HMAC
    uint8_t totalLength() const;
};
