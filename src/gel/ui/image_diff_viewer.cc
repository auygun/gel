// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/image_diff_viewer.h"

#include <algorithm>
#include <cmath>

#include "base/exec.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/base/thread_pool.h"
#include "third_party/stb/stb_image.h"

using namespace base;
using namespace eng;

ImageDiffViewer::ImageDiffViewer(int id,
                                 const std::string& old_ref,
                                 const std::string& new_ref,
                                 const std::string& path,
                                 bool new_from_worktree)
    : id_(id), path_(path) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s###img_diff_%d", path_.c_str(), id_);
  title_ = buf;

  load_token_ = std::make_shared<bool>(true);
  std::weak_ptr<bool> token = load_token_;

  auto on_load_complete = [this, token](ImageData& target, ImageData data) {
    if (token.expired())
      return;
    target = std::move(data);
    if (--pending_loads_ == 0) {
      load_state_ = (old_image_.pixels.empty() && new_image_.pixels.empty())
                        ? LoadState::kError
                        : LoadState::kLoaded;
      if (load_state_ == LoadState::kError)
        error_ = "Failed to decode image(s).";
    }
  };

  if (!old_ref.empty()) {
    ++pending_loads_;
    ThreadPool::Get().PostTaskAndReplyWithResult<ImageData>(
        HERE, [ref = old_ref, p = path]() { return LoadImageFromGit(ref, p); },
        [this, on_load_complete](ImageData data) {
          on_load_complete(old_image_, std::move(data));
        });
  }

  if (new_from_worktree) {
    ++pending_loads_;
    ThreadPool::Get().PostTaskAndReplyWithResult<ImageData>(
        HERE, [p = path]() { return LoadImageFromFile(p); },
        [this, on_load_complete](ImageData data) {
          on_load_complete(new_image_, std::move(data));
        });
  } else if (!new_ref.empty()) {
    ++pending_loads_;
    ThreadPool::Get().PostTaskAndReplyWithResult<ImageData>(
        HERE, [ref = new_ref, p = path]() { return LoadImageFromGit(ref, p); },
        [this, on_load_complete](ImageData data) {
          on_load_complete(new_image_, std::move(data));
        });
  }

  if (pending_loads_ == 0) {
    load_state_ = LoadState::kError;
    error_ = "No image references to compare.";
  }
}

ImageDiffViewer::~ImageDiffViewer() {
  DestroyTextures();
}

void ImageDiffViewer::Update(Renderer& renderer, bool window_focused) {
  if (renderer_ && renderer_ != &renderer) {
    old_texture_ = Renderer::kInvalidId;
    new_texture_ = Renderer::kInvalidId;
  }
  renderer_ = &renderer;

  if (!open_)
    return;

  if (first_frame_) {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(display.x * 0.7f, display.y * 0.8f),
                             ImGuiCond_FirstUseEver);
    first_frame_ = false;
  }

  if (!window_focused)
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
  if (ImGui::Begin(title_.c_str(), &open_)) {
    // Create textures lazily once loaded (inside the window so they are never
    // created after the window closes).
    if (load_state_ == LoadState::kLoaded &&
        old_texture_ == Renderer::kInvalidId &&
        new_texture_ == Renderer::kInvalidId) {
      CreateTextures(renderer);
    }

    if (load_state_ == LoadState::kLoading) {
      ImGui::TextUnformatted("Loading...");
    } else if (load_state_ == LoadState::kError) {
      ImGui::TextUnformatted(error_.c_str());
    } else {
      ImGui::TextUnformatted(path_.c_str());

      // Side-by-side layout in the remaining area.
      float available_w = ImGui::GetContentRegionAvail().x;
      float available_h = ImGui::GetContentRegionAvail().y;
      float half_w = (available_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

      auto draw_image = [&](const char* label, const ImageData& img,
                            Renderer::ResourceId tex) {
        ImGui::BeginChild(label, ImVec2(half_w, available_h),
                          ImGuiChildFlags_Borders);
        if (!img.pixels.empty() && tex != Renderer::kInvalidId) {
          ImGui::Text("%s  %dx%d", label, img.width, img.height);
          float content_w = ImGui::GetContentRegionAvail().x;
          float content_h = ImGui::GetContentRegionAvail().y;
          float scale = std::min(content_w / static_cast<float>(img.width),
                                 content_h / static_cast<float>(img.height));
          scale = std::min(scale, 1.0f);
          float display_w = static_cast<float>(img.width) * scale;
          float display_h = static_cast<float>(img.height) * scale;
          ImGui::Image((ImTextureID)(intptr_t)tex,
                       ImVec2(display_w, display_h));
        } else {
          ImGui::TextUnformatted(label);
          ImGui::TextDisabled("(no image)");
        }
        ImGui::EndChild();
      };

      draw_image("Old", old_image_, old_texture_);
      ImGui::SameLine();
      draw_image("New", new_image_, new_texture_);
    }
  }
  ImGui::End();
  if (!window_focused)
    ImGui::PopStyleColor();
}

void ImageDiffViewer::DestroyTextures() {
  if (renderer_) {
    if (old_texture_ != Renderer::kInvalidId) {
      renderer_->DestroyTexture(old_texture_);
      old_texture_ = Renderer::kInvalidId;
    }
    if (new_texture_ != Renderer::kInvalidId) {
      renderer_->DestroyTexture(new_texture_);
      new_texture_ = Renderer::kInvalidId;
    }
    renderer_ = nullptr;
  }
  load_state_ = LoadState::kIdle;
}

void ImageDiffViewer::CreateTextures(Renderer& renderer) {
  if (!old_image_.pixels.empty()) {
    old_texture_ = renderer.CreateTexture();
    renderer.UpdateTexture(
        old_texture_, old_image_.width, old_image_.height, ImageFormat::kRGBA32,
        static_cast<size_t>(old_image_.width) * old_image_.height * 4,
        old_image_.pixels.data());
  }
  if (!new_image_.pixels.empty()) {
    new_texture_ = renderer.CreateTexture();
    renderer.UpdateTexture(
        new_texture_, new_image_.width, new_image_.height, ImageFormat::kRGBA32,
        static_cast<size_t>(new_image_.width) * new_image_.height * 4,
        new_image_.pixels.data());
  }
}

ImageDiffViewer::ImageData ImageDiffViewer::LoadImageFromFile(
    const std::string& path) {
  ImageData result;
  int w, h;
  unsigned char* pixels = stbi_load(path.c_str(), &w, &h, nullptr, 4);
  if (!pixels)
    return result;
  result.width = w;
  result.height = h;
  result.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
  stbi_image_free(pixels);
  return result;
}

ImageDiffViewer::ImageData ImageDiffViewer::LoadImageFromGit(
    const std::string& ref,
    const std::string& path) {
  ImageData result;

  Exec proc;
  std::string spec =
      (!ref.empty() && ref.back() == ':') ? ref + path : ref + ":" + path;
  std::vector<std::string> args = {"git", "show", spec};
  if (!proc.Start(args))
    return result;

  while (proc.Poll()) {
  }

  if (proc.GetStatus() != Exec::Status::EXITED || proc.GetResult() != 0)
    return result;

  std::string data = std::move(proc.GetOut());
  if (data.empty())
    return result;

  int w, h;
  unsigned char* pixels =
      stbi_load_from_memory(reinterpret_cast<const unsigned char*>(data.data()),
                            static_cast<int>(data.size()), &w, &h, nullptr, 4);
  if (!pixels)
    return result;

  result.width = w;
  result.height = h;
  result.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
  stbi_image_free(pixels);
  return result;
}
