#include "modules/ClipboardModule.hpp"
#include "core/config.hpp"
#include "graphics/LiquidGlassEngine.hpp"

#include <wincodec.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstdio>

namespace notch::modules {

ClipboardModule::~ClipboardModule() {
    if (listenerRegistered_ && mainHwnd_) {
        ::RemoveClipboardFormatListener(mainHwnd_);
    }
}

void ClipboardModule::EnsureInitialized(HWND mainHwnd) {
    if (listenerRegistered_) return;
    mainHwnd_ = mainHwnd;
    if (::AddClipboardFormatListener(mainHwnd_)) {
        listenerRegistered_ = true;
    }
}

void ClipboardModule::OnCollapseRelease() {
    // Images remain JPEG-compressed inside the strict 10 MB ring-buffer cap
}

void ClipboardModule::EnforceRingCap(std::size_t incomingBytes) {
    while (!entries_.empty() && (totalRingBytes_ + incomingBytes > core::GeometryConfig::kClipboardMaxBytes)) {
        // Evict oldest unpinned entry first
        auto it = std::find_if(entries_.rbegin(), entries_.rend(), [](const ClipboardEntry& e) { return !e.pinned; });
        if (it == entries_.rend()) {
            totalRingBytes_ -= entries_.back().memoryBytes;
            entries_.pop_back();
        } else {
            auto fwd = std::next(it).base();
            totalRingBytes_ -= fwd->memoryBytes;
            entries_.erase(fwd);
        }
    }
}

bool ClipboardModule::CompressHBitmapToJpeg(HBITMAP hbm, std::vector<std::uint8_t>& outJpeg) {
    core::ComPtr<IWICImagingFactory> wic;
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        return false;
    }

    core::ComPtr<IWICBitmap> wicBmp;
    if (FAILED(wic->CreateBitmapFromHBITMAP(hbm, nullptr, WICBitmapIgnoreAlpha, &wicBmp))) {
        return false;
    }

    core::ComPtr<IStream> memStream;
    memStream.Attach(::SHCreateMemStream(nullptr, 0));
    if (!memStream) return false;

    core::ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder))) return false;
    if (FAILED(encoder->Initialize(memStream.Get(), WICBitmapEncoderNoCache))) return false;

    core::ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) return false;
    if (FAILED(frame->Initialize(nullptr))) return false;
    if (FAILED(frame->WriteSource(wicBmp.Get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    if (FAILED(encoder->Commit())) return false;

    STATSTG stat{};
    memStream->Stat(&stat, STATFLAG_NONAME);
    const std::size_t sz = static_cast<std::size_t>(stat.cbSize.QuadPart);
    outJpeg.resize(sz);
    LARGE_INTEGER zero{};
    memStream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG readBytes = 0;
    memStream->Read(outJpeg.data(), static_cast<ULONG>(sz), &readBytes);
    return readBytes > 0;
}

void ClipboardModule::OnClipboardUpdate() {
    if (ignoreNextSelfUpdate_) {
        ignoreNextSelfUpdate_ = false;
        return;
    }
    if (!::OpenClipboard(mainHwnd_)) return;

    if (::IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            const wchar_t* text = static_cast<const wchar_t*>(::GlobalLock(hData));
            if (text && wcslen(text) > 0) {
                std::wstring str(text, std::min<std::size_t>(wcslen(text), 2048));
                const bool isLink = (str.rfind(L"http://", 0) == 0 || str.rfind(L"https://", 0) == 0);
                const std::size_t memBytes = str.size() * sizeof(wchar_t);
                EnforceRingCap(memBytes);
                entries_.push_front({nextId_++, isLink ? ClipEntryKind::Link : ClipEntryKind::Text, false, std::move(str), {}, memBytes});
                totalRingBytes_ += memBytes;
            }
            ::GlobalUnlock(hData);
        }
    } else if (::IsClipboardFormatAvailable(CF_BITMAP)) {
        HBITMAP hbm = static_cast<HBITMAP>(::GetClipboardData(CF_BITMAP));
        if (hbm) {
            std::vector<std::uint8_t> jpeg;
            if (CompressHBitmapToJpeg(hbm, jpeg)) {
                const std::size_t memBytes = jpeg.size();
                EnforceRingCap(memBytes);
                wchar_t label[64];
                std::swprintf(label, 64, L"[Image JPEG · %zu KB]", memBytes / 1024);
                entries_.push_front({nextId_++, ClipEntryKind::ImageJpeg, false, label, std::move(jpeg), memBytes});
                totalRingBytes_ += memBytes;
            }
        }
    }

    ::CloseClipboard();
}

void ClipboardModule::TogglePin(std::size_t index) {
    if (index < entries_.size()) {
        entries_[index].pinned = !entries_[index].pinned;
    }
}

void ClipboardModule::CopyEntryBackToClipboard(std::size_t index) {
    if (index >= entries_.size()) return;
    if (entries_[index].kind == ClipEntryKind::ImageJpeg) return;

    if (::OpenClipboard(mainHwnd_)) {
        ::EmptyClipboard();
        const auto& txt = entries_[index].previewText;
        HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, (txt.size() + 1) * sizeof(wchar_t));
        if (hMem) {
            wchar_t* dst = static_cast<wchar_t*>(::GlobalLock(hMem));
            std::copy(txt.begin(), txt.end(), dst);
            dst[txt.size()] = L'\0';
            ::GlobalUnlock(hMem);
            ignoreNextSelfUpdate_ = true;
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        }
        ::CloseClipboard();
    }
}

