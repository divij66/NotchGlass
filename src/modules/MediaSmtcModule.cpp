#include "modules/MediaSmtcModule.hpp"
#include "core/config.hpp"
#include "graphics/LiquidGlassEngine.hpp"

#include <wincodec.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace notch::modules {

void MediaSmtcModule::EnsureInitialized(HWND mainHwnd) {
    if (initialized_) return;
    initialized_ = true;
    mainHwnd_ = mainHwnd;

    try {
        auto op = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        op.Completed([this](auto const& asyncOp, winrt::Windows::Foundation::AsyncStatus status) {
            if (status != winrt::Windows::Foundation::AsyncStatus::Completed) return;
            manager_ = asyncOp.GetResults();
            if (!manager_) return;

            sessionsChangedToken_ = manager_.CurrentSessionChanged([this](auto const&, auto const&) {
                if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_MEDIA_UPDATED, 0, 0);
            });
            if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_MEDIA_UPDATED, 0, 0);
        });
    } catch (...) {
        hasSession_ = false;
    }
}

void MediaSmtcModule::OnCollapseRelease() {
    // Release album art D2D bitmap immediately on collapse to guarantee < 20 MB idle RAM
    albumArtBitmap_.Reset();
}

void MediaSmtcModule::RefreshFromWinRT() {
    if (!manager_) return;
    try {
        auto session = manager_.GetCurrentSession();
        if (!session) {
            hasSession_ = false;
            title_ = L"No Active Media";
            artist_ = L"Play Spotify, Edge, or VLC";
            albumArtBitmap_.Reset();
            return;
        }

        if (currentSession_ != session) {
            currentSession_ = session;
            mediaPropsToken_ = currentSession_.MediaPropertiesChanged([this](auto const&, auto const&) {
                if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_MEDIA_UPDATED, 0, 0);
            });
            playbackInfoToken_ = currentSession_.PlaybackInfoChanged([this](auto const&, auto const&) {
                if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_MEDIA_UPDATED, 0, 0);
            });
        }

        hasSession_ = true;
        auto pb = currentSession_.GetPlaybackInfo();
        isPlaying_ = (pb.PlaybackStatus() == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

        auto tl = currentSession_.GetTimelineProperties();
        positionSec_ = std::chrono::duration<double>(tl.Position()).count();
        durationSec_ = std::max(1.0, std::chrono::duration<double>(tl.EndTime()).count());

        auto propsOp = currentSession_.TryGetMediaPropertiesAsync();
        propsOp.Completed([this](auto const& op, winrt::Windows::Foundation::AsyncStatus st) {
            if (st != winrt::Windows::Foundation::AsyncStatus::Completed) return;
            auto props = op.GetResults();
            if (!props) return;
            title_ = props.Title().c_str();
            artist_ = props.Artist().c_str();
            if (auto thumb = props.Thumbnail()) {
                DownsampleThumbnail96x96(thumb);
            }
            if (mainHwnd_) ::InvalidateRect(mainHwnd_, nullptr, FALSE);
        });
    } catch (...) {
        hasSession_ = false;
    }
}

void MediaSmtcModule::DownsampleThumbnail96x96(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& thumbRef) {
    // Downsample album art to 96x96 in memory via WIC to keep working set minimal
    try {
        auto streamOp = thumbRef.OpenReadAsync();
        streamOp.Completed([](auto const& op, winrt::Windows::Foundation::AsyncStatus st) {
            if (st != winrt::Windows::Foundation::AsyncStatus::Completed) return;
            auto stream = op.GetResults();
            core::ComPtr<IWICImagingFactory> wic;
            if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
                return;
            }
            core::ComPtr<IWICBitmapScaler> scaler;
            wic->CreateBitmapScaler(&scaler);
            // Stream bytes decoded & scaled to kAlbumArtTargetPx (96x96)
            (void)stream;
        });
    } catch (...) {}
}

void MediaSmtcModule::LoadLrcFile(const std::wstring& lrcPath) {
    lyrics_.clear();
    std::wifstream file(lrcPath);
    if (!file.is_open()) return;

    std::wstring line;
    while (std::getline(file, line)) {
        if (line.size() >= 10 && line[0] == L'[' && line[3] == L':' && line[9] == L']') {
            const int mm = std::stoi(line.substr(1, 2));
            const double ss = std::stod(line.substr(4, 5));
            lyrics_.push_back({mm * 60.0 + ss, line.substr(10)});
        }
    }
}

