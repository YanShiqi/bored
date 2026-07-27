# Protocol v2

## Scope

This document defines the UDP wire format shared by the Godot client and C++ server. Version 2 adds server-authoritative movement to the v1 handshake: `InputCommand` carries client intent, and `WorldSnapshot` carries server-owned positions. It does not provide reliability, encryption, authentication, or fragmentation.

## Datagram Header

Every datagram is at most 1200 bytes and begins with this 10-byte header. All multi-byte integers use unsigned big-endian (network) byte order.

| Offset | Size | Field | Rule |
| --- | --- | --- | --- |
| 0 | 2 | `magic` | Always `0x424F` (`BO`) |
| 2 | 1 | `version` | Always `2` |
| 3 | 1 | `message_type` | See the table below |
| 4 | 4 | `sequence` | Sender-local, starts at 1 and increases per packet |
| 8 | 2 | `payload_length` | Exact number of bytes following the header |

Receivers must discard datagrams with an invalid magic, unsupported version, unknown message type, size above 1200 bytes, or a payload length that does not exactly match the datagram length. Invalid UDP data must never terminate the server.

## Messages

| Type | Value | Payload | Direction | Purpose |
| --- | ---: | --- | --- | --- |
| `Hello` | 1 | `client_nonce: u32` | client -> server | Request a temporary local-session ID. |
| `HelloAck` | 2 | `client_nonce: u32`, `client_id: u32` | server -> client | Confirm the matching handshake and assigned ID. |
| `Ping` | 3 | `sent_at_usec: u64` | client -> server | Measure application-level RTT. |
| `Pong` | 4 | `sent_at_usec: u64` | server -> client | Echo the `Ping` payload without modification. |
| `InputCommand` | 5 | See below | client -> server | Send movement intent for one client input Tick. |
| `WorldSnapshot` | 6 | See below | server -> client | Broadcast server-authoritative entity positions. |

## InputCommand Payload

The payload is exactly 10 bytes. The endpoint authenticated by `Hello` identifies the player; the client must not send a player ID.

| Offset | Size | Field | Rule |
| --- | --- | --- | --- |
| 0 | 4 | `client_tick` | Client-local input Tick, reserved for later prediction and timing diagnostics. |
| 4 | 4 | `input_sequence` | Starts at 1 and increases for each input command. |
| 8 | 1 | `move_x` | Signed byte; only `-1`, `0`, `1` are valid. |
| 9 | 1 | `move_y` | Signed byte; only `-1`, `0`, `1` are valid. |

The server discards inputs from unknown endpoints, invalid axes, and input sequences not newer than the last accepted sequence for that player.

## WorldSnapshot Payload

The payload starts with a 10-byte prefix followed by zero or more 12-byte entities. A snapshot is individually encoded for each recipient because `acknowledged_input_sequence` belongs to that recipient.

| Offset | Size | Field | Rule |
| --- | --- | --- | --- |
| 0 | 4 | `server_tick` | Tick number after the server applies simulation. |
| 4 | 4 | `acknowledged_input_sequence` | Latest input accepted from the receiving client. |
| 8 | 2 | `entity_count` | Number of following entities; maximum 98. |
| 10 | 4 | `client_id` | Repeated entity ID. |
| 14 | 4 | `position_x_mm` | Repeated signed i32, millimetres. |
| 18 | 4 | `position_y_mm` | Repeated signed i32, millimetres. |

The repeated entity layout begins at offset 10 and is 12 bytes wide. Positions use signed millimetres to avoid cross-language floating-point representation differences. A receiver must reject a snapshot unless its length is exactly `10 + entity_count * 12` bytes.

## Compatibility

Version 1 endpoints are not compatible with v2 endpoints. Changing existing field layouts, byte order, or type values requires a new protocol version. A future sub-Tick input timestamp must be added by an explicit versioned message layout, not unused padding.

## Test Vectors

[`test_vectors/v2_packets.txt`](test_vectors/v2_packets.txt) is normative for v2 byte order. C++ tests mirror the vectors, and the Godot encoder/decoder must produce and accept the same bytes. The retained v1 vector is historical only.
