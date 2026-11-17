#!/usr/bin/env python3
"""The version an application carries in its name, for the packagers that are not CMake.

    build_id.py <source dir> [--tag v0.2.1] [--fallback 0.2.1]

  0.2.1      the release: the tag given, or a tagged commit with nothing modified
  0.2.1+3    3 commits after the 0.2.1 release
  0.2.1+3+   the same, with changes that are not committed

Same rules as cmake/PixelStereoBuildId.cmake (see docs/conventions.md).
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys


def git(root, *args):
    result = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def display_version(root, tag=None, fallback=None):
    if tag:
        return re.sub(r"^v", "", tag)
    described = git(root, "describe", "--tags", "--long", "--match", "v[0-9]*", "--match", "[0-9]*", "HEAD")
    match = re.fullmatch(r"(.+)-(\d+)-g[0-9a-f]+", described or "")
    if match:
        base, count = re.sub(r"^v", "", match.group(1)), int(match.group(2))
    else:
        counted = git(root, "rev-list", "--count", "HEAD")
        if counted is None:
            return fallback or "unknown"  # a source archive without Git metadata
        base, count = fallback or "0.0.0", int(counted)
    changes = git(root, "status", "--porcelain", "--untracked-files=normal")
    if count == 0 and not changes:
        return base
    return f"{base}+{count}" + ("+" if changes else "")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("root", type=Path)
    parser.add_argument("--tag", help="a release build: the tag being built")
    parser.add_argument("--fallback", help="the number to use when there is no release tag or no Git")
    args = parser.parse_args()
    print(display_version(args.root, args.tag, args.fallback))


if __name__ == "__main__":
    sys.exit(main())
