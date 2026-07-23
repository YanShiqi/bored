#include "bored/protocol.h"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bored::protocol::MessageType;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void test_encodes_hello_fixture() {
    const auto payload = bored::protocol::encode_u32_payload(0x01020304);
    const auto packet = bored::protocol::encode_packet(MessageType::hello, 7, payload);
    const std::vector<std::uint8_t> expected = {
        0x42, 0x4F, 0x01, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04,
    };

    expect(packet == expected, "hello packet must match the v1 test vector");
}

void test_round_trips_hello_ack() {
    const auto payload = bored::protocol::encode_hello_ack_payload(0x01020304, 42);
    const auto encoded = bored::protocol::encode_packet(MessageType::hello_ack, 8, payload);
    std::string error;
    const auto decoded = bored::protocol::decode_packet(encoded, error);

    expect(decoded.has_value(), "hello ack should decode");
    expect(decoded->header.message_type == MessageType::hello_ack, "message type should round trip");
    expect(decoded->header.sequence == 8, "sequence should round trip");
    expect(decoded->payload == payload, "payload should round trip");
}

void test_round_trips_u64_ping_timestamp() {
    constexpr std::uint64_t timestamp = 1'234'567'890'123;
    const auto payload = bored::protocol::encode_u64_payload(timestamp);
    const auto decoded = bored::protocol::decode_u64_payload(payload);

    expect(decoded.has_value(), "eight-byte ping timestamp should decode");
    expect(*decoded == timestamp, "ping timestamp should preserve all bytes");
}

void test_rejects_invalid_packet_boundaries() {
    std::string error;
    const std::vector<std::uint8_t> truncated = {0x42, 0x4F, 0x01};
    expect(!bored::protocol::decode_packet(truncated, error).has_value(), "truncated header must be rejected");

    const std::vector<std::uint8_t> invalid_length = {
        0x42, 0x4F, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x01,
    };
    expect(!bored::protocol::decode_packet(invalid_length, error).has_value(),
           "payload length mismatch must be rejected");
}

struct TestCase {
    std::string_view name;
    std::function<void()> run;
};

} // namespace

int main() {
    const TestCase test_cases[] = {
        {"encodes hello fixture", test_encodes_hello_fixture},
        {"round trips hello ack", test_round_trips_hello_ack},
        {"round trips ping timestamp", test_round_trips_u64_ping_timestamp},
        {"rejects invalid packet boundaries", test_rejects_invalid_packet_boundaries},
    };

    for (const TestCase& test_case : test_cases) {
        try {
            test_case.run();
            std::cout << "PASS " << test_case.name << "\n";
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test_case.name << ": " << error.what() << "\n";
            return 1;
        }
    }

    return 0;
}
