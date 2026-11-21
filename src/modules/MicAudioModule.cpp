#include <initguid.h>
#include "modules/MicAudioModule.hpp"
#include "core/config.hpp"
#include "graphics/LiquidGlassEngine.hpp"

#include <functiondiscoverykeys_devpkey.h>
#include <highlevelmonitorconfigurationapi.h>
#include <wbemidl.h>
#include <algorithm>
#include <cstdio>
#include <cwctype>

namespace notch::modules {

class MicAudioModule::EndpointCallback final : public IAudioEndpointVolumeCallback {
public:
    EndpointCallback(MicAudioModule* owner, bool isCapture) : owner_(owner), isCapture_(isCapture) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
            *ppv = static_cast<IAudioEndpointVolumeCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(::InterlockedIncrement(&refCount_)); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG c = static_cast<ULONG>(::InterlockedDecrement(&refCount_));
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override {
        if (pNotify && owner_) {
            owner_->OnEndpointVolumeChanged(pNotify->fMasterVolume, pNotify->bMuted != FALSE, isCapture_);
        }
        return S_OK;
    }

private:
    LONG refCount_ = 1;
    MicAudioModule* owner_ = nullptr;
    bool isCapture_ = false;
};

class MicAudioModule::SessionNotification final : public IAudioSessionNotification {
public:
    explicit SessionNotification(MicAudioModule* owner) : owner_(owner) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionNotification)) {
            *ppv = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(::InterlockedIncrement(&refCount_)); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG c = static_cast<ULONG>(::InterlockedDecrement(&refCount_));
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP OnSessionCreated(IAudioSessionControl* NewSession) override {
        if (!NewSession || !owner_) return S_OK;
        AudioSessionState state = AudioSessionStateInactive;
        NewSession->GetState(&state);
        LPWSTR displayName = nullptr;
        NewSession->GetDisplayName(&displayName);
        std::wstring app = displayName ? displayName : L"Voice App";
        if (displayName) ::CoTaskMemFree(displayName);
        owner_->OnMicSessionActivityChanged(state == AudioSessionStateActive, app);
        return S_OK;
    }

private:
    LONG refCount_ = 1;
    MicAudioModule* owner_ = nullptr;
};

MicAudioModule::~MicAudioModule() {
    ReleasePhysicalMonitors();
    if (renderVol_ && renderCallback_) renderVol_->UnregisterControlChangeNotify(renderCallback_.Get());
    if (captureVol_ && captureCallback_) captureVol_->UnregisterControlChangeNotify(captureCallback_.Get());
    if (captureSessionMgr_ && sessionNotif_) captureSessionMgr_->UnregisterSessionNotification(sessionNotif_.Get());
}

void MicAudioModule::ReleasePhysicalMonitors() {
    if (!rawPhysicalMonitors_.empty()) {
        ::DestroyPhysicalMonitors(static_cast<DWORD>(rawPhysicalMonitors_.size()), rawPhysicalMonitors_.data());
        rawPhysicalMonitors_.clear();
    }
    monitors_.clear();
}

void MicAudioModule::QueryWmiInternalBrightness(int& outPct, std::wstring& outInstance) {
    outPct = -1;
    outInstance.clear();

    core::ComPtr<IWbemLocator> loc;
    if (FAILED(::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&loc)))) return;

    BSTR ns = ::SysAllocString(L"root\\wmi");
    core::ComPtr<IWbemServices> svc;
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    ::SysFreeString(ns);
    if (FAILED(hr) || !svc) return;

    ::CoSetProxyBlanket(svc.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    BSTR wql = ::SysAllocString(L"WQL");
    BSTR query = ::SysAllocString(L"SELECT CurrentBrightness, InstanceName FROM WmiMonitorBrightness");
    core::ComPtr<IEnumWbemClassObject> enumObj;
    hr = svc->ExecQuery(wql, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumObj);
    ::SysFreeString(wql);
    ::SysFreeString(query);
    if (FAILED(hr) || !enumObj) return;

    core::ComPtr<IWbemClassObject> obj;
    ULONG ret = 0;
    if (SUCCEEDED(enumObj->Next(500, 1, &obj, &ret)) && ret > 0 && obj) {
        VARIANT vCur{}, vInst{};
        VariantInit(&vCur);
        VariantInit(&vInst);
        if (SUCCEEDED(obj->Get(L"CurrentBrightness", 0, &vCur, nullptr, nullptr))) {
            if (vCur.vt == VT_UI1) outPct = static_cast<int>(vCur.bVal);
            else if (vCur.vt == VT_I4) outPct = static_cast<int>(vCur.lVal);
        }
        if (SUCCEEDED(obj->Get(L"InstanceName", 0, &vInst, nullptr, nullptr)) && vInst.vt == VT_BSTR && vInst.bstrVal) {
            outInstance = vInst.bstrVal;
        }
        VariantClear(&vCur);
        VariantClear(&vInst);
    }
}

