TEST(protocol_fp_high_read_keeps_function_and_mask_getters_distinct)
{
    uint8_t data[23]{};uint8_t length=0;
    const uint8_t a[]={1,0,0x43,1,1,1},b[]={1,0,0x78,1,1,1,1};
    ASSERT_TRUE(ioHomeBuildStatusFpHighRead(0x43,data,length));ASSERT_EQ(length,sizeof(a));ASSERT_MEM_EQ(data,a,sizeof(a));
    ASSERT_TRUE(ioHomeBuildStatusFpHighRead(0x78,data,length));ASSERT_EQ(length,sizeof(b));ASSERT_MEM_EQ(data,b,sizeof(b));
    ASSERT_TRUE(!ioHomeBuildStatusFpHighRead(0,data,length));ASSERT_TRUE(!ioHomeBuildStatusFpHighRead(255,data,length));
    ASSERT_TRUE(!ioHomeBuildStatusFpHighRead(1,nullptr,length));
    ASSERT_TRUE(ioHomeBuildMpFpMaskRead(0xC200,data,length));
    const uint8_t getter[]={3,0,0x43};ASSERT_EQ(length,3);ASSERT_MEM_EQ(data,getter,3);
}
TEST(controller_fp_high_probe_is_manual_raw_and_identity_bound)
{
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initRainController(c,m,ch);
    ASSERT_TRUE(!c.requestStatusFpHighRead(&ch,0));ASSERT_TRUE(!c.requestStatusFpHighRead(&ch,255));
    ASSERT_TRUE(c.requestStatusFpHighRead(&ch,0x43));ASSERT_TRUE(!c.requestStatusFpHighRead(&ch,0x78));
    IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
    const uint8_t expected[]={1,0,0x43,1,1,1};ASSERT_EQ(tx.dataLen,6);ASSERT_MEM_EQ(tx.data,expected,6);
    ASSERT_TRUE(replyRainStatus(c));ASSERT_TRUE(!ch.testHasPositionFeedback());
    ASSERT_TRUE(!ch.testHasTargetPositionFeedback());ASSERT_TRUE(!ch.testHasCommandExchangeResult());
    ASSERT_TRUE(!c.rainState(0).evidence.lastCommand.valid);
    ASSERT_TRUE(replyRainStatus(c));ASSERT_TRUE(!ch.testHasPositionFeedback());
}
