// Typed actions reuse the bounded existing register job; no universal 0C61 endpoint.
bool IoHomeController::qualifiedHeatingName(IoHomecontrolChannel *ch,IoHomeAdelis::HeatingModel model)const {
    if(!ch||ch->selectedHeatingModel()!=model)return false;
    for(auto *p=mHeatingNames.get();p;p=p->next.get())if(p->channel==channelIndexFor(ch))
        return p->model==model&&p->gateway==mOwnNodeId&&!std::memcmp(p->systemKey,mSystemKey,16)&&
            sampleIdentityMatches(p->channel,p->node,p->key,p->revision)&&uint32_t(millis()-p->receivedMs)<300000U;
    return false;
}
void IoHomeController::recordHeatingModelName(IoHomecontrolChannel *ch,const uint8_t *data,uint8_t length,uint32_t rx) {
    if(!ch||!IoHomeAdelis::identity(ch->getProtocolIdentity(),ch->getNodeId()))return;
    HeatingNameProof *p=mHeatingNames.get();while(p&&p->channel!=channelIndexFor(ch))p=p->next.get();
    if(!p){std::unique_ptr<HeatingNameProof> entry(new(std::nothrow) HeatingNameProof);if(!entry)return;
        entry->next=std::move(mHeatingNames);mHeatingNames=std::move(entry);p=mHeatingNames.get();}
    p->channel=channelIndexFor(ch);p->node=ch->getNodeId();p->gateway=mOwnNodeId;p->revision=ch->productContextRevision();p->receivedMs=rx;
    std::memcpy(p->key,ch->getEncryptionKey(),16);std::memcpy(p->systemKey,mSystemKey,16);p->model=IoHomeAdelis::HeatingModel::Unknown;
    for(auto model:{IoHomeAdelis::HeatingModel::Agilia,IoHomeAdelis::HeatingModel::Adelis})if(IoHomeAdelis::modelName(model,data,length))p->model=model;
}

bool IoHomeController::startAdelisObservationRead(IoHomecontrolChannel *channel) {
    if(!channel||channel->selectedHeatingModel()!=IoHomeAdelis::HeatingModel::Adelis||
       !startAtlanticRegisterRead(channel,1,0x19,1,IoHomeAtlanticRead::Model::Raw))return false;
    // One explicit job, no new periodic poll. The first authenticated name read
    // must match before any other register is requested.
    auto &job=*mRegisterRead;job.model=IoHomeAtlanticRead::Model::AdelisClassic;
    constexpr uint16_t registers[]={0x119,0x115,0x136,0x126,0x103,0x138,0x13E,0x13A,0x137};
    job.count=sizeof(registers)/sizeof(registers[0]);
    for(uint8_t i=0;i<job.count;++i)job.samples[i].reg=registers[i];
    return true;
}
bool IoHomeController::startAdelisAction(IoHomecontrolChannel *ch,uint16_t reg,uint8_t value) {
    uint8_t payload[6];if(!ch||ch->selectedHeatingModel()!=IoHomeAdelis::HeatingModel::Adelis||!IoHomeAdelis::write(reg,value,payload))return false;
    if(!startAtlanticRegisterRead(ch,1,0x19,1,IoHomeAtlanticRead::Model::Raw))return false;
    auto &s=*mRegisterRead;s.action=IoHomeAtlanticRead::Action::AdelisWrite;s.model=IoHomeAtlanticRead::Model::AdelisClassic;
    s.count=3;s.writeIndex=1;s.desired=value;s.samples[1].reg=s.samples[2].reg=reg;return true;
}
bool IoHomeController::startDhwMode(IoHomecontrolChannel *ch,uint8_t mode) {
    if(!ch||mode>2||!IoHomeDhwCodec::identity(ch->getProtocolIdentity(),ch->getNodeId())||!startAtlanticRegisterRead(ch,0,6,1,IoHomeAtlanticRead::Model::Calypso))return false;
    auto &s=*mRegisterRead;s.action=IoHomeAtlanticRead::Action::DhwMode;s.count=4;s.writeIndex=1;s.desired=mode;
    s.samples[1].reg=s.samples[2].reg=6;s.samples[3].reg=0xFFFF;return true;
}
bool IoHomeController::startDhwTarget(IoHomecontrolChannel *ch,float celsius) {
    uint16_t target=0;if(!ch||!IoHomeDhwCodec::encodeConfirmedTarget(celsius,200,true,target)||
       !IoHomeDhwCodec::identity(ch->getProtocolIdentity(),ch->getNodeId())||!startAtlanticRegisterRead(ch,0,8,1,IoHomeAtlanticRead::Model::Calypso))return false;
    auto &s=*mRegisterRead;s.action=IoHomeAtlanticRead::Action::DhwTarget;s.count=5;s.writeIndex=2;s.targetRaw=target;
    s.samples[1].reg=6;s.samples[2].reg=0xFFFF;s.samples[3].reg=6;s.samples[4].reg=0xFFFF;return true;
}
bool IoHomeController::decodeAtlanticJobReply(const IoHomeQueueEntry &entry,IoHomeAtlanticRead::Sample &out)const {
    if(!mRegisterRead)return false;const auto &s=*mRegisterRead;const uint16_t reg=s.samples[s.completed].reg;
    if(entry.command==IoHomeCommand::Private||entry.command==IoHomeCommand::Execute){
        uint16_t target;if(!IoHomeDhwCodec::statusTarget(mRxFrame,target))return false;
        out={};out.reg=reg;out.result=IoHomeAtlanticRead::Result::Value;out.length=mRxFrame.dataLen;std::memcpy(out.data,mRxFrame.data,out.length);return true;
    }
    if(s.completed==s.writeIndex){
        if(mRxFrame.commandId!=IoHomeCommand::WritePrivateResponse||mRxFrame.dataLen!=entry.heatingLength||std::memcmp(mRxFrame.data,entry.heatingData,entry.heatingLength))return false;
        out={};out.reg=reg;out.result=IoHomeAtlanticRead::Result::Value;out.length=mRxFrame.dataLen-4;std::memcpy(out.data,mRxFrame.data+4,out.length);return true;
    }
    return mRxFrame.commandId==IoHomeCommand::WritePrivateResponse&&IoHomeAtlanticRead::reply(reg,mRxFrame.data,mRxFrame.dataLen,out);
}
#ifdef TEST_NATIVE
// Precondition for existing AGILIA regressions: a prior synthetic qualified name read.
void IoHomeController::testSeedAgiliaName(IoHomecontrolChannel *ch) {
    const uint8_t name[]={'A','G','I','L','I','A',' ','I','O'};recordHeatingModelName(ch,name,sizeof(name),millis());
}
#endif

