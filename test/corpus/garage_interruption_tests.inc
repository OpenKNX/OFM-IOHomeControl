static IoHomeFrame observedGarageFrame(uint8_t command,uint8_t length=0)
{
    IoHomeFrame frame;
    for(const auto &row:ObservedFrames::frames)if(row.sourceIndex>=2&&row.wire[8]==command&&(!length||row.length==length)) {
        deserializeFrameForTest(frame,row.wire,row.length);return frame;
    }
    return frame;
}
TEST(controller_garage_interrupted_session_does_not_confirm_motion_or_reexecute)
{
    const uint8_t key[16]={1},nonce[]={1,2,3,4,5,6};
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initPaired2WControllerForTest(c,m,ch,0xA9DBEC,0x8D7633,key);
    ch.setManualProfileOverride(uint16_t(5)<<6|58);
    ASSERT_TRUE(c.sendCommand(ch.getNodeId(),key,IoHomeCommand::Execute,0xD2,0xFF,0xFF,1));IoHomeFrame tx,challenge;
    ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
    buildSimpleResponseFrame(challenge,0xA9DBEC,0x8D7633,IoHomeCommand::ChallengeRequest,nonce,6);
    ASSERT_TRUE(queueControllerResponse(c,challenge));const auto count=c.radio().testTransmitCount();
    const auto status=observedGarageFrame(0x71,25);ASSERT_EQ(status.getSrcNodeId(),ch.getNodeId());
    ASSERT_TRUE(queueControllerResponse(c,status));ASSERT_EQ(c.state(),ControllerState::WaitResponse);
    ASSERT_EQ(c.radio().testTransmitCount(),count);ASSERT_TRUE(!c.lastTwoWayOutcome().transportComplete);
    ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.testHasStatusUpdate()&&!ch.lastDeviceEvent().valid);
    ioHomeTestAdvanceMillis(10000);for(unsigned i=0;i<10;++i)c.loop();
    ASSERT_EQ(c.radio().testTransmitCount(),count);ASSERT_EQ(ch.testLastCommandExchangeResult(),IoHomeCommandExchangeResult::AuthenticatedUnconfirmed);
    ASSERT_TRUE(c.sendStatusPoll(&ch,false,0xFF,0xFF,1));ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_EQ(tx.commandId,IoHomeCommand::Private);
    IoHomeFrame wrong;buildSimpleResponseFrame(wrong,0xA9DBEC,0x8D7633,IoHomeCommand::SetConfig1Response,nullptr,0);
    ASSERT_TRUE(queueControllerResponse(c,wrong));ASSERT_EQ(c.state(),ControllerState::WaitResponse);
    const auto error=observedGarageFrame(0xFE,10);ASSERT_EQ(error.data[0],0x70);
    ASSERT_TRUE(queueControllerResponse(c,error));ASSERT_EQ(ch.testLastCommandExchangeResult(),IoHomeCommandExchangeResult::ExplicitlyRejected);
    ASSERT_TRUE(!ch.testHasPositionFeedback());ASSERT_TRUE(!strcmp(ioHomeDecodeErrorResponse(error.data[0]).name,"SESSION_INTERRUPTED"));
}
TEST(controller_garage_unknown_current_preserves_explicit_moving_status)
{
    const uint8_t key[16]={1};IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
    initPaired2WControllerForTest(c,m,ch,0xA9DBEC,0x8D7633,key);ch.setManualProfileOverride(uint16_t(5)<<6|58);
    ASSERT_TRUE(c.sendStatusPoll(&ch,false,0xFF,0xFF,1));IoHomeFrame tx,reply;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
    // Constructed case: explicit moving flag, target100%, current withheld.
    const uint8_t data[]={4,0,0xC8,0,0xD4,0,0,10};
    buildSimpleResponseFrame(reply,0xA9DBEC,0x8D7633,IoHomeCommand::PrivateResponse,data,8);
    ASSERT_TRUE(queueControllerResponse(c,reply));ASSERT_TRUE(!ch.testHasPositionFeedback());
    ASSERT_TRUE(ch.testHasTargetPositionFeedback());ASSERT_FLOAT_EQ(ch.testTargetPositionFeedback(),100.f,0.01f);
    ASSERT_TRUE(ch.testHasStatusUpdate()&&ch.testStatusMoving());
}
TEST(protocol_garage_private2_short_is_an_opaque_read_observation)
{
    bool request=false,response=false;
    for(const auto &row:ObservedFrames::frames)if(row.sourceIndex>=2) {
        if(row.wire[8]==0x0C){IoHomeFrame f;ASSERT_TRUE(deserializeFrameForTest(f,row.wire,row.length));
            ASSERT_EQ(f.dataLen,4);ASSERT_MEM_EQ(f.data,"\xD8\x09\x00\x00",4);request=true;}
        if(row.wire[8]==0x0D){IoHomeFrame f;ASSERT_TRUE(deserializeFrameForTest(f,row.wire,row.length));
            ASSERT_EQ(f.dataLen,5);ASSERT_MEM_EQ(f.data,"\x05\x64\x00\x00\x00",5);response=true;}
    }
    ASSERT_TRUE(request&&response);
    const uint8_t key[16]={1};IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
    initPaired2WControllerForTest(c,m,ch,0xA9DBEC,0x8D7633,key);
    ASSERT_TRUE(queueControllerResponse(c,observedGarageFrame(0x0D)));
    ASSERT_EQ(c.radio().testTransmitCount(),0U);ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.testHasStatusUpdate());
}
