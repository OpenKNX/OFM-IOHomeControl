#pragma once
#include <stdint.h>
#include <cstring>

// Bounded transport state, independent of object semantics and RF authorization.
// Callers must validate the exchange/authentication before delivering replies.
// The transport word is explicit: no unproven business meaning is assigned.
class IoHomeObjectTransfer {
public:
    enum class Direction:uint8_t { Read,Write };
    enum class Stage:uint8_t { Idle,Opening,Ready,Waiting,Done,Rejected,Aborted,Cancelled,Timeout,Invalid };
    static constexpr uint16_t Capacity=1024;
    bool begin(Direction direction,uint32_t peer,uint32_t token,uint8_t provider,uint16_t key,
               uint16_t offset,uint16_t span,uint32_t now,uint32_t budget,uint8_t compatibility0=0,uint8_t compatibility2=0) {
        if(active()||!peer||peer>0xFFFFFF||!token||!budget||budget>=0x80000000||span>Capacity||uint32_t(offset)+span>65535)return false;
        // Known fixed-provider write contracts; dynamic providers stay opaque.
        const unsigned record=provider==0&&key==0x030A?20:((provider==0&&key<=3)||(provider==0x0B&&key==0xC000))?10:0;
        if(direction==Direction::Write&&record&&(offset%record||span!=record))return false;
        mDirection=direction;mPeer=peer;mToken=token;mStarted=now;mBudget=budget;mRequested=span;mNegotiated=0;mTransferred=0;mDisposition=0;mPendingSize=0;mControl=0;mRequestWord=0;
        std::memset(mData,0,sizeof(mData));std::memset(mPending,0,sizeof(mPending));
        mOpening[0]=compatibility0;mOpening[1]=provider;mOpening[2]=compatibility2&0x7F;
        put16(mOpening+3,key);put16(mOpening+5,offset);put16(mOpening+7,span);mStage=Stage::Opening;return true;
    }
    bool active() const {return mStage==Stage::Opening||mStage==Stage::Ready||mStage==Stage::Waiting;}
    Stage stage() const {return mStage;}
    uint8_t openingCommand() const {return mDirection==Direction::Read?0x46:0x48;}
    const uint8_t *openingData() const {return mOpening;}
    uint8_t disposition() const {return mDisposition;}
    uint16_t transferred() const {return mTransferred;}
    uint16_t negotiated() const {return mNegotiated;}
    const uint8_t *data() const {return mData;}
    bool acceptOpening(uint32_t peer,uint32_t token,uint8_t command,const uint8_t *p,uint8_t length) {
        if(mStage!=Stage::Opening||!matches(peer,token)||command!=(mDirection==Direction::Read?0x47:0x49)||!p||length!=4)return false;
        mControl=p[0];mDisposition=p[1];mNegotiated=get16(p+2);
        if(mDisposition>1){mStage=Stage::Rejected;return true;}
        if(mNegotiated>mRequested||mNegotiated>Capacity){mStage=Stage::Invalid;return false;}
        mStage=mDisposition==0&&!mNegotiated?Stage::Done:Stage::Ready;return true;
    }
    bool next(uint16_t transportWord,const uint8_t *payload=nullptr,uint8_t size=0) {
        if(mStage!=Stage::Ready||size>18||(size&&!payload))return false;
        const uint16_t remaining=mNegotiated-mTransferred;
        if(mDirection==Direction::Read&&size)return false;
        if(mDirection==Direction::Write&&size!=(remaining>18?18:remaining))return false;
        mPending[0]=mControl;put16(mPending+1,transportWord);if(size)std::memcpy(mPending+3,payload,size);
        mPendingSize=3+size;mRequestWord=transportWord;mStage=Stage::Waiting;return true;
    }
    const uint8_t *pendingData() const {return mPending;}
    uint8_t pendingSize() const {return mPendingSize;}
    bool acceptChunk(uint32_t peer,uint32_t token,const uint8_t *p,uint8_t size) {
        if(mStage!=Stage::Waiting||!matches(peer,token)||!p||size<3||size>21||(p[0]&0x7F)!=(mPending[0]&0x7F))return false;
        const uint16_t replyWord=get16(p+1);
        if(!replyWord&&mRequestWord){mStage=Stage::Aborted;return true;}
        const uint16_t remaining=mNegotiated-mTransferred;
        const uint8_t bytes=mDirection==Direction::Read?size-3:mPendingSize-3;
        if((mDirection==Direction::Write&&size!=3)||bytes>remaining||
            (mDirection==Direction::Read&&bytes&&bytes!=(remaining>18?18:remaining)))return false;
        if(bytes)std::memcpy(mData+mTransferred,mDirection==Direction::Read?p+3:mPending+3,bytes);
        mTransferred+=bytes;
        if(!replyWord&&!mRequestWord){mStage=mTransferred==mNegotiated?Stage::Done:Stage::Invalid;return mStage==Stage::Done;}
        const uint8_t sequence=mControl&0x7F;
        mControl=(mControl&0x80)|(sequence==0x7F?1:sequence+1);mStage=Stage::Ready;return true;
    }
    bool peerError(uint32_t peer,uint32_t token,const uint8_t *p,uint8_t size) {
        if(!active()||!matches(peer,token)||!p||!size)return false;mDisposition=p[0];mStage=Stage::Rejected;return true;
    }
    bool cancel(uint32_t token){if(!active()||token!=mToken)return false;mStage=Stage::Cancelled;return true;}
    void tick(uint32_t now){if(active()&&uint32_t(now-mStarted)>=mBudget)mStage=Stage::Timeout;}
private:
    static uint16_t get16(const uint8_t *p){return uint16_t(p[0])<<8|p[1];}
    static void put16(uint8_t *p,uint16_t value){p[0]=value>>8;p[1]=value;}
    bool matches(uint32_t peer,uint32_t token) const {return peer==mPeer&&token==mToken;}
    Direction mDirection=Direction::Read;Stage mStage=Stage::Idle;
    uint32_t mPeer=0,mToken=0,mStarted=0,mBudget=0;
    uint16_t mRequested=0,mNegotiated=0,mTransferred=0,mRequestWord=0;
    uint8_t mControl=0,mDisposition=0,mPendingSize=0,mOpening[9]{},mPending[21]{},mData[Capacity]{};
};
