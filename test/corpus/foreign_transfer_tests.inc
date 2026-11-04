TEST(controller_foreign_transfer_packets_cannot_take_object_read_ownership)
{
    const uint8_t key[16]={1},nonce[]={1,2,3,4,5,6};
    for(bool waitingChunk:{false,true}) {
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
        initPaired2WControllerForTest(c,m,ch,0x123456,0x654321,key);
        ASSERT_TRUE(!c.requestObjectRead(&ch,0x0C,0x1002,0x2A,2));
        ASSERT_TRUE(c.requestObjectRead(&ch,0,0x8005,0,2));const auto token=c.objectReadToken();
        IoHomeFrame tx,reply;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
        if(waitingChunk) {
            buildSimpleResponseFrame(reply,0x123456,0x654321,IoHomeCommand::ChallengeRequest,nonce,6);ASSERT_TRUE(queueControllerResponse(c,reply));
            const uint8_t opening[]={1,0,0,2};buildSimpleResponseFrame(reply,0x123456,0x654321,IoHomeCommand::Unknown46Response,opening,4);ASSERT_TRUE(queueControllerResponse(c,reply));
            ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_EQ(tx.commandId,IoHomeCommand::Unknown4ARequest);
        }
        const auto count=c.radio().testTransmitCount();const auto stage=c.objectRead().stage();
        for(const auto &row:ObservedFrames::frames)if(row.sourceIndex==1) {
            IoHomeFrame f;ASSERT_TRUE(deserializeFrameForTest(f,row.wire,row.length));ASSERT_TRUE(queueControllerResponse(c,f));
            ASSERT_EQ(c.objectReadToken(),token);ASSERT_EQ(c.objectRead().stage(),stage);
            ASSERT_EQ(c.state(),ControllerState::WaitResponse);ASSERT_EQ(c.radio().testTransmitCount(),count);
            ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.testHasStatusUpdate()&&!ch.lastDeviceEvent().valid);
            ASSERT_MEM_EQ(ch.getEncryptionKey(),key,16);
        }
        ASSERT_TRUE(!c.sendCommand(ch.getNodeId(),key,static_cast<IoHomeCommand>(0x48),0));
    }
}
TEST(protocol_foreign_transfer_headers_do_not_imply_a_complete_exchange)
{
    unsigned read=0,write=0;
    for(const auto &row:ObservedFrames::frames)if(row.sourceIndex==1&&(row.wire[8]==0x46||row.wire[8]==0x48)) {
        IoHomeFrame f;ASSERT_TRUE(deserializeFrameForTest(f,row.wire,row.length));ASSERT_EQ(f.dataLen,9);
        ASSERT_EQ(f.data[1],0x0C);ASSERT_EQ(f.data[2],0);
        const uint16_t offset=uint16_t(f.data[5])<<8|f.data[6];ASSERT_TRUE(offset==0x2A||offset==0x2F||offset==0x46);
        if(row.wire[8]==0x46)++read;else ++write;
    }
    ASSERT_TRUE(read&&write);
    IoHomeObjectTransfer state;ASSERT_TRUE(state.begin(IoHomeObjectTransfer::Direction::Read,0xD7C990,1,0x0C,0x1002,0x2A,2,0,30000));
    const uint8_t opening[]={0x60,1,0,2};ASSERT_TRUE(state.acceptOpening(0xD7C990,1,0x47,opening,4));ASSERT_TRUE(state.nextRead());
    uint8_t data[]={0x61,0,1,0,0x64};ASSERT_TRUE(!state.acceptReadChunk(0xD7C990,1,data,5));
    data[0]=0x60;data[2]=2;ASSERT_TRUE(!state.acceptReadChunk(0xD7C990,1,data,5));
    data[2]=1;ASSERT_TRUE(!state.acceptReadChunk(0xE1BC6F,1,data,5));
    ASSERT_TRUE(!state.acceptReadChunk(0xD7C990,2,data,5));
    // These matching bytes are constructed negative/positive transport tests,
    // not a claim that the observed single frames form this synthetic session.
    ASSERT_TRUE(state.acceptReadChunk(0xD7C990,1,data,5));ASSERT_EQ(state.transferred(),2);
}
