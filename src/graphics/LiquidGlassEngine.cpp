#include "graphics/LiquidGlassEngine.hpp"

#include <algorithm>
#include <cmath>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace notch::graphics {

LiquidGlassEngine::~LiquidGlassEngine() {
    ReleaseBackdropResources();
    ReleaseLayerBitmap();
    if (memDc_) {
        ::DeleteDC(memDc_);
        memDc_ = nullptr;
    }
    if (backdropDc_) {
        ::DeleteDC(backdropDc_);
        backdropDc_ = nullptr;
    }
}

bool LiquidGlassEngine::Initialize(HWND targetHwnd, float dpiScale) {
    hwnd_ = targetHwnd;
    dpiScale_ = std::max(1.0f, dpiScale);
    if (!CreateDeviceResources()) {
        return false;
    }
    // Exclude NotchGlass from BitBlt screen capture so we can blur & refract the real desktop behind it
    excludeCaptureOk_ = (::SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE) != FALSE);
    Resize(core::GeometryConfig::kCollapsedWidthDip, core::GeometryConfig::kCollapsedHeightDip, dpiScale_);
    return true;
}

bool LiquidGlassEngine::CreateDeviceResources() {
    if (!d2dFactory_) {
        D2D1_FACTORY_OPTIONS options{};
        if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, reinterpret_cast<void**>(d2dFactory_.GetAddressOf())))) {
            return false;
        }
    }

    dcTarget_.Reset();
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT
    );
    if (FAILED(d2dFactory_->CreateDCRenderTarget(&props, &dcTarget_)) || !dcTarget_) {
        props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        if (FAILED(d2dFactory_->CreateDCRenderTarget(&props, &dcTarget_)) || !dcTarget_) {
            return false;
        }
    }

    if (!dwriteFactory_) {
        if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())))) {
            return false;
        }
    }

    if (!memDc_) {
        memDc_ = ::CreateCompatibleDC(nullptr);
        if (!memDc_) return false;
    }

    return true;
}

void LiquidGlassEngine::ReleaseLayerBitmap() {
    if (memDc_ && prevBitmap_) {
        ::SelectObject(memDc_, prevBitmap_);
        prevBitmap_ = nullptr;
    }
    if (dibBitmap_) {
        ::DeleteObject(dibBitmap_);
        dibBitmap_ = nullptr;
    }
    allocWidthPx_ = 0;
    allocHeightPx_ = 0;
}

void LiquidGlassEngine::ReleaseBackdropResources() {
    backdropD2dBmp_.Reset();
    if (backdropDc_ && prevBackdropBmp_) {
        ::SelectObject(backdropDc_, prevBackdropBmp_);
        prevBackdropBmp_ = nullptr;
    }
    if (backdropDib_) {
        ::DeleteObject(backdropDib_);
        backdropDib_ = nullptr;
    }
    backdropPixels_ = nullptr;
    backdropAllocW_ = 0;
    backdropAllocH_ = 0;
    backdropW_ = 0;
    backdropH_ = 0;
}

