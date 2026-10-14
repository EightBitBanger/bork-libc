/*
 * Test-only: emulates the kernel's int 0x81 syscalls on 32-bit Linux
 * (int 0x80), so the real libc, crt0 and linker script can run on a
 * development machine. Behaviour follows the kernel syscall handlers where it
 * matters to the libc: fd 0-2 are the console, SYS_REMOVE takes files and
 * empty directories, SYS_RENAME is a same-directory rename that refuses an
 * existing name, SYS_READDIR is index based, opening a directory is EISDIR.
 */

#include <stdint.h>
#include <os/syscall.h>

static int32_t lx(uint32_t n, uint32_t a, uint32_t b, uint32_t c) {
    int32_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

enum {
    L_EXIT_GROUP = 252, L_READ = 3, L_WRITE = 4, L_OPEN = 5, L_CLOSE = 6, L_UNLINK = 10,
    L_CHDIR = 12, L_GETPID = 20, L_RENAME = 38, L_MKDIR = 39, L_RMDIR = 40, L_BRK = 45,
    L_LSEEK = 19, L_TRUNCATE = 92, L_STAT = 106, L_FSTAT = 108, L_GETDENTS = 141,
    L_SCHED_YIELD = 158, L_NANOSLEEP = 162, L_GETCWD = 183,
};

#define LO_CREAT     0x40
#define LO_EXCL      0x80
#define LO_TRUNC     0x200
#define LO_APPEND    0x400
#define LO_DIRECTORY 0x10000

static int32_t map_err(int32_t r) {
    if (r >= 0) return r;
    switch (-r) {
        case 2:  return SYS_ENOENT;
        case 9:  return SYS_EBADF;
        case 12: return SYS_ENOMEM;
        case 13: return SYS_EACCES;
        case 14: return SYS_EFAULT;
        case 16: return SYS_EBUSY;
        case 17: return SYS_EEXIST;
        case 20: return SYS_ENOTDIR;
        case 21: return SYS_EISDIR;
        case 22: return SYS_EINVAL;
        case 24: return SYS_EMFILE;
        case 28: return SYS_ENOSPC;
        case 34: return SYS_ERANGE;
        case 36: return SYS_ENAMETOOLONG;
        case 39: return SYS_ENOTEMPTY;
        default: return SYS_EIO;
    }
}

static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }

// i386 struct stat (asm/stat.h)
struct lstat_t {
    uint32_t dev, ino;
    uint16_t mode, nlink, uid, gid;
    uint32_t rdev, size, blksize, blocks;
    uint32_t atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns;
    uint32_t pad[2];
};

static int32_t to_sysstat(const struct lstat_t* l, uint32_t out) {
    SysStat* s = (SysStat*)out;
    s->type = ((l->mode & 0170000) == 0040000) ? SYS_TYPE_DIRECTORY : SYS_TYPE_FILE;
    s->size = (s->type == SYS_TYPE_FILE) ? l->size : 0;
    s->permissions = ((l->mode & 0400) ? SYS_PERM_READ : 0) |
                     ((l->mode & 0200) ? SYS_PERM_WRITE : 0) |
                     ((l->mode & 0100) ? SYS_PERM_EXECUTE : 0);
    s->created = l->ctime;
    s->modified = l->mtime;
    return 0;
}

static int is_dir(const char* path) {
    struct lstat_t l;
    if (lx(L_STAT, (uint32_t)path, (uint32_t)&l, 0) < 0) return -1;
    return (l.mode & 0170000) == 0040000;
}

static uint32_t brk_cur;

