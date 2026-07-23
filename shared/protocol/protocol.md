# Protocol v1

## Scope

This document defines the first UDP wire format shared by the Godot client and C++ server. It covers local `Hello`, `HelloAck`, `Ping`, and `Pong` traffic only; it does not provide reliability, encryption, authentication, or gameplay state.

## Datagram Header

Every datagram is at most 1200 bytes and begins with this 10-byte header. All multi-byte integers use unsigned big-endian (network) byte order.

| Offset | Size | Field | Rule |
| --- | --- | --- | --- |
| 0 | 2 | `magic` | Always `0x424F` (`BO`) |
| 2 | 1 | `version` | Always `1` |
| 3 | 1 | `message_type` | See the table below |
| 4 | 4 | `sequence` | Sender-local, starts at 1 and increases per packet |
| 8 | 2 | `payload_length` | Exact number of bytes following the header |

Receivers must discard datagrams with an invalid magic, unsupported version, unknown message type, size above 1200 bytes, or a payload length that does not exactly match the datagram length. Invalid UDP data must never terminate the server.

## Messages

| Type | Value | Payload | Direction | Purpose |
| --- | ---: | --- | --- | --- |
| `Hello` | 1 | `client_nonce: u32` | client -> server | Request a temporary local-session ID. |
| `HelloAck` | 2 | `client_nonce: u32`, `client_id: u32` | server -> client | Confirm the matching handshake and assigned ID. |
| `Ping` | 3 | `sent_at_usec: u64` | client -> server | Measure application-level round-trip time. |
| `Pong` | 4 | `sent_at_usec: u64` | server -> client | Echo the `Ping` payload without modification. |

The client only treats `HelloAck` as valid when its nonce equals the nonce from its current `Hello`. RTT is `local_receive_time_usec - sent_at_usec`; this is a diagnostic estimate, not clock synchronization.

## Compatibility

Changing the header layout, field byte order, existing type values, or payload layout requires a new protocol version. New message types can be introduced under a new version until an explicit forward-compatibility rule exists.

## Test Vectors

The fixture in [`test_vectors/v1_packets.txt`](test_vectors/v1_packets.txt) is normative for v1 byte order. C++ tests mirror it, and the Godot encoder must produce the same bytes.
