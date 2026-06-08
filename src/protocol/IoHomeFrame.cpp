#include "IoHomeFrame.h"
#include <string.h>

void IoHomeFrame::init()
{
    ctrlByte0 = 0;
    ctrlByte1 = 0;
    memset(destNode, 0, IOHC_NODE_ID_SIZE);
    memset(srcNode, 0, IOHC_NODE_ID_SIZE);
    commandId = IoHomeCommand::Execute;
    memset(data, 0, IOHC_FRAME_MAX_DATA);
    dataLen = 0;
    memset(hmac, 0, IOHC_HMAC_SIZE);
    hasHmac = false;
    crc = 0;
    hasCrc = false;
}

void IoHomeFrame::setStart2W()
{
    ctrlByte0 = IOHC_CTRL0_START; // START flag, 2W mode (bit 5 = 0)
    ctrlByte1 = 0x00;             // version 0, matching real gateway behavior
}

void IoHomeFrame::setSrcNode(uint32_t iNodeId)
{
    srcNode[0] = (iNodeId >> 16) & 0xFF;
    srcNode[1] = (iNodeId >> 8) & 0xFF;
    srcNode[2] = iNodeId & 0xFF;
}

void IoHomeFrame::setDestNode(uint32_t iNodeId)
{
    destNode[0] = (iNodeId >> 16) & 0xFF;
    destNode[1] = (iNodeId >> 8) & 0xFF;
    destNode[2] = iNodeId & 0xFF;
}

void IoHomeFrame::setDestBroadcast()
{
    memcpy(destNode, IOHC_BROADCAST_DISCOVER, IOHC_NODE_ID_SIZE);
}

void IoHomeFrame::setDestGroup()
{
    memcpy(destNode, IOHC_BROADCAST_GROUP, IOHC_NODE_ID_SIZE);
}

void IoHomeFrame::set1WMode()
{
    ctrlByte0 |= IOHC_CTRL0_MODE_1W;
    ctrlByte1 |= IOHC_CTRL1_LOW_POWER;
}

uint8_t IoHomeFrame::getFrameOrder() const
{
    return ctrlByte0 & IOHC_CTRL0_ORDER_MASK;
}

void IoHomeFrame::setFrameOrder(uint8_t iOrder)
{
    ctrlByte0 = (ctrlByte0 & ~IOHC_CTRL0_ORDER_MASK) | (iOrder & IOHC_CTRL0_ORDER_MASK);
}

uint32_t IoHomeFrame::getSrcNodeId() const
{
    return ((uint32_t)srcNode[0] << 16) | ((uint32_t)srcNode[1] << 8) | srcNode[2];
}

uint32_t IoHomeFrame::getDestNodeId() const
{
    return ((uint32_t)destNode[0] << 16) | ((uint32_t)destNode[1] << 8) | destNode[2];
}

uint8_t IoHomeFrame::totalLength() const
{
    // ctrl0 + ctrl1 + dest(3) + src(3) + cmd(1) + data + optional hmac
    uint8_t lLen = 9 + dataLen;
    if (hasHmac)
        lLen += IOHC_HMAC_SIZE;
    if (hasCrc)
        lLen += IOHC_CRC_SIZE;
    return lLen;
}

