# SampVoice Plus - Documentation Hub

Welcome to the technical documentation for **SampVoice Plus** (SA-MP & open.mp, 32-bit x86). This hub covers installation, universal client architecture, protocol design, spatial audio streams, DX8 DSP effects, zero-install feasibility analysis, and the complete Pawn scripting API reference.

---

## Documentation Sections

| Section | Description |
| :--- | :--- |
| **[📖 Getting Started](getting-started.md)** | Single-file `sampvoice.asi` installation, SA-MP `server.cfg` & open.mp `config.json` setup, and drop-in migration from CyberMor / AmyrAhmady. |
| **[🎧 Universal Client & In-Game UI](client-and-ui.md)** | Single-file `.rc`-embedded `sampvoice.asi` internals, verified PE detection across R1–R5/0.3.DL, F11 configurable hotkey, 15 live languages, and `sampvoice.ini`. |
| **[⚙️ Architecture & Wire Protocol](architecture-and-protocol.md)** | Protocol `v11`/`v12` interoperability, CRC32C UDP validation, generational 32-bit handles, lock-free queues, COW snapshots, and $O(1)$ spatial hash grid. |
| **[🔊 Audio Streams & DX8 Effects](audio-streams-and-effects.md)** | Global, static local, and dynamic local streams (`GStream`, `SLStream`, `DLStream`), `SvStreamSetIcon`, parameter sliding, and all 9 DX8 DSP audio effects. |
| **[📚 Complete API Reference](api-reference.md)** | Exhaustive reference for every Pawn native function, callback, parameter enum, handle tag, and constant in `sampvoice.inc`. |
| **[🛠️ Troubleshooting Guide](troubleshooting.md)** | F11 9-state diagnostics, UDP firewall/bind setup, hot-plug mic recovery, talk modes, hotkey conflicts, and plugin coexistence. |
| **[🔄 Migrating from Legacy SampVoice](migrating-from-sampvoice.md)** | Upgrading players (`sampvoice.asi` backup & scoped `[blacklist]`) and servers (`CyberMor` 3.x/4.x & `AmyrAhmady`, `legacy_color_format`). |
| **[🔍 Zero-Install Feasibility Analysis](zero-install-analysis.md)** | Technical analysis of why zero-install voice chat is not natively possible inside vanilla SA-MP / open.mp without external companion apps. |

---

## Compatibility & System Requirements

* **Client (`sampvoice.asi` / `SampVoice-Plus-Setup.exe` — Windows 7 SP1 or newer, 32-bit x86):**
  * **Game Editions:** SA-MP **0.3.7-R1**, **0.3.7-R2**, **0.3.7-R3 / R3-1**, **0.3.7-R4**, **0.3.7-R4-2**, **0.3.7-R5-1** (incl. **R5-2-MP**), **0.3.DL-R1** (incl. **DL-R1-2-MP**), and **[open.mp launcher](https://open.mp/downloads/launcher)** connections (official SA-MP installers also at [sa-mp.mp/downloads](https://www.sa-mp.mp/downloads/)).
  * **Runtime Requirements:** Standalone `sampvoice.asi` (or optional Inno Setup installer bundling Ultimate ASI Loader `dinput8.dll`). Static `/MT` MSVC runtime, statically linked `libopus`, `.rc` `RCDATA` embedded font/PNG sprites, and direct binding to GTA/SA-MP's `bass.dll`. Only standard Windows OS DLLs (`KERNEL32`, `USER32`, `SHELL32`, `IMM32`, `WS2_32`, `bcrypt`, `VERSION`, `d3d9`) are imported.
  * **Backward Compatibility:** Connects seamlessly to legacy **v11** (`CyberMor` v3.x / `AmyrAhmady`) servers as well as **SampVoice Plus** (`v12` capable) servers.
* **Server (`sampvoice.dll` / `sampvoice.so` — Windows & Linux 32-bit x86):**
  * **open.mp:** Native component (`components/sampvoice.dll` or `.so`).
  * **SA-MP `0.3.7` & `0.3.DL-R1`:** Legacy server plugin (`plugins/sampvoice.dll` or `.so`, supporting SA-MP `0.3.7-R2` (R2-1, R2-2-1), `0.3.7-R3`, and `0.3.DL-R1` on both Windows and Linux). Coexists with `Pawn.RakNet` and `YSF` in either load order.
  * **Cross-Version Interoperability:** Simultaneously accepts legacy `CyberMor` v3.x, `AmyrAhmady`, and Android 3.x launcher (`v11`) clients alongside `SampVoice Plus` (`v12`) clients. **CyberMor `4.x`** (`0xDEADC0DE`) clients are cleanly logged and ignored.
