# Migrating from Legacy SampVoice (`CyberMor` 3.x / 4.x & `AmyrAhmady`)

This guide helps players and server administrators upgrade from **CyberMor SampVoice** (`3.x` or `4.x`) or **AmyrAhmady/sampvoice** to **SampVoice Plus**.

> [!TIP]
> **In short**
> * **CyberMor 3.x / AmyrAhmady:** Swap the server binary and `sampvoice.inc`. Existing `.amx` gamemodes, scripts, legacy PC `3.x` clients, and Android launchers keep working without edits. Just check stream tag colors (`sampvoice.legacy_color_format`).
> * **CyberMor 4.x:** Players must install **SampVoice Plus** (`4.x` clients use a different protocol), scripts must adapt from `4.x` natives to the SampVoice Plus API (see table below), and you no longer run the external `voice` relay process.

---

## 1. For Players (`sampvoice.asi`)

### Automatic Backup & Migration via Installer
Running **`SampVoice-Plus-Setup.exe`** detects legacy SampVoice files (`sampvoice.asi`, `scripts\sampvoice*.asi`, `sampvoice\`, `bass_fx.dll`) and defaults to **Backup**:
* Your legacy files are backed up cleanly while shared SA-MP runtime libraries (`bass.dll`, `vorbisFile.dll`, `vorbisHooked.dll`) stay untouched.
* Uninstalling **SampVoice Plus** restores your previous legacy SampVoice setup cleanly.

*(For installer internals, registry IDs, and silent flags, see [Under the Hood — Windows Installer Internals](under-the-hood.md#6-windows-installer-internals-sampvoiceplusiss) and [Getting Started](getting-started.md#installer-command-line-options).)*

### Manual Migration Checklist
1. Remove old `sampvoice.asi`, `scripts\sampvoice*.asi`, `bass_fx.dll`, and any loose `sampvoice\` font/PNG asset folders from your GTA San Andreas directory (**keep** `bass.dll`, `vorbisFile.dll`, `vorbisHooked.dll`, and your ASI Loader such as `dinput8.dll`).
2. Copy the single **`sampvoice.asi`** file directly into the GTA San Andreas **root directory** (next to `gta_sa.exe`).
3. Verify the file hash if needed: run `certutil -hashfile sampvoice.asi SHA256` and compare it with line 1 of `Documents\GTA San Andreas User Files\SAMP\sampvoice\sampvoice_plus_v<version>_log.txt`.

### Settings, Block List & Singleplayer Safety
* **Clean Folder Separation:** SampVoice Plus saves `sampvoice.ini` and logs under `Documents\GTA San Andreas User Files\SAMP\sampvoice\`, leaving your old `Documents\GTA San Andreas User Files\sampvoice\` folder untouched.
* **Scoped Block List:** Blocked players are saved inside `sampvoice.ini` under `[blacklist]` (`<player> | <server name> | <IP:port> | <server|all>`), allowing per-server or global blocking and editing even while disconnected.
* **Zero Singleplayer Footprint:** When launching singleplayer GTA:SA (no SA-MP present), `sampvoice.asi` exits immediately without creating files, folders, hooks, or audio streams.

---

## 2. For Server Owners & Pawn Scripters

### Replacing the Server Binary (`plugins/` or `components/`)
* **open.mp:** Replace the component with `components/sampvoice.dll` (Windows) or `components/sampvoice.so` (Linux x86).
* **SA-MP `0.3.7-R2` / `R3` / `0.3.DL-R1`:** Replace `plugins/sampvoice.dll` / `.so` and update `pawno/include/sampvoice.inc`.
* **External Relay Removal (`CyberMor 4.x`):** SampVoice Plus integrates the voice relay inside the plugin/component — shut down the separate `voice` binary and open one UDP port (`sampvoice.port` in `server.cfg` or `config.json`).

### Client Compatibility
* **Supported Simultaneously:** `CyberMor 3.x`, `AmyrAhmady`, Android 3.x launchers (`v11`), and `SampVoice Plus` clients connect side-by-side without script changes.
* **Rejected Clients:** `CyberMor 4.x` clients use an incompatible protocol and are rejected cleanly with an informational log; those players should install **SampVoice Plus** (`sampvoice.asi`).
* **Android Activation Key:** Embedded Android 3.x clients always transmit key `0x42` (`B`) rather than following `SvAddKey`; ensure scripts bind `0x42` alongside desktop keys if mobile players connect.

### Stream Tag Colors (`sampvoice.legacy_color_format`)
Legacy **CyberMor 3.x** and **AmyrAhmady** servers sent Pawn colors directly in **`0xAARRGGBB`** byte order. Therefore, any existing gamemode whose stream tags already displayed the right colors on legacy servers is passing `0xAARRGGBB` values:
* **Compatibility Mode (`sampvoice.legacy_color_format = true`):** Set `"legacy_color_format": true` (`config.json`) or `sampvoice.legacy_color_format 1` (`server.cfg`) to keep legacy **`0xAARRGGBB`** colors untouched without editing your gamemode.
* **Default (`sampvoice.legacy_color_format = false`):** Expects standard SA-MP Pawn **`0xRRGGBBAA`** colors (e.g., `0xFF0000FF` for red) and converts them automatically.

### `y_hooks` & Automatic Resource Cleanup
* `SV_VOID:` expands to empty cleanly so `YSI_Coding\y_hooks` and standard callbacks work together in gamemodes and filterscripts.
* Streams and DX8 effects are tracked per script and automatically cleaned up on GMX or filterscript unload.

---

## 3. From CyberMor 4.x Scripts

Unlike **CyberMor 3.x** and **AmyrAhmady** (which share the exact same Pawn API as SampVoice Plus), **CyberMor 4.x** uses a different channel-based Pawn API (`sampvoice.inc` v4.0.0) and requires script adaptation:

| CyberMor 4.x Native | SampVoice Plus Equivalent | Notes / How to Adapt |
| :--- | :--- | :--- |
| `SvEnableDebug()` / `SvDisableDebug()` | `SvDebug(true)` / `SvDebug(false)` | Single boolean native. |
| `SvCheckDebug()` | No direct equivalent | Track the debug state boolean in your script (all 4.x debug prints also route via server log). |
| `SvGetVersion(player)` | `SvGetVersion(playerid)` / `SvGetClientProtocol(playerid)` | Returns `11` for legacy `v11` / Plus clients, or `12` from `SvGetClientProtocol` for Plus; use `SvGetPlusClientVersion` for exact client version. |
| `SvHasMicro(player)` | `SvHasMicro(playerid)` | Identical signature; Plus clients also fire `OnPlayerMicroStateChange`. |
| `SvEnableListener` / `SvDisableListener` / `SvCheckListener` | `SvEnableListener` / `SvDisableListener` / `SvCheckListener` | Identical signatures. |
| `SvEnableSpeaker` / `SvDisableSpeaker` / `SvCheckSpeaker` | `SvMutePlayerDisable` / `SvMutePlayerEnable` / `SvMutePlayerStatus` | **Inverted boolean sense:** `SvCheckSpeaker(player, ...)` corresponds to `!SvMutePlayerStatus(playerid)` (`SvEnableSpeaker` -> `SvMutePlayerDisable`, `SvDisableSpeaker` -> `SvMutePlayerEnable`). Plus mutes all outbound voice for `playerid` cleanly (optionally releasing held keys and expiring automatically). |
| `SvAttachStream` / `SvDetachStream` / `SvHasStream` | `SvAttachSpeakerToStream` / `SvDetachSpeakerFromStream` / `SvHasSpeakerInStream` | Plus binds speakers directly per stream rather than per numeric player channel index; dispatch keys inside `OnPlayerActivationKeyPress` / `Release`. |
| `SvSetKey` / `SvGetKey` | `SvAddKey` / `SvRemoveKey` / `SvRemoveAllKeys` / `SvHasKey` | Bind multiple Virtual-Key codes per player; map each pressed `keyid` inside `OnPlayerActivationKeyPress` / `Release`. |
| `SvPlay` / `SvStop` | `SvStartRecord` / `SvStopRecord` | Starts/stops microphone recording globally for `playerid`; attach `playerid` as speaker to the desired streams. |
| `SvCreateStream(distance = 0.0)` | `SvCreateGStream` / `SvCreateSLStreamAt*` / `SvCreateDLStreamAt*` | Pick Global vs Static/Dynamic Local and target entity (Point, Vehicle, Player, Object) at creation time. |
| `SvEnableTransiter` / `SvDisableTransiter` / `SvCheckTransiter` | No direct equivalent | Plus separates manual **SL** streams (`SvCreateSLStreamAt*` + `SvAttachListenerToStream`) from automatic distance-managed **DL** streams (`SvCreateDLStreamAt*` + `maxplayers`, VW/interior filtering). |
| `SvAttachListener` / `SvDetachListener` / `SvHasListener` | `SvAttachListenerToStream` / `SvDetachListenerFromStream` / `SvHasListenerInStream` | `SvAttachListenerToStream` also accepts optional `Float:volume = 1.0`. |
| `SvSetVolume(stream, vol)` / `SvSetPanning(stream, pan)` | `SvStreamParameterSet(stream, SV_PARAMETER_VOLUME / PANNING, val)` | Also supports `SvStreamParameterSlide*`, `GetEx`, `Reset`, `FREQUENCY`, and `SRC`. |
| `SvSetDistance(stream, dist)` | `SvUpdateDistanceForLStream(lstream, dist)` | Updates local stream radius cleanly. |
| `SvSetPosition(stream, x, y, z)` | `SvUpdatePositionForLPStream(lpstream, x, y, z)` | Valid for point streams (`SL_POINT` / `DL_POINT`). |
| `SvSetTarget(stream, SvMake*(id))` | No runtime re-target equivalent | Entity targets are fixed at creation via `SvCreate{SL,DL}StreamAt{Vehicle,Player,Object}`; create a stream for the new entity or use an `AtPoint` stream moved cleanly. |
| `SvSetEffect(stream, effect)` | `SvEffectAttachStream` / `SvEffectDetachStream` | Plus allows attaching multiple `SV_EFFECT` objects directly to streams without a single slot. |
| `SvSetIcon(stream, icon[])` | `SvStreamSetIcon(stream, icon[])` | Selects one of the 7 built-in HUD sprites (`"speaker"`, `"radio"`, `"phone"`, `"megaphone"`, `"police"`, `"admin"`, `"global"`) on Plus clients. |
| `SvDeleteStream(stream)` | `SvDeleteStream(stream)` | Identical signature. |
| `SvCreateEffect()` / `SvAppendFilter` / `SvRemoveFilter` / `SvDeleteEffect` | `SvEffectCreate{Chorus,...,Reverb}` + `SvEffectUpdate*` + `SvEffectDelete` | Create and attach typed DX8 effects directly; use `SvEffectUpdate*` (`1.3.0+`) for live parameter changes or `SvEffectDetachStream` + `SvEffectDelete` to remove. |

---

## 4. Unlock Plus Features Step by Step

After upgrading your binary, you can adopt SampVoice Plus features at your own pace without breaking older clients:

### Level 1 — Zero Script Changes Required
Players using **SampVoice Plus (`sampvoice.asi`)** automatically benefit as soon as they join:
* All 7 official PC SA-MP builds (`0.3.7-R1`–`R5`, `0.3.DL-R1`) and open.mp launcher supported in one file.
* 15-language `F11` menu, adaptive content-fit sizing, microphone self-monitor, soft limiter, scoped block list, and smoother 3D attenuation (`PlayAudioStreamForPlayer` left intact).
* **Plus `1.2.0+` players** also get per-player volume mixing (`0–200%`), separate proximity vs global volume sliders, Deafen toggle/hotkey, and wall voice occlusion (enabled by default).

### Level 2 — Server Config Options Only (`server.cfg` / `config.json`)
No Pawn edits needed; adjust behavior globally in config (see [Server Configuration Reference](getting-started.md#server-configuration-reference)):
* **`sampvoice.allow_toggle_mode` (`1` -> `0`)** *(Plus `1.2.0+`)*: enforces Push-to-Talk in the client menu without overwriting the player's saved preference.
* **`sampvoice.voice_occlusion` (`1` -> `0`)** *(Plus `1.2.0+`)*: turn wall muffling off if you prefer clear audio through buildings.
* **`sampvoice.voice_status` / `sampvoice.nearby_list`** *(Plus `1.1.0+`)*: control live no-mic/cannot-hear indicators and the Nearby player list filter.

### Level 3 — Small Script Additions (`1.0.0` – `1.2.0` API)
Add targeted features while keeping full support for `v11` and Android clients:
* **Channel Icons (`SvStreamSetIcon`)** *(Plus `1.0.0+`)*: show radio, phone, megaphone, police dispatch, admin, or global icons in the Speaker List and above heads ([API Reference](api-reference.md#stream-control--inspection-19-natives), Demo items 2–8).
* **Ready & Speaking Callbacks (`OnPlayerVoiceReady`, `OnPlayerVoiceStart`, `OnPlayerVoiceStop`, `SvIsPlayerVoiceReady`, `SvIsPlayerSpeaking`)** *(all clients, `1.2.0+` server)*: know when a player's UDP channel finishes connecting and when they start/stop speaking ([API Reference](api-reference.md#8-pawn-callbacks-8-total), Demo items 1 & 13).
* **Key-Releasing & Timed Server Mutes (`SvMutePlayerEnable(..., releasekeys, time)`, `SvMutePlayerRemaining`)** *(key-latch UI on Plus `1.1.0+`; server mute enforced for all clients)*: auto-expiring server mutes with `OnPlayerVoiceUnmute` ([API Reference](api-reference.md#4-player-microphone-activation-keys-mutes--listener-control), Demo item 11).
* **Per-Pair Mutes & Per-Listener Volumes (`SvMutePlayerForListener`, `SvAttachListenerToStream(..., volume)`, `SvStreamSetListenerVolume`)** *(all clients, `1.2.0+` server)*: mute specific `(speaker, listener)` pairs or boost/lower a channel per listener ([API Reference](api-reference.md#5-stream-creation--lifecycle-natives), Demo items 3 & 11).

### Level 4 — Full `1.3.0` Dynamic API
Adopt live metadata, inspection, filtering, and client telemetry:
* **Live Stream Rename & Recolor (`SvStreamSetName`, `SvStreamSetColor`)**: updates stream tags instantly without audio glitches on **Plus `1.3.0+`** clients (older clients refresh cleanly during silence; Demo item 2).
* **Live DX8 Effect Tweaks (`SvEffectUpdate*`)**: tunes chorus, echo, distortion, EQ, and reverb live in place on **Plus `1.3.0+`** without audio restarts (Demo items 3 & 10).
* **Virtual World & Interior Filtering (`SvStreamSetWorld`, `SvCreateDLStreamAtPoint` / `AtObject` trailing args)**: restrict point or standard-object dynamic streams to specific worlds/interiors (Demo item 7).
* **Control-Channel Latency & Safe Queries (`SvGetPlayerVoiceLatency`, `SvStreamParameterGetEx`, `SvGetStreamListeners` / `Speakers` / `GetPlayer*Streams` / `SvGetStream*`)**: read RTT (Plus `1.3.0+`) and inspect active streams, parameters, and member lists cleanly (Demo items 1, 7, 9, 12).
