#pragma once
#include <stdint.h>
class IoHomeRadioDiversity {
public:
    // Reference-inspired 83*3 ms experiment; not a Semtech hardware requirement.
    bool configure(uint16_t cooldownMs){if(cooldownMs>2000)return false;mCooldown=cooldownMs;return true;}
    uint16_t cooldownMs()const{return mCooldown;}
    void activity(uint8_t channel,uint32_t now){if(channel<3){mAt[channel]=now;mSeen|=1U<<channel;}}
    bool recent(uint8_t channel,uint32_t now)const{return channel<3&&mCooldown&&(mSeen&(1U<<channel))&&uint32_t(now-mAt[channel])<mCooldown;}
    uint8_t alternate(uint8_t current,uint32_t now)const{
        if(current>=3)return 0;
        for(uint8_t step=1;step<3;++step){const uint8_t next=(current+step)%3;if(!recent(next,now))return next;}
        return (current+1)%3; // All recent: still make bounded forward progress.
    }
private:
    uint16_t mCooldown=250;uint8_t mSeen=0;uint32_t mAt[3]{};
};
