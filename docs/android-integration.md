# Android Launcher Integration Guide (`libsvplus_mobile.so`)

Step-by-step guide for integrating the **SampVoice Plus** C SDK (`SampVoice-Plus-Android-SDK.zip`) into an Android SA-MP launcher (native C/C++, Java/JNI, or FFI/script hosts).

> [!NOTE]
> **Verification scope:**
> * **Confirmed on real device (`28/11/2026`, ReSA `1.1.8`, `arm64-v8a`, via script host on SDK `1.4.0` and `1.5.0`):** `SVP1` + `SV_CAP_MOBILE` handshake, bidirectional RakNet `222` control packets, microphone capture, 3D and stereo voice playback, DX8 effects used by `FS_SampVoicePlus_Demo` (`Parameq` and `Distortion`), wall voice occlusion, toggle talk mode, server mute, app backgrounding, reconnects, and coexistence with the launcher's legacy native voice left ON. On SDK `1.5.0`, also confirmed: `svp_build_join_rpc()`, `svp_status_t.audio_ready`, `svp_get_key_states()` (lit toggle buttons), `svp_get_tx_stats()`, the `svp_frame()` watchdog (10 s Home/background test), and clean audio when a speaker talks again within 5 s (PC listener on `sampvoice.asi` `1.5.0`).
> * **Verified in local harnesses (mock BASS / mock server) — to be confirmed on device:** runtime `RECORD_AUDIO` permission grant mid-session, `armeabi-v7a` and `x86_64` ABIs on physical devices, and automatic camera `up` derivation (`1.5.0`; the script host used in the device test computed `up` itself).

---

## 1. What It Is & Why Use It

`libsvplus_mobile.so` embeds the **SampVoice Plus** client (`v12`, `SVP1`) inside an Android SA-MP launcher using the same portable C++20 core (`svplus_client_core`) as the desktop `.asi`.

* **Works with all SampVoice 3.x & Plus servers:** Legacy `v11` servers (CyberMor 3.x, AmyrAhmady) keep working automatically through standard `v11` fallback, and mobile players can speak with and hear PC `v11`, Android `v11`, and Plus `v12` players simultaneously.
* **Unlocks SampVoice Plus features on Plus servers:** Adds client support for dynamic buttons for server-registered keys (`SvAddKey`), HUD speaker tags, stream colors and icons, server talk-mode policy (`toggle_allowed`), overhead voice-status icons, per-channel priority ducking, live stream name/color updates, and optional wall voice occlusion (within Android platform constraints).
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
   * Hook outgoing `RPC 25` (`PlayerConnect`) right after the game serializes the standard join payload so you can build or append the 16-byte SampVoice join trailer.
   * Receive RakNet connection and disconnection events (`svp_set_connected`).
   * Intercept incoming RakNet packets with ID `222` (`svp_on_control_packet`) when the current connection sent the Plus trailer, and send outgoing `222` packets via RakNet `HIGH_PRIORITY`, `RELIABLE`, ordering channel `0` inside `host->send_control`.
2. **BASS 2.4 (`libbass.so` or custom/static symbols):**
   * BASS **must be loaded and initialized (`BASS_Init`) by the game or launcher**. `libsvplus_mobile.so` never calls `BASS_Init` and polls on the background audio thread until the output device is initialized (`BASS_GetDevice()` / `BASS_RecordInit`).
   * `libbass.so` is **not bundled** inside `SampVoice-Plus-Android-SDK.zip`.
   * **BASS licensing (Un4seen)** is the responsibility of the launcher.
3. **Android Microphone Permission:**
   * Request `android.permission.RECORD_AUDIO` before/during gameplay so BASS recording capture succeeds.

---

## 4. Disable Legacy Voice in Your Launcher

> [!IMPORTANT]
> If your launcher currently embeds a legacy SampVoice `v11` client, whether embedded in the launcher's SA-MP library or shipped as a separate module, **disable it completely** before enabling `libsvplus_mobile.so`.

