#pragma once

#include <windows.h>
#include <d2d1_3.h>
#include <dwrite_3.h>

#include "core/config.hpp"
#include "core/raii.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace notch::graphics {

// Universal Direct2D + Optical Refraction Liquid Glass Engine
// Captures real desktop backdrop behind the notch (with WDA_EXCLUDEFROMCAPTURE),
// applies separable Gaussian blur + Apple vibrancy + curved perimeter lens refraction,
// and wraps UI controls in specular glass capsules.
class LiquidGlassEngine {
public:
    ~LiquidGlassEngine();

    bool Initialize(HWND targetHwnd, float dpiScale);
    void Resize(float widthDip, float heightDip, float dpiScale);

    // Adaptive luminance sampling behind the top edge (called on expand, never in idle loop)
    float SampleTopEdgeLuminance(RECT monitorRect);

    void SetGlassPreset(core::GlassPreset preset);
    core::GlassPreset GetGlassPreset() const noexcept { return preset_; }
    float GetCachedLuma() const noexcept { return cachedLuma_; }

    // Render one per-pixel-alpha Liquid Glass frame and commit atomically via UpdateLayeredWindow
    void RenderFrame(
        int windowXPx,
        int windowYPx,
        float widthDip,
        float heightDip,
        float cornerRadiusDip,
        float expandProgress,
        const std::function<void(ID2D1RenderTarget*, IDWriteFactory*, D2D1_RECT_F)>& drawOverlayContent
    );

    // Draw an Apple-style frosted specular glass capsule wrapping text/controls
    static void DrawGlassCapsule(
        ID2D1RenderTarget* dc,
        D2D1_RECT_F rect,
        float radius,
        bool highlighted,
        bool innerControl
    );

    // Draw a QR matrix directly as crisp Direct2D vector geometry
    static void DrawQrGeometry(
        ID2D1RenderTarget* dc,
        const std::vector<std::uint8_t>& modules,
        int gridSize,
        D2D1_RECT_F targetRect,
        D2D1_COLOR_F fgColor,
        D2D1_COLOR_F bgColor
    );

    // Reclaim excess working-set memory immediately on collapse
    void TrimGpuMemory();

private:
    bool CreateDeviceResources();
    bool EnsureLayerBitmap(UINT widthPx, UINT heightPx);
    void ReleaseLayerBitmap();
    bool EnsureBackdropResources(UINT widthPx, UINT heightPx);
    void ReleaseBackdropResources();
    void UpdateRefractedBackdrop(int windowXPx, int windowYPx, UINT wPx, UINT hPx, const core::GlassThemeColors& theme);
    void DrawLiquidGlassSurface(
        float widthDip,
        float heightDip,
        float cornerRadiusDip,
        float expandProgress,
        const core::GlassThemeColors& theme
    );

    HWND hwnd_ = nullptr;
    float dpiScale_ = 1.0f;
    float cachedLuma_ = 0.45f;
    bool excludeCaptureOk_ = false;
    ULONGLONG lastBackdropTick_ = 0;
    core::GlassPreset preset_ = core::GlassPreset::LiquidGlass;

    core::ComPtr<ID2D1Factory1> d2dFactory_;
    core::ComPtr<ID2D1DCRenderTarget> dcTarget_;
    core::ComPtr<IDWriteFactory> dwriteFactory_;

    HDC memDc_ = nullptr;
    HBITMAP dibBitmap_ = nullptr;
    HGDIOBJ prevBitmap_ = nullptr;
    UINT allocWidthPx_ = 0;
    UINT allocHeightPx_ = 0;
    UINT curWidthPx_ = 0;
    UINT curHeightPx_ = 0;

    // Live desktop refraction buffer (allocated on expand, released on TrimGpuMemory)
    HDC backdropDc_ = nullptr;
    HBITMAP backdropDib_ = nullptr;
    HGDIOBJ prevBackdropBmp_ = nullptr;
    std::uint32_t* backdropPixels_ = nullptr;
    UINT backdropAllocW_ = 0;
    UINT backdropAllocH_ = 0;
    UINT backdropW_ = 0;
    UINT backdropH_ = 0;
    std::vector<std::uint32_t> downBufA_;
    std::vector<std::uint32_t> downBufB_;
    std::vector<std::uint32_t> refractBuf_;
    core::ComPtr<ID2D1Bitmap> backdropD2dBmp_;
    core::ComPtr<ID2D1RoundedRectangleGeometry> clipGeom_;
};

} // namespace notch::graphics
