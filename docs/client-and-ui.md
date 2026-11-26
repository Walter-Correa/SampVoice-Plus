# Universal Client & In-Game UI (`sampvoice.asi`)

**SampVoice Plus** builds the Windows player client from scratch around **single-file deployment** (`sampvoice.asi` only), **dedicated audio-thread processing**, and **universal compatibility** across every official SA-MP 0.3.7 / 0.3.DL release and open.mp launcher connection.

---

## 1. Single-File Architecture

Unlike legacy SampVoice distributions requiring `bass_fx.dll`, loose `.ttf` fonts, and `.png` files, **SampVoice Plus** compiles everything cleanly into **`sampvoice.asi`** (with an optional 15-language `SampVoice-Plus-Setup.exe` that bundles Ultimate ASI Loader `dinput8.dll` when needed):

| Component | How It Is Bundled |
| :--- | :--- |
| **MSVC C/C++ Runtime** | Statically linked (`/MT`) — zero `VCRUNTIME140.dll` or `MSVCP140.dll` dependencies. |
| **Opus Codec (`1.5.2`)** | Statically linked VOIP encoder/decoder (48 kHz mono, 100 ms / 4800-sample frames, in-band FEC + PLC). |
| **BASS Audio & DSP** | Dynamically binds to GTA/SA-MP's loaded `bass.dll` without hooking `BASS_Init` or modifying global BASS 3D/output state (only scoping `BASS_CONFIG_BUFFER` briefly around `BASS_StreamCreate` and immediately restoring it), creates per-speaker mono push channels with software distance attenuation and stereo panning, and runs persistent per-speaker 80 Hz high-pass + RMS balancer DSP in C++ without `bass_fx.dll` or `MemoryModule`. |
| **UI Font & HUD Textures** | Subset **Google Noto Sans** (`NotoSans-Medium-subset.ttf` for all 15 languages without Windows font fallback, plus `NotoSans-Black-subset.ttf` for the header) and **19 original 256×256 RGBA PNG HUD icons** (3 microphone states: `micro_active`, `micro_muted`, `micro_passive`; 2 overhead/list voice-status indicators: `speaker_deaf`, `micro_missing`; 7 stream icons: `speaker`, `radio`, `phone`, `megaphone`, `police`, `admin`, `global`; plus 7 red `*_blocked` variants, with SVG sources in `client/resources/svg/`) are compiled as Windows `.rc` `RCDATA` resources loaded cleanly via `FindResourceW` and decoded with `stb_image.h`. The menu brand header is rendered in real time via `ImDrawList` vector bars, gradient text, and pill badge. |
| **Dear ImGui (`1.91.5`)** | Statically compiled Direct3D 9 + Win32 backend hooked only on `IDirect3DDevice9::Present` / `Reset` and game `WndProc` (preserving any later third-party `GWL_WNDPROC` chain in passthrough mode on detach). |

---

## 2. Dedicated Audio Thread, Jitter Buffer & Safe Teardown

