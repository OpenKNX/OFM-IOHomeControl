#pragma once
#include "IoHomeFrame.h"

enum class IoHomeStatusUpdateKind : uint8_t
{
    Invalid,
    Position,
    DeviceEvent,
};

// Capture-supported 0x71 envelope: flags, status, BE16 event ID, body length,
// then event data. Only 0x100A has the position layout used by this controller.
// See docs/STATUS-UPDATE-EVENTS.md for provenance and the vendor-parser limits.
inline IoHomeStatusUpdateKind ioHomeStatusUpdateKind(const IoHomeFrame &frame)
{
    if (frame.commandId != IoHomeCommand::StatusUpdate || frame.dataLen < 5 ||
        frame.dataLen > IOHC_FRAME_MAX_DATA || frame.data[4] != frame.dataLen - 5)
        return IoHomeStatusUpdateKind::Invalid;

    const uint16_t eventId = (uint16_t(frame.data[2]) << 8) | frame.data[3];
    if (eventId != 0x100A)
        return IoHomeStatusUpdateKind::DeviceEvent;
    return frame.dataLen >= 11 ? IoHomeStatusUpdateKind::Position
                               : IoHomeStatusUpdateKind::Invalid;
}
