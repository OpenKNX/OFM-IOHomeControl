TEST(protocol_discovery_target_requires_exact_class_suffix_and_supported_profile)
{
    ASSERT_EQ(ioHomeDiscoveryTargetType(0x00FFFB),1023);
    ASSERT_EQ(ioHomeDiscoveryTargetType(0x0002FB),11);
    for(uint32_t dst:{0x00003B,0x00003F,0x01FFFB,0x00FFFC,0x00067B,0x100FFFB})
        ASSERT_EQ(ioHomeDiscoveryTargetType(dst),0);
}
TEST(controller_key_extract_typed_discovery_is_request_local)
{
    ioHomeTestSetMillis(1000);ioHomeTestSetMicros(1000000);
    IoHomeController c;IoHomecontrol m;initKeyExtractControllerForTest(c,m,0x112233);
    ASSERT_TRUE(c.startKeyExtraction());
    auto query=[&](uint32_t dst,uint16_t profile) {
        IoHomeFrame req,resp;buildGatewayDiscoverRequest(req,0xD46FF6);req.setDestNode(dst);
        ASSERT_TRUE(queueGatewayRequestAndLoop(c,req,resp));
        ASSERT_EQ(resp.commandId,IoHomeCommand::DiscoverResponse);
        ASSERT_EQ(uint16_t(resp.data[0])<<2|(resp.data[1]>>6),profile);
        ASSERT_EQ(resp.data[1]&0x3F,0);ASSERT_EQ(resp.data[5],2);
        ASSERT_EQ(resp.getDestNodeId(),0xD46FF6U);
        // Drain the remaining broadcast copies before the next request.
        for(unsigned i=0;i<10;++i){ioHomeTestAdvanceMillis(100);c.loop();}
    };
    query(0x00FFFB,1023);query(0x00003B,2);query(0x00067B,2);
    c.stopKeyExtraction();ASSERT_TRUE(c.startKeyExtraction());query(0x00003B,2);
}
