#include "hm7_library.h"

#include "hm7_apply_lighting.h"
#include "hm7_apply_opacity.h"
#include "hm7_compute_m7.h"
#include "hm7_draw_heightmap.h"
#include "hm7_draw_map_tileset.h"
#include "hm7_draw_textureset.h"
#include "hm7_render.h"

#include <SDL_surface.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace hm7 {
namespace library {
namespace {

constexpr int kAbiVersion = 1;
constexpr int kMapLayers = 3;
constexpr int kPackedStride = kMapLayers + 1;
constexpr int kAutotileSlots = 7;
constexpr int kFirstTilesetTile = 384;
constexpr int kAutotileWidth = 256;
constexpr int kAutotileHeight = 192;
constexpr int kComputeParams = 12;
constexpr int kComboFields = 5;
constexpr int kSurfaceFields = 6;
// RPG Maker XP maps are at most 500 x 500 tiles.
constexpr int kMaxMapSize = 500;
constexpr int kMaxMapPixels = kMaxMapSize * 32;
constexpr int kMaxRenderSize = 4096;
// The only option the game sets. The renderer always takes the sprite
// width from field 2 of the rows data.
constexpr int kOptSurfaceZoomField = 0;

struct SurfaceFree {
    void operator()(SDL_Surface *s) const { SDL_FreeSurface(s); }
};
using Surface = std::unique_ptr<SDL_Surface, SurfaceFree>;

Surface new_surface(int w, int h) {
    return Surface(SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888));
}

void fill_from_rgba(SDL_Surface *dst, const void *src) {
    const std::size_t row = static_cast<std::size_t>(dst->w) * 4;
    for (int y = 0; y < dst->h; ++y) {
        std::memcpy(static_cast<std::uint8_t *>(dst->pixels) + y * dst->pitch,
                    static_cast<const std::uint8_t *>(src) + y * row, row);
    }
}

Surface copy_surface(const SDL_Surface *src) {
    Surface dst = new_surface(src->w, src->h);
    if (!dst)
        return dst;
    for (int y = 0; y < src->h; ++y) {
        std::memcpy(static_cast<std::uint8_t *>(dst->pixels) + y * dst->pitch,
                    static_cast<const std::uint8_t *>(src->pixels) + y * src->pitch,
                    static_cast<std::size_t>(src->w) * 4);
    }
    return dst;
}

struct Texture {
    int basic_index;
    int tile_id;
    Surface image;
};

struct Context {
    int map_w, map_h;
    int render_w, render_h;
    int loop_x, loop_y, camera_mode, less_cut, no_black;

    // render_w x ct_ys x 2: the map x and the map y of each screen pixel.
    std::vector<std::int16_t> computetable;
    int ct_ys;
    // Rows 0 and 1 keep one record for each screen row, so the width
    // covers all the rows that compute_m7 and render_hm7 read.
    Surface rowsdata;
    // The renderer keeps 8 bytes for each column, so it is 2 x render_w.
    Surface s_screen;

    Surface tileset, heightset;
    std::vector<Surface> autotile_frames[kAutotileSlots];
    Surface autotile_hmaps[kAutotileSlots];
    Surface texture_auto;
    std::vector<Texture> textures;
    std::vector<std::int16_t> tiletable;
    int tt_xs, tt_ys;
    std::unordered_map<int, TileEntry> combos;
    std::vector<int> animated;