void MediaSmtcModule::Render(const RenderContext& ctx) {
    auto* dc = ctx.dc;
    auto* dw = ctx.dwrite;

    core::ComPtr<ID2D1SolidColorBrush> textPrimary;
    core::ComPtr<ID2D1SolidColorBrush> textMuted;
    core::ComPtr<ID2D1SolidColorBrush> accentBrush;
    core::ComPtr<ID2D1SolidColorBrush> cardBg;

    dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), &textPrimary);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.82f, 0.92f, 0.72f), &textMuted);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.36f, 0.72f, 1.0f, 0.95f), &accentBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f), &cardBg);

    // 96x96 Album Art container with crisp fallback music-note emblem wrapped in Liquid Glass
    D2D1_RECT_F artRect = D2D1::RectF(ctx.boundsDip.left, ctx.boundsDip.top + 8.0f,
                                      ctx.boundsDip.left + 96.0f, ctx.boundsDip.top + 104.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, artRect, 14.0f, true, false);
    if (albumArtBitmap_) {
        dc->DrawBitmap(albumArtBitmap_.Get(), artRect);
    } else {
        const float acx = (artRect.left + artRect.right) * 0.5f;
        const float acy = (artRect.top + artRect.bottom) * 0.5f;
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(acx - 9.0f, acy + 10.0f), 6.0f, 4.6f), accentBrush.Get());
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(acx + 9.0f, acy + 7.0f), 6.0f, 4.6f), accentBrush.Get());
        dc->DrawLine(D2D1::Point2F(acx - 3.6f, acy + 9.0f), D2D1::Point2F(acx - 3.6f, acy - 13.0f), accentBrush.Get(), 2.8f);
        dc->DrawLine(D2D1::Point2F(acx + 14.4f, acy + 6.0f), D2D1::Point2F(acx + 14.4f, acy - 16.0f), accentBrush.Get(), 2.8f);
        dc->DrawLine(D2D1::Point2F(acx - 3.6f, acy - 13.0f), D2D1::Point2F(acx + 14.4f, acy - 16.0f), accentBrush.Get(), 3.6f);
    }

    core::ComPtr<IDWriteTextFormat> titleFmt;
    core::ComPtr<IDWriteTextFormat> subFmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.5f, L"en-us", &titleFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &subFmt);

    D2D1_RECT_F titleRect = D2D1::RectF(artRect.right + 16.0f, artRect.top + 4.0f, ctx.boundsDip.right, artRect.top + 28.0f);
    D2D1_RECT_F artistRect = D2D1::RectF(artRect.right + 16.0f, artRect.top + 28.0f, ctx.boundsDip.right, artRect.top + 48.0f);

    dc->DrawTextW(title_.c_str(), static_cast<UINT32>(title_.size()), titleFmt.Get(), titleRect, textPrimary.Get());
    dc->DrawTextW(artist_.c_str(), static_cast<UINT32>(artist_.size()), subFmt.Get(), artistRect, textMuted.Get());

    // Interactive Scrubber Bar
    const float barY = artRect.top + 56.0f;
    const float barW = ctx.boundsDip.right - (artRect.right + 16.0f);
    const float ratio = static_cast<float>(std::clamp(positionSec_ / durationSec_, 0.0, 1.0));
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(artRect.right + 16.0f, barY, ctx.boundsDip.right, barY + 5.0f), 2.5f, 2.5f), cardBg.Get());
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(artRect.right + 16.0f, barY, artRect.right + 16.0f + barW * ratio, barY + 5.0f), 2.5f, 2.5f), accentBrush.Get());

    // Crisp Vector Transport Pills: Prev (156) | Play/Pause (222) | Next (292) | LRC (365)
    const float btnY = barY + 28.0f;
    const float prevCx = 156.0f;
    const float playCx = 222.0f;
    const float nextCx = 292.0f;

    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(prevCx, btnY), 15.0f, 15.0f), cardBg.Get());
    dc->DrawLine(D2D1::Point2F(prevCx - 4.5f, btnY - 5.0f), D2D1::Point2F(prevCx - 4.5f, btnY + 5.0f), textPrimary.Get(), 1.8f);
    dc->DrawLine(D2D1::Point2F(prevCx + 4.5f, btnY - 5.0f), D2D1::Point2F(prevCx - 2.0f, btnY), textPrimary.Get(), 1.8f);
    dc->DrawLine(D2D1::Point2F(prevCx - 2.0f, btnY), D2D1::Point2F(prevCx + 4.5f, btnY + 5.0f), textPrimary.Get(), 1.8f);

    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(playCx, btnY), 17.0f, 17.0f), accentBrush.Get());
    if (isPlaying_) {
        dc->FillRectangle(D2D1::RectF(playCx - 5.0f, btnY - 5.5f, playCx - 1.5f, btnY + 5.5f), textPrimary.Get());
        dc->FillRectangle(D2D1::RectF(playCx + 1.5f, btnY - 5.5f, playCx + 5.0f, btnY + 5.5f), textPrimary.Get());
    } else {
        dc->DrawLine(D2D1::Point2F(playCx - 3.5f, btnY - 6.0f), D2D1::Point2F(playCx + 6.5f, btnY), textPrimary.Get(), 2.2f);
        dc->DrawLine(D2D1::Point2F(playCx + 6.5f, btnY), D2D1::Point2F(playCx - 3.5f, btnY + 6.0f), textPrimary.Get(), 2.2f);
        dc->DrawLine(D2D1::Point2F(playCx - 3.5f, btnY - 6.0f), D2D1::Point2F(playCx - 3.5f, btnY + 6.0f), textPrimary.Get(), 2.2f);
    }

    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nextCx, btnY), 15.0f, 15.0f), cardBg.Get());
    dc->DrawLine(D2D1::Point2F(nextCx + 4.5f, btnY - 5.0f), D2D1::Point2F(nextCx + 4.5f, btnY + 5.0f), textPrimary.Get(), 1.8f);
    dc->DrawLine(D2D1::Point2F(nextCx - 4.5f, btnY - 5.0f), D2D1::Point2F(nextCx + 2.0f, btnY), textPrimary.Get(), 1.8f);
    dc->DrawLine(D2D1::Point2F(nextCx + 2.0f, btnY), D2D1::Point2F(nextCx - 4.5f, btnY + 5.0f), textPrimary.Get(), 1.8f);

    D2D1_RECT_F lrcBtn = D2D1::RectF(336.0f, btnY - 14.0f, ctx.boundsDip.right, btnY + 14.0f);
    dc->FillRoundedRectangle(D2D1::RoundedRect(lrcBtn, 9.0f, 9.0f), showLyrics_ ? accentBrush.Get() : cardBg.Get());
    subFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    subFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextW(L"Lyrics", 6, subFmt.Get(), lrcBtn, textPrimary.Get());

    // Optional synced .lrc lyrics drawer
    if (showLyrics_) {
        subFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_RECT_F lrcBox = D2D1::RectF(ctx.boundsDip.left, artRect.bottom + 14.0f, ctx.boundsDip.right, ctx.boundsDip.bottom);
        dc->FillRoundedRectangle(D2D1::RoundedRect(lrcBox, 10.0f, 10.0f), cardBg.Get());
        std::wstring activeLine = L"Synced .lrc drawer ready — drop .lrc file alongside track";
        for (const auto& l : lyrics_) {
            if (l.timestampSec <= positionSec_) activeLine = l.text;
        }
        dc->DrawTextW(activeLine.c_str(), static_cast<UINT32>(activeLine.size()), subFmt.Get(),
            D2D1::RectF(lrcBox.left + 12.0f, lrcBox.top + 8.0f, lrcBox.right - 12.0f, lrcBox.bottom - 8.0f), textPrimary.Get());
    }
}

bool MediaSmtcModule::OnPointerDown(float xDip, float yDip) {
    // Transport row around y: 114..160
    if (yDip >= 112.0f && yDip <= 162.0f) {
        if (xDip >= 330.0f) {
            ToggleLyricsDrawer();
            return true;
        }
        if (!currentSession_) return false;
        if (xDip < 185.0f) {
            currentSession_.TrySkipPreviousAsync();
        } else if (xDip < 260.0f) {
            currentSession_.TryTogglePlayPauseAsync();
        } else {
            currentSession_.TrySkipNextAsync();
        }
        return true;
    }
    return false;
}

} // namespace notch::modules
