#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

namespace bored {

struct UdpEndpoint {
    // sockaddr 只保留在传输适配层，业务层通过 label 识别临时会话。
    sockaddr_storage address{};
#ifdef _WIN32
    int address_length = 0;
#else
    socklen_t address_length = 0;
#endif
    std::string label;
};

struct UdpDatagram {
    UdpEndpoint sender;
    std::vector<std::uint8_t> bytes;
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    [[nodiscard]] bool open(std::uint16_t port, std::string& error);
    // 非阻塞收包；当前没有数据与底层读取失败都会返回空值，调用方不能因此阻塞 Tick。
    [[nodiscard]] std::optional<UdpDatagram> receive();
    [[nodiscard]] bool send_to(
        const UdpEndpoint& endpoint,
        std::span<const std::uint8_t> bytes,
        std::string& error) const;

    void close();

private:
    std::uintptr_t socket_ = 0;
    // Windows 的 socket 生命周期还需要配对 WSAStartup/WSACleanup，Linux 不需要这一步。
    bool winsock_started_ = false;
};

} // namespace bored
