# Complete API Reference (`sampvoice.inc`)

Exhaustive reference for every constant, enum, Pawn tag, native function, and callback exposed by [`sampvoice.inc`](../sampvoice.inc).

---

## 1. Version Constants & Macros

```pawn
#define SV_PLUS_VERSION_MAJOR   1
#define SV_PLUS_VERSION_MINOR   0
#define SV_PLUS_VERSION_PATCH   0
#define SV_PLUS_VERSION_STRING  "1.0.0"

#define SV_VERSION              11  // Legacy wire / CyberMor v3.x & AmyrAhmady protocol
#define SV_PLUS_VERSION         12  // SampVoice Plus extended protocol (SVP1)

#define SV_CAP_ADAPTIVE_JITTER  (1 << 1)
#define SV_CAP_TELEMETRY        (1 << 2)

#define SV_NULL                 0
#define SV_INFINITY             -1
#define SV_FALSE                false
#define SV_TRUE                 true
```

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

## 3. Core & Client Protocol Natives

| Native | Description |
| :--- | :--- |
| `SvDebug(bool:mode)` | Enables or disables verbose debug logging in the host server log (`svplus.log` is created only on SA-MP if `logprintf` is unavailable). Calling `SvDebug(true)` also logs the `Effective config` summary once. |
| `SvInit(bitrate)` | Configures target Opus voice bitrate in bits/sec (default `24000`). |
| `SvGetVersion(playerid)` | Returns `0` (`SV_NULL`) if no voice plugin is present, or wire handshake version (`11` `SV_VERSION` / `12`). |
| `SvGetClientProtocol(playerid)` | Returns `0` if disconnected, `11` (`SV_VERSION`) for legacy clients, or `12` (`SV_PLUS_VERSION`) for SampVoice Plus clients negotiating `SVP1`. |
| `SvGetPlusCapabilities(playerid)` | Returns the client's `SV_CAP_*` bitmask (e.g. `SV_CAP_ADAPTIVE_JITTER \| SV_CAP_TELEMETRY`). |

---

## 4. Player Microphone, Activation Keys, Mutes & Listener Control