* **Off-Render Audio Loop:** Capture polling, Opus encode/decode, UDP socket open/close/polling, keepalive pings, and software spatialized playback (distance attenuation + stereo pan) run exclusively on a dedicated background audio thread (~10 ms cadence) rather than blocking `IDirect3DDevice9::Present`. Window focus (`GetForegroundWindow` + `!IsIconic`) is queried directly against the cached game `HWND` (`std::atomic<HWND>`), and active settings come from an immutable `std::shared_ptr<const ClientConfig>` snapshot.
* **SEH-Guarded Render-Thread Spatial Snapshot:** GTA SA `TheCamera` (`CPlaceable::m_matrix`, mapping `mat[4..6]` -> `up`, `mat[8..10]` -> `front/at`, `mat[12..14]` -> `pos`) and SA-MP structure reads (`CGame::m_pPlayerPed`, `CPlayerPool`, `CVehiclePool`, `CObjectPool`) run exclusively on the render thread through Win32 SEH `__try/__except` memory helpers, with a 250 ms name-cache refresh throttle. Note that local SA-MP player-pool scanning skips heavy unnecessary work on global streams while still supplying world positions for all active spatialized streams.
* **Adaptive Jitter & BASS Prebuffer (`SpeakerChannel`):** `SpeakerJitterBuffer` reorders out-of-order frames within a 35 ms window, decodes Opus in-band FEC, and conceals drops via 4800-sample PLC. On the BASS side, each playback channel starts paused and prebuffers `N` decoded frames (`N` adapts dynamically in `[1..4]` on `BASS_ACTIVE_STALLED` vs 500 stable ticks) before starting playback.
* **Focus-Aware PTT & Clean Release Tail:** Capture input ignores keys when GTA SA loses window focus or chat/pause/F11 UI is open, reuses a pre-reserved scratch buffer, and zero-pads/flushes partial speech tails when Push-To-Talk is released.
* **Singleplayer Strict Non-Interference:** At startup, `sampvoice.asi` polls up to 10 seconds (every 250 ms) for `samp.dll` (or `samp.asi`) in process memory whose PE `TimeDateStamp` and `AddressOfEntryPoint` strictly match an official SA-MP release. In singleplayer GTA:SA (absent after 10 seconds), the initialization thread exits immediately—creating **zero** files, directories, logs, mutexes, MinHook trampolines, window subclasses, or `bass.dll`/audio threads.
* **Background Mod & Hook Diagnostics (`[INF:mods]`):** Once SA-MP is verified, a low-priority background scan (`THREAD_MODE_BACKGROUND_BEGIN`) inspects loaded modules (`<GTA>` / `%USERPROFILE%` normalized, identifying standard `gta_sa.exe`/`dinput8.dll`/`vorbisFile.dll` SHA-256 hashes), monitors dynamic `LdrRegisterDllNotification` loads/unloads outside the loader lock, checks D3D9 `Present`/`Reset` before & after hook, `WndProc`, `bass.dll` exports, `RakClient`, `CalcScreenCoors`, and `CPed::GetBonePosition` for detours, and scans root/ASI/CLEO/SAMPFUNCS/MoonLoader/ModLoader binaries read-only.
* **Loader-Lock Safe Exit:** Because GTA SA / SA-MP terminates forcefully via `ExitProcess` (`lpReserved != nullptr`), `DllMain(DLL_PROCESS_DETACH)` flushes the log file safely via `try_lock` + `WriteFile` (archiving `[ERROR] sampvoice_plus_v<version>_log_<N>.txt` on unclean/error sessions) without joining background threads or touching torn-down CRT state.

---

## 3. Universal SA-MP Client Bridge (Unpatched `samp.dll` + `ProxyRakClient`)

`VersionDetector` (`client/src/samp/VersionDetector.hpp`) pairs `samp.dll`'s PE `TimeDateStamp` **and** `AddressOfEntryPoint` strictly (refusing unknown modified binaries instead of guessing offsets) across all 7 official builds:

| Version | EntryPoint RVA | PE TimeDateStamp | Strategy |
| :--- | :---: | :---: | :--- |
| **0.3.7-R1** | `0x31DF13` | `0x5542F47A` | Verified per-field `SampLayout` + direct `SetCursorMode` (`0x9BD30`) |
| **0.3.7-R2** | `0x3195DD` | `0x59C30C94` | Verified per-field `SampLayout` + direct `SetCursorMode` (`0x9BDD0`) |
| **0.3.7-R3 / R3-1** | `0x0CC4D0` | `0x5C0B4243` | Verified per-field `SampLayout` + direct `SetCursorMode` (`0x9FFE0`) |
| **0.3.7-R4** | `0x0CBCB0` | `0x5DD606CD` | Verified per-field `SampLayout` + direct `SetCursorMode` (`0xA0720`) |
| **0.3.7-R4-2** | `0x0CBCD0` | `0x6094ACAB` | Verified per-field `SampLayout` + direct `SetCursorMode` (`0xA0750`) |
| **0.3.7-R5-1 / R5-2 (incl. R5-2-MP)** | `0x0CBC90` | `0x6372C39E` | Verified per-field `SampLayout` + direct `SetCursorMode` (`0xA06F0`) |
| **0.3.DL-R1 (incl. DL-R1-2-MP)** | `0x0FDB60` | `0x5A6A3130` | Verified per-field `SampLayout` + direct `SetCursorMode` (`0xA0530`) |

