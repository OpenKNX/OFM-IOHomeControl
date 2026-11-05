#include "light-followup/evidence.h"
TEST(protocol_light_followup_complete_observations_roundtrip_and_private2_stays_opaque)
{
    unsigned d800=0,d807=0;bool response=false;
    for(const auto &row:LightFollowup::frames){IoHomeFrame frame;ASSERT_TRUE(deserializeFrameForTest(frame,row.wire,row.length));
        uint8_t wire[32]{};const auto length=frame.serialize2W(wire,sizeof(wire));ASSERT_EQ(length,row.length);ASSERT_MEM_EQ(wire,row.wire,length);
        if(frame.commandId==IoHomeCommand::Private2&&frame.dataLen==4){if(frame.data[1]==0)++d800;if(frame.data[1]==7)++d807;}
        if(frame.commandId==IoHomeCommand::Private2Response&&frame.dataLen==5&&!memcmp(frame.data,"\x6D\xD4\x00\x00\x00",5))response=true;
    }
    ASSERT_TRUE(d800&&d807&&response);
}
