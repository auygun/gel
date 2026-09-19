// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/diff_content.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "gel/commands/git_diff.h"
#include "gel/persistent_settings.h"
#include "gel/ui/style.h"
#include "gel/ui/syntax_highlight.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"

namespace {

// Binary search for the last file whose start_line <= line.
int FindFileForLine(std::span<const FileEntry> files, int line) {
  auto it = std::upper_bound(files.begin(), files.end(), line,
                             [](int l, const FileEntry& f) {
                               return l < static_cast<int>(f.start_line);
                             });
  return it != files.begin()
             ? static_cast<int>(std::distance(files.begin(), it) - 1)
             : -1;
}

// Binary search for the last hunk whose start_line <= line.
int FindHunkForLine(std::span<const HunkEntry> hunks, int line) {
  auto it = std::upper_bound(hunks.begin(), hunks.end(), line,
                             [](int l, const HunkEntry& h) {
                               return l < static_cast<int>(h.start_line);
                             });
  return it != hunks.begin()
             ? static_cast<int>(std::distance(hunks.begin(), it) - 1)
             : -1;
}

bool IsConflictStart(const std::string& line) {
  return line.starts_with("++<<<<<<<");
}

bool IsConflictSeparator(const std::string& line) {
  return line.starts_with("++=======");
}

bool IsConflictEnd(const std::string& line) {
  return line.starts_with("++>>>>>>>");
}

bool IsConflictMarker(const std::string& line) {
  return IsConflictStart(line) || IsConflictSeparator(line) ||
         IsConflictEnd(line);
}

struct UrlSpan {
  int start;
  int end;
};

std::vector<UrlSpan> FindUrls(const std::string& line) {
  std::vector<UrlSpan> urls;
  size_t pos = 0;
  while (pos < line.size()) {
    size_t http_pos = line.find("http://", pos);
    size_t https_pos = line.find("https://", pos);
    size_t found;
    if (http_pos != std::string::npos && https_pos != std::string::npos)
      found = std::min(http_pos, https_pos);
    else if (http_pos != std::string::npos)
      found = http_pos;
    else if (https_pos != std::string::npos)
      found = https_pos;
    else
      break;

    size_t end = found;
    while (end < line.size()) {
      unsigned char c = static_cast<unsigned char>(line[end]);
      if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' ||
          c == ':' || c == '/' || c == '?' || c == '#' || c == '[' ||
          c == ']' || c == '@' || c == '!' || c == '$' || c == '&' ||
          c == '\'' || c == '(' || c == ')' || c == '*' || c == '+' ||
          c == ',' || c == ';' || c == '%' || c == '=') {
        end++;
      } else {
        break;
      }
    }

    // Strip trailing punctuation that commonly follows URLs in prose.
    size_t min_end = found + 8;
    while (end > min_end) {
      char c = line[end - 1];
      if (c == '.' || c == ',' || c == ';' || c == ':' || c == '\'' ||
          c == '"' || c == '>') {
        end--;
      } else if (c == ')') {
        int open = 0, close = 0;
        for (size_t i = found; i < end; i++) {
          if (line[i] == '(')
            open++;
          else if (line[i] == ')')
            close++;
        }
        if (close > open)
          end--;
        else
          break;
      } else {
        break;
      }
    }

    if (end > min_end) {
      urls.push_back({static_cast<int>(found), static_cast<int>(end)});
    }
    pos = end;
  }
  return urls;
}

std::string FindUrlAtOffset(const std::string& line, int offset) {
  auto urls = FindUrls(line);
  for (auto& u : urls) {
    if (offset >= u.start && offset < u.end)
      return line.substr(u.start, u.end - u.start);
  }
  return {};
}

void OpenUrl(const std::string& url) {
  auto& pio = ImGui::GetPlatformIO();
  if (pio.Platform_OpenInShellFn)
    pio.Platform_OpenInShellFn(ImGui::GetCurrentContext(), url.c_str());
}

bool IsMainFileHeader(const std::string& line) {
  return !line.empty() && line[0] == '\x01';
}

// Badge info for file status display.
struct BadgeInfo {
  bool has_badge = false;
  float width = 0.0f;
  const char* letter = "M";
  ImU32 color = 0;
};

