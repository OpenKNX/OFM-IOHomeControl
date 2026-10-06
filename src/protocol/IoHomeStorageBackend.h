#pragma once
#include <stdint.h>
#ifdef ESP32
#include "IoHomeEsp32Storage.h"
#endif
#include <cstring>
#include <cstdio>
// Platform adapter contract: 0 missing, exact length success, -1 unavailable,
// any other positive length corrupt. Journals validate CRC/generation/readback.
class IoHomeStorageBackend {
public:
 using Read=int(*)(const char*,const char*,uint8_t*,unsigned);
 using Write=bool(*)(const char*,const char*,const uint8_t*,unsigned);
 static void install(Read read,Write write){mRead=read;mWrite=write;}
 static bool available(){return mRead&&mWrite;}
 static int read(const char *space,const char *key,uint8_t *out,unsigned size){return available()?mRead(space,key,out,size):-1;}
 static bool write(const char *space,const char *key,const uint8_t *data,unsigned size){return available()&&mWrite(space,key,data,size);}
private:
 inline static Read mRead=nullptr;inline static Write mWrite=nullptr;
};
#if defined(ARDUINO_ARCH_RP2040) && defined(IOHC_RP2040_CHECKED_STORAGE) && IOHC_RP2040_CHECKED_STORAGE
#include <LittleFS.h>
namespace IoHomeRp2040Storage {
inline bool path(const char *space,const char *key,char *out,unsigned size){
 if(!space||!key||!space[0]||!key[0]||std::strlen(space)>15||std::strlen(key)>15)return false;
 for(const char *s:{space,key})for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')))return false;
 return std::snprintf(out,size,"/iohc/%s_%s",space,key)>0;
}
inline int read(const char *space,const char *key,uint8_t *out,unsigned size){
 char name[48];if(!out||!path(space,key,name,sizeof(name)))return -1;
 if(!LittleFS.exists(name))return 0;auto f=LittleFS.open(name,"r");if(!f)return -1;
 if(f.size()!=size){f.close();return 1;}const int n=f.read(out,size);f.close();return n==int(size)?n:-1;
}
inline bool write(const char *space,const char *key,const uint8_t *data,unsigned size){
 char name[48],temp[52];if(!data||!path(space,key,name,sizeof(name)))return false;std::snprintf(temp,sizeof(temp),"%s.tmp",name);
 auto f=LittleFS.open(temp,"w");if(!f)return false;const bool complete=f.write(data,size)==size;f.flush();f.close();
 if(!complete)return false;
 // Replace only the inactive record. A leftover .tmp is never loaded as commit.
 return LittleFS.rename(temp,name);
}
inline bool begin(){
 // Never format a failed mount. No provisioning or record erase on failure.
 if(!LittleFS.setConfig(LittleFSConfig(false))||!LittleFS.begin())return false;
 if(!LittleFS.exists("/iohc")&&!LittleFS.mkdir("/iohc"))return false;
 IoHomeStorageBackend::install(read,write);return true;
}
}
#endif
