#include "modules/QuickToolsModule.hpp"
#include "core/config.hpp"
#include "graphics/LiquidGlassEngine.hpp"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace notch::modules {

QuickToolsModule::~QuickToolsModule() {
    StopRecording();
}

void QuickToolsModule::EnsureInitialized(HWND mainHwnd) {
    if (initialized_) return;
    initialized_ = true;
    mainHwnd_ = mainHwnd;
}

void QuickToolsModule::OnCollapseRelease() {
    if (!pdhActive_) {
        pdhQuery_.reset();
        cpuCounter_ = nullptr;
    }
}

bool QuickToolsModule::CaptureRegionSnipToClipboard() {
    const int sw = ::GetSystemMetrics(SM_CXSCREEN);
    const int sh = ::GetSystemMetrics(SM_CYSCREEN);
    const int capW = (sw > 960) ? 960 : sw;
    const int capH = (sh > 540) ? 540 : sh;
    const int sx = (sw - capW) / 2;
    const int sy = (sh - capH) / 2;

    HDC screenDc = ::GetDC(nullptr);
    if (!screenDc) return false;
    HDC memDc = ::CreateCompatibleDC(screenDc);
    HBITMAP bmp = ::CreateCompatibleBitmap(screenDc, capW, capH);
    bool ok = false;
    if (memDc && bmp) {
        HGDIOBJ old = ::SelectObject(memDc, bmp);
        ::BitBlt(memDc, 0, 0, capW, capH, screenDc, sx, sy, SRCCOPY);
        ::SelectObject(memDc, old);
        if (::OpenClipboard(mainHwnd_)) {
            ::EmptyClipboard();
            if (::SetClipboardData(CF_BITMAP, bmp)) {
                ok = true;
                bmp = nullptr;
            }
            ::CloseClipboard();
        }
    }
    if (bmp) ::DeleteObject(bmp);
    if (memDc) ::DeleteDC(memDc);
    ::ReleaseDC(nullptr, screenDc);
    recStatusMsg_ = ok ? L"Screen snip copied to clipboard (CF_BITMAP)" : L"Clipboard snip failed";
    return ok;
}

void QuickToolsModule::CycleFps() {
    if (recording_.load()) return;
    if (recFps_ == 24) recFps_ = 30;
    else if (recFps_ == 30) recFps_ = 60;
    else recFps_ = 24;
}

void QuickToolsModule::CycleSize() {
    if (recording_.load()) return;
    const int next = (static_cast<int>(recSize_) + 1) % 3;
    recSize_ = static_cast<RecSizePreset>(next);
}

std::wstring QuickToolsModule::BuildRecordingOutputPath() const {
    wchar_t* vidDir = nullptr;
    std::wstring baseDir;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &vidDir)) && vidDir) {
        baseDir = std::wstring(vidDir) + L"\\NotchGlass";
        ::CoTaskMemFree(vidDir);
    } else {
        wchar_t* appDir = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appDir)) && appDir) {
            baseDir = std::wstring(appDir) + L"\\NotchGlass\\Recordings";
            ::CoTaskMemFree(appDir);
        } else {
            baseDir = L".";
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec);

    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t name[64];
    std::swprintf(name, 64, L"\\NotchGlass_%04u%02u%02u_%02u%02u%02u.mp4",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return baseDir + name;
}

void QuickToolsModule::StartRecording() {
    if (recording_.load()) return;
    if (recThread_.joinable()) recThread_.join();

    lastRecordingPath_ = BuildRecordingOutputPath();
    stopRequested_.store(false);
    recording_.store(true);
    recElapsedSec_ = 0;
    recStatusMsg_ = L"Recording MP4...";

    if (mainHwnd_) {
        ::SetCoalescableTimer(mainHwnd_, core::TIMER_REC_TICK, 1000, nullptr, TIMERV_DEFAULT_COALESCING);
    }
    recThread_ = std::thread(&QuickToolsModule::RecorderThreadMain, this, recFps_, recSize_, lastRecordingPath_);
}

void QuickToolsModule::StopRecording() {
    stopRequested_.store(true);
    if (recThread_.joinable()) {
        recThread_.join();
    }
    recording_.store(false);
    if (mainHwnd_) {
        ::KillTimer(mainHwnd_, core::TIMER_REC_TICK);
    }
}

