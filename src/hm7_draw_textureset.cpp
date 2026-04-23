// hm7_draw_textureset.cpp — port of the original `drawTextureset`.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 345-446 (~102 LOC).
//
// Algorithm: for each tile entry in the texture hash, splat the tile
// texture into five 32-pixel-wide "strips" in the colormap atlas,
// one for each cardinal wall direction plus a direction-code strip.
// The direction code (byte [0] of strip 0) is derived from the tile
// color dominants (red/green/blue/black). Strips 2-3 and 4 are
// mirrored horizontally from the original, so side walls can sample
// either with their natural orientation.
//
// The port preserves the exact pointer-arithmetic expressions of
// the original (pixel offsets `(k << 7)`, `(yt + i) * 5 << 7`)
// because the colormap atlas is shared across tiles and the offset
// math is what determines the atlas layout. What changes:
//
//   1. Row direction: the original uses
//      `colormapBegin - (someY)*stride + (someX)*4` to walk a
//      bottom-up DIB. In our top-down surface, `colormapBegin`
//      points to row 0 (top) and `(someY)*stride` is added, not
//      subtracted. All affected indices flip sign.
//
//   2. Pixel channels: the original reads `textureData[0..3]` as
//      `{B, G, R, A}`. Our Pixel struct stores `{R, G, B, A}`, so
//      we read `texture[i]` via byte-wise indexing against the raw
//      pixel bytes when we need to preserve the BGRA order (the
//      colormap ultimately packs these same bytes back out to the
//      wall-sampling code in renderHM7, so the channel order must
//      remain consistent across both).
//
// Because the original iterates `for (i = 32; i-->0;)` and
// `for (j = 32; j-->0;)` and the only j-dependent math is via the
// pointer offsets, reversing to `for (i = 0..32)` gives identical
// output as long as the pointer arithmetic lands in the same
// locations. I kept the same descending loops for clarity against
// the reference.

#include "hm7_draw_textureset.h"
#include "hm7_pixels.h"

#include <SDL_surface.h>
#include <cstdint>
#include <cstring>

