// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_UPPER_PANEL_COMMIT_MODAL_H
#define GEL_UI_UPPER_PANEL_COMMIT_MODAL_H

#include <cstddef>
#include <string>

class GitCmdRunner;
class GitRepo;

// Modal dialog for creating a commit from staged changes.
class CommitModal {
 public:
  CommitModal(GitCmdRunner& runner, GitRepo& git_repo);

  // Open the commit modal. If |amend| is true, prefill with the body of the
  // latest commit (skipping the subject line).
  void Open(bool amend = false);

  // Render the modal popup. Call once per frame.
  void Render();

 private:
  GitCmdRunner& runner_;
  GitRepo& git_repo_;

  bool pending_ = false;
  bool focus_input_ = false;
  char message_[4096] = {};
  char saved_message_[4096] = {};
  bool amend_ = false;
  std::string amend_message_;
};

#endif  // GEL_UI_UPPER_PANEL_COMMIT_MODAL_H
