#pragma once

#include "modules/IModule.hpp"
#include "core/raii.hpp"

#include <pdh.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace notch::modules {

struct GameTelemetrySnapshot {
    float fps = 0.0f;
    float cpuPct = 0.0f;
    float gpuPct = 0.0f;
    float ramUsedGb = 0.0f;
    float tempCelsius = 0.0f;
};

enum class RecSizePreset : std::uint8_t {
    Native = 0,
    P1080  = 1,
    P720   = 2
};

class QuickToolsModule final : public IModule {
public:
    ~QuickToolsModule() override;

    void EnsureInitialized(HWND mainHwnd) override;
    void OnCollapseRelease() override;
    void Render(const RenderContext& ctx) override;
    bool OnPointerDown(float xDip, float yDip) override;

    // Region screen capture directly to CF_BITMAP clipboard
    bool CaptureRegionSnipToClipboard();

    // Native Windows Media Foundation H.264 MP4 Screen Recorder
    void CycleFps();
    void CycleSize();
    void StartRecording();
    void StopRecording();
    void OpenLastRecordingFolder() const;
    void TickRecording1Hz();

    bool IsRecording() const noexcept { return recording_.load(); }
    int GetRecFps() const noexcept { return recFps_; }
    RecSizePreset GetRecSize() const noexcept { return recSize_; }
    std::uint32_t GetRecElapsedSec() const noexcept { return recElapsedSec_; }
    std::wstring GetLastRecordingPath() const { return lastRecordingPath_; }

    // Fullscreen Game Strip telemetry (strictly gated & 1 Hz capped)
    void SetGameStripActive(bool active);
    void SampleGameTelemetry1Hz();
    const GameTelemetrySnapshot& GetGameTelemetry() const noexcept { return telemetry_; }

private:
    void RecorderThreadMain(int fps, RecSizePreset sizePreset, std::wstring outPath);
    std::wstring BuildRecordingOutputPath() const;

    HWND mainHwnd_ = nullptr;
    bool initialized_ = false;

    int recFps_ = 30;
    RecSizePreset recSize_ = RecSizePreset::P1080;
    std::atomic<bool> recording_{false};
    std::atomic<bool> stopRequested_{false};
    std::uint32_t recElapsedSec_ = 0;
    std::wstring lastRecordingPath_;
    std::wstring recStatusMsg_ = L"Ready — configure FPS & Size before recording";
    std::thread recThread_;

    bool pdhActive_ = false;
    core::UniquePdhQuery pdhQuery_;
    PDH_HCOUNTER cpuCounter_ = nullptr;
    GameTelemetrySnapshot telemetry_{};
};

} // namespace notch::modules
