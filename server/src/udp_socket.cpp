#include "bored/udp_socket.h"

#include "bored/protocol.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace bored {
namespace {

std::string socket_error() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

bool is_would_block_error() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

std::string endpoint_label(const sockaddr_storage& address) {
    if (address.ss_family != AF_INET) {
        return "unknown";
    }

    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
    char address_text[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &ipv4->sin_addr, address_text, sizeof(address_text)) == nullptr) {
        return "unknown";
    }

    return std::string{address_text} + ":" + std::to_string(ntohs(ipv4->sin_port));
}

} // namespace

UdpSocket::~UdpSocket() {
    close();
}

bool UdpSocket::open(std::uint16_t port, std::string& error) {
    close();

#ifdef _WIN32
    // Winsock 必须在创建 Windows socket 前初始化，且每次 open/close 成对管理。
    WSADATA winsock_data{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        error = socket_error();
        return false;
    }
    winsock_started_ = true;
#endif

    const auto native_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (native_socket == k_invalid_socket) {
        error = socket_error();
        close();
        return false;
    }

    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_address.sin_port = htons(port);
    if (bind(native_socket, reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
        error = socket_error();
#ifdef _WIN32
        closesocket(native_socket);
#else
        ::close(native_socket);
#endif
        close();
        return false;
    }

#ifdef _WIN32
    // 传输层绝不能等待网络数据，否则一个慢客户端会卡住整个模拟线程。
    u_long non_blocking = 1;
    if (ioctlsocket(native_socket, FIONBIO, &non_blocking) != 0) {
        error = socket_error();
        closesocket(native_socket);
        close();
        return false;
    }
#else
    const int flags = fcntl(native_socket, F_GETFL, 0);
    if (flags == -1 || fcntl(native_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        error = socket_error();
        ::close(native_socket);
        close();
        return false;
    }
#endif

    socket_ = native_socket;
    return true;
}

std::optional<UdpDatagram> UdpSocket::receive() {
    if (socket_ == k_invalid_socket) {
        return std::nullopt;
    }

    // 多留一个字节以识别超限数据报，避免 recvfrom 截断后把它误当作合法包。
    std::array<std::uint8_t, protocol::k_max_datagram_size + 1> buffer{};
    sockaddr_storage sender_address{};
#ifdef _WIN32
    int sender_length = sizeof(sender_address);
    const int received = recvfrom(
        socket_,
        reinterpret_cast<char*>(buffer.data()),
        static_cast<int>(buffer.size()),
        0,
        reinterpret_cast<sockaddr*>(&sender_address),
        &sender_length);
#else
    socklen_t sender_length = sizeof(sender_address);
    const ssize_t received = recvfrom(
        socket_,
        buffer.data(),
        buffer.size(),
        0,
        reinterpret_cast<sockaddr*>(&sender_address),
        &sender_length);
#endif
    if (received < 0) {
        if (!is_would_block_error()) {
            // 当前阶段把瞬时网络错误视为丢包；错误计数将在诊断阶段加入。
        }
        return std::nullopt;
    }

    UdpDatagram datagram;
    datagram.sender.address = sender_address;
    datagram.sender.address_length = sender_length;
    datagram.sender.label = endpoint_label(sender_address);
    datagram.bytes.assign(buffer.begin(), buffer.begin() + received);
    return datagram;
}

bool UdpSocket::send_to(
    const UdpEndpoint& endpoint,
    std::span<const std::uint8_t> bytes,
    std::string& error) const {
    if (socket_ == k_invalid_socket) {
        error = "UDP socket is not open";
        return false;
    }

#ifdef _WIN32
    const int sent = sendto(
        socket_,
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        0,
        reinterpret_cast<const sockaddr*>(&endpoint.address),
        endpoint.address_length);
#else
    const ssize_t sent = sendto(
        socket_,
        bytes.data(),
        bytes.size(),
        0,
        reinterpret_cast<const sockaddr*>(&endpoint.address),
        endpoint.address_length);
#endif
    // sendto 的返回值是有符号类型；先处理错误值，再与无符号的字节数比较。
    if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
        error = socket_error();
        return false;
    }

    return true;
}

void UdpSocket::close() {
    if (socket_ != k_invalid_socket) {
#ifdef _WIN32
        closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = k_invalid_socket;
    }

#ifdef _WIN32
    if (winsock_started_) {
        WSACleanup();
        winsock_started_ = false;
    }
#endif
}

} // namespace bored
