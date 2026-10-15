#ifndef _OS_RESOURCE_H
#define _OS_RESOURCE_H

/*
 * Resources: images the kernel loads from resource files (its RSRC format,
 * the same files the window manager uses for icons) and keeps for this
 * process, ready to draw with os/gfx.h.
 *
 *     resource_load("/mnt/ata0p1/sys/images/icon_folder");   // by path
 *     int icon = resource_get("icon_folder");                 // by name
 *     gfx_draw_resource(surface, icon, 10, 10);
 *
 * A resource's name is the file's name: the last part of the path it was
 * loaded from. Relative paths start in the working directory. Loading the
 * same file twice is harmless; a different file with a name already in use
 * fails with EEXIST. Names and handles are private to this process, and
 * everything loaded stays until the process exits (at most RESOURCE_MAX).
 *
 * Errors: -1 with errno.
 *   ENOENT   no such file (load), nothing loaded by that name (get)
 *   ENOEXEC  not a resource file the kernel can load: wrong format, a
 *            type it doesn't know, damaged, or wider/taller than
 *            GFX_MAX_DIM
 *   EEXIST   another file already loaded under this name
 *   EMFILE   RESOURCE_MAX resources already loaded
 *   ENOMEM   over the process's pixel budget (shared with gfx surfaces)
 *   EPERM    load needs SEC_FILE_READ and SEC_GFX; get and info SEC_GFX
 *
 * Only sprites exist so far (RESOURCE_SPRITE).
 */

#include <stdint.h>
#include <os/syscall.h>

#define RESOURCE_MAX        SYS_RES_MAX
#define RESOURCE_NAME_MAX   SYS_RES_NAME_MAX      // Bytes, terminator included

#define RESOURCE_SPRITE     SYS_RES_TYPE_SPRITE

typedef struct {
    uint32_t type;          // RESOURCE_*
    uint32_t width;         // Pixels
    uint32_t height;
    uint32_t reserved;
} resource_info_t;

// Load the resource file at `path`. 0, or -1 with errno.
int resource_load(const char* path);

// The handle of the resource loaded under `name`, or -1 with errno
int resource_get(const char* name);

// Type and size of a loaded resource. 0, or -1 with errno (EBADF).
int resource_info(int handle, resource_info_t* out);

#endif
