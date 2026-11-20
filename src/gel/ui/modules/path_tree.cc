// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/modules/path_tree.h"

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"
#include "third_party/kaliber/base/thread_pool.h"

namespace {

// Indentation per depth level, relative to the font size.
constexpr float kIndent = 0.75f;

}  // namespace

PathTree::PathTree(bool dirs_open_by_default)
    : dirs_open_by_default_(dirs_open_by_default) {}

void PathTree::Clear() {
  if (!dirs_.empty()) {
    // Free the old tree on the thread pool so that a huge one doesn't stall
    // the main thread.
    base::ThreadPool::Get().PostTask(
        HERE, [d = std::move(dirs_), m = std::move(dir_index_)]() {});
  }
  dirs_.clear();
  dir_index_.clear();
  rows_.clear();
  item_row_.clear();
  dirty_ = true;
}

void PathTree::Add(std::string_view path) {
  if (dirs_.empty())
    dirs_.emplace_back();  // Root.

  int index = item_count();

  // Find the deepest existing directory of the item and create the missing
  // ones below it.
  int parent = 0;
  int child = index;
  int dir = 0;
  for (size_t slash = path.find('/'); slash != std::string_view::npos;
       slash = path.find('/', slash + 1)) {
    std::string_view dir_path = path.substr(0, slash);
    if (child == index) {
      if (auto it = dir_index_.find(dir_path); it != dir_index_.end()) {
        dir = parent = it->second;
        continue;
      }
    }
    int new_dir = static_cast<int>(dirs_.size());
    Dir& d = dirs_.emplace_back();
    d.path = dir_path;
    d.parent = dir;
    d.open = dirs_open_by_default_ != toggled_dirs_.contains(dir_path);
    dir_index_.emplace(d.path, new_dir);
    if (child == index)
      child = ~new_dir;  // Topmost new directory.
    else
      dirs_[dir].children.push_back(~new_dir);
    dir = new_dir;
  }
  if (dir != parent)
    dirs_[dir].children.push_back(index);

  // A directory whose only child is a directory is folded into one row with
  // it. Adding a second child unfolds it.
  Dir& p = dirs_[parent];
  bool unfolds = parent != 0 && p.children.size() == 1 && p.children[0] < 0;
  p.children.push_back(child);
  item_row_.push_back(-1);
  if (dirty_)
    return;

  // Hidden in a collapsed directory: no rows change.
  if (parent != 0 && (!p.visible || (!unfolds && !p.open))) {
    HideChild(child, p.row);
    return;
  }

  // Rows can only be updated in place if they change at the end. In
  // depth-first order, the subtree of a directory is at the end if it and
  // all its ancestors are the last child of their parent.
  for (int d = parent; d != 0; d = dirs_[d].parent) {
    if (dirs_[dirs_[d].parent].children.back() != ~d) {
      dirty_ = true;
      return;
    }
  }

  if (unfolds) {
    // Reflatten the row |parent| was folded into, from the first directory
    // of the fold.
    int first = parent;
    while (dirs_[first].parent != 0 &&
           dirs_[dirs_[first].parent].children.size() == 1)
      first = dirs_[first].parent;
    int row = p.row;
    int depth = rows_[row].depth;
    rows_.resize(row);
    FlattenChild(~first, depth);
    return;
  }

  FlattenChild(child, parent == 0 ? 0 : rows_[p.row].depth + 1);
}

void PathTree::FlattenIfDirty() {
  if (dirty_)
    Flatten();
}

int PathTree::RowOfItem(int item) const {
  return item >= 0 && item < item_count() ? item_row_[item] : -1;
}

void PathTree::Flatten() {
  dirty_ = false;
  rows_.clear();
  std::fill(item_row_.begin(), item_row_.end(), -1);
  if (!dirs_.empty())
    FlattenDir(0, 0);
}

void PathTree::FlattenDir(int dir, int depth) {
  for (int child : dirs_[dir].children)
    FlattenChild(child, depth);
}

void PathTree::FlattenChild(int child, int depth) {
  int row = static_cast<int>(rows_.size());
  if (child >= 0) {
    item_row_[child] = row;
    rows_.push_back({child, depth, 0});
    return;
  }
  // Fold a chain of single-directory directories into one row.
  int first = ~child;
  int last = first;
  while (true) {
    Dir& d = dirs_[last];
    d.row = row;
    d.visible = true;
    if (d.children.size() != 1 || d.children[0] >= 0)
      break;
    last = ~d.children[0];
  }
  size_t slash = dirs_[first].path.rfind('/');
  int label_offset =
      slash == std::string::npos ? 0 : static_cast<int>(slash) + 1;
  rows_.push_back({~last, depth, label_offset});
  if (dirs_[last].open)
    FlattenDir(last, depth + 1);
  else
    HideSubtree(last, row);
}

void PathTree::HideSubtree(int dir, int row) {
  for (int child : dirs_[dir].children)
    HideChild(child, row);
}

void PathTree::HideChild(int child, int row) {
  if (child >= 0) {
    item_row_[child] = row;
    return;
  }
  Dir& d = dirs_[~child];
  d.row = row;
  d.visible = false;
  HideSubtree(~child, row);
}

void PathTree::SetDirOpen(int dir, bool open) {
  Dir& d = dirs_[dir];
  d.open = open;
  if (open == dirs_open_by_default_)
    toggled_dirs_.erase(d.path);
  else
    toggled_dirs_.insert(d.path);
  dirty_ = true;
}

void PathTree::SetAllDirsOpen(bool open) {
  dirs_open_by_default_ = open;
  toggled_dirs_.clear();
  for (size_t i = 1; i < dirs_.size(); i++)
    dirs_[i].open = open;
  dirty_ = true;
}

void PathTree::RenderDirRow(const Row& row, bool selected) {
  int dir = ~row.item;
  const Dir& d = dirs_[dir];
  float font_size = ImGui::GetFontSize();
  float indent = font_size * kIndent;

  ImGui::PushID(row.item);

  ImVec2 cursor = ImGui::GetCursorScreenPos();
  // Clicking anywhere on the row expands or collapses the directory. The
  // tree is reflattened after rendering.
  if (ImGui::Selectable("##dir", selected, ImGuiSelectableFlags_AllowOverlap))
    SetDirOpen(dir, !d.open);

  ImVec2 arrow_pos(cursor.x + row.depth * indent, cursor.y);
  ImGui::RenderArrow(ImGui::GetWindowDrawList(),
                     ImVec2(arrow_pos.x, arrow_pos.y + font_size * 0.15f),
                     ImGui::GetColorU32(ImGuiCol_Text),
                     d.open ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
  ImGui::SetCursorScreenPos(
      ImVec2(cursor.x + ItemTextOffset(row.depth), cursor.y));
  ImGui::TextUnformatted(d.path.c_str() + row.label_offset);

  ImGui::PopID();
}

// static
float PathTree::ItemTextOffset(int depth) {
  float font_size = ImGui::GetFontSize();
  return depth * font_size * kIndent + font_size;
}

// static
const char* PathTree::ItemName(const std::string& path) {
  size_t slash = path.rfind('/');
  return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}
