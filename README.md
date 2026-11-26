# M1 Tank Platoon II — Native Modernization & Modding Runtime (`m1tp2-native`)

A **100% native SDL3 + OpenGL 3.3 Core** modernization, HD renderer, True Hor+ widescreen patch, and modding framework for MicroProse's **M1 Tank Platoon II (1998)** on modern Windows (Windows 10 / 11).

**Zero third-party compatibility wrappers required** — no DxWnd, no DDrawCompat, no dgVoodoo2.

> **Note:** This repository contains **no copyrighted game assets or original executables**. You must own a legal copy of *M1 Tank Platoon II* (e.g., from GOG.com or retail CD v1.2).

---

## Features

- **1-Click Native Launch on Modern Windows**:
  - Replaces the legacy DirectDraw 2D surface/palette/window/input pipeline natively via **SDL3 + OpenGL 3.3**.
  - `m1tp2_native.exe` automatically bypasses Windows 11's broken `sysmain.sdb` `EmulateHeap` application compatibility shim (`0xC0000142`) via an NTFS hardlink (`M1TP2_bin.exe`).
  - Replaces M1TP2's 4096-entry tracked heap allocator and fixes its Glide-init double-free crash (`0x423263`).
- **Native HD OpenGL 3.3 Glide Renderer**:
  - Implements all 32 Glide 2.43 functions natively in OpenGL 3.3 at your **native monitor resolution** (1080p / 1440p / 4K).
  - Hardware **MSAA** (up to 8x), **Anisotropic Filtering** (up to 16x), **Trilinear Mipmaps** with neighbor-RGB chromakey edge bleeding, configurable fog distance (`1x / 2x / 4x / Off`), and live gamma control.
  - Automatically redirects Direct3D selection in `game.cfg` into Native Glide HD.
- **True Hor+ Widescreen 3D + Smart Per-View Auto-Aspect**:
  - Hooks M1TP2's 3D projection (`0x459A5D`) and frustum classifier (`0x4599ED`) for **true Hor+ widescreen** (+33% real horizontal FOV at 16:9, zero stretching).
  - **Smart Auto-Aspect** (default):
    - **TC Outhatch `.50 cal` (`F7`)**, **Chase View (`F8`)**, and **External Views** render full-screen edge-to-edge in True Hor+ widescreen while keeping the `.50 cal` grips, cursor, and HUD text un-stretched in a centered 4:3 safe zone.
    - **Interior Cockpits (`F1`/`F2`)**, **IVIS Map (`F3`)**, **GPS Sight (`F4`)**, **Circular GAS Tube (`F5`)**, **Vision Blocks (`F6`)**, and **2D Menus/Briefings** stay crisp **4:3**.
- **In-Game Settings Overlay (`F12` or `Insert`)**:
  - Live toggle for Fullscreen/Windowed, 2D & 3D Aspect Modes, VSync, MSAA, Anisotropy, Texture/HUD Filters, Fog Distance, Gamma, Skip Intro Videos, Mods VFS, and FPS/Render Stats HUD. Auto-saves to `m1tp2_recomp.ini`.
