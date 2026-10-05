#pragma once
#include "IoHomeStorageBackend.h"
#include <stdint.h>
#include <cstring>
#ifdef ESP32
#include <Preferences.h>
#endif

// A single checked record couples global 2W identity and channel key bindings.
// Legacy flash remains the metadata mirror, not the identity commit authority.
class IoHomeNetworkStore {
public:
    struct Binding { uint32_t node=0; uint8_t key[16]{}; bool managed=false; };
    struct State { uint32_t controller=0; uint8_t key[16]{}; Binding channels[16]{}; };
    enum class Result:uint8_t { Missing,Found,Corrupt,Unavailable };
    static constexpr unsigned Size=348; // version + generation + global identity + 16 bindings + CRC
    Result load(State &out) {
        uint8_t a[Size]{},b[Size]{};int na=read(0,a),nb=read(1,b);
        if(na<0 || nb<0)return Result::Unavailable;
        bool va=na==Size&&valid(a),vb=nb==Size&&valid(b);
        if((na&&!va)||(nb&&!vb))return Result::Corrupt;
        if(va&&vb&&generation(a)==generation(b)&&std::memcmp(a,b,Size))return Result::Corrupt;
        if(!va&&!vb)return Result::Missing;
        const uint8_t *p=(!vb||(va&&generation(a)>=generation(b)))?a:b;
        State s;s.controller=node(p+5);std::memcpy(s.key,p+8,16);
        for(unsigned c=0;c<16;c++){const auto *q=p+24+c*20;s.channels[c].managed=q[0];s.channels[c].node=node(q+1);std::memcpy(s.channels[c].key,q+4,16);}
        out=s;return Result::Found;
    }
    bool commit(const State &s) {
        if(!s.controller || s.controller>0xFFFFFF)return false;
        uint8_t a[Size]{},b[Size]{},p[Size]{};int na=read(0,a),nb=read(1,b);
        if(na<0||nb<0)return false;
        bool va=na==Size&&valid(a),vb=nb==Size&&valid(b);
        if((na&&!va)||(nb&&!vb))return false;
        if(va&&vb&&generation(a)==generation(b)&&std::memcmp(a,b,Size))return false;
        const bool latestA=!vb||(va&&generation(a)>=generation(b));
        uint32_t gen=va||vb?generation(latestA?a:b):0;
        if(gen==0xFFFFFFFF)return false;
        p[0]=1;for(unsigned i=0;i<4;i++)p[1+i]=(gen+1)>>(8*i);
        putNode(p+5,s.controller);std::memcpy(p+8,s.key,16);
        for(unsigned c=0;c<16;c++) {
            if(s.channels[c].node>0xFFFFFF)return false;
            auto *q=p+24+c*20;q[0]=s.channels[c].managed;putNode(q+1,s.channels[c].node);std::memcpy(q+4,s.channels[c].key,16);
        }
        uint16_t crc=checksum(p,Size-2);p[Size-2]=crc;p[Size-1]=crc>>8;
        unsigned slot=va||vb?(latestA?1:0):0;
        if(!write(slot,p))return false;
        uint8_t verify[Size];return read(slot,verify)==Size&&!std::memcmp(p,verify,Size);
    }
    static bool equal(const State &a,const State &b) {
        if(a.controller!=b.controller||std::memcmp(a.key,b.key,16))return false;
        for(unsigned c=0;c<16;c++)if(a.channels[c].managed!=b.channels[c].managed||a.channels[c].node!=b.channels[c].node||std::memcmp(a.channels[c].key,b.channels[c].key,16))return false;
        return true;
    }
#ifdef TEST_NATIVE
    bool failWrites=false;
    int failAfterBytes=-1; // native fault model: interrupted record, not an ESP32 NVS simulation
    void injectRecord(unsigned slot,const uint8_t *record,unsigned length) {
        if(slot>=2||length>Size)return;std::memset(data[slot],0,Size);
        if(length&&record)std::memcpy(data[slot],record,length);storedLength[slot]=length;present[slot]=true;
    }
    const uint8_t *record(unsigned slot) const {return slot<2?data[slot]:nullptr;}
    void corrupt(unsigned slot){data[slot][0]=0;}
#endif
private:
    static uint32_t node(const uint8_t *p){return uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2];}
    static void putNode(uint8_t *p,uint32_t n){p[0]=n>>16;p[1]=n>>8;p[2]=n;}
    static uint32_t generation(const uint8_t *p){return uint32_t(p[1])|uint32_t(p[2])<<8|uint32_t(p[3])<<16|uint32_t(p[4])<<24;}
    static uint16_t checksum(const uint8_t *p,unsigned n){uint16_t crc=0;for(unsigned i=0;i<n;i++){crc^=p[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^((crc&1)?0x8408:0);}return crc;}
    static bool valid(const uint8_t *p) {
        if(p[0]!=1||!generation(p)||!node(p+5)||checksum(p,Size-2)!=uint16_t(p[Size-2]|uint16_t(p[Size-1])<<8))return false;
        for(unsigned c=0;c<16;c++)if(p[24+c*20]>1)return false;
        return p[344]==0&&p[345]==0;
    }
    int read(unsigned slot,uint8_t *out) {
        if(IoHomeStorageBackend::available())return IoHomeStorageBackend::read("iohcnet",slot?"b":"a",out,Size);
#ifdef ESP32
        Preferences p;if(!p.begin("iohcnet",false))return -1;
        const char *name=slot?"b":"a";size_t n=p.getBytesLength(name);
        int result=!n?0:n==Size&&p.getBytes(name,out,Size)==Size?Size:1;p.end();return result;
#elif defined(TEST_NATIVE)
        if(!present[slot])return 0;std::memcpy(out,data[slot],storedLength[slot]);return storedLength[slot];
#else
        return -1;
#endif
    }
    bool write(unsigned slot,const uint8_t *in) {
        if(IoHomeStorageBackend::available())return IoHomeStorageBackend::write("iohcnet",slot?"b":"a",in,Size);
#ifdef ESP32
        Preferences p;if(!p.begin("iohcnet",false))return false;
        bool ok=p.putBytes(slot?"b":"a",in,Size)==Size;p.end();return ok;
#elif defined(TEST_NATIVE)
        if(failWrites)return false;
        if(failAfterBytes>=0){const unsigned bytes=unsigned(failAfterBytes)>Size?Size:unsigned(failAfterBytes);injectRecord(slot,in,bytes);return false;}
        std::memcpy(data[slot],in,Size);storedLength[slot]=Size;present[slot]=true;return true;
#else
        return false;
#endif
    }
#ifdef TEST_NATIVE
    uint8_t data[2][Size]{};unsigned storedLength[2]{};bool present[2]{};
#endif
};
