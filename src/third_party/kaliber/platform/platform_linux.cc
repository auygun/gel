#include "third_party/kaliber/platform/platform.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

#include <dlfcn.h>
#include <errno.h>
#include <fontconfig/fontconfig.h>
#include <unistd.h>

#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/platform/wayland_selection.h"
#define _GLFW_X11
#define _GLFW_WAYLAND
extern "C" {
#include "internal.h"
}
#include "third_party/glfw/glfw/include/GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <X11/Xresource.h>
#include "third_party/glfw/glfw/include/GLFW/glfw3native.h"
#include "xdg-shell-client-protocol.h"

namespace eng {

namespace {

// Rounds to 1/64th as Chromium does, so UI code can losslessly multiply and
// divide by the scale.
float RoundScale(float scale) {
  return std::round(scale * 64.0f) / 64.0f;
}

size_t Pad4(size_t size) {
  return (size + 3) & ~size_t{3};
}

// Reads an integer setting published by the desktop's XSETTINGS manager: the
// same data GTK and Qt read, held in the _XSETTINGS_SETTINGS property of the
// window owning the _XSETTINGS_S<screen> selection.  The blob is a header
// followed by a packed list of name/type/value records; see
// https://specifications.freedesktop.org/xsettings-spec/
bool GetXSettingsInt(Display* display, const char* name, int* value) {
  char selection[32];
  snprintf(selection, sizeof(selection), "_XSETTINGS_S%d",
           DefaultScreen(display));
  Window owner =
      XGetSelectionOwner(display, XInternAtom(display, selection, False));
  if (!owner)
    return false;

  Atom settings = XInternAtom(display, "_XSETTINGS_SETTINGS", False);
  Atom type = None;
  int format = 0;
  unsigned long size = 0;
  unsigned long bytes_after = 0;
  unsigned char* data = nullptr;
  // Length is in 32-bit units; the blob is a few KB at most.
  constexpr long kMaxLength = 16L * 1024;
  if (XGetWindowProperty(display, owner, settings, 0, kMaxLength, False,
                         settings, &type, &format, &size, &bytes_after,
                         &data) != Success) {
    return false;
  }
  if (!data)
    return false;

  const unsigned char* pos = data;
  const unsigned char* end = data + size;
  bool found = false;

  // Header: byte order, 3 bytes of padding, serial, setting count.
  constexpr size_t kHeaderSize = 12;
  if (size >= kHeaderSize) {
    const bool big_endian = pos[0] == MSBFirst;
    auto read16 = [big_endian](const unsigned char* p) -> uint32_t {
      return big_endian ? (uint32_t{p[0]} << 8 | p[1])
                        : (uint32_t{p[1]} << 8 | p[0]);
    };
    auto read32 = [big_endian](const unsigned char* p) -> uint32_t {
      return big_endian ? (uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 |
                           uint32_t{p[2]} << 8 | p[3])
                        : (uint32_t{p[3]} << 24 | uint32_t{p[2]} << 16 |
                           uint32_t{p[1]} << 8 | p[0]);
    };

    const uint32_t count = read32(pos + 8);
    const size_t name_size = strlen(name);
    pos += kHeaderSize;

    for (uint32_t i = 0; i < count && !found; ++i) {
      // Record: type, padding, name length, padded name, change serial.
      if (static_cast<size_t>(end - pos) < 4)
        break;
      const unsigned char setting_type = pos[0];
      const size_t setting_name_size = read16(pos + 2);
      const size_t header_size = 4 + Pad4(setting_name_size) + 4;
      if (static_cast<size_t>(end - pos) < header_size)
        break;
      const char* setting_name = reinterpret_cast<const char*>(pos) + 4;
      pos += header_size;

      if (setting_type == 0) {  // Integer.
        if (static_cast<size_t>(end - pos) < 4)
          break;
        if (setting_name_size == name_size &&
            !memcmp(setting_name, name, name_size)) {
          *value = static_cast<int>(read32(pos));
          found = true;
        }
        pos += 4;
      } else if (setting_type == 1) {  // String.
        if (static_cast<size_t>(end - pos) < 4)
          break;
        const size_t length = read32(pos);
        if (static_cast<size_t>(end - pos) - 4 < Pad4(length))
          break;
        pos += 4 + Pad4(length);
      } else if (setting_type == 2) {  // Color, four 16-bit components.
        if (static_cast<size_t>(end - pos) < 8)
          break;
        pos += 8;
      } else {
        break;  // Unknown type; the remaining records cannot be located.
      }
    }
  }

  XFree(data);
  return found;
}

// No settings manager is running: the Xft.dpi X resource carries the same DPI.
// GLFW parses this resource too, but glfwGetWindowContentScale() discards the
// value unless XrmGetResource reports a "String" representation, which it does
// not always do, so read it here instead.
float GetXftDpiResource(Display* display) {
  const char* resources = XResourceManagerString(display);
  if (!resources)
    return 0.0f;
  XrmDatabase database = XrmGetStringDatabase(resources);
  if (!database)
    return 0.0f;
  float dpi = 0.0f;
  XrmValue value = {};
  char* type = nullptr;
  if (XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type, &value) &&
      value.addr) {
    dpi = static_cast<float>(atof(value.addr));
  }
  XrmDestroyDatabase(database);
  return dpi;
}

// On X11 the scale comes from the X server, which we are already connected to,
// so no toolkit has to be loaded.  The desktop's XSETTINGS manager publishes
// Xft/DPI (the DPI * 1024); with no settings manager running, the Xft.dpi X
// resource carries the same number, and GLFW has already parsed that one into
// the window content scale.  GDK_SCALE multiplies on top, as it does for GTK.
//
// GDK splits this DPI between an integer monitor scale and a residual font
// scale -- normalizing one against the other -- and then multiplies them back
// together.  Only the product reaches the UI, so the split is skipped here.
// Checked against GDK with and without a settings manager, for GDK_SCALE
// unset, 2 and 3.
float DetectScaleFactorX11() {
  Display* display = glfwGetX11Display();
  if (!display)
    return 1.0f;

  float scale = 0.0f;
  int xft_dpi = 0;
  if (GetXSettingsInt(display, "Xft/DPI", &xft_dpi) && xft_dpi > 0)
    scale = static_cast<float>(xft_dpi / 1024.0 / 96.0);
  else
    scale = GetXftDpiResource(display) / 96.0f;
  if (!(scale > 0.0f))
    scale = 1.0f;
  scale = RoundScale(scale);

  // An explicit GDK_SCALE applies on top: GDK stops normalizing the DPI
  // against the monitor scale once the scale is pinned this way.
  if (const char* gdk_scale = std::getenv("GDK_SCALE")) {
    int fixed_scale = atoi(gdk_scale);
    if (fixed_scale > 1)
      scale *= static_cast<float>(fixed_scale);
  }

  DLOG(0) << "X11 scale factor: " << scale;
  return scale;
}

// Wayland has no XSETTINGS equivalent: the text scale lives behind the desktop
// settings portal, which GDK knows how to reach.  The display scale is not
// needed here (the compositor already scales the framebuffer), so this is the
// only value a toolkit still has to be loaded for.
//
// Load libgdk rather than libgtk: it exposes the same setting for roughly half
// the load time and memory, and gdk_init_check() does not call
// setlocale(LC_ALL, "") the way gtk_init_check() does -- that would move the
// process off the "C" locale and change how the rest of the code formats and
// parses numbers.  GTK4 has no standalone libgdk, so fall back to the full
// toolkit there and disable its setlocale() call explicitly.
float DetectTextScaleGdk() {
  void* gdk = dlopen("libgdk-3.so.0", RTLD_LAZY);
  bool is_gtk4 = false;
  if (!gdk) {
    gdk = dlopen("libgtk-4.so.1", RTLD_LAZY);
    is_gtk4 = true;
  }
  if (!gdk)
    return 1.0f;

  bool inited = false;
  if (is_gtk4) {
    auto disable_setlocale =
        reinterpret_cast<void (*)()>(dlsym(gdk, "gtk_disable_setlocale"));
    if (disable_setlocale)
      disable_setlocale();
    auto init_check = reinterpret_cast<int (*)()>(dlsym(gdk, "gtk_init_check"));
    if (init_check)
      inited = init_check();
  } else {
    auto init_check =
        reinterpret_cast<int (*)(int*, char***)>(dlsym(gdk, "gdk_init_check"));
    if (init_check)
      inited = init_check(nullptr, nullptr);
  }
  // GDK registers atexit handlers; do not dlclose.
  if (!inited)
    return 1.0f;

  // The value is DPI * 1024; dividing by 1024 and then by 96 gives the text
  // scale, as Chromium does (ui/gtk/gtk_ui.cc GetXftDpi/FontScale).
  int xft_dpi = -1;
  if (is_gtk4) {
    using GtkSettingsGetDefaultFn = void* (*)();
    using GObjectGetFn = void (*)(void*, const char*, ...);

    auto settings_get_default = reinterpret_cast<GtkSettingsGetDefaultFn>(
        dlsym(gdk, "gtk_settings_get_default"));
    auto g_object_get =
        reinterpret_cast<GObjectGetFn>(dlsym(gdk, "g_object_get"));

    if (settings_get_default && g_object_get) {
      void* settings = settings_get_default();
      if (settings)
        g_object_get(settings, "gtk-xft-dpi", &xft_dpi, nullptr);
    }
  } else {
    // GdkScreen exposes the same setting without initializing GTK.  glib's
    // GValue layout is a frozen ABI, so it can be mirrored here instead of
    // pulling in the glib headers.
    struct GValue {
      size_t g_type;
      union {
        double v_double;
        void* v_pointer;
      } data[2];
    };
    constexpr size_t kGTypeInt = 6 << 2;  // G_TYPE_INT

    using GdkScreenGetDefaultFn = void* (*)();
    using GdkScreenGetSettingFn = int (*)(void*, const char*, void*);
    using GValueInitFn = void* (*)(void*, size_t);
    using GValueGetIntFn = int (*)(const void*);

    auto screen_get_default = reinterpret_cast<GdkScreenGetDefaultFn>(
        dlsym(gdk, "gdk_screen_get_default"));
    auto screen_get_setting = reinterpret_cast<GdkScreenGetSettingFn>(
        dlsym(gdk, "gdk_screen_get_setting"));
    auto g_value_init =
        reinterpret_cast<GValueInitFn>(dlsym(gdk, "g_value_init"));
    auto g_value_get_int =
        reinterpret_cast<GValueGetIntFn>(dlsym(gdk, "g_value_get_int"));

    if (screen_get_default && screen_get_setting && g_value_init &&
        g_value_get_int) {
      void* screen = screen_get_default();
      if (screen) {
        GValue value = {};
        g_value_init(&value, kGTypeInt);
        if (screen_get_setting(screen, "gtk-xft-dpi", &value))
          xft_dpi = g_value_get_int(&value);
      }
    }
  }

  if (xft_dpi <= 0)
    return 1.0f;
  float text_scale = RoundScale(static_cast<float>(xft_dpi / 1024.0 / 96.0));
  DLOG(0) << "gtk-xft-dpi text scale: " << text_scale;
  return text_scale;
}

}  // namespace

