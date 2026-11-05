// Class S scheduling model of issue #209; no reconstructed RF capture.
TEST(controller_remote_settle_catches_unheard_group_member_and_debounces)
{
    ioHomeTestSetMillis(1000);const uint8_t key[16]={1};
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel mamma,pappa;
    initPaired2WControllerForTest(c,m,mamma,0x111111,0xEFB491,key);
    pappa.setNodeId(0xD4C8ED);pappa.setEncryptionKey(key);pappa.setPaired(true);m.testSetChannel(1,&pappa);
    ASSERT_TRUE(m.remoteMap().addRemote(0xBC40B3,"Mamma"));ASSERT_TRUE(m.remoteMap().linkDevice(0xBC40B3,0xEFB491));
    ASSERT_TRUE(m.remoteMap().addRemote(0x857067,"Pappa"));ASSERT_TRUE(m.remoteMap().linkDevice(0x857067,0xD4C8ED));
    ASSERT_TRUE(!c.configureRemoteSettling(0,1,500));ASSERT_TRUE(!c.configureRemoteSettling(0,3,499));
    ASSERT_TRUE(c.configureRemoteSettling(0,3,1000));
    IoHomeFrame f;buildPassiveExecuteFrame(f,0xBC40B3,0x3F);f.set1WMode();f.dataLen=8;f.hasHmac=true;
    ASSERT_TRUE(queueControllerPassiveFrame(c,f));ASSERT_EQ(mamma.testScheduledStatusPollCount(),0);ASSERT_EQ(pappa.testScheduledStatusPollCount(),0);
    ioHomeTestAdvanceMillis(900);ASSERT_TRUE(queueControllerPassiveFrame(c,f));
    ioHomeTestAdvanceMillis(999);c.loop();ASSERT_EQ(mamma.testScheduledStatusPollCount(),0);
    ioHomeTestAdvanceMillis(1);c.loop();ASSERT_EQ(mamma.testScheduledStatusPollCount(),1);ASSERT_EQ(pappa.testScheduledStatusPollCount(),1);
    const auto epoch=pappa.mTestFollowUp.deadline;
    for(unsigned i=0;i<5;++i){ASSERT_TRUE(queueControllerPassiveFrame(c,f));ioHomeTestAdvanceMillis(1000);c.loop();}
    ASSERT_EQ(mamma.testScheduledStatusPollCount(),1);ASSERT_EQ(pappa.testScheduledStatusPollCount(),1);ASSERT_EQ(pappa.mTestFollowUp.deadline,epoch);
    ASSERT_EQ(c.radio().testTransmitCount(),0U);ASSERT_TRUE(!pappa.testHasPositionFeedback());
}
TEST(controller_remote_settle_identity_and_unmapped_source_cannot_schedule)
{
    for(unsigned variant=0;variant<5;++variant){ioHomeTestSetMillis(1000);const uint8_t key[16]={1},other[16]={2};
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel a,b;initPaired2WControllerForTest(c,m,a,0x111111,0xEFB491,key);
        b.setNodeId(0xD4C8ED);b.setEncryptionKey(key);b.setPaired(true);m.testSetChannel(1,&b);
        ASSERT_TRUE(m.remoteMap().addRemote(0xBC40B3,"Mamma"));ASSERT_TRUE(m.remoteMap().linkDevice(0xBC40B3,0xEFB491));
        ASSERT_TRUE(c.configureRemoteSettling(0,3,1000));IoHomeFrame f;buildPassiveExecuteFrame(f,variant==0?0x222222:0xBC40B3,0x3F);f.set1WMode();f.dataLen=8;f.hasHmac=true;
        ASSERT_TRUE(queueControllerPassiveFrame(c,f));
        if(variant==1)b.setEncryptionKey(other);if(variant==2)b.setIs1W(true);if(variant==3)b.setPaired(false);if(variant==4)c.configureRemoteSettling(0,0,0);
        ioHomeTestAdvanceMillis(1001);c.loop();ASSERT_EQ(a.testScheduledStatusPollCount(),0);ASSERT_EQ(b.testScheduledStatusPollCount(),0);
    }
}
