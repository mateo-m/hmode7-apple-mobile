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
// slope / Q11 trig (see HMODE7_PORT_DESIGN.md §6).
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

    // Seed lux values from lightline[row=2, x=0] = fade color, BGRA
    // in-memory on Windows, RGBA on our side. Byte order is
    // abstracted by the Pixel struct; we read whichever channel maps
    // to "blue" etc. In the original: lux_b = data[0] (B), lux_g =
    // data[1] (G), lux_r = data[2] (R), lux_d = data[3] (A=flag).
    // The Ruby side writes these via `set_pixel(0, 0, fade_color)`
    // which mkxp-z stores in our engine's native Color order.
    const Pixel *fadePixel = hm7_row_ptr_const(lightline, /*row 2*/ 2);
    int lux_b = fadePixel[0].b;
    int lux_g = fadePixel[0].g;
    int lux_r = fadePixel[0].r;
    int lux_d = fadePixel[0].a;

    int yMax = params.yMax;
    if (params.lessCut) {
        yMax = (yMax << 1) - params.yMin;
    }

    int y0 = params.heightLimit;
    if (y0 < params.yMin) y0 = params.yMin;

    // Row 1 = the per-column scratch (relief + horizontal zoom).
    Pixel *reliefRow = hm7_row_ptr(lightline, 1);
    // Row 2 = per-row lighting output.
    Pixel *lightingRow = hm7_row_ptr(lightline, 2);

    for (int yt = y0; yt < yMax; ++yt) {
        const int yt_rel = yt - params.pivot;
        const int denom = val_4 + yt_rel * params.sinAngle >> 12;
        const int yp = (divise(params.altitude * yt_rel, denom) * params.zoom >> 12)
                     + params.pivot;
        const int ys = yp;
        const int val_1 = params.slope * yt + params.correction;
        const int val_2 = (ys - yc) * params.cosTheta;
        const int val_3 = (ys - yc) * params.sinTheta;

        // Lightline lighting attenuation (row 2).
        const int ypl = params.pivot - yp;
        if (ypl >= 0 && yt < lightline->w) {
            Pixel &p = lightingRow[yt];
            int lux = (lux_b * ypl) / 960;
            if (lux > 255) lux = 255;
            p.b = static_cast<std::uint8_t>(lux);
            lux = (lux_g * ypl) / 960;
            if (lux > 255) lux = 255;
            p.g = static_cast<std::uint8_t>(lux);
            lux = (lux_r * ypl) / 960;
            if (lux > 255) lux = 255;
            p.r = static_cast<std::uint8_t>(lux);
            p.a = static_cast<std::uint8_t>(lux_d);
        }

        const long oy = static_cast<long>(yt) * xsize;

        int xp0 = 0;
        for (int xt = params.xMin; xt < a; ++xt) {
            const int xp = params.zoom * divise((a - xt) << 18, val_1) >> 12;

            if (xt == params.xMin) {
                xp0 = params.xMin ? (params.zoom * divise(a << 18, val_1) >> 12) : xp;
                if (yt < lightline->w) {
                    Pixel &relief = reliefRow[yt];
                    // Relief coefficient val_5 = (a * sinAngle) / xp0.
                    // Packed high/low byte pair in channels [0,1].
                    int val_5 = (a * params.sinAngle) / (xp0 != 0 ? xp0 : 1);
                    relief.b = static_cast<std::uint8_t>(val_5 >> 8);
                    relief.g = static_cast<std::uint8_t>(val_5 - (relief.b << 8));
                    // Horizontal zoom (a<<12)/xp0, same packing in [2,3].
                    val_5 = (a << 12) / (xp0 != 0 ? xp0 : 1);
                    relief.r = static_cast<std::uint8_t>(val_5 >> 8);
                    // Preserve the original's `- (lightlineData[0] << 8)`
                    // quirk: it uses [0] (blue), not [2] (red). Likely a
                    // bug in the original but we mirror for compatibility.
                    relief.a = static_cast<std::uint8_t>(val_5 - (relief.b << 8));
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