bool LiquidGlassEngine::EnsureLayerBitmap(UINT widthPx, UINT heightPx) {
    widthPx = std::max(widthPx, 2u);
    heightPx = std::max(heightPx, 2u);
    curWidthPx_ = widthPx;
    curHeightPx_ = heightPx;

    if (dibBitmap_ && widthPx <= allocWidthPx_ && heightPx <= allocHeightPx_) {
        return true;
    }

    const UINT newAllocW = std::max(widthPx, static_cast<UINT>(std::ceil(560.0f * dpiScale_)));
    const UINT newAllocH = std::max(heightPx, static_cast<UINT>(std::ceil(360.0f * dpiScale_)));
    ReleaseLayerBitmap();

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(newAllocW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(newAllocH);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    dibBitmap_ = ::CreateDIBSection(memDc_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dibBitmap_) {
        return false;
    }

    prevBitmap_ = ::SelectObject(memDc_, dibBitmap_);
    allocWidthPx_ = newAllocW;
    allocHeightPx_ = newAllocH;
    return true;
}

bool LiquidGlassEngine::EnsureBackdropResources(UINT widthPx, UINT heightPx) {
    if (!dcTarget_) return false;
    if (!backdropDc_) {
        backdropDc_ = ::CreateCompatibleDC(nullptr);
        if (!backdropDc_) return false;
    }

    widthPx = std::max(widthPx, 4u);
    heightPx = std::max(heightPx, 4u);
    if (backdropDib_ && widthPx <= backdropAllocW_ && heightPx <= backdropAllocH_ && backdropD2dBmp_) {
        backdropW_ = widthPx;
        backdropH_ = heightPx;
        return true;
    }

    const UINT allocW = std::max(widthPx, static_cast<UINT>(std::ceil(560.0f * dpiScale_)));
    const UINT allocH = std::max(heightPx, static_cast<UINT>(std::ceil(360.0f * dpiScale_)));
    ReleaseBackdropResources();

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(allocW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(allocH);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    backdropDib_ = ::CreateDIBSection(backdropDc_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!backdropDib_ || !bits) {
        return false;
    }
    prevBackdropBmp_ = ::SelectObject(backdropDc_, backdropDib_);
    backdropPixels_ = static_cast<std::uint32_t*>(bits);
    backdropAllocW_ = allocW;
    backdropAllocH_ = allocH;
    backdropW_ = widthPx;
    backdropH_ = heightPx;

    D2D1_BITMAP_PROPERTIES bprops = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * dpiScale_,
        96.0f * dpiScale_
    );
    if (FAILED(dcTarget_->CreateBitmap(D2D1::SizeU(allocW, allocH), bprops, &backdropD2dBmp_))) {
        return false;
    }
    return true;
}

void LiquidGlassEngine::UpdateRefractedBackdrop(int windowXPx, int windowYPx, UINT wPx, UINT hPx, const core::GlassThemeColors& theme) {
    if (!excludeCaptureOk_ || !EnsureBackdropResources(wPx, hPx)) return;

    const ULONGLONG now = ::GetTickCount64();
    if (now - lastBackdropTick_ >= 65 || downBufB_.empty()) {
        lastBackdropTick_ = now;

        HDC screenDc = ::GetDC(nullptr);
        if (!screenDc) return;
        ::BitBlt(backdropDc_, 0, 0, static_cast<int>(wPx), static_cast<int>(hPx), screenDc, windowXPx, windowYPx, SRCCOPY);
        ::ReleaseDC(nullptr, screenDc);

        // Downsample 4x -> separable 2-pass blur -> optical vibrancy + curved edge meniscus refraction
        const int dw = std::max(8, static_cast<int>(wPx / 4));
        const int dh = std::max(4, static_cast<int>(hPx / 4));
        downBufA_.resize(static_cast<std::size_t>(dw * dh));
        downBufB_.resize(static_cast<std::size_t>(dw * dh));

        for (int y = 0; y < dh; ++y) {
            const UINT sy = std::min(hPx - 1, static_cast<UINT>(y * 4 + 2));
            const std::uint32_t* srcRow = backdropPixels_ + sy * backdropAllocW_;
            for (int x = 0; x < dw; ++x) {
                const UINT sx = std::min(wPx - 1, static_cast<UINT>(x * 4 + 2));
                downBufA_[static_cast<std::size_t>(y * dw + x)] = srcRow[sx];
            }
        }

        auto blurPass = [&](const std::vector<std::uint32_t>& in, std::vector<std::uint32_t>& out, int radius) {
            // Horizontal pass into out, vertical in-place via downBufA_ swap
            std::vector<std::uint32_t> tmp(in.size());
            const int div = radius * 2 + 1;
            for (int y = 0; y < dh; ++y) {
                for (int x = 0; x < dw; ++x) {
                    int r = 0, g = 0, b = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        const int ix = std::clamp(x + k, 0, dw - 1);
                        const std::uint32_t p = in[static_cast<std::size_t>(y * dw + ix)];
                        b += (p & 0xFF);
                        g += ((p >> 8) & 0xFF);
                        r += ((p >> 16) & 0xFF);
                    }
                    tmp[static_cast<std::size_t>(y * dw + x)] =
                        (0xFFu << 24) |
                        (static_cast<std::uint32_t>(r / div) << 16) |
                        (static_cast<std::uint32_t>(g / div) << 8) |
                        static_cast<std::uint32_t>(b / div);
                }
            }
            for (int y = 0; y < dh; ++y) {
                for (int x = 0; x < dw; ++x) {
                    int r = 0, g = 0, b = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        const int iy = std::clamp(y + k, 0, dh - 1);
                        const std::uint32_t p = tmp[static_cast<std::size_t>(iy * dw + x)];
                        b += (p & 0xFF);
                        g += ((p >> 8) & 0xFF);
                        r += ((p >> 16) & 0xFF);
                    }
                    out[static_cast<std::size_t>(y * dw + x)] =
                        (0xFFu << 24) |
                        (static_cast<std::uint32_t>(r / div) << 16) |
                        (static_cast<std::uint32_t>(g / div) << 8) |
                        static_cast<std::uint32_t>(b / div);
                }
            }
        };

        blurPass(downBufA_, downBufB_, 3);
        blurPass(downBufB_, downBufA_, 3);

        // Apply Apple saturation boost + subtle tint & refractive edge warp on upsample
        refractBuf_.resize(static_cast<std::size_t>(wPx * hPx));
        const float sat = theme.saturation;
        const float tintAlpha = (preset_ == core::GlassPreset::Obsidian) ? 0.44f :
                                (preset_ == core::GlassPreset::Graphite) ? 0.28f : 0.14f;
        const float tr = theme.tint.r * 255.0f;
        const float tg = theme.tint.g * 255.0f;
        const float tb = theme.tint.b * 255.0f;

        const float halfW = static_cast<float>(wPx) * 0.5f;
        const float halfH = static_cast<float>(hPx) * 0.5f;
        const float edgeDispScale = theme.refractionPx * 0.45f;

        for (UINT y = 0; y < hPx; ++y) {
            const float ny = (static_cast<float>(y) - halfH) / std::max(1.0f, halfH);
            for (UINT x = 0; x < wPx; ++x) {
                const float nx = (static_cast<float>(x) - halfW) / std::max(1.0f, halfW);
                const float edgeDist = std::max(std::abs(nx), std::abs(ny));
                const float bevel = std::clamp((edgeDist - 0.72f) / 0.28f, 0.0f, 1.0f);
                const float warp = bevel * bevel * edgeDispScale;

                const float srcX = std::clamp(static_cast<float>(x) - nx * warp, 0.0f, static_cast<float>(wPx - 1)) * 0.25f;
                const float srcY = std::clamp(static_cast<float>(y) - ny * warp, 0.0f, static_cast<float>(hPx - 1)) * 0.25f;

                const int ix = std::clamp(static_cast<int>(srcX), 0, dw - 1);
                const int iy = std::clamp(static_cast<int>(srcY), 0, dh - 1);
                const std::uint32_t p = downBufA_[static_cast<std::size_t>(iy * dw + ix)];

                float pb = static_cast<float>(p & 0xFF);
                float pg = static_cast<float>((p >> 8) & 0xFF);
                float pr = static_cast<float>((p >> 16) & 0xFF);

                // Apple optical vibrancy & readability scrim so white text stays crisp over bright wallpapers
                const float luma = 0.2126f * pr + 0.7152f * pg + 0.0722f * pb;
                pr = luma + (pr - luma) * sat;
                pg = luma + (pg - luma) * sat;
                pb = luma + (pb - luma) * sat;

                // Blend gentle dark optical readability veil (stronger on bright backdrops) + preset tint
                const float darkVeil = (luma > 140.0f) ? 0.34f : 0.22f;
                pr = pr * (1.0f - darkVeil) * (1.0f - tintAlpha) + tr * tintAlpha;
                pg = pg * (1.0f - darkVeil) * (1.0f - tintAlpha) + tg * tintAlpha;
                pb = pb * (1.0f - darkVeil) * (1.0f - tintAlpha) + tb * tintAlpha;

                // Subtle chromatic/specular meniscus lift along outer rim
                const float rimBoost = bevel * bevel * 24.0f;
                const std::uint32_t outR = static_cast<std::uint32_t>(std::clamp(pr + rimBoost * 0.90f, 0.0f, 255.0f));
                const std::uint32_t outG = static_cast<std::uint32_t>(std::clamp(pg + rimBoost * 0.96f, 0.0f, 255.0f));
                const std::uint32_t outB = static_cast<std::uint32_t>(std::clamp(pb + rimBoost * 1.10f, 0.0f, 255.0f));

                refractBuf_[static_cast<std::size_t>(y * wPx + x)] =
                    (0xFFu << 24) | (outR << 16) | (outG << 8) | outB;
            }
        }

        D2D1_RECT_U dstRect = D2D1::RectU(0, 0, wPx, hPx);
        backdropD2dBmp_->CopyFromMemory(&dstRect, refractBuf_.data(), wPx * 4);
    }
}

