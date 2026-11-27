# SampVoice Plus — Under the Hood

> [!NOTE]
> This technical reference is written for auditors, plugin developers, contributors, and advanced server administrators who want to inspect wire formats, threading, safety mechanisms, memory hooks, and installer internals. **You do not need this document to install, configure, or script SampVoice Plus.** For setup and scripting, see [Getting Started](getting-started.md), [Audio Streams & Effects](audio-streams-and-effects.md), and the [API Reference](api-reference.md).

---

## 1. System Overview

```text
+-------------------+      WSAPoll/poll + recvmmsg    +--------------------+
| UDP Socket Layer  | ------------------------------> |  Worker Thread(s)  |
| Non-blocking I/O  | <------------------------------ |  TX/RX Forwarding  |
+-------------------+       Pre-allocated Pool        +---------+----------+
                                                                |
                                 Lock-Free SPSC / MPMC + COW Listener Snapshots
                                                                |
+-------------------+      Main Server Tick           +---------v----------+
| SA-MP / open.mp   | <-----------------------------> |     ServerCore     |
| Adapter & Pawn    |                                 |  O(1) SpatialGrid  |
+-------------------+                                 +--------------------+

+-------------------+      SA-MP Render / Game Thread   +--------------------+
| ProxyRakClient    | ---- RPC 25 + Packet 222 -------> | Background Audio   |
| + D3D9 / WndProc  | <--- Game Spatial Snapshot (SEH)  | Thread (~10 ms):   |
| (F11 Menu & HUD)  |                                   | Opus / BASS / UDP  |
+-------------------+                                   +--------------------+
```

---

## 2. Server Internals

### Threading, UDP Layer & Lock-Free Fan-Out
* **Non-Blocking UDP (`UdpSocket`):** Uses `WSAPoll` on Windows and `poll` with `recvmmsg` batching on Linux (`SO_EXCLUSIVEADDRUSE` on Windows fixed ports).
* **Lock-Free Worker Fan-Out:** Audio forwarding reads copy-on-write (`std::atomic_load_explicit`) `Stream::GetListenersSnapshot()` vectors, avoiding listener mutex contention on UDP worker threads.
* **$O(1)$ Spatial Hash Grid:** Dynamic local streams (`DLStream`) track connected players in a spatial hash grid synchronized on `sampvoice.updaterate` (clamped `[10..1000]` ms). In `Stream::TickDynamic`, `maxplayers` limits the **maximum concurrent listeners attached to that stream** (`0` = none, `-1` / `SV_INFINITY` = unlimited, positive = nearest $K$ preserving existing in-range listeners while they remain inside radius, interior, and virtual world filters).

### Generational 32-Bit Handles (`HandlePool`)
* Every `Stream` and `Effect` receives an opaque **32-bit handle** combining slot index and generation counter (`0` = `kInvalidHandle` / `SV_NULL`), eliminating dangling-pointer and ABA bugs across gamemode/filterscript reloads.
* Validates handles in $O(1)$ and tracks `ownerAmx` per resource, reclaiming leaked streams/effects automatically in `ServerCore::CleanupAmxResources(amx)`.

### Safe & Cached Callback Delivery (`PawnBindings::InvokeCallback`)
* **Tick-Only Dispatch:** Callbacks never fire synchronously inside natives; they execute sequentially on the server's main game tick (`OnPlayerActivationKeyPress`, `OnPlayerActivationKeyRelease`, `OnPlayerMicroStateChange`, `OnPlayerVoiceMute`, `OnPlayerVoiceUnmute`, `OnPlayerVoiceReady`, `OnPlayerVoiceStart`, `OnPlayerVoiceStop`).
* **Unload Re-Verification:** Because one AMX script can trigger unloading of another during callback delivery, `PawnBindings::InvokeCallback` snapshots loaded scripts onto a stack buffer and re-verifies under mutex that each `AMX*` pointer is still registered immediately prior to `amx_Push` / `amx_Exec`.
* **Public Index Caching & Log Rate-Limiting:** Per-`(AMX*, callbackId)` `public` indices are cached cleanly and invalidated on script load/unload (avoiding repeated `amx_FindPublic` lookups on hot audio callbacks while preserving optional `y_hooks` forwards). Runtime `AMX execution error` warnings are rate-limited per callback and script to at most one log every `60 s` (reporting suppressed counts).

### Anti-Flood Protection & Key-Edge Guarantees (`PlayerVoiceSlot`)
* **UDP Voice Bucket:** `30 pkt/s` sustained, burst `10` per player (`~3×` headroom over `10 pkt/s` 100 ms Opus frames).
* **Control Packet Buckets (per player, isolated mutex):**
  * `PressKey` / `ReleaseKey` (`17`, `18`): **`20/s`, burst `20`**. Held-key tracking (`pressedKeyBits`) guarantees that releasing a physically held key (`ReleaseKey`) always passes even when under flood throttling so keys never stick latched.
  * `PlusMicroState` (`105`): **`4/s`, burst `4`**.
  * `PlusTelemetryPing` (`101`): **`2/s`, burst `2`**.
  * `PlusVoiceLatency` (`111`): **`1/s`, burst `2`** (excess silently dropped at `Debug` level).
