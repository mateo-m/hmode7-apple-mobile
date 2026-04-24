# hmode7-apple-mobile

Port of MGC's **H-Mode7** RPG Maker XP plugin to mkxp-z on Apple
mobile platforms (iOS, iPadOS, tvOS).

H-Mode7 is a pseudo-3D rotating-perspective renderer — it adds
slanted top-down maps with zoom, rotation, and heightmap-based terrain
elevation to RPG Maker XP. Games using it include Pokemon Insurgence
(the flying Pelipper intro over the Torren region, several other
cutscenes) and other fan games shipped via save-point.org.

The original plugin is distributed as a Windows-only native DLL
(`MGC_Hmode7.dll`), which means no game using H-Mode7 renders correctly
on iOS, Android, macOS, or any non-Windows port. This repository is a
clean-room re-port targeting Ruby 3.1 / mkxp-z's `Bitmap` and `Table`
APIs, so the plugin's pixel-math runs as a module linked directly into
the engine instead of via `Win32API.new(...)`.

## Scope

All nine H-Mode7 exports are covered, for both the pre-V1.3 (8-export)
and V1.4+ (9-export) DLL eras:

- `applyOpacity`, `applyZoom`, `applyLighting`
- `computeM7`, `drawHeightmap`, `drawTextureset`
- `drawMapTileset`, `refreshMapTileset`
- `renderHM7` (the main rasterizer, ~1000 lines in the reference)

The port is in a single namespace `hm7::` and a companion Ruby binding
module `HM7::Native` that the mkxp-z engine loads so scripts calling
`HM7.render_hm7(...)` (etc.) dispatch natively.

## Status

**Complete and verified.** The full Pokemon Insurgence intro cinematic
renders matching the Windows reference, including wall textures,
Z-ordering, billboard sprites, horizon fading, heightmap relief,
autotile animation, and the multi-layer tile-facade rendering that
makes buildings look correct.

Auto-detection of the HM7 version era is built into the postload shim,
so a game shipping a pre-V1.3 DLL (like Insurgence) and a game
shipping a V1.4+ DLL both render correctly without manual
configuration. See
[`docs/WALL_LAYER_MODE.md`](docs/WALL_LAYER_MODE.md) for the
algorithm difference.

## Documents

- [`docs/HMODE7_PORT_DESIGN.md`](docs/HMODE7_PORT_DESIGN.md) —
  technical design: algorithm summary per exported function, data-
  shape diagrams, per-function porting complexity, per-pixel wall
  rasterization math.
- [`docs/WALL_LAYER_MODE.md`](docs/WALL_LAYER_MODE.md) — explains the
  two wall-layer selection algorithms (top-cumulative vs
  bottom-cumulative), how the port auto-detects which to use, and
  when to override it.
- [`reference/MGC_Hmode7_1_4_4.cpp`](reference/MGC_Hmode7_1_4_4.cpp)
  — MGC's unmodified V1.4.4 source for diffing.

## Credits & upstream

Original **H-Mode7 Engine** by MGC (MGCaladtogel), V1.4.4, 2011.
Heightmap cache by DerVVulfman. Distributed via the RPG Maker XP
community at save-point.org.

Original thread (releases, discussion, help-file for textures):

- <https://www.save-point.org/thread-3151.html>
- Archived snapshot:
  <https://web.archive.org/web/20260424175813/https://www.save-point.org/thread-3151.html>

The reference `MGC_Hmode7_1_4_4.cpp` source file in this repository
is MGC's own release, included verbatim for diffing. This fork exists
to make H-Mode7 portable to non-Windows targets. Any bugs introduced
by the port are ours; the underlying algorithm and math are MGC's
work.

## License

Following the original plugin's distribution terms from save-point.org,
this fork is released under the same permissive-but-credit-required
terms. See the original thread for MGC's licensing statement.

## Integration target

This port is designed to ship as part of the
[mkxp-z-apple-mobile](https://github.com/mateo-m/mkxp-z-apple-mobile)
engine used by the Empo iOS RPG Maker player. It can be adapted to
other mkxp-z-based engines with minor build-system changes.