### Heap `ProxyRakClient` inside Shadow RakPeer Block
Instead of patching `.text` code inside `samp.dll`, `SampBridge` validates `CNetGame::m_pRakClient`'s vtable address inside `[sampBase, sampBase + SizeOfImage)` and wraps it with a `ProxyRakClient` placed at offset `+0xDDE` inside a heap block whose `+0` shadow RakPeer vtable safely tears down both `ProxyRakClient` and `(orig - 0xDDE)->vtable[0](1)` on exit:
* Injects `ConnectPacket` (`version = 11`, `0xDeadBeef`) + `ConnectPlusExtension` (`SVP1` magic, `capabilities`, `clientBuild`) on **RPC 25 (`PlayerConnect`)**, ensuring seamless connection to both legacy `v11` servers and Plus servers.
* Uses `orig_->GetServerID().binaryAddress` directly (zero blocking DNS calls) and strictly drops inbound UDP packets from any source IP/port other than the connected server endpoint.
* Cursor Mode `3` (`CMODE_LOCKCAM`) & OS/Win32 Input: `SetCursorMode(game, 3, FALSE)` unlocks the Windows cursor and blocks GTA camera rotation without acquiring DirectInput keyboard in exclusive mode (`DISCL_EXCLUSIVE`); prior SA-MP and Win32 cursor states (`CURSORINFO`, `ShowCursor` count, `ClipCursor`) are saved on open and restored on close/focus loss, while system keys (`VK_LWIN`, `VK_RWIN`, `Alt+Tab` / `WM_SYSKEYDOWN`, `WM_SYSCOMMAND`, `WM_ACTIVATE*`, `WM_KILLFOCUS`) are never consumed.

---

## 4. In-Game Configuration Panel & 15 Real-Time Languages

Pressing **`F11`** (or your custom rebound hotkey) opens the Dear ImGui settings window:
* **General:** Live hotkey capture (`F11` default), real-time switching across all **15 languages**, HUD toggles/scales (`0.01x` arrows / `1%` opacity / `1 px` height steps), overhead 3D icon backdrop opacity (`0%–100%`, default `55%`), overhead height offset (`0–60 px`, default `0`), and drag positioning. When no one is talking while the menu is open with header OFF, the draggable speaker box matches the exact W×H of the active header label.
* **Audio & Microphone:** Master volume, clarity filter & RMS balancer DSP toggles, input device selector, mic gain, noise suppression, RMS meter.
* **Players (`TabBlacklist`):** Combines the online player list (**All** / **Nearby**), per-player voice-status indicators, and client-side blocking in one tab. UI blocking labels inside the tab remain standardized around **Block / Blocked / Unblock** (*Bloqueados / Bloquear / Desbloquear*, etc.), while the underlying INI storage section `[blacklist]` in `sampvoice.ini` (`<player> | <server name> | <IP:port> | <server|all>`) and internal log prefix remain unchanged for 100% compatibility with `1.0.0` configs:
  * **Current server:** Shows the active server name and `IP:port` (`m_szHostname`), lists online players with side-by-side **This server only** (`server`) and **All servers** (`all`) block controls (plus **Unblock** / **Unblock here** and clear-all for the current server origin), or a compact notice when disconnected. When enabled by a Plus `v12` server (`sampvoice.voice_status = true`), each player row in both the general online list and the **Nearby** filter displays the matching `16×16` HUD sprite (`micro_missing` or `speaker_deaf`) vertically aligned beside the player name immediately followed by its discrete translated label (*"No mic"* / *"Can't hear"* in all 15 languages; hidden for `ok` or when `voice_status = false`, shown alongside block status without hiding either, shortening long player names with ellipsis before labels or action buttons), and when `sampvoice.nearby_list = true`, an **All / Nearby** filter lists streamed-in SA-MP players sorted closest-to-farthest without displaying distances in meters *(because GTA/SA-MP already keeps streamed players in client memory, disabling `sampvoice.nearby_list` simply hides that convenience UI filter)*.
  * **All blocked players:** Always visible (even while disconnected) with search, scope switcher (**Origin server only** / **All servers**), and **Remove** buttons across every saved entry.
