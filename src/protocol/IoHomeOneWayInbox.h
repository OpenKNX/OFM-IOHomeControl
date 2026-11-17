#pragma once
#include "IoHomeForeignActivity.h"

// Physical RX metrics are distinct from logical remote presses. These are
// overheard activity observations, never proof that a motor received a command.
struct IoHomeOneWayInbox {
    struct Entry {IoHomeFrame frame;uint32_t receivedMs=0;int16_t rssi=0;uint8_t frequency=0;};
    struct Seen {uint32_t source=0,receivedMs=0;uint16_t sequence=0;bool valid=false;};
    Entry entries[4]{};Seen seen[8]{};
    uint8_t head=0,count=0,nextSeen=0;
    uint32_t physical=0,logical=0,duplicates=0,dropped=0;
    static bool accepts(const IoHomeFrame &f) {
        return (f.ctrlByte0&IOHC_CTRL0_MODE_1W)&&ioHomeForeignKind(f)==IoHomeForeignKind::Write;
    }
    bool observe(const IoHomeFrame &f,uint32_t at) {
        if(!accepts(f))return false;
        ++physical;
        const uint16_t sequence=(uint16_t(f.data[f.dataLen-2])<<8)|f.data[f.dataLen-1];
        for(const auto &s:seen)if(s.valid&&s.source==f.getSrcNodeId()&&s.sequence==sequence&&uint32_t(at-s.receivedMs)<2000U){++duplicates;return false;}
        seen[nextSeen]={f.getSrcNodeId(),at,sequence,true};nextSeen=(nextSeen+1)%8;++logical;return true;
    }
    void push(const Entry &e) {
        if(count==4){++dropped;return;}
        entries[(head+count)%4]=e;++count;
    }
    bool pop(Entry &e){if(!count)return false;e=entries[head];head=(head+1)%4;--count;return true;}
};