bool MicAudioModule::SetWmiInternalBrightness(int pct, const std::wstring& instanceName) {
    core::ComPtr<IWbemLocator> loc;
    if (FAILED(::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&loc)))) return false;

    BSTR ns = ::SysAllocString(L"root\\wmi");
    core::ComPtr<IWbemServices> svc;
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    ::SysFreeString(ns);
    if (FAILED(hr) || !svc) return false;

    ::CoSetProxyBlanket(svc.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    BSTR clsName = ::SysAllocString(L"WmiMonitorBrightnessMethods");
    core::ComPtr<IWbemClassObject> cls;
    hr = svc->GetObject(clsName, 0, nullptr, &cls, nullptr);
    ::SysFreeString(clsName);
    if (FAILED(hr) || !cls) return false;

    BSTR methodName = ::SysAllocString(L"WmiSetBrightness");
    core::ComPtr<IWbemClassObject> inSig;
    hr = cls->GetMethod(methodName, 0, &inSig, nullptr);
    if (FAILED(hr) || !inSig) {
        ::SysFreeString(methodName);
        return false;
    }

    core::ComPtr<IWbemClassObject> inParams;
    if (FAILED(inSig->SpawnInstance(0, &inParams)) || !inParams) {
        ::SysFreeString(methodName);
        return false;
    }

    VARIANT vTimeout{}, vBright{};
    VariantInit(&vTimeout);
    VariantInit(&vBright);
    vTimeout.vt = VT_I4;
    vTimeout.lVal = 0;
    vBright.vt = VT_UI1;
    vBright.bVal = static_cast<BYTE>(std::clamp(pct, 5, 100));
    inParams->Put(L"Timeout", 0, &vTimeout, 0);
    inParams->Put(L"Brightness", 0, &vBright, 0);
    VariantClear(&vTimeout);
    VariantClear(&vBright);

    std::wstring pathStr = L"WmiMonitorBrightnessMethods.InstanceName='" + instanceName + L"'";
    BSTR objPath = ::SysAllocString(pathStr.c_str());
    hr = svc->ExecMethod(objPath, methodName, 0, nullptr, inParams.Get(), nullptr, nullptr);
    ::SysFreeString(objPath);
    ::SysFreeString(methodName);
    return SUCCEEDED(hr);
}

