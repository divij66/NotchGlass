#include "window/SlimLineWindow.hpp"

#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace notch::window {

namespace {
constexpr wchar_t kMainClassName[]    = L"NotchGlass_MainOverlay";
constexpr wchar_t kHitTestClassName[] = L"NotchGlass_TopEdgeHitTest";

struct TabSpan {
    const wchar_t* label;
    float left;
    float right;
};

// 8 crisp single-word tabs + Pin + Glass buttons across 480px width
constexpr TabSpan kTabSpans[8] = {
    {L"Home",  12.0f,  58.0f},
    {L"Audio", 61.0f, 107.0f},
    {L"Focus",110.0f, 157.0f},
    {L"Rec",  160.0f, 198.0f},
    {L"Music",201.0f, 248.0f},
    {L"Shelf",251.0f, 296.0f},
    {L"Clips",299.0f, 344.0f},
    {L"Prefs",347.0f, 392.0f}
};

void DrawMusicNoteIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 4.0f, cy + 4.5f), 2.8f, 2.2f), brush);
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 4.0f, cy + 3.0f), 2.8f, 2.2f), brush);
    dc->DrawLine(D2D1::Point2F(cx - 1.5f, cy + 4.0f), D2D1::Point2F(cx - 1.5f, cy - 6.0f), brush, 1.6f);
    dc->DrawLine(D2D1::Point2F(cx + 6.5f, cy + 2.5f), D2D1::Point2F(cx + 6.5f, cy - 7.5f), brush, 1.6f);
    dc->DrawLine(D2D1::Point2F(cx - 1.5f, cy - 6.0f), D2D1::Point2F(cx + 6.5f, cy - 7.5f), brush, 2.2f);
}

void DrawSunIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 3.0f, 3.0f), brush, 1.4f);
    for (int i = 0; i < 8; ++i) {
        const float ang = i * 0.785398f;
        const float cs = std::cos(ang);
        const float sn = std::sin(ang);
        dc->DrawLine(
            D2D1::Point2F(cx + cs * 4.5f, cy + sn * 4.5f),
            D2D1::Point2F(cx + cs * 6.5f, cy + sn * 6.5f),
            brush, 1.25f
        );
    }
}

void DrawSpeakerIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->FillRectangle(D2D1::RectF(cx - 5.5f, cy - 2.5f, cx - 2.5f, cy + 2.5f), brush);
    dc->DrawLine(D2D1::Point2F(cx - 2.5f, cy - 2.5f), D2D1::Point2F(cx + 1.5f, cy - 5.5f), brush, 1.5f);
    dc->DrawLine(D2D1::Point2F(cx - 2.5f, cy + 2.5f), D2D1::Point2F(cx + 1.5f, cy + 5.5f), brush, 1.5f);
    dc->DrawLine(D2D1::Point2F(cx + 1.5f, cy - 5.5f), D2D1::Point2F(cx + 1.5f, cy + 5.5f), brush, 1.5f);
    dc->DrawLine(D2D1::Point2F(cx + 4.2f, cy - 3.0f), D2D1::Point2F(cx + 5.2f, cy), brush, 1.3f);
    dc->DrawLine(D2D1::Point2F(cx + 5.2f, cy), D2D1::Point2F(cx + 4.2f, cy + 3.0f), brush, 1.3f);
}

void DrawMicIcon(ID2D1RenderTarget* dc, float cx, float cy, bool muted, ID2D1Brush* brush) {
    dc->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(cx - 2.4f, cy - 5.5f, cx + 2.4f, cy + 1.2f), 2.2f, 2.2f),
        brush, 1.4f
    );
    dc->DrawLine(D2D1::Point2F(cx - 4.5f, cy - 1.0f), D2D1::Point2F(cx - 4.5f, cy + 2.0f), brush, 1.3f);
    dc->DrawLine(D2D1::Point2F(cx + 4.5f, cy - 1.0f), D2D1::Point2F(cx + 4.5f, cy + 2.0f), brush, 1.3f);
    dc->DrawLine(D2D1::Point2F(cx - 4.5f, cy + 2.0f), D2D1::Point2F(cx + 4.5f, cy + 2.0f), brush, 1.3f);
    dc->DrawLine(D2D1::Point2F(cx, cy + 2.0f), D2D1::Point2F(cx, cy + 5.5f), brush, 1.4f);
    if (muted) {
        dc->DrawLine(D2D1::Point2F(cx - 5.5f, cy + 5.5f), D2D1::Point2F(cx + 5.5f, cy - 5.5f), brush, 1.6f);
    }
}

void DrawPinIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->DrawLine(D2D1::Point2F(cx - 3.5f, cy - 4.5f), D2D1::Point2F(cx + 3.5f, cy - 4.5f), brush, 1.5f);
    dc->DrawLine(D2D1::Point2F(cx - 2.5f, cy - 4.5f), D2D1::Point2F(cx - 3.5f, cy + 0.5f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx + 2.5f, cy - 4.5f), D2D1::Point2F(cx + 3.5f, cy + 0.5f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx - 5.0f, cy + 0.5f), D2D1::Point2F(cx + 5.0f, cy + 0.5f), brush, 1.6f);
    dc->DrawLine(D2D1::Point2F(cx, cy + 0.5f), D2D1::Point2F(cx, cy + 5.5f), brush, 1.5f);
}

void DrawWifiIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 4.2f), 1.3f, 1.3f), brush);
    dc->DrawLine(D2D1::Point2F(cx - 3.5f, cy + 1.0f), D2D1::Point2F(cx, cy - 0.8f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx, cy - 0.8f), D2D1::Point2F(cx + 3.5f, cy + 1.0f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx - 6.5f, cy - 1.5f), D2D1::Point2F(cx, cy - 4.8f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx, cy - 4.8f), D2D1::Point2F(cx + 6.5f, cy - 1.5f), brush, 1.4f);
}

void DrawBluetoothIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->DrawLine(D2D1::Point2F(cx, cy - 6.0f), D2D1::Point2F(cx, cy + 6.0f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx, cy - 6.0f), D2D1::Point2F(cx + 4.0f, cy - 2.5f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx + 4.0f, cy - 2.5f), D2D1::Point2F(cx - 3.5f, cy + 2.5f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx, cy + 6.0f), D2D1::Point2F(cx + 4.0f, cy + 2.5f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx + 4.0f, cy + 2.5f), D2D1::Point2F(cx - 3.5f, cy - 2.5f), brush, 1.4f);
}

void DrawMoonIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.8f, 4.8f), brush, 1.5f);
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 2.2f, cy - 1.8f), 2.5f, 2.5f), brush);
}

void DrawBoltIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->DrawLine(D2D1::Point2F(cx + 1.5f, cy - 6.0f), D2D1::Point2F(cx - 3.2f, cy + 0.5f), brush, 1.6f);
    dc->DrawLine(D2D1::Point2F(cx - 3.2f, cy + 0.5f), D2D1::Point2F(cx + 1.2f, cy + 0.5f), brush, 1.6f);
    dc->DrawLine(D2D1::Point2F(cx + 1.2f, cy + 0.5f), D2D1::Point2F(cx - 1.5f, cy + 6.0f), brush, 1.6f);
}

void DrawTimerIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 0.5f), 5.0f, 5.0f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx, cy + 0.5f), D2D1::Point2F(cx, cy - 2.5f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx, cy + 0.5f), D2D1::Point2F(cx + 2.5f, cy + 2.0f), brush, 1.4f);
}

void DrawSnipIcon(ID2D1RenderTarget* dc, float cx, float cy, ID2D1Brush* brush) {
    dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 3.5f, cy + 3.5f), 2.0f, 2.0f), brush, 1.3f);
    dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 3.5f, cy + 3.5f), 2.0f, 2.0f), brush, 1.3f);
    dc->DrawLine(D2D1::Point2F(cx - 2.2f, cy + 2.0f), D2D1::Point2F(cx + 3.5f, cy - 5.0f), brush, 1.4f);
    dc->DrawLine(D2D1::Point2F(cx + 2.2f, cy + 2.0f), D2D1::Point2F(cx - 3.5f, cy - 5.0f), brush, 1.4f);
}

} // namespace

SlimLineWindow::SlimLineWindow(HINSTANCE hInstance) : hInstance_(hInstance) {}

SlimLineWindow::~SlimLineWindow() {
    RemoveTrayIcon();
    if (hwnd_) {
        ::KillTimer(hwnd_, core::TIMER_SPRING_ANIM);
        ::KillTimer(hwnd_, core::TIMER_POMODORO_TICK);
        ::KillTimer(hwnd_, core::TIMER_REC_TICK);
        ::UnregisterHotKey(hwnd_, core::HOTKEY_GLOBAL_MIC_MUTE);
        ::UnregisterHotKey(hwnd_, core::HOTKEY_TOGGLE_NOTCH);
        ::RevokeDragDrop(hwnd_);
    }
    if (hitTestHwnd_) {
        ::RevokeDragDrop(hitTestHwnd_);
        ::DestroyWindow(hitTestHwnd_);
    }
}

