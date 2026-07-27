# Networking Architecture and Learning Roadmap

For the current implementation order and acceptance criteria, see
[`project-plan.md`](project-plan.md).
Cross-client/server behavioral contracts live in [`design/`](design/README.md); byte-level packet rules remain in `shared/protocol/`.

## Purpose

This repository is both a playable prototype and a laboratory for high-frequency multiplayer synchronization. The project should make latency, jitter, packet loss, prediction errors, and server corrections visible instead of hiding them behind polished presentation.

## Architecture

The Godot client handles input, rendering, interpolation, prediction, and debugging overlays. The C++ server owns player state, movement validation, simulation ticks, combat results, and snapshot generation. Clients send intentions or input commands; they never authoritatively set gameplay state.

```text
Godot clients <---- UDP packets ----> C++ authoritative server
      |                                      |
visual diagnostics                    fixed-tick simulation
                                             |
                                      C++ load-test bots
```

The client and server are separate programs but remain in one repository so protocol and behavior changes can be committed atomically. Shared files contain protocol definitions and known byte-level test vectors, not client rendering code or server infrastructure.

## Frequency Strategy

Simulation rate, client input rate, and snapshot rate are independent controls. The learning baseline is `30 Hz` simulation, `30 Hz` input, and `15 Hz` snapshots. After correctness is established, the server must be exercised at `64 Hz` and then `128 Hz`; these are performance profiles, not unconditional defaults.

A `64 Hz` Tick has a `15.625 ms` budget and a `128 Hz` Tick has a `7.8125 ms` budget. The P99 simulation duration must retain meaningful headroom below the budget. A higher simulation rate does not imply sending complete snapshots at the same rate: snapshot frequency and payload size must remain separately measurable and configurable.

## Initial Protocol

The implemented v2 protocol is documented in [`../shared/protocol/protocol.md`](../shared/protocol/protocol.md). It uses a 10-byte fixed header with magic, version, message type, sender-local sequence, and payload length; all multi-byte fields are big-endian. `Hello`, `HelloAck`, `Ping`, `Pong`, `InputCommand`, and `WorldSnapshot` provide the current authoritative movement baseline.

- `InputCommand`: player ID, input sequence, client tick, movement axes, buttons. A future sub-Tick input time must be introduced through an explicit protocol version change, not implicit padding.
- `WorldSnapshot`: server tick, acknowledged input sequence, and entity states.
- `Ping`/`Pong`: timestamps used to estimate round-trip time. Clock offset estimation is intentionally deferred.

Business-style messages may later use Protocol Buffers. High-frequency snapshots should remain measurable and compact before adopting a more elaborate serialization layer.

## Synchronization Milestones

1. Connect two Godot clients to one local C++ server and move server-authoritative entities.
2. Run the server at a fixed tick rate and send snapshots at a separately configurable rate.
3. Add a snapshot buffer and interpolation for remote entities.
4. Add local prediction, input acknowledgements, correction, and input replay.
5. Add configurable latency, jitter, loss, duplication, and reordering.
6. Add skills, hit validation, lag compensation, and server rewind experiments.
7. Add AOI filtering, C++ bots, bandwidth metrics, and repeatable load tests.

## Observable Metrics

The client overlay should expose RTT, estimated jitter, packet loss, server tick, snapshot age, interpolation delay, prediction error, and correction count. The server should report connected peers, Tick duration P50/P95/P99, budget overruns, input backlog, packets and bytes per second, snapshot sizes, AOI entity counts, and dropped or late inputs.

## Technical Boundaries

Avoid coupling the C++ server to Godot's high-level RPC protocol. Begin with Godot's low-level packet APIs and a protocol owned by this repository. Introduce C++ GDExtension code only when sharing deterministic simulation code or profiling demonstrates a real client-side performance need.
