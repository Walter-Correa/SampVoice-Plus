# Android Launcher Integration Guide (`libsvplus_mobile.so`)

Step-by-step guide for integrating the **SampVoice Plus** C SDK (`SampVoice-Plus-Android-SDK.zip`) into an Android SA-MP launcher.

> [!NOTE]
> **Verification scope:** All `libsvplus_mobile.so` API contracts, packet/parity behavior, bounded queueing, and BASS/capture retry paths are verified with local test harnesses on Windows (mock BASS and mock server), while the Android CI build compiles and checks the `arm64-v8a`, `armeabi-v7a`, and `x86_64` libraries. Runtime Android hardware behaviors (device microphone routing, Android BASS DX8 filter support, OS permission grant resume, and GTA SA mobile LOS occlusion) are marked *to be confirmed on device*.

---

## 1. What It Is & Why Use It

`libsvplus_mobile.so` embeds the **SampVoice Plus** client (`v12`, `SVP1`) directly inside an Android SA-MP launcher using the same portable C++20 core (`svplus_client_core`) as the desktop `.asi`.

* **Works with all SampVoice 3.x & Plus servers:** Legacy `v11` servers (CyberMor 3.x, AmyrAhmady) keep working automatically through standard `v11` fallback, and mobile players can speak with and hear PC `v11`, Android `v11`, and Plus `v12` players simultaneously.
* **Unlocks SampVoice Plus features on Plus servers:** Adds client support for dynamic buttons for server-registered keys (`SvAddKey`), HUD speaker tags, stream colors and icons, server talk-mode policy (`toggle_allowed`), overhead voice-status icons, control RTT latency reporting (`SvGetPlayerVoiceLatency`), live stream name/color updates, and optional wall voice occlusion (within Android platform constraints).
* **Works alongside server-wide features:** Server-side features — per-pair/timed mutes, channel priority ducking, and key-to-stream bindings (`SvBindKeyToStream`) — apply to every connected client, including `v11` launchers.
* **Server detection:** The mobile SDK advertises `SV_CAP_MOBILE` (`1 << 4`) during the `SVP1` handshake so Pawn scripts can detect Plus mobile launchers and track `SV_STAT_MOBILE_PLAYERS`.

---

## 2. SDK Package (`SampVoice-Plus-Android-SDK.zip`)

Each SampVoice Plus release publishes `SampVoice-Plus-Android-SDK.zip` with the following layout:

```text
SampVoice-Plus-Android-SDK.zip
├── include/
│     └── svplus_mobile.h
├── lib/
│     ├── arm64-v8a/libsvplus_mobile.so
│     ├── armeabi-v7a/libsvplus_mobile.so
│     └── x86_64/libsvplus_mobile.so
├── LICENSE
└── THIRD-PARTY-NOTICES.txt
```

* **Minimum Android API:** `android-21` (Android 5.0).
* **Memory page alignment:** Built with `-Wl,-z,max-page-size=16384` (16 KB ELF `LOAD` alignment on 64-bit ABIs).
* **Strict symbol & runtime isolation:**
  * Exports **strictly `svp_*` symbols** (`C`/`C++` visibility hidden + version script + `-Wl,--exclude-libs,ALL`, preventing `libopus` symbol collisions with the launcher).
  * Links statically against `c++_static` (`ANDROID_STL=c++_static`) — **no `libc++_shared.so` dependency**. Verified `DT_NEEDED` dependencies are limited to NDK system libraries (`libc.so`, `libm.so`, `libdl.so`, `liblog.so`).
* **License:** **MPL-2.0**. Closed-source/proprietary launchers may link and ship `libsvplus_mobile.so` unchanged; modifications to SampVoice Plus source files remain governed by MPL-2.0 (see [`LICENSE`](../LICENSE) and [`THIRD-PARTY-NOTICES.txt`](../THIRD-PARTY-NOTICES.txt)).

---

