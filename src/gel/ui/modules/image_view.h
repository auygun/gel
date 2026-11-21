// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_MODULES_IMAGE_VIEW_H
#define GEL_UI_MODULES_IMAGE_VIEW_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/renderer/renderer.h"

// Whether |path| has the extension of an image format ImageView decodes.
bool IsImageFile(const std::string& path);

// Shows an image: a blob from git or a file of the working tree, read and
// decoded on the thread pool, and shown pixel for pixel or scaled down to fit
// the view. A new load supersedes the one in progress, which stops early.
class ImageView {
 public:
  enum class State {
    kEmpty,
    kLoading,
    // Decoded and shown.
    kLoaded,
    // Could not be read or decoded, e.g. not an image after all.
    kFailed,
    // Too large to show. width() and height() tell its size, or are 0 if the
    // file was too large to decode.
    kTooLarge,
  };

  // |busy_callback| is called with true when a load starts and with false
  // when it ends.
  explicit ImageView(std::function<void(bool)> busy_callback);
  ~ImageView();

  ImageView(ImageView const&) = delete;
  ImageView& operator=(ImageView const&) = delete;

  // Loads |blob|. Git runs in |toplevel| if it isn't empty.
  void LoadBlob(const std::string& blob, const std::filesystem::path& toplevel);
  // Loads the file at |path|.
  void LoadFile(const std::filesystem::path& path);
  // Stops loading and drops the image.
  void Clear();

  // Main thread: uploads a newly decoded image, and destroys the textures
  // dropped since the last call. Call once per frame, before Render().
  void Update(eng::Renderer& renderer);

  // Main thread: the renderer was recreated, and the textures of the old one
  // are gone. Loads the shown image again, for a texture of the new one.
  void OnRendererChanged();

  // Main thread: destroys the textures, before this object goes away without
  // being drawn again. The destructor can't, as it can run on another thread,
  // e.g. at exit, where the renderer frees what is left.
  void DestroyTextures(eng::Renderer& renderer);

  // Renders the image in a child window, |width| wide (-FLT_MIN fills the
  // rest of the line), as tall as the space left.
  void Render(float width);

  State state() const { return state_; }
  int width() const { return width_; }
  int height() const { return height_; }

  // Top-right corner of the child window, left of where a scrollbar would be,
  // for an overlay toolbar lined up with the text viewer's.
  ImVec2 content_top_right() const { return content_top_right_; }

 private:
  struct Request {
    // A working tree file if |from_file|, otherwise |blob| in |toplevel|.
    bool from_file = false;
    std::filesystem::path path;
    std::string blob;
  };

  struct Image {
    State state = State::kFailed;
    int width = 0;
    int height = 0;
    bool has_alpha = false;
    std::vector<uint8_t> pixels;
  };

  // Starts loading |request_|.
  void Load();
  // Drops the texture, destroyed on the next Update(), when no longer drawn.
  void ReleaseTexture();

  // Thread pool: reads and decodes |request|, stopping early once |latest|
  // moves past |generation|.
  static Image ReadImage(const Request& request,
                         const std::atomic<int>& latest,
                         int generation);
  static Image DecodeImage(const std::string& data);

  std::function<void(bool)> busy_callback_;

  // The latest load, or Clear(). Shared with the loads in progress, so that a
  // superseded one stops early and its result is dropped.
  std::shared_ptr<std::atomic<int>> latest_;
  Request request_;

  State state_ = State::kEmpty;
  int width_ = 0;
  int height_ = 0;
  bool has_alpha_ = false;
  // Decoded and waiting to be uploaded.
  std::vector<uint8_t> pixels_;
  // Uploaded this frame. Kept until the next Update(), as the renderer can
  // read them until the frame is presented.
  std::vector<uint8_t> uploaded_pixels_;

  eng::Renderer::ResourceId texture_ = eng::Renderer::kInvalidId;
  // Textures dropped this frame, possibly drawn already, destroyed on the
  // next Update().
  std::vector<eng::Renderer::ResourceId> released_textures_;

  ImVec2 content_top_right_{0, 0};
};

#endif  // GEL_UI_MODULES_IMAGE_VIEW_H
