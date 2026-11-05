// Class A published 29/2C/31 headers; synthetic session/clock/radio injections.
// https://github.com/laberning/home_io_control/issues/187#issuecomment-6655235183
TEST(controller_pair_discovery_channel_is_peer_bound_and_session_only)
{
    const uint8_t key[16]={1};
    for(bool follow:{false,true}){
        IoHomeController c;IoHomecontrol m;IoHomecontrolChannel ch;initPaired2WControllerForTest(c,m,ch,0x30361D,0x1E0A9B,key);
        ASSERT_TRUE(!c.startPairingOnDiscoveryChannel(0,0x1E0A9B));ch.setPaired(false);
        ASSERT_TRUE(follow?c.startPairingOnDiscoveryChannel(0,0x1E0A9B):c.startPairing(0,0x1E0A9B));
        IoHomeFrame tx,rx;ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_EQ(tx.commandId,IoHomeCommand::DiscoverRequest);
        const uint8_t observed[]={0xD1,0,0x30,0x36,0x1D,0x1E,0x0A,0x9B,0x29,0,0x80,0x1E,0x0A,0x9B,2,0x1D,2,0xC7};
        ASSERT_TRUE(deserializeFrameForTest(rx,observed,sizeof(observed)));c.testSetRxScanFrequency(IOHC_FREQ_3);
        ASSERT_TRUE(queueControllerResponse(c,rx));ASSERT_TRUE(lastTransmittedFrameForTest(c,tx));ASSERT_EQ(tx.commandId,IoHomeCommand::Confirmation);
        ASSERT_EQ(c.radio().testCurrentFrequency(),follow?IOHC_FREQ_3:IOHC_FREQ_2);ASSERT_TRUE(!ch.isPaired());
        IoHomeFrame ack;buildDiscoveryConfirmationAckFrame(ack,0x30361D,0x1E0A9B);ASSERT_TRUE(queueControllerResponse(c,ack));
        ioHomeTestAdvanceMillis(IOHC_PAIR_KEY_INIT_DELAY_DEFAULT_MS);ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_EQ(tx.commandId,IoHomeCommand::KeyInitTransfer);
        ASSERT_EQ(c.radio().testCurrentFrequency(),follow?IOHC_FREQ_3:IOHC_FREQ_2);ASSERT_TRUE(!ch.isPaired());
        c.cancelPairing();ASSERT_TRUE(c.startPairing(0,0x1E0A9B));ASSERT_TRUE(transmitQueuedControllerFrame(c,tx));ASSERT_EQ(c.radio().testCurrentFrequency(),IOHC_FREQ_2);
    }
}