void MicAudioModule::RefreshMonitorTargets() {
    ReleasePhysicalMonitors();

    int wmiPct = -1;
    std::wstring wmiInst;
    QueryWmiInternalBrightness(wmiPct, wmiInst);
    bool assignedWmi = false;

    struct EnumState {
        MicAudioModule* self;
        int wmiPct;
        std::wstring wmiInst;
        bool assignedWmi;
    } st{this, wmiPct, wmiInst, assignedWmi};

    auto enumProc = [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
        auto* es = reinterpret_cast<EnumState*>(lParam);
        auto* self = es->self;

        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        std::wstring friendly;
        if (::GetMonitorInfoW(hMon, &mi)) {
            DISPLAY_DEVICEW dd{};
            dd.cb = sizeof(dd);
            if (::EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0) && dd.DeviceString[0] != L'\0') {
                friendly = dd.DeviceString;
            }
        }

        DWORD count = 0;
        if (::GetNumberOfPhysicalMonitorsFromHMONITOR(hMon, &count) && count > 0) {
            const std::size_t base = self->rawPhysicalMonitors_.size();
            self->rawPhysicalMonitors_.resize(base + count);
            if (::GetPhysicalMonitorsFromHMONITOR(hMon, count, self->rawPhysicalMonitors_.data() + base)) {
                for (DWORD i = 0; i < count; ++i) {
                    const auto& pm = self->rawPhysicalMonitors_[base + i];
                    DWORD minB = 0, curB = 80, maxB = 100;
                    const bool ddcOk = (::GetMonitorBrightness(pm.hPhysicalMonitor, &minB, &curB, &maxB) != FALSE);

                    DisplayBrightnessTarget target{};
                    std::wstring baseName = !friendly.empty() ? friendly : std::wstring(pm.szPhysicalMonitorDescription);
                    if (baseName.empty()) {
                        wchar_t fallback[32];
                        std::swprintf(fallback, 32, L"Display %zu", self->monitors_.size() + 1);
                        baseName = fallback;
                    }
                    target.name = baseName;
                    target.minBrightness = minB;
                    target.maxBrightness = maxB > minB ? maxB : 100;
                    target.isExternalDdc = ddcOk;
                    target.physicalMonitorHandle = pm.hPhysicalMonitor;

                    if (ddcOk && maxB > minB) {
                        target.currentPct = static_cast<int>(((curB - minB) * 100) / (maxB - minB));
                    } else if (!es->assignedWmi && es->wmiPct >= 0 && !es->wmiInst.empty()) {
                        target.isWmiInternal = true;
                        target.wmiInstanceName = es->wmiInst;
                        target.currentPct = es->wmiPct;
                        target.name = L"Built-in Display";
                        es->assignedWmi = true;
                    } else {
                        target.currentPct = self->brightnessPct_;
                    }
                    self->monitors_.push_back(std::move(target));
                }
            }
        }
        return TRUE;
    };

    ::EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&st));
    if (selectedMonitorIdx_ >= monitors_.size()) selectedMonitorIdx_ = 0;
    if (!monitors_.empty()) {
        brightnessPct_ = monitors_[selectedMonitorIdx_].currentPct;
    }
}

int MicAudioModule::GetSelectedBrightnessPct() const noexcept {
    if (selectedMonitorIdx_ < monitors_.size()) {
        return monitors_[selectedMonitorIdx_].currentPct;
    }
    return brightnessPct_;
}

void MicAudioModule::SelectMonitor(std::size_t index) {
    if (index < monitors_.size()) {
        selectedMonitorIdx_ = index;
        brightnessPct_ = monitors_[index].currentPct;
    }
}

void MicAudioModule::SetMonitorBrightness(std::size_t index, int pct) {
    pct = std::clamp(pct, 5, 100);
    if (index >= monitors_.size()) return;
    auto& m = monitors_[index];
    m.currentPct = pct;
    if (index == selectedMonitorIdx_) {
        brightnessPct_ = pct;
    }
    if (m.isExternalDdc && m.physicalMonitorHandle) {
        const DWORD val = m.minBrightness + ((m.maxBrightness - m.minBrightness) * static_cast<DWORD>(pct)) / 100;
        ::SetMonitorBrightness(m.physicalMonitorHandle, val);
    } else if (m.isWmiInternal && !m.wmiInstanceName.empty()) {
        SetWmiInternalBrightness(pct, m.wmiInstanceName);
    }
}

void MicAudioModule::SetSelectedMonitorBrightness(int pct) {
    if (syncAllMonitors_ || monitors_.empty()) {
        SetAllMonitorsBrightness(pct);
    } else {
        SetMonitorBrightness(selectedMonitorIdx_, pct);
    }
}

void MicAudioModule::SetAllMonitorsBrightness(int pct) {
    brightnessPct_ = std::clamp(pct, 5, 100);
    for (std::size_t i = 0; i < monitors_.size(); ++i) {
        SetMonitorBrightness(i, brightnessPct_);
    }
}

