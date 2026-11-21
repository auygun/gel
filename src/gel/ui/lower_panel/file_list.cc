// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/file_list.h"

#include <algorithm>
#include <string_view>

#include "gel/commands/git_diff.h"
#include "gel/commands/git_log.h"
#include "gel/persistent_settings.h"
#include "gel/ui/git_cmd_runner.h"
#include "gel/ui/modules/image_view.h"
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
  tree_.Clear();
}

void FileList::RevertAll() {
  auto files = git_diff_.GetFileList();
  int count = static_cast<int>(files.size());
  multi_selection_.Clear();
  for (int i = 0; i < count; i++)
    multi_selection_.SetItemSelected(static_cast<ImGuiID>(i), true);
  PrepareRevert();
}

std::vector<std::string> FileList::GetCommitParents(
    const std::string& commit) const {
  // Taken from the diff header rather than the log, whose parents are
  // rewritten when a path filter is active.
  std::vector<std::string> parents;
  auto files = git_diff_.GetFileList();
  auto content = git_diff_.GetDiffContent();
  // The diff may still show the previously selected commit.
  if (files.empty() || files[0].status != FileStatus::kCommit ||
      content.empty() || content[0] != "commit " + commit)
    return parents;
  size_t end = files.size() > 1 ? files[1].start_line : content.size();
  for (size_t i = 1; i < end && i < content.size(); i++) {
    if (content[i].starts_with("Parent: "))
      parents.push_back(content[i].substr(8));
    else if (!parents.empty())
      break;
  }
  return parents;
}

int FileList::ParentMenuItems(const std::vector<std::string>& parents) const {
  auto commits = git_log_.GetCommits();
  int picked = 0;
  for (size_t i = 0; i < parents.size(); i++) {
    const std::string& hash = parents[i];
    std::string label =
        "Parent " + std::to_string(i + 1) + "  " + hash.substr(0, 7);
    // The subject is only known for parents in the loaded history.
    auto it = std::find_if(commits.begin(), commits.end(),
                           [&](const auto& c) { return c.commit == hash; });
    if (it != commits.end() && !it->message.empty())
      label += "  " + it->message[0];
    if (ImGui::MenuItem(label.c_str()))
      picked = static_cast<int>(i) + 1;
  }
  return picked;
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
          runner_.Run(BuildGitArgs("clean", "-f", cp), RefreshScope::kLocal);
          if (!rp.empty())
            runner_.RunNext(BuildGitArgs("checkout", rp));
        } else if (!rp.empty()) {
          runner_.Run(BuildGitArgs("checkout", rp), RefreshScope::kLocal);
        }
      });
}

void FileList::SyncTree(std::span<const FileEntry> files) {
  int count = static_cast<int>(files.size());
  if (count < tree_.item_count())
    tree_.Clear();
  // The commit header is shown at the top level.
  for (int i = tree_.item_count(); i < count; i++)
    tree_.Add(files[i].status == FileStatus::kCommit ? std::string_view()
                                                     : files[i].path);
  tree_.FlattenIfDirty();
}

void FileList::SetAllDirsOpen(bool open) {
  tree_.SetAllDirsOpen(open);
  // Keep the selected file, or the directory now hiding it, in view.
  ScrollToSelected();
}

ImGuiID FileList::RowToStorageId(int row) const {
  auto rows = tree_.rows();
  int item =
      row >= 0 && row < static_cast<int>(rows.size()) ? rows[row].item : -1;
  return item >= 0 ? static_cast<ImGuiID>(item)
                   : kDirRowIdFlag | static_cast<ImGuiID>(row);
}

