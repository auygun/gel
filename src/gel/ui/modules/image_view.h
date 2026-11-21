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

#include "gel/ui/image_delegate.h"
#include "third_party/imgui/imgui/imgui.h"

// Whether |path| has the extension of an image format ImageView decodes.
bool IsImageFile(const std::string& path);

// Shows an image: a blob from git or a file of the working tree, read and
// decoded on the thread pool, and shown pixel for pixel or scaled down to fit
// the view, drawn through an ImageDelegate. A new load supersedes the one in
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

  // |busy_callback| is called with true when a load starts and with false
  // when it ends.
  ImageView(ImageDelegate& image_delegate,
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

  // Thread pool: reads and decodes |request|, stopping early once |latest|
  // moves past |generation|.
  static Image ReadImage(const Request& request,
                         const std::atomic<int>& latest,
                         int generation);
  static Image DecodeImage(const std::string& data);

  ImageDelegate& image_delegate_;
  std::function<void(bool)> busy_callback_;

  // The latest load, or Clear(). Shared with the loads in progress, so that a
  // superseded one stops early and its result is dropped.
  std::shared_ptr<std::atomic<int>> latest_;
  Request request_;

  State state_ = State::kEmpty;
  int width_ = 0;
  int height_ = 0;
  bool has_alpha_ = false;
  ImageDelegate::ImageId image_ = ImageDelegate::kNoImage;

  ImVec2 content_top_right_{0, 0};
};

#endif  // GEL_UI_MODULES_IMAGE_VIEW_H