void MicAudioModule::SetOutputVolumeScalar(float scalar) {
    outputVolume_ = std::clamp(scalar, 0.0f, 1.0f);
    if (renderVol_) {
        renderVol_->SetMasterVolumeLevelScalar(outputVolume_, nullptr);
    }
}

void MicAudioModule::RefreshAudioMixerData() {
    if (!enumerator_) return;

    // 1) Active Per-App Audio Sessions on Default Output Endpoint
    appSessions_.clear();
    if (renderSessionMgr_) {
        core::ComPtr<IAudioSessionEnumerator> sessionEnum;
        if (SUCCEEDED(renderSessionMgr_->GetSessionEnumerator(&sessionEnum)) && sessionEnum) {
            int count = 0;
            sessionEnum->GetCount(&count);
            for (int i = 0; i < count; ++i) {
                core::ComPtr<IAudioSessionControl> ctrl;
                if (FAILED(sessionEnum->GetSession(i, &ctrl)) || !ctrl) continue;

                AudioSessionState state = AudioSessionStateInactive;
                ctrl->GetState(&state);
                if (state == AudioSessionStateExpired) continue;

                core::ComPtr<IAudioSessionControl2> ctrl2;
                core::ComPtr<ISimpleAudioVolume> simp;
                if (FAILED(ctrl.As(&ctrl2)) || FAILED(ctrl.As(&simp)) || !ctrl2 || !simp) continue;

                DWORD pid = 0;
                ctrl2->GetProcessId(&pid);
                const bool isSys = (ctrl2->IsSystemSoundsSession() == S_OK);

                std::wstring appName = isSys ? L"System Sounds" : L"";
                if (!isSys && pid != 0) {
                    core::UniqueHandle proc(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
                    if (proc.valid()) {
                        wchar_t exePath[MAX_PATH]{};
                        DWORD sz = MAX_PATH;
                        if (::QueryFullProcessImageNameW(proc.handle, 0, exePath, &sz)) {
                            std::wstring full(exePath);
                            const auto slash = full.find_last_of(L"\\/");
                            if (slash != std::wstring::npos) full = full.substr(slash + 1);
                            const auto dot = full.rfind(L".exe");
                            if (dot != std::wstring::npos) full = full.substr(0, dot);
                            if (!full.empty()) {
                                full[0] = static_cast<wchar_t>(::towupper(full[0]));
                                appName = std::move(full);
                            }
                        }
                    }
                }
                if (appName.empty()) {
                    LPWSTR disp = nullptr;
                    if (SUCCEEDED(ctrl->GetDisplayName(&disp)) && disp && disp[0] != L'\0' && disp[0] != L'@') {
                        appName = disp;
                    }
                    if (disp) ::CoTaskMemFree(disp);
                }
                if (appName.empty()) appName = L"Audio App";

                // Skip duplicate inactive apps with identical names if already present
                bool dup = false;
                for (const auto& existing : appSessions_) {
                    if (existing.appName == appName) {
                        dup = true;
                        break;
                    }
                }
                if (dup) continue;

                float vol = 1.0f;
                BOOL muted = FALSE;
                simp->GetMasterVolume(&vol);
                simp->GetMute(&muted);

                appSessions_.push_back({pid, std::move(appName), vol, muted != FALSE, isSys, simp});
            }
        }
    }

    // 2) Active Output & Input Audio Devices
    audioDevices_.clear();
    std::wstring defaultRenderId, defaultCaptureId;
    {
        core::ComPtr<IMMDevice> defR, defC;
        if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &defR)) && defR) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(defR->GetId(&id)) && id) { defaultRenderId = id; ::CoTaskMemFree(id); }
        }
        if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eCapture, eCommunications, &defC)) && defC) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(defC->GetId(&id)) && id) { defaultCaptureId = id; ::CoTaskMemFree(id); }
        }
    }

    auto scanFlow = [&](EDataFlow flow, bool isCap, const std::wstring& defId) {
        core::ComPtr<IMMDeviceCollection> coll;
        if (FAILED(enumerator_->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)) || !coll) return;
        UINT cnt = 0;
        coll->GetCount(&cnt);
        for (UINT i = 0; i < cnt; ++i) {
            core::ComPtr<IMMDevice> dev;
            if (FAILED(coll->Item(i, &dev)) || !dev) continue;

            LPWSTR idStr = nullptr;
            std::wstring devId;
            if (SUCCEEDED(dev->GetId(&idStr)) && idStr) { devId = idStr; ::CoTaskMemFree(idStr); }

            std::wstring friendly = isCap ? L"Microphone" : L"Speakers";
            core::ComPtr<IPropertyStore> props;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props) {
                PROPVARIANT pv{};
                PropVariantInit(&pv);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                    friendly = pv.pwszVal;
                }
                PropVariantClear(&pv);
            }

            core::ComPtr<IAudioEndpointVolume> epVol;
            float vol = 1.0f;
            BOOL muted = FALSE;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(epVol.GetAddressOf()))) && epVol) {
                epVol->GetMasterVolumeLevelScalar(&vol);
                epVol->GetMute(&muted);
            }
            audioDevices_.push_back({devId, friendly, isCap, devId == defId, vol, muted != FALSE, epVol});
        }
    };
    scanFlow(eRender, false, defaultRenderId);
    scanFlow(eCapture, true, defaultCaptureId);
}

