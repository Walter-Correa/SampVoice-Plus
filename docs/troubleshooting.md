# SampVoice Plus — Troubleshooting Guide

This guide helps players and server owners diagnose and solve the most common setup, firewall/UDP, microphone, hotkey, audio, and script issues in **SampVoice Plus**. For supported game/server versions, see **[Supported Platforms](../README.md#supported-platforms)**.

---

## 1. Reading the In-Game `F11` Status Line

Press **`F11`** (or your custom menu key) in-game and check the color-coded **`Status:`** banner right below the top title:

| Status Text (English UI) | Color | Meaning & How to Fix |
| :--- | :---: | :--- |
| **Not connected** | Red | You are not connected to a SA-MP or open.mp server yet. |
| **Connecting…** | Light gray | Game client just joined; waiting for the voice handshake. |
| **Not connected: this server does not have SampVoice** | Red | No SampVoice server plugin/component responded. On servers you own, confirm `components/sampvoice.dll` (`.so`) on open.mp or `plugins sampvoice` on SA-MP is loaded. |
| **Not connected: voice port `<port>` is blocked** | Red | The game connection reached the server, but UDP audio packets to `<port>` could not get through. Open **UDP** port `<port>` in your server firewall/router and configure a fixed `sampvoice.port` (see Section 2). |
| **Connected, but microphone is disabled** | Yellow | You can **hear** others, but **Enable microphone** is unchecked under `F11` → Microphone. |
| **Connected, but no microphone is enabled or available** | Yellow | You can **hear** others, but Windows denied mic access or no capture device is ready (`F11` → Microphone → **Refresh devices**). |
| **Connected, but muted by the server** | Yellow | A server script muted your microphone (`SvMutePlayerEnable`). |
| **Connected, but listening is disabled by the server** | Yellow | A server script suppressed incoming voice audio for you (`SvDisableListener`). |
| **Connected** | Green | Voice send/receive channels are active (`Voice RTT` shows control latency in `F11` → General). |

---

## 2. Firewall, Fixed UDP Port & Server Configuration

By default (`sampvoice.port = 0`), the OS picks a random UDP port every restart. Behind a VPS firewall, NAT router, or container, clients fail the UDP check until a fixed UDP port is configured and allowed:

### open.mp (`config.json`)
```json
{
  "sampvoice": {
    "port": 7778,
    "bind": "0.0.0.0",
    "threads": 4,
    "updaterate": 30,
    "legacy_color_format": false,
    "voice_status": true,
    "nearby_list": true,
    "allow_toggle_mode": true,
    "voice_occlusion": true,
    "priority_duck": 50
  }
}
```

### SA-MP (`server.cfg`)
```cfg
sampvoice.port 7778
sampvoice.bind 0.0.0.0
sampvoice.threads 4
sampvoice.updaterate 30
sampvoice.legacy_color_format 0
sampvoice.voice_status 1
sampvoice.nearby_list 1
sampvoice.allow_toggle_mode 1
sampvoice.voice_occlusion 1
sampvoice.priority_duck 50
```

See the **[Server Configuration Reference](getting-started.md#server-configuration-reference)** for the default, range, and effect of every option.

* **Check Startup Logs:** Look in the server console or `server_log.txt`:
  ```text
  [sv+:INF] SampVoice Plus <version> voice server running on UDP port 7778
  ```
  If `sampvoice.port` is `0` or omitted, it logs `[sv+:WRN] SampVoice Plus <version> voice server running on automatic ... (changes on every restart); set sampvoice.port to a fixed value and open it as UDP in your firewall`. Calling `SvDebug(true)` prints the full resolved `Effective config:` summary once.
* **Client Has Plugin (`SvGetVersion(playerid) != 0`), but `SvIsPlayerVoiceReady` Stays `false`:**
  * The player joined with the plugin installed, so `SvGetVersion` returns `11`, **however** UDP voice traffic to `sampvoice.port` is blocked by a firewall/NAT rule or port-forward mismatch.
  * You can warn players from Pawn using `SvIsPlayerVoiceReady` on a short timer or listen for `OnPlayerVoiceReady(playerid)`:
    ```pawn
    public OnPlayerConnect(playerid)
    {
        if (SvGetVersion(playerid) != SV_NULL)
        {
            SetTimerEx("CheckVoiceReady", 5000, false, "d", playerid);
        }
        return 1;
    }

    forward CheckVoiceReady(playerid);
    public CheckVoiceReady(playerid)
    {
        if (IsPlayerConnected(playerid) && SvGetVersion(playerid) != SV_NULL && !SvIsPlayerVoiceReady(playerid))
        {
            SendClientMessage(playerid, 0xE67E22FF,
                "[Voice] Plugin detected, but UDP voice connection timed out. Check UDP port / firewall.");
        }
        return 1;
    }

    public SV_VOID:OnPlayerVoiceReady(SV_UINT:playerid)
    {
        SendClientMessage(playerid, 0x2ECC71FF, "[Voice] Voice UDP connection ready!");
    }
    ```
* **Explicit Port Safety:** If an explicit `sampvoice.port` is out of range or already used by another program, SampVoice Plus logs an error and refuses to start on a random port.
* **No Duplicate `svplus.log`:** Messages go straight into SA-MP / open.mp logs without creating a separate `svplus.log`.

---

## 3. Microphone Not Working or Disconnected Mid-Game

1. **Hot-Plug & Disconnect Recovery:** USB microphones are scanned every `2` seconds (and right away if an active mic unplugs), falling back cleanly or reopening your preferred device automatically when reconnected.
2. **In-Game Check:** Open **`F11` → Microphone**, ensure **Enable microphone** is checked, select your device (or **Default System Device**), click **Refresh devices** if needed, and verify movement on **Microphone level** (or test with **Listen to my mic**).
3. **Windows Privacy Permissions:** In Windows Settings → *Privacy & security* → *Microphone*, allow desktop apps (`gta_sa.exe`) to use the microphone.

---

## 4. Keys, Talk Modes & Android `B` Key

* **Push-to-Talk vs Toggle:** Switch under **`F11` → Microphone → Talk mode**. Toggle resets automatically on disconnect.
* **"Toggle" Is Grayed Out / Disabled:** The connected server configured `sampvoice.allow_toggle_mode = 0` (`false` in `config.json`) to enforce Push-to-Talk on desktop Plus `1.2.0+` clients. Your saved preference in `sampvoice.ini` is **not** modified and returns automatically on servers that allow Toggle. *(Android and older clients cannot be restricted by this flag; server scripts can cap forgotten holds with `OnPlayerActivationKeyPress` + timed `SvMutePlayerEnable`.)*
* **Menu Hotkey Conflict:** If the server binds `F11` (`0x7A`), SampVoice Plus ignores that key for transmission and shows a conflict banner so you can rebind your menu hotkey under **`F11` → General**.
* **Typing in Chat or Dialogs:** Voice PTT keys and the menu hotkey are paused while SA-MP chat, dialogs (`0.3.7-R1`–`0.3.DL-R1`), or the pause menu are open.
* **Android Players Hear Others, Show Mic Active, Cannot Speak (`0x42` / `B`):**
  * Android launchers with embedded SampVoice ignore server `SvAddKey` bindings and always transmit key `0x42` (`B`). Always include `0x42` (`'B'`) in your key bindings/callbacks if mobile players connect.

---

## 5. Audio Quality, Occlusion, Colors & Client Logs

* **Voice Sounds Muffled Behind Walls:** Servers enable **`sampvoice.voice_occlusion`** by default, muffling local positional voices behind solid buildings/walls on Plus `1.2.0+` clients (radios/global channels and chain-link fences remain clear). Server owners who prefer open voice through walls can set **`sampvoice.voice_occlusion 0`** (`"voice_occlusion": false` in `config.json`).
* **Connected, Can't Hear Anyone:**
  * Check whether client **Deafen** is active (`F11` → Audio → **Deafen** / `deafen_hotkey` — red speaker icon on HUD).
  * Verify **Enable incoming sound**, **Master volume**, **Proximity voice volume**, **Global channels**, and the player's individual `%` mix / Block toggle in **`F11` → Players**.
  * Check the banner for *"listening is disabled by the server"* (`SvDisableListener`).
* **Radio Streams (`PlayAudioStreamForPlayer`) Sound Mono/Quiet?** Not with SampVoice Plus — it preserves SA-MP's global stereo audio configuration intact.
* **Wrong Stream Tag Colors on Migrated Scripts:** Enable `sampvoice.legacy_color_format 1` (`"legacy_color_format": true` in `config.json`) if your existing gamemode passes colors as `0xAARRGGBB` instead of `0xRRGGBBAA`.
* **Adjusting 3D Overhead Icons:** Open **`F11` → General** to toggle **Show 3D overhead icons**, adjust **Overhead icon background opacity**, or adjust **Overhead Icon Height** (`0–60 px`).
* **Client Log & Config Folder:** Saved under `Documents\GTA San Andreas User Files\SAMP\sampvoice\` (`sampvoice.ini` and `sampvoice_plus_v<version>_log.txt`; crash/error archives keep only the 10 newest `[ERROR]` logs; line 1 logs the loaded `sampvoice.asi` SHA-256).

---

## 6. Frequently Asked Server & Scripting Questions

### "`[sv+:WRN] SvXxx: rejected non-finite …`" in the server log
A Pawn script passed `NaN` or `±Infinity` into a SampVoice native — almost always from dividing by `0.0`, multiplying an uninitialized float, or taking `floatsqroot` of a negative number before calling `SvCreate*LocalStream*`, `SvUpdate*`, `SvStreamParameter*`, or `SvEffect*`. The native name (`SvXxx`) printed right in the warning tells you which call received the bad value (logged at most once per native and script every 60 s so the console is never flooded).

### A stream attached to an object is silent
`SvCreateSLStreamAtObject` and `SvCreateDLStreamAtObject` accept **only standard GTA global objects** created with `CreateObject`. If you passed an ID from the Streamer plugin (`CreateDynamicObject`), replace it with **`SvCreateDLStreamAtPoint`** at the object's coordinates (`worldid`, `interiorid`). See [Standard GTA Objects vs Streamer Objects](audio-streams-and-effects.md#4-position-movement--standard-vs-streamer-objects).

### Players with older clients don't see live names, colors, or icons
* **Stream icons (`SvStreamSetIcon`):** require **SampVoice Plus `1.0.0+`** on the player's PC (`v11` and Android clients keep the default speaker sprite).
* **Live metadata & effects (`SvStreamSetName`, `SvStreamSetColor`, `SvEffectUpdate*`):** update live in place for **SampVoice Plus `1.3.0+`** players; legacy `v11` and `1.0.x`–`1.2.x` Plus players still see the new name, color, and effect, updated automatically during stream silence. Use `SvGetPlusClientVersion(playerid) < SV_PLUS_VERSION_NUMBER` inside `OnPlayerVoiceReady` to notify players with outdated clients.

### 3D voice-status icons ("No mic" / "Can't hear") don't show above players
Check these four conditions when a player isn't showing a 3D status icon:
1. **`sampvoice.voice_status`** must be `1` (enabled by default on the server).
2. **Active transmission window:** 3D voice-status icons appear **only while you are transmitting voice and for 3 seconds afterward**.
3. **Range & SA-MP nametag visibility:** The target player must be within **30 m** with their **SA-MP nametag visible** (servers can hide nametags via `ShowNameTags`, `ShowPlayerNameTagForPlayer`, nametag draw distance, or line-of-sight).

### How do I test everything quickly?
Load the pre-compiled **`filterscripts/FS_SampVoicePlus_Demo.amx`** included **ready in both server zips and in the repository** (`filterscripts FS_SampVoicePlus_Demo` in `server.cfg`, `"filterscripts/FS_SampVoicePlus_Demo"` in `config.json` `pawn.side_scripts`, or `rcon loadfs FS_SampVoicePlus_Demo`), then run **`/voice`** or **`/voip`** in-game to test every feature live.

---

## 7. Server Coexistence & Unsupported Clients

* **`Pawn.RakNet` / `YSF` Coexistence:** Fully compatible on SA-MP `0.3.7-R2`, `0.3.7-R3`, and `0.3.DL-R1` (Windows & Linux) in either plugin load order.
* **CyberMor `4.x` Clients:** Rejected cleanly with an informational log because `4.x` uses an incompatible protocol; players using `4.x` should install **SampVoice Plus** (`sampvoice.asi`). Android 3.x launchers connect normally.

*(For protocol internals, anti-flood limits, spatial math, and SA-MP PE offsets, see **[Under the Hood](under-the-hood.md)**.)*
