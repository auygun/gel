// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_TITLE_BAR_H
#define GEL_UI_TITLE_BAR_H

#include <string>

// The window's title bar, for where the window has to draw its own: Wayland
// compositors that leave decorations to the client (GNOME). Modeled on the
// GNOME header bar libdecor draws: the title centered, the window buttons the
// desktop is configured to show at the right.
//
// It is drawn behind every window and reads the mouse directly rather than
// through ImGui's hovered and active items, so it keeps working while a modal
// popup blocks every other window, as a title bar would.
class TitleBar {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual const std::string& GetWindowTitle() const = 0;
    // Whether the window shows as the active one; the title bar dims when
    // not.
    virtual bool IsWindowActive() const = 0;
    virtual bool IsWindowMaximized() const = 0;
    virtual bool IsDarkMode() const = 0;
    // Title bar buttons the desktop shows, besides close.
    virtual bool ShowMinimizeButton() const = 0;
    virtual bool ShowMaximizeButton() const = 0;
    virtual void OnMinimizeRequested() = 0;
    virtual void OnMaximizeToggleRequested() = 0;
    virtual void OnCloseRequested() = 0;
    // A press on the title bar, which hands the window to the compositor to
    // move it.
    virtual void OnMoveRequested() = 0;
    // A right click, at |x|, |y| in the window.
    virtual void OnWindowMenuRequested(int x, int y) = 0;
  };

  explicit TitleBar(Delegate& delegate);

  TitleBar(const TitleBar&) = delete;
  TitleBar& operator=(const TitleBar&) = delete;

  // Height of the title bar, which scales with the UI.
  static float GetHeight();

  // Draws the title bar across the top of the main viewport and handles its
  // input. Call once per frame, between ImGui::NewFrame() and ImGui::Render().
  void Update();

 private:
  enum class Button { kNone, kMinimize, kMaximize, kClose };

  Delegate& delegate_;
  // Button the left button went down on, which a release on it clicks.
  Button pressed_ = Button::kNone;
};

#endif  // GEL_UI_TITLE_BAR_H
