# Repository Guidelines

## Project Structure & Module Organization

This repository contains separate client and server projects. `client/` is the Godot application; keep scenes in `client/scenes/`, GDScript in `client/scripts/`, and visual or audio files in `client/assets/`. `server/` contains the C++ authoritative game server and its tests. Put wire formats and compatibility fixtures in `shared/protocol/`, developer utilities in `tools/`, and architecture decisions or learning notes in `docs/`. Never edit Godot's generated `.godot/` directories or CMake build output.

## Build, Test, and Development Commands

- `godot --editor --path client`: open the client in the Godot editor.
- `godot --path client`: run the client locally.
- `godot --headless --editor --path client --quit`: verify that client resources load.
- `cmake -S server -B server/build`: configure the C++ server once sources exist.
- `cmake --build server/build`: compile the configured server.
- `ctest --test-dir server/build`: run registered C++ tests.

Use `godot4` instead of `godot` when required by the local installation.

## Coding Style & Naming Conventions

Files are UTF-8 with LF endings. Use tabs for GDScript and the formatter selected by the future C++ toolchain (prefer four spaces and `clang-format`). Name GDScript files `snake_case.gd`, C++ types `PascalCase`, functions and variables `snake_case`, and tests after behavior, such as `movement_prediction_test.cpp`. Keep rendering and input in the client; gameplay authority and validation belong to the server.

## Testing Guidelines

Add focused C++ tests under `server/tests/` and protocol fixtures under `shared/protocol/test_vectors/`. Every protocol change should include encode/decode coverage and a compatibility fixture. Before submitting, load the Godot project headlessly and run `ctest`. Networking changes should also record the simulated latency, jitter, and packet-loss settings used for manual verification.

## Commit & Pull Request Guidelines

History currently contains one concise subject, `Initial Godot project`; continue with short imperative messages such as `Add client movement visualization`. Keep commits scoped to one coherent change. Pull requests should summarize behavior, list verification commands, link relevant issues, and include screenshots or short captures for visible client changes. Call out protocol compatibility or migration impact explicitly.

## Agent-Specific Instructions

Keep `.godot/` caches and build output out of commits. Update `docs/project-plan.md` as milestones advance, and update `docs/networking-roadmap.md` when architecture boundaries or synchronization rules change.
