// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/directory_browser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>

#include "gel/ui/icons.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"

DirectoryBrowser::DirectoryBrowser(Delegate& delegate) : delegate_(delegate) {}

void DirectoryBrowser::Open() {
  root_ = {};
  selected_ = nullptr;
  flat_tree_.clear();

  scroll_to_row_ = -1;
  path_input_[0] = '\0';

#if defined(OS_WIN)
  root_.name = "My Computer";
  root_.full_path = "";
  root_.is_open = true;
  root_.children_loaded = true;
  for (char drive = 'A'; drive <= 'Z'; ++drive) {
    std::string drive_path = std::string(1, drive) + ":\\";
    std::error_code ec;
    if (std::filesystem::exists(drive_path, ec)) {
      DirNode child;
      child.name = drive_path;
      child.full_path = drive_path;
      child.is_git_root = std::filesystem::exists(
          std::filesystem::path(drive_path) / ".git", ec);
      root_.children.push_back(std::move(child));
    }
  }
#else
  root_.name = "/";
  root_.full_path = "/";
  root_.is_open = true;
  LoadChildren(root_);
#endif

  ExpandToPath(std::filesystem::current_path());
  FlattenTree();

  open_ = true;
  show_ = true;
}

void DirectoryBrowser::Close() {
  if (open_) {
    ImGui::CloseCurrentPopup();
    open_ = false;
  }
}

void DirectoryBrowser::SortChildren(DirNode& node) {
  auto order = sort_order_;
  std::sort(
      node.children.begin(), node.children.end(),
      [order](const DirNode& a, const DirNode& b) {
        if (order == SortOrder::kGitFirst && a.IsGitRepo() != b.IsGitRepo())
          return a.IsGitRepo() > b.IsGitRepo();
        return std::lexicographical_compare(
            a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
            [](char c1, char c2) {
              return std::tolower(static_cast<unsigned char>(c1)) <
                     std::tolower(static_cast<unsigned char>(c2));
            });
      });
}

void DirectoryBrowser::LoadChildren(DirNode& node) {
  if (node.children_loaded)
    return;
  node.children_loaded = true;

  namespace fs = std::filesystem;
  std::error_code ec;
  auto it = fs::directory_iterator(
      node.full_path, fs::directory_options::skip_permission_denied, ec);
  if (ec)
    return;

  for (auto& entry : it) {
    if (!entry.is_directory(ec) || ec)
      continue;
    auto name = entry.path().filename().string();
    if (!name.empty() && name[0] == '.')
      continue;
    DirNode child;
    child.name = std::move(name);
    child.full_path = entry.path();
    child.is_git_root = fs::exists(entry.path() / ".git", ec);
    child.is_inside_repo = !child.is_git_root && node.IsGitRepo();
    node.children.push_back(std::move(child));
  }

  SortChildren(node);
}

void DirectoryBrowser::ResortAll(DirNode& node) {
  if (!node.children_loaded)
    return;
  SortChildren(node);
  for (auto& child : node.children)
    ResortAll(child);
}

void DirectoryBrowser::NavigateToPath(const std::filesystem::path& path) {
  namespace fs = std::filesystem;
  std::error_code ec;
  auto canonical = fs::canonical(path, ec);
  if (ec || !fs::is_directory(canonical, ec) || ec)
    return;

  // Reset all nodes except root.
  root_.children.clear();
  root_.children_loaded = false;
  root_.is_open = true;
  LoadChildren(root_);

  ExpandToPath(canonical);
  FlattenTree();

  if (selected_) {
    for (int i = 0; i < static_cast<int>(flat_tree_.size()); i++) {
      if (flat_tree_[i].node == selected_) {
        scroll_to_row_ = i;
        break;
      }
    }
  }
}

void DirectoryBrowser::ExpandToPath(const std::filesystem::path& path) {
  DirNode* current = &root_;
  auto rel = path.lexically_relative(root_.full_path);
  if (rel.empty())
    return;

  for (const auto& component : rel) {
    if (component == ".")
      continue;
    auto comp_str = component.string();
    LoadChildren(*current);
    current->is_open = true;

    DirNode* found = nullptr;
    for (auto& child : current->children) {
      if (child.name == comp_str) {
        found = &child;
        break;
      }
    }
    if (!found)
      break;
    current = found;
  }
  selected_ = current;
  current->is_open = true;
  LoadChildren(*current);
}

