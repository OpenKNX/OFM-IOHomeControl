#include "IoHomeRemoteMap.h"

IoHomeRemoteMap::IoHomeRemoteMap()
    : mObservedHead(0), mObservedCount(0)
{
    memset(mEntries, 0, sizeof(mEntries));
    memset(mObserved, 0, sizeof(mObserved));
}

bool IoHomeRemoteMap::addRemote(uint32_t iAddress, const char *iName)
{
    // Check if already exists
    if (findRemote(iAddress))
        return false;

    // Find first inactive slot
    for (uint8_t i = 0; i < IOHC_REMOTE_MAX_ENTRIES; i++)
    {
        if (!mEntries[i].active)
        {
            mEntries[i].active = true;
            mEntries[i].address = iAddress & 0x00FFFFFF;
            strncpy(mEntries[i].name, iName, IOHC_REMOTE_NAME_LEN);
            mEntries[i].name[IOHC_REMOTE_NAME_LEN] = '\0';
            memset(mEntries[i].key, 0, 16);
            mEntries[i].sequenceNum = 0;
            mEntries[i].linkCount = 0;
            memset(mEntries[i].linkedDevices, 0, sizeof(mEntries[i].linkedDevices));
            memset(mEntries[i].supportedCommands, 0, sizeof(mEntries[i].supportedCommands));
            return true;
        }
    }
    return false; // full
}

bool IoHomeRemoteMap::removeRemote(uint32_t iAddress)
{
    IoHomeRemoteEntry *lEntry = findRemote(iAddress);
    if (!lEntry)
        return false;

    memset(lEntry, 0, sizeof(IoHomeRemoteEntry));
    return true;
}

bool IoHomeRemoteMap::linkDevice(uint32_t iRemoteAddr, uint32_t iDeviceAddr)
{
    IoHomeRemoteEntry *lEntry = findRemote(iRemoteAddr);
    if (!lEntry)
        return false;

    // Check if already linked
    for (uint8_t i = 0; i < lEntry->linkCount; i++)
    {
        if (lEntry->linkedDevices[i] == iDeviceAddr)
            return false; // already linked
    }

    if (lEntry->linkCount >= IOHC_REMOTE_MAX_LINKS)
        return false; // full

    lEntry->linkedDevices[lEntry->linkCount] = iDeviceAddr;
    lEntry->linkCount++;
    return true;
}

bool IoHomeRemoteMap::unlinkDevice(uint32_t iRemoteAddr, uint32_t iDeviceAddr)
{
    IoHomeRemoteEntry *lEntry = findRemote(iRemoteAddr);
    if (!lEntry)
        return false;

    for (uint8_t i = 0; i < lEntry->linkCount; i++)
    {
        if (lEntry->linkedDevices[i] == iDeviceAddr)
        {
            // Shift remaining entries
            for (uint8_t j = i; j < lEntry->linkCount - 1; j++)
                lEntry->linkedDevices[j] = lEntry->linkedDevices[j + 1];
            lEntry->linkCount--;
            lEntry->linkedDevices[lEntry->linkCount] = 0;
            return true;
        }
    }
    return false;
}

IoHomeRemoteEntry *IoHomeRemoteMap::findRemote(uint32_t iAddress)
{
    for (uint8_t i = 0; i < IOHC_REMOTE_MAX_ENTRIES; i++)
    {
        if (mEntries[i].active && mEntries[i].address == (iAddress & 0x00FFFFFF))
            return &mEntries[i];
    }
    return nullptr;
}

const IoHomeRemoteEntry *IoHomeRemoteMap::findRemote(uint32_t iAddress) const
{
    for (uint8_t i = 0; i < IOHC_REMOTE_MAX_ENTRIES; i++)
    {
        if (mEntries[i].active && mEntries[i].address == (iAddress & 0x00FFFFFF))
            return &mEntries[i];
    }
    return nullptr;
}

void IoHomeRemoteMap::setRemoteKey(uint32_t iAddress, const uint8_t *iKey)
{
    IoHomeRemoteEntry *lEntry = findRemote(iAddress);
    if (lEntry)
        memcpy(lEntry->key, iKey, 16);
}

uint8_t IoHomeRemoteMap::count() const
{
    uint8_t lCount = 0;
    for (uint8_t i = 0; i < IOHC_REMOTE_MAX_ENTRIES; i++)
    {
        if (mEntries[i].active)
            lCount++;
    }
    return lCount;
}

const IoHomeRemoteEntry *IoHomeRemoteMap::entry(uint8_t iIndex) const
{
    if (iIndex < IOHC_REMOTE_MAX_ENTRIES)
        return &mEntries[iIndex];
    return nullptr;
}

uint16_t IoHomeRemoteMap::flashSize() const
{
    return IOHC_REMOTE_MAP_FLASH_SIZE;
}

uint16_t IoHomeRemoteMap::writeToBuffer(uint8_t *oBuffer) const
{
    uint16_t lOffset = 0;

    // Count of active entries
    uint8_t lCount = count();
    oBuffer[lOffset++] = lCount;

    for (uint8_t i = 0; i < IOHC_REMOTE_MAX_ENTRIES; i++)
    {
        const IoHomeRemoteEntry &lEntry = mEntries[i];
        oBuffer[lOffset++] = lEntry.active ? 1 : 0;
        oBuffer[lOffset++] = (lEntry.address >> 16) & 0xFF;
        oBuffer[lOffset++] = (lEntry.address >> 8) & 0xFF;
        oBuffer[lOffset++] = lEntry.address & 0xFF;
        memcpy(oBuffer + lOffset, lEntry.name, IOHC_REMOTE_NAME_LEN);
        lOffset += IOHC_REMOTE_NAME_LEN;
        memcpy(oBuffer + lOffset, lEntry.key, 16);
        lOffset += 16;
        oBuffer[lOffset++] = (lEntry.sequenceNum >> 8) & 0xFF;
        oBuffer[lOffset++] = lEntry.sequenceNum & 0xFF;
        oBuffer[lOffset++] = lEntry.linkCount;
        for (uint8_t j = 0; j < IOHC_REMOTE_MAX_LINKS; j++)
        {
            oBuffer[lOffset++] = (lEntry.linkedDevices[j] >> 16) & 0xFF;
            oBuffer[lOffset++] = (lEntry.linkedDevices[j] >> 8) & 0xFF;
            oBuffer[lOffset++] = lEntry.linkedDevices[j] & 0xFF;
        }
        // Supported commands bitmask (32 bytes)
        memcpy(oBuffer + lOffset, lEntry.supportedCommands, 32);
        lOffset += 32;
    }
    return lOffset;
}