void FileList::ApplySelectionRequests(ImGuiMultiSelectIO* ms_io, bool tree) {
  // Selection is keyed by file index. In the tree view, selection user data
  // is the row instead, so translate it.
  if (tree) {
    multi_selection_.UserData = this;
    multi_selection_.AdapterIndexToStorageId =
        [](ImGuiSelectionBasicStorage* self, int row) {
          return static_cast<FileList*>(self->UserData)->RowToStorageId(row);
        };
  } else {
    multi_selection_.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage*,
                                                  int index) {
      return static_cast<ImGuiID>(index);
    };
  }
  multi_selection_.ApplyRequests(ms_io);
  if (!tree || ms_io->Requests.empty())
    return;
  // Directory rows are not selectable.
  std::vector<ImGuiID> dir_ids;
  void* it = nullptr;
  ImGuiID id;
  while (multi_selection_.GetNextSelectedItem(&it, &id)) {
    if (id & kDirRowIdFlag)
      dir_ids.push_back(id);
  }
  for (ImGuiID dir_id : dir_ids)
    multi_selection_.SetItemSelected(dir_id, false);
  // Select all also selects files hidden in collapsed directories.
  for (const ImGuiSelectionRequest& req : ms_io->Requests) {
    if (req.Type == ImGuiSelectionRequestType_SetAll && req.Selected) {
      for (int i = 0; i < file_count_; i++)
        multi_selection_.SetItemSelected(static_cast<ImGuiID>(i), true);
      break;
    }
  }
}

void FileList::RenderFileContextMenu(std::span<const FileEntry> files,
                                     int index,
                                     bool is_multi,
                                     bool is_unstaged,
                                     bool is_staged) {
  const FileEntry& entry = files[index];
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##ctx_menu_esc"),
                       ImGuiInputFlags_LockThisFrame);
    ImGui::CloseCurrentPopup();
  }
  if (is_multi) {
    if (!multi_selection_.Contains(static_cast<ImGuiID>(index))) {
      multi_selection_.Clear();
      multi_selection_.SetItemSelected(static_cast<ImGuiID>(index), true);
    }
  }
  if (selected_file_ != index)
    selected_file_ = index;
  auto commits = git_log_.GetCommits();
  int ci = delegate_.GetSelectedCommitIndex();
  bool has_commit = ci >= 0 && ci < static_cast<int>(commits.size());
  bool can_diff = has_commit || is_unstaged || is_staged;
  bool multi_selected = is_multi && multi_selection_.Size > 1;
  // A merge commit is compared against a parent picked from a
  // submenu, any other commit against its only parent.
  std::vector<std::string> parents;
  if (has_commit)
    parents = GetCommitParents(commits[ci].commit);
  bool is_merge = parents.size() > 1;

  // |parent| is 1-based, 0 for a commit with no parents.
  auto external_diff = [&](int parent) {
    std::vector<std::string> args = {"difftool", "--no-prompt"};
    if (settings_.diff_tool[0])
      args.push_back(std::string("--tool=") + settings_.diff_tool);
    if (is_staged)
      args.emplace_back("--cached");
    else if (!is_unstaged) {
      // SHA-1 of the empty tree, used as parent for root commits.
      args.push_back(parent > 0
                         ? commits[ci].commit + "^" + std::to_string(parent)
                         : "4b825dc642cb6eb9a060e54bf8d69288fbee4904");
      args.push_back(commits[ci].commit);
    }
    args.emplace_back("--");
    args.emplace_back(is_unstaged || is_staged ? entry.path
                                               : ":/" + entry.path);
    runner_.Spawn(std::move(args));
  };
  auto visual_diff = [&](int parent) {
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
      // In a merge, a file added relative to the first parent can
      // still exist in the others. A missing one shows as no image.
      old_ref = (entry.status == FileStatus::kAdded && !is_merge) || parent == 0
                    ? ""
                    : commits[ci].commit + "^" + std::to_string(parent);
      new_ref = entry.status == FileStatus::kDeleted ? "" : commits[ci].commit;
    }
    delegate_.OnShowImageDiff(old_ref, new_ref, entry.path, new_from_worktree);
  };
  int only_parent =
      !parents.empty() || (has_commit && !commits[ci].parents.empty()) ? 1 : 0;

  if (is_merge) {
    if (ImGui::BeginMenu("External diff", !multi_selected)) {
      if (int parent = ParentMenuItems(parents); parent > 0)
        external_diff(parent);
      ImGui::EndMenu();
    }
  } else if (ImGui::MenuItem("External diff", nullptr, false,
                             can_diff && !multi_selected)) {
    external_diff(only_parent);
  }
  if (IsImageFile(entry.path)) {
    if (is_merge) {
      if (ImGui::BeginMenu("Visual diff", !multi_selected)) {
        if (int parent = ParentMenuItems(parents); parent > 0)
          visual_diff(parent);
        ImGui::EndMenu();
      }
    } else if (ImGui::MenuItem("Visual diff", nullptr, false,
                               can_diff && !multi_selected)) {
      visual_diff(only_parent);
    }
  }
  if (ImGui::MenuItem(multi_selected ? "Search commits touching paths"
                                     : "Search commits touching path")) {
    if (multi_selected)
      delegate_.OnSearchTouchingPaths(GetSelectedPaths());
    else
      delegate_.OnSearchTouchingPaths({entry.path});
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
        multi_selection_.SetItemSelected(static_cast<ImGuiID>(si), true);
    }
  }
  if (is_unstaged) {
    auto selected_paths = GetSelectedPaths();
    bool busy = runner_.IsCommandBusy();
    ImGui::Separator();
    if (ImGui::MenuItem("Stage to Commit", "Ctrl+T", false, !busy))
      runner_.Run(BuildGitArgs("add", selected_paths), RefreshScope::kLocal);
    bool plural = selected_paths.size() > 1;
    if (ImGui::MenuItem(plural ? "Revert Changes" : "Revert Change", "Ctrl+J",
                        false, !busy))
      PrepareRevert();
  }
  if (is_staged) {
    auto selected_paths = GetSelectedPaths(true);
    bool busy = runner_.IsCommandBusy();
    ImGui::Separator();
    if (ImGui::MenuItem("Unstage from Commit", "Ctrl+U", false, !busy))
      runner_.Run(BuildRestoreArgs("--staged", selected_paths),
                  RefreshScope::kLocal);
  }
}

