#ifndef _OS_PROCESS_H
#define _OS_PROCESS_H

/*
 * Starting other programs. Needs SEC_EXEC (os/security.h), which the
 * default clearance includes.
 *
 *     char* args[] = { "hello", "--loud", "world", NULL };
 *     pid_t pid = spawn("/mnt/ata0p1/bin/hello", args);    // pid, or -1 with errno
 *
 *     spawnl("hello", "hello", "--loud", NULL);           // relative to the cwd
 *
 * The new program receives argv in main(argc, argv) exactly as given. By
 * convention argv[0] is its name; a NULL or empty argv gives it
 * argv = { the resolved path }. The file must be an ELF executable marked
 * executable, and this program's file clearance must allow reading it.
 *
 * spawn() returns as soon as the program is loaded; both then run side by
 * side. The child starts in this program's working directory with this
 * program's clearance and priority, unless spawn_ex() says otherwise.
 *
 * Errors: -1 with errno
 *   ENOENT   no such file              EISDIR   it is a directory
 *   EACCES   not marked executable, or the file needs clearance we lack
 *   ENOEXEC  not an ELF this kernel can run
 *   E2BIG    more than SPAWN_ARG_MAX strings or SPAWN_ARG_BYTES bytes of them
 *   EAGAIN   the process table is full ENOMEM   out of memory
 *   EPERM    no SEC_EXEC, or spawn_ex() asked for bits this process lacks
 */

#include <sys/types.h>
#include <os/syscall.h>
#include <os/security.h>

#define SPAWN_ARG_MAX       SYS_ARG_MAX         // Strings in argv
#define SPAWN_ARG_BYTES     SYS_ARG_BYTES       // Bytes of them, terminators included

#define SPAWN_INHERIT       SYS_SPAWN_INHERIT   // spawn_ex security: this process's own mask
#define SPAWN_CWD_PROGRAM   ""                  // spawn_ex cwd: the executable's directory

// Start `path` with `argv` (NULL-terminated; may be NULL). Process id, or -1.
pid_t spawn(const char* path, char* const argv[]);

// The same with the arguments listed in the call, ending with NULL:
// spawnl(path, arg0, arg1, ..., NULL)
pid_t spawnl(const char* path, const char* arg0, ...);

// spawn() with a choice of working directory and clearance.
//   cwd       NULL: this program's; SPAWN_CWD_PROGRAM: the executable's own
//             directory; anything else a path (relative to ours). One that is
//             not a directory starts the child in "/".
//   security  SPAWN_INHERIT, or a mask holding only bits this process has
//             (a child can be given less clearance, never more)
pid_t spawn_ex(const char* path, char* const argv[], const char* cwd, sec_mask_t security);

#endif
