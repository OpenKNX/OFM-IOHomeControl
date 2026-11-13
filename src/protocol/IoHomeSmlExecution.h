#pragma once
#include "IoHomeFrame.h"
// Issue 230 application-body layout only. This observation is not authenticated
// position/movement proof and must never be passed to the ordinary status decoder.
namespace IoHomeSmlExecution {
enum class State:uint8_t {Unknown,Waiting,Executing,Done};
struct Observation {State state=State::Unknown;uint16_t currentRaw=0;bool valid=false;};
inline Observation decode(const IoHomeFrame &f,const IoHomeProtocolIdentity &id,
                          uint32_t node,uint32_t owner,bool qualifiedSession) {
    if(!qualifiedSession||!id.valid||id.ioAddress!=node||id.manufacturerId!=1||id.profile!=2||id.subProfile!=0||
       f.getSrcNodeId()!=node||f.getDestNodeId()!=owner||f.commandId!=IoHomeCommand::PrivateResponse||
       (f.ctrlByte0&IOHC_CTRL0_MODE_1W)||f.dataLen!=8||f.data[1]!=0||f.data[4]!=0x80||f.data[6]!=0||f.data[7]!=0)return {};
    State state=f.data[0]==0x2B?State::Waiting:f.data[0]==0x2C?State::Executing:f.data[0]==0x2D?State::Done:State::Unknown;
    const uint16_t position=(uint16_t(f.data[2])<<8)|f.data[3];
    if(state==State::Unknown||position>IOHC_POSITION_MAX||
       (state==State::Executing?f.data[5]!=0xC8:f.data[5]!=0))return {};
    return {state,position,true};
}
inline const char *name(State s){return s==State::Waiting?"waiting":s==State::Executing?"executing":s==State::Done?"done":"unknown";}
}
