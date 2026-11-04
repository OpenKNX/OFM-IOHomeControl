#include "recovery_trace_captures.h"

TEST(protocol_recovery_trace_frames_preserve_only_complete_non_authentication_packets)
{
    unsigned movingPollFrames=0,garageFrames=0;
    for(const auto &sample:RecoveryTraceCaptures::frames) {
        IoHomeFrame frame;ASSERT_TRUE(deserializeFrameForTest(frame,sample.wire,sample.length));
        ASSERT_TRUE(!(frame.ctrlByte0&IOHC_CTRL0_MODE_1W));
        ASSERT_TRUE(frame.commandId==IoHomeCommand::Execute||frame.commandId==IoHomeCommand::Private||frame.commandId==IoHomeCommand::PrivateResponse);
        uint8_t encoded[32]{};ASSERT_EQ(frame.serialize2W(encoded,sizeof(encoded)),sample.length);
        ASSERT_MEM_EQ(encoded,sample.wire,sample.length);
        if(sample.sourceIndex==2)++movingPollFrames;else ++garageFrames;
    }
    ASSERT_EQ(movingPollFrames,3U);ASSERT_TRUE(garageFrames>2);
}

TEST(controller_recovery_trace_moving_poll_missing_final_then_captured_stopped_reply)
{
    // Actual public 03/04 bytes; nonce/key and request ownership are synthetic.
    const uint8_t key[16]={1},nonce[6]={1,2,3,4,5,6};
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
    initPaired2WControllerForTest(c,m,ch,0xCA571E,0x717122,key);
    IoHomeFrame tx,challenge,reply;
    ASSERT_TRUE(c.sendStatusPoll(&ch,true,0xFF,0xFF,1));
    ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
    const uint8_t request[]={3,0,0};ASSERT_EQ(tx.dataLen,3);ASSERT_MEM_EQ(tx.data,request,3);
    c.loop();buildPairChallengeRequestFrame(challenge,0xCA571E,0x717122,nonce);
    ASSERT_TRUE(queueControllerResponse(c,challenge));c.loop();
    ioHomeTestAdvanceMillis(c.lastResponseTimingSample().selectedTimeoutMs+1);for(unsigned i=0;i<8;++i)c.loop();
    ASSERT_TRUE(!c.lastTwoWayOutcome().transportComplete);ASSERT_TRUE(!ch.testHasPositionFeedback());
    ASSERT_TRUE(ch.testStatusPollFailureAfterChallenge());
    // The software cadence itself is covered by the production-channel harness.
    // Schedule the next request explicitly; this stub does not implement that harness.
    ioHomeTestAdvanceMillis(1000);
    ASSERT_TRUE(c.sendStatusPoll(&ch,true,0xFF,0xFF,1));
    ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_TRUE(queueControllerResponse(c,challenge));
    bool found=false;
    for(const auto &sample:RecoveryTraceCaptures::frames)if(sample.sourceIndex==2&&sample.wire[8]==4) {
        ASSERT_TRUE(deserializeFrameForTest(reply,sample.wire,sample.length));found=true;break;
    }
    ASSERT_TRUE(found);ASSERT_TRUE(queueControllerResponse(c,reply));
    ASSERT_TRUE(c.lastTwoWayOutcome().transportComplete);
    ASSERT_TRUE(ch.testHasPositionFeedback()&&ch.testHasTargetPositionFeedback()&&ch.testHasStatusUpdate());
    ASSERT_FLOAT_EQ(ch.testPositionFeedback(),0.f,0.0001f);ASSERT_TRUE(!ch.testStatusMoving());
}

TEST(protocol_recovery_trace_moving_miss_grace_and_ladders_are_separate)
{
    for(bool challenged:{false,true}) {
        uint8_t misses=0,auth=0,silent=0;
        ASSERT_EQ(IoHomeStatusPollRetry::movingFailure(challenged,true,misses,auth,silent,1000,0),1000U);
        ASSERT_EQ(misses,1U);ASSERT_EQ(auth,0U);ASSERT_EQ(silent,0U);
        ASSERT_EQ(IoHomeStatusPollRetry::movingFailure(challenged,true,misses,auth,silent,1000,0),1000U);
        ASSERT_EQ(misses,2U);ASSERT_EQ(auth,0U);ASSERT_EQ(silent,0U);
        ASSERT_EQ(IoHomeStatusPollRetry::movingFailure(challenged,true,misses,auth,silent,1000,0),5000U);
        ASSERT_EQ(misses,3U);ASSERT_EQ(challenged?auth:silent,1U);
        ASSERT_EQ(IoHomeStatusPollRetry::movingFailure(challenged,true,misses,auth,silent,1000,0),challenged?30000U:15000U);
        ASSERT_EQ(IoHomeStatusPollRetry::movingFailure(!challenged,true,misses,auth,silent,1000,10000),10000U);
        ASSERT_EQ(challenged?auth:silent,0U);
    }
}

TEST(controller_recovery_trace_garage_auth_continuation_is_not_final_delivery)
{
    const uint8_t key[16]={1},nonce[6]={1,2,3,4,5,6};
    for(bool closingReply:{false,true}) {
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
        initPaired2WControllerForTest(c,m,ch,0xA9DBEC,0x8D7633,key);
        ch.setManualProfileOverride(uint16_t(5)<<6|58);
        ASSERT_TRUE(c.sendCommand(ch.getNodeId(),key,IoHomeCommand::Execute,100,0xFF,0xFF,1));
        IoHomeFrame tx,challenge,reply;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
        ASSERT_EQ(tx.dataLen,6);ASSERT_EQ(tx.data[2],0xC8);ASSERT_EQ(tx.data[3],0);
        ioHomeTestAdvanceMillis(246);
        buildSimpleResponseFrame(challenge,0xA9DBEC,0x8D7633,IoHomeCommand::ChallengeRequest,nonce,6);
        ASSERT_TRUE(queueControllerResponse(c,challenge));
        ASSERT_TRUE(lastTransmittedFrameForTest(c,tx));ASSERT_EQ(tx.commandId,IoHomeCommand::ChallengeResponse);
        ASSERT_TRUE(!ch.testHasPositionFeedback());ASSERT_TRUE(!c.lastTwoWayOutcome().transportComplete);
        if(closingReply) {
            bool found=false;
            for(const auto &sample:RecoveryTraceCaptures::frames)
                if(sample.sourceIndex==0&&sample.length==15&&sample.wire[8]==4&&sample.wire[11]==0xD4) {
                    ASSERT_TRUE(deserializeFrameForTest(reply,sample.wire,sample.length));found=true;break;
                }
            ASSERT_TRUE(found);ioHomeTestAdvanceMillis(24);ASSERT_TRUE(queueControllerResponse(c,reply));
            ASSERT_TRUE(c.lastTwoWayOutcome().transportComplete);
        } else {
            ioHomeTestAdvanceMillis(10000);for(unsigned i=0;i<10;++i)c.loop();
            ASSERT_TRUE(!c.lastTwoWayOutcome().transportComplete);
            ASSERT_EQ(ch.testLastCommandExchangeResult(),IoHomeCommandExchangeResult::AuthenticatedUnconfirmed);
        }
        // Neither challenge/proof nor the short Execute ACK contains measured position.
        ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.testHasTargetPositionFeedback());
    }
}