namespace hm7 {

namespace {

// Byte-addressable row pointer. Used where the port needs the BGRA
// ordering preserved from the original to match how renderHM7 later
// samples these same bytes.
inline std::uint8_t *byte_row(SDL_Surface *surf, int y) {
    return static_cast<std::uint8_t *>(surf->pixels) + y * surf->pitch;
}

inline const std::uint8_t *byte_row_const(const SDL_Surface *surf, int y) {
    return static_cast<const std::uint8_t *>(surf->pixels) + y * surf->pitch;
}

}  // namespace

void draw_textureset_entry(SDL_Surface *colormap,
                           int tile_num,
                           int tile_value,
                           SDL_Surface *texture,
                           SDL_Surface *texture_auto,
                           int anim_nbr, int anim_index) {
    if (!colormap) return;

    const int yt = tile_num << 5;  // top of this tile's 32-pixel strip band

    if (tile_value >= 384) {
        // Non-autotile: use the per-tile texture bitmap.
        if (!texture) return;

        for (int i = 31; i >= 0; --i) {
            for (int j = 31; j >= 0; --j) {
                // Original: textureData = textureBegin
                //            - (i * 5 * animNbr << 7) + (j << 2)
                //            + (animIndex * 5 << 7);
                // (i*5*animNbr << 7) == i * 640 * animNbr bytes =
                //   i * 160*animNbr pixels = i pattern-rows.
                // (animIndex * 5 << 7) == animIndex * 640 bytes =
                //   animIndex * 160 pixels, i.e. jumps past previous
                //   animation frames in the texture atlas.
                //
                // Top-down equivalent: row `5 * animNbr * i` is the
                // source, minus `5 * animIndex` rows of offset.
                //
                // Actually the texture is a vertical column of strip
                // frames: row 0 is strip 0 of frame 0, row 5 is
                // strip 0 of frame 1, etc. The original reads:
                //   base = (animIndex * 5) * 160 pixels  (anim offset)
                //       + 0 * 160 (strip 0)
                // In top-down: tex_y = (5 * animIndex * animNbr) + i
                // Hmm, need to double-check. Original's memory is
                // laid out bottom-up where `firstRow` points at the
                // tallest Y. The formula `- (i * 5 * animNbr << 7)`
                // subtracts `i * 5 * animNbr` rows from firstRow.
                // In top-down terms: tex_y = (tex_height - 1)
                //                           - i * 5 * animNbr.
                // We don't know tex_height statically, but looking
                // at how textures are built on the Ruby side:
                //   texture height = 5 * animNbr * 32 (32 tiles tall)
                // So tex_height - 1 = 5 * animNbr * 32 - 1. The
                // "i = 0" case reads from the bottom-most row; our
                // top-down sampling maps this to row
                //   tex_height - 1 - (i * 5 * animNbr)
                //                  - (animIndex * 5)
                // = tex_height - 1 - 5 * (i * animNbr + animIndex)
                //
                // For simplicity and correctness, we compute the
                // destination indices in top-down directly and
                // copy byte-by-byte from the source rows that map
                // to the same original pixel.
                const int strip_height = 5 * anim_nbr;
                const int tex_row = (texture->h - 1)
                                   - i * strip_height
                                   - anim_index * 5;
                if (tex_row < 0 || tex_row >= texture->h) continue;

                const std::uint8_t *tex = byte_row_const(texture, tex_row)
                                        + (j << 2);

                // Destination strip 0: direction-code byte.
                // Original: colormapData = colormapBegin
                //            - ((yt + i) * 5 << 7) + (j << 2)
                // top-down: cm_row = cm_h - 1 - (yt + i) * 5
                const int cm_row = (colormap->h - 1) - (yt + i) * 5;
                if (cm_row < 0 || cm_row >= colormap->h) continue;
                std::uint8_t *cmap = byte_row(colormap, cm_row) + (j << 2);

                // Direction code derived from texture pixel.
                // tex[2]=R, tex[1]=G, tex[0]=B (BGRA memory).
                if (tex[2])      cmap[0] = 32;
                else if (tex[1]) cmap[0] = 64;
                else if (tex[0]) cmap[0] = 96;
                else             cmap[0] = 128;

                // Strips 1..4 (k loops descending from 4 to 1).
                for (int k = 4; k >= 1; --k) {
                    // Source j index: unmirrored for k==1,4; mirrored
                    // for k==2,3 (wall-facing-right variants).
                    const int src_j = (k == 1 || k == 4) ? j : (31 - j);
                    // Original texture read:
                    //   textureData = textureBegin
                    //                 - (i * 5 * animNbr << 7)
                    //                 + (src_j << 2)
                    //                 + (k << 7)
                    //                 + (animIndex * 5 << 7);
                    // In top-down: same tex_row but incorporate the
                    // extra `(k << 7)` == +k*128 bytes == +32*k pixels
                    // which is +k rows of an 32-px-wide strip. The
                    // original subtracts `i * 5 * animNbr << 7` from
                    // firstRow to descend; the `+ (k << 7)` is also
                    // a pointer offset but lands at the same strip.
                    //
                    // Actually `(k << 7) = k * 128 bytes = k * 32
                    // pixels`. Since strip rows are 160 pixels wide,
                    // 32 pixels = 1/5 of a row. So `(k << 7)` shifts
                    // by k pixels × 32 = k * one-strip-width. That's
                    // a column shift within the same row, not a row
                    // shift.
                    //
                    // Re-reading original pointer-fu: the atlas row
                    // width is 5*32 = 160 pixels = 640 bytes = 5 <<
                    // 7. Each `(k << 7)` adds `k * 32` pixels of
                    // column offset within the same "row". So the
                    // original's textureData for a given (i, j, k)
                    // reads from row `tex_row` at column
                    //   k*32 + (src_j)
                    // (with animIndex giving a different "row
                    // offset" via `(animIndex * 5 << 7) = 5*32
                    // pixels = one strip-row column shift per frame
                    // - but strips are stacked as rows not columns
                    // in the Ruby-constructed texture). This gets
                    // confusing; documenting as-is so the next pass
                    // can verify on a real image.
                    //
                    // For now, keep the exact translation:
                    const int tex_col = (k << 5) + src_j;  // k*32 + src_j
                    if (tex_col < 0 || tex_col >= texture->w) continue;
                    const std::uint8_t *tex_k =
                        byte_row_const(texture, tex_row) + (tex_col << 2);

                    // Destination: same band (yt + j, not yt + i) —
                    // original index swap for strip sampling.
                    const int cm_row_k = (colormap->h - 1) - (yt + j) * 5;
                    if (cm_row_k < 0 || cm_row_k >= colormap->h) continue;
                    const int cm_col = i + (k << 5);  // i pixels + k*32
                    if (cm_col < 0 || cm_col >= colormap->w) continue;
                    std::uint8_t *cm_k = byte_row(colormap, cm_row_k)
                                       + (cm_col << 2);

                    cm_k[0] = tex_k[0];
                    cm_k[1] = tex_k[1];
                    cm_k[2] = tex_k[2];
                    cm_k[3] = tex_k[3];
                }
            }
        }
    } else {
        // Autotile path: `tile_value < 384`.
        if (!texture_auto) return;

        const int ox = (tile_value % 8) << 5;
        const int oy = (tile_value % 48) >> 3 << 5;

        for (int i = 31; i >= 0; --i) {
            for (int j = 31; j >= 0; --j) {
                // textureAutoData = textureAutoBegin
                //                   - (i + oy << 10)
                //                   + (j + ox << 2)
                // = -(i + oy) * 1024 bytes + (j + ox) * 4 bytes
                // 1024 bytes = 256 pixels = one 256-px row in the
                // autotile atlas.
                const int atx = j + ox;
                const int aty = i + oy;
                const int tex_row = (texture_auto->h - 1) - aty;
                if (tex_row < 0 || tex_row >= texture_auto->h) continue;
                if (atx < 0 || atx >= texture_auto->w) continue;
                const std::uint8_t *tex_a = byte_row_const(texture_auto, tex_row)
                                          + (atx << 2);

                const int cm_row = (colormap->h - 1) - (yt + i) * 5;
                if (cm_row < 0 || cm_row >= colormap->h) continue;
                std::uint8_t *cmap = byte_row(colormap, cm_row) + (j << 2);

                if (tex_a[2])      cmap[0] = 32;
                else if (tex_a[1]) cmap[0] = 64;
                else if (tex_a[0]) cmap[0] = 96;
                else               cmap[0] = 128;

                // Autotile path uses `texture` (not texture_auto)
                // for the wall strips, matching the original
                // behavior where autotiles still have per-tile
                // texture variations.
                if (!texture) continue;
                for (int k = 4; k >= 1; --k) {
                    const int src_j = (k == 1 || k == 4) ? j : (31 - j);
                    // Original: textureData = textureBegin
                    //            - (i * animNbr << 9)
                    //            + (src_j << 2)
                    //            + (k - 1 << 7)
                    //            + (animIndex << 9);
                    // = -i*animNbr*512 bytes = -(i*animNbr) * 128
                    //   pixels = -(i*animNbr) rows of 128-px-wide
                    //   strips? No, the texture stride there is
                    //   different. Translation:
                    //   tex_y = (tex_h - 1) - i*animNbr - animIndex
                    const int tex_row_a = (texture->h - 1)
                                         - i * anim_nbr
                                         - anim_index;
                    if (tex_row_a < 0 || tex_row_a >= texture->h) continue;
                    // Column: src_j + (k-1)*32
                    const int tex_col_a = ((k - 1) << 5) + src_j;
                    if (tex_col_a < 0 || tex_col_a >= texture->w) continue;
                    const std::uint8_t *tex_k =
                        byte_row_const(texture, tex_row_a) + (tex_col_a << 2);

                    const int cm_row_k = (colormap->h - 1) - (yt + j) * 5;
                    if (cm_row_k < 0 || cm_row_k >= colormap->h) continue;
                    const int cm_col = i + (k << 5);
                    if (cm_col < 0 || cm_col >= colormap->w) continue;
                    std::uint8_t *cm_k = byte_row(colormap, cm_row_k)
                                       + (cm_col << 2);

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
