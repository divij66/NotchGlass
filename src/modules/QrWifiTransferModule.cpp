#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "modules/QrWifiTransferModule.hpp"
#include "core/config.hpp"
#include "graphics/LiquidGlassEngine.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace notch::modules {

namespace {

// Byte-Mode QR Encoder (ECC Level L, Mask 0, Versions 2..4 up to 78 ASCII bytes)
struct QrVersionSpec {
    int size;
    int dataCodewords;
    int eccCodewords;
    int alignCenter;
};

constexpr QrVersionSpec kQrSpecs[3] = {
    {25, 34, 10, 18}, // Version 2-L (max 32 bytes)
    {29, 55, 15, 22}, // Version 3-L (max 53 bytes)
    {33, 80, 20, 26}, // Version 4-L (max 78 bytes)
};

std::uint8_t GfMul(std::uint8_t a, std::uint8_t b) {
    std::uint8_t r = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) r ^= a;
        const bool hi = (a & 0x80) != 0;
        a <<= 1;
        if (hi) a ^= 0x1D;
        b >>= 1;
    }
    return r;
}

std::vector<std::uint8_t> ComputeReedSolomon(const std::vector<std::uint8_t>& data, int eccCount) {
    std::vector<std::uint8_t> gen{1};
    std::uint8_t root = 1;
    for (int i = 0; i < eccCount; ++i) {
        std::vector<std::uint8_t> next(gen.size() + 1, 0);
        for (std::size_t j = 0; j < gen.size(); ++j) {
            next[j] ^= gen[j];
            next[j + 1] ^= GfMul(gen[j], root);
        }
        gen = std::move(next);
        root = GfMul(root, 2);
    }

    std::vector<std::uint8_t> ecc(eccCount, 0);
    for (std::uint8_t b : data) {
        const std::uint8_t factor = b ^ ecc[0];
        std::rotate(ecc.begin(), ecc.begin() + 1, ecc.end());
        ecc.back() = 0;
        for (int j = 0; j < eccCount; ++j) {
            ecc[j] ^= GfMul(gen[j + 1], factor);
        }
    }
    return ecc;
}

} // namespace

QrWifiTransferModule::~QrWifiTransferModule() {
    StopTransfer();
    if (wsaStarted_) {
        ::WSACleanup();
    }
}

void QrWifiTransferModule::EnsureInitialized(HWND mainHwnd) {
    if (initialized_) return;
    initialized_ = true;
    mainHwnd_ = mainHwnd;

    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        wsaStarted_ = true;
    }
    RefreshCachedLanIp();
}

void QrWifiTransferModule::OnCollapseRelease() {
    // Keep server alive only while waiting for phone transfer; auto-closes via TIMER_WIFI_TIMEOUT or completion
}