void MicAudioModule::EnsureInitialized(HWND mainHwnd) {
    if (initialized_) return;
    initialized_ = true;
    mainHwnd_ = mainHwnd;

    RefreshMonitorTargets();

    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_)))) {
        return;
    }

    core::ComPtr<IMMDevice> renderDev;
    if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev)) && renderDev) {
        if (SUCCEEDED(renderDev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(renderVol_.ReleaseAndGetAddressOf())))) {
            renderCallback_.Attach(new EndpointCallback(this, false));
            renderVol_->RegisterControlChangeNotify(renderCallback_.Get());
            BOOL muted = FALSE;
            renderVol_->GetMasterVolumeLevelScalar(&outputVolume_);
            renderVol_->GetMute(&muted);
            outputMuted_ = (muted != FALSE);
        }
        renderDev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(renderSessionMgr_.ReleaseAndGetAddressOf()));
    }

    core::ComPtr<IMMDevice> captureDev;
    if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eCapture, eCommunications, &captureDev)) && captureDev) {
        if (SUCCEEDED(captureDev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(captureVol_.ReleaseAndGetAddressOf())))) {
            captureCallback_.Attach(new EndpointCallback(this, true));
            captureVol_->RegisterControlChangeNotify(captureCallback_.Get());
            BOOL muted = FALSE;
            captureVol_->GetMute(&muted);
            micMuted_.store(muted != FALSE);
        }
        if (SUCCEEDED(captureDev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(captureSessionMgr_.ReleaseAndGetAddressOf())))) {
            sessionNotif_.Attach(new SessionNotification(this));
            captureSessionMgr_->RegisterSessionNotification(sessionNotif_.Get());
        }
    }

    RefreshAudioMixerData();
}

void MicAudioModule::OnCollapseRelease() {
    activeDragRow_ = -1;
}

void MicAudioModule::ToggleGlobalMicMute() {
    if (!enumerator_) return;
    const bool newMute = !micMuted_.load();
    micMuted_.store(newMute);

    core::ComPtr<IMMDeviceCollection> collection;
    if (SUCCEEDED(enumerator_->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection))) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            core::ComPtr<IMMDevice> dev;
            if (SUCCEEDED(collection->Item(i, &dev))) {
                core::ComPtr<IAudioEndpointVolume> vol;
                if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(vol.GetAddressOf())))) {
                    vol->SetMute(newMute ? TRUE : FALSE, nullptr);
                }
            }
        }
    }
    if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_AUDIO_UPDATED, 0, 0);
}

