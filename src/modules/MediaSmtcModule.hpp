#pragma once

#include "modules/IModule.hpp"
#include "core/raii.hpp"

#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <string>
#include <vector>

namespace notch::modules {

struct LyricLine {
    double timestampSec = 0.0;
    std::wstring text;
};

class MediaSmtcModule final : public IModule {
public:
    void EnsureInitialized(HWND mainHwnd) override;
    void OnCollapseRelease() override;
    void Render(const RenderContext& ctx) override;
    bool OnPointerDown(float xDip, float yDip) override;

    // Called from WM_APP_MEDIA_UPDATED on the UI thread
    void RefreshFromWinRT();

    // Load optional .lrc synced lyrics file
    void LoadLrcFile(const std::wstring& lrcPath);
    void ToggleLyricsDrawer() noexcept { showLyrics_ = !showLyrics_; }

    bool HasActiveMedia() const noexcept { return hasSession_; }
    std::wstring GetTitle() const { return title_; }
    std::wstring GetArtist() const { return artist_; }
    bool IsPlaying() const noexcept { return isPlaying_; }

private:
    void DownsampleThumbnail96x96(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& thumbRef);

    HWND mainHwnd_ = nullptr;
    bool initialized_ = false;
    bool hasSession_ = false;
    bool isPlaying_ = false;
    bool showLyrics_ = false;

    std::wstring title_ = L"No Active Media";
    std::wstring artist_ = L"Play Spotify, Edge, or VLC";
    double positionSec_ = 0.0;
    double durationSec_ = 1.0;

    // Downsampled strictly to 96x96 in memory; cleared on collapse
    core::ComPtr<ID2D1Bitmap> albumArtBitmap_;
    std::vector<LyricLine> lyrics_;

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession currentSession_{nullptr};
    winrt::event_token sessionsChangedToken_{};
    winrt::event_token mediaPropsToken_{};
    winrt::event_token playbackInfoToken_{};
};

} // namespace notch::modules
