// Issue #205 supplies class-B payloads, not the synthetic RF envelopes below.
TEST(controller_window_main_only_is_opt_in_qualified_and_preserves_acei_target_and_hmac)
{
    const uint8_t key[16]={1};
    for(uint8_t sub:{0,1})for(uint8_t target:{0,50,100})for(uint8_t acei:{0x43,0x63}){
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initPaired2WControllerForTest(c,m,ch,0x111111,0x222222,key);
        ASSERT_TRUE(!ch.setWindowMainOnly(true));IoHomeProtocolIdentity identity;identity.valid=true;identity.ioAddress=ch.getNodeId();identity.profile=4;identity.subProfile=sub;identity.manufacturerId=1;identity.nodeClass=IoHomeNodeClass::Actuator;
        ch.onProtocolIdentity(ch.getNodeId(),identity);ch.setConfigured2WAcei(acei);
        ASSERT_TRUE(!ch.windowMainOnly());ASSERT_TRUE(ch.setWindowMainOnly(true));ASSERT_TRUE(ch.windowMainOnly());
        ASSERT_TRUE(!c.sendProfileMovementCommand(ch.getNodeId(),key,target,1,0xD805));
        ASSERT_TRUE(c.sendCommand(ch.getNodeId(),key,IoHomeCommand::Execute,target));IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
        ASSERT_EQ(tx.dataLen,6);const uint8_t expected[]={1,acei,uint8_t(target*2),0,0,0};ASSERT_MEM_EQ(tx.data,expected,6);
        ASSERT_TRUE(!tx.hasHmac);IoHomeFrame challenge,reply;const uint8_t nonce[]={1,2,3,4,5,6};
        buildSimpleResponseFrame(challenge,0x111111,0x222222,IoHomeCommand::ChallengeRequest,nonce,6);ASSERT_TRUE(queueControllerResponse(c,challenge));
        ASSERT_TRUE(lastTransmittedFrameForTest(c,reply));ASSERT_EQ(reply.commandId,IoHomeCommand::ChallengeResponse);
        IoHomeFrame expectedReply;ASSERT_TRUE(IoHomeController::buildControllerChallengeResponse(expectedReply,0x111111,0x222222,tx,nonce,key));ASSERT_MEM_EQ(reply.data,expectedReply.data,6);
        ch.invalidateProductContext();ASSERT_TRUE(!ch.windowMainOnly());
    }
}
TEST(controller_window_issue_extended_payloads_keep_existing_selection)
{
    for(uint8_t mode:{uint8_t(0xFF),uint8_t(IOHC_EXECUTE_PROFILE_SILENT)}){
        const uint8_t key[16]={1};IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
        initPaired2WControllerForTest(c,m,ch,0x111111,0x222222,key);ch.setConfigured2WAcei(0x63);
        ASSERT_TRUE(c.sendCommand(ch.getNodeId(),key,IoHomeCommand::Execute,100,0xFF,mode));IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
        const uint8_t expected[]={1,0x63,0xC8,0,0x80,0xD8,uint8_t(mode==0xFF?6:5),0};ASSERT_EQ(tx.dataLen,8);ASSERT_MEM_EQ(tx.data,expected,8);
    }
}
