#include "recent_observed_frames.h"
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
static IoHomeFrame capturedHighFpReply() {
    IoHomeFrame f;
    for(const auto &row:ObservedFrames::frames)if(row.sourceIndex==0&&row.frequency==868950000&&row.length==23) {
        deserializeFrameForTest(f,row.wire,row.length);return f;
    }
    return f;
}
TEST(controller_fp_high_probe_is_manual_raw_and_identity_bound)
{
    const uint8_t key[16]={1};IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
    initPaired2WControllerForTest(c,m,ch,0xC0FFEE,0xBB0B50,key); // synthetic key/context, literal captured addresses
    ASSERT_TRUE(!c.requestStatusFpHighRead(&ch,0));ASSERT_TRUE(!c.requestStatusFpHighRead(&ch,255));
    ASSERT_TRUE(c.requestStatusFpHighRead(&ch,0x78));ASSERT_TRUE(!c.requestStatusFpHighRead(&ch,0x43));
    IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
    const uint8_t expected[]={1,0,0x78,1,1,1,1};ASSERT_EQ(tx.dataLen,7);ASSERT_MEM_EQ(tx.data,expected,7);
    auto reply=capturedHighFpReply();ASSERT_EQ(reply.commandId,IoHomeCommand::PrivateResponse);
    ASSERT_TRUE(queueControllerResponse(c,reply));ASSERT_TRUE(c.lastTwoWayOutcome().transportComplete);
    ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.testHasTargetPositionFeedback()&&!ch.testHasCommandExchangeResult());
    ASSERT_TRUE(!c.rainState(0).evidence.lastCommand.valid);ASSERT_TRUE(!c.limitationDecision(0).valid);
    ASSERT_TRUE(queueControllerResponse(c,reply));ASSERT_TRUE(!ch.testHasPositionFeedback());
}
TEST(controller_captured_high_fp_rejects_unowned_or_mismatched_replies)
{
    for(unsigned variant=0;variant<10;++variant) {
        const uint8_t key[16]={1};IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
        initPaired2WControllerForTest(c,m,ch,0xC0FFEE,0xBB0B50,key);
        ASSERT_TRUE(c.requestStatusFpHighRead(&ch,variant==0?0x43:0x78));IoHomeFrame tx;
        ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));auto reply=capturedHighFpReply();
        if(variant==1)reply.setSrcNode(0xBB0B51);
        if(variant==2)reply.setDestNode(0xC0FFEF);
        if(variant==3)c.testSetRxScanFrequency(IOHC_FREQ_1);
        if(variant==4){const uint8_t changed[16]={2};ch.setEncryptionKey(changed);}
        if(variant==5){const uint8_t changed[16]={2};c.setSystemKey(changed);}
        if(variant==6)ioHomeTestAdvanceMillis(10000);
        if(variant==7){reply.dataLen--;}
        if(variant==8)ch.invalidateProductContext();
        if(variant==9)c.setOwnNodeId(0xC0FFEF);
        ASSERT_TRUE(queueControllerResponse(c,reply));
        ASSERT_TRUE(!c.lastTwoWayOutcome().transportComplete);
        ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.testHasStatusUpdate());
    }
}
TEST(protocol_captured_high_fp_layout_is_bounded_raw_not_fixed_status)
{
    const auto f=capturedHighFpReply();ASSERT_EQ(f.getSrcNodeId(),0xBB0B50U);ASSERT_EQ(f.getDestNodeId(),0xC0FFEEU);
    ASSERT_TRUE(ioHomeHighFpReplyMatches(0x78,f.data,f.dataLen));ASSERT_TRUE(!ioHomeHighFpReplyMatches(0x43,f.data,f.dataLen));
    ASSERT_TRUE(!ioHomeHighFpReplyMatches(0,f.data,f.dataLen));ASSERT_TRUE(!ioHomeHighFpReplyMatches(255,f.data,f.dataLen));
    for(unsigned length=0;length<f.dataLen;++length)ASSERT_TRUE(!ioHomeHighFpReplyMatches(0x78,f.data,length));
    IoHomeMpFpReply fixed;ASSERT_TRUE(!ioHomeDecodeMpFpReply(f.data,f.dataLen,fixed));
}

TEST(controller_high_fp_cannot_replace_another_pending_read)
{
    const uint8_t key[16]={1};IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
    initPaired2WControllerForTest(c,m,ch,0xC0FFEE,0xBB0B50,key);
    ASSERT_TRUE(c.requestPriority(&ch,1));IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
    ASSERT_TRUE(!c.requestStatusFpHighRead(&ch,0x78));
    ASSERT_TRUE(queueControllerResponse(c,capturedHighFpReply()));
    ASSERT_EQ(c.state(),ControllerState::WaitResponse);ASSERT_TRUE(!c.lastTwoWayOutcome().transportComplete);
    ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.testHasStatusUpdate());
}

TEST(controller_high_fp_error_cannot_publish_battery_evidence)
{
    const uint8_t key[16]={1};IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
    initPaired2WControllerForTest(c,m,ch,0xC0FFEE,0xBB0B50,key);ch.setBatteryMonitoring(1);
    ASSERT_TRUE(c.requestStatusFpHighRead(&ch,0x78));IoHomeFrame tx,reply;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
    const uint8_t error[]={0x12};buildSimpleResponseFrame(reply,0xC0FFEE,0xBB0B50,IoHomeCommand::ErrorResponse,error,1);
    ASSERT_TRUE(queueControllerResponse(c,reply));ASSERT_TRUE(!ch.batteryInfo().error.valid);
    ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.testHasStatusUpdate()&&!ch.testHasCommandExchangeResult());
}
