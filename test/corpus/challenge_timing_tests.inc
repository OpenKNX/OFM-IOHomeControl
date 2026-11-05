TEST(controller_challenge_timing_records_local_stages_without_changing_authentication)
{
    const uint8_t key[16]={1},nonce[]={1,2,3,4,5,6};
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initPaired2WControllerForTest(c,m,ch,0x111111,0x222222,key);
    ASSERT_TRUE(c.enableChallengeTiming(true));ASSERT_TRUE(c.sendCommand(ch.getNodeId(),key,IoHomeCommand::Execute,100));
    IoHomeFrame tx,challenge;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_TRUE(!c.enableChallengeTiming(false));
    buildSimpleResponseFrame(challenge,0x111111,0x222222,IoHomeCommand::ChallengeRequest,nonce,6);ASSERT_TRUE(queueControllerResponse(c,challenge));
    const auto *t=c.lastChallengeTiming();ASSERT_TRUE(t);ASSERT_EQ(t->peer,0x222222U);
    ASSERT_TRUE(t->hmacRecorded&&t->prepareRecorded&&t->txRequested&&t->txAccepted);
    ASSERT_TRUE(uint32_t(t->hmacStartUs-t->rxReadUs)<1000000U);ASSERT_TRUE(uint32_t(t->hmacEndUs-t->hmacStartUs)<1000000U);
    c.loop();ASSERT_TRUE(c.lastChallengeTiming()->txDoneObserved);
    ASSERT_TRUE(!ch.testHasPositionFeedback());ASSERT_TRUE(!c.lastTwoWayOutcome().transportComplete);
}
