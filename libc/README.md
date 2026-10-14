# libc for the kernel's user space

A freestanding C library for ring 3 programs on this kernel. Every call goes
through the `int 0x81` gate (`include/os/syscall.h`); the public API follows
standard C and POSIX so ordinary code compiles unchanged.

## Building

```sh
make                    # build/libc.a and build/crt0.o
make examples           # build/hello (ELF) and build/hello.bin (flat binary)
make test               # host tests (Linux, gcc with -m32)
make CC=i686-elf-gcc    # or any i386 GCC/Clang
```

Linking a program:

```sh
gcc -m32 -ffreestanding -nostdinc -isystem $(gcc -print-file-name=include) \
    -Ilibc/include -fno-pic -fno-pie -fno-stack-protector -c prog.c
gcc -m32 -nostdlib -static -no-pie -T libc/user.ld \
    libc/build/crt0.o prog.o libc/build/libc.a -o prog
```

### With CMake

```sh
cmake -S . -B build -G Ninja            # picks cmake/toolchain-i686.cmake itself
cmake --build build                     # libc.a, hello, hello.bin
cmake -S . -B build -DLIBC_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
```

In your own project, put this directory next to your sources:

```cmake
cmake_minimum_required(VERSION 3.16)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/libc/cmake/toolchain-i686.cmake)
project(myapp C ASM)

add_subdirectory(libc)
libc_add_program(myapp main.c util.c)   # -> myapp (ELF) and myapp.bin (flat)
```

`libc_add_program` links crt0, `user.ld` and `libc.a` with the right flags.
For a target you create yourself, `target_link_libraries(t PRIVATE libc::libc)`
brings the same in. The toolchain uses `i686-elf-gcc` when it is on PATH,
otherwise `gcc -m32`; override with `-DCMAKE_C_COMPILER=clang`. Change the
load address with `-DLIBC_IMAGE_BASE=0x...`.

No libgcc is needed: the 64-bit division helpers GCC emits on i386 are
included (`src/divdi3.c`).

## What's in it

| Header | Contents |
|---|---|
| `stdio.h` | `FILE` streams (buffered, mixed read/write/seek), `fopen` `fdopen` `freopen` `fread` `fwrite` `fgets` `getline` `fseek` `ftell`…, the full `printf` family incl. `snprintf` `asprintf` `dprintf`, `remove` `rename` `perror` |
| `stdlib.h` | `malloc` `calloc` `realloc` `free` `memalign` `posix_memalign` `aligned_alloc`, `strtol`/`strtoull`/`strtod` family, `qsort` `bsearch`, `rand`, `exit` `atexit` `abort` |
| `string.h` | the usual `mem*`/`str*` set plus `strdup` `strndup` `strtok_r` `strlcpy` `strlcat` `strerror` |
| `unistd.h` `fcntl.h` | `open` `read` `write` `lseek` `close`, `unlink` `rmdir` `truncate` `access` `chdir` `getcwd` `isatty` `sbrk` `brk` `getpid` `sleep` `usleep` `_exit` |
| `sys/stat.h` `dirent.h` | `stat` `fstat` `mkdir`, `opendir` `readdir` `rewinddir` `closedir` (with `d_type`) |
| `ctype.h` `errno.h` `assert.h` `limits.h` `sched.h` | as standard (Linux errno numbering) |
| `os/os.h` | kernel-specific: `os_print`, `os_copy` (whole trees), `os_rename` (same directory), `os_sleep_ms`, `os_abspath` |

`printf` supports `%d i u o x X c s p n % f F e E g G a A`, all flags, `*`
width/precision and `hh h l ll j z t L`. Floats are converted exactly (every
digit of the binary value, ties to even), so output matches glibc digit for
digit; the test suite diffs ~100 cases against glibc.

### malloc

Boundary-tag allocator on `SYS_SBRK` (`src/malloc.c`): 16-byte aligned blocks
with 8 bytes overhead, 64 segregated free lists with a bitmap, merging on
free, in-place `realloc` growth, and the heap's free tail returned to the
kernel once it exceeds 256 KB. It tolerates other code calling `sbrk`
directly. `malloc_check()` (non-standard) walks the heap and verifies every
invariant, which is useful while debugging a program or the kernel's sbrk.

## How it maps onto the kernel

- **Console:** `stdout` (line buffered) and `stderr` (unbuffered) write to fd 1
  and 2, which the kernel prints. `stdin` exists, but reads fail with `EBADF`
  until the kernel has console input.
- **open flags:** `O_RDONLY/O_WRONLY/O_RDWR/O_CREAT/O_EXCL/O_TRUNC/O_APPEND` map
  to `SYS_O_*`. The mode argument is ignored (permissions are the kernel's).
- **stat:** the single read/write/execute set fills all three permission
  groups; a device root reports as a directory; `st_ctime` is creation time.
- **rename:** the kernel renames within one directory. Across directories a
  file is copied (`SYS_COPY`) then removed; moving a directory to another
  parent fails with `EXDEV`. Replacing an existing target works, though not
  atomically.
- **unlink/rmdir:** both use `SYS_REMOVE` after checking the type, so each
  gives the POSIX error for the wrong kind of object.

## Assumptions to check against the loader

Your process loader wasn't part of the code I saw, so two things are guesses:

1. **Load address.** `user.ld` links at `0x40000000` (`VM_USER_START`), with
   `_start` as the first byte of the image, so it works whether the loader
   reads ELF segments or copies a flat binary (`objcopy -O binary`). If the
   loader uses another base, link with `-Wl,--defsym=__image_base=0x…`.
   `.bss` is folded into `.data` so the file always contains its zeros and
   the heap (which starts right after the image) can never overlap it.
2. **Arguments.** `crt0` reads nothing from the initial stack, because if
   ESP starts exactly at `VM_USER_STACK_TOP`, reading above it would fault.
   `main` gets `argc == 0` and an empty `argv`/`environ`. Once the loader
   pushes arguments, read them in `__libc_start` (`src/start.c`).

## Natural next syscalls

These would let the standard functions that are missing or stubbed here work:

- console input on fd 0 → `stdin`, `getchar`, `fgets(stdin)`
- a clock (`SYS_TIME`/`SYS_CLOCK`) → `time.h`
- `SYS_SPAWN`/`exec` + `wait` → `system`, `posix_spawn`
- `SYS_FTRUNCATE`, `SYS_DUP` → `ftruncate`, `dup`/`dup2`, `freopen` on std streams
- a futex or mutex call → threads; then wrap malloc and stdio in locks
  (everything here assumes single-threaded processes)

## Tests

`test/` rebuilds the library with every syscall routed through
`linux_shim.c`, which emulates this kernel's syscall semantics on 32-bit
Linux. The test programs are then linked exactly like real programs
(`crt0.o`, `user.ld`, `libc.a`) and run natively:

- `fmt_cases.c` is built against this libc and against glibc, and the
  outputs must be identical (printf, strtod, strtol).
- `run_test.c` covers 40,000 rounds of random malloc/realloc/memalign with
  heap verification, stdio round trips, directories, rename, errno paths,
  qsort and the exit path.
