#pragma once
#include <stdint.h>
#include <string.h>
#include <string>

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
    // Capture-derived priority/lock arbitration levels; semantics remain
    // unconfirmed and no 0x1A reply has been captured. No active handler.
    PriorityLevelRequest = 0x19,
    PriorityLevelResponse = 0x1A,
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
    ConfirmationACK = 0x2D,   // Device ACKs discovery confirmation
    Discover2ERequest = 0x2E, // 1W learning mode / pairing start
    Discover2EResponse = 0x2F, // addressed authenticated response; passive diagnosis only

    // Key exchange
    // 1W controller-key enrollment / serial transfer.
    // Declared payload: serial/wrappedControllerKey[16] + manufacturer +
    // 0x01 + sequence[2]. The normal 29-byte frame has no in-frame 1W HMAC.
    // Some reference profiles append an optional six-byte trailer MAC outside
    // the CTRL0-declared length; it is not the normal HMAC used by authenticated
    // 1W commands such as 0x00, 0x01, 0x2E and 0x39.
    SendKey1W = 0x30,
    KeyInitTransfer = 0x31,         // 2W: ask challenge
    KeyTransfer = 0x32,             // 2W: send encrypted system key
    KeyTransferConfirmation = 0x33, // Device confirms key storage
                                    // Unknown34 = 0x34,  // not used — observed in rspaargaren scan list only

    // Node/system verification. The 0x37 response becomes the authenticated
    // transcript for the following 0x3C -> 0x3D challenge exchange.
    NodeVerifyRequest = 0x36,
    NodeVerifyResponse = 0x37,

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
    // Observed on real 2W START frames (CTRL0=0x50) with an 8-byte payload.
    // CyrilOpenSource/iown-homecontrol-esp32sx1276 lists it among the valid 2W
    // opcodes, but no public source names it or decodes its payload. Keep the
    // neutral name: do not invent semantics.
    Unknown86 = 0x86,
    // Unknown88 = 0x88,  // not used
    // Unknown8A = 0x8A,  // not used
    // Unknown8B = 0x8B,  // not used
    // Unknown8E = 0x8E,  // not used
    // Unknown90 = 0x90,  // not used
    // Unknown92 = 0x92,  // not used
    // Unknown94 = 0x94,  // not used
    // Unknown96 = 0x96,  // not used
    // Unknown98 = 0x98,  // not used

    // Observed high command range. Public captures do not yet establish
    // stable semantics, so keep neutral names and no active handlers.
    ObservedF0 = 0xF0,
    ObservedF1 = 0xF1,
    ObservedF2 = 0xF2,
    ObservedF3 = 0xF3,

    // Error
    ErrorResponse = 0xFE
};

// Result of a queued 2W command exchange as observed by the controller.  This
// is intentionally separate from protocol response codes: channels use it to
// decide whether optimistic local state may be committed or must be restored.
enum class IoHomeCommandExchangeResult : uint8_t
{
    Completed = 0,
    // Reserved for a local build/radio failure. A peer timeout is Unknown,
    // because silence does not prove that a physical command was ignored.
    FailedBeforeAuthentication = 1,
    AuthenticatedUnconfirmed = 2,
    Unknown = 3,
    ExplicitlyRejected = 4,
};

// Experimental low-power wake estimate.  It is intentionally a runtime-only
// diagnostic until a 2W actuator confirms the policy on real hardware.
enum class TwoWayWakeBelief : uint8_t
{
    Asleep = 0,
    MaybeAwake = 1,
    Awake = 2,
};

constexpr uint32_t IOHC_2W_MOVING_EVIDENCE_MS = 120000UL;
constexpr uint32_t IOHC_2W_RECENTLY_HEARD_MS = 30000UL;

inline TwoWayWakeBelief twoWayWakeBelief(bool iHasMovingEvidence,
                                         uint32_t iLastMovingEvidenceMs,
                                         bool iHasHeardEvidence,
                                         uint32_t iLastHeardMs,
                                         uint32_t iNowMs,
                                         bool iStopCommand = false)
{
    if (iStopCommand ||
        (iHasMovingEvidence && static_cast<uint32_t>(iNowMs - iLastMovingEvidenceMs) <= IOHC_2W_MOVING_EVIDENCE_MS))
        return TwoWayWakeBelief::Awake;
    if (iHasHeardEvidence && static_cast<uint32_t>(iNowMs - iLastHeardMs) <= IOHC_2W_RECENTLY_HEARD_MS)
        return TwoWayWakeBelief::MaybeAwake;
    return TwoWayWakeBelief::Asleep;
}

inline uint16_t twoWayWakePreamble(TwoWayWakeBelief iBelief, uint8_t iAttemptIndex,
                                   uint16_t iNormalStartPreamble)
{
    const uint8_t lAttempt = iAttemptIndex > 2 ? 2 : iAttemptIndex;
    if (iBelief == TwoWayWakeBelief::Awake)
        return lAttempt == 1 ? 1024 : iNormalStartPreamble; // normal, long, normal
    if (iBelief == TwoWayWakeBelief::MaybeAwake)
        return lAttempt == 0 ? iNormalStartPreamble : 1024; // normal, long, long
    return 1024;                          // asleep: always wake first
}

inline const char *twoWayWakeBeliefName(TwoWayWakeBelief iBelief)
{
    switch (iBelief)
    {
    case TwoWayWakeBelief::Awake: return "awake";
    case TwoWayWakeBelief::MaybeAwake: return "maybe-awake";
    default: return "asleep";
    }
}

// Discovery-family wire policy.  These settings deliberately keep command,
// destination, CTRL1 flags and preamble independent: hardware captures show
// different CTRL1 combinations for 0x28, 0x2E and 0x2A, while the wake-up
// preamble is a separate receiver/power-class decision.
enum class TwoWayDiscoveryCommandMode : uint8_t
{
    Automatic = 0,
    Discover28 = 1,
    Discover2E = 2,
    DiscoverSPE = 3,
};

enum class TwoWayDiscoveryDestinationMode : uint8_t
{
    Automatic = 0,
    DiscoverAll = 1, // 0x00003B
    DiscoverAlt = 2, // 0x00003F
    LightingDiscoverAll = 3, // 0x0001BB
    LightingDiscoverAlt = 4, // 0x0001BF
};

enum class TwoWayDiscoveryListenChannels : uint8_t
{
    Automatic = 0,
    SkipRequest = 1,
    All = 2,
};

enum class TwoWayDiscoveryFlagMode : uint8_t
{
    Automatic = 0,
    Off = 1,
    On = 2,
};

enum class TwoWayDiscoveryPreambleMode : uint8_t
{
    Automatic = 0,
    Long = 1,
    Normal = 2,
    Short = 3,
};

// Post-discovery handshake policy. Send is the capture-backed default and is
// deliberately tolerant: a missing 0x2D never prevents the following key-init.
enum class PairingDiscoverConfirmMode : uint8_t
{
    Skip = 0,
    Send = 1,
    SendWithAck = 2,
};

