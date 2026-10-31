// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef BASE_COMMAND_LINE_H
#define BASE_COMMAND_LINE_H

#include <map>
#include <string>
#include <vector>

class CommandLine {
 public:
  // Constructs a CommandLine from argv.
  // `value_flags` lists short flag characters that take a value, attached or
  // as the next argument (e.g. -n1, -n 1).
  // `attached_value_flags` lists short flag characters whose value is optional
  // and must be attached (e.g. -U5); on their own they are boolean.
  // Flags in neither set are treated as boolean; characters after them are
  // parsed as additional flags (combined form).
  CommandLine(int argc,
              char** argv,
              const std::vector<char>& value_flags = {},
              const std::vector<char>& attached_value_flags = {});

  // The program name (argv[0]).
  const std::string& program() const { return program_; }

  // True if -<c> or --<name> was present.
  bool HasSwitch(char short_name) const;
  bool HasSwitch(const std::string& long_name) const;

  // Returns the value for -<c> <val> or --<name>=<val> (or empty string).
  std::string GetSwitchValue(char short_name) const;
  std::string GetSwitchValue(const std::string& long_name) const;

  // All non-switch arguments, in order.
  const std::vector<std::string>& GetArgs() const { return args_; }

  // Arguments after "--", typically used as path filters.
  const std::vector<std::string>& GetPathFilter() const { return path_filter_; }

  // Builds an argument list from all parsed components, excluding the
  // specified switches and path filter.
  std::vector<std::string> BuildArgList(
      const std::vector<char>& skip_short,
      const std::vector<std::string>& skip_long) const;

 private:
  std::string program_;
  std::vector<char> attached_value_flags_;  // Sorted.
  std::map<char, std::string> short_switches_;
  std::map<std::string, std::string> long_switches_;
  std::vector<std::string> args_;
  std::vector<std::string> path_filter_;
};

#endif  // BASE_COMMAND_LINE_H
