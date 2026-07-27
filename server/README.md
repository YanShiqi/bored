# C++ Game Server

The authoritative server provides a C++20 fixed-tick scheduling baseline, a UDP handshake, and phase 2 server-authoritative movement. It accepts input intent and broadcasts fixed-point world snapshots; interpolation and prediction begin in phase 3.

## Windows Development

From this directory, configure and build with the checked-in preset:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
.\build\msvc-debug\Debug\bored_server.exe --ticks 35
```

`--port <port>` selects the UDP port (default `39000`), `--tick-rate <hz>` changes the simulation rate (default `30`), `--snapshot-rate <hz>` changes world snapshot frequency (default `15`), and `--ticks <count>` ends a diagnostic run after a fixed number of ticks.