## 3. Before You Start

You need three things wired up in your launcher:

1. **SA-MP Netcode Hooks:**
   * Hook outgoing `RPC 25` (`PlayerConnect`) right after the game writes the standard join payload so you can append the 16-byte SampVoice join trailer.
   * Receive RakNet connection and disconnection events (`svp_set_connected`).
   * Intercept incoming RakNet packets with ID `222` (`svp_on_control_packet`) and send outgoing `222` packets via RakNet `HIGH_PRIORITY`, `RELIABLE`, ordering channel `0` inside `host->send_control`.
2. **BASS 2.4 (`libbass.so` or custom/static symbols):**
   * BASS **must be loaded and initialized (`BASS_Init`) by the game or launcher**. `libsvplus_mobile.so` never calls `BASS_Init` and waits until the audio device is initialized (`BASS_GetDevice()` / `BASS_RecordInit`).
   * `libbass.so` is **not bundled** inside `SampVoice-Plus-Android-SDK.zip`.
   * **BASS licensing (Un4seen)** is the responsibility of the launcher.
3. **Android Microphone Permission:**
   * Request `android.permission.RECORD_AUDIO` before/during gameplay so BASS recording capture succeeds.

---

## 4. Disable Legacy Voice in Your Launcher

> [!IMPORTANT]
> If your launcher currently embeds a legacy SampVoice `v11` client, whether embedded in the launcher's SA-MP library or shipped as a separate module, **disable it completely** before enabling `libsvplus_mobile.so`.

* The server scans `RPC 25` (`PlayerConnect`) for the **first** `0xDEADBEEF` signature (`ConnectPacket`).
* If two voice clients append trailers to `RPC 25`, the server reads only the first one and ignores the second — breaking Plus capability negotiation (`SVP1`), duplicate-key handling, or audio routing.
* Only `libsvplus_mobile.so` should append to `RPC 25` and handle RakNet packet ID `222`.

---

## 5. Step-by-Step Integration

### Step 1: Load the Library & Verify ABI Version

Load `lib/<abi>/libsvplus_mobile.so` (or link against it) and verify `SVP_API_VERSION`:

```c
#include "svplus_mobile.h"

if (svp_get_api_version() != SVP_API_VERSION) {
    /* Binary / header ABI mismatch — do not call session APIs */
    return;
}
/* Optional: svp_get_version() returns packed e.g. 10400 for 1.4.0 */
```

### Step 2: Create the Voice Session (`svp_create`)

Initialize `svp_host_t` and `svp_config_t` with **`struct_size = sizeof(...)`**, bind the mandatory callbacks (`send_control`, `get_camera`, `get_local_ped_position`), and create one process-wide session:

```c
static svp_session_t* g_svp = NULL;

static void on_send_control(void* user, const uint8_t* bytes, uint32_t size) {
    /* Called strictly on the game thread inside svp_frame(). */
    /* Send RakNet packet (bytes[0] == 222): HIGH_PRIORITY, RELIABLE, channel 0. */
    launcher_rak_send((RakClient*)user, bytes, size);
}

static int32_t on_get_camera(void* user, float pos[3], float front[3], float up[3]) {
    (void)user;
    return game_read_camera(pos, front, up) ? 1 : 0;
}

static int32_t on_get_local_ped(void* user, float pos[3]) {
    (void)user;
    return game_read_local_ped_pos(pos) ? 1 : 0;
}

void init_voice(RakClient* rak) {
    svp_host_t host = {0};
    host.struct_size            = sizeof(svp_host_t);
    host.user                   = rak;
    host.send_control           = on_send_control;
    host.get_camera             = on_get_camera;
    host.get_local_ped_position = on_get_local_ped;
    /* Optional callbacks: player/vehicle/object pos, vehicle check, LOS, decode_text, bass, log */
    host.get_player_position    = on_get_player_pos;
    host.get_vehicle_position   = on_get_vehicle_pos;
    host.get_object_position    = on_get_object_pos;
    host.is_local_in_vehicle    = on_is_local_in_vehicle;

    svp_config_t cfg = {0};
    cfg.struct_size          = sizeof(svp_config_t);
    cfg.sound_enable         = 1;
    cfg.sound_volume         = 100;
    cfg.local_volume         = 100;
    cfg.global_volume        = 100;
    cfg.micro_enable         = 1;
    cfg.micro_gain           = 100;
    cfg.micro_noise_suppress = 1;
    cfg.talk_mode            = SVP_TALK_MODE_PUSH_TO_TALK;

    svp_result_t rc = svp_create(&host, &cfg, &g_svp);
    if (rc != SVP_OK) {
        /* Handle error (SVP_ERR_STATE if a session already exists in this process) */
    }
}
```