void QrWifiTransferModule::RefreshCachedLanIp() {
    ULONG bufLen = 32768;
    std::vector<std::uint8_t> buf(bufLen);
    auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_INCLUDE_GATEWAYS;
    if (::GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &bufLen) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(bufLen);
        addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    }
    if (::GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &bufLen) != NO_ERROR) {
        return;
    }

    auto isVirtualAdapter = [](const IP_ADAPTER_ADDRESSES* a) -> bool {
        if (!a->Description) return false;
        std::wstring desc(a->Description);
        for (auto& ch : desc) ch = static_cast<wchar_t>(::towlower(ch));
        return desc.find(L"virtual") != std::wstring::npos ||
               desc.find(L"hyper-v") != std::wstring::npos ||
               desc.find(L"wsl") != std::wstring::npos ||
               desc.find(L"vmware") != std::wstring::npos ||
               desc.find(L"vbox") != std::wstring::npos ||
               desc.find(L"loopback") != std::wstring::npos;
    };

    std::string bestGatewayWifiIp;
    std::string bestGatewayLanIp;
    std::string fallbackIp;

    for (auto* a = addrs; a != nullptr; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        const bool isVirt = isVirtualAdapter(a);
        const bool hasGw = (a->FirstGatewayAddress != nullptr);

        for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next) {
            if (u->Address.lpSockaddr && u->Address.lpSockaddr->sa_family == AF_INET) {
                char ipStr[INET_ADDRSTRLEN]{};
                auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
                ::inet_ntop(AF_INET, &sin->sin_addr, ipStr, sizeof(ipStr));
                std::string candidate(ipStr);
                if (candidate.rfind("169.254.", 0) == 0 || candidate == "127.0.0.1") continue;

                const bool isPrivate = (candidate.rfind("192.168.", 0) == 0 ||
                                        candidate.rfind("10.", 0) == 0 ||
                                        candidate.rfind("172.", 0) == 0);
                if (!isVirt && hasGw && isPrivate) {
                    if (a->IfType == IF_TYPE_IEEE80211 && bestGatewayWifiIp.empty()) {
                        bestGatewayWifiIp = candidate;
                    } else if (bestGatewayLanIp.empty()) {
                        bestGatewayLanIp = candidate;
                    }
                } else if (!isVirt && fallbackIp.empty()) {
                    fallbackIp = candidate;
                }
            }
        }
    }

    if (!bestGatewayWifiIp.empty()) cachedLanIp_ = bestGatewayWifiIp;
    else if (!bestGatewayLanIp.empty()) cachedLanIp_ = bestGatewayLanIp;
    else if (!fallbackIp.empty()) cachedLanIp_ = fallbackIp;
}