bool SlimLineWindow::Create() {
    HICON appIcon = static_cast<HICON>(::LoadImageW(
        hInstance_, MAKEINTRESOURCEW(core::IDI_NOTCHGLASS),
        IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED
    ));
    if (!appIcon) {
        appIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SlimLineWindow::MainWndProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = kMainClassName;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    ::RegisterClassExW(&wc);

    WNDCLASSEXW hwc{};
    hwc.cbSize = sizeof(hwc);
    hwc.lpfnWndProc = &SlimLineWindow::HitTestWndProc;
    hwc.hInstance = hInstance_;
    hwc.lpszClassName = kHitTestClassName;
    hwc.hCursor = ::LoadCursorW(nullptr, IDC_HAND);
    hwc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    ::RegisterClassExW(&hwc);

    RefreshMonitorMetrics();

    const DWORD mainExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                              WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    hwnd_ = ::CreateWindowExW(
        mainExStyle, kMainClassName, L"NotchGlass", WS_POPUP,
        0, 0, 104, 8, nullptr, nullptr, hInstance_, this
    );
    if (!hwnd_) return false;

    const DWORD hitExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE;
    hitTestHwnd_ = ::CreateWindowExW(
        hitExStyle, kHitTestClassName, L"NotchGlassHitTest", WS_POPUP,
        0, 0, 196, 28, nullptr, nullptr, hInstance_, this
    );
    if (!hitTestHwnd_) return false;

    ::SetLayeredWindowAttributes(hitTestHwnd_, 0, 1, LWA_ALPHA);

    if (!glassEngine_.Initialize(hwnd_, dpiScale_)) {
        return false;
    }

    ApplyCollapsedIdleBounds();

    ::RegisterHotKey(hwnd_, core::HOTKEY_GLOBAL_MIC_MUTE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M');
    ::RegisterHotKey(hwnd_, core::HOTKEY_TOGGLE_NOTCH, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N');
    clipboardModule_.EnsureInitialized(hwnd_);
    micAudioModule_.EnsureInitialized(hwnd_);
    focusModule_.EnsureInitialized(hwnd_);

    fileShelfModule_.EnsureInitialized(hwnd_);
    fileShelfModule_.SetSendToPhoneCallback([this](const std::wstring& path) {
        qrWifiModule_.EnsureInitialized(hwnd_);
        qrWifiModule_.StartTransfer(path);
        activeTab_ = ActiveTab::ShelfQr;
        RenderCurrentState();
    });

    ::RegisterDragDrop(hitTestHwnd_, &fileShelfModule_);
    ::RegisterDragDrop(hwnd_, &fileShelfModule_);

    ::SetCoalescableTimer(hwnd_, core::TIMER_FULLSCREEN_CHK, 2000, nullptr, TIMERV_DEFAULT_COALESCING);

    AddTrayIcon();
    ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    ::ShowWindow(hitTestHwnd_, SW_SHOWNOACTIVATE);
    RenderCurrentState();

    startupShowcaseActive_ = true;
    BeginTransition(PillState::ExpandedHub);
    ::SetCoalescableTimer(hwnd_, core::TIMER_LEAVE_COLLAPSE, 3200, nullptr, TIMERV_DEFAULT_COALESCING);
    return true;
}

void SlimLineWindow::RefreshMonitorMetrics() {
    const HMONITOR mon = ::MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(mon, &mi)) {
        primaryMonitorRect_ = mi.rcMonitor;
    }

    const UINT dpi = ::GetDpiForSystem();
    dpiScale_ = static_cast<float>(dpi) / 96.0f;
}

void SlimLineWindow::ApplyWindowBounds(float widthDip, float heightDip) {
    currentWidthDip_ = widthDip;
    currentHeightDip_ = heightDip;

    const int wPx = static_cast<int>(std::ceil(widthDip * dpiScale_));
    const int hPx = static_cast<int>(std::ceil(heightDip * dpiScale_));
    const int monWidth = primaryMonitorRect_.right - primaryMonitorRect_.left;
    windowXPx_ = primaryMonitorRect_.left + (monWidth - wPx) / 2;
    windowYPx_ = primaryMonitorRect_.top;

    ::SetWindowPos(hwnd_, HWND_TOPMOST, windowXPx_, windowYPx_, wPx, hPx,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    glassEngine_.Resize(widthDip, heightDip, dpiScale_);

    const float hitW = HasAnyLivePill()
        ? core::GeometryConfig::kLivePillWidthDip + 16.0f
        : core::GeometryConfig::kProximityWidthDip;
    const float hitH = HasAnyLivePill()
        ? core::GeometryConfig::kLivePillHeightDip + 6.0f
        : core::GeometryConfig::kProximityHeightDip;
    const int hitWPx = static_cast<int>(std::ceil(hitW * dpiScale_));
    const int hitHPx = static_cast<int>(std::ceil(hitH * dpiScale_));
    const int hitXPx = primaryMonitorRect_.left + (monWidth - hitWPx) / 2;
    ::SetWindowPos(hitTestHwnd_, HWND_TOPMOST, hitXPx, windowYPx_, hitWPx, hitHPx,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

bool SlimLineWindow::HasAnyLivePill() const noexcept {
    return focusModule_.HasLiveActivityPill() || quickToolsModule_.IsRecording();
}

void SlimLineWindow::ApplyCollapsedIdleBounds() {
    if (HasAnyLivePill()) {
        currentRadiusDip_ = core::GeometryConfig::kLivePillRadiusDip;
        ApplyWindowBounds(core::GeometryConfig::kLivePillWidthDip, core::GeometryConfig::kLivePillHeightDip);
    } else {
        currentRadiusDip_ = core::GeometryConfig::kCollapsedRadiusDip;
        ApplyWindowBounds(core::GeometryConfig::kCollapsedWidthDip, core::GeometryConfig::kCollapsedHeightDip);
    }
}

void SlimLineWindow::SetClickThrough(bool transparent) {
    LONG_PTR exStyle = ::GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (transparent) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    ::SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
}

void SlimLineWindow::SetNoActivate(bool noActivate) {
    LONG_PTR exStyle = ::GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (noActivate) {
        exStyle |= WS_EX_NOACTIVATE;
    } else {
        exStyle &= ~WS_EX_NOACTIVATE;
    }
    ::SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
}

void SlimLineWindow::BeginTransition(PillState targetState) {
    if (targetState == PillState::Expanding || targetState == PillState::ExpandedHub) {
        ::KillTimer(hwnd_, core::TIMER_LEAVE_COLLAPSE);
        mediaModule_.EnsureInitialized(hwnd_);
        micAudioModule_.RefreshAudioMixerData();
        glassEngine_.SampleTopEdgeLuminance(primaryMonitorRect_);
        SetClickThrough(false);
        ::ShowWindow(hitTestHwnd_, SW_HIDE);
        state_ = PillState::Expanding;
        animTarget_ = 1.0f;
    } else {
        ::KillTimer(hwnd_, core::TIMER_LEAVE_COLLAPSE);
        trackingLeave_ = false;
        dragTarget_ = DragTarget::None;
        focusModule_.CancelTypingTask();
        SetNoActivate(true);
        state_ = PillState::Collapsing;
        animTarget_ = 0.0f;
    }

    ::SetTimer(hwnd_, core::TIMER_SPRING_ANIM, 15, nullptr);
    StepSpringAnimation();
}

void SlimLineWindow::StepSpringAnimation() {
    constexpr float dt = 1.0f / 60.0f;
    constexpr float stiffness = 360.0f;
    constexpr float damping = 32.0f;

    const float force = (animTarget_ - animProgress_) * stiffness;
    animVelocity_ = (animVelocity_ + force * dt) * std::exp(-damping * dt);
    animProgress_ += animVelocity_ * dt;

    const bool settled = (std::abs(animTarget_ - animProgress_) < 0.006f) && (std::abs(animVelocity_) < 0.012f);
    if (settled) {
        animProgress_ = animTarget_;
        animVelocity_ = 0.0f;
    }

    const float baseW = HasAnyLivePill() ? core::GeometryConfig::kLivePillWidthDip : core::GeometryConfig::kCollapsedWidthDip;
    const float baseH = HasAnyLivePill() ? core::GeometryConfig::kLivePillHeightDip : core::GeometryConfig::kCollapsedHeightDip;
    const float baseR = HasAnyLivePill() ? core::GeometryConfig::kLivePillRadiusDip : core::GeometryConfig::kCollapsedRadiusDip;

    const float t = std::clamp(animProgress_, 0.0f, 1.12f);
    const float w = std::lerp(baseW, core::GeometryConfig::kExpandedWidthDip, t);
    const float h = std::lerp(baseH, core::GeometryConfig::kExpandedHeightDip, t);
    currentRadiusDip_ = std::lerp(baseR, core::GeometryConfig::kExpandedRadiusDip, std::clamp(t, 0.0f, 1.0f));

    ApplyWindowBounds(w, h);
    RenderCurrentState();

    if (settled) {
        ::KillTimer(hwnd_, core::TIMER_SPRING_ANIM);
        if (animTarget_ == 1.0f) {
            state_ = PillState::ExpandedHub;
            RenderCurrentState();
            if (!startupShowcaseActive_ && !pinned_ && !modalDialogOpen_ && !fileShelfModule_.IsDraggingOver()) {
                POINT pt{};
                ::GetCursorPos(&pt);
                RECT wr{};
                ::GetWindowRect(hwnd_, &wr);
                if (!::PtInRect(&wr, pt)) {
                    ::SetCoalescableTimer(hwnd_, core::TIMER_LEAVE_COLLAPSE, leaveCollapseMs_, nullptr, TIMERV_DEFAULT_COALESCING);
                }
            }
        } else {
            startupShowcaseActive_ = false;
            state_ = PillState::CollapsedIdle;
            ApplyCollapsedIdleBounds();
            SetClickThrough(true);
            ::ShowWindow(hitTestHwnd_, SW_SHOWNOACTIVATE);

            mediaModule_.OnCollapseRelease();
            fileShelfModule_.OnCollapseRelease();
            focusModule_.OnCollapseRelease();
            micAudioModule_.OnCollapseRelease();
            glassEngine_.TrimGpuMemory();
            RenderCurrentState();
        }
    }
}

void SlimLineWindow::CheckFullscreenGameState() {
    if (!gameStripEnabled_) {
        if (state_ == PillState::GameStrip) {
            ::KillTimer(hwnd_, core::TIMER_GAME_TELEMETRY);
            quickToolsModule_.SetGameStripActive(false);
            state_ = PillState::CollapsedIdle;
            ApplyCollapsedIdleBounds();
            ::ShowWindow(hitTestHwnd_, SW_SHOWNOACTIVATE);
            RenderCurrentState();
        }
        return;
    }

    QUERY_USER_NOTIFICATION_STATE quns = QUNS_ACCEPTS_NOTIFICATIONS;
    if (FAILED(::SHQueryUserNotificationState(&quns))) return;

    const bool fullscreen = (quns == QUNS_BUSY || quns == QUNS_RUNNING_D3D_FULL_SCREEN);
    if (fullscreen && state_ != PillState::GameStrip) {
        state_ = PillState::GameStrip;
        SetClickThrough(true);
        ::ShowWindow(hitTestHwnd_, SW_HIDE);
        ApplyWindowBounds(core::GeometryConfig::kGameStripWidthDip, core::GeometryConfig::kGameStripHeightDip);
        quickToolsModule_.EnsureInitialized(hwnd_);
        quickToolsModule_.SetGameStripActive(true);
        ::SetCoalescableTimer(hwnd_, core::TIMER_GAME_TELEMETRY, core::GeometryConfig::kGameTelemetryMs, nullptr, TIMERV_DEFAULT_COALESCING);
        RenderCurrentState();
    } else if (!fullscreen && state_ == PillState::GameStrip) {
        ::KillTimer(hwnd_, core::TIMER_GAME_TELEMETRY);
        quickToolsModule_.SetGameStripActive(false);
        state_ = PillState::CollapsedIdle;
        ApplyCollapsedIdleBounds();
        ::ShowWindow(hitTestHwnd_, SW_SHOWNOACTIVATE);
        glassEngine_.TrimGpuMemory();
        RenderCurrentState();
    }
}

void SlimLineWindow::RenderCurrentState() {
    frameArena_.Reset();

    glassEngine_.RenderFrame(
        windowXPx_,
        windowYPx_,
        currentWidthDip_,
        currentHeightDip_,
        currentRadiusDip_,
        animProgress_,
        [this](ID2D1RenderTarget* dc, IDWriteFactory* dw, D2D1_RECT_F bounds) {
            if (state_ == PillState::CollapsedIdle) {
                if (HasAnyLivePill()) {
                    // Dynamic-Island-Style Compact Live Activity Pill (REC / Focus / Watch)
                    core::ComPtr<ID2D1SolidColorBrush> textBrush;
                    core::ComPtr<ID2D1SolidColorBrush> dotBrush;
                    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
                    const bool isRec = quickToolsModule_.IsRecording();
                    const D2D1_COLOR_F dotClr = isRec
                        ? D2D1::ColorF(0.96f, 0.30f, 0.36f, 0.98f)
                        : (focusModule_.IsPomodoroRunning()
                            ? D2D1::ColorF(0.26f, 0.90f, 0.56f, 0.98f)
                            : D2D1::ColorF(0.36f, 0.82f, 1.00f, 0.98f));
                    dc->CreateSolidColorBrush(dotClr, &dotBrush);

                    const float cy = bounds.bottom * 0.5f;
                    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(15.0f, cy), 4.2f, 4.2f), dotBrush.Get());

                    const std::uint32_t sec = isRec
                        ? quickToolsModule_.GetRecElapsedSec()
                        : (focusModule_.IsPomodoroRunning()
                            ? focusModule_.GetPomoRemainingSec()
                            : focusModule_.GetStopwatchElapsedSec());
                    const wchar_t* tag = isRec ? L"REC" : (focusModule_.IsPomodoroRunning() ? L"Focus" : L"Watch");
                    wchar_t pillBuf[32];
                    std::swprintf(pillBuf, 32, L"%s  %02u:%02u", tag, sec / 60, sec % 60);

                    core::ComPtr<IDWriteTextFormat> fmt;
                    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &fmt);
                    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    dc->DrawTextW(pillBuf, static_cast<UINT32>(wcslen(pillBuf)), fmt.Get(),
                        D2D1::RectF(22.0f, 0.0f, bounds.right - 10.0f, bounds.bottom), textBrush.Get());
                } else {
                    // Refined Compact Thicker Liquid Glass Bar (104x8px)
                    core::ComPtr<ID2D1SolidColorBrush> barBrush;
                    core::ComPtr<ID2D1SolidColorBrush> dotBrush;
                    dc->CreateSolidColorBrush(D2D1::ColorF(0.86f, 0.94f, 1.0f, 0.82f), &barBrush);
                    dc->CreateSolidColorBrush(
                        micAudioModule_.IsMicMuted()
                            ? D2D1::ColorF(0.95f, 0.34f, 0.38f, 0.98f)
                            : D2D1::ColorF(0.32f, 0.86f, 1.0f, 0.98f),
                        &dotBrush
                    );
                    if (barBrush) {
                        dc->FillRoundedRectangle(
                            D2D1::RoundedRect(D2D1::RectF(20.0f, 2.2f, bounds.right - 16.0f, bounds.bottom - 2.2f), 1.8f, 1.8f),
                            barBrush.Get()
                        );
                    }
                    if (dotBrush) {
                        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(11.0f, bounds.bottom * 0.5f), 2.1f, 2.1f), dotBrush.Get());
                    }
                }
                return;
            }

            if (state_ == PillState::GameStrip) {
                const auto& g = quickToolsModule_.GetGameTelemetry();
                wchar_t buf[128];
                std::swprintf(buf, 128, L"%.0f FPS  |  CPU %.0f%%  |  GPU %.0f%%  |  RAM %.1f GB  |  %.0f°C",
                    g.fps, g.cpuPct, g.gpuPct, g.ramUsedGb, g.tempCelsius);
                core::ComPtr<IDWriteTextFormat> fmt;
                dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &fmt);
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                core::ComPtr<ID2D1SolidColorBrush> brush;
                dc->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.97f, 1.0f, 0.96f), &brush);
                dc->DrawTextW(buf, static_cast<UINT32>(wcslen(buf)), fmt.Get(), bounds, brush.Get());
                return;
            }

            if (animProgress_ < 0.7f) return;

            core::ComPtr<IDWriteTextFormat> tabFmt;
            dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &tabFmt);
            core::ComPtr<ID2D1SolidColorBrush> activeBrush;
            core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
            core::ComPtr<ID2D1SolidColorBrush> amberBrush;
            dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), &activeBrush);
            dc->CreateSolidColorBrush(D2D1::ColorF(0.84f, 0.90f, 0.98f, 0.78f), &mutedBrush);
            dc->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.80f, 0.35f, 0.98f), &amberBrush);

            tabFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            tabFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            for (int i = 0; i < 8; ++i) {
                D2D1_RECT_F tabRect = D2D1::RectF(kTabSpans[i].left, 10.0f, kTabSpans[i].right, 33.0f);
                const bool sel = (static_cast<int>(activeTab_) == i);
                if (sel) {
                    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, tabRect, 11.5f, true, true);
                }
                dc->DrawTextW(kTabSpans[i].label, static_cast<UINT32>(wcslen(kTabSpans[i].label)), tabFmt.Get(),
                    tabRect, sel ? activeBrush.Get() : mutedBrush.Get());
            }

            // Pin toggle icon glass pill (398..424)
            D2D1_RECT_F pinBtn = D2D1::RectF(398.0f, 10.0f, 424.0f, 33.0f);
            graphics::LiquidGlassEngine::DrawGlassCapsule(dc, pinBtn, 11.0f, pinned_, true);
            DrawPinIcon(dc, 411.0f, 21.5f, pinned_ ? amberBrush.Get() : mutedBrush.Get());

            // Glass preset cycle pill (428..466)
            D2D1_RECT_F themeBtn = D2D1::RectF(428.0f, 10.0f, 466.0f, 33.0f);
            graphics::LiquidGlassEngine::DrawGlassCapsule(dc, themeBtn, 11.0f, false, true);
            dc->DrawTextW(L"Glass", 5, tabFmt.Get(), themeBtn, activeBrush.Get());

            D2D1_RECT_F contentBounds = D2D1::RectF(16.0f, 42.0f, bounds.right - 16.0f, bounds.bottom - 12.0f);
            modules::RenderContext rctx{
                dc, dw, contentBounds,
                dpiScale_, state_ == PillState::ExpandedHub
            };

            switch (activeTab_) {
                case ActiveTab::Home:      RenderHomeOverview(dc, dw, contentBounds); break;
                case ActiveTab::Audio:     micAudioModule_.Render(rctx); break;
                case ActiveTab::Focus:     focusModule_.Render(rctx); break;
                case ActiveTab::Rec:       quickToolsModule_.Render(rctx); break;
                case ActiveTab::Media:     mediaModule_.Render(rctx); break;
                case ActiveTab::ShelfQr:
                    if (qrWifiModule_.IsServerActive()) qrWifiModule_.Render(rctx);
                    else fileShelfModule_.Render(rctx);
                    break;
                case ActiveTab::Clipboard: clipboardModule_.Render(rctx); break;
                case ActiveTab::Settings:  RenderSettingsPage(dc, dw, contentBounds); break;
            }
        }
    );
}

