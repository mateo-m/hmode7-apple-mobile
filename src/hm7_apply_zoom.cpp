// hm7_apply_zoom.cpp — port of the original `applyZoom` function.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 1802-1896 (~94 LOC).
//
// Algorithm summary (from the original, translated for top-down RGBA):
//
// For each destination pixel (j, i), compute source coordinates in
// fixed-point 3-bit precision:
//
//   is  = (i * hSrc << 3) / hBmp  ; 'whole' source Y in 1/8 units
//   ise = is >> 3                  ; integer source Y
//   isr = is & 7                   ; fractional Y in 0..7
//   isrc = 8 - isr                 ; complementary weight
//
// Same for j/js/jse/jsr/jsrc. Then blend the 4 source pixels at
// (jse, ise), (jse+1, ise), (jse, ise+1), (jse+1, ise+1) with
// weights (isrc*jsrc, isrc*jsr, isr*jsrc, isr*jsr) summing to 64.
// Alpha-aware: any source pixel with alpha==0 contributes zero
// weight, preventing transparent pixels from darkening opaque
// neighbors. If all four source pixels are transparent, output
// alpha is set to 0.
//
// The port keeps the original fixed-point math exactly. The only
// changes are:
//   1. Row access is top-down (row index matches SDL_Surface row).
//   2. Byte order is RGBA (not BGRA), so we read `px.r/g/b/a` via
//      the Pixel struct; this is byte-for-byte identical since the
//      weighted sum applies uniformly to each channel.

#include "hm7_apply_zoom.h"
#include "hm7_pixels.h"

#include <SDL_surface.h>

namespace hm7 {

int apply_zoom(SDL_Surface *bmp, SDL_Surface *src, int lissage) {
    if (!bmp || !src) return -1;

    const int wBmp = bmp->w;
    const int hBmp = bmp->h;
    const int wSrc = src->w;
    const int hSrc = src->h;

    if (wBmp <= 0 || hBmp <= 0 || wSrc <= 0 || hSrc <= 0) return -1;

    for (int i = 0; i < hBmp; ++i) {
        // `i` is the top-down destination row. The original used
        // bottom-up iteration with `i = hBmp; i-->0;`, which means
        // the loop body executed with `i` taking values
        // `hBmp-1..0`. Fixed-point math uses `i` symmetrically
        // (only scales through `hSrc / hBmp`), so a top-down
        // iteration yields identical source row selections.
        const int is = (i * hSrc << 3) / hBmp;
        const int ise = is >> 3;
        const int isr = is - (ise << 3);
        const int isrc = 8 - isr;

        Pixel *dstRow = hm7_row_ptr(bmp, i);

        for (int j = 0; j < wBmp; ++j) {
            const int js = (j * wSrc << 3) / wBmp;
            const int jse = js >> 3;
            const int jsr = js - (jse << 3);
            const int jsrc = 8 - jsr;

            Pixel &dst = dstRow[j];

            if (!lissage) {
                // Nearest neighbor: copy src[jse, ise] directly.
                // Clamp to valid source indices (i*... / hBmp at
                // i=hBmp-1 gives (hBmp-1)*hSrc << 3 / hBmp ≈
                // (hSrc-1)*8, so `ise` stays within [0, hSrc-1];
                // same for `jse`). Defensive clamp anyway.
                int sy = ise < hSrc ? ise : hSrc - 1;
                int sx = jse < wSrc ? jse : wSrc - 1;
                dst = hm7_row_ptr_const(src, sy)[sx];
                continue;
            }

            // Bilinear: pick up to 4 source pixels and blend.
            const Pixel *srcRow1 = hm7_row_ptr_const(src, ise);
            const Pixel *srcRow2;
            // Next row down (source). Original reads rsSrc below
            // `dcSrc1` in bottom-up memory; in top-down terms that
            // means the next higher row index.
            if (ise + 1 < hSrc) {
                srcRow2 = hm7_row_ptr_const(src, ise + 1);
            } else {
                srcRow2 = srcRow1;
            }

            const Pixel &p1 = srcRow1[jse];
            const Pixel &p2 = (jse + 1 < wSrc) ? srcRow1[jse + 1] : p1;
            const Pixel &p3 = srcRow2[jse];
            const Pixel &p4 = (jse + 1 < wSrc) ? srcRow2[jse + 1] : p3;

            // Weights: keep bytes to mirror the original `char`
            // math. Sum <= 64 (8 * 8), and per-channel values
            // fit in int32 comfortably.
            int c1 = p1.a ? isrc * jsrc : 0;
            int c2 = p2.a ? isrc * jsr  : 0;
            int c3 = p3.a ? isr  * jsrc : 0;
            int c4 = p4.a ? isr  * jsr  : 0;
            int total = c1 + c2 + c3 + c4;

            if (total == 0) {
                // All four source pixels are transparent.
                dst.a = 0;
                continue;
            }

            dst.r = static_cast<std::uint8_t>(
                (c1 * p1.r + c2 * p2.r + c3 * p3.r + c4 * p4.r) >> 6);
            dst.g = static_cast<std::uint8_t>(
                (c1 * p1.g + c2 * p2.g + c3 * p3.g + c4 * p4.g) >> 6);
            dst.b = static_cast<std::uint8_t>(
                (c1 * p1.b + c2 * p2.b + c3 * p3.b + c4 * p4.b) >> 6);
            dst.a = static_cast<std::uint8_t>(
                (c1 * p1.a + c2 * p2.a + c3 * p3.a + c4 * p4.a) >> 6);
        }
    }

    return 0;
}

}  // namespace hm7