* The server scans `RPC 25` (`PlayerConnect`) for the **first** `0xDEADBEEF` signature (`ConnectPacket`).
* If a legacy voice module still injects a `0xDEADBEEF` trailer before your hook runs and you only append at the end, the server reads the first (legacy) trailer and ignores Plus (`SVP1`).
* Since `1.5.0`, calling `svp_build_join_rpc()` scans `RPC 25` for any byte-aligned `0xDEADBEEF` (`EF BE AD DE`) and **replaces that first legacy trailer in place** with the 16-byte Plus trailer. When that happens (confirmed on real device), SampVoice Plus takes over the connection cleanly while native voice receives no `ServerInfo` reply and displays no native microphone button. Disabling legacy voice in settings is still recommended so unused capture/packet code does not run needlessly.

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
/* Optional: svp_get_version() returns packed e.g. 10500 for 1.5.0 */
const uint32_t ver = svp_get_version();
const bool has_v15 = (ver >= 10500u); /* Required for svp_build_join_rpc, status.audio_ready, zero camera up */
```

### Step 2: Create the Voice Session (`svp_create`)

Create the session and install your `RPC 25` / packet `222` hooks **before** joining the server. Initialize `svp_host_t` and `svp_config_t` with **`struct_size = sizeof(...)`**, bind the mandatory callbacks (`send_control`, `get_camera`, `get_local_ped_position`), and create one process-wide session:

```c
static svp_session_t* g_svp = NULL;
static bool           g_plus_handshake_ok = false;

static void on_send_control(void* user, const uint8_t* bytes, uint32_t size) {
    /* Called strictly on the game thread inside svp_frame(). */
    /* Send RakNet packet (bytes[0] == 222): HIGH_PRIORITY, RELIABLE, channel 0. */
    launcher_rak_send((RakClient*)user, bytes, size);
}

