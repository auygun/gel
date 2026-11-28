#ifndef ENGINE_PLATFORM_WAYLAND_FRAME_H
#define ENGINE_PLATFORM_WAYLAND_FRAME_H

#include <array>
#include <cstdint>

struct GLFWwindow;
struct wl_buffer;
struct wl_pointer;
struct wl_subsurface;
struct wl_surface;
struct wp_viewport;

namespace eng {

// The window's frame on Wayland.
//
// Wayland leaves decorations to the client unless the compositor offers to
// draw them through xdg-decoration (KDE, Sway, ...), so they are asked for
// first. Where they are not offered (GNOME) or refused, GLFW would decorate
// through libdecor, whose GTK plugin allocates and redraws a drop shadow
// buffer the size of the whole window on every resize step, on the main
// thread. GLFW is kept from decorating instead, and the window draws its own
// title bar (see Platform::UsesClientDecorations()), leaving this class the
// rest of the frame: a drop shadow around the window, the inner part of which
// resizes it.
//
// The shadow is eight subsurfaces, four corners and four edges, each showing a
// buffer only as large as the shadow is thick. The buffers are drawn once per
// scale, and the compositor stretches them along the window (wp_viewporter),
// so a resize step only moves and stretches the subsurfaces. Being
// synchronized subsurfaces, they change with the window's next frame.
//
// It also keeps the xdg window geometry to the window's content, so the
// compositor leaves the shadow out when it sizes, places, snaps or maximizes
// the window, and restores the window's size when it is unmaximized, for
// compositors that leave that to the window.
class WaylandFrame {
 public:
  // Asks the compositor to decorate |window|, which GLFW created undecorated.
  explicit WaylandFrame(GLFWwindow* window);
  ~WaylandFrame();

  WaylandFrame(const WaylandFrame&) = delete;
  WaylandFrame& operator=(const WaylandFrame&) = delete;

  // Whether the window has to draw its own title bar. The compositor can
  // change its mind at any time; Update() picks the change up.
  bool client_side() const { return client_side_; }

  // Whether the compositor shows the window as the active one, which title
  // bars follow. Unlike keyboard focus, it holds on a seat with no keyboard.
  bool activated() const;

  // Brings the frame up to date with the window: its size, its state, its
  // scale and the decoration mode. Call once events have been processed.
  // Returns whether anything changed that takes the window's next frame to
  // show.
  bool Update();

 private:
  friend struct WaylandFramePointerListener;

  enum Part {
    kTop,
    kBottom,
    kLeft,
    kRight,
    kTopLeft,
    kTopRight,
    kBottomLeft,
    kBottomRight,
    kPartCount
  };

  struct Surface {
    wl_surface* surface = nullptr;
    wl_subsurface* subsurface = nullptr;
    wp_viewport* viewport = nullptr;
    wl_buffer* buffer = nullptr;
  };

  GLFWwindow* window_;
  bool client_side_ = false;
  bool activated_ = false;

  std::array<Surface, kPartCount> parts_;
  // Buffers replaced on a scale change, which the compositor may still be
  // showing until the window's next frame. Destroyed with the next change.
  std::array<wl_buffer*, kPartCount> old_buffers_ = {};
  bool parts_created_ = false;
  bool parts_failed_ = false;
  bool shown_ = false;
  // Scale the buffers were drawn at.
  float buffer_scale_ = 0;
  // Last window geometry set, in surface coordinates.
  int width_ = 0;
  int height_ = 0;
  // Last size the window had when neither maximized nor fullscreen.
  bool was_maximized_ = false;
  int restore_width_ = 0;
  int restore_height_ = 0;

  // Our own pointer on GLFW's seat, which GLFW ignores the shadow's surfaces
  // on.
  wl_pointer* pointer_ = nullptr;
  wl_surface* cursor_surface_ = nullptr;
  uint32_t enter_serial_ = 0;
  // Part under the pointer, or kPartCount.
  int hovered_part_ = kPartCount;
  double pointer_x_ = 0;
  double pointer_y_ = 0;
  // xdg_toplevel resize edge the cursor shows, 0 for none.
  uint32_t cursor_edge_ = 0;

  bool CreateParts();
  void CreatePointer();
  void DestroyParts();
  bool CreateBuffers(float scale);
  // Positions and sizes the parts around the window, attaching their buffers
  // when |attach|.
  void Layout(bool attach);
  void Hide();

  // xdg_toplevel resize edge for the pointer at |x|, |y| on |part|.
  uint32_t EdgeAt(int part, double x, double y) const;
  void SetCursor(uint32_t edge);

  void OnPointerEnter(uint32_t serial, wl_surface* surface, double x, double y);
  void OnPointerLeave();
  void OnPointerMotion(double x, double y);
  void OnPointerButton(uint32_t serial, uint32_t button, uint32_t state);
};

}  // namespace eng

#endif  // ENGINE_PLATFORM_WAYLAND_FRAME_H
