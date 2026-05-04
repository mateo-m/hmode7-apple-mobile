// hm7_draw_textureset.cpp - port of the original `drawTextureset`.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 345-446 (~102 LOC).
//
// Algorithm: for each tile entry in the texture hash, splat the tile
// texture into five 32-pixel-wide "strips" in the colormap atlas,
// one for each cardinal wall direction plus a direction-code strip.
// The direction code (byte [0] of strip 0) is derived from the tile
// color dominants (red/green/blue/black). Strips 2-3 and 4 are
// mirrored horizontally from the original so side walls can sample
// either with their natural orientation.
//
// Pointer-arithmetic translation notes
// -------------------------------------
// The original used raw byte offsets off the bitmap's `firstRow`,
// which in RGSSBITMAP convention points at the DISPLAY top-left
// pixel (at memory offset `(biHeight-1)*pitch` for a bottom-up
// DIB). Subtracting pitch moved DOWN in display; adding pitch
// moved UP. Three formulas show up repeatedly:
//
//  (a) `colormapBegin - ((yt+i) * 5 << 7) + (j << 2)`
//      = `colormapBegin - (yt+i)*640 bytes + j*4 bytes`
//      On a 160-pixel-wide atlas (`5 << 7 = 640` = one row),
//      that's display row `(yt+i)`, column `j`.
//
//  (b) `colormapBegin - ((yt+j) * 5 << 7) + (i << 2) + (k << 7)`
//      = display row `(yt+j)`, column `i + k*32` (transposed
//      because strips 1..4 are 90-degree-rotated copies).
//
//  (c) `textureBegin - (i*5*animNbr << 7) + (j << 2) + (animIdx*5 << 7)`
//      = display row `i*animNbr + animIdx`, column `j`. The
//      texture atlas stores `animNbr` frames stacked row-wise
//      beneath each sprite row: row 0 = sprite row 0 frame 0,
//      row 1 = sprite row 0 frame 1, ..., row animNbr = sprite
//      row 1 frame 0, etc.
//
// On top-down SDL, the TRANSLATION IS TRIVIAL: use the same row
// index without any Y flip. `colormapBegin - (yt+i)*640` -> SDL
// row `(yt+i)`. No `(h - 1) - ...` flipping. That was an earlier
// mistake in this port.
//
// Byte order
// ----------
// The texture source is an RGBA8888 SDL surface (R at offset 0).
// The original reads `textureData[0..2]` as B/G/R (in BGRA memory)
// to derive the direction code. Our port preserves the byte
// indices for the fast "which channel is dominant" check - the
// three-way branch looks at raw bytes 0/1/2 so the literal
// color test keys shift under the rename (byte 0 was B, now R;
// byte 1 was G, still G; byte 2 was R, now B). We document the
// new semantics but keep the original 32/64/96/128 direction
// codes since renderHM7 reads those codes as literal magic
// numbers without caring what channel dominated.

#include "hm7_draw_textureset.h"

#include <SDL_surface.h>
#include <cstdint>

