// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/syntax_highlight.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "base/utf8.h"

#include "gel/ui/style.h"

namespace {

bool IsWordChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

bool IsDigit(char c) {
  return c >= '0' && c <= '9';
}

bool IsHexDigit(char c) {
  return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool MatchKeyword(std::string_view word,
                  const std::string_view* list,
                  size_t count) {
  // Binary search on sorted keyword list.
  auto it = std::lower_bound(list, list + count, word);
  return it != list + count && *it == word;
}

// C-like keywords (C, C++, Java, Go, Rust, C#, JS, TS).
const std::string_view kCLikeKeywords[] = {
    "abstract",  "as",         "async",      "auto",     "await",
    "bool",      "break",      "case",       "catch",    "class",
    "co_await",  "co_return",  "co_yield",   "concept",  "const",
    "consteval", "constexpr",  "constinit",  "continue", "debugger",
    "decltype",  "default",    "defer",      "delete",   "do",
    "else",      "enum",       "explicit",   "export",   "extends",
    "extern",    "false",      "final",      "finally",  "fn",
    "for",       "friend",     "func",       "function", "goto",
    "if",        "impl",       "implements", "import",   "in",
    "inline",    "instanceof", "interface",  "let",      "loop",
    "match",     "mod",        "module",     "move",     "mut",
    "namespace", "new",        "nil",        "noexcept", "null",
    "nullptr",   "of",         "operator",   "override", "package",
    "private",   "protected",  "pub",        "public",   "ref",
    "register",  "requires",   "return",     "self",     "sizeof",
    "static",    "struct",     "super",      "switch",   "synchronized",
    "template",  "this",       "throw",      "throws",   "trait",
    "true",      "try",        "type",       "typedef",  "typeid",
    "typename",  "typeof",     "union",      "unsafe",   "use",
    "using",     "var",        "virtual",    "void",     "volatile",
    "where",     "while",      "yield",
};

const std::string_view kCLikeTypes[] = {
    "String",   "bool",    "byte",     "char",    "char16_t", "char32_t",
    "char8_t",  "double",  "f32",      "f64",     "float",    "i128",
    "i16",      "i32",     "i64",      "i8",      "int",      "int16_t",
    "int32_t",  "int64_t", "int8_t",   "isize",   "long",     "number",
    "short",    "signed",  "size_t",   "ssize_t", "string",   "u128",
    "u16",      "u32",     "u64",      "u8",      "uint16_t", "uint32_t",
    "uint64_t", "uint8_t", "unsigned", "usize",   "wchar_t",
};

// Shader keywords (GLSL, HLSL, WGSL, Metal).
const std::string_view kShaderKeywords[] = {
    "attribute",  "break",
    "buffer",     "case",
    "centroid",   "coherent",
    "const",      "continue",
    "default",    "discard",
    "do",         "else",
    "false",      "flat",
    "fn",         "for",
    "highp",      "if",
    "in",         "inout",
    "invariant",  "layout",
    "let",        "lowp",
    "mediump",    "noperspective",
    "out",        "override",
    "patch",      "precision",
    "readonly",   "restrict",
    "return",     "sample",
    "shared",     "smooth",
    "static",     "struct",
    "subroutine", "switch",
    "true",       "typedef",
    "uniform",    "var",
    "varying",    "volatile",
    "while",      "writeonly",
};

const std::string_view kShaderTypes[] = {
    "RWTexture2D",
    "SamplerState",
    "Texture2D",
    "atomic_uint",
    "bool",
    "bvec2",
    "bvec3",
    "bvec4",
    "dmat2",
    "dmat3",
    "dmat4",
    "double",
    "dvec2",
    "dvec3",
    "dvec4",
    "f16",
    "f32",
    "float",
    "float16_t",
    "float2",
    "float3",
    "float3x3",
    "float4",
    "float4x4",
    "half",
    "half2",
    "half3",
    "half4",
    "i32",
    "image2D",
    "int",
    "int2",
    "int3",
    "int4",
    "ivec2",
    "ivec3",
    "ivec4",
    "mat2",
    "mat2x2",
    "mat2x3",
    "mat2x4",
    "mat3",
    "mat3x2",
    "mat3x3",
    "mat3x4",
    "mat4",
    "mat4x2",
    "mat4x3",
    "mat4x4",
    "sampler",
    "sampler1D",
    "sampler2D",
    "sampler2DArray",
    "sampler2DShadow",
    "sampler3D",
    "samplerCube",
    "texture2d",
    "u32",
    "uint",
    "uint2",
    "uint3",
    "uint4",
    "uvec2",
    "uvec3",
    "uvec4",
    "vec2",
    "vec3",
    "vec4",
    "void",
};

const std::string_view kPythonKeywords[] = {
    "False",  "None",   "True",    "and",      "as",       "assert", "async",
    "await",  "break",  "class",   "continue", "def",      "del",    "elif",
    "else",   "except", "finally", "for",      "from",     "global", "if",
    "import", "in",     "is",      "lambda",   "nonlocal", "not",    "or",
    "pass",   "raise",  "return",  "try",      "while",    "with",   "yield",
};

const std::string_view kShellKeywords[] = {
    "case",     "do", "done", "elif",  "else",   "esac", "fi",    "for",
    "function", "if", "in",   "local", "return", "then", "until", "while",
};

const std::string_view kRubyKeywords[] = {
    "BEGIN",  "END",   "alias",  "and",   "begin", "break",  "case",  "class",
    "def",    "do",    "else",   "elsif", "end",   "ensure", "false", "for",
    "if",     "in",    "module", "next",  "nil",   "not",    "or",    "redo",
    "rescue", "retry", "return", "self",  "super", "then",   "true",  "undef",
    "unless", "until", "when",   "while", "yield",
};

const std::string_view kGnKeywords[] = {
    "action",
    "action_foreach",
    "assert",
    "config",
    "copy",
    "declare_args",
    "defined",
    "else",
    "executable",
    "false",
    "foreach",
    "forward_variables_from",
    "get_label_info",
    "get_path_info",
    "get_target_outputs",
    "group",
    "if",
    "import",
    "loadable_module",
    "not_needed",
    "print",
    "process_file_template",
    "read_file",
    "rebase_path",
    "set_default_toolchain",
    "set_defaults",
    "shared_library",
    "source_set",
    "static_library",
    "template",
    "tool",
    "toolchain",
    "true",
    "write_file",
};

const std::string_view kJsonKeywords[] = {
    "false",
    "null",
    "true",
};

struct Emitter {
  ColoredLine result;

  void Emit(std::string_view text, ColorId color) {
    if (!text.empty())
      result.segments.push_back({std::string(text), color});
  }
};

void TokenizeCLike(std::string_view code,
                   Emitter& em,
                   const std::string_view* keywords,
                   size_t keyword_count,
                   const std::string_view* types,
                   size_t type_count,
                   bool in_block_comment = false) {
  size_t i = 0;
  size_t len = code.size();

  if (in_block_comment) {
    size_t end = code.find("*/");
    if (end == std::string_view::npos) {
      em.Emit(code, ColorId::kSyntaxComment);
      return;
    }
    em.Emit(code.substr(0, end + 2), ColorId::kSyntaxComment);
    i = end + 2;
  }

  while (i < len) {
    // Line comments.
    if (i + 1 < len && code[i] == '/' && code[i + 1] == '/') {
      em.Emit(code.substr(i), ColorId::kSyntaxComment);
      return;
    }

    // Block comments.
    if (i + 1 < len && code[i] == '/' && code[i + 1] == '*') {
      size_t end = code.find("*/", i + 2);
      if (end == std::string_view::npos) {
        em.Emit(code.substr(i), ColorId::kSyntaxComment);
        return;
      }
      em.Emit(code.substr(i, end + 2 - i), ColorId::kSyntaxComment);
      i = end + 2;
      continue;
    }

    // Preprocessor directives.
    if (code[i] == '#') {
      // Check if this is at the start of non-whitespace.
      bool at_start = true;
      for (size_t j = 0; j < i; j++) {
        if (code[j] != ' ' && code[j] != '\t') {
          at_start = false;
          break;
        }
      }
      if (at_start) {
        em.Emit(code.substr(i), ColorId::kSyntaxPreprocessor);
        return;
      }
    }

    // Strings.
    if (code[i] == '"' || code[i] == '\'') {
      char quote = code[i];
      // Handle raw strings R"(...)" in C++.
      if (quote == '"' && i > 0 && code[i - 1] == 'R') {
        // Already emitted the 'R', find closing.
        size_t end = i + 1;
        // Find delimiter in R"delim(...)delim"
        size_t paren = code.find('(', end);
        if (paren != std::string_view::npos) {
          std::string close_delim =
              ")" + std::string(code.substr(end, paren - end)) + "\"";
          size_t close_pos = code.find(close_delim, paren + 1);
          if (close_pos != std::string_view::npos) {
            end = close_pos + close_delim.size();
          } else {
            end = len;
          }
        } else {
          // Malformed, just find the closing quote.
          end = code.find(quote, end);
          if (end == std::string_view::npos)
            end = len;
          else
            end++;
        }
        em.Emit(code.substr(i, end - i), ColorId::kSyntaxString);
        i = end;
        continue;
      }
      size_t end = i + 1;
      while (end < len && code[end] != quote) {
        if (code[end] == '\\' && end + 1 < len)
          end++;
        end++;
      }
      if (end < len)
        end++;
      em.Emit(code.substr(i, end - i), ColorId::kSyntaxString);
      i = end;
      continue;
    }

    // Numbers.
    if (IsDigit(code[i]) ||
        (code[i] == '.' && i + 1 < len && IsDigit(code[i + 1]))) {
      // Don't highlight if preceded by a word character.
      if (i > 0 && IsWordChar(code[i - 1])) {
        em.Emit(code.substr(i, 1), ColorId::kDefault);
        i++;
        continue;
      }
      size_t start = i;
      if (code[i] == '0' && i + 1 < len &&
          (code[i + 1] == 'x' || code[i + 1] == 'X')) {
        i += 2;
        while (i < len && (IsHexDigit(code[i]) || code[i] == '\''))
          i++;
      } else if (code[i] == '0' && i + 1 < len &&
                 (code[i + 1] == 'b' || code[i + 1] == 'B')) {
        i += 2;
        while (i < len && (code[i] == '0' || code[i] == '1' || code[i] == '\''))
          i++;
      } else {
        while (i < len &&
               (IsDigit(code[i]) || code[i] == '.' || code[i] == '\''))
          i++;
        if (i < len && (code[i] == 'e' || code[i] == 'E')) {
          i++;
          if (i < len && (code[i] == '+' || code[i] == '-'))
            i++;
          while (i < len && IsDigit(code[i]))
            i++;
        }
      }
      // Type suffixes.
      while (i < len && (code[i] == 'u' || code[i] == 'U' || code[i] == 'l' ||
                         code[i] == 'L' || code[i] == 'f' || code[i] == 'F' ||
                         code[i] == 'z' || code[i] == 'Z'))
        i++;
      em.Emit(code.substr(start, i - start), ColorId::kSyntaxNumber);
      continue;
    }

    // Words (identifiers / keywords).
    if (IsWordChar(code[i]) && !IsDigit(code[i])) {
      size_t start = i;
      while (i < len && IsWordChar(code[i]))
        i++;
      std::string_view word = code.substr(start, i - start);
      if (MatchKeyword(word, keywords, keyword_count)) {
        em.Emit(word, ColorId::kSyntaxKeyword);
      } else if (MatchKeyword(word, types, type_count)) {
        em.Emit(word, ColorId::kSyntaxType);
      } else {
        em.Emit(word, ColorId::kDefault);
      }
      continue;
    }

    // Plain character.
    em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
            ColorId::kDefault);
    i += base::Utf8SequenceLength(code, i);
  }
}

void TokenizePython(std::string_view code, Emitter& em) {
  size_t i = 0;
  size_t len = code.size();

  while (i < len) {
    // Comments.
    if (code[i] == '#') {
      em.Emit(code.substr(i), ColorId::kSyntaxComment);
      return;
    }

    // Triple-quoted strings.
    if (i + 2 < len &&
        ((code[i] == '"' && code[i + 1] == '"' && code[i + 2] == '"') ||
         (code[i] == '\'' && code[i + 1] == '\'' && code[i + 2] == '\''))) {
      char q = code[i];
      size_t end = code.find(std::string(3, q), i + 3);
      if (end == std::string_view::npos) {
        em.Emit(code.substr(i), ColorId::kSyntaxString);
        return;
      }
      em.Emit(code.substr(i, end + 3 - i), ColorId::kSyntaxString);
      i = end + 3;
      continue;
    }

    // Strings.
    if (code[i] == '"' || code[i] == '\'') {
      char quote = code[i];
      // Handle f-strings, r-strings, b-strings prefix.
      size_t end = i + 1;
      while (end < len && code[end] != quote) {
        if (code[end] == '\\' && end + 1 < len)
          end++;
        end++;
      }
      if (end < len)
        end++;
      em.Emit(code.substr(i, end - i), ColorId::kSyntaxString);
      i = end;
      continue;
    }

    // Numbers.
    if (IsDigit(code[i]) ||
        (code[i] == '.' && i + 1 < len && IsDigit(code[i + 1]))) {
      if (i > 0 && IsWordChar(code[i - 1])) {
        em.Emit(code.substr(i, 1), ColorId::kDefault);
        i++;
        continue;
      }
      size_t start = i;
      if (code[i] == '0' && i + 1 < len &&
          (code[i + 1] == 'x' || code[i + 1] == 'X')) {
        i += 2;
        while (i < len && (IsHexDigit(code[i]) || code[i] == '_'))
          i++;
      } else if (code[i] == '0' && i + 1 < len &&
                 (code[i + 1] == 'b' || code[i + 1] == 'B')) {
        i += 2;
        while (i < len && (code[i] == '0' || code[i] == '1' || code[i] == '_'))
          i++;
      } else if (code[i] == '0' && i + 1 < len &&
                 (code[i + 1] == 'o' || code[i + 1] == 'O')) {
        i += 2;
        while (i < len &&
               ((code[i] >= '0' && code[i] <= '7') || code[i] == '_'))
          i++;
      } else {
        while (i < len &&
               (IsDigit(code[i]) || code[i] == '.' || code[i] == '_'))
          i++;
        if (i < len && (code[i] == 'e' || code[i] == 'E')) {
          i++;
          if (i < len && (code[i] == '+' || code[i] == '-'))
            i++;
          while (i < len && IsDigit(code[i]))
            i++;
        }
      }
      if (i < len && (code[i] == 'j' || code[i] == 'J'))
        i++;
      em.Emit(code.substr(start, i - start), ColorId::kSyntaxNumber);
      continue;
    }

    // Decorator as preprocessor.
    if (code[i] == '@') {
      size_t start = i;
      i++;
      while (i < len && (IsWordChar(code[i]) || code[i] == '.'))
        i++;
      em.Emit(code.substr(start, i - start), ColorId::kSyntaxPreprocessor);
      continue;
    }

    // Words.
    if (IsWordChar(code[i]) && !IsDigit(code[i])) {
      size_t start = i;
      while (i < len && IsWordChar(code[i]))
        i++;
      std::string_view word = code.substr(start, i - start);
      // Handle string prefixes.
      if ((word == "f" || word == "r" || word == "b" || word == "rb" ||
           word == "br" || word == "fr" || word == "rf" || word == "F" ||
           word == "R" || word == "B") &&
          i < len && (code[i] == '"' || code[i] == '\'')) {
        em.Emit(word, ColorId::kSyntaxString);
        continue;
      }
      if (MatchKeyword(word, kPythonKeywords, std::size(kPythonKeywords))) {
        em.Emit(word, ColorId::kSyntaxKeyword);
      } else {
        em.Emit(word, ColorId::kDefault);
      }
      continue;
    }

    em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
            ColorId::kDefault);
    i += base::Utf8SequenceLength(code, i);
  }
}

