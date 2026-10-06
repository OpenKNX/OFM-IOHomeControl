#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
using esp_err_t=int;
using nvs_handle_t=unsigned;
constexpr int ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,ESP_ERR_NVS_TYPE_MISMATCH=2;
constexpr int NVS_READONLY=0;
namespace FakeNvs {
inline int openError=0,lengthError=0,readError=0;
inline size_t length=4;
inline unsigned opens=0,closes=0,queries=0,reads=0;
inline uint8_t data[4]{1,2,3,4};
inline void reset(){openError=lengthError=readError=0;length=4;opens=closes=queries=reads=0;}
}
inline esp_err_t nvs_open(const char*,int mode,nvs_handle_t *out){
 if(mode!=NVS_READONLY)return -99;
 ++FakeNvs::opens;*out=1;return FakeNvs::openError;
}
inline esp_err_t nvs_get_blob(nvs_handle_t,const char*,void *out,size_t *length){
 if(!out){++FakeNvs::queries;*length=FakeNvs::length;return FakeNvs::lengthError;}
 ++FakeNvs::reads;if(FakeNvs::readError)return FakeNvs::readError;
 *length=FakeNvs::length;if(*length<=4)std::memcpy(out,FakeNvs::data,*length);return ESP_OK;
}
inline void nvs_close(nvs_handle_t){++FakeNvs::closes;}
