// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_peel_tags.h"

GitPeelTags::GitPeelTags(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitPeelTags::~GitPeelTags() {
  TerminateWorkerThread();
}

bool GitPeelTags::Run(const std::vector<std::string>& objects) {
  // Each input line is "<object>^{} <object>". The peeled object name is
  // printed followed by the rest of the line, so each output line carries both.
  std::string input;
  for (const auto& object : objects) {
    input += object;
    input += "^{} ";
    input += object;
    input += '\n';
  }
  return RunWithArgs(
      {"cat-file", "--batch-check=%(objectname) %(rest)", "--buffer"},
      std::move(input));
}

void GitPeelTags::Update() {
  did_clear_ = peeled_.Merge();
}

void GitPeelTags::OnStarted() {
  // Results of the previous run that were not merged yet are dropped. They
  // were still unresolved when this run was requested, so it resolves them
  // again.
  peeled_.Reset();
}

void GitPeelTags::OnOutput(std::string line) {
  // "<peeled> <object>" on success, "<object>^{} missing" (or "ambiguous")
  // otherwise.
  size_t space = line.find(' ');
  if (space == std::string::npos)
    return;
  std::string first = line.substr(0, space);
  std::string second = line.substr(space + 1);
  if (first.ends_with("^{}")) {
    first.resize(first.size() - 3);
    peeled_.Push({std::move(first), std::string()});
  } else {
    peeled_.Push({std::move(second), std::move(first)});
  }
}
