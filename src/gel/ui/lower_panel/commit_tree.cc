// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/commit_tree.h"

#include <algorithm>
#include <filesystem>

#include "gel/git_repo.h"
#include "gel/persistent_settings.h"
#include "gel/ui/style.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/base/log.h"

namespace {

// Git treats a file as binary if its first 8000 bytes contain a NUL byte.
constexpr size_t kBinaryCheckBytes = 8000;

// Lines scanned for syntax states per frame, so that a huge file loading
// doesn't stall the frame. Lines past the scan are highlighted as if they
// start outside any multi-line construct until it gets to them.
constexpr int kSyntaxScanLinesPerFrame = 20000;

}  // namespace

CommitTree::CommitTree(Delegate& delegate,
                       GraphicsDelegate& graphics_delegate,
                       GitRepo& git_repo,
                       PersistentSettings& settings,
                       std::function<void(bool)> busy_callback,
                       std::function<void()> main_thread_busy)
    : delegate_(delegate),
      git_repo_(git_repo),
      settings_(settings),
      git_ls_tree_(busy_callback),
      git_show_file_(busy_callback),
      image_view_(graphics_delegate, std::move(busy_callback)),
      text_viewer_(*this, std::move(main_thread_busy)) {
  text_viewer_.set_search_history(&settings_.diff_search_history);
}

CommitTree::~CommitTree() = default;

void CommitTree::SetSource(const Source& source, bool reload) {
  if (source == source_ && !reload)
    return;
  bool same_source = source == source_;
  source_ = source;
  // Keep the selected file across a reload, and across commits, as gitk
  // doesn't, so stepping through the history follows the same file.
  if (!same_source)
    pending_line_ = 0;
  StartListing();
}

void CommitTree::StartListing() {
  selected_entry_ = -1;
  find_selected_ = !selected_path_.empty();
  tree_.Clear();
  // Show the selected file again once listed, without the old content in the
  // meantime.
  ClearContent();
  if (source_.empty()) {
    git_ls_tree_.Stop();
  } else {
    GitLsTree::Source ls_source = source_.unstaged
                                      ? GitLsTree::Source::kWorkTree
                                  : source_.staged ? GitLsTree::Source::kIndex
                                                   : GitLsTree::Source::kCommit;
    git_ls_tree_.Run(ls_source, source_.commit, git_repo_.toplevel());
  }
  tree_generation_ = git_ls_tree_.generation();
}

void CommitTree::ShowFileLine(const std::string& file, int line) {
  pending_file_ = file;
  pending_line_ = line;
  // Already shown: Update() highlights the line.
  if (selected_entry_ >= 0 && selected_path_ == file) {
    scroll_tree_to_selected_ = true;
    return;
  }
  selected_path_ = file;
  // Select it now if listed, otherwise once the listing has it.
  find_selected_ = !SelectPath(file);
}

bool CommitTree::SelectPath(const std::string& path) {
  auto entries = git_ls_tree_.entries();
  // Entries are in git's sorted path order, byte by byte.
  auto it = std::lower_bound(
      entries.begin(), entries.end(), path,
      [](const TreeEntry& e, const std::string& p) { return e.path < p; });
  if (it == entries.end() || it->path != path)
    return false;
  SelectEntry(static_cast<int>(it - entries.begin()));
  // Shown on its own row rather than as a collapsed directory.
  tree_.RevealPath(path);
  scroll_tree_to_selected_ = true;
  return true;
}

void CommitTree::ClearContent() {
  git_show_file_.Stop();
  file_reader_.Stop();
  image_view_.Clear();
  showing_image_ = false;
  text_viewer_.Reset();
  lang_ = Language::kNone;
  from_work_tree_ = false;
  note_.clear();
  binary_checked_bytes_ = 0;
  binary_checked_lines_ = 0;
  text_confirmed_ = false;
  syntax_marks_.clear();
  syntax_scanned_lines_ = 0;
  syntax_state_ = {};
}

void CommitTree::ShowNote(std::string note) {
  note_ = {std::move(note)};
  git_show_file_.Stop();
  file_reader_.Stop();
}

