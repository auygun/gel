// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/git_console_window.h"

#include <algorithm>

#include "gel/ui/style.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

GitConsoleWindow::GitConsoleWindow(GitCmdRunner& runner,
                                   PersistentSettings& settings)
    : runner_(runner), settings_(settings), text_viewer_(*this) {
  text_viewer_.set_keyboard_scroll(true);
}

void GitConsoleWindow::Open() {
  if (!open_)
    show_ = true;
  uncollapse_ = true;
}

void GitConsoleWindow::Toggle() {
  if (open_ && settings_.console_window_collapsed)
    uncollapse_ = true;
  else if (open_) {
    open_ = false;
    settings_.console_window_open = false;
  } else {
    Open();
  }
}

void GitConsoleWindow::RenderLine(int line_index, const std::string& line) {
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  if (line_index >= 0 && line_index < static_cast<int>(line_types_.size())) {
    if (line_types_[line_index] == LineType::kCommand)
      color = ResolveColor(ColorId::kGreen);
    else if (line_types_[line_index] == LineType::kInfo)
      color = ResolveColor(ColorId::kCyan);
  }
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
  ImGui::PopStyleColor();
}

void GitConsoleWindow::AppendLine(std::string text, LineType type) {
  lines_.push_back(std::move(text));
  line_types_.push_back(type);
}

void GitConsoleWindow::RebuildLines() {
  auto& output = runner_.task_output();

  // Add info lines once when output is initially empty.
  if (output.empty() && lines_.empty()) {
    auto state = runner_.task_state();
    if (state == TaskState::kCmdRunning) {
      AppendLine("Running...", LineType::kInfo);
    } else if (state == TaskState::kWaitingForUser) {
      char msg[128];
      snprintf(msg, sizeof(msg), "A %s is in progress.",
               GetTaskName(runner_.task()));
      AppendLine(msg, LineType::kInfo);
      AppendLine(
          "You can continue, abort, or resolve conflicts and then continue.",
          LineType::kInfo);
      AppendLine("", LineType::kDefault);
    }
    return;
  }

  // Parse only new output since last call.
  const char* p = output.c_str();
  while (*p) {
    const char* eol = p;
    while (*eol && *eol != '\n')
      eol++;

    bool is_cmd = (eol - p >= 2 && p[0] == '$' && p[1] == ' ');
    bool is_info = (!is_cmd && p[0] == kInfoLinePrefix);

    const char* text_start = is_info ? p + 1 : p;
    AppendLine({text_start, eol}, is_cmd    ? LineType::kCommand
                                  : is_info ? LineType::kInfo
                                            : LineType::kDefault);

    p = *eol ? eol + 1 : eol;
  }
  output.clear();
}

void GitConsoleWindow::Clear() {
  lines_.clear();
  line_types_.clear();
  runner_.task_output().clear();
}

void GitConsoleWindow::Update(bool window_focused) {
  if (restore_pending_) {
    restore_pending_ = false;
    if (settings_.console_window_open)
      show_ = true;
  }
  bool should_show = runner_.ConsumeShowConsole();
  if (should_show && !open_) {
    show_ = true;
  }

  char popup_title[64];
  const char* task_name = GetTaskName(runner_.task());
  if (task_name[0] != '\0' && runner_.task_state() != TaskState::kIdle)
    snprintf(popup_title, sizeof(popup_title), "Console - %s###git_console",
             task_name);
  else
    snprintf(popup_title, sizeof(popup_title), "Console###git_console");

  if (show_) {
    if (settings_.console_window_width > 0 &&
        settings_.console_window_height > 0) {
      ImGui::SetNextWindowSize(
          ImVec2(static_cast<float>(settings_.console_window_width),
                 static_cast<float>(settings_.console_window_height)),
          ImGuiCond_FirstUseEver);
    } else {
      ImVec2 display = ImGui::GetIO().DisplaySize;
      ImGui::SetNextWindowSize(ImVec2(display.x * 0.7f, display.y * 0.7f),
                               ImGuiCond_FirstUseEver);
    }
    if (settings_.console_window_x >= 0 && settings_.console_window_y >= 0) {
      ImGui::SetNextWindowPos(
          ImVec2(static_cast<float>(settings_.console_window_x),
                 static_cast<float>(settings_.console_window_y)),
          ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowCollapsed(settings_.console_window_collapsed);
    show_ = false;
    open_ = true;
  }

  if (!open_)
    return;
  if (uncollapse_) {
    ImGui::SetNextWindowCollapsed(false);
    uncollapse_ = false;
  }
  if (!window_focused)
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
  if (ImGui::Begin(popup_title, &open_)) {
    bool focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    text_viewer_.set_keyboard_scroll(focused);
    auto state = runner_.task_state();
    auto& output = runner_.task_output();

    // Rebuild the line vectors when the output or state changes.
    bool output_changed = !output.empty() || state != last_state_;
    if (output_changed) {
      RebuildLines();
    }

    // Reserve space for the button row at the bottom.
    float reserved = ImGui::GetFrameHeightWithSpacing();

    ImGui::BeginChild("##tv_container", ImVec2(0, -reserved));
    text_viewer_.Update(0, lines_, &search_options_);
    ImGui::EndChild();

    // Auto-scroll to the last line when new output arrives.
    if (output_changed && !lines_.empty())
      text_viewer_.ScrollToLine(static_cast<int>(lines_.size()) - 1);

    last_state_ = state;

    if (state == TaskState::kWaitingForUser) {
      if (ImGui::Button("Continue")) {
        runner_.Continue();
      }
      ImGui::SameLine();
      if (runner_.task() != GitTask::kMerge) {
        if (ImGui::Button("Skip")) {
          runner_.Skip();
        }
        ImGui::SameLine();
      }
      if (ImGui::Button("Abort")) {
        runner_.Abort();
      }
      ImGui::SameLine();
      if (ImGui::Button("Quit")) {
        runner_.Quit();
      }
      ImGui::SameLine();
      if (ImGui::Button("Run git-gui")) {
        runner_.Spawn({"gui", "citool"}, true);
      }
      ImGui::SameLine();
    }
    if (ImGui::Button("Close"))
      open_ = false;
    if (state == TaskState::kIdle) {
      ImGui::SameLine();
      if (ImGui::Button("Clear")) {
        Clear();
      }
    }
  }
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  ImVec2 pos = win->Pos;
  ImVec2 size = win->SizeFull;
  bool collapsed = win->Collapsed;
  ImGui::End();
  settings_.console_window_x = static_cast<int>(pos.x);
  settings_.console_window_y = static_cast<int>(pos.y);
  settings_.console_window_width = static_cast<int>(size.x);
  settings_.console_window_height = static_cast<int>(size.y);
  settings_.console_window_collapsed = collapsed;
  settings_.console_window_open = open_;
  if (!window_focused)
    ImGui::PopStyleColor();
}
