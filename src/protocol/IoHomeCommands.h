#pragma once
#include <stdint.h>
#include <string.h>

// io-homecontrol command IDs
// Reference: https://github.com/nicolas5000/io-rts-esp32
//            https://github.com/rspaargaren/iohomecontrol
enum class IoHomeCommand : uint8_t
{
    // Device control
    Execute = 0x00,       // Authenticated - set position/execute action
    ActivateMode = 0x01,  // Authenticated - activate preset mode (my1-my4)
    DirectCommand = 0x02, // Authenticated - manual order / direct command
    Private = 0x03,       // No auth required - status query
    PrivateResponse = 0x04,
    // Unknown0A = 0x0A,  // not used — observed in rspaargaren scan list only
    // Unknown0B = 0x0B,  // not used — observed in rspaargaren scan list only
    Private2 = 0x0C,         // Alternate private command (not used — observed in reference, no handler)
    Private2Response = 0x0D, // Response to 0x0C (not used — empty case in reference)
                             // Unknown0E = 0x0E,  // not used — observed in rspaargaren scan list only
                             // Unknown14 = 0x14,  // not used — observed in rspaargaren scan list only
                             // Unknown16 = 0x16,  // not used — observed in rspaargaren scan list only
                             // Unknown19 = 0x19,  // not used — observed in rspaargaren scan list only
    Identify = 0x1E,         // Authenticated - make device identify itself

    // Cozy/Atlantic thermostat control
    WritePrivate = 0x20,         // Authenticated — set temperature, mode, presence
    WritePrivateResponse = 0x21, // Response to WritePrivate
                                 // Unknown23 = 0x23,  // not used — observed in rspaargaren scan list only
                                 // Unknown25 = 0x25,  // not used — observed in rspaargaren scan list only

    // Discovery & pairing
    DiscoverRequest = 0x28, // Broadcast to 0x00003B
    DiscoverResponse = 0x29,
    DiscoverSPERequest = 0x2A, // Encrypted discovery
    DiscoverSPEResponse = 0x2B,
    Confirmation = 0x2C,
    ConfirmationACK = 0x2D,   // Device ACKs discovery confirmation (not used — consumed implicitly in reference)
    Discover2ERequest = 0x2E, // 1W learning mode / pairing start

    // Key exchange
    SendKey1W = 0x30,               // 1W key transfer (encrypted key + manufacturer + sequence; optional HMAC)
    KeyInitTransfer = 0x31,         // 2W: ask challenge
    KeyTransfer = 0x32,             // 2W: send encrypted system key
    KeyTransferConfirmation = 0x33, // Device confirms key storage
                                    // Unknown34 = 0x34,  // not used — observed in rspaargaren scan list only

    // Address assignment
    AddressRequest = 0x36,  // Request address from device
    AddressResponse = 0x37, // Device responds with address

    // 2W key exchange initiation
    LaunchKeyTransfer = 0x38, // Initiate key transfer with 6-byte challenge
    RemoveController = 0x39,  // Authenticated — remove controller from device
                              // Unknown3A = 0x3A,  // not used — observed in rspaargaren scan list only

    // Challenge-response authentication
    ChallengeRequest = 0x3C,
    ChallengeResponse = 0x3D,

    // Unknown commands (observed in packet captures, undocumented)
    Unknown46Request = 0x46,  // Authentication needed (not used)
    Unknown46Response = 0x47, // (not used)
                              // Unknown48 = 0x48,  // not used — observed in rspaargaren scan list only
    Unknown4ARequest = 0x4A,  // No authentication needed (not used)
    Unknown4AResponse = 0x4B, // (not used)

    // Device info
    GetName = 0x50,
    GetNameResponse = 0x51,
    SetName = 0x52,
    SetNameResponse = 0x53, // not used — response to SetName (per nicolas5000)
    GetGeneralInfo1 = 0x54,
    GetGeneralInfo1Response = 0x55,
    GetGeneralInfo2 = 0x56,
    GetGeneralInfo2Response = 0x57,
    GetGeneralInfo3 = 0x58,
    GetGeneralInfo3Response = 0x59,

