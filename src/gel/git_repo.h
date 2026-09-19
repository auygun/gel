// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_GIT_REPO_H
#define GEL_GIT_REPO_H

#include <filesystem>
#include <functional>

class GitRepo {
 public:
  void Init(std::function<void(bool)> callback = nullptr);

  const std::filesystem::path& toplevel() const { return toplevel_; }
  const std::filesystem::path& git_dir() const { return git_dir_; }

 private:
  std::filesystem::path toplevel_;
  std::filesystem::path git_dir_;
  int pending_ = 0;
};

#endif  // GEL_GIT_REPO_H