static BadgeInfo ComputeBadgeInfo(int file_idx,
                                  std::span<const FileEntry> files) {
  BadgeInfo info;
  if (file_idx < 0 || file_idx >= static_cast<int>(files.size()))
    return info;
  auto status = files[file_idx].status;
  if (status == FileStatus::kCommit)
    return info;
  info.has_badge = true;
  info.width = ImGui::GetFontSize() * 1.2f + 4.0f;
  switch (status) {
    case FileStatus::kModified:
      info.letter = "M";
      info.color = ResolveColor(ColorId::kYellow);
      break;
    case FileStatus::kAdded:
      info.letter = "A";
      info.color = ResolveColor(ColorId::kGreen);
      break;
    case FileStatus::kDeleted:
      info.letter = "D";
      info.color = ResolveColor(ColorId::kRed);
      break;
    case FileStatus::kRenamed:
      info.letter = "R";
      info.color = ResolveColor(ColorId::kBlue);
      break;
    case FileStatus::kCopied:
      info.letter = "C";
      info.color = ResolveColor(ColorId::kCyan);
      break;
    case FileStatus::kSubmodule:
      info.letter = "S";
      info.color = ResolveColor(ColorId::kMagenta);
      break;
    case FileStatus::kCommit:
    default:
      break;
  }
  return info;
}

bool IsFileHeader(const std::string& line) {
  return IsMainFileHeader(line) || line.starts_with("Submodule ") ||
         line.starts_with("new file mode") ||
         line.starts_with("deleted file mode") ||
         line.starts_with("rename from ") || line.starts_with("rename to ") ||
         line.starts_with("copy from ") || line.starts_with("copy to ") ||
         line.starts_with("index ") || line.starts_with("similarity index") ||
         line.starts_with("* Unmerged path ");
}

}  // namespace

DiffContent::DiffContent(Delegate& delegate,
                         GitDiff& git_diff,
                         PersistentSettings& settings,
                         std::function<void()> main_thread_busy)
    : delegate_(delegate),
      git_diff_(git_diff),
      settings_(settings),
      text_viewer_(*this, std::move(main_thread_busy)) {}

DiffContent::~DiffContent() = default;

void DiffContent::Reset() {
  text_viewer_.Reset();
  current_lang_ = Language::kNone;
  current_lang_file_idx_ = -1;
  auto_selected_file_ = -1;
  last_top_line_ = 0;

  max_old_line_ = 0;
  max_new_line_ = 0;

  // The content arriving next is what a pending anchor was captured for, and
  // it comes in scrolled to the top. Any progress made against the outgoing
  // content indexes no longer applies.
  if (scroll_anchor_) {
    scroll_anchor_->armed = true;
    scroll_anchor_->expected_top = 0;
    scroll_anchor_->scan_pos = -1;
    scroll_anchor_->resolved_line = -1;
  }
}

void DiffContent::CancelSearch() {
  text_viewer_.CancelSearch();
}

void DiffContent::ScrollToLine(int line) {
  scroll_anchor_.reset();
  ScrollToLineInternal(line);
}

void DiffContent::ScrollToLineInternal(int line) {
  text_viewer_.ScrollToLine(line);
  // The viewer stops reporting the top line until the user scrolls again, so
  // track the requested one to keep scroll anchors accurate.
  last_top_line_ = line;
}

void DiffContent::ScrollToTop() {
  text_viewer_.ScrollToTop();
  auto_selected_file_ = -1;
  last_top_line_ = 0;
  scroll_anchor_.reset();
}

void DiffContent::CaptureScrollAnchor() {
  // Keep the oldest pending anchor: holding down the context +/- button
  // re-runs the diff repeatedly, and capturing again would only record the
  // reset scroll position of the reload that is still in flight.
  if (scroll_anchor_)
    return;
  scroll_anchor_ = MakeScrollAnchor();
}

void DiffContent::SaveScrollPosition() {
  auto anchor = MakeScrollAnchor();
  std::string commit = git_diff_.GetLastRunCommitHash();
  bool unstaged = git_diff_.IsLastRunUnstaged();
  bool staged = git_diff_.IsLastRunStaged();

  // Drop any older entry for this diff. Without an anchor the view sits at the
  // top, which is also what coming back with nothing remembered does.
  std::erase_if(saved_positions_, [&](const ScrollAnchor& a) {
    return a.commit == commit && a.unstaged == unstaged && a.staged == staged;
  });
  if (!anchor)
    return;

  saved_positions_.push_back(std::move(*anchor));
  if (saved_positions_.size() > kMaxSavedPositions)
    saved_positions_.erase(saved_positions_.begin());
}

void DiffContent::RestoreSavedScrollPosition(const std::string& commit,
                                             bool unstaged,
                                             bool staged) {
  auto it = std::find_if(saved_positions_.begin(), saved_positions_.end(),
                         [&](const ScrollAnchor& a) {
                           return a.commit == commit &&
                                  a.unstaged == unstaged && a.staged == staged;
                         });
  if (it == saved_positions_.end())
    return;

  // Keep the entry: navigating back and forth repeatedly returns here again.
  ScrollAnchor anchor = *it;
  anchor.armed = false;
  anchor.scan_pos = -1;
  anchor.resolved_line = -1;
  // The diff being left is still on screen until the re-run clears it.
  anchor.expected_top = last_top_line_;
  scroll_anchor_ = std::move(anchor);
}

