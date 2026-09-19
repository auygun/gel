// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_diff.h"

#include <algorithm>
#include <fstream>
#include <string_view>

#include "gel/git_repo.h"
#include "third_party/kaliber/base/log.h"

using namespace base;

namespace {

bool ScanBlockComment(std::string_view code, Language lang, bool in_comment) {
  if (lang == Language::kCLike || lang == Language::kShader) {
    size_t i = 0;
    while (i < code.size()) {
      if (in_comment) {
        size_t end = code.find("*/", i);
        if (end == std::string_view::npos)
          return true;
        in_comment = false;
        i = end + 2;
      } else {
        if (code[i] == '/' && i + 1 < code.size()) {
          if (code[i + 1] == '/')
            return false;
          if (code[i + 1] == '*') {
            in_comment = true;
            i += 2;
            continue;
          }
        }
        if (code[i] == '"' || code[i] == '\'') {
          char q = code[i++];
          while (i < code.size() && code[i] != q) {
            if (code[i] == '\\' && i + 1 < code.size())
              i++;
            i++;
          }
          if (i < code.size())
            i++;
          continue;
        }
        i++;
      }
    }
    return in_comment;
  }
  if (lang == Language::kHtml) {
    size_t i = 0;
    while (i < code.size()) {
      if (in_comment) {
        size_t end = code.find("-->", i);
        if (end == std::string_view::npos)
          return true;
        in_comment = false;
        i = end + 3;
      } else {
        if (i + 3 < code.size() && code[i] == '<' && code[i + 1] == '!' &&
            code[i + 2] == '-' && code[i + 3] == '-') {
          in_comment = true;
          i += 4;
          continue;
        }
        i++;
      }
    }
    return in_comment;
  }
  return false;
}

}  // namespace

