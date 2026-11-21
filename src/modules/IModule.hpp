#pragma once

#include <windows.h>
#include <d2d1_3.h>
#include <dwrite_3.h>

namespace notch::modules {

struct RenderContext {
    ID2D1RenderTarget* dc = nullptr;
    IDWriteFactory* dwrite = nullptr;
    D2D1_RECT_F boundsDip{};
    float dpiScale = 1.0f;
    bool isExpanded = false;
};

class IModule {
public:
    virtual ~IModule() = default;

    // Lazy initialization: modules allocate heavy WinRT/COM/socket objects ONLY when invoked
    virtual void EnsureInitialized(HWND mainHwnd) = 0;

    // Called immediately on collapse to free temporary bitmaps/caches and keep RAM < 20 MB
    virtual void OnCollapseRelease() = 0;

    // Render module content inside the Liquid Glass expanded hub
    virtual void Render(const RenderContext& ctx) = 0;

    // Handle left click in DIP coordinates relative to the hub window
    virtual bool OnPointerDown(float xDip, float yDip) = 0;
};

} // namespace notch::modules
