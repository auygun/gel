// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/commit_size.h"

#include <algorithm>
#include <iterator>
#include <optional>

#include "gel/persistent_settings.h"
#include "gel/ui/icons.h"
#include "gel/ui/lower_panel/charts/bar_chart.h"
#include "gel/ui/lower_panel/charts/donut_chart.h"
#include "gel/ui/lower_panel/charts/treemap_chart.h"
#include "gel/ui/overlay_toolbar.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"

CommitSize::CommitSize(PersistentSettings& settings,
                       std::function<void(bool)> busy_callback)
    : settings_(settings),
      git_diff_tree_(busy_callback),
      git_cat_file_(std::move(busy_callback)),
      file_tree_(*this, settings),
      donut_chart_(std::make_unique<DonutChart>(*this)),
      bar_chart_(std::make_unique<BarChart>(*this)),
      treemap_chart_(std::make_unique<TreemapChart>(*this)) {}

CommitSize::~CommitSize() = default;

void CommitSize::SetCommit(const std::string& commit_hash) {
  if (commit_hash == commit_)
    return;
  commit_ = commit_hash;
  file_tree_.Clear();
  pending_entries_.clear();
  git_diff_tree_.Kill();
  git_cat_file_.Kill();
  if (!commit_.empty())
    git_diff_tree_.Run(commit_);
}

void CommitSize::OnDirectorySelected(SizeFileTree::TreeNode* node) {
  (void)node;
}

int CommitSize::GetChildCount() const {
  return static_cast<int>(file_tree_.GetSelectedDirectory()->children.size());
}

int64_t CommitSize::GetTotalSize() const {
  return file_tree_.GetSelectedDirectory()->size;
}

const std::string& CommitSize::GetChildName(int child_index) const {
  return file_tree_.GetSelectedDirectory()->children[child_index].name;
}

int64_t CommitSize::GetChildSize(int child_index) const {
  return file_tree_.GetSelectedDirectory()->children[child_index].size;
}

bool CommitSize::IsChildDirectory(int child_index) const {
  return file_tree_.GetSelectedDirectory()->children[child_index].is_directory;
}

void CommitSize::DrillDown(int child_index) {
  auto* dir = file_tree_.GetSelectedDirectory();
  file_tree_.ScrollToDirectory(&dir->children[child_index]);
}

void CommitSize::NavigateToParent() {
  auto* dir = file_tree_.GetSelectedDirectory();
  if (dir == file_tree_.GetRoot())
    return;
  auto& path = dir->full_path;
  auto slash = path.rfind('/');
  auto* parent = (slash == std::string::npos)
                     ? file_tree_.GetRoot()
                     : file_tree_.FindNode(path.substr(0, slash));
  file_tree_.ScrollToDirectory(parent);
}

std::vector<SliceInfo> CommitSize::ComputeSlices() {
  auto* dir = file_tree_.GetSelectedDirectory();
  auto& children = dir->children;
  int64_t total = dir->size;
  std::vector<SliceInfo> slices;
  for (int i = 0; i < static_cast<int>(children.size()); i++) {
    if (children[i].size <= 0)
      continue;
    slices.push_back(
        {i, static_cast<float>(children[i].size) / static_cast<float>(total),
         children[i].size, 0});
  }

  std::sort(slices.begin(), slices.end(),
            [](const SliceInfo& a, const SliceInfo& b) {
              return a.fraction > b.fraction;
            });

  constexpr int kMaxSlices = 100;
  if (static_cast<int>(slices.size()) > kMaxSlices) {
    int64_t others_size = 0;
    int others_count = 0;
    for (size_t i = kMaxSlices - 1; i < slices.size(); i++) {
      others_size += slices[i].size;
      others_count++;
    }
    slices.resize(kMaxSlices - 1);
    slices.push_back(
        {-1, static_cast<float>(others_size) / static_cast<float>(total),
         others_size, others_count});
  }

  float min_fraction = 0.015f;
  if (slices.size() > 1) {
    for (auto& s : slices) {
      if (s.fraction < min_fraction)
        s.fraction = min_fraction;
    }
    float sum = 0;
    for (auto& s : slices)
      sum += s.fraction;
    if (sum > 0) {
      for (auto& s : slices)
        s.fraction /= sum;
    }
  }

  return slices;
}

