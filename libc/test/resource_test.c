/*
 * Resource tests: os/resource.h and gfx_draw_resource* through the kernel's
 * own resource manager, sprite class, sprite decoder and gfx interpreter
 * (gfx_shim.c). The sprite files are written here byte by byte from the
 * documented format (resources/resource_manager.h, classes/image.h), so
 * the format is checked too, not only the round trip.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <os/gfx.h>
#include <os/resource.h>
#include <os/security.h>

int malloc_check(void);
uint32_t __test_gfx_screen_pixel(int32_t x, int32_t y);
uint32_t __test_gfx_resources_live(void);
void     __test_gfx_resources_drop(void);

static int failures = 0, checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL %s:%d: %s (errno %d %s)\n", __FILE__, __LINE__, #cond, errno, strerror(errno)); } \
} while (0)

#define WHITE   0xFFFFFFFFu
#define KEY     0xFF000000u     // The sprite format's see-through color
#define RED     0xFFFF0000u
#define HGREEN  0x8000FF00u     // Half transparent green
#define BLUE    0xFF0000FFu

static void put16(FILE* f, uint32_t v) { fputc((int)(v & 0xFF), f); fputc((int)((v >> 8) & 0xFF), f); }
static void put32(FILE* f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

// A sprite resource file. `type` and `version` can be wrong on purpose;
// `cut` drops that many bytes off the end (a damaged file).
static void write_sprite(const char* path, uint16_t w, uint16_t h, const uint32_t* palette, uint16_t colors,
                         const uint16_t* indices, uint16_t type, uint16_t version, uint32_t cut) {
    uint32_t payload = 6 + 2 * ((uint32_t)colors * 2 + (uint32_t)w * h);
    FILE* f = fopen(path, "wb");
    if (f == NULL) { printf("cannot write %s\n", path); exit(1); }
    fwrite("RSRC", 1, 4, f);
    put16(f, version);
    put16(f, type);
    put32(f, payload);
    put32(f, 0);
    put16(f, w); put16(f, h); put16(f, colors);
    for (uint16_t i = 0; i < colors; i++)
        put32(f, palette[i]);               // Each color: low word, high word (COLOR())
    for (uint32_t i = 0; i < (uint32_t)w * h; i++)
        put16(f, indices ? indices[i] : 1);
    fclose(f);
    if (cut > 0) {
        struct stat st;
        stat(path, &st);
        truncate(path, (off_t)(st.st_size - cut));
    }
}

static const uint32_t palette[4] = { KEY, RED, HGREEN, BLUE };

// 4 x 3:   K R G B
//          R R R R
//          B K 9 B      (9: past the palette, decodes to transparent 0)
static const uint16_t indices[12] = { 0, 1, 2, 3,  1, 1, 1, 1,  3, 0, 9, 3 };

static void test_load_and_get(void) {
    mkdir("res", 0755);
    mkdir("res/other", 0755);
    write_sprite("res/tile", 4, 3, palette, 4, indices, 1, 1, 0);

    CHECK(resource_get("tile") == -1 && errno == ENOENT);
    CHECK(resource_load("res/tile") == 0);
    int h = resource_get("tile");
    CHECK(h >= 1);

    resource_info_t info;
    CHECK(resource_info(h, &info) == 0 && info.type == RESOURCE_SPRITE && info.width == 4 && info.height == 3);
    CHECK(resource_info(h + 1, &info) == -1 && errno == EBADF);
    CHECK(resource_info(h, (resource_info_t*)16) == -1 && errno == EFAULT);

    // Same file again: fine, same handle. Same name, other file: EEXIST.
    CHECK(resource_load("res/tile") == 0 && resource_get("tile") == h);
    write_sprite("res/other/tile", 2, 2, palette, 4, NULL, 1, 1, 0);
    CHECK(resource_load("res/other/tile") == -1 && errno == EEXIST);

    // Names are the file name, nothing else
    write_sprite("res/icon.v2", 2, 2, palette, 4, NULL, 1, 1, 0);
    CHECK(resource_load("res/icon.v2") == 0 && resource_get("icon.v2") > 0 && resource_get("icon") == -1);
    CHECK(resource_get("res/tile") == -1 && errno == ENOENT);
    CHECK(resource_get("") == -1 && errno == EINVAL);
    char long_name[80];
    memset(long_name, 'n', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    CHECK(resource_get(long_name) == -1 && errno == ENAMETOOLONG);
    char long_path[100] = "res/";
    strcat(long_path, long_name);
    write_sprite(long_path, 1, 1, palette, 4, NULL, 1, 1, 0);
    CHECK(resource_load(long_path) == -1 && errno == ENAMETOOLONG);
    CHECK(resource_load(NULL) == -1 && errno == EFAULT);
}

static void test_bad_files(void) {
    CHECK(resource_load("res/missing") == -1 && errno == ENOENT);
    CHECK(resource_load("res/other") == -1 && errno == EISDIR);

    FILE* f = fopen("res/notes.txt", "w");
    fputs("not a resource at all, just some text", f);
    fclose(f);
    CHECK(resource_load("res/notes.txt") == -1 && errno == ENOEXEC);

    f = fopen("res/tiny", "w");
    fputs("RSRC", f);
    fclose(f);
    CHECK(resource_load("res/tiny") == -1 && errno == ENOEXEC);

    write_sprite("res/wrongtype", 2, 2, palette, 4, NULL, 7, 1, 0);
    CHECK(resource_load("res/wrongtype") == -1 && errno == ENOEXEC);
    write_sprite("res/wrongversion", 2, 2, palette, 4, NULL, 1, 9, 0);
    CHECK(resource_load("res/wrongversion") == -1 && errno == ENOEXEC);
    write_sprite("res/cut", 4, 3, palette, 4, indices, 1, 1, 5);
    CHECK(resource_load("res/cut") == -1 && errno == ENOEXEC);
    write_sprite("res/nopalette", 2, 2, palette, 0, NULL, 1, 1, 0);
    CHECK(resource_load("res/nopalette") == -1 && errno == ENOEXEC);

    // Valid sprite, but wider than any surface may be
    write_sprite("res/wide", 5000, 1, palette, 4, NULL, 1, 1, 0);
    CHECK(resource_load("res/wide") == -1 && errno == ENOEXEC);

    // Nothing above was registered
    CHECK(resource_get("notes.txt") == -1 && resource_get("wide") == -1 && resource_get("cut") == -1);
}

static void test_draw(void) {
    int h = resource_get("tile");
    gfx_surface_t* s = gfx_surface_create(20, 10);
    CHECK(s != NULL && h > 0);

    gfx_clear(s, WHITE);
    gfx_draw_resource(s, h, 2, 1);
    CHECK(gfx_flush(s) == 0);

    // Row 0: K R G B at x = 2..5
    CHECK(gfx_get_pixel(s, 2, 1) == WHITE);                 // Key: see-through
    CHECK(gfx_get_pixel(s, 3, 1) == RED);
    uint32_t g = gfx_get_pixel(s, 4, 1);                    // Half green over white
    CHECK(GFX_GREEN(g) == 0xFF && GFX_RED(g) > 0x60 && GFX_RED(g) < 0xA0 && GFX_ALPHA(g) == 0xFF);
    CHECK(gfx_get_pixel(s, 5, 1) == BLUE);
    // Row 1 all red, row 2: B K (transparent) B
    for (int x = 2; x < 6; x++)
        CHECK(gfx_get_pixel(s, x, 2) == RED);
    CHECK(gfx_get_pixel(s, 2, 3) == BLUE && gfx_get_pixel(s, 3, 3) == WHITE &&
          gfx_get_pixel(s, 4, 3) == WHITE && gfx_get_pixel(s, 5, 3) == BLUE);
    // Nothing outside it
    CHECK(gfx_get_pixel(s, 1, 1) == WHITE && gfx_get_pixel(s, 6, 1) == WHITE && gfx_get_pixel(s, 2, 4) == WHITE);

    // The surface's own blend mode is untouched (COPY): a fill with a
    // translucent color still replaces
    gfx_fill_rect(s, 10, 0, 1, 1, 0x40112233);
    CHECK(gfx_get_pixel(s, 10, 0) == 0x40112233);

    // Part of it, clipped by the surface edge, through origin and clip
    gfx_clear(s, WHITE);
    gfx_rect_t part = gfx_rect(1, 1, 3, 2);                 // R R R / K 9 B
    gfx_draw_resource_rect(s, h, &part, 18, 8);             // Only 2 x 2 of it fits
    CHECK(gfx_flush(s) == 0);
    CHECK(gfx_get_pixel(s, 18, 8) == RED && gfx_get_pixel(s, 19, 8) == RED);
    CHECK(gfx_get_pixel(s, 18, 9) == WHITE && gfx_get_pixel(s, 19, 9) == WHITE);
    gfx_draw_resource_rect(s, h, NULL, 0, 0);
    gfx_set_origin(s, 10, 0);
    gfx_rect_t clip = gfx_rect(0, 0, 2, 10);
    gfx_set_clip(s, &clip);
    gfx_draw_resource(s, h, 0, 0);
    gfx_set_clip(s, NULL);
    gfx_set_origin(s, 0, 0);
    CHECK(gfx_flush(s) == 0);
    CHECK(gfx_get_pixel(s, 1, 0) == RED && gfx_get_pixel(s, 11, 0) == RED && gfx_get_pixel(s, 12, 1) == WHITE);

    // A bad handle is reported by the flush, the rest of the batch still runs
    gfx_clear(s, WHITE);
    gfx_draw_resource(s, 63, 0, 0);
    gfx_draw_resource(s, h, 0, 0);
    CHECK(gfx_flush(s) == -1 && errno == EBADF);
    CHECK(gfx_get_pixel(s, 1, 0) == RED);

    // Onto the screen
    gfx_clear(s, WHITE);
    gfx_draw_resource(s, h, 0, 0);
    CHECK(gfx_present(s, 100, 50) == 0);
    CHECK(__test_gfx_screen_pixel(101, 50) == RED && __test_gfx_screen_pixel(100, 50) == WHITE &&
          __test_gfx_screen_pixel(103, 50) == BLUE);

    gfx_surface_destroy(s);
}

static void test_limits_and_security(void) {
    // Fill the table: RESOURCE_MAX in all
    int loaded = (int)__test_gfx_resources_live(), n = 0;
    char path[32];
    for (int i = 0; i < RESOURCE_MAX + 2; i++) {
        snprintf(path, sizeof(path), "res/s%02d", i);
        write_sprite(path, 1, 1, palette, 4, NULL, 1, 1, 0);
        if (resource_load(path) == 0) n++;
        else break;
    }
    CHECK(loaded + n == RESOURCE_MAX && errno == EMFILE);
    CHECK(resource_load("res/tile") == 0);                  // Already loaded is still fine

    // Load needs both bits, get needs gfx
    CHECK(sec_set(SEC_ALL & ~SEC_FILE_READ) == 0);
    CHECK(resource_load("res/s00") == -1 && errno == EPERM);
    CHECK(resource_get("tile") > 0);
    CHECK(sec_set(SEC_ALL & ~SEC_GFX) == 0);
    CHECK(resource_get("tile") == -1 && errno == EPERM);
    CHECK(sec_set(SEC_ALL) == 0);

    __test_gfx_resources_drop();
    CHECK(__test_gfx_resources_live() == 0 && resource_get("tile") == -1);
}

int main(void) {
    test_load_and_get();
    test_bad_files();
    test_draw();
    test_limits_and_security();
    CHECK(malloc_check() == 0);
    printf("resources: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