std::filesystem::path Platform::GetSettingsPath() {
  namespace fs = std::filesystem;
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0')
    return fs::path(xdg) / "gel" / "settings.json";
  const char* home = std::getenv("HOME");
  if (home)
    return fs::path(home) / ".config" / "gel" / "settings.json";
  return {};
}

void Platform::PlatformInit() {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
    InitWaylandSelection();
}

void Platform::PlatformShutdown() {
  ShutdownWaylandSelection();
}

float Platform::PlatformDetectScaleFactor() {
  // On Wayland, GLFW's GLFW_SCALE_FRAMEBUFFER (default true) already scales the
  // framebuffer by the compositor's content scale, so ImGui picks up the
  // display DPI via DisplayFramebufferScale (= framebuffer_size / window_size).
  // We must not include the display scale here or fonts and sizes will be
  // doubled; only the text scale is ours to apply.
  //
  // On X11, the framebuffer is always 1:1 with the window, so the full display
  // scale must be applied manually.
  float scale = glfwGetPlatform() == GLFW_PLATFORM_X11 ? DetectScaleFactorX11()
                                                       : DetectTextScaleGdk();
  DLOG(0) << "Final scale factor: " << scale;
  return scale;
}

bool Platform::PlatformUpdate(double wait_timeout) {
  return false;
}

