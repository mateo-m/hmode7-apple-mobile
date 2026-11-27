// library_test.cpp: drives the hm7_* library the way the game scripts
// that use it do, and checks the pixels that come out.
//
//   scripts/test.sh

#include "hm7_library.h"

#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace hm7::library;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;
constexpr int kMapW = 20;
constexpr int kMapH = 15;
constexpr int kDistanceH = 480;
constexpr int kPivot = 240;

struct Rgba {
    int r, g, b, a;
    bool operator==(const Rgba &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};
constexpr Rgba kRed = {255, 0, 0, 255};
constexpr Rgba kBlue = {0, 0, 255, 255};

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string &what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

void check_equal(long expected, long actual, const std::string &what) {
    check(expected == actual,
          what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

std::string text(const Rgba &c) {
    return "[" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," +
           std::to_string(c.a) + "]";
}

void check_pixel(const Rgba &expected, const Rgba &actual, const std::string &what) {
    check(expected == actual, what + ": expected " + text(expected) + ", got " + text(actual));
}

void check_error(const char *expected, const std::string &what) {
    check(std::strcmp(expected, last_error()) == 0,
          what + ": expected \"" + expected + "\", got \"" + last_error() + "\"");
}

std::vector<std::uint8_t> pixels(int w, int h, const Rgba &c) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = static_cast<std::uint8_t>(c.r);
        px[i + 1] = static_cast<std::uint8_t>(c.g);
        px[i + 2] = static_cast<std::uint8_t>(c.b);
        px[i + 3] = static_cast<std::uint8_t>(c.a);
    }
    return px;
}

int image(int w, int h, const Rgba &c) {
    const std::vector<std::uint8_t> px = pixels(w, h, c);
    return image_create(px.data(), px.size(), w, h);
}

// The values of HM7::Tilemap#refresh_alpha with the defaults of
// Game_System#hm7_reset, at 100 % zoom.
std::vector<std::int32_t> parameters(double alpha) {
    const double rad = M_PI * alpha / 180;
    const int cos_alpha = static_cast<int>(2048 * std::cos(rad));
    const int sin_alpha = static_cast<int>(2048 * std::sin(rad));
    const int depth = kDistanceH << 11;
    const int h0 = (-kDistanceH * kPivot * cos_alpha) / (depth + kPivot * sin_alpha) + kPivot;
    const double z0 = static_cast<double>(depth) / (depth + kPivot * sin_alpha);
    const double slope = (1.0 - z0) / (kPivot - h0);
    const double corrective = 1.0 - kPivot * slope;
    const int last_line = -kPivot - 960;
    int height_limit = (kDistanceH * last_line * cos_alpha) / (depth - last_line * sin_alpha) + kPivot;
    if (height_limit < 0)
        height_limit = 0;
    return {cos_alpha,
            sin_alpha,
            kDistanceH,
            kPivot,
            static_cast<std::int32_t>(131072 * slope),
            static_cast<std::int32_t>(131072 * corrective),
            height_limit,
            2048,
            0,
            0,
            4096,
            0};
}

struct Map {
    int map_w = kMapW;
    int map_h = kMapH;
    int render_w = kScreenW;
    int render_h = kScreenH;
    int loop_y = 0;
};

// A map of tileset tile 384 on every cell: one tiles combination, flat.
// The calls come in the order of HM7::Tilemap#initialize.
int build_map(const Map &m) {
    const int ctx = ctx_create(m.map_w, m.map_h, m.render_w, m.render_h, 0, m.loop_y, 0, 0, 0);
    set_option(ctx, 0, 2);
    const int tileset = image(256, 32, kRed);
    const int heightset = image(256, 32, {0, 0, 0, 0});
    map_set_tileset(ctx, tileset, heightset);
    image_free(tileset);
    image_free(heightset);
    const std::vector<std::int16_t> table(static_cast<std::size_t>(m.map_w) * 4 * m.map_h, 0);
    map_set_tiletable(ctx, table.data(), table.size() * 2, m.map_w * 4, m.map_h);
    const std::int32_t combo[] = {0, 384, 0, 0, 1};
    map_set_combos(ctx, combo, sizeof combo, 1);
    map_set_animated(ctx, nullptr, 0, 0);
    const int texture_auto = image(256, 192, {0, 0, 0, 0});
    map_set_texture_auto(ctx, texture_auto);
    image_free(texture_auto);
    check_equal(0, map_build(ctx, 1, 1, 0, 0), std::string("map_build: ") + last_error());
    return ctx;
}

int build_map() {
    return build_map(Map());
}

int run_compute(int ctx, double alpha) {
    const std::vector<std::int32_t> p = parameters(alpha);
    return compute(ctx, p.data(), p.size() * 4, 0, 0, 255, 1);
}

struct Frame {
    int ret;
    int w;
    std::vector<std::uint8_t> buf;