void MicAudioModule::OnEndpointVolumeChanged(float scalarVolume, bool muted, bool isCapture) {
    if (isCapture) {
        micMuted_.store(muted);
    } else {
        outputVolume_ = scalarVolume;
        outputMuted_ = muted;
    }
    if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_AUDIO_UPDATED, 0, 0);
}

void MicAudioModule::OnMicSessionActivityChanged(bool active, const std::wstring& appName) {
    callActive_.store(active);
    activeCallApp_ = appName;
    if (!active) callElapsedSec_ = 0;
    if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_MIC_SESSION_UPDATE, 0, 0);
}

void MicAudioModule::OnSystemPowerOrBrightnessChanged(UINT powerPct, bool onAc, int brightnessPct) {
    batteryPct_ = powerPct;
    onAcPower_ = onAc;
    brightnessPct_ = brightnessPct;
}

void MicAudioModule::TickCallTimer() {
    if (callActive_.load()) {
        ++callElapsedSec_;
    }
}

void MicAudioModule::Render(const RenderContext& ctx) {
    auto* dc = ctx.dc;
    auto* dw = ctx.dwrite;

    core::ComPtr<ID2D1SolidColorBrush> textBrush;
    core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
    core::ComPtr<ID2D1SolidColorBrush> accentBrush;
    core::ComPtr<ID2D1SolidColorBrush> trackBg;
    core::ComPtr<ID2D1SolidColorBrush> dangerBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.84f, 0.90f, 0.98f, 0.78f), &mutedBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.36f, 0.82f, 1.00f, 0.95f), &accentBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.16f), &trackBg);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.34f, 0.38f, 0.92f), &dangerBrush);

    core::ComPtr<IDWriteTextFormat> titleFmt;
    core::ComPtr<IDWriteTextFormat> smallFmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &titleFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &smallFmt);

    // Top sub-view segmented switcher: [Apps Volume] [Audio Devices] [Displays] + [Refresh]
    const wchar_t* tabs[3] = {L"App Mixer", L"Devices", L"Displays"};
    for (int i = 0; i < 3; ++i) {
        const float tx = ctx.boundsDip.left + i * 102.0f;
        D2D1_RECT_F tb = D2D1::RectF(tx, ctx.boundsDip.top + 4.0f, tx + 96.0f, ctx.boundsDip.top + 28.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, tb, 10.0f, static_cast<int>(subView_) == i, true);
        smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        smallFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        dc->DrawTextW(tabs[i], static_cast<UINT32>(wcslen(tabs[i])), smallFmt.Get(), tb, textBrush.Get());
    }
    D2D1_RECT_F refBtn = D2D1::RectF(ctx.boundsDip.right - 74.0f, ctx.boundsDip.top + 4.0f, ctx.boundsDip.right, ctx.boundsDip.top + 28.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, refBtn, 10.0f, false, true);
    smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    smallFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextW(L"Refresh", 7, smallFmt.Get(), refBtn, accentBrush.Get());

    auto drawSliderRow = [&](D2D1_RECT_F row, const std::wstring& label, float ratio, bool isMuted, const wchar_t* tagTxt) {
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, row, 12.0f, false, true);
        titleFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        titleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        dc->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), titleFmt.Get(),
            D2D1::RectF(row.left + 12.0f, row.top, row.left + 150.0f, row.bottom), isMuted ? mutedBrush.Get() : textBrush.Get());

        // Interactive slider track (x: row.left + 156 .. row.right - 88)
        const float tL = row.left + 156.0f;
        const float tR = row.right - 88.0f;
        const float cy = (row.top + row.bottom) * 0.5f;
        const float clamped = std::clamp(ratio, 0.0f, 1.0f);
        const float fillX = tL + (tR - tL) * clamped;
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tL, cy - 3.0f, tR, cy + 3.0f), 3.0f, 3.0f), trackBg.Get());
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tL, cy - 3.0f, fillX, cy + 3.0f), 3.0f, 3.0f), isMuted ? dangerBrush.Get() : accentBrush.Get());
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, cy), 5.2f, 5.2f), textBrush.Get());

        // Right numeric + Mute/State capsule
        D2D1_RECT_F badge = D2D1::RectF(row.right - 78.0f, row.top + 6.0f, row.right - 8.0f, row.bottom - 6.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, badge, 9.0f, isMuted, true);
        smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        smallFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        dc->DrawTextW(tagTxt, static_cast<UINT32>(wcslen(tagTxt)), smallFmt.Get(), badge, isMuted ? dangerBrush.Get() : textBrush.Get());
    };

    float y = ctx.boundsDip.top + 38.0f;
    if (subView_ == AudioSubView::Apps) {
        if (appSessions_.empty()) {
            D2D1_RECT_F emptyBox = D2D1::RectF(ctx.boundsDip.left, y, ctx.boundsDip.right, y + 60.0f);
            graphics::LiquidGlassEngine::DrawGlassCapsule(dc, emptyBox, 12.0f, false, false);
            smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            const wchar_t* msg = L"No active app audio streams - play media and tap Refresh";
            dc->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), smallFmt.Get(), emptyBox, mutedBrush.Get());
            return;
        }
        for (std::size_t i = 0; i < appSessions_.size() && i < 5; ++i) {
            const auto& s = appSessions_[i];
            wchar_t tag[24];
            if (s.muted) std::swprintf(tag, 24, L"Muted");
            else std::swprintf(tag, 24, L"%d%%", static_cast<int>(s.volume * 100.0f));
            drawSliderRow(D2D1::RectF(ctx.boundsDip.left, y, ctx.boundsDip.right, y + 38.0f), s.appName, s.volume, s.muted, tag);
            y += 44.0f;
        }
    } else if (subView_ == AudioSubView::Devices) {
        if (audioDevices_.empty()) {
            D2D1_RECT_F emptyBox = D2D1::RectF(ctx.boundsDip.left, y, ctx.boundsDip.right, y + 60.0f);
            graphics::LiquidGlassEngine::DrawGlassCapsule(dc, emptyBox, 12.0f, false, false);
            smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            const wchar_t* msg = L"No active audio endpoints found";
            dc->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), smallFmt.Get(), emptyBox, mutedBrush.Get());
            return;
        }
        for (std::size_t i = 0; i < audioDevices_.size() && i < 5; ++i) {
            const auto& d = audioDevices_[i];
            std::wstring lbl = (d.isCapture ? L"[Mic] " : L"[Out] ") + d.name;
            wchar_t tag[24];
            if (d.muted) std::swprintf(tag, 24, L"Muted");
            else std::swprintf(tag, 24, L"%d%%", static_cast<int>(d.volume * 100.0f));
            drawSliderRow(D2D1::RectF(ctx.boundsDip.left, y, ctx.boundsDip.right, y + 38.0f), lbl, d.volume, d.muted, tag);
            y += 44.0f;
        }
    } else {
        // Individual Display Brightness Sliders
        if (monitors_.empty()) {
            D2D1_RECT_F emptyBox = D2D1::RectF(ctx.boundsDip.left, y, ctx.boundsDip.right, y + 60.0f);
            graphics::LiquidGlassEngine::DrawGlassCapsule(dc, emptyBox, 12.0f, false, false);
            smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            const wchar_t* msg = L"No controllable DDC/CI or WMI displays detected";
            dc->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), smallFmt.Get(), emptyBox, mutedBrush.Get());
            return;
        }
        for (std::size_t i = 0; i < monitors_.size() && i < 4; ++i) {
            const auto& m = monitors_[i];
            wchar_t tag[24];
            std::swprintf(tag, 24, L"%d%%", m.currentPct);
            drawSliderRow(D2D1::RectF(ctx.boundsDip.left, y, ctx.boundsDip.right, y + 40.0f), m.name, static_cast<float>(m.currentPct) / 100.0f, false, tag);
            y += 46.0f;
        }
        D2D1_RECT_F syncBtn = D2D1::RectF(ctx.boundsDip.left, ctx.boundsDip.bottom - 34.0f, ctx.boundsDip.right, ctx.boundsDip.bottom);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, syncBtn, 10.0f, syncAllMonitors_, true);
        smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        const wchar_t* sLbl = syncAllMonitors_ ? L"Sync All Displays on Home: ON (Click to Control Individually)" : L"Individual Display Mode Active (Click to Sync All)";
        dc->DrawTextW(sLbl, static_cast<UINT32>(wcslen(sLbl)), smallFmt.Get(), syncBtn, textBrush.Get());
    }
}

