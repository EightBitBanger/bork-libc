
/*
 * Test-only: the kernel's graphics syscalls on a development machine.
 *
 * Built from the kernel's own sources: gfx/gfx_draw.c, gfx_surface.c,
 * gfx_text.c and syscall/calls/sys_gfx_core.h (the command interpreter
 * SYS_GFX_SUBMIT runs), with the few kernel headers they need stubbed in
 * test/kstub. Only the process table, budgets and address-space checks of
 * sys_gfx.h are re-done here, in simplified form.
 *
 * Resources (SYS_RES_*) go through the kernel's own resource manager,
 * sprite class and sprite decoder (resources/, dwm/rendering/sprite.c), on
 * a VFS stand-in over host files; only sys_resource.h's table handling is
 * re-done here. The "screen" is a
 * GFX_SHIM_W x GFX_SHIM_H surface that tests can read back.
 */

#include <stdint.h>
#include <stdbool.h>
#include <kernel/syscall/calls/sys_gfx_core.h>
#include <kernel/resources/resource_manager.h>
#include <kernel/resources/classes/image.h>
#include <kernel/vfs/vfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define GFX_SHIM_W  640
#define GFX_SHIM_H  480

static SysGfxSurface* surfaces[SYS_GFX_SURFACE_MAX];
static SysGfxResource* resources[SYS_RES_MAX];
static GfxSurface     screen;
static uint32_t       cmd_buffer[SYS_GFX_SUBMIT_MAX / 4];

// Host pointers are all "user memory" (Linux puts the stack near 4 GB);
// only the first page counts as unmapped
static bool env_check(void* ctx, uint32_t addr, uint32_t len, bool write) {
    (void)ctx; (void)write;
    return addr >= 4096 && (uint64_t)addr + len <= 0x100000000ULL;
}

static bool env_copy_in(void* ctx, void* dst, uint32_t addr, uint32_t len) {
    (void)ctx;
    memcpy(dst, (const void*)(uintptr_t)addr, len);
    return true;
}

static bool env_copy_out(void* ctx, uint32_t addr, const void* src, uint32_t len) {
    (void)ctx;
    memcpy((void*)(uintptr_t)addr, src, len);
    return true;
}

static SysGfxSurface* get(uint32_t handle) {
    if (handle == 0 || handle > SYS_GFX_SURFACE_MAX)
        return NULL;
    return surfaces[handle - 1];
}

static SysGfxSurface* env_lookup(void* ctx, uint32_t handle) {
    (void)ctx;
    return get(handle);
}

static const SysGfxResource* env_resource(void* ctx, uint32_t handle) {
    (void)ctx;
    return (handle == 0 || handle > SYS_RES_MAX) ? NULL : resources[handle - 1];
}

static const SysGfxEnv env = { NULL, env_check, env_copy_in, env_copy_out, env_lookup, env_resource };

//
// The VFS calls the resource manager makes, on host files (descriptor + 1,
// so 0 stays VFS_INVALID_FILE)
//

File vfs_open(const char* path, uint16_t flags) {
    int oflags = (flags & VFS_OPEN_WRITE) ? O_RDWR : O_RDONLY;
    if (flags & VFS_OPEN_CREATE) oflags |= O_CREAT;
    int fd = open(path, oflags, 0644);
    return (fd < 0) ? VFS_INVALID_FILE : (File)(fd + 1);
}
void vfs_close(File f)                                  { close((int)f - 1); }
int32_t vfs_read(File f, void* b, uint32_t n)           { return (int32_t)read((int)f - 1, b, n); }
int32_t vfs_write(File f, const void* b, uint32_t n)    { return (int32_t)write((int)f - 1, b, n); }
uint32_t vfs_tell(File f)                               { return (uint32_t)lseek((int)f - 1, 0, SEEK_CUR); }
bool vfs_truncate(const char* path, uint32_t size)      { return truncate(path, size) == 0; }

uint32_t vfs_seek(File f, uint32_t pos) {
    // The kernel's seek stops at the end of the file
    struct stat st;
    if (fstat((int)f - 1, &st) == 0 && pos > (uint32_t)st.st_size) pos = (uint32_t)st.st_size;
    return (uint32_t)lseek((int)f - 1, (off_t)pos, SEEK_SET);
}

