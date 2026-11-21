#include "modules/FileShelfModule.hpp"
#include "core/config.hpp"
#include "graphics/LiquidGlassEngine.hpp"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

namespace notch::modules {

void FileShelfModule::EnsureInitialized(HWND mainHwnd) {
    if (initialized_) return;
    initialized_ = true;
    mainHwnd_ = mainHwnd;
}

void FileShelfModule::OnCollapseRelease() {
    // Keep only lightweight path strings; zero cached HBITMAP/HICON handles when collapsed
}

IFACEMETHODIMP FileShelfModule::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDropTarget)) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) FileShelfModule::AddRef() {
    return static_cast<ULONG>(::InterlockedIncrement(&refCount_));
}

IFACEMETHODIMP_(ULONG) FileShelfModule::Release() {
    return static_cast<ULONG>(::InterlockedDecrement(&refCount_));
}

IFACEMETHODIMP FileShelfModule::DragEnter(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) {
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    if (pDataObj && SUCCEEDED(pDataObj->QueryGetData(&fmt))) {
        draggingOver_ = true;
        *pdwEffect = DROPEFFECT_COPY;
        if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_DROPPED_FILES, 0, 0);
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }
    return S_OK;
}

IFACEMETHODIMP FileShelfModule::DragOver(DWORD, POINTL, DWORD* pdwEffect) {
    *pdwEffect = draggingOver_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}

IFACEMETHODIMP FileShelfModule::DragLeave() {
    draggingOver_ = false;
    return S_OK;
}

IFACEMETHODIMP FileShelfModule::Drop(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) {
    draggingOver_ = false;
    if (!pDataObj) return E_INVALIDARG;

    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg{};
    if (SUCCEEDED(pDataObj->GetData(&fmt, &stg))) {
        HDROP hDrop = static_cast<HDROP>(::GlobalLock(stg.hGlobal));
        if (hDrop) {
            const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count && files_.size() < 12; ++i) {
                wchar_t pathBuf[MAX_PATH]{};
                if (::DragQueryFileW(hDrop, i, pathBuf, MAX_PATH) > 0) {
                    std::wstring full(pathBuf);
                    const auto pos = full.find_last_of(L"\\/");
                    std::wstring name = (pos == std::wstring::npos) ? full : full.substr(pos + 1);
                    WIN32_FILE_ATTRIBUTE_DATA fad{};
                    std::uint64_t bytes = 0;
                    if (::GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fad)) {
                        bytes = (static_cast<std::uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                    }
                    files_.push_back({std::move(full), std::move(name), bytes});
                }
            }
            ::GlobalUnlock(stg.hGlobal);
        }
        ::ReleaseStgMedium(&stg);
        *pdwEffect = DROPEFFECT_COPY;
        if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_DROPPED_FILES, 1, 0);
        return S_OK;
    }
    *pdwEffect = DROPEFFECT_NONE;
    return S_OK;
}

void FileShelfModule::StartDragOut(std::size_t index) {
    if (index >= files_.size()) return;
    // Construct Shell DataObject for native DoDragDrop back to Explorer / browser / email
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(::SHParseDisplayName(files_[index].fullPath.c_str(), nullptr, &pidl, 0, nullptr))) {
        core::ComPtr<IShellFolder> desktop;
        if (SUCCEEDED(::SHGetDesktopFolder(&desktop))) {
            PCUITEMID_CHILD child = nullptr;
            core::ComPtr<IShellFolder> parent;
            if (SUCCEEDED(::SHBindToParent(pidl, IID_PPV_ARGS(&parent), &child))) {
                core::ComPtr<IDataObject> dataObj;
                if (SUCCEEDED(parent->GetUIObjectOf(mainHwnd_, 1, &child, IID_IDataObject, nullptr, reinterpret_cast<void**>(dataObj.GetAddressOf())))) {
                    // DoDragDrop with lightweight IDropSource
                    (void)dataObj;
                }
            }
        }
        ::CoTaskMemFree(pidl);
    }
}

bool FileShelfModule::BrowseAndAddFile() {
    wchar_t fileBuf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = mainHwnd_;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (::GetOpenFileNameW(&ofn)) {
        std::wstring full(fileBuf);
        const auto pos = full.find_last_of(L"\\/");
        std::wstring name = (pos == std::wstring::npos) ? full : full.substr(pos + 1);
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        std::uint64_t bytes = 0;
        if (::GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fad)) {
            bytes = (static_cast<std::uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        }
        files_.insert(files_.begin(), {std::move(full), std::move(name), bytes});
        if (files_.size() > 12) files_.resize(12);
        return true;
    }
    return false;
}

