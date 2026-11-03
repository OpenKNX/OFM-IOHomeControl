TEST(controller_object_cancel_preserves_unrelated_queue_order_across_wrap)
{
    const uint8_t key[16]={1};
    for(unsigned shift=0;shift<IOHC_CMD_QUEUE_SIZE;++shift) {
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
        initPaired2WControllerForTest(c,m,ch,0x123456,0x654321,key);
        auto answer=[&]() {
            const uint8_t data[]={1,0,0,0,0,0,0,0};IoHomeFrame reply;
            buildPrivateResponseFrame(reply,0x123456,0x654321,data,sizeof(data));
            ASSERT_TRUE(queueControllerResponse(c,reply));
        };
        for(unsigned i=0;i<shift;++i) {
            ASSERT_TRUE(c.sendCommand(ch.getNodeId(),key,IoHomeCommand::Private,3));
            IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));answer();
        }
        ASSERT_TRUE(c.requestObjectRead(&ch,0,0x8100,0,19));
        ASSERT_TRUE(c.cancelObjectRead(c.objectReadToken()));
        const uint8_t selectors[]={3,6,9};
        for(auto selector:selectors)ASSERT_TRUE(c.sendCommand(ch.getNodeId(),key,IoHomeCommand::Private,selector));
        for(unsigned i=0;i<3;++i) {
            IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
            ASSERT_EQ(tx.commandId,IoHomeCommand::Private);ASSERT_EQ(tx.data[0],selectors[i]);answer();
        }
        ASSERT_EQ(c.objectRead().stage(),IoHomeObjectTransfer::Stage::Cancelled);
        const auto count=c.radio().testTransmitCount();
        for(unsigned i=0;i<4;++i)c.loop();ASSERT_EQ(c.radio().testTransmitCount(),count);
    }
}
