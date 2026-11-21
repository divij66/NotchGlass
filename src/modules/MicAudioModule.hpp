#pragma once

#include "modules/IModule.hpp"
#include "core/raii.hpp"

#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <physicalmonitorenumerationapi.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace notch::modules {

struct DisplayBrightnessTarget {
    std::wstring name;
    DWORD minBrightness = 0;
    DWORD maxBrightness = 100;
    int currentPct = 80;
    bool isExternalDdc = false;
    bool isWmiInternal = false;
    HANDLE physicalMonitorHandle = nullptr;
    std::wstring wmiInstanceName;
};

struct AppAudioSession {
    DWORD pid = 0;
    std::wstring appName;
    float volume = 1.0f;
    bool muted = false;
    bool isSystemSounds = false;
    core::ComPtr<ISimpleAudioVolume> simpleVol;
};

struct AudioDeviceEndpoint {
    std::wstring id;
    std::wstring name;
    bool isCapture = false;
    bool isDefault = false;
    float volume = 1.0f;
    bool muted = false;
    core::ComPtr<IAudioEndpointVolume> endpointVol;
};

enum class AudioSubView : std::uint8_t {
    Apps     = 0,
    Devices  = 1,
    Displays = 2
};

class MicAudioModule final : public IModule {
public:
    ~MicAudioModule() override;

    void EnsureInitialized(HWND mainHwnd) override;
    void OnCollapseRelease() override;
    void Render(const RenderContext& ctx) override;
    bool OnPointerDown(float xDip, float yDip) override;
    bool OnPointerDrag(float xDip, float yDip);
    void OnPointerUp();

    // Global hotkey (Ctrl + Alt + M) handler: mutes/unmutes all capture endpoints
    void ToggleGlobalMicMute();

    // Master Volume & Session/Device Refresh
    void SetOutputVolumeScalar(float scalar);
    void RefreshAudioMixerData();

    // Per-Display Individual Brightness (DDC/CI external + WMI laptop panel)
    void RefreshMonitorTargets();
    void SetMonitorBrightness(std::size_t index, int pct);
    void SetSelectedMonitorBrightness(int pct);
    void SetAllMonitorsBrightness(int pct);
    void SelectMonitor(std::size_t index);
    void SetSyncAllMonitors(bool sync) noexcept { syncAllMonitors_ = sync; }
    bool GetSyncAllMonitors() const noexcept { return syncAllMonitors_; }
    std::size_t GetSelectedMonitorIndex() const noexcept { return selectedMonitorIdx_; }
    const std::vector<DisplayBrightnessTarget>& GetMonitors() const noexcept { return monitors_; }
    int GetSelectedBrightnessPct() const noexcept;

    // System Flyout state updates (event-driven via COM callbacks + WM_POWERBROADCAST)
    void OnEndpointVolumeChanged(float scalarVolume, bool muted, bool isCapture);
    void OnMicSessionActivityChanged(bool active, const std::wstring& appName);
    void OnSystemPowerOrBrightnessChanged(UINT powerPct, bool onAc, int brightnessPct);
    void TickCallTimer();

    bool IsMicMuted() const noexcept { return micMuted_.load(); }
    bool IsCallActive() const noexcept { return callActive_.load(); }
    std::uint32_t GetCallElapsedSec() const noexcept { return callElapsedSec_; }
    float GetOutputVolume() const noexcept { return outputVolume_; }
    bool IsOutputMuted() const noexcept { return outputMuted_; }
    int GetBrightnessPct() const noexcept { return GetSelectedBrightnessPct(); }
    std::size_t GetMonitorCount() const noexcept { return monitors_.size(); }
    UINT GetBatteryPct() const noexcept { return batteryPct_; }
    std::wstring GetActiveCallApp() const { return activeCallApp_; }

    void SetSubView(AudioSubView v) noexcept { subView_ = v; }

private:
    class EndpointCallback;
    class SessionNotification;

    void ReleasePhysicalMonitors();
    void QueryWmiInternalBrightness(int& outPct, std::wstring& outInstance);
    bool SetWmiInternalBrightness(int pct, const std::wstring& instanceName);

    HWND mainHwnd_ = nullptr;
    bool initialized_ = false;
    AudioSubView subView_ = AudioSubView::Apps;
    int activeDragRow_ = -1;

    std::atomic<bool> micMuted_{false};
    std::atomic<bool> callActive_{false};
    std::uint32_t callElapsedSec_ = 0;
    std::wstring activeCallApp_ = L"";

    float outputVolume_ = 0.65f;
    bool outputMuted_ = false;
    int brightnessPct_ = 80;
    bool syncAllMonitors_ = false;
    std::size_t selectedMonitorIdx_ = 0;
    UINT batteryPct_ = 100;
    bool onAcPower_ = true;

    std::vector<PHYSICAL_MONITOR> rawPhysicalMonitors_;
    std::vector<DisplayBrightnessTarget> monitors_;
    std::vector<AppAudioSession> appSessions_;
    std::vector<AudioDeviceEndpoint> audioDevices_;

    core::ComPtr<IMMDeviceEnumerator> enumerator_;
    core::ComPtr<IAudioEndpointVolume> renderVol_;
    core::ComPtr<IAudioEndpointVolume> captureVol_;
    core::ComPtr<IAudioSessionManager2> renderSessionMgr_;
    core::ComPtr<IAudioSessionManager2> captureSessionMgr_;
    core::ComPtr<IAudioEndpointVolumeCallback> renderCallback_;
    core::ComPtr<IAudioEndpointVolumeCallback> captureCallback_;
    core::ComPtr<IAudioSessionNotification> sessionNotif_;
};

} // namespace notch::modules