void Platform::BeginInteractiveMove() {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    auto* wl = GetWaylandSelection();
    if (!wl || !wl->seat)
      return;
    auto* toplevel = ((_GLFWwindow*)window_)->wl.xdg.toplevel;
    if (toplevel)
      xdg_toplevel_move(toplevel, wl->seat, wl->serial);
    mouse_buttons_down_[static_cast<int>(MouseButton::Left)] = false;
    return;
  }

  if (glfwGetPlatform() != GLFW_PLATFORM_X11)
    return;
  Display* display = glfwGetX11Display();
  Window xwindow = glfwGetX11Window(window_);

  // Release any grabbed pointer so the WM can take over.
  XUngrabPointer(display, CurrentTime);
  XFlush(display);

  // _NET_WM_MOVERESIZE direction for move.
  constexpr long kMoveResize_MOVE = 8;

  Atom wm_moveresize = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
  Window root = DefaultRootWindow(display);

  // Get absolute mouse position.
  Window child_ret;
  int root_x, root_y, win_x, win_y;
  unsigned int mask;
  XQueryPointer(display, xwindow, &root, &child_ret, &root_x, &root_y, &win_x,
                &win_y, &mask);

  XEvent ev = {};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = xwindow;
  ev.xclient.message_type = wm_moveresize;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = root_x;
  ev.xclient.data.l[1] = root_y;
  ev.xclient.data.l[2] = kMoveResize_MOVE;
  ev.xclient.data.l[3] = Button1;
  ev.xclient.data.l[4] = 1;  // source indication: normal application

  XSendEvent(display, root, False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(display);
  mouse_buttons_down_[static_cast<int>(MouseButton::Left)] = false;
}