    // Made by map_build.
    Surface map_tileset;
    Surface textureset;
    std::vector<std::int16_t> heightmap;
    int hm_xs;
};

std::unordered_map<int, Surface> g_images;
std::unordered_map<int, std::unique_ptr<Context>> g_contexts;
int g_next_image = 1;
int g_next_context = 1;
std::string g_last_error;

int fail(const char *message) {
    g_last_error = message;
    return -1;
}

// The functions return error codes, so an allocation that throws must
// not leave them.
template <typename Body> int guarded(const char *out_of_memory, Body body) {
    try {
        return body();
    } catch (const std::bad_alloc &) {
        return fail(out_of_memory);
    }
}

SDL_Surface *find_image(int id) {
    auto it = g_images.find(id);
    return it == g_images.end() ? nullptr : it->second.get();
}

Context *find_context(int id) {
    auto it = g_contexts.find(id);
    return it == g_contexts.end() ? nullptr : it->second.get();
}

std::int32_t packed_int(const void *bytes, long index) {
    std::int32_t v;
    std::memcpy(&v, static_cast<const std::uint8_t *>(bytes) + index * 4, 4);
    return v;
}

// The game's DLL read these values as the tagged Fixnum bit pattern of
// RGSS1, which is 2n + 1. The kernels expect that scale.
int raw_tagged(int n) {
    return static_cast<int>(static_cast<unsigned>(n) << 1) | 1;
}

// At most 256 M pixels, so the byte count fits in an int.
bool valid_size(int w, int h) {
    return w > 0 && h > 0 && static_cast<long long>(w) * h <= (1 << 28);
}

std::size_t rgba_bytes(int w, int h) {
    return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
}

bool autotile_size(const SDL_Surface *s) {
    return s->w == kAutotileWidth && s->h == kAutotileHeight;
}

// The kernels read the map atlas at the row of each tiletable index,
// with 8 tiles in each row of 32 px.
bool tiletable_fits(const std::vector<std::int16_t> &cells, int atlas_rows) {
    for (std::size_t i = 0; i < cells.size(); i += kPackedStride) {
        if (cells[i] < 0 || (cells[i] >> 3) >= atlas_rows)
            return false;
    }
    return true;
}

// The other fields of a cell are the wall texture of each layer.
bool wall_textures_fit(const std::vector<std::int16_t> &cells, int texture_rows) {
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i % kPackedStride != 0 && (cells[i] < 0 || cells[i] >= texture_rows))
            return false;
    }
    return true;
}

}  // namespace

int abi_version() {
    return kAbiVersion;
}

const char *last_error() {
    return g_last_error.c_str();
}

int stats() {
    return static_cast<int>(g_images.size());
}

int image_create(const void *rgba, std::size_t len, int w, int h) {
    return guarded("image_create: out of memory", [&] {
        if (!valid_size(w, h))
            return fail("image_create: bad size");
        if (!rgba)
            return fail("image_create: null pixels");
        if (len < rgba_bytes(w, h))
            return fail("image_create: pixel buffer too small");
        Surface image = new_surface(w, h);
        if (!image)
            return fail("image_create: out of memory");
        fill_from_rgba(image.get(), rgba);
        g_images[g_next_image] = std::move(image);
        return g_next_image++;
    });
}

int image_update(int image, const void *rgba, std::size_t len, int w, int h, int opacity) {
    auto it = g_images.find(image);
    if (it == g_images.end())
        return fail("image_update: unknown image");
    if (!valid_size(w, h))
        return fail("image_update: bad size");
    if (!rgba)
        return fail("image_update: null pixels");
    if (len < rgba_bytes(w, h))
        return fail("image_update: pixel buffer too small");
    if (it->second->w != w || it->second->h != h) {
        Surface resized = new_surface(w, h);
        if (!resized)
            return fail("image_update: out of memory");
        it->second = std::move(resized);
    }
    fill_from_rgba(it->second.get(), rgba);
    apply_opacity(it->second.get(), opacity);
    return 0;
}

int image_free(int image) {
    if (g_images.erase(image) == 0)
        return fail("image_free: unknown image");
    return 0;
}

int ctx_create(int map_w, int map_h, int render_w, int render_h, int loop_x, int loop_y, int camera_mode,
               int less_cut, int no_black) {
    if (map_w <= 0 || map_h <= 0 || map_w > kMaxMapSize || map_h > kMaxMapSize)
        return fail("ctx_create: bad map size");
    // compute_m7 projects the columns in mirrored pairs.
    if (render_w <= 0 || render_w % 2 != 0 || render_h <= 0 || render_w > kMaxRenderSize ||
        render_h > kMaxRenderSize)
        return fail("ctx_create: bad render size");

    return guarded("ctx_create: out of memory", [&] {
        std::unique_ptr<Context> ctx(new Context());
        ctx->map_w = map_w;
        ctx->map_h = map_h;
        ctx->render_w = render_w;
        ctx->render_h = render_h;
        ctx->loop_x = loop_x;
        ctx->loop_y = loop_y;
        ctx->camera_mode = camera_mode;
        ctx->less_cut = less_cut;
        ctx->no_black = no_black;
        ctx->ct_ys = less_cut ? render_h << 1 : render_h;
        ctx->rowsdata = new_surface(std::max(render_w, ctx->ct_ys), 3);
        ctx->s_screen = new_surface(render_w * 2, render_h);
        if (!ctx->rowsdata || !ctx->s_screen)
            return fail("ctx_create: out of memory");
        ctx->computetable.assign(static_cast<std::size_t>(render_w) * ctx->ct_ys * 2, 0);
        g_contexts[g_next_context] = std::move(ctx);
        return g_next_context++;
    });
}

