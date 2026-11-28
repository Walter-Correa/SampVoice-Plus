# Getting Started with SampVoice Plus

This guide covers installing **SampVoice Plus** on your Windows GTA San Andreas client and your **SA-MP** or **open.mp** 32-bit x86 server, configuring server options, and trying the built-in interactive demo.

> [!TIP]
> **Already using SampVoice?**
> 1. Replace your old server plugin/component with SampVoice Plus and update `sampvoice.inc`.
> 2. **Scripts from CyberMor 3.x or AmyrAhmady compile and run without changes** (existing `v11` players and Android launchers also stay connected).
> 3. **Two things to keep in mind:** stream tag colors default to `0xRRGGBBAA` (set `sampvoice.legacy_color_format 1` if your old script passes `0xAARRGGBB`), and **CyberMor 4.x** scripts use a different API and must be adapted. See **[Migrating from Legacy SampVoice](migrating-from-sampvoice.md)** for details.

---

## 1. Client Installation

**SampVoice Plus** requires an existing SA-MP client (`samp.dll` + `bass.dll`, from [sa-mp.mp/downloads](https://www.sa-mp.mp/downloads/) or the [open.mp launcher](https://open.mp/downloads/launcher)) plus an ASI Loader. Download **`SampVoice-Plus-Client-Installer.zip`** and choose one of the two methods (each folder also contains translated `Instructions/` in all 15 languages):

1. **Guided Installer (`Installer/SampVoice-Plus-Setup.exe`):** 15-language setup wizard that detects your GTA San Andreas directory, checks SA-MP, installs `sampvoice.asi` (plus Ultimate ASI Loader `dinput8.dll` if no ASI Loader is present), and backs up any existing legacy SampVoice installation cleanly so uninstalling Plus restores it. Reinstalling or updating SA-MP later leaves SampVoice Plus working.
2. **Manual Install (`Manual Install/sampvoice.asi`):** If you already have an ASI Loader (`dinput8.dll`, Silent's ASI Loader, or CLEO), copy **only `sampvoice.asi`** directly into your GTA San Andreas **root folder** (next to `gta_sa.exe`, **not** inside `scripts\`):
   ```text
   [GTA San Andreas Root]/
     ├── gta_sa.exe
     ├── dinput8.dll           (Any ASI Loader)
     ├── samp.dll              (SA-MP 0.3.7-R1..R5, 0.3.DL-R1, or open.mp)
     ├── bass.dll              (Provided by SA-MP)
     └── sampvoice.asi         <-- Single standalone file
   ```
3. Launch SA-MP and join a SampVoice-enabled server. Press **`F11`** (default hotkey) at any time in-game to open settings, change your microphone or hotkey, or switch between **15 languages** live.

Settings (`sampvoice.ini`) and session logs live under `Documents\GTA San Andreas User Files\SAMP\sampvoice\` (created only when SA-MP runs; singleplayer GTA San Andreas is left completely untouched). Never leave an older `sampvoice.asi` installed alongside SampVoice Plus.

### Installer Command-Line Options
For automated or silent deployments of `SampVoice-Plus-Setup.exe`:
* `/VERYSILENT` — runs non-interactively using **Backup** mode for any existing legacy SampVoice files.
* `/LEGACY=remove` — permanently removes legacy SampVoice instead of backing it up.
* `/LEGACY=remove /LEGACYCONFIG=remove` — also removes legacy configuration files under `Documents\GTA San Andreas User Files\sampvoice\`.

For full installer backup/restore mechanics, see [Under the Hood — Windows Installer Internals](under-the-hood.md#6-windows-installer-internals-sampvoiceplusiss).

---

## 2. Server Installation & Configuration

### Step 1: File Placement

Extract **`SampVoice-Plus-Server-Windows.zip`** or **`SampVoice-Plus-Server-Linux.zip`** into your server folder:

```text
[Server Root]/
  ├── components/
  │     └── sampvoice.dll     (open.mp: components/sampvoice.dll or .so on Linux)
  ├── plugins/
  │     └── sampvoice.dll     (SA-MP 0.3.7-R2/R3 & 0.3.DL-R1: plugins/sampvoice.dll or .so)
  ├── filterscripts/
  │     ├── FS_SampVoicePlus_Demo.amx   <-- Ready-to-load interactive demo
  │     └── FS_SampVoicePlus_Demo.pwn
  └── pawno/include/          (or qawno/include/)
        └── sampvoice.inc     (from include/sampvoice.inc)
```

> [!WARNING]
> On **open.mp**, install **only** inside `components/`. Do **not** load both `components/sampvoice.dll` and `plugins/sampvoice.dll` at the same time. SA-MP plugin mode supports **SA-MP `0.3.7-R2` (R2-1, R2-2-1), `0.3.7-R3`, and `0.3.DL-R1`** on Windows and Linux (32-bit x86).

### Step 2: Configure the Host

#### Option A: open.mp (`config.json`) — Recommended
open.mp loads `components/sampvoice.dll` (or `.so`) automatically. Add the `"sampvoice"` section inside **`config.json`**:
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
    "voice_occlusion": true
  }
}
```
*Note: `"port": 0` selects an automatic ephemeral port that changes on every restart and logs a warning.*

#### Option B: SA-MP 0.3.7-R2 / R3 / 0.3.DL-R1 (`server.cfg`)
Add the plugin and optional settings to **`server.cfg`**:

```cfg
plugins sampvoice.dll    # (or sampvoice.so on Linux)

# Optional tuning keys:
sampvoice.port 7778      # 0 = automatic ephemeral port (logs a warning)
sampvoice.bind 0.0.0.0
sampvoice.threads 4
sampvoice.updaterate 30
sampvoice.legacy_color_format 0
sampvoice.voice_status 1
sampvoice.nearby_list 1
sampvoice.allow_toggle_mode 1
sampvoice.voice_occlusion 1
```

### Server Configuration Reference

| Parameter | Default | Range | Description |
| :--- | :---: | :---: | :--- |
| `sampvoice.port` | `0` | `0` .. `65535` | UDP port for voice audio (`0` = automatic OS port, changes on restart). Set a fixed port and open it for UDP in your firewall. |
| `sampvoice.bind` | `0.0.0.0` | IPv4 address | Network interface to bind for voice UDP. When omitted, inherits your server's `bind` / `network.bind` IP if configured. |
| `sampvoice.threads` | `4` | `1` .. CPU cores | Worker threads handling voice UDP traffic. |
| `sampvoice.updaterate` | `30` | `10` .. `1000` | How often dynamic proximity streams recalculate who is in range (in milliseconds). |
| `sampvoice.legacy_color_format` | `0` (`false`) | `0` / `1` | Keeps standard `0xRRGGBBAA` Pawn colors when `0`. Set `1` if migrating scripts that pass legacy `0xAARRGGBB` stream colors. |
| `sampvoice.voice_status` | `1` (`true`) | `0` / `1` | Shows "No mic" / "Can't hear" indicators to Plus clients in the `F11` Players tab and above heads (`0` disables broadcasting). |
| `sampvoice.nearby_list` | `1` (`true`) | `0` / `1` | Enables the **All / Nearby** filter inside the player's `F11` Players tab (`0` hides the Nearby filter). |
| `sampvoice.allow_toggle_mode` | `1` (`true`) | `0` / `1` | Allows **PC SampVoice Plus `1.2.0+`** clients to use **Toggle** talk mode. Setting `0` enforces **Push-to-Talk** without changing the player's saved file *(Android launchers built on the SampVoice Plus SDK honor this flag; legacy `v11` Android launchers and older clients cannot be restricted by it, so scripts can cap long holds with `OnPlayerActivationKeyPress` + timed `SvMutePlayerEnable`)*. |
| `sampvoice.voice_occlusion` | `1` (`true`) | `0` / `1` | Muffles positional streams behind solid buildings/walls on **SampVoice Plus `1.2.0+`** clients (global streams like radios and chain-link fences remain clear; set `0` to keep unmuffled audio everywhere). |

---

## 3. Pawn Script Setup

Include `<sampvoice>` after `<a_samp>` or `<open.mp>`:

```pawn
#include <a_samp> // or <open.mp>
#include <sampvoice>
```

That single include is all you need when compiling **CyberMor 3.x** or **AmyrAhmady** scripts or adopting new SampVoice Plus features. *(Already compiled `.amx` gamemodes and filterscripts from CyberMor 3.x or AmyrAhmady keep working without recompiling; you only need the updated `sampvoice.inc` if you recompile or call new APIs.)*

### Try the Demo (`FS_SampVoicePlus_Demo`)
The pre-compiled **`filterscripts/FS_SampVoicePlus_Demo.amx`** is included **ready both in the Windows/Linux server zips and in the repository (`filterscripts/`)** alongside **`filterscripts/FS_SampVoicePlus_Demo.pwn`**:
* **SA-MP (`server.cfg`):** add `FS_SampVoicePlus_Demo` to your `filterscripts` line.
* **open.mp (`config.json`):** add `"filterscripts/FS_SampVoicePlus_Demo"` under `"pawn"` -> `"side_scripts"`.
* **Runtime RCON:** run `loadfs FS_SampVoicePlus_Demo`.

Once loaded, type **`/voice`** or **`/voip`** in-game to test proximity chat, faction radio channels, 1-on-1 phone calls, megaphones, vehicle intercoms, movable point speaker boxes, object audio, key-to-stream bindings, channel priority with per-channel ducking, megaphone distortion presets, parameter fading, all 9 DX8 filters, privacy mutes, stream inspection, the RCON admin server statistics dashboard, and live callback feeds without disturbing your existing gamemode.

---

## 4. Migrating an Existing Server

Already running CyberMor or AmyrAhmady SampVoice? See **[Migrating from Legacy SampVoice](migrating-from-sampvoice.md)** for the complete player/server checklist, stream color migration, CyberMor 4.x native mapping, and a 4-level roadmap for adding Plus features gradually.
