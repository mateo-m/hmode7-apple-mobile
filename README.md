# hmode7-apple-mobile

Port of MGC's **H-Mode7** RPG Maker XP plugin to mkxp-z on Apple mobile
platforms (iOS, iPadOS, tvOS).

H-Mode7 is a pseudo-3D rotating-perspective renderer — it adds slanted
top-down maps with zoom, rotation, and heightmap-based terrain elevation
to RPG Maker XP. Games using it include Pokemon Insurgence (the flying
intro over the world map, several other cutscenes) and a few other
fangames on save-point.org.

The original plugin is distributed as a Windows-only native DLL
(`MGC_Hmode7.dll`), which means no game using H-Mode7 renders correctly
on iOS, Android, macOS, or any non-Windows port. This repository is a
clean-room re-port targeting Ruby 3.1 / mkxp-z's Bitmap & Table APIs,
so the DLL's pixel-math runs as a module linked into the engine
directly instead of via `Win32API.new(...)`.

## Status

- Design document: [`docs/HMODE7_PORT_DESIGN.md`](docs/HMODE7_PORT_DESIGN.md)
- Original reference source (unmodified, for diffing): [`reference/MGC_Hmode7_1_4_4.cpp`](reference/MGC_Hmode7_1_4_4.cpp)
- Port implementation: _in progress_

## Credits & upstream

Original H-Mode7 Engine by MGC (MGCaladtogel), v1.4.4, 2011. Heightmap
cache by DerVVulman. Distributed via the RPG Maker XP community at
save-point.org. Original thread (with all releases and discussion):

https://www.save-point.org/thread-3151.html

The reference `MGC_Hmode7_1_4_4.cpp` source file in this repository is
MGC's own release; we are not the authors. This fork exists to make
H-Mode7 portable to non-Windows targets. Any bugs introduced by the
port are ours; the underlying algorithm and math are MGC's work.

## License

Following the original plugin's distribution terms from save-point.org,
this fork is released under the same permissive-but-credit-required
terms. See the original thread for MGC's licensing statement.

## Integration target

This port is designed to ship as part of the
[mkxp-z-apple-mobile](https://github.com/mateo-m/mkxp-z-apple-mobile)
engine used by the Empo iOS RPG Maker player. It can be adapted to
other mkxp-z-based engines with minor build-system changes.
