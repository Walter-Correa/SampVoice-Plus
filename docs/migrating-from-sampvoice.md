# Migrating from Legacy SampVoice (`CyberMor` 3.x / 4.x & `AmyrAhmady`)

This guide helps server administrators and Windows players move cleanly from **CyberMor SampVoice** (`3.x` or `4.x`) or **AmyrAhmady/sampvoice** to **SampVoice Plus**.

---

## 1. For Players (`sampvoice.asi`)

### Automatic Backup & Migration via Installer
Running **`SampVoice-Plus-Setup.exe`** detects legacy SampVoice installations (`sampvoice.asi`, `scripts\sampvoice*.asi`, `sampvoice\`, `bass_fx.dll`) and defaults to **Backup (`backup`)**:
* Exclusive legacy files are copied into `<GTA>\sampvoice-plus\backup\` with a SHA-256 manifest, while shared runtime files (`bass.dll`, `vorbisFile.dll`, `vorbisHooked.dll`) are preserved.
* If the legacy installer registered an uninstaller (`{01EA93A6-5255-44FE-ADB9-FDC3EFE89675}_is1`), setup runs it silently so it cannot remove **SampVoice Plus** later.
* Uninstalling **SampVoice Plus** restores your backed-up legacy files cleanly.

### Manual Migration Checklist
1. Remove old `sampvoice.asi`, `scripts\sampvoice*.asi`, `bass_fx.dll`, and any loose `sampvoice\` font/PNG asset folders from your GTA San Andreas directory (**keep** `bass.dll`, `vorbisFile.dll`, `vorbisHooked.dll`, and your ASI Loader such as `dinput8.dll`).
2. Copy the single **`sampvoice.asi`** file directly into the GTA San Andreas root directory (next to `gta_sa.exe`).
3. Verify the file hash if needed: run `certutil -hashfile sampvoice.asi SHA256` and compare it with the GitHub Release notes (and line 1 of `Documents\GTA San Andreas User Files\SAMP\sampvoice\sampvoice_plus_v<version>_log.txt`).

### Settings, Blacklist & Log Isolation
* **New Configuration Folder:** SampVoice Plus stores its configuration (`sampvoice.ini`, including `[blacklist]`) and logs (`sampvoice_plus_v<version>_log.txt`) inside `Documents\GTA San Andreas User Files\SAMP\sampvoice\`.
* **Legacy Settings Untouched:** Your old `Documents\GTA San Andreas User Files\sampvoice\` folder is never modified or deleted automatically (unless you explicitly choose uninstall cleanup in `Setup.exe` / `/LEGACYCONFIG=remove`).
* **Unified Scoped Blacklist (`[blacklist]`):** Blacklisted players are saved directly inside `sampvoice.ini` under `[blacklist]` (`<player> | <server name> | <IP:port> | <server|all>`), letting you mute either on the origin server (`server`) or across all servers (`all`) while preserving the origin server record and inspecting or editing the full list even when disconnected. Any unrelated custom or legacy lines under `[blacklist]` are warned cleanly in the log and preserved without silent data loss.
* **Zero Singleplayer Footprint:** When starting singleplayer GTA:SA (no `samp.dll` or valid `samp.asi` detected within 10 seconds), `sampvoice.asi` exits immediately without creating files, folders, hooks, or audio streams.

---

## 2. For Server Owners & Pawn Scripters

### Drop-In Replacement (`plugins/` or `components/`)
* **open.mp:** Replace the old component with `components/sampvoice.dll` (Windows) or `components/sampvoice.so` (Linux x86).
* **SA-MP `0.3.7-R2` / `R3` / `0.3.DL-R1`:** Replace `plugins/sampvoice.dll` / `.so` and update `pawno/include/sampvoice.inc`.
* **External Relay Removal (`CyberMor 4.x`):** SampVoice Plus integrates the voice relay inside the plugin/component — shut down the separate `voice` binary and open one UDP voice port (`sampvoice.port`).

### Client Compatibility (`v11` + `v12`)
* **Supported Simultaneously:** `CyberMor 3.x`, `AmyrAhmady`, Android 3.x launchers (`v11`), and `SampVoice Plus` (`v12`) clients connect side-by-side without script changes.
* **Rejected Clients:** `CyberMor 4.x` (`0xDEADC0DE`) clients use an incompatible custom protocol (`v4`) and must upgrade to **SampVoice Plus** (`sampvoice.asi`).
* **Android Activation Key:** Embedded Android 3.x clients always transmit key `0x42` (`B`) rather than following `SvAddKey`; ensure scripts bind `0x42` alongside desktop keys if mobile players connect.

### Stream Tag Colors (`sampvoice.legacy_color_format`)
Legacy **CyberMor 3.x** and **AmyrAhmady** servers sent Pawn color integers raw over the network, and clients read them as **`0xAARRGGBB`** (which is why `"Local"` with `0xff0000ff` in AmyrAhmady's README appeared blue instead of red). Therefore, any existing gamemode whose stream tags already displayed with their intended colors on legacy servers is passing `0xAARRGGBB` values:
* **Compatibility Mode (`sampvoice.legacy_color_format = true`):** Set `"legacy_color_format": true` (`config.json`) or `sampvoice.legacy_color_format 1` (`server.cfg`) to keep legacy **`0xAARRGGBB`** colors untouched (recommended when migrating an existing gamemode without editing Pawn color constants).
* **Default (`sampvoice.legacy_color_format = false`):** Expects standard SA-MP Pawn **`0xRRGGBBAA`** colors (for example, red = `0xFF0000FF` instead of legacy ARGB `0xFFFF0000`) and converts them to wire `0xAARRGGBB` automatically (preserving `0` = no stream tag, and converting `0x000000FF` into opaque black `0xFF000000`).

### Script Enhancements
* **`YSI_Coding\y_hooks` Compatibility:** `SV_VOID:` expands to empty cleanly while runtime `amx_FindPublic` handles callbacks across filterscripts and gamemodes.
* **Automatic Cleanup:** Streams and effects are tracked per `AMX*` and reclaimed cleanly on GMX / filterscript unload.
* **Optional Plus Features:** Check `SvGetClientProtocol(playerid) == 12` to unlock `SvStreamSetIcon` (7 built-in icons), `SvEnableListener` / `SvDisableListener`, `Voice RTT`, and `OnPlayerMicroStateChange(playerid, bool:hasMicro)`.
