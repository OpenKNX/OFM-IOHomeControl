TEST(controller_object_pacing_is_nonblocking_and_large_transfer_closes)
{
    for(uint16_t span:{uint16_t(431),uint16_t(432)}) {
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;const uint8_t key[16]={1};
        initPaired2WControllerForTest(c,m,ch,0x123456,0x654321,key);
        ASSERT_TRUE(!c.configureObjectReadTiming(&ch,2001,60000));ASSERT_TRUE(!c.configureObjectReadTiming(&ch,650,120001));
        ASSERT_TRUE(c.configureObjectReadTiming(&ch,650,60000));
        ASSERT_TRUE(!c.requestObjectRead(&ch,2,0xA601,0,span));ASSERT_TRUE(!c.requestObjectRead(&ch,2,0x8005,0,16));
        ASSERT_TRUE(c.requestObjectRead(&ch,0,0x8100,0,span));IoHomeFrame tx,reply;
        ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
        const uint8_t nonce[]={1,2,3,4,5,6};
        buildSimpleResponseFrame(reply,0x123456,0x654321,IoHomeCommand::ChallengeRequest,nonce,6);ASSERT_TRUE(queueControllerResponse(c,reply));
        const uint8_t opening[]={1,0,uint8_t(span>>8),uint8_t(span)};
        buildSimpleResponseFrame(reply,0x123456,0x654321,IoHomeCommand::Unknown46Response,opening,4);ASSERT_TRUE(queueControllerResponse(c,reply));
        uint16_t remaining=span;uint8_t sequence=1;
        do {
            const auto count=c.radio().testTransmitCount();
            ioHomeTestAdvanceMillis(649);for(unsigned i=0;i<5;++i)c.loop();
            ASSERT_EQ(c.radio().testTransmitCount(),count);ASSERT_EQ(c.state(),ControllerState::Idle);
            ioHomeTestAdvanceMillis(1);ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
            ASSERT_EQ(tx.commandId,IoHomeCommand::Unknown4ARequest);ASSERT_EQ(tx.data[0],sequence);
            const uint16_t word=(remaining+17)/18;ASSERT_EQ(uint16_t(tx.data[1])<<8|tx.data[2],word);
            const uint8_t bytes=remaining>18?18:remaining;uint8_t data[21]{};data[0]=sequence++;data[1]=word>>8;data[2]=word;
            for(uint8_t i=0;i<bytes;++i)data[i+3]=uint8_t(span-remaining+i);
            buildSimpleResponseFrame(reply,0x123456,0x654321,IoHomeCommand::Unknown4AResponse,data,3+bytes);
            ASSERT_TRUE(queueControllerResponse(c,reply));remaining-=bytes;
            if(!word)break;
        }while(true);
        ASSERT_EQ(c.objectRead().stage(),IoHomeObjectTransfer::Stage::Done);ASSERT_EQ(c.objectRead().transferred(),span);
        for(unsigned i=0;i<span;++i)ASSERT_EQ(c.objectRead().data()[i],uint8_t(i));
    }
}
TEST(controller_object_timing_deadline_and_identity_are_independent_of_capacity)
{
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;const uint8_t key[16]={1};
    initPaired2WControllerForTest(c,m,ch,0x123456,0x654321,key);
    ASSERT_TRUE(c.configureObjectReadTiming(&ch,650,60000));ASSERT_TRUE(c.requestObjectRead(&ch,0,0x8005,0,16));
    ASSERT_TRUE(c.cancelObjectRead(c.objectReadToken()));c.loop();
    uint8_t changed[16]={2};ch.setEncryptionKey(changed);ASSERT_TRUE(c.requestObjectRead(&ch,0,0x8005,0,16));
    ioHomeTestAdvanceMillis(30000);c.loop();ASSERT_EQ(c.objectRead().stage(),IoHomeObjectTransfer::Stage::Timeout);
    IoHomeObjectTransfer transfer;ASSERT_TRUE(transfer.begin(IoHomeObjectTransfer::Direction::Read,1,1,0,0x8005,0,16,UINT32_MAX-100,60000));
    transfer.tick(59898);ASSERT_TRUE(transfer.active());transfer.tick(59899);ASSERT_EQ(transfer.stage(),IoHomeObjectTransfer::Stage::Timeout);
    ASSERT_TRUE(transfer.begin(IoHomeObjectTransfer::Direction::Read,1,2,2,0x6103,0,16,0,30000));
    const uint8_t rejected[]={0,3,0,0};ASSERT_TRUE(transfer.acceptOpening(1,2,0x47,rejected,4));
    ASSERT_EQ(transfer.stage(),IoHomeObjectTransfer::Stage::Rejected);ASSERT_EQ(transfer.disposition(),3);
}