* **HUD Visibility & Legibility:** The HUD microphone icon stays hidden until the local player spawns (always shown when the F11 menu is open). Overhead 3D voice icons track `CPed::GetBonePosition(8)` (`+ dist * 0.0475 + 0.2`) and `SAMP\sa-mp.cfg` `fontsize` so their bottom edge sits right above the SA-MP nametag (`nameTop - 3 px - overhead_icon_height * distScale`). While locally transmitting and for 3 seconds after key release on Plus `v12` servers with `sampvoice.voice_status = true`, remote players who lack a microphone (`micro_missing`) or cannot hear voice (`speaker_deaf`) also display the matching 3D overhead status icon when streamed within `30 m` and their SA-MP nametag is visible (`ShowNameTags`, `ShowPlayerNameTagForPlayer`, `NameTagDrawDistance`, and `DisableNameTagLOS` line-of-sight check) — supported across **all 7 official SA-MP builds** (`0.3.7-R1`, `0.3.7-R2`, `0.3.7-R3`, `0.3.7-R4`, `0.3.7-R4-2`, `0.3.7-R5`/`R5-2-MP`, and `0.3.DL-R1`/`DL-R1-2-MP`) with binary-verified per-build offsets (failing closed only on unrecognized `Unknown` client binaries). The Speaker List formats both remote speakers and the local player identically as `(icon) [ID] Name [STREAM1] [STREAM2]` using SA-MP's player color for `[ID] Name [ ]` and stream color for tag names (omitted when blank or `0`), with 1 px black outlines and vertical icon centering against `'H'`; the local player always uses the microphone icon (`microActive`) and receives their attached speaker streams from `v12+` servers via `PlusSpeakerStreams = 106`, filtering tags at draw time so **positional streams (`kind = 1`)** always show while **global streams (`kind = 0`)** only appear when the local player is also attached as a listener (`PlaybackEngine::hasStream`); on Plus servers, the local player row in the Speaker List only renders when there is at least one visible channel tag (so a speaker attached solely to global streams they are not listening to still transmits to listeners and lights the HUD microphone icon without showing a local row), while legacy `v11` servers continue showing `(mic) [ID] Name` whenever transmitting. Blocked players who are actively speaking still remain visible without decoding or playing audio — showing the red channel icon variant overhead and in the list, plus the localized blocked tag (`[BLOCKED]`, `[BLOQUEADO]`, etc. in icon red `#E5483C`, brackets in player color).

### Supported Languages (15)

| Code | Language | Code | Language | Code | Language |
| :---: | :--- | :---: | :--- | :---: | :--- |
| `id` | Bahasa Indonesia | `it` | Italiano | `vi` | Tiếng Việt |
| `de` | Deutsch | `hu` | Magyar | `tr` | Türkçe |
| `en` | English *(default)* | `pl` | Polski | `bg` | Български |
| `es` | Español | `pt` | Português | `ru` | Русский |
| `fr` | Français | `ro` | Română | `uk` | Українська |

Saved with a 500 ms debounce timer (and immediately when closing the `F11` menu) to `Documents\GTA San Andreas User Files\SAMP\sampvoice\sampvoice.ini` (including the unified `[blacklist]` section), alongside diagnostic session logs (`sampvoice_plus_v<version>_log.txt`, plus `sampvoice_plus_v<version>_log (2).txt` / `_log (pid <pid>).txt` for concurrent instances and `[ERROR] sampvoice_plus_v<version>_log_<N>.txt` on errors/crashes):