void LiquidGlassEngine::Resize(float widthDip, float heightDip, float dpiScale) {
    dpiScale_ = std::max(1.0f, dpiScale);
    const UINT wPx = static_cast<UINT>(std::ceil(widthDip * dpiScale_));
    const UINT hPx = static_cast<UINT>(std::ceil(heightDip * dpiScale_));
    EnsureLayerBitmap(wPx, hPx);
}

float LiquidGlassEngine::SampleTopEdgeLuminance(RECT monitorRect) {
    HDC screenDc = ::GetDC(nullptr);
    if (!screenDc) return cachedLuma_;

    const int sampleX = monitorRect.left + ((monitorRect.right - monitorRect.left) / 2) - 4;
    const int sampleY = monitorRect.top + 2;

    double lumaSum = 0.0;
    for (int i = 0; i < 4; ++i) {
        const COLORREF c = ::GetPixel(screenDc, sampleX + i * 2, sampleY);
        const double r = GetRValue(c) / 255.0;
        const double g = GetGValue(c) / 255.0;
        const double b = GetBValue(c) / 255.0;
        lumaSum += (0.2126 * r + 0.7152 * g + 0.0722 * b);
    }
    ::ReleaseDC(nullptr, screenDc);

    cachedLuma_ = static_cast<float>(lumaSum / 4.0);
    return cachedLuma_;
}

