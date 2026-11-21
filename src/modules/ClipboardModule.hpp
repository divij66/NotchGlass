#pragma once

#include "modules/IModule.hpp"
#include "core/raii.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace notch::modules {

enum class ClipEntryKind : std::uint8_t {
    Text = 0,
    Link = 1,
    ImageJpeg = 2
};

struct ClipboardEntry {
    std::uint64_t id = 0;
    ClipEntryKind kind = ClipEntryKind::Text;
    bool pinned = false;
    std::wstring previewText;
    std::vector<std::uint8_t> compressedJpeg; // Encoded via WIC inside strict 10 MB ring-buffer cap
    std::size_t memoryBytes = 0;
};

class ClipboardModule final : public IModule {
public:
    ~ClipboardModule() override;

    void EnsureInitialized(HWND mainHwnd) override;
    void OnCollapseRelease() override;
    void Render(const RenderContext& ctx) override;
    bool OnPointerDown(float xDip, float yDip) override;

    // Called ONLY on WM_CLIPBOARDUPDATE (AddClipboardFormatListener) — zero polling
    void OnClipboardUpdate();

    void SetSearchFilter(const std::wstring& filter) { searchFilter_ = filter; }
    void TogglePin(std::size_t index);
    void CopyEntryBackToClipboard(std::size_t index);

    std::size_t GetTotalRingBytes() const noexcept { return totalRingBytes_; }
    const std::deque<ClipboardEntry>& GetEntries() const noexcept { return entries_; }

private:
    void EnforceRingCap(std::size_t incomingBytes);
    bool CompressHBitmapToJpeg(HBITMAP hbm, std::vector<std::uint8_t>& outJpeg);

    HWND mainHwnd_ = nullptr;
    bool listenerRegistered_ = false;
    std::uint64_t nextId_ = 1;
    std::size_t totalRingBytes_ = 0;
    std::deque<ClipboardEntry> entries_;
    std::wstring searchFilter_;
    bool ignoreNextSelfUpdate_ = false;
};

} // namespace notch::modules