int ctx_destroy(int ctx) {
    if (g_contexts.erase(ctx) == 0)
        return fail("ctx_destroy: unknown context");
    return 0;
}

int set_option(int ctx, int option, int value) {
    if (!find_context(ctx))
        return fail("set_option: unknown context");
    if (option != kOptSurfaceZoomField)
        return fail("set_option: unknown option");
    if (value != 2)
        return fail("set_option: surface_zoom_field must be 2");
    return 0;
}

int map_set_tileset(int ctx_id, int tileset_id, int heightset_id) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_set_tileset: unknown context");
    if (ctx->map_tileset)
        return fail("map_set_tileset: map already built");
    SDL_Surface *tileset = find_image(tileset_id);
    if (!tileset)
        return fail("map_set_tileset: unknown tileset image");
    SDL_Surface *heightset = find_image(heightset_id);
    if (!heightset)
        return fail("map_set_tileset: unknown heightset image");
    if (tileset->w != heightset->w || tileset->h != heightset->h)
        return fail("map_set_tileset: tileset and heightset sizes differ");
    Surface tileset_copy = copy_surface(tileset);
    Surface heightset_copy = copy_surface(heightset);
    if (!tileset_copy || !heightset_copy)
        return fail("map_set_tileset: out of memory");
    ctx->tileset = std::move(tileset_copy);
    ctx->heightset = std::move(heightset_copy);
    return 0;
}

int map_set_autotile(int ctx_id, int slot, int frame, int sheet_id, int heightset_id) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_set_autotile: unknown context");
    if (ctx->map_tileset)
        return fail("map_set_autotile: map already built");
    if (slot < 0 || slot >= kAutotileSlots)
        return fail("map_set_autotile: slot out of range");
    if (frame < 0 || frame > 255)
        return fail("map_set_autotile: frame out of range");
    SDL_Surface *sheet = find_image(sheet_id);
    if (!sheet)
        return fail("map_set_autotile: unknown sheet image");
    if (!autotile_size(sheet))
        return fail("map_set_autotile: sheet must be 256x192");
    SDL_Surface *hmap = heightset_id == 0 ? nullptr : find_image(heightset_id);
    if (heightset_id != 0 && !hmap)
        return fail("map_set_autotile: unknown heightset image");
    if (hmap && !autotile_size(hmap))
        return fail("map_set_autotile: heightset sheet must be 256x192");

    return guarded("map_set_autotile: out of memory", [&] {
        Surface sheet_copy = copy_surface(sheet);
        Surface hmap_copy = hmap ? copy_surface(hmap) : new_surface(kAutotileWidth, kAutotileHeight);
        if (!sheet_copy || !hmap_copy)
            return fail("map_set_autotile: out of memory");
        std::vector<Surface> &frames = ctx->autotile_frames[slot];
        if (frames.size() <= static_cast<std::size_t>(frame))
            frames.resize(frame + 1);
        frames[frame] = std::move(sheet_copy);
        ctx->autotile_hmaps[slot] = std::move(hmap_copy);
        return 0;
    });
}

int map_set_tiletable(int ctx_id, const void *cells, std::size_t len, int xs, int ys) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_set_tiletable: unknown context");
    if (ctx->map_tileset)
        return fail("map_set_tiletable: map already built");
    if (!cells)
        return fail("map_set_tiletable: null data");
    if (xs != ctx->map_w * kPackedStride || ys != ctx->map_h)
        return fail("map_set_tiletable: expected map_w*4 x map_h");
    const std::size_t count = static_cast<std::size_t>(xs) * ys;
    if (len != count * 2)
        return fail("map_set_tiletable: tiletable size mismatch");

    return guarded("map_set_tiletable: out of memory", [&] {
        std::vector<std::int16_t> table(count);
        std::memcpy(table.data(), cells, len);
        ctx->tiletable = std::move(table);
        ctx->tt_xs = xs;
        ctx->tt_ys = ys;
        return 0;
    });
}