std::optional<DiffContent::ScrollAnchor> DiffContent::MakeScrollAnchor() const {
  auto content = git_diff_.GetDiffContent();
  auto files = git_diff_.GetFileList();
  auto line_numbers = git_diff_.GetLineNumbers();
  int top = std::min(last_top_line_, static_cast<int>(content.size()) - 1);
  if (top <= 0)
    return std::nullopt;
  int file_idx = FindFileForLine(files, top);
  if (file_idx < 0)
    return std::nullopt;

  ScrollAnchor anchor;
  anchor.expected_top = top;
  anchor.commit = git_diff_.GetLastRunCommitHash();
  anchor.unstaged = git_diff_.IsLastRunUnstaged();
  anchor.staged = git_diff_.IsLastRunStaged();
  anchor.file_index = file_idx;
  anchor.file = files[file_idx].path;

  int file_start = static_cast<int>(files[file_idx].start_line);
  int file_end = file_idx + 1 < static_cast<int>(files.size())
                     ? static_cast<int>(files[file_idx + 1].start_line)
                     : static_cast<int>(content.size());
  anchor.line_offset = top - file_start;

  // Anchor on a source line number, since those survive a context change.
  // File and hunk headers carry none, so fall back to the nearest numbered
  // line of the same file and remember the distance to it.
  auto numbered = [&](int i) {
    return line_numbers[i].old_line > 0 || line_numbers[i].new_line > 0;
  };
  auto take = [&](int i) {
    anchor.old_line = line_numbers[i].old_line;
    anchor.new_line = line_numbers[i].new_line;
    // Removed lines only exist on the old side, every other line on the new
    // side. This matches how the two counters are advanced while parsing.
    anchor.use_old = !content[i].empty() && content[i][0] == '-';
    anchor.lead = i - top;
  };

  int limit = std::min(file_end, static_cast<int>(line_numbers.size()));
  bool found = false;
  for (int i = top; i < limit; i++) {
    if (i > file_start && IsMainFileHeader(content[i]))
      break;
    if (numbered(i)) {
      take(i);
      found = true;
      break;
    }
  }
  for (int i = std::min(top, limit) - 1; i >= file_start && !found; i--) {
    if (numbered(i)) {
      take(i);
      found = true;
    }
  }

  return anchor;
}

bool DiffContent::TryRestoreScrollAnchor(ScrollAnchor& anchor) {
  // Drop the anchor if the panel moved on to different content.
  if (anchor.commit != git_diff_.GetLastRunCommitHash() ||
      anchor.unstaged != git_diff_.IsLastRunUnstaged() ||
      anchor.staged != git_diff_.IsLastRunStaged())
    return true;

  auto content = git_diff_.GetDiffContent();
  auto files = git_diff_.GetFileList();
  auto line_numbers = git_diff_.GetLineNumbers();

  // The re-run is only over once the worker has been idle across a frame
  // boundary and the last merge added nothing: it can go idle right after a
  // merge, leaving its final lines for the next one. Until then the target
  // line is re-applied every frame, because the viewer measures the content
  // height once per frame and silently clamps a scroll past the end of what it
  // measured. Re-applying tracks the anchor as the content grows under it and
  // lands exactly once the last line is in.
  bool loading = git_diff_.busy() || diff_was_busy_ ||
                 content.size() != last_content_size_;

  int file_count = static_cast<int>(files.size());
  int file_idx = -1;
  if (anchor.file_index >= 0 && anchor.file_index < file_count &&
      files[anchor.file_index].path == anchor.file) {
    file_idx = anchor.file_index;
  } else {
    for (int i = 0; i < file_count; i++) {
      if (files[i].path == anchor.file) {
        file_idx = i;
        break;
      }
    }
  }
  // The file may not have been parsed yet. Give up once the diff is complete:
  // it is gone, e.g. a path filter was applied along the way.
  if (file_idx < 0)
    return !loading;

  auto scroll_to = [&](int line) {
    ScrollToLineInternal(line);
    auto_selected_file_ = file_idx;
    anchor.resolved_line = line;
    return !loading;
  };

  if (anchor.resolved_line >= 0)
    return scroll_to(anchor.resolved_line);

  int file_start = static_cast<int>(files[file_idx].start_line);
  // A file entry is only added to the list once the file after it starts, so
  // the last listed entry is complete but its end is not known from the list
  // while the next file streams in. The scan below stops on the file header
  // that follows it instead.
  bool file_complete = file_idx + 1 < file_count || !loading;
  int file_end = file_idx + 1 < file_count
                     ? static_cast<int>(files[file_idx + 1].start_line)
                     : static_cast<int>(content.size());
  int limit = std::min({file_end, static_cast<int>(content.size()),
                        static_cast<int>(line_numbers.size())});
  auto clamp_to_file = [&](int line) {
    return std::clamp(line, file_start, std::max(file_start, limit - 1));
  };

  int want = anchor.use_old ? anchor.old_line : anchor.new_line;
  if (want <= 0) {
    // No line number to match: the anchor sits in a section without any, such
    // as the commit message or a submodule entry. Those keep their size across
    // a context change, so the offset within the file still points at the same
    // text.
    if (!file_complete)
      return false;
    return scroll_to(clamp_to_file(file_start + anchor.line_offset));
  }

  // Line numbers increase monotonically within a file, so the first line at or
  // past the anchored one is the match, and no later line can be a better one.
  // Resume where the previous frame's scan stopped: the diff is parsed
  // incrementally, and the match can be scrolled to before the rest arrives.
  bool at_section_end = false;
  int i = std::max(anchor.scan_pos, file_start);
  for (; i < limit; i++) {
    if (i > file_start && IsMainFileHeader(content[i])) {
      at_section_end = true;
      break;
    }
    int line =
        anchor.use_old ? line_numbers[i].old_line : line_numbers[i].new_line;
    if (line >= want)
      return scroll_to(clamp_to_file(i - anchor.lead));
  }
  anchor.scan_pos = i;
  if (!file_complete && !at_section_end)
    return false;

  // The anchored line is no longer part of the diff (its context was trimmed
  // away): settle on the end of the file's section.
  return scroll_to(clamp_to_file(i - 1));
}