### Step 3: Append the 16-Byte Trailer to `RPC 25` (`PlayerConnect`)

Right after the game serializes the vanilla SA-MP `RPC 25` `BitStream` (same point legacy `v11` clients hook), call `svp_build_join_payload` and write its `16` bytes at the end of the `BitStream`:

```c
uint8_t  trailer[SVP_JOIN_PAYLOAD_SIZE];
uint32_t trailer_len = 0;

if (svp_build_join_payload(g_svp, server_host, server_ipv4_net, server_port,
                           trailer, sizeof(trailer), &trailer_len) == SVP_OK) {
    rak_bitstream_write_bytes(rpc25_bs, trailer, trailer_len); /* 16 bytes */
}
```

* `server_ipv4_net` is the server IPv4 address RakNet is connected to, in network byte order (always known when `RPC 25` is sent; always pass this value). If you pass `0`, `server_host` must be a numeric IPv4 string, because hostnames are not resolved for UDP. Always provide `server_host` and `server_port` too so `server_endpoint` matches cleanly for `svp_player_entry_t`.
* The 16-byte trailer contains `ConnectPacket` (`0xDeadBeef`, `version = 11`, `hasMicro`) + `ConnectPlusExtension` (`0x53565031`, `AdaptiveJitter | Telemetry | Mobile`, packed build code). Legacy `v11` servers ignore the extra 10 bytes after reading `ConnectPacket`.

### Step 4: Report RakNet Connect & Disconnect (`svp_set_connected`)

Notify the SDK when the RakNet session connects and disconnects:

```c
/* When RakNet connection opens (any thread, typically RakNet thread): */
svp_set_connected(g_svp, 1);

/* When RakNet disconnects or times out: */
svp_set_connected(g_svp, 0);
```

Calling `svp_set_connected(1)` slightly after the first `222` control packets arrive is tolerated cleanly; the session survives server reconnects.

### Step 5: Deliver Incoming RakNet Control Packets (`222`)

Pass every incoming RakNet packet whose buffer starts with the 1-byte RakNet packet ID to `svp_on_control_packet`:

```c
uint8_t consumed = 0;
if (svp_on_control_packet(g_svp, pkt_data, pkt_size, &consumed) == SVP_OK && consumed) {
    /* Packet ID was 222 — consumed by SampVoice Plus; skip default game handling */
    return;
}
```

### Step 6: Pump `svp_frame()` Every Frame on the Game Thread

Call `svp_frame(g_svp)` once per frame on a **single game thread** — **including while loading screens are active**:

```c
void on_game_loop_frame(void) {
    if (g_svp) {
        svp_frame(g_svp);
    }
}
```

`svp_frame()` decodes queued player names (`decode_text`), applies queued incoming `222` control packets in arrival order, drains outgoing control packets to `host->send_control`, and snapshots camera/entity coordinates and optional LOS occlusion.

### Step 7: Sync Connected Player Names (`svp_set_player_name` / `svp_clear_player`)

Keep the speaker name cache updated as players join, change names, or disconnect:

