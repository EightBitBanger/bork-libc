
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
| `os/security.h` | process security clearance: `sec_get` `sec_has` `sec_is_admin`, `sec_get_process` `sec_set_process` `sec_grant` `sec_revoke` `sec_drop`, `sec_format` `sec_parse` |
| `os/gfx.h` | 2D graphics on the kernel's gfx layer: surfaces, shapes, gradients, pie charts, text, blits, images, presenting to the screen |
| `os/ipc.h` | message pipes, named ports, shared memory blocks, waiting on several handles: `ipc_pipe` `ipc_port_create` `ipc_connect` `ipc_accept` `ipc_send` `ipc_recv` `ipc_wait`, blocking `*_wait` helpers, `shm_create` `shm_map` `shm_unmap` |
| `os/input.h` | keyboard and mouse: `input_open` `input_read` `input_wait` `input_close` |
| `os/resource.h` | resource files: `resource_load` (by path), `resource_get` (by name), `resource_info`; draw with `gfx_draw_resource` / `gfx_draw_resource_rect` |

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

### Graphics (`os/gfx.h`)

```c
gfx_surface_t* s = gfx_surface_create(320, 200);
gfx_fill_gradient(s, 0, 0, 320, 200, GFX_RGB(20, 24, 48), GFX_RGB(60, 30, 70), GFX_GRADIENT_VERTICAL);
gfx_fill_circle(s, 160, 100, 40, GFX_RGB(80, 200, 140));
gfx_draw_text(s, 8, 8, "hello", GFX_COLOR_WHITE);
gfx_present(s, 100, 100);       // one system call for the whole frame
```

A surface is a block of 0xAARRGGBB pixels the kernel owns, with the same
drawing state as a kernel `GfxContext`: clip, origin, blend mode (copy or
alpha), text scale and an 8-deep save/restore stack. Drawing calls are
recorded in a 4 KB command buffer in the program and executed by the
kernel's own gfx code (`gfx_fill_rect`, `gfx_draw_pie`, ...) when the buffer
fills, on `gfx_flush()`, or on `gfx_present()`. Drawing calls return
nothing; a command the kernel rejects (out-of-range value, bad blit source)
is skipped and reported by the next flush/present as `-1` + `errno`.

- **Pixels in and out:** `gfx_write_pixels` / `gfx_read_pixels` copy raw
  rectangles (no clip or blend). `gfx_draw_pixels[_keyed]` draws an image
  from program memory *through* the clip, origin, blend and color key.
- **Blits** between two surfaces flush both, so they see exactly what was
  drawn before the call. Blitting a surface onto itself is refused.
- **Text** uses the kernel's built-in 6×8 font, scaled 1–8×;
  `gfx_char_width` / `gfx_line_height` / `gfx_text_width` measure it.
- **Limits** (`os/syscall.h`): 16 surfaces and 32 MB of pixels per process
  (64 MB system-wide), 4096×4096 per surface, coordinates within ±8191.
  Everything is freed when the process exits.