void Platform::ShowWindowMenu(int x, int y) {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    auto* wl = GetWaylandSelection();
    if (!wl || !wl->seat)
      return;
    auto* toplevel = ((_GLFWwindow*)window_)->wl.xdg.toplevel;
    if (toplevel)
      xdg_toplevel_show_window_menu(toplevel, wl->seat, wl->serial, x, y);
    return;
  }

  if (glfwGetPlatform() != GLFW_PLATFORM_X11)
    return;
  Display* display = glfwGetX11Display();
  Window xwindow = glfwGetX11Window(window_);
  Window root = DefaultRootWindow(display);

  // Convert window-relative coordinates to root coordinates.
  Window child_ret;
  int root_x, root_y;
  XTranslateCoordinates(display, xwindow, root, x, y, &root_x, &root_y,
                        &child_ret);

  Atom show_menu = XInternAtom(display, "_GTK_SHOW_WINDOW_MENU", False);
  XEvent ev = {};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = xwindow;
  ev.xclient.message_type = show_menu;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 0;  // device id (pointer)
  ev.xclient.data.l[1] = root_x;
  ev.xclient.data.l[2] = root_y;

  XSendEvent(display, root, False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(display);
}

Platform::CSDButtonVisibility Platform::GetCSDButtonVisibility() const {
  CSDButtonVisibility vis;
  // Only query GNOME settings when running on a GNOME-based desktop.
  // XDG_CURRENT_DESKTOP can contain multiple colon-separated values
  // (e.g. "ubuntu:GNOME", "Budgie:GNOME").
  const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
  if (!desktop || !strstr(desktop, "GNOME"))
    return vis;
  FILE* pipe = popen(
      "gsettings get org.gnome.desktop.wm.preferences button-layout "
      "2>/dev/null",
      "r");
  if (pipe) {
    char buf[256] = {};
    [[maybe_unused]] auto* r = fgets(buf, sizeof(buf), pipe);
    int status = pclose(pipe);
    if (status == 0) {
      vis.minimize = strstr(buf, "minimize") != nullptr;
      vis.maximize = strstr(buf, "maximize") != nullptr;
    }
  }
  return vis;
}

