// Real captured DATA/wire bytes; synthetic keys and controlled transaction context.
TEST(controller_issue156_sensor_and_remote71_do_not_complete_execute)
{
    using namespace Issue156RS100;
    const uint8_t key[16]={1};
    for(unsigned variant=0;variant<7;++variant)for(bool challenged:{false,true}) {
        IoHomeController c;IoHomecontrol module;IoHomecontrolChannel channel;
        initPaired2WControllerForTest(c,module,channel,0xA1B2C3,0x24635F,key);
        ASSERT_TRUE(c.sendCommand(channel.getNodeId(),key,IoHomeCommand::Execute,50));
        IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));c.loop();
        if(challenged) {
            IoHomeFrame challenge;challenge.init();challenge.setSrcNode(0x24635F);challenge.setDestNode(0xA1B2C3);
            challenge.commandId=IoHomeCommand::ChallengeRequest;challenge.dataLen=6;
            for(unsigned n=0;n<6;++n)challenge.data[n]=n+1;
            ASSERT_TRUE(queueControllerResponse(c,challenge));
            ASSERT_TRUE(lastTransmittedFrameForTest(c,tx));ASSERT_EQ(tx.commandId,IoHomeCommand::ChallengeResponse);
        }
        IoHomeFrame event;
        const auto *wire=variant==0?immediate2005:variant==1?remoteStopped:remoteMoving;
        const auto len=variant==0?sizeof(immediate2005):variant==1?sizeof(remoteStopped):sizeof(remoteMoving);
        ASSERT_TRUE(deserializeFrameForTest(event,wire,len));
        if(variant==3){event.ctrlByte0=IOHC_CTRL0_END;} // last controller is a foreign wall remote
        if(variant==4){event.ctrlByte0=IOHC_CTRL0_END;event.data[4]--;} // bad declared length
        if(variant==5){event.setSrcNode(0x112233);} // wrong peer
        if(variant==6){event.ctrlByte0=IOHC_CTRL0_END|IOHC_CTRL0_MODE_1W;}
        const auto count=c.radio().testTransmitCount();
        ASSERT_TRUE(queueControllerResponse(c,event));
        ASSERT_EQ(c.state(),ControllerState::WaitResponse);
        ASSERT_TRUE(!c.lastTwoWayOutcome().valid);
        ASSERT_EQ(c.radio().testTransmitCount(),count);
        ASSERT_TRUE(!channel.lastDeviceEvent().valid&&!channel.testHasPositionFeedback()&&!channel.testHasStatusUpdate());
        // The real response can still finish the outstanding request.
        const uint8_t data[]={1,0x60,0,0,0,0,0,0};IoHomeFrame response;
        buildPrivateResponseFrame(response,0xA1B2C3,0x24635F,data,sizeof(data));
        ASSERT_TRUE(queueControllerResponse(c,response));
        ASSERT_TRUE(c.lastTwoWayOutcome().valid&&c.lastTwoWayOutcome().transportComplete);
    }
}

TEST(controller_issue156_final_position71_can_still_complete_execute)
{
    const uint8_t key[16]={1};IoHomeController c;IoHomecontrol module;IoHomecontrolChannel channel;
    initPaired2WControllerForTest(c,module,channel,0xA1B2C3,0x24635F,key);
    ASSERT_TRUE(c.sendCommand(channel.getNodeId(),key,IoHomeCommand::Execute,50));
    IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));c.loop();
    // Synthetic final response: captured position DATA with explicitly local last controller.
    IoHomeFrame response;ASSERT_TRUE(deserializeFrameForTest(response,Issue156RS100::remoteStopped,sizeof(Issue156RS100::remoteStopped)));
    response.ctrlByte0=IOHC_CTRL0_END;
    response.data[11]=0xA1;response.data[12]=0xB2;response.data[13]=0xC3;
    ASSERT_TRUE(queueControllerResponse(c,response));
    ASSERT_TRUE(c.lastTwoWayOutcome().valid&&c.lastTwoWayOutcome().transportComplete);
    ASSERT_TRUE(!channel.testHasPositionFeedback()); // Execute ACK positions stay untrusted/stale
}

TEST(controller_issue156_status_cannot_complete_object_open_wait)
{
    const uint8_t key[16]={1};IoHomeController c;IoHomecontrol module;IoHomecontrolChannel channel;
    initPaired2WControllerForTest(c,module,channel,0xA1B2C3,0x24635F,key);
    ASSERT_TRUE(c.requestObjectRead(&channel,0,0x8100,0,1024));
    IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));c.loop();
    for(const auto &fixture:Issue156RS100::frames)if(fixture.command==0x71) {
        IoHomeFrame event;ASSERT_TRUE(deserializeFrameForTest(event,fixture.wire,fixture.length));
        const auto count=c.radio().testTransmitCount();
        ASSERT_TRUE(queueControllerResponse(c,event));
        ASSERT_EQ(c.objectRead().stage(),IoHomeObjectTransfer::Stage::Opening);
        ASSERT_EQ(c.state(),ControllerState::WaitResponse);ASSERT_EQ(c.radio().testTransmitCount(),count);
        ASSERT_TRUE(!c.lastTwoWayOutcome().valid&&!channel.lastDeviceEvent().valid&&!channel.testHasStatusUpdate());
    }
}
