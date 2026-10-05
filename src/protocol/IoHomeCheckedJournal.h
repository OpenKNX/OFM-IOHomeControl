#pragma once
#include <cstdint>
#include <cstring>
#ifdef ESP32
#include <Preferences.h>
#endif
// Checked two-slot byte journal. CRC/readback establish storage integrity, not
// peer authenticity or physical power-cut acceptance. Nonempty corrupt slots
// and ambiguous equal generations fail closed. No automatic erase/rollback.
template<unsigned PayloadSize> class IoHomeCheckedJournal {
public:
 enum class Result:uint8_t {Missing,Found,Corrupt,Unavailable};
 static constexpr unsigned Size=PayloadSize+8;
 explicit IoHomeCheckedJournal(const char *name):mName(name){}
 Result load(uint8_t *out) {
  uint8_t a[Size]{},b[Size]{};int na=read(0,a),nb=read(1,b);
  if(na<0||nb<0)return Result::Unavailable;
  bool va=na==Size&&valid(a),vb=nb==Size&&valid(b);
  if((na&&!va)||(nb&&!vb)||(va&&vb&&gen(a)==gen(b)&&std::memcmp(a,b,Size)))return Result::Corrupt;
  if(!va&&!vb)return Result::Missing;
  if(!out)return Result::Unavailable;
  const uint8_t *p=!vb||(va&&gen(a)>=gen(b))?a:b;std::memcpy(out,p+6,PayloadSize);return Result::Found;
 }
 bool commit(const uint8_t *payload) {
  if(!payload)return false;
  uint8_t a[Size]{},b[Size]{},p[Size]{};int na=read(0,a),nb=read(1,b);
  if(na<0||nb<0)return false;bool va=na==Size&&valid(a),vb=nb==Size&&valid(b);
  if((na&&!va)||(nb&&!vb)||(va&&vb&&gen(a)==gen(b)&&std::memcmp(a,b,Size)))return false;
  bool latestA=!vb||(va&&gen(a)>=gen(b));uint32_t generation=va||vb?gen(latestA?a:b):0;
  if(generation==0xFFFFFFFF)return false;
  p[0]=1;for(unsigned i=0;i<4;i++)p[1+i]=(generation+1)>>(8*i);std::memcpy(p+6,payload,PayloadSize);
  uint16_t crc=checksum(p,Size-2);p[Size-2]=crc;p[Size-1]=crc>>8;
  unsigned slot=va||vb?(latestA?1:0):0;if(!write(slot,p))return false;
  uint8_t verify[Size]{};return read(slot,verify)==Size&&!std::memcmp(verify,p,Size);
 }
#ifdef TEST_NATIVE
 bool failWrites=false;int failAfterBytes=-1;
 void corrupt(unsigned slot){if(slot<2)data[slot][0]=0;}
#endif
private:
 const char *mName;
 static uint32_t gen(const uint8_t *p){return uint32_t(p[1])|uint32_t(p[2])<<8|uint32_t(p[3])<<16|uint32_t(p[4])<<24;}
 static uint16_t checksum(const uint8_t *p,unsigned n){uint16_t c=0;for(unsigned i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^((c&1)?0x8408:0);}return c;}
 static bool valid(const uint8_t *p){return p[0]==1&&p[5]==0&&gen(p)&&checksum(p,Size-2)==uint16_t(p[Size-2]|uint16_t(p[Size-1])<<8);}
 int read(unsigned slot,uint8_t *out){
#ifdef ESP32
  Preferences p;if(!p.begin(mName,false))return -1;size_t n=p.getBytesLength(slot?"b":"a");int result=!n?0:n==Size&&p.getBytes(slot?"b":"a",out,Size)==Size?Size:1;p.end();return result;
#elif defined(TEST_NATIVE)
  if(!length[slot])return 0;std::memcpy(out,data[slot],length[slot]);return length[slot];
#else
  return -1;
#endif
 }
 bool write(unsigned slot,const uint8_t *in){
#ifdef ESP32
  Preferences p;if(!p.begin(mName,false))return false;bool ok=p.putBytes(slot?"b":"a",in,Size)==Size;p.end();return ok;
#elif defined(TEST_NATIVE)
  if(failWrites)return false;unsigned n=failAfterBytes<0?Size:unsigned(failAfterBytes)>Size?Size:unsigned(failAfterBytes);std::memcpy(data[slot],in,n);length[slot]=n;return failAfterBytes<0;
#else
  return false;
#endif
 }
#ifdef TEST_NATIVE
 uint8_t data[2][Size]{};unsigned length[2]{};
#endif
};
