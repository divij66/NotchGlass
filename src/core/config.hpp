#pragma once

#include <cstddef>
#include <cstdint>
#include <d2d1_3.h>

namespace notch::core {

enum class GlassPreset : std::uint8_t {
    LiquidGlass = 0,
    CandyGlass  = 1,
    Obsidian    = 2,
    Graphite    = 3
};

struct GlassThemeColors {
    D2D1_COLOR_F tint;
    float saturation;
    float refractionPx;
    float blurSigma;
};

inline constexpr GlassThemeColors GetThemePreset(GlassPreset preset) noexcept {
    switch (preset) {
        case GlassPreset::CandyGlass:
            return {{0.94f, 0.82f, 0.98f, 0.16f}, 1.72f, 11.5f, 24.0f};
        case GlassPreset::Obsidian:
            return {{0.05f, 0.06f, 0.09f, 0.62f}, 1.35f, 7.5f, 22.0f};
        case GlassPreset::Graphite:
            return {{0.16f, 0.19f, 0.24f, 0.44f}, 1.42f, 8.5f, 22.0f};
        case GlassPreset::LiquidGlass:
        default:
            return {{0.96f, 0.98f, 1.00f, 0.12f}, 1.78f, 12.0f, 26.0f};
    }
}

struct GeometryConfig {
    // Collapsed Refined Slim Pill dimensions (smaller horizontally, thicker & glassier)
    static constexpr float kCollapsedWidthDip  = 104.0f;
    static constexpr float kCollapsedHeightDip = 8.0f;
    static constexpr float kCollapsedRadiusDip = 6.0f;

    // Dynamic-Island-style Live Activity Pill (when Pomodoro or Stopwatch is running)
    static constexpr float kLivePillWidthDip   = 176.0f;
    static constexpr float kLivePillHeightDip  = 26.0f;
    static constexpr float kLivePillRadiusDip  = 13.0f;

    // Proximity Hit-Test window at top-center (DIPs)
    static constexpr float kProximityWidthDip  = 196.0f;
    static constexpr float kProximityHeightDip = 28.0f;

    // Expanded Hub dimensions (DIPs)
    static constexpr float kExpandedWidthDip   = 480.0f;
    static constexpr float kExpandedHeightDip  = 312.0f;
    static constexpr float kExpandedRadiusDip  = 26.0f;
    static constexpr float kCollapseGraceDip   = 12.0f;

    // Fullscreen Slim Game Strip dimensions (DIPs)
    static constexpr float kGameStripWidthDip  = 330.0f;
    static constexpr float kGameStripHeightDip = 22.0f;

    // Interaction Timers (ms)
    static constexpr UINT kHoverExpandDelayMs  = 120;
    static constexpr UINT kLeaveCollapseMs     = 45;
    static constexpr UINT kEventDebounceMs     = 75;
    static constexpr UINT kFlyoutAutoHideMs    = 2200;
    static constexpr UINT kGameTelemetryMs     = 1000; // Strict 1 Hz cap only during fullscreen games
    static constexpr UINT kWifiIdleTimeoutMs   = 300000; // 5 minutes auto-teardown

    // Memory Caps
    static constexpr std::size_t kClipboardMaxBytes = 10u * 1024u * 1024u; // 10 MB ring buffer
    static constexpr UINT kAlbumArtTargetPx         = 96;
};

// Custom Window Messages (event-driven callbacks marshal back to UI thread)
enum AppMessages : UINT {
    WM_APP_MEDIA_UPDATED      = WM_APP + 1,
    WM_APP_AUDIO_UPDATED      = WM_APP + 2,
    WM_APP_MIC_SESSION_UPDATE = WM_APP + 3,
    WM_APP_BRIGHTNESS_UPDATED = WM_APP + 4,
    WM_APP_WIFI_TRANSFER_DONE = WM_APP + 5,
    WM_APP_REMINDER_TRIGGERED = WM_APP + 6,
    WM_APP_DROPPED_FILES      = WM_APP + 7,
    WM_APP_NET_IF_CHANGED     = WM_APP + 8,
    WM_APP_TRAY_ICON          = WM_APP + 20,
    WM_APP_TOGGLE_NOTCH       = WM_APP + 21
};

enum TimerIds : UINT_PTR {
    TIMER_HOVER_EXPAND    = 101,
    TIMER_LEAVE_COLLAPSE  = 102,
    TIMER_DEBOUNCE_REDRAW = 103,
    TIMER_FLYOUT_HIDE     = 104,
    TIMER_GAME_TELEMETRY  = 105,
    TIMER_CALL_ELAPSED    = 106,
    TIMER_WIFI_TIMEOUT    = 107,
    TIMER_POMODORO_TICK   = 108,
    TIMER_FULLSCREEN_CHK  = 109,
    TIMER_SPRING_ANIM     = 110,
    TIMER_REC_TICK        = 111
};

enum HotkeyIds : int {
    HOTKEY_GLOBAL_MIC_MUTE = 1,
    HOTKEY_REGION_SNIP     = 2,
    HOTKEY_TOGGLE_NOTCH    = 3
};

constexpr UINT IDI_NOTCHGLASS = 101;

} // namespace notch::core
