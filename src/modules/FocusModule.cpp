#include "modules/FocusModule.hpp"
#include "core/config.hpp"
#include "graphics/LiquidGlassEngine.hpp"

#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace notch::modules {

void FocusModule::EnsureInitialized(HWND mainHwnd) {
    if (initialized_) return;
    initialized_ = true;
    mainHwnd_ = mainHwnd;
    LoadTodos();
    LoadDailyNote();
}

void FocusModule::OnCollapseRelease() {
    typingTask_ = false;
    typingNote_ = false;
    draftTask_.clear();
    draftNote_.clear();
}

float FocusModule::GetPomoProgress() const noexcept {
    if (pomoTotalSec_ == 0) return 0.0f;
    const float done = static_cast<float>(pomoTotalSec_ - pomoRemainingSec_);
    return std::clamp(done / static_cast<float>(pomoTotalSec_), 0.0f, 1.0f);
}

void FocusModule::SyncTimerState() {
    if (!mainHwnd_) return;
    if (pomoRunning_ || stopwatchRunning_) {
        ::SetCoalescableTimer(mainHwnd_, core::TIMER_POMODORO_TICK, 1000, nullptr, TIMERV_DEFAULT_COALESCING);
    } else {
        ::KillTimer(mainHwnd_, core::TIMER_POMODORO_TICK);
    }
}

void FocusModule::Tick1Hz() {
    if (pomoRunning_) {
        if (pomoRemainingSec_ > 0) {
            --pomoRemainingSec_;
            if (!pomoIsBreak_) {
                ++focusedSecondsToday_;
            }
        }
        if (pomoRemainingSec_ == 0) {
            pomoRunning_ = false;
            if (!pomoIsBreak_) {
                ++completedSessions_;
                pomoIsBreak_ = true;
                pomoTotalSec_ = 5 * 60;
                pomoRemainingSec_ = 5 * 60;
            } else {
                pomoIsBreak_ = false;
                pomoTotalSec_ = 25 * 60;
                pomoRemainingSec_ = 25 * 60;
            }
            ::MessageBeep(MB_ICONINFORMATION);
        }
    }
    if (stopwatchRunning_) {
        ++stopwatchSec_;
    }
    SyncTimerState();
}

void FocusModule::TogglePomodoro() {
    mode_ = FocusSubMode::Pomodoro;
    pomoRunning_ = !pomoRunning_;
    SyncTimerState();
}

void FocusModule::ResetPomodoro() {
    pomoRunning_ = false;
    pomoIsBreak_ = false;
    pomoRemainingSec_ = pomoTotalSec_;
    SyncTimerState();
}

void FocusModule::SetPomodoroMinutes(int mins) {
    pomoRunning_ = false;
    pomoIsBreak_ = (mins <= 5);
    pomoTotalSec_ = static_cast<std::uint32_t>(std::max(1, mins) * 60);
    pomoRemainingSec_ = pomoTotalSec_;
    SyncTimerState();
}

void FocusModule::ToggleStopwatch() {
    mode_ = FocusSubMode::Stopwatch;
    stopwatchRunning_ = !stopwatchRunning_;
    SyncTimerState();
}

void FocusModule::ResetStopwatch() {
    stopwatchRunning_ = false;
    stopwatchSec_ = 0;
    laps_.clear();
    SyncTimerState();
}

void FocusModule::RecordStopwatchLap() {
    if (stopwatchSec_ == 0 && !stopwatchRunning_) return;
    laps_.insert(laps_.begin(), stopwatchSec_);
    if (laps_.size() > 3) laps_.resize(3);
}

