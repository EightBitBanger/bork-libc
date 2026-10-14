/*
 * Program start and end: __libc_start (called by crt0), exit, atexit, abort.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

#define ATEXIT_MAX  32

extern int main(int argc, char** argv, char** envp);

// Provided by the linker script
typedef void (*init_fn)(void);
extern init_fn __init_array_start[] __attribute__((weak));
extern init_fn __init_array_end[]   __attribute__((weak));
extern init_fn __fini_array_start[] __attribute__((weak));
extern init_fn __fini_array_end[]   __attribute__((weak));

static void (*atexit_fns[ATEXIT_MAX])(void);
static int atexit_count = 0;

/*
 * The kernel does not pass arguments yet, so main() gets argc = 0 and an
 * empty argv. Once it does, read them here from the initial stack.
 */
static char* empty_argv[] = { NULL };

__attribute__((noreturn)) void __libc_start(void) {
    for (init_fn* f = __init_array_start; f && f < __init_array_end; f++)
        (*f)();

    exit(main(0, empty_argv, environ));
}

int atexit(void (*fn)(void)) {
    if (atexit_count >= ATEXIT_MAX)
        return -1;
    atexit_fns[atexit_count++] = fn;
    return 0;
}

void exit(int status) {
    while (atexit_count > 0)
        atexit_fns[--atexit_count]();

    for (init_fn* f = __fini_array_end; f && f > __fini_array_start; )
        (*--f)();

    __stdio_exit();
    _exit(status);
}

void abort(void) {
    static const char msg[] = "abort() called\n";
    write(2, msg, sizeof(msg) - 1);
    _exit(134);     // 128 + SIGABRT, as a shell would report it
}

void __assert_fail(const char* expr, const char* file, int line, const char* func) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s:%d: %s: Assertion `%s' failed.\n", file, line, func, expr);
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
    if (n > 0) write(2, buf, (size_t)n);
    abort();
}

char* getenv(const char* name) {
    size_t len = strlen(name);
    for (char** e = environ; e && *e; e++) {
        if (strncmp(*e, name, len) == 0 && (*e)[len] == '=')
            return *e + len + 1;
    }
    return NULL;
}
