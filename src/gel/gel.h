// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_GEL_H
#define GEL_GEL_H

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "gel/persistent_settings.h"
#include "gel/ui/main_window.h"
#include "third_party/kaliber/base/timer.h"
#include "third_party/kaliber/imgui_backend.h"
#include "third_party/kaliber/platform/platform.h"
#include "third_party/kaliber/platform/platform_observer.h"
#include "third_party/kaliber/renderer/renderer.h"

// Application lifecycle class. Sets up the platform window, renderer, and ImGui
// backend, then enters the main loop. Delegates all UI and git logic to
// MainWindow.
class Gel final : public eng::PlatformObserver {
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

  PersistentSettings settings_;
  MainWindow main_window_;

  bool keep_drawing_ = false;
  std::atomic<int> active_bg_tasks_{0};

  std::optional<eng::RendererType> pending_renderer_;
  std::optional<PendingFont> pending_font_;
  eng::Renderer::ResourceId icon_texture_ = eng::Renderer::kInvalidId;

  bool IsDarkMode() const;
  bool CreateRenderer(eng::RendererType type);
  void CreateIconTexture();
  void OnBusyChanged(bool busy);

  // PlatformObserver implementation
  void OnWindowCreated() final;
  void OnWindowDestroyed() final;
  void OnFramebufferResized(int width, int height) final;
  void LostFocus() final;
  void GainedFocus() final;
};

#endif  // GEL_GEL_H