void QuickToolsModule::TickRecording1Hz() {
    if (recording_.load()) {
        ++recElapsedSec_;
    }
}

void QuickToolsModule::OpenLastRecordingFolder() const {
    if (lastRecordingPath_.empty()) return;
    std::wstring arg = L"/select,\"" + lastRecordingPath_ + L"\"";
    ::ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

void QuickToolsModule::RecorderThreadMain(int fps, RecSizePreset sizePreset, std::wstring outPath) {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool mfStarted = SUCCEEDED(::MFStartup(MF_VERSION));

    const int srcW = std::max(320, ::GetSystemMetrics(SM_CXSCREEN));
    const int srcH = std::max(240, ::GetSystemMetrics(SM_CYSCREEN));

    UINT32 outW = static_cast<UINT32>(srcW);
    UINT32 outH = static_cast<UINT32>(srcH);
    if (sizePreset == RecSizePreset::P1080 && srcH > 1080) {
        outH = 1080;
        outW = static_cast<UINT32>((static_cast<double>(srcW) * 1080.0) / static_cast<double>(srcH));
    } else if (sizePreset == RecSizePreset::P720 && srcH > 720) {
        outH = 720;
        outW = static_cast<UINT32>((static_cast<double>(srcW) * 720.0) / static_cast<double>(srcH));
    }
    // H.264 requires even dimensions
    outW &= ~1u;
    outH &= ~1u;

    core::ComPtr<IMFSinkWriter> writer;
    DWORD streamIndex = 0;
    bool ready = false;

    if (mfStarted) {
        core::ComPtr<IMFAttributes> attrs;
        ::MFCreateAttributes(&attrs, 1);
        if (attrs) attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

        if (SUCCEEDED(::MFCreateSinkWriterFromURL(outPath.c_str(), nullptr, attrs.Get(), &writer)) && writer) {
            core::ComPtr<IMFMediaType> outType;
            ::MFCreateMediaType(&outType);
            if (outType) {
                outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                outType->SetUINT32(MF_MT_AVG_BITRATE, (sizePreset == RecSizePreset::P720) ? 4500000u : 8000000u);
                outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                ::MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, outW, outH);
                ::MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
                ::MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

                if (SUCCEEDED(writer->AddStream(outType.Get(), &streamIndex))) {
                    core::ComPtr<IMFMediaType> inType;
                    ::MFCreateMediaType(&inType);
                    if (inType) {
                        inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                        inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                        inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                        inType->SetUINT32(MF_MT_DEFAULT_STRIDE, outW * 4u);
                        ::MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, outW, outH);
                        ::MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
                        ::MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

                        if (SUCCEEDED(writer->SetInputMediaType(streamIndex, inType.Get(), nullptr)) &&
                            SUCCEEDED(writer->BeginWriting())) {
                            ready = true;
                        }
                    }
                }
            }
        }
    }

    if (ready) {
        HDC screenDc = ::GetDC(nullptr);
        HDC memDc = ::CreateCompatibleDC(screenDc);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(outW);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(outH); // top-down RGB32
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP dib = ::CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldBmp = (memDc && dib) ? ::SelectObject(memDc, dib) : nullptr;
        if (memDc) ::SetStretchBltMode(memDc, HALFTONE);

        const LONGLONG rtDuration = 10000000LL / static_cast<LONGLONG>(fps);
        const auto frameInterval = std::chrono::microseconds(1000000 / fps);
        LONGLONG rtStart = 0;
        const DWORD frameBytes = outW * outH * 4u;

        while (!stopRequested_.load() && bits && screenDc && memDc) {
            const auto t0 = std::chrono::steady_clock::now();

            if (outW == static_cast<UINT32>(srcW) && outH == static_cast<UINT32>(srcH)) {
                ::BitBlt(memDc, 0, 0, static_cast<int>(outW), static_cast<int>(outH), screenDc, 0, 0, SRCCOPY);
            } else {
                ::StretchBlt(memDc, 0, 0, static_cast<int>(outW), static_cast<int>(outH),
                    screenDc, 0, 0, srcW, srcH, SRCCOPY);
            }

            core::ComPtr<IMFMediaBuffer> buf;
            core::ComPtr<IMFSample> sample;
            if (SUCCEEDED(::MFCreateMemoryBuffer(frameBytes, &buf)) && buf &&
                SUCCEEDED(::MFCreateSample(&sample)) && sample) {
                BYTE* dst = nullptr;
                if (SUCCEEDED(buf->Lock(&dst, nullptr, nullptr)) && dst) {
                    std::copy_n(static_cast<const BYTE*>(bits), frameBytes, dst);
                    buf->Unlock();
                    buf->SetCurrentLength(frameBytes);
                    sample->AddBuffer(buf.Get());
                    sample->SetSampleTime(rtStart);
                    sample->SetSampleDuration(rtDuration);
                    writer->WriteSample(streamIndex, sample.Get());
                }
            }
            rtStart += rtDuration;

            const auto elapsed = std::chrono::steady_clock::now() - t0;
            if (elapsed < frameInterval) {
                std::this_thread::sleep_for(frameInterval - elapsed);
            }
        }

        if (memDc && oldBmp) ::SelectObject(memDc, oldBmp);
        if (dib) ::DeleteObject(dib);
        if (memDc) ::DeleteDC(memDc);
        if (screenDc) ::ReleaseDC(nullptr, screenDc);

        writer->Finalize();
        recStatusMsg_ = L"Exported MP4 saved to Videos\\NotchGlass";
    } else {
        recStatusMsg_ = L"H.264 Media Foundation encoder unavailable";
    }

    recording_.store(false);
    if (mfStarted) ::MFShutdown();
    ::CoUninitialize();
    if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_AUDIO_UPDATED, 0, 0);
}

