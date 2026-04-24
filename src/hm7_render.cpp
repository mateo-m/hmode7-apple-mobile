// hm7_render.cpp — port of the original `renderHM7` function.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 763-1767 (~1010 LOC).
//
// See the header for high-level algorithm description. This file
// preserves the original's structure as a single monolithic function
// because it's easier to audit against the reference source. The
// "split into 5 helpers" recommendation from the design doc can be a
// future refactor once the port is verified correct.
//
// Pointer arithmetic translation:
//   Original (bottom-up DIB):
//     LPBYTE firstRow = bmp->firstRow;    // points to last row
//     px = firstRow - y * pitch + x * 4;  // y=0 means last row
//   Port (top-down SDL):
//     uint8_t *pixels = surf->pixels;     // points to row 0
//     px = pixels + y * pitch + x * 4;    // y=0 means first row
//
// All `firstXxxRow - N * rowSize` become `firstXxxRow + N * pitch`
// where `firstXxxRow` is rebound to `surf->pixels` (row 0 in
// top-down) — i.e., we entirely invert the row indexing direction.
// The original `lightLineRowSize = lightlineBitmap->infoheader->biWidth << 2`
// becomes `lightline->pitch`.

#include "hm7_render.h"

#include <SDL_surface.h>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace hm7 {

namespace {

inline std::uint8_t *byte_row(SDL_Surface *surf, int y) {
    return static_cast<std::uint8_t *>(surf->pixels) + y * surf->pitch;
}
inline const std::uint8_t *byte_row_const(const SDL_Surface *surf, int y) {
    return static_cast<const std::uint8_t *>(surf->pixels) + y * surf->pitch;
}

// Helper: clamp an `int` to [0, 255] (used a lot for per-channel
// BGR+alpha arithmetic).
inline int clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

}  // namespace

