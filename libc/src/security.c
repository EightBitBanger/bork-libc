/*
 * Process security clearance (os/security.h) on SYS_SEC_GET / SYS_SEC_SET.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <os/security.h>
#include "internal.h"

static const struct {
    sec_mask_t  bit;
    const char* name;
} sec_names[] = {
    { SEC_FILE_READ,  "file_read"  },
    { SEC_FILE_WRITE, "file_write" },
    { SEC_CONSOLE,    "console"    },
    { SEC_GFX,        "gfx"        },
    { SEC_GFX_SCREEN, "gfx_screen" },
    { SEC_IPC,        "ipc"        },
    { SEC_INPUT,      "input"      },
    { SEC_ADMIN,      "admin"      },
};

#define SEC_NAME_COUNT  (sizeof(sec_names) / sizeof(sec_names[0]))

//
// This process
//

sec_mask_t sec_get(void) {
    int32_t r = syscall1(SYS_SEC_GET, 0);
    return SYS_IS_ERROR(r) ? SEC_NONE : (sec_mask_t)r;
}

bool sec_has(sec_mask_t bits) {
    return (sec_get() & bits) == bits;
}

bool sec_is_admin(void) {
    return sec_has(SEC_ADMIN);
}

//
// Reading and changing masks
//

int sec_get_process(pid_t pid, sec_mask_t* out) {
    if (out == NULL || pid < 0) {
        errno = (out == NULL) ? EINVAL : ESRCH;
        return -1;
    }
    int32_t r = syscall1(SYS_SEC_GET, (uint32_t)pid);
    if (SYS_IS_ERROR(r)) {
        errno = __errno_from_sys(r);
        return -1;
    }
    *out = (sec_mask_t)r;
    return 0;
}

int sec_set_process(pid_t pid, sec_mask_t mask, sec_mask_t* previous) {
    if (pid < 0) {
        errno = ESRCH;
        return -1;
    }
    int32_t r = syscall2(SYS_SEC_SET, (uint32_t)pid, mask);
    if (SYS_IS_ERROR(r)) {
        errno = __errno_from_sys(r);
        return -1;
    }
    if (previous != NULL)
        *previous = (sec_mask_t)r;
    return 0;
}

static int sec_modify(pid_t pid, sec_mask_t set, sec_mask_t clear) {
    // Checked here too, so a bad bit fails the same way whatever the order
    if (((set | clear) & ~SEC_ALL) != 0) {
        errno = EINVAL;
        return -1;
    }
    sec_mask_t cur;
    if (sec_get_process(pid, &cur) < 0)
        return -1;
    sec_mask_t next = (cur | set) & ~clear;
    if (next == cur && sec_is_admin())
        return 0;           // Nothing to change (a non-admin still gets EPERM below)
    return sec_set_process(pid, next, NULL);
}

int sec_grant(pid_t pid, sec_mask_t bits)  { return sec_modify(pid, bits, 0); }
int sec_revoke(pid_t pid, sec_mask_t bits) { return sec_modify(pid, 0, bits); }
int sec_set(sec_mask_t mask)               { return sec_set_process(0, mask, NULL); }
int sec_drop(sec_mask_t bits)              { return sec_revoke(0, bits); }

//
// Names
//

const char* sec_bit_name(sec_mask_t bit) {
    for (size_t i = 0; i < SEC_NAME_COUNT; i++) {
        if (sec_names[i].bit == bit)
            return sec_names[i].name;
    }
    return NULL;
}

typedef struct {
    char*  buf;
    size_t size;
    size_t len;
} sec_text_t;

static void sec_put(sec_text_t* t, const char* s) {
    for (; *s; s++, t->len++) {
        if (t->len + 1 < t->size)
            t->buf[t->len] = *s;
    }
}

int sec_format(sec_mask_t mask, char* buf, size_t size) {
    sec_text_t t = { buf, size, 0 };

    if (mask == 0) {
        sec_put(&t, "none");
    } else {
        sec_mask_t rest = mask;
        for (size_t i = 0; i < SEC_NAME_COUNT; i++) {
            if (!(mask & sec_names[i].bit))
                continue;
            if (t.len) sec_put(&t, "|");
            sec_put(&t, sec_names[i].name);
            rest &= ~sec_names[i].bit;
        }
        if (rest != 0) {
            char hex[11] = "0x";
            int n = 2;
            for (int shift = 28; shift >= 0; shift -= 4) {
                unsigned d = (rest >> shift) & 0xF;
                if (d != 0 || n > 2 || shift == 0)
                    hex[n++] = "0123456789abcdef"[d];
            }
            hex[n] = '\0';
            if (t.len) sec_put(&t, "|");
            sec_put(&t, hex);
        }
    }

    if (size > 0)
        buf[(t.len < size) ? t.len : size - 1] = '\0';
    return (int)t.len;
}

int sec_parse(const char* text, sec_mask_t* out) {
    if (text == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }

    sec_mask_t mask = 0;
    const char* p = text;
    for (;;) {
        while (*p == '|' || *p == ',' || *p == ' ' || *p == '\t') p++;
        if (*p == '\0')
            break;

        size_t n = 0;
        while (p[n] && p[n] != '|' && p[n] != ',' && p[n] != ' ' && p[n] != '\t') n++;

        char word[16];
        if (n >= sizeof(word)) {
            errno = EINVAL;
            return -1;
        }
        memcpy(word, p, n);
        word[n] = '\0';
        p += n;

        sec_mask_t bit = 0;
        bool known = false;
        for (size_t i = 0; i < SEC_NAME_COUNT && !known; i++) {
            if (strcasecmp(word, sec_names[i].name) == 0) {
                bit = sec_names[i].bit;
                known = true;
            }
        }
        if (!known) {
            if (strcasecmp(word, "none") == 0)      { bit = SEC_NONE; known = true; }
            else if (strcasecmp(word, "all") == 0)  { bit = SEC_ALL;  known = true; }
            else if (strcasecmp(word, "file") == 0) { bit = SEC_FILE; known = true; }
            else if (word[0] >= '0' && word[0] <= '9') {
                char* end;
                unsigned long v = strtoul(word, &end, 0);
                if (*end == '\0') { bit = (sec_mask_t)v; known = true; }
            }
        }
        if (!known) {
            errno = EINVAL;
            return -1;
        }
        mask |= bit;
    }

    *out = mask;
    return 0;
}


