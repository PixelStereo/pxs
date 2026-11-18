# Conventions

These rules apply to every Pixel Stereo application. The mechanisms are in this repository; an application only
passes its own names.

## The application carries its version in its name

| Build | Name |
|---|---|
| a release (the tagged commit, nothing modified; any build made by CI on a tag) | `App 0.2.1` |
| built 3 commits after the 0.2.1 release | `App 0.2.1+3` |
| the same, with changes that are not committed | `App 0.2.1+3+` |

The bundle (`App 0.2.1.app`), the Windows executable and the window title carry that name; the build writes it in
`application.json` (`name`, `version`, `executable`) so that packaging scripts never rebuild it. Use
`pxs_build_suffix()` and `pxs_nearest_release()` from `cmake/PixelStereoBuildId.cmake`, or `scripts/build_id.py` from a Python packager (CI must check out the tags: `fetch-depth: 0`). The number counted from is the
nearest release tag, not a `VERSION` file that can lag behind (the file is synchronised after a release is published).
The suffix is computed when CMake configures. A source archive without Git metadata has no suffix.

## Downloads

Release files are named `<App>-<os>-<arch>-<tag><suffix>`, the same in every application:

| | |
|---|---|
| os | `macos`, `windows`, `linux` (and `android`, `ios`) |
| arch | `arm64`, `x64` (Intel or AMD 64-bit), `universal` for a macOS app holding both |
| tag | the release tag, `v1.2.3` |
| suffix | `.zip` (macOS), `-Setup.exe` (Windows), `.AppImage` / `.tar.gz` / `.deb` (Linux), `.apk`, `-unsigned.ipa` / `.ipa` |

`MpOsc-macos-arm64-v0.3.1.zip`, `MpOsc-windows-x64-v0.3.1-Setup.exe`, `NyxMap-linux-x64-v0.6.8.AppImage`. A build that is
not a release says `dev` in place of the tag. The README download links are rewritten from these names (the script
accepts the older names of releases published before the rule, until a newer release exists).

- **macOS: a zip with the application inside.** Never a DMG. Qt deployed into the bundle, every Mach-O file and bundle
  signed ad hoc (without a valid signature an Apple Silicon Mac calls the download "damaged"), then
  `ditto -c -k --keepParent` (Python's `zipfile` loses the symlinks the signature depends on). Check the zip, not the
  folder: extract it, `codesign --verify --deep --strict`, launch it. See `scripts/package-macos.sh`.
- **Windows: an installer**, `App-windows-x64-<tag>-Setup.exe`, built with `packaging/windows.nsi`.
  - The user chooses: **all users (the default)**, or only the current user (no administrator rights needed).
    Silent installs: `/S /AllUsers` or `/S /CurrentUser`, and `/D=<dir>` last.
  - **Nothing else to install:** the Visual C++ runtime DLLs (`vcruntime140`, `vcruntime140_1`, `msvcp140`…) are next to
    the executable, from the compiler's `Microsoft.VC*.CRT` redistributable folder. No `vc_redist` installer in the
    package (use `windeployqt --no-compiler-runtime`), no prompt about Visual Studio.
  - The payload goes in `<install dir>\app`, emptied before each install: the executable name carries the version, so an
    upgrade must not leave the previous one beside the new one.
  - The CI installs the result silently in a clean folder and starts what it installed.
- **Linux**: an AppImage and a `tar.gz` where the application has a Linux version.

## The README follows the releases

`<!-- release-downloads:start -->` / `end` markers in the README (platform badges, as in NyxMap), rewritten together with
`VERSION` when a release is published, from the newest release that has every requested file (a release being
uploaded never produces dead links). The application's About box reads its version from the build, so it follows too.
An application only calls the shared workflow:

```yaml
name: Release downloads
on:
  release: { types: [published, edited, deleted] }
  workflow_dispatch:
permissions: { contents: write }
jobs:
  update:
    uses: PixelStereo/pxs/.github/workflows/release-downloads.yml@v0.12.0
    with: { app: MpOsc, platforms: "mac,win", pxs-ref: v0.12.0 }   # platforms: mac, win, linux, android, ios
```

The script is `scripts/release_downloads.py`; the commit says which release it follows.

## Licence

AGPL-3.0, for the applications and for this repository.

## Parameters

Declared once with `pxs::params`, see `parameters.md`.

## The look

Always dark (never a light theme, never following the system). A palette is a set of colour tokens (`pxs::graphite()`,
`pxs::nyxmap()`); an application's own style sheet is written with the tokens (`@text`, `@muted`, `@fill`, `@border`,
`@focus`…), never with literal colours, and handed to `pxs::apply()`. Checkable buttons are named `toggle`, group boxes
`section`, small labels above a control `caption`, so the shared sheet styles them everywhere the same way. Tabs are part of that sheet too (recessed tab, raised selected tab with the accent underline): do not restyle them per application. `pxs::apply()` also sets the Fusion style with the shared check boxes (a dark rounded square, the accent as a filled square when on): every `QCheckBox` and checkable list row looks the same in all the applications, so never restyle `QCheckBox::indicator` in a style sheet and do not subclass a check box to draw one (call `pxs::paintCheck()` for a custom widget). An application that installs a proxy style of its own before `pxs::apply()` keeps it.