void DirectoryBrowser::CreateDirectory() {
  if (!selected_)
    return;

  namespace fs = std::filesystem;
  std::string base_name = "New Folder";
  fs::path new_path = selected_->full_path / base_name;
  std::error_code ec;
  int suffix = 2;
  while (fs::exists(new_path, ec)) {
    new_path =
        selected_->full_path / (base_name + " " + std::to_string(suffix));
    ++suffix;
  }

  if (!fs::create_directory(new_path, ec) || ec)
    return;

  // Reload children of the selected node.
  selected_->children.clear();
  selected_->children_loaded = false;
  selected_->is_open = true;
  LoadChildren(*selected_);
  FlattenTree();

  // Select the new directory and start renaming.
  auto new_name = new_path.filename().string();
  for (auto& child : selected_->children) {
    if (child.name == new_name) {
      selected_ = &child;
      renaming_ = &child;
      snprintf(rename_input_, sizeof(rename_input_), "%s", new_name.c_str());
      snprintf(path_input_, sizeof(path_input_), "%s",
               child.full_path.string().c_str());
      rename_focus_ = true;
      break;
    }
  }

  for (int i = 0; i < static_cast<int>(flat_tree_.size()); i++) {
    if (flat_tree_[i].node == selected_) {
      scroll_to_row_ = i;
      break;
    }
  }
}

void DirectoryBrowser::DeleteDirectory() {
  if (!selected_ || selected_ == &root_)
    return;

  namespace fs = std::filesystem;
  std::error_code ec;
  fs::remove(selected_->full_path, ec);

  // Find parent and remove the child.
  DirNode* parent = nullptr;
  for (auto& row : flat_tree_) {
    for (auto& child : row.node->children) {
      if (&child == selected_) {
        parent = row.node;
        break;
      }
    }
    if (parent)
      break;
  }

  if (parent) {
    selected_ = parent;
    parent->children.clear();
    parent->children_loaded = false;
    LoadChildren(*parent);
    FlattenTree();
    focus_tree_ = true;
    snprintf(path_input_, sizeof(path_input_), "%s",
             parent->full_path.string().c_str());
  }
}

void DirectoryBrowser::CommitRename() {
  if (!renaming_)
    return;

  namespace fs = std::filesystem;
  std::string new_name(rename_input_);
  if (new_name.empty() || new_name == renaming_->name) {
    renaming_ = nullptr;
    return;
  }

  fs::path old_path = renaming_->full_path;
  fs::path new_path = old_path.parent_path() / new_name;
  std::error_code ec;
  fs::rename(old_path, new_path, ec);
  if (ec) {
    renaming_ = nullptr;
    return;
  }

  renaming_->name = new_name;
  renaming_->full_path = new_path;

  // Re-sort the parent since the name changed.
  DirNode* parent = nullptr;
  for (auto& row : flat_tree_) {
    for (auto& child : row.node->children) {
      if (&child == renaming_) {
        parent = row.node;
        break;
      }
    }
    if (parent)
      break;
  }
  renaming_ = nullptr;
  if (parent) {
    SortChildren(*parent);
    // Re-find selected_ since sort invalidates pointers into the vector.
    selected_ = nullptr;
    for (auto& child : parent->children) {
      if (child.full_path == new_path) {
        selected_ = &child;
        break;
      }
    }
  }
  FlattenTree();

  if (selected_)
    snprintf(path_input_, sizeof(path_input_), "%s",
             selected_->full_path.string().c_str());
}

void DirectoryBrowser::CancelRename() {
  if (!renaming_)
    return;
  renaming_ = nullptr;
}

void DirectoryBrowser::FlattenTree() {
  flat_tree_.clear();
  FlattenNode(root_, 0);
}

void DirectoryBrowser::FlattenNode(DirNode& node, int depth) {
  flat_tree_.push_back({&node, depth});
  if (node.is_open) {
    for (auto& child : node.children)
      FlattenNode(child, depth + 1);
  }
}

void DirectoryBrowser::RenderSortButtons() {
  float btn_sz = ImGui::GetFrameHeight();
  float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    sort_tooltip_suppressed_ = true;
  bool any_hovered = false;

  struct SortButton {
    SortOrder order;
    const char* id;
    void (*draw_icon)();
    const char* tooltip;
  };
  static const SortButton kButtons[] = {
      {SortOrder::kGitFirst, "##sort_git", DrawGitFirstIcon, "Git repos first"},
      {SortOrder::kName, "##sort_name", DrawSortByNameIcon, "Sort by name"},
  };

  for (int i = 0; i < 2; i++) {
    auto& btn = kButtons[i];
    bool active = (sort_order_ == btn.order);
    bool clicked = ToggleButton(btn.id, active, ImVec2(btn_sz, btn_sz));
    btn.draw_icon();
    if (clicked) {
      sort_order_ = btn.order;
      ResortAll(root_);
      FlattenTree();
    }
    ItemTooltip(btn.tooltip, any_hovered, sort_tooltip_suppressed_);
    if (i < 1)
      ImGui::SameLine(0, spacing);
  }

  if (!any_hovered)
    sort_tooltip_suppressed_ = false;
}

