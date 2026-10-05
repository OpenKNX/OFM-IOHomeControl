#pragma once
#include "IoHomeStorageBackend.h"
#include "IoHomeCrypto.h"
#include <cstring>
#include <cstdio>
#ifdef ESP32
#include <Preferences.h>
#endif
#ifdef TEST_NATIVE
#include <map>
#endif

// Independent two-record reservation journal. Never relies on void flash.save.
// NVS putBytes completion + readback is the software commit boundary; target
// power-cut qualification remains mandatory. Records are identity/key bound.
class IoHomeDurableReservation
{
public:
    enum class Load : uint8_t { Empty, Found, DifferentIdentity, Corrupt, Unavailable };
    static constexpr uint8_t Size = 34;
    struct Record { uint8_t bytes[Size] = {}; };
    static bool valid(const Record &r)
    {
        return r.bytes[0] == 1 && checksum(r.bytes, Size-2) ==
            uint16_t(r.bytes[Size-2] | (uint16_t(r.bytes[Size-1]) << 8));
    }
    static uint32_t generation(const Record &r)
    {
        return uint32_t(r.bytes[1]) | uint32_t(r.bytes[2])<<8 |
            uint32_t(r.bytes[3])<<16 | uint32_t(r.bytes[4])<<24;
    }
    Load load(uint8_t channel, uint32_t node, const uint8_t *key, uint16_t &watermark)
    {
        Record a,b; const int na=read(channel,0,a,node,key), nb=read(channel,1,b,node,key);
        if (na<0 || nb<0) return Load::Unavailable;
        const bool va=na==Size && valid(a), vb=nb==Size && valid(b);
        if ((na && !va) || (nb && !vb)) return Load::Corrupt;
        if (!va && !vb) return na==0 && nb==0 ? Load::Empty : Load::Corrupt;
        const Record &r=(!vb || (va && generation(a)>=generation(b))) ? a:b;
        if (r.bytes[5] != (node>>16 & 255) || r.bytes[6] != (node>>8 & 255) ||
            r.bytes[7] != (node & 255) || !key || std::memcmp(r.bytes+8,key,16))
            return Load::DifferentIdentity;
        watermark=uint16_t(r.bytes[24] | uint16_t(r.bytes[25])<<8);
        return Load::Found;
    }
    bool commit(uint8_t channel, uint32_t node, const uint8_t *key, uint16_t watermark)
    {
        if (!key || !node) return false;
        Record a,b; const int na=read(channel,0,a,node,key), nb=read(channel,1,b,node,key);
        if (na<0 || nb<0) return false;
        const bool va=na==Size && valid(a), vb=nb==Size && valid(b);
        if ((na && !va) || (nb && !vb)) return false;
        if (!va && !vb && (na || nb)) return false;
        if ((va && !matches(a,node,key)) || (vb && !matches(b,node,key))) return false;
        const bool latestA=!vb || (va && generation(a)>=generation(b));
        const uint32_t gen=va || vb ? generation(latestA ? a:b):0;
        if (gen==0xFFFFFFFF) return false;
        Record r; r.bytes[0]=1;
        for (uint8_t i=0;i<4;i++) r.bytes[1+i]=(gen+1)>>(8*i);
        r.bytes[5]=node>>16; r.bytes[6]=node>>8; r.bytes[7]=node;
        std::memcpy(r.bytes+8,key,16); r.bytes[24]=watermark; r.bytes[25]=watermark>>8;
        const uint16_t crc=checksum(r.bytes,Size-2);
        r.bytes[Size-2]=crc; r.bytes[Size-1]=crc>>8;
        const uint8_t slot=va || vb ? (latestA ? 1:0):0;
        if (!write(channel,slot,r,node,key)) return false;
        Record verify;
        return read(channel,slot,verify,node,key)==Size && !std::memcmp(r.bytes,verify.bytes,Size);
    }
#ifdef TEST_NATIVE
    bool failWrites=false;
    void corrupt(uint8_t c,uint8_t s) { mEntries[mLastIdentity[c]].records[s].bytes[0]=0; }
#endif
private:
    static uint16_t checksum(const uint8_t *p,uint8_t n)
    {
        uint16_t crc=0;
        for (uint8_t i=0;i<n;i++) { crc^=p[i]; for(uint8_t j=0;j<8;j++) crc=(crc>>1)^((crc&1)?0x8408:0); }
        return crc;
    }
    static bool matches(const Record &r,uint32_t node,const uint8_t *key) {
        return key && r.bytes[5]==(node>>16&255) && r.bytes[6]==(node>>8&255) &&
               r.bytes[7]==(node&255) && !std::memcmp(r.bytes+8,key,16);
    }
    // Identity-indexed records survive channel moves and temporary reassignment.
    // The 48-bit name hash only selects storage; the complete identity is checked
    // on load AND commit, so a hash collision fails closed.
    static uint64_t identityHash(uint32_t node,const uint8_t *key) {
        uint64_t h=14695981039346656037ULL;
        for(uint8_t i=0;i<3;i++){h^=uint8_t(node>>(8*i));h*=1099511628211ULL;}
        for(uint8_t i=0;i<16;i++){h^=key[i];h*=1099511628211ULL;}
        return h&0xFFFFFFFFFFFFULL;
    }
    int read(uint8_t c,uint8_t s,Record &r,uint32_t node,const uint8_t *key)
    {
        if (c>=16 || !key || !node) return -1;
        if(IoHomeStorageBackend::available()){char name[16];snprintf(name,sizeof(name),"i%012llx%c",static_cast<unsigned long long>(identityHash(node,key)),s?'b':'a');return IoHomeStorageBackend::read("iohcseq",name,r.bytes,Size);}
#ifdef ESP32
        Preferences p;
        if (!p.begin("iohcseq",false)) return -1;
        char name[16]; snprintf(name,sizeof(name),"i%012llx%c",static_cast<unsigned long long>(identityHash(node,key)),s?'b':'a');
        const size_t n=p.getBytesLength(name);
        const int result=n==0 ? 0:n==Size && p.getBytes(name,r.bytes,Size)==Size ? Size:1;
        p.end(); return result;
#elif defined(TEST_NATIVE)
        const auto id=identityHash(node,key); mLastIdentity[c]=id;
        auto &entry=mEntries[id];r=entry.records[s]; return entry.present[s] ? Size:0;
#else
        return -1; // no unverified fallback to void save
#endif
    }
    bool write(uint8_t c,uint8_t s,const Record &r,uint32_t node,const uint8_t *key)
    {
        if(c>=16||!node||!key)return false;
        if(IoHomeStorageBackend::available()){char name[16];snprintf(name,sizeof(name),"i%012llx%c",static_cast<unsigned long long>(identityHash(node,key)),s?'b':'a');return IoHomeStorageBackend::write("iohcseq",name,r.bytes,Size);}
#ifdef ESP32
        Preferences p; if (!p.begin("iohcseq",false)) return false;
        char name[16]; snprintf(name,sizeof(name),"i%012llx%c",static_cast<unsigned long long>(identityHash(node,key)),s?'b':'a');
        const bool ok=p.putBytes(name,r.bytes,Size)==Size; p.end(); return ok;
#elif defined(TEST_NATIVE)
        if (failWrites) return false;
        const auto id=identityHash(node,key);mLastIdentity[c]=id;
        auto &entry=mEntries[id];entry.records[s]=r;entry.present[s]=true;return true;
#else
        return false;
#endif
    }
#ifdef TEST_NATIVE
    struct Entry {Record records[2]{};bool present[2]{};};
    std::map<uint64_t,Entry> mEntries;
    uint64_t mLastIdentity[16]{};
#endif
};
