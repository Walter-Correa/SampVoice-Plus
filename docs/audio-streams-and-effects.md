# Audio Streams & DX8 DSP Effects Guide

**SampVoice Plus** routes every spoken transmission through **Streams** paired with optional **Parameters** (volume, panning, pitch) and **DX8 Audio Effects**. This guide explains how they behave and gives short recipes you can also test live in **`/voice`** (`FS_SampVoicePlus_Demo`).

---

## 1. Core Concepts: Global, SL, and DL Streams

Every stream has a **type** (who hears it and whether it has a 3D position), **speakers** (who can talk into it), and **listeners** (who can hear it).

| Category | Pawn Creation Natives | 3D Audio? | Who Picks Listeners? | Common Uses |
| :--- | :--- | :---: | :--- | :--- |
| **Global (`SV_GSTREAM`)** | `SvCreateGStream` | No (2D) | **Your script** (`SvAttachListenerToStream`) | Faction radios, admin broadcasts, 1-on-1 phone calls |
| **Static Local (`SL`)** | `SvCreateSLStreamAtPoint`<br>`SvCreateSLStreamAtVehicle`<br>`SvCreateSLStreamAtPlayer`<br>`SvCreateSLStreamAtObject` | Yes (3D) | **Your script** (`SvAttachListenerToStream`) | Vehicle intercoms (passengers only), VIP clubs, scripted room zones |
| **Dynamic Local (`DL`)** | `SvCreateDLStreamAtPoint`<br>`SvCreateDLStreamAtVehicle`<br>`SvCreateDLStreamAtPlayer`<br>`SvCreateDLStreamAtObject` | Yes (3D) | **The server automatically** by distance (`maxplayers`, VW & interior) | Proximity local chat, public megaphones, movable speaker boxes |

* **SL vs DL in plain terms:**
  * On **SL (Static Local)** streams, your script decides every listener manually — distance only fades the 3D volume on the client. Useful when you want **only specific players** to hear a 3D source (such as occupants inside one vehicle).
  * On **DL (Dynamic Local)** streams, the server checks positions continuously (`sampvoice.updaterate`) and automatically attaches/detaches nearby players within `distance`.
  * **`maxplayers` on DL streams:** Sets the **maximum number of listeners on that stream at once** (not per-player stream caps). When slots are open, the closest players inside `distance` join; current in-range listeners stay attached until they leave range. Pass **`SV_INFINITY`** (`-1`) for unlimited listeners, or **`0`** to allow none.
* **Activation Keys:** Register Win32 Virtual-Key codes with `SvAddKey(playerid, keyid)` and manage speakers manually inside `OnPlayerActivationKeyPress` / `OnPlayerActivationKeyRelease`, or let the server handle speaker attachment automatically with `SvBindKeyToStream`. Remember that Android 3.x launchers always send `0x42` (`B`).

---

## 2. Key Bindings

Instead of duplicating speaker attach/detach boilerplate inside `OnPlayerActivationKeyPress` and `OnPlayerActivationKeyRelease`, `SvBindKeyToStream(playerid, keyid, stream)` lets the voice core attach and detach speakers automatically (up to `32` bindings per player, cleared cleanly on disconnect, `SvRemoveKey`, or `SvDeleteStream`). Keys also remain registered for callbacks, and Android `0x42` (`B`) works identically when bound:

**Before (manual callbacks):**
```pawn
SvAddKey(playerid, 0x42);

public SV_VOID:OnPlayerActivationKeyPress(SV_UINT:playerid, SV_UINT:keyid)
{
    if (keyid == 0x42 && g_Local[playerid] != SV_NULL)
        SvAttachSpeakerToStream(g_Local[playerid], playerid);
}

public SV_VOID:OnPlayerActivationKeyRelease(SV_UINT:playerid, SV_UINT:keyid)
{
    if (keyid == 0x42 && g_Local[playerid] != SV_NULL)
        SvDetachSpeakerFromStream(g_Local[playerid], playerid);
}
```