bool MicAudioModule::OnPointerDown(float xDip, float yDip) {
    // Sub-tabs row (y: 44..72)
    if (yDip >= 42.0f && yDip <= 74.0f) {
        if (xDip >= 386.0f) {
            RefreshAudioMixerData();
            RefreshMonitorTargets();
            return true;
        }
        if (xDip < 114.0f) subView_ = AudioSubView::Apps;
        else if (xDip < 216.0f) subView_ = AudioSubView::Devices;
        else subView_ = AudioSubView::Displays;
        return true;
    }

    if (subView_ == AudioSubView::Displays && yDip >= 264.0f) {
        syncAllMonitors_ = !syncAllMonitors_;
        return true;
    }

    const float rowStride = (subView_ == AudioSubView::Displays) ? 46.0f : 44.0f;
    const float relY = yDip - 80.0f;
    if (relY < 0.0f) return false;
    const int row = static_cast<int>(relY / rowStride);
    if (row < 0) return false;
    activeDragRow_ = row;

    // Right badge button toggles mute on Apps/Devices or selects active display
    if (xDip >= 382.0f) {
        const auto idx = static_cast<std::size_t>(row);
        if (subView_ == AudioSubView::Apps && idx < appSessions_.size()) {
            appSessions_[idx].muted = !appSessions_[idx].muted;
            if (appSessions_[idx].simpleVol) appSessions_[idx].simpleVol->SetMute(appSessions_[idx].muted ? TRUE : FALSE, nullptr);
            activeDragRow_ = -1;
            return true;
        }
        if (subView_ == AudioSubView::Devices && idx < audioDevices_.size()) {
            audioDevices_[idx].muted = !audioDevices_[idx].muted;
            if (audioDevices_[idx].endpointVol) audioDevices_[idx].endpointVol->SetMute(audioDevices_[idx].muted ? TRUE : FALSE, nullptr);
            activeDragRow_ = -1;
            return true;
        }
        if (subView_ == AudioSubView::Displays && idx < monitors_.size()) {
            SelectMonitor(idx);
            activeDragRow_ = -1;
            return true;
        }
        return false;
    }

    return OnPointerDrag(xDip, yDip);
}

