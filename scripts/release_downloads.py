#!/usr/bin/env python3
"""Rewrite the README download block and VERSION from the newest complete GitHub release.

    gh api --paginate --slurp "repos/$GITHUB_REPOSITORY/releases?per_page=100" |
        python3 release_downloads.py --app MpOsc --repo PixelStereo/MpOsc --platforms mac,win

Assets are named <App>-<os>-<arch>-<tag><suffix> (docs/conventions.md); the older names of releases published before
the rule are still recognised until a newer release exists. A release that lacks one of the requested platforms (it
is still being uploaded) is skipped, so the README never points to a dead link.
Prints the tag the README now follows.
"""
import argparse
import json
import re
import sys
from pathlib import Path

START = "<!-- release-downloads:start -->"
END = "<!-- release-downloads:end -->"
TAG = re.compile(r"v?\d+\.\d+(?:\.\d+)?(?:[-+][0-9A-Za-z.-]+)?")

# platform -> (label, shields colour, logo, logo colour, os names seen in file names, suffixes by preference)
PLATFORMS = {
    "mac": ("macOS", "34CC34", "apple", "white", ["macos", "mac"], [".zip", ".dmg"]),
    "win": ("Windows", "CCCC34", "windows", "white", ["windows", "win"], ["-Setup.exe", "-setup.exe", ".exe", ".zip"]),
    "linux": ("Linux", "CC3434", "linux", "111111", ["linux"], [".AppImage", ".tar.gz", ".deb"]),
    "android": ("Android", "3DDC84", "android", "white", ["android"], [".apk"]),
    "ios": ("iOS", "555555", "apple", "white", ["ios"], ["-unsigned.ipa", ".ipa"]),
}
ARCH_PREFERENCE = ["arm64", "universal", "x64", "x86_64", "amd64"]


def version(tag):
    numbers = re.match(r"v?(\d+)\.(\d+)(?:\.(\d+))?", tag)
    stable = 0 if re.search(r"[-+]", tag) else 1  # 1.2.0 comes after 1.2.0-rc1
    return (tuple(int(n or 0) for n in numbers.groups()) if numbers else (0, 0, 0)) + (stable,)


def pick(app, tag, platform, assets):
    """The best uploaded file for a platform, or None."""
    _, _, _, _, oses, suffixes = PLATFORMS[platform]
    best = None
    for name in assets:
        low = name.lower()
        if not low.startswith(app.lower() + "-") or tag.lower() not in low:
            continue
        rest = low[len(app) + 1:]
        if not any(rest == o or rest.startswith(o + "-") for o in oses):
            continue
        suffix = next((i for i, s in enumerate(suffixes) if low.endswith(s.lower())), None)
        if suffix is None:
            continue
        arch = next((i for i, a in enumerate(ARCH_PREFERENCE) if f"-{a}-" in low), len(ARCH_PREFERENCE))
        key = (suffix, arch, name)
        if best is None or key < best:
            best = key
    return best[2] if best else None


def select(releases, app, repo, platforms):
    # Newest version first (not newest publication: a republished old release must not win).
    published = sorted((r for r in releases if not r.get("draft") and r.get("published_at")),
                       key=lambda r: (version(r["tag_name"]), r["published_at"]), reverse=True)
    for release in published:
        tag = release["tag_name"]
        if not TAG.fullmatch(tag):
            continue
        assets = {a["name"]: a for a in release.get("assets", []) if a.get("state") == "uploaded" and a.get("size", 0) > 0}
        urls = {}
        for platform in platforms:
            name = pick(app, tag, platform, assets)
            if not name:
                break
            urls[platform] = f"https://github.com/{repo}/releases/download/{tag}/{name}"
        else:
            return release, urls
    raise ValueError("No published release has a file for every requested platform")


def render(release, urls, repo, docs):
    tag = release["tag_name"]
    preview = " · PREVIEW" if release.get("prerelease") else ""
    lines = [START, f"**LATEST VERSION · [{tag}{preview}](https://github.com/{repo}/releases/tag/{tag})**", ""]
    for platform, url in urls.items():
        label, colour, logo, logo_colour = PLATFORMS[platform][:4]
        lines.append(f"[![{label}](https://img.shields.io/badge/{label}-Download-{colour}"
                     f"?style=for-the-badge&logo={logo}&logoColor={logo_colour})]({url})")
    if docs:
        lines += ["", f'<a href="{docs}"><img alt="Documentation" src="https://img.shields.io/badge/documentation-blue'
                      f'?style=for-the-badge&logo=material%20for%20mkdocs" height="28"></a>']
    lines.append(END)
    return "\n".join(lines)


def update(root, releases, app, repo, platforms, docs=None):
    release, urls = select(releases, app, repo, platforms)
    path = root / "README.md"
    text = path.read_text(encoding="utf-8")
    if text.count(START) != 1 or text.count(END) != 1 or text.index(START) > text.index(END):
        raise ValueError("README download markers must occur exactly once, in order")
    start, end = text.index(START), text.index(END) + len(END)
    path.write_text(text[:start] + render(release, urls, repo, docs) + text[end:], encoding="utf-8")
    (root / "VERSION").write_text(release["tag_name"].removeprefix("v") + "\n", encoding="utf-8")
    return release["tag_name"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--repo", required=True, help="owner/name")
    ap.add_argument("--platforms", default="mac,win", help="comma list among " + ",".join(PLATFORMS))
    ap.add_argument("--docs", help="documentation URL, adds a badge")
    ap.add_argument("--root", default=".")
    args = ap.parse_args()
    platforms = [p.strip() for p in args.platforms.split(",") if p.strip()]
    unknown = [p for p in platforms if p not in PLATFORMS]
    if unknown:
        sys.exit("unknown platform: " + ", ".join(unknown))
    data = json.load(sys.stdin)
    releases = [r for page in data for r in (page if isinstance(page, list) else [page])]
    print(update(Path(args.root), releases, args.app, args.repo, platforms, args.docs))


if __name__ == "__main__":
    main()