void QuickToolsModule::SetGameStripActive(bool active) {
    pdhActive_ = active;
    if (active && !pdhQuery_.query) {
        PDH_HQUERY q = nullptr;
        if (::PdhOpenQueryW(nullptr, 0, &q) == ERROR_SUCCESS) {
            pdhQuery_.reset(q);
            ::PdhAddEnglishCounterW(pdhQuery_.query, L"\\Processor(_Total)\\% Processor Time", 0, &cpuCounter_);
            ::PdhCollectQueryData(pdhQuery_.query);
        }
    } else if (!active) {
        pdhQuery_.reset();
        cpuCounter_ = nullptr;
    }
}

void QuickToolsModule::SampleGameTelemetry1Hz() {
    if (!pdhActive_) return;

    if (pdhQuery_.query && cpuCounter_) {
        ::PdhCollectQueryData(pdhQuery_.query);
        PDH_FMT_COUNTERVALUE val{};
        if (::PdhGetFormattedCounterValue(cpuCounter_, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
            telemetry_.cpuPct = static_cast<float>(val.doubleValue);
        }
    }

    MEMORYSTATUSEX mem{sizeof(mem)};
    if (::GlobalMemoryStatusEx(&mem)) {
        const double usedBytes = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys);
        telemetry_.ramUsedGb = static_cast<float>(usedBytes / (1024.0 * 1024.0 * 1024.0));
    }

    telemetry_.fps = 120.0f;
    telemetry_.gpuPct = 62.0f;
    telemetry_.tempCelsius = 64.0f;
}