    Rgba at(int x, int y) const {
        const std::uint8_t *p = &buf[(static_cast<std::size_t>(y) * w + x) * 4];
        return {p[0], p[1], p[2], p[3]};
    }
};

Frame run_render(int ctx, double alpha, const std::vector<std::int32_t> &surfaces = {}, int w = kScreenW,
                 int h = kScreenH) {
    Frame f = {0, w, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4)};
    const int n = static_cast<int>(surfaces.size() / 6);
    f.ret = render(ctx, f.buf.data(), f.buf.size(), static_cast<long>(f.buf.size()), parameters(alpha)[6], 0,
                   0, 0, 0, surfaces.empty() ? nullptr : surfaces.data(), surfaces.size() * 4, n);
    return f;
}

// The horizon fade of the game is additive blue (Color.new(0, 0, 255, 1)).
// Seen from above, compute_m7 makes it 255 * (pivot - y) / 960 on each
// row over the pivot.
int fade_from_above(int y) {
    return 255 * (kPivot - y) / 960;
}

void test_abi_version() {
    check_equal(1, abi_version(), "abi_version");
}

void test_flat_map() {
    const int ctx = build_map();
    check(ctx > 0, "ctx_create returned " + std::to_string(ctx));
    check_equal(0, run_compute(ctx, 0), "compute");
    const Frame f = run_render(ctx, 0);
    check(f.ret >= 0, std::string("render: ") + last_error());
    // The zoom of 4108 / 4096 puts the last row just past the map.
    for (int y : {kPivot, 300, kScreenH - 2})
        check_pixel(kRed, f.at(320, y), "flat map under the pivot, row " + std::to_string(y));
    for (int y : {0, 100, kPivot - 1}) {
        const Rgba faded = {255, 0, fade_from_above(y), 255};
        check_pixel(faded, f.at(320, y), "flat map over the pivot, row " + std::to_string(y));
    }
    ctx_destroy(ctx);
}

void test_slanted_map() {
    // At 60 degrees the bottom screen row looks past a 15-tile map, so
    // the map loops vertically.
    Map m;
    m.loop_y = 1;
    const int ctx = build_map(m);
    run_compute(ctx, 60);
    const Frame f = run_render(ctx, 60);
    check(f.ret >= 0, std::string("render: ") + last_error());
    check_pixel(kRed, f.at(320, kScreenH - 1), "ground under the camera");
    check(!(f.at(320, 0) == kRed), "the top row is not the ground: " + text(f.at(320, 0)));
    ctx_destroy(ctx);
}

// compute_m7 packs the zoom of each row as (a << 12) / xp0, with
// xp0 = zoom * ((a << 18) / (slope * y + corrective)) >> 12, and the
// slope and corrective values in the doubled scale of the DLL.
int expected_row_zoom(double alpha, int y, int render_w = kScreenW) {
    const std::vector<std::int32_t> p = parameters(alpha);
    const int a = render_w >> 1;
    const int slope = (p[4] << 1) | 1;
    const int corrective = (p[5] << 1) | 1;
    const int xp0 = p[10] * ((a << 18) / (slope * y + corrective)) >> 12;
    return (a << 12) / xp0;
}

