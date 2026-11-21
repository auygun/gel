// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_IMAGE_DELEGATE_H
#define GEL_UI_IMAGE_DELEGATE_H

#include <cstdint>
#include <vector>

#include "third_party/imgui/imgui/imgui.h"

// Images for the UI to show, without knowing how they are drawn: the UI hands
// over an image's pixels and gets an id to draw it by. Implemented by Gel,
// which keeps the textures. Main thread only.
class ImageDelegate {
 public:
  using ImageId = int;
  static constexpr ImageId kNoImage = 0;

  virtual ~ImageDelegate() = default;

  // Adds an image, |width| x |height| pixels of RGBA, 4 bytes each, and
  // returns its id.
  virtual ImageId CreateImage(int width,
                              int height,
                              std::vector<uint8_t> rgba) = 0;

  // Removes image |id|. It can still be drawn in the frame it is removed in.
  // Safe to call from a destructor, also once the main loop has ended.
  virtual void DestroyImage(ImageId id) = 0;

  // Draws image |id| into the rect [min, max] of the current window.
  virtual void DrawImage(ImageId id, const ImVec2& min, const ImVec2& max) = 0;
};

#endif  // GEL_UI_IMAGE_DELEGATE_H