int DiffContent::ConsumeAutoSelectedFile() {
  int f = auto_selected_file_;
  auto_selected_file_ = -1;
  return f;
}

void DiffContent::HighlightLine(int line) {
  scroll_anchor_.reset();
  text_viewer_.HighlightLine(line);
}

void DiffContent::Update(float width, const std::string& search_term) {
  if (search_term_ != search_term ||
      search_needle_options_ != settings_.search_options) {
    search_term_ = search_term;
    search_needle_options_ = settings_.search_options;
    search_needle_ = base::SearchTerm(search_term_, search_needle_options_);
  }

  // Restore the position captured before a diff re-run as soon as the content
  // covering it has been parsed. Done before the viewer runs so the scroll
  // takes effect on this frame.
  if (scroll_anchor_ && scroll_anchor_->armed &&
      TryRestoreScrollAnchor(*scroll_anchor_))
    scroll_anchor_.reset();

  // Compute commit message end line for search highlighting and URL detection.
  commit_msg_end_ = 0;
  {
    auto files = git_diff_.GetFileList();
    if (!files.empty() && files[0].status == FileStatus::kCommit) {
      commit_msg_end_ =
          files.size() > 1
              ? static_cast<int>(files[1].start_line)
              : static_cast<int>(git_diff_.GetDiffContent().size());
    }
  }

  text_viewer_.Update(width, git_diff_.GetDiffContent(),
                      &settings_.diff_search_options);

  overlay_anchor_ = text_viewer_.content_top_right();

  // Update auto-selected file from scroll position.
  // The user taking over the view outranks restoring an earlier position, both
  // while waiting for the anchored line to arrive and while the restore is
  // still following it through the rest of the re-run.
  if (scroll_anchor_ && text_viewer_.user_moved_view())
    scroll_anchor_.reset();

  int top_line = text_viewer_.GetTopVisibleLine();
  if (top_line >= 0) {
    last_top_line_ = top_line;
    // Same, for a move the viewer does not report as user input.
    if (scroll_anchor_ && top_line != scroll_anchor_->expected_top)
      scroll_anchor_.reset();
    auto files = git_diff_.GetFileList();
    int file_for_line = FindFileForLine(files, top_line);
    if (file_for_line >= 0)
      auto_selected_file_ = file_for_line;
  }

  diff_was_busy_ = git_diff_.busy();
  last_content_size_ = git_diff_.GetDiffContent().size();
}

// --- TextViewer::Delegate implementation ---

void DiffContent::OnTextSelected(const std::string& text) {
  delegate_.SetPrimarySelection(text);
}

void DiffContent::OnRightClick(int line_index, int char_offset) {
  ctx_menu_char_ = char_offset;
  delegate_.OnBlameSearchCancel();
}

bool DiffContent::OnCtrlClick(int line_index, int char_offset) {
  if (line_index >= commit_msg_end_)
    return false;
  auto content = git_diff_.GetDiffContent();
  if (line_index < 0 || line_index >= static_cast<int>(content.size()))
    return false;
  const std::string& line = content[line_index];
  if (line.starts_with("    ")) {
    std::string url = FindUrlAtOffset(line, char_offset);
    if (!url.empty()) {
      OpenUrl(url);
      return true;
    }
    return false;
  }
  // Check for "Parent: <hash>" line.
  if (line.starts_with("Parent: ") && char_offset >= 8) {
    delegate_.OnNavigateToCommit(line.substr(8));
    return true;
  }
  return false;
}