void Platform::BeginInteractiveResize(int edges) {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    auto* wl = GetWaylandSelection();
    if (!wl || !wl->seat)
      return;
    // Convert from _NET_WM_MOVERESIZE direction to xdg_toplevel_resize_edge.
    // The caller uses X11 direction values (0-7); map to Wayland edge bitmask.
    constexpr uint32_t kMap[] = {
        XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT,      // 0: _SIZE_TOPLEFT
        XDG_TOPLEVEL_RESIZE_EDGE_TOP,           // 1: _SIZE_TOP
        XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT,     // 2: _SIZE_TOPRIGHT
        XDG_TOPLEVEL_RESIZE_EDGE_RIGHT,         // 3: _SIZE_RIGHT
        XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT,  // 4: _SIZE_BOTTOMRIGHT
        XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM,        // 5: _SIZE_BOTTOM
        XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT,   // 6: _SIZE_BOTTOMLEFT
        XDG_TOPLEVEL_RESIZE_EDGE_LEFT,          // 7: _SIZE_LEFT
    };
    if (edges < 0 || edges > 7)
      return;
    auto* toplevel = ((_GLFWwindow*)window_)->wl.xdg.toplevel;
    if (toplevel)
      xdg_toplevel_resize(toplevel, wl->seat, wl->serial, kMap[edges]);
    mouse_buttons_down_[static_cast<int>(MouseButton::Left)] = false;
    return;
  }

  if (glfwGetPlatform() != GLFW_PLATFORM_X11)
    return;
  Display* display = glfwGetX11Display();
  Window xwindow = glfwGetX11Window(window_);

  XUngrabPointer(display, CurrentTime);
  XFlush(display);

  Atom wm_moveresize = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
  Window root = DefaultRootWindow(display);

  Window child_ret;
  int root_x, root_y, win_x, win_y;
  unsigned int mask;
  XQueryPointer(display, xwindow, &root, &child_ret, &root_x, &root_y, &win_x,
                &win_y, &mask);

  XEvent ev = {};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = xwindow;
  ev.xclient.message_type = wm_moveresize;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = root_x;
  ev.xclient.data.l[1] = root_y;
  ev.xclient.data.l[2] = edges;
  ev.xclient.data.l[3] = Button1;
  ev.xclient.data.l[4] = 1;

  XSendEvent(display, root, False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(display);
  mouse_buttons_down_[static_cast<int>(MouseButton::Left)] = false;
}

void Platform::SetWindowBackgroundColor(float r, float g, float b) {
  if (glfwGetPlatform() == GLFW_PLATFORM_X11) {
    Display* display = glfwGetX11Display();
    Window xwindow = glfwGetX11Window(window_);
    auto to8 = [](float v) -> unsigned long {
      return static_cast<unsigned long>(std::lroundf(v * 255.0f));
    };
    unsigned long pixel = to8(r) << 16 | to8(g) << 8 | to8(b);
    XSetWindowBackground(display, xwindow, pixel);
    XClearWindow(display, xwindow);
    XFlush(display);
  }
}

bool Platform::IsDarkMode() const {
  FILE* pipe = popen(
      "gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null",
      "r");
  if (pipe) {
    char buf[128] = {};
    [[maybe_unused]] auto* r = fgets(buf, sizeof(buf), pipe);
    int status = pclose(pipe);
    if (status == 0) {
      if (strstr(buf, "prefer-dark"))
        return true;
      return false;
    }
  }
  return false;
}

