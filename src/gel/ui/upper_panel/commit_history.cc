// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/upper_panel/commit_history.h"

#include <algorithm>
#include <cmath>

#include "base/utf8.h"
#include "gel/commands/git_log.h"
#include "gel/persistent_settings.h"
#include "gel/ui/commit_search.h"
#include "gel/ui/git_cmd_runner.h"
#include "gel/ui/style.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"
#include "third_party/kaliber/platform/platform.h"

namespace {

// Renders stash, tag and branch labels as individually bordered rectangles.
// The stash label comes first, then tags with a left-pointing arrow shape, then
// branches as rounded rectangles. The active branch (matching |head_branch|) is
// drawn bold. When tags are collapsed and |tooltip_args| is provided, shows a
// tooltip with the full tag list on hover.
struct TagTooltipArgs {
  bool& any_hovered;
  bool tooltip_suppressed;
  ImVec2 window_padding;
};
struct ContextMenuArgs {
  CommitContextMenu& menu;
  const std::string& commit;
  ImVec2 window_padding;
  ImVec2 item_spacing;
};
enum class LabelKind { kTag, kBranch, kStash };

void RenderBranchLabels(const std::vector<std::string>& tags,
                        const std::vector<GitLog::BranchInfo>& branches,
                        bool is_stash,
                        const std::string& head_branch,
                        TagTooltipArgs* tooltip_args = nullptr,
                        ContextMenuArgs* ctx_args = nullptr) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float font_size = ImGui::GetFontSize();
  float pad_x = font_size * 0.3f;
  float pad_y = font_size * 0.05f;
  float rounding = font_size * 0.2f;
  float spacing = font_size * 0.3f;

  auto render_label = [&](const std::string& name, LabelKind kind) {
    bool is_tag = kind == LabelKind::kTag;
    bool is_head = kind == LabelKind::kBranch && (name == head_branch);

    ImVec2 text_size = ImGui::CalcTextSize(name.c_str());
    ImVec2 cursor_screen = ImGui::GetCursorScreenPos();
    float arrow_w = is_tag ? (text_size.y + pad_y * 2) * 0.5f : 0.0f;
    ImVec2 rect_min(cursor_screen.x + arrow_w, cursor_screen.y + 1.0f);
    ImVec2 rect_max(rect_min.x + text_size.x + pad_x * 2,
                    rect_min.y + text_size.y + pad_y * 2);

    uint32_t bg = GetSyntaxPalette().added_bg;
    if (kind == LabelKind::kTag)
      bg = GetSyntaxPalette().tag_bg;
    else if (kind == LabelKind::kStash)
      bg = GetSyntaxPalette().stash_bg;
    ImVec4 text_col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    text_col.w *= 0.4f;
    uint32_t border = ImGui::GetColorU32(text_col);
    if (is_tag) {
      // Tag shape: rectangle with a left-pointing arrow.
      float mid_y = (rect_min.y + rect_max.y) * 0.5f;
      float tip_x = rect_min.x - arrow_w;
      dl->AddTriangleFilled(ImVec2(tip_x, mid_y), rect_min,
                            ImVec2(rect_min.x, rect_max.y), bg);
      dl->AddRectFilled(rect_min, rect_max, bg);
      dl->AddLine(ImVec2(tip_x, mid_y), rect_min, border);
      dl->AddLine(ImVec2(tip_x, mid_y), ImVec2(rect_min.x, rect_max.y), border);
      dl->AddLine(rect_min, ImVec2(rect_max.x, rect_min.y), border);
      dl->AddLine(ImVec2(rect_max.x, rect_min.y), rect_max, border);
      dl->AddLine(rect_max, ImVec2(rect_min.x, rect_max.y), border);
    } else {
      dl->AddRectFilled(rect_min, rect_max, bg, rounding);
      dl->AddRect(rect_min, rect_max, border, rounding);
    }

    ImVec2 text_pos(rect_min.x + pad_x, rect_min.y + pad_y);
    ImGui::SetCursorScreenPos(text_pos);
    ImGui::TextUnformatted(name.c_str());
    if (is_head) {
      // Faux bold: render text again with 1px offset.
      ImGui::SameLine(0, 0);
      ImGui::SetCursorScreenPos(ImVec2(text_pos.x + 1.0f, text_pos.y));
      ImGui::TextUnformatted(name.c_str());
    }

    ImGui::SameLine(0, 0);
    ImGui::SetCursorScreenPos(ImVec2(rect_max.x + spacing, cursor_screen.y));
  };