bool IoHomeController::configureAtlanticPoll(IoHomecontrolChannel *ch,uint16_t seconds) {
    const uint8_t c=channelIndexFor(ch);if(!ch||c>=16)return false;
    auto *p=mAtlanticPolls.get();while(p&&p->channel!=c)p=p->next.get();
    if(!seconds){if(p){p->seconds=0;if(mRegisterRead&&mRegisterRead->active&&mRegisterRead->token==p->token)cancelAtlanticRegisterRead(p->token);}return true;}
    const bool dhw=IoHomeDhwCodec::identity(ch->getProtocolIdentity(),ch->getNodeId());
    const bool adelis=ch->selectedHeatingModel()==IoHomeAdelis::HeatingModel::Adelis&&IoHomeAdelis::identity(ch->getProtocolIdentity(),ch->getNodeId());
    if(!ch->isPaired()||ch->is1W()||(!dhw&&!adelis)||seconds<(dhw?300:60))return false;
    if(!p){std::unique_ptr<AtlanticPoll> entry(new(std::nothrow) AtlanticPoll);if(!entry)return false;
        entry->next=std::move(mAtlanticPolls);mAtlanticPolls=std::move(entry);p=mAtlanticPolls.get();}
    p->channel=c;p->node=ch->getNodeId();p->revision=ch->productContextRevision();p->gateway=mOwnNodeId;p->model=uint8_t(ch->selectedHeatingModel());p->seconds=seconds;
    std::memcpy(p->key,ch->getEncryptionKey(),16);std::memcpy(p->systemKey,mSystemKey,16);p->dueMs=millis()+uint32_t(seconds)*1000U+uint32_t(c+1)*1000U;return true;
}
void IoHomeController::serviceAtlanticPolls() {
    for(auto *p=mAtlanticPolls.get();p;p=p->next.get())if(p->seconds){
        auto *ch=mModule?mModule->getChannel(p->channel):nullptr;
        if(!ch||p->gateway!=mOwnNodeId||std::memcmp(p->systemKey,mSystemKey,16)||!sampleIdentityMatches(p->channel,p->node,p->key,p->revision)||uint8_t(ch->selectedHeatingModel())!=p->model){p->seconds=0;continue;}
        if(int32_t(millis()-p->dueMs)<0||!idleForManagedOperation()||backgroundPollPauseRemaining()||ch->isMoving())continue;
        const bool dhw=IoHomeDhwCodec::identity(ch->getProtocolIdentity(),ch->getNodeId());
        if(!startAtlanticRegisterRead(ch,dhw?0:1,dhw?6:0x19,1,dhw?IoHomeAtlanticRead::Model::Calypso:IoHomeAtlanticRead::Model::Raw)){p->dueMs=millis()+5000U;continue;}
        auto &job=*mRegisterRead;
        if(dhw){job.count=5;const uint16_t regs[]={6,0x17,4,0x21,0xFFFF};for(uint8_t i=0;i<5;++i)job.samples[i].reg=regs[i];}
        else{job.model=IoHomeAtlanticRead::Model::AdelisClassic;job.count=4;const uint16_t regs[]={0x119,0x115,0x136,0x126};for(uint8_t i=0;i<4;++i)job.samples[i].reg=regs[i];}
        p->token=job.token;p->dueMs=millis()+uint32_t(p->seconds)*1000U;return;
    }
}

bool IoHomeController::requestHeatingNameQualification(IoHomecontrolChannel *ch) {
    if(qualifiedHeatingName(ch,IoHomeAdelis::HeatingModel::Agilia))return true;
    if(!ch||ch->selectedHeatingModel()!=IoHomeAdelis::HeatingModel::Agilia)return false;
    const uint8_t c=channelIndexFor(ch);HeatingNameProof *proof=mHeatingNames.get();while(proof&&proof->channel!=c)proof=proof->next.get();
    if(proof&&proof->attempted&&uint32_t(millis()-proof->lastAttemptMs)<60000U)return false;
    if(startAtlanticRegisterRead(ch,1,0x19,1,IoHomeAtlanticRead::Model::Raw)){
        recordHeatingModelName(ch,nullptr,0,millis());for(auto *p=mHeatingNames.get();p;p=p->next.get())if(p->channel==c){p->attempted=true;p->lastAttemptMs=millis();break;}
    }
    return false; // A fresh read qualifies only a later command; never defer/replay the write.
}
