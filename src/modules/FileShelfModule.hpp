#pragma once

#include "modules/IModule.hpp"
#include "core/raii.hpp"

#include <oleidl.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace notch::modules {

struct ShelfFileItem {
    std::wstring fullPath;
    std::wstring fileName;
    std::uint64_t sizeBytes = 0;
};

class FileShelfModule final : public IModule, public IDropTarget {
public:
    void EnsureInitialized(HWND mainHwnd) override;
    void OnCollapseRelease() override;
    void Render(const RenderContext& ctx) override;
    bool OnPointerDown(float xDip, float yDip) override;

    void SetSendToPhoneCallback(std::function<void(const std::wstring&)> cb) {
        onSendToPhone_ = std::move(cb);
    }

    bool IsDraggingOver() const noexcept { return draggingOver_; }
    const std::vector<ShelfFileItem>& GetFiles() const noexcept { return files_; }
    void StartDragOut(std::size_t index);
    bool BrowseAndAddFile();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IDropTarget
    IFACEMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    IFACEMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    IFACEMETHODIMP DragLeave() override;
    IFACEMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;

private:
    LONG refCount_ = 1;
    HWND mainHwnd_ = nullptr;
    bool initialized_ = false;
    bool draggingOver_ = false;
    std::vector<ShelfFileItem> files_;
    std::function<void(const std::wstring&)> onSendToPhone_;
};

} // namespace notch::modules