void TokenizeShell(std::string_view code, Emitter& em) {
  size_t i = 0;
  size_t len = code.size();

  while (i < len) {
    if (code[i] == '#') {
      em.Emit(code.substr(i), ColorId::kSyntaxComment);
      return;
    }

    if (code[i] == '"' || code[i] == '\'') {
      char quote = code[i];
      size_t end = i + 1;
      if (quote == '\'') {
        // Single quotes: no escaping in shell.
        end = code.find('\'', end);
        if (end == std::string_view::npos)
          end = len;
        else
          end++;
      } else {
        while (end < len && code[end] != quote) {
          if (code[end] == '\\' && end + 1 < len)
            end++;
          end++;
        }
        if (end < len)
          end++;
      }
      em.Emit(code.substr(i, end - i), ColorId::kSyntaxString);
      i = end;
      continue;
    }

    if (IsDigit(code[i]) && (i == 0 || !IsWordChar(code[i - 1]))) {
      size_t start = i;
      while (i < len && IsDigit(code[i]))
        i++;
      em.Emit(code.substr(start, i - start), ColorId::kSyntaxNumber);
      continue;
    }

    if (IsWordChar(code[i]) && !IsDigit(code[i])) {
      size_t start = i;
      while (i < len && IsWordChar(code[i]))
        i++;
      std::string_view word = code.substr(start, i - start);
      if (MatchKeyword(word, kShellKeywords, std::size(kShellKeywords))) {
        em.Emit(word, ColorId::kSyntaxKeyword);
      } else {
        em.Emit(word, ColorId::kDefault);
      }
      continue;
    }

    em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
            ColorId::kDefault);
    i += base::Utf8SequenceLength(code, i);
  }
}

