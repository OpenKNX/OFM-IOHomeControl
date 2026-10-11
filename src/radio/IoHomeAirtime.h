#pragma once
#include <cstdint>

// Qualified waveform timing is still required. These are conservative host
// admission/accounting models for the configured io-homecontrol PHYs only.
enum class IoHomeTxPhy : uint8_t { Sx1276IoHome, Sx1262SoftwareUart };
#if defined(RADIO_SX1262) || defined(TEST_NATIVE)
constexpr IoHomeTxPhy kIoHomeTxPhy = IoHomeTxPhy::Sx1262SoftwareUart;
#else
constexpr IoHomeTxPhy kIoHomeTxPhy = IoHomeTxPhy::Sx1276IoHome;
#endif
struct IoHomeAirtime {
    uint32_t preambleBits, syncBits, bodyBits, preambleUs, packetUs, budgetMs;
};
inline IoHomeAirtime ioHomeAirtime(uint8_t frameBytes, uint16_t preambleBytes,
                                  IoHomeTxPhy phy = kIoHomeTxPhy) {
    const bool sx1276 = phy == IoHomeTxPhy::Sx1276IoHome;
    // SX1276 IoHomeOn observations fit 10 bit-times/configured preamble byte.
    // Sync is conservatively bounded at 3 UART cells including the power byte.
    // The length byte is ALREADY in frameBytes; never add it a second time.
    // SX1262 SetPacketParams takes preamble bits, sync=24; UART(frame+CRC)
    // is packed into complete octets by sx1262EncodeIoHomeFrame (pad bits radiate).
    const uint32_t pre = uint32_t(preambleBytes) * (sx1276 ? 10U : 8U);
    const uint32_t sync = sx1276 ? 30U : 24U;
    const uint32_t cells = (uint32_t(frameBytes) + 2U) * 10U;
    const uint32_t body = sx1276 ? cells : ((cells + 7U) / 8U) * 8U;
    // Use 38,000 bps as an intentional conservative bound for configured
    // 32MHz/833 (SX1276) and 32*32MHz/26667 (SX1262), nominally ~38.4kbps.
    // Add 5ms fixed allowance for mode/ramp uncertainty, not measured RF time.
    const uint32_t preUs = uint32_t((uint64_t(pre)*1000000ULL + 37999U)/38000U);
    const uint32_t packetUs = uint32_t((uint64_t(pre+sync+body)*1000000ULL + 37999U)/38000U) + 5000U;
    return {pre, sync, body, preUs, packetUs, (packetUs+999U)/1000U};
}