std::wstring FocusModule::GetAppDataDir() const {
    wchar_t* localApp = nullptr;
    std::wstring dir;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localApp)) && localApp) {
        dir = std::wstring(localApp) + L"\\NotchGlass";
        ::CoTaskMemFree(localApp);
    } else {
        dir = L".";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void FocusModule::LoadTodos() {
    tasks_.clear();
    std::ifstream in(std::filesystem::path(GetAppDataDir() + L"\\todos.txt"), std::ios::binary);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 4) continue;
            const bool done = (line[0] == '1');
            const bool prio = (line[2] == '1');
            const std::string utf8 = line.substr(4);
            if (utf8.empty()) continue;
            const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
            if (wlen > 0) {
                std::wstring w(static_cast<std::size_t>(wlen), L'\0');
                ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), w.data(), wlen);
                tasks_.push_back({std::move(w), done, prio});
            }
        }
    }
    if (tasks_.empty()) {
        tasks_.push_back({L"Deep work sprint (25 min Pomodoro)", false, true});
        tasks_.push_back({L"Review priorities & clear inbox", false, false});
    }
}

void FocusModule::SaveTodos() const {
    std::ofstream out(std::filesystem::path(GetAppDataDir() + L"\\todos.txt"), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    for (const auto& t : tasks_) {
        const int ulen = ::WideCharToMultiByte(CP_UTF8, 0, t.text.c_str(), static_cast<int>(t.text.size()), nullptr, 0, nullptr, nullptr);
        if (ulen <= 0) continue;
        std::string u(static_cast<std::size_t>(ulen), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, t.text.c_str(), static_cast<int>(t.text.size()), u.data(), ulen, nullptr, nullptr);
        out << (t.done ? '1' : '0') << '|' << (t.priority ? '1' : '0') << '|' << u << '\n';
    }
}

void FocusModule::LoadDailyNote() {
    std::ifstream in(std::filesystem::path(GetAppDataDir() + L"\\daily_note.txt"), std::ios::binary);
    if (!in.is_open()) return;
    std::string utf8((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!utf8.empty() && (utf8.back() == '\r' || utf8.back() == '\n')) utf8.pop_back();
    if (utf8.empty()) return;
    const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen > 0) {
        std::wstring w(static_cast<std::size_t>(wlen), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), w.data(), wlen);
        dailyNote_ = std::move(w);
    }
}