uint8_t IoHomeFrame::serialize(uint8_t *oBuffer, uint8_t iMaxLen) const
{
    // io-homecontrol CTRL0 length field is awkward in 1W mode:
    // - SendKey1W (0x30) uses 9 + dataLen, excluding the appended 6-byte HMAC.
    //   This is required for the known 0x30 reference frame to start with 0xFC.
    // - Normal authenticated 1W commands (Execute/ActivateMode/...) include the
    //   appended HMAC in the length field. Example reference Execute starts with
    //   0xF6 for 9 + 8 data bytes + 6 HMAC bytes -> length field 22.
    // CRC is still transport-layer and is never included here.
    const bool lOneWay = (ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0;
    const bool lIncludeHmacInLength = lOneWay && hasHmac && commandId != IoHomeCommand::SendKey1W;

    uint8_t lDeclaredLen = 9 + dataLen + (lIncludeHmacInLength ? IOHC_HMAC_SIZE : 0);
    uint8_t lTotal = 9 + dataLen + (hasHmac ? IOHC_HMAC_SIZE : 0) + (hasCrc ? IOHC_CRC_SIZE : 0);
    if (lDeclaredLen > IOHC_FRAME_MAX_SIZE || lTotal > iMaxLen)
        return 0;

    // Update ctrl byte 0 with frame length (excluding CTRL0 and CRC, encoded as len-1).
    uint8_t lCtrl0 = ctrlByte0;
    lCtrl0 = (lCtrl0 & ~IOHC_CTRL0_LEN_MASK) | ((lDeclaredLen - 1) & IOHC_CTRL0_LEN_MASK);

    uint8_t lPos = 0;
    oBuffer[lPos++] = lCtrl0;
    oBuffer[lPos++] = ctrlByte1;
    memcpy(oBuffer + lPos, destNode, IOHC_NODE_ID_SIZE);
    lPos += IOHC_NODE_ID_SIZE;
    memcpy(oBuffer + lPos, srcNode, IOHC_NODE_ID_SIZE);
    lPos += IOHC_NODE_ID_SIZE;
    oBuffer[lPos++] = static_cast<uint8_t>(commandId);

    if (dataLen > 0)
    {
        memcpy(oBuffer + lPos, data, dataLen);
        lPos += dataLen;
    }

    if (hasHmac)
    {
        memcpy(oBuffer + lPos, hmac, IOHC_HMAC_SIZE);
        lPos += IOHC_HMAC_SIZE;
    }

    if (hasCrc)
    {
        uint16_t lCrc = IoHomeCrypto::crc16Kermit(oBuffer, lPos);
        oBuffer[lPos++] = lCrc & 0xFF;        // LSB first
        oBuffer[lPos++] = (lCrc >> 8) & 0xFF; // MSB
    }

    return lPos;
}

bool IoHomeFrame::deserialize(const uint8_t *iBuffer, uint8_t iLen)
{
    if (!iBuffer || iLen < IOHC_FRAME_MIN_SIZE || iLen > IOHC_FRAME_MAX_SIZE + IOHC_CRC_SIZE)
        return false;

    init();

    uint8_t lDeclaredLen = (iBuffer[0] & IOHC_CTRL0_LEN_MASK) + 1;
    if (lDeclaredLen < IOHC_FRAME_MIN_SIZE || lDeclaredLen > IOHC_FRAME_MAX_SIZE)
        return false;

    // The declared length excludes HMAC and CRC which are appended after.
    // Detect HMAC/CRC presence by matching against known length patterns.
    uint8_t lHmacLen = 0;
    uint8_t lCrcLen = 0;

    if (iLen == lDeclaredLen + IOHC_HMAC_SIZE + IOHC_CRC_SIZE)
    {
        // Both HMAC and CRC present
        lHmacLen = IOHC_HMAC_SIZE;
        lCrcLen = IOHC_CRC_SIZE;
    }
    else if (iLen == lDeclaredLen + IOHC_HMAC_SIZE)
    {
        // HMAC only
        lHmacLen = IOHC_HMAC_SIZE;
    }
    else if (iLen == lDeclaredLen + IOHC_CRC_SIZE)
    {
        // CRC only
        lCrcLen = IOHC_CRC_SIZE;
    }
    else if (iLen != lDeclaredLen)
    {
        // No valid match
        return false;
    }

    // Payload length includes HMAC (CRC is separate, after HMAC)
    uint8_t lPayloadLen = lDeclaredLen + lHmacLen;

    if (lCrcLen > 0)
    {
        uint16_t lReceivedCrc = (uint16_t)iBuffer[lDeclaredLen + lHmacLen] |
                                ((uint16_t)iBuffer[lDeclaredLen + lHmacLen + 1] << 8);
        uint16_t lExpectedCrc = IoHomeCrypto::crc16Kermit(iBuffer, lDeclaredLen + lHmacLen);
        if (lReceivedCrc != lExpectedCrc)
            return false;
        crc = lReceivedCrc;
        hasCrc = true;
    }

    uint8_t lPos = 0;
    ctrlByte0 = iBuffer[lPos++];
    ctrlByte1 = iBuffer[lPos++];

    memcpy(destNode, iBuffer + lPos, IOHC_NODE_ID_SIZE);
    lPos += IOHC_NODE_ID_SIZE;
    memcpy(srcNode, iBuffer + lPos, IOHC_NODE_ID_SIZE);
    lPos += IOHC_NODE_ID_SIZE;

    commandId = static_cast<IoHomeCommand>(iBuffer[lPos++]);

    // Determine if HMAC is present (authenticated commands)
    // For 2W mode, most commands include HMAC
    bool lIs2W = !(ctrlByte0 & IOHC_CTRL0_MODE_1W);
    uint8_t lCmd = static_cast<uint8_t>(commandId);
    bool lNeedsHmac = false;
    if (lIs2W)
        lNeedsHmac = (lCmd == 0x00 || lCmd == 0x01 || lCmd == 0x02 || lCmd == 0x20 || lCmd == 0x39 || lCmd == 0x71);
    else // 1W
        lNeedsHmac = (lCmd == 0x00 || lCmd == 0x2E || lCmd == 0x39 ||
                      (lCmd == 0x30 && lHmacLen == IOHC_HMAC_SIZE));

    uint8_t lRemainingBytes = lPayloadLen - lPos;

    if (lNeedsHmac && lRemainingBytes >= IOHC_HMAC_SIZE)
    {
        dataLen = lRemainingBytes - IOHC_HMAC_SIZE;
        hasHmac = true;
    }
    else
    {
        dataLen = lRemainingBytes;
        hasHmac = false;
    }

    if (dataLen > IOHC_FRAME_MAX_DATA)
        dataLen = IOHC_FRAME_MAX_DATA;

    if (dataLen > 0)
        memcpy(data, iBuffer + lPos, dataLen);
    lPos += dataLen;

    if (hasHmac)
    {
        memcpy(hmac, iBuffer + lPos, IOHC_HMAC_SIZE);
        lPos += IOHC_HMAC_SIZE;
    }

    return true;
}
