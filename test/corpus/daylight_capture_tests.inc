#include "rs100_daylight_objects.h"
TEST(protocol_rs100_daylight_objects_preserve_diffs_without_assigning_units)
{
    const RS100Daylight::Object *first=nullptr,*second=nullptr;unsigned lights=0;
    for(const auto &o:RS100Daylight::objects) {
        if(o.key==0x8005){ASSERT_EQ(o.length,16);ASSERT_EQ(o.data[0],6);for(unsigned i=1;i<16;++i)ASSERT_EQ(o.data[i],0);++lights;}
        else {ASSERT_EQ(o.key,0x8103);ASSERT_EQ(o.length,67);if(o.capture==6)first=&o;else second=&o;}
    }
    ASSERT_EQ(lights,2U);ASSERT_TRUE(first&&second);
    unsigned differences=0;
    for(unsigned i=0;i<67;++i)if(first->data[i]!=second->data[i]){ASSERT_TRUE(i==42||i==45||i==48);++differences;}
    ASSERT_EQ(differences,3U);
}
