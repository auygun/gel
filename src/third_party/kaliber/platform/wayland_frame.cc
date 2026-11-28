#include "third_party/kaliber/platform/wayland_frame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>

#include "third_party/kaliber/base/log.h"
#define _GLFW_X11
#define _GLFW_WAYLAND
extern "C" {
#include "internal.h"
}
#include "third_party/glfw/glfw/include/GLFW/glfw3.h"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace eng {

namespace {

// Extent of the shadow around the window, in surface coordinates.
constexpr int kShadowSize = 16;
// Band of the shadow next to the window that resizes it. The rest lets the
// pointer through to whatever is below.
constexpr int kResizeSize = 10;
// Length along an edge, from the window's corner, that resizes diagonally.
constexpr int kCornerSize = 20;

// Shadow opacity next to the window, falling off to nothing at kShadowSize.
constexpr float kShadowAlpha = 0.25f;
// Opacity of a one unit thick outline around the window, which keeps it apart
// from a background of the same color.
constexpr float kOutlineAlpha = 0.2f;

float ShadowAlpha(float distance) {
  if (distance >= kShadowSize)
    return 0;
  float falloff = 1 - distance / kShadowSize;
  float alpha = kShadowAlpha * falloff * falloff * falloff;
  if (distance < 1)
    alpha = std::max(alpha, kOutlineAlpha);
  return alpha;
}

_GLFWwindow* Internal(GLFWwindow* window) {
  return reinterpret_cast<_GLFWwindow*>(window);
}

// Creates a |width| by |height| buffer of black, premultiplied, with the
// opacity |alpha| returns for each pixel.
template <typename AlphaFn>
wl_buffer* CreateShadowBuffer(int width, int height, AlphaFn alpha) {
  const int stride = width * 4;
  const int size = stride * height;
  int fd = memfd_create("gel-shadow", MFD_CLOEXEC);
  if (fd < 0)
    return nullptr;
  if (ftruncate(fd, size) < 0) {
    close(fd);
    return nullptr;
  }
  void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    return nullptr;
  }
  auto* pixels = static_cast<uint8_t*>(data);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      uint8_t* p = pixels + (static_cast<size_t>(y) * stride) +
                   (static_cast<size_t>(x) * 4);
      // WL_SHM_FORMAT_ARGB8888 is little-endian: B, G, R, A.
      p[0] = p[1] = p[2] = 0;
      p[3] = static_cast<uint8_t>(std::lround(alpha(x, y) * 255));
    }
  }
  munmap(data, size);

  wl_shm_pool* pool = wl_shm_create_pool(_glfw.wl.shm, fd, size);
  close(fd);
  wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                                WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  return buffer;
}

}  // namespace

struct WaylandFramePointerListener {
  static void Enter(void* data,
                    wl_pointer*,
                    uint32_t serial,
                    wl_surface* surface,
                    wl_fixed_t x,
                    wl_fixed_t y) {
    static_cast<WaylandFrame*>(data)->OnPointerEnter(
        serial, surface, wl_fixed_to_double(x), wl_fixed_to_double(y));
  }
  static void Leave(void* data, wl_pointer*, uint32_t, wl_surface*) {
    static_cast<WaylandFrame*>(data)->OnPointerLeave();
  }
  static void Motion(void* data,
                     wl_pointer*,
                     uint32_t,
                     wl_fixed_t x,
                     wl_fixed_t y) {
    static_cast<WaylandFrame*>(data)->OnPointerMotion(wl_fixed_to_double(x),
                                                      wl_fixed_to_double(y));
  }
  static void Button(void* data,
                     wl_pointer*,
                     uint32_t serial,
                     uint32_t,
                     uint32_t button,
                     uint32_t state) {
    static_cast<WaylandFrame*>(data)->OnPointerButton(serial, button, state);
  }
  static void Axis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
  static void Frame(void*, wl_pointer*) {}
  static void AxisSource(void*, wl_pointer*, uint32_t) {}
  static void AxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
  static void AxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
  static void AxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
  static void AxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}

  static constexpr wl_pointer_listener kListener = {
      Enter,
      Leave,
      Motion,
      Button,
      Axis,
      Frame,
      AxisSource,
      AxisStop,
      AxisDiscrete,
      AxisValue120,
      AxisRelativeDirection,
  };
};

