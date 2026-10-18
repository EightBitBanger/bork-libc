#ifndef _TIME_H
#define _TIME_H

/*
 * Time since boot. The kernel keeps a millisecond tick (SYS_TIME) and no
 * calendar clock yet, so there is CLOCK_MONOTONIC (and CLOCK_BOOTTIME, the
 * same clock) but no CLOCK_REALTIME, time() or dates.
 *
 *     struct timespec t;
 *     clock_gettime(CLOCK_MONOTONIC, &t);     // resolution 1 ms
 */

#include <stddef.h>
#include <sys/types.h>

#define CLOCKS_PER_SEC      1000L

#define CLOCK_MONOTONIC     1
#define CLOCK_BOOTTIME      7

typedef long    clock_t;
typedef int     clockid_t;

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

// CLOCK_MONOTONIC / CLOCK_BOOTTIME: time since boot. 0, or -1 with errno
// EINVAL (another clock), EFAULT.
int clock_gettime(clockid_t clock, struct timespec* out);
int clock_getres(clockid_t clock, struct timespec* out);     // 1 ms

// Milliseconds since boot in CLOCKS_PER_SEC units (no per-process CPU time
// yet). Wraps after about 24 days on this 32-bit clock_t.
clock_t clock(void);

// Sleep at least *req (rounded up to whole milliseconds). Never
// interrupted, so *rem (may be NULL) is set to zero. EINVAL for a negative
// or out-of-range request.
int nanosleep(const struct timespec* req, struct timespec* rem);

#endif