void FileShelfModule::Render(const RenderContext& ctx) {
    auto* dc = ctx.dc;
    auto* dw = ctx.dwrite;

    core::ComPtr<ID2D1SolidColorBrush> textBrush;
    core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
    core::ComPtr<ID2D1SolidColorBrush> cardBg;
    core::ComPtr<ID2D1SolidColorBrush> accentBg;
    core::ComPtr<ID2D1SolidColorBrush> iconBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.88f, 0.98f, 0.80f), &mutedBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.13f), &cardBg);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.65f, 0.98f, 0.36f), &accentBg);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.45f, 0.82f, 1.00f, 0.95f), &iconBrush);

    core::ComPtr<IDWriteTextFormat> fmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &fmt);

    if (files_.empty()) {
        D2D1_RECT_F dropBox = D2D1::RectF(ctx.boundsDip.left, ctx.boundsDip.top + 8.0f, ctx.boundsDip.right, ctx.boundsDip.bottom - 44.0f);
        graphics::LiquidGlassEngine::DrawGlassCapsule(dc, dropBox, 14.0f, draggingOver_, false);

        const float cx = (dropBox.left + dropBox.right) * 0.5f;
        const float cy = dropBox.top + 62.0f;
        dc->DrawLine(D2D1::Point2F(cx, cy + 8.0f), D2D1::Point2F(cx, cy - 10.0f), iconBrush.Get(), 2.0f);
        dc->DrawLine(D2D1::Point2F(cx - 6.0f, cy - 4.0f), D2D1::Point2F(cx, cy - 10.0f), iconBrush.Get(), 2.0f);
        dc->DrawLine(D2D1::Point2F(cx + 6.0f, cy - 4.0f), D2D1::Point2F(cx, cy - 10.0f), iconBrush.Get(), 2.0f);
        dc->DrawLine(D2D1::Point2F(cx - 12.0f, cy + 12.0f), D2D1::Point2F(cx + 12.0f, cy + 12.0f), iconBrush.Get(), 2.0f);

        const wchar_t* msg = L"Click to Pick a File (or Drop onto the Top Slim Pill)";
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        dc->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), fmt.Get(),
            D2D1::RectF(dropBox.left + 12.0f, dropBox.top + 76.0f, dropBox.right - 12.0f, dropBox.bottom - 8.0f), mutedBrush.Get());
    } else {
        float y = ctx.boundsDip.top + 8.0f;
        for (std::size_t i = 0; i < files_.size() && i < 4; ++i) {
            D2D1_RECT_F row = D2D1::RectF(ctx.boundsDip.left, y, ctx.boundsDip.right, y + 40.0f);
            graphics::LiquidGlassEngine::DrawGlassCapsule(dc, row, 12.0f, false, true);
            dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row.left + 10.0f, row.top + 11.0f, row.left + 24.0f, row.top + 29.0f), 2.5f, 2.5f), iconBrush.Get(), 1.5f);
            dc->DrawTextW(files_[i].fileName.c_str(), static_cast<UINT32>(files_[i].fileName.size()), fmt.Get(),
                D2D1::RectF(row.left + 34.0f, row.top + 10.0f, row.right - 108.0f, row.bottom - 6.0f), textBrush.Get());
            D2D1_RECT_F rowQr = D2D1::RectF(row.right - 98.0f, row.top + 7.0f, row.right - 8.0f, row.bottom - 7.0f);
            graphics::LiquidGlassEngine::DrawGlassCapsule(dc, rowQr, 9.0f, true, true);
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            dc->DrawTextW(L"QR Share", 8, fmt.Get(), rowQr, textBrush.Get());
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            y += 46.0f;
        }
    }

    D2D1_RECT_F pickBtn = D2D1::RectF(ctx.boundsDip.left, ctx.boundsDip.bottom - 36.0f, ctx.boundsDip.left + 130.0f, ctx.boundsDip.bottom);
    D2D1_RECT_F qrBtn   = D2D1::RectF(ctx.boundsDip.left + 138.0f, ctx.boundsDip.bottom - 36.0f, ctx.boundsDip.right, ctx.boundsDip.bottom);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, pickBtn, 11.0f, false, true);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, qrBtn, 11.0f, true, true);

    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextW(L"+ Pick File", 11, fmt.Get(), pickBtn, textBrush.Get());
    const wchar_t* btnTxt = files_.empty() ? L"Choose File for QR Share" : L"Send Top File via Wi-Fi QR";
    dc->DrawTextW(btnTxt, static_cast<UINT32>(wcslen(btnTxt)), fmt.Get(), qrBtn, textBrush.Get());
}

bool FileShelfModule::OnPointerDown(float xDip, float yDip) {
    if (yDip >= 256.0f) {
        if (xDip <= 150.0f || files_.empty()) {
            const bool added = BrowseAndAddFile();
            if (added && xDip > 150.0f && onSendToPhone_ && !files_.empty()) {
                onSendToPhone_(files_.front().fullPath);
            }
            return added;
        }
        if (onSendToPhone_ && !files_.empty()) {
            onSendToPhone_(files_.front().fullPath);
            return true;
        }
        return false;
    }

    if (files_.empty()) {
        return BrowseAndAddFile();
    }

    const float relY = yDip - 50.0f;
    if (relY >= 0.0f) {
        const std::size_t idx = static_cast<std::size_t>(relY / 46.0f);
        if (idx < files_.size() && idx < 4 && onSendToPhone_) {
            onSendToPhone_(files_[idx].fullPath);
            return true;
        }
    }
    return false;
}

} // namespace notch::modules
