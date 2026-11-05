// Synthetic transport cases; source semantics: laberning da10f29 (#204).
TEST(protocol_empty_object_read_is_terminal_and_preserves_existing_outcomes)
{
    using T=IoHomeObjectTransfer;using S=T::Stage;
    ASSERT_EQ(unsigned(S::IdentityChanged),11U);ASSERT_EQ(unsigned(S::Empty),12U);
    for(unsigned variant=0;variant<8;++variant){T t;
        const auto direction=variant==3?T::Direction::Write:T::Direction::Read;
        ASSERT_TRUE(t.begin(direction,1,2,0,0x8005,0,16,10,100));
        uint8_t reply[]={0,7,0,0};if(variant==1)reply[1]=3;if(variant==2)reply[3]=1;
        const bool accepted=t.acceptOpening(variant==4?3:1,variant==5?3:2,variant==6?0x49:(variant==3?0x49:0x47),reply,variant==7?3:4);
        if(variant>=4){ASSERT_TRUE(!accepted);ASSERT_EQ(t.stage(),S::Opening);t.tick(110);ASSERT_EQ(t.stage(),S::Timeout);}
        else{ASSERT_EQ(t.stage(),variant==0?S::Empty:variant==2?S::Invalid:S::Rejected);
            ASSERT_EQ(accepted,variant!=2);ASSERT_TRUE(!t.active());ASSERT_TRUE(!t.nextRead());ASSERT_EQ(t.pendingSize(),0);ASSERT_EQ(t.transferred(),0);t.tick(1000);ASSERT_TRUE(!t.active());}
        ASSERT_TRUE(t.begin(T::Direction::Read,1,3,0,0x8005,0,16,1000,100));
        const uint8_t normal[]={0,0,0,1};ASSERT_TRUE(t.acceptOpening(1,3,0x47,normal,4));ASSERT_EQ(t.stage(),S::Ready);
    }
}
TEST(controller_empty_object_does_not_send_chunk_or_close_or_publish)
{
    IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;const uint8_t key[16]={1};
    initPaired2WControllerForTest(c,m,ch,0x123456,0x654321,key);
    ASSERT_TRUE(c.requestObjectRead(&ch,0,0x8005,0,16));IoHomeFrame tx,reply;
    ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));const uint8_t nonce[]={1,2,3,4,5,6};
    buildSimpleResponseFrame(reply,0x123456,0x654321,IoHomeCommand::ChallengeRequest,nonce,6);ASSERT_TRUE(queueControllerResponse(c,reply));
    const uint8_t empty[]={0,7,0,0};buildSimpleResponseFrame(reply,0x123456,0x654321,IoHomeCommand::Unknown46Response,empty,4);
    ASSERT_TRUE(queueControllerResponse(c,reply));ASSERT_EQ(c.objectRead().stage(),IoHomeObjectTransfer::Stage::Empty);
    const auto count=c.radio().testTransmitCount();for(unsigned i=0;i<10;++i){ioHomeTestAdvanceMillis(1000);c.loop();}
    ASSERT_EQ(c.radio().testTransmitCount(),count);ASSERT_EQ(c.objectRead().transferred(),0);
    ASSERT_TRUE(!ch.testHasPositionFeedback()&&!ch.lastDeviceEvent().valid);
    ASSERT_TRUE(!c.requestObjectRead(&ch,2,0xA601,0,16));ASSERT_TRUE(c.requestObjectRead(&ch,0,0x8005,0,16));
}