| Native | Returns | Description |
| :--- | :---: | :--- |
| `SvHasMicro(playerid)` | `bool` | Returns `true` if the player reported an active capture microphone during connect. |
| `SvStartRecord(playerid)` | `bool` | Programmatically commands the client to begin transmitting voice frames. |
| `SvStopRecord(playerid)` | `bool` | Programmatically stops continuous recording on the client. |
| `SvAddKey(playerid, keyid)` | `bool` | Registers a Win32 Virtual-Key code (`keyid`, e.g. `0x42` `'B'`) to trigger key callbacks. *Note: Android launchers with embedded SampVoice ignore `SvAddKey` and always send `0x42` — see [Android key handling](troubleshooting.md#4-keys-talk-modes-chat--dialogs).* |
| `SvHasKey(playerid, keyid)` | `bool` | Checks whether `keyid` is currently bound for `playerid`. |
| `SvRemoveKey(playerid, keyid)` | `bool` | Unbinds a single activation key from `playerid`. If the key is currently held down, the script receives `OnPlayerActivationKeyRelease` on the next server tick. |
| `SvRemoveAllKeys(playerid)` | `void` | Clears all activation keys registered for `playerid`. For each bound key currently held down, the script receives `OnPlayerActivationKeyRelease` on the next server tick. |
| `SvMutePlayerStatus(playerid)` | `bool` | Returns `true` if `playerid` is server-muted (outbound speaking disabled). |
| `SvMutePlayerEnable(playerid)` | `void` | Server-mutes `playerid` and drops inbound voice frames server-side. *(Speaker-mute equivalent of CyberMor 4.x `SvDisableSpeaker`.)* |
| `SvMutePlayerDisable(playerid)` | `void` | Unmutes `playerid` (re-enables speaking). *(Equivalent of `SvEnableSpeaker`.)* |
| `SvEnableListener(playerid)` | `bool` | Enables global voice reception for `playerid`. Returns `true` if transitioned from disabled to enabled; `false` if already enabled or no plugin. |
| `SvDisableListener(playerid)` | `bool` | Disables all inbound voice playback for `playerid` server-side while keeping stream memberships intact (notifies `v12` F11 diagnostics; works for `v11`/`v12`/Android). Returns `true` on transition. |
| `SvCheckListener(playerid)` | `bool` | Returns `true` if `playerid` has voice listening enabled (`true` by default; resets on disconnect, persists across GMX). |

> [!NOTE]
> `SvMutePlayerEnable`, `SvMutePlayerDisable`, and `SvMutePlayerStatus` are the speaker-mute equivalents in SampVoice Plus (`SvEnableSpeaker` / `SvDisableSpeaker` from 4.x are not part of this API because speaker routing uses explicit `SvAttachSpeakerToStream` / `SvDetachSpeakerFromStream` and per-player mutes).

---

## 5. Stream Creation & Lifecycle Natives

### Global Streams (`SV_GSTREAM`)
* `SvCreateGStream(color = SV_NULL, const name[] = "")`

### Static Local Streams (`SV_LPSTREAM` / `SV_SLSTREAM`)
* `SvCreateSLStreamAtPoint(Float:distance, Float:posx, Float:posy, Float:posz, color = SV_NULL, const name[] = "")`
* `SvCreateSLStreamAtVehicle(Float:distance, vehicleid, color = SV_NULL, const name[] = "")`
* `SvCreateSLStreamAtPlayer(Float:distance, playerid, color = SV_NULL, const name[] = "")`
* `SvCreateSLStreamAtObject(Float:distance, objectid, color = SV_NULL, const name[] = "")`

### Dynamic Local Streams (`SV_LPSTREAM` / `SV_DLSTREAM`)
* `SvCreateDLStreamAtPoint(Float:distance, maxplayers, Float:posx, Float:posy, Float:posz, color = SV_NULL, const name[] = "")`
* `SvCreateDLStreamAtVehicle(Float:distance, maxplayers, vehicleid, color = SV_NULL, const name[] = "")`
* `SvCreateDLStreamAtPlayer(Float:distance, maxplayers, playerid, color = SV_NULL, const name[] = "")`
* `SvCreateDLStreamAtObject(Float:distance, maxplayers, objectid, color = SV_NULL, const name[] = "")`

> **`color` parameter format (`sampvoice.legacy_color_format`):** Standard Pawn scripts pass colors in **`0xRRGGBBAA`** format (matching `SendClientMessage`, e.g., `0xFF0000FF` for red, `0` / `SV_NULL` for no stream tag). By default (`sampvoice.legacy_color_format = false`), the server converts `0xRRGGBBAA` to wire `0xAARRGGBB` (so `0x000000FF` becomes opaque black `0xFF000000` while `0` remains `0`). The Speaker List displays `(icon) [ID] Name [TAG]` using SA-MP's player color for `[ID] Name [ ]` and the stream color for `TAG` (omitted when `name` is empty or `color == 0`). Set `sampvoice.legacy_color_format 1` (`"legacy_color_format": true` in open.mp `config.json`) if your script pre-shifted colors to `0xAARRGGBB`.

### Stream Control
* `SvUpdateDistanceForLStream(SV_LSTREAM:lstream, Float:distance)`
* `SvUpdatePositionForLPStream(SV_LPSTREAM:lpstream, Float:posx, Float:posy, Float:posz)`
* `bool:SvStreamSetIcon(SV_STREAM:stream, const SV_STR:icon[])` *(sets the HUD icon displayed in the `v12` Speaker List and overhead 3D icon; valid lowercase names: `"speaker"` default, `"radio"`, `"phone"`, `"megaphone"`, `"police"`, `"admin"`, `"global"`; unknown names log one warning and fall back to `"speaker"`; invalid stream handle returns `false`; if a speaker transmits across multiple streams at once, the client shows the icon from the first active stream)*
* `SvDeleteStream(SV_STREAM:stream)`

### Stream Listeners & Speakers
* `bool:SvAttachListenerToStream(SV_STREAM:stream, playerid)` *(also clears any block added by `SvDetachListenerFromStream` on dynamic streams, re-attaching if in range)*
* `bool:SvHasListenerInStream(SV_STREAM:stream, playerid)`
* `bool:SvDetachListenerFromStream(SV_STREAM:stream, playerid)` *(on dynamic streams, also blocks automatic re-attachment on subsequent spatial ticks until `SvAttachListenerToStream` is called or the player disconnects)*
* `SvDetachAllListenersFromStream(SV_STREAM:stream)`
* `bool:SvAttachSpeakerToStream(SV_STREAM:stream, playerid)`
* `bool:SvHasSpeakerInStream(SV_STREAM:stream, playerid)`
* `bool:SvDetachSpeakerFromStream(SV_STREAM:stream, playerid)`
* `SvDetachAllSpeakersFromStream(SV_STREAM:stream)`

---

## 6. Stream Parameter Natives

* `SvStreamParameterSet(SV_STREAM:stream, SV_PARAMETER:parameter, Float:value)`
* `SvStreamParameterReset(SV_STREAM:stream, SV_PARAMETER:parameter)`
* `bool:SvStreamParameterHas(SV_STREAM:stream, SV_PARAMETER:parameter)`
* `Float:SvStreamParameterGet(SV_STREAM:stream, SV_PARAMETER:parameter)`
* `SvStreamParameterSlideFromTo(SV_STREAM:stream, SV_PARAMETER:parameter, Float:startvalue, Float:endvalue, time)`
* `SvStreamParameterSlideTo(SV_STREAM:stream, SV_PARAMETER:parameter, Float:endvalue, time)`
* `SvStreamParameterSlide(SV_STREAM:stream, SV_PARAMETER:parameter, Float:deltavalue, time)`

> [!NOTE]
> `SV_PARAMETER_VOLUME` scales the voice stream alongside master and spatial distance attenuation, and `SV_PARAMETER_PANNING` adds an offset (clamped to `[-1.0, +1.0]`) onto the software spatial stereo pan. `SV_PARAMETER_EAXMIX` is accepted without error for script compatibility but has no audible effect (hardware EAX is unsupported on modern Windows, and SampVoice Plus performs spatialization without altering SA-MP's global 3D/EAX state).

---

## 7. DX8 Audio Effect Natives

* `SV_EFFECT:SvEffectCreateChorus(priority, Float:wetdrymix, Float:depth, Float:feedback, Float:frequency, waveform, Float:delay, phase)`
* `SV_EFFECT:SvEffectCreateCompressor(priority, Float:gain, Float:attack, Float:release, Float:threshold, Float:ratio, Float:predelay)`
* `SV_EFFECT:SvEffectCreateDistortion(priority, Float:gain, Float:edge, Float:posteqcenterfrequency, Float:posteqbandwidth, Float:prelowpasscutoff)`
* `SV_EFFECT:SvEffectCreateEcho(priority, Float:wetdrymix, Float:feedback, Float:leftdelay, Float:rightdelay, bool:pandelay)`
* `SV_EFFECT:SvEffectCreateFlanger(priority, Float:wetdrymix, Float:depth, Float:feedback, Float:frequency, waveform, Float:delay, phase)`
* `SV_EFFECT:SvEffectCreateGargle(priority, ratehz, waveshape)`
* `SV_EFFECT:SvEffectCreateI3dl2reverb(priority, room, roomhf, Float:roomrollofffactor, Float:decaytime, Float:decayhfratio, reflections, Float:reflectionsdelay, reverb, Float:reverbdelay, Float:diffusion, Float:density, Float:hfreference)`
* `SV_EFFECT:SvEffectCreateParameq(priority, Float:center, Float:bandwidth, Float:gain)`
* `SV_EFFECT:SvEffectCreateReverb(priority, Float:ingain, Float:reverbmix, Float:reverbtime, Float:highfreqrtratio)`
* `SvEffectAttachStream(SV_EFFECT:effect, SV_STREAM:stream)`
* `SvEffectDetachStream(SV_EFFECT:effect, SV_STREAM:stream)`
* `SvEffectDelete(SV_EFFECT:effect)`

---

## 8. Pawn Callbacks

`SV_VOID:` expands to empty in `sampvoice.inc` (full `YSI_Coding\y_hooks` and standard `pawncc` compatibility — omit or keep `SV_VOID:` freely):

```pawn
forward SV_VOID:OnPlayerActivationKeyPress(SV_UINT:playerid, SV_UINT:keyid);
forward SV_VOID:OnPlayerActivationKeyRelease(SV_UINT:playerid, SV_UINT:keyid);
// SampVoice Plus v12 feature (only invoked by v12 clients when mic availability changes after connect):
forward SV_VOID:OnPlayerMicroStateChange(SV_UINT:playerid, bool:hasMicro);
```

* **`OnPlayerMicroStateChange(playerid, bool:hasMicro)`** *(SampVoice Plus v12 feature)* — Invoked on the server main thread when a v12+ client enables, disables, plugs in, or unplugs their microphone after connecting, updating `SvHasMicro(playerid)`. Legacy v11 clients do not send `PlusMicroState`.
