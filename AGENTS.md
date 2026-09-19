## Project Overview

Gel is a cross-platform graphical git repository browser (GUI wrapper for `git log`). It visualizes commit history/diffs and supports cherry-pick, revert, rebase, and merge operations. Built with ImGui, Vulkan/OpenGL backends, and GLFW for windowing. Targets Linux, Windows, and macOS.

## Build System

Uses GN + Ninja.

```sh
# First-time setup: fetch submodule dependencies
git submodule update --init --recursive

# Generate build files
gn gen out/release
gn gen --args='is_debug=true' out/debug

# Build
ninja -C out/release
ninja -C out/debug

# Run
./out/release/gel
```

Build arguments are declared in `src/third_party/kaliber/build/BUILDCONFIG.gn`. Key args: `is_debug` (default false), `cc`, `cxx`, `compile_preprocess` (auto-detects ccache).

There is no test suite. Verify changes by building and running the application.

## Linting

```sh
# clang-format (Chromium style, C++20) — always run before committing
clang-format -i <files>

# clang-tidy (requires compile_commands.json in project root)
# Generate compile_commands.json:
ninja -C out/release -t compdb > compile_commands.json
# Run on specific files:
clang-tidy -p compile_commands.json <files>
```

## Code Style

- `.clang-format`: `BasedOnStyle: Chromium`, `Standard: c++20`
- C++20 standard throughout
- Warnings treated as errors (`-Werror` / `/WX`)
- Include paths are relative to `src/` (configured via `//build:default`)

## Architecture

### Source Layout

- `src/gel/` — Application code (the `gel` executable target)
- `src/base/` — Small platform-abstraction library (process execution, pipes, command-line parsing, DoubleBuffer for thread-safe worker→main data exchange)
- `src/third_party/` — Vendored dependencies (ImGui, GLFW, FreeType, Vulkan SDK, glslang, etc.)
- `src/third_party/kaliber/build/` — GN build configs (`BUILDCONFIG.gn`), toolchains, compiler/linker flag configs
- `build/` — Code-generation scripts (`gen_*.py`) for embedding assets and version info at build time

### Key Layers

**`Gel`** (`gel.h/cc`) — Application lifecycle. Creates the platform window, renderer (Vulkan or OpenGL), and ImGui backend. Runs the main loop. Delegates all UI and git logic to `MainWindow`.

**`MainWindow`** (`ui/main_window.h/cc`) — Central coordinator. Owns all UI panels and git workers. Implements delegate interfaces for `CommitHistory`, `CommitDiff`, `Toolbar`, and `DirectoryBrowser`. Merges worker data each frame and dispatches user actions.

**Git command workers** (`commands/`) — Each worker (GitLog, GitDiff, GitLocalStatus, GitBlame, GitCatFile, GitDiffTree) inherits from `Git` base class which runs git as a subprocess in a background thread. Workers parse output incrementally and expose results via thread-safe double-buffers or locked fields. `GitCmdRunner` (`ui/git_cmd_runner.h`) handles interactive git operations (cherry-pick, revert, rebase, merge).

**UI panels** (`ui/`) — Separate classes for each UI region:
- `upper_panel/` — CommitHistory (table + graph), CommitContextMenu, CommitGraph
- `lower_panel/` — CommitDiff (file list + diff viewer), CommitSize (file tree + charts), BlameLookup
- `modules/` — Reusable components: TextViewer (scrollable text with selection/search), MarkdownRenderer
- Top-level `ui/` — Toolbar, SettingsModal, HelpModal, GitOperationWindow, DirectoryBrowser, PopupModal, Style

**Kaliber** (`third_party/kaliber/`) — Engine framework providing Platform abstraction, Renderer (Vulkan/OpenGL), ImGui backend integration, TaskRunner/ThreadPool, and utility types (Timer). See README file for details: `src/third_party/kaliber/README.md`.

### Threading Model

The main thread runs the render loop and ImGui. Git commands run on dedicated worker threads (one per `Git`-derived class). Workers signal the main thread via atomic flags and double-buffered data structures. A small ThreadPool (3 threads) handles deferred destruction and one-off background queries.

### Code Generation

GN actions in `src/gel/BUILD.gn` generate headers at build time:
- `help_data.h` — HELP.md embedded as string data
- `licenses_data.h` — LICENSE.md embedded
- `version.h` — Version from git tags (tracks .git/HEAD via depfile)
- `icon_data.h` — App icon embedded as byte array

### Delegate Interfaces

UI panels communicate with `MainWindow` via delegate interfaces (e.g., `CommitHistory::Delegate`, `CommitDiff::Delegate`, `Toolbar::Delegate`). When adding cross-panel interactions, extend the relevant delegate rather than creating direct dependencies between panels.

### Double-Buffer Pattern

Workers expose results to the main thread via `base::DoubleBuffer<T>`. Writers call `GetBack()` + `Swap()`, readers call `GetFront()`. Never access worker-owned data outside this pattern.

### Dependencies

Most third-party libraries (GLFW, ImGui, FreeType, glslang, SPIRV-Reflect, Vulkan Headers, Volk, VMA) are git submodules under `src/third_party/`, but not all of them. Submodules must not be modified. Embedded (non-submodule) libraries may be modified if needed.

## Mandatory Formatting (IMPORTANT)

**You MUST format every file you modify before committing. This is not optional.**

- **C/C++ files** (`.h`, `.cc`, `.cpp`): Run `clang-format -i <files>` on every changed file. **Exclude `src/third_party/` (except `src/third_party/kaliber/`)** — do not format vendored code.
- **BUILD.gn files**: Run `gn format <file>` on every changed file.

Always format as the last step before committing.