void DiffContent::DrawLineBackground(int line_index,
                                     const std::string& line,
                                     ImDrawList* draw_list,
                                     ImVec2 line_pos,
                                     float line_height,
                                     float window_x,
                                     float window_width) {
  // Draw background for file header lines.
  if (IsFileHeader(line)) {
    draw_list->AddRectFilled(
        ImVec2(window_x, line_pos.y),
        ImVec2(window_x + window_width, line_pos.y + line_height),
        GetSyntaxPalette().file_header_bg);
  }

  // Draw background for conflict regions (O(log n) lookup via precomputed
  // transitions from GitDiff).
  auto transitions = git_diff_.GetConflictTransitions();
  int zone = 0;
  if (!transitions.empty()) {
    // Binary search for the last transition at or before this line.
    auto it = std::upper_bound(
        transitions.begin(), transitions.end(), line_index,
        [](int idx, const ConflictTransition& t) { return idx < t.line; });
    if (it != transitions.begin())
      zone = (--it)->zone;
  }
  if (zone > 0) {
    if (!IsConflictSeparator(line)) {
      auto& sp = GetSyntaxPalette();
      ImU32 bg;
      if (IsConflictStart(line))
        bg = sp.current_header_bg;
      else if (IsConflictEnd(line))
        bg = sp.incoming_header_bg;
      else if (zone == 1)
        bg = sp.current_content_bg;
      else
        bg = sp.incoming_content_bg;
      draw_list->AddRectFilled(
          ImVec2(window_x, line_pos.y),
          ImVec2(window_x + window_width, line_pos.y + line_height), bg);
    }
  }
  // Draw background tint for +/- lines in syntax/background modes.
  else if (settings_.diff_color_mode != DiffColorMode::kAnsi && !line.empty()) {
    if (auto sign = FindDiffSign(line_index, line)) {
      ImU32 bg = *sign == '+' ? GetSyntaxPalette().added_bg
                              : GetSyntaxPalette().removed_bg;
      draw_list->AddRectFilled(
          ImVec2(window_x, line_pos.y),
          ImVec2(window_x + window_width, line_pos.y + line_height), bg);
    }
  }
}

