#include "third_party/kaliber/platform/platform.h"

#include "third_party/glfw/glfw/include/GLFW/glfw3.h"
#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/platform/platform_observer.h"

namespace eng {

namespace {

Key TranslateGLFWKey(int key) {
  switch (key) {
    case GLFW_KEY_A:
      return Key::A;
    case GLFW_KEY_B:
      return Key::B;
    case GLFW_KEY_C:
      return Key::C;
    case GLFW_KEY_D:
      return Key::D;
    case GLFW_KEY_E:
      return Key::E;
    case GLFW_KEY_F:
      return Key::F;
    case GLFW_KEY_G:
      return Key::G;
    case GLFW_KEY_H:
      return Key::H;
    case GLFW_KEY_I:
      return Key::I;
    case GLFW_KEY_J:
      return Key::J;
    case GLFW_KEY_K:
      return Key::K;
    case GLFW_KEY_L:
      return Key::L;
    case GLFW_KEY_M:
      return Key::M;
    case GLFW_KEY_N:
      return Key::N;
    case GLFW_KEY_O:
      return Key::O;
    case GLFW_KEY_P:
      return Key::P;
    case GLFW_KEY_Q:
      return Key::Q;
    case GLFW_KEY_R:
      return Key::R;
    case GLFW_KEY_S:
      return Key::S;
    case GLFW_KEY_T:
      return Key::T;
    case GLFW_KEY_U:
      return Key::U;
    case GLFW_KEY_V:
      return Key::V;
    case GLFW_KEY_W:
      return Key::W;
    case GLFW_KEY_X:
      return Key::X;
    case GLFW_KEY_Y:
      return Key::Y;
    case GLFW_KEY_Z:
      return Key::Z;

    case GLFW_KEY_0:
      return Key::Num0;
    case GLFW_KEY_1:
      return Key::Num1;
    case GLFW_KEY_2:
      return Key::Num2;
    case GLFW_KEY_3:
      return Key::Num3;
    case GLFW_KEY_4:
      return Key::Num4;
    case GLFW_KEY_5:
      return Key::Num5;
    case GLFW_KEY_6:
      return Key::Num6;
    case GLFW_KEY_7:
      return Key::Num7;
    case GLFW_KEY_8:
      return Key::Num8;
    case GLFW_KEY_9:
      return Key::Num9;

    case GLFW_KEY_F1:
      return Key::F1;
    case GLFW_KEY_F2:
      return Key::F2;
    case GLFW_KEY_F3:
      return Key::F3;
    case GLFW_KEY_F4:
      return Key::F4;
    case GLFW_KEY_F5:
      return Key::F5;
    case GLFW_KEY_F6:
      return Key::F6;
    case GLFW_KEY_F7:
      return Key::F7;
    case GLFW_KEY_F8:
      return Key::F8;
    case GLFW_KEY_F9:
      return Key::F9;
    case GLFW_KEY_F10:
      return Key::F10;
    case GLFW_KEY_F11:
      return Key::F11;
    case GLFW_KEY_F12:
      return Key::F12;
    case GLFW_KEY_F13:
      return Key::F13;
    case GLFW_KEY_F14:
      return Key::F14;
    case GLFW_KEY_F15:
      return Key::F15;
    case GLFW_KEY_F16:
      return Key::F16;
    case GLFW_KEY_F17:
      return Key::F17;
    case GLFW_KEY_F18:
      return Key::F18;
    case GLFW_KEY_F19:
      return Key::F19;
    case GLFW_KEY_F20:
      return Key::F20;
    case GLFW_KEY_F21:
      return Key::F21;
    case GLFW_KEY_F22:
      return Key::F22;
    case GLFW_KEY_F23:
      return Key::F23;
    case GLFW_KEY_F24:
      return Key::F24;

    case GLFW_KEY_ESCAPE:
      return Key::Escape;
    case GLFW_KEY_SPACE:
      return Key::Space;
    case GLFW_KEY_ENTER:
      return Key::Enter;
    case GLFW_KEY_TAB:
      return Key::Tab;
    case GLFW_KEY_BACKSPACE:
      return Key::Backspace;

    case GLFW_KEY_UP:
      return Key::Up;
    case GLFW_KEY_DOWN:
      return Key::Down;
    case GLFW_KEY_LEFT:
      return Key::Left;
    case GLFW_KEY_RIGHT:
      return Key::Right;
    case GLFW_KEY_PAGE_UP:
      return Key::PageUp;
    case GLFW_KEY_PAGE_DOWN:
      return Key::PageDown;
    case GLFW_KEY_HOME:
      return Key::Home;
    case GLFW_KEY_END:
      return Key::End;
    case GLFW_KEY_INSERT:
      return Key::Insert;
    case GLFW_KEY_DELETE:
      return Key::Delete;

    case GLFW_KEY_LEFT_SHIFT:
      return Key::ShiftLeft;
    case GLFW_KEY_RIGHT_SHIFT:
      return Key::ShiftRight;
    case GLFW_KEY_LEFT_CONTROL:
      return Key::ControlLeft;
    case GLFW_KEY_RIGHT_CONTROL:
      return Key::ControlRight;
    case GLFW_KEY_LEFT_ALT:
      return Key::AltLeft;
    case GLFW_KEY_RIGHT_ALT:
      return Key::AltRight;
    case GLFW_KEY_LEFT_SUPER:
      return Key::SuperLeft;
    case GLFW_KEY_RIGHT_SUPER:
      return Key::SuperRight;
    case GLFW_KEY_MENU:
      return Key::Menu;

    case GLFW_KEY_APOSTROPHE:
      return Key::Apostrophe;
    case GLFW_KEY_COMMA:
      return Key::Comma;
    case GLFW_KEY_MINUS:
      return Key::Minus;
    case GLFW_KEY_PERIOD:
      return Key::Period;
    case GLFW_KEY_SLASH:
      return Key::Slash;
    case GLFW_KEY_SEMICOLON:
      return Key::Semicolon;
    case GLFW_KEY_EQUAL:
      return Key::Equal;
    case GLFW_KEY_LEFT_BRACKET:
      return Key::LeftBracket;
    case GLFW_KEY_BACKSLASH:
      return Key::Backslash;
    case GLFW_KEY_RIGHT_BRACKET:
      return Key::RightBracket;
    case GLFW_KEY_GRAVE_ACCENT:
      return Key::GraveAccent;

    case GLFW_KEY_CAPS_LOCK:
      return Key::CapsLock;
    case GLFW_KEY_SCROLL_LOCK:
      return Key::ScrollLock;
    case GLFW_KEY_NUM_LOCK:
      return Key::NumLock;
    case GLFW_KEY_PRINT_SCREEN:
      return Key::PrintScreen;
    case GLFW_KEY_PAUSE:
      return Key::Pause;

    case GLFW_KEY_KP_0:
      return Key::Keypad0;
    case GLFW_KEY_KP_1:
      return Key::Keypad1;
    case GLFW_KEY_KP_2:
      return Key::Keypad2;
    case GLFW_KEY_KP_3:
      return Key::Keypad3;
    case GLFW_KEY_KP_4:
      return Key::Keypad4;
    case GLFW_KEY_KP_5:
      return Key::Keypad5;
    case GLFW_KEY_KP_6:
      return Key::Keypad6;
    case GLFW_KEY_KP_7:
      return Key::Keypad7;
    case GLFW_KEY_KP_8:
      return Key::Keypad8;
    case GLFW_KEY_KP_9:
      return Key::Keypad9;
    case GLFW_KEY_KP_DECIMAL:
      return Key::KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE:
      return Key::KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY:
      return Key::KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT:
      return Key::KeypadSubtract;
    case GLFW_KEY_KP_ADD:
      return Key::KeypadAdd;
    case GLFW_KEY_KP_ENTER:
      return Key::KeypadEnter;
    case GLFW_KEY_KP_EQUAL:
      return Key::KeypadEqual;

    default:
      return Key::Unknown;
  }
}

}  // namespace

void KaliberMain(Platform* platform);

Platform::Platform() = default;

Platform::~Platform() {
  DLOG(0) << "Shutting down platform.";
  PlatformShutdown();
  for (auto& c : cursors_) {
    if (c)
      glfwDestroyCursor(c);
  }
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
}

void Platform::CreateMainWindow(bool use_opengl, int width, int height) {
  // Clean up existing window when recreating (e.g. renderer fallback).
  if (window_) {
    for (auto& c : cursors_) {
      if (c) {
        glfwDestroyCursor(c);
        c = nullptr;
      }
    }
    glfwDestroyWindow(window_);
    window_ = nullptr;
    // Reset input state so keys/buttons held when the old window was destroyed
    // don't remain stuck (the new window won't receive their release events).
    keys_down_.fill(false);
    mouse_buttons_down_.fill(false);
  }

#if defined(OS_LINUX)
  if (display_backend_hint_)
    glfwInitHint(GLFW_PLATFORM, display_backend_hint_);
  if (!decorated_)
    glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);
#endif