```c
svp_set_player_name(g_svp, player_id, raw_player_name); /* Join or SetPlayerName */
svp_clear_player(g_svp, player_id);                     /* Disconnect */
```

Both functions are thread-safe and stage raw bytes for deferred `decode_text` conversion on the next `svp_frame()`.

### Step 8: Render Buttons From `svp_get_keys` & Send Touch Input (`svp_set_key_state`)

Instead of hardcoding a single `B` talk button, poll `svp_get_keys` (or watch `status.keys_generation` from `svp_get_status`) and render one on-screen button per server-registered key:

```c
static uint64_t s_last_keys_gen = 0;
static uint8_t  s_keys[64];
static uint32_t s_key_count = 0;

void refresh_talk_buttons(void) {
    uint32_t count = 0;
    uint64_t gen   = 0;
    if (svp_get_keys(g_svp, s_keys, (uint32_t)sizeof(s_keys), &count, &gen) == SVP_OK) {
        if (gen != s_last_keys_gen) {
            s_last_keys_gen = gen;
            s_key_count     = count; /* Up to count keys registered by SvAddKey */
            rebuild_hud_talk_buttons(s_keys, count);
        }
    }
}
```

Map Win32 Virtual-Key codes (`vk_code`) to concise button labels (common examples: `0x42` -> `"B"`, `0x5A` -> `"Z"`, `0x4E` -> `"N"`, `0x58` -> `"X"`, `0x56` -> `"V"`, `0x47` -> `"G"`, `0x12` -> `"Alt"`, `0x14` -> `"CapsLock"`, `0x20` -> `"Space"`). When the player touches or releases a button:

```c
svp_set_key_state(g_svp, vk_code, 1); /* Touch down */
svp_set_key_state(g_svp, vk_code, 0); /* Touch up / touch cancel */
```

Toggle talk mode (`SVP_TALK_MODE_TOGGLE`) is handled internally by `libsvplus_mobile.so` whenever `status.toggle_allowed != 0`; the launcher always passes physical touch press (`1`) and release (`0`).

### Step 9: Handle Chat/Dialogs & App Backgrounding

Block key transmission while chat input or native/SA-MP dialogs are focused, and suspend/resume cleanly on Android `Activity` lifecycle events:

```c
svp_set_input_blocked(g_svp, chat_or_dialog_open ? 1 : 0);

/* Android Activity lifecycle: */
svp_set_suspended(g_svp, 1); /* onPause */
svp_set_suspended(g_svp, 0); /* onResume */
```

### Step 10: Query Status, Active Speakers, My Streams & Voice Icons

Poll HUD state from the UI/render thread (all getters are non-blocking):

```c
svp_status_t st = {0};
st.struct_size = sizeof(svp_status_t);
if (svp_get_status(g_svp, &st) == SVP_OK) {
    /* st.connected, st.server_has_voice, st.voice_ready, st.mic_available,
       st.transmitting, st.input_level, st.server_muted, st.toggle_allowed,
       st.nearby_list_enabled, st.occlusion_enabled, st.latency_ms (<0 if unknown),
       st.server_endpoint, st.keys_generation, st.speakers_generation */
}

/* Array calls: ALWAYS set element 0's struct_size before calling! */
svp_speaker_info_t speakers[32] = {0};
speakers[0].struct_size = sizeof(svp_speaker_info_t);
uint32_t speaker_count = 0;
if (svp_get_speakers(g_svp, speakers, 32, &speaker_count) == SVP_OK) {
    for (uint32_t i = 0; i < speaker_count && i < 32; ++i) {
        /* speakers[i].player_name, stream_name, stream_color, icon_id (SVP_ICON_*),
           blocked, tag_count, tags[0..tag_count-1].{color, name} (all UTF-8) */
    }
}

svp_local_stream_t my_streams[16] = {0};
my_streams[0].struct_size = sizeof(svp_local_stream_t);
uint32_t stream_count = 0;
svp_get_my_streams(g_svp, my_streams, 16, &stream_count);

int32_t voice_state = svp_get_player_voice_state(g_svp, remote_player_id);
/* Returns SVP_VOICE_STATE_OK (0), NO_MICRO (1), CANNOT_HEAR (2), or < 0 on error */
```