**After (server-managed key binding):**
```pawn
g_Local[playerid] = SvCreateDLStreamAtPlayer(35.0, 24, playerid, 0xFFFFFFFF, "Proximity");
SvBindKeyToStream(playerid, 0x42, g_Local[playerid]); // registers 0x42 and attaches/detaches automatically
```

* One key can drive multiple streams and multiple keys can target one stream (`N:M`); releasing one key keeps the speaker attached as long as another bound key for that stream stays held.
* Binding a key that is already physically held attaches the player to the stream immediately (useful when switching radio channels mid-transmission).
* Any speaker attached manually via `SvAttachSpeakerToStream` prior to binding or pressing the key stays attached when the key is released.

---

## 3. Channel Priority

Assigning a positive priority (`1` = highest, `2` lower than `1`, `0` = normal/no priority) either at creation (`SvCreate*(..., priority)`) or at runtime (`SvSetStreamPriority`) makes higher-priority active channels automatically duck lower-priority streams heard by the same listener without stacking:

```pawn
// Proximity stays at priority 0 (default) and ducks automatically while dispatch speaks
new SV_GSTREAM:policeRadio = SvCreateGStream(0x3498DBFF, "Police Dispatch", 1);

// Lower dispatch priority at runtime (still higher than priority 0 proximity)
SvSetStreamPriority(policeRadio, 2);
```

* A priority stream only ducks for listener `L` while another unmuted speaker `X != L` (not pair-muted for `L`) is actively speaking (`~1000 ms` hold), fading smoothly down over `~150 ms` and back over `~400 ms` after silence.
* Configure the retained percentage (`0..100`, default `50`) with `sampvoice.priority_duck`. Scripts still read un-ducked volumes via `SvStreamGetListenerVolume` and `SvStreamParameterGet`.

---

## 4. Position, Movement & Standard vs Streamer Objects

* **Entity-Attached Streams Track Automatically:** Streams attached to a player, vehicle, or standard GTA object follow that entity automatically on every server update tick — no manual timer needed.
* **Moving Point Streams (`SvUpdatePositionForLPStream`):** `SvUpdatePositionForLPStream(stream, x, y, z)` works **only** on point-based local streams (`SV_STREAM_TYPE_SL_POINT` and `SV_STREAM_TYPE_DL_POINT`). Calling it on player, vehicle, object, or global streams returns `0` (`false`).
* **Adjusting Radius (`SvUpdateDistanceForLStream`):** Works on any local stream (`SL` or `DL`) at runtime (`distance >= 0.0`).

> [!IMPORTANT]
> **Standard GTA Objects vs Streamer Dynamic Objects (`CreateDynamicObject`)**
> * `SvCreateSLStreamAtObject` and `SvCreateDLStreamAtObject` only accept **standard GTA global object IDs** created with `CreateObject`. Both the server and client resolve that ID directly in SA-MP's global object table.
> * **Why Streamer objects do not work directly:** A Streamer ID belongs to the Streamer plugin, not SA-MP's global object pool; the actual per-player GTA object created by Streamer has a different internal ID that changes as players stream in/out, causing silence or audio attached to the wrong object.
> * **How to attach sound to a Streamer dynamic object:** Create a **point stream** (`SvCreateDLStreamAtPoint`) at the object's coordinates using its virtual world and interior (`worldid`, `interiorid`). If the object moves, update the stream with `SvUpdatePositionForLPStream` on a coarse timer (every few hundred milliseconds) or once when movement finishes — each call sends a control packet to every attached listener, so never call it every game tick.