void TokenizeRuby(std::string_view code, Emitter& em) {
  size_t i = 0;
  size_t len = code.size();

  while (i < len) {
    if (code[i] == '#') {
      em.Emit(code.substr(i), ColorId::kSyntaxComment);
      return;
    }

    if (code[i] == '"' || code[i] == '\'') {
      char quote = code[i];
      size_t end = i + 1;
      while (end < len && code[end] != quote) {
        if (code[end] == '\\' && end + 1 < len)
          end++;
        end++;
      }
      if (end < len)
        end++;
      em.Emit(code.substr(i, end - i), ColorId::kSyntaxString);
      i = end;
      continue;
    }

    if (IsDigit(code[i]) && (i == 0 || !IsWordChar(code[i - 1]))) {
      size_t start = i;
      while (i < len && (IsDigit(code[i]) || code[i] == '_' || code[i] == '.'))
        i++;
      em.Emit(code.substr(start, i - start), ColorId::kSyntaxNumber);
      continue;
    }

    if ((IsWordChar(code[i]) && !IsDigit(code[i])) || code[i] == '@') {
      size_t start = i;
      if (code[i] == '@')
        i++;
      while (i < len && IsWordChar(code[i]))
        i++;
      std::string_view word = code.substr(start, i - start);
      if (MatchKeyword(word, kRubyKeywords, std::size(kRubyKeywords))) {
        em.Emit(word, ColorId::kSyntaxKeyword);
      } else {
        em.Emit(word, ColorId::kDefault);
      }
      continue;
    }

    em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
            ColorId::kDefault);
    i += base::Utf8SequenceLength(code, i);
  }
}