uint32_t vfs_get_size(File f) {
    struct stat st;
    return (fstat((int)f - 1, &st) == 0) ? (uint32_t)st.st_size : 0;
}

// sys_resource.h's sys_res_load, with host paths and one table
static int32_t res_load(uint32_t addr) {
    if (!env_check(NULL, addr, 1, false)) return SYS_EFAULT;
    const char* path = (const char*)(uintptr_t)addr;
    if (strlen(path) >= SYS_PATH_MAX) return SYS_ENAMETOOLONG;

    char name[SYS_RES_NAME_MAX];
    int32_t r = sys_gfx_resource_name(path, name);
    if (r < 0) return r;

    int32_t slot = -1;
    for (uint32_t i = 0; i < SYS_RES_MAX; i++) {
        if (resources[i] && strcmp(resources[i]->name, name) == 0)
            return strcmp(resources[i]->path, path) == 0 ? SYS_OK : SYS_EEXIST;
        if (!resources[i] && slot < 0) slot = (int32_t)i;
    }
    if (slot < 0) return SYS_EMFILE;

    struct stat st;
    if (stat(path, &st) != 0) return SYS_ENOENT;
    if (S_ISDIR(st.st_mode)) return SYS_EISDIR;

    ResourceType type = RESOURCE_TYPE_NONE;
    if (!resource_peek_type(path, &type) || type != RESOURCE_TYPE_SPRITE) return SYS_EBADFMT;
    struct Sprite* sprite = resource_sprite_load(path);
    if (sprite == NULL) return SYS_EBADFMT;

    SysGfxResource* res = (SysGfxResource*)malloc(sizeof(SysGfxResource));
    if (res == NULL) { resource_free(RESOURCE_TYPE_SPRITE, sprite); return SYS_ENOMEM; }
    memset(res, 0, sizeof(*res));
    r = sys_gfx_resource_from_sprite(res, sprite);
    resource_free(RESOURCE_TYPE_SPRITE, sprite);
    if (r < 0) { free(res); return r; }

    memcpy(res->name, name, sizeof(name));
    res->path = (char*)malloc(strlen(path) + 1);
    if (res->path) memcpy(res->path, path, strlen(path) + 1);
    resources[slot] = res;
    return SYS_OK;
}

// What the test reads back: live resources (to see none leak)
uint32_t __test_gfx_resources_live(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < SYS_RES_MAX; i++) n += (resources[i] != NULL);
    return n;
}

// The process "exits": everything it loaded goes
void __test_gfx_resources_drop(void) {
    for (uint32_t i = 0; i < SYS_RES_MAX; i++) {
        if (resources[i]) { sys_gfx_resource_free(resources[i]); free(resources[i]); resources[i] = NULL; }
    }
}

static int32_t user_rect(uint32_t addr, SysGfxRect* out) {
    if (!env_check(NULL, addr, sizeof(*out), false))
        return SYS_EFAULT;
    memcpy(out, (const void*)(uintptr_t)addr, sizeof(*out));
    return SYS_OK;
}

// What the test reads back from the "screen"
uint32_t __test_gfx_screen_pixel(int32_t x, int32_t y) {
    const uint32_t* p = screen.pixels ? gfx_surface_pixel(&screen, x, y) : NULL;
    return p ? *p : 0;
}

