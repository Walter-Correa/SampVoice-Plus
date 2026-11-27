# SampVoice Plus — Documentation Hub

Welcome to the documentation for **SampVoice Plus** (SA-MP & open.mp, 32-bit x86). Choose the section that fits what you want to do.

---

## Start Here by Role

### For Players
* **Start here:** **[📖 Getting Started (Client Install)](getting-started.md#1-client-installation)** — install `sampvoice.asi` in one click (`SampVoice-Plus-Setup.exe`) or drop the single file into your GTA San Andreas folder.
* **[🎧 Client & In-Game Settings (`F11`)](client-and-ui.md)** — every tab in the `F11` menu, microphone/audio/HUD controls, player mute list, 15 languages, and `sampvoice.ini` keys.
* **[🔄 Upgrading from Legacy SampVoice](migrating-from-sampvoice.md#1-for-players-sampvoiceasi)** — backup/restore installer behavior and replacing old `sampvoice` files cleanly.

### For Server Owners & Scripters
* **Start here:** **[📖 Getting Started (Server Setup)](getting-started.md#2-server-installation--configuration)** — place the plugin/component, set `server.cfg` or `config.json`, and load the included **`FS_SampVoicePlus_Demo`** filterscript.
* **[🔄 Migrating from Legacy SampVoice](migrating-from-sampvoice.md#2-for-server-owners--pawn-scripters)** — drop-in upgrade from CyberMor 3.x / AmyrAhmady, stream color order (`legacy_color_format`), adapting CyberMor 4.x scripts, and unlocking 1.1–1.3 features step by step.
* **[🔊 Audio Streams & DX8 Effects Guide](audio-streams-and-effects.md)** — Global, SL, and DL streams, parameters, all 9 DX8 effects, live icons/metadata, world filters, standard vs dynamic objects, and ready-to-use recipes.
* **[📚 Complete API Reference](api-reference.md)** — all **107** Pawn natives, **8** callbacks, parameter enums, handle tags, and stream type constants (`Since` version and client requirements included).

### For Launcher Developers
* **[📱 Android Launcher Integration Guide](android-integration.md)** — step-by-step C integration for `SampVoice-Plus-Android-SDK.zip` (`svplus_mobile.h`), BASS initialization rules, `svp_frame()` threading model, keys/HUD APIs, and Android limits.

### Troubleshooting & Reference
* **[🛠️ Troubleshooting Guide](troubleshooting.md)** — `F11` status diagnostics, UDP port/firewall checks, mic recovery, Android `B` key behavior, wall occlusion, and log warnings.
* **[⚙️ Under the Hood (Advanced)](under-the-hood.md)** — technical deep dive for contributors and auditors: protocol internals, anti-flood limits, spatial formulas, SA-MP PE build table, memory hooks, and comparison notes.
* **[🔍 Zero-Install Feasibility Analysis](zero-install-analysis.md)** *(background reading)* — why vanilla SA-MP / open.mp cannot run in-game voice natively without a client `.asi`.

---

## Quick Compatibility Overview

For the full version, OS, and Android matrix, see **[Supported Platforms](../README.md#supported-platforms)** in the README.

* **Client (`sampvoice.asi`, Windows 32-bit x86):** Supports SA-MP **0.3.7-R1 through R5-2-MP**, **0.3.DL-R1**, and the **open.mp launcher** in one standalone file, connecting to both legacy `v11` and SampVoice Plus servers.
* **Server (`sampvoice.dll` / `.so`, Windows & Linux 32-bit x86):** Ships as an **open.mp** native component (`components/`) and **SA-MP `0.3.7-R2` / `R3` / `0.3.DL-R1`** plugin (`plugins/`), accepting legacy `v11` (CyberMor 3.x, AmyrAhmady, Android 3.x launchers) alongside SampVoice Plus clients. CyberMor 4.x clients must upgrade to SampVoice Plus.
