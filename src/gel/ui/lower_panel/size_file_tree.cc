// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/size_file_tree.h"

#include <algorithm>
#include <functional>

#include "gel/persistent_settings.h"
#include "gel/ui/icons.h"
#include "gel/ui/lower_panel/charts/chart_renderer.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/base/thread_pool.h"

SizeFileTree::SizeFileTree(Delegate& delegate, PersistentSettings& settings)
    : delegate_(delegate), settings_(settings) {}

void SizeFileTree::SetFiles(std::vector<ChangedFileEntry> files) {
  // Move old data out and free it on the thread pool to avoid blocking the
  // main thread with destructor calls.
  base::ThreadPool::Get().PostTask(HERE,
                                   [f = std::move(files_), r = std::move(root_),
                                    t = std::move(flat_tree_)]() {});
  files_ = std::move(files);
  BuildTree();
  selected_dir_ = &root_;
  FlattenTree();
}

void SizeFileTree::Clear() {
  base::ThreadPool::Get().PostTask(HERE,
                                   [f = std::move(files_), r = std::move(root_),
                                    t = std::move(flat_tree_)]() {});
  selected_dir_ = nullptr;
}

void SizeFileTree::SelectDirectory(TreeNode* node) {
  selected_dir_ = node;
  delegate_.OnDirectorySelected(node);
}

void SizeFileTree::ScrollToDirectory(TreeNode* node) {
  selected_dir_ = node;
  scroll_to_selected_ = true;
}

SizeFileTree::TreeNode* SizeFileTree::FindNode(const std::string& path) {
  if (path.empty())
    return &root_;
  TreeNode* current = &root_;
  size_t start = 0;
  while (start < path.size()) {
    auto slash = path.find('/', start);
    std::string component = (slash == std::string::npos)
                                ? path.substr(start)
                                : path.substr(start, slash - start);
    TreeNode* found = nullptr;
    for (auto& child : current->children) {
      if (child.name == component) {
        found = &child;
        break;
      }
    }
    if (!found)
      return nullptr;
    current = found;
    if (slash == std::string::npos)
      break;
    start = slash + 1;
  }
  return current;
}

void SizeFileTree::BuildTree() {
  root_ = {};
  root_.name = "/";
  root_.is_directory = true;
  root_.open = true;

  for (auto& file : files_) {
    TreeNode* current = &root_;
    size_t start = 0;
    while (start < file.path.size()) {
      auto slash = file.path.find('/', start);
      bool is_leaf = (slash == std::string::npos);
      std::string component = is_leaf ? file.path.substr(start)
                                      : file.path.substr(start, slash - start);

      TreeNode* child = nullptr;
      for (auto& c : current->children) {
        if (c.name == component) {
          child = &c;
          break;
        }
      }
      if (!child) {
        current->children.push_back({});
        child = &current->children.back();
        child->name = component;
        child->full_path = current->full_path.empty()
                               ? component
                               : current->full_path + "/" + component;
        child->is_directory = !is_leaf;
      }

      if (is_leaf) {
        child->size = file.size;
        break;
      }
      start = slash + 1;
      current = child;
    }
  }

  // Compute directory sizes bottom-up.
  std::function<int64_t(TreeNode&)> compute_sizes =
      [&](TreeNode& node) -> int64_t {
    if (!node.is_directory)
      return node.size;
    node.size = 0;
    for (auto& child : node.children)
      node.size += compute_sizes(child);
    return node.size;
  };
  compute_sizes(root_);

  SortTree();
}

void SizeFileTree::SortTree() {
  auto sort_mode = settings_.size_tree_sort;
  std::function<void(TreeNode&)> sort_tree = [&](TreeNode& node) {
    if (!node.is_directory)
      return;
    std::sort(node.children.begin(), node.children.end(),
              [sort_mode](const TreeNode& a, const TreeNode& b) {
                if (a.is_directory != b.is_directory)
                  return a.is_directory > b.is_directory;
                if (sort_mode == SizeTreeSort::kName)
                  return a.name < b.name;
                return a.size > b.size;
              });
    for (auto& child : node.children)
      sort_tree(child);
  };
  sort_tree(root_);
}