namespace {

// A font that cannot draw plain ASCII is useless as a UI font, whatever its
// spacing says. This is what keeps emoji, dingbat and icon fonts out of the
// list -- Noto Color Emoji, for one, is tagged FC_MONO.
bool CoversBasicLatin(FcPattern* font) {
  FcCharSet* charset = nullptr;
  if (FcPatternGetCharSet(font, FC_CHARSET, 0, &charset) != FcResultMatch)
    return false;
  return FcCharSetHasChar(charset, 'A') && FcCharSetHasChar(charset, 'z') &&
         FcCharSetHasChar(charset, '0');
}

// Fontconfig stores one family value per language. Value 0 is whatever the
// font lists first, which for many CJK and localized fonts is not the English
// name, so match FC_FAMILYLANG instead of trusting the order.
std::string GetEnglishFamilyName(FcPattern* font) {
  std::string fallback;
  for (int i = 0;; ++i) {
    FcChar8* family = nullptr;
    if (FcPatternGetString(font, FC_FAMILY, i, &family) != FcResultMatch)
      break;
    std::string name(reinterpret_cast<const char*>(family));
    if (fallback.empty())
      fallback = name;
    FcChar8* lang = nullptr;
    if (FcPatternGetString(font, FC_FAMILYLANG, i, &lang) == FcResultMatch &&
        strcmp(reinterpret_cast<const char*>(lang), "en") == 0)
      return name;
  }
  return fallback;
}

bool IsFixedPitch(FcPattern* font) {
  int spacing = 0;
  if (FcPatternGetInteger(font, FC_SPACING, 0, &spacing) != FcResultMatch)
    return false;
  // FC_DUAL covers CJK faces whose Latin glyphs are half-width, and
  // FC_CHARCELL is FC_MONO with a fixed cell height. Both are fixed pitch for
  // our purposes; testing FC_SPACING == FC_MONO alone drops them. Note that
  // some genuinely monospace fonts (Noto Sans Mono CJK, for one) carry no
  // FC_SPACING at all and are reported as proportional here.
  return spacing == FC_MONO || spacing == FC_CHARCELL || spacing == FC_DUAL;
}

bool IsScalableFormat(FcPattern* font) {
  FcChar8* format = nullptr;
  if (FcPatternGetString(font, FC_FONTFORMAT, 0, &format) != FcResultMatch)
    return true;
  const char* f = reinterpret_cast<const char*>(format);
  return strcmp(f, "TrueType") == 0 || strcmp(f, "CFF") == 0;
}

}  // namespace

std::vector<Platform::FontInfo> Platform::GetFonts() const {
  std::vector<FontInfo> result;
  FcConfig* config = FcInitLoadConfigAndFonts();
  FcPattern* pattern = FcPatternCreate();
  FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
  FcObjectSet* os =
      FcObjectSetBuild(FC_FAMILY, FC_FAMILYLANG, FC_FILE, FC_INDEX, FC_WEIGHT,
                       FC_SPACING, FC_CHARSET, FC_FONTFORMAT, nullptr);
  FcFontSet* fs = FcFontList(config, pattern, os);
  if (fs) {
    // Collect one entry per family name, preferring weight closest to regular.
    struct Entry {
      std::string path;
      int index;
      int weight;
      bool is_monospace;
    };
    std::map<std::string, Entry> families;
    for (int i = 0; i < fs->nfont; i++) {
      FcPattern* font = fs->fonts[i];
      if (!IsScalableFormat(font) || !CoversBasicLatin(font))
        continue;

      FcChar8* file = nullptr;
      if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch)
        continue;
      std::string name = GetEnglishFamilyName(font);
      if (name.empty())
        continue;

      int weight = FC_WEIGHT_REGULAR;
      FcPatternGetInteger(font, FC_WEIGHT, 0, &weight);
      // FC_INDEX already uses FreeType's packing, which is what FontInfo
      // carries, so it is stored whole rather than masked.
      int index = 0;
      FcPatternGetInteger(font, FC_INDEX, 0, &index);

      int dist = std::abs(weight - FC_WEIGHT_REGULAR);
      auto it = families.find(name);
      if (it == families.end() ||
          dist < std::abs(it->second.weight - FC_WEIGHT_REGULAR)) {
        families[name] = {reinterpret_cast<const char*>(file), index, weight,
                          IsFixedPitch(font)};
      }
    }
    for (auto& [name, entry] : families)
      result.push_back({name, entry.path, entry.index, entry.is_monospace});
    FcFontSetDestroy(fs);
  }
  FcObjectSetDestroy(os);
  FcPatternDestroy(pattern);
  FcConfigDestroy(config);
  return result;
}