int32_t __test_gfx_syscall(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    if (screen.pixels == NULL) {
        if (!gfx_surface_create(&screen, GFX_SHIM_W, GFX_SHIM_H))
            return SYS_ENOMEM;
        memset(screen.pixels, 0, GFX_SHIM_W * GFX_SHIM_H * 4);
    }

    switch (n) {
        case SYS_GFX_INFO: {
            const GfxFont* font = gfx_font_default();
            SysGfxInfo info = { GFX_SHIM_W, GFX_SHIM_H, 32, font->advance, font->line_height,
                                SYS_GFX_MAX_DIM, SYS_GFX_SURFACE_MAX, SYS_GFX_PROCESS_BYTES };
            if (!env_check(NULL, a1, sizeof(info), true)) return SYS_EFAULT;
            memcpy((void*)(uintptr_t)a1, &info, sizeof(info));
            return SYS_OK;
        }

        case SYS_GFX_CREATE: {
            if (sys_gfx_surface_bytes((int32_t)a1, (int32_t)a2) == 0) return SYS_EINVAL;
            uint32_t i = 0;
            while (i < SYS_GFX_SURFACE_MAX && surfaces[i]) i++;
            if (i == SYS_GFX_SURFACE_MAX) return SYS_EMFILE;
            SysGfxSurface* s = (SysGfxSurface*)malloc(sizeof(SysGfxSurface));
            int32_t r = s ? sys_gfx_surface_init(s, (int32_t)a1, (int32_t)a2) : SYS_ENOMEM;
            if (r < 0) { free(s); return r; }
            surfaces[i] = s;
            return (int32_t)(i + 1);
        }

        case SYS_GFX_DESTROY: {
            SysGfxSurface* s = get(a1);
            if (!s) return SYS_EBADF;
            sys_gfx_surface_free(s);
            free(s);
            surfaces[a1 - 1] = NULL;
            return SYS_OK;
        }

        case SYS_GFX_SUBMIT: {
            SysGfxSurface* s = get(a1);
            if (!s) return SYS_EBADF;
            if (a3 == 0) return 0;
            if ((a3 & 3) || a3 > SYS_GFX_SUBMIT_MAX) return SYS_EINVAL;
            if (!env_check(NULL, a2, a3, false)) return SYS_EFAULT;
            memcpy(cmd_buffer, (const void*)(uintptr_t)a2, a3);
            return sys_gfx_execute(s, cmd_buffer, a3 / 4, &env);
        }

        case SYS_GFX_WRITE:
        case SYS_GFX_READ: {
            SysGfxSurface* s = get(a1);
            if (!s) return SYS_EBADF;
            SysGfxRect r;
            int32_t e = user_rect(a2, &r);
            if (e < 0) return e;
            return (n == SYS_GFX_WRITE) ? sys_gfx_write_pixels(s, &r, a3, a4, &env)
                                        : sys_gfx_read_pixels(s, &r, a3, a4, &env);
        }

        case SYS_RES_LOAD:
            return res_load(a1);

        case SYS_RES_GET: {
            if (!env_check(NULL, a1, 1, false)) return SYS_EFAULT;
            const char* name = (const char*)(uintptr_t)a1;
            size_t length = strlen(name);
            if (length == 0) return SYS_EINVAL;
            if (length >= SYS_RES_NAME_MAX) return SYS_ENAMETOOLONG;
            for (uint32_t i = 0; i < SYS_RES_MAX; i++)
                if (resources[i] && strcmp(resources[i]->name, name) == 0) return (int32_t)(i + 1);
            return SYS_ENOENT;
        }

        case SYS_RES_INFO: {
            const SysGfxResource* r = env_resource(NULL, a1);
            if (!r) return SYS_EBADF;
            if (!env_check(NULL, a2, sizeof(SysResInfo), true)) return SYS_EFAULT;
            SysResInfo info = { r->type, (uint32_t)r->surface.width, (uint32_t)r->surface.height, 0 };
            memcpy((void*)(uintptr_t)a2, &info, sizeof(info));
            return SYS_OK;
        }

        case SYS_GFX_PRESENT: {
            // Same arithmetic as sys_gfx_present_locked, onto the fake screen
            SysGfxSurface* s = get(a1);
            int32_t x = (int32_t)a3, y = (int32_t)a4;
            if (!s) return SYS_EBADF;
            if (!sys_gfx_coord_ok(x) || !sys_gfx_coord_ok(y)) return SYS_EINVAL;
            GfxRect src = gfx_surface_bounds(&s->surface);
            if (a2 != 0) {
                SysGfxRect r;
                int32_t e = user_rect(a2, &r);
                if (e < 0) return e;
                if (!sys_gfx_size_ok(r.w) || !sys_gfx_size_ok(r.h) || !sys_gfx_coord_ok(r.x) || !sys_gfx_coord_ok(r.y))
                    return SYS_EINVAL;
                GfxRect want = gfx_rect(r.x, r.y, r.w, r.h);
                if (!gfx_rect_intersect(&want, &src, &src)) return SYS_OK;
                x += src.x - r.x;
                y += src.y - r.y;
            }
            GfxContext gc;
            gfx_context_init(&gc, &screen);
            gfx_blit(&gc, &s->surface, &src, x, y);
            return SYS_OK;
        }
    }
    return SYS_ENOSYS;
}