void SizeFileTree::FlattenTree() {
  flat_tree_.clear();
  FlattenNode(root_, 0);
}

void SizeFileTree::FlattenNode(TreeNode& node, int depth) {
  flat_tree_.push_back({&node, depth});
  if (node.is_directory && node.open) {
    for (auto& child : node.children)
      FlattenNode(child, depth + 1);
  }
}

void SizeFileTree::OpenAncestors(TreeNode* target) {
  if (!target || target == &root_)
    return;
  // Walk from root, opening each ancestor along the path.
  TreeNode* current = &root_;
  current->open = true;
  size_t start = 0;
  while (start < target->full_path.size()) {
    auto slash = target->full_path.find('/', start);
    std::string component =
        (slash == std::string::npos)
            ? target->full_path.substr(start)
            : target->full_path.substr(start, slash - start);
    for (auto& child : current->children) {
      if (child.name == component) {
        if (child.is_directory && &child != target)
          child.open = true;
        current = &child;
        break;
      }
    }
    if (slash == std::string::npos)
      break;
    start = slash + 1;
  }
}

void SizeFileTree::RenderSortButtons(float btn_size) {
  struct SortButton {
    SizeTreeSort sort;
    const char* id;
    void (*draw_icon)();
    const char* tooltip;
  };
  static const SortButton kButtons[] = {
      {SizeTreeSort::kName, "##sort_name", DrawSortByNameIcon, "Sort by name"},
      {SizeTreeSort::kSize, "##sort_size", DrawSortBySizeIcon, "Sort by size"},
  };
  constexpr int kNumButtons = 2;

  float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    sort_tooltip_suppressed_ = true;
  bool any_hovered = false;

  for (int i = 0; i < kNumButtons; i++) {
    auto& btn = kButtons[i];
    bool active = (settings_.size_tree_sort == btn.sort);
    ToggleButton(btn.id, active, ImVec2(btn_size, btn_size));
    btn.draw_icon();
    if (ImGui::IsItemClicked()) {
      settings_.size_tree_sort = btn.sort;
      settings_.Save();
      SortTree();
      FlattenTree();
    }
    ItemTooltip(btn.tooltip, any_hovered, sort_tooltip_suppressed_);
    if (i < kNumButtons - 1)
      ImGui::SameLine(0, spacing);
  }

  if (!any_hovered)
    sort_tooltip_suppressed_ = false;
}