void QrWifiTransferModule::GenerateQrMatrix(const std::string& payload) {
    QrVersionSpec spec = kQrSpecs[2];
    for (const auto& s : kQrSpecs) {
        if (payload.size() <= static_cast<std::size_t>(s.dataCodewords - 2)) {
            spec = s;
            break;
        }
    }

    const int n = spec.size;
    qrSize_ = n;
    qrModules_.assign(static_cast<std::size_t>(n * n), 0);
    std::vector<std::uint8_t> reserved(static_cast<std::size_t>(n * n), 0);

    auto setMod = [&](int x, int y, bool dark, bool isFunc) {
        if (x < 0 || y < 0 || x >= n || y >= n) return;
        qrModules_[static_cast<std::size_t>(y * n + x)] = dark ? 1 : 0;
        if (isFunc) reserved[static_cast<std::size_t>(y * n + x)] = 1;
    };

    auto drawFinder = [&](int ox, int oy) {
        for (int dy = -1; dy <= 7; ++dy) {
            for (int dx = -1; dx <= 7; ++dx) {
                const int x = ox + dx;
                const int y = oy + dy;
                if (x < 0 || y < 0 || x >= n || y >= n) continue;
                const bool inBorder = (dx == -1 || dx == 7 || dy == -1 || dy == 7);
                const bool dark = !inBorder && (dx == 0 || dx == 6 || dy == 0 || dy == 6 || (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
                setMod(x, y, dark, true);
            }
        }
    };

    drawFinder(0, 0);
    drawFinder(n - 7, 0);
    drawFinder(0, n - 7);

    // Alignment pattern
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const bool dark = (std::max(std::abs(dx), std::abs(dy)) != 1);
            setMod(spec.alignCenter + dx, spec.alignCenter + dy, dark, true);
        }
    }

    // Timing patterns + dark module
    for (int i = 8; i < n - 8; ++i) {
        setMod(i, 6, (i % 2) == 0, true);
        setMod(6, i, (i % 2) == 0, true);
    }
    setMod(8, n - 8, true, true);

    // Reserve format info strips
    for (int i = 0; i < 9; ++i) {
        if (i != 6) {
            reserved[static_cast<std::size_t>(8 * n + i)] = 1;
            reserved[static_cast<std::size_t>(i * n + 8)] = 1;
        }
    }
    for (int i = 0; i < 8; ++i) {
        reserved[static_cast<std::size_t>(8 * n + (n - 1 - i))] = 1;
        reserved[static_cast<std::size_t>((n - 1 - i) * n + 8)] = 1;
    }

    // Encode Byte-Mode bit stream
    std::vector<std::uint8_t> bits;
    auto pushBits = [&](std::uint32_t val, int count) {
        for (int i = count - 1; i >= 0; --i) bits.push_back(static_cast<std::uint8_t>((val >> i) & 1u));
    };

    const std::size_t maxLen = static_cast<std::size_t>(spec.dataCodewords - 2);
    const std::size_t len = std::min<std::size_t>(payload.size(), maxLen);
    pushBits(0b0100, 4); // Byte mode indicator
    pushBits(static_cast<std::uint32_t>(len), 8);
    for (std::size_t i = 0; i < len; ++i) {
        pushBits(static_cast<std::uint8_t>(payload[i]), 8);
    }
    pushBits(0, 4); // Terminator
    while (bits.size() % 8 != 0) bits.push_back(0);

    std::vector<std::uint8_t> codewords;
    for (std::size_t i = 0; i < bits.size(); i += 8) {
        std::uint8_t byte = 0;
        for (int b = 0; b < 8; ++b) byte = static_cast<std::uint8_t>((byte << 1) | bits[i + b]);
        codewords.push_back(byte);
    }
    bool alt = false;
    while (codewords.size() < static_cast<std::size_t>(spec.dataCodewords)) {
        codewords.push_back(alt ? 0x11 : 0xEC);
        alt = !alt;
    }

    auto ecc = ComputeReedSolomon(codewords, spec.eccCodewords);
    codewords.insert(codewords.end(), ecc.begin(), ecc.end());

    // Place codewords in zig-zag columns + Mask Pattern 0 ((x + y) % 2 == 0)
    std::size_t bitIdx = 0;
    const std::size_t totalBits = codewords.size() * 8;
    int dirUp = -1;
    for (int right = n - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int step = 0; step < n; ++step) {
            const int y = (dirUp == -1) ? (n - 1 - step) : step;
            for (int col = 0; col < 2; ++col) {
                const int x = right - col;
                if (reserved[static_cast<std::size_t>(y * n + x)]) continue;
                bool bit = false;
                if (bitIdx < totalBits) {
                    bit = ((codewords[bitIdx / 8] >> (7 - (bitIdx % 8))) & 1) != 0;
                    ++bitIdx;
                }
                if (((x + y) % 2) == 0) bit = !bit;
                qrModules_[static_cast<std::size_t>(y * n + x)] = bit ? 1 : 0;
            }
        }
        dirUp = -dirUp;
    }

    // Format string for ECC Level L (01) + Mask 0 (000) -> 0x77C4 (15 bits MSB=14 .. LSB=0)
    constexpr std::uint16_t kFormatBits = 0x77C4;
    for (int i = 0; i < 8; ++i) {
        const auto b = static_cast<std::uint8_t>((kFormatBits >> i) & 1u);
        const int rowTop = (i < 6) ? i : (i + 1); // 0..5, 7, 8
        qrModules_[static_cast<std::size_t>(rowTop * n + 8)] = b;
        qrModules_[static_cast<std::size_t>(8 * n + (n - 1 - i))] = b;
    }
    for (int i = 0; i < 7; ++i) {
        const auto b = static_cast<std::uint8_t>((kFormatBits >> (14 - i)) & 1u);
        const int colLeft = (i < 6) ? i : 7; // 0..5, 7
        qrModules_[static_cast<std::size_t>(8 * n + colLeft)] = b;
        qrModules_[static_cast<std::size_t>((n - 1 - i) * n + 8)] = b;
    }
}

