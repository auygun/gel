// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/upper_panel/commit_modal.h"

#include <filesystem>
#include <fstream>

#include "base/exec.h"
#include "gel/git_repo.h"
#include "gel/ui/git_cmd_runner.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/base/thread_pool.h"

namespace {

// Fetch the commit message from git's saved message files.
// Checks MERGE_MSG and SQUASH_MSG.
std::string FetchSavedMessage(const std::filesystem::path& git_dir) {
  for (const auto& name : {"MERGE_MSG", "SQUASH_MSG"}) {
    std::filesystem::path path = git_dir / name;
    if (std::filesystem::exists(path)) {
      std::ifstream stream(path);
      return std::string(std::istreambuf_iterator<char>(stream),
                         std::istreambuf_iterator<char>());
    }
  }
  return {};
}

}  // namespace

CommitModal::CommitModal(GitCmdRunner& runner, GitRepo& git_repo)
    : runner_(runner), git_repo_(git_repo) {}

void CommitModal::Open(bool amend) {
  amend_ = amend;
  pending_ = true;
  amend_message_.clear();
  message_[0] = '\0';

  base::ThreadPool::Get().PostTaskAndReplyWithResult<std::string>(
      HERE,
      [git_dir = git_repo_.git_dir()]() -> std::string {
        return FetchSavedMessage(git_dir);
      },
      [this](std::string msg) {
        if (!msg.empty())
          snprintf(message_, sizeof(message_), "%s", msg.c_str());
      });
}

void CommitModal::Render() {
  if (pending_) {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.3f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::OpenPopup("Commit");
    focus_input_ = true;
    pending_ = false;
  }
  bool stay_open = true;
  if (ImGui::BeginPopupModal(
          "Commit", &stay_open,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::CloseCurrentPopup();
    }
    if (focus_input_) {
      ImGui::SetKeyboardFocusHere();
      focus_input_ = false;
    }
    float char_width = ImGui::CalcTextSize("0").x;
    float input_width = char_width * 80 + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(input_width);
    ImGui::InputTextMultiline("##commit_msg", message_, sizeof(message_),
                              ImVec2(input_width, 150));
    InputTextContextMenu(message_, sizeof(message_));
    bool prev_amend = amend_;
    ImGui::Checkbox("Amend", &amend_);
    if (amend_ && !prev_amend) {
      snprintf(saved_message_, sizeof(saved_message_), "%s", message_);
      if (amend_message_.empty()) {
        base::ThreadPool::Get().PostTaskAndReplyWithResult<std::string>(
            HERE,
            []() -> std::string {
              Exec exec;
              if (!exec.Start({"git", "log", "-1", "--format=%B"}))
                return {};
              while (exec.Poll())
                ;
              return exec.GetOut();
            },
            [this](std::string msg) {
              amend_message_ = std::move(msg);
              snprintf(message_, sizeof(message_), "%s",
                       amend_message_.c_str());
            });
      } else {
        snprintf(message_, sizeof(message_), "%s", amend_message_.c_str());
      }
    } else if (!amend_ && prev_amend) {
      snprintf(message_, sizeof(message_), "%s", saved_message_);
      saved_message_[0] = '\0';
    }
    bool has_message = message_[0] != '\0';
    if (ImGui::Button("Commit", ImVec2(120, 0))) {
      if (has_message) {
        std::vector<std::string> args = {"commit"};
        if (amend_)
          args.emplace_back("--amend");
        args.emplace_back("-m");
        args.emplace_back(message_);
        runner_.Run(std::move(args));
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