void LiquidGlassEngine::SetGlassPreset(core::GlassPreset preset) {
    preset_ = preset;
    lastBackdropTick_ = 0;
}

void LiquidGlassEngine::DrawGlassCapsule(
    ID2D1RenderTarget* dc,
    D2D1_RECT_F rect,
    float radius,
    bool highlighted,
    bool innerControl
) {
    if (!dc) return;
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, radius, radius);

    // Subtle frosted translucent body + directional glass highlight gradient
    const float topA = highlighted ? (innerControl ? 0.34f : 0.24f) : (innerControl ? 0.16f : 0.11f);
    const float botA = highlighted ? (innerControl ? 0.18f : 0.12f) : (innerControl ? 0.07f : 0.05f);

    D2D1_GRADIENT_STOP stops[2]{
        {0.0f, highlighted ? D2D1::ColorF(0.65f, 0.88f, 1.00f, topA) : D2D1::ColorF(1.0f, 1.0f, 1.0f, topA)},
        {1.0f, D2D1::ColorF(0.85f, 0.92f, 1.00f, botA)}
    };
    core::ComPtr<ID2D1GradientStopCollection> sc;
    core::ComPtr<ID2D1LinearGradientBrush> fillBrush;
    if (SUCCEEDED(dc->CreateGradientStopCollection(stops, 2, &sc))) {
        dc->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.left, rect.bottom)),
            sc.Get(),
            &fillBrush
        );
    }
    if (fillBrush) {
        dc->FillRoundedRectangle(rr, fillBrush.Get());
    }

    // Apple-style specular glass catchlight rim wrapping tightly around the capsule
    core::ComPtr<ID2D1SolidColorBrush> rimBrush;
    dc->CreateSolidColorBrush(
        highlighted ? D2D1::ColorF(0.75f, 0.93f, 1.00f, 0.58f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, innerControl ? 0.26f : 0.20f),
        &rimBrush
    );
    if (rimBrush) {
        dc->DrawRoundedRectangle(rr, rimBrush.Get(), highlighted ? 1.15f : 0.85f);
    }
}