void DiffContent::RenderLine(int line_index, const std::string& line) {
  // Render line numbers for content lines (added, removed, context).
  // Non-content lines get blank space to align with content.
  if (line_index >= 0 &&
      line_index < static_cast<int>(git_diff_.GetLineNumbers().size())) {
    auto ln = git_diff_.GetLineNumbers()[line_index];
    if (ln.old_line > max_old_line_)
      max_old_line_ = ln.old_line;
    if (ln.new_line > max_new_line_)
      max_new_line_ = ln.new_line;
    ImGui::PushStyleColor(ImGuiCol_Text, ResolveColor(ColorId::kTextDisabled));
    if (settings_.show_line_numbers && ShouldShowLineNumbers(line_index)) {
      auto [old_digits, new_digits] = ComputeLineNumberDigits();
      ImVec2 char_size = ImGui::CalcTextSize("0");
      float old_cell = char_size.x * old_digits;
      float new_cell = char_size.x * new_digits;
      float sep_width = char_size.x;
      float total_width = old_cell + sep_width + new_cell;
      const auto& line_numbers = git_diff_.GetLineNumbers();
      auto prev_it = line_index > 0
                         ? std::next(line_numbers.begin(), line_index - 1)
                         : line_numbers.end();
      bool show_old = ln.old_line > 0 && (prev_it == line_numbers.end() ||
                                          ln.old_line != prev_it->old_line);
      bool show_new = ln.new_line > 0 && (prev_it == line_numbers.end() ||
                                          ln.new_line != prev_it->new_line);

      // Reserve total line-number area width.
      ImVec2 area_start = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(total_width, 0));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      float line_height = ImGui::GetTextLineHeight();
      ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);

      // Old number (centered within old_cell)
      if (show_old) {
        std::string num = std::to_string(ln.old_line);
        float num_w = char_size.x * num.size();
        float x = area_start.x + (old_cell - num_w) / 2.0f;
        dl->AddText(ImVec2(x, area_start.y), text_color, num.c_str());
      }

      // Vertical separator centered in the gap between old_cell and new_cell.
      float sep_x = area_start.x + old_cell + sep_width / 2.0f;
      ImU32 sep_color = ImGui::GetColorU32(ImGuiCol_Separator);
      dl->AddLine(ImVec2(sep_x, area_start.y),
                  ImVec2(sep_x, area_start.y + line_height), sep_color);

      // New number (centered within new_cell)
      if (show_new) {
        std::string num = std::to_string(ln.new_line);
        float num_w = char_size.x * num.size();
        float x =
            area_start.x + old_cell + sep_width + (new_cell - num_w) / 2.0f;
        dl->AddText(ImVec2(x, area_start.y), text_color, num.c_str());
      }
    } else {
      ImGui::Dummy(ImVec2(0, 0));
    }
    ImGui::SameLine(0, 1);
    ImGui::PopStyleColor();
  }

  // Apply syntax highlighting in syntax mode (skip conflict markers).
  if (settings_.diff_color_mode == DiffColorMode::kSyntax &&
      !IsConflictMarker(line)) {
    auto files = git_diff_.GetFileList();
    int file_idx = FindFileForLine(files, line_index);
    if (file_idx != current_lang_file_idx_) {
      current_lang_file_idx_ = file_idx;
      current_lang_ = file_idx >= 0 ? DetectLanguage(files[file_idx].path,
                                                     settings_.ext_mappings)
                                    : Language::kNone;
    }
    if (current_lang_ != Language::kNone) {
      bool in_block_comment = false;
      auto stx = git_diff_.GetSyntaxTransitions();
      if (!stx.empty()) {
        auto it = std::upper_bound(
            stx.begin(), stx.end(), line_index,
            [](int idx, const SyntaxTransition& t) { return idx < t.line; });
        if (it != stx.begin()) {
          --it;
          bool is_old_side = !line.empty() && line[0] == '-';
          in_block_comment =
              is_old_side ? it->old_in_block_comment : it->new_in_block_comment;
        }
      }
      ColoredLine highlighted =
          HighlightSyntax(line, current_lang_, in_block_comment);
      if (!highlighted.segments.empty()) {
        RenderColoredLine(highlighted);
        return;
      }
    }
  }

  // Render commit message lines with URL link styling.
  if (line_index < commit_msg_end_ && line.starts_with("    ")) {
    auto urls = FindUrls(line);
    if (!urls.empty()) {
      ImVec4 link_color = ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight);
      ImU32 link_color_u32 = ImGui::ColorConvertFloat4ToU32(link_color);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      int prev_end = 0;
      bool first = true;
      for (auto& url : urls) {
        if (url.start > prev_end) {
          if (!first)
            ImGui::SameLine(0, 0);
          ImGui::TextUnformatted(line.c_str() + prev_end,
                                 line.c_str() + url.start);
          first = false;
        }
        if (!first)
          ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, link_color);
        ImVec2 pos_before = ImGui::GetCursorScreenPos();
        ImGui::TextUnformatted(line.c_str() + url.start,
                               line.c_str() + url.end);
        ImGui::PopStyleColor();
        ImVec2 pos_after = ImGui::GetItemRectMax();
        float underline_y = pos_after.y - 1.0f;
        dl->AddLine(ImVec2(pos_before.x, underline_y),
                    ImVec2(pos_after.x, underline_y), link_color_u32);
        ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x >= pos_before.x && mouse.x <= pos_after.x &&
            mouse.y >= pos_before.y && mouse.y <= pos_after.y) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        first = false;
        prev_end = url.end;
      }
      if (prev_end < static_cast<int>(line.size())) {
        if (!first)
          ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(line.c_str() + prev_end,
                               line.c_str() + line.size());
      }
      return;
    }
  }

  // Render "Parent: <hash>" with clickable hash.
  if (line_index < commit_msg_end_ && line.starts_with("Parent: ")) {
    ImVec4 link_color = ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight);
    ImU32 link_color_u32 = ImGui::ColorConvertFloat4ToU32(link_color);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::TextUnformatted(line.c_str(), line.c_str() + 8);
    ImGui::SameLine(0, 0);
    std::string hash = line.substr(8);
    ImVec2 pos_before = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Text, link_color);
    ImGui::TextUnformatted(hash.c_str());
    ImGui::PopStyleColor();
    ImVec2 pos_after = ImGui::GetItemRectMax();
    float underline_y = pos_after.y - 1.0f;
    dl->AddLine(ImVec2(pos_before.x, underline_y),
                ImVec2(pos_after.x, underline_y), link_color_u32);
    ImVec2 mouse = ImGui::GetMousePos();
    if (mouse.x >= pos_before.x && mouse.x <= pos_after.x &&
        mouse.y >= pos_before.y && mouse.y <= pos_after.y)
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return;
  }

  // Render main file header with status badge and clean path.
  if (IsMainFileHeader(line)) {
    const char* path = line.c_str() + 1;
    auto files = git_diff_.GetFileList();
    int file_idx = FindFileForLine(files, line_index);
    BadgeInfo badge = ComputeBadgeInfo(file_idx, files);

    if (badge.has_badge) {
      // Draw status badge.
      ImVec2 badge_pos = ImGui::GetCursorScreenPos();
      float badge_size = ImGui::GetFontSize();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      float rounding = 3.0f;
      dl->AddRectFilled(ImVec2(badge_pos.x, badge_pos.y + 1),
                        ImVec2(badge_pos.x + badge.width - 4.0f,
                               badge_pos.y + badge_size - 1),
                        (badge.color & 0x00FFFFFF) | 0x40000000, rounding);
      dl->AddRect(ImVec2(badge_pos.x, badge_pos.y + 1),
                  ImVec2(badge_pos.x + badge.width - 4.0f,
                         badge_pos.y + badge_size - 1),
                  (badge.color & 0x00FFFFFF) | 0x80000000, rounding);
      ImVec2 text_size = ImGui::CalcTextSize(badge.letter);
      dl->AddText(
          ImVec2(badge_pos.x + (badge.width - 4.0f - text_size.x) * 0.5f,
                 badge_pos.y + (badge_size - text_size.y) * 0.5f),
          badge.color, badge.letter);

      ImGui::Dummy(ImVec2(badge.width, badge_size));
      ImGui::SameLine(0, 0);
    }

    // Render full path in bold.
    ImGui::PushStyleColor(ImGuiCol_Text, ResolveColor(ColorId::kDefaultBold));
    ImGui::TextUnformatted(path);
    ImGui::PopStyleColor();
    return;
  }

  // Render with line-level color.
  uint32_t color = ImGui::GetColorU32(ImGuiCol_Text);
  if (!line.empty() && !IsConflictMarker(line)) {
    if (auto sign = FindDiffSign(line_index, line)) {
      color = *sign == '+' ? ResolveColor(ColorId::kGreen)
                           : ResolveColor(ColorId::kRed);
    } else {
      if (line.starts_with("@@"))
        color = ResolveColor(ColorId::kBrightCyan);
      else if (IsFileHeader(line))
        color = ResolveColor(ColorId::kDefaultBold);
    }
  }
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
  ImGui::PopStyleColor();
}