void TokenizeHtml(std::string_view code,
                  Emitter& em,
                  bool in_block_comment = false) {
  size_t i = 0;
  size_t len = code.size();

  if (in_block_comment) {
    size_t end = code.find("-->");
    if (end == std::string_view::npos) {
      em.Emit(code, ColorId::kSyntaxComment);
      return;
    }
    em.Emit(code.substr(0, end + 3), ColorId::kSyntaxComment);
    i = end + 3;
  }

  while (i < len) {
    // HTML comments.
    if (i + 3 < len && code[i] == '<' && code[i + 1] == '!' &&
        code[i + 2] == '-' && code[i + 3] == '-') {
      size_t end = code.find("-->", i + 4);
      if (end == std::string_view::npos) {
        em.Emit(code.substr(i), ColorId::kSyntaxComment);
        return;
      }
      em.Emit(code.substr(i, end + 3 - i), ColorId::kSyntaxComment);
      i = end + 3;
      continue;
    }

    // Tags.
    if (code[i] == '<') {
      size_t start = i;
      i++;
      // Closing tag slash.
      if (i < len && code[i] == '/')
        i++;
      // Tag name.
      size_t name_start = i;
      while (i < len && IsWordChar(code[i]))
        i++;
      if (i > name_start) {
        em.Emit(code.substr(start, name_start - start), ColorId::kDefault);
        em.Emit(code.substr(name_start, i - name_start),
                ColorId::kSyntaxKeyword);
        // Attributes and closing >.
        while (i < len && code[i] != '>') {
          if (code[i] == '"' || code[i] == '\'') {
            char q = code[i];
            size_t end = code.find(q, i + 1);
            if (end == std::string_view::npos)
              end = len - 1;
            em.Emit(code.substr(i, end + 1 - i), ColorId::kSyntaxString);
            i = end + 1;
          } else if (IsWordChar(code[i]) && !IsDigit(code[i])) {
            size_t as = i;
            while (i < len && IsWordChar(code[i]))
              i++;
            em.Emit(code.substr(as, i - as), ColorId::kSyntaxType);
          } else {
            em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
                    ColorId::kDefault);
            i += base::Utf8SequenceLength(code, i);
          }
        }
        if (i < len) {
          em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
                  ColorId::kDefault);
          i += base::Utf8SequenceLength(code, i);
        }
        continue;
      }
      // Not a real tag, just emit the '<'.
      em.Emit(code.substr(start, 1), ColorId::kDefault);
      i = start + 1;
      continue;
    }

    // Entity references like &amp;
    if (code[i] == '&') {
      size_t end = code.find(';', i + 1);
      if (end != std::string_view::npos && end - i < 12) {
        em.Emit(code.substr(i, end + 1 - i), ColorId::kSyntaxNumber);
        i = end + 1;
        continue;
      }
    }

    em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
            ColorId::kDefault);
    i += base::Utf8SequenceLength(code, i);
  }
}

