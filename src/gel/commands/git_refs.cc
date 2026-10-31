// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_refs.h"

#include <string_view>

GitRefs::GitRefs(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitRefs::~GitRefs() {
  TerminateWorkerThread();
}

bool GitRefs::Run() {
  // Avoid fields like %(*objectname) or %(objecttype): they read every ref's
  // object from the object database.
  return RunWithArgs({"for-each-ref",
                      "--format=%(objectname)%01%(HEAD)%01%(refname)",
                      "refs/heads", "refs/remotes", "refs/tags", "refs/stash"});
}

void GitRefs::Update() {
  did_clear_ = buffers_.Merge();
}

void GitRefs::OnStarted() {
  buffers_.Reset();
}

void GitRefs::OnOutput(std::string line) {
  // "<object>\x01<'*' if checked out, else ' '>\x01<refname>"
  size_t sep1 = line.find('\x01');
  if (sep1 == std::string::npos)
    return;
  size_t sep2 = line.find('\x01', sep1 + 1);
  if (sep2 == std::string::npos)
    return;
  std::string_view refname = std::string_view(line).substr(sep2 + 1);

  Ref ref;
  if (refname.starts_with("refs/heads/")) {
    ref.kind = Ref::Kind::kBranch;
    ref.name = refname.substr(11);
    ref.is_head = line.compare(sep1 + 1, sep2 - sep1 - 1, "*") == 0;
  } else if (refname.starts_with("refs/remotes/")) {
    // Skip "refs/remotes/<remote>/HEAD" symrefs.
    if (refname.ends_with("/HEAD"))
      return;
    ref.kind = Ref::Kind::kRemoteBranch;
    ref.name = refname.substr(13);
  } else if (refname.starts_with("refs/tags/")) {
    ref.kind = Ref::Kind::kTag;
    ref.name = refname.substr(10);
  } else if (refname == "refs/stash") {
    ref.kind = Ref::Kind::kStash;
  } else {
    return;
  }
  line.resize(sep1);
  ref.object = std::move(line);
  buffers_.Push(std::move(ref));
}

void GitRefs::OnFinished(Exec::Status status,
                         int result_code,
                         std::string err) {
  if (status == Exec::Status::EXITED && result_code == 0)
    buffers_.Push<RunFinished>({});
}
