#!/usr/bin/env python3

# Copyright (C) 2026 Opera Norway AS. All rights reserved.
#
# This file is an original work developed by Opera.

"""Embed a PNG image as a C++ byte array.

Reads a PNG file and generates a header containing the raw bytes as a
`constexpr unsigned char[]` array along with a length constant.

Used by the GN build to produce `icon_data.h` from the app icon PNG.
"""

import sys


def main():
    if len(sys.argv) != 3:
        print(
            "Usage: gen_icon_data.py <input.png> <output.h>",
            file=sys.stderr,
        )
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(input_path, "rb") as f:
        png_data = f.read()

    varname = "kIconData"
    guard = "GEL_ICON_DATA_H"

    lines = []
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.append("#include <cstddef>")
    lines.append("")
    lines.append("// clang-format off")
    lines.append(f"constexpr unsigned char {varname}[] = {{")

    # 12 bytes per line to match the original format.
    for i in range(0, len(png_data), 12):
        chunk = png_data[i : i + 12]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_vals},")

    lines.append("};")
    lines.append(
        f"constexpr unsigned int {varname}Len = {len(png_data)};"
    )
    lines.append("// clang-format on")
    lines.append("")
    lines.append(f"#endif  // {guard}")
    lines.append("")

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