int map_set_combos(int ctx_id, const void *records, std::size_t len, int n) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_set_combos: unknown context");
    if (ctx->map_tileset)
        return fail("map_set_combos: map already built");
    if (n < 0 || (!records && n > 0) || len < static_cast<std::size_t>(n) * kComboFields * 4)
        return fail("map_set_combos: bad array");

    return guarded("map_set_combos: out of memory", [&] {
        std::unordered_map<int, TileEntry> combos;
        for (int i = 0; i < n; ++i) {
            const long base = static_cast<long>(i) * kComboFields;
            TileEntry entry = {};
            entry.tile_num = packed_int(records, base);
            if (entry.tile_num < 0)
                return fail("map_set_combos: negative combo index");
            // The layer values come from the int16 tile table of the
            // map, and the bush value is 0 to 3.
            for (int layer = 0; layer < kMapLayers; ++layer) {
                const int value = packed_int(records, base + 1 + layer);
                if (value < 0 || value > INT16_MAX)
                    return fail("map_set_combos: tile value out of range");
                entry.layer_tile_values[layer] = value;
            }
            const int bush = packed_int(records, base + 1 + kMapLayers);
            if (bush < 0 || bush > kMapLayers)
                return fail("map_set_combos: bush value out of range");
            entry.layer_tile_values[kMapLayers] = bush;
            combos[entry.tile_num] = entry;
        }
        ctx->combos = std::move(combos);
        return 0;
    });
}

int map_set_animated(int ctx_id, const void *keys, std::size_t len, int n) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_set_animated: unknown context");
    if (ctx->map_tileset)
        return fail("map_set_animated: map already built");
    if (n < 0 || (!keys && n > 0) || len < static_cast<std::size_t>(n) * 4)
        return fail("map_set_animated: bad array");

    return guarded("map_set_animated: out of memory", [&] {
        std::vector<int> animated(n);
        for (int i = 0; i < n; ++i)
            animated[i] = packed_int(keys, i);
        ctx->animated = std::move(animated);
        return 0;
    });
}

int map_add_texture(int ctx_id, int basic_index, int tile_id, int image_id) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_add_texture: unknown context");
    if (ctx->map_tileset)
        return fail("map_add_texture: map already built");
    if (basic_index < 0)
        return fail("map_add_texture: negative basic index");
    SDL_Surface *image = find_image(image_id);
    if (!image)
        return fail("map_add_texture: unknown image");
    // A regular texture has five 32-pixel wall strips, an autotile one has four.
    if (image->w < (tile_id >= kFirstTilesetTile ? 160 : 128) || image->h < 32)
        return fail("map_add_texture: texture too small (need 160x32 or 128x32)");

    return guarded("map_add_texture: out of memory", [&] {
        Surface copy = copy_surface(image);
        if (!copy)
            return fail("map_add_texture: out of memory");
        ctx->textures.push_back({basic_index, tile_id, std::move(copy)});
        return 0;
    });
}

int map_set_texture_auto(int ctx_id, int image_id) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_set_texture_auto: unknown context");
    if (ctx->map_tileset)
        return fail("map_set_texture_auto: map already built");
    SDL_Surface *image = find_image(image_id);
    if (!image)
        return fail("map_set_texture_auto: unknown image");
    if (!autotile_size(image))
        return fail("map_set_texture_auto: sheet must be 256x192");
    Surface copy = copy_surface(image);
    if (!copy)
        return fail("map_set_texture_auto: out of memory");
    ctx->texture_auto = std::move(copy);
    return 0;
}

