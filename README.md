# hmode7-apple-mobile

> Native port of MGC's H-Mode7 RPG Maker XP plugin to mkxp-z on Apple mobile platforms.

[![License](https://img.shields.io/badge/license-permissive-blue.svg)](#license)
[![Status](https://img.shields.io/badge/status-complete-brightgreen.svg)](#status)

H-Mode7 is a pseudo-3D rotating-perspective renderer. It adds slanted top-down maps with zoom, rotation, and heightmap-based terrain elevation to RPG Maker XP. Pokemon Insurgence uses it for its flying-Pelipper intro across the Torren region; other fan games use it for cutscenes and overworld effects.

The original plugin ships as a Windows-only native DLL (`MGC_Hmode7.dll`), which means no game using H-Mode7 renders correctly on iOS, Android, macOS, or any non-Windows port. This repo is a clean-room re-port written against mkxp-z's `Bitmap` and `Table` APIs, so the plugin's pixel-math runs as a module linked directly into the engine instead of via `Win32API.new(...)`.

## Highlights

- **Drop-in replacement for `MGC_Hmode7.dll`.** Games keep their original Ruby scripts; `HM7.render_hm7(...)` (etc.) dispatches to the native module via mkxp-z's binding layer.
- **Multi-Ruby compatible.** The same source compiles against Ruby 1.8, 1.9, 3.0, and 3.1 in mkxp-z-apple-mobile's per-version build pipeline. Vintage RGSS1 games that run on actual Ruby 1.8 (Pokemon Insurgence, etc.) get hmode7 dispatched via the matching merged `.o`.
- **Two-DLL-era support.** Auto-detects whether a game shipped a pre-V1.3 (8-export) or V1.4+ (9-export) DLL and switches the wall-layer-selection algorithm accordingly. See [`docs/WALL_LAYER_MODE.md`](docs/WALL_LAYER_MODE.md).
- **Verified against Pokemon Insurgence.** The full intro cinematic renders matching the Windows reference: wall textures, Z-ordering, billboard sprites, horizon fading, heightmap relief, autotile animation, and multi-layer tile-facade rendering for buildings.
- **No GPU.** Per-pixel, per-scanline software rasterization. Reads and writes directly into mkxp-z `Bitmap` pixel buffers and syncs back to the GL texture once per frame.

## Status

Complete and verified. All nine H-Mode7 exports are covered, for both DLL eras:

| Export | Role |
|---|---|
| `applyOpacity`, `applyZoom`, `applyLighting` | Per-bitmap pixel transforms |
| `computeM7` | Mode-7 projection LUT builder |
| `drawHeightmap` | Per-pixel terrain heightmap |
| `drawTextureset` | Wall-strip splatter |
| `drawMapTileset`, `refreshMapTileset` | Per-cell composited tileset |
| `renderHM7` | Main per-scanline rasterizer (~1000 LOC in the reference) |

## Documents

- [`docs/HMODE7_PORT_DESIGN.md`](docs/HMODE7_PORT_DESIGN.md): technical design with per-function algorithm summary, data-shape diagrams, porting complexity, and per-pixel wall rasterization math.
- [`docs/WALL_LAYER_MODE.md`](docs/WALL_LAYER_MODE.md): the two wall-layer selection algorithms (top-cumulative vs bottom-cumulative), how the port auto-detects which to use, and when to override.
- [`reference/MGC_Hmode7_1_4_4.cpp`](reference/MGC_Hmode7_1_4_4.cpp): MGC's unmodified V1.4.4 source, included verbatim for diffing.

## How to integrate

This port is designed to ship as part of the [mkxp-z-apple-mobile](https://github.com/mateo-m/mkxp-z-apple-mobile) engine used by [Empo](https://github.com/mateo-m/empo-app). The engine consumes it as a git submodule; the Ruby binding registers `HM7::Native` at engine init, and a postload shim auto-detects the DLL era to set `HM7::Native::WALL_LAYER_MODE`.

Adapting to other mkxp-z-based engines requires minor build-system changes (the `cpp` files compile with the engine's binding layer; nothing platform-specific).

## License

Following the original plugin's distribution terms from save-point.org, this port is released under the same permissive-but-credit-required terms as MGC's original. See the [original thread](https://www.save-point.org/thread-3151.html) for MGC's licensing statement.

## Credits

- **H-Mode7 Engine** by [MGC](https://www.save-point.org/thread-3151.html) (MGCaladtogel), V1.4.4, 2011. The underlying algorithm and math are MGC's work; any bugs introduced by this port are ours.
- Heightmap cache by DerVVulfman.
- Original release thread + texture help-file: <https://www.save-point.org/thread-3151.html> ([archived snapshot](https://web.archive.org/web/20260424175813/https://www.save-point.org/thread-3151.html)).
