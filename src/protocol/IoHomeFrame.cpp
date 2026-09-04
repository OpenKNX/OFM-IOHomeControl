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
    memset(trailerMac, 0, IOHC_HMAC_SIZE);
    hasTrailerMac = false;
    crc = 0;
    hasCrc = false;
}

void IoHomeFrame::setStart2W()
{
    ctrlByte0 = IOHC_CTRL0_START; // START flag, 2W mode (bit 5 = 0)
    ctrlByte1 = 0x00;             // version 0, matching real gateway behavior
}

void IoHomeFrame::setLowPower(bool iLowPower)
{
    if (iLowPower)
        ctrlByte1 |= IOHC_CTRL1_LOW_POWER;
    else
        ctrlByte1 &= ~IOHC_CTRL1_LOW_POWER;
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

void IoHomeFrame::setDestBroadcast2E()
{
    memcpy(destNode, IOHC_BROADCAST_DISCOVER2E, IOHC_NODE_ID_SIZE);
}

void IoHomeFrame::setDestGroup()
{
    memcpy(destNode, IOHC_BROADCAST_GROUP, IOHC_NODE_ID_SIZE);
}

void IoHomeFrame::set1WMode()
{
    ctrlByte0 |= IOHC_CTRL0_MODE_1W;
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
    if (hasTrailerMac)
        lLen += IOHC_HMAC_SIZE;
    if (hasCrc)
        lLen += IOHC_CRC_SIZE;
    return lLen;
}

namespace
{
    uint8_t serializeProtocolFrame(const IoHomeFrame &iFrame,
                                   uint8_t *oBuffer,
                                   uint8_t iMaxLen,
                                   uint8_t iMaxDeclaredLen,
                                   bool iIncludeHmacInLength,
                                   bool iAppendHmac,
                                   bool iAppendCrc)
    {
        if (!oBuffer)
            return 0;

        const uint8_t lDeclaredLen = 9 + iFrame.dataLen +
                                     (iIncludeHmacInLength ? IOHC_HMAC_SIZE : 0);
        const uint8_t lTotalLen = 9 + iFrame.dataLen +
                                  (iAppendHmac ? IOHC_HMAC_SIZE : 0) +
                                  (iAppendCrc ? IOHC_CRC_SIZE : 0);

        if (lDeclaredLen < IOHC_FRAME_MIN_SIZE ||
            lDeclaredLen > iMaxDeclaredLen ||
            lTotalLen > iMaxLen ||
            iFrame.dataLen > IOHC_FRAME_MAX_DATA)
        {
            return 0;
        }

        // Update CTRL0 with the declared protocol-frame length encoded as len-1.
        // CRC is always transport-layer and never part of the declared length.
        uint8_t lCtrl0 = iFrame.ctrlByte0;
        lCtrl0 = (lCtrl0 & ~IOHC_CTRL0_LEN_MASK) |
                 ((lDeclaredLen - 1) & IOHC_CTRL0_LEN_MASK);

        uint8_t lPos = 0;
        oBuffer[lPos++] = lCtrl0;
        oBuffer[lPos++] = iFrame.ctrlByte1;
        memcpy(oBuffer + lPos, iFrame.destNode, IOHC_NODE_ID_SIZE);
        lPos += IOHC_NODE_ID_SIZE;
        memcpy(oBuffer + lPos, iFrame.srcNode, IOHC_NODE_ID_SIZE);
        lPos += IOHC_NODE_ID_SIZE;
        oBuffer[lPos++] = static_cast<uint8_t>(iFrame.commandId);

        if (iFrame.dataLen > 0)
        {
            memcpy(oBuffer + lPos, iFrame.data, iFrame.dataLen);
            lPos += iFrame.dataLen;
        }

        if (iAppendHmac)
        {
            memcpy(oBuffer + lPos, iFrame.hmac, IOHC_HMAC_SIZE);
            lPos += IOHC_HMAC_SIZE;
        }

        if (iAppendCrc)
        {
            const uint16_t lCrc = IoHomeCrypto::crc16Kermit(oBuffer, lPos);
            oBuffer[lPos++] = lCrc & 0xFF;        // LSB first
            oBuffer[lPos++] = (lCrc >> 8) & 0xFF; // MSB
        }

        return lPos;
    }
}

uint8_t IoHomeFrame::serialize2W(uint8_t *oBuffer, uint8_t iMaxLen) const
{
    // 2W frames are strict protocol frames. A 0x3D ChallengeResponse carries
    // the 6-byte HMAC as normal data with hasHmac=false. Appended HMAC and CRC
    // are rejected here so normal 2W TX cannot accidentally emit legacy/raw bytes.
    if ((ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0 || hasHmac || hasTrailerMac || hasCrc)
        return 0;

    return serializeProtocolFrame(*this, oBuffer, iMaxLen,
                                  IOHC_FRAME_MAX_SIZE_2W,
                                  false,
                                  false,
                                  false);
}

uint8_t IoHomeFrame::serialize1W(uint8_t *oBuffer, uint8_t iMaxLen) const
{
    // 1W has its own HMAC/length semantics:
    // - SendKey1W (0x30) is a dedicated 29-byte declared frame:
    //   9-byte header + encryptedKey[16] + manufacturer + 0x01 + sequence[2].
    //   A device-profile option may append its six-byte MAC outside CTRL0's
    //   declared length; that is not a normal 1W HMAC.
    // - Normal authenticated 1W commands include the appended HMAC in CTRL0.
    // Transport CRC is not appended by the normal 1W serializer.
    if ((ctrlByte0 & IOHC_CTRL0_MODE_1W) == 0 || hasCrc)
        return 0;

    if (commandId != IoHomeCommand::SendKey1W && hasTrailerMac)
        return 0;

    if (commandId == IoHomeCommand::SendKey1W)
    {
        if (hasHmac || dataLen != 20)
            return 0;

        const uint8_t lLen = serializeProtocolFrame(*this, oBuffer, iMaxLen,
                                                     IOHC_FRAME_MAX_SIZE_1W,
                                                     false, false, false);
        if (lLen == 0 || !hasTrailerMac)
            return lLen;
        if (lLen + IOHC_HMAC_SIZE > iMaxLen)
            return 0;
        memcpy(oBuffer + lLen, trailerMac, IOHC_HMAC_SIZE);
        return lLen + IOHC_HMAC_SIZE;
    }

    return serializeProtocolFrame(*this, oBuffer, iMaxLen,
                                  IOHC_FRAME_MAX_SIZE_1W,
                                  hasHmac,
                                  hasHmac,
                                  false);
}

uint8_t IoHomeFrame::serializeRawWithCrc(uint8_t *oBuffer, uint8_t iMaxLen) const
{
    // Raw/diagnostic serialization is the only place where the transport CRC is
    // appended by the frame layer. Keep 2W appended-HMAC forbidden even here;
    // 2W auth responses must encode HMAC as command data on 0x3D.
    const bool lOneWay = (ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0;
    if (!lOneWay && (hasHmac || hasTrailerMac))
        return 0;

    if (lOneWay)
    {
        if (commandId != IoHomeCommand::SendKey1W && hasTrailerMac)
            return 0;
        if (commandId == IoHomeCommand::SendKey1W)
        {
            if (hasHmac || dataLen != 20)
                return 0;
            const uint8_t lLen = serializeProtocolFrame(*this, oBuffer, iMaxLen,
                                                         IOHC_FRAME_MAX_SIZE_1W,
                                                         false, false, false);
            if (lLen == 0 || lLen + (hasTrailerMac ? IOHC_HMAC_SIZE : 0) + IOHC_CRC_SIZE > iMaxLen)
                return 0;
            uint8_t lPos = lLen;
            if (hasTrailerMac)
            {
                memcpy(oBuffer + lPos, trailerMac, IOHC_HMAC_SIZE);
                lPos += IOHC_HMAC_SIZE;
            }
            const uint16_t lCrc = IoHomeCrypto::crc16Kermit(oBuffer, lPos);
            oBuffer[lPos++] = lCrc & 0xFF;
            oBuffer[lPos++] = (lCrc >> 8) & 0xFF;
            return lPos;
        }

        return serializeProtocolFrame(*this, oBuffer, iMaxLen,
                                      IOHC_FRAME_MAX_SIZE_1W,
                                      hasHmac,
                                      hasHmac,
                                      true);
    }

    return serializeProtocolFrame(*this, oBuffer, iMaxLen,
                                  IOHC_FRAME_MAX_SIZE_2W,
                                  false,
                                  false,
                                  true);
}

uint8_t IoHomeFrame::serialize(uint8_t *oBuffer, uint8_t iMaxLen) const
{
    if ((ctrlByte0 & IOHC_CTRL0_MODE_1W) != 0)
        return serialize1W(oBuffer, iMaxLen);
    return serialize2W(oBuffer, iMaxLen);
}

bool IoHomeFrame::deserializeFrame(const uint8_t *iBuffer, uint8_t iLen)
{
    if (!iBuffer || iLen < IOHC_FRAME_MIN_SIZE)
        return false;

    init();

    const bool lIs1W = (iBuffer[0] & IOHC_CTRL0_MODE_1W) != 0;
    const uint8_t lMaxDeclared = lIs1W ? IOHC_FRAME_MAX_SIZE_1W
                                       : IOHC_FRAME_MAX_SIZE_2W;
    const uint8_t lDeclaredLen = (iBuffer[0] & IOHC_CTRL0_LEN_MASK) + 1;
    if (lDeclaredLen < IOHC_FRAME_MIN_SIZE || lDeclaredLen > lMaxDeclared)
        return false;

    if (!lIs1W && iLen != lDeclaredLen)
        return false;

    const uint8_t lCmd = iBuffer[IOHC_FRAME_MIN_SIZE - 1];
    // SendKey1W (0x30) may carry a six-byte MAC trailer outside its declared
    // CTRL0 length. Normal authenticated 1W commands include HMAC in length.
    if (lIs1W)
    {
        const bool lSendKeyTrailer = lCmd == static_cast<uint8_t>(IoHomeCommand::SendKey1W) &&
                                     iLen == lDeclaredLen + IOHC_HMAC_SIZE;
        if (iLen != lDeclaredLen && !lSendKeyTrailer)
            return false;

        if (iLen > IOHC_FRAME_MAX_SIZE_1W)
            return false;
    }

    const uint8_t lPayloadLen = lDeclaredLen;

    uint8_t lPos = 0;
    ctrlByte0 = iBuffer[lPos++];
    ctrlByte1 = iBuffer[lPos++];

    memcpy(destNode, iBuffer + lPos, IOHC_NODE_ID_SIZE);
    lPos += IOHC_NODE_ID_SIZE;
    memcpy(srcNode, iBuffer + lPos, IOHC_NODE_ID_SIZE);
    lPos += IOHC_NODE_ID_SIZE;

    commandId = static_cast<IoHomeCommand>(iBuffer[lPos++]);

    const uint8_t lDeclaredRemainingBytes = lDeclaredLen - lPos;
    const uint8_t lRemainingBytes = lPayloadLen - lPos;

    hasCrc = false;
    crc = 0;

    if (!lIs1W)
    {
        dataLen = lDeclaredRemainingBytes;
        hasHmac = false;
    }
    else if (lCmd == static_cast<uint8_t>(IoHomeCommand::SendKey1W))
    {
        if (lDeclaredRemainingBytes != 20)
            return false;
        dataLen = lDeclaredRemainingBytes;
        hasHmac = false;
        hasTrailerMac = iLen == lDeclaredLen + IOHC_HMAC_SIZE;
    }
    else if ((lCmd == 0x00 || lCmd == 0x01 || lCmd == 0x20 || lCmd == 0x2E || lCmd == 0x39) &&
             lDeclaredRemainingBytes >= IOHC_HMAC_SIZE)
    {
        // 0x00 (_p0x00_14/_p0x00_16), 0x01 (_p0x01_13), 0x20 (_p0x20_13/15/16),
        // 0x2E and 0x39 all carry an appended 6-byte 1W HMAC per the reference
        // (rspaargaren/iohomecontrol). This mirrors what the controller
        // already transmits for 0x01 (build1WActivateMode13).
        dataLen = lDeclaredRemainingBytes - IOHC_HMAC_SIZE;
        hasHmac = true;
    }
    else
    {
        dataLen = lRemainingBytes;
        hasHmac = false;
    }

    if (dataLen > IOHC_FRAME_MAX_DATA)
        return false;

    if (dataLen > 0)
        memcpy(data, iBuffer + lPos, dataLen);
    lPos += dataLen;

    if (hasHmac)
    {
        if (lPos + IOHC_HMAC_SIZE > lPayloadLen)
            return false;
        memcpy(hmac, iBuffer + lPos, IOHC_HMAC_SIZE);
        lPos += IOHC_HMAC_SIZE;
    }

    if (hasTrailerMac)
        memcpy(trailerMac, iBuffer + lPos, IOHC_HMAC_SIZE);

    return lPos == lPayloadLen;
}

bool IoHomeFrame::deserializeRawWithOptionalCrc(const uint8_t *iBuffer, uint8_t iLen)
{
    if (!iBuffer || iLen < IOHC_FRAME_MIN_SIZE)
        return false;

    // First accept exact, CRC-free protocol frames.
    if (deserializeFrame(iBuffer, iLen))
        return true;

    const bool lIs1W = (iBuffer[0] & IOHC_CTRL0_MODE_1W) != 0;
    const uint8_t lMaxDeclared = lIs1W ? IOHC_FRAME_MAX_SIZE_1W
                                       : IOHC_FRAME_MAX_SIZE_2W;
    const uint8_t lDeclaredLen = (iBuffer[0] & IOHC_CTRL0_LEN_MASK) + 1;
    if (lDeclaredLen < IOHC_FRAME_MIN_SIZE || lDeclaredLen > lMaxDeclared)
        return false;

    uint8_t lProtocolLen = lDeclaredLen;

    const uint8_t lCmd = iBuffer[IOHC_FRAME_MIN_SIZE - 1];
    if (iLen == lDeclaredLen + IOHC_CRC_SIZE)
    {
        lProtocolLen = lDeclaredLen;
    }
    else if (lIs1W && lCmd == static_cast<uint8_t>(IoHomeCommand::SendKey1W) &&
             iLen == lDeclaredLen + IOHC_HMAC_SIZE + IOHC_CRC_SIZE)
    {
        lProtocolLen = lDeclaredLen + IOHC_HMAC_SIZE;
    }
    else
    {
        return false;
    }

    const uint16_t lReceivedCrc = (uint16_t)iBuffer[lProtocolLen] |
                                  ((uint16_t)iBuffer[lProtocolLen + 1] << 8);
    const uint16_t lExpectedCrc = IoHomeCrypto::crc16Kermit(iBuffer, lProtocolLen);
    if (lReceivedCrc != lExpectedCrc)
        return false;

    if (!deserializeFrame(iBuffer, lProtocolLen))
        return false;

    crc = lReceivedCrc;
    hasCrc = true;
    return true;
}

bool IoHomeFrame::deserialize(const uint8_t *iBuffer, uint8_t iLen)
{
    return deserializeRawWithOptionalCrc(iBuffer, iLen);
}