void LiquidGlassEngine::DrawLiquidGlassSurface(
    float widthDip,
    float heightDip,
    float cornerRadiusDip,
    float expandProgress,
    const core::GlassThemeColors& theme
) {
    const bool isSlim = (expandProgress < 0.08f && heightDip < 14.0f);
    const float inset = isSlim ? 0.5f : 1.5f;
    const D2D1_RECT_F rect = D2D1::RectF(
        inset,
        0.0f,
        std::max(inset + 2.0f, widthDip - inset),
        std::max(2.0f, heightDip - inset)
    );
    const D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, cornerRadiusDip, cornerRadiusDip);

    if (!isSlim) {
        core::ComPtr<ID2D1SolidColorBrush> shadowBrush;
        dcTarget_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f * std::clamp(expandProgress, 0.25f, 1.0f)), &shadowBrush);
        if (shadowBrush) {
            D2D1_ROUNDED_RECT shadowRect = D2D1::RoundedRect(
                D2D1::RectF(rect.left - 1.0f, rect.top + 1.0f, rect.right + 1.0f, rect.bottom + 1.5f),
                cornerRadiusDip + 1.0f,
                cornerRadiusDip + 1.0f
            );
            dcTarget_->FillRoundedRectangle(shadowRect, shadowBrush.Get());
        }
    }

    // Clip to rounded Liquid Glass shape and composite the real refracted desktop backdrop when available
    core::ComPtr<ID2D1RoundedRectangleGeometry> pillGeom;
    bool pushedLayer = false;
    if (!isSlim && excludeCaptureOk_ && backdropD2dBmp_ && d2dFactory_ &&
        SUCCEEDED(d2dFactory_->CreateRoundedRectangleGeometry(rrect, &pillGeom))) {
        D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters(
            D2D1::InfiniteRect(),
            pillGeom.Get(),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(),
            1.0f,
            nullptr,
            D2D1_LAYER_OPTIONS_NONE
        );
        dcTarget_->PushLayer(&lp, nullptr);
        pushedLayer = true;

        dcTarget_->DrawBitmap(
            backdropD2dBmp_.Get(),
            D2D1::RectF(0.0f, 0.0f, widthDip, heightDip),
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(backdropW_) / dpiScale_, static_cast<float>(backdropH_) / dpiScale_)
        );
    }

    // Translucent Apple Liquid Glass specular glaze (light & crystal clear, never muddy dark gray)
    const float topA = isSlim ? 0.72f : (preset_ == core::GlassPreset::Obsidian ? 0.56f : 0.24f);
    const float midA = isSlim ? 0.62f : (preset_ == core::GlassPreset::Obsidian ? 0.44f : 0.12f);
    const float botA = isSlim ? 0.80f : (preset_ == core::GlassPreset::Obsidian ? 0.58f : 0.20f);

    D2D1_GRADIENT_STOP stops[3]{
        {0.00f, D2D1::ColorF(theme.tint.r, theme.tint.g, theme.tint.b, topA)},
        {0.50f, D2D1::ColorF(theme.tint.r * 0.70f, theme.tint.g * 0.76f, theme.tint.b * 0.88f, midA)},
        {1.00f, D2D1::ColorF(0.10f, 0.16f, 0.26f, botA)}
    };
    core::ComPtr<ID2D1GradientStopCollection> stopCol;
    core::ComPtr<ID2D1LinearGradientBrush> glassBrush;
    if (SUCCEEDED(dcTarget_->CreateGradientStopCollection(stops, 3, &stopCol))) {
        dcTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(widthDip * 0.35f, heightDip)),
            stopCol.Get(),
            &glassBrush
        );
    }
    if (glassBrush) {
        dcTarget_->FillRoundedRectangle(rrect, glassBrush.Get());
    }

    // Caustic sheen across top curved meniscus
    if (!isSlim) {
        D2D1_GRADIENT_STOP sheenStops[2]{
            {0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.22f)},
            {1.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.00f)}
        };
        core::ComPtr<ID2D1GradientStopCollection> sheenCol;
        core::ComPtr<ID2D1LinearGradientBrush> sheenBrush;
        if (SUCCEEDED(dcTarget_->CreateGradientStopCollection(sheenStops, 2, &sheenCol))) {
            dcTarget_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(widthDip * 0.55f, heightDip * 0.45f)),
                sheenCol.Get(),
                &sheenBrush
            );
            if (sheenBrush) {
                dcTarget_->FillRoundedRectangle(rrect, sheenBrush.Get());
            }
        }
    }

    if (pushedLayer) {
        dcTarget_->PopLayer();
    }

    // Bright dual-edge Apple Liquid Glass rim catchlight
    core::ComPtr<ID2D1SolidColorBrush> rimBrush;
    dcTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.86f, 0.95f, 1.00f, isSlim ? 0.88f : 0.58f),
        &rimBrush
    );
    if (rimBrush) {
        dcTarget_->DrawRoundedRectangle(rrect, rimBrush.Get(), isSlim ? 1.0f : 1.35f);
    }
}

