# hmode7-apple-mobile

> Native port of MGC's H-Mode7 plugin to mkxp-z.

[![License](https://img.shields.io/badge/license-permissive-blue.svg)](#license)
[![Status](https://img.shields.io/badge/status-complete-brightgreen.svg)](#status)

H-Mode7 adds a pseudo-3D rotating-perspective renderer to RPG Maker XP. Slanted top-down maps with zoom, rotation, and heightmap-based terrain elevation. Some RPG Maker XP fan games use it for cutscenes and overworld effects.

The original ships as a Windows DLL (`MGC_Hmode7.dll`), so games using it don't render on iOS, Android, macOS, or anything else. This repo replaces the DLL with a C++ module compiled against mkxp-z's `Bitmap` and `Table` APIs and linked directly into the engine. Games keep their original Ruby scripts; calls to `HM7.render_hm7(...)` reach the native module through mkxp-z's binding layer instead of `Win32API.new(...)`.

## Highlights

- **Drop-in replacement.** No script changes. `HM7.*` calls go to the native module.
- **Multi-Ruby.** The same source compiles against Ruby 1.8, 1.9, 3.0, and 3.1 in mkxp-z-apple-mobile's per-version pipeline. RGSS1 games on Ruby 1.8 and modern forks on Ruby 3.1 each get the matching merged `.o`.
- **Two DLL eras.** Auto-detects pre-V1.3 (8 exports) vs V1.4+ (9 exports) and switches the wall-layer-selection algorithm accordingly. See [`docs/WALL_LAYER_MODE.md`](docs/WALL_LAYER_MODE.md).
- **Reference parity.** Output matches the Windows reference for wall textures, Z-ordering, billboard sprites, horizon fading, heightmap relief, autotile animation, and multi-layer tile facades.
- **CPU-side.** Per-pixel, per-scanline software rasterizer. Reads and writes mkxp-z `Bitmap` buffers, then syncs to the GL texture once per frame.

## Status

Complete. Output verified against the public V1.4.4 source for all nine exports across both DLL eras:

| Export | Role |
|---|---|
| `applyOpacity`, `applyZoom`, `applyLighting` | Per-bitmap pixel transforms |
| `computeM7` | Mode-7 projection LUT builder |
| `drawHeightmap` | Per-pixel terrain heightmap |
| `drawTextureset` | Wall-strip splatter |
| `drawMapTileset`, `refreshMapTileset` | Per-cell composited tileset |
| `renderHM7` | Main per-scanline rasterizer (~1000 LOC in the reference) |

## Documents

- [`docs/HMODE7_PORT_DESIGN.md`](docs/HMODE7_PORT_DESIGN.md): per-function algorithm notes, data shapes, porting complexity, wall rasterization math.
- [`docs/WALL_LAYER_MODE.md`](docs/WALL_LAYER_MODE.md): the two wall-layer-selection algorithms, auto-detection, override knobs.
- [`reference/MGC_Hmode7_1_4_4.cpp`](reference/MGC_Hmode7_1_4_4.cpp): MGC's V1.4.4 source, included verbatim for diffing.

## Integration

This port ships as part of [mkxp-z-apple-mobile](https://github.com/mateo-m/mkxp-z-apple-mobile), the engine behind [Empo](https://github.com/mateo-m/empo-app). The engine consumes it as a git submodule. At engine init the Ruby binding registers `HM7::Native`, and a postload shim sets `HM7::Native::WALL_LAYER_MODE` based on the DLL era it detects.

Adapting to another mkxp-z-based engine takes a small build-system change. The `.cpp` files compile against the engine's binding layer; no platform-specific code.

## Contributing

Issues and PRs welcome on [GitHub](https://github.com/mateo-m/hmode7-apple-mobile/issues).

## License

Distributed under the same terms as MGC's original from save-point.org. See the [original thread](https://www.save-point.org/thread-3151.html) for the licensing statement.

## Credits

- **H-Mode7 Engine** by [MGC](https://www.save-point.org/thread-3151.html) (MGCaladtogel), V1.4.4, 2011.
- Heightmap cache by DerVVulfman.
- Original release thread + texture help-file: <https://www.save-point.org/thread-3151.html> ([archived snapshot](https://web.archive.org/web/20260424175813/https://www.save-point.org/thread-3151.html)).
