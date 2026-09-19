// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/file_list.h"

#include <algorithm>

#include "gel/commands/git_diff.h"
#include "gel/commands/git_log.h"
#include "gel/persistent_settings.h"
#include "gel/ui/git_cmd_runner.h"
#include "gel/ui/style.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

namespace {

std::vector<std::string> BuildGitArgs(const char* cmd,
                                      const std::vector<std::string>& paths) {
  std::vector<std::string> args = {cmd, "--"};
  for (auto& p : paths)
    args.push_back(p);
  return args;
}

std::vector<std::string> BuildGitArgs(const char* cmd,
                                      const char* flag,
                                      const std::vector<std::string>& paths) {
  std::vector<std::string> args = {cmd, flag, "--"};
  for (auto& p : paths)
    args.push_back(p);
  return args;
}

std::vector<std::string> BuildRestoreArgs(
    const char* flag,
    const std::vector<std::string>& paths) {
  std::vector<std::string> args = {"restore", flag, "--"};
  for (auto& p : paths)
    args.push_back(p);
  return args;
}

bool IsImageFile(const std::string& path) {
  auto dot = path.rfind('.');
  if (dot == std::string::npos)
    return false;
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
         ext == ".gif" || ext == ".tga" || ext == ".psd";
}

}  // namespace

FileList::FileList(Delegate& delegate,
                   GitDiff& git_diff,
                   GitLog& git_log,
                   GitCmdRunner& runner,
                   PersistentSettings& settings,
                   ConfirmCallback on_confirm)
    : delegate_(delegate),
      git_diff_(git_diff),
      git_log_(git_log),
      runner_(runner),
      settings_(settings),
      on_confirm_(std::move(on_confirm)) {}

void FileList::Reset() {
  selected_file_ = -1;
  multi_selection_.Clear();
}

void FileList::RevertAll() {
  auto files = git_diff_.GetFileList();
  int count = static_cast<int>(files.size());
  multi_selection_.Clear();
  for (int i = 0; i < count; i++)
    multi_selection_.SetItemSelected(static_cast<ImGuiID>(i), true);
  PrepareRevert();
}

int FileList::ConsumeClickedFile() {
  int f = clicked_file_;
  clicked_file_ = -1;
  return f;
}

void FileList::SetSelectedFile(int index, bool scroll_into_view) {
  selected_file_ = index;
  if (scroll_into_view)
    scroll_file_list_ = true;
  if (multi_selection_.Size <= 1 &&
      (delegate_.IsUnstagedSelected() || delegate_.IsStagedSelected())) {
    multi_selection_.Clear();
    if (index >= 0)
      multi_selection_.SetItemSelected(static_cast<ImGuiID>(index), true);
  }
}

void FileList::ScrollToSelected() {
  scroll_file_list_ = true;
}

std::vector<std::string> FileList::GetSelectedPaths(bool include_old_paths) {
  auto files = git_diff_.GetFileList();
  std::vector<std::string> paths;
  auto add = [&](int idx) {
    if (idx < 0 || idx >= static_cast<int>(files.size()))
      return;
    if (files[idx].status == FileStatus::kCommit)
      return;
    paths.push_back(files[idx].path);
    if (include_old_paths && !files[idx].old_path.empty())
      paths.push_back(files[idx].old_path);
  };
  if (multi_selection_.Size > 0) {
    void* it = nullptr;
    ImGuiID id;
    while (multi_selection_.GetNextSelectedItem(&it, &id))
      add(static_cast<int>(id));
  } else {
    add(selected_file_);
  }
  return paths;
}

void FileList::PrepareRevert() {
  auto files = git_diff_.GetFileList();
  std::vector<std::string> revert_paths;
  std::vector<std::string> clean_paths;
  auto add_file = [&](int idx) {
    if (idx < 0 || idx >= static_cast<int>(files.size()))
      return;
    if (files[idx].status == FileStatus::kCommit)
      return;
    if (files[idx].status == FileStatus::kAdded)
      clean_paths.push_back(files[idx].path);
    else
      revert_paths.push_back(files[idx].path);
  };
  if (multi_selection_.Size > 0) {
    void* it = nullptr;
    ImGuiID id;
    while (multi_selection_.GetNextSelectedItem(&it, &id))
      add_file(static_cast<int>(id));
  } else {
    add_file(selected_file_);
  }
  if (revert_paths.empty() && clean_paths.empty())
    return;
  on_confirm_(
      "Revert Changes?",
      "Revert selected files? Uncommitted changes will be lost.",
      [this, rp = std::move(revert_paths), cp = std::move(clean_paths)]() {
        if (!cp.empty()) {
          runner_.Run(BuildGitArgs("clean", "-f", cp), true);
          if (!rp.empty())
            runner_.RunNext(BuildGitArgs("checkout", rp));
        } else if (!rp.empty()) {
          runner_.Run(BuildGitArgs("checkout", rp), true);
        }
      });
}