void TokenizeJson(std::string_view code, Emitter& em) {
  size_t i = 0;
  size_t len = code.size();

  while (i < len) {
    if (code[i] == '"') {
      size_t end = i + 1;
      while (end < len && code[end] != '"') {
        if (code[end] == '\\' && end + 1 < len)
          end++;
        end++;
      }
      if (end < len)
        end++;
      // Check if this is a key (followed by ':') or value.
      size_t after = end;
      while (after < len && (code[after] == ' ' || code[after] == '\t'))
        after++;
      ColorId color = (after < len && code[after] == ':')
                          ? ColorId::kSyntaxType
                          : ColorId::kSyntaxString;
      em.Emit(code.substr(i, end - i), color);
      i = end;
      continue;
    }

    if (IsDigit(code[i]) ||
        (code[i] == '-' && i + 1 < len && IsDigit(code[i + 1]))) {
      size_t start = i;
      if (code[i] == '-')
        i++;
      while (i < len && (IsDigit(code[i]) || code[i] == '.' || code[i] == 'e' ||
                         code[i] == 'E' || code[i] == '+' || code[i] == '-'))
        i++;
      em.Emit(code.substr(start, i - start), ColorId::kSyntaxNumber);
      continue;
    }

    if (IsWordChar(code[i]) && !IsDigit(code[i])) {
      size_t start = i;
      while (i < len && IsWordChar(code[i]))
        i++;
      std::string_view word = code.substr(start, i - start);
      if (MatchKeyword(word, kJsonKeywords, std::size(kJsonKeywords))) {
        em.Emit(word, ColorId::kSyntaxKeyword);
      } else {
        em.Emit(word, ColorId::kDefault);
      }
      continue;
    }

    em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
            ColorId::kDefault);
    i += base::Utf8SequenceLength(code, i);
  }
}