* **NAT Port Migration:** Upon valid `serverKey` + `CRC32C` + matching IPv4, the server binds the client's UDP source port dynamically and permits NAT rebinding with a **2-second** rate limit (`TryIdentifyUdpEndpoint`).

### Pawn Native Validation & Rate-Limited Diagnostics
* Float inputs (`distance`, coordinates, stream parameters, slide deltas, DX8 effect parameters) check `std::isfinite` (rejecting `NaN` / `±Inf`). `distance`, `SV_PARAMETER_VOLUME`, and `SV_PARAMETER_FREQUENCY` reject negative values (`< 0.0`), while `SV_PARAMETER_FREQUENCY = 0.0` restores the original `48000 Hz` sample rate and `SvStreamParameterSlide` requires finite `deltavalue` and finite `current + deltavalue`.
* Rejections return `0` / `false` (`SV_NULL`) without altering state or emitting packets, logging at most one `[sv+:WRN]` per `(amx, nativeName)` every `60 s` with suppressed-count summaries.

### Lock-Free Voice Worker Pipeline & Statistics Counters
* **Voice Forwarding Fast Path (`ProcessVoicePacket`):** Rejects muted speakers, pair-mutes (`1000 × 1024` atomic bit matrix), disabled listeners, and per-listener `voicePausedMask_` flags cleanly via lock-free atomic loads before `SendTo`.
* **Per-Worker Cache-Aligned Counters:** Each worker thread accumulates TX packet/byte counts locally per packet and flushes them with a single `relaxed` atomic add into cache-line-aligned (`alignas(64)`) per-worker slots (`workerVoiceCounters_`), updating `perPlayerTxPackets[listenerId]` inside the worker's own array to eliminate cross-core cache bouncing.
* **1-Second Rates & 60-Second Tick Profiling:** `ServerCore::Tick()` computes RX/TX packet and byte rates once per second from counter deltas, and buckets `steady_clock` tick durations into a 60-slot ring (`TickSecondBucket`) to report 60-second average and maximum microseconds cleanly via `SvGetServerStats` (`SV_STAT_*`). Player stats (`SvGetPlayerStats`, `SV_PSTAT_*`) combine `PlayerVoiceSlot` counters with per-worker listener TX sums offset at disconnect/connect.

### Channel Priority & Ducking Evaluation
* **Zero Cost When Unused:** Only streams with `priority > 0` enter `prioritizedStreams_`. When no prioritized stream exists, the `50 ms` speaking scan skips ducking work entirely (`0` packets, byte-identical to `1.3.0`).
* **Active Priority Scan (`UpdatePriorityDucking`):** Runs at the end of the `50 ms` speaking scan (`speaking_[]` with `1000 ms` silence hold). For each listener `L`, computes `bestActivePrio[L]` = smallest `p > 0` across streams containing an unmuted speaker `X != L` who is actively speaking and not pair-muted for `L` (same priority and self-speech do not duck; no stacking).
* **Packet Delivery & State Separation:** Changes in `bestActivePrio[L]` update lower-priority (`0` or `> bestPrio`) listener streams via `SlideStreamParameter` (`150 ms` down, `400 ms` restore after `1 s` silence) scaling `scriptVolume * listenerFactor * priorityFactor` — starting any interrupted transition linearly from the current interpolated duck factor so volume never jumps back to `100%` mid-slide — or apply immediately on ongoing volume slides and late `AttachListener` / `TickDynamic` entries, while keeping script-queried `SvStreamGetListenerVolume` and `SvStreamParameterGet` untouched.

### Configuration Resolution & Logging
* **Explicit Bind & Port Fail-Fast:** When a non-zero `sampvoice.port` is outside `1..65535` or occupied (`SO_EXCLUSIVEADDRUSE`), startup logs `[sv+:ERR]` and aborts rather than binding a random ephemeral port. When `sampvoice.bind` is omitted, it inherits host `bind` / `network.bind` when non-wildcard.
* **Legacy Key Aliases (dotted `sampvoice.*` wins with `[WRN]` on conflict):** `sampvoice_port`, `sv_voice_port`, `sampvoice_bind`, `sampvoice_threads`, `sampvoice.update_rate`, `sampvoice_updaterate`, `legacy_color_format`.
* **Logger Integration:** When `logprintf` (SA-MP) or `ILogger` (open.mp) sink is active, logs go directly to the host log without creating `svplus.log`. `SvDebug(true)` enables verbose `[sv+:DBG]` logging and emits the resolved `Effective config:` summary once.