void test_row_zoom() {
    const int ctx = build_map();
    run_compute(ctx, 0);
    const int zoom = row_zoom(ctx, kPivot);
    check_equal(expected_row_zoom(0, kPivot), zoom, "row_zoom of the pivot row at 0 degrees");
    // The integer steps of compute_m7 make 100 % come out a little
    // over 4096.
    check(zoom > 4096 && zoom < 4096 * 101 / 100, "row_zoom near 4096: " + std::to_string(zoom));
    run_compute(ctx, 60);
    check_equal(expected_row_zoom(60, 400), row_zoom(ctx, 400), "row_zoom of row 400 at 60 degrees");
    check_equal(0, row_zoom(ctx, -1), "row_zoom over the top");
    ctx_destroy(ctx);
}

void test_tall_narrow_screen() {
    // The rows data has one record for each screen row, also when the
    // screen is narrower than it is tall.
    Map m;
    m.render_w = 64;
    m.render_h = 480;
    const int ctx = build_map(m);
    check_equal(0, run_compute(ctx, 0), "compute");
    const Frame f = run_render(ctx, 0, {}, 64, 480);
    check(f.ret >= 0, std::string("render: ") + last_error());
    check_pixel(kRed, f.at(32, 478), "low row of a 64 x 480 screen");
    check_equal(expected_row_zoom(0, 479, 64), row_zoom(ctx, 479), "row_zoom of row 479");
    ctx_destroy(ctx);
}

void test_sprite() {
    const int ctx = build_map();
    run_compute(ctx, 0);
    const int sprite = image(32, 48, kBlue);
    const Frame f = run_render(ctx, 0, {0, 320, 300, sprite, 0, 0});
    check(f.ret >= 0, std::string("render: ") + last_error());
    check_pixel(kBlue, f.at(320, 290), "pixel in the sprite");
    check_pixel(kRed, f.at(100, 400), "pixel away from the sprite");
    image_free(sprite);
    ctx_destroy(ctx);
}

void test_many_sprites() {
    const int ctx = build_map();
    run_compute(ctx, 0);
    const int sprite = image(8, 8, kBlue);
    std::vector<std::int32_t> records;
    for (int row = 3; row >= 0; --row) {
        for (int column = 0; column < 80; ++column) {
            const std::int32_t record[] = {0, 4 + column * 8, 260 + row * 40, sprite, 0, 0};
            records.insert(records.end(), record, record + 6);
        }
    }
    const Frame f = run_render(ctx, 0, records);
    check(f.ret >= 0, std::string("render with 320 sprites: ") + last_error());
    check_pixel(kBlue, f.at(4 + 79 * 8, 260 - 2), "the last of 320 sprites");
    image_free(sprite);
    ctx_destroy(ctx);
}

// The game sets up each map once, before map_build. A new map input
// after it would not match the heightmap and the atlas of the build.
void test_setters_after_build() {
    const std::vector<std::int16_t> table(kMapW * 4 * kMapH, 0);
    const std::int32_t combo[] = {0, 384, 0, 0, 1};
    const int texture = image(160, 32, kBlue);
    const int sheet = image(256, 192, kBlue);
    const int tileset = image(256, 32, kBlue);
    const struct {
        const char *name;
        std::function<int(int)> call;
    } setters[] = {
        {"map_set_tiletable",
         [&](int ctx) { return map_set_tiletable(ctx, table.data(), table.size() * 2, kMapW * 4, kMapH); }},
        {"map_set_combos", [&](int ctx) { return map_set_combos(ctx, combo, sizeof combo, 1); }},
        {"map_set_animated", [&](int ctx) { return map_set_animated(ctx, nullptr, 0, 0); }},
        {"map_set_tileset", [&](int ctx) { return map_set_tileset(ctx, tileset, tileset); }},
        {"map_set_autotile", [&](int ctx) { return map_set_autotile(ctx, 0, 0, sheet, 0); }},
        {"map_add_texture", [&](int ctx) { return map_add_texture(ctx, 0, 384, texture); }},
        {"map_set_texture_auto", [&](int ctx) { return map_set_texture_auto(ctx, sheet); }},
        {"map_build", [&](int ctx) { return map_build(ctx, 1, 1, 0, 0); }},
    };
    const int ctx = build_map();
    run_compute(ctx, 0);
    for (const auto &setter : setters) {
        check_equal(-1, setter.call(ctx), std::string(setter.name) + " on a built map");
        check_error((std::string(setter.name) + ": map already built").c_str(),
                    std::string(setter.name) + " on a built map");
    }
    const Frame f = run_render(ctx, 0);
    check(f.ret >= 0, std::string("render after the refused calls: ") + last_error());
    check_pixel(kRed, f.at(320, 300), "the built map after the refused calls");
    ctx_destroy(ctx);
    image_free(texture);
    image_free(sheet);
    image_free(tileset);
}

