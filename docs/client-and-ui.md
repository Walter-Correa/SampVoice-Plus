# Client & In-Game Settings (`sampvoice.asi`)

**SampVoice Plus** bundles everything players need into a single drop-in Windows file (**`sampvoice.asi`**) — no external `bass_fx.dll` or loose asset folders — with support across **SA-MP 0.3.7-R1 through R5-2-MP, 0.3.DL-R1, and the open.mp launcher**.

*(Looking for technical architecture details such as threading, PE build offsets, jitter buffer mechanics, or spatial math? See [Under the Hood — Client Internals](under-the-hood.md#4-client-internals-sampvoiceasi).)*

---

## 1. In-Game Panel (`F11`) & 15 Live Languages

Press **`F11`** (or your custom rebound hotkey) while connected or disconnected to open the settings window. The panel automatically measures its content so tabs fit cleanly without scrollbars, and can also be dragged to resize manually (click **"Auto size"** in the bottom-right footer to return to automatic sizing).

### General Tab
* **Status Banner:** Shows connection diagnostics across 9 states (`Connected` with control latency, `Not connected`, port blocked warning, missing/disabled mic, server mute, or server-disabled listener).
* **Menu Hotkey:** Click to rebind `F11` to another key or mouse button, or reset to default.
* **Language:** Switch between all **15 built-in languages** immediately without restarting.
* **HUD Overlays & Drag Positioning:** Toggle and scale the **Speaking now** speaker list (with optional header row, background toggle, opacity `0–100%`, max visible speakers `1–15`, scale `0.75–2.0`), local **Microphone icon** (`0.5–2.5`), and **3D Overhead icons** above players' heads (`0–60 px` vertical offset, `0–100%` backdrop disc opacity, scale `0.5–2.5`), or reset positions back to default. When connected to a server with wall occlusion active, an informational notice is shown.

### Audio Tab
* **Incoming Sound (`sound_enable`) & Master Volume (`0–100%`):** Master toggle and level for all incoming voices *(leaves GTA radio and `PlayAudioStreamForPlayer` stereo music untouched)*.
* **Proximity (`0–200%`) & Global (`0–200%`) Volumes:** Independently boost or lower local 3D voices vs global radio/announcement channels.
* **DSP Clarity Tools:** Optional **80 Hz Speech Clarity Filter** (`sound_filter`) and **RMS Volume Balancer / Compressor** (`sound_balancer`).
* **Deafen & Deafen Hotkey:** Session-only mute of all decoded incoming audio without affecting your microphone or clearing the `"Speaking now"` activity list *(displays the muted-speaker HUD indicator while active; never saved as stuck across restarts)*.

### Microphone Tab
* **Enable Microphone (`micro_enable`) & Input Device:** Toggle microphone transmission and choose any Windows recording device (with hot-plug detection when plugging or unplugging USB headsets).
* **Microphone Gain (`0–200%`, default `100%`) & Soft Limiter:** Boost quiet microphones smoothly above `100%` without harsh clipping, plus optional **Noise Suppression** (`micro_noise_suppress`), live level bar, and a local **"Listen to my mic"** loopback self-test (automatically stops when switching tabs, closing the menu, or changing/disabling the mic).
* **Talk Mode (`Push-to-Talk` vs `Toggle`):** Hold the server's key to transmit or tap once to latch ON/OFF; since `1.5.0`, opening chat, a dialog, or the pause menu, or alt-tabbing clears the latch, so tap again to resume. When a `1.2.0+` server enforces Push-to-Talk (`allow_toggle_mode = 0`), **Toggle** appears grayed out with a tooltip without overwriting your saved preference for other servers.

### Players Tab
* **Current Server Online List (**All / Nearby**):** View online players (or switch to **Nearby** when enabled by the server to sort streamed-in neighbors closest first).
* **Voice-Status Indicators:** When enabled by the server (`sampvoice.voice_status`), players missing a mic or with listening disabled show discrete *"No mic"* / *"Can't hear"* badges in the Players list and 3D icons above heads (subject to the visibility rules below).
* **Per-Player Volume (`0–200%`):** Click the percentage button next to any player to boost or quiet them specifically on that server (highlighted when changed from `100%`, saved under `[player_volume]`).
* **Block / Unblock Controls:** Block a player on **This server only** or across **All servers**, and inspect/remove all saved blocks anytime (even when disconnected) under **All blocked players**.

---

## 2. HUD Elements ("Speaking now", Mic & 3D Icons)

* **Microphone Icon:** Stays hidden until your player spawns (always visible while the `F11` menu is open for dragging/scaling).
* **Speaking Now List:** Displays active speakers cleanly as `(icon) [ID] Name [CHANNEL TAG]`, using SA-MP player colors and stream tag colors. Local player rows show active positional streams plus joined global channels.
* **3D Overhead Voice-Status Icons ("No mic" / "Can't hear"):** When enabled by the server (`sampvoice.voice_status`), these 3D badges appear above other players' heads only when:
  * you are actively transmitting voice (plus **3 seconds** afterward);
  * the player is within **30 m**;
  * that player's **SA-MP nametag** is visible (servers can hide nametags via `ShowNameTags`, `ShowPlayerNameTagForPlayer`, nametag distance, or line-of-sight).
* **Blocked Players:** Locally blocked speakers produce zero audio while remaining visible with a red channel icon and localized `[BLOCKED]` tag so you always know someone is speaking nearby.

---

## 3. Supported Languages (15)

| Code | Language | Code | Language | Code | Language |
| :---: | :--- | :---: | :--- | :---: | :--- |
| `id` | Bahasa Indonesia | `it` | Italiano | `vi` | Tiếng Việt |
| `de` | Deutsch | `hu` | Magyar | `tr` | Türkçe |
| `en` | English *(default)* | `pl` | Polski | `bg` | Български |
| `es` | Español | `pt` | Português | `ru` | Русский |
| `fr` | Français | `ro` | Română | `uk` | Українська |

---

## 4. `sampvoice.ini` Reference & Log Location

Configuration and logs are saved inside **`Documents\GTA San Andreas User Files\SAMP\sampvoice\`** (`sampvoice.ini`, plus `sampvoice_plus_v<version>_log.txt`, multi-instance numbered logs, and `[ERROR] ...` archives). Line 1 of every session log records the SHA-256 hash of `sampvoice.asi` for verification.

### `[general]`

| Key | Default | Range / Values | Description |
| :--- | :---: | :---: | :--- |
| `menu_hotkey` | `0x7A` (`F11`) | `0x01`–`0xFE` | Virtual key code to toggle the settings window |
| `language` | `en` | 15 language codes | Active UI language |
| `menu_x` / `menu_y` | `-1` | `-1` or `0.0`–`1.0` | Saved menu position (`-1` = centered) |
| `menu_w` / `menu_h` | `-1` | `-1` or `0.05`–`1.0` | Saved manual menu size (used only when `menu_size_manual = 1`) |
| `menu_size_manual` | `0` | `0` / `1` | `0` = automatic fit size; `1` = keep user drag-resized dimensions |
| `show_speaker_list` | `1` | `0` / `1` | Show `"Speaking now"` list overlay |
| `speaker_list_header` | `1` | `0` / `1` | Show `"Speaking now"` header line |
| `speaker_list_bg` | `1` | `0` / `1` | Draw background behind speaker list |
| `speaker_list_bg_opacity` | `45` | `0`–`100` | Speaker list background opacity (%) |
| `speaker_list_max` | `5` | `1`–`15` | Maximum visible speaker rows |
| `speaker_list_scale` | `1.0` | `0.75`–`2.0` | Speaker list text/layout scale |
| `speaker_list_x` / `speaker_list_y` | `0.0219` / `0.3944` | `0.0`–`1.0` | Normalized screen position of speaker list |
| `show_micro_icon` | `1` | `0` / `1` | Show local microphone status sprite |
| `micro_icon_scale` | `1.25` | `0.5`–`2.5` | Microphone icon scale |
| `micro_icon_x` / `micro_icon_y` | `0.1516` / `0.6907` | `0.0`–`1.0` | Normalized screen position of mic icon |
| `show_overhead_icons` | `1` | `0` / `1` | Show 3D voice sprites above players' heads |
| `overhead_icon_bg_opacity` | `55` | `0`–`100` | Backdrop disc opacity (%) behind 3D overhead icons (`0` = off) |
| `overhead_icon_height` | `0` | `0`–`60` | Extra vertical pixel offset above SA-MP nametags |
| `speaker_icon_scale` | `1.0` | `0.5`–`2.5` | 3D overhead icon scale |

### `[sound]`

| Key | Default | Range / Values | Description |
| :--- | :---: | :---: | :--- |
| `sound_enable` | `1` | `0` / `1` | Enable incoming voice audio playback |
| `sound_volume` | `100` | `0`–`100` | Master incoming voice volume (%) |
| `local_volume` | `100` | `0`–`200` | Volume multiplier (%) for positional / proximity streams |
| `global_volume` | `100` | `0`–`200` | Volume multiplier (%) for global streams |
| `sound_balancer` | `0` | `0` / `1` | Enable RMS volume balancer / compressor |
| `sound_filter` | `0` | `0` / `1` | Enable 80 Hz speech clarity high-pass filter |
| `deafen_hotkey` | `0` | `0` or `0x01`–`0xFE` | Optional toggle hotkey for client Deafen (`0` = none) |

### `[microphone]`

| Key | Default | Range / Values | Description |
| :--- | :---: | :---: | :--- |
| `micro_enable` | `1` | `0` / `1` | Enable microphone capture |
| `micro_gain` | `100` | `0`–`200` | Mic input gain (`100` = unity; soft limiter above `100`; old `micro_volume` migrates automatically) |
| `micro_device` | *(empty)* | Device name | Windows input device name (empty = System Default) |
| `micro_noise_suppress` | `1` | `0` / `1` | Enable software microphone noise suppression |
| `talk_mode` | `push` | `push`, `ptt`, `0` / `toggle`, `1` | Activation key mode (Push-to-Talk vs Toggle) |

### `[blacklist]` & `[player_volume]`
* **`[blacklist]`:** `<player> | <server name> | <IP>:<port> | <server|all>`.
* **`[player_volume]`:** `<player> | <server name> | <IP>:<port> | <0-200>` *(default `100` entries are omitted)*.
