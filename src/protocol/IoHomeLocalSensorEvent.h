#pragma once
#include "IoHomeStatusUpdate.h"

// Source: https://github.com/laberning/home_io_control/issues/156#issuecomment-6665992911
// Published application payloads, not complete RF frames. No physical unit is proven.
struct IoHomeLocalSensorEvent {
    uint16_t raw = 0;
    uint8_t auxiliary = 0; // Opaque; B9 is not a protocol constant.
    static bool decode(IoHomeCommand command,const uint8_t *data,uint8_t length,IoHomeLocalSensorEvent &out) {
        if(command!=IoHomeCommand::StatusUpdate||!data||length!=10||
           data[2]!=0x20||data[3]!=0x05||data[4]!=5||data[5]!=6||data[6]!=0)return false;
        out.raw=(uint16_t(data[7])<<8)|data[8];out.auxiliary=data[9];return true;
    }
    static bool decode(const IoHomeFrame &frame,IoHomeLocalSensorEvent &out) {
        return !(frame.ctrlByte0&IOHC_CTRL0_MODE_1W)&&decode(frame.commandId,frame.data,frame.dataLen,out);
    }
};
struct IoHomeLocalSensorSample {
    uint32_t receivedAtMs=0,verifiedAtMs=0,source=0,destination=0;
    IoHomeLocalSensorEvent value;
    uint8_t frequencyIndex=0xFF;
    bool valid=false;
    void clear(){*this={};}
    // Only call from the existing inbound-challenge authenticated channel dispatch.
    void record(const IoHomeFrame &frame,uint8_t frequency,uint32_t received,uint32_t verified) {
        IoHomeLocalSensorEvent decoded;
        if(!IoHomeLocalSensorEvent::decode(frame,decoded))return;
        value=decoded;source=frame.getSrcNodeId();destination=frame.getDestNodeId();
        frequencyIndex=frequency;receivedAtMs=received;verifiedAtMs=verified;valid=true;
    }
};
static_assert(sizeof(IoHomeLocalSensorSample)<=24,"Bound per-channel raw sensor diagnostics");
