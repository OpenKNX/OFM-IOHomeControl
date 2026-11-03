// Included after the existing native-controller helpers in test_protocol.cpp.
#include "atlantic_issues_198_199.h"

TEST(protocol_atl199_all_thirteen_exact_issue_observations_preserve_frame_context)
{
    unsigned discover=0,write=0;
    const uint8_t variants[]={1,9,10,11,12,14,15,16,17,19,23};
    for(const auto &row:kAtlanticIssue199) {
        IoHomeFrame frame;
        ASSERT_TRUE(frame.deserializeFrame(row.wire,row.length));
        ASSERT_EQ(frame.ctrlByte0,row.wire[0]);ASSERT_EQ(frame.ctrlByte1,row.wire[1]);
        ASSERT_EQ(frame.getSrcNodeId(),0xD46FF6U);
        ASSERT_TRUE(!(frame.ctrlByte0&IOHC_CTRL0_MODE_1W));
        ASSERT_TRUE(!frame.hasHmac&&!frame.hasCrc); // packet excerpt supplies neither proof nor CRC
        uint8_t replay[IOHC_FRAME_BUFFER_SIZE]{};
        ASSERT_EQ(frame.serialize2W(replay,sizeof(replay)),row.length);
        ASSERT_MEM_EQ(replay,row.wire,row.length);
        if(frame.commandId==IoHomeCommand::DiscoverRequest) {
            ++discover;ASSERT_EQ(frame.getDestNodeId(),0x00FFFBU);ASSERT_EQ(frame.dataLen,0U);
            ASSERT_TRUE(row.frequency==868250000U||row.frequency==869850000U);
            ASSERT_TRUE(!ioHomeObserveAtlantic21(frame.commandId,frame.data,frame.dataLen).valid);
        } else {
            ASSERT_EQ(frame.commandId,IoHomeCommand::WritePrivate);ASSERT_EQ(frame.getDestNodeId(),0x3FU);
            ASSERT_EQ(row.frequency,868950000U);ASSERT_EQ(frame.dataLen,6U);
            const auto observed=ioHomeObserveAtlantic21(frame.commandId,frame.data,frame.dataLen);
            ASSERT_TRUE(observed.valid);ASSERT_EQ(observed.value,variants[write]);ASSERT_EQ(frame.data[5],0U);
            ++write;
        }
    }
    ASSERT_EQ(discover,2U);ASSERT_EQ(write,11U);
}

TEST(protocol_atl199_unknown_21_taxonomy_does_not_accept_other_commands_or_shapes)
{
    uint8_t data[]={0x0C,0x61,1,0x21,0x17,0};
    ASSERT_TRUE(ioHomeObserveAtlantic21(IoHomeCommand::WritePrivate,data,6).valid);
    ASSERT_TRUE(!ioHomeObserveAtlantic21(IoHomeCommand::WritePrivateResponse,data,6).valid);
    ASSERT_TRUE(!ioHomeObserveAtlantic21(IoHomeCommand::Execute,data,6).valid);
    ASSERT_TRUE(!ioHomeObserveAtlantic21(IoHomeCommand::WritePrivate,nullptr,6).valid);
    for(uint8_t size=0;size<6;++size)
        ASSERT_TRUE(!ioHomeObserveAtlantic21(IoHomeCommand::WritePrivate,data,size).valid);
    for(unsigned index:{0U,1U,2U,3U,5U}) {
        data[index]^=1;ASSERT_TRUE(!ioHomeObserveAtlantic21(IoHomeCommand::WritePrivate,data,6).valid);data[index]^=1;
    }
}

TEST(controller_atl199_foreign_28_never_advances_normal_pairing_or_recovers_a_key)
{
    const uint8_t key[16]={1};
    IoHomeController controller;IoHomecontrol module;IoHomecontrolChannel channel;
    initPaired2WControllerForTest(controller,module,channel,0x60F823,0,key);controller.setSystemKey(key);
    ASSERT_TRUE(controller.startPairing(0));IoHomeFrame request;
    ASSERT_TRUE(transmitQueuedControllerFrame(controller,request));
    ASSERT_EQ(request.commandId,IoHomeCommand::DiscoverRequest);
    for(const auto &row:kAtlanticIssue199) {
        if(row.wire[8]!=0x28)continue;
        controller.testSetRxScanFrequency(row.frequency);
        const auto count=controller.radio().testTransmitCount();
        controller.radio().testQueueReceivedPacket(row.wire,row.length);controller.loop();
        ASSERT_EQ(controller.state(),ControllerState::PairWaitDiscoveryResponse);
        ASSERT_EQ(controller.radio().testTransmitCount(),count);
        ASSERT_TRUE(!channel.isPaired()&&!channel.hasProtocolIdentity());
        ASSERT_TRUE(controller.protocolIdentityForIoAddress(0xD46FF6)==nullptr);
        ASSERT_EQ(controller.pairingTelemetry().keyExchangeAttempts,0U);
        ASSERT_MEM_EQ(controller.getSystemKey(),key,16);
        ASSERT_EQ(controller.atlanticStatus().stage,IoHomeController::AtlanticStage::None);
        ASSERT_TRUE(!controller.isGatewayMode());
    }
    // Noise must not wedge the pairing state or create a device on cancellation.
    controller.cancelPairing();controller.loop();ASSERT_EQ(controller.state(),ControllerState::Idle);
    ASSERT_TRUE(!channel.isPaired());ASSERT_EQ(controller.getGatewayPairedDeviceCount(),0U);
}

