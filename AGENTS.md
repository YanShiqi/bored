# Repository Guidelines

## Project Structure & Module Organization

This is a compact Godot project. The root contains `project.godot`, the main engine configuration file, and `node_2d.tscn`, the current root scene. Engine-generated state lives in `.godot/` and should not be edited by hand. Shared visual assets currently live at the root (`icon.svg` and `icon.svg.import`); as the project grows, place scenes in `scenes/`, scripts in `scripts/`, art/audio in `assets/`, and tests in `tests/`.

## Build, Test, and Development Commands

- `godot --editor project.godot`: open the project in the Godot editor.
- `godot --path .`: run the project from the repository root.
- `godot --headless --path . --quit`: validate that the project loads in a non-interactive environment.
- `godot --headless --path . --export-release <preset> <output>`: export a release build after export presets have been configured in the editor.

Use `godot4` instead of `godot` if that is how Godot is installed locally.

## Coding Style & Naming Conventions

Keep files UTF-8 encoded, as defined in `.editorconfig`. Prefer Godot 4.x conventions: scene files use PascalCase or descriptive snake_case names, GDScript files use `snake_case.gd`, class names use `PascalCase`, and node names should describe their role (`Player`, `MainCamera`, `LevelRoot`). Use tabs for GDScript indentation, matching the Godot editor default. Keep scene ownership simple: one primary script per scene unless a node has a clear independent responsibility.

## Testing Guidelines

No test framework is configured yet. When adding gameplay or reusable logic, add focused tests under `tests/` and document the runner here. For Godot projects, GUT is a common choice; name tests after the behavior under test, for example `tests/test_player_movement.gd`. At minimum, run `godot --headless --path . --quit` before submitting changes to catch load errors.

## Commit & Pull Request Guidelines

This workspace does not currently include Git history, so use concise imperative commit messages such as `Add player movement scene` or `Fix viewport stretch settings`. Pull requests should include a short summary, testing performed, linked issues if applicable, and screenshots or clips for visible scene, UI, or rendering changes.

## Agent-Specific Instructions

Do not edit `.godot/` cache files manually. Prefer changes through Godot scene/resource files and keep generated `.import` files paired with their source assets when assets change.
