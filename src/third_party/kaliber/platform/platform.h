#ifndef ENGINE_PLATFORM_PLATFORM_H
#define ENGINE_PLATFORM_PLATFORM_H

#include <array>
#include <atomic>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "third_party/kaliber/input_codes.h"

struct GLFWcursor;
struct GLFWwindow;

namespace eng {

class PlatformObserver;

// Codepoints probed to discover fallback fonts, one per script the default
// font (DejaVu Sans Mono) does not cover. Each probe is resolved to a font
// file separately and the results are merged into the ImGui font atlas, so a
// script is only rendered if some installed font covers its probe.
//
// Order matters: the first merged font that has a glyph wins, so a font
// resolved from an earlier probe takes precedence where coverage overlaps.
// CJK leads because CJK fonts also carry punctuation and kana; emoji trail so
// that dingbats in the BMP keep resolving to a monochrome symbol font rather
// than to color emoji. Hangul is probed separately from Han: the two are
// commonly in different fonts (Microsoft YaHei covers Han and kana but no
// Hangul), so one probe for both would leave Korean unrendered.
//
// Note that coverage is not the same as correct rendering: ImGui performs no
// text shaping, so scripts that need reordering, joining or mark stacking
// (Khmer, Devanagari, Arabic, Myanmar, ...) will show the right glyphs in the
// wrong arrangement. See the Dear ImGui README on internationalization.
inline constexpr unsigned int kFallbackFontProbes[] = {
    0x4E2D,   // Han and kana, 中
    0xAC00,   // Hangul, 가
    0x274C,   // Symbols and dingbats, ❌
    0x0627,   // Arabic, ا
    0x05D0,   // Hebrew, א
    0x0710,   // Syriac, ܐ
    0x0780,   // Thaana, ހ
    0x0561,   // Armenian, ա
    0x10D0,   // Georgian, ა
    0x0915,   // Devanagari, क
    0x0995,   // Bengali, ক
    0x0A15,   // Gurmukhi, ਕ
    0x0A95,   // Gujarati, ક
    0x0B15,   // Oriya, କ
    0x0B95,   // Tamil, க
    0x0C15,   // Telugu, క
    0x0C95,   // Kannada, ಕ
    0x0D15,   // Malayalam, ക
    0x0D9A,   // Sinhala, ක
    0x0E01,   // Thai, ก
    0x0E81,   // Lao, ກ
    0x0F40,   // Tibetan, ཀ
    0x1000,   // Myanmar, က
    0x1200,   // Ethiopic, ሀ
    0x13A0,   // Cherokee, Ꭰ
    0x1401,   // Unified Canadian Aboriginal Syllabics, ᐁ
    0x1780,   // Khmer, ក
    0x1820,   // Mongolian, ᠠ
    0x07C0,   // N'Ko, ߀
    0x2D30,   // Tifinagh, ⴰ
    0xA500,   // Vai, ꔀ
    0x1F4C4,  // Emoji, 📄
};

class Platform {
 public:
  static std::filesystem::path GetSettingsPath();

  float GetDeviceScaleFactor() const { return device_scale_factor_; }

  Platform();
  ~Platform();

  void SetMainArgs(int argc, char** argv) {
    argc_ = argc;
    argv_ = argv;
  }

  int GetMainArgC() const { return argc_; }
  char** GetMainArgV() const { return argv_; }

  void CreateMainWindow(bool use_opengl = true,
                        int width = -1,
                        int height = -1);

#if defined(OS_LINUX)
  // Set the display backend hint (GLFW platform) before the first
  // CreateMainWindow call.  0 = auto, GLFW_PLATFORM_WAYLAND or
  // GLFW_PLATFORM_X11 to force a specific backend.
  void SetDisplayBackendHint(int platform_hint);
#endif

  void Update(double wait_timeout = 0);

  void Exit();

  void SetObserver(PlatformObserver* observer) { observer_ = observer; }

  bool IsDarkMode() const;

  struct FontInfo {
    std::string name;
    std::string path;
    // Face within the file, in the packing ImFontConfig::FontNo and
    // FT_New_Memory_Face use: low 16 bits select the face of a 'ttcf'
    // collection, bits 16-30 a variable-font named instance.
    int face_index = 0;
    // Fixed advance width. Reported rather than filtered on: the UI measures
    // text instead of assuming a cell width, so a proportional font is a
    // usable choice, but monospace is the better default for reading diffs
    // and callers group the list accordingly.
    bool is_monospace = false;
  };
  // Every usable UI font on the system: scalable, upright, and covering basic
  // Latin, sorted by name. Grouping monospace ahead of the rest is left to
  // the caller, which is where the presentation choice belongs.
  std::vector<FontInfo> GetFonts() const;
  // Fonts to merge into the ImGui font atlas behind the chosen one, covering
  // scripts it lacks. Only path and face_index are meaningful; the name is
  // left empty since these are never shown. See kFallbackFontProbes.
  std::vector<FontInfo> GetFallbackFonts() const;

