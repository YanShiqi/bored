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
    [[nodiscard]] std::optional<UdpDatagram> receive();
    [[nodiscard]] bool send_to(
        const UdpEndpoint& endpoint,
        std::span<const std::uint8_t> bytes,
        std::string& error) const;

    void close();

private:
    std::uintptr_t socket_ = 0;
    bool winsock_started_ = false;
};

} // namespace bored
