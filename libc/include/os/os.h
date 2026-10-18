#ifndef _OS_OS_H
#define _OS_OS_H

/*
 * Calls specific to this kernel, with no standard equivalent. They follow
 * the usual convention: -1 and errno on failure.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <os/syscall.h>

/* Write straight to the console (and COM1). A NUL ends the console line. */
ssize_t os_print(const char* buf, size_t length);

/* Copy a file or a whole directory tree to a new path (must not exist) */
int os_copy(const char* src, const char* dest);

/* Rename within the same directory: new_name is a name, not a path */
int os_rename(const char* path, const char* new_name);

/* The home directory: mount root of the device that holds the system
   folders ("/mnt/ata0p1"). Same buffer rules as getcwd(): buf NULL allocates
   (size 0 = PATH_MAX). Returns buf, or NULL with errno ENOENT (no home
   device), ERANGE (buf too small), EFAULT. Also in the environment as HOME. */
char* os_gethome(char* buf, size_t size);

/* "<home>/sys", or "<home>/sys/<sub>" when sub is given ("images",
   "images/icon_folder"). Returns out, or NULL with errno (ENAMETOOLONG
   if it does not fit, else as os_gethome). */
char* os_syspath(const char* sub, char* out, size_t size);

/* Milliseconds since boot. Monotonic (never goes back, never set); 0 if the
   kernel has no SYS_TIME. Also as clock_gettime(CLOCK_MONOTONIC) in time.h. */
uint64_t os_time_ms(void);

/* Block for at least ms milliseconds (0 = just yield) */
int os_sleep_ms(uint32_t ms);

/* What stat() doesn't say (SYS_FINFO): the clearance a file requires, whether
   this process has it, whether its permissions can be changed here, and how
   full a storage device is (for its root). 0, or -1 with errno. */
typedef SysFileInfo os_finfo_t;
int os_finfo(const char* path, os_finfo_t* out);

/* Replace a file's or folder's permissions: SYS_PERM_READ / _WRITE /
   _EXECUTE (chmod() in sys/stat.h takes POSIX modes). 0, or -1 with errno:
   EACCES (the folder holding it isn't writable, or no clearance for it),
   EBUSY (a device's root), EPERM (no file write clearance). */
int os_set_permissions(const char* path, uint32_t permissions);

/* Make `path` absolute against the working directory and fold "." and "..",
   the way the kernel does. Returns out, or NULL (errno) if it does not fit. */
/* Write everything the file systems still hold in memory out to their
   devices (SYS_SYNC). Do it before shutting down or after saving something
   that must survive a power cut. 0, or -1 with errno: EIO, EPERM (no file
   write clearance). */
int os_sync(void);

char* os_abspath(const char* path, char* out, size_t size);

#endif