`st.nearby_list_enabled` controls whether the launcher's player list offers the nearby-players filter. Voice-status icons follow `svp_get_player_voice_state()`, which already respects the server's `voice_status` setting (returning `SVP_VOICE_STATE_OK` for everyone when disabled).

### Step 11: Update Settings & Launcher-Owned Per-Player Mutes/Volumes

Publish updated user audio/capture/deafen settings via `svp_set_config`:

```c
svp_config_t cfg = {0};
cfg.struct_size          = sizeof(svp_config_t);
cfg.sound_enable         = 1;
cfg.sound_volume         = 100; /* 0..100 */
cfg.local_volume         = 100; /* 0..200 */
cfg.global_volume        = 100; /* 0..200 */
cfg.micro_enable         = 1;
cfg.micro_gain           = 100; /* 0..200% */
cfg.micro_noise_suppress = 1;
cfg.talk_mode            = SVP_TALK_MODE_PUSH_TO_TALK;
cfg.deafened             = 0;
svp_set_config(g_svp, &cfg);
```

And supply per-player mute / volume overrides via `svp_set_player_entries` (matching `server_endpoint` from `svp_get_status()`):

```c
svp_player_entry_t entries[1] = {0};
entries[0].struct_size = sizeof(svp_player_entry_t);
snprintf(entries[0].player_name, sizeof(entries[0].player_name), "%s", "Player_Name");
snprintf(entries[0].server_endpoint, sizeof(entries[0].server_endpoint), "%s", st.server_endpoint);
entries[0].scope      = SVP_BLACKLIST_SCOPE_SERVER; /* or SVP_BLACKLIST_SCOPE_ALL */
entries[0].muted      = 0;
entries[0].volume_pct = 50; /* 0..200 (100 = unity) */

svp_set_player_entries(g_svp, entries, 1);
```

> [!NOTE]
> `libsvplus_mobile.so` performs **zero file I/O**. **The launcher owns persistence** of user settings (`svp_config_t`) and per-player blacklist/volume lists (`svp_player_entry_t`) in Android SharedPreferences/JSON/files.

### Step 12: Destroy on Process Exit (`svp_destroy`)

Call `svp_destroy(g_svp)` once at shutdown. Ensure no other `svp_*` call for `g_svp` is running concurrently during `svp_destroy()` and none is issued afterwards. Only one active session is allowed per process (`svp_create` may be called again cleanly after `svp_destroy` returns).

---

## 6. Threading Contract

### C API Functions (`svp_*`)

| Function | Safe Calling Thread(s) | Notes |
| :--- | :--- | :--- |
| `svp_get_api_version`, `svp_get_version` | Any thread | Lock-free constants. |
| `svp_create` | Any thread (init) | Starts the `10 ms` audio thread. Returns `SVP_ERR_STATE` if already created. |
| `svp_destroy` | Owner shutdown thread | Caller must synchronize: no concurrent or subsequent `svp_*` calls on `session`. |
| `svp_build_join_payload`, `svp_set_connected`, `svp_on_control_packet` | Any thread (e.g. RakNet thread) | `svp_on_control_packet` queues packets bounded to `8192` pkts / `4 MB` for `svp_frame()`. |
| `svp_frame` | **Single game thread only** | Run every frame (including loading screens). Logs one warning if invoked from a second thread. |
| `svp_set_player_name`, `svp_clear_player` | Any thread | Staged lock-safely for deferred decoding inside `svp_frame()`. |
| `svp_set_key_state`, `svp_set_input_blocked`, `svp_set_suspended` | Any thread | Lock-free / atomic updates. |
| `svp_set_config`, `svp_set_player_entries` | Any thread | Atomic / synchronized snapshot swap. |
| `svp_get_keys`, `svp_get_status`, `svp_get_speakers`, `svp_get_my_streams`, `svp_get_player_voice_state` | Any thread (UI/render) | Non-blocking snapshots into caller buffers. |

