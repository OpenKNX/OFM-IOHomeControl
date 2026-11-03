TEST(controller_object_identity_reads_only_allow_exact_reviewed_selectors)
{
    const uint8_t key[16]={1};
    for(uint16_t object:{uint16_t(0x8000),uint16_t(0x8001),uint16_t(0x8002),uint16_t(0x8003)}) {
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;
        initPaired2WControllerForTest(c,m,ch,0xA1B2C3,0x24635F,key);
        const bool allowed=object==0x8001||object==0x8002;
        ASSERT_EQ(c.requestObjectRead(&ch,0,object,0,1024),allowed);
        if(allowed) {
            IoHomeFrame tx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));
            ASSERT_EQ(tx.commandId,IoHomeCommand::Unknown46Request);ASSERT_EQ(tx.dataLen,9U);
            ASSERT_EQ(tx.data[3],0x80U);ASSERT_EQ(tx.data[4],object&0xFFU);
            ASSERT_TRUE(c.cancelObjectRead(c.objectReadToken()));c.loop();
        }
    }
}

TEST(protocol_issue156_object_dumps_replay_complete_and_partial_without_fabricated_capture_chunks)
{
    // Synthetic chunk envelopes exercise the existing mode1 reader. Only object
    // bytes/opening replies are captured; the log does not supply a4A/4Bwire stream.
    using namespace Issue156RS100;using Stage=IoHomeObjectTransfer::Stage;
    const uint8_t *openings[]={open8100,open8103,open8001,open8002};
    for(unsigned index=0;index<4;++index) {
        const auto &object=objects[index];IoHomeObjectTransfer transfer;
        ASSERT_TRUE(transfer.begin(IoHomeObjectTransfer::Direction::Read,0x24635F,index+1,0,object.id,0,1024,100,30000));
        IoHomeFrame opening;ASSERT_TRUE(deserializeFrameForTest(opening,openings[index],13));
        ASSERT_TRUE(transfer.acceptOpening(0x24635F,index+1,0x47,opening.data,opening.dataLen));
        ASSERT_EQ(transfer.negotiated(),object.declaredLength);
        uint16_t offset=0;
        while(offset<object.availableLength) {
            ASSERT_TRUE(transfer.nextRead());
            const auto remaining=uint16_t(object.declaredLength-offset);
            const uint8_t amount=remaining>18?18:remaining;
            ASSERT_TRUE(offset+amount<=object.availableLength); // no inventing partial-tail bytes
            uint8_t packet[21]{};memcpy(packet,transfer.pendingData(),3);
            memcpy(packet+3,object.data+offset,amount);
            auto malformed=packet[0];packet[0]^=1;
            ASSERT_TRUE(!transfer.acceptReadChunk(0x24635F,index+1,packet,amount+3));packet[0]=malformed;
            packet[2]++;ASSERT_TRUE(!transfer.acceptReadChunk(0x24635F,index+1,packet,amount+3));packet[2]--;
            ASSERT_TRUE(!transfer.acceptReadChunk(0x24635F,index+2,packet,amount+3));
            ASSERT_TRUE(transfer.acceptReadChunk(0x24635F,index+1,packet,amount+3));offset+=amount;
            ASSERT_TRUE(!transfer.acceptReadChunk(0x24635F,index+1,packet,amount+3)); // duplicate, no progress
            ASSERT_EQ(transfer.transferred(),offset);
        }
        ASSERT_MEM_EQ(transfer.data(),object.data,object.availableLength);
        if(object.complete) {
            ASSERT_TRUE(transfer.nextRead());ASSERT_EQ(transfer.pendingData()[1],0U);ASSERT_EQ(transfer.pendingData()[2],0U);
            uint8_t close[3];memcpy(close,transfer.pendingData(),3);
            ASSERT_TRUE(transfer.acceptReadChunk(0x24635F,index+1,close,3));ASSERT_EQ(transfer.stage(),Stage::Done);
        } else {
            ASSERT_EQ(transfer.transferred(),54U);ASSERT_EQ(transfer.negotiated(),67U);
            ASSERT_TRUE(transfer.stage()!=Stage::Done);ASSERT_TRUE(transfer.nextRead());
            uint8_t shortChunk[3];memcpy(shortChunk,transfer.pendingData(),3);
            ASSERT_TRUE(!transfer.acceptReadChunk(0x24635F,index+1,shortChunk,3));
            transfer.tick(30100);ASSERT_EQ(transfer.stage(),Stage::Timeout);
            ASSERT_EQ(transfer.transferred(),54U); // diagnostic prefix, never complete capabilities
        }
    }
}

TEST(protocol_object_reader_rejects_bad_opening_bounds_and_wrong_handle)
{
    IoHomeObjectTransfer transfer;
    ASSERT_TRUE(!transfer.begin(IoHomeObjectTransfer::Direction::Read,1,1,0,0x8100,0,1025,0,100));
    ASSERT_TRUE(transfer.begin(IoHomeObjectTransfer::Direction::Read,1,1,0,0x8100,0,150,0,100));
    uint8_t opening[]={1,0,0,151};
    ASSERT_TRUE(!transfer.acceptOpening(1,1,0x47,opening,3));
    ASSERT_TRUE(!transfer.acceptOpening(1,1,0x47,opening,4));ASSERT_EQ(transfer.stage(),IoHomeObjectTransfer::Stage::Invalid);
    ASSERT_TRUE(transfer.begin(IoHomeObjectTransfer::Direction::Read,1,2,0,0x8100,0,150,0,100));
    opening[1]=2;opening[3]=150;
    ASSERT_TRUE(transfer.acceptOpening(1,2,0x47,opening,4));ASSERT_EQ(transfer.stage(),IoHomeObjectTransfer::Stage::Rejected);
}
