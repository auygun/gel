// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_DIFF_H
#define GEL_COMMANDS_GIT_DIFF_H

#include <atomic>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "base/double_buffer.h"
#include "gel/commands/git.h"
#include "gel/ui/syntax_highlight.h"

class GitRepo;

// Conflict zone transition. Sorted by line index. The zone field gives the
// conflict state from that line onward: 0 = none, 1 = current, 2 = incoming.
struct ConflictTransition {
  int line = 0;
  int zone = 0;
};

// Block comment state transition. Sorted by line index.
// Old side = '-' and ' ' lines, new side = '+' and ' ' lines.
// Both states reset at hunk boundaries (@@) and file boundaries.
struct SyntaxTransition {
  int line = 0;
  bool old_in_block_comment = false;
  bool new_in_block_comment = false;
};

struct DiffLineNumbers {
  int old_line = 0;
  int new_line = 0;
};

// Hunk header entry. Sorted by start_line. Supports binary search:
// the last hunk whose start_line <= line gives the hunk covering that line.
struct HunkEntry {
  size_t start_line = 0;
  int at_count = 0;  // Number of '@' chars (2 = @@, 3 = @@@, etc.)
};

enum class FileStatus {
  kCommit,
  kModified,
  kAdded,
  kDeleted,
  kRenamed,
  kCopied,
  kSubmodule,
};

struct FileEntry {
  FileStatus status = FileStatus::kModified;
  std::string path;
  std::string old_path;
  size_t start_line = 0;
};

class GitDiff final : public Git {
 public:
  explicit GitDiff(std::function<void(bool)> busy_callback);
  ~GitDiff() final;

  GitDiff(GitDiff const&) = delete;
  GitDiff& operator=(GitDiff const&) = delete;

  // Sets a path filter (e.g. {"src/foo.cc"}) appended to every command.
  void SetPathFilter(std::vector<std::string> args);
  void ClearPathFilter() { path_filter_.clear(); }
  bool HasPathFilter() const { return !path_filter_.empty(); }

  // Run the diff-tree command with optional extra arguments (e.g. a commit
  // hash).
  bool RunTree(std::vector<std::string> extra_args = {});

  // Run a plain "git diff" to show unstaged changes. Untracked files are
  // discovered internally via git-ls-files and appended after the diff.
  bool RunUnstaged();

  // Run "git diff --cached" to show staged changes.
  bool RunStaged();

  void SetGitRepo(GitRepo& git_repo) { git_repo_ = &git_repo; }

  void SetExtraArgs(std::vector<std::string> args) {
    extra_args_ = std::move(args);
  }

  void SetLinesOfContext(int n) { lines_of_context_ = n; }
  int GetLinesOfContext() const { return lines_of_context_; }

  void Update();

  bool DidClear() const { return did_clear_; }

  const std::string& GetLastRunCommitHash() const {
    return last_run_commit_hash_;
  }
  bool IsLastRunUnstaged() const { return last_run_mode_ == Mode::kUnstaged; }
  bool IsLastRunStaged() const { return last_run_mode_ == Mode::kStaged; }

  std::span<const std::string> GetDiffContent() const {
    return buffers_.data<std::string>();
  }

  std::span<const FileEntry> GetFileList() const {
    return buffers_.data<FileEntry>();
  }

  std::span<const ConflictTransition> GetConflictTransitions() const {
    return buffers_.data<ConflictTransition>();
  }

  std::span<const SyntaxTransition> GetSyntaxTransitions() const {
    return buffers_.data<SyntaxTransition>();
  }

  void SetExtMappings(
      std::vector<std::pair<std::string, std::string>> mappings);

  std::span<const DiffLineNumbers> GetLineNumbers() const {
    return buffers_.data<DiffLineNumbers>();
  }

  std::span<const HunkEntry> GetHunkEntries() const {
    return buffers_.data<HunkEntry>();
  }

 private:
  int lines_of_context_ = 3;
  GitRepo* git_repo_ = nullptr;
  std::vector<std::string> extra_args_;
  std::vector<std::string> path_filter_;
  // diff lines, file list, hunk entries, conflict transitions, syntax
  // transitions, line numbers.
  DoubleBuffer<std::string,
               FileEntry,
               HunkEntry,
               ConflictTransition,
               SyntaxTransition,
               DiffLineNumbers>
      buffers_;
  bool did_clear_ = false;

  enum class Mode { kTree, kStaged, kUnstaged };

  // Main-thread only. Set by Run*() to track what the diff is showing.
  std::string last_run_commit_hash_;
  Mode last_run_mode_ = Mode::kTree;

  // Main-thread state passed to the worker through pending_params_.
  struct Params {
    Mode mode = Mode::kTree;
    std::string commit_hash;
    std::filesystem::path toplevel;
    std::vector<std::string> path_filter;
    std::vector<std::pair<std::string, std::string>> ext_mappings;
  };
  std::mutex params_lock_;
  Params pending_params_;

  // Worker-thread state for parsing file headers from diff output.
  FileEntry pending_entry_;
  bool has_pending_entry_ = false;
  Mode mode_ = Mode::kTree;
  std::string commit_hash_;
  std::filesystem::path toplevel_;
  std::vector<std::string> path_filter_worker_;
  size_t line_count_ = 0;
  bool last_line_empty_ = false;
  bool old_in_block_comment_ = false;
  bool new_in_block_comment_ = false;
  Language worker_lang_ = Language::kNone;
  std::vector<std::pair<std::string, std::string>> ext_mappings_worker_;

  // Worker-side storage for precomputing line numbers.
  int current_old_line_ = 0;
  int current_new_line_ = 0;
  bool in_hunk_ = false;
  // '@' count of the current hunk header (2 = @@, 3+ = combined diff). Used
  // to gate conflict marker detection, which only applies to the two-prefix-
  // character combined diff format used for merge commits.
  int current_hunk_at_count_ = 2;

  void FlushPendingEntry();
  void AppendUntrackedFiles(const std::vector<std::string>& paths);

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status, int result, std::string err) final;
  void OnKilled() final;

 public:
};

#endif  // GEL_COMMANDS_GIT_DIFF_H