void FocusModule::SaveDailyNote() const {
    std::ofstream out(std::filesystem::path(GetAppDataDir() + L"\\daily_note.txt"), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    const int ulen = ::WideCharToMultiByte(CP_UTF8, 0, dailyNote_.c_str(), static_cast<int>(dailyNote_.size()), nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return;
    std::string u(static_cast<std::size_t>(ulen), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, dailyNote_.c_str(), static_cast<int>(dailyNote_.size()), u.data(), ulen, nullptr, nullptr);
    out << u << '\n';
}

void FocusModule::SetDailyNote(const std::wstring& note) {
    std::wstring clean = note;
    while (!clean.empty() && (clean.back() == L'\r' || clean.back() == L'\n' || clean.back() == L' ')) clean.pop_back();
    while (!clean.empty() && clean.front() == L' ') clean.erase(clean.begin());
    if (clean.empty()) return;
    if (clean.size() > 72) clean.resize(72);
    dailyNote_ = std::move(clean);
    SaveDailyNote();
}

void FocusModule::AddTask(const std::wstring& text) {
    std::wstring clean = text;
    while (!clean.empty() && (clean.back() == L'\r' || clean.back() == L'\n' || clean.back() == L' ')) clean.pop_back();
    while (!clean.empty() && clean.front() == L' ') clean.erase(clean.begin());
    if (clean.empty()) return;
    if (clean.size() > 64) clean.resize(64);
    tasks_.insert(tasks_.begin(), {std::move(clean), false, false});
    if (tasks_.size() > 16) tasks_.resize(16);
    SaveTodos();
}

void FocusModule::AddTaskFromClipboard() {
    if (!::OpenClipboard(mainHwnd_)) return;
    HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* txt = static_cast<const wchar_t*>(::GlobalLock(hData));
        if (txt) {
            AddTask(txt);
            ::GlobalUnlock(hData);
        }
    }
    ::CloseClipboard();
}

bool FocusModule::HandleChar(wchar_t ch) {
    if (typingNote_) {
        if (ch == L'\r') {
            SetDailyNote(draftNote_);
            draftNote_.clear();
            typingNote_ = false;
            return true;
        }
        if (ch == 27) {
            draftNote_.clear();
            typingNote_ = false;
            return true;
        }
        if (ch == L'\b') {
            if (!draftNote_.empty()) draftNote_.pop_back();
            return true;
        }
        if (ch >= 32 && draftNote_.size() < 72) {
            draftNote_.push_back(ch);
            return true;
        }
        return false;
    }

    if (!typingTask_) return false;
    if (ch == L'\r') {
        AddTask(draftTask_);
        draftTask_.clear();
        typingTask_ = false;
        return true;
    }
    if (ch == 27) { // Esc
        draftTask_.clear();
        typingTask_ = false;
        return true;
    }
    if (ch == L'\b') {
        if (!draftTask_.empty()) draftTask_.pop_back();
        return true;
    }
    if (ch >= 32 && draftTask_.size() < 48) {
        draftTask_.push_back(ch);
        return true;
    }
    return false;
}

bool FocusModule::HandleKeyDown(WPARAM vk) {
    if (!typingTask_ && !typingNote_) return false;
    if (vk == VK_ESCAPE) {
        draftTask_.clear();
        draftNote_.clear();
        typingTask_ = false;
        typingNote_ = false;
        return true;
    }
    return false;
}

void FocusModule::Render(const RenderContext& ctx) {
    auto* dc = ctx.dc;
    auto* dw = ctx.dwrite;

    core::ComPtr<ID2D1SolidColorBrush> textBrush;
    core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
    core::ComPtr<ID2D1SolidColorBrush> accentBrush;
    core::ComPtr<ID2D1SolidColorBrush> greenBrush;
    core::ComPtr<ID2D1SolidColorBrush> amberBrush;
    core::ComPtr<ID2D1SolidColorBrush> trackBg;
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.84f, 0.90f, 0.98f, 0.78f), &mutedBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.36f, 0.82f, 1.00f, 0.95f), &accentBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.88f, 0.56f, 0.95f), &greenBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.78f, 0.30f, 0.95f), &amberBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.16f), &trackBg);

    core::ComPtr<IDWriteTextFormat> bigFmt;
    core::ComPtr<IDWriteTextFormat> titleFmt;
    core::ComPtr<IDWriteTextFormat> smallFmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"en-us", &bigFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &titleFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &smallFmt);

    const float leftW = 192.0f;
    D2D1_RECT_F timerCard = D2D1::RectF(ctx.boundsDip.left, ctx.boundsDip.top + 4.0f, ctx.boundsDip.left + leftW, ctx.boundsDip.bottom);
    D2D1_RECT_F todoCard  = D2D1::RectF(timerCard.right + 10.0f, ctx.boundsDip.top + 4.0f, ctx.boundsDip.right, ctx.boundsDip.bottom);

    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, timerCard, 15.0f, pomoRunning_ || stopwatchRunning_, false);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, todoCard, 15.0f, false, false);

    // 3 Sub-mode switcher pills: [Timer] [Watch] [Chart]
    D2D1_RECT_F pTab = D2D1::RectF(timerCard.left + 8.0f, timerCard.top + 8.0f, timerCard.left + 63.0f, timerCard.top + 30.0f);
    D2D1_RECT_F sTab = D2D1::RectF(timerCard.left + 67.0f, timerCard.top + 8.0f, timerCard.left + 122.0f, timerCard.top + 30.0f);
    D2D1_RECT_F aTab = D2D1::RectF(timerCard.left + 126.0f, timerCard.top + 8.0f, timerCard.right - 8.0f, timerCard.top + 30.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, pTab, 10.0f, mode_ == FocusSubMode::Pomodoro, true);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, sTab, 10.0f, mode_ == FocusSubMode::Stopwatch, true);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, aTab, 10.0f, mode_ == FocusSubMode::Streaks, true);

    smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    smallFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextW(L"Timer", 5, smallFmt.Get(), pTab, textBrush.Get());
    dc->DrawTextW(L"Watch", 5, smallFmt.Get(), sTab, textBrush.Get());
    dc->DrawTextW(L"Chart", 5, smallFmt.Get(), aTab, textBrush.Get());

    bigFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    bigFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    std::size_t doneCount = 0;
    for (const auto& t : tasks_) {
        if (t.done) ++doneCount;
    }
    const std::size_t totalTasks = tasks_.size();
    const std::size_t pendingCount = totalTasks - doneCount;
    const int donePct = totalTasks > 0 ? static_cast<int>((doneCount * 100u) / totalTasks) : 0;

    if (mode_ == FocusSubMode::Pomodoro) {
        wchar_t tBuf[32];
        std::swprintf(tBuf, 32, L"%02u:%02u", pomoRemainingSec_ / 60, pomoRemainingSec_ % 60);
        dc->DrawTextW(tBuf, static_cast<UINT32>(wcslen(tBuf)), bigFmt.Get(),
            D2D1::RectF(timerCard.left + 8.0f, timerCard.top + 40.0f, timerCard.right - 8.0f, timerCard.top + 80.0f),
            pomoRunning_ ? greenBrush.Get() : textBrush.Get());

        const float barL = timerCard.left + 14.0f;
        const float barR = timerCard.right - 14.0f;
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barL, timerCard.top + 88.0f, barR, timerCard.top + 93.0f), 2.5f, 2.5f), trackBg.Get());
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barL, timerCard.top + 88.0f, barL + (barR - barL) * GetPomoProgress(), timerCard.top + 93.0f), 2.5f, 2.5f), accentBrush.Get());

        const int mins[4] = {15, 25, 50, 5};
        const wchar_t* mLabels[4] = {L"15m", L"25m", L"50m", L"5m"};
        const float pw = (leftW - 28.0f) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            D2D1_RECT_F mb = D2D1::RectF(timerCard.left + 8.0f + i * (pw + 4.0f), timerCard.top + 104.0f, timerCard.left + 8.0f + i * (pw + 4.0f) + pw, timerCard.top + 128.0f);
            const bool activePreset = (pomoTotalSec_ == static_cast<std::uint32_t>(mins[i] * 60));
            graphics::LiquidGlassEngine::DrawGlassCapsule(dc, mb, 8.0f, activePreset, true);
            dc->DrawTextW(mLabels[i], static_cast<UINT32>(wcslen(mLabels[i])), smallFmt.Get(), mb, textBrush.Get());
        }

        D2D1_RECT_F startBtn = D2D1::RectF(timerCard.left + 8.0f, timerCard.top + 140.0f, timerCard.left + 114.0f, timerCard.top + 168.0f);
        D2D1_RECT_F resetBtn = D2D1::RectF(timerCard.left + 118.0f, timerCard.top + 140.0f, timerCard.right - 8.0f, timerCard.top + 168.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, startBtn, 10.0f, pomoRunning_, true);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, resetBtn, 10.0f, false, true);
        const wchar_t* sLbl = pomoRunning_ ? L"Pause" : L"Start Focus";
        dc->DrawTextW(sLbl, static_cast<UINT32>(wcslen(sLbl)), smallFmt.Get(), startBtn, textBrush.Get());
        dc->DrawTextW(L"Reset", 5, smallFmt.Get(), resetBtn, mutedBrush.Get());

        wchar_t statBuf[48];
        std::swprintf(statBuf, 48, L"%u sprints · %d%% tasks done", completedSessions_, donePct);
        dc->DrawTextW(statBuf, static_cast<UINT32>(wcslen(statBuf)), smallFmt.Get(),
            D2D1::RectF(timerCard.left + 8.0f, timerCard.bottom - 34.0f, timerCard.right - 8.0f, timerCard.bottom - 8.0f), mutedBrush.Get());
    } else if (mode_ == FocusSubMode::Stopwatch) {
        wchar_t sBuf[32];
        const UINT hh = stopwatchSec_ / 3600;
        const UINT mm = (stopwatchSec_ % 3600) / 60;
        const UINT ss = stopwatchSec_ % 60;
        if (hh > 0) std::swprintf(sBuf, 32, L"%u:%02u:%02u", hh, mm, ss);
        else std::swprintf(sBuf, 32, L"%02u:%02u", mm, ss);

        dc->DrawTextW(sBuf, static_cast<UINT32>(wcslen(sBuf)), bigFmt.Get(),
            D2D1::RectF(timerCard.left + 8.0f, timerCard.top + 40.0f, timerCard.right - 8.0f, timerCard.top + 82.0f),
            stopwatchRunning_ ? accentBrush.Get() : textBrush.Get());

        D2D1_RECT_F swStart = D2D1::RectF(timerCard.left + 8.0f, timerCard.top + 92.0f, timerCard.left + 72.0f, timerCard.top + 120.0f);
        D2D1_RECT_F swLap   = D2D1::RectF(timerCard.left + 76.0f, timerCard.top + 92.0f, timerCard.left + 128.0f, timerCard.top + 120.0f);
        D2D1_RECT_F swReset = D2D1::RectF(timerCard.left + 132.0f, timerCard.top + 92.0f, timerCard.right - 8.0f, timerCard.top + 120.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, swStart, 10.0f, stopwatchRunning_, true);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, swLap, 10.0f, false, true);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, swReset, 10.0f, false, true);
        dc->DrawTextW(stopwatchRunning_ ? L"Pause" : L"Start", 5, smallFmt.Get(), swStart, textBrush.Get());
        dc->DrawTextW(L"Lap", 3, smallFmt.Get(), swLap, textBrush.Get());
        dc->DrawTextW(L"Reset", 5, smallFmt.Get(), swReset, mutedBrush.Get());

        smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        float ly = timerCard.top + 130.0f;
        if (laps_.empty()) {
            dc->DrawTextW(L"Tap Start for Dynamic Island live pill", 38, smallFmt.Get(),
                D2D1::RectF(timerCard.left + 10.0f, ly, timerCard.right - 10.0f, ly + 36.0f), mutedBrush.Get());
        } else {
            for (std::size_t i = 0; i < laps_.size(); ++i) {
                wchar_t lBuf[32];
                std::swprintf(lBuf, 32, L"Lap %zu   %02u:%02u", laps_.size() - i, laps_[i] / 60, laps_[i] % 60);
                dc->DrawTextW(lBuf, static_cast<UINT32>(wcslen(lBuf)), smallFmt.Get(),
                    D2D1::RectF(timerCard.left + 12.0f, ly, timerCard.right - 10.0f, ly + 20.0f), textBrush.Get());
                ly += 22.0f;
            }
        }
    } else {
        // Work Completion Donut Chart (Done vs Not Done) + Sprint Controls
        const float cx = timerCard.left + 48.0f;
        const float cy = timerCard.top + 82.0f;
        const float rad = 28.0f;
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), trackBg.Get(), 8.0f);

        if (donePct > 0) {
            core::ComPtr<ID2D1Factory> factory;
            dc->GetFactory(&factory);
            core::ComPtr<ID2D1PathGeometry> arcGeo;
            if (factory && SUCCEEDED(factory->CreatePathGeometry(&arcGeo)) && arcGeo) {
                core::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(arcGeo->Open(&sink)) && sink) {
                    const float frac = std::clamp(static_cast<float>(donePct) / 100.0f, 0.01f, 0.999f);
                    const float angle = frac * 6.2831853f - 1.5707963f;
                    D2D1_POINT_2F startPt = D2D1::Point2F(cx, cy - rad);
                    D2D1_POINT_2F endPt = D2D1::Point2F(cx + rad * std::cos(angle), cy + rad * std::sin(angle));
                    sink->BeginFigure(startPt, D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddArc(D2D1::ArcSegment(
                        endPt, D2D1::SizeF(rad, rad), 0.0f,
                        D2D1_SWEEP_DIRECTION_CLOCKWISE,
                        (frac > 0.5f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
                    ));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    dc->DrawGeometry(arcGeo.Get(), greenBrush.Get(), 8.0f);
                }
            }
        }

        wchar_t pctBuf[16];
        std::swprintf(pctBuf, 16, L"%d%%", donePct);
        dc->DrawTextW(pctBuf, static_cast<UINT32>(wcslen(pctBuf)), titleFmt.Get(),
            D2D1::RectF(cx - 24.0f, cy - 12.0f, cx + 24.0f, cy + 12.0f), textBrush.Get());

        smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        wchar_t doneBuf[32], pendBuf[32];
        std::swprintf(doneBuf, 32, L"%zu Completed", doneCount);
        std::swprintf(pendBuf, 32, L"%zu Pending", pendingCount);
        dc->DrawTextW(doneBuf, static_cast<UINT32>(wcslen(doneBuf)), smallFmt.Get(),
            D2D1::RectF(cx + 38.0f, cy - 20.0f, timerCard.right - 8.0f, cy - 2.0f), greenBrush.Get());
        dc->DrawTextW(pendBuf, static_cast<UINT32>(wcslen(pendBuf)), smallFmt.Get(),
            D2D1::RectF(cx + 38.0f, cy + 2.0f, timerCard.right - 8.0f, cy + 20.0f), amberBrush.Get());

        // Horizontal comparison bar
        const float bL = timerCard.left + 12.0f;
        const float bR = timerCard.right - 12.0f;
        const float splitX = bL + (bR - bL) * (totalTasks > 0 ? static_cast<float>(doneCount) / static_cast<float>(totalTasks) : 0.0f);
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bL, timerCard.top + 128.0f, bR, timerCard.top + 135.0f), 3.5f, 3.5f), amberBrush.Get());
        if (doneCount > 0) {
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bL, timerCard.top + 128.0f, splitX, timerCard.top + 135.0f), 3.5f, 3.5f), greenBrush.Get());
        }

        D2D1_RECT_F logBtn  = D2D1::RectF(timerCard.left + 10.0f, timerCard.top + 152.0f, timerCard.right - 10.0f, timerCard.top + 180.0f);
        D2D1_RECT_F noteBtn = D2D1::RectF(timerCard.left + 10.0f, timerCard.top + 188.0f, timerCard.right - 10.0f, timerCard.top + 216.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, logBtn, 10.0f, true, true);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, noteBtn, 10.0f, typingNote_, true);

        smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        dc->DrawTextW(L"+ Log 25m Sprint", 16, smallFmt.Get(), logBtn, textBrush.Get());
        dc->DrawTextW(typingNote_ ? L"Save Note (Enter)" : L"Edit Morning Note",
            typingNote_ ? 17 : 17, smallFmt.Get(), noteBtn, amberBrush.Get());

        wchar_t sBuf[48];
        std::swprintf(sBuf, 48, L"%u sprints · %u min focus", completedSessions_, GetFocusedMinutesToday());
        dc->DrawTextW(sBuf, static_cast<UINT32>(wcslen(sBuf)), smallFmt.Get(),
            D2D1::RectF(timerCard.left + 8.0f, timerCard.bottom - 30.0f, timerCard.right - 8.0f, timerCard.bottom - 8.0f), mutedBrush.Get());
    }

    // Right Column: Morning Motivation Note + Persistent To-Do List
    titleFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    titleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextW(L"To-Do List", 10, titleFmt.Get(),
        D2D1::RectF(todoCard.left + 12.0f, todoCard.top + 8.0f, todoCard.left + 100.0f, todoCard.top + 30.0f), textBrush.Get());

    D2D1_RECT_F typeBtn  = D2D1::RectF(todoCard.right - 134.0f, todoCard.top + 8.0f, todoCard.right - 74.0f, todoCard.top + 28.0f);
    D2D1_RECT_F pasteBtn = D2D1::RectF(todoCard.right - 70.0f, todoCard.top + 8.0f, todoCard.right - 8.0f, todoCard.top + 28.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, typeBtn, 9.0f, typingTask_, true);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, pasteBtn, 9.0f, false, true);
    smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    dc->DrawTextW(typingTask_ ? L"Enter..." : L"+ Type", typingTask_ ? 8 : 6, smallFmt.Get(), typeBtn, textBrush.Get());
    dc->DrawTextW(L"+ Paste", 7, smallFmt.Get(), pasteBtn, accentBrush.Get());

    // Morning Motivation Note Capsule (y: todoCard.top + 34 .. +64)
    D2D1_RECT_F noteBox = D2D1::RectF(todoCard.left + 8.0f, todoCard.top + 34.0f, todoCard.right - 8.0f, todoCard.top + 64.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, noteBox, 10.0f, typingNote_, true);
    smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    std::wstring noteDisp = typingNote_
        ? (draftNote_.empty() ? L"Type morning note & press Enter..." : (draftNote_ + L"|"))
        : (L"Note: " + dailyNote_);
    dc->DrawTextW(noteDisp.c_str(), static_cast<UINT32>(noteDisp.size()), smallFmt.Get(),
        D2D1::RectF(noteBox.left + 10.0f, noteBox.top, noteBox.right - 10.0f, noteBox.bottom),
        typingNote_ ? textBrush.Get() : amberBrush.Get());

    float rowY = todoCard.top + 70.0f;
    if (typingTask_) {
        D2D1_RECT_F inputBox = D2D1::RectF(todoCard.left + 8.0f, rowY, todoCard.right - 8.0f, rowY + 28.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, inputBox, 9.0f, true, true);
        std::wstring disp = draftTask_.empty() ? L"Type task & press Enter..." : (draftTask_ + L"|");
        dc->DrawTextW(disp.c_str(), static_cast<UINT32>(disp.size()), smallFmt.Get(),
            D2D1::RectF(inputBox.left + 10.0f, inputBox.top, inputBox.right - 10.0f, inputBox.bottom),
            draftTask_.empty() ? mutedBrush.Get() : textBrush.Get());
        rowY += 34.0f;
    }

    const std::size_t maxRows = typingTask_ ? 4 : 5;
    for (std::size_t i = todoScroll_; i < tasks_.size() && (i - todoScroll_) < maxRows; ++i) {
        if (rowY + 30.0f > todoCard.bottom - 4.0f) break;
        D2D1_RECT_F r = D2D1::RectF(todoCard.left + 8.0f, rowY, todoCard.right - 8.0f, rowY + 30.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, r, 10.0f, tasks_[i].priority && !tasks_[i].done, true);

        const float cx = r.left + 15.0f;
        const float cy = (r.top + r.bottom) * 0.5f;
        if (tasks_[i].done) {
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.0f, 6.0f), greenBrush.Get());
            dc->DrawLine(D2D1::Point2F(cx - 2.8f, cy), D2D1::Point2F(cx - 0.5f, cy + 2.4f), textBrush.Get(), 1.5f);
            dc->DrawLine(D2D1::Point2F(cx - 0.5f, cy + 2.4f), D2D1::Point2F(cx + 3.2f, cy - 2.2f), textBrush.Get(), 1.5f);
        } else {
            dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.0f, 6.0f), tasks_[i].priority ? amberBrush.Get() : mutedBrush.Get(), 1.4f);
        }

        dc->DrawTextW(tasks_[i].text.c_str(), static_cast<UINT32>(tasks_[i].text.size()), smallFmt.Get(),
            D2D1::RectF(r.left + 28.0f, r.top, r.right - 28.0f, r.bottom),
            tasks_[i].done ? mutedBrush.Get() : textBrush.Get());

        const float dx = r.right - 14.0f;
        dc->DrawLine(D2D1::Point2F(dx - 3.0f, cy - 3.0f), D2D1::Point2F(dx + 3.0f, cy + 3.0f), mutedBrush.Get(), 1.3f);
        dc->DrawLine(D2D1::Point2F(dx + 3.0f, cy - 3.0f), D2D1::Point2F(dx - 3.0f, cy + 3.0f), mutedBrush.Get(), 1.3f);
        rowY += 35.0f;
    }
}

