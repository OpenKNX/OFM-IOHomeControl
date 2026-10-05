#pragma once
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <initializer_list>
struct LittleFSConfig {bool autoFormat;explicit LittleFSConfig(bool format=true):autoFormat(format){}};
struct FakeFile {
 std::vector<uint8_t> *data=nullptr;size_t cursor=0;
 explicit operator bool()const{return data!=nullptr;}
 size_t size()const{return data?data->size():0;}
 int read(uint8_t *out,size_t size){if(!data||cursor+size>data->size())return -1;std::memcpy(out,data->data()+cursor,size);cursor+=size;return size;}
 size_t write(const uint8_t *in,size_t size){if(!data)return 0;data->assign(in,in+size);return size;}
 void flush(){}void close(){}
};
struct FakeLittleFS {
 bool autoFormat=true,mount=true,failRename=false;std::map<std::string,std::vector<uint8_t>> files;
 bool setConfig(const LittleFSConfig &c){autoFormat=c.autoFormat;return true;}
 bool begin(){return mount;}
 bool exists(const char *path){return files.count(path);}
 bool mkdir(const char *path){files[path]={};return true;}
 FakeFile open(const char *path,const char *mode){if(mode[0]=='w'){files[path].clear();return {&files[path],0};}auto i=files.find(path);return i==files.end()?FakeFile{}:FakeFile{&i->second,0};}
 bool rename(const char *from,const char *to){if(failRename||!exists(from))return false;files[to]=files[from];files.erase(from);return true;}
};
inline FakeLittleFS LittleFS;
