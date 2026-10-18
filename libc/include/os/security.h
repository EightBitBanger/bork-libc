#ifndef _OS_SECURITY_H
#define _OS_SECURITY_H

/*
 * Process security clearance.
 *
 * Every process has a mask of clearance bits. A system call that needs a
 * bit the process lacks fails with errno EPERM and does nothing, so for
 * example fopen("x", "w") without SEC_FILE_WRITE returns NULL / EPERM. The
 * kernel checks the mask on every call: taking a bit away also stops files
 * and surfaces that are already open (close() always works).
 *
 *   SEC_FILE_READ    open for reading, read, stat, fstat, opendir/readdir,
 *                    chdir, access
 *   SEC_FILE_WRITE   open for writing / create / truncate / append, write to
 *                    a file, mkdir, remove, unlink, rmdir, rename, truncate
 *                    (os_copy and a cross-directory rename need both)
 *   SEC_CONSOLE      printf to stdout / stderr, os_print
 *   SEC_GFX          os/gfx.h surfaces, and presenting them into this
 *                    program's window through the window manager
 *   SEC_GFX_SCREEN   presenting straight onto the display when no window
 *                    manager takes the surface (needs SEC_GFX as well)
 *   SEC_IPC          os/ipc.h pipes, ports and shared memory
 *   SEC_INPUT        os/input.h: take the keyboard and mouse
 *   SEC_EXEC         os/process.h: start other programs (spawn)
 *   SEC_ADMIN        change security masks: sec_set_process() on any
 *                    process, this one included, and reading other
 *                    processes' masks
 *
 * Always available: exit, sbrk/malloc, sleep, yield, getpid, close, lseek,
 * getcwd, ipc_close, ipc_info, and reading this process's own mask.
 *
 * The bits are independent. SEC_ADMIN does not imply the others, but an
 * administrative process may grant itself any of them. A process that
 * clears its own SEC_ADMIN cannot get it back; only another admin (or the
 * kernel) can.
 *
 * pid 0 always means "this process".
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <os/syscall.h>

typedef uint32_t sec_mask_t;

#define SEC_FILE_READ    SYS_SEC_FILE_READ
#define SEC_FILE_WRITE   SYS_SEC_FILE_WRITE
#define SEC_FILE         (SEC_FILE_READ | SEC_FILE_WRITE)
#define SEC_CONSOLE      SYS_SEC_CONSOLE
#define SEC_GFX          SYS_SEC_GFX
#define SEC_GFX_SCREEN   SYS_SEC_GFX_SCREEN
#define SEC_IPC          SYS_SEC_IPC
#define SEC_INPUT        SYS_SEC_INPUT
#define SEC_EXEC         SYS_SEC_EXEC
#define SEC_ADMIN        SYS_SEC_ADMIN

#define SEC_NONE         SYS_SEC_NONE
#define SEC_ALL          SYS_SEC_VALID      // Every bit the kernel knows

//
// This process
//

// Get this process's security mask
sec_mask_t sec_get(void);

// True when this process matches `bits`
bool sec_has(sec_mask_t bits);

// True when this process may change security masks
bool sec_is_admin(void);

//
// Changing masks (SEC_ADMIN only)
//
// All return 0, or -1 with errno:
//   EPERM   this process does not have SEC_ADMIN (or, for reading another
//           process's mask, is not an admin)
//   ESRCH   no running process with that id
//   EINVAL  bits outside SEC_ALL
//

// Read any process's mask into *out (pid 0: this one; others need SEC_ADMIN)
int sec_get_process(pid_t pid, sec_mask_t* out);

// Replace a process's whole mask. *previous (may be NULL) gets the old one.
int sec_set_process(pid_t pid, sec_mask_t mask, sec_mask_t* previous);

// Add / remove bits, leaving the rest as they are. Both read the mask and
// then write it back, so two admins changing one process at the same moment
// can overwrite each other; use sec_set_process() for an exact mask.
int sec_grant(pid_t pid, sec_mask_t bits);
int sec_revoke(pid_t pid, sec_mask_t bits);

// sec_set_process(0, ...)
int sec_set(sec_mask_t mask);

// Give up bits this process holds for good: sec_revoke(0, bits). Needs
// SEC_ADMIN like every change, so drop SEC_ADMIN last (or together with
// the rest, in the same call).
int sec_drop(sec_mask_t bits);

//
// Names
//

// "file_read", "gfx", "admin", ... for one bit; NULL for anything else
const char* sec_bit_name(sec_mask_t bit);

// The mask as names joined by '|' ("file_read|console"), "none" for 0, and
// unknown bits as one hex number at the end ("console|0x8000"). Like
// snprintf: the length the whole text needs, written truncated (and always
// terminated) when size > 0.
int sec_format(sec_mask_t mask, char* buf, size_t size);

// Parse names joined by '|', ',' or spaces ("file_read|gfx"), plus "none",
// "all", "file" (both file bits) and hex/decimal numbers. 0 and *out on
// success, -1 with errno EINVAL for an unknown name.
int sec_parse(const char* text, sec_mask_t* out);

#endif



