// hm7_compute_m7.cpp — port of the original `computeM7` function.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 133-251 (~120 LOC).
//
// Algorithm: builds the mode-7 projection LUT. For each screen row
// (yt) and screen column (xt) in the visible range, computes the
// corresponding world-space map coordinates (xr, yr) via a two-step
// projection:
//
//   yp = pivot + altitude * (yt - pivot) / ((altitude - distProj)
//                 * cosAngle + (yt - pivot) * sinAngle) * zoom
//   xp = zoom * ((a - xt) << 18) / (slope * yt + correction)
//   xs = a +/- xp
//   yr = yc + ((xs - xc) * sinTheta + (ys - yc) * cosTheta) / 4096
//   xr = xc + ((xs - xc) * cosTheta - (ys - yc) * sinTheta) / 4096
//
// All math is integer fixed-point; exponents are Q12 zoom / Q17
// slope / Q11 trig.
//
// The output is a 3D int16 Table laid out as two planes:
//   data_table[yt*xsize + xt]          = xr  (plane 0)
//   data_table[xsize*ysize + yt*xsize + xt] = yr  (plane 1)
//
// Horizontal symmetry: the right half of each row is mirrored from
// the left half (no re-projection needed since xp is symmetric in
// the rotation formula).
//
// Lightline scratch layout (see header doc):
//   row 2 — per-row lighting attenuation, BGRA-style
//   row 1 — per-column relief coefficient + horizontal zoom scratch
//   row 0 — (untouched here, reserved for renderHM7)

#include "hm7_compute_m7.h"
#include "hm7_pixels.h"

#include <SDL_surface.h>