void SlimLineWindow::RenderHomeOverview(ID2D1RenderTarget* dc, IDWriteFactory* dw, D2D1_RECT_F bounds) {
    core::ComPtr<ID2D1SolidColorBrush> textBrush;
    core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
    core::ComPtr<ID2D1SolidColorBrush> accentBrush;
    core::ComPtr<ID2D1SolidColorBrush> trackBg;
    core::ComPtr<ID2D1SolidColorBrush> greenBrush;
    core::ComPtr<ID2D1SolidColorBrush> dangerBrush;

    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.84f, 0.90f, 0.98f, 0.80f), &mutedBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.82f, 1.00f, 0.96f), &accentBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.18f), &trackBg);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.88f, 0.56f, 0.95f), &greenBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.34f, 0.38f, 0.95f), &dangerBrush);

    core::ComPtr<IDWriteTextFormat> titleFmt;
    core::ComPtr<IDWriteTextFormat> subFmt;
    core::ComPtr<IDWriteTextFormat> miniFmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &titleFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &subFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.5f, L"en-us", &miniFmt);

    // 1) Compact Music Capsule (bounds.top+2 .. +50)
    D2D1_RECT_F musicCard = D2D1::RectF(bounds.left, bounds.top + 2.0f, bounds.right, bounds.top + 50.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, musicCard, 14.0f, false, false);

    D2D1_RECT_F artBadge = D2D1::RectF(musicCard.left + 8.0f, musicCard.top + 8.0f, musicCard.left + 40.0f, musicCard.top + 40.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, artBadge, 9.0f, true, true);
    DrawMusicNoteIcon(dc, (artBadge.left + artBadge.right) * 0.5f, (artBadge.top + artBadge.bottom) * 0.5f, accentBrush.Get());

    const std::wstring title = mediaModule_.GetTitle();
    const std::wstring artist = mediaModule_.GetArtist();
    dc->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), titleFmt.Get(),
        D2D1::RectF(artBadge.right + 10.0f, musicCard.top + 8.0f, musicCard.right - 108.0f, musicCard.top + 25.0f), textBrush.Get());
    dc->DrawTextW(artist.c_str(), static_cast<UINT32>(artist.size()), subFmt.Get(),
        D2D1::RectF(artBadge.right + 10.0f, musicCard.top + 25.0f, musicCard.right - 108.0f, musicCard.top + 42.0f), mutedBrush.Get());

    const float cyM = (musicCard.top + musicCard.bottom) * 0.5f;
    const float prevX = musicCard.right - 84.0f;
    const float playX = musicCard.right - 52.0f;
    const float nextX = musicCard.right - 20.0f;

    dc->DrawLine(D2D1::Point2F(prevX - 4.0f, cyM - 4.5f), D2D1::Point2F(prevX - 4.0f, cyM + 4.5f), textBrush.Get(), 1.6f);
    dc->DrawLine(D2D1::Point2F(prevX + 4.0f, cyM - 4.5f), D2D1::Point2F(prevX - 2.0f, cyM), textBrush.Get(), 1.6f);
    dc->DrawLine(D2D1::Point2F(prevX - 2.0f, cyM), D2D1::Point2F(prevX + 4.0f, cyM + 4.5f), textBrush.Get(), 1.6f);

    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, D2D1::RectF(playX - 13.0f, cyM - 13.0f, playX + 13.0f, cyM + 13.0f), 13.0f, true, true);
    if (mediaModule_.IsPlaying()) {
        dc->FillRectangle(D2D1::RectF(playX - 4.0f, cyM - 4.5f, playX - 1.5f, cyM + 4.5f), textBrush.Get());
        dc->FillRectangle(D2D1::RectF(playX + 1.5f, cyM - 4.5f, playX + 4.0f, cyM + 4.5f), textBrush.Get());
    } else {
        dc->DrawLine(D2D1::Point2F(playX - 3.0f, cyM - 5.0f), D2D1::Point2F(playX + 5.0f, cyM), textBrush.Get(), 2.0f);
        dc->DrawLine(D2D1::Point2F(playX + 5.0f, cyM), D2D1::Point2F(playX - 3.0f, cyM + 5.0f), textBrush.Get(), 2.0f);
        dc->DrawLine(D2D1::Point2F(playX - 3.0f, cyM - 5.0f), D2D1::Point2F(playX - 3.0f, cyM + 5.0f), textBrush.Get(), 2.0f);
    }

    dc->DrawLine(D2D1::Point2F(nextX + 4.0f, cyM - 4.5f), D2D1::Point2F(nextX + 4.0f, cyM + 4.5f), textBrush.Get(), 1.6f);
    dc->DrawLine(D2D1::Point2F(nextX - 4.0f, cyM - 4.5f), D2D1::Point2F(nextX + 2.0f, cyM), textBrush.Get(), 1.6f);
    dc->DrawLine(D2D1::Point2F(nextX + 2.0f, cyM), D2D1::Point2F(nextX - 4.0f, cyM + 4.5f), textBrush.Get(), 1.6f);

    // 2) Individual Display Brightness Capsule + Volume/App Mixer & Mic Capsule (top+58 .. +154)
    const float midTop = musicCard.bottom + 8.0f;
    const float midW = (bounds.right - bounds.left - 8.0f) * 0.5f;
    D2D1_RECT_F brightCard = D2D1::RectF(bounds.left, midTop, bounds.left + midW, midTop + 96.0f);
    D2D1_RECT_F volCard    = D2D1::RectF(brightCard.right + 8.0f, midTop, bounds.right, midTop + 96.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, brightCard, 14.0f, false, false);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, volCard, 14.0f, false, false);

    // Display selector pills: [D1] [D2] ... [All]
    DrawSunIcon(dc, brightCard.left + 16.0f, brightCard.top + 16.0f, accentBrush.Get());
    const auto& mons = micAudioModule_.GetMonitors();
    const std::size_t selIdx = micAudioModule_.GetSelectedMonitorIndex();
    wchar_t bBuf[96];
    if (micAudioModule_.GetSyncAllMonitors()) {
        std::swprintf(bBuf, 96, L"All Displays: %d%%", micAudioModule_.GetBrightnessPct());
    } else if (!mons.empty() && selIdx < mons.size()) {
        std::swprintf(bBuf, 96, L"Disp %zu: %d%%", selIdx + 1, mons[selIdx].currentPct);
    } else {
        std::swprintf(bBuf, 96, L"Brightness: %d%%", micAudioModule_.GetBrightnessPct());
    }
    dc->DrawTextW(bBuf, static_cast<UINT32>(wcslen(bBuf)), titleFmt.Get(),
        D2D1::RectF(brightCard.left + 28.0f, brightCard.top + 7.0f, brightCard.right - 8.0f, brightCard.top + 26.0f), textBrush.Get());

    miniFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    miniFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    const std::size_t showMons = std::min<std::size_t>(mons.size(), 3);
    float pillX = brightCard.left + 10.0f;
    for (std::size_t i = 0; i < showMons; ++i) {
        D2D1_RECT_F mp = D2D1::RectF(pillX, brightCard.top + 30.0f, pillX + 38.0f, brightCard.top + 48.0f);
        const bool isSel = !micAudioModule_.GetSyncAllMonitors() && (selIdx == i);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, mp, 8.0f, isSel, true);
        wchar_t dLbl[12];
        std::swprintf(dLbl, 12, L"D%zu", i + 1);
        dc->DrawTextW(dLbl, static_cast<UINT32>(wcslen(dLbl)), miniFmt.Get(), mp, textBrush.Get());
        pillX += 42.0f;
    }
    D2D1_RECT_F syncPill = D2D1::RectF(brightCard.right - 56.0f, brightCard.top + 30.0f, brightCard.right - 10.0f, brightCard.top + 48.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, syncPill, 8.0f, micAudioModule_.GetSyncAllMonitors(), true);
    dc->DrawTextW(L"Sync", 4, miniFmt.Get(), syncPill, textBrush.Get());

    const float bTrackL = brightCard.left + 12.0f;
    const float bTrackR = brightCard.right - 12.0f;
    const float bRatio = std::clamp(micAudioModule_.GetBrightnessPct() / 100.0f, 0.05f, 1.0f);
    const float bFillX = bTrackL + (bTrackR - bTrackL) * bRatio;
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bTrackL, brightCard.top + 60.0f, bTrackR, brightCard.top + 66.0f), 3.0f, 3.0f), trackBg.Get());
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bTrackL, brightCard.top + 60.0f, bFillX, brightCard.top + 66.0f), 3.0f, 3.0f), accentBrush.Get());
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bFillX, brightCard.top + 63.0f), 5.0f, 5.0f), textBrush.Get());

    std::wstring dispName = (!mons.empty() && selIdx < mons.size()) ? mons[selIdx].name : L"Display Control";
    dc->DrawTextW(dispName.c_str(), static_cast<UINT32>(std::min<std::size_t>(dispName.size(), 26)), subFmt.Get(),
        D2D1::RectF(brightCard.left + 12.0f, brightCard.top + 73.0f, brightCard.right - 10.0f, brightCard.bottom - 4.0f), mutedBrush.Get());

    // Right Card: Master Volume + Per-App Mixer Pill + Live Mic Pill
    DrawSpeakerIcon(dc, volCard.left + 16.0f, volCard.top + 16.0f, accentBrush.Get());
    wchar_t vBuf[48];
    std::swprintf(vBuf, 48, L"Master: %d%%", static_cast<int>(micAudioModule_.GetOutputVolume() * 100.0f));
    dc->DrawTextW(vBuf, static_cast<UINT32>(wcslen(vBuf)), titleFmt.Get(),
        D2D1::RectF(volCard.left + 28.0f, volCard.top + 7.0f, volCard.right - 72.0f, volCard.top + 26.0f), textBrush.Get());

    D2D1_RECT_F mixerBtn = D2D1::RectF(volCard.right - 68.0f, volCard.top + 7.0f, volCard.right - 8.0f, volCard.top + 25.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, mixerBtn, 8.0f, true, true);
    dc->DrawTextW(L"Apps >", 6, miniFmt.Get(), mixerBtn, textBrush.Get());

    const float vTrackL = volCard.left + 12.0f;
    const float vTrackR = volCard.right - 12.0f;
    const float vRatio = std::clamp(micAudioModule_.GetOutputVolume(), 0.0f, 1.0f);
    const float vFillX = vTrackL + (vTrackR - vTrackL) * vRatio;
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(vTrackL, volCard.top + 38.0f, vTrackR, volCard.top + 44.0f), 3.0f, 3.0f), trackBg.Get());
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(vTrackL, volCard.top + 38.0f, vFillX, volCard.top + 44.0f), 3.0f, 3.0f), accentBrush.Get());
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(vFillX, volCard.top + 41.0f), 5.0f, 5.0f), textBrush.Get());

    D2D1_RECT_F micBtn = D2D1::RectF(volCard.left + 10.0f, volCard.top + 58.0f, volCard.right - 10.0f, volCard.bottom - 8.0f);
    const bool muted = micAudioModule_.IsMicMuted();
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, micBtn, 10.0f, !muted, true);
    DrawMicIcon(dc, micBtn.left + 14.0f, (micBtn.top + micBtn.bottom) * 0.5f, muted, muted ? dangerBrush.Get() : greenBrush.Get());
    const std::uint32_t callSec = micAudioModule_.GetCallElapsedSec();
    wchar_t micTxt[48];
    std::swprintf(micTxt, 48, L"%s · %02u:%02u", muted ? L"Mic Muted" : L"Mic Live", callSec / 60, callSec % 60);
    dc->DrawTextW(micTxt, static_cast<UINT32>(wcslen(micTxt)), subFmt.Get(),
        D2D1::RectF(micBtn.left + 26.0f, micBtn.top + 7.0f, micBtn.right - 6.0f, micBtn.bottom - 4.0f), textBrush.Get());

    // 3) Focus / Dynamic Pill Quick Strip (top+162 .. +196)
    const float fTop = brightCard.bottom + 8.0f;
    D2D1_RECT_F focusStrip = D2D1::RectF(bounds.left, fTop, bounds.right, fTop + 34.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, focusStrip, 12.0f, focusModule_.HasLiveActivityPill(), false);
    DrawTimerIcon(dc, focusStrip.left + 18.0f, (focusStrip.top + focusStrip.bottom) * 0.5f,
        focusModule_.HasLiveActivityPill() ? greenBrush.Get() : accentBrush.Get());

    wchar_t fTxt[80];
    const std::uint32_t ps = focusModule_.GetPomoRemainingSec();
    const std::uint32_t ws = focusModule_.GetStopwatchElapsedSec();
    std::swprintf(fTxt, 80, L"Pomodoro %02u:%02u %s   ·   Stopwatch %02u:%02u   ·   Tasks",
        ps / 60, ps % 60, focusModule_.IsPomodoroRunning() ? L"(Live)" : L"",
        ws / 60, ws % 60);
    dc->DrawTextW(fTxt, static_cast<UINT32>(wcslen(fTxt)), subFmt.Get(),
        D2D1::RectF(focusStrip.left + 32.0f, focusStrip.top + 8.0f, focusStrip.right - 88.0f, focusStrip.bottom - 4.0f), textBrush.Get());

    D2D1_RECT_F openFocusBtn = D2D1::RectF(focusStrip.right - 82.0f, focusStrip.top + 5.0f, focusStrip.right - 8.0f, focusStrip.bottom - 5.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, openFocusBtn, 9.0f, true, true);
    dc->DrawTextW(L"Focus & To-Do", 13, miniFmt.Get(), openFocusBtn, textBrush.Get());

    // 4) 6 Quick Action Glass Tiles (top+204 .. bounds.bottom)
    const float qTop = focusStrip.bottom + 8.0f;
    const float totalW = bounds.right - bounds.left;
    const float gap = 6.0f;
    const float tileW = (totalW - gap * 5.0f) / 6.0f;

    wchar_t timerLbl[16];
    const std::uint32_t dispSec = focusModule_.IsStopwatchRunning() ? ws : ps;
    std::swprintf(timerLbl, 16, L"%02u:%02u", dispSec / 60, dispSec % 60);

    const wchar_t* qLabels[6] = {L"Wi-Fi", L"BT", L"Night", L"Power", timerLbl, L"Rec"};
    const bool qStates[6] = {wifiToggle_, btToggle_, nightLightToggle_, highPerfToggle_, focusModule_.HasLiveActivityPill(), quickToolsModule_.IsRecording()};

    for (int i = 0; i < 6; ++i) {
        const float lx = bounds.left + i * (tileW + gap);
        D2D1_RECT_F tile = D2D1::RectF(lx, qTop, lx + tileW, bounds.bottom);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, tile, 11.0f, qStates[i], true);

        const float icx = (tile.left + tile.right) * 0.5f;
        const float icy = tile.top + 16.0f;
        ID2D1Brush* ib = qStates[i] ? textBrush.Get() : mutedBrush.Get();
        switch (i) {
            case 0: DrawWifiIcon(dc, icx, icy, ib); break;
            case 1: DrawBluetoothIcon(dc, icx, icy, ib); break;
            case 2: DrawMoonIcon(dc, icx, icy, ib); break;
            case 3: DrawBoltIcon(dc, icx, icy, ib); break;
            case 4: DrawTimerIcon(dc, icx, icy, ib); break;
            case 5: DrawSnipIcon(dc, icx, icy, ib); break;
        }
        dc->DrawTextW(qLabels[i], static_cast<UINT32>(wcslen(qLabels[i])), miniFmt.Get(),
            D2D1::RectF(tile.left + 2.0f, tile.top + 28.0f, tile.right - 2.0f, tile.bottom - 3.0f), ib);
    }
}

