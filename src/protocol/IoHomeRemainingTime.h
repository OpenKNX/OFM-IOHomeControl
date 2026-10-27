#pragma once
#include <cstdint>
// Native main-info 03/04 layout only. Caller owns correlation/identity qualification.
// Source: laberning/home_io_control 821454537355a1d20f8f8622889611f4f491a11b.
struct IoHomeRemainingTime {
    enum class Kind : uint8_t {Unavailable,Positive,Zero,Unbounded,Undefined};
    uint16_t raw=0xFFFF;
    Kind kind=Kind::Unavailable;
    bool known() const {return kind==Kind::Positive;}
    static IoHomeRemainingTime fromRaw(uint16_t raw) {
        IoHomeRemainingTime out;out.raw=raw;
        out.kind=raw==0xFFFF?Kind::Undefined:raw==0xFFFE?Kind::Unbounded:raw==0?Kind::Zero:Kind::Positive;
        return out;
    }
    static IoHomeRemainingTime decode(const uint8_t *data,uint8_t length) {
        if(!data||length<8)return {};
        return fromRaw((uint16_t(data[6])<<8)|data[7]);
    }
};