void LiquidGlassEngine::RenderFrame(
    int windowXPx,
    int windowYPx,
    float widthDip,
    float heightDip,
    float cornerRadiusDip,
    float expandProgress,
    const std::function<void(ID2D1RenderTarget*, IDWriteFactory*, D2D1_RECT_F)>& drawOverlayContent
) {
    if (!hwnd_ || !memDc_) return;
    if (!dcTarget_ && !CreateDeviceResources()) return;

    const UINT wPx = static_cast<UINT>(std::ceil(widthDip * dpiScale_));
    const UINT hPx = static_cast<UINT>(std::ceil(heightDip * dpiScale_));
    if (!EnsureLayerBitmap(wPx, hPx)) return;

    if (expandProgress > 0.08f || heightDip >= 14.0f) {
        UpdateRefractedBackdrop(windowXPx, windowYPx, wPx, hPx, core::GetThemePreset(preset_));
    }

    RECT bindRect{0, 0, static_cast<LONG>(curWidthPx_), static_cast<LONG>(curHeightPx_)};
    if (FAILED(dcTarget_->BindDC(memDc_, &bindRect))) {
        return;
    }

    dcTarget_->SetDpi(96.0f * dpiScale_, 96.0f * dpiScale_);
    dcTarget_->BeginDraw();
    dcTarget_->Clear(D2D1::ColorF(0, 0, 0, 0));

    const auto theme = core::GetThemePreset(preset_);
    DrawLiquidGlassSurface(widthDip, heightDip, cornerRadiusDip, expandProgress, theme);

    if (drawOverlayContent) {
        drawOverlayContent(dcTarget_.Get(), dwriteFactory_.Get(), D2D1::RectF(0, 0, widthDip, heightDip));
    }

    const HRESULT hr = dcTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        dcTarget_.Reset();
        ReleaseBackdropResources();
        return;
    }

    POINT dstPt{windowXPx, windowYPx};
    SIZE wndSize{static_cast<LONG>(curWidthPx_), static_cast<LONG>(curHeightPx_)};
    POINT srcPt{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    HDC screenDc = ::GetDC(nullptr);
    ::UpdateLayeredWindow(hwnd_, screenDc, &dstPt, &wndSize, memDc_, &srcPt, 0, &blend, ULW_ALPHA);
    if (screenDc) ::ReleaseDC(nullptr, screenDc);
}

void LiquidGlassEngine::DrawQrGeometry(
    ID2D1RenderTarget* dc,
    const std::vector<std::uint8_t>& modules,
    int gridSize,
    D2D1_RECT_F targetRect,
    D2D1_COLOR_F fgColor,
    D2D1_COLOR_F bgColor
) {
    if (!dc || gridSize <= 0 || modules.size() < static_cast<std::size_t>(gridSize * gridSize)) return;

    core::ComPtr<ID2D1SolidColorBrush> bgBrush;
    core::ComPtr<ID2D1SolidColorBrush> fgBrush;
    dc->CreateSolidColorBrush(bgColor, &bgBrush);
    dc->CreateSolidColorBrush(fgColor, &fgBrush);

    dc->FillRoundedRectangle(D2D1::RoundedRect(targetRect, 8.0f, 8.0f), bgBrush.Get());

    const float pad = 13.0f;
    const float avail = std::min(targetRect.right - targetRect.left, targetRect.bottom - targetRect.top) - pad * 2.0f;
    const float cell = avail / static_cast<float>(gridSize);

    for (int y = 0; y < gridSize; ++y) {
        for (int x = 0; x < gridSize; ++x) {
            if (modules[static_cast<std::size_t>(y * gridSize + x)]) {
                const float rx = targetRect.left + pad + x * cell;
                const float ry = targetRect.top + pad + y * cell;
                dc->FillRectangle(D2D1::RectF(rx, ry, rx + cell + 0.25f, ry + cell + 0.25f), fgBrush.Get());
            }
        }
    }
}

void LiquidGlassEngine::TrimGpuMemory() {
    ReleaseBackdropResources();
    std::vector<std::uint32_t>().swap(downBufA_);
    std::vector<std::uint32_t>().swap(downBufB_);
    std::vector<std::uint32_t>().swap(refractBuf_);
    ::SetProcessWorkingSetSize(::GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

} // namespace notch::graphics
