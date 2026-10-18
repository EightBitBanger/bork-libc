# libc for the bork kernel user space

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
| `os/os.h` | kernel-specific: `os_print`, `os_copy` (whole trees), `os_rename` (same directory), `os_sleep_ms`, `os_abspath`, `os_gethome` (home device root, also `$HOME`), `os_syspath` (`<home>/sys/...`), `os_time_ms` |
| `os/security.h` | process security clearance: `sec_get` `sec_has` `sec_is_admin`, `sec_get_process` `sec_set_process` `sec_grant` `sec_revoke` `sec_drop`, `sec_format` `sec_parse` |
| `os/gfx.h` | 2D graphics on the kernel's gfx layer: surfaces, shapes, gradients, pie charts, text, blits, images, presenting to the screen |
| `os/ipc.h` | message pipes, named ports, shared memory blocks, waiting on several handles: `ipc_pipe` `ipc_port_create` `ipc_connect` `ipc_accept` `ipc_send` `ipc_recv` `ipc_wait`, blocking `*_wait` helpers, `shm_create` `shm_map` `shm_unmap` |
| `os/wm.h` | windows from the `dwm` process: `wm_connect` `wm_window_create` `wm_window_surface` `wm_window_commit` `wm_wait_event` …; protocol in `os/wm_proto.h` |
| `os/input.h` | keyboard and mouse: `input_open` `input_read` `input_wait` `input_close` `input_warp` |
| `time.h` | time since boot: `clock_gettime` (`CLOCK_MONOTONIC`), `clock_getres`, `clock`, `nanosleep` (no calendar clock yet) |
| `os/resource.h` | resource files: `resource_load` (by path), `resource_get` (by name), `resource_info`, `resource_load_system` (`<home>/sys/images/<name>`); draw with `gfx_draw_resource` / `gfx_draw_resource_rect` |

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

### Windows (`os/wm.h`)

```c
wm_connect(5000);                           // Wait up to 5 s for the dwm
wm_window_desc_t d = { .title = "Hello", .x = WM_POS_DEFAULT, .y = WM_POS_DEFAULT,
                       .width = 320, .height = 200, .style = WM_STYLE_RESIZABLE };
wm_window_t win = wm_window_create(&d);
gfx_surface_t* s = wm_window_surface(win);  // Draw with os/gfx.h as usual
gfx_clear(s, GFX_RGB(20, 20, 32));
wm_window_commit(win, NULL);                // Window appears with its first commit
wm_event_t ev;
while (wm_wait_event(&ev, -1) > 0 && ev.type != WM_EVENT_CLOSE)
    if (ev.type == WM_EVENT_RESIZED) { /* redraw */ wm_window_commit(win, NULL); }
wm_disconnect();
```

The dwm registers the IPC port `sys.dwm` (a `sys.` name, so only an admin
process can claim it). Each window's pixels live in a shared-memory buffer
the program fills from its own gfx surface on commit; the dwm paints the
window from it and sends the window's events back down the pipe. The
program never draws on the screen, so it needs only `SEC_IPC` and `SEC_GFX`
(both in the default clearance). On resize the library makes a new buffer
(old picture copied in) before handing out `WM_EVENT_RESIZED`. Closing the
connection, or exiting, closes every window. `examples/wm_demo.c` is a
complete program; `test/wm_test.c` runs it all against the kernel's `ipc.c`
and the DWM's own sources.

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
