#define ARDUINO_ARCH_RP2040 1
#define IOHC_RP2040_CHECKED_STORAGE 1
#include "protocol/IoHomeCheckedJournal.h"
#include <cassert>
#include <cstdio>
int main(){
 LittleFS.mount=false;assert(!IoHomeRp2040Storage::begin()&&!LittleFS.autoFormat&&!IoHomeStorageBackend::available());LittleFS.mount=true;assert(IoHomeRp2040Storage::begin());
 IoHomeCheckedJournal<4> journal("journal");uint8_t a[4]{1,2,3,4},b[4]{4,3,2,1},out[4]{};assert(journal.commit(a));
 LittleFS.failRename=true;assert(!journal.commit(b));assert(journal.load(out)==IoHomeCheckedJournal<4>::Result::Found&&!std::memcmp(a,out,4));
 LittleFS.failRename=false;assert(journal.commit(b));assert(journal.load(out)==IoHomeCheckedJournal<4>::Result::Found&&!std::memcmp(b,out,4));
 char path[48];assert(!IoHomeRp2040Storage::path("../bad","key",path,sizeof(path)));assert(!IoHomeRp2040Storage::path("namespace","bad/key",path,sizeof(path)));
 puts("RP2040 LittleFS adapter contract: non-formatting mount, failed rename, checked readback passed; hardware unqualified");
}
