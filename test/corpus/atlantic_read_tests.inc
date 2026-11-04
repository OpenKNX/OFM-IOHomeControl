static void initCalypsoReadTest(IoHomeController &c,IoHomecontrol &m,IoHomecontrolChannel &ch)
{
    const uint8_t key[16]={1};initPaired2WControllerForTest(c,m,ch,0xC0FFEE,0xF9CF0F,key);
    IoHomeProtocolIdentity id{};id.valid=id.fullMetadata=true;id.nodeClass=IoHomeNodeClass::Actuator;
    id.ioAddress=ch.getNodeId();id.profile=0x33;id.subProfile=0;id.manufacturerId=0x0C;id.powerSaveMode=IoHomePowerMode::AlwaysAlive;
    ch.onProtocolIdentity(ch.getNodeId(),id);
}
static bool challengeAtlanticRead(IoHomeController &c)
{
    const uint8_t nonce[]={1,2,3,4,5,6};IoHomeFrame frame;
    buildSimpleResponseFrame(frame,0xC0FFEE,0xF9CF0F,IoHomeCommand::ChallengeRequest,nonce,6);
    return queueControllerResponse(c,frame);
}
TEST(protocol_atlantic_read_golden_values_preserve_unknown_suffix_and_candidates)
{
    using namespace IoHomeAtlanticRead;
    unsigned modes=0,temps=0,energies=0,empty=0;uint32_t firstEnergy=0,lastEnergy=0;
    const uint8_t expectedModes[]={0,1,2,0};const uint16_t expectedTemps[]={496,495,494,499};
    for(const auto &f:AtlanticRegisterCaptures::frames) {
        Sample s;ASSERT_TRUE(reply(f.reg,f.wire+9,f.length-9,s));
        const bool calypso=f.wire[5]==0xF9;
        const auto v=decode(calypso?Model::Calypso:Model::AdelisClassic,s);
        if(calypso&&f.reg==6){ASSERT_TRUE(v.known&&!v.candidate);ASSERT_EQ(v.scaled,expectedModes[modes++]);}
        if(calypso&&f.reg==0x17){ASSERT_EQ(s.length,6);ASSERT_EQ(v.scaled,expectedTemps[temps++]);ASSERT_EQ(s.data[2],0);}
        if(calypso&&f.reg==4){if(!energies++)firstEnergy=v.scaled;lastEnergy=v.scaled;}
        if(!calypso&&f.reg==0x119)ASSERT_TRUE(isAdelisClassicName(s));
        if(!calypso&&f.reg==0x115){ASSERT_TRUE(v.candidate);ASSERT_EQ(v.scaled,2025);}
        if(s.result==Result::Empty){++empty;ASSERT_TRUE(!v.known);}
        ASSERT_TRUE(!decode(Model::Raw,s).known);
        uint8_t bad[23]{};memcpy(bad,f.wire+9,f.length-9);bad[3]^=1;
        ASSERT_TRUE(!reply(f.reg,bad,f.length-9,s));
    }
    ASSERT_EQ(modes,4U);ASSERT_EQ(temps,4U);ASSERT_EQ(lastEnergy-firstEnergy,85U);ASSERT_TRUE(empty>0);
    Sample truncated;const uint8_t shortTemp[]={0x0C,0x60,0,0x17,1,0xF0};
    ASSERT_TRUE(reply(0x17,shortTemp,6,truncated));ASSERT_TRUE(!decode(Model::Calypso,truncated).known);
    ASSERT_TRUE(!reply(0x1700,shortTemp,6,truncated));
}
TEST(controller_atlantic_read_requires_challenge_echo_peer_and_model_policy)
{
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initCalypsoReadTest(c,m,ch);
    ASSERT_TRUE(!c.startAtlanticRegisterRead(&ch,1,0x19));
    ASSERT_TRUE(!c.startAtlanticRegisterRead(&ch,0,0,17));
    ASSERT_TRUE(!c.startAtlanticRegisterRead(&ch,0,255,2));
    ASSERT_TRUE(!c.startAtlanticRegisterRead(&ch,0,0,1,IoHomeAtlanticRead::Model::AdelisClassic));
    ASSERT_TRUE(c.startAtlanticRegisterRead(&ch,0,6,1,IoHomeAtlanticRead::Model::Calypso));
    IoHomeFrame tx,frame;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
    const uint8_t req[]={0x0C,0x60,0,6};ASSERT_EQ(tx.commandId,IoHomeCommand::WritePrivate);ASSERT_EQ(tx.dataLen,4);ASSERT_MEM_EQ(tx.data,req,4);
    const uint8_t data[]={0x0C,0x60,0,6,1};
    buildSimpleResponseFrame(frame,0xC0FFEE,0xF9CF0F,IoHomeCommand::WritePrivateResponse,data,5);
    ASSERT_TRUE(queueControllerResponse(c,frame));ASSERT_EQ(c.atlanticRegisterRead().completed,0);
    ASSERT_TRUE(challengeAtlanticRead(c));
    frame.data[3]=7;ASSERT_TRUE(queueControllerResponse(c,frame));ASSERT_EQ(c.atlanticRegisterRead().completed,0);
    frame.data[3]=6;frame.setSrcNode(0xF9CF10);ASSERT_TRUE(queueControllerResponse(c,frame));ASSERT_EQ(c.atlanticRegisterRead().completed,0);
    frame.setSrcNode(0xF9CF0F);ASSERT_TRUE(queueControllerResponse(c,frame));
    const auto &state=c.atlanticRegisterRead();ASSERT_TRUE(!state.active);ASSERT_EQ(state.completed,1);
    ASSERT_EQ(state.samples[0].result,IoHomeAtlanticRead::Result::Value);ASSERT_EQ(state.samples[0].data[0],1);
    ASSERT_EQ(ch.heatingPublications,0U);ASSERT_TRUE(!ch.testHasPositionFeedback());ASSERT_TRUE(!ch.testHasCommandExchangeResult());
    ASSERT_EQ(c.startAtlanticRegisterRead(&ch,0,7),c.debugRfLimitsBypassed()); // release cooldown only
    ASSERT_TRUE(!c.sendCommand(ch.getNodeId(),ch.getEncryptionKey(),IoHomeCommand::Execute,0));
}
TEST(controller_atlantic_read_range_empty_cancel_identity_and_bounded_wait)
{
    for(unsigned variant=0;variant<4;++variant) {
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initCalypsoReadTest(c,m,ch);
        ASSERT_TRUE(c.startAtlanticRegisterRead(&ch,0,0x10,2));IoHomeFrame tx,frame;
        ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_TRUE(challengeAtlanticRead(c));
        if(variant==0) {
            const uint8_t data[]={0x0C,0x60,0,0x10};buildSimpleResponseFrame(frame,0xC0FFEE,0xF9CF0F,IoHomeCommand::WritePrivateResponse,data,4);
            ASSERT_TRUE(queueControllerResponse(c,frame));ASSERT_EQ(c.atlanticRegisterRead().samples[0].result,IoHomeAtlanticRead::Result::Empty);
            const auto count=c.radio().testTransmitCount();for(unsigned i=0;i<4;++i)c.loop();ASSERT_EQ(c.radio().testTransmitCount(),count);
            ioHomeTestAdvanceMillis(1000);ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_EQ(tx.data[3],0x11);
            ASSERT_TRUE(c.cancelAtlanticRegisterRead(c.atlanticRegisterRead().token));
        } else if(variant==1) {
            uint8_t replacement[16]={2};ch.setEncryptionKey(replacement);c.loop();
            ASSERT_EQ(c.atlanticRegisterRead().samples[0].result,IoHomeAtlanticRead::Result::IdentityChanged);
        } else if(variant==2) {
            ioHomeTestAdvanceMillis(130000);c.loop();ASSERT_TRUE(!c.atlanticRegisterRead().active);
        } else {
            const uint8_t err[]={8};buildSimpleResponseFrame(frame,0xC0FFEE,0xF9CF0F,IoHomeCommand::ErrorResponse,err,1);
            ASSERT_TRUE(queueControllerResponse(c,frame));ASSERT_EQ(c.atlanticRegisterRead().samples[0].result,IoHomeAtlanticRead::Result::Rejected);
        }
        ASSERT_TRUE(!c.atlanticRegisterRead().active);ASSERT_EQ(ch.heatingPublications,0U);
    }
}