struct TwoWayDiscoverySettings
{
    TwoWayDiscoveryCommandMode command = TwoWayDiscoveryCommandMode::Automatic;
    TwoWayDiscoveryDestinationMode destination = TwoWayDiscoveryDestinationMode::Automatic;
    TwoWayDiscoveryFlagMode ack = TwoWayDiscoveryFlagMode::Automatic;
    TwoWayDiscoveryFlagMode lowPower = TwoWayDiscoveryFlagMode::Automatic;
    TwoWayDiscoveryPreambleMode preamble = TwoWayDiscoveryPreambleMode::Automatic;
    TwoWayDiscoveryListenChannels listenChannels = TwoWayDiscoveryListenChannels::Automatic;
};

struct TwoWayDiscoveryFrameOptions
{
    IoHomeCommand command = IoHomeCommand::DiscoverRequest;
    uint32_t destination = 0x00003B;
    bool lowPower = false;
    bool ackCapable = false;
    uint16_t preamble = 1024;
    TwoWayDiscoveryListenChannels listenChannels = TwoWayDiscoveryListenChannels::SkipRequest;
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

// Persistent policy for ordinary 1W Execute destinations. Automatic keeps the
// established typed-broadcast behavior; All is an explicit remote-wire-profile
// choice and is deliberately not inferred from the manufacturer.
enum class OneWayExecuteDestinationPolicy : uint8_t
{
    Automatic = 0,
    Typed = 1,
    All = 2,
};

// Enrollment-only address policy. Ordinary Execute destinations are controlled
// independently by OneWayExecuteDestinationPolicy.
enum class OneWayEnrollmentDestinationPolicy : uint8_t
{
    Automatic = 0,
    All = 1,
    Typed = 2,
};

// Per-controller-identity 1W wake-up policy. Automatic preserves the module's
// existing capture-backed behavior. AlwaysAlive uses the normal 32-symbol
// preamble for every copy. LowPower sends one 1024-symbol wake-up copy with
// CTRL1 LOW_POWER set, followed by normal copies with the flag clear.
enum class OneWayPowerClass : uint8_t
{
    Automatic = 0,
    AlwaysAlive = 1,
    LowPower = 2,
};

// Configurable VELUX enrollment class sweep. A stored value of zero means the
// captured default sweep containing all three classes.
constexpr uint8_t IOHC_1W_ENROLL_CLASS_ROLLER = 0x01;
constexpr uint8_t IOHC_1W_ENROLL_CLASS_AWNING = 0x02;
constexpr uint8_t IOHC_1W_ENROLL_CLASS_DUAL = 0x04;
constexpr uint8_t IOHC_1W_ENROLL_CLASS_ALL =
    IOHC_1W_ENROLL_CLASS_ROLLER | IOHC_1W_ENROLL_CLASS_AWNING | IOHC_1W_ENROLL_CLASS_DUAL;
constexpr uint8_t IOHC_1W_ENROLL_CLASS_INTERIOR = 0x08; // KLI 312: Blind + VenetianBlind

enum class PrivateProbeShape : uint8_t
{
    Function = 0,       // <function> 00 00
    FunctionSubIndex,   // <function> <sub-index> 00
    StatusExtended,     // <function> 80 <block> 00
};

enum class TwoWayPowerClass : uint8_t
{
    Automatic = 0,
    AlwaysAlive = 1,
    LowPower = 2,
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
    Screen = 0x0B,
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
    AtlanticGroup = 0x0C,
    ZehnderGroup = 0x0D,
    Unresolved0E = 0x0E,
    Unresolved0F = 0x0F
};

inline const char *ioHomeManufacturerName(uint8_t iManufacturer)
{
    switch (static_cast<IoHomeManufacturer>(iManufacturer))
    {
    case IoHomeManufacturer::Velux: return "VELUX";
    case IoHomeManufacturer::Somfy: return "Somfy";
    case IoHomeManufacturer::Honeywell: return "Honeywell";
    case IoHomeManufacturer::Hormann: return "Hoermann";
    case IoHomeManufacturer::AssaAbloy: return "ASSA ABLOY";
    case IoHomeManufacturer::Niko: return "Niko";
    case IoHomeManufacturer::WindowMaster: return "WINDOW MASTER";
    case IoHomeManufacturer::Renson: return "Renson";
    case IoHomeManufacturer::Ciat: return "CIAT";
    case IoHomeManufacturer::Secuyou: return "Secuyou";
    case IoHomeManufacturer::Overkiz: return "OVERKIZ";
    case IoHomeManufacturer::AtlanticGroup: return "Atlantic Group";
    case IoHomeManufacturer::ZehnderGroup: return "Zehnder Group";
    case IoHomeManufacturer::Unresolved0E: return "Unresolved";
    case IoHomeManufacturer::Unresolved0F: return "Unresolved";
    default: return "Unknown";
    }
}

// Check if device type only supports open/close (no continuous positioning)
inline bool isOpenCloseOnly(IoHomeDeviceType iType)
{
    return iType == IoHomeDeviceType::Lock ||
           iType == IoHomeDeviceType::OnOffSwitch ||
           iType == IoHomeDeviceType::IntrusionAlarm ||
           iType == IoHomeDeviceType::SwingingShutter;
}

// KLF special parameter values. Ignore is an outgoing FP placeholder;
// no-feedback is an incoming status sentinel, not a position.
#define IOHC_PARAMETER_TARGET 0xD100
#define IOHC_PARAMETER_CURRENT 0xD200
#define IOHC_PARAMETER_DEFAULT 0xD300
#define IOHC_PARAMETER_IGNORE 0xD400
#define IOHC_NO_FEEDBACK_VALUE 0xF7FF
#define IOHC_POSITION_STOP IOHC_PARAMETER_CURRENT

// FPI1 selects FP1..FP8 and FPI2 selects FP9..FP16. This is a representation
// helper only; it does not authorize a transmission to a discovered device.
struct IoHomeFpSelection
{
    uint8_t fpi1 = 0;
    uint8_t fpi2 = 0;
};

inline IoHomeFpSelection ioHomeFpSelection(uint8_t iFpIndex)
{
    IoHomeFpSelection lSelection;
    if (iFpIndex >= 1 && iFpIndex <= 8)
        lSelection.fpi1 = static_cast<uint8_t>(0x80U >> (iFpIndex - 1));
    else if (iFpIndex >= 9 && iFpIndex <= 16)
        lSelection.fpi2 = static_cast<uint8_t>(0x80U >> (iFpIndex - 9));
    return lSelection;
}

// OVPd refreshes no more than three selected FPs in one command. Reject
// duplicate or invalid indices; values are serialized in ascending FP order.
inline bool ioHomeFpSelectIndices(const uint8_t *iIndices, uint8_t iCount,
                                  IoHomeFpSelection &oSelection)
{
    oSelection = {};
    if (!iIndices || iCount == 0 || iCount > 3)
        return false;
    for (uint8_t i = 0; i < iCount; ++i)
    {
        const IoHomeFpSelection lOne = ioHomeFpSelection(iIndices[i]);
        if ((lOne.fpi1 == 0 && lOne.fpi2 == 0) ||
            (oSelection.fpi1 & lOne.fpi1) != 0 ||
            (oSelection.fpi2 & lOne.fpi2) != 0)
        {
            oSelection = {};
            return false;
        }
        oSelection.fpi1 |= lOne.fpi1;
        oSelection.fpi2 |= lOne.fpi2;
    }
    return true;
}

// OVPd refresh-command representation, not an authenticated native RF
// Private/Execute payload. Per-parameter extended-information defaults to 1
// (current relative value); callers must supply a confirmed discrete code.
inline bool ioHomeBuildFpRefreshRepresentation(const uint8_t *iIndices,
                                                const uint8_t *iExtendedInfo,
                                                uint8_t iCount, uint8_t *oData,
                                                uint8_t &oLen)
{
    oLen = 0;
    IoHomeFpSelection lSelection;
    if (!oData || !iExtendedInfo ||
        !ioHomeFpSelectIndices(iIndices, iCount, lSelection))
        return false;
    oData[oLen++] = lSelection.fpi1;
    for (uint8_t lIndex = 1; lIndex <= 8; ++lIndex)
        for (uint8_t i = 0; i < iCount; ++i)
            if (iIndices[i] == lIndex)
                oData[oLen++] = iExtendedInfo[i];
    oData[oLen++] = lSelection.fpi2;
    for (uint8_t lIndex = 9; lIndex <= 16; ++lIndex)
        for (uint8_t i = 0; i < iCount; ++i)
            if (iIndices[i] == lIndex)
                oData[oLen++] = iExtendedInfo[i];
    return true;
}

// Captured single-FP 2W Execute shape; higher FP/FPI2 transmit layout is not
// capture-confirmed. No semantic conversion is applied to iRaw.
inline bool ioHomeBuildDiagnosticFpRawPayload(uint8_t iFpIndex, uint16_t iRaw,
                                               uint8_t *oData)
{
    if (!oData || iFpIndex < 1 || iFpIndex > 3)
        return false;
    oData[0] = 0x01; // user originator (IOHC_ORIGINATOR_USER)
    oData[1] = 0xE7;
    oData[2] = 0xD4;
    oData[3] = 0x00;
    oData[4] = ioHomeFpSelection(iFpIndex).fpi1;
    oData[5] = static_cast<uint8_t>(iRaw >> 8);
    oData[6] = static_cast<uint8_t>(iRaw & 0xFF);
    oData[7] = 0x00;
    return true;
}
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
#define IOHC_ACEI_1W 0x43       // Somfy/default 1W remote profile
#define IOHC_ACEI_1W_VELUX 0x61 // VELUX KLI 1W remote profile

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
#define IOHC_STATUS_UPDATE_ORIGINATOR_OFFSET 14

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

inline void encodePackedProfile(uint16_t iProfile, uint8_t iSubProfile,
                                uint8_t &oProfileMsb, uint8_t &oProfileSub)
{
    oProfileMsb = static_cast<uint8_t>((iProfile >> 2) & 0xFF);
    oProfileSub = static_cast<uint8_t>(((iProfile & 0x03) << 6) | (iSubProfile & 0x3F));
}

inline uint16_t decodePackedProfile(uint8_t iProfileMsb, uint8_t iProfileSub)
{
    return (static_cast<uint16_t>(iProfileMsb) << 2) |
           (static_cast<uint16_t>(iProfileSub) >> 6);
}

inline uint8_t decodePackedSubProfile(uint8_t iProfileSub)
{
    return static_cast<uint8_t>(iProfileSub & 0x3F);
}

inline uint16_t encodeNodeTypeSubType(uint16_t iProfile, uint8_t iSubProfile)
{
    return static_cast<uint16_t>(((iProfile & 0x03FF) << 6) | (iSubProfile & 0x3F));
}

inline void decodeNodeTypeSubType(uint16_t iNodeTypeSubType,
                                  uint16_t &oProfile, uint8_t &oSubProfile)
{
    oProfile = static_cast<uint16_t>((iNodeTypeSubType >> 6) & 0x03FF);
    oSubProfile = static_cast<uint8_t>(iNodeTypeSubType & 0x3F);
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
#define IOHC_KEY_EXTRACT_COLD_REPLY_PREAMBLE 80 // cold 0x29 reply on each discovery channel
#define IOHC_RESPONSE_PREAMBLE_SX1262 8
#define IOHC_RESPONSE_PREAMBLE_SX1276 12
#define IOHC_NAME_MAX_SIZE 16   // max name payload bytes (per nicolas5000: CMD_PARAM_NAME_MAXSIZE/2)

// DiscoverResponse / DiscoverSPEResponse payload layout. The RF frame source
// is the node's ioAddress. It is independent from data[2..4], which carries
// ioBackboneAddress and may validly be 0x000000. Bytes 0..1 contain the packed
// 10-bit profile + 6-bit subProfile, data[5] manufacturerId, data[6] the raw
// multiInfoByte, and data[7..8] the discovery timestamp.
#define IOHC_DISCOVERY_METADATA_SIZE 2
#define IOHC_DISCOVERY_IO_BACKBONE_OFFSET 2
#define IOHC_DISCOVERY_MANUFACTURER_OFFSET 5
#define IOHC_DISCOVERY_FLAGS_OFFSET 6
#define IOHC_DISCOVERY_EXTENDED_SIZE (IOHC_DISCOVERY_FLAGS_OFFSET + 1)
#define IOHC_DISCOVERY_TIMESTAMP_OFFSET 7
#define IOHC_DISCOVERY_FULL_SIZE 9
#define IOHC_DISCOVERY_RAW_MAX_SIZE 23
#define IOHC_DISCOVERY_POWER_SAVE_MASK 0x03
#define IOHC_DISCOVERY_IO_MEMBER_MASK 0x04
#define IOHC_DISCOVERY_RF_SUPPORT_MASK 0x08
#define IOHC_DISCOVERY_UNKNOWN_BIT4_MASK 0x10
#define IOHC_DISCOVERY_SYNC_CONTROL_GROUP_MASK 0x20
#define IOHC_DISCOVERY_RESPONSE_TIME_CLASS_MASK 0xC0
#define IOHC_DISCOVERY_RESPONSE_TIME_CLASS_SHIFT 6
#define IOHC_POWER_SAVE_ALWAYS_ALIVE 0x00
#define IOHC_POWER_SAVE_LOW_POWER 0x01

enum class IoHomePowerMode : uint8_t
{
    AlwaysAlive = 0,
    LowPower = 1,
    Unknown = 0xFF
};

enum class IoHomeKeyState : uint8_t
{
    None = 0,
    Old = 1,
    Current = 2,
    Unknown = 0xFF
};

// Node class is an independent protocol dimension. It must never be inferred
// from Profile/SubProfile: public gateway implementations carry both values
// separately, and the same profile number can occur in different classes.
enum class IoHomeNodeClass : uint8_t
{
    Unknown = 0,
    Actuator = 1,
    Sensor = 2,
    Controller = 3,
    Stack = 4,
    Beacon = 5
};

enum class IoHomeGeneralInfo3Outcome : uint8_t
{
    NotQueried = 0,
    Requested = 1,
    Response = 2,
    ErrorResponse = 3,
    Timeout = 4,
    TransportFailure = 5
};

enum class IoHomeMetadataSource : uint8_t
{
    Unknown = 0,
    DiscoverResponse = 1,
    DiscoverSpeResponse = 2,
    Restored = 3
};

inline uint8_t ioHomeMetadataSourcePriority(IoHomeMetadataSource iSource)
{
    switch (iSource)
    {
    case IoHomeMetadataSource::DiscoverResponse:
    case IoHomeMetadataSource::DiscoverSpeResponse: return 2;
    case IoHomeMetadataSource::Restored: return 1;
    default: return 0;
    }
}

inline const char *ioHomeKeyStateName(IoHomeKeyState iState)
{
    switch (iState)
    {
    case IoHomeKeyState::None: return "none";
    case IoHomeKeyState::Old: return "old";
    case IoHomeKeyState::Current: return "current";
    default: return "n/a";
    }
}

inline const char *ioHomeMetadataSourceName(IoHomeMetadataSource iSource)
{
    switch (iSource)
    {
    case IoHomeMetadataSource::DiscoverResponse: return "0x29";
    case IoHomeMetadataSource::DiscoverSpeResponse: return "0x2B";
    case IoHomeMetadataSource::Restored: return "flash";
    default: return "unknown";
    }
}

inline const char *ioHomePowerModeName(IoHomePowerMode iMode)
{
    switch (iMode)
    {
    case IoHomePowerMode::AlwaysAlive: return "always-alive";
    case IoHomePowerMode::LowPower: return "low-power";
    default: return "unknown";
    }
}

inline const char *ioHomeNodeClassName(IoHomeNodeClass iClass)
{
    switch (iClass)
    {
    case IoHomeNodeClass::Actuator: return "actuator";
    case IoHomeNodeClass::Sensor: return "sensor";
    case IoHomeNodeClass::Controller: return "controller";
    case IoHomeNodeClass::Stack: return "stack";
    case IoHomeNodeClass::Beacon: return "beacon";
    default: return "unknown";
    }
}

inline IoHomeNodeClass decodeIoHomeNodeClass(uint8_t iValue)
{
    return iValue <= static_cast<uint8_t>(IoHomeNodeClass::Beacon)
               ? static_cast<IoHomeNodeClass>(iValue)
               : IoHomeNodeClass::Unknown;
}

inline const char *ioHomeGeneralInfo3OutcomeName(IoHomeGeneralInfo3Outcome iOutcome)
{
    switch (iOutcome)
    {
    case IoHomeGeneralInfo3Outcome::Requested: return "requested";
    case IoHomeGeneralInfo3Outcome::Response: return "response";
    case IoHomeGeneralInfo3Outcome::ErrorResponse: return "error-response";
    case IoHomeGeneralInfo3Outcome::Timeout: return "timeout";
    case IoHomeGeneralInfo3Outcome::TransportFailure: return "transport-failure";
    default: return "not-queried";
    }
}

inline uint8_t ioHomeKlfTurnaroundHintMs(uint8_t iClass)
{
    static constexpr uint8_t kValues[] = {5, 10, 20, 40};
    return kValues[iClass & 0x03];
}

// Layer 1: RF protocol identity using the canonical Somfy vocabulary.
struct IoHomeProtocolIdentity
{
    bool valid = false;
    bool fullMetadata = false;
    uint32_t ioAddress = 0;
    uint16_t profile = 0;
    uint8_t subProfile = 0;
    uint16_t nodeTypeSubType = 0;
    IoHomeNodeClass nodeClass = IoHomeNodeClass::Unknown;
    bool hasIoBackboneAddress = false;
    uint32_t ioBackboneAddress = 0;
    uint8_t manufacturerId = 0;
    bool hasMib = false;
    uint8_t multiInfoByte = 0;
    uint8_t powerSaveModeRaw = 0xFF;
    IoHomePowerMode powerSaveMode = IoHomePowerMode::Unknown;
    bool ioMembershipFlag = false;
    bool rfSupportInNode = false;
    // Bit 4 deliberately has no decoded field or behavior. Inspect it only in
    // multiInfoByte diagnostics until its meaning is confirmed.
    // Provisional: Velocet docs/commands.md calls MIB bit 5 "SyncCtrlGrp".
    // KLF v3.18 does not define it and OVPd retains the MIB without decoding
    // it. Do not use this candidate for production behavior without captures.
    bool syncControlGroupCandidate = false;
    // 0..3 only when a MIB was present. 0xFF keeps an absent MIB distinct from
    // valid class 0. KLF calls 5/10/20/40 milliseconds, while Velocet
    // docs/commands.md calls the same values seconds. The raw class is the
    // authority; neither source interpretation controls RF timeouts.
    uint8_t responseTimeClass = 0xFF;
    uint8_t klfTurnaroundHintMs = 0;
    bool responseTimeUnitConfirmed = false;
    IoHomeKeyState keyState = IoHomeKeyState::Unknown;
    IoHomeMetadataSource metadataSource = IoHomeMetadataSource::Unknown;
    IoHomeMetadataSource keyStateSource = IoHomeMetadataSource::Unknown;
    bool hasDiscoveryTimestamp = false;
    uint16_t discoveryTimestamp = 0;
    uint8_t rawData[IOHC_DISCOVERY_RAW_MAX_SIZE] = {};
    uint8_t rawDataLen = 0;
};

inline bool ioHomeShouldAcceptProtocolIdentity(
    const IoHomeProtocolIdentity &iCurrent,
    const IoHomeProtocolIdentity &iIncoming)
{
    if (!iIncoming.valid)
        return false;
    if (!iCurrent.valid || iCurrent.ioAddress != iIncoming.ioAddress)
        return true;
    const uint8_t lCurrentPriority =
        ioHomeMetadataSourcePriority(iCurrent.metadataSource);
    const uint8_t lIncomingPriority =
        ioHomeMetadataSourcePriority(iIncoming.metadataSource);
    if (lIncomingPriority < lCurrentPriority)
        return false;
    return lIncomingPriority != lCurrentPriority ||
           !iCurrent.fullMetadata || iIncoming.fullMetadata;
}

inline bool ioHomeDiscoveryRecordChanged(
    const IoHomeProtocolIdentity *iPrevious,
    const IoHomeProtocolIdentity &iCurrent)
{
    return !iPrevious || !iPrevious->valid ||
           iPrevious->rawDataLen != iCurrent.rawDataLen ||
           memcmp(iPrevious->rawData, iCurrent.rawData, iCurrent.rawDataLen) != 0;
}

inline bool ioHomeKeyStateKnown(const IoHomeProtocolIdentity &iIdentity)
{
    return iIdentity.keyState != IoHomeKeyState::Unknown &&
           iIdentity.keyStateSource != IoHomeMetadataSource::Unknown;
}

// Layer 2 is deliberately separate from protocol identity. It can evolve from
// confirmed command/response behavior without changing the hardware identity.
struct IoHomeGenericCapabilities
{
    bool position = false;
    bool velocity = false;
    bool tilt = false;
    bool tiltVelocity = false;
    bool light = false;
    bool lock = false;
    bool onOff = false;
    bool ventilation = false;
    bool heating = false;
    bool dualCurtain = false;
};

// Device-information responses use the normal 2W payload ceiling. Keep this
// local to the command model because IoHomeFrame.h includes this header before
// declaring IOHC_FRAME_MAX_DATA.
static constexpr uint8_t IOHC_DEVICE_INFO_RAW_MAX_SIZE = 23;
static constexpr uint8_t IOHC_PRODUCT_FAMILY_LABEL_SIZE = 48;
static constexpr uint16_t IOHC_ENRICHED_FLASH_SIZE =
    2 + 3 * (1 + IOHC_DEVICE_INFO_RAW_MAX_SIZE) + 2 +
    IOHC_PRODUCT_FAMILY_LABEL_SIZE + 1;

inline uint32_t ioHomeMetadataRefreshStepIntervalMs(bool iLowPower)
{
    return iLowPower ? 10000UL : 3000UL;
}

inline uint32_t ioHomeMetadataRefreshCooldownMs(bool iLowPower)
{
    return iLowPower ? 60000UL : 10000UL;
}

enum class IoHomeIdentificationConfidence : uint8_t
{
    Unknown,
    GenericProfile,
    VendorFamilyWildcard,
    VendorFamilyExact,
};

// Layer 3: optional vendor/commercial identification evidence. Basic control
// must never depend on these fields or on a successful product match.
struct IoHomeProductIdentityEvidence
{
    // Vendor/commercial classification only; zero means unknown or unmatched.
    // This must never select generic protocol behavior or replace profile.
    uint16_t manufacturerSubType = 0;
    char productFamilyLabel[IOHC_PRODUCT_FAMILY_LABEL_SIZE] = {};
    uint16_t productQuirkFlags = 0;
    bool manufacturerSignatureInconsistent = false;
    uint8_t signatureManufacturerId = 0;
    IoHomeIdentificationConfidence identificationConfidence =
        IoHomeIdentificationConfidence::Unknown;
    uint8_t nameResponse[IOHC_DEVICE_INFO_RAW_MAX_SIZE] = {};
    uint8_t nameResponseLen = 0;
    uint8_t generalInfo1[IOHC_DEVICE_INFO_RAW_MAX_SIZE] = {};
    uint8_t generalInfo1Len = 0;
    uint8_t generalInfo2[IOHC_DEVICE_INFO_RAW_MAX_SIZE] = {};
    uint8_t generalInfo2Len = 0;
    uint8_t generalInfo3[IOHC_DEVICE_INFO_RAW_MAX_SIZE] = {};
    uint8_t generalInfo3Len = 0;
    IoHomeGeneralInfo3Outcome generalInfo3Outcome = IoHomeGeneralInfo3Outcome::NotQueried;
    uint8_t generalInfo3ErrorResponse[IOHC_DEVICE_INFO_RAW_MAX_SIZE] = {};
    uint8_t generalInfo3ErrorResponseLen = 0;
    bool generalInfo2TypeValid = false;
    uint16_t generalInfo2Profile = 0;
    uint8_t generalInfo2SubProfile = 0;
    bool generalInfo2MatchesDiscovery = false;
};

inline bool ioHomePersistableEnrichmentChanged(
    const IoHomeProductIdentityEvidence &iEvidence,
    IoHomeCommand iResponse, const uint8_t *iData, uint8_t iDataLen)
{
    const uint8_t *lStored = nullptr;
    uint8_t lStoredLen = 0;
    switch (iResponse)
    {
    case IoHomeCommand::GetNameResponse:
        lStored = iEvidence.nameResponse;
        lStoredLen = iEvidence.nameResponseLen;
        break;
    case IoHomeCommand::GetGeneralInfo1Response:
        lStored = iEvidence.generalInfo1;
        lStoredLen = iEvidence.generalInfo1Len;
        break;
    case IoHomeCommand::GetGeneralInfo2Response:
        lStored = iEvidence.generalInfo2;
        lStoredLen = iEvidence.generalInfo2Len;
        break;
    default:
        return false;
    }
    const uint8_t lLength = iDataLen < IOHC_DEVICE_INFO_RAW_MAX_SIZE
                                ? iDataLen : IOHC_DEVICE_INFO_RAW_MAX_SIZE;
    return lStoredLen != lLength ||
           (lLength > 0 && (!iData || memcmp(lStored, iData, lLength) != 0));
}

static constexpr uint8_t IOHC_PRODUCT_SIGNATURE_SIZE = 10;

struct IoHomeProductSignature
{
    uint8_t bytes[IOHC_PRODUCT_SIGNATURE_SIZE] = {};
    uint8_t length = 0;
};

enum class IoHomeSignatureMatchQuality : uint8_t
{
    None,
    Wildcard,
    Exact,
};

// A literal '?' in a database pattern matches exactly one byte, including a
// non-printable one. The candidate signature itself is always byte-exact.
inline IoHomeSignatureMatchQuality ioHomeMatchSignaturePattern(
    const IoHomeProductSignature &iSignature,
    const uint8_t *iPattern, uint8_t iPatternLen)
{
    if (!iPattern || iSignature.length == 0 ||
        iSignature.length != iPatternLen ||
        iPatternLen > IOHC_PRODUCT_SIGNATURE_SIZE)
        return IoHomeSignatureMatchQuality::None;
    bool lWildcard = false;
    for (uint8_t i = 0; i < iPatternLen; ++i)
    {
        if (iPattern[i] == 0x3F)
            lWildcard = true;
        else if (iPattern[i] != iSignature.bytes[i])
            return IoHomeSignatureMatchQuality::None;
    }
    return lWildcard ? IoHomeSignatureMatchQuality::Wildcard
                     : IoHomeSignatureMatchQuality::Exact;
}

inline IoHomeProductSignature ioHomeGeneralInfo1ProductSignature(
    const IoHomeProductIdentityEvidence &iEvidence)
{
    IoHomeProductSignature lSignature;
    lSignature.length = iEvidence.generalInfo1Len < IOHC_PRODUCT_SIGNATURE_SIZE
                            ? iEvidence.generalInfo1Len
                            : IOHC_PRODUCT_SIGNATURE_SIZE;
    if (lSignature.length > 0)
        memcpy(lSignature.bytes, iEvidence.generalInfo1, lSignature.length);
    return lSignature;
}

inline bool ioHomeUsesGeneralInfo2ProductSignature(
    const IoHomeProtocolIdentity &iIdentity)
{
    return iIdentity.valid && iIdentity.manufacturerId == 12 &&
           (iIdentity.profile == 22 || iIdentity.profile == 52) &&
           iIdentity.subProfile == 1;
}

inline IoHomeProductSignature ioHomeProductSignature(
    const IoHomeProtocolIdentity &iIdentity,
    const IoHomeProductIdentityEvidence &iEvidence)
{
    if (!ioHomeUsesGeneralInfo2ProductSignature(iIdentity))
        return ioHomeGeneralInfo1ProductSignature(iEvidence);
    IoHomeProductSignature lSignature;
    lSignature.length = iEvidence.generalInfo2Len < IOHC_PRODUCT_SIGNATURE_SIZE
                            ? iEvidence.generalInfo2Len : IOHC_PRODUCT_SIGNATURE_SIZE;
    if (lSignature.length > 0)
        memcpy(lSignature.bytes, iEvidence.generalInfo2, lSignature.length);
    return lSignature;
}

inline std::string ioHomeProductSignatureHex(const IoHomeProductSignature &iSignature)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string lResult;
    for (uint8_t i = 0; i < iSignature.length && i < IOHC_PRODUCT_SIGNATURE_SIZE; ++i)
    {
        lResult.push_back(kDigits[iSignature.bytes[i] >> 4]);
        lResult.push_back(kDigits[iSignature.bytes[i] & 0x0F]);
    }
    return lResult;
}

inline std::string ioHomeProductSignaturePrintable(const IoHomeProductSignature &iSignature)
{
    std::string lResult;
    for (uint8_t i = 0; i < iSignature.length && i < IOHC_PRODUCT_SIGNATURE_SIZE; ++i)
        lResult.push_back(iSignature.bytes[i] >= 0x20 && iSignature.bytes[i] <= 0x7E
                              ? static_cast<char>(iSignature.bytes[i]) : '.');
    return lResult;
}

struct IoHomeVendorSignatureEntry
{
    IoHomeNodeClass nodeClass;
    uint16_t profile;
    uint8_t subProfile;
    uint8_t manufacturerId;
    uint8_t signaturePattern[IOHC_PRODUCT_SIGNATURE_SIZE];
    uint16_t manufacturerSubType;
    const char *productFamilyLabel;
    uint16_t optionalQuirkFlags;
};

struct IoHomeVendorProductMatch
{
    IoHomeSignatureMatchQuality quality = IoHomeSignatureMatchQuality::None;
    uint16_t manufacturerSubType = 0;
    const char *productFamilyLabel = nullptr;
    uint16_t optionalQuirkFlags = 0;
    bool manufacturerInconsistent = false;
    uint8_t signatureManufacturerId = 0;
};

inline const char *ioHomeIdentificationConfidenceName(
    IoHomeIdentificationConfidence iConfidence)
{
    switch (iConfidence)
    {
    case IoHomeIdentificationConfidence::GenericProfile: return "GenericProfile";
    case IoHomeIdentificationConfidence::VendorFamilyWildcard: return "VendorFamilyWildcard";
    case IoHomeIdentificationConfidence::VendorFamilyExact: return "VendorFamilyExact";
    default: return "Unknown";
    }
}

inline IoHomeIdentificationConfidence ioHomeIdentificationConfidence(
    const IoHomeProtocolIdentity &iIdentity,
    const IoHomeVendorProductMatch &iMatch)
{
    if (!iIdentity.valid)
        return IoHomeIdentificationConfidence::Unknown;
    if (iMatch.quality == IoHomeSignatureMatchQuality::Exact)
        return IoHomeIdentificationConfidence::VendorFamilyExact;
    if (iMatch.quality == IoHomeSignatureMatchQuality::Wildcard)
        return IoHomeIdentificationConfidence::VendorFamilyWildcard;
    return IoHomeIdentificationConfidence::GenericProfile;
}

inline IoHomeVendorProductMatch ioHomeLookupVendorProduct(
    const IoHomeProtocolIdentity &iIdentity,
    const IoHomeProductIdentityEvidence &iEvidence,
    const IoHomeVendorSignatureEntry *iEntries, size_t iEntryCount)
{
    IoHomeVendorProductMatch lBest;
    if (!iIdentity.valid || !iEntries)
        return lBest;
    const IoHomeProductSignature lSignature = ioHomeProductSignature(iIdentity, iEvidence);
    if (lSignature.length != IOHC_PRODUCT_SIGNATURE_SIZE)
        return lBest;
    uint8_t lBestSpecificity = 0;
    for (size_t i = 0; i < iEntryCount; ++i)
    {
        const IoHomeVendorSignatureEntry &lEntry = iEntries[i];
        if ((lEntry.nodeClass != IoHomeNodeClass::Unknown &&
             lEntry.nodeClass != iIdentity.nodeClass) ||
            (lEntry.profile != 0xFFFF && lEntry.profile != iIdentity.profile) ||
            (lEntry.subProfile != 0xFF && lEntry.subProfile != iIdentity.subProfile) ||
            lEntry.manufacturerSubType == 0 || !lEntry.productFamilyLabel)
            continue;
        const IoHomeSignatureMatchQuality lQuality = ioHomeMatchSignaturePattern(
            lSignature, lEntry.signaturePattern, IOHC_PRODUCT_SIGNATURE_SIZE);
        if (lQuality == IoHomeSignatureMatchQuality::None)
            continue;
        if (lEntry.manufacturerId != iIdentity.manufacturerId)
        {
            // A signature alone cannot change the discovery manufacturer.
            // Keep the strongest foreign match only as inconsistency evidence.
            lBest.manufacturerInconsistent = true;
            lBest.signatureManufacturerId = lEntry.manufacturerId;
            continue;
        }
        uint8_t lSpecificity = 0;
        for (uint8_t j = 0; j < IOHC_PRODUCT_SIGNATURE_SIZE; ++j)
            lSpecificity += lEntry.signaturePattern[j] != 0x3F;
        if (static_cast<uint8_t>(lQuality) < static_cast<uint8_t>(lBest.quality) ||
            (lQuality == lBest.quality && lSpecificity <= lBestSpecificity))
            continue;
        lBest.quality = lQuality;
        lBest.manufacturerSubType = lEntry.manufacturerSubType;
        lBest.productFamilyLabel = lEntry.productFamilyLabel;
        lBest.optionalQuirkFlags = lEntry.optionalQuirkFlags;
        lBestSpecificity = lSpecificity;
    }
    return lBest;
}

inline IoHomeVendorProductMatch ioHomeLookupVendorProduct(
    const IoHomeProtocolIdentity &iIdentity,
    const IoHomeProductIdentityEvidence &iEvidence)
{
    // Provisional local ID 1: "5163340C06" is the Somfy-labelled GI1 sample
    // in test_protocol.cpp, but that test combines it with an unrelated VELUX
    // discovery fixture. Its node class, profile/subProfile and commercial
    // model have NOT been capture-confirmed. Wildcard selectors avoid inventing
    // those fields; the label explicitly avoids claiming an exact product.
    // Do not attach quirks or generic protocol behavior to this provisional ID.
    static const IoHomeVendorSignatureEntry kEntries[] = {
        {IoHomeNodeClass::Unknown, 0xFFFF, 0xFF,
         static_cast<uint8_t>(IoHomeManufacturer::Somfy),
         {'5','1','6','3','3','4','0','C','0','6'},
         1, "Somfy GI1 5163340C06 (family unverified)", 0},
    };
    return ioHomeLookupVendorProduct(iIdentity, iEvidence,
                                     kEntries, sizeof(kEntries) / sizeof(kEntries[0]));
}

inline IoHomeIdentificationConfidence ioHomeIdentificationConfidence(
    const IoHomeProtocolIdentity &iIdentity,
    const IoHomeProductIdentityEvidence &iEvidence)
{
    return ioHomeIdentificationConfidence(
        iIdentity, ioHomeLookupVendorProduct(iIdentity, iEvidence));
}

inline void ioHomeUpdateVendorProductEvidence(
    const IoHomeProtocolIdentity &iIdentity,
    IoHomeProductIdentityEvidence &ioEvidence)
{
    const IoHomeVendorProductMatch lMatch =
        ioHomeLookupVendorProduct(iIdentity, ioEvidence);
    ioEvidence.manufacturerSubType = lMatch.manufacturerSubType;
    memset(ioEvidence.productFamilyLabel, 0, sizeof(ioEvidence.productFamilyLabel));
    if (lMatch.productFamilyLabel)
    {
        const size_t lLength = strlen(lMatch.productFamilyLabel);
        memcpy(ioEvidence.productFamilyLabel, lMatch.productFamilyLabel,
               lLength < sizeof(ioEvidence.productFamilyLabel)
                   ? lLength : sizeof(ioEvidence.productFamilyLabel) - 1);
    }
    ioEvidence.productQuirkFlags = lMatch.optionalQuirkFlags;
    ioEvidence.manufacturerSignatureInconsistent =
        lMatch.quality == IoHomeSignatureMatchQuality::None &&
        lMatch.manufacturerInconsistent;
    ioEvidence.signatureManufacturerId = ioEvidence.manufacturerSignatureInconsistent
                                              ? lMatch.signatureManufacturerId : 0;
    ioEvidence.identificationConfidence =
        ioHomeIdentificationConfidence(iIdentity, lMatch);
}

inline void decodeProtocolIdentityMib(IoHomeProtocolIdentity &ioIdentity,
                                     uint8_t iMib)
{
    ioIdentity.hasMib = true;
    ioIdentity.multiInfoByte = iMib;

    const uint8_t lPowerSave = iMib & IOHC_DISCOVERY_POWER_SAVE_MASK;
    ioIdentity.powerSaveModeRaw = lPowerSave;
    if (lPowerSave == IOHC_POWER_SAVE_ALWAYS_ALIVE)
    {
        ioIdentity.powerSaveMode = IoHomePowerMode::AlwaysAlive;
    }
    else if (lPowerSave == IOHC_POWER_SAVE_LOW_POWER)
    {
        ioIdentity.powerSaveMode = IoHomePowerMode::LowPower;
    }
    else
    {
        ioIdentity.powerSaveMode = IoHomePowerMode::Unknown;
    }

    ioIdentity.ioMembershipFlag = (iMib & IOHC_DISCOVERY_IO_MEMBER_MASK) != 0;
    ioIdentity.rfSupportInNode = (iMib & IOHC_DISCOVERY_RF_SUPPORT_MASK) != 0;
    ioIdentity.syncControlGroupCandidate =
        (iMib & IOHC_DISCOVERY_SYNC_CONTROL_GROUP_MASK) != 0;
    ioIdentity.responseTimeClass = static_cast<uint8_t>(
        (iMib & IOHC_DISCOVERY_RESPONSE_TIME_CLASS_MASK) >> IOHC_DISCOVERY_RESPONSE_TIME_CLASS_SHIFT);
    ioIdentity.klfTurnaroundHintMs = ioHomeKlfTurnaroundHintMs(ioIdentity.responseTimeClass);
    // The KLF millisecond interpretation is not capture-confirmed for native RF.
    ioIdentity.responseTimeUnitConfirmed = false;
}

inline IoHomeProtocolIdentity decodeProtocolIdentity(const uint8_t *iData, uint8_t iDataLen)
{
    // Native 0x29/0x2B discovery identity layout is OVPd-confirmed by
    // Node/Class/Abstract.lua and capture-confirmed: NodeType(2),
    // BackboneAddress(3), ManufacturerId(1), MultiInfoByte(1), TimeStamp(2).
    // This does not confirm every interpretation of individual MIB bits.
    IoHomeProtocolIdentity lResult;
    if (!iData) return lResult;
    lResult.rawDataLen = iDataLen < IOHC_DISCOVERY_RAW_MAX_SIZE
                             ? iDataLen
                             : IOHC_DISCOVERY_RAW_MAX_SIZE;
    memcpy(lResult.rawData, iData, lResult.rawDataLen);
    if (iDataLen < IOHC_DISCOVERY_METADATA_SIZE) return lResult;
    lResult.valid = true;
    lResult.fullMetadata = iDataLen >= IOHC_DISCOVERY_FULL_SIZE;
    lResult.profile = decodePackedProfile(iData[0], iData[1]);
    lResult.subProfile = decodePackedSubProfile(iData[1]);
    lResult.nodeTypeSubType = encodeNodeTypeSubType(lResult.profile, lResult.subProfile);
    if (iDataLen > IOHC_DISCOVERY_IO_BACKBONE_OFFSET + 2)
    {
        lResult.hasIoBackboneAddress = true;
        lResult.ioBackboneAddress = (static_cast<uint32_t>(iData[IOHC_DISCOVERY_IO_BACKBONE_OFFSET]) << 16) |
                                    (static_cast<uint32_t>(iData[IOHC_DISCOVERY_IO_BACKBONE_OFFSET + 1]) << 8) |
                                    static_cast<uint32_t>(iData[IOHC_DISCOVERY_IO_BACKBONE_OFFSET + 2]);
    }
    if (iDataLen > IOHC_DISCOVERY_MANUFACTURER_OFFSET)
        lResult.manufacturerId = iData[IOHC_DISCOVERY_MANUFACTURER_OFFSET];
    if (iDataLen > IOHC_DISCOVERY_FLAGS_OFFSET)
        decodeProtocolIdentityMib(lResult, iData[IOHC_DISCOVERY_FLAGS_OFFSET]);
    if (iDataLen > IOHC_DISCOVERY_TIMESTAMP_OFFSET + 1)
    {
        lResult.hasDiscoveryTimestamp = true;
        lResult.discoveryTimestamp =
            (static_cast<uint16_t>(iData[IOHC_DISCOVERY_TIMESTAMP_OFFSET]) << 8) |
            static_cast<uint16_t>(iData[IOHC_DISCOVERY_TIMESTAMP_OFFSET + 1]);
    }
    return lResult;
}

inline uint8_t encodeProtocolIdentity(const IoHomeProtocolIdentity &iIdentity,
                                      uint8_t *oData, uint8_t iCapacity)
{
    if (!iIdentity.valid || !oData)
        return 0;

    uint8_t lLength = IOHC_DISCOVERY_METADATA_SIZE;
    if (iIdentity.hasIoBackboneAddress)
        lLength = IOHC_DISCOVERY_MANUFACTURER_OFFSET;
    if (iIdentity.manufacturerId != 0 || iIdentity.rawDataLen > IOHC_DISCOVERY_MANUFACTURER_OFFSET)
        lLength = IOHC_DISCOVERY_MANUFACTURER_OFFSET + 1;
    if (iIdentity.hasMib)
        lLength = IOHC_DISCOVERY_EXTENDED_SIZE;
    if (iIdentity.hasDiscoveryTimestamp || iIdentity.fullMetadata)
        lLength = IOHC_DISCOVERY_FULL_SIZE;
    if (iCapacity < lLength)
        return 0;

    memset(oData, 0, lLength);
    encodePackedProfile(iIdentity.profile, iIdentity.subProfile,
                        oData[0], oData[1]);
    if (lLength > IOHC_DISCOVERY_IO_BACKBONE_OFFSET + 2)
    {
        oData[IOHC_DISCOVERY_IO_BACKBONE_OFFSET] =
            static_cast<uint8_t>((iIdentity.ioBackboneAddress >> 16) & 0xFF);
        oData[IOHC_DISCOVERY_IO_BACKBONE_OFFSET + 1] =
            static_cast<uint8_t>((iIdentity.ioBackboneAddress >> 8) & 0xFF);
        oData[IOHC_DISCOVERY_IO_BACKBONE_OFFSET + 2] =
            static_cast<uint8_t>(iIdentity.ioBackboneAddress & 0xFF);
    }
    if (lLength > IOHC_DISCOVERY_MANUFACTURER_OFFSET)
        oData[IOHC_DISCOVERY_MANUFACTURER_OFFSET] = iIdentity.manufacturerId;
    if (lLength > IOHC_DISCOVERY_FLAGS_OFFSET)
        oData[IOHC_DISCOVERY_FLAGS_OFFSET] = iIdentity.multiInfoByte;
    if (lLength > IOHC_DISCOVERY_TIMESTAMP_OFFSET + 1)
    {
        oData[IOHC_DISCOVERY_TIMESTAMP_OFFSET] =
            static_cast<uint8_t>((iIdentity.discoveryTimestamp >> 8) & 0xFF);
        oData[IOHC_DISCOVERY_TIMESTAMP_OFFSET + 1] =
            static_cast<uint8_t>(iIdentity.discoveryTimestamp & 0xFF);
    }
    return lLength;
}

inline const char *ioHomeCommandResultName(uint8_t iCode)
{
    switch (iCode)
    {
    case 0x00: return "unknown-status-reply"; case 0x01: return "completed-ok";
    case 0x02: return "no-contact"; case 0x03: return "manually-operated";
    case 0x04: return "blocked"; case 0x05: return "wrong-system-key";
    case 0x06: return "priority-level-locked"; case 0x07: return "wrong-position-reached";
    case 0x08: return "execution-error"; case 0x09: return "not-executed";
    case 0x0A: return "calibrating"; case 0x0B: return "power-too-high";
    case 0x0C: return "power-too-low"; case 0x0D: return "lock-position-open";
    case 0x0E: return "motion-time-too-long"; case 0x0F: return "thermal-protection";
    case 0x10: return "not-operational"; case 0x11: return "filter-maintenance";
    case 0x12: return "battery-level"; case 0x13: return "target-modified";
    case 0x14: return "mode-not-implemented"; case 0x15: return "command-incompatible-with-movement";
    case 0x16: return "user-action"; case 0x17: return "dead-bolt-error";
    case 0x18: return "automatic-cycle-engaged"; case 0x19: return "wrong-load";
    case 0x1A: return "colour-not-reachable"; case 0x1B: return "target-not-reachable";
    case 0x1C: return "bad-index"; case 0x1D: return "command-overruled";
    case 0x1E: return "waiting-for-power"; case 0x20: return "node-locked";
    case 0x21: return "wrong-position"; case 0x22: return "limits-not-set";
    case 0x23: return "ip-not-set"; case 0x24: return "out-of-range";
    case 0x38: return "priority-locked-not-executed"; case 0x58: return "invalid-function-index";
    case 0xDF: return "information"; case 0xE0: return "parameter-limited";
    case 0xE1: return "limited-by-local-user"; case 0xE2: return "limited-by-user";
    case 0xE3: return "limited-by-rain"; case 0xE4: return "limited-by-timer";
    case 0xE5: return "limited-by-scd"; case 0xE6: return "limited-by-ups";
    case 0xE7: return "limited-by-unknown-device"; case 0xEA: return "limited-by-saac";
    case 0xEB: return "limited-by-wind"; case 0xEC: return "limited-by-self";
    case 0xED: return "limited-by-automatic-cycle"; case 0xEE: return "limited-by-emergency";
    default: return "unknown";
    }
}

inline const char *ioHomeCommandResultDescription(uint8_t iCode)
{
    switch (iCode)
    {
    case 0x01: return "command completed successfully";
    case 0x02: return "device could not establish contact";
    case 0x03: return "device was operated manually";
    case 0x04: return "movement or command is blocked";
    case 0x05: return "device rejected the system key";
    case 0x06: case 0x38: return "a higher priority level currently holds the device";
    case 0x07: case 0x21: return "device did not reach the requested position";
    case 0x08: return "an error occurred while executing the command";
    case 0x09: return "device did not execute the command";
    case 0x0A: return "device is calibrating";
    case 0x0B: return "measured power consumption is too high";
    case 0x0C: return "measured power consumption is too low";
    case 0x0D: return "lock reports an open position";
    case 0x0E: return "movement exceeded the permitted duration";
    case 0x0F: return "thermal protection is active";
    case 0x10: return "product is not operational";
    case 0x11: return "filter maintenance is required";
    case 0x12: return "battery status information";
    case 0x13: return "target was modified by the device";
    case 0x14: return "requested mode is not implemented";
    case 0x15: return "command is incompatible with current movement";
    case 0x16: return "local user action affected the command";
    case 0x17: return "dead bolt operation failed";
    case 0x18: return "automatic cycle is engaged";
    case 0x19: return "an incompatible load is connected";
    case 0x1A: return "requested colour cannot be reached";
    case 0x1B: return "requested target cannot be reached";
    case 0x1C: case 0x58: return "requested function or index is invalid";
    case 0x1D: return "another command overruled this command";
    case 0x1E: return "node is waiting for power";
    case 0x20: return "node is locked";
    case 0x22: return "movement limits have not been set";
    case 0x23: return "IP configuration has not been set";
    case 0x24: return "requested value is outside the supported range";
    case 0xDF: return "device returned informational status";
    case 0xE0: return "device limited the requested parameter";
    case 0xE1: return "operation is limited by a local user";
    case 0xE2: return "operation is limited by a user";
    case 0xE3: return "operation is limited by rain protection";
    case 0xE4: return "operation is limited by a timer";
    case 0xE5: return "operation is limited by an SCD";
    case 0xE6: return "operation is limited by the power supply";
    case 0xE7: return "operation is limited by another device";
    case 0xEA: return "operation is limited by stand-alone automation";
    case 0xEB: return "operation is limited by wind protection";
    case 0xEC: return "operation is limited by the node itself";
    case 0xED: return "operation is limited by an automatic cycle";
    case 0xEE: return "operation is limited by an emergency condition";
    default: return "unknown or unspecified command result";
    }
}

// 1W repeat transmission (fire-and-forget sends 4x at 40ms intervals)
#define IOHC_1W_REPEAT_COUNT 3        // 3 additional repeats (4 total transmissions, matches reference)
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
#define IOHC_COZY_TEMP_MIN_TENTHS 70
#define IOHC_COZY_TEMP_MAX_TENTHS 280

namespace IoHomeCozyPayload
{
    inline uint8_t buildTemperature(uint8_t *oData, uint16_t iTempTenths)
    {
        if (oData == nullptr || iTempTenths < IOHC_COZY_TEMP_MIN_TENTHS ||
            iTempTenths > IOHC_COZY_TEMP_MAX_TENTHS)
            return 0;
        oData[0] = IOHC_COZY_ORIGINATOR;
        oData[1] = IOHC_COZY_ACEI_WRITE;
        oData[2] = 0x01;
        oData[3] = 0x03; // temperature set register 0x0103
        // Atlantic/Thermor setpoint: unsigned 16-bit little-endian tenths.
        oData[4] = static_cast<uint8_t>(iTempTenths & 0xFF);
        oData[5] = static_cast<uint8_t>(iTempTenths >> 8);
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
