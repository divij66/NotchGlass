# NotchGlass — Liquid Glass Dynamic Island Utility for Windows 10/11

A lightweight, pure **C++20 / Win32 / Direct2D / DirectComposition / Windows.UI.Composition** top-edge overlay built for **0% idle CPU** and **12–20 MB idle RAM**. Hover the compact top bar (or press `Ctrl + Alt + N`) to reveal media controls, per-app volume, per-display brightness, Pomodoro/Stopwatch + To-Do progress charts, morning motivation notes, a built-in MP4 screen recorder, clipboard history, and local Wi-Fi QR file sharing.

## Features
- **Apple-Style Liquid Glass UI**: Custom HLSL pixel shader (`shaders/LiquidGlassPS.hlsl`) with optical edge refraction, saturation vibrancy boost, adaptive luminance tint, and 4 presets (`Liquid Glass`, `Candy Glass`, `Obsidian`, `Graphite`).
- **Dynamic Island Live Pill**: Automatically shrinks into a compact floating pill (`Focus MM:SS`, `Watch MM:SS`, or red `REC MM:SS`) when collapsed while active timers or recordings are running.
- **Per-App Volume & Individual Display Brightness**: Control master output, individual app sessions (`ISimpleAudioVolume`), output/mic devices, and each connected monitor (`WMI` / `DDC/CI`) individually or synced.
- **Focus Mode, Work Chart & Morning Motivation Note**:
  - Built-in Pomodoro timer (`15m`, `25m`, `50m`, `5m` break) and Stopwatch with laps.
  - Persistent To-Do list (`%LOCALAPPDATA%\NotchGlass\todos.txt`) synced with a **Completed vs. Pending Work Donut Chart**.
  - Editable **Morning Motivation Note** saved locally (`%LOCALAPPDATA%\NotchGlass\daily_note.txt`).
- **Built-in H.264 MP4 Screen Recorder + Region Snip**: Configure **FPS (`24` / `30` / `60`)** and **Resolution (`Native` / `1080p` / `720p`)** before recording & exporting `.mp4` clips to `Videos\NotchGlass\`.
- **Wi-Fi QR File Transfer**: Drop files onto the notch and scan an ISO-18004 QR code (Versions 2–4, GF(256) Reed-Solomon) to download directly over local Wi-Fi via an ephemeral Winsock2 HTTP server.
- **Media (SMTC) + Synced `.lrc` Lyrics & Clipboard Ring Buffer**: Event-driven playback controls and `10 MB`-capped clipboard history (`WM_CLIPBOARDUPDATE`).

## Shortcuts & Controls
| Action | Control |
| --- | --- |
| Expand / Collapse Hub | Hover top-center bar or press **`Ctrl + Alt + N`** |
| Global Mic Mute Toggle | **`Ctrl + Alt + M`** |
| Drag-and-Drop Wi-Fi Share | Drag any file onto the top bar → **Send to Phone** |
| Tray Menu | Right-click the NotchGlass system tray icon |

## Quick Install on Windows 10/11
1. Download the repository ZIP (or download `NotchGlass.exe` from **Actions → Artifacts**).
2. Extract and double-click **`install.bat`** *(requires Visual Studio 2022 Build Tools with Desktop C++ & Windows 10/11 SDK when building from source)*.
3. Launch **NotchGlass** from the Start Menu or Desktop shortcut.

## Manual Build (PowerShell)
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\scripts\verify_memory.ps1 -ExecutablePath .\build\Release\NotchGlass.exe
```

## GitHub Actions Auto-Build
Pushing to the `main` branch automatically builds `NotchGlass.exe` using `.github/workflows/build-windows.yml` and uploads it as a downloadable workflow artifact (`NotchGlass-Windows-x64`).

## License
Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for details.
