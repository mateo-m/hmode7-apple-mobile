// hm7_draw_map_tileset.cpp — ports drawMapTileset + refreshMapTileset.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 557-755 (~200 LOC total).
//
// For each entry in the tilemap_hash, walk the per-layer tile values
// and copy 32x32 pixel blocks from the tileset or autotile bitmap
// into the compact map_tileset atlas. drawMapTileset additionally
// writes per-layer height bytes + bush byte; refreshMapTileset
// leaves those intact and only updates color bytes (used when
// autotile animation advances a frame).
//
// The atlas layout for a fixed nb_layers=3 (so nbBlocks=2):
//   - The tile at (tile_col, tile_row) occupies a 64x32 pixel block
//     starting at (tile_col * 64, tile_row * 32) in the atlas.
//   - Within each 64x32 block, the first 32 columns hold pixel BGRA
//     color; columns 32+ hold per-pixel metadata packed into the
//     BGRA channels of "neighbor" pixels.
//   - At offset (xt + j) * nbBlocks << 2 bytes into a row, byte
//     offsets [0..3] are the pixel's BGRA (color), byte offsets
//     [4..4+nbLayers-1] are per-layer heights, byte
//     [4+nbLayers] is bush-start-layer index.
//
// All pointer arithmetic from the original translates to straight
// byte indexing against top-down SDL surface rows.

#include "hm7_draw_map_tileset.h"

#include <SDL_surface.h>
#include <cstdint>
#include <cstring>