  bool should_exit() const { return should_exit_; }

  // Returns true if a user-initiated event (input, resize, focus, or worker
  // wake) was received since the last call. Clears the flag atomically.
  bool NewEventReceived() {
    return has_event_.exchange(false, std::memory_order_relaxed);
  }

  int GetMouseX() const { return mouse_x_; }
  int GetMouseY() const { return mouse_y_; }

  bool IsCursorInside() const { return cursor_inside_; }

  float GetMouseScrollYDelta() const { return mouse_scroll_y_delta_; }
  float GetMouseScrollXDelta() const { return mouse_scroll_x_delta_; }

  bool IsMouseButtonDown(MouseButton button) const {
    return mouse_buttons_down_[static_cast<int>(button)];
  }

  bool IsKeyDown(Key key) const { return keys_down_[static_cast<int>(key)]; }
  bool IsAnyKeyDown() const {
    for (auto k : keys_down_)
      if (k)
        return true;
    return false;
  }
  bool IsAnyMouseButtonDown() const {
    for (auto b : mouse_buttons_down_)
      if (b)
        return true;
    return false;
  }

  struct MouseButtonEvent {
    MouseButton button;
    bool pressed;
  };

  // Retrieve mouse button events queued this frame.
  const std::vector<MouseButtonEvent>& GetMouseButtonEvents() const {
    return mouse_button_events_;
  }

  // Retrieve the characters typed this frame.
  const std::vector<unsigned int>& GetInputCharacters() const {
    return input_characters_;
  }

  int GetWindowWidth() const;
  int GetWindowHeight() const;

  void GetWindowPosition(int& x, int& y) const;
  void SetWindowPosition(int x, int y);
  bool IsMaximized() const;
  void SetMaximized(bool maximized);
  void Minimize();

  // Client-side decorations (CSD). When decorated is false the window manager
  // title bar is removed and the application is responsible for drawing window
  // controls and handling move/resize.
  void SetDecorated(bool decorated);
  bool IsDecorated() const;
  void BeginInteractiveMove();
  void BeginInteractiveResize(int edges);
  void ShowWindowMenu(int x, int y);

  struct CSDButtonVisibility {
    bool minimize = true;
    bool maximize = true;
  };
  CSDButtonVisibility GetCSDButtonVisibility() const;

  // Set the window background color used by the window manager before any
  // rendering occurs.  On X11 this sets the X window background pixel.
  void SetWindowBackgroundColor(float r, float g, float b);

  void SetWindowTitle(const std::string& title);

  void SetWindowIcon(int width, int height, const unsigned char* rgba_pixels);

  void SetMouseCursor(int cursor);

  // Confine the cursor to the content area of the window while true.
  void SetCursorCaptured(bool captured);

  void SetClipboardText(const char* text);
  const char* GetClipboardText();

  void SetPrimarySelection(const char* text);
  const char* GetPrimarySelection();

  void OpenURL(const char* url);

  GLFWwindow* GetWindow();

 private:
  float device_scale_factor_ = 1.0f;
  bool decorated_ = true;
#if defined(OS_LINUX)
  int display_backend_hint_ = 0;
#endif

  bool has_focus_ = false;
  bool should_exit_ = false;
  bool cursor_captured_ = false;
  bool cursor_inside_ = false;
  std::atomic<bool> has_event_{false};

  int pending_width_ = 0;
  int pending_height_ = 0;
  bool has_pending_resize_ = false;

  PlatformObserver* observer_ = nullptr;

  GLFWwindow* window_ = nullptr;
  static constexpr int kCursorCount = 11;
  GLFWcursor* cursors_[kCursorCount] = {};

  // Input state tracking
  int mouse_x_{0};
  int mouse_y_{0};
  float mouse_scroll_y_delta_{0.0f};
  float mouse_scroll_x_delta_{0.0f};
  std::array<bool, static_cast<int>(MouseButton::MaxButtons)>
      mouse_buttons_down_{};
  std::array<bool, static_cast<int>(Key::MaxKeys)> keys_down_{};
  // Buffer to store mouse button events this frame
  std::vector<MouseButtonEvent> mouse_button_events_;
  // Buffer to store UTF-32 characters typed this frame
  std::vector<unsigned int> input_characters_;

  void PlatformInit();
  void PlatformShutdown();
  bool PlatformUpdate(double wait_timeout);
  float PlatformDetectScaleFactor();

  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

 private:
  int argc_ = 0;
  char** argv_ = nullptr;
};

#if defined(OS_LINUX)
// Install a .desktop file and PNG icon to the user's local XDG data directory
// so that desktop environments can display the app icon.
void InstallDesktopEntry(std::span<const unsigned char> icon_data);
void UninstallDesktopEntry();
#endif

}  // namespace eng

#endif  // ENGINE_PLATFORM_PLATFORM_H