TEST(controller_atlantic_read_distinguishes_silence_from_sent_auth_continuation)
{
    for(bool challenged:{false,true}) {
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initCalypsoReadTest(c,m,ch);
        ASSERT_TRUE(c.startAtlanticRegisterRead(&ch,0,6));IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
        if(challenged)ASSERT_TRUE(challengeAtlanticRead(c));
        ioHomeTestAdvanceMillis(10000);for(unsigned i=0;i<10;++i)c.loop();
        ASSERT_TRUE(!c.atlanticRegisterRead().active);
        ASSERT_EQ(c.atlanticRegisterRead().samples[0].result,challenged?IoHomeAtlanticRead::Result::Unconfirmed:IoHomeAtlanticRead::Result::NoReply);
        ASSERT_EQ(c.atlanticRegisterRead().completed,0);
    }
}

TEST(controller_rf_build_mode_atlantic_diagnostic_cooldown)
{
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initCalypsoReadTest(c,m,ch);
    ASSERT_TRUE(c.startAtlanticRegisterRead(&ch,0,6));
    ASSERT_TRUE(c.cancelAtlanticRegisterRead(c.atlanticRegisterRead().token));
    ASSERT_EQ(c.startAtlanticRegisterRead(&ch,0,7),c.debugRfLimitsBypassed());
}