bool QrWifiTransferModule::StartTransfer(const std::wstring& filePath) {
    StopTransfer();
    if (!wsaStarted_) return false;

    RefreshCachedLanIp();
    activeFilePath_ = filePath;

    core::UniqueSocket sock(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!sock.valid()) return false;

    BOOL reuse = TRUE;
    ::setsockopt(sock.sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; // Ephemeral OS-assigned port

    if (::bind(sock.sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) return false;
    if (::listen(sock.sock, 4) == SOCKET_ERROR) return false;

    int addrLen = sizeof(addr);
    ::getsockname(sock.sock, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    port_ = ntohs(addr.sin_port);

    char urlBuf[128];
    std::snprintf(urlBuf, sizeof(urlBuf), "http://%s:%u/", cachedLanIp_.c_str(), port_);
    transferUrl_ = urlBuf;
    GenerateQrMatrix(transferUrl_);

    listenSocket_ = std::move(sock);
    serverRunning_.store(true);
    if (mainHwnd_) {
        ::SetCoalescableTimer(mainHwnd_, core::TIMER_WIFI_TIMEOUT, core::GeometryConfig::kWifiIdleTimeoutMs, nullptr, TIMERV_DEFAULT_COALESCING);
    }

    serverThread_ = std::thread(&QrWifiTransferModule::ServerThreadMain, this, filePath);
    return true;
}

void QrWifiTransferModule::StopTransfer() {
    serverRunning_.store(false);
    listenSocket_.reset();
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void QrWifiTransferModule::ServerThreadMain(std::wstring filePath) {
    const std::filesystem::path fsPath(filePath);
    std::string downloadName = fsPath.filename().string();
    if (downloadName.empty()) downloadName = "notchglass_file.bin";
    for (char& ch : downloadName) {
        if (ch == '"' || ch == '\\' || ch == '\r' || ch == '\n') ch = '_';
    }

    while (serverRunning_.load() && listenSocket_.valid()) {
        core::UniqueSocket client(::accept(listenSocket_.sock, nullptr, nullptr));
        if (!client.valid()) break;

        char reqBuf[2048]{};
        const int n = ::recv(client.sock, reqBuf, sizeof(reqBuf) - 1, 0);
        if (n <= 0) continue;

        std::string req(reqBuf, static_cast<std::size_t>(n));
        if (req.rfind("GET /download", 0) == 0) {
            std::ifstream in(fsPath, std::ios::binary | std::ios::ate);
            if (in.is_open()) {
                const auto fileSize = static_cast<std::size_t>(in.tellg());
                in.seekg(0);

                std::ostringstream hdr;
                hdr << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: application/octet-stream\r\n"
                    << "Content-Disposition: attachment; filename=\"" << downloadName << "\"\r\n"
                    << "Content-Length: " << fileSize << "\r\nConnection: close\r\n\r\n";
                const std::string hStr = hdr.str();
                ::send(client.sock, hStr.data(), static_cast<int>(hStr.size()), 0);

                std::array<char, 16384> chunk{};
                while (in.read(chunk.data(), chunk.size()) || in.gcount() > 0) {
                    ::send(client.sock, chunk.data(), static_cast<int>(in.gcount()), 0);
                }
                if (mainHwnd_) ::PostMessageW(mainHwnd_, core::WM_APP_WIFI_TRANSFER_DONE, 0, 0);
                break;
            }
        } else {
            const std::string html =
                "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
                "<!doctype html><html><meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<body style='font-family:system-ui;background:#0b101b;color:#f1f5f9;padding:32px;text-align:center'>"
                "<h2>NotchGlass Wi-Fi Transfer</h2>"
                "<p style='color:#94a3b8;font-size:14px'>" + downloadName + "</p>"
                "<p><a href='/download' style='display:inline-block;padding:14px 24px;background:#38bdf8;color:#082f49;border-radius:10px;text-decoration:none;font-weight:600'>Download to Phone</a></p>"
                "</body></html>";
            ::send(client.sock, html.data(), static_cast<int>(html.size()), 0);
        }
    }
    serverRunning_.store(false);
}

void QrWifiTransferModule::Render(const RenderContext& ctx) {
    auto* dc = ctx.dc;
    auto* dw = ctx.dwrite;

    D2D1_RECT_F qrRect = D2D1::RectF(ctx.boundsDip.left + 4.0f, ctx.boundsDip.top + 8.0f,
                                     ctx.boundsDip.left + 164.0f, ctx.boundsDip.top + 168.0f);
    graphics::LiquidGlassEngine::DrawQrGeometry(
        dc, qrModules_, qrSize_, qrRect,
        D2D1::ColorF(0.05f, 0.07f, 0.12f, 1.0f),
        D2D1::ColorF(0.99f, 0.99f, 1.00f, 1.0f)
    );

    core::ComPtr<ID2D1SolidColorBrush> textBrush;
    core::ComPtr<ID2D1SolidColorBrush> mutedBrush;
    core::ComPtr<ID2D1SolidColorBrush> btnBg;
    core::ComPtr<ID2D1SolidColorBrush> accentBg;
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &textBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.80f, 0.88f, 0.98f, 0.82f), &mutedBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.16f), &btnBg);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.65f, 0.98f, 0.38f), &accentBg);

    core::ComPtr<IDWriteTextFormat> titleFmt;
    core::ComPtr<IDWriteTextFormat> smallFmt;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-us", &titleFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &smallFmt);

    const float infoL = qrRect.right + 14.0f;
    const auto slashPos = activeFilePath_.find_last_of(L"\\/");
    const std::wstring fileName = (slashPos == std::wstring::npos) ? activeFilePath_ : activeFilePath_.substr(slashPos + 1);

    dc->DrawTextW(fileName.c_str(), static_cast<UINT32>(fileName.size()), titleFmt.Get(),
        D2D1::RectF(infoL, qrRect.top + 4.0f, ctx.boundsDip.right, qrRect.top + 26.0f), textBrush.Get());

    std::wstring wurl(transferUrl_.begin(), transferUrl_.end());
    dc->DrawTextW(wurl.c_str(), static_cast<UINT32>(wurl.size()), smallFmt.Get(),
        D2D1::RectF(infoL, qrRect.top + 30.0f, ctx.boundsDip.right, qrRect.top + 52.0f), mutedBrush.Get());

    const wchar_t* desc = L"Scan with phone camera on same Wi-Fi.\nWinsock2 closes automatically after download.";
    dc->DrawTextW(desc, static_cast<UINT32>(wcslen(desc)), smallFmt.Get(),
        D2D1::RectF(infoL, qrRect.top + 58.0f, ctx.boundsDip.right, qrRect.top + 104.0f), mutedBrush.Get());

    D2D1_RECT_F copyBtn = D2D1::RectF(infoL, qrRect.bottom - 34.0f, infoL + 104.0f, qrRect.bottom - 2.0f);
    D2D1_RECT_F stopBtn = D2D1::RectF(infoL + 112.0f, qrRect.bottom - 34.0f, ctx.boundsDip.right, qrRect.bottom - 2.0f);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, copyBtn, 10.0f, true, true);
    graphics::LiquidGlassEngine::DrawGlassCapsule(dc, stopBtn, 10.0f, false, true);

    smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    smallFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextW(L"Copy URL", 8, smallFmt.Get(), copyBtn, textBrush.Get());
    dc->DrawTextW(L"Close Socket", 12, smallFmt.Get(), stopBtn, textBrush.Get());
}

bool QrWifiTransferModule::OnPointerDown(float xDip, float yDip) {
    if (!serverRunning_.load()) return false;
    if (yDip >= 165.0f && yDip <= 220.0f) {
        if (xDip >= 186.0f && xDip <= 295.0f) {
            if (::OpenClipboard(mainHwnd_)) {
                ::EmptyClipboard();
                std::wstring wurl(transferUrl_.begin(), transferUrl_.end());
                HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, (wurl.size() + 1) * sizeof(wchar_t));
                if (hMem) {
                    wchar_t* dst = static_cast<wchar_t*>(::GlobalLock(hMem));
                    std::copy(wurl.begin(), wurl.end(), dst);
                    dst[wurl.size()] = L'\0';
                    ::GlobalUnlock(hMem);
                    ::SetClipboardData(CF_UNICODETEXT, hMem);
                }
                ::CloseClipboard();
            }
            return true;
        }
        if (xDip > 295.0f) {
            StopTransfer();
            return true;
        }
    }
    return false;
}

} // namespace notch::modules