namespace hm7 {

namespace {

// Byte-addressable row pointer. Used throughout so byte indices
// map 1:1 with the original plugin's byte offsets.
inline std::uint8_t *byte_row(SDL_Surface *surf, int y) {
    return static_cast<std::uint8_t *>(surf->pixels) + y * surf->pitch;
}

inline const std::uint8_t *byte_row_const(const SDL_Surface *surf, int y) {
    return static_cast<const std::uint8_t *>(surf->pixels) + y * surf->pitch;
}

// Bounds-safe row+column accessors that return nullptr on OOB so
// the copy loops can `if (!ptr) continue;` past bad coords
// (e.g. near the atlas edge, or tiles near 0 that would index
// negative rows).
inline std::uint8_t *dst_at(SDL_Surface *surf, int row, int col_px) {
    if (row < 0 || row >= surf->h)
        return nullptr;
    if (col_px < 0 || col_px >= surf->w)
        return nullptr;
    return byte_row(surf, row) + (col_px << 2);
}

inline const std::uint8_t *src_at(const SDL_Surface *surf, int row, int col_px) {
    if (row < 0 || row >= surf->h)
        return nullptr;
    if (col_px < 0 || col_px >= surf->w)
        return nullptr;
    return byte_row_const(surf, row) + (col_px << 2);
}

}  // namespace

void draw_textureset_entry(SDL_Surface *colormap, int tile_num, int tile_value, SDL_Surface *texture,
                           SDL_Surface *texture_auto, int anim_nbr, int anim_index) {
    if (!colormap)
        return;
    // `yt` is the top-down destination row base for this tile's
    // 32-row atlas band. Tiles are stacked row-by-row, no multiplier.
    const int yt = tile_num << 5;

    // The plugin supports `anim_nbr == 0` by accident (the formula
    // `i * 5 * animNbr` degenerates to 0, so every sprite row reads
    // from display row 0). Our port treats missing/zero as "single
    // frame" which the Ruby side also passes as `1`. Clamp to at
    // least 1 so we don't divide into row 0 forever.
    if (anim_nbr <= 0)
        anim_nbr = 1;
    if (anim_index < 0)
        anim_index = 0;

    if (tile_value >= 384) {
        // ------------------------------------------------------
        //  Regular-texture path (tileValue >= 384).
        // ------------------------------------------------------
        if (!texture)
            return;

        for (int i = 31; i >= 0; --i) {
            // Texture source row for sprite row `i` at the current
            // animation frame. See translation note (c) above.
            const int tex_row = i * anim_nbr + anim_index;

            for (int j = 31; j >= 0; --j) {
                const std::uint8_t *tex = src_at(texture, tex_row, j);
                if (!tex)
                    continue;

                // Direction-code strip: the atlas byte at
                // (colormap row = yt+i, col = j) byte 0 carries a
                // small magic code renderHM7 later reads to decide
                // which wall variant to sample.
                //
                // Original assumed BGRA memory: tex[2]=R, tex[1]=G,
                // tex[0]=B. Our RGBA memory: tex[0]=R, tex[1]=G,
                // tex[2]=B. Either way the dominant-channel test
                // produces one of four distinct byte codes, and
                // renderHM7 only compares the result against 32/96
                // (not against a specific channel), so keep the
                // four-way branch but label channels with the
                // ACTUAL RGBA indices to avoid confusing future
                // readers.
                std::uint8_t *cmap = dst_at(colormap, yt + i, j);
                if (cmap) {
                    if (tex[0])
                        cmap[0] = 32;  // R dominant
                    else if (tex[1])
                        cmap[0] = 64;  // G dominant
                    else if (tex[2])
                        cmap[0] = 96;  // B dominant
                    else
                        cmap[0] = 128;  // black
                }

                // Strips 1..4 at `(yt+j)` rows (transposed), cols
                // `i + k*32`. The source column is mirrored for
                // k==2 and k==3 (the two side-wall strips need to
                // face opposite directions).
                for (int k = 4; k >= 1; --k) {
                    const int src_j = (k == 1 || k == 4) ? j : (31 - j);
                    const std::uint8_t *tex_k = src_at(texture, tex_row, src_j + (k << 5));
                    if (!tex_k)
                        continue;

                    std::uint8_t *cm_k = dst_at(colormap, yt + j, i + (k << 5));
                    if (!cm_k)
                        continue;

                    cm_k[0] = tex_k[0];
                    cm_k[1] = tex_k[1];
                    cm_k[2] = tex_k[2];
                    cm_k[3] = tex_k[3];
                }
            }
        }
    } else {
        // ------------------------------------------------------
        //  Autotile path (tileValue < 384).
        //
        //  Source is the shared autotile textureset (`texture_auto`,
        //  256-px wide by convention - the hardcoded `<< 10`
        //  pitch shift in the original only works for that width).
        //  Direction-code is derived from `texture_auto` at the
        //  tile's sub-rect `(ox, oy)`; strips 1..4 pull from the
        //  per-tile animated texture bitmap (128-px wide, stored
        //  at `texture` with frames stacked row-wise, so row
        //  stride uses `<< 9` in the original).
        // ------------------------------------------------------
        if (!texture_auto)
            return;

        const int ox = (tile_value & 7) << 5;
        const int oy = ((tile_value % 48) >> 3) << 5;

        for (int i = 31; i >= 0; --i) {
            for (int j = 31; j >= 0; --j) {
                // Direction-code source pulls from `texture_auto`
                // at (aty=oy+i, atx=ox+j).
                const std::uint8_t *tex_auto = src_at(texture_auto, oy + i, ox + j);
                if (!tex_auto)
                    continue;

                std::uint8_t *cmap = dst_at(colormap, yt + i, j);
                if (cmap) {
                    if (tex_auto[0])
                        cmap[0] = 32;
                    else if (tex_auto[1])
                        cmap[0] = 64;
                    else if (tex_auto[2])
                        cmap[0] = 96;
                    else
                        cmap[0] = 128;
                }

                // Strips 1..4 source from `texture` at row
                // `i*animNbr + animIndex` (see translation note
                // (c) above, with 128-px atlas => row stride
                // `<< 9` in the original instead of `<< 7`, but
                // the logical row index is the same `i*animNbr +
                // animIndex` regardless of atlas width).
                if (!texture)
                    continue;
                const int tex_row = i * anim_nbr + anim_index;

                for (int k = 4; k >= 1; --k) {
                    const int src_j = (k == 1 || k == 4) ? j : (31 - j);
                    // Original uses `(k - 1 << 7)` here, NOT `(k
                    // << 7)` as in the regular-texture path. So the
                    // column offset shifts by `(k-1)*32` pixels,
                    // not `k*32`. Reproduce faithfully.
                    const std::uint8_t *tex_k = src_at(texture, tex_row, src_j + ((k - 1) << 5));
                    if (!tex_k)
                        continue;

                    std::uint8_t *cm_k = dst_at(colormap, yt + j, i + (k << 5));
                    if (!cm_k)
                        continue;

                    cm_k[0] = tex_k[0];
                    cm_k[1] = tex_k[1];
                    cm_k[2] = tex_k[2];
                    cm_k[3] = tex_k[3];
                }
            }
        }
    }
}

}  // namespace hm7
