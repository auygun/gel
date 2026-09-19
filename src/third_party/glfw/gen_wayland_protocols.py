#!/usr/bin/env python3
"""Generate Wayland protocol headers using wayland-scanner.

Usage: gen_wayland_protocols.py <output_dir> <xml_dir> <proto1> [proto2 ...]
"""

import os
import subprocess
import sys


def main():
    output_dir = sys.argv[1]
    xml_dir = sys.argv[2]
    protocols = sys.argv[3:]

    os.makedirs(output_dir, exist_ok=True)

    for proto in protocols:
        xml = os.path.join(xml_dir, proto + ".xml")
        header = os.path.join(output_dir, proto + "-client-protocol.h")
        code = os.path.join(output_dir, proto + "-client-protocol-code.h")

        subprocess.check_call(
            ["wayland-scanner", "client-header", xml, header])
        subprocess.check_call(
            ["wayland-scanner", "private-code", xml, code])


if __name__ == "__main__":
    main()
