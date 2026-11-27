// Some games replace MGC_Hmode7.dll with their own H-Mode7 library
// (hmode7.dll, libhmode7.so) that keeps each map in a context. Their
// scripts call these functions through Win32API, one for each hm7_*
// export, with the same arguments in the same order.
//
// Images and contexts have integer ids. A pixel buffer is RGBA bytes,
// row by row, with no padding. A packed buffer is little-endian int32
// ("l<*") or int16 ("s<*") values. A negative return is an error, and
// last_error() gives its text. heightmap_get is the exception: a height
// can be negative.

#ifndef HM7_LIBRARY_H
#define HM7_LIBRARY_H

#include <cstddef>

namespace hm7 {
namespace library {

int abi_version();
const char *last_error();
// The number of live images.
int stats();

int image_create(const void *rgba, std::size_t len, int w, int h);
// Copies new pixels into the image, then multiplies their alpha by
// the opacity. The image takes the new size.
int image_update(int image, const void *rgba, std::size_t len, int w, int h, int opacity);
int image_free(int image);

// render_w is even.
int ctx_create(int map_w, int map_h, int render_w, int render_h, int loop_x, int loop_y, int camera_mode,
               int less_cut, int no_black);
int ctx_destroy(int ctx);
int set_option(int ctx, int option, int value);

// Each setter copies its images, so the caller can free them at once.
// The setters and map_build refuse a context that is already built.
int map_set_tileset(int ctx, int tileset, int heightset);
// A heightset of 0 makes the autotile flat.
int map_set_autotile(int ctx, int slot, int frame, int sheet, int heightset);
// int16 cells, 4 for each map cell: 3 layers and the height.
int map_set_tiletable(int ctx, const void *cells, std::size_t len, int xs, int ys);
// n records of 5 int32: the combination key and its 4 layer values.
int map_set_combos(int ctx, const void *records, std::size_t len, int n);
// n int32 combination keys that hold an animated autotile.
int map_set_animated(int ctx, const void *keys, std::size_t len, int n);
// 160 pixels wide for a tileset tile (tile_id >= 384), 128 for an autotile.
int map_add_texture(int ctx, int basic_index, int tile_id, int image);
int map_set_texture_auto(int ctx, int image);
// A pattern of 0 gives flat heights.
int map_build(int ctx, int tiles_count, int basic_count, int pattern, int lighting);
// 7 int32 autotile frame numbers.
int map_animate(int ctx, const void *frames, std::size_t len);

// 12 int32 values of HM7::Tilemap#refresh_alpha, and the horizon fade
// color.
int compute(int ctx, const void *params, std::size_t len, int red, int green, int blue, int alpha);
// Draws into `screen`, an RGBA buffer of render_w x render_h that the
// caller keeps between frames. Each surface record is 6 int32: type,
// screen x, screen y (the foot line), image (0 skips it), altitude
// and blend. The records come lowest foot line first, then left to
// right, as the game sorts them. o_scr_y is the camera Y offset that the
// last render returned, or 0. Returns the camera Y offset.
int render(int ctx, void *screen, std::size_t len, long bufsize, int height_limit, int display_x,
           int display_y, int filter, int o_scr_y, const void *surfaces, std::size_t surfaces_len, int n);
// The zoom of a screen row, in 1/4096. 0 outside the rows.
int row_zoom(int ctx, int y);
// The height at a heightmap point. 0 outside the map.
int heightmap_get(int ctx, int x, int y);

}  // namespace library
}  // namespace hm7

#endif