int map_build(int ctx_id, int tiles_count, int basic_count, int pattern_id, int lighting) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_build: unknown context");
    if (ctx->map_tileset)
        return fail("map_build: map already built");
    if (!ctx->tileset)
        return fail("map_build: no tileset");
    if (ctx->tiletable.empty())
        return fail("map_build: no tiletable");
    // The atlases have 8 tiles of 32 px in each row.
    if (tiles_count < 0 || tiles_count > (kMaxRenderSize << 3))
        return fail("map_build: bad tiles_count");
    if (basic_count < 0 || basic_count > (kMaxRenderSize << 3))
        return fail("map_build: bad basic_tiles_count");
    SDL_Surface *pattern = pattern_id == 0 ? nullptr : find_image(pattern_id);
    if (pattern_id != 0 && !pattern)
        return fail("map_build: unknown heightpattern image");
    const int atlas_rows = 1 + (tiles_count >> 3);
    if (!tiletable_fits(ctx->tiletable, atlas_rows))
        return fail("map_build: tile index out of range");
    for (const auto &combo : ctx->combos) {
        if ((combo.first >> 3) >= atlas_rows)
            return fail("map_build: combo index out of range");
    }
    const int texture_rows = std::max(basic_count, 1);
    if (!wall_textures_fit(ctx->tiletable, texture_rows))
        return fail("map_build: wall texture index out of range");
    for (const Texture &texture : ctx->textures) {
        if (texture.basic_index >= texture_rows)
            return fail("map_build: texture basic index out of range");
    }

    return guarded("map_build: out of memory", [&] {
        Surface map_tileset = new_surface(512, atlas_rows << 5);
        Surface textureset = new_surface(160, texture_rows << 5);
        Surface flat_pattern;
        if (!pattern) {
            flat_pattern = new_surface(32, 32);
            pattern = flat_pattern.get();
        }
        if (!map_tileset || !textureset || !pattern)
            return fail("map_build: out of memory");

        const int height = ctx->map_h << 5;
        const int hm_xs = ctx->map_w << 6;
        const int hm_ys = height + (height >> 1);
        std::vector<std::int16_t> heightmap(static_cast<std::size_t>(hm_xs) * hm_ys, 0);

        SDL_Surface *auto_tilesets[kAutotileSlots];
        SDL_Surface *auto_heightsets[kAutotileSlots];
        for (int slot = 0; slot < kAutotileSlots; ++slot) {
            const std::vector<Surface> &frames = ctx->autotile_frames[slot];
            auto_tilesets[slot] = frames.empty() ? nullptr : frames[0].get();
            auto_heightsets[slot] = ctx->autotile_hmaps[slot].get();
        }
        for (const auto &combo : ctx->combos) {
            draw_map_tileset_entry(map_tileset.get(), ctx->tileset.get(), ctx->heightset.get(), combo.second,
                                   auto_tilesets, kAutotileSlots, auto_heightsets, kAutotileSlots,
                                   kMapLayers);
        }
        for (const Texture &texture : ctx->textures) {
            draw_textureset_entry(textureset.get(), texture.basic_index, texture.tile_id, texture.image.get(),
                                  ctx->texture_auto.get(), 1, 0);
        }
        draw_heightmap(heightmap.data(), hm_xs, ctx->tiletable.data(), ctx->tt_xs, ctx->tt_ys,
                       map_tileset.get(), pattern, kMapLayers);
        if (lighting)
            apply_lighting(heightmap.data(), hm_xs, hm_ys);

        ctx->map_tileset = std::move(map_tileset);
        ctx->textureset = std::move(textureset);
        ctx->heightmap = std::move(heightmap);
        ctx->hm_xs = hm_xs;
        return 0;
    });
}

int map_animate(int ctx_id, const void *frames_buf, std::size_t len) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("map_animate: unknown context");
    if (!ctx->map_tileset)
        return fail("map_animate: map not built");
    if (!frames_buf || len < static_cast<std::size_t>(kAutotileSlots) * 4)
        return fail("map_animate: null frames");

    // The kernel skips an autotile with no sheet, so a frame past the
    // last one keeps the frame that the atlas has now.
    SDL_Surface *auto_tilesets[kAutotileSlots];
    for (int slot = 0; slot < kAutotileSlots; ++slot) {
        const std::vector<Surface> &frames = ctx->autotile_frames[slot];
        const std::int32_t frame = packed_int(frames_buf, slot);
        const bool known = frame >= 0 && static_cast<std::size_t>(frame) < frames.size();
        auto_tilesets[slot] = known ? frames[frame].get() : nullptr;
    }
    for (int key : ctx->animated) {
        auto it = ctx->combos.find(key);
        if (it == ctx->combos.end())
            continue;
        refresh_map_tileset_entry(ctx->map_tileset.get(), ctx->tileset.get(), it->second, auto_tilesets,
                                  kAutotileSlots, kMapLayers);
    }
    return 0;
}