namespace {

struct StyleButton {
  ChartStyle style;
  const char* id;
  void (*draw_icon)();
  const char* tooltip;
};
constexpr StyleButton kStyleButtons[] = {
    {ChartStyle::kDonut, "##donut", DrawDonutChartIcon, "Donut chart"},
    {ChartStyle::kBar, "##bar", DrawBarChartIcon, "Bar chart"},
    {ChartStyle::kTreemap, "##treemap", DrawTreemapIcon, "Treemap"},
};

}  // namespace

float CommitSize::OverlayContentWidth() {
  float num_buttons =
      static_cast<float>(1 + std::size(kStyleButtons));  // Sort + styles.
  // Plus the separator between the tree's and the chart's controls.
  return OverlayToolbar::HalfButtonWidth() +
         num_buttons *
             (OverlayToolbar::Spacing() + OverlayToolbar::ButtonSize()) +
         OverlayToolbar::Spacing() + OverlayToolbar::SeparatorWidth();
}

void CommitSize::RenderOverlay(const ImVec2& anchor) {
  float btn_size = OverlayToolbar::ButtonSize();
  overlay_.Begin("##size_overlay", anchor, OverlayContentWidth());

  // The tree's controls first, as the file list's are in the diff overlay,
  // then the chart's.
  bool expand_all_requested = false;
  bool collapse_all_requested = false;
  overlay_.ExpandCollapseButtons(expand_all_requested, collapse_all_requested);

  // On while the tree is sorted by size, off while by name.
  ImGui::SameLine();
  bool by_size = settings_.size_tree_sort == SizeTreeSort::kSize;
  std::optional<SizeTreeSort> sort_requested;
  if (ToggleButton("##sort_by_size", by_size, ImVec2(btn_size, btn_size)))
    sort_requested = by_size ? SizeTreeSort::kName : SizeTreeSort::kSize;
  DrawSortBySizeIcon();
  overlay_.Tooltip(by_size ? "Sort: by size" : "Sort: by name");

  // Separates the tree's controls from the chart's.
  ImGui::SameLine();
  overlay_.Separator();

  std::optional<ChartStyle> style_requested;
  for (auto& btn : kStyleButtons) {
    ImGui::SameLine();
    bool active = (settings_.chart_style == btn.style);
    if (ToggleButton(btn.id, active, ImVec2(btn_size, btn_size)) && !active)
      style_requested = btn.style;
    btn.draw_icon();
    overlay_.Tooltip(btn.tooltip);
  }

  overlay_.End();

  if (expand_all_requested)
    file_tree_.SetAllDirsOpen(true);
  if (collapse_all_requested)
    file_tree_.SetAllDirsOpen(false);
  if (sort_requested)
    file_tree_.SetSort(*sort_requested);
  if (style_requested) {
    settings_.chart_style = *style_requested;
    settings_.Save();
  }
}

void CommitSize::RenderBreadcrumb() {
  auto* dir = file_tree_.GetSelectedDirectory();
  int64_t total = dir->size;

  // On the overlay's line, centered on it and clipped short of it, with the
  // chart starting below it.
  ImVec2 anchor = OverlayToolbar::WindowAnchor();
  float row_top = ImGui::GetCursorScreenPos().y;
  ImGui::SetCursorScreenPos(
      ImVec2(ImGui::GetCursorScreenPos().x,
             std::max(row_top, anchor.y + OverlayToolbar::CenterOffset() -
                                   ImGui::GetTextLineHeight() / 2)));
  float clip_right = anchor.x - OverlayToolbar::Width(OverlayContentWidth()) -
                     OverlayToolbar::Spacing() * 2;
  ImVec2 win_min = ImGui::GetWindowPos();
  ImVec2 win_max(win_min.x + ImGui::GetWindowSize().x,
                 win_min.y + ImGui::GetWindowSize().y);
  ImGui::PushClipRect(win_min,
                      ImVec2(std::max(win_min.x, clip_right), win_max.y), true);

  SizeFileTree::TreeNode* nav_target = nullptr;
  if (ImGui::SmallButton("/##chart_root"))
    nav_target = file_tree_.GetRoot();
  ImGui::SameLine(0, 0);

  if (!dir->full_path.empty()) {
    std::string accumulated;
    size_t start = 0;
    int crumb_id = 0;
    while (start < dir->full_path.size()) {
      auto slash = dir->full_path.find('/', start);
      std::string component = (slash == std::string::npos)
                                  ? dir->full_path.substr(start)
                                  : dir->full_path.substr(start, slash - start);
      if (!accumulated.empty())
        accumulated += '/';
      accumulated += component;

      ImGui::TextUnformatted("/");
      ImGui::SameLine(0, 0);
      std::string label =
          component + "##chart_crumb" + std::to_string(crumb_id++);
      if (ImGui::SmallButton(label.c_str())) {
        nav_target = file_tree_.FindNode(accumulated);
      }
      ImGui::SameLine(0, 0);

      if (slash == std::string::npos)
        break;
      start = slash + 1;
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%s)", HumanSize(total).c_str());
  ImGui::PopClipRect();

  float chart_top = anchor.y + OverlayToolbar::BottomOffset();
  if (ImGui::GetCursorScreenPos().y < chart_top)
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, chart_top));

  if (nav_target && nav_target->is_directory) {
    file_tree_.ScrollToDirectory(nav_target);
  }
}

