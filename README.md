# NotchGlass — Native Windows 10/11 C++20 Slim-Line Liquid Glass Utility

A pure **C++20**, **Win32**, **Direct2D / DirectWrite**, **DirectComposition**, and **Windows.UI.Composition** desktop overlay engineered for **12–20 MB idle RAM** (< 50 MB peak) and **0% idle CPU**.

## Build Instructions (Windows 10/11 + MSVC 2022 + Windows SDK 10.0.22621+)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\scripts\verify_memory.ps1 -ExecutablePath .\build\Release\NotchGlass.exe
```

## Architecture Highlights
- **0% Idle CPU**: Standard `GetMessageW` blocking loop; invisible `200×16px` top-edge proximity hit-test window using `WM_MOUSEMOVE` + `TrackMouseEvent(TME_LEAVE)` instead of global `WH_MOUSE_LL` hooks.
- **Liquid Glass Shader (`shaders/LiquidGlassPS.hlsl`)**: Custom `ID2D1EffectImpl` performing edge refraction displacement, saturation vibrancy boost, luminance-adaptive opacity, and `1px` specular rim lighting over `CompositionBackdropBrush`.
- **Event-Driven System Modules**:
  - `MediaSmtcModule`: WinRT SMTC events + `96×96` WIC thumbnail downsampling + `.lrc` synced lyrics drawer.
  - `MicAudioModule`: `Ctrl + Alt + M` global mic mute (`IAudioEndpointVolume::SetMute`) + `IAudioSessionNotification` call timer + system flyouts.
  - `FileShelfModule`: OLE `IDropTarget` & `DoDragDrop` temporary file tray.
  - `QrWifiTransferModule`: Pure C++20 Version 3 Reed-Solomon QR matrix rendered as Direct2D vector geometry + ephemeral Winsock2 HTTP server.
  - `ClipboardModule`: `AddClipboardFormatListener` (`WM_CLIPBOARDUPDATE`) + WIC JPEG compression inside a strict `10 MB` ring-buffer cap.
  - `QuickToolsModule`: Pomodoro, reminders, DXGI Desktop Duplication snip, and 1 Hz PDH `Slim Game Strip` gated strictly behind `SHQueryUserNotificationState`.
