// hm7_pixels.h — pixel layout + row access primitives.
//
// The original Windows plugin operates on bottom-up BGRA DIBs. mkxp-z
// on Apple mobile targets stores Bitmap data as SDL surfaces whose byte
// order is little-endian ABGR32 (`SDL_PIXELFORMAT_ABGR8888`), which in
// memory is R, G, B, A. Rows grow top-down.
//
// This header hides the difference from the port body: every function
// reads/writes through `hm7_pixel` (a struct matching mkxp-z's layout)
// and `hm7_row_ptr` (a top-down row getter). The math inside the port
// stays identical to the original; only the names change.
//
// The translation rules applied when reading the Windows source:
//
//   Windows (BGRA, bottom-up)          iOS port (RGBA, top-down)
//   --------------------------         -------------------------
//   data[0]  (blue)                    pixel.b
//   data[1]  (green)                   pixel.g
//   data[2]  (red)                     pixel.r
//   data[3]  (alpha)                   pixel.a
//   firstRow - y*pitch                 hm7_row_ptr(bmp, y)

#ifndef HM7_PIXELS_H
#define HM7_PIXELS_H

#include <cstdint>
#include <SDL_surface.h>

namespace hm7 {

// 4-byte pixel matching mkxp-z's Apple/Android ABGR8888 byte layout.
// Named b/g/r/a so the original BGRA-ordered source reads naturally.
struct Pixel {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

static_assert(sizeof(Pixel) == 4, "Pixel must be 4 bytes");

// Row accessor. Returns a pointer to the first pixel of row `y` in
// top-down order (row 0 = top of image). `pitch` is in bytes; mkxp-z
// SDL surfaces use `surface->pitch` which is typically `width*4` with
// occasional padding.
inline Pixel *hm7_row_ptr(SDL_Surface *surf, int y) {
    return reinterpret_cast<Pixel *>(
        static_cast<std::uint8_t *>(surf->pixels) + y * surf->pitch);
}

inline const Pixel *hm7_row_ptr_const(const SDL_Surface *surf, int y) {
    return reinterpret_cast<const Pixel *>(
        static_cast<const std::uint8_t *>(surf->pixels) + y * surf->pitch);
}

}  // namespace hm7

#endif  // HM7_PIXELS_H