  if (!glfwInit()) {
    DLOG(0) << "Failed to initialize GLFW.";
    return;
  }

  glfwSetErrorCallback([](int error, const char* description) {
    DLOG(0) << "GLFW error " << error << ": " << description;
  });

  glfwDefaultWindowHints();
  if (!use_opengl) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  } else {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  }
  if (!decorated_)
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
#if defined(OS_LINUX)
  glfwWindowHintString(GLFW_X11_CLASS_NAME, "gel");
  glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "gel");
  glfwWindowHintString(GLFW_WAYLAND_APP_ID, "gel");
#endif

  if (width <= 0 || height <= 0) {
    int work_x, work_y, work_w, work_h;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    glfwGetMonitorWorkarea(monitor, &work_x, &work_y, &work_w, &work_h);
    float scale_x = 1, scale_y = 1;
    glfwGetMonitorContentScale(monitor, &scale_x, &scale_y);
    if (width <= 0)
      width = static_cast<int>(work_w * 3.0f / 4 / (scale_x > 0 ? scale_x : 1));
    if (height <= 0)
      height =
          static_cast<int>(work_h * 3.0f / 4 / (scale_y > 0 ? scale_y : 1));
  }

  window_ = glfwCreateWindow(width, height, "gel", nullptr, nullptr);
  CHECK(window_) << "Failed to create GLFW window.";
  glfwSetWindowSizeLimits(window_, 200, 200, GLFW_DONT_CARE, GLFW_DONT_CARE);

  glfwSetWindowUserPointer(window_, this);

  glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int /*scancode*/,
                                 int action, int /*mods*/) {
    auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
    p->has_event_.store(true, std::memory_order_relaxed);
    Key translated = TranslateGLFWKey(key);
    if (translated != Key::Unknown) {
      p->keys_down_[static_cast<int>(translated)] =
          (action == GLFW_PRESS || action == GLFW_REPEAT);
    }
  });

  glfwSetCharCallback(window_, [](GLFWwindow* w, unsigned int codepoint) {
    auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
    p->has_event_.store(true, std::memory_order_relaxed);
    p->input_characters_.push_back(codepoint);
  });

  glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double, double) {
    auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
    p->has_event_.store(true, std::memory_order_relaxed);
  });

  glfwSetCursorEnterCallback(window_, [](GLFWwindow* w, int) {
    auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
    p->has_event_.store(true, std::memory_order_relaxed);
  });

  glfwSetMouseButtonCallback(
      window_, [](GLFWwindow* w, int button, int action, int /*mods*/) {
        auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
        p->has_event_.store(true, std::memory_order_relaxed);
        MouseButton mb = MouseButton::Unknown;
        if (button == GLFW_MOUSE_BUTTON_LEFT)
          mb = MouseButton::Left;
        else if (button == GLFW_MOUSE_BUTTON_RIGHT)
          mb = MouseButton::Right;
        else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
          mb = MouseButton::Middle;
        if (mb != MouseButton::Unknown) {
          p->mouse_buttons_down_[static_cast<int>(mb)] = (action == GLFW_PRESS);
          p->mouse_button_events_.push_back({mb, action == GLFW_PRESS});
        }
      });

  glfwSetScrollCallback(
      window_, [](GLFWwindow* w, double xoffset, double yoffset) {
        auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
        p->has_event_.store(true, std::memory_order_relaxed);
        p->mouse_scroll_x_delta_ += static_cast<float>(xoffset);
        p->mouse_scroll_y_delta_ += static_cast<float>(yoffset);
      });

  glfwSetFramebufferSizeCallback(
      window_, [](GLFWwindow* w, int width, int height) {
        auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
        p->has_event_.store(true, std::memory_order_relaxed);
        p->pending_width_ = width;
        p->pending_height_ = height;
        p->has_pending_resize_ = true;
      });

  glfwSetWindowFocusCallback(window_, [](GLFWwindow* w, int focused) {
    auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
    p->has_event_.store(true, std::memory_order_relaxed);
    if (!p->observer_)
      return;
    if (focused)
      p->observer_->GainedFocus();
    else
      p->observer_->LostFocus();
  });

  glfwSetWindowCloseCallback(window_, [](GLFWwindow* w) {
    auto* p = static_cast<Platform*>(glfwGetWindowUserPointer(w));
    // If a mouse button is held, the close was likely triggered by the cursor
    // reaching the decoration close button during a drag.  Suppress it.
    for (bool down : p->mouse_buttons_down_) {
      if (down) {
        glfwSetWindowShouldClose(w, GLFW_FALSE);
        return;
      }
    }
    p->has_event_.store(true, std::memory_order_relaxed);
    p->should_exit_ = true;
  });

  device_scale_factor_ = PlatformDetectScaleFactor();

  // Create standard cursors matching ImGuiMouseCursor_ enum order.
  cursors_[0] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
  cursors_[1] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
  cursors_[2] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
  cursors_[3] = glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR);
  cursors_[4] = glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR);
  cursors_[5] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
  cursors_[6] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
  cursors_[7] = glfwCreateStandardCursor(GLFW_POINTING_HAND_CURSOR);
  cursors_[10] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);

  observer_->OnWindowCreated();

  PlatformInit();
}