---

## 3. Wire Protocol (`v11` & `v12`)

### Handshake & Protocol Negotiation (SA-MP RPC 25 `PlayerConnect`)

| Protocol | Signature / Magic | Negotiated Behavior |
| :--- | :--- | :--- |
| **`v11`** (`kLegacyVersion`) | `0xDeadBeef`, `version = 11` | Standard CyberMor v3.x / AmyrAhmady control opcodes (`0..22`) and 100 ms (4800-sample) Opus UDP frames. |
| **`v12`** (`kPlusVersion`) | `0xDeadBeef` (`version = 11`) + `ConnectPlusExtension` (`0x53565031` `'SVP1'`) | Advertises `PlusCapabilityFlags` (`AdaptiveJitter = 1<<1`, `Telemetry = 1<<2`, `VoiceLatency = 1<<3`, `Mobile = 1<<4`) and packed 16-bit `clientBuild` (`((major & 0xF) << 12) \| ((minor & 0x3F) << 6) \| (patch & 0x3F)`, e.g. `0x1000` for `1.0.0`, `0x10C0` / `4288` for `1.3.0`, `0x1100` / `4352` for `1.4.0`). |
| **CyberMor v4.x** | `0xDEADC0DE` | Incompatible architecture; logged once per connection via `PlayerStore::RegisterUnsupportedV4` and ignored cleanly. |

`SvGetVersion(playerid)` returns `ConnectPacket.version` (`11` for both legacy `v11` and current Plus `v12`), whereas `SvGetClientProtocol(playerid)` returns `12` once upgraded via `SVP1`.

### Control Packets (RakNet Packet ID `222`)

#### Legacy `v11` Control Opcodes (`0..22`)
* `0`: `ServerInfo` (`serverKey`, `serverPort`)
* `1`: `PluginInit` (`bitrate`, `mute`)
* `2` / `3`: `MuteEnable` / `MuteDisable`
* `4` / `5`: `StartRecord` / `StopRecord`
* `6` / `7` / `8`: `AddKey` / `RemoveKey` / `RemoveAllKeys`
* `9..13`: `CreateGStream` / `CreateLPStream` / `CreateLStreamAtVehicle` / `CreateLStreamAtPlayer` / `CreateLStreamAtObject`
* `14` / `15`: `UpdateLStreamDistance` / `UpdateLPStreamPosition`
* `16`: `DeleteStream`
* `17` / `18`: `PressKey` / `ReleaseKey` *(client -> server)*
* `19` / `20`: `SetStreamParameter` / `SlideStreamParameter` *(when all listeners use volume factor `1.0`, identical packets are sent; per-listener `factor != 1.0` sends individualized `streamBaseVolume * factor` scaled packets. For `SV_PARAMETER_FREQUENCY`, the server always emits `48000.0` (`kFrequency`) on the wire and in slide math whenever the logical endpoint/default is `0.0`, preventing older Plus `1.0`–`1.2` clients (`max(100, v)`) from clamping `0` down to `100 Hz`, while `SvStreamParameterGet`/`GetEx` continue returning `0.0` at rest)*
* `21` / `22`: `CreateEffect` / `DeleteEffect`

#### SampVoice Plus `v12` Extensions (`100..111`, Never Sent to `v11`)

| Opcode | Name | Direction | Payload & Wire Rules |
| :---: | :--- | :---: | :--- |
| `100` | `PlusCapabilities` | S -> C | `{ uint32_t serverCapabilities; uint32_t recommendedFrameMs; }`. `VoiceLatency` (`1 << 3`) signals that `111` reports are accepted. |
| `101` / `102` | `PlusTelemetryPing` / `Pong` | C <-> S | Fixed `uint64_t clientTimestampUs` (`Pong` appends `uint32_t serverQueueDepth`). Client measures SA-MP control-channel RTT (including server tick wait). |
| `103` | `PlusListenerState` | S -> C | `{ uint8_t enabled; }` — notifies F11 banner of `SvDisableListener` / `SvEnableListener`. |
| `104` | `PlusStreamIcon` | S -> C | `{ uint32_t stream; uint8_t iconId; }` (`0..6`). |
| `105` | `PlusMicroState` | C -> S | `{ uint8_t hasMicro; }` — updates live `SvHasMicro` & `OnPlayerMicroStateChange`. |
| `106` | `PlusSpeakerStreams` | S -> C | `{ uint16_t count; }` + `count` records (`PlusSpeakerStreamEntryHeader { uint32_t streamId; uint32_t color; uint16_t nameLen; uint8_t kind; }` + `name[nameLen]`, where `kind: 0=global, 1=positional`). |
| `107` | `PlusMuteKeyLatch` | S -> C | Zero payload alongside `MuteEnable` when `releasekeys = true`; latches keys until physical release + re-press (`1.1.0+`). |
| `108` | `PlusServerOptions` | S -> C | Packed 4-byte layout `{ uint8_t voiceStatus; uint8_t nearbyList; uint8_t allowToggleMode; uint8_t voiceOcclusion; }`. Plus `1.1.0` reads bytes `0..1`; `1.2.0+` reads 2/3/4 bytes safely, defaulting missing trailing bytes (`allowToggleMode=true`, `voiceOcclusion=false`). |
| `109` | `PlusPlayerVoiceState` | S -> C | Chunked initial snapshot + deltas (`{ uint16_t count; }` + `{ uint16_t playerId; uint8_t state; }`: `0=ok`, `1=no micro`, `2=cannot hear`). Never sent if `voice_status = false`. |
| `110` | `PlusStreamInfo` | S -> C | `{ uint32_t stream; uint32_t color; }` + trailing NUL-terminated UTF-8 name (up to `128` bytes). Sent only to Plus `1.3.0+` for live metadata updates. |
| `111` | `PlusVoiceLatency` | C -> S | `{ uint32_t latencyMs; }`. Sent by Plus `1.3.0+` every `2 s` when `PlusCapabilityFlags::VoiceLatency` is advertised. |

