#include "recent_observed_frames.h"
TEST(protocol_recent_observed_frames_are_literal_and_reencode_exactly)
{
    unsigned high=0,standard=0,interrupted=0,private2=0,foreign=0;
    for(const auto &row:ObservedFrames::frames) {
        IoHomeFrame f;ASSERT_TRUE(deserializeFrameForTest(f,row.wire,row.length));
        uint8_t bytes[32]{};ASSERT_EQ(f.serialize2W(bytes,sizeof(bytes)),row.length);ASSERT_MEM_EQ(bytes,row.wire,row.length);
        if(row.sourceIndex==0&&row.frequency==868950000){if(row.length==23)++high;if(row.length==15)++standard;}
        if(row.sourceIndex==1)++foreign;
        if(f.commandId==IoHomeCommand::ErrorResponse&&f.dataLen==1&&f.data[0]==0x70) {
            ++interrupted;ASSERT_TRUE(!strcmp(ioHomeDecodeErrorResponse(0x70).name,"SESSION_INTERRUPTED"));
            ASSERT_EQ(f.getSrcNodeId(),0x8D7633U);ASSERT_EQ(f.getDestNodeId(),0xA9DBECU);
        }
        if(unsigned(f.commandId)==0x0D){++private2;ASSERT_MEM_EQ(f.data,"\x05\x64\x00\x00\x00",5);}
    }
    ASSERT_EQ(high,1U);ASSERT_EQ(standard,1U);ASSERT_TRUE(interrupted>0&&private2>0&&foreign>0);
}
