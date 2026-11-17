# pxs

Shared code and conventions for the Pixel Stereo Qt applications (NyxMap, AtemRemoteOSC, ISFLab, DMXPulse,
Cold-Flame, MpOsc). Licensed under the GNU Affero General Public License v3.0 (see `LICENSE`).

| Path | What |
|---|---|
| `src/osc` | `pxs::osc`: OSC 1.0 codec (messages, bundles, address patterns), Qt Core only |
| `src/widgets` | `pxs::widgets`: `SliderField` (drag, type a value, snaps, gradient bar) and `ColorPicker` (RGB, HSV, HSL, wheel, palette), Qt Widgets only |
| `cmake/PixelStereoBuildId.cmake` | the application's version in its name: `0.2.1`, `0.2.1+3`, `0.2.1+3+` |
| `scripts/build_id.py` | the same version for packagers that are not CMake |
| `packaging/windows.nsi` | the Windows installer: all users by default, or the current user only |
| `scripts/package-macos.sh` | the macOS zip: Qt deployed, ad-hoc signed, made with `ditto` |
| `docs/conventions.md` | how the applications are named, built, packaged and released |

## Using it

Pin a tag, never a branch:

```cmake
include(FetchContent)
FetchContent_Declare(pxs GIT_REPOSITORY https://github.com/PixelStereo/pxs.git GIT_TAG v0.3.2)
FetchContent_MakeAvailable(pxs)
include(${pxs_SOURCE_DIR}/cmake/PixelStereoBuildId.cmake)
target_link_libraries(MyApp PRIVATE pxs::osc pxs::widgets)
```

Qt 6.8 or later, C++17.

## Migrating an application

The files in `src/osc` are byte for byte the ones MpOsc and NyxMap carried. An application migrates by removing its
copy, linking `pxs::osc` and keeping `#include "OscCodec.h"`; the namespace stays `osc::`.
