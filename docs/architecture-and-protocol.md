# Architecture & Wire Protocol

This document explains how **SampVoice Plus** achieves high throughput, AMX memory safety, low latency, and seamless interoperability with legacy SampVoice clients.

---

## 1. Dual-Protocol Interoperability (`v11` & `v12`)

Every voice handshake occurs inside **SA-MP RPC 25 (`PlayerConnect`)**:

| Protocol | Signature / Magic | Negotiated Client Support |
| :---: | :--- | :--- |
| **`v11`** (`kLegacyVersion`) | `0xDeadBeef`, `version = 11` | Standard CyberMor v3.x / AmyrAhmady control opcodes (`0..22`) and 100 ms (4800-sample) UDP voice frames. |
| **`v12`** (`kPlusVersion`) | `0xDeadBeef` (`version = 11`) + `ConnectPlusExtension` (`SVP1`) | Advertises packed 16-bit `clientBuild` (`((major & 0xF) << 12) \| ((minor & 0x3F) << 6) \| (patch & 0x3F)`, e.g. `0x1000` for `1.0.0`) and `PlusCapabilityFlags` (`AdaptiveJitter`, `Telemetry`) while maintaining full `v11` wire compatibility for older servers. |
| **CyberMor v4.x** | `0xDEADC0DE` | Unsupported architecture; logged once per connection via `PlayerStore::RegisterUnsupportedV4` and ignored cleanly. |

* **Capability Negotiation:** Plus servers upgrade clients sending a valid `SVP1` extension (or `version >= 12`) to `clientProtocol = 12` (`SvGetClientProtocol(playerid)`), while keeping `SvGetVersion(playerid)` compatible (`11` or `12`). Plus control packets (`PlusCapabilities = 100`, `PlusTelemetryPing = 101`, `PlusTelemetryPong = 102`, `PlusListenerState = 103`, `PlusStreamIcon = 104`, `PlusMicroState = 105`, `PlusSpeakerStreams = 106`) are strictly gated to `v12` clients and never sent to `v11` (PC 3.x / AmyrAhmady / Android 3.x launcher) clients.
* **NAT Port Migration:** Upon valid `serverKey` + `CRC32C` + matching IPv4, the server binds the client's UDP source port dynamically and permits NAT rebinding with a 2-second rate limit (`TryIdentifyUdpEndpoint`).
* **Graceful `DeleteStream` Delay (`v11` Listeners):** When a legacy `v11` listener is detached from an active stream (or the stream is deleted) within 1000 ms of its last forwarded voice packet, the server stops forwarding voice immediately and defers `DeleteStream` until `lastVoice + 1000 ms` (canceled without resending `CreateStream` if re-attached beforehand) so BASS stops playback normally instead of leaving speaker names stuck on screen; `v12` Plus clients always receive `DeleteStream` immediately.

---

## 2. UDP Wire Framing & CRC32C Integrity

```cpp
struct VoicePacketHeader {
    uint32_t hash;   // CRC32C of bytes [offsetof(svrkey) .. sizeof(VoicePacketHeader))
    uint32_t svrkey; // Server-assigned session key (zeroed before forwarding to listeners)
    uint32_t packet; // VoicePacketType (0 = KeepAlive, 1 = VoicePacket)
    uint32_t stream; // Opaque 32-bit stream handle
    uint16_t sender; // Speaker player ID
    uint16_t length; // Opus payload length in bytes (<= 1400)
    uint32_t packid; // Monotonic frame sequence number
};
```

* Hardware-accelerated **CRC32C** verification rejects malformed, spoofed, or stale packets before touching player state or audio queues.
* Maximum Opus payload is capped at **`1400` bytes** (`kMaxVoicePayloadBytes`), keeping UDP datagrams inside standard Ethernet MTU (`kMaxUdpPacketSize = 1500`).

### `PlusSpeakerStreams` (`106`) Wire Layout (`v12+`)

Sent reliably by the server whenever a `v12+` speaker attaches to or detaches from streams:

```cpp
struct PlusSpeakerStreamsPacket {
    uint16_t count; // Number of trailing (PlusSpeakerStreamEntryHeader + name[nameLen]) records
};

struct PlusSpeakerStreamEntryHeader { // 11 bytes packed
    uint32_t streamId;
    uint32_t color;   // Wire 0xAARRGGBB after ConvertStreamColor
    uint16_t nameLen; // Byte length excluding NUL
    uint8_t  kind;    // 0 = global, 1 = positional (SLStream / DLStream)
};
```

---

## 3. Generational 32-Bit Handles (`HandlePool`)

**SampVoice Plus** replaces raw heap pointers cast to Pawn integers with **`HandlePool`**:
* Every `Stream` and `Effect` receives an opaque **32-bit handle** combining slot index and generation counter (`0` = `kInvalidHandle` / `SV_NULL`).
* Validates handles in $O(1)$, eliminating dangling-pointer and ABA bugs across gamemode/filterscript unloads.
* Tracks `ownerAmx` per stream/effect and reclaims resources automatically in `ServerCore::CleanupAmxResources(amx)`.

---

## 4. Concurrency, COW Snapshots & $O(1)$ Spatial Grid

```
+-------------------+      WSA/poll + recvmmsg      +--------------------+
| UDP Socket Layer  | ----------------------------> |  Worker Thread(s)  |
| Non-blocking I/O  | <---------------------------- |  TX/RX Forwarding  |
+-------------------+      Pre-allocated Pool       +---------+----------+
                                                              |
                               Lock-Free SPSC / MPMC + COW Listener Snapshots
                                                              |
+-------------------+      Main Server Tick         +---------v----------+
| SA-MP / open.mp   | <---------------------------> |     ServerCore     |
| Adapter & Pawn    |                               |  O(1) SpatialGrid  |
+-------------------+                               +--------------------+
```

1. **Non-Blocking UDP (`UdpSocket`):** Uses `WSAPoll` on Windows and `poll` with `recvmmsg` batching on Linux (`SO_EXCLUSIVEADDRUSE` on Windows fixed ports).
2. **Lock-Free Worker Fan-Out:** Audio forwarding reads copy-on-write (`std::atomic_load_explicit`) `Stream::GetListenersSnapshot()` vectors, avoiding listener mutex contention on UDP worker threads.
3. **$O(1)$ Spatial Hash Grid:** Dynamic local streams (`DLStream`) support legacy `maxplayers` semantics (`0` = none, `-1` / `SV_INFINITY` = unlimited, positive = nearest $K$ preserving existing in-range listeners) updated on `sampvoice.updaterate`.