void SizeFileTree::RenderFileTree() {
  float indent = ImGui::GetFontSize() * 0.75f;
  float arrow_width = ImGui::GetFontSize();
  float base_x = ImGui::GetStyle().FramePadding.x;

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(flat_tree_.size()));

  if (scroll_to_row_ >= 0 &&
      scroll_to_row_ < static_cast<int>(flat_tree_.size()))
    clipper.IncludeItemByIndex(scroll_to_row_);

  bool need_reflatten = false;

  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
      auto& row = flat_tree_[i];
      auto* node = row.node;

      ImGui::PushID(i);

      float indent_px = row.depth * indent;

      if (node->is_directory) {
        // Draw selectable background spanning full width.
        ImVec2 cursor = ImGui::GetCursorPos();
        bool is_selected = (node == selected_dir_);
        if (ImGui::Selectable("##dir", is_selected,
                              ImGuiSelectableFlags_AllowOverlap | 0)) {
          SelectDirectory(node);
        }
        // Toggle open on double-click.
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          node->open = !node->open;
          need_reflatten = true;
        }

        ImVec2 item_min = ImGui::GetItemRectMin();
        ImVec2 item_max = ImGui::GetItemRectMax();

        if (i == scroll_to_row_) {
          ImGui::SetScrollHereY();
          scroll_to_row_ = -1;
        }

        // Render arrow + label on top of the selectable.
        ImGui::SetCursorPos(cursor);
        ImGui::PushClipRect(item_min, item_max, true);
        ImGui::SetCursorPosX(base_x + indent_px);

        // Draw arrow.
        auto* dl = ImGui::GetWindowDrawList();
        ImVec2 arrow_pos = ImGui::GetCursorScreenPos();
        float arrow_sz = ImGui::GetFontSize() * 0.35f;
        ImVec2 arrow_center(arrow_pos.x + arrow_width * 0.5f,
                            arrow_pos.y + ImGui::GetTextLineHeight() * 0.5f);
        ImU32 arrow_col = ImGui::GetColorU32(ImGuiCol_Text);
        if (node->open) {
          // Down arrow.
          dl->AddTriangleFilled(
              ImVec2(arrow_center.x - arrow_sz,
                     arrow_center.y - arrow_sz * 0.5f),
              ImVec2(arrow_center.x + arrow_sz,
                     arrow_center.y - arrow_sz * 0.5f),
              ImVec2(arrow_center.x, arrow_center.y + arrow_sz * 0.5f),
              arrow_col);
        } else {
          // Right arrow.
          dl->AddTriangleFilled(
              ImVec2(arrow_center.x - arrow_sz * 0.5f,
                     arrow_center.y - arrow_sz),
              ImVec2(arrow_center.x + arrow_sz * 0.5f, arrow_center.y),
              ImVec2(arrow_center.x - arrow_sz * 0.5f,
                     arrow_center.y + arrow_sz),
              arrow_col);
        }

        // Click on arrow area toggles open.
        ImGui::SetCursorScreenPos(arrow_pos);
        ImGui::InvisibleButton("##arrow",
                               ImVec2(arrow_width, ImGui::GetTextLineHeight()));
        if (ImGui::IsItemClicked()) {
          node->open = !node->open;
          need_reflatten = true;
        }
        ImGui::SameLine(0, 0);

        char label[512];
        snprintf(label, sizeof(label), "%s/ (%s)", node->name.c_str(),
                 HumanSize(node->size).c_str());
        ImGui::TextUnformatted(label);
        ImGui::PopClipRect();

        // Overlay collapse + sort buttons right-aligned on the root row.
        if (node == &root_) {
          float btn_sz = ImGui::GetTextLineHeight();
          float btn_spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;
          float btns_w = 3.0f * btn_sz + 2.0f * btn_spacing;
          float right_edge = ImGui::GetWindowContentRegionMax().x;
          ImGui::SetCursorPos(ImVec2(right_edge - btns_w, cursor.y));

          ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
          bool collapse_hovered = false;
          ImGui::Button("##collapse", ImVec2(btn_sz, btn_sz));
          DrawCollapseAllIcon();
          if (ImGui::IsItemClicked()) {
            std::function<void(TreeNode&)> collapse = [&](TreeNode& n) {
              if (!n.is_directory)
                return;
              n.open = false;
              for (auto& child : n.children)
                collapse(child);
            };
            for (auto& child : root_.children)
              collapse(child);
            root_.open = true;
            need_reflatten = true;
          }
          ItemTooltip("Collapse all", collapse_hovered,
                      sort_tooltip_suppressed_);
          ImGui::SameLine(0, btn_spacing);

          RenderSortButtons(btn_sz);
          ImGui::PopStyleVar();
        }
      } else {
        // Leaf file node.
        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::Selectable("##file", false, 0);
        ImVec2 item_min = ImGui::GetItemRectMin();
        ImVec2 item_max = ImGui::GetItemRectMax();
        ImGui::SetCursorPos(cursor);
        ImGui::PushClipRect(item_min, item_max, true);
        ImGui::SetCursorPosX(base_x + indent_px + arrow_width);

        char label[512];
        snprintf(label, sizeof(label), "%s (%s)", node->name.c_str(),
                 HumanSize(node->size).c_str());
        ImGui::TextUnformatted(label);
        ImGui::PopClipRect();
      }

      ImGui::PopID();
    }
  }

  if (need_reflatten)
    FlattenTree();
}

void SizeFileTree::Update() {
  // Handle deferred scroll-to-selected: open ancestors and reflatten
  // so the target row exists in flat_tree_ before the clipper runs.
  if (scroll_to_selected_) {
    scroll_to_selected_ = false;
    OpenAncestors(selected_dir_);
    FlattenTree();
    // Find the row index of the selected directory.
    for (int i = 0; i < static_cast<int>(flat_tree_.size()); i++) {
      if (flat_tree_[i].node == selected_dir_) {
        scroll_to_row_ = i;
        break;
      }
    }
  }

  if (HasData())
    RenderFileTree();
}