float DiffContent::GetTextOffset(int line_index) const {
  auto content = git_diff_.GetDiffContent();
  if (line_index < 0 || line_index >= static_cast<int>(content.size()))
    return 0.0f;

  // Only file header lines (starting with \x01) have a badge rendered.
  // Regular diff lines render at the normal cursor position.
  if (IsMainFileHeader(content[line_index])) {
    auto files = git_diff_.GetFileList();
    int file_idx = FindFileForLine(files, line_index);
    BadgeInfo badge = ComputeBadgeInfo(file_idx, files);
    return (badge.has_badge ? badge.width : 0.0f);
  }

  if (!settings_.show_line_numbers || !ShouldShowLineNumbers(line_index))
    return 0.0f;

  // Compute line number column width.
  auto [old_digits, new_digits] = ComputeLineNumberDigits();
  int total_chars = old_digits + 1 + new_digits;  // "old|new"
  ImVec2 char_size = ImGui::CalcTextSize("0");
  return char_size.x * total_chars;
}

void DiffContent::DrawLineHighlights(int line_index,
                                     const std::string& plain_text,
                                     ImDrawList* draw_list,
                                     ImVec2 line_pos,
                                     float line_height,
                                     ImFont* font,
                                     float font_size) {
  // Highlight search term occurrences in the commit message lines.
  if (line_index < commit_msg_end_ && !search_needle_.empty() &&
      plain_text.starts_with("    ")) {
    size_t term_len = search_needle_.size();
    ImU32 search_hl_color = GetSyntaxPalette().search_match_bg;
    base::ForEachMatch(plain_text, search_needle_, match_buf_, [&](size_t pos) {
      float text_offset = GetTextOffset(line_index);
      float x1 =
          font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, plain_text.c_str(),
                              plain_text.c_str() + pos)
              .x;
      float x2 =
          font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, plain_text.c_str(),
                              plain_text.c_str() + pos + term_len)
              .x;
      draw_list->AddRectFilled(
          ImVec2(line_pos.x + text_offset + x1, line_pos.y),
          ImVec2(line_pos.x + text_offset + x2, line_pos.y + line_height),
          search_hl_color);
    });
  }
}

void DiffContent::OnContextMenu(int line_index) {
  auto content = git_diff_.GetDiffContent();
  int line_count = static_cast<int>(content.size());

  ImGui::Separator();

  // URL context menu items for commit message lines.
  if (line_index >= 0 && line_index < line_count &&
      line_index < commit_msg_end_ && content[line_index].starts_with("    ")) {
    std::string url = FindUrlAtOffset(content[line_index], ctx_menu_char_);
    if (!url.empty()) {
      if (ImGui::MenuItem("Open link in browser"))
        OpenUrl(url);
      if (ImGui::MenuItem("Copy link URL"))
        ImGui::SetClipboardText(url.c_str());
      ImGui::Separator();
    }
  }

  bool can_show_origin = false;
  int ctx_file_idx = -1;
  if (line_index >= 0 && line_index < line_count) {
    auto files = git_diff_.GetFileList();
    ctx_file_idx = FindFileForLine(files, line_index);
    if (ctx_file_idx >= 0 &&
        files[ctx_file_idx].status != FileStatus::kCommit) {
      const std::string& text = content[line_index];
      char first = text.empty() ? '\0' : text[0];
      can_show_origin = (first == ' ' || first == '+' || first == '-');
      if (first == '+' &&
          (delegate_.IsUnstagedSelected() || delegate_.IsStagedSelected()))
        can_show_origin = false;
    }
  }
  if (ImGui::MenuItem("Show origin of this line", nullptr, false,
                      can_show_origin)) {
    std::string file;
    int line_number = 0;
    bool is_old = false;
    ComputeLineOrigin(ctx_file_idx, file, line_number, is_old, line_index);
    delegate_.OnShowLineOrigin(file, line_number, is_old);
  }
  if (ImGui::MenuItem("Run git gui blame on this line", nullptr, false,
                      can_show_origin)) {
    std::string file;
    int line_number = 0;
    bool is_old = false;
    ComputeLineOrigin(ctx_file_idx, file, line_number, is_old, line_index);
    delegate_.OnRunGitGuiBlame(file, line_number, is_old);
  }
}

