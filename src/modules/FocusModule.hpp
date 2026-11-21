#pragma once

#include "modules/IModule.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace notch::modules {

struct TodoTaskItem {
    std::wstring text;
    bool done = false;
    bool priority = false;
};

enum class FocusSubMode : std::uint8_t {
    Pomodoro  = 0,
    Stopwatch = 1,
    Streaks   = 2
};

class FocusModule final : public IModule {
public:
    void EnsureInitialized(HWND mainHwnd) override;
    void OnCollapseRelease() override;
    void Render(const RenderContext& ctx) override;
    bool OnPointerDown(float xDip, float yDip) override;

    // 1 Hz timer tick (active ONLY while Pomodoro or Stopwatch is running)
    void Tick1Hz();

    // Keyboard input forwarding while typing a new task or morning motivation note
    bool IsTypingTask() const noexcept { return typingTask_ || typingNote_; }
    void CancelTypingTask() noexcept {
        typingTask_ = false;
        typingNote_ = false;
        draftTask_.clear();
        draftNote_.clear();
    }
    bool HandleChar(wchar_t ch);
    bool HandleKeyDown(WPARAM vk);

    // Pomodoro & Stopwatch Controls
    void TogglePomodoro();
    void ResetPomodoro();
    void SetPomodoroMinutes(int mins);
    void ToggleStopwatch();
    void ResetStopwatch();
    void RecordStopwatchLap();

    // Dynamic-Island Live Activity Pill state when collapsed
    bool HasLiveActivityPill() const noexcept { return pomoRunning_ || stopwatchRunning_; }
    bool IsPomodoroRunning() const noexcept { return pomoRunning_; }
    bool IsStopwatchRunning() const noexcept { return stopwatchRunning_; }
    std::uint32_t GetPomoRemainingSec() const noexcept { return pomoRemainingSec_; }
    std::uint32_t GetStopwatchElapsedSec() const noexcept { return stopwatchSec_; }
    float GetPomoProgress() const noexcept;
    std::uint32_t GetCompletedSessions() const noexcept { return completedSessions_; }
    std::uint32_t GetFocusedMinutesToday() const noexcept { return focusedSecondsToday_ / 60; }

    // To-Do & Daily Motivation Note Helpers
    void AddTask(const std::wstring& text);
    void AddTaskFromClipboard();
    void SetDailyNote(const std::wstring& note);
    const std::wstring& GetDailyNote() const noexcept { return dailyNote_; }

private:
    void SyncTimerState();
    std::wstring GetAppDataDir() const;
    void LoadTodos();
    void SaveTodos() const;
    void LoadDailyNote();
    void SaveDailyNote() const;

    HWND mainHwnd_ = nullptr;
    bool initialized_ = false;

    FocusSubMode mode_ = FocusSubMode::Pomodoro;
    bool pomoRunning_ = false;
    std::uint32_t pomoTotalSec_ = 25 * 60;
    std::uint32_t pomoRemainingSec_ = 25 * 60;
    bool pomoIsBreak_ = false;
    std::uint32_t completedSessions_ = 0;
    std::uint32_t focusedSecondsToday_ = 0;

    bool stopwatchRunning_ = false;
    std::uint32_t stopwatchSec_ = 0;
    std::vector<std::uint32_t> laps_;

    bool typingTask_ = false;
    bool typingNote_ = false;
    std::wstring draftTask_;
    std::wstring draftNote_;
    std::wstring dailyNote_ = L"Stay locked in. Ship one meaningful thing today.";
    std::vector<TodoTaskItem> tasks_;
    std::size_t todoScroll_ = 0;
};

} // namespace notch::modules
