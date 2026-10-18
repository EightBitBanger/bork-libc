/*
 * Resources (os/resource.h) on SYS_RES_LOAD / SYS_RES_GET / SYS_RES_INFO.
 * Drawing them is in gfx.c (gfx_draw_resource*).
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <os/os.h>
#include <os/resource.h>
#include "internal.h"

_Static_assert(sizeof(resource_info_t) == sizeof(SysResInfo), "resource_info_t mirrors SysResInfo");

int resource_load(const char* path) {
    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    return __sys_ret(syscall1(SYS_RES_LOAD, (uint32_t)(uintptr_t)path));
}

int resource_get(const char* name) {
    if (name == NULL) {
        errno = EFAULT;
        return -1;
    }
    return __sys_ret(syscall1(SYS_RES_GET, (uint32_t)(uintptr_t)name));
}

int resource_info(int handle, resource_info_t* out) {
    return __sys_ret(syscall2(SYS_RES_INFO, (uint32_t)handle, (uint32_t)(uintptr_t)out));
}


int resource_load_system(const char* name) {
    if (name == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (name[0] == '\0' || strchr(name, '/') != NULL) {
        errno = EINVAL;
        return -1;
    }

    char path[SYS_PATH_MAX];
    if (os_syspath("images", path, sizeof(path)) == NULL)
        return -1;

    size_t used = strlen(path), length = strlen(name);
    if (used + 1 + length + 1 > sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    path[used] = '/';
    memcpy(path + used + 1, name, length + 1);
    return resource_load(path);
}

// The name a file registers under: its last path component
static const char* resource_basename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int resource_open(const char* path) {
    if (resource_load(path) < 0)
        return -1;
    return resource_get(resource_basename(path));
}

int resource_open_system(const char* name) {
    if (resource_load_system(name) < 0)
        return -1;
    return resource_get(name);
}
