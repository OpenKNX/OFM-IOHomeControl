// Source fixtures are exact public packet/DATA/object excerpts, not synthetic captures.
#include "issue156_rs100_fixtures.h"
TEST(protocol_issue156_exact_frames_keep_metadata_and_classification)
{
    for(const auto &fixture:Issue156RS100::frames) {
        IoHomeFrame frame;ASSERT_TRUE(deserializeFrameForTest(frame,fixture.wire,fixture.length));
        ASSERT_EQ(frame.getSrcNodeId(),fixture.source);ASSERT_EQ(frame.getDestNodeId(),fixture.destination);
        ASSERT_EQ(uint8_t(frame.commandId),fixture.command);ASSERT_EQ(frame.dataLen,fixture.length-9);
        ASSERT_TRUE(fixture.frequency==IOHC_FREQ_1||fixture.frequency==IOHC_FREQ_2||fixture.frequency==IOHC_FREQ_3);
        if(fixture.command==0x71) {
            ASSERT_TRUE(ioHomeStatusUpdateKind(frame)==(frame.data[2]==0x20?IoHomeStatusUpdateKind::DeviceEvent:IoHomeStatusUpdateKind::Position));
            if(frame.data[2]==0x10) {
                ASSERT_TRUE(frame.ctrlByte0&IOHC_CTRL0_START);ASSERT_TRUE(!(frame.ctrlByte0&IOHC_CTRL0_END));
                ASSERT_EQ(fixture.frequency,868250000U);ASSERT_EQ(frame.data[14],1U);
                ASSERT_EQ(ioHomeDecodeLastCommand(frame).node,0x152375U);
                // Categorical bit interpretation only, not a 0x71 KNX publication or percentage claim.
                ASSERT_EQ(ioHomeBatteryState(frame.data[1]),IoHomeBatteryState::Full);
            }
        }
    }
    IoHomeFrame stopped,moving;
    ASSERT_TRUE(deserializeFrameForTest(stopped,Issue156RS100::remoteStopped,sizeof(Issue156RS100::remoteStopped)));
    ASSERT_TRUE(deserializeFrameForTest(moving,Issue156RS100::remoteMoving,sizeof(Issue156RS100::remoteMoving)));
    ASSERT_EQ((uint16_t(stopped.data[5])<<8)|stopped.data[6],0xC800);
    ASSERT_EQ((uint16_t(stopped.data[7])<<8)|stopped.data[8],0xC800);ASSERT_TRUE(stopped.data[0]&1);
    ASSERT_EQ((uint16_t(moving.data[5])<<8)|moving.data[6],0U);ASSERT_TRUE(!(moving.data[0]&1));
    ASSERT_EQ(std::lround(float((uint16_t(moving.data[7])<<8)|moving.data[8])/512.0f),99);
}

TEST(protocol_issue156_later_sensor_report_is_data_only_and_opaque)
{
    IoHomeFrame frame;frame.init();frame.commandId=IoHomeCommand::StatusUpdate;
    // Synthetic header/context only. The later report has DATA, not a captured packet.
    memcpy(frame.data,Issue156RS100::later2005,sizeof(Issue156RS100::later2005));frame.dataLen=sizeof(Issue156RS100::later2005);
    ASSERT_TRUE(ioHomeStatusUpdateKind(frame)==IoHomeStatusUpdateKind::DeviceEvent);
    ASSERT_EQ(frame.data[8],0xDB);ASSERT_EQ(frame.data[9],0xB9);
    ASSERT_TRUE(memcmp(frame.data,Issue156RS100::immediate2005+9,frame.dataLen)!=0);
    ASSERT_TRUE(!ioHomeDecodeLastCommand(frame).valid);
}

TEST(protocol_issue156_object_dump_lengths_and_get_info_identity_remain_distinct)
{
    const uint16_t lengths[]={150,54,14,16};
    for(unsigned i=0;i<4;++i) {
        const auto &object=Issue156RS100::objects[i];ASSERT_EQ(object.availableLength,lengths[i]);
        ASSERT_EQ(object.complete,object.availableLength==object.declaredLength);
    }
    IoHomeFrame info;ASSERT_TRUE(deserializeFrameForTest(info,Issue156RS100::getInfo1,sizeof(Issue156RS100::getInfo1)));
    ASSERT_EQ(info.dataLen,sizeof(Issue156RS100::object8001));
    ASSERT_MEM_EQ(info.data,Issue156RS100::object8001,info.dataLen);ASSERT_EQ(info.data[10],4U);
}