void SlimLineWindow::RenderSettingsPage(ID2D1RenderTarget* dc, IDWriteFactory* dw, D2D1_RECT_F bounds) {
    core::ComPtr<ID2D1SolidColorBrush> textBrush;
    core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
    core::ComPtr<ID2D1SolidColorBrush> dangerBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.84f, 0.90f, 0.98f, 0.80f), &mutedBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.45f, 0.48f, 0.95f), &dangerBrush);

    core::ComPtr<IDWriteTextFormat> titleFmt;
    core::ComPtr<IDWriteTextFormat> btnFmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &titleFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &btnFmt);

    D2D1_RECT_F themeBox = D2D1::RectF(bounds.left, bounds.top + 4.0f, bounds.right, bounds.top + 74.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, themeBox, 14.0f, false, false);
    dc->DrawTextW(L"Liquid Glass Optical Preset", 27, titleFmt.Get(),
        D2D1::RectF(themeBox.left + 12.0f, themeBox.top + 8.0f, themeBox.right - 12.0f, themeBox.top + 26.0f), textBrush.Get());

    const wchar_t* themeNames[4] = {L"Liquid", L"Candy", L"Obsidian", L"Graphite"};
    const float tW = (themeBox.right - themeBox.left - 24.0f - 18.0f) * 0.25f;
    btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    btnFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    for (int i = 0; i < 4; ++i) {
        const float tx = themeBox.left + 12.0f + i * (tW + 6.0f);
        D2D1_RECT_F tBtn = D2D1::RectF(tx, themeBox.top + 32.0f, tx + tW, themeBox.bottom - 10.0f);
        const bool isSel = (static_cast<int>(glassEngine_.GetGlassPreset()) == i);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, tBtn, 10.0f, isSel, true);
        dc->DrawTextW(themeNames[i], static_cast<UINT32>(wcslen(themeNames[i])), btnFmt.Get(), tBtn, textBrush.Get());
    }

    const float bTop = themeBox.bottom + 10.0f;
    const float halfW = (bounds.right - bounds.left - 8.0f) * 0.5f;
    D2D1_RECT_F bRects[4] = {
        D2D1::RectF(bounds.left, bTop, bounds.left + halfW, bTop + 42.0f),
        D2D1::RectF(bounds.left + halfW + 8.0f, bTop, bounds.right, bTop + 42.0f),
        D2D1::RectF(bounds.left, bTop + 48.0f, bounds.left + halfW, bTop + 90.0f),
        D2D1::RectF(bounds.left + halfW + 8.0f, bTop + 48.0f, bounds.right, bTop + 90.0f),
    };
    const wchar_t* bLabels[4] = {
        pinned_ ? L"Pin Open: ON" : L"Pin Open: OFF",
        (leaveCollapseMs_ <= 60) ? L"Collapse: Instant (45ms)" : L"Collapse: Smooth (250ms)",
        (hoverDelayMs_ <= 90) ? L"Hover: Fast (80ms)" : L"Hover: Normal (120ms)",
        gameStripEnabled_ ? L"Game Strip 1Hz: ON" : L"Game Strip 1Hz: OFF"
    };
    const bool bOn[4] = {pinned_, leaveCollapseMs_ <= 60, hoverDelayMs_ <= 90, gameStripEnabled_};
    for (int i = 0; i < 4; ++i) {
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, bRects[i], 11.0f, bOn[i], true);
        dc->DrawTextW(bLabels[i], static_cast<UINT32>(wcslen(bLabels[i])), btnFmt.Get(), bRects[i], textBrush.Get());
    }

    const float fTop = bounds.bottom - 42.0f;
    D2D1_RECT_F infoBox = D2D1::RectF(bounds.left, fTop, bounds.right - 98.0f, bounds.bottom);
    D2D1_RECT_F exitBtn = D2D1::RectF(bounds.right - 90.0f, fTop, bounds.right, bounds.bottom);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, infoBox, 11.0f, false, false);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, exitBtn, 11.0f, true, true);

    btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    const wchar_t* shortcutInfo = L"Ctrl+Alt+N Toggle  ·  Ctrl+Alt+M Mic Mute";
    dc->DrawTextW(shortcutInfo, static_cast<UINT32>(wcslen(shortcutInfo)), btnFmt.Get(),
        D2D1::RectF(infoBox.left + 12.0f, infoBox.top, infoBox.right - 8.0f, infoBox.bottom), mutedBrush.Get());
    btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    dc->DrawTextW(L"Exit App", 8, btnFmt.Get(), exitBtn, dangerBrush.Get());
}