### UDP Voice Wire Framing & CRC32C Integrity
```cpp
struct VoicePacketHeader {
    uint32_t hash;   // CRC32C of bytes [offsetof(svrkey) .. sizeof(VoicePacketHeader))
    uint32_t svrkey; // Server-assigned session key (zeroed before forwarding to listeners)
    uint32_t packet; // VoicePacketType (0 = KeepAlive, 1 = VoicePacket)
    uint32_t stream; // Opaque 32-bit generational stream handle
    uint16_t sender; // Speaker player ID
    uint16_t length; // Opus payload length in bytes (<= 1400)
    uint32_t packid; // Monotonic frame sequence number
};
```
Maximum Opus payload is capped at `1400` bytes (`kMaxVoicePayloadBytes`), keeping UDP packets under `1500`-byte MTU (`kMaxUdpPacketSize`).

### Deferred `DeleteStream`, Re-Attach & Recreate Rules (`v11` & Pre-`1.3.0`)
* **Deferred `DeleteStream` (`v11` Listeners Only):** Because `v11` BASS 3D clients can stick active speaker names on screen if a stream is destroyed mid-speech, detaching a legacy `v11` listener (or deleting a stream) within `1000 ms` of the last forwarded voice packet stops voice forwarding immediately and defers `DeleteStream` until `lastVoice + 1000 ms`; Plus (`v12`) clients always receive `DeleteStream` immediately.
* **Re-Attach Within the Deferred `DeleteStream` Window (`v11`):**
  * Cancels the pending `DeleteStream` without emitting a duplicate `CreateStream`, and calls `SendReattachSyncPackets` (sending current distance if local, `UpdateLPStreamPosition` if point-anchored, and current parameters / listener volume factor).
  * Tracks `PendingDeleteEntry::outdated`: marked `true` whenever stream name (`SvStreamSetName`), color (`SvStreamSetColor`), icon (`SvStreamSetIcon`), or attached effects (`SvEffectAttachStream` / `DetachStream` / `Delete` / `Update*`) changed while detached, **or** initialized to `true` when `DetachListenerInternal` / `DetachAllListeners` cancels an active pending recreate (`CancelPendingRecreate`). Position updates alone do **not** mark `outdated`.
  * When `outdated == true`, re-attach performs full stream recreate (`SendRecreateToListener`, or deferred via `ScheduleOrRecreateStream` until `1000 ms` silence / `5 s` max if voice flowed within `1 s`).
* **Metadata & Effect Updates by Client Version:**
  * `SvStreamSetName` / `SvStreamSetColor`: Plus **`1.3.0+`** receives only `PlusStreamInfo` (`110`); `v11` and pre-`1.3.0` Plus (`1.0.x`–`1.2.x`) recreate the stream with the same handle (restoring distance, position, parameters, effects, and icon; deferred up to `1000 ms` silence / `5 s` max while speaking, coalescing repeated edits), plus `PlusSpeakerStreams` (`106`) refresh for attached `v12+` speakers. On legacy `v11` clients, if continuous non-stop speech reaches the `5 s` ceiling, the server sets `Stream::voicePausedMask_` for that `v11` listener for **`1000 ms`** (~`1 s` brief cut) while keeping script state (`SvHasListenerInStream`) unchanged so the client drains its BASS channel before receiving `DeleteStream` + `CreateStream` without leaving speaker names stuck on screen; Plus `1.0`–`1.2` clients still recreate immediately without pausing.
  * `SvEffectUpdate*`: Plus **`1.3.0+`** receives a single `CreateEffect` (`21`) applied in place via `AudioStream::addOrUpdateEffect`; `v11` and pre-`1.3.0` Plus receive `DeleteEffect` (`22`) + `CreateEffect` (`21`).