void ClipboardModule::Render(const RenderContext& ctx) {
    auto* dc = ctx.dc;
    auto* dw = ctx.dwrite;

    core::ComPtr<ID2D1SolidColorBrush> textBrush;
    core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
    core::ComPtr<ID2D1SolidColorBrush> accentBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.84f, 0.90f, 0.98f, 0.78f), &mutedBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.36f, 0.82f, 1.00f, 0.95f), &accentBrush);

    core::ComPtr<IDWriteTextFormat> fmt;
    core::ComPtr<IDWriteTextFormat> badgeFmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &fmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &badgeFmt);

    wchar_t header[96];
    std::swprintf(header, 96, L"Clipboard History: %zu KB / 10 MB (%zu items)",
        totalRingBytes_ / 1024, entries_.size());
    dc->DrawTextW(header, static_cast<UINT32>(wcslen(header)), badgeFmt.Get(),
        D2D1::RectF(ctx.boundsDip.left + 4.0f, ctx.boundsDip.top + 2.0f, ctx.boundsDip.right, ctx.boundsDip.top + 22.0f), mutedBrush.Get());

    float y = ctx.boundsDip.top + 28.0f;
    for (std::size_t i = 0; i < entries_.size() && i < 5; ++i) {
        D2D1_RECT_F row = D2D1::RectF(ctx.boundsDip.left, y, ctx.boundsDip.right, y + 40.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, row, 12.0f, entries_[i].pinned, true);
        dc->DrawTextW(entries_[i].previewText.c_str(), static_cast<UINT32>(std::min<std::size_t>(entries_[i].previewText.size(), 58)), fmt.Get(),
            D2D1::RectF(row.left + 12.0f, row.top + 10.0f, row.right - 74.0f, row.bottom - 6.0f), textBrush.Get());

        D2D1_RECT_F pinBtn = D2D1::RectF(row.right - 64.0f, row.top + 7.0f, row.right - 8.0f, row.bottom - 7.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, pinBtn, 9.0f, entries_[i].pinned, true);
        badgeFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        badgeFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        dc->DrawTextW(entries_[i].pinned ? L"Pinned" : L"Pin", entries_[i].pinned ? 6 : 3, badgeFmt.Get(),
            pinBtn, entries_[i].pinned ? accentBrush.Get() : mutedBrush.Get());
        y += 46.0f;
    }
}

bool ClipboardModule::OnPointerDown(float xDip, float yDip) {
    const float relY = yDip - 70.0f;
    if (relY < 0.0f) return false;
    const std::size_t idx = static_cast<std::size_t>(relY / 46.0f);
    if (idx >= entries_.size()) return false;
    if (xDip >= 390.0f) {
        TogglePin(idx);
    } else {
        CopyEntryBackToClipboard(idx);
    }
    return true;
}

} // namespace notch::modules