- **Presenting:** goes to the window manager when it implements the
  `dwm_present_user_surface` hook (see the kernel's `sys_gfx.h`), otherwise
  straight into the display back buffer and VRAM.

Not thread-safe per surface; use one surface from one thread at a time.

### IPC (`os/ipc.h`)

```c
// Server                                  // Client
int port = ipc_port_create("echo");         int p = ipc_connect("echo");
int c = ipc_accept_wait(port);              ipc_send_wait(p, "hi", 2, 0);
char buf[IPC_MSG_MAX];                      ipc_recv_wait(p, buf, sizeof buf, NULL);
ssize_t n = ipc_recv_wait(c, buf, sizeof buf, NULL);
ipc_send_wait(c, buf, n, 0);
```

- **Handles** are small ints, 64 per process, separate from file
  descriptors; all closed when the process exits.
- **Pipes** carry whole messages of 1–512 bytes, both ways, in order, at
  most 64 queued per direction. `ipc_send`/`ipc_recv` never block (`EAGAIN`);
  the `*_wait` helpers and `ipc_wait` do. After the peer closes, the reader
  drains what is queued and then gets `EPIPE`. A message can carry one
  handle (a pipe end or shared memory); the receiver gets its own handle.
- **Ports** are system-wide names (`[A-Za-z0-9._-]`, up to 31 characters);
  `sys.*` names need `SEC_ADMIN`. `ipc_connect` makes a pipe and queues the
  server's end on the port (16 deep) for `ipc_accept`.
- **Shared memory:** `shm_create(size)` gives zeroed whole pages (16 MB per
  block, 64 MB system-wide); `shm_map` maps them read-write. Send the handle
  in a message and the other process maps the same pages. Freed with the
  last handle.
- **`ipc_wait`** takes up to 32 `{handle, events}` items (readable,
  writable; hangup is always reported) and a timeout (0 polls, `IPC_FOREVER`).

### Input (`os/input.h`)

```c
int in = input_open();                      // Needs SEC_INPUT
input_event_t ev[INPUT_READ_MAX];
while (input_wait(in, IPC_FOREVER) > 0) {
    int n = input_read(in, ev, INPUT_READ_MAX);
    ...                                     // ev[i].type: INPUT_MOUSE / INPUT_KEY
}
input_close(in);
```

`input_open` takes the keyboard and mouse for this process alone (`EBUSY`
if another has them). The window manager stops reading input and stops
drawing until the handle is closed or the process exits, then repaints the
whole screen, so a program that takes the input usually draws full screen
(`SEC_GFX_SCREEN`). The first event is always a mouse event with the current
position and buttons. Keys are presses only, with Shift applied (`KEY_*` for
Esc, Enter, arrows ...); `mods` holds Shift/Ctrl/Alt.
`examples/input_demo.c` is a complete program: a background and a box to
drag around.

### Resources (`os/resource.h`)

```c
resource_load("/mnt/ata0p1/sys/images/icon_folder");   // by path
int icon = resource_get("icon_folder");                 // by name
gfx_draw_resource(surface, icon, 10, 10);               // like a desktop icon
```

The kernel reads the file with its resource manager (the RSRC format the
window manager's icons use), decodes it, and keeps it for this process
under the file's name, the last part of the path. Loading the same file
twice is harmless; a different file with a name in use is `EEXIST`. A file
that is not a loadable resource is `ENOEXEC`. Up to 64 per process, freed
at exit, counted against the same pixel budget as surfaces. Only sprites
exist so far. `gfx_draw_resource[_rect]` blends by alpha and skips opaque
black (the sprite format's see-through color), as the desktop does, without
changing the surface's blend mode. Loading needs `SEC_FILE_READ` and
`SEC_GFX`. `examples/sprite_demo.c` is a complete program.

### Security clearance (`os/security.h`)

Every process has a mask of clearance bits, checked by the kernel on every
system call. A call the mask does not allow fails with `EPERM` and has no
effect, so `fopen("x", "w")` without `SEC_FILE_WRITE` returns `NULL`/`EPERM`.

| Bit | Allows |
|---|---|
| `SEC_FILE_READ` | open for reading, `read`, `stat`/`fstat`, `opendir`/`readdir`, `chdir` |
| `SEC_FILE_WRITE` | open for writing/create/truncate/append, writing to a file, `mkdir`, `remove`/`unlink`/`rmdir`, `rename`, `truncate` (`os_copy` needs both file bits) |
| `SEC_CONSOLE` | output to stdout/stderr, `os_print` |
| `SEC_GFX` | `os/gfx.h` surfaces, presenting into the program's window via the window manager |
| `SEC_GFX_SCREEN` | presenting straight onto the display when no window manager takes the surface |
| `SEC_IPC` | `os/ipc.h`: pipes, ports, shared memory |
| `SEC_INPUT` | `os/input.h`: taking the keyboard and mouse |
| `SEC_ADMIN` | changing any process's mask (its own included), reading other processes' masks |

Always available: exit, `malloc`/`sbrk`, sleep, yield, `getpid`, `close`,
`lseek`, `getcwd`, `ipc_close`, `ipc_info`, and reading the process's own mask. Bits are independent:
an admin is not automatically allowed everything, but may grant itself any
bit. Revoking a bit also stops descriptors and surfaces already open.

```c
if (sec_is_admin()) {
    sec_revoke(worker_pid, SEC_FILE_WRITE);   // another process
    sec_drop(SEC_GFX_SCREEN | SEC_ADMIN);     // this one, for good
}
char text[96];
sec_format(sec_get(), text, sizeof text);     // "file_read|file_write|console|gfx"
```

Only the kernel decides who starts with `SEC_ADMIN`
(`CreateProcessWithSecurity` in the kernel's `scheduler/process.h`); plain
`CreateProcess` gives file, console, gfx and IPC bits.

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
- `SYS_SPAWN`/`exec` + `wait` → `system`, `posix_spawn` (a child should get
  at most its parent's security mask, unless the parent is an admin)
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
- `gfx_test.c` runs `os/gfx.h` against the kernel's real gfx code and
  command interpreter (`gfx/*.c`, `syscall/calls/sys_gfx_core.h`), built
  into the shim with a few stub headers from `test/kstub`. It checks the
  drawing results pixel by pixel, error reporting, limits and presenting,
  fuzzes the interpreter with 20,000 random command batches (verifying the
  heap is intact afterwards), and writes `gfx_test.ppm` to look at. It
  needs the kernel sources, by default one level above this folder
  (`make KERNEL_SRC=...` or `-DLIBC_KERNEL_SRC=...` otherwise); without
  them it is skipped.
- `ipc_test.c` runs `os/ipc.h` and `os/input.h` against the kernel's real
  `ipc/ipc.c`, with the platform layer under it emulated in `ipc_shim.c`
  (several processes in one, a fake clock, a scripted input queue). It
  covers pipes, ports, shared memory, handle passing, waits woken by another
  "process" or an "IRQ", process exit, the input grab and permissions, then
  fuzzes 20,000 random calls from three processes and checks that every
  kernel allocation was freed.
- `resource_test.c` writes sprite files byte by byte from the documented
  format and loads them through the kernel's real resource manager, sprite
  class and decoder (on a VFS stand-in over host files): naming, repeat
  loads, name clashes, missing/damaged/wrong-type/oversized files, limits,
  permissions, and the drawn pixels (keying, alpha, part draws, clipping,
  presenting).
- `input_demo_test.c` runs `examples/input_demo.c` unchanged with scripted
  mouse and keyboard input and checks the emulated screen pixel by pixel.
- `run_test.c` covers 40,000 rounds of random malloc/realloc/memalign with
  heap verification, stdio round trips, directories, rename, errno paths,
  qsort, security masks (the shim enforces the kernel's per-call rules) and
  the exit path.