// Matches a font face that covers the given codepoint via fontconfig. Returns
// an empty path when nothing covers it.
static Platform::FontInfo MatchFontForCodepoint(FcConfig* config,
                                                unsigned int codepoint) {
  Platform::FontInfo result;
  FcPattern* pattern = FcPatternCreate();
  FcCharSet* charset = FcCharSetCreate();
  FcCharSetAddChar(charset, codepoint);
  FcPatternAddCharSet(pattern, FC_CHARSET, charset);
  FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
  FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR);
  FcConfigSubstitute(config, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult fc_result;
  FcPattern* match = FcFontMatch(config, pattern, &fc_result);
  if (match) {
    // FcFontMatch always returns something, so verify the match actually
    // covers the codepoint before merging an otherwise irrelevant font.
    FcCharSet* match_charset = nullptr;
    FcChar8* file = nullptr;
    if (FcPatternGetCharSet(match, FC_CHARSET, 0, &match_charset) ==
            FcResultMatch &&
        FcCharSetHasChar(match_charset, codepoint) &&
        FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
      result.path = reinterpret_cast<const char*>(file);
      // Without FC_INDEX every face of a collection collapses to face 0, so a
      // CJK fallback matched out of a .ttc would merge the wrong script.
      FcPatternGetInteger(match, FC_INDEX, 0, &result.face_index);
    }
    FcPatternDestroy(match);
  }
  FcCharSetDestroy(charset);
  FcPatternDestroy(pattern);
  return result;
}

std::vector<Platform::FontInfo> Platform::GetFallbackFonts() const {
  std::vector<FontInfo> result;
  FcConfig* config = FcInitLoadConfigAndFonts();
  for (unsigned int codepoint : kFallbackFontProbes) {
    FontInfo font = MatchFontForCodepoint(config, codepoint);
    if (font.path.empty())
      continue;
    auto same = [&](const FontInfo& f) {
      return f.path == font.path && f.face_index == font.face_index;
    };
    if (std::find_if(result.begin(), result.end(), same) == result.end())
      result.push_back(std::move(font));
  }
  FcConfigDestroy(config);
  return result;
}

void Platform::SetClipboardText(const char* text) {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    auto* wl = GetWaylandSelection();
    if (wl && wl->cb_device) {
      if (wl->cb_source) {
        wl_data_source_destroy(wl->cb_source);
        wl->cb_source = nullptr;
      }
      // Destroy any incoming offer since we now own the clipboard.
      if (wl->cb_offer) {
        wl_data_offer_destroy(wl->cb_offer);
        wl->cb_offer = nullptr;
      }
      wl->cb_read_valid = false;
      wl->cb_text = text;
      wl->cb_source = wl_data_device_manager_create_data_source(wl->cb_manager);
      wl_data_source_add_listener(wl->cb_source, &kCbSourceListener, wl);
      wl_data_source_offer(wl->cb_source, "text/plain;charset=utf-8");
      wl_data_source_offer(wl->cb_source, "text/plain");
      wl_data_source_offer(wl->cb_source, "UTF8_STRING");
      wl_data_device_set_selection(wl->cb_device, wl->cb_source, wl->serial);
      wl_display_flush(wl->display);
      return;
    }
  }
  // GLFW's X11 clipboard uses XChangeProperty without the INCR protocol,
  // which causes a BadLength X protocol error when the text exceeds the
  // maximum request size.  For large text, pipe through xclip instead.
  size_t len = strlen(text);
  if (glfwGetPlatform() == GLFW_PLATFORM_X11 && len > 200000) {
    FILE* proc = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (!proc)
      proc = popen("xsel --clipboard --input 2>/dev/null", "w");
    if (proc) {
      // Ignore SIGPIPE in case the child exits early (e.g. command not found).
      struct sigaction sa = {}, old_sa;
      sa.sa_handler = SIG_IGN;
      sigaction(SIGPIPE, &sa, &old_sa);
      fwrite(text, 1, len, proc);
      pclose(proc);
      sigaction(SIGPIPE, &old_sa, nullptr);
      return;
    }
  }

  glfwSetClipboardString(window_, text);
}