void FileList::RenderFileRow(std::span<const FileEntry> files,
                             int index,
                             int row,
                             float text_offset,
                             const char* label,
                             bool is_multi,
                             bool is_unstaged,
                             bool is_staged) {
  const FileEntry& entry = files[index];

  ImGui::PushID(index);

  if (is_multi)
    ImGui::SetNextItemSelectionUserData(row);

  ImVec2 cursor = ImGui::GetCursorScreenPos();
  bool item_selected =
      is_multi ? multi_selection_.Contains(static_cast<ImGuiID>(index))
               : selected_file_ == index;
  if (ImGui::Selectable("##file", item_selected,
                        ImGuiSelectableFlags_AllowOverlap)) {
    selected_file_ = index;
    clicked_file_ = index;
  }
  bool is_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);

  // Right-click context menu for file entries.
  if (entry.status != FileStatus::kCommit && ImGui::BeginPopupContextItem()) {
    RenderFileContextMenu(files, index, is_multi, is_unstaged, is_staged);
    ImGui::EndPopup();
  }

  // Render the status letter and filename on top of the selectable.
  ImGui::SetCursorScreenPos(cursor);
  float available_width = ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorScreenPos(ImVec2(cursor.x + text_offset, cursor.y));

  if (entry.status == FileStatus::kCommit) {
    ImGui::TextUnformatted(label);
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

    ImGui::TextUnformatted(label);
  }

  if (is_hovered) {
    float content_width = text_offset + ImGui::CalcTextSize(label).x;
    if (entry.status != FileStatus::kCommit)
      content_width +=
          ImGui::CalcTextSize("M").x + ImGui::GetStyle().ItemSpacing.x;
    if (content_width > available_width)
      ImGui::SetTooltip("%s", entry.path.c_str());
  }

  ImGui::PopID();
}