TEST(controller_atl199_broadcast_write_private_never_mutates_paired_device_state)
{
    const uint8_t key[16]={1};
    for(bool senderIsPaired:{false,true})for(bool trace:{false,true}) {
        IoHomeController controller;IoHomecontrol module;IoHomecontrolChannel channel;
        initPaired2WControllerForTest(controller,module,channel,0x60F823,senderIsPaired?0xD46FF6:0x123456,key);
        controller.setSystemKey(key);controller.setDynamicEventTrace(trace);
        const auto count=controller.radio().testTransmitCount();
        for(const auto &row:kAtlanticIssue199) {
            if(row.wire[8]!=0x20)continue;
            controller.testSetRxScanFrequency(row.frequency);
            controller.radio().testQueueReceivedPacket(row.wire,row.length);controller.loop();
            ASSERT_TRUE(!channel.testHasPositionFeedback()&&!channel.testHasTargetPositionFeedback());
            ASSERT_TRUE(!channel.testHasStatusUpdate()&&!channel.testStatusExpected());
            ASSERT_TRUE(!channel.lastDeviceEvent().valid&&!channel.testHasCommandExchangeResult());
            ASSERT_TRUE(!channel.testHasScheduledStatusPoll());
            ASSERT_EQ(controller.radio().testTransmitCount(),count);
            ASSERT_EQ(controller.atlanticStatus().stage,IoHomeController::AtlanticStage::None);
            ASSERT_MEM_EQ(controller.getSystemKey(),key,16);ASSERT_MEM_EQ(channel.getEncryptionKey(),key,16);
        }
    }
}

TEST(protocol_atl198_metadata_projection_preserves_type_and_subtype_without_claiming_raw_capture)
{
    // Constructed from reported metadata. Issue198 does NOT provide these nine wire bytes.
    const uint8_t projected[]={0x0C,0xC0,0xF9,0xCF,0x0F,0x0C,0xDC,0,0};
    const auto identity=decodeAcceptedDiscoveryIdentity(projected,sizeof(projected));
    ASSERT_TRUE(identity.valid&&identity.fullMetadata);
    ASSERT_EQ(identity.profile,0x33);ASSERT_EQ(identity.subProfile,0U);ASSERT_EQ(identity.manufacturerId,0x0C);
    ASSERT_EQ(identity.ioBackboneAddress,0xF9CF0FU);ASSERT_EQ(identity.powerSaveMode,IoHomePowerMode::AlwaysAlive);
    ASSERT_EQ(identity.klfTurnaroundHintMs,40U);ASSERT_TRUE(!identity.responseTimeUnitConfirmed);
    ASSERT_EQ(static_cast<uint8_t>(IoHomeDeviceType::DomesticHotWater),0x33);
    ASSERT_EQ(static_cast<uint8_t>(IoHomeDeviceType::ElectricalHeater),0x34);
    ASSERT_TRUE(!std::strcmp(ioHomeUnqualifiedApplianceTypeName(identity.profile),"domestic_hot_water"));
    ASSERT_TRUE(!std::strcmp(ioHomeUnqualifiedApplianceTypeName(0x34),"electrical_heater"));
    ASSERT_TRUE(ioHomeProfileDescriptor(identity)==nullptr);
}

TEST(controller_atl198_metadata_cannot_inherit_generic_shutter_or_cozy_controls)
{
    const uint8_t key[16]={1};
    for(uint16_t type:{uint16_t(0x33),uint16_t(0x34)})for(bool manualShutter:{false,true}) {
        IoHomeController controller;IoHomecontrol module;IoHomecontrolChannel channel;
        initPaired2WControllerForTest(controller,module,channel,0x60F823,0xF9CF0F,key);
        IoHomeProtocolIdentity identity{};identity.valid=identity.fullMetadata=true;
        identity.nodeClass=IoHomeNodeClass::Actuator;identity.ioAddress=0xF9CF0F;
        identity.profile=type;identity.subProfile=0;identity.manufacturerId=0x0C;
        identity.powerSaveMode=IoHomePowerMode::AlwaysAlive;
        channel.onProtocolIdentity(identity.ioAddress,identity);
        if(manualShutter)channel.setManualProfileOverride(0x0080);
        ASSERT_TRUE(!channel.allowsActuatorControls());
        ASSERT_TRUE(!controller.sendCommand(channel.getNodeId(),key,IoHomeCommand::Execute,0));
        ASSERT_TRUE(!controller.sendCommand(channel.getNodeId(),key,IoHomeCommand::Execute,100));
        ASSERT_TRUE(!controller.sendProfileParameterCommand(channel.getNodeId(),key,ParameterSemantic::SlatOrientation,50));
        ASSERT_TRUE(!controller.sendCommand(channel.getNodeId(),key,IoHomeCommand::WritePrivate,3,200));
        ASSERT_EQ(controller.radio().testTransmitCount(),0U);
        // Read-only identity/object investigation remains possible, without issuing a packet here.
        ASSERT_TRUE(controller.requestObjectRead(&channel,0,0x8001,0,14));
        ASSERT_TRUE(controller.cancelObjectRead(controller.objectReadToken()));
        controller.loop();ASSERT_EQ(controller.radio().testTransmitCount(),0U);
        ASSERT_EQ(channel.getProtocolIdentity().profile,type);ASSERT_EQ(channel.getProtocolIdentity().subProfile,0U);
        ASSERT_EQ(channel.getProtocolIdentity().manufacturerId,0x0C);
    }
}