void SlimLineWindow::HandleHubClick(float xDip, float yDip) {
    dragTarget_ = DragTarget::None;

    if (yDip >= 8.0f && yDip <= 36.0f) {
        for (int i = 0; i < 8; ++i) {
            if (xDip >= kTabSpans[i].left && xDip <= kTabSpans[i].right) {
                focusModule_.CancelTypingTask();
                SetNoActivate(true);
                activeTab_ = static_cast<ActiveTab>(i);
                if (activeTab_ == ActiveTab::Audio) micAudioModule_.RefreshAudioMixerData();
                if (activeTab_ == ActiveTab::Rec) quickToolsModule_.EnsureInitialized(hwnd_);
                if (activeTab_ == ActiveTab::ShelfQr) qrWifiModule_.EnsureInitialized(hwnd_);
                RenderCurrentState();
                return;
            }
        }
        if (xDip >= 396.0f && xDip <= 425.0f) {
            pinned_ = !pinned_;
            RenderCurrentState();
            return;
        }
        if (xDip >= 426.0f) {
            const int next = (static_cast<int>(glassEngine_.GetGlassPreset()) + 1) % 4;
            glassEngine_.SetGlassPreset(static_cast<core::GlassPreset>(next));
            RenderCurrentState();
            return;
        }
    }

    bool handled = false;
    switch (activeTab_) {
        case ActiveTab::Home: {
            // 1) Music Strip (y: 44..94)
            if (yDip >= 44.0f && yDip <= 94.0f) {
                mediaModule_.EnsureInitialized(hwnd_);
                if (xDip >= 358.0f && xDip < 394.0f) {
                    handled = mediaModule_.OnPointerDown(156.0f, 128.0f);
                } else if (xDip >= 394.0f && xDip < 430.0f) {
                    handled = mediaModule_.OnPointerDown(222.0f, 128.0f);
                } else if (xDip >= 430.0f) {
                    handled = mediaModule_.OnPointerDown(292.0f, 128.0f);
                } else {
                    activeTab_ = ActiveTab::Media;
                    handled = true;
                }
            }
            // 2) Display Brightness & Audio / Mic (y: 100..196)
            else if (yDip >= 100.0f && yDip <= 198.0f) {
                if (xDip < 238.0f) {
                    // Monitor selector pills (y: 130..150)
                    if (yDip >= 126.0f && yDip <= 152.0f) {
                        if (xDip >= 172.0f) {
                            micAudioModule_.SetSyncAllMonitors(!micAudioModule_.GetSyncAllMonitors());
                        } else {
                            const std::size_t idx = static_cast<std::size_t>(std::max(0.0f, (xDip - 26.0f) / 42.0f));
                            if (idx < micAudioModule_.GetMonitorCount()) {
                                micAudioModule_.SetSyncAllMonitors(false);
                                micAudioModule_.SelectMonitor(idx);
                            }
                        }
                        handled = true;
                    } else {
                        dragTarget_ = DragTarget::HomeBrightness;
                        HandleHubDrag(xDip, yDip);
                        return;
                    }
                } else {
                    if (yDip <= 128.0f && xDip >= 390.0f) {
                        micAudioModule_.RefreshAudioMixerData();
                        activeTab_ = ActiveTab::Audio;
                        handled = true;
                    } else if (yDip >= 156.0f) {
                        micAudioModule_.ToggleGlobalMicMute();
                        handled = true;
                    } else {
                        dragTarget_ = DragTarget::HomeVolume;
                        HandleHubDrag(xDip, yDip);
                        return;
                    }
                }
            }
            // 3) Focus / Stopwatch Strip (y: 204..238)
            else if (yDip >= 202.0f && yDip <= 240.0f) {
                if (xDip >= 378.0f) {
                    activeTab_ = ActiveTab::Focus;
                } else {
                    focusModule_.TogglePomodoro();
                }
                handled = true;
            }
            // 4) 6 Quick Action Tiles (y: 244..300)
            else if (yDip >= 242.0f) {
                const int idx = std::clamp(static_cast<int>((xDip - 16.0f) / 75.0f), 0, 5);
                switch (idx) {
                    case 0: wifiToggle_ = !wifiToggle_; break;
                    case 1: btToggle_ = !btToggle_; break;
                    case 2: nightLightToggle_ = !nightLightToggle_; break;
                    case 3: highPerfToggle_ = !highPerfToggle_; break;
                    case 4: focusModule_.TogglePomodoro(); break;
                    case 5:
                        quickToolsModule_.EnsureInitialized(hwnd_);
                        activeTab_ = ActiveTab::Rec;
                        break;
                }
                handled = true;
            }
            break;
        }
        case ActiveTab::Audio:
            dragTarget_ = DragTarget::AudioModule;
            handled = micAudioModule_.OnPointerDown(xDip, yDip);
            break;
        case ActiveTab::Focus:
            handled = focusModule_.OnPointerDown(xDip, yDip);
            if (focusModule_.IsTypingTask()) {
                SetNoActivate(false);
                ::SetForegroundWindow(hwnd_);
            } else {
                SetNoActivate(true);
            }
            break;
        case ActiveTab::Rec:
            quickToolsModule_.EnsureInitialized(hwnd_);
            handled = quickToolsModule_.OnPointerDown(xDip, yDip);
            break;
        case ActiveTab::Media:
            handled = mediaModule_.OnPointerDown(xDip, yDip);
            break;
        case ActiveTab::ShelfQr:
            if (qrWifiModule_.IsServerActive()) {
                handled = qrWifiModule_.OnPointerDown(xDip, yDip);
            } else {
                modalDialogOpen_ = true;
                handled = fileShelfModule_.OnPointerDown(xDip, yDip);
                modalDialogOpen_ = false;
            }
            break;
        case ActiveTab::Clipboard:
            handled = clipboardModule_.OnPointerDown(xDip, yDip);
            break;
        case ActiveTab::Settings: {
            if (yDip >= 74.0f && yDip <= 112.0f) {
                const int idx = std::clamp(static_cast<int>((xDip - 28.0f) / 106.0f), 0, 3);
                glassEngine_.SetGlassPreset(static_cast<core::GlassPreset>(idx));
                handled = true;
            } else if (yDip >= 122.0f && yDip <= 220.0f) {
                const bool topRow = (yDip < 170.0f);
                const bool leftCol = (xDip < 240.0f);
                if (topRow && leftCol) {
                    pinned_ = !pinned_;
                } else if (topRow && !leftCol) {
                    leaveCollapseMs_ = (leaveCollapseMs_ <= 60) ? 250 : 45;
                } else if (!topRow && leftCol) {
                    hoverDelayMs_ = (hoverDelayMs_ <= 90) ? 120 : 80;
                } else {
                    gameStripEnabled_ = !gameStripEnabled_;
                }
                handled = true;
            } else if (yDip >= 256.0f && xDip >= 364.0f) {
                ::DestroyWindow(hwnd_);
                return;
            }
            break;
        }
    }
    if (handled) {
        RenderCurrentState();
    }
}