  if (is_stash)
    render_label("stash", LabelKind::kStash);

  if (!tags.empty()) {
    size_t total_len = 0;
    for (const auto& name : tags)
      total_len += name.size();
    if (total_len > 15) {
      if (tags.size() > 1) {
        render_label(std::to_string(tags.size()) + " tags", LabelKind::kTag);
      } else {
        std::string truncated =
            tags[0].substr(0, base::Utf8SafePrefixLength(tags[0], 13)) + "..";
        render_label(truncated, LabelKind::kTag);
      }
      if (ctx_args)
        ctx_args->menu.RenderCollapsedTagMenu(tags, ctx_args->commit,
                                              ctx_args->window_padding,
                                              ctx_args->item_spacing);
      if (tooltip_args) {
        std::string tooltip;
        for (size_t i = 0; i < tags.size(); ++i) {
          if (i > 0)
            tooltip += '\n';
          tooltip += tags[i];
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            tooltip_args->window_padding);
        ItemTooltip(tooltip.c_str(), tooltip_args->any_hovered,
                    tooltip_args->tooltip_suppressed);
        ImGui::PopStyleVar();
      }
    } else {
      int tag_idx = 0;
      for (const auto& name : tags) {
        render_label(name, LabelKind::kTag);
        if (ctx_args)
          ctx_args->menu.RenderTagMenu(tag_idx++, name, ctx_args->commit,
                                       ctx_args->window_padding,
                                       ctx_args->item_spacing);
      }
    }
  }
  int branch_idx = 0;
  for (const auto& bi : branches) {
    render_label(bi.name, LabelKind::kBranch);
    if (ctx_args)
      ctx_args->menu.RenderBranchMenu(branch_idx++, bi.name, bi.is_remote,
                                      head_branch, ctx_args->window_padding,
                                      ctx_args->item_spacing);
  }

  // Extra space between the last label and the commit message.
  ImVec2 cur = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(ImVec2(cur.x + spacing, cur.y));
}

}  // namespace

CommitHistory::CommitHistory(
    Delegate& delegate,
    GitLog& git_log,
    GitCmdRunner& runner,
    GitRepo& git_repo,
    PersistentSettings& settings,
    std::function<void(const std::string&)> open_in_new_window)
    : delegate_(delegate),
      git_log_(git_log),
      runner_(runner),
      context_menu_(runner, std::move(open_in_new_window)),
      settings_(settings),
      commit_modal_(runner, git_repo) {}

int CommitHistory::GetSelectedCommitIndex() const {
  if (selected_row_ == -1 || unstaged_selected_ || staged_selected_)
    return -1;
  return selected_row_ - SyntheticRows();
}

void CommitHistory::SelectCommit(int row, bool skip_record, bool user_click) {
  bool new_unstaged = false;
  bool new_staged = false;
  std::string new_commit;

  if (row >= 0) {
    if (has_unstaged_changes_ && row == 0) {
      new_unstaged = true;
    } else if (has_staged_changes_ && row == (has_unstaged_changes_ ? 1 : 0)) {
      new_staged = true;
    } else {
      int ci = std::min(row - SyntheticRows(),
                        static_cast<int>(git_log_.GetCommits().size()) - 1);
      if (ci < 0) {
        row = -1;
      } else {
        new_commit = git_log_.GetCommits()[ci].commit;
        row = ci + SyntheticRows();
      }
    }
  }

  bool same_row = (selected_row_ == row);
  bool selection_changed =
      (new_commit != selected_commit_ || new_unstaged != unstaged_selected_ ||
       new_staged != staged_selected_);

  selected_row_ = row;
  unstaged_selected_ = new_unstaged;
  staged_selected_ = new_staged;
  selected_commit_ = std::move(new_commit);

  if (selection_changed) {
    scroll_to_commit_.clear();
    scroll_to_row_ = -1;
    RecordSelection(skip_record);
    delegate_.OnCommitSelected(selected_commit_, false, restoring_selection_);
  } else if (same_row) {
    scroll_to_commit_.clear();
    scroll_to_row_ = -1;
    delegate_.OnCommitSelected(selected_commit_, user_click,
                               restoring_selection_);
  }
}