    // Undocumented device info range (rspaargaren scan list only)
    // Unknown60 = 0x60,  // not used
    // Unknown64 = 0x64,  // not used
    // Unknown6E = 0x6E,  // not used

    // Device feedback configuration
    SetConfig1 = 0x6F,         // Authentication needed - request automatic status feedback
    SetConfig1Response = 0x70, // Response to 0x6F

    // Status
    StatusUpdate = 0x71,
    StatusUpdateResponse = 0x72, // ACK for unsolicited 0x71; payload {0x05, 0x00}
                                 // Unknown73 = 0x73,  // not used — observed in rspaargaren scan list only

    // Extended command range (rspaargaren scan list only, undocumented)
    // May be a parallel set for a different device class or protocol version
    // Unknown80 = 0x80,  // not used
    // Unknown82 = 0x82,  // not used
    // Unknown84 = 0x84,  // not used
    // Unknown86 = 0x86,  // not used
    // Unknown88 = 0x88,  // not used
    // Unknown8A = 0x8A,  // not used
    // Unknown8B = 0x8B,  // not used
    // Unknown8E = 0x8E,  // not used
    // Unknown90 = 0x90,  // not used
    // Unknown92 = 0x92,  // not used
    // Unknown94 = 0x94,  // not used
    // Unknown96 = 0x96,  // not used
    // Unknown98 = 0x98,  // not used

    // Error
    ErrorResponse = 0xFE
};

// Configured 1W enrollment completion policy. Automatic stays conservative:
// it resolves to STOP+DOWN only for a controller profile whose manufacturer is
// explicitly VELUX; unknown and Somfy-style profiles resolve to no finalizer.
enum class OneWayEnrollmentFinalizer : uint8_t
{
    Automatic = 0,
    None = 1,
    StopDown = 2,
};

// io-homecontrol device types
enum class IoHomeDeviceType : uint8_t
{
    Unknown = 0x00,
    VenetianBlind = 0x01,
    RollerShutter = 0x02,
    Awning = 0x03,
    WindowOpener = 0x04,
    GarageOpener = 0x05,
    Light = 0x06,
    GateOpener = 0x07,
    RollingDoorOpener = 0x08,
    Lock = 0x09,
    Blind = 0x0A,
    Unknown0B = 0x0B, // observed but unidentified
    Beacon = 0x0C,
    DualShutter = 0x0D,
    HeatingTempInterface = 0x0E,
    OnOffSwitch = 0x0F,
    HorizontalAwning = 0x10,
    ExternalVenetianBlind = 0x11,
    LouvrBlind = 0x12,
    CurtainTrack = 0x13,
    VentilationPoint = 0x14,
    ExteriorHeating = 0x15,
    HeatPump = 0x16,
    IntrusionAlarm = 0x17,
    SwingingShutter = 0x18
};

// io-homecontrol manufacturer IDs
enum class IoHomeManufacturer : uint8_t
{
    Unknown = 0x00,
    Velux = 0x01,
    Somfy = 0x02,
    Honeywell = 0x03,
    Hormann = 0x04,
    AssaAbloy = 0x05,
    Niko = 0x06,
    WindowMaster = 0x07,
    Renson = 0x08,
    Ciat = 0x09,
    Secuyou = 0x0A,
    Overkiz = 0x0B,
    AtlanticGroup = 0x0C
};

// Check if device type only supports open/close (no continuous positioning)
inline bool isOpenCloseOnly(IoHomeDeviceType iType)
{
    return iType == IoHomeDeviceType::Lock ||
           iType == IoHomeDeviceType::OnOffSwitch ||
           iType == IoHomeDeviceType::IntrusionAlarm ||
           iType == IoHomeDeviceType::SwingingShutter;
}

// Special position values for Execute command
#define IOHC_POSITION_STOP 0xD200
#define IOHC_POSITION_UNKNOWN 0xD400
#define IOHC_POSITION_FAVORITE 0xD800
#define IOHC_POSITION_MAX 0xC800        // 100% = fully closed
#define IOHC_POSITION_VENT 0xD803       // ventilation position
#define IOHC_POSITION_FORCE_OPEN 0x6400 // force open (50%)

