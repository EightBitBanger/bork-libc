/*
 * Starting programs (os/process.h) on SYS_SPAWN.
 */

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <os/process.h>
#include "internal.h"

pid_t spawn_ex(const char* path, char* const argv[], const char* cwd, sec_mask_t security) {
    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    return __sys_ret(syscall4(SYS_SPAWN, (uint32_t)(uintptr_t)path, (uint32_t)(uintptr_t)argv,
                              (uint32_t)(uintptr_t)cwd, (uint32_t)security));
}

pid_t spawn(const char* path, char* const argv[]) {
    return spawn_ex(path, argv, NULL, SPAWN_INHERIT);
}

pid_t spawnl(const char* path, const char* arg0, ...) {
    const char* args[SPAWN_ARG_MAX + 1];
    size_t count = 0;

    va_list ap;
    va_start(ap, arg0);
    for (const char* a = arg0; a != NULL; a = va_arg(ap, const char*)) {
        if (count == SPAWN_ARG_MAX) {
            va_end(ap);
            errno = E2BIG;
            return -1;
        }
        args[count++] = a;
    }
    va_end(ap);
    args[count] = NULL;

    return spawn(path, (char* const*)args);
}