int compute(int ctx_id, const void *params, std::size_t len, int red, int green, int blue, int alpha) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("compute: unknown context");
    if (!params || len < static_cast<std::size_t>(kComputeParams) * 4)
        return fail("compute: null params");

    // The renderer reads the horizon fade color from the first pixel.
    auto channel = [](int v) { return static_cast<Uint8>(std::min(std::max(v, 0), 255)); };
    SDL_Surface *rows = ctx->rowsdata.get();
    *static_cast<Uint32 *>(rows->pixels) =
        SDL_MapRGBA(rows->format, channel(red), channel(green), channel(blue), channel(alpha));

    ComputeM7Params cp = {};
    cp.cosAngle = raw_tagged(packed_int(params, 0));
    cp.sinAngle = raw_tagged(packed_int(params, 1));
    cp.altitude = packed_int(params, 2);
    cp.pivot = packed_int(params, 3);
    cp.slope = raw_tagged(packed_int(params, 4));
    cp.correction = raw_tagged(packed_int(params, 5));
    cp.heightLimit = packed_int(params, 6);
    cp.cosTheta = raw_tagged(packed_int(params, 7));
    cp.sinTheta = raw_tagged(packed_int(params, 8));
    cp.distProj = packed_int(params, 9);
    cp.zoom = packed_int(params, 10);
    cp.xMin = 0;
    cp.xMax = ctx->render_w;
    cp.yMin = 0;
    cp.yMax = ctx->ct_ys;
    cp.lessCut = 0;
    compute_m7(ctx->computetable.data(), ctx->render_w, ctx->ct_ys, rows, cp);
    return 0;
}