void test_unknown_ids() {
    check_equal(-1, row_zoom(9999, 0), "row_zoom");
    check_error("row_zoom: unknown context", "row_zoom");
    check_equal(-1, heightmap_get(9999, 0, 0), "heightmap_get");
    check_error("heightmap_get: unknown context", "heightmap_get");
    check_equal(-1, image_free(9999), "image_free");
    check_error("image_free: unknown image", "image_free");

    const int ctx = build_map();
    run_compute(ctx, 0);
    const Frame f = run_render(ctx, 0, {0, 320, 300, 9999, 0, 0});
    check_equal(-1, f.ret, "render");
    check_error("render: surface references an unknown image", "render");
    ctx_destroy(ctx);
}

void test_bad_input() {
    const int ctx = ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0);
    check_equal(-1, heightmap_get(ctx, 0, 0), "heightmap_get before map_build");
    check_error("heightmap_get: map not built", "heightmap_get before map_build");

    const std::vector<std::int16_t> table(kMapW * 4 * kMapH, 0);
    check_equal(-1, map_set_tiletable(ctx, table.data(), table.size() * 2 - 2, kMapW * 4, kMapH),
                "short table");
    check_error("tiletable size mismatch", "short table");
    check_equal(-1, map_set_tiletable(ctx, table.data(), table.size() * 2, kMapW, kMapH), "narrow table");
    check_error("map_set_tiletable: expected map_w*4 x map_h", "narrow table");

    const int small = image(32, 32, kRed);
    check_equal(-1, map_set_autotile(ctx, 0, 0, small, 0), "small autotile");
    check_error("map_set_autotile: sheet must be 256x192", "small autotile");
    check_equal(-1, map_set_autotile(ctx, 7, 0, small, 0), "autotile slot 7");
    check_error("map_set_autotile: slot out of range", "autotile slot 7");
    check_equal(-1, map_set_texture_auto(ctx, small), "small texture_auto");
    check_error("map_set_texture_auto: sheet must be 256x192", "small texture_auto");
    check_equal(-1, map_add_texture(ctx, 0, 384, small), "small texture");
    check_error("map_add_texture: texture too small (need 160x32 or 128x32)", "small texture");
    check_equal(-1, map_add_texture(ctx, -1, 384, small), "negative basic index");
    check_error("map_add_texture: negative basic index", "negative basic index");
    image_free(small);
    // A regular wall texture has five 32-pixel strips, an autotile one has four.
    const int four_strips = image(128, 32, kRed);
    check_equal(-1, map_add_texture(ctx, 0, 384, four_strips), "128-pixel regular texture");
    check_error("map_add_texture: texture too small (need 160x32 or 128x32)", "128-pixel regular texture");
    check_equal(0, map_add_texture(ctx, 0, 48, four_strips), "128-pixel autotile texture");
    image_free(four_strips);

    check_equal(-1, ctx_create(kMapW, kMapH, 641, kScreenH, 0, 0, 0, 0, 0), "odd render width");
    check_error("ctx_create: bad render size", "odd render width");

    const std::int32_t combo[] = {-1, 384, 0, 0, 1};
    check_equal(-1, map_set_combos(ctx, combo, sizeof combo, 1), "negative combo");
    check_error("map_set_combos: negative combo index", "negative combo");
    check_equal(-1, map_set_combos(ctx, combo, sizeof combo, 2), "short combos");
    check_error("map_set_combos: bad array", "short combos");
    check_equal(-1, set_option(ctx, 0, 1), "set_option 1");
    check_error("set_option: surface_zoom_field must be 0 or 2", "set_option 1");
    ctx_destroy(ctx);

    const int built = build_map();
    run_compute(built, 0);
    std::vector<std::uint8_t> buf(kScreenW * kScreenH * 4);
    // The renderer starts its rows at the camera offset.
    check_equal(-1, render(built, buf.data(), buf.size(), buf.size(), 0, 0, 0, 0, -1, nullptr, 0, 0),
                "negative camera offset");
    check_error("render: negative camera offset", "negative camera offset");
    check_equal(-1, render(built, buf.data(), buf.size(), buf.size(), 0, 0, 0, 3, 0, nullptr, 0, 0),
                "filter 3");
    check_error("render: filter must be 0, 1 or 2", "filter 3");
    check_equal(-1, render(built, buf.data(), buf.size() - 4, buf.size() - 4, 0, 0, 0, 0, 0, nullptr, 0, 0),
                "short buffer");
    check_error("render: output buffer must be render_w*render_h*4 bytes", "short buffer");
    check_equal(-1, compute(built, nullptr, 0, 0, 0, 0, 0), "compute with no params");
    check_error("compute: null params", "compute with no params");
    check_equal(0, heightmap_get(built, 5, 5), "heightmap_get of a flat map");
    ctx_destroy(built);

    // A tiletable index past the map atlas would make the kernels read
    // outside it.
    std::vector<std::int16_t> far(kMapW * 4 * kMapH, 0);
    far[0] = 100;
    const int unbuilt = ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0);
    const int tileset = image(256, 32, kRed);
    map_set_tileset(unbuilt, tileset, tileset);
    image_free(tileset);
    map_set_tiletable(unbuilt, far.data(), far.size() * 2, kMapW * 4, kMapH);
    check_equal(-1, map_build(unbuilt, 1, 1, 0, 0), "map_build with a far index");
    check_error("map_build: tile index out of range", "map_build with a far index");
    ctx_destroy(unbuilt);
}

