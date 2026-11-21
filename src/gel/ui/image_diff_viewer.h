// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_IMAGE_DIFF_VIEWER_H
#define GEL_UI_IMAGE_DIFF_VIEWER_H

#include <filesystem>
#include <functional>
#include <string>

#include "gel/ui/image_delegate.h"
#include "gel/ui/modules/image_view.h"

// Window that displays a side-by-side visual diff of an image file between two
// commits, each side an ImageView.
class ImageDiffViewer {
 public:
  // |id| must be unique across all live ImageDiffViewer instances so that each
  // gets its own ImGui window. |path| is relative to the repository. A side
  // whose ref is empty shows no image. With |new_from_worktree|, the new image
  // is read from the working tree at |toplevel|. The images are drawn through
  // |image_delegate|. |busy_callback| is called with true when an image starts
  // loading and with false when it's done.
  ImageDiffViewer(int id,
                  const std::filesystem::path& toplevel,
                  const std::string& old_ref,
                  const std::string& new_ref,
                  const std::string& path,
                  bool new_from_worktree,
                  ImageDelegate& image_delegate,
                  std::function<void(bool)> busy_callback);
  ~ImageDiffViewer();

  // Renders the window. Must be called each frame.
  void Update(bool window_focused);

  bool IsOpen() const { return open_; }

 private:
  // Renders one side, |width| wide, with |label| and the image's size above
  // it.
  static void RenderSide(const char* label, ImageView& view, float width);

  std::string title_;
  std::string path_;
  bool open_ = true;
  bool first_frame_ = true;

  ImageView old_view_;
  ImageView new_view_;
};

#endif  // GEL_UI_IMAGE_DIFF_VIEWER_H
