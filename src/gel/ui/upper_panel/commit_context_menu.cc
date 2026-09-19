// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/upper_panel/commit_context_menu.h"

#include "gel/ui/git_cmd_runner.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

CommitContextMenu::CommitContextMenu(
    GitCmdRunner& runner,
    std::function<void(const std::string&)> open_in_new_window)
    : runner_(runner), open_in_new_window_(std::move(open_in_new_window)) {}

bool CommitContextMenu::RenderCommitMenu(const std::string& commit,
                                         const std::string& subject,
                                         const ImVec2& window_padding,
                                         const ImVec2& item_spacing) {
  bool open = false;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, window_padding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, item_spacing);
  if (ImGui::BeginPopupContextItem()) {
    open = true;
    bool busy = runner_.IsBusy();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImGuiID tag_id = win->GetID("##create_tag");
    ImGuiID branch_id = win->GetID("##create_branch");
    // Save input focus state before submenus that might steal it.
    ImGuiID saved_active = g.ActiveId;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##ctx_menu_esc"),
                         ImGuiInputFlags_LockThisFrame);
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::MenuItem("Revert", nullptr, false, !busy))
      runner_.Run({"revert", "--no-edit", commit});
    if (ImGui::BeginMenu("Cherry-pick", !busy)) {
      if (ImGui::MenuItem("Commit"))
        runner_.Run({"cherry-pick", commit});
      if (ImGui::MenuItem("No-commit"))
        runner_.Run({"cherry-pick", "--no-commit", commit});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Interactive rebase", !busy)) {
      if (ImGui::MenuItem("Autosquash"))
        runner_.Run({"rebase", "-i", "--autosquash", commit});
      if (ImGui::MenuItem("No-autosquash"))
        runner_.Run({"rebase", "-i", "--no-autosquash", commit});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Reset", !runner_.IsCommandBusy())) {
      if (ImGui::MenuItem("Soft"))
        runner_.Run({"reset", "--soft", commit});
      if (ImGui::MenuItem("Mixed"))
        runner_.Run({"reset", "--mixed", commit});
      if (ImGui::MenuItem("Hard"))
        runner_.Run({"reset", "--hard", commit});
      ImGui::EndMenu();
    }
    // Restore input focus if a submenu stole it.
    if (saved_active != 0 && g.ActiveId != saved_active &&
        (saved_active == tag_id || saved_active == branch_id)) {
      g.ActiveId = saved_active;
      g.ActiveIdIsAlive = saved_active;
      g.ActiveIdWindow = win;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy commit subject"))
      ImGui::SetClipboardText(subject.c_str());
    ImGui::Separator();
    float input_area_top = ImGui::GetCursorScreenPos().y;
    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    if (ImGui::InputTextWithHint("##create_tag", "New tag name",
                                 tag_create_input_, sizeof(tag_create_input_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
      if (tag_create_input_[0] != '\0') {
        pending_tag_name_ = tag_create_input_;
        pending_tag_commit_ = commit;
        runner_.Run({"tag", tag_create_input_, commit});
        tag_create_input_[0] = '\0';
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create/Move tag") && tag_create_input_[0] != '\0') {
      pending_tag_name_ = tag_create_input_;
      pending_tag_commit_ = commit;
      runner_.Run({"tag", tag_create_input_, commit});
      tag_create_input_[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    if (ImGui::InputTextWithHint("##create_branch", "New branch name",
                                 branch_create_input_,
                                 sizeof(branch_create_input_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
      if (branch_create_input_[0] != '\0') {
        runner_.Run({"branch", branch_create_input_, commit});
        branch_create_input_[0] = '\0';
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create branch") && branch_create_input_[0] != '\0') {
      runner_.Run({"branch", branch_create_input_, commit});
      branch_create_input_[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    // Close child submenus when the mouse is in the input/button area
    // so they don't block hover and click on the controls.
    {
      int level = g.BeginPopupStack.Size;
      if ((int)g.OpenPopupStack.Size > level && g.IO.MousePos.x >= win->Pos.x &&
          g.IO.MousePos.x < win->Pos.x + win->Size.x &&
          g.IO.MousePos.y >= input_area_top &&
          g.IO.MousePos.y < ImGui::GetCursorScreenPos().y)
        ImGui::ClosePopupToLevel(level, true);
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
  return open;
}

void RenderTagMenuItems(
    GitCmdRunner& runner,
    const std::string& tag_name,
    const std::string& commit,
    char* create_buf,
    size_t create_buf_size,
    const std::function<void(const std::string&)>& open_in_new_window,
    std::string& pending_tag_name,
    std::string& pending_tag_commit) {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  ImGuiID create_id = win->GetID("##create_tag");
  ImGuiID saved_active = g.ActiveId;

  bool busy = runner.IsBusy();
  if (ImGui::MenuItem("Checkout (detached)", nullptr, false, !busy))
    runner.Run({"checkout", tag_name});
  if (ImGui::BeginMenu("Reset", !runner.IsCommandBusy())) {
    if (ImGui::MenuItem("Soft"))
      runner.Run({"reset", "--soft", tag_name});
    if (ImGui::MenuItem("Mixed"))
      runner.Run({"reset", "--mixed", tag_name});
    if (ImGui::MenuItem("Hard"))
      runner.Run({"reset", "--hard", tag_name});
    ImGui::EndMenu();
  }
  if (saved_active != 0 && g.ActiveId != saved_active &&
      saved_active == create_id) {
    g.ActiveId = saved_active;
    g.ActiveIdIsAlive = saved_active;
    g.ActiveIdWindow = win;
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Copy tag name"))
    ImGui::SetClipboardText(tag_name.c_str());
  if (ImGui::MenuItem("Open in new window"))
    open_in_new_window(tag_name);
  ImGui::Separator();
  float input_area_top = ImGui::GetCursorScreenPos().y;
  ImGui::BeginDisabled(busy);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
  if (ImGui::InputTextWithHint("##create_tag", "New tag name", create_buf,
                               create_buf_size,
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (create_buf[0] != '\0') {
      pending_tag_name = create_buf;
      pending_tag_commit = commit;
      runner.Run({"tag", create_buf, commit});
      create_buf[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Create/Move tag") && create_buf[0] != '\0') {
    pending_tag_name = create_buf;
    pending_tag_commit = commit;
    runner.Run({"tag", create_buf, commit});
    create_buf[0] = '\0';
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  {
    int level = g.BeginPopupStack.Size;
    if ((int)g.OpenPopupStack.Size > level && g.IO.MousePos.x >= win->Pos.x &&
        g.IO.MousePos.x < win->Pos.x + win->Size.x &&
        g.IO.MousePos.y >= input_area_top &&
        g.IO.MousePos.y < ImGui::GetCursorScreenPos().y)
      ImGui::ClosePopupToLevel(level, true);
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Delete tag", nullptr, false, !busy))
    runner.Run({"tag", "-d", tag_name});
}

void CommitContextMenu::RenderTagMenu(int index,
                                      const std::string& tag_name,
                                      const std::string& commit,
                                      const ImVec2& window_padding,
                                      const ImVec2& item_spacing) {
  char popup_id[32];
  snprintf(popup_id, sizeof(popup_id), "##tag_ctx_%d", index);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, window_padding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, item_spacing);
  if (ImGui::BeginPopupContextItem(popup_id)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##tag_esc"),
                         ImGuiInputFlags_LockThisFrame);
      ImGui::CloseCurrentPopup();
    }
    RenderTagMenuItems(runner_, tag_name, commit, tag_create_input_,
                       sizeof(tag_create_input_), open_in_new_window_,
                       pending_tag_name_, pending_tag_commit_);
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
}

void CommitContextMenu::RenderCollapsedTagMenu(
    const std::vector<std::string>& tags,
    const std::string& commit,
    const ImVec2& window_padding,
    const ImVec2& item_spacing) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, window_padding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, item_spacing);
  if (ImGui::BeginPopupContextItem("##collapsed_tag_ctx")) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##tag_esc"),
                         ImGuiInputFlags_LockThisFrame);
      ImGui::CloseCurrentPopup();
    }
    for (const auto& tag_name : tags) {
      if (ImGui::BeginMenu(tag_name.c_str())) {
        RenderTagMenuItems(runner_, tag_name, commit, tag_create_input_,
                           sizeof(tag_create_input_), open_in_new_window_,
                           pending_tag_name_, pending_tag_commit_);
        ImGui::EndMenu();
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
}

void RenderBranchMenuItems(
    GitCmdRunner& runner,
    const std::string& branch_name,
    bool is_remote,
    bool is_head,
    char* rename_buf,
    size_t rename_buf_size,
    char* create_buf,
    size_t create_buf_size,
    const std::function<void(const std::string&)>& open_in_new_window) {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  ImGuiID rename_id = win->GetID("##rename_branch");
  ImGuiID create_id = win->GetID("##create_branch");
  // Save input focus state before submenus that might steal it.
  ImGuiID saved_active = g.ActiveId;

  bool busy = runner.IsBusy();
  if (ImGui::MenuItem("Checkout", nullptr, false,
                      !busy && !is_head && !is_remote))
    runner.Run({"checkout", branch_name});
  if (ImGui::MenuItem("Rebase", nullptr, false, !busy && !is_head))
    runner.Run({"rebase", branch_name});
  if (ImGui::BeginMenu("Merge", !busy && !is_head)) {
    if (ImGui::MenuItem("Merge"))
      runner.Run({"merge", branch_name});
    if (ImGui::MenuItem("No-ff"))
      runner.Run({"merge", "--no-ff", branch_name});
    if (ImGui::MenuItem("FF-only"))
      runner.Run({"merge", "--ff-only", branch_name});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Reset", !runner.IsCommandBusy())) {
    if (ImGui::MenuItem("Soft"))
      runner.Run({"reset", "--soft", branch_name});
    if (ImGui::MenuItem("Mixed"))
      runner.Run({"reset", "--mixed", branch_name});
    if (ImGui::MenuItem("Hard"))
      runner.Run({"reset", "--hard", branch_name});
    ImGui::EndMenu();
  }
  // Restore input focus if a submenu stole it.
  if (saved_active != 0 && g.ActiveId != saved_active &&
      (saved_active == rename_id || saved_active == create_id)) {
    g.ActiveId = saved_active;
    g.ActiveIdIsAlive = saved_active;
    g.ActiveIdWindow = win;
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Copy branch name"))
    ImGui::SetClipboardText(branch_name.c_str());
  if (ImGui::MenuItem("Open in new window"))
    open_in_new_window(branch_name);
  ImGui::Separator();
  float input_area_top = ImGui::GetCursorScreenPos().y;
  ImGui::BeginDisabled(is_remote || busy);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
  if (ImGui::InputTextWithHint("##rename_branch", "New name", rename_buf,
                               rename_buf_size,
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (rename_buf[0] != '\0') {
      runner.Run({"branch", "-m", branch_name, rename_buf});
      rename_buf[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Rename") && rename_buf[0] != '\0') {
    runner.Run({"branch", "-m", branch_name, rename_buf});
    rename_buf[0] = '\0';
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(busy);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
  if (ImGui::InputTextWithHint("##create_branch", "New branch name", create_buf,
                               create_buf_size,
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (create_buf[0] != '\0') {
      runner.Run({"branch", create_buf, branch_name});
      create_buf[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Create branch") && create_buf[0] != '\0') {
    runner.Run({"branch", create_buf, branch_name});
    create_buf[0] = '\0';
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  // Close child submenus when the mouse is in the input/button area
  // so they don't block hover and click on the controls.
  {
    int level = g.BeginPopupStack.Size;
    if ((int)g.OpenPopupStack.Size > level && g.IO.MousePos.x >= win->Pos.x &&
        g.IO.MousePos.x < win->Pos.x + win->Size.x &&
        g.IO.MousePos.y >= input_area_top &&
        g.IO.MousePos.y < ImGui::GetCursorScreenPos().y)
      ImGui::ClosePopupToLevel(level, true);
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Delete branch", nullptr, false,
                      !busy && !is_head && !is_remote))
    runner.Run({"branch", "-D", branch_name});
}

void CommitContextMenu::RenderBranchMenu(int index,
                                         const std::string& branch_name,
                                         bool is_remote,
                                         const std::string& head_branch,
                                         const ImVec2& window_padding,
                                         const ImVec2& item_spacing) {
  char popup_id[32];
  snprintf(popup_id, sizeof(popup_id), "##branch_ctx_%d", index);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, window_padding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, item_spacing);
  if (ImGui::BeginPopupContextItem(popup_id)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##branch_esc"),
                         ImGuiInputFlags_LockThisFrame);
      ImGui::CloseCurrentPopup();
    }
    RenderBranchMenuItems(runner_, branch_name, is_remote,
                          branch_name == head_branch, branch_rename_input_,
                          sizeof(branch_rename_input_), branch_create_input_,
                          sizeof(branch_create_input_), open_in_new_window_);
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
}

bool CommitContextMenu::ConsumePendingTagMove(std::string& name,
                                              std::string& commit) {
  if (pending_tag_name_.empty())
    return false;
  name = std::move(pending_tag_name_);
  commit = std::move(pending_tag_commit_);
  pending_tag_name_.clear();
  pending_tag_commit_.clear();
  return true;
}
