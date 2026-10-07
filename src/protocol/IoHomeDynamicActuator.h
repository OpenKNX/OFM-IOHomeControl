#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include "IoHomeLogRedaction.h"

// Normalized KizBox2 product model. These fields are NOT an RF serialization.
// Neither the 8100/8103 schema nor the event scalar encoding is proven yet.
struct IoHomeActuatorCapabilities {
    static constexpr size_t MaxSensors=8, MaxEvents=16;
    bool valid=false;
    uint8_t sensorTypes[MaxSensors]{};
    uint8_t sensorTypeCount=0;
    uint16_t eventIds[MaxEvents]{};
    uint8_t eventIdCount=0;
    uint32_t parametersManagement=0;
    bool autonomyValid=false,autonomy=false;
    bool containsEvent(uint16_t id)const {
        if(!valid||eventIdCount>MaxEvents)return false;
        for(unsigned i=0;i<eventIdCount;++i)if(eventIds[i]==id)return true;
        return false;
    }
    bool containsSensor(uint8_t type)const {
        if(!valid||sensorTypeCount>MaxSensors)return false;
        for(unsigned i=0;i<sensorTypeCount;++i)if(sensorTypes[i]==type)return true;
        return false;
    }
    bool hasSunEnergy()const{return containsEvent(0x2005)&&containsSensor(0x06);}
    bool hasClosureSpeed()const{return valid&&(parametersManagement&0x8000U);}
    // Only a verified decoder may supply these normalized arrays. Atomic reject
    // on overflow/duplicates; never search opaque RF bytes for recognizable IDs.
    bool assign(const uint8_t *s,size_t ns,const uint16_t *e,size_t ne,uint32_t parameters) {
        if(ns>MaxSensors||ne>MaxEvents||(ns&&!s)||(ne&&!e))return false;
        for(size_t i=0;i<ns;++i)for(size_t j=0;j<i;++j)if(s[i]==s[j])return false;
        for(size_t i=0;i<ne;++i)for(size_t j=0;j<i;++j)if(e[i]==e[j])return false;
        IoHomeActuatorCapabilities next{};
        if(ns)std::memcpy(next.sensorTypes,s,ns);
        if(ne)std::memcpy(next.eventIds,e,ne*sizeof(uint16_t));
        next.sensorTypeCount=ns;next.eventIdCount=ne;next.parametersManagement=parameters;next.valid=true;
        *this=next;return true;
    }
};

struct IoHomeDynamicSubscription {
    uint16_t serverParameter=0x0FB0;
    uint8_t payload[2]{};
};
// Exact SERVER write payload, NOT an RF opcode. The server-to-RF bridge must
// be recovered before sending this through the radio. No leading 01 byte.
inline bool ioHomeDynamicSubscription(const IoHomeActuatorCapabilities &caps,
                                      uint16_t event,IoHomeDynamicSubscription &out) {
    if((event!=0x2001&&event!=0x2005)||!caps.containsEvent(event))return false;
    out={};out.payload[0]=uint8_t(event>>8);out.payload[1]=uint8_t(event);return true;
}

struct IoHomeDynamicValues {
    bool sunEnergyValid=false,luminanceValid=false,dynamicBatteryValid=false;
    uint16_t sunEnergyRaw=0;
    uint32_t luminanceLux=0;
    uint8_t dynamicBatteryRaw=0;
    uint32_t sunEnergyTimestampMs=0,batteryTimestampMs=0;
    // Input is a normalized, authenticated scalar from a verified decoder.
    // No caller in the RF receive path currently claims that decoding.
    bool acceptScalar(const IoHomeActuatorCapabilities &caps,uint16_t event,
                      uint8_t subtype,uint32_t value,uint32_t now) {
        if(event==0x2005&&subtype==0x06&&caps.hasSunEnergy()&&value<=0xFFFFU) {
            sunEnergyRaw=uint16_t(value);luminanceLux=value*110U;
            sunEnergyValid=luminanceValid=true;sunEnergyTimestampMs=now;return true;
        }
        if(event==0x2001&&subtype==0&&caps.containsEvent(event)&&value<=100) {
            dynamicBatteryRaw=uint8_t(value);dynamicBatteryValid=true;batteryTimestampMs=now;return true;
        }
        return false; // no percent publication and no invented SunEnergy unit
    }
};

// Unknown object chunks may contain pairing keys (e.g. A607). Only the bound
// dynamic metadata objects are eligible for raw object-chunk tracing.
inline std::string ioHomeDynamicTracePayload(IoHomeCommand command,const uint8_t *data,
                                             uint8_t length,bool dynamicObject) {
    if(command==IoHomeCommand::Unknown4AResponse&&!dynamicObject)return ioHomeRedactionMarker(length);
    return ioHomePayloadHexForLog(command,data,length);
}
