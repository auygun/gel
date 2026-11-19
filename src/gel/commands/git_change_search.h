// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_CHANGE_SEARCH_H
#define GEL_COMMANDS_GIT_CHANGE_SEARCH_H

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/double_buffer.h"
#include "gel/commands/git.h"

// Runs "git diff-tree --stdin" to find the commits whose changes match, for
// the commit search modes that look at what a commit changed rather than at
// its metadata: pickaxe (-S / -G) and touched paths.
//
// The process is started once per query and fed commits as the search gets
// to them, so it is not restarted for every few commits and the search can
// change course without losing work. Each commit is followed by a marker line
// carrying its index; diff-tree echoes lines that are not commits, flushing
// its output, so the marker comes back as soon as the commit is checked,
// right after the commit itself if it matched.
class GitChangeSearch final : public Git {
 public:
  // What to match. An empty query matches nothing and must not be run.
  struct Query {
    std::vector<std::string> git_options;   // Before "diff-tree".
    std::vector<std::string> diff_options;  // e.g. "-Sfoo", "-i".
    std::vector<std::string> pathspecs;

    bool empty() const { return diff_options.empty() && pathspecs.empty(); }
    bool operator==(const Query& other) const = default;
  };

  struct Event {
    enum class Kind : uint8_t {
      // The commit passed to Check() as |index| has been checked; |match| is
      // set if it matched.
      kChecked,
      // The process ended. |failed| is set if git failed, with its error
      // output in |text|.
      kFinished,
    };
    int run_id = 0;
    Kind kind = Kind::kChecked;
    bool failed = false;
    bool match = false;
    int index = 0;
    std::string text;
  };

  explicit GitChangeSearch(std::function<void(bool)> busy_callback);
  ~GitChangeSearch() final;

  GitChangeSearch(GitChangeSearch const&) = delete;
  GitChangeSearch& operator=(GitChangeSearch const&) = delete;

  // Starts git for |query|, waiting for commits to check. Events of this run
  // carry |run_id|. Any previous run is killed.
  bool Start(int run_id, const Query& query);

  // Checks |commit|, which is reported back as |index|. Commits are checked
  // in the order they are passed. They are sent to git on Flush().
  void Check(int index, std::string_view commit);

  // Sends the commits passed to Check() since the last call to git.
  void Flush();

  // Drops the commits passed to Check() and not sent yet.
  void DropUnsent() { input_.clear(); }

  // Lets git end once it has checked the commits passed so far.
  void Finish() { CloseStdin(); }

  // Main thread: picks up the events that arrived since the last call. They
  // are kept until ClearEvents().
  void Update() { events_.Merge(); }
  std::span<const Event> GetEvents() const { return events_.data(); }
  void ClearEvents() { events_.Clear(); }

 private:
  DoubleBuffer<Event> events_;
  std::string input_;  // Commits passed to Check() and not sent yet.

  // Worker thread.
  bool matched_ = false;  // A commit line came since the last marker.
  std::string err_;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnError(std::string line) final;
  void OnFinished(Exec::Status status, int result) final;
  void OnKilled() final {}
};

#endif  // GEL_COMMANDS_GIT_CHANGE_SEARCH_H