void DirectoryBrowser::RenderTree() {
  ImGui::PushTabStop(false);
  float indent = ImGui::GetFontSize() * 0.75f;
  float arrow_width = ImGui::GetFontSize();
  float icon_width = ImGui::GetFontSize() * 1.2f;
  float base_x = ImGui::GetStyle().FramePadding.x;

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(flat_tree_.size()));

  if (scroll_to_row_ >= 0 &&
      scroll_to_row_ < static_cast<int>(flat_tree_.size()))
    clipper.IncludeItemByIndex(scroll_to_row_);

  bool need_reflatten = false;
  bool action_triggered = false;

  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
      auto& row = flat_tree_[i];
      auto* node = row.node;

      ImGui::PushID(i);

      float indent_px = row.depth * indent;

      ImVec2 cursor = ImGui::GetCursorPos();
      bool is_selected = (node == selected_);
      if (focus_tree_ && is_selected)
        ImGui::SetKeyboardFocusHere();
      if (ImGui::Selectable("##dir", is_selected,
                            ImGuiSelectableFlags_AllowOverlap)) {
        if (renaming_ && node != renaming_)
          CancelRename();
        selected_ = node;
        snprintf(path_input_, sizeof(path_input_), "%s",
                 node->full_path.string().c_str());

        // Click on the arrow area toggles expand/collapse.
        float arrow_end = ImGui::GetItemRectMin().x + base_x +
                          row.depth * indent + arrow_width;
        if (ImGui::GetMousePos().x < arrow_end) {
          bool has = !node->children_loaded || !node->children.empty();
          if (has) {
            node->is_open = !node->is_open;
            if (node->is_open)
              LoadChildren(*node);
            need_reflatten = true;
          }
        }
      }

      if (ImGui::IsItemHovered() &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (node->is_git_root) {
          action_triggered = true;
        } else if (!node->children.empty() || !node->children_loaded) {
          node->is_open = !node->is_open;
          if (node->is_open)
            LoadChildren(*node);
          need_reflatten = true;
        }
      }

      if (i == scroll_to_row_) {
        ImGui::SetScrollHereY();
        scroll_to_row_ = -1;
      }

      ImVec2 item_min = ImGui::GetItemRectMin();
      ImVec2 item_max = ImGui::GetItemRectMax();

      ImGui::SetCursorPos(cursor);
      ImGui::PushClipRect(item_min, item_max, true);
      ImGui::SetCursorPosX(base_x + indent_px);

      // Expand/collapse arrow.
      bool has_children = !node->children_loaded || !node->children.empty();
      if (has_children) {
        auto* dl = ImGui::GetWindowDrawList();
        ImVec2 arrow_pos = ImGui::GetCursorScreenPos();
        float arrow_sz = ImGui::GetFontSize() * 0.35f;
        ImVec2 arrow_center(arrow_pos.x + arrow_width * 0.5f,
                            arrow_pos.y + ImGui::GetTextLineHeight() * 0.5f);
        ImU32 arrow_col = ImGui::GetColorU32(ImGuiCol_Text);
        if (node->is_open) {
          dl->AddTriangleFilled(
              ImVec2(arrow_center.x - arrow_sz,
                     arrow_center.y - arrow_sz * 0.5f),
              ImVec2(arrow_center.x + arrow_sz,
                     arrow_center.y - arrow_sz * 0.5f),
              ImVec2(arrow_center.x, arrow_center.y + arrow_sz * 0.5f),
              arrow_col);
        } else {
          dl->AddTriangleFilled(
              ImVec2(arrow_center.x - arrow_sz * 0.5f,
                     arrow_center.y - arrow_sz),
              ImVec2(arrow_center.x + arrow_sz * 0.5f, arrow_center.y),
              ImVec2(arrow_center.x - arrow_sz * 0.5f,
                     arrow_center.y + arrow_sz),
              arrow_col);
        }

        ImGui::SetCursorScreenPos(arrow_pos);
        ImGui::Dummy(ImVec2(arrow_width, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0, 0);
      } else {
        ImGui::SetCursorPosX(base_x + indent_px + arrow_width);
      }

      // Folder icon.
      ImGui::Dummy(ImVec2(icon_width, ImGui::GetTextLineHeight()));
      if (node->is_git_root)
        DrawGitFolderIcon();
      else
        DrawFolderIcon();
      ImGui::SameLine(0, 0);

      if (node == renaming_) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (rename_focus_) {
          ImGui::SetKeyboardFocusHere();
          rename_focus_ = false;
        }
        if (ImGui::InputText("##rename", rename_input_, sizeof(rename_input_),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_AutoSelectAll)) {
          CommitRename();
        } else if (!rename_focus_ && !ImGui::IsItemActive() &&
                   ImGui::IsItemDeactivated()) {
          CancelRename();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
          CancelRename();
      } else {
        ImGui::TextUnformatted(node->name.c_str());
      }
      ImGui::PopClipRect();

      ImGui::PopID();
    }
  }

  focus_tree_ = false;

  if (selected_ && !ImGui::IsAnyItemActive() && ImGui::IsWindowFocused()) {
    if (!action_triggered && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      if (selected_->is_git_root) {
        action_triggered = true;
      } else {
        selected_->is_open = !selected_->is_open;
        if (selected_->is_open)
          LoadChildren(*selected_);
        need_reflatten = true;
      }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !selected_->is_open) {
      selected_->is_open = true;
      LoadChildren(*selected_);
      need_reflatten = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && selected_->is_open) {
      selected_->is_open = false;
      need_reflatten = true;
    }
  }

  if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Tab)) {
    if (ImGui::GetIO().KeyShift)
      focus_path_input_ = true;
    else
      focus_after_tree_ = true;
  }

  ImGui::PopTabStop();

  if (need_reflatten)
    FlattenTree();

  if (action_triggered && selected_ && selected_->is_git_root)
    delegate_.OnDirectorySelected(selected_->full_path);
}

