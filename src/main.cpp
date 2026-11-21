#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.h>

#include "core/raii.hpp"
#include "window/SlimLineWindow.hpp"

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_ LPWSTR,
    _In_ int
) {
    // Per-Monitor V2 DPI awareness for crisp 4px slim line & Liquid Glass across mixed-DPI displays
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Single-instance guard: if already open in background, pop the notch open
    notch::core::UniqueHandle singleInstanceMutex(
        ::CreateMutexW(nullptr, TRUE, L"Local\\NotchGlass_SingleInstance_Mutex")
    );
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = ::FindWindowW(L"NotchGlass_MainOverlay", nullptr)) {
            ::PostMessageW(existing, notch::core::WM_APP_TOGGLE_NOTCH, 1, 0);
        }
        return 0;
    }

    // Initialize WinRT + OLE/COM on Single-Threaded Apartment (required for IDropTarget & SMTC)
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    notch::window::SlimLineWindow appWindow(hInstance);
    if (!appWindow.Create()) {
        ::MessageBoxW(nullptr, L"NotchGlass could not initialize Direct2D/DirectComposition on this display adapter.", L"NotchGlass Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Event-driven message pump: GetMessageW blocks in kernel with 0% CPU when idle
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    winrt::uninit_apartment();
    return static_cast<int>(msg.wParam);
}
