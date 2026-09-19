#!/usr/bin/env python3

# Copyright (C) 2026 Opera Norway AS. All rights reserved.
#
# This file is an original work developed by Opera.

"""Convert a PNG image into a Windows ICO file.

Wraps the raw PNG data inside a minimal ICO container (header + directory
entry + embedded PNG). The resulting file can be used as the application
icon on Windows.

Used by the GN build to produce the `.ico` resource from the app icon PNG.
"""

import struct
import sys


def main():
    if len(sys.argv) != 3:
        print(
            "Usage: gen_icon_ico.py <input.png> <output.ico>",
            file=sys.stderr,
        )
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(input_path, "rb") as f:
        png_data = f.read()

    # ICO header: reserved(2) + type(2) + count(2)
    header = struct.pack("<HHH", 0, 1, 1)
    # ICO directory entry: width, height (0 = 256), color_planes, bpp,
    # data_size, data_offset
    png_size = len(png_data)
    data_offset = 6 + 16  # header + one directory entry
    entry = struct.pack("<BBBBHHII", 0, 0, 0, 0, 1, 32, png_size, data_offset)

    with open(output_path, "wb") as f:
        f.write(header + entry + png_data)


if __name__ == "__main__":
    main()
