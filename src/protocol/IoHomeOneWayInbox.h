#pragma once
#include "IoHomeForeignActivity.h"

// Physical copies, distinct wire commands and actionable Execute sequences
// are observations. A button gesture or motor outcome cannot be reconstructed
// reliably from them alone.
struct IoHomeOneWayInbox {
    struct Entry {IoHomeFrame frame;uint32_t receivedMs=0;int16_t rssi=0;uint8_t frequency=0;};
    struct Seen {
        uint32_t source=0, receivedMs=0, signature=0;
        uint16_t sequence=0;
        bool actionable=false, valid=false;
    };
    Entry entries[4]{};
    Seen seen[8]{};
    uint8_t head=0,count=0,nextSeen=0;
    uint32_t physical=0,commands=0,logical=0,duplicates=0,dropped=0;

    static bool accepts(const IoHomeFrame &frame) {
        return validBody(frame) && ioHomeForeignKind(frame) == IoHomeForeignKind::Write;
    }

    bool observe(const IoHomeFrame &frame, uint32_t at) {
        if (!validBody(frame))
            return false;
        const bool actionable = accepts(frame);
        const auto command = frame.commandId;
        if (!actionable && command != IoHomeCommand::WritePrivate &&
            command != IoHomeCommand::RemoveController && command != IoHomeCommand::SendKey1W)
            return false;

        const uint16_t sequence = uint16_t(frame.data[frame.dataLen-2]) << 8 | frame.data[frame.dataLen-1];
        const uint32_t source = frame.getSrcNodeId();
        uint32_t signature = frame.getDestNodeId() << 8 | uint8_t(command);
        for (uint8_t i = 0; i < frame.dataLen - 2; ++i)
            signature = (signature ^ frame.data[i]) * 16777619U;

        ++physical;
        bool alreadyActionable = false;
        for (const auto &previous : seen) {
            if (!previous.valid || previous.source != source || previous.sequence != sequence ||
                uint32_t(at - previous.receivedMs) >= 2000U)
                continue;
            if (previous.signature == signature) {
                ++duplicates;
                return false;
            }
            alreadyActionable |= previous.actionable;
        }
        seen[nextSeen] = {source, at, signature, sequence, actionable, true};
        nextSeen = (nextSeen + 1) % 8;
        ++commands;
        if (!actionable || alreadyActionable)
            return false;
        ++logical;
        return true;
    }

    void push(const Entry &e) {
        if(count==4){++dropped;return;}
        entries[(head+count)%4]=e;++count;
    }
    bool pop(Entry &e){if(!count)return false;e=entries[head];head=(head+1)%4;--count;return true;}

private:
    static bool validBody(const IoHomeFrame &frame) {
        if (!(frame.ctrlByte0 & IOHC_CTRL0_MODE_1W) ||
            frame.dataLen < 2 || frame.dataLen > sizeof(frame.data))
            return false;
        if (frame.commandId == IoHomeCommand::SendKey1W)
            return frame.dataLen == 20;
        return frame.hasHmac;
    }
};
