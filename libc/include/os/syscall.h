#ifndef _OS_SYSCALL_H
#define _OS_SYSCALL_H

/*
 * Kernel system call ABI, user side.
 *
 * Mirrors kernel/syscall/syscall.h: numbers, error codes, flags and SysStat
 * must stay identical to the kernel's. Everything else in the libc is built
 * on the syscallN() stubs at the bottom of this file.
 *
 *   int 0x81
 *   EAX                  syscall number
 *   EBX ECX EDX ESI EDI  arguments 1-5
 *   EAX                  result: [-4095, -1] is an error (SYS_E*), anything
 *                        else is success. Addresses (sbrk) can exceed
 *                        0x7FFFFFFF, so test errors with SYS_IS_ERROR().
 *
 * The kernel preserves every register except EAX (and the FPU/SSE state).
 */

#include <stdint.h>

#define SYSCALL_VECTOR      0x81

#define SYS_EXIT        0   // (int code)               never returns
#define SYS_PRINT       1   // (buf, length)            bytes printed
#define SYS_YIELD       2   // ()                       0
#define SYS_SLEEP       3   // (ms)                     0
#define SYS_GETPID      4   // ()                       process id
#define SYS_SBRK        5   // (int increment)          previous break
#define SYS_OPEN        6   // (path, flags)            descriptor
#define SYS_CLOSE       7   // (fd)                     0
#define SYS_READ        8   // (fd, buf, length)        bytes read, 0 at EOF
#define SYS_WRITE       9   // (fd, buf, length)        bytes written
#define SYS_SEEK       10   // (fd, int offset, whence) new offset
#define SYS_FSTAT      11   // (fd, SysStat*)           0
#define SYS_STAT       12   // (path, SysStat*)         0
#define SYS_MKDIR      13   // (path)                   0
#define SYS_REMOVE     14   // (path)                   0 (file or EMPTY dir)
#define SYS_RENAME     15   // (path, new_name)         0 (same directory)
#define SYS_TRUNCATE   16   // (path, size)             0
#define SYS_READDIR    17   // (path, index, buf, size) name length, 0 at end
#define SYS_CHDIR      18   // (path)                   0
#define SYS_GETCWD     19   // (buf, size)              path length
#define SYS_COPY       20   // (src, dest)              0
#define SYS_COUNT      21

#define SYS_OK               0
#define SYS_ENOSYS          -1
#define SYS_EFAULT          -2
#define SYS_EINVAL          -3
#define SYS_ENOMEM          -4
#define SYS_ENOENT          -5
#define SYS_EEXIST          -6
#define SYS_EACCES          -7
#define SYS_EBADF           -8
#define SYS_EMFILE          -9
#define SYS_EISDIR         -10
#define SYS_ENOTDIR        -11
#define SYS_ENOTEMPTY      -12
#define SYS_EBUSY          -13
#define SYS_ENAMETOOLONG   -14
#define SYS_ERANGE         -15
#define SYS_ENOSPC         -16
#define SYS_EIO            -17

#define SYS_ERROR_MIN       -4095
#define SYS_IS_ERROR(r)     ((uint32_t)(r) >= (uint32_t)SYS_ERROR_MIN)

#define SYS_PRINT_MAX       4096
#define SYS_PATH_MAX        512
#define SYS_NAME_MAX        255
#define SYS_FD_MAX          32
#define SYS_FD_FIRST        3
#define SYS_RW_MAX          (1024U * 1024U)
#define SYS_OFFSET_MAX      0x7FFFFFFFU

#define SYS_FD_STDIN        0
#define SYS_FD_STDOUT       1
#define SYS_FD_STDERR       2

#define SYS_O_READ          0x01
#define SYS_O_WRITE         0x02
#define SYS_O_CREATE        0x04
#define SYS_O_TRUNC         0x08
#define SYS_O_APPEND        0x10
#define SYS_O_EXCL          0x20

#define SYS_SEEK_SET        0
#define SYS_SEEK_CUR        1
#define SYS_SEEK_END        2

#define SYS_TYPE_FILE       0
#define SYS_TYPE_DIRECTORY  1
#define SYS_TYPE_MOUNT      2

#define SYS_PERM_EXECUTE    0x01
#define SYS_PERM_READ       0x02
#define SYS_PERM_WRITE      0x04

typedef struct {
    uint32_t size;
    uint32_t type;
    uint32_t permissions;
    uint32_t created;
    uint32_t modified;
} SysStat;

_Static_assert(sizeof(SysStat) == 20, "SysStat is part of the kernel ABI");

#ifdef LIBC_SYSCALL_HOOK
/* Test builds route every call through a C function instead of the gate */
int32_t LIBC_SYSCALL_HOOK(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
#define __SYSCALL(n, a1, a2, a3, a4, a5) LIBC_SYSCALL_HOOK(n, a1, a2, a3, a4, a5)
#else
static inline int32_t __syscall_trap(uint32_t n, uint32_t a1, uint32_t a2,
                                     uint32_t a3, uint32_t a4, uint32_t a5) {
    int32_t result;
    __asm__ volatile("int $0x81"
                     : "=a"(result)
                     : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return result;
}
#define __SYSCALL(n, a1, a2, a3, a4, a5) __syscall_trap(n, a1, a2, a3, a4, a5)
#endif

static inline int32_t syscall0(uint32_t n) { return __SYSCALL(n, 0, 0, 0, 0, 0); }
static inline int32_t syscall1(uint32_t n, uint32_t a1) { return __SYSCALL(n, a1, 0, 0, 0, 0); }
static inline int32_t syscall2(uint32_t n, uint32_t a1, uint32_t a2) { return __SYSCALL(n, a1, a2, 0, 0, 0); }
static inline int32_t syscall3(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3) { return __SYSCALL(n, a1, a2, a3, 0, 0); }
static inline int32_t syscall4(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { return __SYSCALL(n, a1, a2, a3, a4, 0); }
static inline int32_t syscall5(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { return __SYSCALL(n, a1, a2, a3, a4, a5); }

#endif
