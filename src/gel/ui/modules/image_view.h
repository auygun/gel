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

#include "gel/ui/graphics_delegate.h"
#include "third_party/imgui/imgui/imgui.h"

// Whether |path| has the extension of an image format ImageView decodes.
bool IsImageFile(const std::string& path);

// Shows an image: a blob from git or a file of the working tree, read and
// decoded on the thread pool, and shown pixel for pixel or stretched to fill
// the view, drawn through a GraphicsDelegate. A new load supersedes the one in
// progress, which stops early.
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

  enum class ScaleMode {
    // Pixel for pixel, centered, with scrollbars where it's larger than the
    // view.
    kFullSize,
    // Scaled up or down to fill the view, keeping its aspect ratio, centered.
    kFill,
  };

  // |busy_callback| is called with true when a load starts and with false
  // when it ends.
  ImageView(GraphicsDelegate& graphics_delegate,
            std::function<void(bool)> busy_callback);
  ~ImageView();

  ImageView(ImageView const&) = delete;
  ImageView& operator=(ImageView const&) = delete;

  // Loads |blob|. Git runs in |toplevel| if it isn't empty.
  void LoadBlob(const std::string& blob, const std::filesystem::path& toplevel);
  // Loads the file at |path|.
  void LoadFile(const std::filesystem::path& path);
  // Stops loading and drops the image.
  void Clear();

  // Renders the image in a child window, |width| wide (-FLT_MIN fills the
  // rest of the line), as tall as the space left.
  void Render(float width);

  // How the image is fit to the view. Kept across loads.
  void SetScaleMode(ScaleMode mode);
  ScaleMode scale_mode() const { return scale_mode_; }

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

  // Draws a checkerboard over [min, max], where it's visible, behind an image
  // with transparent pixels.
  static void DrawCheckerboard(const ImVec2& min, const ImVec2& max);

  // Thread pool: reads and decodes |request|, stopping early once |latest|
  // moves past |generation|.
  static Image ReadImage(const Request& request,
                         const std::atomic<int>& latest,
                         int generation);
  static Image DecodeImage(const std::string& data);

  GraphicsDelegate& graphics_delegate_;
  std::function<void(bool)> busy_callback_;

  // The latest load, or Clear(). Shared with the loads in progress, so that a
  // superseded one stops early and its result is dropped.
  std::shared_ptr<std::atomic<int>> latest_;
  Request request_;

  State state_ = State::kEmpty;
  int width_ = 0;
  int height_ = 0;
  bool has_alpha_ = false;
  GraphicsDelegate::ImageId image_ = GraphicsDelegate::kNoImage;

  ScaleMode scale_mode_ = ScaleMode::kFill;
  // Whether to scroll to the image's center, for a new image or one just
  // switched to full size.
  bool center_scroll_ = true;

  ImVec2 content_top_right_{0, 0};
};

#endif  // GEL_UI_MODULES_IMAGE_VIEW_H