bool MicAudioModule::OnPointerDrag(float xDip, float) {
    if (activeDragRow_ < 0) return false;
    const auto idx = static_cast<std::size_t>(activeDragRow_);
    // Slider track spans x: 172..376
    const float ratio = std::clamp((xDip - 172.0f) / (376.0f - 172.0f), 0.0f, 1.0f);

    if (subView_ == AudioSubView::Apps && idx < appSessions_.size()) {
        appSessions_[idx].volume = ratio;
        if (appSessions_[idx].simpleVol) appSessions_[idx].simpleVol->SetMasterVolume(ratio, nullptr);
        return true;
    }
    if (subView_ == AudioSubView::Devices && idx < audioDevices_.size()) {
        audioDevices_[idx].volume = ratio;
        if (audioDevices_[idx].endpointVol) audioDevices_[idx].endpointVol->SetMasterVolumeLevelScalar(ratio, nullptr);
        return true;
    }
    if (subView_ == AudioSubView::Displays && idx < monitors_.size()) {
        const int pct = std::clamp(static_cast<int>(ratio * 100.0f), 5, 100);
        SetMonitorBrightness(idx, pct);
        return true;
    }
    return false;
}

void MicAudioModule::OnPointerUp() {
    activeDragRow_ = -1;
}

} // namespace notch::modules