void CommitTree::SelectEntry(int index) {
  ClearContent();
  selected_entry_ = index;
  if (index < 0)
    return;
  const TreeEntry& entry = git_ls_tree_.entries()[index];
  selected_path_ = entry.path;
  if (pending_file_ != entry.path)
    pending_line_ = 0;
  lang_ = DetectLanguage(entry.path, settings_.ext_mappings);
  if (entry.submodule) {
    ShowNote("Submodule (no content to show).");
  } else if (source_.unstaged && entry.symlink) {
    // Its target, as git stores a symlink, rather than the file it points to,
    // which could be anything, even a device that never ends.
    std::error_code ec;
    std::filesystem::path target = std::filesystem::read_symlink(
        WorkTreePath(git_repo_.toplevel(), entry.path), ec);
    if (ec) {
      ShowNote("Could not read the symlink.");
    } else {
      std::u8string utf8 = target.u8string();
      ShowNote(
          "Symlink to " +
          std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
    }
  } else if (IsImageFile(entry.path)) {
    showing_image_ = true;
    if (source_.unstaged)
      image_view_.LoadFile(WorkTreePath(git_repo_.toplevel(), entry.path));
    else
      image_view_.LoadBlob(entry.blob, git_repo_.toplevel());
  } else {
    LoadText(entry);
  }
}

void CommitTree::LoadText(const TreeEntry& entry) {
  if (source_.unstaged) {
    from_work_tree_ = true;
    file_reader_.Read(WorkTreePath(git_repo_.toplevel(), entry.path));
  } else {
    from_work_tree_ = false;
    git_show_file_.Run(entry.blob, git_repo_.toplevel());
  }
}

void CommitTree::UpdateImage() {
  if (!showing_image_)
    return;
  switch (image_view_.state()) {
    case ImageView::State::kFailed:
      // Not an image after all, or missing from the working tree: the text
      // tells which.
      showing_image_ = false;
      image_view_.Clear();
      LoadText(git_ls_tree_.entries()[selected_entry_]);
      break;
    case ImageView::State::kTooLarge:
      showing_image_ = false;
      if (image_view_.width() > 0) {
        ShowNote("Image too large to show (" +
                 std::to_string(image_view_.width()) + "x" +
                 std::to_string(image_view_.height()) + ").");
      } else {
        ShowNote("Image too large to show.");
      }
      image_view_.Clear();
      break;
    default:
      break;
  }
}

std::span<const std::string> CommitTree::LoadedLines() const {
  return from_work_tree_ ? file_reader_.lines() : git_show_file_.lines();
}

bool CommitTree::ContentLoaded() const {
  return from_work_tree_ ? file_reader_.done() : git_show_file_.done();
}

bool CommitTree::ContentFailed() const {
  return from_work_tree_ ? file_reader_.failed() : git_show_file_.failed();
}

std::span<const std::string> CommitTree::ShownLines() const {
  if (!note_.empty())
    return note_;
  return text_confirmed_ ? LoadedLines() : std::span<const std::string>();
}

void CommitTree::ScanContent() {
  if (selected_entry_ < 0 || !note_.empty() || showing_image_)
    return;
  auto lines = LoadedLines();
  int count = static_cast<int>(lines.size());
  bool loaded = ContentLoaded();

  if (loaded && ContentFailed()) {
    // Without throwing for errors other than a missing file, e.g. a
    // directory without search permission.
    std::error_code ec;
    bool missing =
        from_work_tree_ &&
        !std::filesystem::exists(
            WorkTreePath(git_repo_.toplevel(), selected_path_), ec) &&
        !ec;
    ShowNote(missing ? "The file is missing from the working tree."
                     : "Could not read the file.");
    return;
  }

  // Shown once its start is seen to be text, as git tells binary files.
  if (!text_confirmed_) {
    for (; binary_checked_lines_ < count &&
           binary_checked_bytes_ < kBinaryCheckBytes;
         binary_checked_lines_++) {
      const std::string& line = lines[binary_checked_lines_];
      if (line.find('\0') != std::string::npos) {
        ShowNote("Binary file (not showing content).");
        return;
      }
      binary_checked_bytes_ += line.size() + 1;
    }
    // A short file: once all of it is checked.
    text_confirmed_ = binary_checked_bytes_ >= kBinaryCheckBytes ||
                      (loaded && binary_checked_lines_ == count);
  }

  if (lang_ == Language::kNone || !text_confirmed_)
    return;
  int end = std::min(count, syntax_scanned_lines_ + kSyntaxScanLinesPerFrame);
  for (; syntax_scanned_lines_ < end; syntax_scanned_lines_++) {
    SyntaxState next =
        ScanSyntaxState(lines[syntax_scanned_lines_], lang_, syntax_state_);
    if (next != syntax_state_)
      syntax_marks_.push_back({syntax_scanned_lines_ + 1, next});
    syntax_state_ = next;
  }
}

void CommitTree::Update() {
  // Read before merging, so that once done, the merge took in all entries.
  bool listed = git_ls_tree_.done();
  git_ls_tree_.Update();
  listed = listed && git_ls_tree_.done();
  DCHECK(tree_generation_ == git_ls_tree_.generation());
  auto entries = git_ls_tree_.entries();
  int count = static_cast<int>(entries.size());
  int added = tree_.item_count();
  for (int i = added; i < count; i++)
    tree_.Add(entries[i].path);

  // Select the file shown before, and the file of a blame lookup, once
  // listed: looked for in each batch of new entries, and given up on once
  // the listing is done without it.
  if (find_selected_ && (count > added || listed)) {
    if (SelectPath(selected_path_)) {
      find_selected_ = false;
    } else if (listed) {
      find_selected_ = false;
      // Not in this tree: show nothing.
      if (pending_file_ == selected_path_) {
        pending_file_.clear();
        pending_line_ = 0;
      }
    }
  }

  UpdateImage();
  if (from_work_tree_)
    file_reader_.Update();
  else
    git_show_file_.Update();
  ScanContent();

  if (pending_line_ > 0 && selected_entry_ >= 0 &&
      selected_path_ == pending_file_) {
    // File line N is content line N - 1.
    if (showing_image_) {
      // An image has no lines. Kept while it loads: if it doesn't, the
      // content is shown as text.
      if (image_view_.state() == ImageView::State::kLoaded) {
        pending_line_ = 0;
        pending_file_.clear();
      }
    } else if (pending_line_ <= static_cast<int>(ShownLines().size()) &&
               note_.empty()) {
      text_viewer_.HighlightLine(pending_line_ - 1);
      pending_line_ = 0;
      pending_file_.clear();
    } else if (ContentLoaded() && (text_confirmed_ || !note_.empty())) {
      pending_line_ = 0;
      pending_file_.clear();
    }
  }
}

void CommitTree::RenderTree() {
  auto entries = git_ls_tree_.entries();
  tree_.FlattenIfDirty();
  auto rows = tree_.rows();
  int row_count = static_cast<int>(rows.size());
  int selected_row = tree_.RowOfItem(selected_entry_);

  ImGuiListClipper clipper;
  clipper.Begin(row_count);
  if (scroll_tree_to_selected_ && selected_row >= 0)
    clipper.IncludeItemByIndex(selected_row);

  int clicked = -1;
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
      const PathTree::Row& r = rows[row];
      if (r.item < 0) {
        tree_.RenderDirRow(r, row == selected_row);
      } else {
        const TreeEntry& entry = entries[r.item];
        ImGui::PushID(r.item);
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##file", r.item == selected_entry_,
                              ImGuiSelectableFlags_AllowOverlap))
          clicked = r.item;
        bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        if (ImGui::BeginPopupContextItem()) {
          if (ImGui::MenuItem("Copy path"))
            ImGui::SetClipboardText(entry.path.c_str());
          ImGui::EndPopup();
        }
        float available = ImGui::GetContentRegionAvail().x;
        float offset = PathTree::ItemTextOffset(r.depth);
        const char* name = PathTree::ItemName(entry.path);
        ImGui::SetCursorScreenPos(ImVec2(cursor.x + offset, cursor.y));
        if (entry.submodule)
          ImGui::PushStyleColor(ImGuiCol_Text, ResolveColor(ColorId::kMagenta));
        ImGui::TextUnformatted(name);
        if (entry.submodule)
          ImGui::PopStyleColor();
        if (hovered && offset + ImGui::CalcTextSize(name).x > available)
          ImGui::SetTooltip("%s", entry.path.c_str());
        ImGui::PopID();
      }
      if (row == selected_row && scroll_tree_to_selected_) {
        ImGui::SetScrollHereY();
        scroll_tree_to_selected_ = false;
      }
    }
  }
  // Apply directories expanded or collapsed this frame.
  tree_.FlattenIfDirty();

  if (clicked >= 0 && clicked != selected_entry_) {
    pending_file_.clear();
    pending_line_ = 0;
    find_selected_ = false;
    SelectEntry(clicked);
  }
}