void CommitHistory::ScrollToSelected() {
  scroll_to_commit_ = selected_commit_;
  scroll_search_offset_ = 0;
  scroll_to_row_ = -1;
}

void CommitHistory::ResetGraph() {
  graph_.Reset();
}

void CommitHistory::HandleLocalStatusUpdate(bool has_unstaged,
                                            bool has_staged) {
  if (has_unstaged_changes_ == has_unstaged &&
      has_staged_changes_ == has_staged)
    return;

  int old_synthetic = SyntheticRows();
  has_unstaged_changes_ = has_unstaged;
  has_staged_changes_ = has_staged;
  int new_synthetic = SyntheticRows();

  if (!scroll_to_commit_.empty())
    return;

  if (unstaged_selected_ && !has_unstaged) {
    if (new_synthetic > 0)
      SelectCommit(0);
    else if (!git_log_.GetCommits().empty())
      SelectCommit(0);
    else
      SelectCommit(-1);
  } else if (staged_selected_ && !has_staged) {
    if (new_synthetic > 0)
      SelectCommit(0);
    else if (!git_log_.GetCommits().empty())
      SelectCommit(0);
    else
      SelectCommit(-1);
  } else if (unstaged_selected_) {
    // Unstaged is always row 0; no adjustment needed.
  } else if (selected_row_ >= 0) {
    SelectCommit(selected_row_ + new_synthetic - old_synthetic, true);
  }
}

void CommitHistory::AutoSelectFirst() {
  if (selected_row_ == -1 && !git_log_.GetCommits().empty())
    SelectCommit(SyntheticRows());
}

void CommitHistory::ClearSelectionHistory() {
  selection_history_.clear();
  history_index_ = -1;
  if (selected_row_ >= 0)
    RecordSelection();
}

CommitHistory::HistoryEntry CommitHistory::CurrentHistoryEntry() const {
  if (unstaged_selected_)
    return {HistoryEntry::Kind::kUnstaged, -1};
  if (staged_selected_)
    return {HistoryEntry::Kind::kStaged, -1};
  return {HistoryEntry::Kind::kCommit, GetSelectedCommitIndex()};
}

void CommitHistory::RecordSelection(bool skip) {
  if (skip)
    return;

  HistoryEntry entry = CurrentHistoryEntry();

  // Don't push duplicates.
  if (history_index_ >= 0 && history_index_ < (int)selection_history_.size()) {
    if (selection_history_[history_index_] == entry)
      return;
  }

  // Truncate redo portion.
  selection_history_.resize(history_index_ + 1);
  selection_history_.push_back(entry);
  history_index_ = (int)selection_history_.size() - 1;
}

bool CommitHistory::RestoreSelection(int history_index) {
  const HistoryEntry& entry = selection_history_[history_index];
  int row = -1;
  switch (entry.kind) {
    case HistoryEntry::Kind::kUnstaged:
      if (!has_unstaged_changes_)
        return false;
      row = 0;
      break;
    case HistoryEntry::Kind::kStaged:
      if (!has_staged_changes_)
        return false;
      row = has_unstaged_changes_ ? 1 : 0;
      break;
    case HistoryEntry::Kind::kCommit:
      if (entry.commit_index < 0 ||
          entry.commit_index >= (int)git_log_.GetCommits().size())
        return false;
      row = entry.commit_index + SyntheticRows();
      break;
  }
  restoring_selection_ = true;
  SelectCommit(row, true);
  restoring_selection_ = false;
  history_index_ = history_index;
  return true;
}

