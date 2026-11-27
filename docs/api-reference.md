# Complete API Reference (`sampvoice.inc`)

Exhaustive Pawn reference for all **107 natives**, **8 callbacks**, enums, handle tags, and constants defined in [`sampvoice.inc`](../sampvoice.inc).

> [!NOTE]
> **Client Compatibility Legend (`Client` column):**
> * **All clients** — works on legacy `CyberMor 3.x`, `AmyrAhmady`, Android 3.x launchers, and all SampVoice Plus versions.
> * **Plus `1.0.0+` / `1.1.0+` / `1.2.0+` / `1.3.0+`** — enhanced client feature requiring that desktop `sampvoice.asi` version or newer (older clients stay connected without errors; where applicable, the note explains fallback behavior).
>
> For packet layouts, rate limits, and wire-level fallback behavior, see **[Under the Hood — Wire Protocol](under-the-hood.md#3-wire-protocol-v11--v12)**.

---

## Quick Index
* [1. Version Constants & Macros](#1-version-constants--macros)
* [2. Enumerations (`SV_PARAMETER`)](#2-enumerations)
* [3. Core, Client Protocol & Statistics Natives](#3-core-client-protocol--statistics-natives)
* [4. Player Microphone, Activation Keys, Mutes & Listener Control](#4-player-microphone-activation-keys-mutes--listener-control)
* [5. Stream Creation & Lifecycle Natives](#5-stream-creation--lifecycle-natives)
* [6. Stream Parameter Natives](#6-stream-parameter-natives)
* [7. DX8 Audio Effect Natives](#7-dx8-audio-effect-natives)
* [8. Pawn Callbacks (`8` total)](#8-pawn-callbacks-8-total)

---

## 1. Version Constants & Macros

```pawn
#define SV_PLUS_VERSION_MAJOR   1
#define SV_PLUS_VERSION_MINOR   4
#define SV_PLUS_VERSION_PATCH   0
#define SV_PLUS_VERSION_NUMBER  10400
#define SV_PLUS_VERSION_STRING  "1.4.0"

#define SV_VERSION              11  // Legacy SampVoice 3.x protocol
#define SV_PLUS_VERSION         12  // SampVoice Plus extended protocol

#define SV_CAP_ADAPTIVE_JITTER  (1 << 1)
#define SV_CAP_TELEMETRY        (1 << 2)
#define SV_CAP_MOBILE           (1 << 4)

#define SV_VOICE_STATUS_OK          0
#define SV_VOICE_STATUS_NO_MICRO    1
#define SV_VOICE_STATUS_CANNOT_HEAR 2

#define SV_STREAM_TYPE_GLOBAL       0
#define SV_STREAM_TYPE_SL_POINT     1
#define SV_STREAM_TYPE_SL_VEHICLE   2
#define SV_STREAM_TYPE_SL_PLAYER    3
#define SV_STREAM_TYPE_SL_OBJECT    4
#define SV_STREAM_TYPE_DL_POINT     5
#define SV_STREAM_TYPE_DL_VEHICLE   6
#define SV_STREAM_TYPE_DL_PLAYER    7
#define SV_STREAM_TYPE_DL_OBJECT    8

#define SV_NULL                 0
#define SV_INFINITY             -1
#define SV_FALSE                false
#define SV_TRUE                 true

#define SV_STAT_COUNT           30
#define SV_PSTAT_COUNT          8
```

*(Full `SV_STAT_*` (`0..29`) and `SV_PSTAT_*` (`0..7`) indices and units are listed in [Server & Player Statistics](#server-statistics-constants-sv_stat_-since-140).)*

---

## 2. Enumerations

```pawn
enum SV_PARAMETER
{
    SV_PARAMETER_FREQUENCY = 1,
    SV_PARAMETER_VOLUME    = 2,
    SV_PARAMETER_PANNING   = 3,
    SV_PARAMETER_EAXMIX    = 4,
    SV_PARAMETER_SRC       = 8
}
```

---

## 3. Core, Client Protocol & Statistics Natives

| Native | Description | Return / Invalid Cases | Since | Client |
| :--- | :--- | :--- | :---: | :---: |
| `SvDebug(bool:mode)` | Toggles verbose server logging (`[sv+:DBG]`). Enabling logs the resolved `Effective config` summary once. *(Declared `SV_VOID:`.)* | `1` when applied; `0` if voice core is inactive. | `1.0.0` | Server only |
| `SvInit(bitrate)` | Configures target Opus voice bitrate in bits/sec (default `24000`, valid range `6000`–`128000`; `0` resets to `24000`; out-of-range clamps with a `[sv+:WRN]`). Only applies to players connecting after the call (use in `OnGameModeInit`; disconnect resets to `24000`). *(Declared `SV_VOID:`.)* | `1` when applied; `0` if voice core is inactive. | `1.0.0` | Enforced on `Plus 1.1.0+` |
| `SvGetVersion(playerid)` | Returns the client's raw connect protocol version (`11` for both legacy `v11` and Plus `v12` PC clients). Use `SvGetClientProtocol` to tell legacy vs Plus apart. | Version number, or `0` (`SV_NULL`) if disconnected or no plugin. | `1.0.0` | All clients |
| `SvGetClientProtocol(playerid)` | Distinguishes legacy `v11` clients (`11`) from SampVoice Plus `v12` clients (`12`). | `11`, `12`, or `0` if disconnected or no plugin. | `1.0.0` | All clients |
| `SvGetPlusCapabilities(playerid)` | Returns the `SV_CAP_*` bitmask advertised by a Plus client (e.g. `SV_CAP_ADAPTIVE_JITTER \| SV_CAP_TELEMETRY \| SV_CAP_MOBILE`). | Bitmask integer, or `0` for non-Plus / disconnected players. | `1.0.0` | `Plus 1.0.0+` |
| `SvGetPlusClientVersion(playerid)` | Returns the packed Plus SDK/client version as `MAJOR*10000 + MINOR*100 + PATCH` (e.g. `10000`, `10100`, `10200`, `10300`, `10400`; Android launchers using the Plus C SDK also report their SDK version). Compare with `SV_PLUS_VERSION_NUMBER` to prompt updates. | Encoded version `> 0`, or `0` for legacy `v11` (including Android `v11`), no plugin, or disconnected. | `1.2.0` | `Plus 1.0.0+` |
| `bool:SvIsPlayerVoiceReady(playerid)` | Returns whether the player's voice UDP channel finished connecting and received startup sync (safe to poll after GMX reloads). | `true` when ready; `false` if disconnected, missing plugin, or waiting on UDP. | `1.2.0` | All clients |
| `SvGetServerStats(stats[], size = sizeof stats)` | Copies server-wide voice runtime metrics indexed by `SV_STAT_*` (`0 .. SV_STAT_COUNT - 1`) up to `size` cells. Passing a smaller `size` fills only the prefix safely. | Number of cells written (`<= size`); `0` on bad array / `size <= 0`. | `1.4.0` | All clients |
| `SvGetPlayerStats(playerid, stats[], size = sizeof stats)` | Copies per-player voice statistics indexed by `SV_PSTAT_*` (`0 .. SV_PSTAT_COUNT - 1`) up to `size` cells. Counters reset cleanly on disconnect. | Number of cells written (`<= size`); `0` if player invalid/disconnected or bad array. | `1.4.0` | All clients |

### Server Statistics Constants (`SV_STAT_*`, `Since 1.4.0`)

| Index | Constant | Unit | Meaning |
| :---: | :--- | :---: | :--- |
| `0` | `SV_STAT_UPTIME` | `s` | Seconds elapsed since voice server start. |
| `1` | `SV_STAT_WORKER_THREADS` | count | Active voice UDP forwarding worker threads. |
| `2` | `SV_STAT_PLUGIN_PLAYERS` | players | Connected players carrying any voice plugin (`v11` or `v12`). |
| `3` | `SV_STAT_PLUS_PLAYERS` | players | Connected SampVoice Plus (`v12`) players (including mobile SDK clients). |
| `4` | `SV_STAT_LEGACY_PLAYERS` | players | Connected legacy `v11` players (Android launchers included). |
| `5` | `SV_STAT_VOICE_READY_PLAYERS` | players | Players whose voice UDP handshake completed (`SvIsPlayerVoiceReady`). |
| `6` | `SV_STAT_SPEAKING_PLAYERS` | players | Players currently transmitting accepted voice (`SvIsPlayerSpeaking`). |
| `7` | `SV_STAT_STREAMS_TOTAL` | streams | Total active streams in the pool. |
| `8` | `SV_STAT_STREAMS_GLOBAL` | streams | Active Global (`SV_STREAM_TYPE_GLOBAL`) streams. |
| `9` | `SV_STAT_STREAMS_SL` | streams | Active Static Local (`SV_STREAM_TYPE_SL_*`) streams. |
| `10` | `SV_STAT_STREAMS_DL` | streams | Active Dynamic Local (`SV_STREAM_TYPE_DL_*`) streams. |
| `11` | `SV_STAT_EFFECTS_TOTAL` | effects | Total active DX8 effects in the pool. |
| `12` | `SV_STAT_STREAM_LISTENERS` | count | Sum of currently attached listener slots across all streams. |
| `13` | `SV_STAT_STREAM_SPEAKERS` | count | Sum of currently attached speaker slots across all streams. |
| `14` | `SV_STAT_RX_VOICE_PACKETS` | packets | Total accepted incoming client voice UDP packets (clamped at `INT_MAX`). |
| `15` | `SV_STAT_TX_VOICE_PACKETS` | packets | Total forwarded listener voice UDP packets (clamped at `INT_MAX`). |
| `16` | `SV_STAT_RX_VOICE_PPS` | pkt/s | Incoming voice packets per second over the last completed 1 s interval. |
| `17` | `SV_STAT_TX_VOICE_PPS` | pkt/s | Outgoing voice packets per second over the last completed 1 s interval. |
| `18` | `SV_STAT_RX_VOICE_BPS` | B/s | Incoming voice UDP bytes per second over the last completed 1 s interval. |
| `19` | `SV_STAT_TX_VOICE_BPS` | B/s | Outgoing voice UDP bytes per second over the last completed 1 s interval. |
| `20` | `SV_STAT_DROP_VOICE_FLOOD` | packets | Total incoming voice packets dropped by per-player anti-flood. |
| `21` | `SV_STAT_DROP_CONTROL_FLOOD` | packets | Total control packets dropped by per-player anti-flood. |
| `22` | `SV_STAT_INVALID_PACKETS` | packets | Total invalid UDP packets rejected (size, CRC32C, unknown key, invalid type). |
| `23` | `SV_STAT_NAT_PORT_UPDATES` | count | Total NAT UDP source port rebinds detected for connected sessions. |
| `24` | `SV_STAT_CONTROL_QUEUE_DROPS` | packets | Total control events dropped because the incoming queue was full. |
| `25` | `SV_STAT_PENDING_DELETES` | entries | Pending deferred `v11` `DeleteStream` transmissions. |
| `26` | `SV_STAT_PENDING_RECREATES` | entries | Pending deferred `v11` / pre-`1.3.0` stream recreations (including active `1000 ms` `v11` pauses). |
| `27` | `SV_STAT_TICK_AVG_US` | `us` | Average `ServerCore::Tick` duration over the last `60 s` window (microseconds). |
| `28` | `SV_STAT_TICK_MAX_US` | `us` | Maximum `ServerCore::Tick` duration observed in the last `60 s` window (microseconds). |
| `29` | `SV_STAT_MOBILE_PLAYERS` | players | Connected SampVoice Plus (`v12`) mobile clients advertising `SV_CAP_MOBILE` (subset of `SV_STAT_PLUS_PLAYERS`). |

*(Total size constant: `SV_STAT_COUNT = 30`.)*

### Player Statistics Constants (`SV_PSTAT_*`, `Since 1.4.0`)

| Index | Constant | Unit | Meaning |
| :---: | :--- | :---: | :--- |
| `0` | `SV_PSTAT_RX_VOICE_PACKETS` | packets | Total accepted voice UDP packets received from this player. |
| `1` | `SV_PSTAT_TX_VOICE_PACKETS` | packets | Total voice UDP packets forwarded to this player. |
| `2` | `SV_PSTAT_DROP_VOICE_FLOOD` | packets | Voice UDP packets dropped for this player by anti-flood. |
| `3` | `SV_PSTAT_DROP_CONTROL_FLOOD` | packets | Control packets dropped for this player by anti-flood. |
| `4` | `SV_PSTAT_LAST_VOICE_MS` | `ms` | Milliseconds elapsed since this player's last accepted voice packet (`-1` if never spoke). |
| `5` | `SV_PSTAT_VOICE_LATENCY_MS` | `ms` | Latest control RTT reported by `Plus 1.3.0+` (matches `SvGetPlayerVoiceLatency`, `-1` otherwise). |
| `6` | `SV_PSTAT_LISTENER_STREAMS` | streams | Number of streams the player is currently listening to. |
| `7` | `SV_PSTAT_SPEAKER_STREAMS` | streams | Number of streams the player is currently speaking into. |

*(Total size constant: `SV_PSTAT_COUNT = 8`.)*

---

## 4. Player Microphone, Activation Keys, Mutes & Listener Control

| Native | Description | Return / Invalid Cases | Since | Client |
| :--- | :--- | :--- | :---: | :---: |
| `bool:SvHasMicro(playerid)` | Checks whether `playerid` currently has an active microphone (`Plus` updates live when mic is toggled/unplugged). | `true` if available; `false` if unavailable, invalid, or disconnected. | `1.0.0` | All clients (`live`: `Plus 1.0.0+`) |
| `bool:SvIsPlayerSpeaking(playerid)` | Checks whether the server is actively accepting voice frames from `playerid` for at least one speaker stream (`false` after `> 1000 ms` of silence/mute). Persists across GMX; clears silently on disconnect. | `true` while speaking; `false` otherwise. | `1.2.0` | All clients |
| `SvGetPlayerVoiceStatus(playerid)` | Queries the player's voice state independently of `sampvoice.voice_status`. | `SV_VOICE_STATUS_OK` (`0`), `NO_MICRO` (`1`), `CANNOT_HEAR` (`2` — no plugin or `SvDisableListener`), or `-1` if invalid/disconnected. | `1.2.0` | All clients |
| `SvGetPlayerVoiceLatency(playerid)` | Returns the player's latest reported SampVoice SA-MP control-channel round-trip latency in ms (same value shown in `F11`, including server tick wait; reset on disconnect). | `>= 0` ms, or `-1` if invalid/disconnected, older than `1.3.0`, or not yet measured. | `1.3.0` | `Plus 1.3.0+` |
| `bool:SvStartRecord(playerid)` | Instructs the client to start transmitting voice continuously without holding a key. | `true` on valid active voice player; `false` if invalid/disconnected/no plugin. | `1.0.0` | All clients |
| `bool:SvStopRecord(playerid)` | Stops programmatic recording started by `SvStartRecord`. | `true` on valid active voice player; `false` otherwise. | `1.0.0` | All clients |
| `bool:SvAddKey(playerid, keyid)` | Binds a Win32 Virtual-Key code (e.g. `0x42` `'B'`) to trigger key callbacks. *(Android 3.x launchers ignore this and always send `0x42`.)* | `true` if added; `false` if already bound or player invalid/no plugin. | `1.0.0` | All clients |
| `bool:SvHasKey(playerid, keyid)` | Checks whether `keyid` is bound for `playerid`. | `true` if bound; `false` otherwise. | `1.0.0` | All clients |
| `bool:SvRemoveKey(playerid, keyid)` | Unbinds `keyid` from `playerid`. If held, queues synthetic `OnPlayerActivationKeyRelease` on next tick. | `true` if removed; `false` if not bound or invalid/no plugin. | `1.0.0` | All clients |
| `SvRemoveAllKeys(playerid)` | Unbinds every activation key and removes all key-to-stream bindings for `playerid`, firing next-tick releases for held keys. *(Declared `SV_VOID:`.)* | `1` if valid player with voice plugin; `0` otherwise. | `1.0.0` | All clients |
| `bool:SvBindKeyToStream(playerid, keyid, SV_STREAM:stream)` | Automatically attaches `playerid` as speaker to `stream` on `keyid` press and detaches on release (registers `keyid` if not already registered). Supports N:M bindings (max `32` per player). If `keyid` is already held when bound, `playerid` is attached immediately. Script-managed speaker attachments before binding or key press stay attached after release; `OnPlayerActivationKeyPress`/`Release` still fires normally. *(Android `0x42` works as expected.)* | `true` if bound; `false` if invalid player/stream, or `32`-binding cap reached (`[sv+:WRN]`). | `1.4.0` | All clients |
| `bool:SvUnbindKeyFromStream(playerid, keyid, SV_STREAM:stream)` | Removes one `(keyid, stream)` binding without unregistering `keyid`. If `keyid` was held and no other held bound key targets `stream`, detaches binding-owned speaker state. | `true` if unbound; `false` if not bound or invalid player. | `1.4.0` | All clients |
| `bool:SvIsKeyBoundToStream(playerid, keyid, SV_STREAM:stream)` | Checks whether `(keyid, stream)` is currently bound for `playerid`. | `true` if bound and stream valid; `false` otherwise. | `1.4.0` | All clients |
| `SvUnbindAllKeys(playerid)` | Removes all key-to-stream bindings for `playerid` while leaving registered keys intact (detaches any currently binding-attached speaker streams). | Number of removed bindings (`0` if none or invalid player). | `1.4.0` | All clients |
| `bool:SvMutePlayerStatus(playerid)` | Checks whether `playerid` is server-muted from speaking. | `true` if server-muted; `false` otherwise. | `1.0.0` | All clients |
| `SvMutePlayerEnable(playerid, bool:releasekeys = false, time = 0)` | Mutes `playerid` server-side. Optional `releasekeys = true` clears held keys immediately (`OnPlayerActivationKeyRelease`) and latches keys until physical release (`Plus 1.1.0+`). Optional `time > 0` ms auto-unmutes up to `86400000` ms (`24 h` cap with warning; `0` = indefinite). Repeating while muted replaces duration without re-firing `OnPlayerVoiceMute`. *(Backward compatible with 1-arg scripts; declared `SV_VOID:`.)* | `1` if applied; `0` if invalid/disconnected/no plugin. | `1.0.0` (`releasekeys`/`time`: `1.1.0`) | All clients (`latch`: `Plus 1.1.0+`) |
| `SvMutePlayerDisable(playerid)` | Unmutes `playerid`, cancels any timed mute, and fires next-tick `OnPlayerVoiceUnmute(..., false)` if muted. *(Declared `SV_VOID:`.)* | `1` if transitioning from muted -> unmuted; `0` otherwise. | `1.0.0` | All clients |
| `SvMutePlayerRemaining(playerid)` | Returns remaining duration of a timed mute. | `> 0` ms remaining, `0` if muted indefinitely, or `-1` if not muted / invalid. | `1.1.0` | All clients |
| `bool:SvMutePlayerForListener(speakerid, listenerid)` | Mutes `speakerid` solely for `listenerid` without touching streams, `SvIsPlayerSpeaking`, or key callbacks. Cleared on either player's disconnect. | `true` if changed; `false` if IDs equal/invalid/disconnected or already muted. | `1.2.0` | All clients |
| `bool:SvUnmutePlayerForListener(speakerid, listenerid)` | Removes a per-pair mute set by `SvMutePlayerForListener`. | `true` if cleared; `false` if not muted or invalid/equal/disconnected IDs. | `1.2.0` | All clients |
| `bool:SvIsPlayerMutedForListener(speakerid, listenerid)` | Checks per-pair mute status between `speakerid` and `listenerid`. | `true` if pair-muted; `false` otherwise. | `1.2.0` | All clients |
| `bool:SvEnableListener(playerid)` | Re-enables global voice reception after `SvDisableListener`. | `true` on transition disabled -> enabled; `false` otherwise. | `1.0.0` | All clients |
| `bool:SvDisableListener(playerid)` | Suppresses all inbound voice audio for `playerid` server-side while keeping stream memberships (`F11` displays *"listening is disabled by the server"* on Plus clients). | `true` on transition enabled -> disabled; `false` otherwise. | `1.0.0` | All clients |
| `bool:SvCheckListener(playerid)` | Checks whether `playerid` has voice listening enabled (`true` by default). | `true` if listening enabled; `false` if disabled or invalid. | `1.0.0` | All clients |

---

## 5. Stream Creation & Lifecycle Natives

> [!NOTE]
> * **Float validation:** Non-finite floats (`NaN`, `±Inf`) or negative `distance` (`< 0.0`) return `0` (`SV_NULL`) with a rate-limited `[sv+:WRN]`.
> * **Stream colors:** Standard Pawn colors use **`0xRRGGBBAA`** (`0xFF0000FF` = red, `0` = no tag) unless `sampvoice.legacy_color_format` is enabled (`0xAARRGGBB`).

### Stream Creation (`9` Natives)

| Native | Description | Return / Invalid Cases | Since | Client |
| :--- | :--- | :--- | :---: | :---: |
| `SV_GSTREAM:SvCreateGStream(color = SV_NULL, const name[] = "", priority = 0)` | Creates a 2D global voice channel (`name` max 128 bytes). Optional `priority` (`0` = no priority; `1` = highest). Listeners must be attached manually. | Handle `!= 0`, or `SV_NULL` on failure / negative `priority`. | `1.0.0` (`priority`: `1.4.0`) | All clients |
| `SV_LPSTREAM:SvCreateSLStreamAtPoint(Float:distance, Float:posx, Float:posy, Float:posz, color = SV_NULL, const name[] = "", priority = 0)` | Creates a static 3D local stream at fixed world coordinates. Listeners must be attached manually. | Handle `!= 0`, or `SV_NULL` if floats invalid/negative distance or negative `priority`. | `1.0.0` (`priority`: `1.4.0`) | All clients |
| `SV_SLSTREAM:SvCreateSLStreamAtVehicle(Float:distance, vehicleid, color = SV_NULL, const name[] = "", priority = 0)` | Creates a static 3D local stream following standard SA-MP `vehicleid`. | Handle `!= 0`, or `SV_NULL` on invalid distance/ID/priority. | `1.0.0` (`priority`: `1.4.0`) | All clients |
| `SV_SLSTREAM:SvCreateSLStreamAtPlayer(Float:distance, playerid, color = SV_NULL, const name[] = "", priority = 0)` | Creates a static 3D local stream following `playerid`. | Handle `!= 0`, or `SV_NULL` on invalid distance/ID/priority. | `1.0.0` (`priority`: `1.4.0`) | All clients |
| `SV_SLSTREAM:SvCreateSLStreamAtObject(Float:distance, objectid, color = SV_NULL, const name[] = "", priority = 0)` | Creates a static 3D local stream following **standard GTA global object** (`CreateObject`). *(See Streamer note below.)* | Handle `!= 0`, or `SV_NULL` on invalid distance/ID/priority. | `1.0.0` (`priority`: `1.4.0`) | All clients |
| `SV_LPSTREAM:SvCreateDLStreamAtPoint(Float:distance, maxplayers, Float:posx, Float:posy, Float:posz, color = SV_NULL, const name[] = "", worldid = -1, interiorid = -1, priority = 0)` | Creates a dynamic 3D stream at `(posx, posy, posz)` with automatic listener attachment (`maxplayers` = max concurrent listeners on this stream; `-1` = unlimited). Optional `worldid`/`interiorid` (`-1` = any; `< -1` rejected) and `priority`. | Handle `!= 0`, or `SV_NULL` on invalid floats, negative distance/priority, or VW/interior `< -1`. | `1.0.0` (`VW/int`: `1.3.0`, `prio`: `1.4.0`) | All clients |
| `SV_DLSTREAM:SvCreateDLStreamAtVehicle(Float:distance, maxplayers, vehicleid, color = SV_NULL, const name[] = "", priority = 0)` | Creates a dynamic 3D stream tracking standard SA-MP `vehicleid`. | Handle `!= 0`, or `SV_NULL` on invalid inputs/priority. | `1.0.0` (`priority`: `1.4.0`) | All clients |
| `SV_DLSTREAM:SvCreateDLStreamAtPlayer(Float:distance, maxplayers, playerid, color = SV_NULL, const name[] = "", priority = 0)` | Creates a dynamic 3D stream tracking `playerid` (matching player VW & interior automatically). | Handle `!= 0`, or `SV_NULL` on invalid inputs/priority. | `1.0.0` (`priority`: `1.4.0`) | All clients |
| `SV_DLSTREAM:SvCreateDLStreamAtObject(Float:distance, maxplayers, objectid, color = SV_NULL, const name[] = "", worldid = -1, interiorid = -1, priority = 0)` | Creates a dynamic 3D stream tracking **standard GTA global object** (`CreateObject`). Optional `worldid`/`interiorid` (`-1` = any) and `priority`. | Handle `!= 0`, or `SV_NULL` on invalid distance/ID/priority or VW/interior `< -1`. | `1.0.0` (`VW/int`: `1.3.0`, `prio`: `1.4.0`) | All clients |

> [!IMPORTANT]
> **Standard GTA Objects vs Streamer Dynamic Objects:** `SvCreateSLStreamAtObject` and `SvCreateDLStreamAtObject` only work with standard global object IDs from **`CreateObject`**, because both server and client look up that ID in SA-MP's global object table. Streamer objects (`CreateDynamicObject`) use internal plugin IDs that map to changing per-player object slots, causing silence or audio on the wrong object. For Streamer objects, use **`SvCreateDLStreamAtPoint`** with the object's coordinates (`worldid`, `interiorid`) and update moving objects cleanly via a timer calling `SvUpdatePositionForLPStream`.

### Stream Control & Inspection (`19` Natives)

| Native | Description | Return / Invalid Cases | Since | Client |
| :--- | :--- | :--- | :---: | :---: |
| `SvUpdateDistanceForLStream(SV_LSTREAM:lstream, Float:distance)` | Changes audible radius of a local stream (`>= 0.0`). *(Declared `SV_VOID:`.)* | `1` if applied; `0` if global/invalid handle or invalid float. | `1.0.0` | All clients |
| `SvUpdatePositionForLPStream(SV_LPSTREAM:lpstream, Float:posx, Float:posy, Float:posz)` | Moves a **point-based** local stream (`SL_POINT` / `DL_POINT`) to new coordinates. Fails cleanly on entity/global streams. *(Declared `SV_VOID:`.)* | `1` if applied; `0` if non-point/invalid handle or non-finite coords. | `1.0.0` | All clients |
| `bool:SvSetStreamPriority(SV_STREAM:stream, priority = 0)` | Sets channel ducking priority (`0` = none; `1` = highest, `2` lower than `1`, ...). When an unmuted speaker (`!= listener`) speaks in a higher-priority channel, lower-priority channels heard by that listener duck cleanly (`sampvoice.priority_duck`). | `true` if valid stream & non-negative priority; `false` otherwise. | `1.4.0` | All clients |
| `SvGetStreamPriority(SV_STREAM:stream)` | Queries configured priority on `stream` (`0` = default). | Priority `>= 0`, or `-1` if invalid stream. | `1.4.0` | All clients |
| `bool:SvStreamSetIcon(SV_STREAM:stream, const icon[])` | Sets HUD icon for Speaker List & 3D overhead: `"speaker"` (default), `"radio"`, `"phone"`, `"megaphone"`, `"police"`, `"admin"`, or `"global"`. Unknown names log one warning and fall back to `"speaker"`. | `true` if stream valid; `false` if invalid stream or AMX string. | `1.0.0` | `Plus 1.0.0+` |
| `bool:SvStreamSetName(SV_STREAM:stream, const name[])` | Renames stream at runtime (max 128 bytes) without changing handle. Same name sends zero network updates. Plus `1.3.0+` updates tags live without audio restarts; older clients refresh cleanly during silence. | `true` if valid stream; `false` on invalid handle or bad AMX address. | `1.3.0` | `live`: `Plus 1.3.0+` (fallback older) |
| `bool:SvStreamSetColor(SV_STREAM:stream, color)` | Recolors stream at runtime using server's `legacy_color_format` conversion. Same raw color sends zero network updates. | `true` if valid stream; `false` on invalid handle. | `1.3.0` | `live`: `Plus 1.3.0+` (fallback older) |
| `bool:SvStreamGetName(SV_STREAM:stream, name[], size = sizeof name)` | Copies stream name into `name`. | `true` on success; `false` without writing if invalid `stream`, `name`, or `size <= 0`. | `1.3.0` | Server only |
| `SvStreamGetColor(SV_STREAM:stream)` | Returns raw color integer passed by script (`SvCreate*` / `SvStreamSetColor`). | Color integer, or `0` if invalid stream. | `1.3.0` | Server only |
| `SvGetStreamType(SV_STREAM:stream)` | Returns one of `SV_STREAM_TYPE_GLOBAL` (`0`) or `SV_STREAM_TYPE_{SL,DL}_{POINT,VEHICLE,PLAYER,OBJECT}` (`1..8`). | `0..8`, or `-1` if invalid stream. | `1.3.0` | Server only |
| `Float:SvGetStreamDistance(SV_STREAM:stream)` | Returns hearing radius of a local stream. | Float distance, or `-1.0` if global or invalid stream. | `1.3.0` | Server only |
| `bool:SvGetStreamPosition(SV_STREAM:stream, &Float:x, &Float:y, &Float:z)` | Outputs world coordinates of a point stream (`SL_POINT` / `DL_POINT`). | `true` on point stream; `false` without altering outputs otherwise. | `1.3.0` | Server only |
| `SvGetStreamTarget(SV_STREAM:stream)` | Returns attached `vehicleid`, `playerid`, or `objectid`. | Entity ID `>= 0`, or `-1` if point, global, or invalid stream. | `1.3.0` | Server only |
| `SvGetStreamMaxPlayers(SV_STREAM:stream)` | Returns `maxplayers` capacity of a DL stream (including `-1` `SV_INFINITY`). | Capacity integer, or `-1` if static, global, or invalid. | `1.3.0` | Server only |
| `bool:SvStreamSetWorld(SV_STREAM:stream, worldid, interiorid)` | Updates virtual world and interior filters on `DL_POINT` or `DL_OBJECT` streams (`-1` = unfiltered; `< -1` rejected with warning). Listeners update next tick. | `true` if updated; `false` on wrong stream type, invalid handle, or `< -1`. | `1.3.0` | All clients |
| `SvGetStreamWorld(SV_STREAM:stream)` | Queries configured virtual world filter. | World ID, `-1` if unfiltered/non-VW stream, or `-2` if invalid stream. | `1.3.0` | Server only |
| `SvGetStreamInterior(SV_STREAM:stream)` | Queries configured interior filter. | Interior ID, `-1` if unfiltered/non-interior stream, or `-2` if invalid stream. | `1.3.0` | Server only |
| `SvDeleteStream(SV_STREAM:stream)` | Deletes an active stream and cleans its handle. *(Declared `SV_VOID:`.)* | `1` if active & deleted; `0` if already deleted or invalid. | `1.0.0` | All clients |
| `bool:SvIsValidStream(SV_STREAM:stream)` | Checks whether `stream` resolves to a live stream (`0` / deleted / stale handles return `false`). | `true` if active; `false` otherwise. | `1.2.0` | Server only |

### Stream Listeners & Speakers (`16` Natives)

| Native | Description | Return / Invalid Cases | Since | Client |
| :--- | :--- | :--- | :---: | :---: |
| `bool:SvAttachListenerToStream(SV_STREAM:stream, playerid, Float:volume = 1.0)` | Attaches `playerid` as listener with optional per-listener factor (`[0.0, 4.0]`; 2-arg `.amx` passes `1.0`). On DL streams, clears manual detach block and saves `volume` for future auto-attaches even while out of range (`false` if out of range now). | `true` if attached now; `false` if already attached, invalid, or negative/`NaN` volume. | `1.0.0` (`volume`: `1.2.0`) | All clients |
| `bool:SvStreamSetListenerVolume(SV_STREAM:stream, playerid, Float:volume)` | Multiplies base `SV_PARAMETER_VOLUME` by per-listener factor (`[0.0, 4.0]`; `> 4.0` clamps). On global/SL, requires current attachment; on DL, works for any connected player and persists across range transitions. | `true` if applied; `false` on negative/`NaN` volume or invalid/unattached player. | `1.2.0` | All (`v11` uncapped `>1.0`; Plus capped `4.0`) |
| `Float:SvStreamGetListenerVolume(SV_STREAM:stream, playerid)` | Queries per-listener volume multiplier (`1.0` default). | Factor `>= 0.0`, or `-1.0` if stream/player invalid/disconnected. | `1.2.0` | Server only |
| `SvGetStreamListenerCount(SV_STREAM:stream)` | Counts currently attached listeners on `stream`. | Count `>= 0`, or `-1` if invalid stream. | `1.2.0` | Server only |
| `SvGetStreamListeners(SV_STREAM:stream, listeners[], size = sizeof listeners)` | Writes attached listener IDs in entry order up to `size`. | Number of IDs written; `0` if invalid stream/array or `size <= 0`. | `1.3.0` | Server only |
| `bool:SvHasListenerInStream(SV_STREAM:stream, playerid)` | Checks whether `playerid` is currently attached as listener. | `true` if listening; `false` otherwise. | `1.0.0` | Server only |
| `bool:SvDetachListenerFromStream(SV_STREAM:stream, playerid)` | Detaches listener (`DL` streams stay blocked from auto re-attach until `SvAttachListenerToStream` or disconnect). | `true` if detached; `false` if not attached or invalid. | `1.0.0` | All clients |
| `SvDetachAllListenersFromStream(SV_STREAM:stream)` | Detaches every listener from `stream`. *(Declared `SV_VOID:`.)* | `1` if valid stream; `0` if invalid. | `1.0.0` | All clients |
| `bool:SvAttachSpeakerToStream(SV_STREAM:stream, playerid)` | Allows `playerid` to transmit into `stream`. | `true` if attached; `false` if already attached or invalid. | `1.0.0` | All clients |
| `SvGetStreamSpeakerCount(SV_STREAM:stream)` | Counts currently attached speakers on `stream`. | Count `>= 0`, or `-1` if invalid stream. | `1.2.0` | Server only |
| `SvGetStreamSpeakers(SV_STREAM:stream, speakers[], size = sizeof speakers)` | Writes attached speaker IDs in entry order up to `size`. | Number of IDs written; `0` if invalid stream/array or `size <= 0`. | `1.3.0` | Server only |
| `bool:SvHasSpeakerInStream(SV_STREAM:stream, playerid)` | Checks whether `playerid` is currently attached as speaker. | `true` if attached; `false` otherwise. | `1.0.0` | Server only |
| `bool:SvDetachSpeakerFromStream(SV_STREAM:stream, playerid)` | Removes `playerid` as speaker from `stream`. | `true` if detached; `false` if not attached or invalid. | `1.0.0` | All clients |
| `SvDetachAllSpeakersFromStream(SV_STREAM:stream)` | Removes all speakers from `stream`. *(Declared `SV_VOID:`.)* | `1` if valid stream; `0` if invalid. | `1.0.0` | All clients |
| `SvGetPlayerListenerStreams(playerid, SV_STREAM:streams[], size = sizeof streams)` | Lists every active stream handle (Global, SL, and DL) `playerid` is currently listening to, in entry order. | Number of handles written; `0` if invalid/disconnected player or bad array. | `1.3.0` | Server only |
| `SvGetPlayerSpeakerStreams(playerid, SV_STREAM:streams[], size = sizeof streams)` | Lists every active stream handle `playerid` is currently speaking into, in entry order. | Number of handles written; `0` if invalid/disconnected player or bad array. | `1.3.0` | Server only |

---

## 6. Stream Parameter Natives

> [!NOTE]
> * **Per-Listener Volume Combination:** Effective gain is `SV_PARAMETER_VOLUME * listenerFactor` (Plus clients cap final gain at `4.0`; legacy `v11` clients apply it directly to BASS without a `> 1.0` cap).
> * **`SV_PARAMETER_FREQUENCY`:** `0.0` represents the original `48000 Hz` sample rate across both direct sets/resets and parameter slides (`SvStreamParameterSlide` also starts from `48000 Hz` when frequency is unset); negative values (`< 0.0`) are rejected, while `SvStreamParameterGet` / `GetEx` still return `0.0` at rest.
> * **`SV_PARAMETER_EAXMIX`:** Accepted for script compatibility with zero audible effect on Windows audio output.

| Native | Description | Return / Invalid Cases | Since | Client |
| :--- | :--- | :--- | :---: | :---: |
| `SvStreamParameterSet(SV_STREAM:stream, SV_PARAMETER:parameter, Float:value)` | Sets parameter value immediately (`SV_PARAMETER_VOLUME` / `FREQUENCY` reject `< 0.0`, where `0.0` frequency restores original `48000 Hz`; all reject non-finite). *(Declared `SV_VOID:`.)* | `1` if applied; `0` on invalid stream/parameter or rejected float. | `1.0.0` | All (`48000 Hz` sent on wire for `0.0`) |
| `SvStreamParameterReset(SV_STREAM:stream, SV_PARAMETER:parameter)` | Resets parameter back to its default (`Frequency: 0.0`, `Volume: 1.0`, `Panning: 0.0`, `EaxMix: -1.0`, `Src: 1.0`). *(Declared `SV_VOID:`.)* | `1` if valid stream & parameter; `0` otherwise. | `1.0.0` | All clients |
| `bool:SvStreamParameterHas(SV_STREAM:stream, SV_PARAMETER:parameter)` | Checks whether `parameter` was explicitly set on `stream`. | `true` if set; `false` if default/unset or invalid. | `1.0.0` | Server only |
| `Float:SvStreamParameterGet(SV_STREAM:stream, SV_PARAMETER:parameter)` | Returns parameter value (or default if unset). **Ambiguity warning:** `-1.0` is also valid `Panning` and default `EaxMix`; check `SvIsValidStream` beforehand or prefer `SvStreamParameterGetEx`. | Parameter float, or `-1.0` on invalid stream/parameter. | `1.0.0` | Server only |
| `bool:SvStreamParameterGetEx(SV_STREAM:stream, SV_PARAMETER:parameter, &Float:value)` | Unambiguous query writing current (or default) parameter into `value` only when valid. | `true` if valid; `false` without touching `value` otherwise. | `1.3.0` | Server only |
| `SvStreamParameterSlideFromTo(SV_STREAM:stream, SV_PARAMETER:parameter, Float:startvalue, Float:endvalue, time)` | Smoothly interpolates parameter from `startvalue` to `endvalue` over `time` ms (`0.0` frequency maps to `48000 Hz` in the slide math/packet while resting `Get` remains `0.0`). Rejects non-finite floats or `< 0.0` for Volume/Frequency. *(Declared `SV_VOID:`.)* | `1` if applied; `0` on invalid stream/parameter or rejected float. | `1.0.0` | All clients |
| `SvStreamParameterSlideTo(SV_STREAM:stream, SV_PARAMETER:parameter, Float:endvalue, time)` | Smoothly interpolates parameter from its current value (`48000 Hz` if frequency is unset/`0.0`) to `endvalue` over `time` ms. Rejects non-finite or `< 0.0` on Volume/Frequency. *(Declared `SV_VOID:`.)* | `1` if applied; `0` on invalid stream/parameter or rejected float. | `1.0.0` | All clients |
| `SvStreamParameterSlide(SV_STREAM:stream, SV_PARAMETER:parameter, Float:deltavalue, time)` | Smoothly shifts parameter by relative `deltavalue` over `time` ms (relative to `48000 Hz` when frequency is unset/`0.0`). Rejects only if `deltavalue` or `current + deltavalue` is non-finite. *(Declared `SV_VOID:`.)* | `1` if applied; `0` on invalid stream/parameter or non-finite values. | `1.0.0` | All clients |

---

## 7. DX8 Audio Effect Natives

> [!NOTE]
> * All `SvEffectCreate*` and `SvEffectUpdate*` natives reject non-finite float parameters (`NaN`, `±Inf`) with a rate-limited `[sv+:WRN]` and return `0`. Valid negative DX8 dB values are accepted.
> * **`SvEffectUpdate*` (`1.3.0`):** Keeps `priority` unchanged, sends zero packets if all parameters already match, updates **Plus `1.3.0+`** listeners smoothly in place, and refreshes legacy `v11` / pre-`1.3.0` listeners automatically.

| Native | Description | Return / Invalid Cases | Since | Client |
| :--- | :--- | :--- | :---: | :---: |
| `SV_EFFECT:SvEffectCreateChorus(priority, Float:wetdrymix, Float:depth, Float:feedback, Float:frequency, waveform, Float:delay, phase)` | Creates a DX8 Chorus filter handle. | Effect handle `!= 0`, or `SV_NULL` on non-finite floats. | `1.0.0` | All clients |
| `SV_EFFECT:SvEffectCreateCompressor(priority, Float:gain, Float:attack, Float:release, Float:threshold, Float:ratio, Float:predelay)` | Creates a DX8 Compressor filter handle. | Effect handle `!= 0`, or `SV_NULL` on non-finite floats. | `1.0.0` | All clients |
| `SV_EFFECT:SvEffectCreateDistortion(priority, Float:gain, Float:edge, Float:posteqcenterfrequency, Float:posteqbandwidth, Float:prelowpasscutoff)` | Creates a DX8 Distortion filter handle. | Effect handle `!= 0`, or `SV_NULL` on non-finite floats. | `1.0.0` | All clients |
| `SV_EFFECT:SvEffectCreateEcho(priority, Float:wetdrymix, Float:feedback, Float:leftdelay, Float:rightdelay, bool:pandelay)` | Creates a DX8 Echo filter handle. | Effect handle `!= 0`, or `SV_NULL` on non-finite floats. | `1.0.0` | All clients |
| `SV_EFFECT:SvEffectCreateFlanger(priority, Float:wetdrymix, Float:depth, Float:feedback, Float:frequency, waveform, Float:delay, phase)` | Creates a DX8 Flanger filter handle. | Effect handle `!= 0`, or `SV_NULL` on non-finite floats. | `1.0.0` | All clients |
| `SV_EFFECT:SvEffectCreateGargle(priority, ratehz, waveshape)` | Creates a DX8 Gargle filter handle. | Effect handle `!= 0`, or `SV_NULL` on failure. | `1.0.0` | All clients |
| `SV_EFFECT:SvEffectCreateI3dl2reverb(priority, room, roomhf, Float:roomrollofffactor, Float:decaytime, Float:decayhfratio, reflections, Float:reflectionsdelay, reverb, Float:reverbdelay, Float:diffusion, Float:density, Float:hfreference)` | Creates a DX8 I3DL2 Reverb filter handle. | Effect handle `!= 0`, or `SV_NULL` on non-finite floats. | `1.0.0` | All clients |
| `SV_EFFECT:SvEffectCreateParameq(priority, Float:center, Float:bandwidth, Float:gain)` | Creates a DX8 Parametric EQ filter handle. | Effect handle `!= 0`, or `SV_NULL` on non-finite floats. | `1.0.0` | All clients |
| `SV_EFFECT:SvEffectCreateReverb(priority, Float:ingain, Float:reverbmix, Float:reverbtime, Float:highfreqrtratio)` | Creates a DX8 Reverb filter handle. | Effect handle `!= 0`, or `SV_NULL` on non-finite floats. | `1.0.0` | All clients |
| `bool:SvEffectUpdateChorus(SV_EFFECT:effect, Float:wetdrymix, Float:depth, Float:feedback, Float:frequency, waveform, Float:delay, phase)` | Updates an existing Chorus filter across attached streams. | `true` if valid Chorus effect & finite floats; `false` on invalid/wrong effect type or `NaN`/`Inf`. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `bool:SvEffectUpdateCompressor(SV_EFFECT:effect, Float:gain, Float:attack, Float:release, Float:threshold, Float:ratio, Float:predelay)` | Updates an existing Compressor filter across attached streams. | Same as above. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `bool:SvEffectUpdateDistortion(SV_EFFECT:effect, Float:gain, Float:edge, Float:posteqcenterfrequency, Float:posteqbandwidth, Float:prelowpasscutoff)` | Updates an existing Distortion filter across attached streams. | Same as above. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `bool:SvEffectUpdateEcho(SV_EFFECT:effect, Float:wetdrymix, Float:feedback, Float:leftdelay, Float:rightdelay, bool:pandelay)` | Updates an existing Echo filter across attached streams. | Same as above. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `bool:SvEffectUpdateFlanger(SV_EFFECT:effect, Float:wetdrymix, Float:depth, Float:feedback, Float:frequency, waveform, Float:delay, phase)` | Updates an existing Flanger filter across attached streams. | Same as above. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `bool:SvEffectUpdateGargle(SV_EFFECT:effect, ratehz, waveshape)` | Updates an existing Gargle filter across attached streams. | `true` if valid Gargle effect; `false` on invalid/type mismatch. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `bool:SvEffectUpdateI3dl2reverb(SV_EFFECT:effect, room, roomhf, Float:roomrollofffactor, Float:decaytime, Float:decayhfratio, reflections, Float:reflectionsdelay, reverb, Float:reverbdelay, Float:diffusion, Float:density, Float:hfreference)` | Updates an existing I3DL2 Reverb filter across attached streams. | `true` if valid I3DL2 effect & finite floats; `false` otherwise. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `bool:SvEffectUpdateParameq(SV_EFFECT:effect, Float:center, Float:bandwidth, Float:gain)` | Updates an existing Parametric EQ filter across attached streams. | `true` if valid ParamEQ effect & finite floats; `false` otherwise. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `bool:SvEffectUpdateReverb(SV_EFFECT:effect, Float:ingain, Float:reverbmix, Float:reverbtime, Float:highfreqrtratio)` | Updates an existing Reverb filter across attached streams. | `true` if valid Reverb effect & finite floats; `false` otherwise. | `1.3.0` | In-place `Plus 1.3.0+`; fallback older |
| `SvEffectAttachStream(SV_EFFECT:effect, SV_STREAM:stream)` | Attaches `effect` to `stream`. One effect handle can attach to multiple streams. *(Declared `SV_VOID:`.)* | `1` if newly attached; `0` if invalid handle or already attached. | `1.0.0` | All clients |
| `SvEffectDetachStream(SV_EFFECT:effect, SV_STREAM:stream)` | Detaches `effect` from `stream`. *(Declared `SV_VOID:`.)* | `1` if detached; `0` if invalid handle or not attached. | `1.0.0` | All clients |
| `SvEffectDelete(SV_EFFECT:effect)` | Deletes `effect` and removes it from all streams. *(Declared `SV_VOID:`.)* | `1` if active & deleted; `0` if invalid or already deleted. | `1.0.0` | All clients |
| `bool:SvIsValidEffect(SV_EFFECT:effect)` | Checks whether `effect` resolves to an active effect (`0` / deleted / stale handles return `false`). | `true` if active; `false` otherwise. | `1.2.0` | Server only |

---

## 8. Pawn Callbacks (`8` total)

`SV_VOID:` expands to empty in `sampvoice.inc` (full `YSI_Coding\y_hooks` and standard `pawncc` compatibility — omit or keep `SV_VOID:` freely). All callbacks are invoked asynchronously on the server main tick across every loaded script:

```pawn
forward SV_VOID:OnPlayerActivationKeyPress(SV_UINT:playerid, SV_UINT:keyid);
forward SV_VOID:OnPlayerActivationKeyRelease(SV_UINT:playerid, SV_UINT:keyid);
forward SV_VOID:OnPlayerMicroStateChange(SV_UINT:playerid, bool:hasMicro);
forward SV_VOID:OnPlayerVoiceMute(SV_UINT:playerid, bool:releasekeys, SV_UINT:time, bool:speaking);
forward SV_VOID:OnPlayerVoiceUnmute(SV_UINT:playerid, bool:expired);
forward SV_VOID:OnPlayerVoiceReady(SV_UINT:playerid);
forward SV_VOID:OnPlayerVoiceStart(SV_UINT:playerid);
forward SV_VOID:OnPlayerVoiceStop(SV_UINT:playerid);
```

| Callback | When It Fires | Since | Client |
| :--- | :--- | :---: | :---: |
| `OnPlayerActivationKeyPress(playerid, keyid)` | Triggered when `playerid` presses a key previously bound with `SvAddKey`. Typically calls `SvAttachSpeakerToStream`. | `1.0.0` | All (`Android` sends `0x42`) |
| `OnPlayerActivationKeyRelease(playerid, keyid)` | Triggered when `playerid` releases a held key, when `SvRemoveKey` / `SvRemoveAllKeys` runs while held, or synthetically when `SvMutePlayerEnable(..., true)` fires. | `1.0.0` | All clients |
| `OnPlayerMicroStateChange(playerid, bool:hasMicro)` | Triggered whenever a Plus client enables, disables, plugs in, or unplugs a microphone after connecting, updating `SvHasMicro`. | `1.0.0` | `Plus 1.0.0+` |
| `OnPlayerVoiceMute(playerid, bool:releasekeys, time, bool:speaking)` | Fired on the next server tick when an unmuted player becomes muted via `SvMutePlayerEnable`. `speaking` indicates whether `playerid` was attached to any speaker stream when muted. Not fired on disconnect or already-muted calls. | `1.1.0` | All clients |
| `OnPlayerVoiceUnmute(playerid, bool:expired)` | Fired when a timed mute expires (`expired = true`) or on the next tick after `SvMutePlayerDisable` (`expired = false`). Never fired on disconnect. | `1.1.0` | All clients |
| `OnPlayerVoiceReady(playerid)` | Fired once per connection right after the UDP voice handshake completes, so `SvIsPlayerVoiceReady(playerid)` is already `true`. Never re-fired on NAT changes or GMX. *(Scripts loaded later should check `SvIsPlayerVoiceReady` directly.)* | `1.2.0` | All clients |
| `OnPlayerVoiceStart(playerid)` | Fired once the server accepts voice frames while `playerid` is unmuted, holding an active key (or `SvStartRecord`), and attached as a speaker on `>= 1` stream. Persists across GMX (`SvIsPlayerSpeaking`). | `1.2.0` | All clients |
| `OnPlayerVoiceStop(playerid)` | Fired after `> 1000 ms` without accepted voice frames (or `~1000 ms` after being server-muted mid-speech). **Not** called on disconnect — scripts caching speaking state should reset it in `OnPlayerDisconnect`. | `1.2.0` | All clients |