*(Curious about how 3D distance rolloff, stereo panning, wall occlusion, and legacy `v11` client scaling work internally? See [Under the Hood — Client Internals](under-the-hood.md#4-client-internals-sampvoiceasi).)*

---

## 5. Stream Parameters & Smooth Fading

Every stream has 5 `SV_PARAMETER` attributes (`SvStreamParameter*`):

| Constant | Default | Meaning & Rules |
| :--- | :---: | :--- |
| `SV_PARAMETER_FREQUENCY` | `0.0` | Playback pitch / sample rate in Hz. **`0.0` means the original `48000 Hz` sample rate** both in direct sets/resets and in slides (so a relative `SvStreamParameterSlide` when unset starts from `48000 Hz`, while `Get` still returns `0.0` when unset, reset, or finished at `0.0`; values `< 0.0` are rejected and tiny positive values clamp safely to `100 Hz` on playback). |
| `SV_PARAMETER_VOLUME` | `1.0` | Base stream volume (`>= 0.0`; negative rejected). Combined with each listener's individual factor (`SvStreamSetListenerVolume`). |
| `SV_PARAMETER_PANNING` | `0.0` | Stereo offset (`-1.0` left .. `+1.0` right), added to 3D stereo panning. |
| `SV_PARAMETER_EAXMIX` | `-1.0` | Accepted for legacy script compatibility (no audible change). |
| `SV_PARAMETER_SRC` | `1.0` | Playback sample interpolation quality. |

* **Immediate changes:** `SvStreamParameterSet`, `SvStreamParameterReset`, `SvStreamParameterGetEx` *(safe query that leaves your output variable untouched on invalid handles)*, `SvStreamParameterHas`, and `SvStreamParameterGet`.
* **Smooth transitions (`time` in ms):** `SvStreamParameterSlideFromTo`, `SvStreamParameterSlideTo`, and `SvStreamParameterSlide` *(shifts relative to current value by `deltavalue`; allows sliding cleanly across bounds as long as `deltavalue` and `current + deltavalue` are finite)*.

---

## 6. DX8 Audio Effects (`SvEffect*`)

Attach any combination of **9 real-time DirectX 8 / BASS effect types** cleanly to a stream (`SvEffectCreate*` + `SvEffectAttachStream`). Multiple effects — including multiple instances of the same type — can be attached to a stream, and one effect handle can be shared across multiple streams:

| Effect Creator & Updater | Typical Use |
| :--- | :--- |
| `SvEffectCreateChorus` / `SvEffectUpdateChorus` | Vocal doubling / choir thickness |
| `SvEffectCreateCompressor` / `SvEffectUpdateCompressor` | Broadcast speech leveling |
| `SvEffectCreateDistortion` / `SvEffectUpdateDistortion` | Walkie-talkie / megaphone grit |
| `SvEffectCreateEcho` / `SvEffectUpdateEcho` | Stadium / PA slapback echo |
| `SvEffectCreateFlanger` / `SvEffectUpdateFlanger` | Metallic sweep modulation |
| `SvEffectCreateGargle` / `SvEffectUpdateGargle` | Chopper / robot tremolo |
| `SvEffectCreateI3dl2reverb` / `SvEffectUpdateI3dl2reverb` | Full acoustic room simulation |
| `SvEffectCreateParameq` / `SvEffectUpdateParameq` | Narrow-band telephone / dispatch radio EQ |
| `SvEffectCreateReverb` / `SvEffectUpdateReverb` | Hall / indoor reverberation |

* **Live Tuning (`1.3.0+`):** Calling `SvEffectUpdate*` keeps `priority` fixed and updates **SampVoice Plus `1.3.0+`** listeners smoothly in place without interrupting audio (older clients recreate the effect automatically).
* **Cleanup:** Call `SvEffectDetachStream(fx, stream)` and `SvEffectDelete(fx)` when no longer needed (or let automatic per-script unload cleanup handle shutdown).

---

## 7. Icons, Live Name/Color & World Filtering

* **HUD Icons (`SvStreamSetIcon`):** Set `"speaker"` (default), `"radio"`, `"phone"`, `"megaphone"`, `"police"`, `"admin"`, or `"global"` for **SampVoice Plus (`1.0.0+`)** players in the Speaker List and overhead 3D icons (`v11` clients ignore cleanly).
* **Live Name & Color (`SvStreamSetName`, `SvStreamSetColor`):** Rename or recolor streams at runtime without changing handles. Updates **Plus `1.3.0+`** HUD tags immediately; older clients refresh cleanly during silence. Remember `sampvoice.legacy_color_format` controls whether colors expect `0xRRGGBBAA` (default) or `0xAARRGGBB`.
* **World & Interior Filtering (`SvStreamSetWorld`):** Point and object **DL** streams can filter listeners by `worldid` and `interiorid` (`-1` = any), either at creation (`SvCreateDLStreamAtPoint` / `AtObject`) or later at runtime via `SvStreamSetWorld`.

---

## 8. Practical Recipes (See `/voice` in `FS_SampVoicePlus_Demo`)

All 8 patterns below are implemented live inside [`filterscripts/FS_SampVoicePlus_Demo.pwn`](../filterscripts/FS_SampVoicePlus_Demo.pwn):

1. **Proximity Chat (Demo item 2):**
   ```pawn
   g_Local[playerid] = SvCreateDLStreamAtPlayer(35.0, 24, playerid, 0xFFFFFFFF, "Proximity");
   // Update distance, name, color, icon at runtime via SvUpdateDistanceForLStream / SvStreamSet*
   ```
2. **Faction Radio Channel + Live Filter (Demo item 3):**
   ```pawn
   new SV_GSTREAM:radio = SvCreateGStream(0x3498DBFF, "Alpha Radio");
   SvStreamSetIcon(radio, "radio");
   new SV_EFFECT:eq = SvEffectCreateParameq(1, 1800.0, 12.0, 5.0);
   SvEffectAttachStream(eq, radio);
   SvAttachListenerToStream(radio, playerid, 1.0); // Adjust per player via SvStreamSetListenerVolume
   ```
3. **1-on-1 Phone Call (Demo item 4):** Create a `SvCreateGStream` with `"phone"` icon, attach both players as listeners & speakers, call `SvStartRecord(p)` on connect and `SvStopRecord(p)` + `SvDeleteStream` on hangup/disconnect.
4. **Megaphone (Demo item 5):** Create a wide `SvCreateDLStreamAtPlayer(95.0, -1, playerid, 0xF1C40FFF, "Megaphone")` with `"megaphone"` icon and light `SvEffectCreateDistortion`, activated on a separate key (`M` / `0x4D`).
5. **Vehicle Intercom — Occupants Only via SL (Demo item 6):**
   ```pawn
   new SV_SLSTREAM:ic = SvCreateSLStreamAtVehicle(30.0, vehId, 0x1ABC9CFF, "Veh Intercom");
   // In OnPlayerStateChange: SvAttachListenerToStream when entering vehId, SvDetachListenerFromStream on exit
   ```
6. **Movable Speaker Box in World/Interior (Demo item 7):** `SvCreateDLStreamAtPoint(30.0, -1, x, y, z, 0xE84393FF, "Speaker Box", vw, intid)`, move with `SvUpdatePositionForLPStream`, toggle filter with `SvStreamSetWorld`, clear with `SvDetachAllListenersFromStream` + `SvDetachAllSpeakersFromStream`.
7. **Standard GTA Moving Object Audio (Demo item 8):** Create object via `CreateObject`, attach `SvCreateDLStreamAtObject(30.0, -1, objId, ...)`, then `MoveObject(objId, ...)` — voice tracks the moving object automatically.
8. **Streamer Dynamic Object Audio:** Create `SvCreateDLStreamAtPoint` at the `CreateDynamicObject` coordinates with its `worldid`/`interiorid`; if moved dynamically, sync via a timer (e.g., `250–500 ms`) calling `SvUpdatePositionForLPStream`.

> [!NOTE]
> Full native signatures, return values, and invalid-input rules are documented in **[Complete API Reference](api-reference.md)**.