int32_t __test_syscall(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;
    switch (n) {
        case SYS_EXIT:
            lx(L_EXIT_GROUP, a1, 0, 0);
            for (;;) { }

        case SYS_PRINT:
            return map_err(lx(L_WRITE, 1, a1, a2));

        case SYS_YIELD:
            lx(L_SCHED_YIELD, 0, 0, 0);
            return 0;

        case SYS_SLEEP: {
            uint32_t ts[2] = { a1 / 1000, (a1 % 1000) * 1000000 };
            lx(L_NANOSLEEP, (uint32_t)ts, 0, 0);
            return 0;
        }

        case SYS_GETPID:
            return lx(L_GETPID, 0, 0, 0);

        case SYS_SBRK: {
            if (brk_cur == 0) brk_cur = (uint32_t)lx(L_BRK, 0, 0, 0);
            int32_t inc = (int32_t)a1;
            if (inc == 0) return (int32_t)brk_cur;
            uint32_t want = brk_cur + (uint32_t)inc;
            uint32_t got = (uint32_t)lx(L_BRK, want, 0, 0);
            if (got != want) return SYS_ENOMEM;
            uint32_t old = brk_cur;
            brk_cur = want;
            return (int32_t)old;
        }

        case SYS_OPEN: {
            const char* path = (const char*)a1;
            uint32_t f = a2;
            if (!(f & (SYS_O_READ | SYS_O_WRITE))) return SYS_EINVAL;
            if (is_dir(path) == 1) return SYS_EISDIR;
            uint32_t lf = ((f & SYS_O_READ) && (f & SYS_O_WRITE)) ? 2 : (f & SYS_O_WRITE) ? 1 : 0;
            if (f & SYS_O_CREATE) lf |= LO_CREAT;
            if (f & SYS_O_EXCL)   lf |= LO_EXCL;
            if (f & SYS_O_TRUNC)  lf |= LO_TRUNC;
            if (f & SYS_O_APPEND) lf |= LO_APPEND;
            return map_err(lx(L_OPEN, a1, lf, 0644));
        }

        case SYS_CLOSE:
            if (a1 < SYS_FD_FIRST) return SYS_EBADF;
            return map_err(lx(L_CLOSE, a1, 0, 0));

        case SYS_READ:
            if (a1 < SYS_FD_FIRST) return SYS_EBADF;   // No console input yet
            return map_err(lx(L_READ, a1, a2, a3));

        case SYS_WRITE:
            if (a1 == 1 || a1 == 2) return map_err(lx(L_WRITE, a1, a2, a3));
            if (a1 < SYS_FD_FIRST) return SYS_EBADF;
            return map_err(lx(L_WRITE, a1, a2, a3));

        case SYS_SEEK:
            if (a1 < SYS_FD_FIRST) return SYS_EBADF;
            return map_err(lx(L_LSEEK, a1, a2, a3));

        case SYS_FSTAT: {
            if (a1 < SYS_FD_FIRST) return SYS_EBADF;
            struct lstat_t l;
            int32_t r = lx(L_FSTAT, a1, (uint32_t)&l, 0);
            return r < 0 ? map_err(r) : to_sysstat(&l, a2);
        }

        case SYS_STAT: {
            struct lstat_t l;
            int32_t r = lx(L_STAT, a1, (uint32_t)&l, 0);
            return r < 0 ? map_err(r) : to_sysstat(&l, a2);
        }

        case SYS_MKDIR:
            return map_err(lx(L_MKDIR, a1, 0755, 0));

        case SYS_REMOVE: {
            int d = is_dir((const char*)a1);
            if (d < 0) return SYS_ENOENT;
            return map_err(lx(d ? L_RMDIR : L_UNLINK, a1, 0, 0));
        }

        case SYS_RENAME: {
            const char* path = (const char*)a1;
            const char* name = (const char*)a2;
            char to[SYS_PATH_MAX];
            uint32_t cut = slen(path);
            while (cut > 0 && path[cut - 1] != '/') cut--;
            uint32_t nl = slen(name);
            if (cut + nl + 1 > sizeof(to)) return SYS_ENAMETOOLONG;
            for (uint32_t i = 0; i < cut; i++) to[i] = path[i];
            for (uint32_t i = 0; i <= nl; i++) to[cut + i] = name[i];
            if (is_dir(to) >= 0) return SYS_EEXIST;
            return map_err(lx(L_RENAME, a1, (uint32_t)to, 0));
        }

        case SYS_TRUNCATE:
            return map_err(lx(L_TRUNCATE, a1, a2, 0));

        case SYS_READDIR: {
            int32_t fd = lx(L_OPEN, a1, LO_DIRECTORY, 0);
            if (fd < 0) return map_err(fd);
            char buf[2048];
            uint32_t seen = 0;
            int32_t result = 0;
            for (;;) {
                int32_t got = lx(L_GETDENTS, (uint32_t)fd, (uint32_t)buf, sizeof(buf));
                if (got <= 0) break;
                for (int32_t off = 0; off < got; ) {
                    uint16_t reclen = *(uint16_t*)(buf + off + 8);
                    const char* nm = buf + off + 10;
                    off += reclen;
                    if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
                    if (seen++ != a2) continue;
                    uint32_t len = slen(nm);
                    if (a4 < len + 1) { result = SYS_ERANGE; goto done; }
                    for (uint32_t i = 0; i <= len; i++) ((char*)a3)[i] = nm[i];
                    result = (int32_t)len;
                    goto done;
                }
            }
        done:
            lx(L_CLOSE, (uint32_t)fd, 0, 0);
            return result;
        }

        case SYS_CHDIR:
            return map_err(lx(L_CHDIR, a1, 0, 0));

        case SYS_GETCWD: {
            int32_t r = lx(L_GETCWD, a1, a2, 0);
            return r < 0 ? map_err(r) : r - 1;
        }

        case SYS_COPY: {
            // Files only (enough for the tests)
            if (is_dir((const char*)a2) >= 0) return SYS_EEXIST;
            int32_t in = lx(L_OPEN, a1, 0, 0);
            if (in < 0) return map_err(in);
            int32_t out = lx(L_OPEN, a2, 1 | LO_CREAT | LO_EXCL, 0644);
            if (out < 0) { lx(L_CLOSE, (uint32_t)in, 0, 0); return map_err(out); }
            char buf[4096];
            int32_t r;
            while ((r = lx(L_READ, (uint32_t)in, (uint32_t)buf, sizeof(buf))) > 0)
                lx(L_WRITE, (uint32_t)out, (uint32_t)buf, (uint32_t)r);
            lx(L_CLOSE, (uint32_t)in, 0, 0);
            lx(L_CLOSE, (uint32_t)out, 0, 0);
            return 0;
        }

        default:
            return SYS_ENOSYS;
    }
}
