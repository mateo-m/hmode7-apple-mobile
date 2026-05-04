// hm7_draw_heightmap.cpp — port of the original `drawHeightmap`.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 257-339 (~83 LOC).
//
// Algorithm: for each map pixel (xt, yt), bilinearly sample the
// small `pattern` bitmap to get a per-pixel ground wobble height
// (`hGround`), then look up the corresponding tile in `tilemap_data`
// and accumulate per-layer heights from `map_tileset` (bytes
// [4..4+nbLayers-1] of each 32-byte tile cell). Two heights emerge:
//   `tGround` — all layers summed (solid occluder)
//   `oGround` — only "below-bush" layers (walk-over boundary)
// The sums are written to the heightmap's two planes.
//
// Pixel-layout caveats:
// - The `pattern` bitmap is used as a scalar lookup (only channel
//   byte 0 is read, `patternData[0]`). The original reads `[0]` and
//   `[4]` which is "this pixel's blue + next pixel's blue". On our
//   top-down RGBA surface that's `row[xs].r` and `row[xs+1].r` since
//   the original's BGRA [0] is the blue channel and Windows used
//   that as the height value. **Verify this when we first get a
//   visible result: the plugin ships heightpatterns as grayscale
//   images; we may need to read R instead of B depending on how
//   mkxp-z loaded the bitmap.** Keeping original semantics (red
//   channel) for now since grayscale = R==G==B anyway.
// - The `map_tileset` bitmap has per-tile metadata packed beyond
//   the visible 32x32 BGRA block: bytes [4..4+nbLayers-1] are per-
//   layer heights, byte [4+nbLayers] is the bush-start-index. We
//   read these directly from the mapped surface's pixel buffer.
//
// The `mapTilesetBegin - ((yts << 10) * nbBlocks) + (xts << 2)`
// pointer arithmetic in the original computes a flat offset into
// the bottom-up DIB. Translated to top-down it becomes
// `mapTilesetBegin + (mapTilesetHeight - 1 - yts) * rowSize + xts * 4`
// where `rowSize = mapTilesetWidth * 4`. Since the pack factor is
// `nbBlocks = (nbLayers + 8) >> 2 = 2` for the fixed nb_layers=3
// case, the width is 2 * 32 per tile cell and the shift math in the
// original reduces to straightforward 2D indexing once we drop the
// DIB pointer arithmetic.

#include "hm7_draw_heightmap.h"
#include "hm7_pixels.h"

#include <SDL_surface.h>

