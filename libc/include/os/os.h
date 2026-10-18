#ifndef _OS_OS_H
#define _OS_OS_H

/*
 * Calls specific to this kernel, with no standard equivalent. They follow
 * the usual convention: -1 and errno on failure.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

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

/* Make `path` absolute against the working directory and fold "." and "..",
   the way the kernel does. Returns out, or NULL (errno) if it does not fit. */
char* os_abspath(const char* path, char* out, size_t size);

#endif