void TokenizeGn(std::string_view code, Emitter& em) {
  size_t i = 0;
  size_t len = code.size();

  while (i < len) {
    if (code[i] == '#') {
      em.Emit(code.substr(i), ColorId::kSyntaxComment);
      return;
    }

    if (code[i] == '"') {
      size_t end = i + 1;
      while (end < len && code[end] != '"') {
        if (code[end] == '\\' && end + 1 < len)
          end++;
        end++;
      }
      if (end < len)
        end++;
      em.Emit(code.substr(i, end - i), ColorId::kSyntaxString);
      i = end;
      continue;
    }

    if (IsDigit(code[i]) && (i == 0 || !IsWordChar(code[i - 1]))) {
      size_t start = i;
      while (i < len && IsDigit(code[i]))
        i++;
      em.Emit(code.substr(start, i - start), ColorId::kSyntaxNumber);
      continue;
    }

    if (IsWordChar(code[i]) && !IsDigit(code[i])) {
      size_t start = i;
      while (i < len && IsWordChar(code[i]))
        i++;
      std::string_view word = code.substr(start, i - start);
      if (MatchKeyword(word, kGnKeywords, std::size(kGnKeywords))) {
        em.Emit(word, ColorId::kSyntaxKeyword);
      } else {
        em.Emit(word, ColorId::kDefault);
      }
      continue;
    }

    em.Emit(code.substr(i, base::Utf8SequenceLength(code, i)),
            ColorId::kDefault);
    i += base::Utf8SequenceLength(code, i);
  }
}

}  // namespace