### Host Callbacks (`svp_host_t`)

| Callback | Required | Invocation Thread |
| :--- | :---: | :--- |
| `send_control` | **Yes** | Strictly **game thread inside `svp_frame()`** |
| `get_camera`, `get_local_ped_position` | **Yes** | Strictly **game thread inside `svp_frame()`** |
| `get_player_position`, `get_vehicle_position`, `get_object_position`, `is_local_in_vehicle` | No | Strictly **game thread inside `svp_frame()`** |
| `is_line_of_sight_clear`, `decode_text` | No | Strictly **game thread inside `svp_frame()`** |
| `get_bass_proc` | No | **Background audio thread** during `svp_create()` |
| `log` | No | **Any internal thread** (background audio/network or game thread) — must be thread-safe |

---

## 7. Coordinate, Entity & ABI Conventions

* **World coordinates:** Standard GTA San Andreas world coordinates (`X` East, `Y` North, `Z` Up).
* **Camera orientation:** `front[3]` and `up[3]` must be unit vectors matching GTA `TheCamera` (`CPlaceable::m_matrix` `at`/`front` and `up` vectors), matching the desktop PC client.
* **Object IDs:** `get_object_position` and `is_line_of_sight_clear(..., ignore_object_id)` operate strictly on **global GTA objects** (`CreateObject` IDs `0..MAX_OBJECTS-1`, with `SVP_INVALID_OBJECT_ID` `0xFFFF` meaning none), **not** SA-MP Streamer (`CreateDynamicObject`) IDs.
* **Player positions:** `get_player_position` is queried for the target player of player-anchored streams (`SvCreateSLStreamAtPlayer` / `SvCreateDLStreamAtPlayer`). Return `0` when that player is not currently streamed in; the stream stays without a position until they stream back in.
* **Binary compatibility (`struct_size`):**
  * Initialize `struct_size = sizeof(the_struct)` on every input/output struct (`>= V1` size, otherwise rejected with `SVP_ERR_INVALID_ARG`).
  * For array parameters (`svp_set_player_entries`, `svp_get_speakers`, `svp_get_my_streams`), element `0`'s `struct_size` defines the **byte stride** used to step across all elements (`count` > 0).
  * If a caller passes a struct larger than known by the compiled `.so`, trailing bytes beyond what the library understands are left untouched, and output `struct_size` reports bytes written by the library.

---

## 8. Optional Callbacks

| Callback | Purpose | Behavior When Omitted (`NULL`) |
| :--- | :--- | :--- |
| `is_line_of_sight_clear` | World wall/object LOS check between camera/listener and speaker source for voice occlusion (`~1000 Hz` LPF + `-6 dB`). Return `1` if clear, `0` if occluded. | Wall voice occlusion remains disabled (audio is never muffled). |
| `decode_text` | Converts legacy single-byte server encodings (e.g. Windows-1251, Windows-1252, Windows-1250) into NUL-terminated UTF-8. Return `1` on success. | Falls back to ISO-8859-1 -> UTF-8 conversion cleanly (also used if `decode_text` returns `0`). |
| `get_bass_proc` | Custom symbol resolver (`dlsym` alternative) when BASS is statically linked or loaded with custom/renamed exports. | Uses `dlopen("libbass.so", RTLD_NOLOAD)` first (reusing the already-loaded library handle; the reference is released via `dlclose` in `svp_destroy()`), falling back to `dlopen("libbass.so", RTLD_LAZY \| RTLD_LOCAL)` + `dlsym`. |
| `log` | Custom log sink receiving `(user, SVP_LOG_*, message)`. | Writes directly to Android logcat (tag **`SampVoicePlus`**). |

