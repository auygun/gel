## Overview

Kaliber is a C++20 engine framework providing platform abstraction, GPU rendering (Vulkan/OpenGL), ImGui backend integration, and concurrency utilities. It lives as a vendored dependency under `src/third_party/kaliber/` within the [gel](../../../CLAUDE.md) project.

## Build

Kaliber is built as part of the parent gel project using GN + Ninja. There are no standalone build targets. See the parent `CLAUDE.md` / `AGENTS.md` for build commands.

GN target: `//src/third_party/kaliber` (depends on `:base`, `:platform`, `:renderer`).

## Architecture

All engine types live in the `eng` namespace. Base/utility types live in `base`.

### Modules

**base/** — Concurrency and utility library:
- `TaskRunner` — Thread-safe FIFO task queue with `PostTask`, `PostTaskAndReply`, and `PostTaskAndReplyWithResult`. Supports thread-local instances and three consumer modes (`Multi`, `Single`, `Sequenced`).
- `ThreadPool` — Global singleton (`ThreadPool::Get()`) wrapping worker threads and a `TaskRunner`. Also creates `SequencedTaskRunner` instances for ordered background work.
- `Closure` / `Location` — `std::function<void()>` alias and debug-only source-location tracking (`HERE` macro). `BindWeak` binds a method to a `std::weak_ptr`.
- `LOG` / `CHECK` / `DCHECK` — Chromium-style streaming log macros. `DLOG`/`DCHECK` compile away in release.
- `vecmath.h` — Vector/matrix math types (`base::Vector2f`, `base::Vector3f`, `base::Vector4f`, `base::Matrix4f`).
- `timer.h`, `interpolation.h`, `hash.h`, `file.h` — Small utilities.

**platform/** — Windowing and input via GLFW:
- `Platform` — Creates/manages the GLFW window, tracks input state (keys, mouse, scroll), handles DPI scaling, clipboard, primary selection, CSD (client-side decorations), font enumeration, and dark mode detection. Platform-specific code in `platform_linux.cc`, `platform_win.cc`, `platform_mac.mm`.
- `PlatformObserver` — Interface for window lifecycle callbacks (`OnWindowCreated`, `OnFramebufferResized`, `LostFocus`, etc.).
- Linux-specific: Wayland primary selection protocol (`wayland_selection.h`), XDG desktop entry installation (`desktop_entry_linux.cc`).

**renderer/** — Abstract GPU renderer with two backends:
- `Renderer` — Abstract base class. Factory method `Renderer::Create(RendererType)` instantiates Vulkan or OpenGL, with automatic fallback. Manages resources via `ResourceId` handles (geometry, textures, shaders). Frame lifecycle: `PrepareForDrawing()` → draw calls → `Present()`.
- `RendererVulkan` — Vulkan backend using Volk (loader), VMA (memory allocator), and glslang/SPIRV-Reflect for shader compilation.
- `RendererOpenGL` — OpenGL backend using GLAD.
- `ShaderSource` — Loads vertex/fragment shader source with macro injection for cross-backend compatibility.
- `RendererTypes` — Enums and descriptors for primitives, vertex attributes, data types, image formats, and texture compression.

**Top-level** — ImGui integration:
- `ImguiBackend` — Bridges ImGui with Kaliber's `Platform` (input) and `Renderer` (drawing). Handles font loading (file or memory, with FreeType), texture atlas management, geometry upload, and primary selection sync.
- `input_codes.h` — Platform-agnostic `Key` and `MouseButton` enums.

### Key Patterns

- **Resource handles**: GPU resources are identified by `Renderer::ResourceId` (uint32). `kInvalidId` (0) is the sentinel. Create → use → destroy lifecycle.
- **Frame data**: Vertex/index/texture data passed to `Update*` methods must remain valid for the current frame (pointer-based, no copy).
- **Thread safety**: `Platform` and `Renderer` are main-thread only. `TaskRunner` and `ThreadPool` are thread-safe. `Platform::WakeEventLoop()` is safe to call from worker threads.

## Code Style

- Chromium C++ style (`.clang-format` in repo root)
- C++20 standard
- Include paths relative to `src/` (e.g., `#include "third_party/kaliber/base/log.h"`)
- Run `clang-format -i` on changed files before committing
