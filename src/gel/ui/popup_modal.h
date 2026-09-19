// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_POPUP_MODAL_H
#define GEL_UI_POPUP_MODAL_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "gel/ui/modules/text_viewer.h"

class PopupModal : public TextViewer::Delegate {
 public:
  explicit PopupModal(std::function<void()> exit_callback);
  ~PopupModal();

  void ShowMessage(std::string_view title,
                   std::string message,
                   bool exit_on_close = false);
  void ShowConfirmation(std::string_view title,
                        std::string message,
                        std::function<void()> on_confirm);

  void Render(bool window_focused);
  bool IsVisible() const { return visible_; }

 private:
  struct Request {
    std::string title;
    std::string message;
    bool exit_on_close = false;
    std::function<void()> on_confirm;
  };

  std::function<void()> exit_callback_;
  TextViewer text_viewer_;
  std::vector<Request> queue_;
  bool visible_ = false;
  bool keys_released_ = false;
  base::MatchOptions search_options_;
  std::vector<std::string> lines_;
  size_t max_line_length_ = 0;

  void PrepareLines(const std::string& message);
};

#endif  // GEL_UI_POPUP_MODAL_H