namespace hm7 {

namespace {

inline std::uint8_t *byte_row(SDL_Surface *surf, int y) {
    return static_cast<std::uint8_t *>(surf->pixels) + y * surf->pitch;
}
inline const std::uint8_t *byte_row_const(const SDL_Surface *surf, int y) {
    return static_cast<const std::uint8_t *>(surf->pixels) + y * surf->pitch;
}

// Compute the destination byte pointer in map_tileset for tile
// sub-pixel (i, j) at tile position (xt, yt), nbBlocks horizontal.
// Translates the original bottom-up DIB formula
//   mapTilesetBegin - ((yt+i << 10) * nbBlocks) + ((xt+j) * nbBlocks << 2)
// to a top-down SDL surface byte pointer.
inline std::uint8_t *map_tileset_ptr(SDL_Surface *atlas,
                                     int xt, int yt, int i, int j,
                                     int nbBlocks) {
    // yt+i rows, atlas is top-down so row index is (yt+i).
    // x byte offset: (xt+j) * nbBlocks * 4
    const int row_y = yt + i;
    const int x_off = (xt + j) * nbBlocks * 4;
    if (row_y < 0 || row_y >= atlas->h) return nullptr;
    if (x_off < 0 || x_off >= atlas->pitch) return nullptr;
    return byte_row(atlas, row_y) + x_off;
}

// Source byte pointer into tileset/autotile/heightset bitmaps.
// Translates (ys+i << 10) + (xs+j << 2) DIB offset.
inline const std::uint8_t *source_ptr(const SDL_Surface *src,
                                      int xs, int ys, int i, int j) {
    const int row_y = ys + i;
    const int x_off = (xs + j) * 4;
    if (row_y < 0 || row_y >= src->h) return nullptr;
    if (x_off < 0 || x_off >= src->pitch) return nullptr;
    return byte_row_const(src, row_y) + x_off;
}

}  // namespace

void draw_map_tileset_entry(SDL_Surface *map_tileset,
                            SDL_Surface *tileset,
                            SDL_Surface *heightset,
                            const TileEntry &e,
                            SDL_Surface **auto_tilesets, int auto_tilesets_len,
                            SDL_Surface **auto_heightsets, int auto_heightsets_len,
                            int nb_layers) {
    if (!map_tileset || !tileset || !heightset) return;

    const int nbBlocks = (nb_layers + 8) >> 2;
    const int tileNum = e.tile_num;

    // xt/yt: top-left of this tile cell in the atlas.
    const int xt = (tileNum & 7) << 5;
    const int yt = (tileNum >> 3) << 5;

    const int bush = e.layer_tile_values[nb_layers];  // bush-start layer

    // Iterate layers top-down (descending from nb_layers-1 to 0) so
    // the topmost layer lands first; later layers only write when
    // the pixel is still transparent (alpha == 0).
    for (int layer = nb_layers - 1; layer >= 0; --layer) {
        int tileValue = e.layer_tile_values[layer];

        if (tileValue >= 384) {
            // Non-autotile: copy from tileset/heightset.
            tileValue -= 384;
            const int xs = (tileValue & 7) << 5;
            const int ys = (tileValue >> 3) << 5;

            for (int i = 31; i >= 0; --i) {
                for (int j = 31; j >= 0; --j) {
                    std::uint8_t *dst = map_tileset_ptr(map_tileset, xt, yt, i, j, nbBlocks);
                    if (!dst) continue;
                    const std::uint8_t *hset = source_ptr(heightset, xs, ys, i, j);
                    if (hset) {
                        dst[4 + layer] = hset[0];
                    }
                    dst[4 + nb_layers] = static_cast<std::uint8_t>(bush);

                    // Only composite color into still-transparent pixels.
                    if (!dst[3]) {
                        const std::uint8_t *src = source_ptr(tileset, xs, ys, i, j);
                        if (src && src[3]) {
                            dst[0] = src[0];
                            dst[1] = src[1];
                            dst[2] = src[2];
                            dst[3] = 255;
                        }
                    }
                }
            }
        } else {
            // Autotile path.
            const int numAutoTileset = tileValue / 48 - 1;
            if (numAutoTileset < 0) continue;  // tileValue < 48 = invalid
            if (numAutoTileset >= auto_tilesets_len) continue;
            if (numAutoTileset >= auto_heightsets_len) continue;

            SDL_Surface *autoTs = auto_tilesets[numAutoTileset];
            SDL_Surface *autoHs = auto_heightsets[numAutoTileset];
            if (!autoTs || !autoHs) continue;

            const int localTileValue = tileValue % 48;
            const int xs = (localTileValue & 7) << 5;
            const int ys = (localTileValue >> 3) << 5;

            for (int i = 31; i >= 0; --i) {
                for (int j = 31; j >= 0; --j) {
                    std::uint8_t *dst = map_tileset_ptr(map_tileset, xt, yt, i, j, nbBlocks);
                    if (!dst) continue;

                    const std::uint8_t *ahs = source_ptr(autoHs, xs, ys, i, j);
                    if (ahs) dst[4 + layer] = ahs[0];
                    dst[4 + nb_layers] = static_cast<std::uint8_t>(bush);

                    if (!dst[3]) {
                        const std::uint8_t *src = source_ptr(autoTs, xs, ys, i, j);
                        if (src && src[3]) {
                            dst[0] = src[0];
                            dst[1] = src[1];
                            dst[2] = src[2];
                            dst[3] = 255;
                        }
                    }
                }
            }
        }
    }
}

void refresh_map_tileset_entry(SDL_Surface *map_tileset,
                               SDL_Surface *tileset,
                               const TileEntry &e,
                               SDL_Surface **auto_tilesets, int auto_tilesets_len,
                               int nb_layers) {
    if (!map_tileset || !tileset) return;

    const int nbBlocks = (nb_layers + 8) >> 2;
    const int tileNum = e.tile_num;
    const int xt = (tileNum & 7) << 5;
    const int yt = (tileNum >> 3) << 5;

    // Refresh walks layers ascending (0..nb_layers-1) in the
    // original. The alpha check is intentionally absent (unlike
    // drawMapTileset), so each layer overwrites previous color
    // if the current layer has an opaque pixel.
    for (int layer = 0; layer < nb_layers; ++layer) {
        int tileValue = e.layer_tile_values[layer];

        if (tileValue >= 384) {
            tileValue -= 384;
            const int xs = (tileValue & 7) << 5;
            const int ys = (tileValue >> 3) << 5;

            for (int i = 31; i >= 0; --i) {
                for (int j = 31; j >= 0; --j) {
                    std::uint8_t *dst = map_tileset_ptr(map_tileset, xt, yt, i, j, nbBlocks);
                    if (!dst) continue;
                    const std::uint8_t *src = source_ptr(tileset, xs, ys, i, j);
                    if (src && src[3]) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = src[3];
                    }
                }
            }
        } else {
            const int numAutoTileset = tileValue / 48 - 1;
            if (numAutoTileset < 0 || numAutoTileset >= auto_tilesets_len) continue;

            SDL_Surface *autoTs = auto_tilesets[numAutoTileset];
            if (!autoTs) continue;

            const int localTileValue = tileValue % 48;
            const int xs = (localTileValue & 7) << 5;
            const int ys = (localTileValue >> 3) << 5;

            for (int i = 31; i >= 0; --i) {
                for (int j = 31; j >= 0; --j) {
                    std::uint8_t *dst = map_tileset_ptr(map_tileset, xt, yt, i, j, nbBlocks);
                    if (!dst) continue;
                    const std::uint8_t *src = source_ptr(autoTs, xs, ys, i, j);
                    if (src && src[3]) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = src[3];
                    }
                }
            }
        }
    }
}

}  // namespace hm7
