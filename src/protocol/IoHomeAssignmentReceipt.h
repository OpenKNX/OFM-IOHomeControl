#pragma once
#include "IoHomeCommands.h"
#include <cstring>
#include <cstdio>
#ifdef ESP32
#include <Preferences.h>
#endif

// Checked NVS receipt survives ETS project-update exceptions. Private key stays
// in device storage; online readback exposes identity/presentation only.
class IoHomeAssignmentReceipt
{
public:
    struct Value
    {
        uint32_t generation=0,node=0;
        uint8_t key[16]{};
        uint16_t profile=0;
        uint8_t subProfile=0,manufacturer=0,power=0xFF;
        bool metadataValid=false,metadataComplete=false,projectApplied=false;
    };
    enum class Result:uint8_t { Missing, Found, Corrupt, Unavailable };
    bool save(uint8_t channel,const Value &v)
    {
        if(channel>=16 || !v.node) return false;
        uint8_t data[36]{}; data[0]=1;
        for(uint8_t i=0;i<4;i++) data[1+i]=v.generation>>(8*i);
        for(uint8_t i=0;i<3;i++) data[5+i]=v.node>>(16-8*i);
        std::memcpy(data+8,v.key,16); data[24]=v.profile; data[25]=v.profile>>8;
        data[26]=v.subProfile; data[27]=v.manufacturer; data[28]=v.power;
        data[29]=(v.metadataValid?1:0)|(v.metadataComplete?2:0)|(v.projectApplied?4:0);
        const uint16_t crc=checksum(data,34); data[34]=crc; data[35]=crc>>8;
        if(!write(channel,data)) return false;
        uint8_t verify[36]; return read(channel,verify)==36 && !std::memcmp(data,verify,36);
    }
    Result load(uint8_t channel,Value &v)
    {
        uint8_t data[36]; const int n=read(channel,data);
        if(n<0) return Result::Unavailable; if(n==0) return Result::Missing;
        if(n!=36 || data[0]!=1 || checksum(data,34)!=uint16_t(data[34]|uint16_t(data[35])<<8)) return Result::Corrupt;
        Value next;
        for(uint8_t i=0;i<4;i++) next.generation|=uint32_t(data[1+i])<<(8*i);
        for(uint8_t i=0;i<3;i++) next.node=(next.node<<8)|data[5+i];
        if(!next.node || data[26]>63 || (data[29]&~7)) return Result::Corrupt;
        std::memcpy(next.key,data+8,16); next.profile=data[24]|uint16_t(data[25])<<8;
        next.subProfile=data[26]; next.manufacturer=data[27]; next.power=data[28];
        next.metadataValid=data[29]&1; next.metadataComplete=data[29]&2; next.projectApplied=data[29]&4;
        v=next; return Result::Found;
    }
    bool erase(uint8_t channel)
    {
#ifdef ESP32
        Preferences p; if(!p.begin("iohcassign",false)) return false;
        char name[8]; snprintf(name,sizeof(name),"c%u",channel);
        const bool ok=!p.isKey(name)||p.remove(name); p.end(); return ok;
#elif defined(TEST_NATIVE)
        mPresent[channel]=false; return true;
#else
        return false;
#endif
    }
#ifdef TEST_NATIVE
    bool failWrites=false;
#endif
private:
    static uint16_t checksum(const uint8_t *p,uint8_t n)
    { uint16_t crc=0;for(uint8_t i=0;i<n;i++){crc^=p[i];for(uint8_t j=0;j<8;j++)crc=(crc>>1)^((crc&1)?0x8408:0);}return crc; }
    int read(uint8_t c,uint8_t *data)
    {
        if(c>=16) return -1;
#ifdef ESP32
        Preferences p; if(!p.begin("iohcassign",false)) return -1;
        char name[8]; snprintf(name,sizeof(name),"c%u",c); const size_t n=p.getBytesLength(name);
        const int result=n==0?0:n==36 && p.getBytes(name,data,36)==36?36:1;
        p.end(); return result;
#elif defined(TEST_NATIVE)
        if(!mPresent[c])return 0;std::memcpy(data,mData[c],36);return 36;
#else
        return -1;
#endif
    }
    bool write(uint8_t c,const uint8_t *data)
    {
#ifdef ESP32
        Preferences p;if(!p.begin("iohcassign",false))return false;
        char name[8];snprintf(name,sizeof(name),"c%u",c);
        const bool ok=p.putBytes(name,data,36)==36;p.end();return ok;
#elif defined(TEST_NATIVE)
        if(failWrites)return false;std::memcpy(mData[c],data,36);mPresent[c]=true;return true;
#else
        return false;
#endif
    }
#ifdef TEST_NATIVE
    uint8_t mData[16][36]{};bool mPresent[16]{};
#endif
};