bool FocusModule::OnPointerDown(float xDip, float yDip) {
    // Left card: x: 16..208
    if (xDip <= 212.0f) {
        if (yDip >= 50.0f && yDip <= 78.0f) {
            if (xDip < 81.0f) mode_ = FocusSubMode::Pomodoro;
            else if (xDip < 140.0f) mode_ = FocusSubMode::Stopwatch;
            else mode_ = FocusSubMode::Streaks;
            return true;
        }
        if (mode_ == FocusSubMode::Pomodoro) {
            if (yDip >= 146.0f && yDip <= 178.0f) {
                const int idx = std::clamp(static_cast<int>((xDip - 24.0f) / 45.0f), 0, 3);
                const int mins[4] = {15, 25, 50, 5};
                SetPomodoroMinutes(mins[idx]);
                return true;
            }
            if (yDip >= 182.0f && yDip <= 220.0f) {
                if (xDip < 132.0f) TogglePomodoro();
                else ResetPomodoro();
                return true;
            }
        } else if (mode_ == FocusSubMode::Stopwatch) {
            if (yDip >= 134.0f && yDip <= 172.0f) {
                if (xDip < 90.0f) ToggleStopwatch();
                else if (xDip < 146.0f) RecordStopwatchLap();
                else ResetStopwatch();
                return true;
            }
        } else {
            // Streaks / Chart mode buttons
            if (yDip >= 194.0f && yDip <= 228.0f) {
                ++completedSessions_;
                focusedSecondsToday_ += 25 * 60;
                return true;
            }
            if (yDip >= 230.0f && yDip <= 264.0f) {
                typingTask_ = false;
                typingNote_ = !typingNote_;
                draftNote_ = typingNote_ ? dailyNote_ : L"";
                return true;
            }
        }
        return false;
    }

    // Right card (Note + To-Do List): x: 218..464
    if (yDip >= 48.0f && yDip <= 78.0f) {
        if (xDip >= 324.0f && xDip < 392.0f) {
            typingNote_ = false;
            typingTask_ = !typingTask_;
            if (!typingTask_) draftTask_.clear();
            return true;
        }
        if (xDip >= 392.0f) {
            AddTaskFromClipboard();
            return true;
        }
    }

    // Morning Motivation Note Capsule (y: 80..110)
    if (yDip >= 79.0f && yDip <= 112.0f) {
        typingTask_ = false;
        typingNote_ = !typingNote_;
        draftNote_ = typingNote_ ? dailyNote_ : L"";
        return true;
    }

    const float startY = typingTask_ ? 150.0f : 116.0f;
    if (yDip >= startY) {
        const std::size_t row = static_cast<std::size_t>((yDip - startY) / 35.0f);
        const std::size_t idx = todoScroll_ + row;
        if (idx < tasks_.size()) {
            if (xDip >= 434.0f) {
                tasks_.erase(tasks_.begin() + static_cast<std::ptrdiff_t>(idx));
            } else {
                tasks_[idx].done = !tasks_[idx].done;
            }
            SaveTodos();
            return true;
        }
    }
    return false;
}

} // namespace notch::modules