namespace hm7 {

int draw_heightmap(std::int16_t *heightmap, int raw_xsize, std::int16_t *tilemap_data, int tilemap_xsize,
                   int tilemap_ysize, SDL_Surface *map_tileset, SDL_Surface *pattern, int nb_layers) {
    if (!heightmap || !tilemap_data || !map_tileset || !pattern)
        return -1;
    if (nb_layers <= 0)
        return -1;

    const int nbBlocks = (nb_layers + 8) >> 2;  // = 2 for nb_layers=3
    const int mapWidthPx = (tilemap_xsize / (nb_layers + 1)) << 5;
    const int mapHeightPx = tilemap_ysize << 5;
    const int patternWidth = pattern->w;
    const int patternHeight = pattern->h;

    // Heightmap layout in memory (Table allocated as
    // `Table.new(mapWidthPx*2, mapHeightPx + mapHeightPx/2)`):
    //
    //   Region A (plane 0 / plane 1 interleaved along X):
    //     rows [0, mapHeightPx), each row is `raw_xsize =
    //     mapWidthPx * 2` int16 wide. Total size:
    //     mapHeightPx * raw_xsize = 2 * mapHeightPx * mapWidthPx
    //     int16 entries. Used by this function (plane 0) and by
    //     apply_lighting (plane 1 / shadow).
    //
    //   Region B ("bush" heights, one plane only):
    //     starts at flat offset `raw_xsize * mapHeightPx =
    //     2 * mapHeightPx * mapWidthPx`, spans `mapWidthPx *
    //     mapHeightPx / 2 * 2 = mapHeightPx * mapWidthPx`
    //     int16 (the remaining half-height rows of the Table).
    //
    // An earlier version computed bush as `heightmap + mapHeightPx
    // * mapWidthPx`, which overlapped the second half of region A
    // and corrupted plane-0 heights and plane-1 shadow deltas for
    // ys in [mapHeightPx/2, mapHeightPx). That showed up as
    // renderHM7 reading huge oShadow values (100s) that added to
    // all 3 channels and saturated lit-ground pixels to white.
    // The correct offset is `raw_xsize * mapHeightPx`, which is
    // `2 * mapHeightPx * mapWidthPx` because raw_xsize packs the
    // two interleaved planes.
    std::int16_t *bush = heightmap + static_cast<long>(raw_xsize) * mapHeightPx;

    for (int yt = 0; yt < mapHeightPx; ++yt) {
        // Ruby builds the pattern by "10 units per pattern pixel"
        // (xc / 10 → integer, xc - 10*xs → fractional 0..9). Keep
        // the same fixed-point so the bilinear weights match bit-
        // for-bit.
        const long yc = static_cast<long>(yt) * patternHeight * 10 / mapHeightPx;
        const int ys = static_cast<int>(yc / 10);
        const int yr = static_cast<int>(yc - 10 * ys);

        std::int16_t *heightRow = heightmap + yt * raw_xsize;
        std::int16_t *bushRow = bush + yt * mapWidthPx;

        for (int xt = 0; xt < mapWidthPx; ++xt) {
            const long xc = static_cast<long>(xt) * patternWidth * 10 / mapWidthPx;
            const int xs = static_cast<int>(xc / 10);
            const int xr = static_cast<int>(xc - 10 * xs);

            // Bilinear sample of the pattern's red/intensity channel.
            // Original reads `patternData1[0]` (B in BGRA) and
            // `patternData1[4]` (B of next pixel). Our Pixel struct
            // stores R/G/B/A; for grayscale patterns R==G==B so
            // either works. Use `.r` as the canonical "intensity".
            const Pixel *row1 = hm7_row_ptr_const(pattern, ys);
            int hGround;
            if (ys != patternHeight - 1 && xs != patternWidth - 1) {
                const Pixel *row2 = hm7_row_ptr_const(pattern, ys + 1);
                hGround = ((10 - xr) * (10 - yr) * row1[xs].r + xr * (10 - yr) * row1[xs + 1].r +
                           (10 - xr) * yr * row2[xs].r + xr * yr * row2[xs + 1].r) /
                          100;
            } else if (ys != patternHeight - 1) {
                const Pixel *row2 = hm7_row_ptr_const(pattern, ys + 1);
                hGround = ((10 - yr) * row1[xs].r + yr * row2[xs].r) / 10;
            } else if (xs != patternWidth - 1) {
                hGround = ((10 - xr) * row1[xs].r + xr * row1[xs + 1].r) / 10;
            } else {
                hGround = row1[xs].r;
            }

            // Tile lookup. tilemap_data is packed as
            //   data[y*xsize + x*(nbLayers+1) + k]
            // with k=0 the "layer count" and k=1..nbLayers the
            // actual layer indices. The original reads only k=0,
            // which is the top-layer tile index used for the
            // tileset lookup.
            const int cellX = xt >> 5;
            const int cellY = yt >> 5;
            const int tileIndex = tilemap_data[cellY * tilemap_xsize + cellX * (nb_layers + 1)];

            // Tile sub-pixel inside the 32x32 cell.
            const int subX = xt - (cellX << 5);
            const int subY = yt - (cellY << 5);

            // Map into the mapTileset bitmap. The packing:
            //   xts = (tile_col << 5 + subX) * nbBlocks  px
            //   yts = tile_row << 5 + subY                px
            // where `tile_col = tileIndex & 7`,
            //       `tile_row = tileIndex >> 3`.
            const int tileCol = tileIndex & 7;
            const int tileRow = tileIndex >> 3;
            const int xts = ((tileCol << 5) + subX) * nbBlocks;
            const int yts = (tileRow << 5) + subY;

            // Read the 4+nbLayers metadata bytes for this pixel.
            // They live at (xts + 4, yts) through (xts + 4 + nbLayers, yts)?
            // Actually in the original: mapTilesetData[4 + l] for l
            // in [0, nbLayers). The byte offset `+4` in DIB BGRA is
            // the next pixel's B (since each pixel is 4 bytes). In
            // our top-down RGBA that's the next pixel's R if we're
            // indexed at .r, or equivalently the byte at offset 4
            // from the current pixel's base.
            //
            // Since we work through the Pixel struct, metadata
            // byte `k` at offset `4*k + c` (c in 0..3) is:
            //   pixel[k].r when c==0
            //   pixel[k].g when c==1
            //   pixel[k].b when c==2
            //   pixel[k].a when c==3
            // The original's byte order was BGRA, so `data[4]` =
            // next pixel's B. In our port that's `pixel[1].b`.
            // **For the metadata path specifically, we need to
            // treat the raw bytes identically to the original,
            // because drawMapTileset writes them as raw bytes.**
            //
            // Safest: get a raw byte pointer to the pixel row
            // instead of going through the Pixel struct.
            const std::uint8_t *tsRow = reinterpret_cast<std::uint8_t *>(
                static_cast<std::uint8_t *>(map_tileset->pixels) + yts * map_tileset->pitch);
            const std::uint8_t *tsData = tsRow + (xts << 2);

            const int bushStart = tsData[4 + nb_layers];
            int oGround = 0;
            for (int l = 0; l < bushStart && l < nb_layers; ++l) {
                oGround += tsData[4 + l];
            }
            int tGround = oGround;
            for (int l = bushStart; l < nb_layers; ++l) {
                tGround += tsData[4 + l];
            }

            heightRow[xt << 1] = static_cast<std::int16_t>(hGround + tGround);
            bushRow[xt] = static_cast<std::int16_t>(hGround + oGround);
        }
    }

    return 0;
}

}  // namespace hm7
