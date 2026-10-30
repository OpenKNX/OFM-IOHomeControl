#pragma once
#include "IoHomeStatusUpdate.h"
#include <cstring>

// Session-only raw diagnostics. Authentication is a controller responsibility:
// record() must only be reached after the inbound frame's challenge proof passes.
struct IoHomeDeviceEvent
{
    uint32_t verifiedAtMs = 0, count = 0, source = 0, destination = 0;
    uint16_t eventId = 0;
    uint8_t frequencyIndex = 0xFF, length = 0;
    bool valid = false;
    uint8_t data[IOHC_FRAME_MAX_DATA]{};

    void clear() { *this = {}; }
    bool record(const IoHomeFrame &frame, uint8_t frequency, uint32_t now)
    {
        if ((frame.ctrlByte0 & IOHC_CTRL0_MODE_1W) ||
            ioHomeStatusUpdateKind(frame) != IoHomeStatusUpdateKind::DeviceEvent)
            return false;
        const uint32_t next = count == UINT32_MAX ? count : count + 1;
        clear();
        valid = true;
        verifiedAtMs = now;
        count = next;
        source = frame.getSrcNodeId();
        destination = frame.getDestNodeId();
        eventId = (uint16_t(frame.data[2]) << 8) | frame.data[3];
        frequencyIndex = frequency;
        length = frame.dataLen;
        std::memcpy(data, frame.data, length);
        return true;
    }
};
static_assert(sizeof(IoHomeDeviceEvent) <= 48, "Bound per-channel device-event RAM");
