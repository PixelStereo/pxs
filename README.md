# pxs

Shared code and conventions for the Pixel Stereo Qt applications (NyxMap, AtemRemoteOSC, ISFLab, DMXPulse,
Cold-Flame, MpOsc). Licensed under the GNU Affero General Public License v3.0 (see `LICENSE`).

| Path | What |
|---|---|
| `src/osc` | `pxs::osc`: OSC 1.0 codec (messages, bundles, address patterns), Qt Core only |
| `src/params` | `pxs::params`: how a parameter is declared (`Parameter`, `ParamInfo`) and how the others are told it changed (`ChangeFeed`); see `docs/parameters.md` |
| `src/tree` | `pxs::tree`: the address space, a tree of nodes carrying parameters, OSC messages applied to it, the OSCQuery description derived from it (inspired by Ossia); see `docs/oscquery.md` |
| `src/dmx` | `pxs::dmx`: the frames of Art-Net and sACN, encoded and decoded and checked in full, Qt Core only; the sockets and what a channel drives stay in the application |
| `src/midi` | `pxs::midi`: MIDI messages as interaction events (parse, build, learn), the ports and the inputs to listen to while a MIDI interaction needs them; the ports need an RtMidi the application names (`PXS_RTMIDI_TARGET`) or a system one, else `available()` is false |
| `src/theme` | `pxs::theme`: colour tokens, the `graphite` and `nyxmap` dark palettes, the shared style sheet (sections, toggles, captions, tabs), `pxs::apply()` (also sets Fusion, the check boxes and radio buttons); `#include <pxs/Theme.h>` |
| `src/widgets` | `pxs::widgets`: the number fields (`SliderField`, `RangeField`, `NumberBox`, `IntBox`, from NyxMap) and `ColorPicker` (RGB, HSV, HSL, wheel, palette), Qt Widgets only |
| `cmake/PixelStereoBuildId.cmake` | the application's version in its name: `0.2.1`, `0.2.1+3`, `0.2.1+3+` |
| `scripts/build_id.py` | the same version for packagers that are not CMake |
| `packaging/windows.nsi` | the Windows installer: all users by default, or the current user only |
| `scripts/package-macos.sh` | the macOS zip: Qt deployed, ad-hoc signed, made with `ditto` |
| `docs/conventions.md` | how the applications are named, built, packaged and released |

## Using it

Pin a tag, never a branch:

```cmake
include(FetchContent)
FetchContent_Declare(pxs GIT_REPOSITORY https://github.com/PixelStereo/pxs.git GIT_TAG v0.10.0)
FetchContent_MakeAvailable(pxs)
include(${pxs_SOURCE_DIR}/cmake/PixelStereoBuildId.cmake)
target_link_libraries(MyApp PRIVATE pxs::osc pxs::params pxs::tree pxs::theme pxs::widgets)
```

Qt 6.4 or later, C++17.

## Migrating an application

The files in `src/osc` are byte for byte the ones MpOsc and NyxMap carried. An application migrates by removing its
copy, linking `pxs::osc` and keeping `#include "OscCodec.h"`; the namespace stays `osc::`.