void Platform::Update(double wait_timeout) {
  mouse_scroll_y_delta_ = 0.0f;
  mouse_scroll_x_delta_ = 0.0f;
  mouse_button_events_.clear();
  input_characters_.clear();

  if (!PlatformUpdate(wait_timeout)) {
    if (wait_timeout > 0) {
      // Cap to 24 hours to avoid overflow when GLFW converts to milliseconds.
      constexpr double kMaxTimeout = 86400.0;
      if (wait_timeout > kMaxTimeout)
        wait_timeout = kMaxTimeout;
      glfwWaitEventsTimeout(wait_timeout);
    } else {
      glfwPollEvents();
    }
  }

  double cursor_x = 0, cursor_y = 0;
  glfwGetCursorPos(window_, &cursor_x, &cursor_y);
  mouse_x_ = static_cast<int>(cursor_x);
  mouse_y_ = static_cast<int>(cursor_y);
  cursor_inside_ = glfwGetWindowAttrib(window_, GLFW_HOVERED) != 0;

  if (has_pending_resize_) {
    has_pending_resize_ = false;
    if (observer_)
      observer_->OnFramebufferResized(pending_width_, pending_height_);
  }
  if (glfwWindowShouldClose(window_))
    should_exit_ = true;
}

void Platform::Exit() {
  glfwSetWindowShouldClose(window_, GLFW_TRUE);
  should_exit_ = true;
}

