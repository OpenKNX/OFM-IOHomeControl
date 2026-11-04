#include "atlantic_register_captures.h"
TEST(protocol_atlantic_register_captures_preserve_echo_length_and_wire)
{
    for(const auto &sample:AtlanticRegisterCaptures::frames) {
        IoHomeFrame frame;ASSERT_TRUE(deserializeFrameForTest(frame,sample.wire,sample.length));
        ASSERT_EQ(frame.commandId,IoHomeCommand::WritePrivateResponse);
        ASSERT_EQ(frame.data[0],0x0C);ASSERT_EQ(frame.data[1],0x60);
        ASSERT_EQ(uint16_t(frame.data[2])<<8|frame.data[3],sample.reg);
        uint8_t encoded[32]{};ASSERT_EQ(frame.serialize2W(encoded,sizeof(encoded)),sample.length);
        ASSERT_MEM_EQ(encoded,sample.wire,sample.length);
    }
}