// ACEI byte layout (Access Control / Execute Info)
// Used in Execute (0x00) and ActivateMode (0x01) data[1]
// bits[7:5] = priority level, bits[4:3] = service number,
// bits[2:1] = extended info, bit[0] = is_valid (must be 1)
#define IOHC_ACEI_PRIORITY_MASK 0xE0
#define IOHC_ACEI_SERVICE_MASK 0x18
#define IOHC_ACEI_EXTENDED_MASK 0x06
#define IOHC_ACEI_VALID_BIT 0x01
#define IOHC_ACEI_DEFAULT 0x67 // priority=3 (user remote), service=0, extended=3, valid=1
#define IOHC_ACEI_1W 0x43      // priority=2 (user), service=0, extended=1, valid=1 (1W mode)

// 2W Execute extended profile byte. Somfy RS100 captures use the silent
// profile for absolute-position and favourite commands.
#define IOHC_EXECUTE_PROFILE_SILENT 0x05
#define IOHC_EXECUTE_PROFILE_DEFAULT 0x06

// Command originator IDs (Execute data[0])
#define IOHC_ORIGINATOR_LOCAL 0x00     // local user (button on device)
#define IOHC_ORIGINATOR_USER 0x01      // remote user (remote control)
#define IOHC_ORIGINATOR_RAIN 0x02      // rain sensor
#define IOHC_ORIGINATOR_TIMER 0x03     // timer
#define IOHC_ORIGINATOR_SCD 0x04       // security/comfort device
#define IOHC_ORIGINATOR_SAAC 0x08      // stand-alone automatic control
#define IOHC_ORIGINATOR_WIND 0x09      // wind sensor
#define IOHC_ORIGINATOR_SELF 0x10      // actuator itself
#define IOHC_ORIGINATOR_EMERGENCY 0xFF // emergency override

// ACEI priority levels (bits[7:5] of ACEI byte)
#define IOHC_PRIORITY_PROTECTION 0  // human protection (highest)
#define IOHC_PRIORITY_SENSOR 1      // environment sensor
#define IOHC_PRIORITY_USER 2        // user (local button)
#define IOHC_PRIORITY_USER_REMOTE 3 // user (remote control) — default
#define IOHC_PRIORITY_COMFORT 4     // comfort
#define IOHC_PRIORITY_AUTO_LOW 5    // automatic (low)
#define IOHC_PRIORITY_AUTO_MID 6    // automatic (mid)
#define IOHC_PRIORITY_AUTO_HIGH 7   // automatic (high, lowest priority)

// Broadcast addresses
constexpr uint8_t IOHC_BROADCAST_DISCOVER[3] = {0x00, 0x00, 0x3B};
constexpr uint8_t IOHC_BROADCAST_DISCOVER2E[3] = {0x00, 0x00, 0x3F};
constexpr uint8_t IOHC_BROADCAST_GROUP[3] = {0x00, 0x00, 0x00};

// io-homecontrol address classes (derived from 3-byte node ID)
enum class IoHomeAddressClass : uint8_t
{
    Group,               // 0x000000 — group broadcast
    BroadcastDeviceType, // 0x0001xx..0x003Axx — broadcast by device type
    DiscoverAll,         // 0x00003B — discovery broadcast
    DiscoverAlt,         // 0x00003F — alternate discovery
    Unicast              // any address with high byte != 0
};

inline IoHomeAddressClass getAddressClass(uint32_t iNodeId)
{
    uint8_t lHigh = (iNodeId >> 16) & 0xFF;
    if (lHigh != 0)
        return IoHomeAddressClass::Unicast;
    uint16_t lLow = iNodeId & 0xFFFF;
    if (lLow == 0x0000)
        return IoHomeAddressClass::Group;
    if (lLow == 0x003B)
        return IoHomeAddressClass::DiscoverAll;
    if (lLow == 0x003F)
        return IoHomeAddressClass::DiscoverAlt;
    return IoHomeAddressClass::BroadcastDeviceType;
}

