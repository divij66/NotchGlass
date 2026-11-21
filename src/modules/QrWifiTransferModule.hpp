#pragma once

#include "modules/IModule.hpp"
#include "core/raii.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace notch::modules {

// Pure C++20 QR Code generator (Byte mode, ECC Level L, Version 1..4) + ephemeral Winsock2 HTTP server
class QrWifiTransferModule final : public IModule {
public:
    ~QrWifiTransferModule() override;

    void EnsureInitialized(HWND mainHwnd) override;
    void OnCollapseRelease() override;
    void Render(const RenderContext& ctx) override;
    bool OnPointerDown(float xDip, float yDip) override;

    // Spin up single-threaded Winsock2 HTTP server ONLY when user clicks "Send to Phone"
    bool StartTransfer(const std::wstring& filePath);
    void StopTransfer();

    // Refresh cached local LAN IPv4 address (called on startup and WM_APP_NET_IF_CHANGED)
    void RefreshCachedLanIp();

    bool IsServerActive() const noexcept { return serverRunning_.load(); }
    std::string GetTransferUrl() const { return transferUrl_; }
    const std::vector<std::uint8_t>& GetQrMatrix() const noexcept { return qrModules_; }
    int GetQrSize() const noexcept { return qrSize_; }

private:
    void GenerateQrMatrix(const std::string& payload);
    void ServerThreadMain(std::wstring filePath);

    HWND mainHwnd_ = nullptr;
    bool initialized_ = false;
    bool wsaStarted_ = false;

    std::string cachedLanIp_ = "192.168.1.10";
    std::uint16_t port_ = 0;
    std::string sessionToken_;
    std::string transferUrl_;
    std::wstring activeFilePath_;

    std::vector<std::uint8_t> qrModules_;
    int qrSize_ = 0;

    std::atomic<bool> serverRunning_{false};
    core::UniqueSocket listenSocket_;
    std::thread serverThread_;
};

} // namespace notch::modules