namespace hm7 {

namespace {
// Windows divide-by-zero behavior guard, matches `DIVISE` macro in
// the original. Returns `a / b` unless `b == 0` in which case 0.
inline int divise(int a, int b) {
    return b != 0 ? a / b : 0;
}
}  // namespace

int compute_m7(std::int16_t *data_table,
               int xsize, int ysize,
               SDL_Surface *lightline,
               const ComputeM7Params &params) {
    if (!data_table || !lightline) return -1;

    const int a = xsize >> 1;
    const int xc = a;
    const int yc = params.pivot;
    const int val_4 = (params.altitude - params.distProj) * params.cosAngle;
    const long oz = static_cast<long>(xsize) * ysize;

    // Lightline layout reminder: the Ruby side allocates
    // `@rowsdata = Bitmap.new(W, 3)` and writes the fade color via
    // `@rowsdata.set_pixel(0, 0, fade)` - that's the pixel at top-
    // down row 0, column 0. On the Windows plugin (bottom-up DIB)
    // this pixel corresponds to `firstRow[0..3]`. In our top-down
    // SDL representation it's row 0, column 0 - no row flip needed.
    //
    // Row 0 (top-down row 0): per-row lighting output (+ col 0 fade
    //                         seed at the start of this call).
    // Row 1 (top-down row 1): per-column relief + horizontal zoom
    //                         scratch.
    // Row 2 (top-down row 2): renderHM7 `ym` tracking scratch.
    //
    // All byte-index semantics are POSITIONAL: byte 0, 1, 2, 3 are
    // just "first, second, third, fourth" of the 4-byte record, NOT
    // "blue, green, red, alpha" as the original BGRA source reads.
    // We intentionally use raw byte indices instead of the Pixel
    // struct fields so write/read locations line up with renderHM7
    // regardless of surface byte order.
    std::uint8_t *lightBytes = static_cast<std::uint8_t *>(lightline->pixels);
    const int lightPitch = lightline->pitch;

    std::uint8_t *lightingRowBytes = lightBytes + 0 * lightPitch;  // row 0
    std::uint8_t *reliefRowBytes   = lightBytes + 1 * lightPitch;  // row 1

    // Seed lux values from lightline[row=0, col=0] = fade color. The
    // per-byte semantics: byte 0/1/2 are the color values
    // the original called B/G/R (the channel order is irrelevant for
    // the later per-screen-row attenuation math - what matters is
    // that whatever we read here gets stored at the same byte index
    // later and read by renderHM7 at the same byte index).
    const std::uint8_t *fade = lightingRowBytes + 0;
    int lux_0 = fade[0];
    int lux_1 = fade[1];
    int lux_2 = fade[2];
    int lux_3 = fade[3];

    int yMax = params.yMax;
    if (params.lessCut) {
        yMax = (yMax << 1) - params.yMin;
    }

    int y0 = params.heightLimit;
    if (y0 < params.yMin) y0 = params.yMin;

    for (int yt = y0; yt < yMax; ++yt) {
        const int yt_rel = yt - params.pivot;
        const int denom = val_4 + yt_rel * params.sinAngle >> 12;
        const int yp = (divise(params.altitude * yt_rel, denom) * params.zoom >> 12)
                     + params.pivot;
        const int ys = yp;
        const int val_1 = params.slope * yt + params.correction;
        const int val_2 = (ys - yc) * params.cosTheta;
        const int val_3 = (ys - yc) * params.sinTheta;

        // Lightline lighting attenuation (row 0, packed by byte).
        // renderHM7 reads these same byte offsets, so the writes
        // here must land at the same byte indices, not at Pixel
        // struct fields which would reorder them.
        const int ypl = params.pivot - yp;
        if (ypl >= 0 && yt < lightline->w) {
            std::uint8_t *p = lightingRowBytes + (yt << 2);
            int lux = (lux_0 * ypl) / 960;
            if (lux > 255) lux = 255;
            p[0] = static_cast<std::uint8_t>(lux);
            lux = (lux_1 * ypl) / 960;
            if (lux > 255) lux = 255;
            p[1] = static_cast<std::uint8_t>(lux);
            lux = (lux_2 * ypl) / 960;
            if (lux > 255) lux = 255;
            p[2] = static_cast<std::uint8_t>(lux);
            p[3] = static_cast<std::uint8_t>(lux_3);
        }

        const long oy = static_cast<long>(yt) * xsize;

        int xp0 = 0;
        for (int xt = params.xMin; xt < a; ++xt) {
            const int xp = params.zoom * divise((a - xt) << 18, val_1) >> 12;

            if (xt == params.xMin) {
                xp0 = params.xMin ? (params.zoom * divise(a << 18, val_1) >> 12) : xp;
                if (yt < lightline->w) {
                    std::uint8_t *relief = reliefRowBytes + (yt << 2);
                    // Relief coefficient val_5 = (a * sinAngle) / xp0.
                    // Packed high/low byte pair at byte offsets [0,1].
                    int val_5 = (a * params.sinAngle) / (xp0 != 0 ? xp0 : 1);
                    relief[0] = static_cast<std::uint8_t>(val_5 >> 8);
                    relief[1] = static_cast<std::uint8_t>(val_5 - (relief[0] << 8));
                    // Horizontal zoom (a<<12)/xp0, packed at [2,3].
                    val_5 = (a << 12) / (xp0 != 0 ? xp0 : 1);
                    relief[2] = static_cast<std::uint8_t>(val_5 >> 8);
                    // Preserve the original's `- (lightlineData[0] << 8)`
                    // quirk: it uses [0] (the relief hi byte we just
                    // wrote), not [2]. Likely a bug in the reference
                    // but we mirror it so the composite byte layout
                    // matches what renderHM7 expects.
                    relief[3] = static_cast<std::uint8_t>(val_5 - (relief[0] << 8));
                }
            }

            // Left half: xs = a - xp.
            {
                const int xs = a - xp;
                const int xs_rel = xs - xc;
                const int yr = yc + ((xs_rel * params.sinTheta + val_2) >> 12);
                const int xr = xc + ((xs_rel * params.cosTheta - val_3) >> 12);
                data_table[xt + oy]      = static_cast<std::int16_t>(xr);
                data_table[xt + oy + oz] = static_cast<std::int16_t>(yr);
            }

            // Right half (mirrored): xs = a + xp, stored at xsize-1-xt.
            {
                const int xs = a + xp;
                const int xs_rel = xs - xc;
                const int yr = yc + ((xs_rel * params.sinTheta + val_2) >> 12);
                const int xr = xc + ((xs_rel * params.cosTheta - val_3) >> 12);
                data_table[xsize - 1 - xt + oy]      = static_cast<std::int16_t>(xr);
                data_table[xsize - 1 - xt + oy + oz] = static_cast<std::int16_t>(yr);
            }
        }
    }

    return 0;
}

}  // namespace hm7