inline void encodePackedDeviceType(uint16_t iType, uint8_t iSubtype,
                                   uint8_t &oTypeLsb, uint8_t &oTypeSub)
{
    oTypeLsb = static_cast<uint8_t>(iType & 0xFF);
    oTypeSub = static_cast<uint8_t>(((iType >> 8) & 0x03) | ((iSubtype & 0x3F) << 2));
}

// io-homecontrol frequencies (Hz)
#define IOHC_FREQ_1 868250000UL // 868.25 MHz
#define IOHC_FREQ_2 868950000UL // 868.95 MHz
#define IOHC_FREQ_3 869850000UL // 869.85 MHz
#define IOHC_NUM_FREQUENCIES 3

constexpr uint32_t IOHC_FREQUENCIES[IOHC_NUM_FREQUENCIES] = {
    IOHC_FREQ_2, IOHC_FREQ_3, IOHC_FREQ_1}; // scan order: CH2→CH3→CH1 (per nicolas5000)

// Radio parameters
#define IOHC_BITRATE 38400
#define IOHC_BANDWIDTH 250000
#define IOHC_FREQ_DEV 19200
#define IOHC_PREAMBLE_LONG 1024       // symbols (bytes), wakes low-power 2W targets
#define IOHC_PREAMBLE_NORMAL_START 32 // symbols (bytes), normal always-alive 2W START
#define IOHC_PREAMBLE_SHORT 8         // symbols (bytes), for continuation frames
#define IOHC_NAME_MAX_SIZE 16   // max name payload bytes (per nicolas5000: CMD_PARAM_NAME_MAXSIZE/2)

// 1W repeat transmission (fire-and-forget sends 4x at 40ms intervals)
#define IOHC_1W_REPEAT_COUNT 4        // 4 additional repeats (5 total transmissions, matches reference)
#define IOHC_1W_REPEAT_INTERVAL_MS 40 // ms between 1W repeat transmissions
#define IOHC_1W_ENROLL_FINALIZER_DELAY_MS 40
#define IOHC_1W_ENROLL_FINALIZER_DEADLINE_MS 3000

// Sync word
constexpr uint8_t IOHC_SYNC_WORD[3] = {0x55, 0xFF, 0x33};
#define IOHC_SYNC_WORD_SIZE 3

// Transfer key used during pairing (hardcoded in protocol)
constexpr uint8_t IOHC_TRANSFER_KEY[16] = {
    0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
    0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73};

// EMS2 protocol (building-wide wake + alternate sync word)
constexpr uint8_t IOHC_EMS2_SYNC_WORD[2] = {0x2D, 0xD4};
#define IOHC_EMS2_SYNC_WORD_SIZE 2
#define IOHC_EMS2_FREQ IOHC_FREQ_2 // 868.95 MHz
#define IOHC_EMS2_WAKE_DURATION_MS 10000

// Cozy thermostat mode constants (WritePrivate 0x20 payload)
// Values from rspaargaren device captures
#define IOHC_COZY_MODE_AUTO 0x00
#define IOHC_COZY_MODE_MANUAL 0x01
#define IOHC_COZY_MODE_PROG 0x02
#define IOHC_COZY_MODE_OFF 0x04

// Cozy thermostat presence/window constants
#define IOHC_COZY_PRESENCE_ON 0x01
#define IOHC_COZY_PRESENCE_OFF 0x00
#define IOHC_COZY_WINDOW_OPEN 0x01
#define IOHC_COZY_WINDOW_CLOSED 0x00

// Cozy thermostat payload builders (for WritePrivate 0x20)
// Format from rspaargaren device captures: {originator, ACEI, 0x01, sub-cmd, [value, ...]}
// Cozy uses originator=0x0C (Atlantic mfr) and ACEI=0x61 (valid, level 0)
// NOT the standard Execute originator/ACEI (0x01/0x67)
#define IOHC_COZY_ORIGINATOR 0x0C
#define IOHC_COZY_ACEI_READ 0x60  // read/trigger commands (powerOn, midnight)
#define IOHC_COZY_ACEI_WRITE 0x61 // write commands (temp, mode, presence, window)