void SlimLineWindow::HandleHubDrag(float xDip, float yDip) {
    bool changed = false;
    if (dragTarget_ == DragTarget::HomeBrightness) {
        // Brightness slider spans x: 28..224
        const float norm = std::clamp((xDip - 28.0f) / (224.0f - 28.0f), 0.05f, 1.0f);
        micAudioModule_.SetSelectedMonitorBrightness(static_cast<int>(norm * 100.0f));
        changed = true;
    } else if (dragTarget_ == DragTarget::HomeVolume) {
        // Volume slider spans x: 256..452
        const float norm = std::clamp((xDip - 256.0f) / (452.0f - 256.0f), 0.0f, 1.0f);
        micAudioModule_.SetOutputVolumeScalar(norm);
        changed = true;
    } else if (dragTarget_ == DragTarget::AudioModule) {
        changed = micAudioModule_.OnPointerDrag(xDip, yDip);
    }
    if (changed) {
        RenderCurrentState();
    }
}

LRESULT CALLBACK SlimLineWindow::MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SlimLineWindow* self = reinterpret_cast<SlimLineWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SlimLineWindow*>(cs->lpCreateParams);
        if (self) {
            self->hwnd_ = hwnd;
        }
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMainMessage(hwnd, msg, wParam, lParam) : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SlimLineWindow::HitTestWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SlimLineWindow* self = reinterpret_cast<SlimLineWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SlimLineWindow*>(cs->lpCreateParams);
        if (self) {
            self->hitTestHwnd_ = hwnd;
        }
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleHitTestMessage(hwnd, msg, wParam, lParam) : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SlimLineWindow::HandleHitTestMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            if (!cursorInProximity_) {
                cursorInProximity_ = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hitTestHwnd_, 0};
                ::TrackMouseEvent(&tme);
                ::SetCoalescableTimer(hwnd_, core::TIMER_HOVER_EXPAND, hoverDelayMs_, nullptr, TIMERV_DEFAULT_COALESCING);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            ::KillTimer(hwnd_, core::TIMER_HOVER_EXPAND);
            if (state_ == PillState::CollapsedIdle) {
                BeginTransition(PillState::ExpandedHub);
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            cursorInProximity_ = false;
            ::KillTimer(hwnd_, core::TIMER_HOVER_EXPAND);
            return 0;
        }
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT SlimLineWindow::HandleMainMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            startupShowcaseActive_ = false;
            ::KillTimer(hwnd_, core::TIMER_LEAVE_COLLAPSE);
            if (!trackingLeave_) {
                trackingLeave_ = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                ::TrackMouseEvent(&tme);
            }
            if ((wParam & MK_LBUTTON) && dragTarget_ != DragTarget::None) {
                const float xDip = static_cast<float>(GET_X_LPARAM(lParam)) / dpiScale_;
                const float yDip = static_cast<float>(GET_Y_LPARAM(lParam)) / dpiScale_;
                HandleHubDrag(xDip, yDip);
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            trackingLeave_ = false;
            startupShowcaseActive_ = false;
            if (dragTarget_ == DragTarget::None && !pinned_ && !modalDialogOpen_ &&
                !focusModule_.IsTypingTask() && !fileShelfModule_.IsDraggingOver()) {
                ::SetCoalescableTimer(hwnd_, core::TIMER_LEAVE_COLLAPSE, leaveCollapseMs_, nullptr, TIMERV_DEFAULT_COALESCING);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const float xDip = static_cast<float>(GET_X_LPARAM(lParam)) / dpiScale_;
            const float yDip = static_cast<float>(GET_Y_LPARAM(lParam)) / dpiScale_;
            HandleHubClick(xDip, yDip);
            if (dragTarget_ != DragTarget::None) {
                ::SetCapture(hwnd_);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (dragTarget_ != DragTarget::None) {
                dragTarget_ = DragTarget::None;
                micAudioModule_.OnPointerUp();
                ::ReleaseCapture();
            }
            return 0;
        }
        case WM_CHAR: {
            if (focusModule_.HandleChar(static_cast<wchar_t>(wParam))) {
                if (!focusModule_.IsTypingTask()) SetNoActivate(true);
                RenderCurrentState();
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (focusModule_.HandleKeyDown(wParam)) {
                SetNoActivate(true);
                RenderCurrentState();
            }
            return 0;
        }
        case WM_HOTKEY: {
            if (wParam == core::HOTKEY_GLOBAL_MIC_MUTE) {
                micAudioModule_.ToggleGlobalMicMute();
                ::SetCoalescableTimer(hwnd_, core::TIMER_DEBOUNCE_REDRAW, core::GeometryConfig::kEventDebounceMs, nullptr, TIMERV_DEFAULT_COALESCING);
            } else if (wParam == core::HOTKEY_TOGGLE_NOTCH) {
                if (state_ == PillState::CollapsedIdle) {
                    pinned_ = true;
                    BeginTransition(PillState::ExpandedHub);
                } else {
                    pinned_ = false;
                    BeginTransition(PillState::CollapsedIdle);
                }
            }
            return 0;
        }
        case WM_CLIPBOARDUPDATE: {
            clipboardModule_.OnClipboardUpdate();
            if (state_ == PillState::ExpandedHub && activeTab_ == ActiveTab::Clipboard) {
                RenderCurrentState();
            }
            return 0;
        }
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED: {
            RefreshMonitorMetrics();
            micAudioModule_.RefreshMonitorTargets();
            ApplyWindowBounds(currentWidthDip_, currentHeightDip_);
            RenderCurrentState();
            return 0;
        }
        case WM_POWERBROADCAST: {
            SYSTEM_POWER_STATUS sps{};
            if (::GetSystemPowerStatus(&sps)) {
                micAudioModule_.OnSystemPowerOrBrightnessChanged(sps.BatteryLifePercent, sps.ACLineStatus == 1, micAudioModule_.GetBrightnessPct());
            }
            return TRUE;
        }
        case core::WM_APP_MEDIA_UPDATED: {
            mediaModule_.RefreshFromWinRT();
            ::SetCoalescableTimer(hwnd_, core::TIMER_DEBOUNCE_REDRAW, core::GeometryConfig::kEventDebounceMs, nullptr, TIMERV_DEFAULT_COALESCING);
            return 0;
        }
        case core::WM_APP_AUDIO_UPDATED:
        case core::WM_APP_MIC_SESSION_UPDATE: {
            if (micAudioModule_.IsCallActive()) {
                ::SetCoalescableTimer(hwnd_, core::TIMER_CALL_ELAPSED, 1000, nullptr, TIMERV_DEFAULT_COALESCING);
            } else {
                ::KillTimer(hwnd_, core::TIMER_CALL_ELAPSED);
            }
            ::SetCoalescableTimer(hwnd_, core::TIMER_DEBOUNCE_REDRAW, core::GeometryConfig::kEventDebounceMs, nullptr, TIMERV_DEFAULT_COALESCING);
            return 0;
        }
        case core::WM_APP_DROPPED_FILES: {
            activeTab_ = ActiveTab::ShelfQr;
            if (state_ == PillState::CollapsedIdle) {
                BeginTransition(PillState::ExpandedHub);
            } else {
                RenderCurrentState();
            }
            return 0;
        }
        case core::WM_APP_WIFI_TRANSFER_DONE: {
            ::KillTimer(hwnd_, core::TIMER_WIFI_TIMEOUT);
            qrWifiModule_.StopTransfer();
            RenderCurrentState();
            return 0;
        }
        case core::WM_APP_NET_IF_CHANGED: {
            qrWifiModule_.RefreshCachedLanIp();
            return 0;
        }
        case core::WM_APP_TOGGLE_NOTCH: {
            if (wParam == 1 || state_ == PillState::CollapsedIdle) {
                pinned_ = true;
                BeginTransition(PillState::ExpandedHub);
            } else {
                pinned_ = false;
                BeginTransition(PillState::CollapsedIdle);
            }
            return 0;
        }
        case WM_TIMER: {
            switch (wParam) {
                case core::TIMER_SPRING_ANIM:
                    StepSpringAnimation();
                    break;
                case core::TIMER_HOVER_EXPAND:
                    ::KillTimer(hwnd_, core::TIMER_HOVER_EXPAND);
                    if (state_ == PillState::CollapsedIdle) {
                        BeginTransition(PillState::ExpandedHub);
                    }
                    break;
                case core::TIMER_LEAVE_COLLAPSE: {
                    ::KillTimer(hwnd_, core::TIMER_LEAVE_COLLAPSE);
                    startupShowcaseActive_ = false;
                    if ((state_ == PillState::ExpandedHub || state_ == PillState::Expanding) &&
                        dragTarget_ == DragTarget::None && !pinned_ && !modalDialogOpen_ &&
                        !focusModule_.IsTypingTask() && !fileShelfModule_.IsDraggingOver()) {
                        POINT pt{};
                        ::GetCursorPos(&pt);
                        RECT wr{};
                        ::GetWindowRect(hwnd_, &wr);
                        if (!::PtInRect(&wr, pt)) {
                            BeginTransition(PillState::CollapsedIdle);
                        }
                    }
                    break;
                }
                case core::TIMER_DEBOUNCE_REDRAW:
                    ::KillTimer(hwnd_, core::TIMER_DEBOUNCE_REDRAW);
                    if (state_ != PillState::CollapsedIdle) {
                        RenderCurrentState();
                    }
                    break;
                case core::TIMER_GAME_TELEMETRY:
                    if (state_ == PillState::GameStrip) {
                        quickToolsModule_.SampleGameTelemetry1Hz();
                        RenderCurrentState();
                    }
                    break;
                case core::TIMER_CALL_ELAPSED:
                    micAudioModule_.TickCallTimer();
                    if (state_ == PillState::ExpandedHub && activeTab_ == ActiveTab::Home) {
                        RenderCurrentState();
                    }
                    break;
                case core::TIMER_POMODORO_TICK:
                    focusModule_.Tick1Hz();
                    if (state_ == PillState::CollapsedIdle) {
                        ApplyCollapsedIdleBounds();
                        RenderCurrentState();
                    } else if (state_ == PillState::ExpandedHub && (activeTab_ == ActiveTab::Focus || activeTab_ == ActiveTab::Home)) {
                        RenderCurrentState();
                    }
                    break;
                case core::TIMER_REC_TICK:
                    if (quickToolsModule_.IsRecording()) {
                        quickToolsModule_.TickRecording1Hz();
                    } else {
                        ::KillTimer(hwnd_, core::TIMER_REC_TICK);
                    }
                    if (state_ == PillState::CollapsedIdle) {
                        ApplyCollapsedIdleBounds();
                        RenderCurrentState();
                    } else if (state_ == PillState::ExpandedHub && (activeTab_ == ActiveTab::Rec || activeTab_ == ActiveTab::Home)) {
                        RenderCurrentState();
                    }
                    break;
                case core::TIMER_WIFI_TIMEOUT:
                    ::KillTimer(hwnd_, core::TIMER_WIFI_TIMEOUT);
                    qrWifiModule_.StopTransfer();
                    RenderCurrentState();
                    break;
                case core::TIMER_FULLSCREEN_CHK:
                    CheckFullscreenGameState();
                    break;
            }
            return 0;
        }
        case core::WM_APP_TRAY_ICON: {
            const UINT eventMsg = LOWORD(lParam);
            if (eventMsg == WM_LBUTTONUP || eventMsg == WM_LBUTTONDBLCLK) {
                if (state_ == PillState::CollapsedIdle) {
                    pinned_ = true;
                    BeginTransition(PillState::ExpandedHub);
                } else {
                    pinned_ = false;
                    BeginTransition(PillState::CollapsedIdle);
                }
            } else if (eventMsg == WM_RBUTTONUP || eventMsg == WM_CONTEXTMENU) {
                ShowTrayContextMenu();
            }
            return 0;
        }
        case WM_CLOSE:
            ::DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            RemoveTrayIcon();
            if (hwnd_) ::RevokeDragDrop(hwnd_);
            if (hitTestHwnd_) ::RevokeDragDrop(hitTestHwnd_);
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void SlimLineWindow::AddTrayIcon() {
    if (trayAdded_ || !hwnd_) return;
    HICON icon = static_cast<HICON>(::LoadImageW(
        hInstance_, MAKEINTRESOURCEW(core::IDI_NOTCHGLASS),
        IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED
    ));
    if (!icon) {
        icon = ::LoadIconW(nullptr, IDI_APPLICATION);
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = core::WM_APP_TRAY_ICON;
    nid.hIcon = icon;
    ::lstrcpynW(nid.szTip, L"NotchGlass - Click to toggle (Ctrl+Alt+N)", ARRAYSIZE(nid.szTip));
    if (::Shell_NotifyIconW(NIM_ADD, &nid)) {
        trayAdded_ = true;
    }
}

void SlimLineWindow::RemoveTrayIcon() {
    if (!trayAdded_ || !hwnd_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    ::Shell_NotifyIconW(NIM_DELETE, &nid);
    trayAdded_ = false;
}

void SlimLineWindow::ShowTrayContextMenu() {
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;
    ::AppendMenuW(menu, MF_STRING, 1001, (state_ == PillState::CollapsedIdle) ? L"Open NotchGlass" : L"Collapse NotchGlass");
    ::AppendMenuW(menu, MF_STRING, 1002, pinned_ ? L"Unpin Notch" : L"Pin Notch Open");
    ::AppendMenuW(menu, MF_STRING, 1003, L"Cycle Glass Theme");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, 1004, L"Exit NotchGlass");

    POINT pt{};
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(hwnd_);
    const UINT cmd = static_cast<UINT>(::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr));
    ::DestroyMenu(menu);

    switch (cmd) {
        case 1001:
            if (state_ == PillState::CollapsedIdle) {
                pinned_ = true;
                BeginTransition(PillState::ExpandedHub);
            } else {
                pinned_ = false;
                BeginTransition(PillState::CollapsedIdle);
            }
            break;
        case 1002:
            pinned_ = !pinned_;
            if (pinned_ && state_ == PillState::CollapsedIdle) {
                BeginTransition(PillState::ExpandedHub);
            } else {
                RenderCurrentState();
            }
            break;
        case 1003: {
            const int next = (static_cast<int>(glassEngine_.GetGlassPreset()) + 1) % 4;
            glassEngine_.SetGlassPreset(static_cast<core::GlassPreset>(next));
            RenderCurrentState();
            break;
        }
        case 1004:
            ::DestroyWindow(hwnd_);
            break;
    }
}

} // namespace notch::window