### Minimum Client Feature Matrix
* **Plus `1.0.0+` (`10000`):** Configurable UI, 7 stream icons, adaptive `35 ms` jitter + FEC/PLC, scoped `[blacklist]`, software spatialization.
* **Plus `1.1.0+` (`10100`):** `PlusMuteKeyLatch` (`107`), `PlusServerOptions` (`108` bytes `0..1`), `PlusPlayerVoiceState` (`109`), 3D overhead voice status icons, `All / Nearby` list, client-side `SvInit` bitrate enforcement.
* **Plus `1.2.0+` (`10200`):** `PlusServerOptions` bytes `2..3` (`allow_toggle_mode`, `voice_occlusion`), per-player volume mix, self-loopback monitor, Deafen.
* **Plus `1.3.0+` (`10300`):** `PlusStreamInfo` (`110`), in-place `CreateEffect` (`21`) updates, `PlusVoiceLatency` (`111`) reporting.

---

## 4. Client Internals (`sampvoice.asi`)

### Portable Client Core (`svplus_client_core`) & Platform Interfaces
Audio capture, Opus encoding/decoding, jitter buffering, DSP filters, software 3D spatialization, and UDP voice networking live in a OS-independent static library (`client/core/`, CMake target `svplus_client_core`, buildable standalone via `SVPLUS_BUILD_CLIENT_CORE=ON`) fed by `CoreConfig` with zero Windows, SA-MP, or UI dependencies. Host adapters bind four allocation-free interfaces at startup ([`Platform.hpp`](../client/core/include/svplus/client/Platform.hpp)):
* **`IControlTransport`:** Queues/clears outgoing control packets (`222`); on PC, `Win32ControlTransport` forwards to `SampBridge` -> `ProxyRakClient`.
* **`IGameState`:** Queries camera basis, ped/player/vehicle/object coordinates, local vehicle membership, cached player names, optional line-of-sight occlusion (`hasOcclusion()`), and legacy string decoding; on PC, `Win32GameState` reads SEH-guarded SA-MP/GTA state via `SampBridge` and converts ANSI (`CP_ACP`) control names to UTF-8.
* **`IKeyInput`:** Polls activation key states; on PC, `Win32KeyInput` queries `GetAsyncKeyState`.
* **`IBassLibrary`:** Manages BASS library readiness and C function table resolution (`BassApi`); on PC, `BassLoader` binds dynamically to SA-MP's loaded `bass.dll` (`LoadLibraryA` / `GetProcAddress`) without hooking `BASS_Init`.

### Single-File Packaging & OS Dependencies
* Static MSVC `/MT` runtime (zero `MSVCP*.dll` / `VCRUNTIME*.dll`), static `libopus` `1.5.2` (48 kHz mono, 100 ms / 4800 samples, in-band FEC + PLC), Dear ImGui `1.91.5` hooked only on `IDirect3DDevice9::Present`/`Reset` and `WndProc`, `.rc` `RCDATA` Noto Sans subset fonts + 19 256×256 RGBA PNG sprites (`stb_image.h`).
* Imports only standard Windows DLLs (`KERNEL32`, `USER32`, `SHELL32`, `IMM32`, `WS2_32`, `bcrypt`, `VERSION`, `d3d9`) and binds dynamically to SA-MP's loaded `bass.dll` without hooking `BASS_Init` or mutating `GVOL_STREAM`, `BASS_BUFFER` permanently (only briefly scoped around `BASS_StreamCreate`), `UPDATETHREADS`, or `BASS_Set3DFactors`.