GitDiff::GitDiff(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitDiff::~GitDiff() {
  TerminateWorkerThread();
}

void GitDiff::SetPathFilter(std::vector<std::string> args) {
  path_filter_ = std::move(args);
}

void GitDiff::SetExtMappings(
    std::vector<std::pair<std::string, std::string>> mappings) {
  std::scoped_lock lock(params_lock_);
  pending_params_.ext_mappings = std::move(mappings);
}

bool GitDiff::RunTree(std::vector<std::string> extra_args) {
  last_run_mode_ = Mode::kTree;
  last_run_commit_hash_ = extra_args.empty() ? std::string{} : extra_args[0];
  {
    std::scoped_lock lock(params_lock_);
    pending_params_.mode = Mode::kTree;
    pending_params_.commit_hash =
        extra_args.empty() ? std::string{} : extra_args[0];
    pending_params_.toplevel =
        git_repo_ ? git_repo_->toplevel() : std::filesystem::path{};
    pending_params_.path_filter = path_filter_;
  }
  std::string context_flag = "-U" + std::to_string(lines_of_context_);
  std::vector<std::string> args = {"diff-tree",
                                   "-r",
                                   "-p",
                                   "--textconv",
                                   "--submodule",
                                   "-C",
                                   "--cc",
                                   "--pretty=format:commit %H%n"
                                   "Parents: %P%n"
                                   "Author: %an <%ae>  %ad%n"
                                   "Committer: %cn <%ce>  %cd%n"
                                   "%w(0,4,4)%n%B",
                                   context_flag,
                                   "--root",
                                   "--date=format-local:%Y-%m-%d %H:%M:%S"};
  args.insert(args.end(), extra_args_.begin(), extra_args_.end());
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  if (!path_filter_.empty()) {
    args.emplace_back("--");
    args.insert(args.end(), path_filter_.begin(), path_filter_.end());
  }
  return RunWithArgs(std::move(args));
}

bool GitDiff::RunUnstaged() {
  last_run_mode_ = Mode::kUnstaged;
  last_run_commit_hash_.clear();
  {
    std::scoped_lock lock(params_lock_);
    pending_params_.mode = Mode::kUnstaged;
    pending_params_.commit_hash.clear();
    pending_params_.toplevel =
        git_repo_ ? git_repo_->toplevel() : std::filesystem::path{};
    pending_params_.path_filter = path_filter_;
  }
  std::string context_flag = "-U" + std::to_string(lines_of_context_);
  std::vector<std::string> args = {"diff", "-p", "--submodule", context_flag};
  if (!path_filter_.empty()) {
    args.emplace_back("--");
    args.insert(args.end(), path_filter_.begin(), path_filter_.end());
  }
  return RunWithArgs(std::move(args));
}

bool GitDiff::RunStaged() {
  last_run_mode_ = Mode::kStaged;
  last_run_commit_hash_.clear();
  {
    std::scoped_lock lock(params_lock_);
    pending_params_.mode = Mode::kStaged;
    pending_params_.commit_hash.clear();
    pending_params_.toplevel =
        git_repo_ ? git_repo_->toplevel() : std::filesystem::path{};
    pending_params_.path_filter = path_filter_;
  }
  std::string context_flag = "-U" + std::to_string(lines_of_context_);
  std::vector<std::string> args = {"diff", "--cached", "-p", "--submodule",
                                   context_flag};
  if (!path_filter_.empty()) {
    args.emplace_back("--");
    args.insert(args.end(), path_filter_.begin(), path_filter_.end());
  }
  return RunWithArgs(std::move(args));
}

void GitDiff::Update() {
  did_clear_ = buffers_.Merge();
}

void GitDiff::FlushPendingEntry() {
  if (has_pending_entry_) {
    buffers_.Push<FileEntry>(std::move(pending_entry_));
    pending_entry_.status = FileStatus::kModified;
    pending_entry_.path.clear();
    pending_entry_.old_path.clear();
    pending_entry_.start_line = 0;
    has_pending_entry_ = false;
  }
}

void GitDiff::OnStarted() {
  {
    std::scoped_lock lock(params_lock_);
    mode_ = pending_params_.mode;
    commit_hash_ = std::move(pending_params_.commit_hash);
    toplevel_ = std::move(pending_params_.toplevel);
    path_filter_worker_ = std::move(pending_params_.path_filter);
    ext_mappings_worker_ = std::move(pending_params_.ext_mappings);
  }
  buffers_.Reset();

  if (mode_ == Mode::kTree)
    buffers_.Push<FileEntry>({FileStatus::kCommit, "Commit", {}, 0});
  pending_entry_.status = FileStatus::kModified;
  pending_entry_.path.clear();
  pending_entry_.old_path.clear();
  pending_entry_.start_line = 0;
  has_pending_entry_ = false;
  line_count_ = 0;
  last_line_empty_ = false;
  old_in_block_comment_ = false;
  new_in_block_comment_ = false;
  worker_lang_ = Language::kNone;
  current_old_line_ = 0;
  current_new_line_ = 0;
  in_hunk_ = false;
  current_hunk_at_count_ = 2;
}

void GitDiff::OnOutput(std::string line) {
  // Split space-separated parent hashes into individual "Parent: <hash>" lines.
  if (line.starts_with("Parents: ")) {
    size_t start = 9;  // skip "Parents: "
    while (start < line.size()) {
      size_t end = line.find(' ', start);
      std::string hash = end != std::string::npos
                             ? line.substr(start, end - start)
                             : line.substr(start);
      if (!hash.empty()) {
        buffers_.Push<std::string>(std::string("Parent: ") + hash);
        buffers_.Push<DiffLineNumbers>({0, 0});
        line_count_++;
      }
      start = end != std::string::npos ? end + 1 : line.size();
    }
    last_line_empty_ = false;
    return;
  }

  if (line.starts_with("Submodule ")) {
    // Submodule changes appear as standalone lines:
    //   "Submodule path HASH1..HASH2:" (updated, followed by "> msg" lines)
    //   "Submodule path HASH1...HASH2 (new submodule)"
    //   "Submodule path HASH1...HASH2 (submodule deleted)"
    //   "Submodule path contains modified content" (dirty worktree)
    // Extract submodule path from "Submodule <path> <hashes>".
    size_t path_start = 10;
    size_t path_end = line.find(' ', path_start);
    std::string path = path_end != std::string::npos
                           ? line.substr(path_start, path_end - path_start)
                           : line.substr(path_start);
    // A worktree diff can emit two headers for the same submodule: one for a
    // dirty worktree and one for the pointer change. Keep them in one entry.
    bool same_entry = has_pending_entry_ &&
                      pending_entry_.status == FileStatus::kSubmodule &&
                      pending_entry_.path == path;
    if (!same_entry) {
      bool was_pending = has_pending_entry_;
      FlushPendingEntry();
      if (was_pending ||
          (mode_ == Mode::kTree && line_count_ > 0 && !last_line_empty_)) {
        buffers_.Push<std::string>({});
        line_count_++;
        buffers_.Push<DiffLineNumbers>({0, 0});
      }
      has_pending_entry_ = true;
      pending_entry_.old_path.clear();
      pending_entry_.status = FileStatus::kSubmodule;
      pending_entry_.path = std::move(path);
      pending_entry_.start_line = line_count_;
      // The submodule log lines that follow are not a hunk of the previous
      // file, so drop that file's syntax and line-number state.
      worker_lang_ = Language::kNone;
      old_in_block_comment_ = false;
      new_in_block_comment_ = false;
      current_old_line_ = 0;
      current_new_line_ = 0;
      in_hunk_ = false;
      // Push the clean path header, then let the original Submodule line
      // fall through to be pushed as a secondary header line below it.
      std::string header = std::string("\x01") + pending_entry_.path;
      buffers_.Push<std::string>(header);
      line_count_++;
      buffers_.Push<DiffLineNumbers>({0, 0});
    }
  } else if (line.starts_with("* Unmerged path ")) {
    bool was_pending = has_pending_entry_;
    FlushPendingEntry();
    if (was_pending ||
        (mode_ == Mode::kTree && line_count_ > 0 && !last_line_empty_)) {
      buffers_.Push<std::string>({});
      line_count_++;
      buffers_.Push<DiffLineNumbers>({0, 0});
    }
    has_pending_entry_ = true;
    pending_entry_.old_path.clear();
    pending_entry_.status = FileStatus::kModified;
    pending_entry_.path = line.substr(17);
    pending_entry_.start_line = line_count_;
  } else if (line.starts_with("diff --git ") ||
             line.starts_with("diff --cc ")) {
    // Insert an empty line before each file header for visual separation,
    // except before the very first one.
    bool was_pending = has_pending_entry_;
    FlushPendingEntry();
    if (was_pending ||
        (mode_ == Mode::kTree && line_count_ > 0 && !last_line_empty_)) {
      buffers_.Push<std::string>({});
      line_count_++;
      buffers_.Push<DiffLineNumbers>({0, 0});
    }
    has_pending_entry_ = true;
    pending_entry_.path.clear();
    pending_entry_.old_path.clear();
    pending_entry_.status = FileStatus::kModified;
    pending_entry_.start_line = line_count_;
    if (line.starts_with("diff --git ")) {
      // Extract path from "diff --git a/X b/Y".
      size_t b_pos = line.find(" b/", 13);
      if (b_pos != std::string::npos)
        pending_entry_.path = line.substr(b_pos + 3);
      // Replace the noisy raw header with a clean path-only line.
      // The \x01 prefix marks it as a file header for the renderer.
      line = std::string("\x01") + pending_entry_.path;
    } else {
      pending_entry_.path = line.substr(10);
      line = std::string("\x01") + pending_entry_.path;
    }

    buffers_.Push<std::string>(line);
    worker_lang_ = DetectLanguage(pending_entry_.path, ext_mappings_worker_);
    old_in_block_comment_ = false;
    new_in_block_comment_ = false;
    last_line_empty_ = line.empty();
    line_count_++;
    current_old_line_ = 0;
    current_new_line_ = 0;
    in_hunk_ = false;
    buffers_.Push<DiffLineNumbers>({0, 0});
    return;
  } else if (has_pending_entry_) {
    if (line.starts_with("new file mode")) {
      pending_entry_.status = FileStatus::kAdded;
    } else if (line.starts_with("deleted file mode")) {
      pending_entry_.status = FileStatus::kDeleted;
    } else if (line.starts_with("rename from ")) {
      pending_entry_.old_path = line.substr(12);
      pending_entry_.status = FileStatus::kRenamed;
    } else if (line.starts_with("rename to ")) {
      pending_entry_.path = line.substr(10);
    } else if (line.starts_with("copy from ")) {
      pending_entry_.old_path = line.substr(10);
      pending_entry_.status = FileStatus::kCopied;
    } else if (line.starts_with("copy to ")) {
      pending_entry_.path = line.substr(8);
      pending_entry_.status = FileStatus::kCopied;
    }
  }

  // Skip the "--- a/..." and "+++ b/..." lines that follow file headers.
  if (line.starts_with("--- a/") || line.starts_with("+++ b/") ||
      line.starts_with("--- /dev/null") || line.starts_with("+++ /dev/null"))
    return;

  // Detect conflict markers and record zone transitions. The "++" prefix
  // only means "added relative to both parents" in the two-prefix-character
  // combined diff format (at_count > 2, i.e. a merge commit's "@@@" hunks).
  // In an ordinary two-way diff a single leading '+' is the whole marker, so
  // a second '+' is just file content that happens to start with '+' (e.g.
  // an added patch file), not a conflict marker.
  if (current_hunk_at_count_ > 2) {
    if (line.starts_with("++<<<<<<<"))
      buffers_.Push<ConflictTransition>({static_cast<int>(line_count_), 1});
    else if (line.starts_with("++======="))
      buffers_.Push<ConflictTransition>({static_cast<int>(line_count_), 2});
    else if (line.starts_with("++>>>>>>>"))
      buffers_.Push<ConflictTransition>({static_cast<int>(line_count_) + 1, 0});
  }

  if (line.starts_with("@@")) {
    // Count '@' characters to distinguish @@ from @@@ (combined diff).
    int at_count = 0;
    for (size_t i = 0; i < line.size() && line[i] == '@'; i++)
      at_count++;
    current_hunk_at_count_ = at_count;
    buffers_.Push<HunkEntry>({line_count_, at_count});
    if (old_in_block_comment_ || new_in_block_comment_) {
      buffers_.Push<SyntaxTransition>(
          {static_cast<int>(line_count_) + 1, false, false});
    }
    old_in_block_comment_ = false;
    new_in_block_comment_ = false;
    in_hunk_ = true;
    size_t dash = line.find('-', 3);
    if (dash != std::string::npos)
      current_old_line_ = std::max(std::atoi(line.c_str() + dash + 1) - 1, 0);
    else
      current_old_line_ = 0;
    size_t plus = line.find('+', 3);
    if (plus != std::string::npos)
      current_new_line_ = std::max(std::atoi(line.c_str() + plus + 1) - 1, 0);
    else
      current_new_line_ = 0;
  } else if (!line.empty() &&
             (line[0] == ' ' || line[0] == '+' || line[0] == '-')) {
    std::string_view code(line.data() + 1, line.size() - 1);
    bool old_was = old_in_block_comment_;
    bool new_was = new_in_block_comment_;
    if (line[0] == '-') {
      old_in_block_comment_ =
          ScanBlockComment(code, worker_lang_, old_in_block_comment_);
    } else if (line[0] == '+') {
      new_in_block_comment_ =
          ScanBlockComment(code, worker_lang_, new_in_block_comment_);
    } else {
      old_in_block_comment_ =
          ScanBlockComment(code, worker_lang_, old_in_block_comment_);
      new_in_block_comment_ =
          ScanBlockComment(code, worker_lang_, new_in_block_comment_);
    }
    if (old_in_block_comment_ != old_was || new_in_block_comment_ != new_was) {
      buffers_.Push<SyntaxTransition>({static_cast<int>(line_count_) + 1,
                                       old_in_block_comment_,
                                       new_in_block_comment_});
    }
    if (in_hunk_) {
      if (line[0] == ' ') {
        current_old_line_++;
        current_new_line_++;
      } else if (line[0] == '+') {
        current_new_line_++;
      } else if (line[0] == '-') {
        current_old_line_++;
      }
    }
  }

  last_line_empty_ = line.empty();
  line_count_++;
  bool is_content =
      !line.empty() && (line[0] == ' ' || line[0] == '+' || line[0] == '-');
  buffers_.Push<std::string>(std::move(line));
  if (in_hunk_ && is_content) {
    buffers_.Push<DiffLineNumbers>({current_old_line_, current_new_line_});
  } else {
    buffers_.Push<DiffLineNumbers>({0, 0});
  }
}

void GitDiff::OnKilled() {
  pending_entry_.status = FileStatus::kModified;
  pending_entry_.path.clear();
  pending_entry_.old_path.clear();
  pending_entry_.start_line = 0;
  has_pending_entry_ = false;
  last_line_empty_ = false;
}

void GitDiff::OnFinished(Exec::Status status, int result, std::string err) {
  FlushPendingEntry();
  if (line_count_ == 0 && !commit_hash_.empty()) {
    // diff-tree produces no output for empty commits (--allow-empty) or when
    // the path filter excludes all changed files. Run a lightweight command to
    // fetch just the commit header.
    std::vector<std::string> args = {"git", "show", "--no-patch",
                                     "--pretty=format:commit %H%n"
                                     "Parents: %P%n"
                                     "Author: %an <%ae>  %ad%n"
                                     "Committer: %cn <%ce>  %cd%n"
                                     "%w(0,4,4)%n%B",
                                     "--date=format-local:%Y-%m-%d %H:%M:%S"};
    args.insert(args.end(), extra_args_.begin(), extra_args_.end());
    args.push_back(commit_hash_);
    Exec proc;
    if (proc.Start(args)) {
      while (proc.Poll()) {
      }
      auto& out = proc.GetOut();
      size_t start = 0;
      for (;;) {
        auto nl = out.find('\n', start);
        if (nl == std::string::npos)
          break;
        OnOutput(out.substr(start, nl - start));
        start = nl + 1;
      }
    }
  }
  if (mode_ == Mode::kUnstaged) {
    std::vector<std::string> untracked;
    Exec ls;
    std::vector<std::string> ls_args = {"git"};
    if (!toplevel_.empty())
      ls_args.insert(ls_args.end(), {"-C", toplevel_.string()});
    ls_args.insert(ls_args.end(),
                   {"ls-files", "--others", "--exclude-standard"});
    if (!path_filter_worker_.empty()) {
      ls_args.emplace_back("--");
      if (!toplevel_.empty()) {
        // Path filter entries are cwd-relative but -C <toplevel> makes git
        // interpret them relative to the repo root. Convert them.
        auto prefix = std::filesystem::relative(std::filesystem::current_path(),
                                                toplevel_);
        for (auto& p : path_filter_worker_)
          ls_args.push_back((prefix / p).string());
      } else {
        ls_args.insert(ls_args.end(), path_filter_worker_.begin(),
                       path_filter_worker_.end());
      }
    }
    if (ls.Start(ls_args)) {
      while (ls.Poll()) {
      }
      auto& out = ls.GetOut();
      size_t start = 0;
      for (;;) {
        auto nl = out.find('\n', start);
        if (nl == std::string::npos)
          break;
        if (nl > start)
          untracked.push_back(out.substr(start, nl - start));
        start = nl + 1;
      }
    }
    if (!untracked.empty())
      AppendUntrackedFiles(untracked);
  }
}

void GitDiff::AppendUntrackedFiles(const std::vector<std::string>& paths) {
  for (auto& path : paths) {
    if (line_count_ > 0) {
      buffers_.Push<std::string>({});
      line_count_++;
      buffers_.Push<DiffLineNumbers>({0, 0});
    }

    buffers_.Push<FileEntry>({FileStatus::kAdded, path, {}, line_count_});

    std::string header = std::string("\x01") + path;
    buffers_.Push<std::string>(header);
    line_count_++;
    buffers_.Push<DiffLineNumbers>({0, 0});

    worker_lang_ = DetectLanguage(path, ext_mappings_worker_);
    old_in_block_comment_ = false;
    new_in_block_comment_ = false;
    current_old_line_ = 0;
    current_new_line_ = 0;
    in_hunk_ = true;

    try {
      std::ifstream file(toplevel_ / path, std::ios::binary);
      if (!file)
        continue;

      // Git treats a file as binary if its first 8000 bytes contain a NUL
      // byte. Check up front so no binary content is shown before the
      // placeholder message.
      std::string head;
      head.resize(8000);
      file.read(&head[0], head.size());
      head.resize(static_cast<size_t>(file.gcount()));
      if (head.find('\0') != std::string::npos) {
        line_count_++;
        buffers_.Push<std::string>("* Binary file (not showing content).");
        buffers_.Push<DiffLineNumbers>({0, 0});
        continue;
      }

      file.clear();
      file.seekg(0);
      std::string line;
      while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        bool was = new_in_block_comment_;
        new_in_block_comment_ =
            ScanBlockComment(line, worker_lang_, new_in_block_comment_);
        if (new_in_block_comment_ != was)
          buffers_.Push<SyntaxTransition>({static_cast<int>(line_count_) + 1,
                                           false, new_in_block_comment_});
        line_count_++;
        std::string diff_line = "+" + line;
        buffers_.Push<std::string>(diff_line);
        current_new_line_++;
        buffers_.Push<DiffLineNumbers>({0, current_new_line_});
      }
    } catch (...) {
      continue;
    }
  }
}