void DirectoryBrowser::Update(bool window_focused) {
  if (!open_)
    return;

  if (show_) {
    ImGui::OpenPopup("Select Repository");
    show_ = false;
    focus_tree_ = true;

    if (selected_) {
      snprintf(path_input_, sizeof(path_input_), "%s",
               selected_->full_path.string().c_str());
      for (int i = 0; i < static_cast<int>(flat_tree_.size()); i++) {
        if (flat_tree_[i].node == selected_) {
          scroll_to_row_ = i;
          break;
        }
      }
    }
  }

  ImVec2 display = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  float w = std::min(display.x * 0.8f, 700.0f);
  float h = std::min(display.y * 0.8f, 600.0f);
  ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

  if (!window_focused)
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));

  bool stay_open = true;
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
  if (ImGui::BeginPopupModal("Select Repository", &stay_open, flags)) {
    // Path input bar + sort buttons on the same line.
    {
      float btn_sz = ImGui::GetFrameHeight();
      float spacing = ImGui::GetStyle().ItemSpacing.x;
      float btns_w = 2.0f * btn_sz + spacing * 0.5f;
      float input_w = ImGui::GetContentRegionAvail().x - btns_w - spacing;
      ImGui::SetNextItemWidth(input_w);
      if (focus_path_input_) {
        ImGui::SetKeyboardFocusHere();
        focus_path_input_ = false;
      }
      if (ImGui::InputText("##path", path_input_, sizeof(path_input_),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        NavigateToPath(path_input_);
      }
      InputTextContextMenu(path_input_, sizeof(path_input_));
      ImGui::SameLine();
      RenderSortButtons();
    }

    // Tree view.
    float reserved = ImGui::GetFrameHeightWithSpacing() * 2;
    ImGui::BeginChild("##dir_tree", ImVec2(0, -reserved));
    RenderTree();
    ImGui::EndChild();

    if (focus_after_tree_) {
      ImGui::SetKeyboardFocusHere();
      focus_after_tree_ = false;
    }

    // Action buttons.
    if (selected_) {
      if (selected_->is_git_root) {
        if (ImGui::Button("Open", ImVec2(150, 0))) {
          delegate_.OnDirectorySelected(selected_->full_path);
        }
      } else {
        if (ImGui::Button("Create Repository", ImVec2(150, 0))) {
          delegate_.OnCreateRepository(selected_->full_path);
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("New Folder", ImVec2(150, 0))) {
        CreateDirectory();
      }
      ImGui::SameLine();
      {
        std::error_code ec;
        bool can_delete = selected_ != &root_ &&
                          std::filesystem::is_empty(selected_->full_path, ec);
        if (!can_delete)
          ImGui::BeginDisabled();
        if (ImGui::Button("Delete", ImVec2(150, 0)))
          DeleteDirectory();
        if (!can_delete)
          ImGui::EndDisabled();
      }
    }

    ImGui::EndPopup();
  }

  if (!window_focused)
    ImGui::PopStyleColor();

  if (!stay_open) {
    open_ = false;
    delegate_.OnBrowserDismissed();
  }
}
