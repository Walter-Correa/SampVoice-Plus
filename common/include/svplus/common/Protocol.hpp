/*
 * SampVoice Plus - Common Network Protocol Definitions
 *
 * Wire-compatible with CyberMor/sampvoice (v3.0/v3.1) and AmyrAhmady/sampvoice (v3.1-omp).
 * Uses safe 32-bit opaque generational stream/effect identifiers instead of raw memory
 * pointers, protecting against stale handle dereferences and ABA bugs across script reloads
 * while maintaining wire compatibility with legacy v11 clients.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace svplus::protocol {

// -----------------------------------------------------------------------------
// Protocol Constants
// -----------------------------------------------------------------------------

inline constexpr uint8_t  kLegacyVersion             = 11;          // CyberMor/AmyrAhmady v3.1 protocol version
inline constexpr uint8_t  kPlusVersion               = 12;          // SampVoice Plus protocol identifier (via ConnectPlusExtension)
inline constexpr uint8_t  kCyberMorV4Protocol        = 4;           // CyberMor v4.x control/voice architecture (unsupported)
inline constexpr uint32_t kConnectSignature          = 0xDeadBeefu; // Signature scanned inside RPC 25 (PlayerConnect)
inline constexpr uint8_t  kConnectSignatureBytes[4]  = { 0xEF, 0xBE, 0xAD, 0xDE };
inline constexpr uint32_t kCyberMorV4Signature       = 0xDEADC0DEu; // Signature used by CyberMor v4.x clients in RPC 25
inline constexpr uint8_t  kCyberMorV4SignatureBytes[4] = { 0xDE, 0xC0, 0xAD, 0xDE };

inline constexpr uint8_t  kRakNetPacketIdControl        = 222; // Custom RakNet packet ID for SampVoice control channel
inline constexpr uint8_t  kRakNetPacketIdDisconnect     = 32;  // ID_DISCONNECTION_NOTIFICATION
inline constexpr uint8_t  kRakNetPacketIdConnectionLost = 33;  // ID_CONNECTION_LOST
inline constexpr uint8_t  kRakNetRpcPlayerConnect       = 25;  // SA-MP PlayerConnect RPC ID

inline constexpr uint16_t kInvalidPlayerId           = 0xFFFFu;
inline constexpr uint32_t kInvalidHandle             = 0u;

inline constexpr uint32_t kFrequency                 = 48000u;      // Opus sample rate (Hz)
inline constexpr uint32_t kDefaultBitrate            = 24000u;      // Default Opus target bitrate (bps)
inline constexpr uint32_t kOpusBitrate               = kDefaultBitrate;
inline constexpr uint32_t kLegacyFrameMs             = 100u;        // Legacy CyberMor/AmyrAhmady frame duration (ms)
inline constexpr uint32_t kLegacyFrameSamples        = (kFrequency / 1000u) * kLegacyFrameMs;       // 4800 samples

inline constexpr std::size_t kMaxVoicePayloadBytes   = 1400u;       // Safe MTU-aware max Opus payload
inline constexpr std::size_t kMaxControlPayloadSize  = 4096u;
inline constexpr std::size_t kMaxUdpPacketSize       = 1500u;

// -----------------------------------------------------------------------------
// Packet Opcodes & Enumerations
// -----------------------------------------------------------------------------

enum class ControlPacketType : uint16_t {
    // v3.0 standard packets
    ServerInfo              = 0,
    PluginInit              = 1,
    MuteEnable              = 2,
    MuteDisable             = 3,
    StartRecord             = 4,
    StopRecord              = 5,
    AddKey                  = 6,
    RemoveKey               = 7,
    RemoveAllKeys           = 8,
    CreateGStream           = 9,
    CreateLPStream          = 10,
    CreateLStreamAtVehicle  = 11,
    CreateLStreamAtPlayer   = 12,
    CreateLStreamAtObject   = 13,
    UpdateLStreamDistance   = 14,
    UpdateLPStreamPosition  = 15,
    DeleteStream            = 16,
    PressKey                = 17,
    ReleaseKey              = 18,

    // v3.1 standard packets
    SetStreamParameter      = 19,
    SlideStreamParameter    = 20,
    CreateEffect            = 21,
    DeleteEffect            = 22,

    // SampVoice Plus extensions (only sent to clients negotiating kPlusVersion+)
    PlusCapabilities        = 100,
    PlusTelemetryPing       = 101,
    PlusTelemetryPong       = 102,
    PlusListenerState       = 103,
    PlusStreamIcon          = 104,
    PlusMicroState          = 105,
    PlusSpeakerStreams      = 106
};

enum class StreamIconId : uint8_t {
    Speaker   = 0,
    Radio     = 1,
    Phone     = 2,
    Megaphone = 3,
    Police    = 4,
    Admin     = 5,
    Global    = 6,
    Count
};

inline constexpr std::size_t kStreamIconCount     = static_cast<std::size_t>(StreamIconId::Count);
// Single shared red color for blocked-speaker icons (100% gradient stop) and [BLACKLIST] HUD text.
inline constexpr uint32_t    kBlockedAccentArgb   = 0xFFE5483Cu; // #E5483C

enum class VoicePacketType : uint32_t {
    KeepAlive   = 0,
    VoicePacket = 1
};

enum class ParameterType : uint32_t {
    Frequency = 1,
    Volume    = 2,
    Panning   = 3,
    EaxMix    = 4,
    Src       = 8
};

enum class EffectType : uint32_t {
    Chorus      = 0,
    Compressor  = 1,
    Distortion  = 2,
    Echo        = 3,
    Flanger     = 4,
    Gargle      = 5,
    I3dl2Reverb = 6,
    ParamEq     = 7,
    Reverb      = 8
};

// Capability bitmask for SampVoice Plus clients
enum class PlusCapabilityFlags : uint32_t {
    None           = 0,
    AdaptiveJitter = 1u << 1,
    Telemetry      = 1u << 2
};

[[nodiscard]] constexpr PlusCapabilityFlags operator|(PlusCapabilityFlags a, PlusCapabilityFlags b) noexcept {
    return static_cast<PlusCapabilityFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr PlusCapabilityFlags operator&(PlusCapabilityFlags a, PlusCapabilityFlags b) noexcept {
    return static_cast<PlusCapabilityFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

// -----------------------------------------------------------------------------
// Packed Wire Structures (Exact Binary Layout)
// -----------------------------------------------------------------------------

#pragma pack(push, 1)

struct Vec3f {
    float x;
    float y;
    float z;
};

struct ControlPacketHeader {
    uint16_t packet; // ControlPacketType
    uint16_t length; // Payload size in bytes following this header
};

struct VoicePacketHeader {
    uint32_t hash;   // CRC32C of bytes [offsetof(svrkey) .. sizeof(VoicePacketHeader))
    uint32_t svrkey; // Server-assigned session key (cleared to 0 before forwarding to listeners)
    uint32_t packet; // VoicePacketType
    uint32_t stream; // Opaque 32-bit stream handle (safe generational ID on SV+ server)
    uint16_t sender; // Sender player ID
    uint16_t length; // Opus payload length in bytes
    uint32_t packid; // Monotonic frame sequence number
};

// Appended inside RPC 25 (PlayerConnect) BitStream
struct ConnectPacket {
    uint32_t signature; // Must be kConnectSignature (0xDeadBeef)
    uint8_t  version;   // kLegacyVersion (11) or kPlusVersion (12)
    uint8_t  micro;     // Non-zero if microphone is available
};

// Optional extension appended immediately after ConnectPacket by SV+ clients
struct ConnectPlusExtension {
    uint32_t magic;        // 0x53565031 ('SVP1')
    uint32_t capabilities; // Bitmask of PlusCapabilityFlags
    uint16_t clientBuild;  // Client build number
};

struct ServerInfoPacket {
    uint32_t serverKey;
    uint16_t serverPort;
};

struct PluginInitPacket {
    uint32_t bitrate;
    uint8_t  mute;
};

struct AddKeyPacket {
    uint8_t keyId;
};

struct RemoveKeyPacket {
    uint8_t keyId;
};

struct CreateGStreamPacket {
    uint32_t stream;
    uint32_t color;
    // Followed by null-terminated UTF-8 name[]
};

struct CreateLPStreamPacket {
    uint32_t stream;
    float    distance;
    Vec3f    position;
    uint32_t color;
    // Followed by null-terminated UTF-8 name[]
};

struct CreateLStreamAtPacket {
    uint32_t stream;
    float    distance;
    uint32_t target;
    uint32_t color;
    // Followed by null-terminated UTF-8 name[]
};

struct UpdateLStreamDistancePacket {
    uint32_t stream;
    float    distance;
};

struct UpdateLPStreamPositionPacket {
    uint32_t stream;
    Vec3f    position;
};

struct DeleteStreamPacket {
    uint32_t stream;
};

struct PressKeyPacket {
    uint8_t keyId;
};

struct ReleaseKeyPacket {
    uint8_t keyId;
};

struct SetStreamParameterPacket {
    uint32_t stream;
    uint32_t parameter;
    float    value;
};

struct SlideStreamParameterPacket {
    uint32_t stream;
    uint32_t parameter;
    float    startValue;
    float    endValue;
    uint32_t timeMs;
};

struct CreateEffectPacket {
    uint32_t stream;
    uint32_t effect;
    uint32_t number;   // EffectType (matches BASS_FX_DX8_*)
    int32_t  priority;
    // Followed by effect parameter bytes
};

struct DeleteEffectPacket {
    uint32_t stream;
    uint32_t effect;
};

// BASS DX8 effect parameter wire layouts (matching BASS_DX8_* structs)
struct ChorusParams {
    float    wetDryMix;
    float    depth;
    float    feedback;
    float    frequency;
    uint32_t waveform;
    float    delay;
    uint32_t phase;
};

struct CompressorParams {
    float gain;
    float attack;
    float release;
    float threshold;
    float ratio;
    float predelay;
};

struct DistortionParams {
    float gain;
    float edge;
    float postEqCenterFrequency;
    float postEqBandwidth;
    float preLowpassCutoff;
};

struct EchoParams {
    float   wetDryMix;
    float   feedback;
    float   leftDelay;
    float   rightDelay;
    int32_t panDelay; // Matches Windows BOOL (4 bytes) in BASS_DX8_ECHO
};

struct FlangerParams {
    float    wetDryMix;
    float    depth;
    float    feedback;
    float    frequency;
    uint32_t waveform;
    float    delay;
    uint32_t phase;
};

struct GargleParams {
    uint32_t rateHz;
    uint32_t waveShape;
};

struct I3dl2ReverbParams {
    int32_t room;
    int32_t roomHf;
    float   roomRolloffFactor;
    float   decayTime;
    float   decayHfRatio;
    int32_t reflections;
    float   reflectionsDelay;
    int32_t reverb;
    float   reverbDelay;
    float   diffusion;
    float   density;
    float   hfReference;
};

struct ParamEqParams {
    float center;
    float bandwidth;
    float gain;
};

struct ReverbParams {
    float inGain;
    float reverbMix;
    float reverbTime;
    float highFreqRtRatio;
};

// SV+ Extended Control Packets
struct PlusCapabilitiesPacket {
    uint32_t serverCapabilities;
    uint32_t recommendedFrameMs;
};

struct PlusTelemetryPingPacket {
    uint64_t clientTimestampUs;
};

struct PlusTelemetryPongPacket {
    uint64_t clientTimestampUs;
    uint32_t serverQueueDepth;
};

struct PlusListenerStatePacket {
    uint8_t enabled; // 1 = listening enabled, 0 = disabled by server
};

struct PlusStreamIconPacket {
    uint32_t stream;
    uint8_t  iconId; // StreamIconId
};

struct PlusMicroStatePacket {
    uint8_t hasMicro; // 1 = mic enabled & device present, 0 = unavailable/disabled
};

struct PlusSpeakerStreamsPacket {
    uint16_t count; // Followed by `count` serialized (PlusSpeakerStreamEntryHeader + name[nameLen]) records
};

struct PlusSpeakerStreamEntryHeader {
    uint32_t streamId;
    uint32_t color;   // Wire ARGB after ConvertStreamColor
    uint16_t nameLen; // UTF-8/ANSI byte count excluding NUL
    uint8_t  kind;    // 0 = global, 1 = positional (SL/DL)
};

#pragma pack(pop)

// -----------------------------------------------------------------------------
// Compile-time Wire Layout Assertions
// -----------------------------------------------------------------------------

static_assert(sizeof(ControlPacketHeader) == 4, "ControlPacketHeader must be 4 bytes");
static_assert(sizeof(VoicePacketHeader) == 24, "VoicePacketHeader must be 24 bytes");
static_assert(offsetof(VoicePacketHeader, hash) == 0, "hash must be at offset 0");
static_assert(offsetof(VoicePacketHeader, svrkey) == 4, "svrkey must be at offset 4");
static_assert(sizeof(ConnectPacket) == 6, "ConnectPacket must be 6 bytes");
static_assert(sizeof(ServerInfoPacket) == 6, "ServerInfoPacket must be 6 bytes");
static_assert(sizeof(PluginInitPacket) == 5, "PluginInitPacket must be 5 bytes");
static_assert(sizeof(CreateGStreamPacket) == 8, "CreateGStreamPacket must be 8 bytes");
static_assert(sizeof(CreateLPStreamPacket) == 24, "CreateLPStreamPacket must be 24 bytes");
static_assert(sizeof(CreateLStreamAtPacket) == 16, "CreateLStreamAtPacket must be 16 bytes");
static_assert(sizeof(UpdateLStreamDistancePacket) == 8, "UpdateLStreamDistancePacket must be 8 bytes");
static_assert(sizeof(UpdateLPStreamPositionPacket) == 16, "UpdateLPStreamPositionPacket must be 16 bytes");
static_assert(sizeof(DeleteStreamPacket) == 4, "DeleteStreamPacket must be 4 bytes");
static_assert(sizeof(SetStreamParameterPacket) == 12, "SetStreamParameterPacket must be 12 bytes");
static_assert(sizeof(SlideStreamParameterPacket) == 20, "SlideStreamParameterPacket must be 20 bytes");
static_assert(sizeof(CreateEffectPacket) == 16, "CreateEffectPacket must be 16 bytes");
static_assert(sizeof(DeleteEffectPacket) == 8, "DeleteEffectPacket must be 8 bytes");
static_assert(sizeof(EchoParams) == 20, "EchoParams must match BASS_DX8_ECHO (20 bytes)");
static_assert(sizeof(PlusListenerStatePacket) == 1, "PlusListenerStatePacket must be 1 byte");
static_assert(sizeof(PlusStreamIconPacket) == 5, "PlusStreamIconPacket must be 5 bytes");
static_assert(sizeof(PlusMicroStatePacket) == 1, "PlusMicroStatePacket must be 1 byte");
static_assert(sizeof(PlusSpeakerStreamsPacket) == 2, "PlusSpeakerStreamsPacket must be 2 bytes");
static_assert(sizeof(PlusSpeakerStreamEntryHeader) == 11, "PlusSpeakerStreamEntryHeader must be 11 bytes");

inline constexpr uint32_t kPlusExtensionMagic = 0x53565031u; // 'SVP1'

} // namespace svplus::protocol