// SDL takes its allocations from these, so a test can make the large
// ones fail. SDL needs its small allocations to store the error text.
SDL_malloc_func g_malloc;
SDL_calloc_func g_calloc;
SDL_realloc_func g_realloc;
SDL_free_func g_free;
bool g_out_of_memory = false;
constexpr std::size_t kLargeAllocation = 4096;

bool refuse(std::size_t size) {
    return g_out_of_memory && size > kLargeAllocation;
}
void *test_malloc(std::size_t size) {
    return refuse(size) ? nullptr : g_malloc(size);
}
void *test_calloc(std::size_t n, std::size_t size) {
    return refuse(n * size) ? nullptr : g_calloc(n, size);
}
void *test_realloc(void *p, std::size_t size) {
    return refuse(size) ? nullptr : g_realloc(p, size);
}

void test_out_of_memory() {
    const std::vector<std::uint8_t> px = pixels(64, 64, kRed);
    const int img = image_create(px.data(), px.size(), 64, 64);
    const int live = stats();

    g_out_of_memory = true;
    check_equal(-1, image_create(px.data(), px.size(), 64, 64), "image_create with no memory");
    check_error("image_create: out of memory", "image_create with no memory");
    const std::vector<std::uint8_t> bigger = pixels(128, 128, kBlue);
    check_equal(-1, image_update(img, bigger.data(), bigger.size(), 128, 128, 255),
                "image_update with no memory");
    check_error("image_update: out of memory", "image_update with no memory");
    check_equal(-1, ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0), "ctx_create with no memory");
    check_error("ctx_create: out of memory", "ctx_create with no memory");
    g_out_of_memory = false;

    check_equal(live, stats(), "live images after the failed calls");
    // The image still works after the failed update.
    check_equal(0, image_update(img, px.data(), px.size(), 64, 64, 255), "image_update at the old size");
    image_free(img);
}

}  // namespace

int main() {
    SDL_GetOriginalMemoryFunctions(&g_malloc, &g_calloc, &g_realloc, &g_free);
    SDL_SetMemoryFunctions(test_malloc, test_calloc, test_realloc, g_free);

    test_abi_version();
    test_flat_map();
    test_slanted_map();
    test_row_zoom();
    test_tall_narrow_screen();
    test_sprite();
    test_many_sprites();
    test_setters_after_build();
    test_unknown_ids();
    test_bad_input();
    test_out_of_memory();

    std::printf("library_test: %d of %d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