---

## 9. Android Platform Limits & Verification Status

| Area | Behavior & Notes |
| :--- | :--- |
| **DX8 audio effects (`SvEffectCreate*`)** | If Android BASS rejects `BASS_ChannelSetFX` for a DX8 effect type (not all Windows DirectX 8 DSP filters exist on Android BASS builds), playback **continues normally without that effect** and one log warning is emitted per refused effect type. *(Refusal fallback verified in harness; Android BASS FX matrix **to be confirmed on device**.)* |
| **Wall voice occlusion** | Active only when `host->is_line_of_sight_clear` is supplied and server enables `sampvoice.voice_occlusion`. *(Logic verified in harness; GTA Android world LOS behavior **to be confirmed on device**.)* |
| **Microphone input** | Opens the default BASS recording device (`BASS_RecordInit(-1)`). *(Harness-verified capture lifecycle; Android hardware device selection/latency **to be confirmed on device**.)* |
| **Missing `RECORD_AUDIO` permission** | If `BASS_RecordInit` / `BASS_RecordStart` fails because permission has not been granted yet, capture retries automatically every `2 s` and recovers once permission is granted. *(Retry path verified in harness; Android runtime permission flow **to be confirmed on device**.)* |

---

## 10. Common Issues

| Symptom | Likely Cause | Resolution |
| :--- | :--- | :--- |
| **No voice playback or capture at all** | `BASS_Init` was never called by the game/launcher (or failed). | Ensure BASS is initialized before or shortly after `svp_create()`, or provide `get_bass_proc` if BASS isn't loaded as `libbass.so`. |
| **Server sees client as `v11` (not Plus)** | Legacy SampVoice hook still active in the launcher, emitting a duplicate `0xDEADBEEF` signature in `RPC 25`. | Remove/disable any legacy SampVoice `RPC 25` patch so `svp_build_join_payload` is the only trailer appended. |
| **No push-to-talk buttons show in HUD** | Server hasn't registered keys via `SvAddKey`, or `svp_frame()` isn't running. | Check `svp_get_status()` (`connected`, `voice_ready`) and confirm `svp_frame()` runs every frame. |
| **Audio stutters or drops control packets during map load** | `svp_frame()` stopped being called during loading screens or pauses. | Keep calling `svp_frame()` once per frame even during loading screens. Queues cap at `8192` packets / `4 MB` and drop newest packets with a rate-limited `WRN` until `svp_frame()` resumes. |
| **Need runtime logs on Android** | Searching wrong logcat tag or custom sink drops debug lines. | Filter logcat for tag **`SampVoicePlus`** (`adb logcat -s SampVoicePlus`). |

---

## 11. Notes for Server Owners

* **Identify Plus mobile clients in Pawn:** Check `SvGetPlusCapabilities(playerid) & SV_CAP_MOBILE` inside `OnPlayerVoiceReady`. *(Note: older launchers still using legacy Android `v11` report `0`.)*
* **Monitor connected mobile count:** Read `stats[SV_STAT_MOBILE_PLAYERS]` from `SvGetServerStats(stats)`.
* **Activation keys on Android:** Legacy `v11` Android launchers hardcode key `0x42` (`B`), whereas launchers built with the **SampVoice Plus Android SDK** dynamically display buttons for every key registered via `SvAddKey(playerid, key)` and send exact key press/release packets.
* **Key-to-stream bindings:** `SvBindKeyToStream` works seamlessly with both `v11` Android launchers (on `0x42`) and Plus mobile SDK launchers (on any bound key).

---

*See also: [Complete API Reference](api-reference.md) · [Under the Hood (Mobile C SDK Internals)](under-the-hood.md#mobile-c-sdk-libsvplus_mobileso) · [`svplus_mobile.h`](../client/mobile/include/svplus_mobile.h)*
