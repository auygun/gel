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
#include "gel/commands/merge_diff_mode.h"
#include "gel/ui/syntax_highlight.h"

class GitRepo;

// Conflict zone transition. Sorted by line index. The zone field gives the
// conflict state from that line onward: 0 = none, 1 = current, 2 = incoming.
struct ConflictTransition {
  int line = 0;
  int zone = 0;
};

// Multi-line syntax state transition (block comments, multi-line strings).
// Sorted by line index. Old side = '-' and ' ' lines, new side = '+' and ' '
// lines. Both states reset at hunk boundaries (@@) and file boundaries.
struct SyntaxTransition {
  int line = 0;
  SyntaxState old_state;
  SyntaxState new_state;
};

// Line numbers of a diff line, 0 on a side that does not contain it.
struct DiffLineNumbers {
  // Line number in the old side. In a combined diff (merge commit) this is the
  // first parent.
  int old_line = 0;
  int new_line = 0;
  // Combined diffs only: index into the parent line numbers of this line's
  // entries for parents 2..N (at_count - 2 entries), or -1.
  int extra_parents = -1;
};

// Line number of a combined diff line in one of the parents after the first,
// 0 if that parent does not contain it.
struct ParentLineNumber {
  int line = 0;
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

  void SetMergeDiffMode(MergeDiffMode mode) { merge_diff_mode_ = mode; }
  MergeDiffMode GetMergeDiffMode() const { return merge_diff_mode_; }

  // Main thread: mode the last RunTree() used. Only meaningful for a merge.
  MergeDiffMode GetLastRunMergeDiffMode() const {
    return last_run_merge_diff_mode_;
  }

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

  // Line numbers in parents 2..N of combined diff lines, indexed by
  // DiffLineNumbers::extra_parents.
  std::span<const ParentLineNumber> GetParentLineNumbers() const {
    return buffers_.data<ParentLineNumber>();
  }

  std::span<const HunkEntry> GetHunkEntries() const {
    return buffers_.data<HunkEntry>();
  }

 private:
  int lines_of_context_ = 3;
  MergeDiffMode merge_diff_mode_ = MergeDiffMode::kCombined;
  GitRepo* git_repo_ = nullptr;
  std::vector<std::string> extra_args_;
  std::vector<std::string> path_filter_;
  // diff lines, file list, hunk entries, conflict transitions, syntax
  // transitions, line numbers, line numbers in parents 2..N of combined diffs.
  DoubleBuffer<std::string,
               FileEntry,
               HunkEntry,
               ConflictTransition,
               SyntaxTransition,
               DiffLineNumbers,
               ParentLineNumber>
      buffers_;
  bool did_clear_ = false;

  enum class Mode { kTree, kStaged, kUnstaged };

  // Main-thread only. Set by Run*() to track what the diff is showing.
  std::string last_run_commit_hash_;
  Mode last_run_mode_ = Mode::kTree;
  MergeDiffMode last_run_merge_diff_mode_ = MergeDiffMode::kCombined;

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
  SyntaxState old_syntax_state_;
  SyntaxState new_syntax_state_;
  Language worker_lang_ = Language::kNone;
  std::vector<std::pair<std::string, std::string>> ext_mappings_worker_;

  // Worker-side storage for precomputing line numbers.
  int current_old_line_ = 0;
  int current_new_line_ = 0;
  // Line numbers in parents 2..N of the current combined diff hunk. Empty for
  // a normal diff hunk.
  std::vector<int> current_extra_parent_lines_;
  // Number of ParentLineNumber entries pushed so far.
  int parent_line_count_ = 0;
  // Sides containing the content line being parsed. Entry i of
  // line_in_extra_parents_ stands for parent i + 2.
  bool line_in_old_ = false;
  bool line_in_result_ = false;
  std::vector<bool> line_in_extra_parents_;
  bool in_hunk_ = false;
  // '@' count of the current hunk header (2 = @@, 3+ = combined diff). Used
  // to gate conflict marker detection, which only applies to the two-prefix-
  // character combined diff format used for merge commits.
  int current_hunk_at_count_ = 2;

  void FlushPendingEntry();
  void AppendUntrackedFiles(const std::vector<std::string>& paths);

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnError(std::string) final {}
  void OnFinished(Exec::Status, int result) final;
  void OnKilled() final;

 public:
};

#endif  // GEL_COMMANDS_GIT_DIFF_H