static int32_t on_get_camera(void* user, float pos[3], float front[3], float up[3]) {
    (void)user;
    return game_read_camera(pos, front, up) ? 1 : 0; /* Since 1.5.0: up may be (0, 0, 0) */
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

* **BASS readiness:** `svp_create()` never blocks waiting for BASS. Its background audio thread probes `libbass.so` / `get_bass_proc` every `10 ms` until BASS is initialized; check `st.audio_ready != 0` in `svp_get_status()` (`1.5.0+`).

### Step 3: Build or Append the `RPC 25` (`PlayerConnect`) Plus Payload

> [!NOTE]
> The handshake occurs **only** in the outgoing join `RPC 25`. Creating a session after already joining takes effect only after reconnecting. Track whether `RPC 25` succeeded with a per-connection flag (`g_plus_handshake_ok`).

**Recommended path (`svp_build_join_rpc`, Since `1.5.0`):**

Pass the raw `RPC 25` buffer serialized by the game (`rpc_in`, `rpc_in_bits`) directly to `svp_build_join_rpc()`. It handles `0xDEADBEEF` deduplication/replacement, partial-byte zero-padding, and endpoint recording in one call (`out_buf` may alias `rpc_in` if sized at least `rpc_bytes + SVP_JOIN_PAYLOAD_SIZE`):

```c
uint8_t  out_rpc[512];
uint32_t out_len = 0;
int32_t  replaced_offset = -1;

g_plus_handshake_ok = false;
if (svp_build_join_rpc(g_svp, server_host, server_ipv4_net, server_port,
                       rpc_in_bytes, rpc_in_bits,
                       out_rpc, sizeof(out_rpc),
                       &out_len, &replaced_offset) == SVP_OK) {
    replace_or_return_rpc_bitstream(out_rpc, out_len);
    g_plus_handshake_ok = true;
}
```

**Manual path (`svp_build_join_payload`):**

Use only when targeting SDK `< 1.5.0` or when the host guarantees no legacy `0xDEADBEEF` trailer is present in `RPC 25`:

```c
uint8_t  trailer[SVP_JOIN_PAYLOAD_SIZE];
uint32_t trailer_len = 0;

if (svp_build_join_payload(g_svp, server_host, server_ipv4_net, server_port,
                           trailer, sizeof(trailer), &trailer_len) == SVP_OK) {
    rak_bitstream_write_bytes(rpc25_bs, trailer, trailer_len); /* 16 bytes */
    g_plus_handshake_ok = true;
}
```

* **Hook APIs delivering a temporary copy:** If your hook system passes a temporary copy of `RPC 25` instead of the mutable outgoing `BitStream`, writing bytes into the input `BitStream` has no effect on the packet actually sent on the wire. Construct a fresh `BitStream` from `out_rpc[0..out_len-1]` and return it in the exact tuple format required by your hook layer (some script/loader layers strictly validate each return slot's type and silently discard the whole return tuple without warning if any slot type mismatches).
* **Address fields:** `server_ipv4_net` is the connected RakNet IPv4 address in network byte order (if `0`, `server_host` must be a numeric IPv4 string, not a hostname). Always pass `server_host` and `server_port` so `server_endpoint` matches `svp_player_entry_t`.
* **Verify on server:** When the server processes `RPC 25`, Plus `1.5.0+` servers log at `Info` level in `server_log.txt`:
  `[sv+:INF] Player <id> voice connected: SampVoice Plus <version> (wire v11, proto v12, mic=yes|no, mobile)`
  and `FS_SampVoicePlus_Demo` shows `Mobile:Yes` under `"1. My voice status"`.

### Step 4: Report RakNet Connect & Disconnect (`svp_set_connected`)

Notify the SDK when the RakNet session connects and disconnects:

```c
/* When RakNet connection opens (any thread, typically RakNet thread): */
svp_set_connected(g_svp, 1);

/* When RakNet disconnects or times out: */
g_plus_handshake_ok = false;
svp_set_connected(g_svp, 0);
```

Calling `svp_set_connected(1)` slightly after the first `222` control packets arrive is tolerated cleanly; the session survives server reconnects.

### Step 5: Deliver Incoming RakNet Control Packets (`222`)

Feed packet `222` to `svp_on_control_packet()` **only when the current connection carried the Plus trailer (`g_plus_handshake_ok == true`)** — otherwise forward packet `222` untouched to the game or native voice handler:

```c
if (g_plus_handshake_ok) {
    uint8_t consumed = 0;
    if (svp_on_control_packet(g_svp, pkt_data, pkt_size, &consumed) == SVP_OK && consumed) {
        /* Packet ID was 222 — consumed by SampVoice Plus; skip default game handling */
        return;
    }
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

`svp_frame()` decodes queued player names (`decode_text`), applies queued incoming `222` control packets in arrival order, drains outgoing control packets to `host->send_control`, and snapshots camera/entity coordinates and optional LOS occlusion. Host callbacks invoked here must remain fast and must **never** throw C++ exceptions or `longjmp` across the SDK boundary (`noexcept` in C++, `pcall` in Lua/FFI).

### Step 7: Sync Connected Player Names (`svp_set_player_name` / `svp_clear_player`)

Keep the speaker name cache updated as players join, change names, or disconnect:

```c
svp_set_player_name(g_svp, player_id, raw_player_name); /* Join or SetPlayerName */
svp_clear_player(g_svp, player_id);                     /* Disconnect */
```

Both functions are thread-safe and stage raw bytes for deferred `decode_text` conversion on the next `svp_frame()`.

### Step 8: Render Buttons From `svp_get_keys` / `svp_get_key_states` & Send Touch Input (`svp_set_key_state`)

Instead of hardcoding a single `B` talk button, poll `svp_get_key_states` (or `svp_get_keys` / `status.keys_generation` from `svp_get_status`) and render one on-screen button per server-registered key, highlighting any button whose `active[i] != 0` (held in push-to-talk or latched in toggle mode, and automatically `0` when blocked, suspended, muted, or watchdog-stalled):

```c
static uint64_t s_last_keys_gen = 0;
static uint8_t  s_keys[64];
static uint8_t  s_key_active[64];
static uint32_t s_key_count = 0;

void refresh_talk_buttons(void) {
    uint32_t count = 0;
    uint64_t gen   = 0;
    if (svp_get_key_states(g_svp, s_keys, s_key_active, (uint32_t)sizeof(s_keys), &count, &gen) == SVP_OK) {
        if (gen != s_last_keys_gen) {
            s_last_keys_gen = gen;
            s_key_count     = count; /* Up to count keys registered by SvAddKey */
            rebuild_hud_talk_buttons(s_keys, count);
        }
        /* Render each button with its active highlight from s_key_active[i] */
    }
}
```

* Buttons appear only after the server calls `SvAddKey(playerid, vk)`. Many scripts (including `FS_SampVoicePlus_Demo`) register keys dynamically only when a feature (`Proximity`, `Radio`, `Phone`, `Megaphone`) is enabled.
* Map Win32 Virtual-Key codes (`vk_code`) to concise button labels (`0x42` -> `"B"`, `0x5A` -> `"Z"`, `0x4E` -> `"N"`, `0x58` -> `"X"`, `0x56` -> `"V"`, `0x47` -> `"G"`, `0x4D` -> `"M"`, `0x12` -> `"Alt"`, `0x14` -> `"CapsLock"`, `0x20` -> `"Space"`).
* Call `svp_set_key_state(g_svp, vk_code, 0)` on both touch release **and** Android OS touch cancel (`ACTION_CANCEL` / focus loss) so touches never stick down:

```c
svp_set_key_state(g_svp, vk_code, 1); /* Touch down */
svp_set_key_state(g_svp, vk_code, 0); /* Touch up / touch cancel */
```

Toggle talk mode (`SVP_TALK_MODE_TOGGLE`) is handled internally by `libsvplus_mobile.so` whenever `status.toggle_allowed != 0`; the launcher always passes physical touch press (`1`) and release (`0`).

### Step 9: Handle Chat/Dialogs & App Backgrounding

Block key transmission while chat input or native/SA-MP dialogs are focused, and suspend/resume cleanly on Android `Activity` lifecycle events:

```c
/* Hosts/loaders without a separate chat-input getter must at least block on active dialogs: */
svp_set_input_blocked(g_svp, (chat_open || dialog_active) ? 1 : 0);

/* Android Activity lifecycle (recommended even with the 1.5.0+ > 1 s frame watchdog): */
svp_set_suspended(g_svp, 1); /* onPause() */
/* Also release any pressed touch keys on onPause() */
svp_set_suspended(g_svp, 0); /* onResume() */
```

As a safety net in `1.5.0+` (especially for script hosts without OS pause callbacks), if `svp_frame()` stops being called for `> 1 s` while connected, the SDK releases all held/toggled keys and pauses capture/playback until `svp_frame()` resumes. Hosts with Activity lifecycle events should still call `svp_set_suspended()` explicitly.

### Step 10: Query Status, Active Speakers, My Streams & Voice Icons

Poll HUD state from the UI/render thread (all getters are non-blocking):

```c
svp_status_t st = {0};
st.struct_size = sizeof(svp_status_t);
if (svp_get_status(g_svp, &st) == SVP_OK) {
    /* st.connected, st.server_has_voice, st.voice_ready, st.mic_available,
       st.transmitting, st.input_level (0.0..1.0), st.server_muted, st.toggle_allowed,
       st.nearby_list_enabled, st.occlusion_enabled, st.latency_ms (<0 if unknown),
       st.server_endpoint, st.keys_generation, st.speakers_generation,
       st.audio_ready (1.5.0+, only written when struct_size >= V2) */
}

/* Array calls: ALWAYS set element 0's struct_size before calling! */
svp_speaker_info_t speakers[32] = {0};
speakers[0].struct_size = sizeof(svp_speaker_info_t);
uint32_t speaker_count = 0;
if (svp_get_speakers(g_svp, speakers, 32, &speaker_count) == SVP_OK) {
    for (uint32_t i = 0; i < speaker_count && i < 32; ++i) {
        /* speakers[i].player_name, stream_name, stream_color (0xAARRGGBB),
           icon_id (SVP_ICON_*), blocked, tag_count, tags[0..tag_count-1].{color, name} (UTF-8) */
    }
}

svp_local_stream_t my_streams[16] = {0};
my_streams[0].struct_size = sizeof(svp_local_stream_t);
uint32_t stream_count = 0;
svp_get_my_streams(g_svp, my_streams, 16, &stream_count);

int32_t voice_state = svp_get_player_voice_state(g_svp, remote_player_id);
/* Returns SVP_VOICE_STATE_OK (0), NO_MICRO (1), CANNOT_HEAR (2), or < 0 on error */

/* Diagnostic audio TX telemetry for the last completed transmission (Since 1.5.0): */
svp_tx_stats_t tx_stats = {0};
tx_stats.struct_size = sizeof(svp_tx_stats_t);
if (svp_get_tx_stats(g_svp, &tx_stats) == SVP_OK) {
    /* tx_stats.frames, tx_stats.enc_errors, tx_stats.pre_peak_db / pre_rms_db,
       tx_stats.post_peak_db / post_rms_db (-INFINITY on silence), tx_stats.tx_count,
       tx_stats.watchdog_trigger_count */
}
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

## 6. Threading Contract & ABI Rules

### C API Functions (`svp_*`)

| Function | Safe Calling Thread(s) | Notes |
| :--- | :--- | :--- |
| `svp_get_api_version`, `svp_get_version` | Any thread | Lock-free constants. |
| `svp_create` | Any thread (init) | Starts the `10 ms` audio thread without blocking on BASS. Returns `SVP_ERR_STATE` if already created. |
| `svp_destroy` | Owner shutdown thread | Caller must synchronize: no concurrent or subsequent `svp_*` calls on `session`. |
| `svp_build_join_rpc`, `svp_build_join_payload`, `svp_set_connected`, `svp_on_control_packet` | Any thread (e.g. RakNet thread) | `svp_on_control_packet` queues packets bounded to `8192` pkts / `4 MB` for `svp_frame()`. |
| `svp_frame` | **Single game thread only** | Run every frame (including loading screens). Logs one warning if invoked from a second thread. `> 1 s` stall while connected triggers the frame watchdog. |
| `svp_set_player_name`, `svp_clear_player` | Any thread | Staged lock-safely for deferred decoding inside `svp_frame()`. |
| `svp_set_key_state`, `svp_set_input_blocked`, `svp_set_suspended` | Any thread | Lock-free / atomic updates. |
| `svp_set_config`, `svp_set_player_entries` | Any thread | Atomic / synchronized snapshot swap. |
| `svp_get_keys`, `svp_get_key_states`, `svp_get_status`, `svp_get_speakers`, `svp_get_my_streams`, `svp_get_player_voice_state`, `svp_get_tx_stats` | Any thread (UI/render) | Non-blocking snapshots into caller buffers. |

### Host Callbacks (`svp_host_t`)

| Callback | Required | Invocation Thread |
| :--- | :---: | :--- |
| `send_control` | **Yes** | Strictly **game thread inside `svp_frame()`** |
| `get_camera`, `get_local_ped_position` | **Yes** | Strictly **game thread inside `svp_frame()`** |
| `get_player_position`, `get_vehicle_position`, `get_object_position`, `is_local_in_vehicle` | No | Strictly **game thread inside `svp_frame()`** |
| `is_line_of_sight_clear`, `decode_text` | No | Strictly **game thread inside `svp_frame()`** |
| `get_bass_proc` | No | **Background audio thread** during `svp_create()` |
| `log` | No | **Any internal thread** (background audio/network or game thread) — must be thread-safe |

> [!WARNING]
> * **Managed / single-thread FFI hosts (Java/JNI without thread attach, LuaJIT FFI, C#):** Leave **both** `get_bass_proc` and `log` as `NULL` so the background audio/network threads never invoke non-thread-safe host callbacks.
> * **No exceptions or `longjmp`:** Host callbacks must never throw C++ exceptions (`noexcept`) or `longjmp` through `libsvplus_mobile.so`; Lua/FFI callbacks must wrap body logic inside `pcall`.
> * **Android 11+ TBI pointer tags:** Native heap pointers (`malloc`, `std::vector`) carry top-byte tags (e.g. `0xB400007A...`) on Android 11+ (`targetSdkVersion >= 30`), truncating if converted through 53-bit IEEE-754 floating-point script numbers. Copy bytes into an untagged host-owned buffer before passing numeric pointer addresses.

---

## 7. Coordinate, Entity & Value Conventions

* **World coordinates:** Standard GTA San Andreas world coordinates (`X` East, `Y` North, `Z` Up).
* **Camera orientation:** `front[3]` must be a unit vector matching GTA `TheCamera`. Since `1.5.0`, `up[3]` may be left `(0, 0, 0)` (length `< 1e-3`) so the SDK derives orthogonal `up` automatically; if computing `up` in the host, use world `Z = (0, 0, 1)`:
  ```text
  u = Z - dot(Z, front) * front = (-front.z * front.x, -front.z * front.y, 1 - front.z^2)
  up = normalize(u)          // when |front.z| <= 0.999; else reuse last valid up or (0, 1, 0)
  ```
* **Color & level values:**
  * `stream_color` (`svp_speaker_info_t`) and `tags[].color` (`svp_stream_tag_t`) are `0xAARRGGBB` (matching `HudOverlay`: bits `23..16` = Red, `15..8` = Green, `7..0` = Blue; top alpha bits are ignored and rendered opaque `255`).
  * `svp_status_t.input_level` ranges from `0.0` (silence) to `1.0` (full scale).
* **Object IDs:** `get_object_position` and `is_line_of_sight_clear(..., ignore_object_id)` operate strictly on **global GTA objects** (`CreateObject` IDs `0..MAX_OBJECTS-1`, with `SVP_INVALID_OBJECT_ID` `0xFFFF` meaning none), **not** SA-MP Streamer (`CreateDynamicObject`) IDs.
* **Player positions:** `get_player_position` is queried for the target player of player-anchored streams (`SvCreateSLStreamAtPlayer` / `SvCreateDLStreamAtPlayer`). Return `0` when that player is not currently streamed in; the stream stays without a position until they stream back in.
* **Binary compatibility (`struct_size`):**
  * Initialize `struct_size = sizeof(the_struct)` on every input/output struct (`>= V1` size, otherwise rejected with `SVP_ERR_INVALID_ARG`).
  * For array parameters (`svp_set_player_entries`, `svp_get_speakers`, `svp_get_my_streams`), element `0`'s `struct_size` defines the **byte stride** used to step across all elements (`count > 0`).
  * If a caller passes a struct larger than known by the compiled `.so`, trailing bytes beyond what the library understands are left untouched, and output `struct_size` reports bytes written by the library.

---

## 8. Optional Callbacks

| Callback | Purpose | Behavior When Omitted (`NULL`) |
| :--- | :--- | :--- |
| `is_line_of_sight_clear` | World wall/object LOS check between camera/listener and speaker source for voice occlusion (`~1000 Hz` LPF + `-6 dB`). Return `1` if clear, `0` if occluded. `PlaybackEngine` already pulls the target ray endpoint back **`0.5 m`** in front of the sound origin; if your engine cannot exclude `ignore_object_id`, check **buildings only** (skip dynamic objects) whenever `ignore_object_id != SVP_INVALID_OBJECT_ID`. | Wall voice occlusion remains disabled (audio is never muffled). |
| `decode_text` | Converts legacy single-byte server encodings (e.g. Windows-1251, Windows-1252, Windows-1250) into NUL-terminated UTF-8. Return `1` on success. | Falls back to ISO-8859-1 -> UTF-8 conversion cleanly (also used if `decode_text` returns `0`). |
| `get_bass_proc` | Custom symbol resolver (`dlsym` alternative) when BASS is statically linked or loaded with custom/renamed exports. *(Leave `NULL` in single-thread managed/FFI hosts.)* | Uses `dlopen("libbass.so", RTLD_NOLOAD)` first (reusing the already-loaded library handle; the reference is released via `dlclose` in `svp_destroy()`), falling back to `dlopen("libbass.so", RTLD_LAZY \| RTLD_LOCAL)` + `dlsym`. |
| `log` | Custom log sink receiving `(user, SVP_LOG_*, message)`. *(Leave `NULL` in single-thread managed/FFI hosts.)* | Writes directly to Android logcat (tag **`SampVoicePlus`**). |

---

## 9. Android Platform Limits & Verification Status

| Area | Behavior & Notes |
| :--- | :--- |
| **Core handshake, `222` control, 3D/stereo voice, mute, toggle, background, reconnect** | Verified end-to-end on **ReSA `1.1.8` (`arm64-v8a`, `28/11/2026`)** on SDK `1.4.0` and `1.5.0` alongside PC `sampvoice.asi`. *(ABI builds `armeabi-v7a` and `x86_64` remain **to be confirmed on device**.)* |
| **DX8 audio effects (`SvEffectCreate*`)** | `Parameq` (radio bandpass) and `Distortion` (megaphone/radio grit) **confirmed working on ReSA `1.1.8` `arm64-v8a`**. If any Android BASS build refuses another DX8 FX type (`BASS_ChannelSetFX`), playback continues cleanly without that effect and logs once per refused FX type. *(Remaining FX matrix **to be confirmed on device**.)* |
| **Wall voice occlusion** | `is_line_of_sight_clear` occlusion **confirmed on ReSA `1.1.8` `arm64-v8a`** (buildings attenuate positional voices while global streams remain clear). |
| **Microphone input & `RECORD_AUDIO` permission** | Default BASS capture (`BASS_RecordInit(-1)`) **confirmed on ReSA `1.1.8` `arm64-v8a`**. If permission is denied initially, capture retries every `2 s`. *(Runtime mid-game permission grant recovery verified in harness; **to be confirmed on device**.)* |
| **SDK `1.5.0` additions** | `svp_build_join_rpc()`, `svp_status_t.audio_ready`, `svp_get_key_states()`, `svp_get_tx_stats()`, and the `svp_frame()` `> 1 s` watchdog **confirmed on ReSA `1.1.8` `arm64-v8a`**; automatic camera `up` derivation **verified in harness; to be confirmed on device**. |

---

## 10. Common Issues

| Symptom | Likely Cause | Resolution |
| :--- | :--- | :--- |
| **No voice playback or capture at all** | `BASS_Init` was never called by the game/launcher (or failed). | Ensure BASS is initialized before or shortly after `svp_create()` (monitor `st.audio_ready` in `1.5.0+`), or supply `get_bass_proc` if BASS isn't loaded as `libbass.so`. |
| **Server reports `"No voice plugin"` or logs no voice connection** | `RPC 25` did not carry the trailer (hook delivered a copy and in-place writes were discarded, or return tuple had wrong types). | Use `svp_build_join_rpc()` and return the replacement `BitStream` in the hook API's exact expected tuple format; check server `[sv+:INF] Player <id> voice connected:` log. |
| **Server sees client as `v11` (not Plus)** | Legacy SampVoice hook emitted a duplicate `0xDEADBEEF` trailer ahead of the Plus trailer. | Switch to `svp_build_join_rpc()` (replaces the first `0xDEADBEEF` cleanly) and disable the launcher's native voice option. |
| **No push-to-talk buttons show in HUD** | Server hasn't registered keys yet (many scripts register keys only when toggled ON), or `svp_frame()` isn't running. | Toggle a feature in `/voice` (`Proximity` -> `ON`), verify `connected` / `voice_ready` in `svp_get_status()`, and confirm `svp_frame()` runs every frame. |
| **Voice only works after reconnecting** | `svp_create()` and/or `RPC 25` hook were loaded after already joining the server. | Create the session and install hooks before connecting, or reconnect once loaded. |
| **Player stays stuck speaking when backgrounded** | App paused without calling `svp_set_suspended(1)` or releasing touch keys. | Call `svp_set_suspended(1)` and `svp_set_key_state(vk, 0)` on `onPause()`; `1.5.0+` also releases/pauses automatically after `> 1 s` without `svp_frame()`. |
| **Audio stutters or drops control packets during map load** | `svp_frame()` stopped being called during loading screens or pauses. | Keep calling `svp_frame()` once per frame even during loading screens. Queues cap at `8192` packets / `4 MB` and drop newest packets with a rate-limited `WRN` until `svp_frame()` resumes. |
| **Need runtime logs on Android** | Searching wrong logcat tag or custom sink drops debug lines. | Filter logcat for tag **`SampVoicePlus`** (`adb logcat -s SampVoicePlus`). |

---

## 11. Notes for Server Owners

* **Connection log (`1.5.0+`):** Every voice connection logs cleanly at `Info` level without exposing session keys:
  `[sv+:INF] Player <id> voice connected: SampVoice Plus <version> (wire v11, proto v12, mic=yes|no, mobile)`
* **Identify Plus mobile clients in Pawn:** Check `SvGetPlusCapabilities(playerid) & SV_CAP_MOBILE` inside `OnPlayerVoiceReady`. *(Older launchers still using legacy Android `v11` report `0`.)*
* **Monitor connected mobile count:** Read `stats[SV_STAT_MOBILE_PLAYERS]` from `SvGetServerStats(stats)`.
* **Activation keys on Android:** Legacy `v11` Android launchers hardcode key `0x42` (`B`), whereas launchers built with the **SampVoice Plus Android SDK** dynamically display buttons for every key registered via `SvAddKey(playerid, key)` and send exact key press/release packets.
* **`FS_SampVoicePlus_Demo` initial state:** The repository demo filterscript starts with proximity, radio, and megaphone **OFF**; players enable features via `/voice` (which triggers `SvAddKey` for `B`, `N`, and `M`).

---

## 12. Hosts Without Native Code (Script Loaders, FFI, JNI)

When driving `libsvplus_mobile.so` through a script loader or managed FFI bridge (such as **AL Loader** with LuaJIT FFI) instead of linking C++ directly:

* **Hook copy semantics & strict return tuples:** Script hook APIs frequently pass a temporary read/write copy of outgoing `BitStream`s and ignore in-place modifications completely. Build a fresh `BitStream` via `svp_build_join_rpc()` and return it using the loader's exact multi-return signature. For example, **AL Loader** requires `(send:boolean, id, bitStream, priority, reliability, channel)` (returning `true, nil, rpcOutBs` keeps all other parameters intact; passing `id` first or omitting `true` silently discards the entire return tuple without warning). **AL Loader**'s return tuple differs from other script loaders, so developers integrating on another loader must always verify their own loader's documentation.
* **TBI tagged pointers on Android 11+:** Native buffers from C `malloc` often carry top-byte tags (`0xB4...`) that corrupt in Lua's 53-bit `double`. Keep `ffi.new("uint8_t[?]", cap)` buffers owned by FFI/host and verify lossless `tonumber`/`uint64_t` round-trip before passing pointer integers to C APIs expecting numeric addresses.
* **Thread-safe callbacks (`NULL` for `log` and `get_bass_proc`):** LuaJIT FFI and unattached JNI callbacks must never run concurrently from internal SDK audio/network threads — always leave `host.log = NULL` and `host.get_bass_proc = NULL`.
* **`pcall` inside game-thread callbacks:** Wrap every FFI callback (`send_control`, `get_camera`, `get_local_ped_position`, entity/LOS queries, `decode_text`) in `pcall` so a Lua error cannot `longjmp` across `svp_frame()` on `arm64`.
* **Script hot-reload requires reconnecting:** Reloading a script destroys the active session and re-initializes hooks mid-connection; players must reconnect to re-run `RPC 25`.
* **Missing OS lifecycle hooks:** Script loaders that have no `onPause` / `onResume` events cannot call `svp_set_suspended()` directly — on `1.5.0+`, the SDK's `> 1 s` `svp_frame()` watchdog automatically unlatches held/toggle keys and pauses audio until frames resume.

---

## 13. Integration Test Checklist

Run with a Windows PC running **`sampvoice.asi`** (`Plus 1.5.0+`, since earlier receivers could go silent when a speaker talked again within 5 s) and the server running **`FS_SampVoicePlus_Demo`** (`/voice`, `open.mp` or `SA-MP`):

| # | Test | Expected (`1–2` lines) |
| :-: | :--- | :--- |
| 1 | **SDK & Script Load** | SDK loads with matching ABI and `svp_get_version() >= 10500`, `audio_ready` becomes `1`, and zero critical errors appear in logs. |
| 2 | **`RPC 25` Handshake & Mobile Metrics** | Server logs `[sv+:INF] Player <id> voice connected: SampVoice Plus ... (..., mobile)`; `/voice` -> `"1. My voice status"` shows `Mobile:Yes` and `"Server stats"` shows `SV_STAT_MOBILE_PLAYERS = 1`. |
| 3 | **Dynamic Key Buttons (`SvAddKey`)** | Enabling **Proximity** (`B`), **Radio** (`N`), and **Megaphone** (`M`) in `/voice` creates each on-screen talk button within `1 s`. |
| 4 | **Bidirectional Voice (3D & Stereo)** | Holding `B` transmits mobile -> PC (even while walking/moving camera with multitouch) and PC -> mobile rotates 3D pan smoothly and attenuates with distance. |
| 5 | **Radio (`Parameq`) & Megaphone (`Distortion`)** | Global radio channel (`N`) plays clearly at any distance with `Parameq`/`Distortion` filter; Megaphone (`M`) covers `95 m` with `Light`/`Medium`/`Strong` distortion presets. |
| 6 | **Per-Channel Priority Ducking** | Set radio priority `1` (`duck = 20%`), set mobile listener volume on radio to `0%`, and on PC hold `B` + `N` together: PC proximity audio on mobile ducks smoothly (`~150 ms`) while `N` is held and restores (`~400 ms`) after releasing `N`. |
| 7 | **Other DX8 Effects** | Server-attached DX8 filters (e.g. `Echo`, `Chorus`, `Compressor`, `Reverb`) apply without audio stalls; unsupported FX fail softly with a single warning. |
| 8 | **Wall Voice Occlusion** | Standing behind a solid building/wall with `sampvoice.voice_occlusion = 1` muffles positional proximity speech while global radio stays clear. |
| 9 | **Toggle Talk Mode** | With `talk_mode = SVP_TALK_MODE_TOGGLE` and `toggle_allowed = 1`, tapping a button starts transmitting and keeps that button lit (`svp_get_key_states` active `= 1`) until tapped again (while clearing automatically during dialogs, suspend, mute, or watchdog stall). |
| 10 | **Server & Pair Mute** | RCON server mute cuts transmission (`server_muted = 1`) and drops latched keys; per-pair mute silences target speaker only for that listener. |
| 11 | **App Backgrounding & Frame Watchdog** | Pressing Home/switching apps for `10 s` pauses capture/playback and drops active transmit state within `~1 s` (`frame_stalled`), then cleanly drains `ReleaseKey` on resume without restarting toggle TX. |
| 12 | **Disconnect, Reconnect & Hot-Reload** | Reconnecting (or reloading session and rejoining) completes a fresh `RPC 25` handshake and restores voice cleanly without leaking speakers/streams. |
| 13 | **Legacy `v11` Client Coexistence** | Connecting a legacy `v11` client alongside Mobile `v12` and PC `v12` lets all three speak and hear each other without errors. |
| 14 | **Sustained Session Stability** | `5+` minutes of movement, speech, and stream transitions sustain zero `cbErrs` and near-zero queue drops with stable frame time. |
| 15 | **Launcher Native Voice Left Enabled** | With the launcher's legacy voice left ON, `svp_build_join_rpc()` replaces the native `0xDEADBEEF` trailer, Plus `v12` connects (`Mobile:Yes`), and no native mic button appears. |
| 16 | **Quick Re-Key** | Talk, release, and press again within `1–3 s`, `5–10` times on radio and proximity: the PC hears clean speech every time and each `Speaker ended` line shows `fresh=yes` and `stalls=0`. |

---

*See also: [Complete API Reference](api-reference.md) · [Under the Hood (Mobile C SDK Internals)](under-the-hood.md#mobile-c-sdk-libsvplus_mobileso) · [`svplus_mobile.h`](../client/mobile/include/svplus_mobile.h)*
