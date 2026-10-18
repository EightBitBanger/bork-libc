/*
 * time.h and os_time_ms() on SYS_TIME (milliseconds since boot, 64-bit).
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <os/os.h>
#include "internal.h"

uint64_t os_time_ms(void) {
    uint64_t ms = 0;
    if (SYS_IS_ERROR(syscall1(SYS_TIME, (uint32_t)(uintptr_t)&ms)))
        return 0;
    return ms;
}

static bool known_clock(clockid_t clock) {
    return clock == CLOCK_MONOTONIC || clock == CLOCK_BOOTTIME;
}

int clock_gettime(clockid_t clock, struct timespec* out) {
    if (!known_clock(clock)) {
        errno = EINVAL;
        return -1;
    }
    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    uint64_t ms = 0;
    int r = __sys_ret(syscall1(SYS_TIME, (uint32_t)(uintptr_t)&ms));
    if (r < 0)
        return -1;
    out->tv_sec  = (time_t)(ms / 1000u);
    out->tv_nsec = (long)(ms % 1000u) * 1000000L;
    return 0;
}

int clock_getres(clockid_t clock, struct timespec* out) {
    if (!known_clock(clock)) {
        errno = EINVAL;
        return -1;
    }
    if (out != NULL) {
        out->tv_sec  = 0;
        out->tv_nsec = 1000000L;
    }
    return 0;
}

clock_t clock(void) {
    return (clock_t)os_time_ms();
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
    if (req == NULL || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }

    // Whole milliseconds, rounded up, in slices that fit SYS_SLEEP's 32 bits
    uint64_t ms = (uint64_t)req->tv_sec * 1000u + ((uint64_t)req->tv_nsec + 999999u) / 1000000u;
    while (ms > 0) {
        uint32_t slice = (ms > 0x7FFFFFFFu) ? 0x7FFFFFFFu : (uint32_t)ms;
        if (__sys_ret(syscall1(SYS_SLEEP, slice)) < 0)
            return -1;
        ms -= slice;
    }
    if (rem != NULL) {
        rem->tv_sec  = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}
