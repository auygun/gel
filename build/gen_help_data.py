#!/usr/bin/env python3

# Copyright (C) 2026 Opera Norway AS. All rights reserved.
#
# This file is an original work developed by Opera.

"""Embed a Markdown file as a C++ string literal.

Reads a Markdown file and generates a header containing the contents as a
`constexpr char[]` array. The data is split into 2000-character chunks to
stay within MSVC's string literal length limit.

Used by the GN build to produce `help_data.h` from `HELP.md`.
"""

import sys

def main():
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print("Usage: gen_help_data.py <input.md> <output.h> [varname]", file=sys.stderr)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    varname = sys.argv[3] if len(sys.argv) > 3 else "kHelpData"

    guard = "GEL_" + varname.lstrip('k').upper() + "_H"

    with open(input_path, "rb") as f:
        content = f.read()

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n")
        f.write("\n")
        f.write("// clang-format off\n")
        f.write(f'constexpr char {varname}[] =\n')

        # Split into chunks of 2000 characters to avoid MSVC limit on single string literal length.
        chunk_size = 2000
        for i in range(0, len(content), chunk_size):
            chunk = content[i:i+chunk_size]
            f.write('    "')
            for b in chunk:
                if b == ord('\\'): f.write('\\\\')
                elif b == ord('"'): f.write('\\"')
                elif b == ord('\n'): f.write('\\n')
                elif b == ord('\r'): f.write('\\r')
                elif b == ord('\t'): f.write('\\t')
                elif 32 <= b <= 126: f.write(chr(b))
                else: f.write(f'\\x{b:02x}""')
            f.write('"\n')

        f.write('    ;\n')
        f.write("// clang-format on\n")
        f.write("\n")
        f.write(f"#endif  // {guard}\n")

if __name__ == "__main__":
    main()