int render(int ctx_id, void *screen_buf, std::size_t len, long bufsize, int height_limit, int display_x,
           int display_y, int filter, int o_scr_y, const void *records, std::size_t records_len, int n) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("render: unknown context");
    if (!ctx->map_tileset)
        return fail("render: map not built");
    const std::size_t screen_bytes = rgba_bytes(ctx->render_w, ctx->render_h);
    if (!screen_buf || len != screen_bytes || bufsize != static_cast<long>(screen_bytes))
        return fail("render: output buffer must be render_w*render_h*4 bytes");
    if (filter < 0 || filter > 2)
        return fail("render: filter must be 0, 1 or 2");
    // The game sends back the offset that render returned, which is never
    // negative. The renderer draws rows from the offset down, and it keeps
    // the bottom row plus the offset in 16 bits.
    if (o_scr_y < 0 || o_scr_y > 0xFFFF - ctx->ct_ys)
        return fail("render: camera offset out of range");
    // The renderer adds the map position to the int16 map coordinates of
    // the projection table.
    if (display_x < -kMaxMapPixels || display_x > kMaxMapPixels || display_y < -kMaxMapPixels ||
        display_y > kMaxMapPixels)
        return fail("render: map position out of range");
    if (n < 0 || (!records && n > 0) || records_len < static_cast<std::size_t>(n) * kSurfaceFields * 4)
        return fail("render: bad surfaces array");

    return guarded("render: out of memory", [&] {
        std::vector<RenderSurface> surfaces;
        surfaces.reserve(n);
        for (int i = 0; i < n; ++i) {
            const long base = static_cast<long>(i) * kSurfaceFields;
            const int image_id = packed_int(records, base + 3);
            // A sprite with no frame yet.
            if (image_id == 0)
                continue;
            SDL_Surface *image = find_image(image_id);
            if (!image)
                return fail("render: surface references an unknown image");
            if (image->w > kMaxRenderSize)
                return fail("render: surface wider than 4096 px");
            if (image->h > kMaxRenderSize)
                return fail("render: surface taller than 4096 px");
            // An upright sprite: both anchor points sit on its foot line.
            const long long screen_x = packed_int(records, base + 1);
            const int screen_y = packed_int(records, base + 2);
            // The game only sends a sprite whose foot is on the screen.
            if (screen_y < 0 || screen_y >= ctx->render_h)
                return fail("render: surface foot outside the screen");
            // The renderer multiplies the altitude by a 16-bit zoom.
            const int altitude = packed_int(records, base + 4);
            if (altitude < INT16_MIN || altitude > INT16_MAX)
                return fail("render: surface altitude out of range");
            const int half = image->w >> 1;
            // The renderer shifts the column offsets of a sprite by 12
            // bits, so a sprite far off the screen would overflow them.
            if (screen_x + half <= 0 || screen_x - half >= ctx->render_w)
                continue;
            RenderSurface s = {};
            s.screen_x1 = static_cast<int>(screen_x - half);
            s.screen_y1 = screen_y;
            s.screen_x2 = static_cast<int>(screen_x + half);
            s.screen_y2 = screen_y;
            s.inverse = 0;
            s.bitmap = image;
            s.dh = altitude;
            s.blend = packed_int(records, base + 5);
            s.disp_width = image->w;
            s.disp_offset = 0;
            surfaces.push_back(s);
        }

        Surface screen(SDL_CreateRGBSurfaceWithFormatFrom(screen_buf, ctx->render_w, ctx->render_h, 32,
                                                          ctx->render_w * 4, SDL_PIXELFORMAT_ABGR8888));
        if (!screen)
            return fail("render: out of memory");

        RenderParams rp = {};
        rp.screen_bitmap = screen.get();
        rp.data_table = ctx->computetable.data();
        rp.data_xsize = ctx->render_w;
        rp.data_ysize_real = ctx->ct_ys;
        rp.lightline = ctx->rowsdata.get();
        rp.heightmap = ctx->heightmap.data();
        rp.heightmap_xsize = ctx->hm_xs;
        rp.map_tileset = ctx->map_tileset.get();
        rp.tilemap_data = ctx->tiletable.data();
        rp.tilemap_xsize = ctx->tt_xs;
        rp.tilemap_ysize = ctx->tt_ys;
        rp.colormap = ctx->textureset.get();
        rp.loop_x = ctx->loop_x;
        rp.loop_y = ctx->loop_y;
        rp.cam = ctx->camera_mode;
        rp.s_screen_bitmap = ctx->s_screen.get();
        rp.less_cut = ctx->less_cut;
        rp.no_black = ctx->no_black;
        rp.x_min = 0;
        rp.x_max = ctx->render_w;
        rp.y_min = 0;
        rp.y_max_draw = ctx->render_h;

        RenderVars rv = {};
        rv.height_limit = height_limit;
        rv.display_x = raw_tagged(display_x);
        rv.display_y = raw_tagged(display_y);
        rv.filter = filter;
        rv.o_scr_y = o_scr_y;

        // The renderer expects zeros in the scratch columns that it did
        // not write this frame.
        std::memset(ctx->s_screen->pixels, 0,
                    static_cast<std::size_t>(ctx->s_screen->pitch) * ctx->s_screen->h);

        // The scripts that call this library come with H-Mode7 V1.2.1,
        // which comes before the V1.4 wall layer rewrite.
        return render_hm7(rp, rv, surfaces.data(), static_cast<int>(surfaces.size()), kMapLayers,
                          WallLayerMode::TopCumulative);
    });
}

int row_zoom(int ctx_id, int y) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("row_zoom: unknown context");
    SDL_Surface *rows = ctx->rowsdata.get();
    if (y < 0 || y >= rows->w)
        return 0;
    // compute_m7 keeps the zoom as a big-endian pair at bytes 2 and 3.
    const std::uint8_t *p = static_cast<const std::uint8_t *>(rows->pixels) + rows->pitch + y * 4;
    return (p[2] << 8) | p[3];
}

int heightmap_get(int ctx_id, int x, int y) {
    Context *ctx = find_context(ctx_id);
    if (!ctx)
        return fail("heightmap_get: unknown context");
    if (ctx->heightmap.empty())
        return fail("heightmap_get: map not built");
    const long hm_ys = static_cast<long>(ctx->heightmap.size()) / ctx->hm_xs;
    if (x < 0 || y < 0 || x >= ctx->hm_xs || y >= hm_ys)
        return 0;
    return ctx->heightmap[x + static_cast<std::size_t>(y) * ctx->hm_xs];
}

}  // namespace library
}  // namespace hm7