const char* GetLanguageName(Language lang) {
  switch (lang) {
    case Language::kNone:
      return "None";
    case Language::kCLike:
      return "C-like";
    case Language::kPython:
      return "Python";
    case Language::kShell:
      return "Shell";
    case Language::kRuby:
      return "Ruby";
    case Language::kHtml:
      return "HTML/XML";
    case Language::kJson:
      return "JSON";
    case Language::kGn:
      return "GN";
    case Language::kShader:
      return "Shader";
  }
  return "None";
}

Language ParseLanguageName(const std::string& name) {
  for (int i = 1; i <= kLanguageCount; i++) {
    if (name == GetLanguageName(static_cast<Language>(i)))
      return static_cast<Language>(i);
  }
  return Language::kNone;
}

Language DetectLanguage(
    const std::string& filename,
    const std::vector<std::pair<std::string, std::string>>& custom_mappings) {
  // Extract extension for custom mapping lookup.
  std::string ext;
  size_t dot = filename.rfind('.');
  if (dot != std::string::npos && dot != filename.size() - 1) {
    ext = filename.substr(dot + 1);
    for (char& c : ext)
      c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  }

  // Check custom mappings first.
  for (const auto& [map_ext, lang_name] : custom_mappings) {
    if (map_ext == ext) {
      Language lang = ParseLanguageName(lang_name);
      if (lang == Language::kNone)
        return Language::kNone;  // Explicitly mapped to None = disabled.
      return lang;
    }
  }

  // Built-in extensionless filenames.
  if (ext.empty()) {
    size_t slash = filename.rfind('/');
    std::string_view fn = filename;
    std::string_view base =
        slash != std::string::npos ? fn.substr(slash + 1) : fn;
    if (base == "Makefile" || base == "Dockerfile")
      return Language::kShell;
    return Language::kNone;
  }

  // Built-in extension mappings.
  if (ext == "c" || ext == "cc" || ext == "cpp" || ext == "cxx" || ext == "h" ||
      ext == "hh" || ext == "hpp" || ext == "hxx" || ext == "java" ||
      ext == "go" || ext == "rs" || ext == "cs" || ext == "js" ||
      ext == "jsx" || ext == "ts" || ext == "tsx" || ext == "m" ||
      ext == "mm" || ext == "swift" || ext == "kt" || ext == "kts" ||
      ext == "scala" || ext == "zig" || ext == "d" || ext == "dart")
    return Language::kCLike;
  if (ext == "glsl" || ext == "hlsl" || ext == "metal" || ext == "wgsl" ||
      ext == "vert" || ext == "frag" || ext == "comp" || ext == "geom" ||
      ext == "tesc" || ext == "tese" || ext == "rgen" || ext == "rchit" ||
      ext == "rmiss" || ext == "rahit" || ext == "rcall" || ext == "mesh" ||
      ext == "task" || ext == "slang")
    return Language::kShader;
  if (ext == "py" || ext == "pyi" || ext == "pyw")
    return Language::kPython;
  if (ext == "sh" || ext == "bash" || ext == "zsh" || ext == "fish" ||
      ext == "ksh" || ext == "csh")
    return Language::kShell;
  if (ext == "rb" || ext == "rake" || ext == "gemspec")
    return Language::kRuby;
  if (ext == "html" || ext == "htm" || ext == "xhtml" || ext == "xml" ||
      ext == "svg" || ext == "vue" || ext == "svelte" || ext == "jsp" ||
      ext == "erb")
    return Language::kHtml;
  if (ext == "json" || ext == "jsonc" || ext == "json5")
    return Language::kJson;
  if (ext == "gn" || ext == "gni")
    return Language::kGn;

  return Language::kNone;
}