void CommitSize::Update() {
  // Phase 1: diff-tree results arrive -> start cat-file for blob sizes.
  std::vector<DiffTreeEntry> diff_entries;
  if (git_diff_tree_.Update(diff_entries)) {
    pending_entries_ = std::move(diff_entries);
    std::vector<std::string> hashes;
    for (const auto& e : pending_entries_) {
      if (!e.deleted)
        hashes.push_back(e.new_hash);
    }
    if (hashes.empty()) {
      // All files deleted or no entries -- deliver results immediately.
      std::vector<ChangedFileEntry> files;
      files.reserve(pending_entries_.size());
      for (auto& e : pending_entries_)
        files.push_back({std::move(e.path), 0});
      pending_entries_.clear();
      file_tree_.SetFiles(std::move(files));
    } else {
      git_cat_file_.Run(hashes);
    }
  }

  // Phase 2: cat-file sizes arrive -> combine with paths and deliver.
  std::vector<int64_t> sizes;
  if (git_cat_file_.Update(sizes)) {
    std::vector<ChangedFileEntry> files;
    files.reserve(pending_entries_.size());
    size_t si = 0;
    for (auto& e : pending_entries_) {
      int64_t sz = 0;
      if (!e.deleted && si < sizes.size())
        sz = sizes[si++];
      files.push_back({std::move(e.path), sz});
    }
    pending_entries_.clear();
    file_tree_.SetFiles(std::move(files));
  }
}

bool CommitSize::HasTreeData() const {
  return !commit_.empty() && file_tree_.HasData();
}

void CommitSize::RenderSidePane(float width) {
  if (ImGui::BeginChild("size_tree", ImVec2(width, -FLT_MIN),
                        ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    if (HasTreeData())
      file_tree_.Update();
    else if (commit_.empty())
      ImGui::TextDisabled("No commit selected");
    else if (git_diff_tree_.busy() || git_cat_file_.busy())
      ImGui::TextDisabled("Loading...");
    else
      ImGui::TextDisabled("No changed files");
  }
  ImGui::EndChild();
}

void CommitSize::RenderMainPane(float width) {
  overlay_anchor_.reset();
  if (ImGui::BeginChild("size_chart", ImVec2(width, -FLT_MIN),
                        ImGuiChildFlags_Borders)) {
    if (HasTreeData()) {
      auto* dir = file_tree_.GetSelectedDirectory();
      if (!dir || dir->children.empty()) {
        ImGui::TextDisabled("Select a directory");
      } else if (dir->size <= 0) {
        ImGui::TextDisabled("Empty directory");
      } else {
        RenderBreadcrumb();
        ImGui::Spacing();

        auto slices = ComputeSlices();

        switch (settings_.chart_style) {
          case ChartStyle::kDonut:
            donut_chart_->Render(slices);
            break;
          case ChartStyle::kBar:
            bar_chart_->Render(slices);
            break;
          case ChartStyle::kTreemap:
            treemap_chart_->Render(slices);
            break;
          case ChartStyle::kCount:
            break;
        }
      }
      overlay_anchor_ = OverlayToolbar::WindowAnchor();
    }
  }
  ImGui::EndChild();
}

void CommitSize::FinishFrame() {
  // Over the chart, after its window, like the diff view's overlay over the
  // diff.
  if (overlay_anchor_)
    RenderOverlay(*overlay_anchor_);
}