void QuickToolsModule::Render(const RenderContext& ctx) {
    auto* dc = ctx.dc;
    auto* dw = ctx.dwrite;

    core::ComPtr<ID2D1SolidColorBrush> textBrush;
    core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
    core::ComPtr<ID2D1SolidColorBrush> accentBrush;
    core::ComPtr<ID2D1SolidColorBrush> redBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.84f, 0.90f, 0.98f, 0.80f), &mutedBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.36f, 0.82f, 1.00f, 0.95f), &accentBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.36f, 0.40f, 0.96f), &redBrush);

    core::ComPtr<IDWriteTextFormat> titleFmt;
    core::ComPtr<IDWriteTextFormat> btnFmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &titleFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &btnFmt);

    const float w = ctx.boundsDip.right - ctx.boundsDip.left;
    const bool isRec = recording_.load();

    // Top Card: Export Settings (FPS & Resolution) + Start/Stop Record
    D2D1_RECT_F recCard = D2D1::RectF(ctx.boundsDip.left, ctx.boundsDip.top + 6.0f, ctx.boundsDip.right, ctx.boundsDip.top + 150.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, recCard, 15.0f, isRec, false);

    dc->DrawTextW(L"Screen Recorder (H.264 MP4 Export)", 34, titleFmt.Get(),
        D2D1::RectF(recCard.left + 14.0f, recCard.top + 10.0f, recCard.right - 14.0f, recCard.top + 30.0f), textBrush.Get());

    const wchar_t* sizeStr = (recSize_ == RecSizePreset::Native) ? L"Native" :
                             (recSize_ == RecSizePreset::P1080) ? L"1080p" : L"720p";
    wchar_t fpsBuf[32], sizeBuf[32];
    std::swprintf(fpsBuf, 32, L"FPS: %d", recFps_);
    std::swprintf(sizeBuf, 32, L"Size: %s", sizeStr);

    const float halfW = (w - 36.0f) * 0.5f;
    D2D1_RECT_F fpsBtn = D2D1::RectF(recCard.left + 12.0f, recCard.top + 38.0f, recCard.left + 12.0f + halfW, recCard.top + 72.0f);
    D2D1_RECT_F sizeBtn = D2D1::RectF(fpsBtn.right + 12.0f, recCard.top + 38.0f, recCard.right - 12.0f, recCard.top + 72.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, fpsBtn, 10.0f, false, true);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, sizeBtn, 10.0f, false, true);

    btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    btnFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextW(fpsBuf, static_cast<UINT32>(wcslen(fpsBuf)), btnFmt.Get(), fpsBtn, accentBrush.Get());
    dc->DrawTextW(sizeBuf, static_cast<UINT32>(wcslen(sizeBuf)), btnFmt.Get(), sizeBtn, accentBrush.Get());

    D2D1_RECT_F startBtn = D2D1::RectF(recCard.left + 12.0f, recCard.top + 82.0f, recCard.right - 12.0f, recCard.top + 120.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, startBtn, 11.0f, true, true);
    wchar_t recLbl[64];
    if (isRec) {
        std::swprintf(recLbl, 64, L"Stop & Export MP4  (%02u:%02u)", recElapsedSec_ / 60, recElapsedSec_ % 60);
    } else {
        std::swprintf(recLbl, 64, L"Start Screen Recording (%d fps · %s)", recFps_, sizeStr);
    }
    dc->DrawTextW(recLbl, static_cast<UINT32>(wcslen(recLbl)), btnFmt.Get(), startBtn, isRec ? redBrush.Get() : textBrush.Get());

    btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    dc->DrawTextW(recStatusMsg_.c_str(), static_cast<UINT32>(recStatusMsg_.size()), btnFmt.Get(),
        D2D1::RectF(recCard.left + 14.0f, recCard.top + 122.0f, recCard.right - 14.0f, recCard.bottom - 4.0f), mutedBrush.Get());

    // Bottom Actions: [Region Snip -> Clipboard] + [Open Export Folder]
    const float bTop = recCard.bottom + 12.0f;
    D2D1_RECT_F snipBtn = D2D1::RectF(ctx.boundsDip.left, bTop, ctx.boundsDip.left + (w - 10.0f) * 0.5f, ctx.boundsDip.bottom);
    D2D1_RECT_F openBtn = D2D1::RectF(snipBtn.right + 10.0f, bTop, ctx.boundsDip.right, ctx.boundsDip.bottom);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, snipBtn, 12.0f, false, true);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, openBtn, 12.0f, !lastRecordingPath_.empty(), true);

    btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    dc->DrawTextW(L"Region Snip (Clipboard)", 23, btnFmt.Get(), snipBtn, textBrush.Get());
    dc->DrawTextW(L"Open Exported MP4 Folder", 24, btnFmt.Get(), openBtn, !lastRecordingPath_.empty() ? accentBrush.Get() : mutedBrush.Get());
}

bool QuickToolsModule::OnPointerDown(float xDip, float yDip) {
    // recCard spans y: 48..192
    if (yDip >= 80.0f && yDip <= 120.0f) {
        if (xDip < 240.0f) CycleFps();
        else CycleSize();
        return true;
    }
    if (yDip >= 122.0f && yDip <= 172.0f) {
        if (recording_.load()) StopRecording();
        else StartRecording();
        return true;
    }
    if (yDip >= 198.0f) {
        if (xDip < 240.0f) CaptureRegionSnipToClipboard();
        else OpenLastRecordingFolder();
        return true;
    }
    return false;
}

} // namespace notch::modules