void DiffContent::OnSearchInput(char* buf, size_t buf_size) {
  InputTextContextMenu(buf, buf_size);
}

void DiffContent::ComputeLineOrigin(int file_idx,
                                    std::string& out_file,
                                    int& out_line,
                                    bool& out_is_old,
                                    int target_line) {
  auto files = git_diff_.GetFileList();
  auto content = git_diff_.GetDiffContent();
  auto& file = files[file_idx];
  int old_line = 0, new_line = 0;
  bool in_hunk = false;
  bool combined = false;

  for (int j = static_cast<int>(file.start_line);
       j <= target_line && j < static_cast<int>(content.size()); j++) {
    const std::string& text = content[j];

    if (text.starts_with("@@")) {
      combined = text.size() > 2 && text[2] == '@';
      size_t dash = text.find('-', 3);
      if (dash != std::string::npos)
        old_line = std::atoi(text.c_str() + dash + 1);
      size_t plus = text.find('+', 3);
      if (plus != std::string::npos)
        new_line = std::atoi(text.c_str() + plus + 1);
      in_hunk = true;
      continue;
    }

    if (!in_hunk)
      continue;

    char first = text.empty() ? '\0' : text[0];

    if (j == target_line) {
      if (first == '+') {
        out_file = file.path;
        out_line = new_line;
      } else {
        // Context (' ') and removed ('-') lines exist in the old revision.
        // Use old_line so the line number matches the revision being blamed.
        out_file = (first == '-' && !file.old_path.empty()) ? file.old_path
                                                            : file.path;
        out_line = old_line;
        out_is_old = true;
      }
      break;
    }

    // In combined diffs (@@@ headers), lines have two prefix characters.
    // A ' -' line (parent 2 removed, not in parent 1 or result) must not
    // increment old_line or new_line despite first being ' '.
    if (combined && first == ' ' && text.size() >= 2 && text[1] == '-') {
      // Line from parent 2 only, not in parent 1 or result: skip.
    } else if (first == ' ') {
      old_line++;
      new_line++;
    } else if (first == '+') {
      new_line++;
    } else if (first == '-') {
      old_line++;
    }
  }
}

bool DiffContent::ShouldShowLineNumbers(int line_index) const {
  auto content = git_diff_.GetDiffContent();
  const std::string& line = content[line_index];
  bool is_content = line_index >= commit_msg_end_ && !line.empty() &&
                    (line[0] == ' ' || line[0] == '+' || line[0] == '-');
  if (!is_content)
    return false;

  // Line numbers are only shown for normal diff hunks (@@ headers with
  // exactly 2 '@' characters). Combined-diff hunks (@@@) are skipped.
  auto hunks = git_diff_.GetHunkEntries();
  int hunk_idx = FindHunkForLine(hunks, line_index);
  return hunk_idx >= 0 && hunks[hunk_idx].at_count == 2;
}

std::pair<int, int> DiffContent::ComputeLineNumberDigits() const {
  int old_digits =
      std::max(max_old_line_ > 0
                   ? static_cast<int>(std::to_string(max_old_line_).size())
                   : 0,
               4) +
      1;
  int new_digits =
      std::max(max_new_line_ > 0
                   ? static_cast<int>(std::to_string(max_new_line_).size())
                   : 0,
               4) +
      1;
  return {old_digits, new_digits};
}

std::optional<char> DiffContent::FindDiffSign(int line_index,
                                              const std::string& line) {
  auto hunks = git_diff_.GetHunkEntries();
  int hunk_idx = FindHunkForLine(hunks, line_index);
  int at_count = hunk_idx >= 0 ? hunks[hunk_idx].at_count - 1 : 1;

  for (int i = 0; i < at_count && i < static_cast<int>(line.size()); i++) {
    char c = line[i];
    if (c == '+' || c == '-')
      return c;
  }
  return std::nullopt;
}
