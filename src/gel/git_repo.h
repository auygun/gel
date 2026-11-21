// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_GIT_REPO_H
#define GEL_GIT_REPO_H

#include <filesystem>
#include <functional>
#include <string>

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

// |path|, relative to the repository, in the working tree at |toplevel|.
// Converted from UTF-8, which a std::string path is not taken as on Windows.
std::filesystem::path WorkTreePath(const std::filesystem::path& toplevel,
                                   const std::string& path);

#endif  // GEL_GIT_REPO_H
