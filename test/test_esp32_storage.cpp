#include "protocol/IoHomeEsp32Storage.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 using namespace FakeNvs;
 uint8_t out[4]{};
 auto read=[&](){return IoHomeEsp32Storage::read("iohcnet","a",out,4);};
 reset();openError=ESP_ERR_NVS_NOT_FOUND;assert(read()==0&&queries==0&&closes==0);
 reset();openError=-10;assert(read()==-1&&queries==0&&closes==0);
 reset();lengthError=ESP_ERR_NVS_NOT_FOUND;assert(read()==0&&reads==0&&closes==1);
 reset();lengthError=ESP_ERR_NVS_TYPE_MISMATCH;assert(read()==1&&reads==0&&closes==1);
 reset();lengthError=-10;assert(read()==-1&&reads==0&&closes==1);
 for(size_t n:{size_t(0),size_t(3),size_t(5)}){
  reset();length=n;assert(read()==1&&reads==0&&closes==1);
 }
 reset();assert(read()==4&&reads==1&&closes==1&&!std::memcmp(out,data,4));
 reset();readError=-10;assert(read()==-1&&reads==1&&closes==1);
 puts("ESP32 NVS reader: missing namespace/key, corrupt type/size, I/O failure and valid read passed");
}
