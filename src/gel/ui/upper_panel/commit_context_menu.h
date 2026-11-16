// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_UPPER_PANEL_COMMIT_CONTEXT_MENU_H
#define GEL_UI_UPPER_PANEL_COMMIT_CONTEXT_MENU_H

#include <functional>
#include <string>
#include <vector>

class GitCmdRunner;
struct ImVec2;

// Renders the standard set of tag context menu items (checkout, reset, copy,
// open in new window, delete). Used by both single and collapsed tag menus.
// When tag creation is triggered, |pending_tag_name| and |pending_tag_commit|
// are set so the caller can offer a "force create" retry on failure.
void RenderTagMenuItems(
    GitCmdRunner& runner,
    const std::string& tag_name,
    const std::string& commit,
    char* create_buf,
    size_t create_buf_size,
    const std::function<void(const std::string&)>& open_in_new_window,
    std::string& pending_tag_name,
    std::string& pending_tag_commit);

// Renders the standard set of branch context menu items used by both the
// commit history branch labels and the toolbar branch popup.
void RenderBranchMenuItems(
    GitCmdRunner& runner,
    const std::string& branch_name,
    bool is_remote,
    bool is_head,
    char* rename_buf,
    size_t rename_buf_size,
    char* create_buf,
    size_t create_buf_size,
    const std::function<void(const std::string&)>& open_in_new_window);

// Context menus for the commit history table: commit row right-click menu,
// tag label menus, and branch label menus.
class CommitContextMenu {
 public:
  CommitContextMenu(GitCmdRunner& runner,
                    std::function<void(const std::string&)> open_in_new_window);

  // Renders the right-click context menu for a commit row. Returns true if the
  // popup is open.
  bool RenderCommitMenu(const std::string& commit,
                        const std::string& subject,
                        const ImVec2& window_padding,
                        const ImVec2& item_spacing);

  // Renders context menu for a single tag label.
  void RenderTagMenu(int index,
                     const std::string& tag_name,
                     const std::string& commit,
                     const ImVec2& window_padding,
                     const ImVec2& item_spacing);

  // Renders context menu for collapsed tags.
  void RenderCollapsedTagMenu(const std::vector<std::string>& tags,
                              const std::string& commit,
                              const ImVec2& window_padding,
                              const ImVec2& item_spacing);

  // Renders context menu for a branch label.
  void RenderBranchMenu(int index,
                        const std::string& branch_name,
                        bool is_remote,
                        const std::string& head_branch,
                        const ImVec2& window_padding,
                        const ImVec2& item_spacing);

  // Takes the name and commit of the last "Create tag" request. Called
  // when a "git tag" command fails, to offer forcing it with "git tag -f".
  bool ConsumePendingTagCreate(std::string& name, std::string& commit);

  // Forgets the last "Create tag" request. Called whenever a command in
  // the serialized slot finishes, so a stale request can't be picked up by a
  // later, unrelated "git tag" failure.
  void ClearPendingTagCreate();

 private:
  GitCmdRunner& runner_;
  std::function<void(const std::string&)> open_in_new_window_;
  char tag_create_input_[128] = {};
  char branch_create_input_[128] = {};
  char branch_rename_input_[128] = {};
  std::string pending_tag_name_;
  std::string pending_tag_commit_;
};

#endif  // GEL_UI_UPPER_PANEL_COMMIT_CONTEXT_MENU_H
