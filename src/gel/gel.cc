// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/gel.h"

#include <cmath>
#include <limits>
#include <utility>

#include "base/command_line.h"
#include "gel/dejavu_sans_mono.h"
#include "icon_data.h"
#include "third_party/glfw/glfw/include/GLFW/glfw3.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"
#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/base/task_runner.h"
#include "third_party/kaliber/base/thread_pool.h"
#include "third_party/kaliber/base/timer.h"
#include "third_party/stb/stb_image.h"

using namespace base;
using namespace eng;

namespace eng {

extern void KaliberMain(Platform* platform) {
  const CommandLine command_line(platform->GetMainArgC(),
                                 platform->GetMainArgV(), {'n'});
  TaskRunner::CreateThreadLocalTaskRunner();
  ThreadPool thread_pool;
  // Auxiliary pool for deferred destruction and one-off background queries
  // (e.g. git --version). Git command workers use their own dedicated threads.
  // 3 threads is enough since these tasks are short-lived and rarely
  // concurrent.
  thread_pool.Initialize(3);

  auto gel = std::make_unique<Gel>(platform);
  auto [renderer, imgui_backend] = gel->Run(command_line);
  thread_pool.CancelTasks();
  platform->SetObserver(nullptr);
  thread_pool.PostTask(HERE, [gel = std::shared_ptr<Gel>(std::move(gel))] {});

  if (renderer && imgui_backend) {
    // Keep the window responsive while the thread pool drains. Draw an
    // animated spinner so the user sees that shutdown is in progress.
    DeltaTimer shutdown_timer;
    float elapsed = 0;
    while (thread_pool.GetPendingTaskCount() > 0) {
      platform->Update(0);
      float dt = shutdown_timer.Delta();
      elapsed += dt;
      imgui_backend->ProcessInput(platform);
      imgui_backend->NewFrame(dt);

      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->WorkPos);
      ImGui::SetNextWindowSize(viewport->WorkSize);
      ImGui::Begin("##shutdown", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoBackground);

      ImVec2 center(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                    viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
      float radius = 16.0f;
      int num_segments = 12;
      float speed = 8.0f;
      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      for (int i = 0; i < num_segments; ++i) {
        float angle =
            elapsed * speed + (float)i / (float)num_segments * 2.0f * IM_PI;
        float alpha = (float)(i + 1) / (float)num_segments;
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text, alpha);
        float dot_radius = 2.5f;
        ImVec2 pos(center.x + radius * std::cos(angle),
                   center.y + radius * std::sin(angle));
        draw_list->AddCircleFilled(pos, dot_radius, color);
      }

      ImGui::SetCursorPos(ImVec2(center.x - 50.0f, center.y + radius + 32.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
      if (ImGui::Button("Exit now", ImVec2(100, 0))) {
        _Exit(0);
      }
      ImGui::PopStyleVar();

      ImGui::End();

      renderer->PrepareForDrawing();
      imgui_backend->Draw();
      renderer->Present();
    }
  }

  // Always wait for all posted tasks to complete before the thread pool
  // is destroyed, even when we have nothing to render.
  while (thread_pool.GetPendingTaskCount() > 0)
    platform->Update(0);
}

}  // namespace eng

Gel::Gel(Platform* platform)
    : platform_(platform),
      main_window_(
          *platform,
          settings_,
          [this](bool b) { OnBusyChanged(b); },
          [this] { keep_drawing_ = true; }) {}

Gel::~Gel() = default;

bool Gel::IsDarkMode() const {
  if (settings_.style == Style::kSystem)
    return platform_->IsDarkMode();
  return !IsLightStyle(settings_.style);
}

bool Gel::CreateRenderer(RendererType type) {
  if (renderer_ && renderer_->IsInitialzed() &&
      renderer_->GetRendererType() == type)
    return true;

  auto try_init = [this](RendererType t) {
    renderer_.reset();
    platform_->CreateMainWindow(t == RendererType::kOpenGL,
                                settings_.window_width,
                                settings_.window_height);
    if (!IsDarkMode())
      platform_->SetWindowBackgroundColor(0.87f, 0.87f, 0.87f);
    int icon_w, icon_h;
    unsigned char* icon_pixels = stbi_load_from_memory(
        kIconData, sizeof(kIconData), &icon_w, &icon_h, nullptr, 4);
    if (icon_pixels) {
      platform_->SetWindowIcon(icon_w, icon_h, icon_pixels);
      stbi_image_free(icon_pixels);
    }
    if (settings_.window_x != -1)
      platform_->SetWindowPosition(settings_.window_x, settings_.window_y);
    if (settings_.window_maximized)
      platform_->SetMaximized(true);
    renderer_ = Renderer::Create(t);
    if (settings_.vulkan_device[0] != '\0')
      renderer_->SetPreferredGpu(settings_.vulkan_device);
    return renderer_->Initialize(platform_);
  };

  if (try_init(type)) {
    if (settings_.renderer_type != type) {
      settings_.renderer_type = type;
      settings_.Save();
    }
    return true;
  }

#if defined(OS_APPLE)
  // OpenGL is not supported on macOS.
  return false;
#else
  auto fallback = type == RendererType::kVulkan ? RendererType::kOpenGL
                                                : RendererType::kVulkan;

  DLOG(0) << renderer_->GetDebugName()
          << " initialization failed, falling back to "
          << (fallback == RendererType::kOpenGL ? "OpenGL" : "Vulkan") << ".";

  if (try_init(fallback)) {
    if (settings_.renderer_type != fallback) {
      settings_.renderer_type = fallback;
      settings_.Save();
    }
    return true;
  }

  return false;
#endif
}

void Gel::CreateIconTexture() {
  int icon_w, icon_h;
  unsigned char* pixels = stbi_load_from_memory(kIconData, sizeof(kIconData),
                                                &icon_w, &icon_h, nullptr, 4);
  if (pixels) {
    icon_texture_ = renderer_->CreateTexture();
    renderer_->UpdateTexture(icon_texture_, icon_w, icon_h,
                             ImageFormat::kRGBA32,
                             static_cast<size_t>(icon_w) * icon_h * 4, pixels);
    stbi_image_free(pixels);
  }
}

std::pair<std::unique_ptr<Renderer>, std::unique_ptr<ImguiBackend>> Gel::Run(
    const CommandLine& command_line) {
  platform_->SetObserver(this);

  settings_.Load();

#if defined(OS_LINUX)
  switch (settings_.display_backend) {
    case DisplayBackend::kWayland:
      platform_->SetDisplayBackendHint(GLFW_PLATFORM_WAYLAND);
      break;
    case DisplayBackend::kX11:
      platform_->SetDisplayBackendHint(GLFW_PLATFORM_X11);
      break;
    default:
      break;
  }
#endif

  // Remove the window manager title bar and use client-side decorations
  // (custom window controls drawn in the toolbar).
  if (settings_.client_side_decorations)
    platform_->SetDecorated(false);

  if (!CreateRenderer(settings_.renderer_type)) {
    LOG(0) << "Failed to create renderer";
    return {};
  }

  if (!platform_->IsDecorated()) {
    auto vis = platform_->GetCSDButtonVisibility();
    main_window_.SetUsingCSD(true, vis.minimize, vis.maximize);
  }

  renderer_->SetClearColor(IsDarkMode()
                               ? base::Vector4f{0, 0, 0, 1}
                               : base::Vector4f{0.87f, 0.87f, 0.87f, 1});

  settings_.SanitizeLayoutValues(platform_->GetWindowWidth(),
                                 platform_->GetWindowHeight());

  imgui_backend_ = std::make_unique<ImguiBackend>();
  // Used when no font is configured, or when the configured one fails to load.
  imgui_backend_->SetDefaultFont(DejaVuSansMono_compressed_data,
                                 DejaVuSansMono_compressed_size);
  imgui_backend_->Initialize(platform_, std::string(settings_.font_path),
                             settings_.font_face_index);
  imgui_backend_->CreateRenderResources(renderer_.get());
  imgui_backend_->SetGeometryChangedCallback([this] { keep_drawing_ = true; });
  CreateIconTexture();

  main_window_.Initialize(command_line);

  timer_ = DeltaTimer();

  // Main loop: process OS events, run one ImGui frame, present.
  // When all workers are idle, pass a long timeout so the platform can sleep
  // until the next user input or worker wake. Render at least 2 frames per
  // event so ImGui can process deferred state changes (e.g. popups opening on
  // the frame after the click).
  int redraw_frames = 1;
  bool cursor_blinking = false;
  for (;;) {
    // When a text input cursor is blinking, use a short timeout so the
    // cursor animation stays visible instead of freezing when idle.
    constexpr double kCursorBlinkTimeout = 0.4;
    double wait = redraw_frames > 0
                      ? 0
                      : (cursor_blinking ? kCursorBlinkTimeout
                                         : std::numeric_limits<double>::max());
    platform_->Update(wait);
    if (platform_->should_exit())
      break;

    if (pending_renderer_) {
      settings_.window_maximized = platform_->IsMaximized();
      platform_->GetWindowPosition(settings_.window_x, settings_.window_y);
      if (!settings_.window_maximized) {
        settings_.window_width = platform_->GetWindowWidth();
        settings_.window_height = platform_->GetWindowHeight();
      }
      renderer_.reset();
      CreateRenderer(*pending_renderer_);
      {
        auto& bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        renderer_->SetClearColor({bg.x, bg.y, bg.z, bg.w});
      }
      imgui_backend_->CreateRenderResources(renderer_.get());
      CreateIconTexture();
      pending_renderer_.reset();
      continue;
    }

    // Decide whether to draw. Active animations, background tasks, user
    // input, a blinking cursor, or a pending tooltip delay all require
    // continuous redrawing. We render at least 2 frames per event so
    // ImGui can process deferred state changes (e.g. popups opening on
    // the frame after the click).
    ImGuiContext& g = *ImGui::GetCurrentContext();
    bool tooltip_delay_active =
        g.HoverItemDelayIdPreviousFrame != 0 &&
        (g.HoverItemDelayTimer < g.Style.HoverDelayNormal ||
         g.HoverItemUnlockedStationaryId != g.HoverItemDelayIdPreviousFrame);
    if (std::exchange(keep_drawing_, false) ||
        active_bg_tasks_.load(std::memory_order_relaxed) > 0 ||
        platform_->NewEventReceived() || platform_->IsAnyMouseButtonDown() ||
        platform_->IsAnyKeyDown() || cursor_blinking || tooltip_delay_active) {
      redraw_frames = 3;
    }
    if (redraw_frames <= 0)
      continue;
    --redraw_frames;

    // Rebuild the font atlas between frames (before NewFrame) so the atlas
    // is valid for the entire frame.
    if (pending_font_) {
      imgui_backend_->RebuildFont(pending_font_->path,
                                  pending_font_->face_index);
      pending_font_.reset();
    }

    float frame_delta = timer_.Delta();
    imgui_backend_->ProcessInput(platform_);

    // Enable keyboard nav while the settings modal is open so Tab/Shift+Tab
    // cycles through all widgets (combos, sliders, etc.), not just text inputs.
    auto& config_flags = ImGui::GetIO().ConfigFlags;
    if (main_window_.IsSettingsModalOpen() ||
        main_window_.IsDirectoryBrowserOpen())
      config_flags |= ImGuiConfigFlags_NavEnableKeyboard;
    else
      config_flags &= ~ImGuiConfigFlags_NavEnableKeyboard;

    imgui_backend_->NewFrame(frame_delta);

    // Confine the cursor to the content area while ImGui is capturing the mouse
    // with a button held (e.g. scrollbar drag).  This prevents the cursor from
    // reaching window decorations where GLFW/WM would consume the button
    // release and leave ImGui's capture state stuck. Not needed with CSD since
    // there are no WM decorations.
    if (!main_window_.using_csd()) {
      platform_->SetCursorCaptured(
          ImGui::GetIO().WantCaptureMouse &&
          (platform_->IsMouseButtonDown(MouseButton::Left) ||
           platform_->IsMouseButtonDown(MouseButton::Right) ||
           platform_->IsMouseButtonDown(MouseButton::Middle)));
    }

    TaskRunner::GetThreadLocalTaskRunner()->RunTasks<Consumer::Single>();

    auto ur = main_window_.Update(
        frame_delta, *renderer_, (ImTextureID)(intptr_t)icon_texture_,
        active_bg_tasks_.load(std::memory_order_relaxed));
    if (ur.pending_renderer)
      pending_renderer_ = ur.pending_renderer;
    if (ur.pending_font)
      pending_font_ = ur.pending_font;

    cursor_blinking =
        main_window_.IsWindowFocused() && ImGui::GetIO().WantTextInput;

    renderer_->PrepareForDrawing();
    imgui_backend_->Draw();
    renderer_->Present();
  }

  // Save window geometry and panel layout before exiting.
  settings_.window_maximized = platform_->IsMaximized();
  platform_->GetWindowPosition(settings_.window_x, settings_.window_y);
  if (!settings_.window_maximized) {
    settings_.window_width = platform_->GetWindowWidth();
    settings_.window_height = platform_->GetWindowHeight();
  }

  settings_.Save();

  // Clear the callback before handing imgui_backend_ to the caller, since the
  // callback captures |this| and the caller may outlive this Gel instance.
  imgui_backend_->SetGeometryChangedCallback(nullptr);

  return {std::move(renderer_), std::move(imgui_backend_)};
}

void Gel::OnBusyChanged(bool busy) {
  if (busy)
    active_bg_tasks_.fetch_add(1, std::memory_order_relaxed);
  else
    active_bg_tasks_.fetch_sub(1, std::memory_order_relaxed);
}

void Gel::OnWindowCreated() {
  if (renderer_)
    renderer_->Initialize(platform_);
}

void Gel::OnWindowDestroyed() {
  renderer_->Shutdown();
}

void Gel::OnFramebufferResized(int width, int height) {
  main_window_.SetCSDInteractive(false);
  if (renderer_ && (width != renderer_->GetFramebufferWidth() ||
                    height != renderer_->GetFramebufferHeight())) {
    renderer_->OnFramebufferResized(width, height);
  }
}

void Gel::LostFocus() {
  main_window_.OnLostFocus();
}

void Gel::GainedFocus() {
  main_window_.OnGainedFocus();
  // Reset the delta timer so the first frame after regaining focus doesn't
  // get a large delta from the time spent unfocused.
  timer_ = DeltaTimer();
}