uint16_t IoHomeRemoteMap::readFromBuffer(const uint8_t *iBuffer, uint16_t iLen)
{
    if (iLen < 1)
        return 0;

    uint16_t lOffset = 0;
    uint8_t lCount = iBuffer[lOffset++]; // stored count (informational)
    (void)lCount;

    for (uint8_t i = 0; i < IOHC_REMOTE_MAX_ENTRIES && lOffset + IOHC_REMOTE_ENTRY_FLASH_SIZE <= iLen; i++)
    {
        mEntries[i].active = iBuffer[lOffset++] != 0;
        mEntries[i].address = ((uint32_t)iBuffer[lOffset] << 16) |
                              ((uint32_t)iBuffer[lOffset + 1] << 8) |
                              iBuffer[lOffset + 2];
        lOffset += 3;
        memcpy(mEntries[i].name, iBuffer + lOffset, IOHC_REMOTE_NAME_LEN);
        mEntries[i].name[IOHC_REMOTE_NAME_LEN] = '\0';
        lOffset += IOHC_REMOTE_NAME_LEN;
        memcpy(mEntries[i].key, iBuffer + lOffset, 16);
        lOffset += 16;
        mEntries[i].sequenceNum = ((uint16_t)iBuffer[lOffset] << 8) | iBuffer[lOffset + 1];
        lOffset += 2;
        mEntries[i].linkCount = iBuffer[lOffset++];
        if (mEntries[i].linkCount > IOHC_REMOTE_MAX_LINKS)
            mEntries[i].linkCount = IOHC_REMOTE_MAX_LINKS;
        for (uint8_t j = 0; j < IOHC_REMOTE_MAX_LINKS; j++)
        {
            mEntries[i].linkedDevices[j] = ((uint32_t)iBuffer[lOffset] << 16) |
                                           ((uint32_t)iBuffer[lOffset + 1] << 8) |
                                           iBuffer[lOffset + 2];
            lOffset += 3;
        }
        // Supported commands bitmask (32 bytes)
        if (lOffset + 32 <= iLen)
        {
            memcpy(mEntries[i].supportedCommands, iBuffer + lOffset, 32);
            lOffset += 32;
        }
    }
    return lOffset;
}

void IoHomeRemoteMap::observeAddress(uint32_t iAddress)
{
    uint32_t lAddr = iAddress & 0x00FFFFFF;

    // Deduplicate: check if already observed
    for (uint8_t i = 0; i < mObservedCount; i++)
    {
        if (mObserved[i] == lAddr)
            return;
    }

    // Insert into circular buffer
    if (mObservedCount < IOHC_REMOTE_OBSERVED_MAX)
    {
        mObserved[mObservedCount] = lAddr;
        mObservedCount++;
    }
    else
    {
        mObserved[mObservedHead] = lAddr;
        mObservedHead = (mObservedHead + 1) % IOHC_REMOTE_OBSERVED_MAX;
    }
}

uint8_t IoHomeRemoteMap::observedCount() const
{
    return mObservedCount;
}

uint32_t IoHomeRemoteMap::observedAddress(uint8_t iIndex) const
{
    if (iIndex < mObservedCount)
        return mObserved[iIndex];
    return 0;
}

// --- Supported commands bitmask ---

void IoHomeRemoteMap::setRemoteSupportedCommands(uint32_t iAddress, const uint8_t *iBitmask, uint8_t iBitmaskLen)
{
    IoHomeRemoteEntry *lEntry = findRemote(iAddress);
    if (!lEntry)
        return;

    uint8_t copyLen = iBitmaskLen < 32 ? iBitmaskLen : 32;
    memcpy(lEntry->supportedCommands, iBitmask, copyLen);
    // Zero-fill remaining bytes
    if (copyLen < 32)
        memset(lEntry->supportedCommands + copyLen, 0, 32 - copyLen);
}

const uint8_t *IoHomeRemoteMap::getRemoteSupportedCommands(uint32_t iAddress, uint8_t *oLen) const
{
    const IoHomeRemoteEntry *lEntry = findRemote(iAddress);
    if (!lEntry || oLen == nullptr)
        return nullptr;

    // Find the actual length (trim trailing zero bytes)
    uint8_t len = 32;
    while (len > 0 && lEntry->supportedCommands[len - 1] == 0)
        len--;
    *oLen = len;
    return lEntry->supportedCommands;
}

bool IoHomeRemoteMap::remoteCommandSupported(uint32_t iAddress, uint8_t iCmd) const
{
    const IoHomeRemoteEntry *lEntry = findRemote(iAddress);
    if (!lEntry)
        return false;

    uint8_t byteIdx = iCmd / 8;
    uint8_t bitIdx = iCmd % 8;
    if (byteIdx >= 32)
        return false;

    return (lEntry->supportedCommands[byteIdx] >> bitIdx) & 0x01;
}
