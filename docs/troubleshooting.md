# SampVoice Plus — Troubleshooting Guide

This guide helps server owners and players diagnose and resolve the most common setup, firewall/UDP, microphone, hotkey, and compatibility issues in **SampVoice Plus**. See the **[Supported Platforms](../README.md#supported-platforms)** table for the full client, server, OS, Android, and legacy version matrix.

---

## 1. Reading the In-Game F11 Diagnostic Banner

Press **`F11`** (or your custom `menu_hotkey`) in-game to inspect the **`Status:`** line (white `Status:` label followed by a color-coded diagnostic state right below the top header, 9 states):

| Status Text (English UI) | Color | Meaning & How to Fix |
| :--- | :---: | :--- |
| **Not connected** | Red | You are not currently connected to a SA-MP or open.mp server. |
| **Connecting…** | Light gray | Game client just joined; waiting for the voice handshake packets. |
| **Not connected: this server does not have SampVoice** | Red | Server handshake timed out with no `ServerInfo` control packet. Ensure `components/sampvoice.dll` (`.so`) on open.mp or `plugins sampvoice` on SA-MP is loaded and initialized. |
| **Not connected: voice port `<port>` is blocked** | Red | The client received `ServerInfo` over RakNet, but UDP keep-alives to `<port>` got no `PluginInit` response. **Open UDP port `<port>`** in your server firewall/router and configure a fixed `sampvoice.port` (see Section 2). |
| **Connected, but microphone is disabled** | Yellow | Voice handshake succeeded so you can **hear** others, but **Enable microphone** is unchecked under `F11` → Microphone. Enable it to transmit voice. |
| **Connected, but no microphone is enabled or available** | Yellow | Voice handshake succeeded so you can **hear** others, but Windows denied capture permissions or no usable recording device is available (`F11` → Microphone → **Refresh devices**). |
| **Connected, but muted by the server** | Yellow | A Pawn script called `SvMutePlayerEnable(playerid)` for your slot (outbound mic transmission muted by the server). |
| **Connected, but listening is disabled by the server** | Yellow | A Pawn script called `SvDisableListener(playerid)` for your slot (`v12` state; inbound audio is suppressed until `SvEnableListener(playerid)`). |
| **Connected** | Green | Two-way audio and control channels are active (see `Voice RTT` in `F11` → General for latency on `v12` servers). |

---

## 2. Firewall, Fixed UDP Port & IP Binding (`server.cfg` / `config.json`)

By default (`sampvoice.port = 0`), the OS assigns a random ephemeral UDP port on every server start. If you run behind a VPS firewall, NAT router, or Docker container, clients may fail the UDP handshake unless a fixed port is opened:

### open.mp (`config.json`)
```json
{
  "sampvoice": {
    "port": 7778,
    "bind": "0.0.0.0",
    "threads": 4,
    "updaterate": 30
  }
}
```

### SA-MP (`server.cfg`)
```cfg
sampvoice.port 7778
sampvoice.bind 0.0.0.0
sampvoice.threads 4
sampvoice.updaterate 30
```

* **Check Startup Logs:** Look for the startup voice line in the server console/`server_log.txt`/`log.txt`:
  ```text
  [sv+:INF] SampVoice Plus 1.0.0 voice server running on UDP port 7778
  ```
  If `sampvoice.port` is omitted or `0`, this line is logged as a `[sv+:WRN]` warning noting that the automatic UDP port changes on every restart. Calling `SvDebug(true)` from a Pawn script enables debug logging and prints the resolved `Effective config` summary once (`[sv+:DBG] Effective config: port=7778 [config.json] bind=0.0.0.0 [default] ...`).
* **Host IP Inheritance:** If `sampvoice.bind` is omitted, SampVoice Plus automatically inherits `network.bind` (`config.json`) or `bind` (`server.cfg`) when set to a specific IP instead of `0.0.0.0`.
* **Explicit Port Safety:** If an explicit `sampvoice.port` is out of range (`< 0`, `> 65535`, non-numeric) or already occupied by another process, SampVoice Plus logs an `[ERR]` and **refuses to silently fall back** to a random port.
* **No Duplicate `svplus.log`:** When running inside open.mp or SA-MP (`logprintf` sink present), all messages route straight to the host log without creating a redundant `svplus.log` file.
* **Duplicate Aliases:** If you accidentally configure both `sampvoice.port` and legacy aliases (`sampvoice_port` / `sv_voice_port`), a `[WRN]` is logged and dotted `sampvoice.*` takes priority.

---

## 3. Microphone Not Working or Disconnected Mid-Game

1. **Hot-Plug & Disconnect Recovery:** SampVoice Plus scans Windows capture devices every 2 seconds and immediately whenever BASS reports a stopped/unplugged stream. If you unplug a USB headset, it falls back to your default Windows microphone (or pauses capture cleanly) and automatically reopens your preferred device when you plug it back in.
2. **In-Game Check:** Open **`F11` → Microphone**:
   * Ensure **Enable microphone** is checked.
   * Pick your mic under **Input device** (or leave **Default System Device**) and click **Refresh devices** if needed.
   * Speak and watch the **Microphone level** bar move.
3. **Windows Privacy Settings:** In Windows Settings → *Privacy & security* → *Microphone*, allow desktop apps (`gta_sa.exe`) to access the microphone.

---

## 4. Keys, Talk Modes, Chat & Dialogs

* **Push-to-Talk vs Toggle:** Under **`F11` → Microphone → Talk mode**, choose **Push-to-Talk** (hold the server-assigned key to talk) or **Toggle** (press once to latch mic ON, press again to turn OFF; automatically resets on disconnect).
* **Menu Hotkey Conflicts:** If a server binds `F11` (`0x7A`) via `SvAddKey(playerid, ...)`, SampVoice Plus ignores that key for voice transmission and displays a **red conflict warning** in the F11 menu so you can rebind your menu key cleanly (`F11` → General → **Menu Hotkey**).
* **Typing in Chat or SA-MP Dialogs:** Voice activation keys and the F11 menu hotkey are automatically suppressed while the SA-MP chat box, an active **SA-MP dialog** (`0.3.7-R1` through `0.3.DL-R1`), or the ESC pause menu is open.
* **Android Players Hear Audio and Show Mic Icon Active, but Never Transmit Voice (`0x42` / `B` Key):**
  * **Symptom:** A player on an Android launcher with embedded SampVoice hears everyone normally and sees the mic icon active, yet `OnPlayerActivationKeyPress` never fires on the server and nobody hears them.
  * **Cause:** Android launchers with embedded SampVoice ignore server `SvAddKey` packets and always hardcode `0x42` (`B`) in `PressKey` / `ReleaseKey` control packets. Matching legacy behavior, SampVoice Plus only dispatches keys explicitly registered for the player.
  * **Script Fix:** Register `SvAddKey(playerid, 0x42)` for mobile players (or include `0x42` alongside desktop keys) and treat `0x42` in `OnPlayerActivationKeyPress` / `OnPlayerActivationKeyRelease` as "speak on the player's active voice channel".

---

## 5. Audio Quality, Radios & 3D Volume

* SampVoice Plus preserves SA-MP's global BASS output configuration (stereo playback, global buffer, update period, and `BASS_Set3DFactors`) so in-game `PlayAudioStreamForPlayer` radios and custom streams keep full stereo quality.
* To adjust incoming voice volume, speech clarity filter, or compressor balancer without affecting game SFX, open **`F11` → Audio**.
* To toggle overhead 3D speaker sprites above players' heads, uncheck **`F11` → General → Show 3D overhead icons** (`show_overhead_icons=0` in `sampvoice.ini`).
* **To adjust overhead 3D icon height or backdrop:** Open **`F11` → General** for **Overhead icon background opacity** (`overhead_icon_bg_opacity`, default `55%`) and **Overhead Icon Height** (`overhead_icon_height`, `0–60 px` extra offset above the SA-MP nametag, default `0`).
* **Wrong stream tag colors from old scripts:** If a Pawn script already passes `0xAARRGGBB` instead of `0xRRGGBBAA`, enable `sampvoice.legacy_color_format 1` (`"legacy_color_format": true` in open.mp `config.json`).

---

## 6. Server Plugin Coexistence & Unsupported Clients

* **Coexistence with `Pawn.RakNet` / `YSF`:** SampVoice Plus removes its one-shot RakServer inline hook immediately upon server binding. Both plugin load orders (`plugins pawnraknet sampvoice` and `plugins sampvoice pawnraknet`) are supported on SA-MP `0.3.7-R2`, `0.3.7-R3`, and `0.3.DL-R1` (Windows & Linux).
* **SA-MP `0.3.DL-R1` Support:** Fully supported on both **Windows** (`plugins/sampvoice.dll`) and **Linux** (`plugins/sampvoice.so`).
* **CyberMor `4.x` Clients:** Clients sending `0xDEADC0DE` (CyberMor 4.x) cannot connect to `v11`/`v12` servers; the server logs a single `INF` notice and players using 4.x should install **SampVoice Plus** (or 3.x). Android launchers embedding SampVoice 3.x (`v11`) connect normally.
* **Client Log, Hash Verification & Config Locations:** Check `Documents\GTA San Andreas User Files\SAMP\sampvoice\sampvoice_plus_v<version>_log.txt` (with `sampvoice_plus_v<version>_log (2).txt` / `_log (pid <pid>).txt` when running multiple GTA instances, and `[ERROR] sampvoice_plus_v<version>_log_<N>.txt` after crashes/errors) and `sampvoice.ini` (settings + `[blacklist]`). Line 1 logs your loaded `sampvoice.asi` SHA-256 hash and `[INF:mods]` summarizes loaded modules, hooks, and GTA code mods to help verify official binaries (`certutil -hashfile sampvoice.asi SHA256`).