int render_hm7(const RenderParams &pp,
               const RenderVars &vv,
               const RenderSurface *surfaces,
               int surface_count,
               int nb_layers) {

    // Bail if critical surfaces are missing.
    if (!pp.screen_bitmap || !pp.lightline || !pp.data_table ||
        !pp.heightmap || !pp.map_tileset || !pp.tilemap_data ||
        !pp.colormap || !pp.s_screen_bitmap) {
        return 0;
    }

    const int nbBlocks = (nb_layers + 8) >> 2;
    const long oz = static_cast<long>(pp.data_xsize) * pp.data_ysize_real;

    const int mapWidthPx = (pp.tilemap_xsize / (nb_layers + 1)) << 5;
    const int mapHeightPx = pp.tilemap_ysize << 5;

    int ysize;
    if (pp.less_cut) {
        ysize = pp.data_ysize_real >> 1;
    } else {
        ysize = pp.data_ysize_real;
    }

    const int yMaxDraw = pp.y_max_draw;
    int yMax;
    if (pp.less_cut) {
        yMax = (yMaxDraw << 1) - pp.y_min;
    } else {
        yMax = yMaxDraw;
    }
    const int xMin = pp.x_min;
    const int xMax = pp.x_max;
    const int yMin = pp.y_min;
    const int noBlack = pp.no_black;
    const int cam = pp.cam;
    const int loopX = pp.loop_x;
    const int loopY = pp.loop_y;

    const int heightLimit = vv.height_limit;
    const int displayX = vv.display_x;
    const int displayY = vv.display_y;
    const int filter = vv.filter;
    const int oScrY = vv.o_scr_y;

    // Per-layer scratch arrays. Original malloc'd; we use fixed
    // stack buffers sized for the common case (nb_layers <= 8).
    constexpr int MAX_LAYERS = 8;
    char initA[MAX_LAYERS] = {0};
    char lA[MAX_LAYERS] = {0};
    int hA[MAX_LAYERS] = {0};
    int dA[MAX_LAYERS] = {0};

    // Surface stream state. Original uses incremental `sCount`
    // walking; we mirror that with a local index.
    int sIdx = 0;
    int sNext = surface_count > 0 ? 1 : 0;

    // Current surface state (assigned when sNext transitions to a
    // new surface record).
    int sType = 0, sScreenX1 = 0, sScreenY1 = 0, sScreenX2 = 0, sScreenY2 = 0;
    int sInverse = 0;
    SDL_Surface *sBitmap = nullptr;
    int sDh = 0, sBlend = 0, sDispWidth = 0, sDispOffset = 0;
    int sWidth = 0, sHeight = 0, sRowSize = 0;

    auto load_surface = [&](int i) {
        const RenderSurface &s = surfaces[i];
        sType = s.type;
        sScreenX1 = s.screen_x1;
        sScreenY1 = s.screen_y1;
        sScreenX2 = s.screen_x2;
        sScreenY2 = s.screen_y2;
        sInverse = s.inverse;
        sBitmap = s.bitmap;
        sDh = s.dh;
        sBlend = s.blend;
        sDispWidth = s.disp_width;
        sDispOffset = s.disp_offset;
        sWidth = sBitmap ? sBitmap->w : 0;
        sHeight = sBitmap ? sBitmap->h : 0;
        sRowSize = sWidth << 2;
    };

    if (sNext) load_surface(0);

    const int screenWidth = pp.screen_bitmap->w;
    const int a = screenWidth >> 1;
    (void)a;  // kept for future use in pre-surface pass
    const int screenRowSize = pp.screen_bitmap->pitch;
    const int sScreenRowSize = pp.s_screen_bitmap->pitch;
    const int lightLineRowSize = pp.lightline->pitch;

    int x0;
    int step;
    if (filter == 0) { x0 = 0; step = 1; }
    else if (filter == 1) { x0 = 0; step = 2; }
    else { x0 = 1; step = 2; }

    int oCamera = 0;

    int y0;
    if (heightLimit > yMin) y0 = heightLimit;
    else y0 = yMin;

    // Lightline rows, laid out top-down to match the Ruby side
    // which writes the seed fade color via `set_pixel(0, 0, ...)`
    // - that lands in top-down row 0, column 0. The original
    // Windows plugin's `firstLightlineRow` points to the same
    // display pixel (a bottom-up DIB's `firstRow` is display
    // row 0 = our top-down row 0). Subtracting the DIB pitch in
    // the original moves DOWN the display; in top-down SDL we
    // ADD the pitch to move down, i.e. go to a higher row index.
    //
    // Row 0: per-row lighting (lux) + col 0 fade seed.
    // Row 1: per-column relief + horizontal zoom scratch.
    // Row 2: per-column topmost-drawn-Y (ym) tracking scratch.
    std::uint8_t *lightLightRow = byte_row(pp.lightline, 0);  // per-row lux
    std::uint8_t *reliefRow     = byte_row(pp.lightline, 1);  // relief
    std::uint8_t *ymRow         = byte_row(pp.lightline, 2);  // ym tracking

    // Bootstrap sCmin/sCmax for pre-surface pass (used only when
    // yt == yMax-1 sInitZoomData gate fires).
    int sCmax = 0, sCmin = 0, sCsl = 0;
    int sCmaxHT2 = 0, sCminHT2 = 0, sCslHT2 = 0;
    int sCmaxHT0 = 0, sCminHT0 = 0, sCslHT0 = 0;

    // Outer y loop: bottom-to-top of the draw range.
    for (int yt = yMax - 1; yt >= y0; --yt) {
        const int rYt = yt + oScrY;

        // Read per-row lighting from lightline row 2 (lux values).
        std::uint8_t *lux_px = lightLightRow + (yt << 2);
        const int lux_b = lux_px[0];
        const int lux_g = lux_px[1];
        const int lux_r = lux_px[2];
        const int lux_d = lux_px[3];

        // h_coeff lives in lightline row 1, packed (hi, lo) in bytes [0,1].
        std::uint8_t *relief_px = reliefRow + (yt << 2);
        const int h_coeff = (relief_px[0] << 8) + relief_px[1];

        const long oy = static_cast<long>(yt) * pp.data_xsize;

        int sInitZoomData = 0;

        // --------------------------------
        //  PRE-SURFACES pass (first iter only)
        // --------------------------------
        if (yt == yMax - 1) {
            while (sNext && yt <= sScreenY1) {
                if (!sInitZoomData) {
                    std::uint8_t *lp = reliefRow + ((yMax - 1) << 2);
                    sCmax = (lp[0] << 8) + lp[1];
                    sCmaxHT2 = (lp[2] << 8) + lp[3];
                    sCmaxHT0 = sCmax;
                    lp = reliefRow + (y0 << 2);
                    sCmin = (lp[0] << 8) + lp[1];
                    sCsl = sCmax - sCmin;
                    sCminHT2 = (lp[2] << 8) + lp[3];
                    sCslHT2 = sCmaxHT2 - sCminHT2;
                    sCminHT0 = sCmin;
                    sCslHT0 = sCsl;
                    sInitZoomData = 1;
                }

                const int sDx = sScreenX2 - sScreenX1;
                if (sDx && sBitmap) {
                    const int sDy = sScreenY1 - sScreenY2;
                    const int sSlope = (sDy << 7) / sDx;

                    int sXmax = (sScreenX2 > xMax) ? xMax : sScreenX2;
                    int sXmin;
                    if (x0) {
                        sXmin = (sScreenX1 & 1) ? sScreenX1 : (sScreenX1 + 1);
                    } else {
                        sXmin = (sScreenX1 & 1) ? (sScreenX1 + 1) : sScreenX1;
                    }
                    if (sXmin >= xMax) sXmin = xMax - 1;
                    else if (sXmin < xMin) sXmin = xMin + x0;

                    int sC1, sC2;
                    if (sScreenY1 >= yMax) {
                        sC1 = sCmin + (sCsl * (sScreenY1 - y0)) / (yMax - 1 - y0);
                        if (sScreenY2 < 0 || sScreenY2 >= yMax) {
                            sC2 = sCmin + (sCsl * (sScreenY2 - y0)) / (yMax - 1 - y0);
                        } else {
                            std::uint8_t *lp2 = reliefRow + (sScreenY2 << 2);
                            sC2 = (lp2[0] << 8) + lp2[1];
                        }
                    } else {
                        std::uint8_t *lp2 = reliefRow + (sScreenY1 << 2);
                        sC1 = (lp2[0] << 8) + lp2[1];
                        if (sScreenY2 < 0) {
                            sC2 = sCmin + (sCsl * (sScreenY2 - y0)) / (yMax - 1 - y0);
                        } else {
                            std::uint8_t *lp3 = reliefRow + (sScreenY2 << 2);
                            sC2 = (lp3[0] << 8) + lp3[1];
                        }
                    }
                    if (!sC1) sC1 = 1;
                    if (!sC2) sC2 = 1;

                    for (int sXt = sXmin; sXt < sXmax; sXt += step) {
                        int sH0, dx1, dx2;
                        if (sInverse) {
                            sH0 = (sScreenX2 - 1 - sXt) * sSlope >> 7;
                            dx1 = ((sScreenX2 - 1 - sXt) << 12) / sC1;
                            dx2 = ((sXt - sScreenX1) << 12) / sC2;
                        } else {
                            sH0 = (sXt - sScreenX1) * sSlope >> 7;
                            dx1 = ((sXt - sScreenX1) << 12) / sC1;
                            dx2 = ((sScreenX2 - 1 - sXt) << 12) / sC2;
                        }
                        sH0 = sH0 - (sScreenY1 - yt);
                        if (rYt - sH0 < yMin) continue;
                        if (!(dx1 + dx2)) continue;
                        int sX;
                        if (sInverse) {
                            sX = (sDispOffset + (sDispWidth * dx2) / (dx1 + dx2)) << 2;
                        } else {
                            sX = (sDispOffset + (sDispWidth * dx1) / (dx1 + dx2)) << 2;
                        }
                        if (sX < 0 || sX >= sRowSize) continue;

                        int sLux_b = 0, sLux_g = 0, sLux_r = 0, sLux_d = 0;
                        int sFYt, sFYth, sHbase;
                        if (sH0 < 0) {
                            // Billboards use depth-scale (HT2)
                            // regardless of sType. See the else
                            // branch for the full rationale.
                            sFYt = sCminHT2 + (sCslHT2 * (yt - sH0 - y0)) / (yMax - 1 - y0);
                            sFYth = sFYt;
                            sHbase = 0;
                        } else {
                            std::uint8_t *ll = lightLightRow + ((yt - sH0) << 2);
                            sLux_b = ll[0]; sLux_g = ll[1]; sLux_r = ll[2]; sLux_d = ll[3];
                            std::uint8_t *lr = reliefRow + ((yt - sH0) << 2);
                            // Sprites are BILLBOARDS. They always
                            // face the camera: they rotate with
                            // theta but never tilt with alpha. That
                            // means the on-screen sprite height must
                            // scale with DEPTH (perspective divisor
                            // `xp0`) alone, not with the slant angle.
                            //
                            // `relief[0..1]` holds `a * sinAngle /
                            // xp0` (slant-projected scale). That is
                            // correct for vertical WALLS which
                            // shrink at shallow slants, but wrong
                            // for billboard sprites - at slant=0
                            // (top-down) this collapses to 0 and at
                            // shallow slants it squishes the sprite.
                            //
                            // `relief[2..3]` holds `(a << 12) / xp0`
                            // (pure depth scale, Q12). That's the
                            // right number for billboard scaling,
                            // and what the original plugin
                            // effectively wanted here - the user-
                            // observable behaviour on Windows is
                            // billboards that stay full-height at
                            // every alpha, only rotating with theta.
                            const int depthZoom = (lr[2] << 8) + lr[3];
                            sFYt = depthZoom;
                            sFYth = depthZoom;
                            sHbase = sH0;
                        }

                        sH0 += (sDh * sFYth >> 15);
                        int sHend = (sH0 < 0) ? 0 : sH0;
                        if (rYt - sH0 < yMin) continue;
                        int sRealHeight = (sHeight * sFYt) >> 12;
                        if (sRealHeight < 2) continue;

                        int sHinit;
                        if (rYt - sRealHeight - sH0 < yMin) {
                            sHinit = rYt - yMin;
                        } else {
                            sHinit = sRealHeight + sH0;
                        }
                        int sFh = ((sHeight - 1) << 10) / (sRealHeight - 1);

                        int sHMax;
                        if (yt == yMax - 1) {
                            sHMax = ysize + oScrY;
                        } else {
                            std::uint8_t *ylp = ymRow + (sXt << 2);
                            sHMax = (ylp[0] << 8) + ylp[1];
                        }

                        for (int h = sHinit; h > sHend; ) {
                            --h;
                            if (rYt - h > sHMax) break;
                            if (rYt - h > yMaxDraw - 1) break;

                            // Original source row math (bottom-up DIB):
                            //   sData = firstSRow
                            //         - (sHeight - 1 - ((h - sH0) * sFh >> 10)) * sRowSize
                            //         + sX
                            // where firstSRow pointed at DISPLAY row 0 (top of
                            // image, at memory offset (sHeight-1)*pitch since
                            // bottom-up). Subtracting N pitches from firstSRow
                            // reaches display row N.
                            //
                            // So the original accessed display row
                            //   N = sHeight - 1 - ((h - sH0) * sFh >> 10)
                            // directly.
                            //
                            // In top-down SDL, display row == memory row, so we
                            // just use that same N as the top-down row index.
                            //
                            // High `h` corresponds to the top of the on-screen
                            // sprite draw, which should sample row 0 of the
                            // bitmap (the TOP of the source image). At h==sHinit:
                            //   X = sRealHeight * sFh >> 10 ~= sHeight-1
                            //   N = sHeight-1 - (sHeight-1) = 0  (top) ✓
                            // Low `h` corresponds to the anchor (bottom of sprite
                            // on-screen) and should sample row sHeight-1. At h=0:
                            //   X = 0, N = sHeight-1 (bottom) ✓
                            const int src_row = (sHeight - 1) - ((h - sH0) * sFh >> 10);
                            if (src_row < 0 || src_row >= sHeight) continue;
                            const std::uint8_t *sData =
                                byte_row_const(sBitmap, src_row) + sX;
                            if (!sData[3]) continue;

                            // sScreenData position. Original:
                            //   sScreenData = firstSScreenRow - (rYt - h) * sScreenRowSize + (sXt << 3)
                            // top-down: row = rYt - h
                            const int ss_row = rYt - h;
                            if (ss_row < 0 || ss_row >= pp.s_screen_bitmap->h) continue;
                            std::uint8_t *sScreenData =
                                byte_row(pp.s_screen_bitmap, ss_row) + (sXt << 3);

                            if (sScreenData[0] && !sScreenData[1] && sScreenData[7] == 255) {
                                continue;
                            }

                            int blue = sData[0];
                            int green = sData[1];
                            int red = sData[2];
                            int alpha = sData[3];
                            if (sLux_d) {
                                blue += sLux_b; green += sLux_g; red += sLux_r;
                                blue = std::min(blue, 255);
                                green = std::min(green, 255);
                                red = std::min(red, 255);
                            } else {
                                blue -= sLux_b; green -= sLux_g; red -= sLux_r;
                            }

                            if (sScreenData[0] &&
                                (sBlend || sData[3] < 255 || sScreenData[2] + sScreenData[3] >= rYt - sHend)) {
                                const int blend = sScreenData[1];
                                const int sOpacity = sScreenData[7];
                                if (!blend) {
                                    blue = (blue * (255 - sOpacity) + sScreenData[4] * sOpacity) >> 8;
                                    green = (green * (255 - sOpacity) + sScreenData[5] * sOpacity) >> 8;
                                    red = (red * (255 - sOpacity) + sScreenData[6] * sOpacity) >> 8;
                                } else if (blend == 1) {
                                    blue = blue + ((sScreenData[4] * sOpacity) >> 8);
                                    green = green + ((sScreenData[5] * sOpacity) >> 8);
                                    red = red + ((sScreenData[6] * sOpacity) >> 8);
                                    blue = std::min(blue, 255);
                                    green = std::min(green, 255);
                                    red = std::min(red, 255);
                                } else if (blend == 2) {
                                    blue = blue - ((sScreenData[4] * sOpacity) >> 8);
                                    green = green - ((sScreenData[5] * sOpacity) >> 8);
                                    red = red - ((sScreenData[6] * sOpacity) >> 8);
                                }
                                alpha = ~static_cast<char>(((255 - alpha) * (255 - sScreenData[7])) / 255);
                            }
                            blue = clamp_u8(blue);
                            green = clamp_u8(green);
                            red = clamp_u8(red);

                            sScreenData[0] = 1;
                            sScreenData[1] = static_cast<std::uint8_t>(sBlend);
                            if (rYt - sHbase > 510) {
                                sScreenData[2] = 255;
                                sScreenData[3] = 255;
                            } else if (rYt - sHbase > 255) {
                                sScreenData[2] = static_cast<std::uint8_t>(rYt - sHbase - 255);
                                sScreenData[3] = 255;
                            } else {
                                sScreenData[2] = 0;
                                sScreenData[3] = static_cast<std::uint8_t>(rYt - sHbase);
                            }
                            sScreenData[4] = static_cast<std::uint8_t>(blue);
                            sScreenData[5] = static_cast<std::uint8_t>(green);
                            sScreenData[6] = static_cast<std::uint8_t>(red);
                            sScreenData[7] = static_cast<std::uint8_t>(alpha & 0xff);
                        }
                    }
                }

                // Advance to next surface.
                ++sIdx;
                if (sIdx < surface_count) {
                    load_surface(sIdx);
                } else {
                    sNext = 0;
                }
            }
        }

        // --------------------------------
        //  COLUMN LOOP
        // --------------------------------
        for (int xt = xMin + x0; xt < xMax; xt += step) {
            // Lightline row 0 = per-column ym tracking.
            std::uint8_t *ylp = ymRow + (xt << 2);
            int ym;
            if (yt == yMax - 1) {
                ym = ysize + oScrY;
                ylp[0] = (ym >> 8) & 0xff;
                ylp[1] = (ym - (ylp[0] << 8)) & 0xff;
            } else {
                ym = (ylp[0] << 8) + ylp[1];
            }

            int xs = pp.data_table[xt + oy] + displayX;
            int ys = pp.data_table[xt + oy + oz] + displayY;

            if (!loopX) {
                if (xs >= mapWidthPx || xs < 0) {
                    if (rYt < ym && rYt < yMaxDraw) {
                        std::uint8_t *screenData =
                            byte_row(pp.screen_bitmap, rYt) + (xt << 2);
                        std::uint8_t *sScreenData =
                            byte_row(pp.s_screen_bitmap, rYt) + (xt << 3);
                        if (sScreenData[0]) {
                            int blue, green, red;
                            if (!sScreenData[1] && sScreenData[7] == 255) {
                                blue = sScreenData[4];
                                green = sScreenData[5];
                                red = sScreenData[6];
                            } else {
                                const int blend = sScreenData[1];
                                if (blend == 2) {
                                    blue = 0; green = 0; red = 0;
                                } else {
                                    const int sOpacity = sScreenData[7];
                                    blue = (sScreenData[4] * sOpacity) >> 8;
                                    green = (sScreenData[5] * sOpacity) >> 8;
                                    red = (sScreenData[6] * sOpacity) >> 8;
                                }
                            }
                            screenData[0] = static_cast<std::uint8_t>(blue);
                            screenData[1] = static_cast<std::uint8_t>(green);
                            screenData[2] = static_cast<std::uint8_t>(red);
                            screenData[3] = sScreenData[7];
                            sScreenData[0] = 0;
                            ylp[0] = (rYt >> 8) & 0xff;
                            ylp[1] = (rYt - (ylp[0] << 8)) & 0xff;
                            continue;
                        }
                        screenData[3] = 0;
                    }
                    if (rYt < ym) {
                        ylp[0] = (rYt >> 8) & 0xff;
                        ylp[1] = (rYt - (ylp[0] << 8)) & 0xff;
                    }
                    continue;
                }
            } else {
                if (xs >= mapWidthPx) xs -= mapWidthPx * (xs / mapWidthPx);
                else if (xs < 0) xs -= mapWidthPx * (xs / mapWidthPx - 1);
            }

            if (!loopY) {
                if (ys >= mapHeightPx || ys < 0) {
                    if (rYt < ym && rYt < yMaxDraw) {
                        std::uint8_t *screenData =
                            byte_row(pp.screen_bitmap, rYt) + (xt << 2);
                        std::uint8_t *sScreenData =
                            byte_row(pp.s_screen_bitmap, rYt) + (xt << 3);
                        if (sScreenData[0]) {
                            int blue, green, red;
                            if (!sScreenData[1] && sScreenData[7] == 255) {
                                blue = sScreenData[4];
                                green = sScreenData[5];
                                red = sScreenData[6];
                            } else {
                                const int blend = sScreenData[1];
                                if (blend == 2) {
                                    blue = 0; green = 0; red = 0;
                                } else {
                                    const int sOpacity = sScreenData[7];
                                    blue = (sScreenData[4] * sOpacity) >> 8;
                                    green = (sScreenData[5] * sOpacity) >> 8;
                                    red = (sScreenData[6] * sOpacity) >> 8;
                                }
                            }
                            screenData[0] = static_cast<std::uint8_t>(blue);
                            screenData[1] = static_cast<std::uint8_t>(green);
                            screenData[2] = static_cast<std::uint8_t>(red);
                            screenData[3] = sScreenData[7];
                            sScreenData[0] = 0;
                            ylp[0] = (rYt >> 8) & 0xff;
                            ylp[1] = (rYt - (ylp[0] << 8)) & 0xff;
                            continue;
                        }
                        screenData[3] = 0;
                    }
                    if (rYt < ym) {
                        ylp[0] = (rYt >> 8) & 0xff;
                        ylp[1] = (rYt - (ylp[0] << 8)) & 0xff;
                    }
                    continue;
                }
            } else {
                if (ys >= mapHeightPx) ys -= mapHeightPx * (ys / mapHeightPx);
                else while (ys < 0) ys += mapHeightPx;
            }

            // Tile lookup in tilemap.
            const std::int16_t *ptrTileIndex = pp.tilemap_data
                + (ys >> 5) * pp.tilemap_xsize
                + (xs >> 5) * (nb_layers + 1);
            const int tileIndex = *ptrTileIndex;

            for (int itLayer = 0; itLayer < nb_layers; ++itLayer) {
                initA[itLayer] = 0;
            }

            const int tileCol = tileIndex & 7;
            const int tileRow = tileIndex >> 3;
            const int xts = (((tileCol << 5) + (xs & 31))) * nbBlocks;
            const int yts = (tileRow << 5) + (ys & 31);
            const int xsr = xs & 31;
            const int ysr = ys & 31;

            // mapTilesetData = mapTileset[yts, xts*4]
            const std::uint8_t *mapTilesetData =
                byte_row_const(pp.map_tileset, yts) + (xts << 2);

            // dy from heightmap plane 0 * h_coeff.
            int dy = (pp.heightmap[(xs << 1) + ys * pp.heightmap_xsize] * h_coeff) >> 15;
            const int oShadow = pp.heightmap[(xs << 1) + ys * pp.heightmap_xsize + 1];
            const int shadow = (oShadow != 0) ? 1 : 0;

            int totHA = 0;
            for (int itLayer = 0; itLayer < nb_layers; ++itLayer) {
                hA[itLayer] = (mapTilesetData[4 + itLayer] * h_coeff) >> 15;
                totHA += hA[itLayer];
                dA[itLayer] = totHA;
                lA[itLayer] = mapTilesetData[4 + itLayer] >> 3;
            }

            int alpha = mapTilesetData[3];
            int odyh;
            if (dy > rYt - yMin) {
                odyh = dy - rYt + yMin;
                dy = rYt - yMin;
            } else {
                odyh = 0;
            }
            if (yt + 1 == ysize) {
                int ody_cam;
                if (cam > 1) ody_cam = dy;
                else ody_cam = dy - totHA;
                if (ody_cam > oCamera) oCamera = ody_cam;
            }
            int ody = rYt - dy;

            // Inline surface pass (for matching surface).
            if (sNext && yt <= sScreenY1 && xt >= sScreenX1) {
                if (xt < sScreenX2) {
                    if (!sInitZoomData) {
                        std::uint8_t *lp = reliefRow + ((yMax - 1) << 2);
                        sCmax = (lp[0] << 8) + lp[1];
                        sCmaxHT2 = (lp[2] << 8) + lp[3];
                        sCmaxHT0 = sCmax;
                        lp = reliefRow + (y0 << 2);
                        sCmin = (lp[0] << 8) + lp[1];
                        sCsl = sCmax - sCmin;
                        sCminHT2 = (lp[2] << 8) + lp[3];
                        sCslHT2 = sCmaxHT2 - sCminHT2;
                        sCminHT0 = sCmin;
                        sCslHT0 = sCsl;
                        sInitZoomData = 1;
                    }

                    const int sDx = sScreenX2 - sScreenX1;
                    if (sDx && sBitmap) {
                        const int sDy = sScreenY1 - sScreenY2;
                        const int sSlope = (sDy << 7) / sDx;
                        int sXmax = (sScreenX2 > xMax) ? xMax : sScreenX2;
                        int sXmin = xt;

                        int sC1, sC2;
                        if (sScreenY1 >= yMax) {
                            sC1 = sCmin + (sCsl * (sScreenY1 - y0)) / (yMax - 1 - y0);
                            if (sScreenY2 < 0 || sScreenY2 >= yMax) {
                                sC2 = sCmin + (sCsl * (sScreenY2 - y0)) / (yMax - 1 - y0);
                            } else {
                                std::uint8_t *lp2 = reliefRow + (sScreenY2 << 2);
                                sC2 = (lp2[0] << 8) + lp2[1];
                            }
                        } else {
                            std::uint8_t *lp2 = reliefRow + (sScreenY1 << 2);
                            sC1 = (lp2[0] << 8) + lp2[1];
                            if (sScreenY2 < 0) {
                                sC2 = sCmin + (sCsl * (sScreenY2 - y0)) / (yMax - 1 - y0);
                            } else {
                                std::uint8_t *lp3 = reliefRow + (sScreenY2 << 2);
                                sC2 = (lp3[0] << 8) + lp3[1];
                            }
                        }
                        if (!sC1) sC1 = 1;
                        if (!sC2) sC2 = 1;

                        for (int sXt = sXmin; sXt < sXmax; sXt += step) {
                            int sH0, dx1, dx2;
                            if (sInverse) {
                                sH0 = (sScreenX2 - 1 - sXt) * sSlope >> 7;
                                dx1 = ((sScreenX2 - 1 - sXt) << 12) / sC1;
                                dx2 = ((sXt - sScreenX1) << 12) / sC2;
                            } else {
                                sH0 = (sXt - sScreenX1) * sSlope >> 7;
                                dx1 = ((sXt - sScreenX1) << 12) / sC1;
                                dx2 = ((sScreenX2 - 1 - sXt) << 12) / sC2;
                            }
                            sH0 = sH0 - (sScreenY1 - yt);
                            if (rYt - sH0 < yMin) continue;
                            if (!(dx1 + dx2)) continue;
                            int sX;
                            if (sInverse) sX = (sDispOffset + (sDispWidth * dx2) / (dx1 + dx2)) << 2;
                            else          sX = (sDispOffset + (sDispWidth * dx1) / (dx1 + dx2)) << 2;
                            if (sX < 0 || sX >= sRowSize) continue;

                            int sLux_b = 0, sLux_g = 0, sLux_r = 0, sLux_d = 0;
                            int sFYt, sFYth, sHbase;
                            if (sH0 < 0) {
                                // Billboards use depth-scale (HT2).
                                sFYt = sCminHT2 + (sCslHT2 * (yt - sH0 - y0)) / (yMax - 1 - y0);
                                sFYth = sFYt;
                                sHbase = 0;
                            } else {
                                std::uint8_t *ll = lightLightRow + ((yt - sH0) << 2);
                                sLux_b = ll[0]; sLux_g = ll[1]; sLux_r = ll[2]; sLux_d = ll[3];
                                std::uint8_t *lr = reliefRow + ((yt - sH0) << 2);
                                // Sprites are billboards: use the
                                // depth-proportional zoom from
                                // relief[2..3], not the slant-
                                // projected relief[0..1]. See the
                                // pre-pass for the full rationale.
                                const int depthZoom = (lr[2] << 8) + lr[3];
                                sFYt = depthZoom;
                                sFYth = depthZoom;
                                sHbase = sH0;
                            }

                            sH0 += (sDh * sFYth) >> 15;
                            int sHend = (sH0 < 0) ? 0 : sH0;
                            if (rYt - sH0 < yMin) continue;
                            int sRealHeight = (sHeight * sFYt) >> 12;
                            if (sRealHeight < 2) continue;

                            int sHinit;
                            if (rYt - sRealHeight - sH0 < yMin) sHinit = rYt - yMin;
                            else                                 sHinit = sRealHeight + sH0;
                            int sFh = ((sHeight - 1) << 10) / (sRealHeight - 1);

                            int sHMax;
                            if (yt == yMax - 1) sHMax = ysize + oScrY;
                            else {
                                std::uint8_t *ylp2 = ymRow + (sXt << 2);
                                sHMax = (ylp2[0] << 8) + ylp2[1];
                            }

                            for (int h = sHinit; h > sHend; ) {
                                --h;
                                if (rYt - h > sHMax) break;
                                if (rYt - h > yMaxDraw - 1) break;
                                // Source row: see long explanation in the
                                // pre-surfaces pass above. Display row index
                                // is `sHeight - 1 - ((h - sH0) * sFh >> 10)`
                                // and in top-down SDL that equals the memory
                                // row we want.
                                const int src_row = (sHeight - 1) - ((h - sH0) * sFh >> 10);
                                if (src_row < 0 || src_row >= sHeight) continue;
                                const std::uint8_t *sData = byte_row_const(sBitmap, src_row) + sX;
                                if (!sData[3]) continue;

                                const int ss_row = rYt - h;
                                if (ss_row < 0 || ss_row >= pp.s_screen_bitmap->h) continue;
                                std::uint8_t *sScreenData = byte_row(pp.s_screen_bitmap, ss_row)
                                                           + (sXt << 3);

                                if (sScreenData[0] && !sScreenData[1] && sScreenData[7] == 255 &&
                                    sScreenData[2] + sScreenData[3] + 2 >= rYt - sHend) {
                                    continue;
                                }

                                int blue = sData[0], green = sData[1], red = sData[2], alpha_s = sData[3];
                                if (sLux_d) {
                                    blue += sLux_b; green += sLux_g; red += sLux_r;
                                    blue = std::min(blue, 255);
                                    green = std::min(green, 255);
                                    red = std::min(red, 255);
                                } else {
                                    blue -= sLux_b; green -= sLux_g; red -= sLux_r;
                                }
                                if (sScreenData[0] &&
                                    (sBlend || sData[3] < 255 || sScreenData[2] + sScreenData[3] >= rYt - sHend)) {
                                    const int blend = sScreenData[1];
                                    const int sOpacity = sScreenData[7];
                                    if (!blend) {
                                        blue = (blue * (255 - sOpacity) + sScreenData[4] * sOpacity) >> 8;
                                        green = (green * (255 - sOpacity) + sScreenData[5] * sOpacity) >> 8;
                                        red = (red * (255 - sOpacity) + sScreenData[6] * sOpacity) >> 8;
                                    } else if (blend == 1) {
                                        blue += (sScreenData[4] * sOpacity) >> 8;
                                        green += (sScreenData[5] * sOpacity) >> 8;
                                        red += (sScreenData[6] * sOpacity) >> 8;
                                        blue = std::min(blue, 255);
                                        green = std::min(green, 255);
                                        red = std::min(red, 255);
                                    } else if (blend == 2) {
                                        blue -= (sScreenData[4] * sOpacity) >> 8;
                                        green -= (sScreenData[5] * sOpacity) >> 8;
                                        red -= (sScreenData[6] * sOpacity) >> 8;
                                    }
                                    alpha_s = ~static_cast<char>(((255 - alpha_s) * (255 - sScreenData[7])) / 255);
                                }
                                blue = clamp_u8(blue);
                                green = clamp_u8(green);
                                red = clamp_u8(red);

                                sScreenData[0] = 1;
                                sScreenData[1] = static_cast<std::uint8_t>(sBlend);
                                if (rYt - sHbase > 510) {
                                    sScreenData[2] = 255;
                                    sScreenData[3] = 255;
                                } else if (rYt - sHbase > 255) {
                                    sScreenData[2] = static_cast<std::uint8_t>(rYt - sHbase - 255);
                                    sScreenData[3] = 255;
                                } else {
                                    sScreenData[2] = 0;
                                    sScreenData[3] = static_cast<std::uint8_t>(rYt - sHbase);
                                }
                                sScreenData[4] = static_cast<std::uint8_t>(blue);
                                sScreenData[5] = static_cast<std::uint8_t>(green);
                                sScreenData[6] = static_cast<std::uint8_t>(red);
                                sScreenData[7] = static_cast<std::uint8_t>(alpha_s & 0xff);
                            }
                        }
                    }
                }
                // Advance surface.
                ++sIdx;
                if (sIdx < surface_count) {
                    load_surface(sIdx);
                } else {
                    sNext = 0;
                }
            }

            if (ym <= ody) continue;

            // Wall draw: vertical column from dy up to ym.
            int ground = 0;
            int top_flag = 0;
            int blue, green, red;
            const std::uint8_t *colormapData = nullptr;
            int pos = 0;

            for (int yd = dy; rYt - yd < ym; --yd) {
                if (rYt - yd + 1 - yMaxDraw > 0) break;

                const int screen_row = rYt - yd;
                if (screen_row < 0 || screen_row >= pp.screen_bitmap->h) continue;
                std::uint8_t *screenData = byte_row(pp.screen_bitmap, screen_row) + (xt << 2);
                std::uint8_t *sScreenData = byte_row(pp.s_screen_bitmap, screen_row) + (xt << 3);

                if (sScreenData[0] && !sScreenData[1] && sScreenData[7] == 255 &&
                    sScreenData[2] + sScreenData[3] >= rYt) {
                    screenData[0] = sScreenData[4];
                    screenData[1] = sScreenData[5];
                    screenData[2] = sScreenData[6];
                    screenData[3] = 255;
                    sScreenData[0] = 0;
                    continue;
                }

                if (yd < dy && yt + 1 == yMax && !noBlack) {
                    screenData[0] = 0; screenData[1] = 0; screenData[2] = 0; screenData[3] = 255;
                    sScreenData[0] = 0;
                } else {
                    if (dy - yd > 0) {
                        int totHA_i = 0;
                        ground = 1;
                        for (int itLayer = nb_layers - 1; itLayer >= 0; --itLayer) {
                            if (dy - yd <= dA[itLayer]) {
                                if (!initA[itLayer]) {
                                    int ti = ptrTileIndex[itLayer + 1] << 5;
                                    // Colormap row lookup. Original (bottom-up DIB):
                                    //   colormapData = colormapBegin
                                    //                   - ((ti + ysr) * 10 << 6)
                                    //                   + (xsr << 2);
                                    // The `* 10 << 6 = * 640 bytes` shift is exactly
                                    // one colormap-atlas row (160 px wide). So in
                                    // top-down, `(ti + ysr)` IS the row index; no
                                    // `* 10` multiplier. drawTextureset writes at
                                    // the same row convention: tile `N` occupies
                                    // rows [N*32, N*32+31], and renderHM7 here
                                    // expects to read row `ti + ysr` where
                                    // ti = layer_tile_num * 32.
                                    const int cm_row = ti + ysr;
                                    if (cm_row < 0 || cm_row >= pp.colormap->h) {
                                        colormapData = nullptr;
                                    } else {
                                        const std::uint8_t *cmRow = byte_row_const(pp.colormap, cm_row);
                                        const int oColor = cmRow[xsr << 2];
                                        // Second lookup depends on oColor (direction code).
                                        if (oColor == 32 || oColor == 96) {
                                            const int cm_row2 = ti + xsr;
                                            if (cm_row2 < 0 || cm_row2 >= pp.colormap->h) {
                                                colormapData = nullptr;
                                            } else {
                                                colormapData = byte_row_const(pp.colormap, cm_row2)
                                                             + (oColor << 2);
                                            }
                                        } else {
                                            colormapData = cmRow + (oColor << 2);
                                        }
                                    }
                                    initA[itLayer] = 1;
                                }
                                if (hA[itLayer] != 0) {
                                    pos = (31 - lA[itLayer]
                                         + ((dy + odyh - yd - totHA_i) * lA[itLayer]) / hA[itLayer]) << 2;
                                } else {
                                    pos = 0;
                                }
                                ground = 0;
                                break;
                            }
                            totHA_i = dA[itLayer];
                        }
                        if (!ground && colormapData && colormapData[pos + 3]) {
                            blue = colormapData[pos];
                            green = colormapData[pos + 1];
                            red = colormapData[pos + 2];
                        } else {
                            // Fallback: sample the 2D tile art along
                            // an interpolation from row 0 (at wall
                            // base) toward the anchor's ysr (at wall
                            // top). This reveals vertical detail
                            // hidden in the tile art (stair steps,
                            // building facades, etc.) that would
                            // otherwise extrude as a single uniform
                            // column. Wall opacity still follows the
                            // sampled pixel's alpha so silhouettes
                            // respect the tile's transparent borders.
                            //
                            // When the sampled pixel happens to land
                            // on a transparent part of the tile art,
                            // fall back to the anchor pixel so we
                            // don't bite out chunks of the building
                            // silhouette.
                            int sample_ysr = ysr;
                            if (dy > 0) {
                                sample_ysr = ysr - (ysr * (dy - yd)) / dy;
                                if (sample_ysr < 0) sample_ysr = 0;
                            }
                            const int sample_yts = (tileRow << 5) + sample_ysr;
                            const std::uint8_t *wall_sample = nullptr;
                            if (sample_yts >= 0 && sample_yts < pp.map_tileset->h) {
                                wall_sample = byte_row_const(pp.map_tileset, sample_yts)
                                            + (xts << 2);
                            }
                            if (wall_sample && wall_sample[3]) {
                                blue = wall_sample[0];
                                green = wall_sample[1];
                                red = wall_sample[2];
                            } else {
                                blue = mapTilesetData[0];
                                green = mapTilesetData[1];
                                red = mapTilesetData[2];
                            }
                        }
                        top_flag = 0;
                    } else {
                        top_flag = 1;
                        blue = mapTilesetData[0];
                        green = mapTilesetData[1];
                        red = mapTilesetData[2];
                    }

                    if (lux_d) {
                        blue += lux_b; green += lux_g; red += lux_r;
                        if (shadow && (top_flag || ground)) {
                            blue += oShadow; green += oShadow; red += oShadow;
                            blue = clamp_u8(blue);
                            green = clamp_u8(green);
                            red = clamp_u8(red);
                        } else {
                            blue = std::min(blue, 255);
                            green = std::min(green, 255);
                            red = std::min(red, 255);
                        }
                    } else {
                        blue -= lux_b; green -= lux_g; red -= lux_r;
                        if (shadow && (top_flag || ground)) {
                            blue += oShadow; green += oShadow; red += oShadow;
                        }
                        blue = clamp_u8(blue);
                        green = clamp_u8(green);
                        red = clamp_u8(red);
                    }

                    if (sScreenData[0] && sScreenData[2] + sScreenData[3] >= rYt) {
                        const int blend = sScreenData[1];
                        const int sOpacity = sScreenData[7];
                        if (blend == 0) {
                            blue = (blue * (255 - sOpacity) + sScreenData[4] * sOpacity) >> 8;
                            green = (green * (255 - sOpacity) + sScreenData[5] * sOpacity) >> 8;
                            red = (red * (255 - sOpacity) + sScreenData[6] * sOpacity) >> 8;
                        } else if (blend == 1) {
                            blue += (sScreenData[4] * sOpacity) >> 8;
                            green += (sScreenData[5] * sOpacity) >> 8;
                            red += (sScreenData[6] * sOpacity) >> 8;
                            blue = std::min(blue, 255);
                            green = std::min(green, 255);
                            red = std::min(red, 255);
                        } else if (blend == 2) {
                            blue -= (sScreenData[4] * sOpacity) >> 8;
                            green -= (sScreenData[5] * sOpacity) >> 8;
                            red -= (sScreenData[6] * sOpacity) >> 8;
                            blue = std::max(blue, 0);
                            green = std::max(green, 0);
                            red = std::max(red, 0);
                        }
                    }

                    screenData[0] = static_cast<std::uint8_t>(blue);
                    screenData[1] = static_cast<std::uint8_t>(green);
                    screenData[2] = static_cast<std::uint8_t>(red);
                    screenData[3] = static_cast<std::uint8_t>(alpha);
                    sScreenData[0] = 0;
                }
            }

            // Update ymRow with new ody value for this column.
            ylp[0] = (ody >> 8) & 0xff;
            ylp[1] = (ody - (ylp[0] << 8)) & 0xff;
            if (rYt < yMaxDraw) {
                std::uint8_t *sS = byte_row(pp.s_screen_bitmap, rYt) + (xt << 3);
                sS[0] = 0;
            }
        }
    }

    // --------------------------------
    //  FINAL OVERDRAW pass
    // --------------------------------
    for (int xt = xMin + x0; xt < xMax; xt += step) {
        std::uint8_t *ylp = ymRow + (xt << 2);
        const int y0min = (ylp[0] << 8) + ylp[1];
        for (int yt = y0min - 1; yt >= yMin; --yt) {
            if (yt < 0 || yt >= pp.screen_bitmap->h) continue;
            std::uint8_t *screenData = byte_row(pp.screen_bitmap, yt) + (xt << 2);
            std::uint8_t *sScreenData = byte_row(pp.s_screen_bitmap, yt) + (xt << 3);
            if (sScreenData[0]) {
                int blue, green, red;
                if (!sScreenData[1] && sScreenData[7] == 255) {
                    blue = sScreenData[4];
                    green = sScreenData[5];
                    red = sScreenData[6];
                } else {
                    const int blend = sScreenData[1];
                    if (blend == 2) {
                        blue = 0; green = 0; red = 0;
                    } else {
                        const int sOpacity = sScreenData[7];
                        blue = (sScreenData[4] * sOpacity) >> 8;
                        green = (sScreenData[5] * sOpacity) >> 8;
                        red = (sScreenData[6] * sOpacity) >> 8;
                    }
                }
                screenData[0] = static_cast<std::uint8_t>(blue);
                screenData[1] = static_cast<std::uint8_t>(green);
                screenData[2] = static_cast<std::uint8_t>(red);
                screenData[3] = sScreenData[7];
                sScreenData[0] = 0;
                continue;
            }
            screenData[3] = 0;
        }
    }

    return oCamera;
}

}  // namespace hm7
