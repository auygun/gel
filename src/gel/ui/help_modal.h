// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_HELP_MODAL_H
#define GEL_UI_HELP_MODAL_H

#include <string>

#include "gel/ui/modules/markdown_renderer.h"

// Modal window that renders the embedded help documentation using
// imgui_markdown.
class HelpModal : public MarkdownRenderer::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void SetPrimarySelection(const std::string& text) = 0;
  };

  explicit HelpModal(Delegate& delegate);
  ~HelpModal() override;

  void Open();
  void Update(bool window_focused);
  bool IsOpen() const { return open_; }

 private:
  // MarkdownRenderer::Delegate implementation
  void OnTextSelected(const std::string& text) final;

  Delegate& delegate_;
  bool open_ = false;
  bool show_ = false;

  // One renderer per tab (0 = Help, 1 = Licenses). Each owns its own search
  // bar and everything behind it, so searching is per tab.
  static constexpr int kTabCount = 2;
  MarkdownRenderer renderers_[kTabCount];
  int active_tab_ = 0;
  bool select_tab_ = false;

  // Tooltip state for heading navigation buttons.
  bool heading_tooltip_suppressed_ = false;
};

#endif  // GEL_UI_HELP_MODAL_H