void CommitTree::RenderSidePane(float width) {
  if (ImGui::BeginChild("commit_tree", ImVec2(width, -FLT_MIN),
                        ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    if (tree_.item_count() > 0)
      RenderTree();
    else if (source_.empty())
      ImGui::TextDisabled("No commit selected");
    else if (!git_ls_tree_.done())
      ImGui::TextDisabled("Loading...");
    else
      ImGui::TextDisabled("No files");
  }
  ImGui::EndChild();
}

void CommitTree::RenderHeader(float width) {
  // The file's path, in a bar like the diff's file headers, as wide as the
  // pane. A negative |width| fills the rest of the line.
  ImVec2 pos = ImGui::GetCursorScreenPos();
  float height = ImGui::GetFrameHeight();
  if (width <= 0)
    width = ImGui::GetContentRegionAvail().x;
  // Clip the path to the bar.
  ImGui::PushClipRect(pos, ImVec2(pos.x + width, pos.y + height), true);
  ImGui::GetWindowDrawList()->AddRectFilled(
      pos, ImVec2(pos.x + width, pos.y + height),
      GetSyntaxPalette().file_header_bg, ImGui::GetStyle().FrameRounding);
  ImGui::SetCursorScreenPos(
      ImVec2(pos.x + ImGui::GetStyle().FramePadding.x,
             pos.y + (height - ImGui::GetTextLineHeight()) / 2));
  ImGui::PushStyleColor(ImGuiCol_Text, ResolveColor(ColorId::kDefaultBold));
  ImGui::TextUnformatted(selected_path_.c_str());
  ImGui::PopStyleColor();
  ImGui::PopClipRect();
  ImGui::SetCursorScreenPos(
      ImVec2(pos.x, pos.y + height + ImGui::GetStyle().ItemSpacing.y));
}

void CommitTree::RenderMainPane(float width) {
  ImGui::BeginGroup();
  if (selected_entry_ >= 0)
    RenderHeader(width);
  if (showing_image_) {
    image_view_.Render(width);
    overlay_anchor_ = image_view_.content_top_right();
  } else {
    text_viewer_.Update(width, ShownLines(), &settings_.diff_search_options);
    overlay_anchor_ = text_viewer_.content_top_right();
  }
  ImGui::EndGroup();
}

void CommitTree::RenderOverlay() {
  if (overlay_anchor_.x == 0 && overlay_anchor_.y == 0)
    return;
  float content_width = OverlayToolbar::HalfButtonWidth();
  overlay_.Begin("##tree_overlay", overlay_anchor_, content_width);
  bool expand_all_requested = false;
  bool collapse_all_requested = false;
  overlay_.ExpandCollapseButtons(expand_all_requested, collapse_all_requested);
  overlay_.End();

  if (expand_all_requested || collapse_all_requested) {
    tree_.SetAllDirsOpen(expand_all_requested);
    scroll_tree_to_selected_ = true;
  }
}

void CommitTree::FinishFrame() {
  RenderOverlay();
}

// --- TextViewer::Delegate implementation ---

void CommitTree::OnTextSelected(const std::string& text) {
  delegate_.SetPrimarySelection(text);
}

float CommitTree::LineNumberWidth() const {
  int max_line = static_cast<int>(ShownLines().size());
  int digits = std::max(static_cast<int>(std::to_string(max_line).size()), 4);
  // One character of space after the numbers.
  return ImGui::CalcTextSize("0").x * (digits + 1);
}

float CommitTree::GetTextOffset(int line_index) const {
  if (!settings_.show_line_numbers || !note_.empty())
    return 0.0f;
  return LineNumberWidth();
}

void CommitTree::RenderLine(int line_index, const std::string& line) {
  // A note in place of the content.
  if (!note_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ResolveColor(ColorId::kTextDisabled));
    ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
    ImGui::PopStyleColor();
    return;
  }

  if (settings_.show_line_numbers) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = LineNumberWidth();
    std::string number = std::to_string(line_index + 1);
    float char_w = ImGui::CalcTextSize("0").x;
    // Right-aligned, with the one character of space after it.
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(pos.x + width - char_w * (number.size() + 1), pos.y),
        ResolveColor(ColorId::kTextDisabled), number.c_str());
    ImGui::Dummy(ImVec2(width, 0));
    ImGui::SameLine(0, 0);
  }

  if (lang_ != Language::kNone &&
      settings_.diff_color_mode == DiffColorMode::kSyntax) {
    SyntaxState state;
    auto it = std::upper_bound(
        syntax_marks_.begin(), syntax_marks_.end(), line_index,
        [](int idx, const SyntaxMark& m) { return idx < m.line; });
    if (it != syntax_marks_.begin())
      state = std::prev(it)->state;
    // A content line has no diff prefix, which HighlightSyntax() takes to
    // start with ' ', '+' or '-'. Prefix one and render it as a zero-width
    // segment.
    ColoredLine highlighted = HighlightSyntax(" " + line, lang_, state);
    if (!highlighted.segments.empty()) {
      auto& first = highlighted.segments.front();
      if (!first.text.empty() && first.text[0] == ' ')
        first.text.erase(0, 1);
      RenderColoredLine(highlighted);
      return;
    }
  }
  ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
}

void CommitTree::OnContextMenu(int line_index) {
  // Lines of the file, not a note in place of them.
  bool is_content = selected_entry_ >= 0 && note_.empty() && line_index >= 0 &&
                    line_index < static_cast<int>(ShownLines().size());
  ImGui::Separator();
  if (ImGui::MenuItem("Show origin of this line", nullptr, false, is_content)) {
    delegate_.OnShowLineOrigin(selected_path_,
                               git_ls_tree_.entries()[selected_entry_].blob,
                               line_index + 1);
  }
  if (ImGui::MenuItem("Run git gui blame on this line", nullptr, false,
                      is_content && !source_.staged))
    delegate_.OnRunGitGuiBlame(selected_path_, line_index + 1);
}

void CommitTree::OnSearchInput(char* buf, size_t buf_size) {
  InputTextContextMenu(buf, buf_size);
}
