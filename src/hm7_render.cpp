// hm7_render.cpp — port of the original `renderHM7` function.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 763-1767 (~1010 LOC).
//
// See the header for the algorithm. The body keeps the original's
// loop structure, because that is what makes it possible to audit
// line by line against the reference source. The parts the original
// wrote out twice live in local helpers instead.
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
#include "hm7_pixels.h"

#include <SDL_surface.h>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace hm7 {

namespace {

// Per-layer scratch arrays live on the stack, so the renderer caps
// how many layers it accepts. The original malloc'd them per call.
// Three layers is what RPG Maker XP maps carry, and the plugin's own
// "n layers" rework never went past a handful.
constexpr int kMaxLayers = 8;

// Helper: clamp an `int` to [0, 255] (used a lot for per-channel
// BGR+alpha arithmetic).
inline int clamp_u8(int v) {
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return v;
}

// The lightline scratch rows pack 16-bit values into byte pairs, big
// end first. Both halves of that pack appear all over the renderer.
inline int read_u16(const std::uint8_t *p) {
    return (p[0] << 8) + p[1];
}

inline void write_u16(std::uint8_t *p, int v) {
    p[0] = (v >> 8) & 0xff;
    p[1] = v & 0xff;
}

// Move one pixel from the surface-compositing scratch to the screen.
// Returns true when the scratch held a surface pixel, which means the
// screen pixel is now written and the caller must not draw over it.
//
// The scratch layout per pixel is 8 bytes:
//   [0] "holds a pixel" flag   [1] blend mode
//   [2,3] depth pair           [4,5,6] colour   [7] opacity
bool flush_surface_pixel(std::uint8_t *screenData, std::uint8_t *sScreenData) {
    if (!sScreenData[0])
        return false;

    int blue, green, red;
    if (!sScreenData[1] && sScreenData[7] == 255) {
        blue = sScreenData[4];
        green = sScreenData[5];
        red = sScreenData[6];
    } else if (sScreenData[1] == 2) {
        blue = 0;
        green = 0;
        red = 0;
    } else {
        const int sOpacity = sScreenData[7];
        blue = (sScreenData[4] * sOpacity) >> 8;
        green = (sScreenData[5] * sOpacity) >> 8;
        red = (sScreenData[6] * sOpacity) >> 8;
    }

    screenData[0] = static_cast<std::uint8_t>(blue);
    screenData[1] = static_cast<std::uint8_t>(green);
    screenData[2] = static_cast<std::uint8_t>(red);
    screenData[3] = sScreenData[7];
    sScreenData[0] = 0;
    return true;
}

}  // namespace

