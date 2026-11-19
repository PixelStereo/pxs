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

# A platform is what a badge offers: a macOS zip per architecture, the Windows installer, Linux. `archs` are the
# architectures of the file names that fit it (None: a name without one, from before the rule), best first.
PLATFORMS = {
    "mac-arm": dict(label="macOS", sub="Apple Silicon", logo="apple", colour="6b7787", logo_colour="white",
                    oses=["macos", "mac"], archs=["arm64", "universal", None], suffixes=[".zip", ".dmg"]),
    "mac-intel": dict(label="macOS", sub="Intel", logo="apple", colour="515b69", logo_colour="white",
                      oses=["macos", "mac"], archs=["x64", "x86_64", "amd64", "universal"], suffixes=[".zip", ".dmg"]),
    "win": dict(label="Windows", sub="Installer", logo="windows", colour="2b6fb6", logo_colour="white",
                oses=["windows", "win"], archs=["x64", "x86_64", "amd64", None, "arm64"],
                suffixes=["-Setup.exe", "-setup.exe", ".exe", ".zip"]),
    "linux": dict(label="Linux", sub="AppImage", logo="linux", colour="b8821c", logo_colour="white",
                  oses=["linux"], archs=["x64", "x86_64", "amd64", None],
                  suffixes=[".AppImage", ".tar.gz", ".deb"]),
    # The Raspberry Pi 4 and 5 (64-bit Raspberry Pi OS): the Linux arm64 file
    "rpi": dict(label="Raspberry Pi", sub="AppImage", logo="raspberrypi", colour="a22846", logo_colour="white",
                oses=["linux"], archs=["arm64"], suffixes=[".AppImage", ".tar.gz", ".deb"]),
    "android": dict(label="Android", sub="APK", logo="android", colour="2f9e6a", logo_colour="white",
                    oses=["android"], archs=["arm64", None], suffixes=[".apk"]),
    "ios": dict(label="iOS", sub="IPA", logo="apple", colour="515b69", logo_colour="white",
                oses=["ios"], archs=["arm64", None], suffixes=["-unsigned.ipa", ".ipa"]),
}
# "mac" asks for both macOS badges, the Intel one only when the release has it
ALIASES = {"mac": ["mac-arm", "mac-intel?"]}
GRAPHITE = "1a1c20"
KNOWN_ARCHS = ("arm64", "universal", "x64", "x86_64", "amd64")
# What the badge says for a file of this kind (Linux)
SUB_BY_SUFFIX = {".appimage": "AppImage", ".tar.gz": "Archive", ".deb": "Debian package", "-setup.exe": "Installer",
                 ".exe": "Installer", ".zip": None, ".dmg": "Disk image"}


def version(tag):
    numbers = re.match(r"v?(\d+)\.(\d+)(?:\.(\d+))?", tag)
    stable = 0 if re.search(r"[-+]", tag) else 1  # 1.2.0 comes after 1.2.0-rc1
    return (tuple(int(n or 0) for n in numbers.groups()) if numbers else (0, 0, 0)) + (stable,)


def arch_of(name):
    low = name.lower()
    return next((a for a in KNOWN_ARCHS if f"-{a}-" in low), None)


def pick(app, tag, platform, assets):
    """The best uploaded file for a platform, or None."""
    spec = PLATFORMS[platform]
    best = None
    for name in assets:
        low = name.lower()
        if not low.startswith(app.lower() + "-") or tag.lower() not in low:
            continue
        rest = low[len(app) + 1:]
        if not any(rest == o or rest.startswith(o + "-") for o in spec["oses"]):
            continue
        suffix = next((i for i, s in enumerate(spec["suffixes"]) if low.endswith(s.lower())), None)
        arch = arch_of(name)
        if suffix is None or arch not in spec["archs"]:
            continue
        key = (suffix, spec["archs"].index(arch), name)
        if best is None or key < best:
            best = key
    return best[2] if best else None


def expand(platforms):
    """(platform, optional) in order; "mac" stands for the two macOS badges."""
    out = []
    for item in platforms:
        for entry in ALIASES.get(item, [item]):
            optional = entry.endswith("?")
            out.append((entry.rstrip("?"), optional))
    return out


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
        for platform, optional in expand(platforms):
            name = pick(app, tag, platform, assets)
            if not name:
                if optional:
                    continue
                break
            urls[platform] = f"https://github.com/{repo}/releases/download/{tag}/{name}"
        else:
            return release, urls
    raise ValueError("No published release has a file for every requested platform")


def esc(text):
    """Text for a shields.io badge path: a dash is doubled, a space is an underscore."""
    return text.replace("-", "--").replace("_", "__").replace(" ", "_")


def badge(platform, url):
    spec = PLATFORMS[platform]
    sub = spec["sub"]
    if platform in ("linux", "rpi", "win"):
        kind = next((v for k, v in SUB_BY_SUFFIX.items() if url.lower().endswith(k) and v), None)
        sub = kind or sub
    return (f"[![{spec['label']} {spec['sub']}](https://img.shields.io/badge/{esc(spec['label'])}-{esc(sub)}-{spec['colour']}"
            f"?style=for-the-badge&logo={spec['logo']}&logoColor={spec['logo_colour']}&labelColor={GRAPHITE})]({url})")


def render(release, urls, repo, docs):
    tag = release["tag_name"]
    preview = " · PREVIEW" if release.get("prerelease") else ""
    lines = [START, f"**LATEST VERSION · [{tag}{preview}](https://github.com/{repo}/releases/tag/{tag})**", "",
             "&nbsp;&nbsp;".join(badge(p, u) for p, u in urls.items())]
    if docs:
        lines += ["", f'<a href="{docs}"><img alt="Documentation" src="https://img.shields.io/badge/Documentation-Read-3a3f47'
                      f'?style=for-the-badge&logo=materialformkdocs&logoColor=white&labelColor={GRAPHITE}" height="28"></a>']
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
    ap.add_argument("--platforms", default="mac,win", help="comma list among " + ",".join(PLATFORMS) + ", mac (arm, and Intel when there is one)")
    ap.add_argument("--docs", help="documentation URL, adds a badge")
    ap.add_argument("--root", default=".")
    args = ap.parse_args()
    platforms = [p.strip() for p in args.platforms.split(",") if p.strip()]
    unknown = [p for p, _ in expand(platforms) if p not in PLATFORMS]
    if unknown:
        sys.exit("unknown platform: " + ", ".join(unknown))
    data = json.load(sys.stdin)
    releases = [r for page in data for r in (page if isinstance(page, list) else [page])]
    print(update(Path(args.root), releases, args.app, args.repo, platforms, args.docs))


if __name__ == "__main__":
    main()
