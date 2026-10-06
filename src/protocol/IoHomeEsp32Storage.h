#pragma once
#include <nvs.h>
#include <stdint.h>

namespace IoHomeEsp32Storage {
// Missing records are normal before provisioning. Query NVS directly so only
// NOT_FOUND means missing; wrong types/sizes are corrupt and I/O failures remain
// unavailable. Opening read-only also avoids creating empty namespaces on boot.
inline int read(const char *space,const char *key,uint8_t *out,unsigned size) {
    nvs_handle_t handle;
    const esp_err_t opened=nvs_open(space,NVS_READONLY,&handle);
    if(opened==ESP_ERR_NVS_NOT_FOUND)return 0;
    if(opened!=ESP_OK)return -1;
    size_t length=0;
    esp_err_t error=nvs_get_blob(handle,key,nullptr,&length);
    int result;
    if(error==ESP_ERR_NVS_NOT_FOUND)result=0;
    else if(error==ESP_ERR_NVS_TYPE_MISMATCH)result=1;
    else if(error!=ESP_OK)result=-1;
    else if(length!=size)result=1;
    else {
        error=nvs_get_blob(handle,key,out,&length);
        result=error==ESP_OK&&length==size?int(size):-1;
    }
    nvs_close(handle);
    return result;
}
}