WaylandFrame::WaylandFrame(GLFWwindow* window) : window_(window) {
  // GLFW asked for client-side decorations, it having been kept from drawing
  // them. Ask for server-side ones instead, and wait for the compositor's
  // answer so that the window's first frame is already drawn for it.
  _GLFWwindow* w = Internal(window_);
  if (w->wl.xdg.decoration) {
    zxdg_toplevel_decoration_v1_set_mode(
        w->wl.xdg.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    wl_display_roundtrip(_glfw.wl.display);
  }
  Update();
}

WaylandFrame::~WaylandFrame() {
  DestroyParts();
}

bool WaylandFrame::activated() const {
  return activated_;
}

bool WaylandFrame::Update() {
  _GLFWwindow* w = Internal(window_);
  if (!w->wl.xdg.toplevel)
    return false;

  bool changed = false;

  // With no decoration manager the compositor never decorates (GNOME).
  // Otherwise it says which side it wants, and may change its mind (a KDE
  // window rule, say).
  bool client_side = client_side_;
  if (!w->wl.xdg.decoration)
    client_side = true;
  else if (w->wl.xdg.decorationMode ==
           ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
    client_side = true;
  else if (w->wl.xdg.decorationMode ==
           ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE)
    client_side = false;
  if (client_side != client_side_) {
    client_side_ = client_side;
    changed = true;
  }

  // The title bar shows whether the window is active.
  if (w->wl.activated != activated_) {
    activated_ = w->wl.activated;
    changed = true;
  }

  // Leaving maximized or fullscreen, a compositor that never saw the window
  // at another size, as when it was maximized before its first frame, leaves
  // the size to the window (GNOME): it would stay as large. Go back to the
  // last size it had otherwise.
  bool maximized = w->wl.maximized || w->wl.fullscreen;
  if (was_maximized_ && !maximized && w->wl.width == width_ &&
      w->wl.height == height_ && restore_width_ > 0 &&
      (restore_width_ != width_ || restore_height_ != height_)) {
    glfwSetWindowSize(window_, restore_width_, restore_height_);
  }
  was_maximized_ = maximized;
  if (!maximized) {
    restore_width_ = w->wl.width;
    restore_height_ = w->wl.height;
  }

  // The window geometry is the content, in every mode: once set, it is no
  // longer derived from the surface, so it has to follow the size anyway.
  bool resized = w->wl.width != width_ || w->wl.height != height_;
  if (resized) {
    width_ = w->wl.width;
    height_ = w->wl.height;
    xdg_surface_set_window_geometry(w->wl.xdg.surface, 0, 0, width_, height_);
  }

  bool show = client_side_ && !maximized;
  if (show && !parts_created_ && !parts_failed_)
    parts_failed_ = !CreateParts();
  show = show && parts_created_;

  if (show) {
    CreatePointer();
    float scale = 1;
    glfwGetWindowContentScale(window_, &scale, nullptr);
    scale = std::max(scale, 1.0f);
    bool new_buffers = scale != buffer_scale_;
    if (new_buffers && !CreateBuffers(scale)) {
      // Keep what there is: no shadow, or the old one.
      new_buffers = false;
      if (!parts_[0].buffer) {
        if (shown_)
          Hide();
        shown_ = false;
        return changed;
      }
    }
    if (!shown_ || new_buffers || resized) {
      Layout(!shown_ || new_buffers);
      changed = true;
    }
    shown_ = true;
  } else if (shown_) {
    Hide();
    shown_ = false;
    changed = true;
  }
  return changed;
}

bool WaylandFrame::CreateParts() {
  if (!_glfw.wl.subcompositor || !_glfw.wl.viewporter || !_glfw.wl.shm) {
    LOG(0) << "Wayland compositor lacks wl_subcompositor, wp_viewporter or "
              "wl_shm: the window has no resize border.";
    return false;
  }

  _GLFWwindow* w = Internal(window_);
  for (int i = 0; i < kPartCount; ++i) {
    Surface& part = parts_[i];
    part.surface = wl_compositor_create_surface(_glfw.wl.compositor);
    part.subsurface = wl_subcompositor_get_subsurface(
        _glfw.wl.subcompositor, part.surface, w->wl.surface);
    wl_subsurface_place_below(part.subsurface, w->wl.surface);
    part.viewport =
        wp_viewporter_get_viewport(_glfw.wl.viewporter, part.surface);

    // Only the band next to the window takes input. The region is clipped to
    // the surface, so an edge's can run past it, whatever the window size.
    constexpr int kLong = 0x100000;
    constexpr int kOuter = kShadowSize - kResizeSize;
    int x = 0, y = 0, width = kResizeSize, height = kResizeSize;
    switch (i) {
      case kTop:
        y = kOuter;
        width = kLong;
        break;
      case kBottom:
        width = kLong;
        break;
      case kLeft:
        x = kOuter;
        height = kLong;
        break;
      case kRight:
        height = kLong;
        break;
      case kTopLeft:
        x = y = kOuter;
        break;
      case kTopRight:
        y = kOuter;
        break;
      case kBottomLeft:
        x = kOuter;
        break;
      default:  // kBottomRight
        break;
    }
    wl_region* region = wl_compositor_create_region(_glfw.wl.compositor);
    wl_region_add(region, x, y, width, height);
    wl_surface_set_input_region(part.surface, region);
    wl_region_destroy(region);
  }

  parts_created_ = true;
  return true;
}

void WaylandFrame::CreatePointer() {
  // Once GLFW has one, the seat has a pointer. Moving the window is left to
  // the window's own title bar, so a seat without one needs no frame input.
  if (pointer_ || !_glfw.wl.pointer)
    return;
  pointer_ = wl_seat_get_pointer(_glfw.wl.seat);
  wl_pointer_add_listener(pointer_, &WaylandFramePointerListener::kListener,
                          this);
  cursor_surface_ = wl_compositor_create_surface(_glfw.wl.compositor);
}

void WaylandFrame::DestroyParts() {
  if (pointer_) {
    if (wl_pointer_get_version(pointer_) >= WL_POINTER_RELEASE_SINCE_VERSION)
      wl_pointer_release(pointer_);
    else
      wl_pointer_destroy(pointer_);
    pointer_ = nullptr;
  }
  if (cursor_surface_) {
    wl_surface_destroy(cursor_surface_);
    cursor_surface_ = nullptr;
  }
  for (Surface& part : parts_) {
    if (part.viewport)
      wp_viewport_destroy(part.viewport);
    if (part.subsurface)
      wl_subsurface_destroy(part.subsurface);
    if (part.surface)
      wl_surface_destroy(part.surface);
    if (part.buffer)
      wl_buffer_destroy(part.buffer);
    part = {};
  }
  for (wl_buffer*& buffer : old_buffers_) {
    if (buffer)
      wl_buffer_destroy(buffer);
    buffer = nullptr;
  }
  parts_created_ = false;
}

bool WaylandFrame::CreateBuffers(float scale) {
  // Corners are square; edges one pixel long, stretched along the window.
  const int n = static_cast<int>(std::ceil(kShadowSize * scale));
  // Distance from the window of pixel |i| counted outward from it.
  auto away = [scale](int i) { return (i + 0.5f) / scale; };
  // ... and counted inward, toward it.
  auto toward = [scale, n](int i) { return (n - i - 0.5f) / scale; };

  std::array<wl_buffer*, kPartCount> buffers = {};
  buffers[kTop] = CreateShadowBuffer(
      1, n, [&](int, int y) { return ShadowAlpha(toward(y)); });
  buffers[kBottom] = CreateShadowBuffer(
      1, n, [&](int, int y) { return ShadowAlpha(away(y)); });
  buffers[kLeft] = CreateShadowBuffer(
      n, 1, [&](int x, int) { return ShadowAlpha(toward(x)); });
  buffers[kRight] = CreateShadowBuffer(
      n, 1, [&](int x, int) { return ShadowAlpha(away(x)); });
  buffers[kTopLeft] = CreateShadowBuffer(n, n, [&](int x, int y) {
    return ShadowAlpha(std::hypot(toward(x), toward(y)));
  });
  buffers[kTopRight] = CreateShadowBuffer(n, n, [&](int x, int y) {
    return ShadowAlpha(std::hypot(away(x), toward(y)));
  });
  buffers[kBottomLeft] = CreateShadowBuffer(n, n, [&](int x, int y) {
    return ShadowAlpha(std::hypot(toward(x), away(y)));
  });
  buffers[kBottomRight] = CreateShadowBuffer(n, n, [&](int x, int y) {
    return ShadowAlpha(std::hypot(away(x), away(y)));
  });

  if (std::find(buffers.begin(), buffers.end(), nullptr) != buffers.end()) {
    LOG(0) << "Failed to create the window shadow buffers.";
    for (wl_buffer* buffer : buffers) {
      if (buffer)
        wl_buffer_destroy(buffer);
    }
    return false;
  }

  // The replaced buffers stay shown until the window's next frame, the
  // subsurfaces being synchronized, and destroying a buffer still shown
  // leaves the surface's contents undefined. They go with the next change,
  // long after.
  for (int i = 0; i < kPartCount; ++i) {
    if (old_buffers_[i])
      wl_buffer_destroy(old_buffers_[i]);
    old_buffers_[i] = parts_[i].buffer;
    parts_[i].buffer = buffers[i];
  }
  buffer_scale_ = scale;
  return true;
}

void WaylandFrame::Layout(bool attach) {
  const int s = kShadowSize;
  const int w = width_;
  const int h = height_;
  struct Rect {
    int x, y, width, height;
  };
  const Rect rects[kPartCount] = {
      {0, -s, w, s},   // kTop
      {0, h, w, s},    // kBottom
      {-s, 0, s, h},   // kLeft
      {w, 0, s, h},    // kRight
      {-s, -s, s, s},  // kTopLeft
      {w, -s, s, s},   // kTopRight
      {-s, h, s, s},   // kBottomLeft
      {w, h, s, s},    // kBottomRight
  };
  for (int i = 0; i < kPartCount; ++i) {
    Surface& part = parts_[i];
    const Rect& r = rects[i];
    // Position is the parent's state; it applies with the parent's commit, as
    // does the rest, the subsurfaces being synchronized.
    wl_subsurface_set_position(part.subsurface, r.x, r.y);
    wp_viewport_set_destination(part.viewport, r.width, r.height);
    if (attach) {
      wl_surface_attach(part.surface, part.buffer, 0, 0);
      wl_surface_damage(part.surface, 0, 0, INT32_MAX, INT32_MAX);
    }
    wl_surface_commit(part.surface);
  }
}

void WaylandFrame::Hide() {
  for (Surface& part : parts_) {
    wl_surface_attach(part.surface, nullptr, 0, 0);
    wl_surface_commit(part.surface);
  }
  hovered_part_ = kPartCount;
}

uint32_t WaylandFrame::EdgeAt(int part, double x, double y) const {
  // |x| and |y| are on the part's surface. The edges line up with the window
  // along their length.
  switch (part) {
    case kTop:
      if (x < kCornerSize)
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
      if (x >= width_ - kCornerSize)
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    case kBottom:
      if (x < kCornerSize)
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
      if (x >= width_ - kCornerSize)
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    case kLeft:
      if (y < kCornerSize)
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
      if (y >= height_ - kCornerSize)
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
      return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    case kRight:
      if (y < kCornerSize)
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
      if (y >= height_ - kCornerSize)
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
      return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    case kTopLeft:
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    case kTopRight:
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    case kBottomLeft:
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    case kBottomRight:
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    default:
      return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  }
}

void WaylandFrame::SetCursor(uint32_t edge) {
  if (edge == cursor_edge_ || !pointer_ || !_glfw.wl.cursorTheme)
    return;

  // CSS cursor names, then the X core cursor font's.
  const char* names[2] = {};
  switch (edge) {
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
      names[0] = "n-resize";
      names[1] = "top_side";
      break;
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
      names[0] = "s-resize";
      names[1] = "bottom_side";
      break;
    case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
      names[0] = "w-resize";
      names[1] = "left_side";
      break;
    case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
      names[0] = "e-resize";
      names[1] = "right_side";
      break;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
      names[0] = "nw-resize";
      names[1] = "top_left_corner";
      break;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
      names[0] = "ne-resize";
      names[1] = "top_right_corner";
      break;
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
      names[0] = "sw-resize";
      names[1] = "bottom_left_corner";
      break;
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
      names[0] = "se-resize";
      names[1] = "bottom_right_corner";
      break;
    default:
      names[0] = "default";
      names[1] = "left_ptr";
      break;
  }

  // As GLFW does: the double size theme above scale 1, when there is one.
  wl_cursor_theme* theme = _glfw.wl.cursorTheme;
  int scale = 1;
  if (buffer_scale_ > 1 && _glfw.wl.cursorThemeHiDPI) {
    theme = _glfw.wl.cursorThemeHiDPI;
    scale = 2;
  }
  wl_cursor* cursor = nullptr;
  for (const char* name : names) {
    cursor = wl_cursor_theme_get_cursor(theme, name);
    if (cursor)
      break;
  }
  if (!cursor || !cursor->image_count)
    return;
  wl_cursor_image* image = cursor->images[0];
  wl_buffer* buffer = wl_cursor_image_get_buffer(image);
  if (!buffer)
    return;

  wl_pointer_set_cursor(pointer_, enter_serial_, cursor_surface_,
                        image->hotspot_x / scale, image->hotspot_y / scale);
  wl_surface_set_buffer_scale(cursor_surface_, scale);
  wl_surface_attach(cursor_surface_, buffer, 0, 0);
  wl_surface_damage(cursor_surface_, 0, 0, image->width, image->height);
  wl_surface_commit(cursor_surface_);
  cursor_edge_ = edge;
}

void WaylandFrame::OnPointerEnter(uint32_t serial,
                                  wl_surface* surface,
                                  double x,
                                  double y) {
  hovered_part_ = kPartCount;
  for (int i = 0; i < kPartCount; ++i) {
    if (surface && parts_[i].surface == surface)
      hovered_part_ = i;
  }
  if (hovered_part_ == kPartCount)
    return;
  enter_serial_ = serial;
  // The cursor was set over another surface, whatever we last set.
  cursor_edge_ = 0;
  OnPointerMotion(x, y);
}

void WaylandFrame::OnPointerLeave() {
  hovered_part_ = kPartCount;
}

void WaylandFrame::OnPointerMotion(double x, double y) {
  if (hovered_part_ == kPartCount)
    return;
  pointer_x_ = x;
  pointer_y_ = y;
  SetCursor(EdgeAt(hovered_part_, x, y));
}

void WaylandFrame::OnPointerButton(uint32_t serial,
                                   uint32_t button,
                                   uint32_t state) {
  if (hovered_part_ == kPartCount || button != BTN_LEFT ||
      state != WL_POINTER_BUTTON_STATE_PRESSED) {
    return;
  }
  _GLFWwindow* w = Internal(window_);
  if (!w->wl.xdg.toplevel || !w->resizable)
    return;
  xdg_toplevel_resize(w->wl.xdg.toplevel, _glfw.wl.seat, serial,
                      EdgeAt(hovered_part_, pointer_x_, pointer_y_));
}

}  // namespace eng