### Audio Thread, Jitter Buffer & Software Spatialization
* **Background Audio Thread (~10 ms):** Runs capture polling, Opus encode/decode, UDP socket polling/keepalive, jitter draining, `80 Hz` high-pass + RMS balancer DSP, and BASS push submission completely off `Present()`. Focus (`GetForegroundWindow` + `!IsIconic`) checks cached `std::atomic<HWND>`; idle `PlaybackEngine::tick` reuses scratch buffers with zero per-tick heap allocations.
* **SEH-Guarded Render-Thread Snapshot:** Reads GTA SA `TheCamera` (`CPlaceable::m_matrix`: `mat[4..6]` `up`, `mat[8..10]` `front/at`, `mat[12..14]` `pos`) and SA-MP pools (`CGame::m_pPlayerPed`, `CPlayerPool`, `CVehiclePool`, `CObjectPool`) strictly on the render thread inside Win32 `__try/__except` blocks, throttling player name cache updates every `250 ms`.
* **Jitter, Prebuffer & Backlog Catch-Up:** `SpeakerJitterBuffer` reorders frames over a `35 ms` window (`FEC` / `4800`-sample `PLC`). Each `SpeakerChannel` prebuffers `N` (`1..4` dynamic on stalls vs `500` stable frames), accelerates `+6%` above `300 ms` queue backlog, flushes bursts above `800 ms`, displays active speakers up to `1000 ms`, and frees idle BASS channels after `5 s`.
* **Software Attenuation & Stereo Pan:**
  * **Listener Origin ($\mathbf{p}_{\text{listener}}$):** Uses local ped when within $20\text{ m}$ of camera, else camera position.
  * **Distance Gain ($g_{\text{spatial}}$):** With $d_{\max} = \max(1.0, \text{distance})$ and $d_{\min} = 0.20 \cdot d_{\max}$:
    $$g_{\text{spatial}}(d) = \begin{cases} 1.0 & d \le d_{\min} \\ \left(\dfrac{d_{\max} - d}{d_{\max} - d_{\min}}\right)^{1.5} & d_{\min} < d < d_{\max} \\ 0.0 & d \ge d_{\max} \end{cases}$$
    *(On legacy `v11` clients using BASS 3D `min = 0.1 * max`, the server scales transmitted stream distances by `kLegacyDistanceScale = 2.0`, leaving `DL` cuts at the real radius while `SL` holds BASS's quiet floor beyond `max`.)*
  * **Stereo Pan ($p_{\text{spatial}}$):** From $\mathbf{r} = \widehat{\mathbf{f} \times \mathbf{t}}$ and $\hat{\mathbf{u}} = \frac{\mathbf{p}_{\text{source}} - \mathbf{p}_{\text{listener}}}{d}$:
    $$p_{\text{spatial}} = 0.5 \cdot \operatorname{clamp}(\hat{\mathbf{u}} \cdot \mathbf{r},\, -1.0,\, +1.0) \cdot \operatorname{clamp}\!\left(\frac{d}{3.0},\, 0.0,\, 1.0\right)$$
    Applied via `BASS_ATTRIB_PAN` (`50 ms` slide). Final gain `masterVol * channelTypeVol * streamBaseVolume * listenerFactor * playerVol * g_spatial * occGainFactor` is clamped to `[0.0, 4.0]`.
* **Wall Voice Occlusion (`sampvoice.voice_occlusion`):** Round-robin GTA building/object LOS checks ignoring shoot-through fences/scaffolding/cloth (`<= 8` checks/frame, `100 ms` interval per stream, 2-sample hysteresis with instant reset after `> 500 ms` pause, exempting local occupied vehicle and global streams, ignoring self-entity on `LocalAtObject`, `0.5 m` margin on point/object streams) driving a `~1000 Hz` low-pass + `0.5×` (`-6 dB`) gain with `~150 ms` mid-speech ramp.

### Universal SA-MP Bridge & `ProxyRakClient`
`VersionDetector` pairs `samp.dll` PE `TimeDateStamp` and `AddressOfEntryPoint` across all 7 official PC builds (`SampBridge` wraps `CNetGame::m_pRakClient` with `ProxyRakClient` at `+0xDDE` inside a heap block with `+0` shadow RakPeer vtable rather than patching `.text`):

| Version | EntryPoint RVA | PE TimeDateStamp | Strategy |
| :--- | :---: | :---: | :--- |
| **0.3.7-R1** | `0x31DF13` | `0x5542F47A` | Verified per-field `SampLayout` + `SetCursorMode` (`0x9BD30`) |
| **0.3.7-R2** | `0x3195DD` | `0x59C30C94` | Verified per-field `SampLayout` + `SetCursorMode` (`0x9BDD0`) |
| **0.3.7-R3 / R3-1** | `0x0CC4D0` | `0x5C0B4243` | Verified per-field `SampLayout` + `SetCursorMode` (`0x9FFE0`) |
| **0.3.7-R4** | `0x0CBCB0` | `0x5DD606CD` | Verified per-field `SampLayout` + `SetCursorMode` (`0xA0720`) |
| **0.3.7-R4-2** | `0x0CBCD0` | `0x6094ACAB` | Verified per-field `SampLayout` + `SetCursorMode` (`0xA0750`) |
| **0.3.7-R5-1 / R5-2 (incl. R5-2-MP)** | `0x0CBC90` | `0x6372C39E` | Verified per-field `SampLayout` + `SetCursorMode` (`0xA06F0`) |
| **0.3.DL-R1 (incl. DL-R1-2-MP)** | `0x0FDB60` | `0x5A6A3130` | Verified per-field `SampLayout` + `SetCursorMode` (`0xA0530`) |

Unknown client binaries fail closed cleanly; `SetCursorMode(game, 3, FALSE)` unlocks the cursor without `DISCL_EXCLUSIVE`, preserving `VK_LWIN`/`Alt+Tab` and original `CWndProc` chains.

### HUD Rendering, Isolation & Shutdown
* **Overhead 3D Anchoring & Speaker List:** Tracks `CPed::GetBonePosition(8)` (`+ dist * 0.0475 + 0.2`) and `sa-mp.cfg` `fontsize`, positioning icon bottom at `nameTop - 3 px - overhead_icon_height * distScale` (`30 m` visibility when nametag + LOS pass). Local speaker row uses `PlusSpeakerStreams` (`106`), always displaying `positional` streams and showing `global` streams only while also attached as listener.
* **Singleplayer Isolation & Safe Exit:** Polls up to `10 s` (`250 ms` steps) for valid `samp.dll`/`samp.asi`; exits creating zero files, hooks, mutexes, or audio threads if absent. On SA-MP sessions, low-priority background `[INF:mods]` inspects loaded modules and detours; `DllMain(DLL_PROCESS_DETACH)` flushes logs lock-safely via `try_lock` + `WriteFile` and prunes `[ERROR]` crash logs down to the 10 newest.
* **UI Authoring Rule (`MenuLayout`):** Any layout/list dimension routes through `MenuLayout` (`flexWidth`, `flexRows`, `distributeHeights`, `columnSeparator`) instead of fixed pixels so multi-language content never overflows.

### Mobile C SDK (`libsvplus_mobile.so`)
*For step-by-step launcher integration snippets, thread contract tables, and platform limits, see **[Android Launcher Integration Guide](android-integration.md)**.*
* **Threading Model:** `svp_create()` spawns the dedicated `10 ms` audio thread (`CaptureEngine`, `VoiceClient`, `PlaybackEngine`) independently of the game loop. All host game-world/control callbacks (`send_control`, `get_camera`, `get_local_ped_position`, entity positions, optional LOS occlusion, and `decode_text`) run strictly inside `svp_frame()` on the single game thread (with exceptions only for `get_bass_proc` during `svp_create()` and thread-safe `log`). Incoming RakNet `222` control packets and player names pushed from other threads (`svp_on_control_packet()`, `svp_set_player_name()`, `svp_clear_player()`) are queued and applied inside `svp_frame()`.
* **Handshake & Capability Flag:** `svp_build_join_payload()` emits `ConnectPacket` (`version = 11`) + `'SVP1'` advertising `AdaptiveJitter | Telemetry | Mobile`, allowing servers to distinguish mobile SDK clients via `SV_CAP_MOBILE` and `SV_STAT_MOBILE_PLAYERS` while falling back to `v11` on legacy servers cleanly.
* **Launcher-Owned BASS:** Never invokes `BASS_Init`; uses `host->get_bass_proc` when supplied or `dlopen("libbass.so")` (trying `RTLD_NOLOAD` first to reuse the launcher's handle), logging refused DX8 FX once per effect type while keeping audio active.
* **Symbol Isolation & 16 KB ELF Pages:** Built with `ANDROID_STL=c++_static`, hidden visibility, linker version script exporting strictly `svp_*`, `-Wl,--exclude-libs,ALL` (preventing `libopus` symbol collisions with launcher dependencies), and `-Wl,-z,max-page-size=16384`.

---

## 5. Comparison Notes

Detailed technical backing for the feature comparison table in the root [README](../README.md#comparison-sampvoice-implementations):

1. **Protocol & 4.x compatibility:** CyberMor 4.x clients send `0xDEADC0DE` (`v4` only) and are incompatible with `v11`; SampVoice Plus servers accept both `v11` (CyberMor 3.x, AmyrAhmady, Android launchers) and `v12` (`SVP1`) simultaneously while rejecting `v4`.
2. **Server targets:** Ships both an open.mp native component and a SA-MP plugin for `0.3.7-R2`, `0.3.7-R3`, and `0.3.DL-R1` (32-bit Windows & Linux).
3. **PC client coverage:** Strictly matches PE `TimeDateStamp` + `AddressOfEntryPoint` across `0.3.7-R1` through `0.3.7-R5-2-MP`, `0.3.DL-R1`, and open.mp launcher in a single `.asi`.
4. **External relay process:** CyberMor 4.x requires running a separate `voice` process; other implementations integrate UDP forwarding directly.
5. **Client dependencies:** Legacy 3.x/4.x and AmyrAhmady require external `bass_fx.dll` and loose assets; Plus bundles everything into `sampvoice.asi`.
6. **Installer:** Guided 15-language Inno Setup installer with SHA-256 backup and restore manifest.
7. **Anti-cheat safety:** Leaves `samp.dll` machine code unpatched (`Present`/`Reset` Direct3D 9 hooks + `ProxyRakClient`).
8. **`Pawn.RakNet` / `YSF` coexistence:** Upstream 3.x and 4.x keep a permanent `GetRakServerInterface` hook; open.mp components do not use it; Plus removes its one-shot hook immediately after binding.
9. **Network configuration:** 3.x uses an ephemeral port (`sv_voice_port` never released in 3.x binaries); 4.x supports `voice_port` & `voice_host`; AmyrAhmady exposes `sampvoice.port`; Plus configures both `sampvoice.port` and `sampvoice.bind`, failing fast on invalid config.
10. **Flood defense:** Per-player UDP voice bucket (`30 pkt/s`, burst `10`) plus isolated control buckets.
11. **Lifecycle safety:** 3.x and AmyrAhmady use raw pointers leaking on GMX; 4.x uses pool IDs without AMX ownership; Plus pairs generational handles with `ownerAmx` cleanup.
12. **`y_hooks` interoperability:** 3.x caches `AmxLoad` indices only with `SV_VOID: void:`; 4.x/AmyrAhmady keep `void:` forwards; Plus uses empty `SV_VOID:` + cached runtime `amx_FindPublic`.
13. **Stream icons:** `SvSetIcon` in 4.x takes custom PNGs; `SvStreamSetIcon` in Plus (`v12`) selects 7 built-in SVG/PNG sprites.
14. **Mute & listener controls:** 3.x/AmyrAhmady offer mic mute; 4.x offers `SvEnableListener`/`SvEnableSpeaker`; Plus offers global listener toggle, timed key-releasing mic mutes, and per-pair muting.
15. **World filtering:** 3.x/4.x check streamed-in & distance only; AmyrAhmady checks interior/VW without spectate tracking; Plus filters interior, VW, and active spectate target.
16. **Stuck speaker guard:** Legacy 3.x/AmyrAhmady have known stale entries; 4.x clears on `OnStopStream`; Plus enforces a **1 s (`1000 ms`) active-speaker display hold**, **5 s idle BASS channel free**, and disconnect/deferred detach cleanup.
17. **`PlayAudioStream` stereo:** 3.x/AmyrAhmady/4.x mutate BASS globals (`GVOL_STREAM`, `BUFFER`, etc.); Plus leaves `BASS_Init` and globals untouched.
18. **UI translations:** 3.x/AmyrAhmady are Russian-only; 4.x has 5 languages (`en` + `.txt`); Plus embeds 15 languages with real-time switching.
19. **DSP effects:** All 9 DX8 audio filters via `SvEffectCreate*` and live `SvEffectUpdate*`.
20. **Client UX:** Configurable hotkey, 9-state F11 diagnostics + RTT, 2 s mic hot-plug detection, PTT + Toggle modes.
21. **Recovery pipeline:** 3.x/AmyrAhmady decode FEC and drop late frames; 4.x uses ring + FEC + PLC; Plus pairs a `35 ms` `SpeakerJitterBuffer`, Opus FEC, 4800-sample PLC, and adaptive `1..4`-frame prebuffer.

---

## 6. Windows Installer Internals (`SampVoicePlus.iss`)

* Installs `sampvoice.asi` alongside Ultimate ASI Loader (`dinput8.dll`, verified against expected SHA-256 `73D6DB0481AF99B8481E8E6A685DD385E5A3E0E10518BFE97A993F7F368667EB`) when no ASI loader exists.
* Registers under AppId `{8F3B4D21-7C9E-4A52-B108-6E4F9D2C51A7}_is1`, placing `unins000.*`, `manifest.ini`, and `THIRD-PARTY-NOTICES.txt` in `<GTA>\sampvoice-plus\` and `Uninstall SampVoice Plus.lnk` in the GTA root.
* **Legacy Handling:** Detects manual files (`sampvoice.asi`, `scripts\sampvoice*.asi`, `sampvoice\`, `bass_fx.dll`) or legacy setup AppId `{01EA93A6-5255-44FE-ADB9-FDC3EFE89675}_is1`. Default **Backup** mode copies exclusive legacy files (preserving shared `bass.dll`, `vorbisFile.dll`, `vorbisHooked.dll`) into `sampvoice-plus\backup\` and silently invokes the legacy uninstaller so it cannot remove Plus later; uninstalling Plus restores the backed-up legacy client cleanly.

---

## 7. Build & CI Notes

* **Windows (`build_msvc.bat` & CI):** 32-bit `Win32` MSVC static CRT (`/MT`), zero dynamic runtime checks, and optional Inno Setup installer. The demo filterscript (`filterscripts/FS_SampVoicePlus_Demo.pwn` and its pre-compiled `.amx`) is versioned directly in the repository and copied as-is into the Windows and Linux server packages.
* **Linux (`debian:bullseye`, `-m32`) & Dual-Platform CI:** Produces `sampvoice.so` for SA-MP & open.mp targeting `glibc <= 2.31` with hidden symbol exports, and runs 45 s SA-MP `0.3.7-R2` + `Pawn.RakNet` + `0.3.DL-R1` smoke tests against the compiled server plugin.