void CommitHistory::ResetScrollPos() {
  reset_scroll_pos_ = true;
}

void CommitHistory::Update(const std::string& search_term) {
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    tag_tooltip_suppressed_ = true;
  bool any_tag_hovered = false;
  bool reset_scroll_pos = reset_scroll_pos_;
  reset_scroll_pos_ = false;

  // Compact table styling: zero window padding so the table's scrollbar sits
  // flush against the right edge, then indent to keep left padding.
  ImVec2 default_window_padding = ImGui::GetStyle().WindowPadding;
  float left_padding = 1.0f + default_window_padding.x / 2.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (ImGui::BeginChild("upper_part",
                        ImVec2(-FLT_MIN, settings_.upper_panel_height),
                        ImGuiChildFlags_ResizeY | ImGuiChildFlags_Borders)) {
    settings_.upper_panel_height = ImGui::GetWindowSize().y;
    ImVec2 default_item_spacing = ImGui::GetStyle().ItemSpacing;
    ImVec2 default_cell_padding = ImGui::GetStyle().CellPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
    int total_columns = 1 + static_cast<int>(settings_.table_columns.size());
    ImGui::Indent(left_padding);
    if (ImGui::BeginTable("table_scrolly", total_columns,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_NoSavedSettings |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_PadOuterX)) {
      // Row height must satisfy two constraints:
      // 1. >= text height, so the table doesn't expand rows beyond the
      //    clipper's item height.
      // 2. Even integer, so that n*row_height is always representable as a
      //    float when total content exceeds 2^24 pixels (~1M rows).  At
      //    that range the float ULP is 2, meaning only even values are
      //    representable.  An odd row_height makes odd-n products
      //    unrepresentable, causing the clipper's seek to alternate ±1
      //    pixel and making rows appear to change height every other step.
      float row_height =
          2.0f * std::ceil((ImGui::GetTextLineHeight() + 2.0f) * 0.5f);

      float lane_spacing = row_height * 0.75f;
      for (int c = 0; c < total_columns; c++) {
        float w = c < static_cast<int>(settings_.table_column_widths.size())
                      ? settings_.table_column_widths[c]
                      : 0.0f;
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, w);
      }

      // Snap the scroll position to the nearest row boundary.  With very
      // large item counts (>1M) the product row*height exceeds 2^24 and
      // consecutive float values differ by ±2, so the sub-pixel offset of
      // the first visible row shifts erratically, making row heights appear
      // uneven.  Snapping to a row boundary (computed in double) keeps the
      // first row aligned to the window top on every frame.  This must run
      // before keyboard/programmatic scroll so those can override the target.
      if (!reset_scroll_pos) {
        float scroll_y = ImGui::GetScrollY();
        float max_scroll = ImGui::GetScrollMaxY();
        double d = std::round((double)scroll_y / (double)row_height);
        float snapped = (float)(d * (double)row_height);
        // When already at the bottom or when the snapped value would exceed
        // max, stay at max.  GetScrollMaxY() is generally not row-aligned,
        // so normal snapping would pull the scroll away from the bottom
        // every frame, causing a visible flicker when the user tries to
        // scroll past the end.
        if (scroll_y >= max_scroll || snapped > max_scroll)
          snapped = max_scroll;
        ImGui::SetScrollY(snapped);
      } else {
        ImGui::SetScrollY(0);
      }

      auto commit_history = git_log_.GetCommits();

      graph_.Update(commit_history);

      // GetWindowScrollbarID() can't be used here because BeginTable() pushes
      // an override ID onto the inner window's ID stack, changing the seed used
      // by GetID(). Compute the scrollbar ID directly with the window's base ID
      // to match what Scrollbar() uses during Begin().
      ImGuiWindow* inner_window = ImGui::GetCurrentTable()->InnerWindow;
      ImGuiID scrollbar_id = ImHashStr("#SCROLLY", 0, inner_window->ID);
      bool scrollbar_active = ImGui::GetActiveID() == scrollbar_id;

      // After a refresh, scan for the previously selected commit and scroll
      // to its new position once it appears in the repopulated list.
      // SetScrollY targets are clamped by the previous frame's content size,
      // so keep applying each frame until the content grows large enough for
      // the scroll to stick. Cancel if the user grabs the scrollbar.
      if (!scroll_to_commit_.empty() && !reset_scroll_pos) {
        if (scrollbar_active) {
          scroll_to_commit_.clear();
        } else if (scroll_search_offset_ < git_log_.GetCommits().size()) {
          auto commits = git_log_.GetCommits();
          auto it = std::find_if(
              commits.begin() + scroll_search_offset_, commits.end(),
              [&](const auto& c) { return c.commit == scroll_to_commit_; });
          scroll_search_offset_ = commits.size();
          if (it != commits.end()) {
            int synthetic_rows = SyntheticRows();
            SelectCommit(static_cast<int>(it - commits.begin()) +
                         synthetic_rows);
            float row_top = selected_row_ * row_height;
            float scroll_y = ImGui::GetScrollY();
            float window_h = ImGui::GetWindowHeight();
            if (row_top >= scroll_y &&
                row_top + row_height <= scroll_y + window_h) {
              // Already visible, no need to scroll.
              scroll_to_commit_.clear();
            } else {
              float target =
                  std::max(0.0f, row_top - (window_h - row_height) * 0.5f);
              ImGui::SetScrollY(target);
              scroll_to_commit_.clear();
            }
          }
        } else {
          // Row already loaded but commit not found yet. Re-select so
          // the diff updates immediately; keep searching in case the
          // original commit appears later in the log.
          int ci = selected_row_ - SyntheticRows();
          if (ci >= 0 && ci < static_cast<int>(git_log_.GetCommits().size()) &&
              git_log_.GetCommits()[ci].commit != selected_commit_) {
            SelectCommit(selected_row_);
          }
          if (!git_log_.busy())
            scroll_to_commit_.clear();
        }
      }

      // Keyboard navigation sets scroll_to_row_ to keep the selected row
      // visible. Like scroll_to_commit_, SetScrollY is clamped by the previous
      // frame's content size, so retry each frame while git log is loading.
      if (scroll_to_row_ >= 0 && !reset_scroll_pos) {
        if (scrollbar_active) {
          scroll_to_row_ = -1;
        } else {
          float target = scroll_to_row_ * row_height;
          float scroll_y = ImGui::GetScrollY();
          float window_h = ImGui::GetWindowHeight();
          if (target < scroll_y)
            ImGui::SetScrollY(target);
          else if (target + row_height > scroll_y + window_h) {
            if (scroll_y >= ImGui::GetScrollMaxY())
              scroll_to_row_ = -1;  // Already at bottom, can't go further.
            else
              ImGui::SetScrollY(scroll_to_row_ >= commit_count_ - 1
                                    ? ImGui::GetScrollMaxY()
                                    : target + row_height - window_h);
          } else
            scroll_to_row_ = -1;
        }
      }

      // Freeze the item count while the scrollbar is held. Inserting new items
      // mid-drag causes the scroll position to jump because the scrollbar
      // ratio changes.  Always clamp to the actual size to avoid out-of-bounds
      // access when the content is cleared (e.g. F5 refresh).
      int synthetic_rows = SyntheticRows();
      int actual_count =
          static_cast<int>(commit_history.size()) + synthetic_rows;
      if (!scrollbar_active)
        commit_count_ = actual_count;
      else
        commit_count_ = std::min(commit_count_, actual_count);

      // Use a list clipper to only submit the visible rows.
      // Synthetic rows (unstaged/staged changes) come first, followed by real
      // commits. The number of synthetic rows depends on which types of local
      // changes exist.
      int staged_row = has_unstaged_changes_ ? 1 : 0;

      // Handle keyboard navigation. Home/End/Left/Right are skipped when a
      // text input has focus so the input field can handle them.
      // Skip entirely when a modal popup is open so the modal captures all
      // keyboard input.
      if (commit_count_ > 0 && !ImGui::GetTopMostPopupModal() &&
          ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        bool ctrl = ImGui::GetIO().KeyCtrl;
        bool shift = ImGui::GetIO().KeyShift;
        bool text_input = ImGui::GetIO().WantTextInput;

        // Ctrl+navigation keys: scroll without changing selection.
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                     ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                     ImGui::IsKeyPressed(ImGuiKey_PageUp) ||
                     ImGui::IsKeyPressed(ImGuiKey_PageDown) ||
                     (!text_input && (ImGui::IsKeyPressed(ImGuiKey_Home) ||
                                      ImGui::IsKeyPressed(ImGuiKey_End))))) {
          float scroll_y = ImGui::GetScrollY();
          float max_scroll = ImGui::GetScrollMaxY();
          if (ImGui::IsKeyPressed(ImGuiKey_Home))
            ImGui::SetScrollY(0);
          else if (ImGui::IsKeyPressed(ImGuiKey_End))
            ImGui::SetScrollY(max_scroll);
          else {
            float delta = row_height;
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp) ||
                ImGui::IsKeyPressed(ImGuiKey_PageDown))
              delta =
                  std::max(row_height, ImGui::GetWindowHeight() - row_height);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                ImGui::IsKeyPressed(ImGuiKey_PageUp))
              ImGui::SetScrollY(std::max(0.0f, scroll_y - delta));
            else
              ImGui::SetScrollY(std::min(max_scroll, scroll_y + delta));
          }
        }

        int new_row = -1;
        if (!ctrl && !text_input && ImGui::IsKeyPressed(ImGuiKey_Home)) {
          new_row = 0;
        } else if (!ctrl && !text_input && ImGui::IsKeyPressed(ImGuiKey_End)) {
          new_row = commit_count_ - 1;
        } else if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
          int page = std::max(
              1, static_cast<int>(ImGui::GetWindowHeight() / row_height) - 1);
          new_row = std::max(0, (selected_row_ < 0 ? 0 : selected_row_) - page);
        } else if (!ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
          new_row = std::max(0, (selected_row_ < 0 ? 0 : selected_row_) - 1);
        } else if (!ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
          new_row = std::min(commit_count_ - 1,
                             (selected_row_ < 0 ? 0 : selected_row_) + 1);
        } else if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
          int page = std::max(
              1, static_cast<int>(ImGui::GetWindowHeight() / row_height) - 1);
          new_row = std::min(commit_count_ - 1,
                             (selected_row_ < 0 ? 0 : selected_row_) + page);
        }

        if (new_row >= 0 && new_row != selected_row_) {
          SelectCommit(new_row);
          scroll_to_row_ = selected_row_;
        }

        // Left/Right: navigate selection history.
        if (!ctrl && !text_input && ImGui::IsKeyPressed(ImGuiKey_LeftArrow) &&
            history_index_ > 0) {
          for (int i = history_index_ - 1; i >= 0; i--) {
            if (RestoreSelection(i)) {
              scroll_to_row_ = selected_row_;
              break;
            }
          }
        } else if (!ctrl && !text_input &&
                   ImGui::IsKeyPressed(ImGuiKey_RightArrow) &&
                   history_index_ >= 0 &&
                   history_index_ < (int)selection_history_.size() - 1) {
          for (int i = history_index_ + 1; i < (int)selection_history_.size();
               i++) {
            if (RestoreSelection(i)) {
              scroll_to_row_ = selected_row_;
              break;
            }
          }
        }
      }

      std::string head_branch = git_log_.head_branch();
      // Built once per frame: the rows below all match the same term.
      base::SearchTerm search_needle(search_term, settings_.search_options);

      ImGuiListClipper clipper;
      clipper.Begin(commit_count_, row_height);
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          ImGui::PushID(row);
          ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);

          if (has_unstaged_changes_ && row == 0) {
            // "Unstaged changes" synthetic row.
            ImGui::TableSetColumnIndex(0);
            ImVec2 cursor = ImGui::GetCursorPos();
            if (ImGui::Selectable("##row", selected_row_ == 0,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, row_height))) {
              SelectCommit(0, false, true);
            }
            {
              ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                  default_window_padding);
              ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                  default_item_spacing);
              if (ImGui::BeginPopupContextItem()) {
                if (selected_row_ != 0)
                  SelectCommit(0);
                bool busy = runner_.IsCommandBusy();
                if (ImGui::MenuItem("Stage All Files to Commit", nullptr, false,
                                    !busy))
                  runner_.Run({"add", "-A"}, true);
                if (ImGui::MenuItem("Revert All Files", nullptr, false, !busy))
                  delegate_.OnRevertAllRequested();
                ImGui::EndPopup();
              }
              ImGui::PopStyleVar(2);
            }
            ImGui::SetCursorPos(cursor);
            ImGui::TextUnformatted("Unstaged changes");
          } else if (has_staged_changes_ && row == staged_row) {
            // "Staged changes" synthetic row.
            ImGui::TableSetColumnIndex(0);
            ImVec2 cursor = ImGui::GetCursorPos();
            if (ImGui::Selectable("##row", selected_row_ == row,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, row_height))) {
              SelectCommit(row, false, true);
            }
            {
              ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                  default_window_padding);
              ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                  default_item_spacing);
              if (ImGui::BeginPopupContextItem()) {
                if (selected_row_ != row)
                  SelectCommit(row);
                bool busy = runner_.IsCommandBusy();
                if (ImGui::MenuItem("Unstage All Files from Commit", nullptr,
                                    false, !busy))
                  runner_.Run({"reset", "HEAD"}, true);
                if (ImGui::MenuItem("Commit", nullptr, false, !busy)) {
                  commit_modal_.Open(false);
                }
                ImGui::EndPopup();
              }
              ImGui::PopStyleVar(2);
            }
            ImGui::SetCursorPos(cursor);
            ImGui::TextUnformatted("Staged changes");
          } else {
            int ci = row - synthetic_rows;

            // Column 0: graph + commit message.
            // An invisible Selectable spanning all columns provides row
            // selection highlighting. SetCursorPos restores the cursor so the
            // colored text renders on top of the selectable background.
            ImGui::TableSetColumnIndex(0);
            ImVec2 graph_origin = ImGui::GetCursorScreenPos();
            ImVec2 cursor = ImGui::GetCursorPos();
            if (ImGui::Selectable("##row", selected_row_ == row,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, row_height))) {
              SelectCommit(row, false, true);
            }

            // Right-click context menu for commit rows.
            if (context_menu_.RenderCommitMenu(
                    commit_history[ci].commit,
                    commit_history[ci].message.empty()
                        ? std::string()
                        : commit_history[ci].message[0],
                    default_window_padding, default_item_spacing))
              SelectCommit(row);

            ImGui::SetCursorPos(cursor);
            float text_offset =
                graph_.RowColumns(ci) * lane_spacing + lane_spacing * 0.5f;

            // Draw graph on top of the highlight.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            graph_.Draw(dl, ci, graph_origin, row_height, lane_spacing);

            // Advance cursor past the graph for this row's text.
            ImGui::SetCursorPosX(cursor.x + text_offset);

            bool flagged =
                CommitMatchesSearch(commit_history[ci], search_needle);

            TagTooltipArgs tooltip_args{any_tag_hovered,
                                        tag_tooltip_suppressed_,
                                        default_window_padding};
            ContextMenuArgs ctx_args{context_menu_, commit_history[ci].commit,
                                     default_window_padding,
                                     default_item_spacing};
            bool has_labels = !commit_history[ci].tags.empty() ||
                              !commit_history[ci].branches.empty() ||
                              commit_history[ci].is_stash;
            if (flagged) {
              ImGui::SetCursorPos(ImVec2(cursor.x + text_offset + 1, cursor.y));
              if (has_labels) {
                RenderBranchLabels(commit_history[ci].tags,
                                   commit_history[ci].branches,
                                   commit_history[ci].is_stash, head_branch);
              }
              ImGui::TextUnformatted(commit_history[ci].message[0].c_str());
              ImGui::SetCursorPos(ImVec2(cursor.x + text_offset, cursor.y));
              if (has_labels) {
                RenderBranchLabels(commit_history[ci].tags,
                                   commit_history[ci].branches,
                                   commit_history[ci].is_stash, head_branch,
                                   &tooltip_args, &ctx_args);
              }
              ImGui::TextUnformatted(commit_history[ci].message[0].c_str());
            } else {
              if (has_labels) {
                RenderBranchLabels(commit_history[ci].tags,
                                   commit_history[ci].branches,
                                   commit_history[ci].is_stash, head_branch,
                                   &tooltip_args, &ctx_args);
              }
              ImGui::TextUnformatted(commit_history[ci].message[0].c_str());
            }

            // Data columns.  Use total_columns captured before BeginTable
            // so the loop never exceeds the table's column count.
            for (int col = 0;
                 col < total_columns - 1 &&
                 col < static_cast<int>(settings_.table_columns.size());
                 col++) {
              ImGui::TableSetColumnIndex(col + 1);
              ImVec2 cp = ImGui::GetCursorPos();
              auto data = settings_.table_columns[col];
              switch (data) {
                case ColumnData::kAuthor:
                  if (flagged) {
                    ImGui::SetCursorPos(
                        ImVec2(cp.x + default_cell_padding.x + 1, cp.y));
                    ImGui::TextUnformatted(commit_history[ci].author.c_str());
                  }
                  ImGui::SetCursorPos(
                      ImVec2(cp.x + default_cell_padding.x, cp.y));
                  ImGui::TextUnformatted(commit_history[ci].author.c_str());
                  break;
                case ColumnData::kAuthorDate:
                  if (flagged) {
                    ImGui::SetCursorPos(
                        ImVec2(cp.x + default_cell_padding.x + 1, cp.y));
                    ImGui::TextUnformatted(
                        commit_history[ci].author_date.c_str());
                  }
                  ImGui::SetCursorPos(
                      ImVec2(cp.x + default_cell_padding.x, cp.y));
                  ImGui::TextUnformatted(
                      commit_history[ci].author_date.c_str());
                  break;
                case ColumnData::kCommit:
                  ImGui::SetCursorPos(
                      ImVec2(cp.x + default_cell_padding.x, cp.y));
                  ImGui::TextUnformatted(commit_history[ci].commit.c_str());
                  break;
                case ColumnData::kCommitter:
                  ImGui::SetCursorPos(
                      ImVec2(cp.x + default_cell_padding.x, cp.y));
                  ImGui::TextUnformatted(commit_history[ci].committer.c_str());
                  break;
                case ColumnData::kCommitterDate:
                  ImGui::SetCursorPos(
                      ImVec2(cp.x + default_cell_padding.x, cp.y));
                  ImGui::TextUnformatted(
                      commit_history[ci].committer_date.c_str());
                  break;
                case ColumnData::kCount:
                  break;
              }
            }
          }

          ImGui::PopID();
        }
      }

      bool is_loading = commit_history.empty() && git_log_.busy();
      if (is_loading || !info_message_.empty()) {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
        ImGui::TableSetColumnIndex(0);
        if (is_loading)
          ImGui::TextDisabled("Loading...");
        else
          ImGui::TextDisabled("%s", info_message_.c_str());
      }

      // Track current column stretch weights after layout is computed.
      {
        ImGuiTable* table = ImGui::GetCurrentTable();
        settings_.table_column_widths.resize(total_columns);
        for (int c = 0; c < total_columns; c++)
          settings_.table_column_widths[c] = table->Columns[c].StretchWeight;
      }
      ImGui::EndTable();
    }
    ImGui::Unindent(left_padding);
    ImGui::PopStyleVar(2);
  }

  if (!any_tag_hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    tag_tooltip_suppressed_ = false;

  ImGui::EndChild();
  ImGui::PopStyleVar();

  commit_modal_.Render();
}