int Platform::GetWindowWidth() const {
  int w, h;
  glfwGetWindowSize(window_, &w, &h);
  return w;
}

int Platform::GetWindowHeight() const {
  int w, h;
  glfwGetWindowSize(window_, &w, &h);
  return h;
}

void Platform::GetWindowPosition(int& x, int& y) const {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    x = y = -1;
    return;
  }
  glfwGetWindowPos(window_, &x, &y);
}

void Platform::SetWindowTitle(const std::string& title) {
  glfwSetWindowTitle(window_, title.c_str());
}

void Platform::SetWindowPosition(int x, int y) {
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
    return;
  glfwSetWindowPos(window_, x, y);
}

bool Platform::IsMaximized() const {
  return glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) != 0;
}

void Platform::SetMaximized(bool maximized) {
  if (maximized)
    glfwMaximizeWindow(window_);
  else
    glfwRestoreWindow(window_);
}

void Platform::Minimize() {
  glfwIconifyWindow(window_);
}

void Platform::SetDecorated(bool decorated) {
  decorated_ = decorated;
}

bool Platform::IsDecorated() const {
  return decorated_;
}

void Platform::SetMouseCursor(int cursor) {
  if (cursor < 0) {
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
  } else {
    glfwSetInputMode(
        window_, GLFW_CURSOR,
        cursor_captured_ ? GLFW_CURSOR_CAPTURED : GLFW_CURSOR_NORMAL);
    glfwSetCursor(window_, cursor < kCursorCount ? cursors_[cursor] : nullptr);
  }
}

void Platform::SetCursorCaptured(bool captured) {
  if (cursor_captured_ == captured)
    return;
  cursor_captured_ = captured;
  glfwSetInputMode(window_, GLFW_CURSOR,
                   captured ? GLFW_CURSOR_CAPTURED : GLFW_CURSOR_NORMAL);
}

#if defined(OS_LINUX)
void Platform::SetDisplayBackendHint(int platform_hint) {
  display_backend_hint_ = platform_hint;
}
#endif

GLFWwindow* Platform::GetWindow() {
  return window_;
}

#if !defined(OS_LINUX)
void Platform::SetClipboardText(const char* text) {
  glfwSetClipboardString(window_, text);
}

const char* Platform::GetClipboardText() {
  return glfwGetClipboardString(window_);
}
#endif

#if !defined(OS_MAC)
void Platform::SetWindowIcon(int width,
                             int height,
                             const unsigned char* rgba_pixels) {
  GLFWimage image;
  image.width = width;
  image.height = height;
  image.pixels = const_cast<unsigned char*>(rgba_pixels);
  glfwSetWindowIcon(window_, 1, &image);
}
#endif

}  // namespace eng

int main(int argc, char** argv) {
  eng::Platform platform;
  platform.SetMainArgs(argc, argv);
  eng::KaliberMain(&platform);
  return 0;
}
