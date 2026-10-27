#pragma once
#include <cstdint>
// Volatile, key-free diagnostic evidence. Never authorizes RF or assignment.
struct IoHomeDiscoveryEvidence {
    enum Flag : uint8_t {Passive2f=1,PassiveChallenge=2,PassiveAuth=4,Active2b=8,Directed2f=16,DirectedAuth=32,Persisted=64};
    enum class Failure : uint8_t {None,Queued,NoReply,QueueBusy};
    struct Node {
        uint32_t node=0,firstSeen=0,lastSeen=0,src=0,dst=0;
        int16_t rssi=0;
        uint8_t flags=0,freq=0xFF,request=0,response=0,channel=0;
        Failure failure=Failure::None;
        uint8_t optionalConfig=0xFF;
        bool rxKnown=false,assignable=false;
        uint8_t etsActive=0; // 0 unknown, 1 reported inactive, 2 reported active; volatile
        const char *metadata() const {return flags&Active2b?"fresh_2b":flags&Directed2f?"directed_2f":flags&Persisted?"flash":"unknown";}
    };
    // Eight simultaneous nodes fit the smallest production target; overflow is explicit.
    static constexpr uint8_t Capacity=8;
    Node nodes[Capacity]{};
    uint32_t session=0,activePackets=0,duplicates=0,overflow=0,originalHub=0,extractionNode=0;
    uint16_t candidateSkipped=0,candidateTimeout=0;
    uint8_t count=0;
    void reset(){uint32_t next=session+1;*this=IoHomeDiscoveryEvidence();session=next?next:1;}
    Node *find(uint32_t node){for(uint8_t i=0;i<count;i++)if(nodes[i].node==node)return &nodes[i];return nullptr;}
    Node *touch(uint32_t node,uint32_t now){
        if(!node||node>0xFFFFFF)return nullptr;
        if(auto *p=find(node)){p->lastSeen=now;return p;}
        if(count==Capacity){++overflow;return nullptr;}
        auto *p=&nodes[count++];p->node=node;p->firstSeen=p->lastSeen=now;return p;
    }
    void active(uint32_t node,uint32_t now){++activePackets;if(auto *p=touch(node,now)){if(p->flags&Active2b)++duplicates;p->flags|=Active2b;}}
    unsigned unique(uint8_t flag)const{unsigned n=0;for(uint8_t i=0;i<count;i++)if(nodes[i].flags&flag)++n;return n;}
};
static_assert(sizeof(IoHomeDiscoveryEvidence)<=300,"Bound diagnostic RAM on REG1 IP");
