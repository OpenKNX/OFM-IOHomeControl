#pragma once
#include <stdint.h>
#include <string.h>

#define IOHC_REMOTE_MAX_ENTRIES 8
#define IOHC_REMOTE_MAX_LINKS 4
#define IOHC_REMOTE_NAME_LEN 16
#define IOHC_REMOTE_OBSERVED_MAX 32

struct IoHomeRemoteEntry
{
  uint32_t address;                              // 24-bit remote address
  char name[IOHC_REMOTE_NAME_LEN + 1];           // null-terminated
  uint8_t key[16];                               // remote's encryption key
  uint16_t sequenceNum;                          // sequence counter for 1W replay
  uint32_t linkedDevices[IOHC_REMOTE_MAX_LINKS]; // linked device addresses
  uint8_t linkCount;
  uint8_t supportedCommands[32];                 // bitmask of discovered supported commands (32 commands = 256 cmd codes)
  bool  active;
};

// Per-entry flash size: active(1) + address(3) + name(16) + key(16) + seq(2) + linkCount(1) + links(4*3) + supportedCmds(32)
#define IOHC_REMOTE_ENTRY_FLASH_SIZE (1 + 3 + IOHC_REMOTE_NAME_LEN + 16 + 2 + 1 + IOHC_REMOTE_MAX_LINKS * 3 + 32)
// Total flash: count(1) + entries
#define IOHC_REMOTE_MAP_FLASH_SIZE (1 + IOHC_REMOTE_MAX_ENTRIES * IOHC_REMOTE_ENTRY_FLASH_SIZE)

class IoHomeRemoteMap
{
public:
  IoHomeRemoteMap();

  bool addRemote(uint32_t iAddress, const char *iName);
  bool removeRemote(uint32_t iAddress);
  bool linkDevice(uint32_t iRemoteAddr, uint32_t iDeviceAddr);
  bool unlinkDevice(uint32_t iRemoteAddr, uint32_t iDeviceAddr);
  IoHomeRemoteEntry *findRemote(uint32_t iAddress);
  const IoHomeRemoteEntry *findRemote(uint32_t iAddress) const;
  void setRemoteKey(uint32_t iAddress, const uint8_t *iKey);

  // Supported commands bitmask
  void setRemoteSupportedCommands(uint32_t iAddress, const uint8_t *iBitmask, uint8_t iBitmaskLen);
  const uint8_t *getRemoteSupportedCommands(uint32_t iAddress, uint8_t *oLen) const;
  bool remoteCommandSupported(uint32_t iAddress, uint8_t iCmd) const;

  uint8_t count() const;
  const IoHomeRemoteEntry *entry(uint8_t iIndex) const;

  // Flash persistence
  uint16_t flashSize() const;
  uint16_t writeToBuffer(uint8_t *oBuffer) const;
  uint16_t readFromBuffer(const uint8_t *iBuffer, uint16_t iLen);

  // Track observed addresses in passive mode
  void observeAddress(uint32_t iAddress);
  uint8_t observedCount() const;
  uint32_t observedAddress(uint8_t iIndex) const;

private:
  IoHomeRemoteEntry mEntries[IOHC_REMOTE_MAX_ENTRIES];
  uint32_t mObserved[IOHC_REMOTE_OBSERVED_MAX];
  uint8_t mObservedHead;
  uint8_t mObservedCount;
};