- **Native Modding VFS (`mods/`) & Asset Toolkit (`tools/m1tp2_modtool.py`)**:
  - Hooks `CatalogLookup` (`0x427120`) and `crt_fopen` (`0x45C560`) so loose files in `mods/<mod_name>/` override **any asset** in `Game\m1.cat`, `Game\3dz.cat`, or loose `BATTLES\`, `World\`, `Cockpits\`, `Campaign\`, and `SOUND\` folders — with **no 255-entry `over.txt` limit** and no edits to stock archives.
  - Included Python 3 CLI (`tools/m1tp2_modtool.py`) unpacks/repacks `.cat` archives (automatically recovering **94.8%** of `3dz.cat` and **90.6%** of `m1.cat` filenames), exports `.3dz` 3D tank models with UVs to Blender-ready `.obj`/`.mtl`, and dumps embedded unit/ordnance tables.

---

## Quick Install (End Users)

1. Install your owned copy of **M1 Tank Platoon II** (GOG.com or retail v1.2).
2. Download **`M1TP2-Native-Setup-v0.1.0.exe`** from the [Releases](https://github.com/ajalberd/m1tp2-native/releases) page.
3. Run the installer — it automatically detects your M1TP2 folder, backs up any old `ddraw.dll` / DDrawCompat files to `_m1tp2_backup\`, installs the native runtime, configures Glide HD mode, and creates a Desktop shortcut.
4. Launch via **`m1tp2_native.exe`** (or the new Desktop shortcut). Press **`F12`** in-game anytime to tune graphics, aspect ratio, MSAA, AF, fog, and intro skipping!

> **Portable / Manual Install:** Download `M1TP2-Native-v0.1.0-portable.zip`, back up any existing `ddraw.dll` (e.g. GOG's DDrawCompat), extract the ZIP into your M1TP2 root folder (next to `M1TP2.exe`), and run `m1tp2_native.exe`.

> **Silent CLI Install / Uninstall:**
> ```powershell
> .\M1TP2-Native-Setup-v0.1.0.exe /S /D="C:\GOG Games\M1 Tank Platoon II"
> .\M1TP2-Native-Setup-v0.1.0.exe /U /D="C:\GOG Games\M1 Tank Platoon II"
> ```

---

## Modding Quick Start

Drop modified files into `mods\<your_mod>\` inside your M1TP2 folder:
- Archive overrides (`m1.cat` / `3dz.cat`): `mods\my_mod\3dz\...`, `mods\my_mod\pics\...`, `mods\my_mod\vpics\...`, `mods\my_mod\cockpits\...`
- Disk overrides: `mods\my_mod\BATTLES\...`, `mods\my_mod\World\...`, `mods\my_mod\Cockpits\cockpit.dat`, `mods\my_mod\Game\battle.txt`
- Optional `mods\loadorder.txt` controls load priority (one folder name per line; later overrides earlier).

Use `tools/m1tp2_modtool.py` (Python 3, stdlib only) to inspect/unpack/export game assets:
```powershell
python tools\m1tp2_modtool.py cat-list m1.cat --filter m1a2
python tools\m1tp2_modtool.py cat-unpack m1.cat .\unpacked --filter m1a2
python tools\m1tp2_modtool.py 3dz-to-obj bmp2_h.3dz .\bmp2_h.obj
python tools\m1tp2_modtool.py new-mod my_first_mod
```

---

## Building from Source

### Prerequisites
- Windows (x64 or x86)
- [Zig 0.14.x](https://ziglang.org/download/) (`zig` in `PATH`, or pass `-ZigExe`)
- PowerShell 5.1+ (built into Windows)
- Python 3 (only needed when running `package_release.ps1` to bundle the 1-shot installer)

### Build Commands
```powershell
# Auto-downloads SDL3 3.4.20 MinGW dev package on first build and compiles all binaries into .\build\
.\build.ps1

# Optional: build AND stage directly into your M1TP2 install folder
.\build.ps1 -GameDir "C:\GOG Games\M1 Tank Platoon II"

# Build the standalone 1-shot GUI installer + portable ZIP into .\dist\
.\package_release.ps1 -Version "0.1.0"
```

---

## Architecture

| File | Role |
|---|---|
| `src/m1tp2_launcher.c` | Creates/launches `M1TP2_bin.exe` NTFS hardlink to bypass Windows 11 `EmulateHeap` shim |
| `src/ddraw_shim.c` | Zero-CRT `ddraw.dll` bootstrap; hooks `WinMain` (`0x4116A0`) and loads `m1tp2_recomp.dll` safely outside loader lock |
| `src/core_main.cpp` | Runtime bootstrap, thread-safe logger, INI config, vectored crash handler |
| `src/heap_guard.cpp` | Unlimited tracked heap allocator + fix for Glide-init double-free (`0x423263`) |
| `src/vfs_mod.cpp` | Native `mods/` VFS hooking `CatalogLookup` (`0x427120`) and `crt_fopen` (`0x45C560`) |
| `src/video_sdl.cpp` | SDL3 + OpenGL 3.3 replacement for DirectDraw 2D layer, windowing, keyboard/mouse input, and SkipIntro |
| `src/glide_gl.cpp` | Native OpenGL 3.3 Glide 2.43 renderer + True Hor+ 3D projection (`0x459A5D`) & frustum (`0x4599ED`) hooks |
| `src/overlay_ui.cpp` | Zero-dependency OpenGL 3.3 `F12` settings overlay & FPS/render stats HUD |
| `src/installer_gui.c` | Self-extracting native Win32 GUI + CLI 1-shot installer & uninstaller |
| `tools/m1tp2_modtool.py` | `.cat` unpacker/repacker, `.3dz` → `.obj`/`.mtl` exporter, unit/ordnance table dumper |

---

## License & Acknowledgments

- `m1tp2-native` source code is released under the **MIT License** (see [LICENSE](LICENSE)).
- Uses [SDL3](https://libsdl.org/) under the zlib License (see `THIRD_PARTY_LICENSES.txt`).
- *M1 Tank Platoon II* is © MicroProse / Atari; no game assets are distributed here.
