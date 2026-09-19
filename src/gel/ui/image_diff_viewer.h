// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_IMAGE_DIFF_VIEWER_H
#define GEL_UI_IMAGE_DIFF_VIEWER_H

#include <memory>
#include <string>
#include <vector>

#include "third_party/kaliber/renderer/renderer.h"

// Window that displays a side-by-side visual diff of an image file between two
// commits. Extracts old and new versions using "git show", decodes them with
// stb_image, and renders them as textures.
class ImageDiffViewer {
 public:
  // |id| must be unique across all live ImageDiffViewer instances so that each
  // gets its own ImGui window.
  ImageDiffViewer(int id,
                  const std::string& old_ref,
                  const std::string& new_ref,
                  const std::string& path,
                  bool new_from_worktree = false);
  ~ImageDiffViewer();

  // Renders the window. Must be called each frame.
  void Update(eng::Renderer& renderer, bool window_focused);

  bool IsOpen() const { return open_; }

 private:
  struct ImageData {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
  };

  enum class LoadState { kIdle, kLoading, kLoaded, kError };

  int id_;
  std::string title_;
  bool open_ = true;
  bool first_frame_ = true;
  LoadState load_state_ = LoadState::kLoading;
  std::string error_;
  std::string path_;

  // Image data (filled by worker thread, consumed on main thread).
  ImageData old_image_;
  ImageData new_image_;

  // GPU textures (created/destroyed on main thread).
  eng::Renderer::ResourceId old_texture_ = eng::Renderer::kInvalidId;
  eng::Renderer::ResourceId new_texture_ = eng::Renderer::kInvalidId;
  eng::Renderer* renderer_ = nullptr;

  int pending_loads_ = 0;
  std::shared_ptr<bool> load_token_;

  void DestroyTextures();
  void CreateTextures(eng::Renderer& renderer);

  static ImageData LoadImageFromGit(const std::string& ref,
                                    const std::string& path);
  static ImageData LoadImageFromFile(const std::string& path);
};

#endif  // GEL_UI_IMAGE_DIFF_VIEWER_H
