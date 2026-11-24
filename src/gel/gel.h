// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_GEL_H
#define GEL_GEL_H

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "gel/persistent_settings.h"
#include "gel/ui/graphics_delegate.h"
#include "gel/ui/main_window.h"
#include "third_party/kaliber/base/timer.h"
#include "third_party/kaliber/imgui_backend.h"
#include "third_party/kaliber/platform/platform.h"
#include "third_party/kaliber/platform/platform_observer.h"
#include "third_party/kaliber/renderer/renderer.h"

// Application lifecycle class. Sets up the platform window, renderer, and ImGui
// backend, then enters the main loop. Delegates all UI and git logic to
// MainWindow. The only class that knows the renderer: the UI gets what it
// needs of it through GraphicsDelegate, its images being textures kept here.
class Gel final : public eng::PlatformObserver, public GraphicsDelegate {
 public:
  explicit Gel(eng::Platform* platform);
  ~Gel() final;

  // Initializes subsystems and runs the main loop. Does not return until the
  // window is closed. Returns ownership of the renderer and ImGui backend so
  // the caller can control their destruction order.
  std::pair<std::unique_ptr<eng::Renderer>, std::unique_ptr<eng::ImguiBackend>>
  Run(const CommandLine& command_line);

 private:
  eng::Platform* platform_ = nullptr;
  std::unique_ptr<eng::Renderer> renderer_;
  std::unique_ptr<eng::ImguiBackend> imgui_backend_;
  base::DeltaTimer timer_;

  // The textures of the UI's images. They go with the renderer: a new one
  // starts with none, and the UI loads its images again.
  // Declared before |main_window_|, which removes its images as it goes.
  std::unordered_map<ImageId, eng::Renderer::ResourceId> images_;
  ImageId next_image_id_ = kNoImage + 1;
  // Removed this frame, and possibly drawn in it: destroyed once the frame is
  // presented.
  std::vector<eng::Renderer::ResourceId> removed_textures_;
  // Pixels uploaded this frame, and the frame before. The renderer uploads
  // them on a thread of its own, so they are freed a frame later.
  std::vector<std::vector<uint8_t>> uploading_pixels_;
  std::vector<std::vector<uint8_t>> uploaded_pixels_;

  PersistentSettings settings_;
  MainWindow main_window_;

  bool keep_drawing_ = false;
  // Work the user is waiting on, shown by the mouse spinner: stepping to a
  // search match and external tools starting. Background git commands are
  // tracked by BusyTracker and shown in the toolbar instead.
  std::atomic<int> mouse_busy_tasks_{0};

  std::optional<eng::RendererType> pending_renderer_;
  std::optional<PendingFont> pending_font_;

  bool IsDarkMode() const;
  bool CreateRenderer(eng::RendererType type);
  void OnBusyChanged(bool busy);
  // Destroys the textures of the images removed before the frame just
  // presented, and frees the pixels uploaded the frame before.
  void EndFrame();

  // GraphicsDelegate implementation
  ImageId CreateImage(int width, int height, std::vector<uint8_t> rgba) final;
  void DestroyImage(ImageId id) final;
  bool DrawImage(ImageId id, const ImVec2& min, const ImVec2& max) final;
  eng::RendererType GetRendererType() const final;
  std::vector<std::string> GetAvailableGpus() const final;
  int GetSelectedGpuIndex() const final;
  void SetBackgroundColor(const ImVec4& color) final;

  // PlatformObserver implementation
  void OnWindowCreated() final;
  void OnWindowDestroyed() final;
  void OnFramebufferResized(int width, int height) final;
  void LostFocus() final;
  void GainedFocus() final;
};

#endif  // GEL_GEL_H