namespace IoHomeCozyPayload
{
    inline uint8_t buildTemperature(uint8_t *oData, uint8_t iTempTenths)
    {
        oData[0] = IOHC_COZY_ORIGINATOR;
        oData[1] = IOHC_COZY_ACEI_WRITE;
        oData[2] = 0x01;
        oData[3] = 0x03;        // temp set sub-command
        oData[4] = iTempTenths; // temperature × 10 (70-280 for 7.0-28.0°C)
        oData[5] = 0x00;
        return 6;
    }
    inline uint8_t buildMode(uint8_t *oData, uint8_t iMode)
    {
        oData[0] = IOHC_COZY_ORIGINATOR;
        oData[1] = IOHC_COZY_ACEI_WRITE;
        oData[2] = 0x01;
        oData[3] = 0x00; // mode set sub-command
        oData[4] = iMode;
        return 5;
    }
    inline uint8_t buildPresence(uint8_t *oData, uint8_t iValue)
    {
        oData[0] = IOHC_COZY_ORIGINATOR;
        oData[1] = IOHC_COZY_ACEI_WRITE;
        oData[2] = 0x01;
        oData[3] = 0x10; // presence sub-command
        oData[4] = iValue;
        return 5;
    }
    inline uint8_t buildWindow(uint8_t *oData, uint8_t iValue)
    {
        oData[0] = IOHC_COZY_ORIGINATOR;
        oData[1] = IOHC_COZY_ACEI_WRITE;
        oData[2] = 0x01;
        oData[3] = 0x0E; // window open/close sub-command
        oData[4] = iValue;
        return 5;
    }
    inline uint8_t buildPowerOn(uint8_t *oData)
    {
        oData[0] = IOHC_COZY_ORIGINATOR;
        oData[1] = IOHC_COZY_ACEI_READ;
        oData[2] = 0x01;
        oData[3] = 0x2C;
        return 4;
    }
    inline uint8_t buildMidnightSync(uint8_t *oData)
    {
        oData[0] = IOHC_COZY_ORIGINATOR;
        oData[1] = IOHC_COZY_ACEI_READ;
        oData[2] = 0x01;
        oData[3] = 0x30;
        return 4;
    }
}

// Command scanning — list of known command IDs for device probing
constexpr uint8_t IOHC_SCAN_COMMANDS[] = {
    0x00, 0x01, 0x02, 0x03, 0x0C, 0x1E, 0x20, 0x28, 0x2A, 0x2C, 0x2E,
    0x31, 0x32, 0x36, 0x38, 0x39, 0x3C, 0x46, 0x4A,
    0x50, 0x52, 0x54, 0x56, 0x58, 0x6F, 0x71};
#define IOHC_SCAN_COMMANDS_COUNT (sizeof(IOHC_SCAN_COMMANDS) / sizeof(IOHC_SCAN_COMMANDS[0]))

// Blind travel-time position interpolation (pure math, no hardware dependency)
inline float interpolatePosition(float iStart, float iTarget,
                                 uint32_t iElapsedMs, uint32_t iTotalMs)
{
    if (iTotalMs == 0 || iElapsedMs >= iTotalMs)
        return iTarget;
    return iStart + (iTarget - iStart) * (float)iElapsedMs / (float)iTotalMs;
}

// Snap position to exact boundaries to avoid float precision drift
// Values within 0.5% of endpoints snap to 0% or 100%
inline float snapPositionBoundary(float iPosition)
{
    if (iPosition <= 0.5f)
        return 0.0f;
    if (iPosition >= 99.5f)
        return 100.0f;
    return iPosition;
}

inline uint32_t estimateTravelDurationMs(float iStart, float iTarget,
                                         float iOpeningSeconds, float iClosingSeconds)
{
    float lDistance = iTarget >= iStart ? (iTarget - iStart) : (iStart - iTarget);
    if (lDistance <= 0.0f)
        return 0;

    float lFullTravelSeconds = iTarget > iStart ? iClosingSeconds : iOpeningSeconds;
    if (lFullTravelSeconds <= 0.0f)
        return 0;

    float lDurationMs = lFullTravelSeconds * 1000.0f * lDistance / 100.0f;
    return lDurationMs <= 0.0f ? 0 : (uint32_t)(lDurationMs + 0.5f);
}
