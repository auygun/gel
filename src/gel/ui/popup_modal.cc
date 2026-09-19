// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/popup_modal.h"

#include <algorithm>

#include "third_party/imgui/imgui/imgui.h"

PopupModal::PopupModal(std::function<void()> exit_callback)
    : exit_callback_(std::move(exit_callback)), text_viewer_(*this, nullptr) {}

PopupModal::~PopupModal() = default;

void PopupModal::ShowMessage(std::string_view title,
                             std::string message,
                             bool exit_on_close) {
  Request req;
  req.title = title;
  req.message = std::move(message);
  req.exit_on_close = exit_on_close;
  queue_.push_back(std::move(req));
}

void PopupModal::ShowConfirmation(std::string_view title,
                                  std::string message,
                                  std::function<void()> on_confirm) {
  Request req;
  req.title = title;
  req.message = std::move(message);
  req.on_confirm = std::move(on_confirm);
  queue_.push_back(std::move(req));
}

void PopupModal::PrepareLines(const std::string& message) {
  lines_.clear();
  max_line_length_ = 0;
  size_t pos = 0;
  while (pos <= message.size()) {
    size_t nl = message.find('\n', pos);
    if (nl == std::string::npos)
      nl = message.size();
    lines_.push_back(message.substr(pos, nl - pos));
    max_line_length_ = std::max(max_line_length_, lines_.back().size());
    pos = nl + 1;
  }
}

void PopupModal::Render(bool window_focused) {
  if (queue_.empty()) {
    visible_ = false;
    return;
  }

  auto& front = queue_.front();

  if (!visible_) {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const char* id = front.title.empty() ? "##popup" : front.title.c_str();
    ImGui::OpenPopup(id);
    visible_ = true;
    keys_released_ = false;
    PrepareLines(front.message);
    text_viewer_.Reset();
  }

  bool any_dismiss_down = ImGui::IsKeyDown(ImGuiKey_Enter) ||
                          ImGui::IsKeyDown(ImGuiKey_KeypadEnter) ||
                          ImGui::IsKeyDown(ImGuiKey_Escape);
  if (!any_dismiss_down)
    keys_released_ = true;

  bool enter_pressed = false;
  bool escape_pressed = false;
  if (keys_released_ && !text_viewer_.search_active()) {
    enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
  }

  ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoScrollbar;
  const char* popup_id;
  if (front.title.empty()) {
    popup_id = "##popup";
    popup_flags |= ImGuiWindowFlags_NoTitleBar;
  } else {
    popup_id = front.title.c_str();
  }

  ImVec2 display = ImGui::GetIO().DisplaySize;
  const auto& style = ImGui::GetStyle();
  float char_w = ImGui::CalcTextSize("A").x;
  float line_h = ImGui::GetTextLineHeightWithSpacing();
  float reserved = ImGui::GetFrameHeight() * 2;

  float w = char_w * static_cast<float>(max_line_length_) +
            style.WindowPadding.x * 4 + style.ScrollbarSize;
  w = std::clamp(w, 350.0f, display.x * 0.8f);

  float content_h =
      line_h * static_cast<float>(lines_.size()) + style.WindowPadding.y * 4;
  float max_content_h = display.y * 0.7f - reserved - style.WindowPadding.y * 2;
  content_h = std::clamp(content_h, line_h * 3, max_content_h);
  float h = content_h + reserved + style.WindowPadding.y * 2;

  ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

  if (!window_focused)
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));

  bool was_open = ImGui::IsPopupOpen(popup_id);
  bool stay_open = true;
  if (ImGui::BeginPopupModal(popup_id, &stay_open, popup_flags)) {
    ImGui::BeginChild("##popup_tv", ImVec2(0, -reserved));
    text_viewer_.Update(0, lines_, &search_options_);
    ImGui::EndChild();

    ImGui::Separator();

    if (front.on_confirm) {
      if (ImGui::Button("Yes", ImVec2(120, 0)) || enter_pressed) {
        auto cb = std::move(front.on_confirm);
        ImGui::CloseCurrentPopup();
        visible_ = false;
        queue_.erase(queue_.begin());
        cb();
      }
      ImGui::SameLine();
      if (ImGui::Button("No", ImVec2(120, 0)) || escape_pressed) {
        ImGui::CloseCurrentPopup();
        visible_ = false;
        queue_.erase(queue_.begin());
      }
    } else {
      if (ImGui::Button("OK", ImVec2(120, 0)) || enter_pressed ||
          escape_pressed) {
        ImGui::CloseCurrentPopup();
        visible_ = false;
        bool should_exit = front.exit_on_close;
        queue_.erase(queue_.begin());
        if (should_exit)
          exit_callback_();
      }
    }

    ImGui::EndPopup();
  }

  // The title bar's close button dismisses the popup before the body runs, so
  // it takes the dismissing choice here: "No" for a confirmation, "OK" for a
  // message, which may still be the one that exits.
  if (was_open && !stay_open) {
    visible_ = false;
    bool should_exit = !front.on_confirm && front.exit_on_close;
    queue_.erase(queue_.begin());
    if (should_exit)
      exit_callback_();
  }

  if (!window_focused)
    ImGui::PopStyleColor();
}
