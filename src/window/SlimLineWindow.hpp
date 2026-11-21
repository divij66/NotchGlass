#pragma once

#include <windows.h>
#include <memory>

#include "core/config.hpp"
#include "core/raii.hpp"
#include "graphics/LiquidGlassEngine.hpp"
#include "modules/MediaSmtcModule.hpp"
#include "modules/MicAudioModule.hpp"
#include "modules/FileShelfModule.hpp"
#include "modules/QrWifiTransferModule.hpp"
#include "modules/ClipboardModule.hpp"
#include "modules/QuickToolsModule.hpp"
#include "modules/FocusModule.hpp"

namespace notch::window {

enum class PillState : std::uint8_t {
    CollapsedIdle = 0,
    Expanding     = 1,
    ExpandedHub   = 2,
    Collapsing    = 3,
    TransientPill = 4, // Volume / Brightness / Mic / Reminder pop-out
    GameStrip     = 5  // Click-through Slim Game Strip over fullscreen apps
};

enum class ActiveTab : std::uint8_t {
    Home      = 0,
    Audio     = 1,
    Focus     = 2,
    Rec       = 3,
    Media     = 4,
    ShelfQr   = 5,
    Clipboard = 6,
    Settings  = 7
};

enum class DragTarget : std::uint8_t {
    None           = 0,
    HomeBrightness = 1,
    HomeVolume     = 2,
    AudioModule    = 3
};

class SlimLineWindow {
public:
    explicit SlimLineWindow(HINSTANCE hInstance);
    ~SlimLineWindow();

    bool Create();
    HWND GetHwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK HitTestWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    LRESULT HandleMainMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleHitTestMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void RefreshMonitorMetrics();
    void ApplyWindowBounds(float widthDip, float heightDip);
    void ApplyCollapsedIdleBounds();
    bool HasAnyLivePill() const noexcept;
    void SetClickThrough(bool transparent);
    void SetNoActivate(bool noActivate);
    void BeginTransition(PillState targetState);
    void StepSpringAnimation();
    void CheckFullscreenGameState();
    void RenderCurrentState();
    void RenderHomeOverview(ID2D1RenderTarget* dc, IDWriteFactory* dw, D2D1_RECT_F bounds);
    void RenderSettingsPage(ID2D1RenderTarget* dc, IDWriteFactory* dw, D2D1_RECT_F bounds);
    void HandleHubClick(float xDip, float yDip);
    void HandleHubDrag(float xDip, float yDip);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayContextMenu();

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND hitTestHwnd_ = nullptr;

    RECT primaryMonitorRect_{0, 0, 1920, 1080};
    float dpiScale_ = 1.0f;
    int windowXPx_ = 0;
    int windowYPx_ = 0;

    PillState state_ = PillState::CollapsedIdle;
    ActiveTab activeTab_ = ActiveTab::Home;
    DragTarget dragTarget_ = DragTarget::None;
    bool pinned_ = false;
    bool cursorInProximity_ = false;
    bool trackingLeave_ = false;
    bool trayAdded_ = false;
    bool modalDialogOpen_ = false;
    bool startupShowcaseActive_ = false;

    // Configurable Settings & Home Quick Toggles
    UINT hoverDelayMs_ = core::GeometryConfig::kHoverExpandDelayMs;
    UINT leaveCollapseMs_ = core::GeometryConfig::kLeaveCollapseMs;
    bool gameStripEnabled_ = true;
    bool wifiToggle_ = true;
    bool btToggle_ = true;
    bool nightLightToggle_ = false;
    bool highPerfToggle_ = false;

    // Spring animation state (active ONLY during transitions, zero timers at idle)
    float animProgress_ = 0.0f;
    float animVelocity_ = 0.0f;
    float animTarget_ = 0.0f;
    float currentWidthDip_ = core::GeometryConfig::kCollapsedWidthDip;
    float currentHeightDip_ = core::GeometryConfig::kCollapsedHeightDip;
    float currentRadiusDip_ = core::GeometryConfig::kCollapsedRadiusDip;

    graphics::LiquidGlassEngine glassEngine_;
    core::FrameArena<16384> frameArena_;

    // Modular features (lazy-initialized on demand)
    modules::MediaSmtcModule mediaModule_;
    modules::MicAudioModule micAudioModule_;
    modules::FileShelfModule fileShelfModule_;
    modules::QrWifiTransferModule qrWifiModule_;
    modules::ClipboardModule clipboardModule_;
    modules::QuickToolsModule quickToolsModule_;
    modules::FocusModule focusModule_;
};

} // namespace notch::window