const char* Platform::GetClipboardText() {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    auto* wl = GetWaylandSelection();
    if (wl && wl->cb_device) {
      // If we own the clipboard, return our stored text.
      if (wl->cb_source)
        return wl->cb_text.c_str();
      // If we have an external offer, read from it lazily (safe here because
      // we're not inside wl_display_dispatch, so the compositor can respond).
      if (wl->cb_offer && !wl->cb_read_valid) {
        int fds[2];
        if (pipe(fds) == 0) {
          wl_data_offer_receive(wl->cb_offer, "text/plain;charset=utf-8",
                                fds[1]);
          close(fds[1]);
          wl_display_flush(wl->display);
          // Roundtrip to ensure the compositor processes the receive request.
          wl_display_roundtrip(wl->display);
          wl->cb_read_cache.clear();
          char buf[4096];
          for (;;) {
            ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n > 0) {
              wl->cb_read_cache.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
              break;
            } else {
              if (errno == EINTR)
                continue;
              break;
            }
          }
          close(fds[0]);
          wl->cb_read_valid = true;
        }
      }
      if (wl->cb_read_valid)
        return wl->cb_read_cache.c_str();
      return "";
    }
  }
  return glfwGetClipboardString(window_);
}

const char* Platform::GetPrimarySelection() {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    auto* wl = GetWaylandSelection();
    if (wl && wl->ps_device) {
      // If we own the primary selection, return our stored text.
      if (wl->ps_source)
        return wl->ps_text.c_str();
      // If we have an external offer, read from it lazily.
      if (wl->ps_offer && !wl->ps_read_valid) {
        int fds[2];
        if (pipe(fds) == 0) {
          zwp_primary_selection_offer_v1_receive(
              wl->ps_offer, "text/plain;charset=utf-8", fds[1]);
          close(fds[1]);
          wl_display_flush(wl->display);
          wl_display_roundtrip(wl->display);
          wl->ps_read_cache.clear();
          char buf[4096];
          for (;;) {
            ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n > 0) {
              wl->ps_read_cache.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
              break;
            } else {
              if (errno == EINTR)
                continue;
              break;
            }
          }
          close(fds[0]);
          wl->ps_read_valid = true;
        }
      }
      if (wl->ps_read_valid)
        return wl->ps_read_cache.c_str();
      return "";
    }
  }
  return glfwGetX11SelectionString();
}

void Platform::SetPrimarySelection(const char* text) {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    auto* wl = GetWaylandSelection();
    if (wl && wl->ps_device) {
      if (wl->ps_source) {
        zwp_primary_selection_source_v1_destroy(wl->ps_source);
        wl->ps_source = nullptr;
      }
      wl->ps_text = text;
      wl->ps_source =
          zwp_primary_selection_device_manager_v1_create_source(wl->ps_manager);
      zwp_primary_selection_source_v1_add_listener(wl->ps_source,
                                                   &kPsSourceListener, wl);
      zwp_primary_selection_source_v1_offer(wl->ps_source,
                                            "text/plain;charset=utf-8");
      zwp_primary_selection_source_v1_offer(wl->ps_source, "text/plain");
      zwp_primary_selection_source_v1_offer(wl->ps_source, "UTF8_STRING");
      zwp_primary_selection_device_v1_set_selection(wl->ps_device,
                                                    wl->ps_source, wl->serial);
      wl_display_flush(wl->display);
    }
  } else {
    glfwSetX11SelectionString(text);
  }
}

void Platform::OpenURL(const char* url) {
  std::string cmd = "xdg-open '";
  cmd += url;
  cmd += "' >/dev/null 2>&1 &";
  [[maybe_unused]] int r = std::system(cmd.c_str());
}

}  // namespace eng