int render_hm7(const RenderParams &pp, const RenderVars &vv, const RenderSurface *surfaces, int surface_count,
               int nb_layers, WallLayerMode wall_layer_mode) {
    const bool use_top_cumulative = (wall_layer_mode == WallLayerMode::TopCumulative);

    // Bail if critical surfaces are missing.
    if (!pp.screen_bitmap || !pp.lightline || !pp.data_table || !pp.heightmap || !pp.map_tileset ||
        !pp.tilemap_data || !pp.colormap || !pp.s_screen_bitmap) {
        return 0;
    }

    // The per-layer scratch arrays below are fixed size, and the
    // layer loops write one entry per layer. Refuse a layer count
    // that would run past them instead of corrupting the stack.
    if (nb_layers < 1 || nb_layers > kMaxLayers) {
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

    // Per-layer scratch arrays. The original malloc'd them per call.
    // The guard above keeps `nb_layers` inside them.
    char initA[kMaxLayers] = {0};
    char lA[kMaxLayers] = {0};
    int hA[kMaxLayers] = {0};
    int dA[kMaxLayers] = {0};

    // Surface stream state. Original uses incremental `sCount`
    // walking; we mirror that with a local index.
    int sIdx = 0;
    int sNext = surface_count > 0 ? 1 : 0;

    // Current surface state (assigned when sNext transitions to a
    // new surface record).
    int sScreenX1 = 0, sScreenY1 = 0, sScreenX2 = 0, sScreenY2 = 0;
    int sInverse = 0;
    SDL_Surface *sBitmap = nullptr;
    int sDh = 0, sBlend = 0, sDispWidth = 0, sDispOffset = 0;
    int sHeight = 0, sRowSize = 0;

    auto load_surface = [&](int i) {
        const RenderSurface &s = surfaces[i];
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
        sHeight = sBitmap ? sBitmap->h : 0;
        sRowSize = (sBitmap ? sBitmap->w : 0) << 2;
    };

    if (sNext)
        load_surface(0);

    int x0;
    int step;
    if (filter == 0) {
        x0 = 0;
        step = 1;
    } else if (filter == 1) {
        x0 = 0;
        step = 2;
    } else {
        x0 = 1;
        step = 2;
    }

    int oCamera = 0;

    int y0;
    if (heightLimit > yMin)
        y0 = heightLimit;
    else
        y0 = yMin;

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
    std::uint8_t *lightLightRow = hm7_byte_row(pp.lightline, 0);  // per-row lux
    std::uint8_t *reliefRow = hm7_byte_row(pp.lightline, 1);      // relief
    std::uint8_t *ymRow = hm7_byte_row(pp.lightline, 2);          // ym tracking

    // Bootstrap sCmin/sCmax for the surface passes (set once per
    // screen row, when the sInitZoomData gate fires).
    int sCmax = 0, sCmin = 0, sCsl = 0;
    int sCmaxHT2 = 0, sCminHT2 = 0, sCslHT2 = 0;

    // Draws the current surface into the compositing scratch for one
    // screen row, over the columns [sXmin, xMax). Both surface passes
    // below run this same code.
    //
    // `full_cover_test` picks the occlusion test in the inner loop.
    // The column pass keeps the reference plugin's test, which also
    // compares the stored depth. The first-row pass keeps the port's
    // shorter test, which the reference has commented out.
    auto draw_surface_row = [&](int yt, int rYt, int sXmin, int &sInitZoomData, bool full_cover_test) {
        if (!sInitZoomData) {
            std::uint8_t *lp = reliefRow + ((yMax - 1) << 2);
            sCmax = read_u16(lp);
            sCmaxHT2 = read_u16(lp + 2);
            lp = reliefRow + (y0 << 2);
            sCmin = read_u16(lp);
            sCsl = sCmax - sCmin;
            sCminHT2 = read_u16(lp + 2);
            sCslHT2 = sCmaxHT2 - sCminHT2;
            sInitZoomData = 1;
        }

        const int sDx = sScreenX2 - sScreenX1;
        if (!sDx || !sBitmap)
            return;

        const int sDy = sScreenY1 - sScreenY2;
        const int sSlope = (sDy << 7) / sDx;
        const int sXmax = (sScreenX2 > xMax) ? xMax : sScreenX2;

        int sC1, sC2;
        if (sScreenY1 >= yMax) {
            sC1 = sCmin + (sCsl * (sScreenY1 - y0)) / (yMax - 1 - y0);
            if (sScreenY2 < 0 || sScreenY2 >= yMax) {
                sC2 = sCmin + (sCsl * (sScreenY2 - y0)) / (yMax - 1 - y0);
            } else {
                sC2 = read_u16(reliefRow + (sScreenY2 << 2));
            }
        } else {
            sC1 = read_u16(reliefRow + (sScreenY1 << 2));
            if (sScreenY2 < 0) {
                sC2 = sCmin + (sCsl * (sScreenY2 - y0)) / (yMax - 1 - y0);
            } else {
                sC2 = read_u16(reliefRow + (sScreenY2 << 2));
            }
        }
        if (!sC1)
            sC1 = 1;
        if (!sC2)
            sC2 = 1;

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
            if (rYt - sH0 < yMin)
                continue;
            if (!(dx1 + dx2))
                continue;

            int sX;
            if (sInverse) {
                sX = (sDispOffset + (sDispWidth * dx2) / (dx1 + dx2)) << 2;
            } else {
                sX = (sDispOffset + (sDispWidth * dx1) / (dx1 + dx2)) << 2;
            }
            if (sX < 0 || sX >= sRowSize)
                continue;

            int sLux_b = 0, sLux_g = 0, sLux_r = 0, sLux_d = 0;
            int sFYt, sFYth, sHbase;
            if (sH0 < 0) {
                // Above the lightline rows, so scale from the row
                // range. Billboards use depth scale (HT2) whatever
                // the surface type is. The else branch explains why.
                sFYt = sCminHT2 + (sCslHT2 * (yt - sH0 - y0)) / (yMax - 1 - y0);
                sFYth = sFYt;
                sHbase = 0;
            } else {
                std::uint8_t *ll = lightLightRow + ((yt - sH0) << 2);
                sLux_b = ll[0];
                sLux_g = ll[1];
                sLux_r = ll[2];
                sLux_d = ll[3];

                // Sprites are BILLBOARDS. They always face the
                // camera: they rotate with theta but never tilt with
                // alpha. So the on-screen sprite height must scale
                // with DEPTH (the perspective divisor `xp0`) alone,
                // not with the slant angle.
                //
                // `relief[0..1]` holds `a * sinAngle / xp0`, the
                // slant-projected scale. That is correct for vertical
                // WALLS, which shrink at shallow slants, but wrong
                // for billboards: at slant 0 (top-down) it collapses
                // to 0, and at shallow slants it squashes the sprite.
                //
                // `relief[2..3]` holds `(a << 12) / xp0`, the pure
                // depth scale in Q12. That is the right number for
                // billboards, and it matches what players see on
                // Windows: sprites stay full height at every alpha
                // and only rotate with theta.
                //
                // This is where the port leaves the reference. The
                // original picked the pair by surface type, at
                // `sFYt = (sLightlineData[type] << 8) + ...`
                // (MGC_Hmode7_1_4_4.cpp:1073 and :1455), and always
                // took the slant pair for `sFYth`. This port takes
                // the depth pair for both, whatever the type is, so
                // it drops the type from `RenderSurface`. Restoring
                // type-dependent scaling means adding the field back
                // and bringing back the squashed sprites.
                const int depthZoom = read_u16(reliefRow + ((yt - sH0) << 2) + 2);
                sFYt = depthZoom;
                sFYth = depthZoom;
                sHbase = sH0;
            }

            sH0 += (sDh * sFYth) >> 15;
            const int sHend = (sH0 < 0) ? 0 : sH0;
            if (rYt - sH0 < yMin)
                continue;
            const int sRealHeight = (sHeight * sFYt) >> 12;
            if (sRealHeight < 2)
                continue;

            int sHinit;
            if (rYt - sRealHeight - sH0 < yMin) {
                sHinit = rYt - yMin;
            } else {
                sHinit = sRealHeight + sH0;
            }
            const int sFh = ((sHeight - 1) << 10) / (sRealHeight - 1);

            int sHMax;
            if (yt == yMax - 1) {
                sHMax = ysize + oScrY;
            } else {
                sHMax = read_u16(ymRow + (sXt << 2));
            }

            for (int h = sHinit; h > sHend;) {
                --h;
                if (rYt - h > sHMax)
                    break;
                if (rYt - h > yMaxDraw - 1)
                    break;

                // Original source row math (bottom-up DIB):
                //   sData = firstSRow
                //         - (sHeight - 1 - ((h - sH0) * sFh >> 10)) * sRowSize
                //         + sX
                // `firstSRow` pointed at DISPLAY row 0 (the top of
                // the image, at memory offset (sHeight-1)*pitch
                // because the DIB is bottom-up). Subtracting N
                // pitches from it reaches display row N, so the
                // original read display row
                //   N = sHeight - 1 - ((h - sH0) * sFh >> 10)
                // directly. In top-down SDL the display row IS the
                // memory row, so the same N indexes the row we want.
                //
                // A high `h` is the top of the on-screen sprite and
                // must sample row 0 of the bitmap. At h == sHinit:
                //   X = sRealHeight * sFh >> 10 ~= sHeight-1
                //   N = sHeight-1 - (sHeight-1) = 0  (top)
                // A low `h` is the anchor at the foot of the sprite
                // and must sample row sHeight-1. At h == 0:
                //   X = 0, N = sHeight-1  (bottom)
                const int src_row = (sHeight - 1) - ((h - sH0) * sFh >> 10);
                if (src_row < 0 || src_row >= sHeight)
                    continue;
                const std::uint8_t *sData = hm7_byte_row_const(sBitmap, src_row) + sX;
                if (!sData[3])
                    continue;

                // Scratch position. Original:
                //   sScreenData = firstSScreenRow
                //               - (rYt - h) * sScreenRowSize
                //               + (sXt << 3)
                // In top-down SDL the row is just `rYt - h`.
                const int ss_row = rYt - h;
                if (ss_row < 0 || ss_row >= pp.s_screen_bitmap->h)
                    continue;
                std::uint8_t *sScreenData = hm7_byte_row(pp.s_screen_bitmap, ss_row) + (sXt << 3);

                const bool covered = sScreenData[0] && !sScreenData[1] && sScreenData[7] == 255 &&
                                     (!full_cover_test || sScreenData[2] + sScreenData[3] + 2 >= rYt - sHend);
                if (covered)
                    continue;

                int blue = sData[0];
                int green = sData[1];
                int red = sData[2];
                int alpha = sData[3];
                if (sLux_d) {
                    blue += sLux_b;
                    green += sLux_g;
                    red += sLux_r;
                    blue = std::min(blue, 255);
                    green = std::min(green, 255);
                    red = std::min(red, 255);
                } else {
                    blue -= sLux_b;
                    green -= sLux_g;
                    red -= sLux_r;
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
    };

    // The column samples a map position that lies outside the map,
    // and the map does not loop on that axis. Nothing on the ground
    // is drawn, so only a surface pixel already in the scratch can
    // reach the screen here. `ylp` tracks the topmost drawn row.
    auto draw_off_map_column = [&](int xt, int rYt, int ym, std::uint8_t *ylp) {
        if (rYt < ym && rYt < yMaxDraw) {
            std::uint8_t *screenData = hm7_byte_row(pp.screen_bitmap, rYt) + (xt << 2);
            std::uint8_t *sScreenData = hm7_byte_row(pp.s_screen_bitmap, rYt) + (xt << 3);
            if (flush_surface_pixel(screenData, sScreenData)) {
                write_u16(ylp, rYt);
                return;
            }
            screenData[3] = 0;
        }
        if (rYt < ym) {
            write_u16(ylp, rYt);
        }
    };

    // Step to the next surface in the stream.
    auto advance_surface = [&]() {
        ++sIdx;
        if (sIdx < surface_count) {
            load_surface(sIdx);
        } else {
            sNext = 0;
        }
    };

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
        const int h_coeff = read_u16(relief_px);

        const long oy = static_cast<long>(yt) * pp.data_xsize;

        int sInitZoomData = 0;

        // --------------------------------
        //  Surface pass for the first screen row. It draws every
        //  surface that starts at or below this row, so nothing is
        //  missing when the column loop below starts compositing.
        // --------------------------------
        if (yt == yMax - 1) {
            while (sNext && yt <= sScreenY1) {
                // Start column, snapped to the parity the filter
                // draws and clipped to the visible range.
                int sXmin;
                if (x0) {
                    sXmin = (sScreenX1 & 1) ? sScreenX1 : (sScreenX1 + 1);
                } else {
                    sXmin = (sScreenX1 & 1) ? (sScreenX1 + 1) : sScreenX1;
                }
                if (sXmin >= xMax) {
                    sXmin = xMax - 1;
                } else if (sXmin < xMin) {
                    sXmin = xMin + x0;
                }

                draw_surface_row(yt, rYt, sXmin, sInitZoomData, /*full_cover_test=*/false);
                advance_surface();
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
                write_u16(ylp, ym);
            } else {
                ym = read_u16(ylp);
            }

            int xs = pp.data_table[xt + oy] + displayX;
            int ys = pp.data_table[xt + oy + oz] + displayY;

            if (!loopX) {
                if (xs >= mapWidthPx || xs < 0) {
                    draw_off_map_column(xt, rYt, ym, ylp);
                    continue;
                }
            } else {
                if (xs >= mapWidthPx)
                    xs -= mapWidthPx * (xs / mapWidthPx);
                else if (xs < 0)
                    xs -= mapWidthPx * (xs / mapWidthPx - 1);
            }

            if (!loopY) {
                if (ys >= mapHeightPx || ys < 0) {
                    draw_off_map_column(xt, rYt, ym, ylp);
                    continue;
                }
            } else {
                if (ys >= mapHeightPx)
                    ys -= mapHeightPx * (ys / mapHeightPx);
                else
                    while (ys < 0)
                        ys += mapHeightPx;
            }

            // Tile lookup in tilemap.
            const std::int16_t *ptrTileIndex =
                pp.tilemap_data + (ys >> 5) * pp.tilemap_xsize + (xs >> 5) * (nb_layers + 1);
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
            const std::uint8_t *mapTilesetData = hm7_byte_row_const(pp.map_tileset, yts) + (xts << 2);

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
                if (cam > 1)
                    ody_cam = dy;
                else
                    ody_cam = dy - totHA;
                if (ody_cam > oCamera)
                    oCamera = ody_cam;
            }
            int ody = rYt - dy;

            // Surface pass for the column that this surface starts on.
            if (sNext && yt <= sScreenY1 && xt >= sScreenX1) {
                if (xt < sScreenX2) {
                    draw_surface_row(yt, rYt, xt, sInitZoomData, /*full_cover_test=*/true);
                }
                advance_surface();
            }

            if (ym <= ody)
                continue;

            // Wall draw: vertical column from dy up to ym.
            int ground = 0;
            int top_flag = 0;
            int blue, green, red;
            const std::uint8_t *colormapData = nullptr;
            int pos = 0;

            for (int yd = dy; rYt - yd < ym; --yd) {
                if (rYt - yd + 1 - yMaxDraw > 0)
                    break;

                const int screen_row = rYt - yd;
                if (screen_row < 0 || screen_row >= pp.screen_bitmap->h)
                    continue;
                std::uint8_t *screenData = hm7_byte_row(pp.screen_bitmap, screen_row) + (xt << 2);
                std::uint8_t *sScreenData = hm7_byte_row(pp.s_screen_bitmap, screen_row) + (xt << 3);

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
                    screenData[0] = 0;
                    screenData[1] = 0;
                    screenData[2] = 0;
                    screenData[3] = 255;
                    sScreenData[0] = 0;
                } else {
                    if (dy - yd > 0) {
                        // Wall-pixel layer selection. See
                        // hm7_render.h's `WallLayerMode` comment
                        // for why we support two algorithms.
                        int totHA_i = 0;
                        ground = 1;
                        for (int itLayer = nb_layers - 1; itLayer >= 0; --itLayer) {
                            const int threshold = use_top_cumulative ? (totHA_i + hA[itLayer]) : dA[itLayer];
                            if (dy - yd <= threshold) {
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
                                        const std::uint8_t *cmRow = hm7_byte_row_const(pp.colormap, cm_row);
                                        const int oColor = cmRow[xsr << 2];
                                        // Second lookup depends on oColor (direction code).
                                        if (oColor == 32 || oColor == 96) {
                                            const int cm_row2 = ti + xsr;
                                            if (cm_row2 < 0 || cm_row2 >= pp.colormap->h) {
                                                colormapData = nullptr;
                                            } else {
                                                colormapData =
                                                    hm7_byte_row_const(pp.colormap, cm_row2) + (oColor << 2);
                                            }
                                        } else {
                                            colormapData = cmRow + (oColor << 2);
                                        }
                                    }
                                    initA[itLayer] = 1;
                                }
                                if (hA[itLayer] != 0) {
                                    pos = (31 - lA[itLayer] +
                                           ((dy + odyh - yd - totHA_i) * lA[itLayer]) / hA[itLayer])
                                          << 2;
                                } else {
                                    pos = 0;
                                }
                                ground = 0;
                                break;
                            }
                            // Advance accumulator to the next
                            // layer. Under top-cumulative mode we
                            // add only the current layer's own
                            // height. Under bottom-cumulative we
                            // replace with `dA[itLayer]` (the
                            // reference's behaviour - also left
                            // here for completeness even though
                            // this branch almost never fires).
                            if (use_top_cumulative) {
                                totHA_i += hA[itLayer];
                            } else {
                                totHA_i = dA[itLayer];
                            }
                        }
                        if (!ground && colormapData && colormapData[pos + 3]) {
                            blue = colormapData[pos];
                            green = colormapData[pos + 1];
                            red = colormapData[pos + 2];
                        } else {
                            blue = mapTilesetData[0];
                            green = mapTilesetData[1];
                            red = mapTilesetData[2];
                        }
                        top_flag = 0;
                    } else {
                        top_flag = 1;
                        blue = mapTilesetData[0];
                        green = mapTilesetData[1];
                        red = mapTilesetData[2];
                    }

                    if (lux_d) {
                        blue += lux_b;
                        green += lux_g;
                        red += lux_r;
                        if (shadow && (top_flag || ground)) {
                            blue += oShadow;
                            green += oShadow;
                            red += oShadow;
                            blue = clamp_u8(blue);
                            green = clamp_u8(green);
                            red = clamp_u8(red);
                        } else {
                            blue = std::min(blue, 255);
                            green = std::min(green, 255);
                            red = std::min(red, 255);
                        }
                    } else {
                        blue -= lux_b;
                        green -= lux_g;
                        red -= lux_r;
                        if (shadow && (top_flag || ground)) {
                            blue += oShadow;
                            green += oShadow;
                            red += oShadow;
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
            write_u16(ylp, ody);
            if (rYt < yMaxDraw) {
                std::uint8_t *sS = hm7_byte_row(pp.s_screen_bitmap, rYt) + (xt << 3);
                sS[0] = 0;
            }
        }
    }

    // --------------------------------
    //  FINAL OVERDRAW pass
    // --------------------------------
    for (int xt = xMin + x0; xt < xMax; xt += step) {
        std::uint8_t *ylp = ymRow + (xt << 2);
        const int y0min = read_u16(ylp);
        for (int yt = y0min - 1; yt >= yMin; --yt) {
            if (yt < 0 || yt >= pp.screen_bitmap->h)
                continue;
            std::uint8_t *screenData = hm7_byte_row(pp.screen_bitmap, yt) + (xt << 2);
            std::uint8_t *sScreenData = hm7_byte_row(pp.s_screen_bitmap, yt) + (xt << 3);
            if (!flush_surface_pixel(screenData, sScreenData)) {
                screenData[3] = 0;
            }
        }
    }

    return oCamera;
}

}  // namespace hm7
