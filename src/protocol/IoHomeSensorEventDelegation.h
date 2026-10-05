#pragma once
#include <stdint.h>
#include <cstring>

// Recovered legacy RAM database, not automatic KNX alarm subscriptions.
// Callers resolve/authenticate the SensorNode before applying a selector.
class IoHomeSensorEventDelegation {
public:
    enum class Selector:uint8_t {RemoveExact=0,Upsert=1,RemoveController=2,RemoveSensor=3,Query=0xFF};
    struct Record {uint8_t bytes[8]{};uint32_t sensor=0,revision=0;};
    struct Reply {uint8_t state=0;uint16_t sensorValue=0;};
    static bool decodeReply(const uint8_t *data,uint8_t size,Reply &out){
        if(!data||size!=4||data[0]!=5)return false;
        out={data[1],uint16_t(uint16_t(data[2])<<8|data[3])};return true;
    }
    static bool decodeSelector(uint8_t value,Selector &out){
        if(value>3&&value!=0xFF)return false;out=Selector(value);return true;
    }
    // Exact 8-byte record: active, SensorNode handle, controller[3], option,
    // state, unused byte. Unnamed option/state bits remain uninterpreted.
    bool apply(Selector selector,uint32_t sensor,uint8_t handle,uint32_t revision,
               const uint8_t *controller,uint8_t option){
        if(!controller||!sensor||sensor>0xFFFFFF||!revision)return false;
        if(selector==Selector::Query)return true; // strictly non-mutating
        if(uint8_t(selector)>3)return false;
        for(auto &r:mRecords)if(r.bytes[0]&&r.sensor==sensor&&r.revision!=revision)r={};
        Record *exact=nullptr,*free=nullptr;
        for(auto &r:mRecords){
            if(!r.bytes[0]){if(!free)free=&r;continue;}
            const bool sameController=!std::memcmp(r.bytes+2,controller,3);
            const bool sameSensor=r.sensor==sensor&&r.revision==revision;
            if(sameSensor&&sameController)exact=&r;
            if((selector==Selector::RemoveController&&sameController)||
               (selector==Selector::RemoveSensor&&sameSensor))r={};
        }
        if(selector==Selector::RemoveExact){if(exact)*exact={};return true;}
        if(selector!=Selector::Upsert)return true;
        if(exact){exact->bytes[6]=1;return true;} // preserve existing option
        if(!free)return false;
        free->sensor=sensor;free->revision=revision;free->bytes[0]=1;free->bytes[1]=handle;
        std::memcpy(free->bytes+2,controller,3);free->bytes[5]=option;free->bytes[6]=1;
        return true;
    }
    void invalidate(uint32_t sensor){for(auto &r:mRecords)if(r.sensor==sensor)r={};}
    const Record *records()const{return mRecords;}
    static constexpr uint8_t Capacity=16; // chosen bounded OFM RAM capacity
private:
    Record mRecords[Capacity]{};
};