void FileList::Update(float width) {
  bool tree = settings_.file_list_tree;
  // Separate windows, so that ImGui's per-window multi-selection state (e.g.
  // the shift+click range anchor, which is a row index) isn't shared between
  // the two views.
  if (ImGui::BeginChild(tree ? "file_tree" : "file_list",
                        ImVec2(width, -FLT_MIN), ImGuiChildFlags_Borders,
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

    if (tree)
      SyncTree(files.first(file_count_));
    auto tree_rows = tree_.rows();
    int row_count = tree ? static_cast<int>(tree_rows.size()) : file_count_;

    // The row showing the selected file. In the tree view, this is the
    // collapsed directory the file is hidden in, if any.
    int selected_row = -1;
    if (selected_file_ >= 0 && selected_file_ < file_count_)
      selected_row = tree ? tree_.RowOfItem(selected_file_) : selected_file_;

    ImGuiMultiSelectIO* ms_io = nullptr;
    if (is_multi) {
      ImGuiMultiSelectFlags flags = ImGuiMultiSelectFlags_ClearOnEscape |
                                    ImGuiMultiSelectFlags_BoxSelect1d;
      ms_io = ImGui::BeginMultiSelect(flags, multi_selection_.Size, row_count);
      ApplySelectionRequests(ms_io, tree);
    }

    ImGuiListClipper clipper;
    clipper.Begin(row_count);

    // Ensure the selected row is rendered so we can scroll to it.
    if (scroll_file_list_ && selected_row >= 0 && selected_row < row_count)
      clipper.IncludeItemByIndex(selected_row);
    if (is_multi && ms_io) {
      int src = static_cast<int>(ms_io->RangeSrcItem);
      if (src >= 0 && src < row_count)
        clipper.IncludeItemByIndex(src);
    }

    while (clipper.Step()) {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
        if (!tree) {
          if (row >= static_cast<int>(files.size()))
            break;
          RenderFileRow(files, row, row, 0.0f, files[row].path.c_str(),
                        is_multi, is_unstaged, is_staged);
        } else if (int item = tree_rows[row].item; item >= 0) {
          RenderFileRow(files, item, row,
                        PathTree::ItemTextOffset(tree_rows[row].depth),
                        PathTree::ItemName(files[item].path), is_multi,
                        is_unstaged, is_staged);
        } else {
          // Not a multi-select item, so clicking it leaves the file selection
          // alone.
          tree_.RenderDirRow(tree_rows[row], row == selected_row);
        }

        // Keep the auto-selected file visible when scrolling through diff
        // content.
        if (row == selected_row && scroll_file_list_) {
          ImGui::SetScrollHereY();
          scroll_file_list_ = false;
        }
      }
    }

    if (is_multi) {
      ms_io = ImGui::EndMultiSelect();
      ApplySelectionRequests(ms_io, tree);
    }

    // Apply directories expanded or collapsed this frame.
    if (tree)
      tree_.FlattenIfDirty();

    bool cmd_busy = runner_.IsCommandBusy();
    if (!ImGui::GetTopMostPopupModal() && !cmd_busy && is_unstaged &&
        ImGui::GetIO().KeyCtrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        auto paths = GetSelectedPaths();
        if (!paths.empty())
          runner_.Run(BuildGitArgs("add", paths), RefreshScope::kLocal);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_J, false))
        PrepareRevert();
    }
    if (!ImGui::GetTopMostPopupModal() && !cmd_busy && is_staged &&
        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_U, false)) {
      auto paths = GetSelectedPaths(true);
      if (!paths.empty())
        runner_.Run(BuildRestoreArgs("--staged", paths), RefreshScope::kLocal);
    }
  }
  ImGui::EndChild();
}