TEST(protocol_atl198_reported_message_timeline_preserves_missing_key_confirmation)
{
    const uint8_t commands[]={0x28,0x29,0x2C,0x2D,0x31,0x3C,0x32,0x6F};
    const bool received[]={false,true,false,true,false,true,false,false};
    ASSERT_EQ(sizeof(kAtlanticIssue198Messages)/sizeof(kAtlanticIssue198Messages[0]),8U);
    unsigned previous=0,index=0;
    for(const auto &message:kAtlanticIssue198Messages) {
        ASSERT_EQ(message.command,commands[index]);ASSERT_EQ(message.received,received[index]);
        ASSERT_TRUE(message.elapsedMs>=previous);previous=message.elapsedMs;++index;
        ASSERT_TRUE(message.command!=0x33); // no manufactured confirmation packet
    }
    ASSERT_EQ(kAtlantic198ReportedPairingDurationMs,1944U);
    ASSERT_TRUE(!kAtlantic198RawKeyConfirmationSupplied);
}

TEST(controller_atl198_projected_pairing_prefix_waits_for_missing_confirmation)
{
    // Constructed controller exercise, NOT a raw Calypso capture replay.
    const uint8_t key[16]={1},challenge[6]={1,2,3,4,5,6};
    IoHomeController controller;IoHomecontrol module;IoHomecontrolChannel channel;
    initPaired2WControllerForTest(controller,module,channel,0x60F823,0,key);controller.setSystemKey(key);
    ASSERT_TRUE(controller.startPairing(0));IoHomeFrame frame,response;
    ASSERT_TRUE(transmitQueuedControllerFrame(controller,frame));ASSERT_EQ(frame.commandId,IoHomeCommand::DiscoverRequest);
    buildDiscoverResponseFrame(response,0x60F823,0xF9CF0F,false,IOHC_POWER_SAVE_ALWAYS_ALIVE);
    const uint8_t projection[]={0x0C,0xC0,0xF9,0xCF,0x0F,0x0C,0xDC,0,0};
    memcpy(response.data,projection,sizeof(projection));response.dataLen=sizeof(projection);
    ASSERT_TRUE(queueControllerResponse(controller,response));
    ASSERT_TRUE(lastTransmittedFrameForTest(controller,frame));ASSERT_EQ(frame.commandId,IoHomeCommand::Confirmation);
    ASSERT_EQ(controller.state(),ControllerState::PairWaitDiscoveryConfirmationAck);
    ASSERT_EQ(channel.getProtocolIdentity().profile,0x33);ASSERT_EQ(channel.getProtocolIdentity().subProfile,0U);
    ASSERT_EQ(channel.getProtocolIdentity().manufacturerId,0x0C);
    ASSERT_TRUE(!channel.allowsActuatorControls());
    buildDiscoveryConfirmationAckFrame(response,0x60F823,0xF9CF0F);
    ASSERT_TRUE(queueControllerResponse(controller,response));ASSERT_EQ(controller.state(),ControllerState::PairWaitKeyInitDelay);
    ioHomeTestAdvanceMillis(IOHC_PAIR_KEY_INIT_DELAY_DEFAULT_MS);
    ASSERT_TRUE(transmitQueuedControllerFrame(controller,frame));ASSERT_EQ(frame.commandId,IoHomeCommand::KeyInitTransfer);
    ASSERT_TRUE(!(frame.ctrlByte1&IOHC_CTRL1_LOW_POWER));
    buildPairChallengeRequestFrame(response,0x60F823,0xF9CF0F,challenge);
    ASSERT_TRUE(queueControllerResponse(controller,response));
    ASSERT_TRUE(lastTransmittedFrameForTest(controller,frame));ASSERT_EQ(frame.commandId,IoHomeCommand::KeyTransfer);
    ASSERT_EQ(controller.state(),ControllerState::PairWaitKeyTransferConfirmation);
    ASSERT_TRUE(!channel.isPaired());ASSERT_EQ(channel.getNodeId(),0U);
    ASSERT_TRUE(controller.pairingTelemetry().outcome!=IoHomeController::PairingOutcome::Success);
    // The reported success/6F in issue198 cannot supply an absent 33 packet.
    controller.cancelPairing();controller.loop();ASSERT_EQ(controller.state(),ControllerState::Idle);
}
