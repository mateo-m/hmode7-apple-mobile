// hm7_draw_heightmap.h — public surface for the drawHeightmap port.

#ifndef HM7_DRAW_HEIGHTMAP_H
#define HM7_DRAW_HEIGHTMAP_H

#include <cstdint>

struct SDL_Surface;

namespace hm7 {

// Build the per-pixel heightmap for an entire map.
//
// For each pixel (xt, yt) in the map area, derives a "ground height"
// by bilinearly sampling the small `pattern` bitmap, then adds per-
// tile layer heights from the precomputed `map_tileset`. The result
// goes into two planes of `heightmap`:
//   plane 0 — full solid height (ground + all layers); used to
//             render walls in renderHM7.
//   plane 1 — bush/standable height (ground + occluder layers
//             only, excluding bush layers above); used for
//             walking-over-objects math.
//
// `heightmap`: int16 backing of the heightmap Table. Size:
//   raw_xsize = (2 * map_width_px), ysize = map_height_px (rows
//   cover only the "ground" region; the "bush" region starts
//   `mapHeight * mapWidth` entries past the ground plane).
// `tilemap_data`: int16 backing of the tilemap Table, size
//   `(map_width / nbLayers+1) × map_height × 1`. Each row packs
//   `(count, layer1, layer2, layer3)` per tile cell.
// `map_tileset`: the compact tileset bitmap built by drawMapTileset.
// `pattern`: the small heightpattern bitmap (typically 32x32).
// `tilemap_xsize`, `tilemap_ysize`: tilemap Table dimensions.
// `raw_xsize`: heightmap stored xsize (2 × map_width_px).
// `nb_layers`: hardcoded to 3 per design doc §10.1.
//
// Original: MGC_Hmode7_1_4_4.cpp lines 257-339, ~83 LOC.
int draw_heightmap(std::int16_t *heightmap, int raw_xsize, std::int16_t *tilemap_data, int tilemap_xsize,
                   int tilemap_ysize, SDL_Surface *map_tileset, SDL_Surface *pattern, int nb_layers);

}  // namespace hm7

#endif  // HM7_DRAW_HEIGHTMAP_H
