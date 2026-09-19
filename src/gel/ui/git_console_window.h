// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_GIT_CONSOLE_WINDOW_H
#define GEL_UI_GIT_CONSOLE_WINDOW_H

#include <string>
#include <vector>

#include "gel/persistent_settings.h"
#include "gel/ui/git_cmd_runner.h"
#include "gel/ui/modules/text_viewer.h"

// Git console window: displays output from in-progress git tasks
// (cherry-pick, revert, rebase) and provides Continue/Abort controls.
class GitConsoleWindow : public TextViewer::Delegate {
 public:
  GitConsoleWindow(GitCmdRunner& runner, PersistentSettings& settings);

  void Open();
  void Toggle();
  void Update(bool window_focused);
  bool IsOpen() const { return open_; }

 private:
  enum class LineType { kDefault, kCommand, kInfo };

  // TextViewer::Delegate implementation.
  void RenderLine(int line_index, const std::string& line) override;

  void AppendLine(std::string text, LineType type);
  void RebuildLines();
  void Clear();

  GitCmdRunner& runner_;
  PersistentSettings& settings_;

  TextViewer text_viewer_;

  bool open_ = false;
  bool show_ = false;
  bool uncollapse_ = false;
  bool restore_pending_ = true;
  base::MatchOptions search_options_;
  TaskState last_state_ = TaskState::kIdle;

  std::vector<std::string> lines_;
  std::vector<LineType> line_types_;
};

#endif  // GEL_UI_GIT_CONSOLE_WINDOW_H
