#!/usr/bin/env python3

# Copyright (C) 2026 Opera Norway AS. All rights reserved.
#
# This file is an original work developed by Opera.

"""Generate a version header from git tags.

Every release is tagged, so the version is the tag HEAD points to
(e.g. `v0.15-beta` -> `0.15-beta`). Builds not on a tag get a `-dev`
suffix based on the most recent `v*.*` tag (or `0.0.0-dev` if none
exists).

Also writes a Ninja depfile tracking `.git/HEAD` and the current branch ref
so the action re-runs on new commits.

Used by the GN build to produce `version.h`.
"""

import os
import subprocess
import sys


def main():
    if len(sys.argv) < 4:
        print("Usage: gen_version.py <output.h> <depfile> <repo_root>"
              " [--debug]", file=sys.stderr)
        sys.exit(1)

    output_path = sys.argv[1]
    depfile_path = sys.argv[2]
    repo_root = sys.argv[3]
    is_debug = "--debug" in sys.argv

    # The version is the tag HEAD points to, if any.
    try:
        tag = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match", "--match",
             "v*.*", "HEAD"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except subprocess.CalledProcessError:
        tag = ""

    if tag:
        version = tag.removeprefix("v")
    else:
        # Not on a tag: derive the version from the most recent tag and
        # mark the build as a development build.
        try:
            base_tag = subprocess.check_output(
                ["git", "describe", "--tags", "--abbrev=0", "--match",
                 "v*.*", "HEAD"],
                stderr=subprocess.DEVNULL,
                text=True,
            ).strip()
        except subprocess.CalledProcessError:
            base_tag = ""
        if base_tag:
            version = base_tag.removeprefix("v") + "-dev"
        else:
            version = "0.0.0-dev"

    if is_debug:
        version += " (debug)"

    content = (
        "#ifndef GEL_VERSION_H\n"
        "#define GEL_VERSION_H\n"
        "\n"
        f'#define GEL_VERSION "{version}"\n'
        "\n"
        "#endif  // GEL_VERSION_H\n"
    )

    # Only write if content changed to avoid unnecessary recompilation.
    try:
        with open(output_path, "r", encoding="utf-8") as f:
            existing = f.read()
    except FileNotFoundError:
        existing = None

    if existing != content:
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(content)

    # Write depfile so Ninja re-runs this action when the current branch ref
    # changes (i.e. on new commits), not just on branch switches (.git/HEAD).
    git_dir = os.path.join(repo_root, ".git")
    deps = [os.path.join(git_dir, "HEAD")]
    try:
        with open(deps[0], "r") as f:
            head = f.read().strip()
        if head.startswith("ref: "):
            ref_path = os.path.join(git_dir, head[5:])
            if os.path.isfile(ref_path):
                deps.append(ref_path)
    except OSError:
        pass
    packed = os.path.join(git_dir, "packed-refs")
    if os.path.isfile(packed):
        deps.append(packed)
    with open(depfile_path, "w") as f:
        f.write(output_path + ": " + " ".join(deps) + "\n")


if __name__ == "__main__":
    main()
