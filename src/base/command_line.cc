// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "base/command_line.h"

#include <algorithm>

CommandLine::CommandLine(int argc, char** argv,
                         const std::vector<char>& value_flags) {
  if (argc > 0)
    program_ = argv[0];

  std::vector<char> value_flags_sorted(value_flags.begin(), value_flags.end());
  std::sort(value_flags_sorted.begin(), value_flags_sorted.end());

  bool stop_parsing = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (stop_parsing) {
      path_filter_.push_back(arg);
      continue;
    }

    if (arg == "--") {
      stop_parsing = true;
      continue;
    }

    if (arg.starts_with("--")) {
      std::string name = arg.substr(2);
      std::string value;
      auto eq = name.find('=');
      if (eq != std::string::npos) {
        value = name.substr(eq + 1);
        name = name.substr(0, eq);
      }
      long_switches_[name] = value;
    } else if (arg.starts_with("-") && arg.size() > 1) {
        char flag = arg[1];
        bool takes_value = std::binary_search(
            value_flags_sorted.begin(), value_flags_sorted.end(), flag);
        if (arg.size() == 2) {
          if (takes_value) {
            // Flag accepts a value: consume next arg if it doesn't look like a
            // switch.
            std::string value;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
              value = argv[++i];
            }
            short_switches_[flag] = value;
          } else {
            // Boolean flag.
            short_switches_[flag] = {};
          }
        } else if (takes_value) {
          // Flag with attached value (e.g. -n1).
          short_switches_[flag] = arg.substr(2);
        } else {
          // Combined boolean flags (e.g. -abc).
          for (size_t j = 1; j < arg.size(); j++)
            short_switches_[arg[j]] = {};
        }
    } else {
      args_.push_back(arg);
    }
  }
}

bool CommandLine::HasSwitch(char short_name) const {
  return short_switches_.count(short_name) > 0;
}

bool CommandLine::HasSwitch(const std::string& long_name) const {
  return long_switches_.count(long_name) > 0;
}

std::string CommandLine::GetSwitchValue(char short_name) const {
  auto it = short_switches_.find(short_name);
  return it != short_switches_.end() ? it->second : std::string();
}

std::string CommandLine::GetSwitchValue(const std::string& long_name) const {
  auto it = long_switches_.find(long_name);
  return it != long_switches_.end() ? it->second : std::string();
}

std::vector<std::string> CommandLine::BuildArgList(
    const std::vector<char>& skip_short,
    const std::vector<std::string>& skip_long) const {
  std::vector<std::string> result;

  for (const auto& [flag, value] : short_switches_) {
    bool excluded = false;
    for (char c : skip_short) {
      if (flag == c) {
        excluded = true;
        break;
      }
    }
    if (excluded)
      continue;
    result.push_back(std::string("-") + flag);
    if (!value.empty())
      result.push_back(value);
  }

  for (const auto& [name, value] : long_switches_) {
    bool excluded = false;
    for (const auto& s : skip_long) {
      if (name == s) {
        excluded = true;
        break;
      }
    }
    if (excluded)
      continue;
    if (!value.empty())
      result.push_back(
          std::string("--").append(name).append("=").append(value));
    else
      result.push_back("--" + name);
  }

  for (const auto& arg : args_)
    result.push_back(arg);

  return result;
}
