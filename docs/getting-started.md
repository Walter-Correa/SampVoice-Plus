# Getting Started with SampVoice Plus

This guide walks you through installing **SampVoice Plus** on both the player client (`sampvoice.asi`) and your **SA-MP** or **open.mp** 32-bit x86 server, configuring server options, and migrating existing gamemodes.

---

## 1. Client Installation (Installer or Single File)

**SampVoice Plus** requires an existing SA-MP client (`samp.dll` + `bass.dll`, available at [sa-mp.mp/downloads](https://www.sa-mp.mp/downloads/) or via the [open.mp launcher](https://open.mp/downloads/launcher)) plus an ASI Loader (`dinput8.dll`). Download **`SampVoice-Plus-Client-Installer.zip`** (for players) and choose between the two included installation methods (`Installer/SampVoice-Plus-Setup.exe` or `Manual Install/sampvoice.asi`, each accompanied by an `Instructions/` folder with guides in all 15 languages):

1. **Guided Installer (`Installer/SampVoice-Plus-Setup.exe`):** 15-language Inno Setup installer that auto-detects GTA San Andreas, installs `sampvoice.asi` + [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (`dinput8.dll`, MIT) if needed, offers to back up or permanently remove legacy SampVoice (always keeping shared `bass.dll`, `vorbisFile.dll`, and `vorbisHooked.dll`), stores `unins000.*`, `manifest.ini`, and `THIRD-PARTY-NOTICES.txt` inside `sampvoice-plus\`, and places the `Uninstall SampVoice Plus.lnk` shortcut in the GTA San Andreas root folder. Reinstalling or updating SA-MP later keeps voice working intact.
   - **Legacy Migration & Uninstall Notes:** If legacy CyberMor or AmyrAhmady SampVoice was installed via setup (`{01EA93A6-5255-44FE-ADB9-FDC3EFE89675}_is1`) or manually, the installer defaults to **Backup (`backup`)**, copying exclusive legacy files (including root `unins000.exe`/`unins000.dat` if registered) into `sampvoice-plus\backup\` and silently running the legacy uninstaller so it cannot delete Plus later. Uninstalling SampVoice Plus restores the backed-up legacy files to the GTA folder (the legacy Windows "Installed apps" registry entry is not recreated, though the restored legacy client works normally). Choosing **Uninstall legacy SampVoice** permanently deletes exclusive legacy files (`sampvoice.asi`, `scripts\sampvoice*.asi`, `sampvoice\`, `bass_fx.dll`) and optionally deletes legacy settings in `Documents\GTA San Andreas User Files\sampvoice\` (used by both CyberMor `storage.hpp` and AmyrAhmady `Path.cpp` / `svconfig.bin`).
   - **Silent Installer Parameters:** `/VERYSILENT` uses **backup** mode by default. Pass `/LEGACY=remove` to permanently remove legacy SampVoice without prompts, and combine `/LEGACY=remove /LEGACYCONFIG=remove` to also delete legacy `Documents\GTA San Andreas User Files\sampvoice\`. Running the same or newer installer version verifies/repairs or updates automatically; silent downgrades are blocked cleanly with a non-zero exit code.
2. **Manual Install (`Manual Install/sampvoice.asi`):** If you already use an ASI Loader (`dinput8.dll`, Silent's ASI Loader, or CLEO), copy **only `sampvoice.asi`** directly into your GTA San Andreas **root directory** (where `gta_sa.exe` lives — not `scripts\`). Full license notices are in `THIRD-PARTY-NOTICES.txt` at the root of `SampVoice-Plus-Client-Installer.zip`:
   ```
   [GTA San Andreas Root]/
     ├── gta_sa.exe
     ├── dinput8.dll           (Any ASI Loader; bundled optionally by Setup.exe)
     ├── samp.dll              (Any supported build: R1, R2, R3/R3-1, R4, R4-2, R5-1/R5-2-MP, 0.3.DL-R1/DL-R1-2-MP, or open.mp)
     ├── bass.dll              (Provided by SA-MP)
     └── sampvoice.asi         <-- Single standalone plugin file (root folder only)
   ```
3. Launch the game and connect to any SampVoice-enabled server.
4. Press **`F11`** (default hotkey) at any time in-game to open the settings panel, rebind the menu hotkey, choose your microphone, adjust DSP filters, or switch between **15 languages** in real time.

> [!NOTE]
> The configuration file (`sampvoice.ini`, including `[blacklist]`) and session logs (`sampvoice_plus_v<version>_log.txt`, or `sampvoice_plus_v<version>_log (2).txt` / `_log (pid <pid>).txt` for concurrent GTA instances, plus `[ERROR] sampvoice_plus_v<version>_log_<N>.txt` on unclean exits or errors) are stored automatically in `Documents\GTA San Andreas User Files\SAMP\sampvoice\` only after SA-MP is detected. Never keep legacy `sampvoice.asi` alongside SampVoice Plus; the client detects conflicting `.asi` modules cleanly in either load order. See **[Migrating from Legacy SampVoice](migrating-from-sampvoice.md)** if upgrading from 3.x/4.x or AmyrAhmady.

---

## 2. Server Installation & Configuration

### Step 1: File Placement

Extract the server release archive for your OS (**`SampVoice-Plus-Server-Windows.zip`** or **`SampVoice-Plus-Server-Linux.zip`**) and copy files from the archive's root folders (`components/`, `plugins/`, `include/`) into your server (`LICENSE` and `THIRD-PARTY-NOTICES.txt` live at the archive root):

```
[Server Root]/
  ├── components/               <-- For open.mp (Native Component Mode - Recommended)
  │     └── sampvoice.dll     (components/sampvoice.dll or .so on Linux)
  ├── plugins/                  <-- For SA-MP 0.3.7-R2/R3 & 0.3.DL-R1 servers
  │     └── sampvoice.dll     (plugins/sampvoice.dll or .so on Linux)
  └── pawno/include/            (or qawno/include/)
        └── sampvoice.inc     (from include/sampvoice.inc)
```

> [!WARNING]
> Do **not** install both the open.mp component in `components/` and the SA-MP plugin in `plugins/` at the same time on open.mp. Choose one mode only. Legacy plugin mode supports **SA-MP `0.3.7-R2` (R2-1, R2-2-1), `0.3.7-R3`, and `0.3.DL-R1` on both Windows & Linux** (`32-bit x86`); open.mp servers must use `components/`.

### Step 2: Configure the Host

#### Option A: open.mp (`components/`) — Recommended
Place `sampvoice.dll` (or `sampvoice.so` on Linux) inside `components/`. open.mp detects and loads it automatically without plugin entries. Optional tuning parameters inside **`config.json`**:
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
*Note: `"port": 0` selects an automatic ephemeral port that changes on every restart and logs a `[sv+:WRN]` warning.*

#### Option B: SA-MP 0.3.7-R2 / R3 / 0.3.DL-R1 (`server.cfg`)
Place `sampvoice.dll` (or `sampvoice.so`) inside `plugins/` and update **`server.cfg`**:

```cfg
plugins sampvoice.dll    # (or sampvoice.so on Linux)

# Optional tuning keys:
sampvoice.port 7778      # 0 = automatic ephemeral port (logs a [sv+:WRN] warning)
sampvoice.bind 0.0.0.0
sampvoice.threads 4
sampvoice.updaterate 30
sampvoice.legacy_color_format 0
sampvoice.voice_status 1
sampvoice.nearby_list 1
sampvoice.allow_toggle_mode 1
sampvoice.voice_occlusion 1
```

| Parameter | Default | Range | Description |
| :--- | :---: | :---: | :--- |
| `sampvoice.port` | `0` | `0` .. `65535` | UDP voice socket port (`0` selects an ephemeral port logged at startup). Fixed ports bind with `SO_EXCLUSIVEADDRUSE` and fail fast if busy or invalid instead of silently picking a random port. Aliases `sampvoice_port` / `sv_voice_port` supported (warns if duplicate). |
| `sampvoice.bind` | `0.0.0.0` | IPv4 address | Explicit IPv4 interface to bind for voice UDP (`sampvoice_bind` alias supported). Inherits host `bind` / `network.bind` when omitted. |
| `sampvoice.threads` | `4` | `1` .. HW cores | Number of worker threads servicing UDP receive/forwarding queues (`sampvoice_threads` alias supported). |
| `sampvoice.updaterate` | `30` | `10` .. `1000` | Spatial grid rebuild & dynamic listener sync cadence in ms (`sampvoice.update_rate` / `sampvoice_updaterate` aliases supported). |
| `sampvoice.legacy_color_format` | `0` (`false`) | `0` / `1` | When `0` (`false`), converts Pawn `0xRRGGBBAA` stream colors to wire `0xAARRGGBB`. Set `1` (`true`) if scripts already pass `0xAARRGGBB`. |
| `sampvoice.voice_status` | `1` (`true`) | `0` / `1` | Enables `PlusPlayerVoiceState` (`109`) replication so Plus `v12` clients can display no-mic (`micro_missing`) / cannot-hear (`speaker_deaf`) indicators in the **Players** tab and overhead (never exposes `SvMutePlayerEnable` or blocks). |
| `sampvoice.nearby_list` | `1` (`true`) | `0` / `1` | Enables the **All / Nearby** filter in the client `F11` **Players** tab (lists streamed-in SA-MP players closest-to-farthest without distance numbers; SA-MP already keeps streamed players in client memory, so disabling this only hides the UI filter). |
| `sampvoice.allow_toggle_mode` | `1` (`true`) | `0` / `1` | Allows **PC SampVoice Plus `1.2.0+`** clients to use **Toggle** talk mode. Setting `0` (`false`) disables Toggle cleanly and forces effective **Push-to-Talk** without changing the player's `sampvoice.ini`. Android launchers and older/legacy clients manage hold/toggle locally and only send standard `PressKey`/`ReleaseKey` packets (identical on the wire to holding a key), so servers needing to cap forgotten holds on those clients can pair `OnPlayerActivationKeyPress` with a timer and timed `SvMutePlayerEnable` (use `SvGetPlusClientVersion` to distinguish desktop `1.2.0+` clients). |
| `sampvoice.voice_occlusion` | `1` (`true`) | `0` / `1` | Enables smooth wall/building voice muffling (`~1000 Hz` low-pass + `−6 dB` gain) on positional voice streams for Plus `1.2.0+` clients (on by default; set `0` / `false` to disable; global streams such as radio/phone are never muffled, and shoot-through chain-link fences/scaffolding/cloth pass sound normally). |

---

## 3. Pawn Script Setup

Include `<sampvoice>` after `<a_samp>` or `<open.mp>`:

```pawn
#include <a_samp> // or <open.mp>
#include <sampvoice>

public OnGameModeInit()
{
    return 1;
}
```

See **[`Example.pwn`](../Example.pwn)** in the repository root for a complete multi-feature example.

---

## 4. Drop-In Migration from Legacy SampVoice

**SampVoice Plus** maintains **100% source and wire-protocol backward compatibility** with `CyberMor/sampvoice` v3.x and `AmyrAhmady/sampvoice`:

1. **Zero Pawn Native / Callback Changes Required:** All existing `Sv*` natives, `SV_PARAMETER_*` constants, `SV_*` handle tags, and `OnPlayerActivationKeyPress` / `OnPlayerActivationKeyRelease` callbacks keep identical signatures and semantics. *(The only behavioral difference is stream tag color order: Plus expects `0xRRGGBBAA` by default—set `sampvoice.legacy_color_format 1` if migrating ARGB colors from legacy servers; see **[Migrating from Legacy SampVoice](migrating-from-sampvoice.md#stream-tag-colors-sampvoicelegacy_color_format)**.)*
2. **Legacy Client Compatibility (`v11`):** Older `sampvoice.asi` releases (`CyberMor` v3.x / `AmyrAhmady` protocol `v11`) continue to connect and exchange voice/control packets seamlessly.
3. **Plus Capability Detection in Pawn:** Use `SvGetVersion(playerid)` (`11` for both legacy `v11` and current Plus `v12` clients, or higher if a client sends `version >= 12` directly) and `SvGetClientProtocol(playerid)` (`11` for legacy vs `12` for SampVoice Plus clients) alongside `SvGetPlusCapabilities(playerid)`.
4. **Automatic GMX / Script Cleanup:** Every stream and effect created by a gamemode or filterscript is tracked per `AMX*` and automatically reclaimed if that script unloads without calling `SvDeleteStream`/`SvEffectDelete`.