ColoredLine HighlightSyntax(const std::string& line,
                            Language lang,
                            bool in_block_comment) {
  if (lang == Language::kNone || line.empty())
    return {};

  char prefix = line[0];
  bool has_diff_prefix = (prefix == ' ' || prefix == '+' || prefix == '-');

  // Skip header and hunk marker lines (but allow unprefixed content lines
  // from untracked files).
  if (!has_diff_prefix &&
      (line[0] == '\x01' || line.starts_with("@@") ||
       line.starts_with("diff ") || line.starts_with("index ") ||
       line.starts_with("new file") || line.starts_with("deleted file") ||
       line.starts_with("rename ") || line.starts_with("copy ") ||
       line.starts_with("similarity ") || line.starts_with("Submodule ") ||
       line.starts_with("* ")))
    return {};

  Emitter em;
  std::string_view code;
  if (has_diff_prefix) {
    code = std::string_view(line.data() + 1, line.size() - 1);
    em.Emit(line.substr(0, 1), ColorId::kDefault);
  } else {
    code = line;
  }

  switch (lang) {
    case Language::kCLike:
      TokenizeCLike(code, em, kCLikeKeywords, std::size(kCLikeKeywords),
                    kCLikeTypes, std::size(kCLikeTypes), in_block_comment);
      break;
    case Language::kPython:
      TokenizePython(code, em);
      break;
    case Language::kShell:
      TokenizeShell(code, em);
      break;
    case Language::kRuby:
      TokenizeRuby(code, em);
      break;
    case Language::kHtml:
      TokenizeHtml(code, em, in_block_comment);
      break;
    case Language::kJson:
      TokenizeJson(code, em);
      break;
    case Language::kGn:
      TokenizeGn(code, em);
      break;
    case Language::kShader:
      TokenizeCLike(code, em, kShaderKeywords, std::size(kShaderKeywords),
                    kShaderTypes, std::size(kShaderTypes), in_block_comment);
      break;
    case Language::kNone:
      break;
  }

  return em.result;
}
