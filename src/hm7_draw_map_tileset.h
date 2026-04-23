// hm7_draw_map_tileset.h — public surface for drawMapTileset + refreshMapTileset.
//
// Both functions walk the `tilemap_hash` Ruby Hash (keys = tile nums,
// values = Arrays of layer tile indices). Per entry, they copy 32x32
// BGRA blocks from the source `tileset` (or autotile bitmap) into
// the compact `map_tileset` atlas, compositing up to `nb_layers`
// layers top-down. `drawMapTileset` also populates per-layer height
// metadata bytes [4..4+nbLayers-1] and the "bush layer" byte [4+
// nbLayers]; `refreshMapTileset` only updates color bytes [0..3],
// leaving heights intact (used for animated-autotile frame updates).
//
// Because both iterate over a Ruby Hash, the port provides a single
// per-entry "draw one tile" function that the Ruby-side iteration
// calls with each hash key/value pair.

#ifndef HM7_DRAW_MAP_TILESET_H
#define HM7_DRAW_MAP_TILESET_H

#include <cstdint>

struct SDL_Surface;

namespace hm7 {

// Entry for draw_map_tileset_entry / refresh_map_tileset_entry.
// `tile_nums` corresponds to one entry of `tilemap_hash`: tile
// number as key, Array of `(layer0_tile, layer1_tile, layer2_tile,
// bush_start_layer)` as value.
struct TileEntry {
    int tile_num;
    int layer_tile_values[4];  // [0..nb_layers-1] are layer tiles,
                               // [nb_layers] is bush-start-layer index.
};

// Full draw path (drawMapTileset): populate color + per-layer height
// + bush. Used once per map load.
//
// `auto_tilesets[0..6]` — autotile bitmap array (indexed by
//                          (tile_value < 384 ? tile_value/48 : n/a)).
// `auto_heightsets[0..6]` — matching autotile heightset bitmaps
//                          (filled only by drawMapTileset; unused
//                          in refresh).
void draw_map_tileset_entry(SDL_Surface *map_tileset,
                            SDL_Surface *tileset,
                            SDL_Surface *heightset,
                            const TileEntry &e,
                            SDL_Surface **auto_tilesets, int auto_tilesets_len,
                            SDL_Surface **auto_heightsets, int auto_heightsets_len,
                            int nb_layers);

// Refresh path: only rewrite colors for animated-autotile frames.
// Heights / bush byte are preserved from the prior drawMapTileset
// pass. Much faster per-entry than the full draw path.
void refresh_map_tileset_entry(SDL_Surface *map_tileset,
                               SDL_Surface *tileset,
                               const TileEntry &e,
                               SDL_Surface **auto_tilesets, int auto_tilesets_len,
                               int nb_layers);

}  // namespace hm7

#endif  // HM7_DRAW_MAP_TILESET_H