### `sampvoice.ini` (`[general]` Keys)

| Key | Default | Range / Values | Description |
| :--- | :---: | :---: | :--- |
| `menu_hotkey` | `0x7A` (`F11`) | `0x01`–`0xFE` | Virtual key code to toggle the settings panel |
| `language` | `en` | 15 language codes | Active UI translation |
| `menu_x` / `menu_y` | `-1` | `-1` or `0.0`–`1.0` | Saved settings window position as screen fraction (`-1` = centered) |
| `menu_w` / `menu_h` | `-1` | `-1` or `0.1`–`1.0` | Saved settings window size as screen fraction (`-1` = 1002×666 @ 1080p scaled) |
| `show_speaker_list` | `1` | `0` / `1` | Show active speakers list overlay |
| `speaker_list_header` | `1` | `0` / `1` | Show the `#FFE08A` `"Speaking now"` header row |
| `speaker_list_bg` | `1` | `0` / `1` | Show the speaker list window background |
| `speaker_list_bg_opacity` | `45` | `0`–`100` | Speaker list background opacity (%) |
| `speaker_list_max` | `5` | `1`–`15` | Maximum visible remote speakers in FIFO speech order |
| `speaker_list_scale` | `1.0` | `0.5`–`2.5` *(slider `0.75`–`2.0`)* | Scale factor for the speaker list overlay |
| `speaker_list_x` / `speaker_list_y` | `0.0219` / `0.3944` | `0.0`–`1.0` | Normalized screen fraction (`>1` pixel values auto-migrate on first frame) |
| `show_micro_icon` | `1` | `0` / `1` | Show local microphone status icon |
| `micro_icon_scale` | `1.25` | `0.5`–`2.5` | Scale factor for the HUD microphone icon (`42 px × scale`) |
| `micro_icon_x` / `micro_icon_y` | `0.1516` / `0.6907` | `0.0`–`1.0` | Normalized screen fraction (`>1` pixel values auto-migrate on first frame) |
| `show_overhead_icons` | `1` | `0` / `1` | Show 3D voice icons above speakers' heads |
| `overhead_icon_bg_opacity` | `55` | `0`–`100` | Backdrop disc opacity (%) behind 3D overhead icons (`0` disables the disc and restores full icon size) |
| `overhead_icon_height` | `0` | `0`–`60` | Extra vertical offset in base pixels (`overhead_icon_height × distScale`) above SA-MP nametag |
| `speaker_icon_scale` | `1.0` | `0.25`–`3.0` *(slider `0.5`–`2.5`)* | Scale factor for 3D overhead icons |

### `sampvoice.ini` (`[sound]` Keys)

| Key | Default | Range / Values | Description |
| :--- | :---: | :---: | :--- |
| `sound_enable` | `1` | `0` / `1` | Enable incoming voice audio playback |
| `sound_volume` | `100` | `0`–`100` | Master incoming voice volume (%) |
| `sound_balancer` | `0` | `0` / `1` | Enable RMS compressor / volume balancer DSP |
| `sound_filter` | `0` | `0` / `1` | Enable 80 Hz high-pass speech clarity filter DSP |

### `sampvoice.ini` (`[microphone]` Keys)

| Key | Default | Range / Values | Description |
| :--- | :---: | :---: | :--- |
| `micro_enable` | `1` | `0` / `1` | Enable microphone capture & voice transmission |
| `micro_volume` | `75` | `0`–`100` | Microphone capture gain (`0`–`100%`, scaled cleanly) |
| `micro_device` | *(empty)* | Device name | Preferred Windows recording device (empty = Default System Device) |
| `micro_noise_suppress` | `1` | `0` / `1` | Enable software microphone noise suppression |
| `talk_mode` | `push` | `push` (`ptt`, `0`) / `toggle` (`1`) | Activation key behavior (Push-to-Talk vs Toggle) |
