#pragma once
#include "IoHomeFrame.h"
#include "IoHomeCrypto.h"
#include <cstring>

// Explicit commissioning proof, separate from heater pull and ordinary pairing.
// No key export is possible before a known member's cryptographic response.
class IoHomeControllerKeyImport {
public:
    enum class Stage:uint8_t {Idle,Pulling,MemberRequest,MemberReply,MemberChallenge,MemberMac,Verified,Committed,Rejected,TimedOut,Cancelled,Failed};
    Stage stage=Stage::Idle;
    uint32_t session=0,owner=0,hub=0,member=0,started=0,phaseStarted=0;
    uint8_t frequency=0xFF;
    bool active()const{return stage>=Stage::Pulling&&stage<=Stage::MemberMac;}
    bool begin(uint32_t token,uint32_t local,uint32_t peer,uint32_t knownMember,uint32_t now) {
        if(active()||!token||!local||!peer||!knownMember||local==peer||local==knownMember||peer==knownMember||
           local>0xFFFFFF||peer>0xFFFFFF||knownMember>0xFFFFFF)return false;
        wipe();session=token;owner=local;hub=peer;member=knownMember;started=phaseStarted=now;stage=Stage::Pulling;return true;
    }
    bool candidate(const uint8_t *key,uint32_t now) {
        if(stage!=Stage::Pulling||!key||expired(now))return false;
        uint8_t nonzero=0;for(unsigned i=0;i<16;++i)nonzero|=key[i];if(!nonzero){reject();return false;}
        std::memcpy(candidateKey,key,16);stage=Stage::MemberRequest;phaseStarted=now;return true;
    }
    void requestSent(uint8_t channel,uint32_t now) {
        if(stage!=Stage::MemberRequest||channel>=3)return;
        frequency=channel;phaseStarted=now;stage=Stage::MemberReply;
    }
    bool memberReply(const IoHomeFrame &frame,uint8_t channel,uint32_t now) {
        if(stage!=Stage::MemberReply||!bound(frame,channel)||expired(now)||uint32_t(now-phaseStarted)>1500||
           frame.commandId!=IoHomeCommand::NodeVerifyResponse||frame.dataLen!=3||
           frame.data[0]!=uint8_t(member>>16)||frame.data[1]!=uint8_t(member>>8)||frame.data[2]!=uint8_t(member))return false;
        transcript[0]=uint8_t(IoHomeCommand::NodeVerifyResponse);std::memcpy(transcript+1,frame.data,3);
        stage=Stage::MemberChallenge;phaseStarted=now;return true;
    }
    const uint8_t *challenge() {
        if(stage!=Stage::MemberChallenge)return nullptr;
        if(!challengeValid){IoHomeCrypto::generateChallenge(nonce);challengeValid=true;}
        return nonce;
    }
    void challengeSent(uint32_t now) {if(stage==Stage::MemberChallenge&&challengeValid){stage=Stage::MemberMac;phaseStarted=now;}}
    bool memberProof(const IoHomeFrame &frame,uint8_t channel,uint32_t now) {
        if(stage!=Stage::MemberMac||!bound(frame,channel)||expired(now)||uint32_t(now-phaseStarted)>1500||
           frame.commandId!=IoHomeCommand::ChallengeResponse)return false;
        uint8_t expected[6]{},difference=0xFF;
        if(frame.dataLen==6&&IoHomeCrypto::createHmac2W(transcript,4,nonce,candidateKey,expected)) {
            difference=0;for(unsigned i=0;i<6;++i)difference|=expected[i]^frame.data[i];
        }
        if(difference){reject();return false;}
        stage=Stage::Verified;return true;
    }
    bool copyVerifiedKey(uint8_t (&out)[16])const {
        if(stage!=Stage::Verified)return false;std::memcpy(out,candidateKey,16);return true;
    }
    bool expired(uint32_t now)const{return uint32_t(now-started)>30000U;}
    void reject(){wipe();stage=Stage::Rejected;}
    void cancel(){wipe();stage=Stage::Cancelled;}
    void timeout(){wipe();stage=Stage::TimedOut;}
    void fail(){wipe();stage=Stage::Failed;}
    void committed(){if(stage==Stage::Verified){wipe();stage=Stage::Committed;}}
private:
    uint8_t candidateKey[16]{},nonce[6]{},transcript[4]{};bool challengeValid=false;
    bool bound(const IoHomeFrame &frame,uint8_t channel)const {
        return channel==frequency&&frame.getSrcNodeId()==member&&frame.getDestNodeId()==owner&&!(frame.ctrlByte0&IOHC_CTRL0_MODE_1W);
    }
    void wipe(){std::memset(candidateKey,0,16);std::memset(nonce,0,6);std::memset(transcript,0,4);challengeValid=false;frequency=0xFF;}
};