void FileList::Update(float width) {
  if (ImGui::BeginChild("file_list", ImVec2(width, -FLT_MIN),
                        ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    auto files = git_diff_.GetFileList();
    bool is_unstaged = delegate_.IsUnstagedSelected();
    bool is_staged = delegate_.IsStagedSelected();
    bool is_multi = is_unstaged || is_staged;

    ImGuiID active_id = ImGui::GetActiveID();
    ImGuiID scrollbar_id =
        ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindowRead(), ImGuiAxis_Y);
    bool scrollbar_active = active_id && active_id == scrollbar_id;

    // Freeze the item count while the scrollbar is held to prevent jumps.
    // Always clamp to the actual size to avoid out-of-bounds access when the
    // content is cleared (e.g. F5 refresh) while the scrollbar is held.
    if (!scrollbar_active)
      file_count_ = static_cast<int>(files.size());
    else
      file_count_ = std::min(file_count_, static_cast<int>(files.size()));

    ImGuiMultiSelectIO* ms_io = nullptr;
    if (is_multi) {
      ImGuiMultiSelectFlags flags = ImGuiMultiSelectFlags_ClearOnEscape |
                                    ImGuiMultiSelectFlags_BoxSelect1d;
      ms_io =
          ImGui::BeginMultiSelect(flags, multi_selection_.Size, file_count_);
      multi_selection_.ApplyRequests(ms_io);
    }

    ImGuiListClipper clipper;
    clipper.Begin(file_count_);

    // Ensure the selected file is rendered so we can scroll to it.
    if (scroll_file_list_ && selected_file_ >= 0 &&
        selected_file_ < file_count_)
      clipper.IncludeItemByIndex(selected_file_);
    if (is_multi && ms_io) {
      int src = static_cast<int>(ms_io->RangeSrcItem);
      if (src >= 0 && src < file_count_)
        clipper.IncludeItemByIndex(src);
    }

    while (clipper.Step()) {
      for (int i = clipper.DisplayStart;
           i < clipper.DisplayEnd && i < static_cast<int>(files.size()); i++) {
        auto& entry = files[i];

        ImGui::PushID(i);

        if (is_multi)
          ImGui::SetNextItemSelectionUserData(i);

        ImVec2 cursor = ImGui::GetCursorPos();
        bool item_selected =
            is_multi ? multi_selection_.Contains(static_cast<ImGuiID>(i))
                     : selected_file_ == i;
        if (ImGui::Selectable("##file", item_selected,
                              ImGuiSelectableFlags_AllowOverlap)) {
          selected_file_ = i;
          clicked_file_ = i;
        }
        bool is_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        // Keep the auto-selected file visible when scrolling through diff
        // content.
        if (selected_file_ == i && scroll_file_list_) {
          ImGui::SetScrollHereY();
          scroll_file_list_ = false;
        }

        // Right-click context menu for file entries.
        if (entry.status != FileStatus::kCommit &&
            ImGui::BeginPopupContextItem()) {
          if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##ctx_menu_esc"),
                               ImGuiInputFlags_LockThisFrame);
            ImGui::CloseCurrentPopup();
          }
          if (is_multi) {
            if (!multi_selection_.Contains(static_cast<ImGuiID>(i))) {
              multi_selection_.Clear();
              multi_selection_.SetItemSelected(static_cast<ImGuiID>(i), true);
            }
          }
          if (selected_file_ != i)
            selected_file_ = i;
          auto commits = git_log_.GetCommits();
          int ci = delegate_.GetSelectedCommitIndex();
          bool has_commit = ci >= 0 && ci < static_cast<int>(commits.size());
          bool can_diff = has_commit || is_unstaged || is_staged;
          bool multi_selected = is_multi && multi_selection_.Size > 1;
          if (ImGui::MenuItem("External diff", nullptr, false,
                              can_diff && !multi_selected)) {
            std::vector<std::string> args = {"difftool", "--no-prompt"};
            if (settings_.diff_tool[0])
              args.push_back(std::string("--tool=") + settings_.diff_tool);
            if (is_staged)
              args.emplace_back("--cached");
            else if (!is_unstaged) {
              // SHA-1 of the empty tree, used as parent for root commits.
              std::string parent =
                  commits[ci].parents.empty()
                      ? "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
                      : commits[ci].commit + "^";
              args.push_back(parent);
              args.push_back(commits[ci].commit);
            }
            args.emplace_back("--");
            args.emplace_back(is_unstaged || is_staged ? entry.path
                                                       : ":/" + entry.path);
            runner_.Spawn(std::move(args));
          }
          if (IsImageFile(entry.path) &&
              ImGui::MenuItem("Visual diff", nullptr, false,
                              can_diff && !multi_selected)) {
            std::string old_ref;
            std::string new_ref;
            bool new_from_worktree = false;
            if (is_unstaged) {
              old_ref = ":";
              new_from_worktree = true;
            } else if (is_staged) {
              old_ref = "HEAD";
              new_ref = ":";
            } else {
              old_ref = (entry.status == FileStatus::kAdded ||
                         commits[ci].parents.empty())
                            ? ""
                            : commits[ci].commit + "^";
              new_ref = entry.status == FileStatus::kDeleted
                            ? ""
                            : commits[ci].commit;
            }
            delegate_.OnShowImageDiff(old_ref, new_ref, entry.path,
                                      new_from_worktree);
          }
          if (ImGui::MenuItem("Copy path")) {
            if (multi_selected) {
              auto paths = GetSelectedPaths();
              std::string joined;
              for (auto& p : paths) {
                if (!joined.empty())
                  joined += ' ';
                joined += p;
              }
              ImGui::SetClipboardText(joined.c_str());
            } else {
              ImGui::SetClipboardText(entry.path.c_str());
            }
          }
          if (is_multi) {
            ImGui::Separator();
            if (ImGui::MenuItem("Select All")) {
              for (int si = 0; si < file_count_; si++)
                multi_selection_.SetItemSelected(static_cast<ImGuiID>(si),
                                                 true);
            }
          }
          if (is_unstaged) {
            auto selected_paths = GetSelectedPaths();
            bool busy = runner_.IsCommandBusy();
            ImGui::Separator();
            if (ImGui::MenuItem("Stage to Commit", "Ctrl+T", false, !busy))
              runner_.Run(BuildGitArgs("add", selected_paths), true);
            bool plural = selected_paths.size() > 1;
            if (ImGui::MenuItem(plural ? "Revert Changes" : "Revert Change",
                                "Ctrl+J", false, !busy))
              PrepareRevert();
          }
          if (is_staged) {
            auto selected_paths = GetSelectedPaths(true);
            bool busy = runner_.IsCommandBusy();
            ImGui::Separator();
            if (ImGui::MenuItem("Unstage from Commit", "Ctrl+U", false, !busy))
              runner_.Run(BuildRestoreArgs("--staged", selected_paths), true);
          }
          ImGui::EndPopup();
        }

        // Render the status letter and filename on top of the selectable.
        ImGui::SetCursorPos(cursor);
        float available_width = ImGui::GetContentRegionAvail().x;

        if (entry.status == FileStatus::kCommit) {
          ImGui::TextUnformatted(entry.path.c_str());
        } else {
          const char* status_letter = "M";
          ImU32 status_color = ResolveColor(ColorId::kYellow);
          switch (entry.status) {
            case FileStatus::kModified:
              status_letter = "M";
              status_color = ResolveColor(ColorId::kYellow);
              break;
            case FileStatus::kAdded:
              status_letter = "A";
              status_color = ResolveColor(ColorId::kGreen);
              break;
            case FileStatus::kDeleted:
              status_letter = "D";
              status_color = ResolveColor(ColorId::kRed);
              break;
            case FileStatus::kRenamed:
              status_letter = "R";
              status_color = ResolveColor(ColorId::kBlue);
              break;
            case FileStatus::kCopied:
              status_letter = "C";
              status_color = ResolveColor(ColorId::kCyan);
              break;
            case FileStatus::kSubmodule:
              status_letter = "S";
              status_color = ResolveColor(ColorId::kMagenta);
              break;
            default:
              break;
          }

          ImGui::PushStyleColor(ImGuiCol_Text, status_color);
          ImGui::TextUnformatted(status_letter);
          ImGui::PopStyleColor();

          ImGui::SameLine();

          ImGui::TextUnformatted(entry.path.c_str());
        }

        if (is_hovered) {
          float content_width = ImGui::CalcTextSize(entry.path.c_str()).x;
          if (entry.status != FileStatus::kCommit)
            content_width +=
                ImGui::CalcTextSize("M").x + ImGui::GetStyle().ItemSpacing.x;
          if (content_width > available_width)
            ImGui::SetTooltip("%s", entry.path.c_str());
        }

        ImGui::PopID();
      }
    }

    if (is_multi) {
      ms_io = ImGui::EndMultiSelect();
      multi_selection_.ApplyRequests(ms_io);
    }

    bool cmd_busy = runner_.IsCommandBusy();
    if (!ImGui::GetTopMostPopupModal() && !cmd_busy && is_unstaged &&
        ImGui::GetIO().KeyCtrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        auto paths = GetSelectedPaths();
        if (!paths.empty())
          runner_.Run(BuildGitArgs("add", paths), true);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_J, false))
        PrepareRevert();
    }
    if (!ImGui::GetTopMostPopupModal() && !cmd_busy && is_staged &&
        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_U, false)) {
      auto paths = GetSelectedPaths(true);
      if (!paths.empty())
        runner_.Run(BuildRestoreArgs("--staged", paths), true);
    }
  }
  ImGui::EndChild();
}
