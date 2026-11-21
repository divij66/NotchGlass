#pragma once

#include <winsock2.h>
#include <windows.h>
#include <pdh.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace notch::core {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

struct UniqueHandle {
    HANDLE handle = nullptr;

    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE h) noexcept : handle(h) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    void reset(HANDLE h = nullptr) noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
        handle = h;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }
};

struct UniquePdhQuery {
    PDH_HQUERY query = nullptr;

    ~UniquePdhQuery() { reset(); }
    void reset(PDH_HQUERY q = nullptr) noexcept {
        if (query) {
            ::PdhCloseQuery(query);
        }
        query = q;
    }
};

struct UniqueSocket {
    SOCKET sock = INVALID_SOCKET;

    UniqueSocket() noexcept = default;
    explicit UniqueSocket(SOCKET s) noexcept : sock(s) {}
    ~UniqueSocket() { reset(); }

    UniqueSocket(const UniqueSocket&) = delete;
    UniqueSocket& operator=(const UniqueSocket&) = delete;

    UniqueSocket(UniqueSocket&& other) noexcept : sock(std::exchange(other.sock, INVALID_SOCKET)) {}
    UniqueSocket& operator=(UniqueSocket&& other) noexcept {
        if (this != &other) {
            reset();
            sock = std::exchange(other.sock, INVALID_SOCKET);
        }
        return *this;
    }

    void reset(SOCKET s = INVALID_SOCKET) noexcept {
        if (sock != INVALID_SOCKET) {
            ::closesocket(sock);
        }
        sock = s;
    }

    [[nodiscard]] bool valid() const noexcept { return sock != INVALID_SOCKET; }
};

template <typename GdiObj, BOOL(WINAPI* Deleter)(HGDIOBJ)>
struct UniqueGdiObject {
    GdiObj obj = nullptr;

    UniqueGdiObject() noexcept = default;
    explicit UniqueGdiObject(GdiObj o) noexcept : obj(o) {}
    ~UniqueGdiObject() { reset(); }

    UniqueGdiObject(const UniqueGdiObject&) = delete;
    UniqueGdiObject& operator=(const UniqueGdiObject&) = delete;

    UniqueGdiObject(UniqueGdiObject&& other) noexcept : obj(std::exchange(other.obj, nullptr)) {}
    UniqueGdiObject& operator=(UniqueGdiObject&& other) noexcept {
        if (this != &other) {
            reset();
            obj = std::exchange(other.obj, nullptr);
        }
        return *this;
    }

    void reset(GdiObj o = nullptr) noexcept {
        if (obj) {
            Deleter(obj);
        }
        obj = o;
    }
};

using UniqueHBitmap = UniqueGdiObject<HBITMAP, ::DeleteObject>;

// Fixed-capacity stack/arena allocator for zero heap fragmentation during render frames
template <std::size_t CapacityBytes>
class FrameArena {
public:
    void Reset() noexcept { offset_ = 0; }

    template <typename T>
    [[nodiscard]] std::span<T> AllocArray(std::size_t count) noexcept {
        static_assert(std::is_trivially_destructible_v<T>);
        const std::size_t align = alignof(T);
        const std::size_t alignedOffset = (offset_ + (align - 1)) & ~(align - 1);
        const std::size_t bytes = count * sizeof(T);
        if (alignedOffset + bytes > CapacityBytes) {
            return {};
        }
        T* ptr = reinterpret_cast<T*>(buffer_.data() + alignedOffset);
        offset_ = alignedOffset + bytes;
        return {ptr, count};
    }

    [[nodiscard]] std::size_t UsedBytes() const noexcept { return offset_; }

private:
    alignas(16) std::array<std::byte, CapacityBytes> buffer_{};
    std::size_t offset_ = 0;
};

} // namespace notch::core
