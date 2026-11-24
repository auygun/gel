// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_GRAPHICS_DELEGATE_H
#define GEL_UI_GRAPHICS_DELEGATE_H

#include <cstdint>
#include <string>
#include <vector>

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/renderer/renderer_types.h"

// What the UI needs of the graphics, without knowing the renderer: images to
// show, the renderer's settings, and the window's background. Implemented by
// Gel, the only class that knows the renderer. Main thread only.
class GraphicsDelegate {
 public:
  using ImageId = int;
  static constexpr ImageId kNoImage = 0;

  virtual ~GraphicsDelegate() = default;

  // Adds an image, |width| x |height| pixels of RGBA, 4 bytes each, and
  // returns the id to draw it by. The pixels are uploaded and then freed: the
  // image goes when the renderer does, see DrawImage().
  virtual ImageId CreateImage(int width,
                              int height,
                              std::vector<uint8_t> rgba) = 0;

  // Removes image |id|. It can still be drawn in the frame it is removed in.
  // Safe to call from a destructor, also once the main loop has ended.
  virtual void DestroyImage(ImageId id) = 0;

  // Draws image |id| into the rect [min, max] of the current window. Returns
  // false if the image is gone, as all are once the renderer changes: its
  // owner then creates it again.
  virtual bool DrawImage(ImageId id, const ImVec2& min, const ImVec2& max) = 0;

  // The renderer in use, and the GPUs it can use and the one it uses, for the
  // settings.
  virtual eng::RendererType GetRendererType() const = 0;
  virtual std::vector<std::string> GetAvailableGpus() const = 0;
  virtual int GetSelectedGpuIndex() const = 0;

  // Sets the window's background color, which the UI is drawn over, e.g. for
  // a new style.
  virtual void SetBackgroundColor(const ImVec4& color) = 0;
};

#endif  // GEL_UI_GRAPHICS_DELEGATE_H
