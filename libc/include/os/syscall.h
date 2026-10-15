
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
#define SYS_GFX_INFO   21   // (SysGfxInfo*)            0
#define SYS_GFX_CREATE 22   // (width, height)          surface handle (>= 1)
#define SYS_GFX_DESTROY 23  // (handle)                 0
#define SYS_GFX_SUBMIT 24   // (handle, cmds, bytes)    commands run
#define SYS_GFX_WRITE  25   // (handle, SysGfxRect*, pixels, stride)  0
#define SYS_GFX_READ   26   // (handle, SysGfxRect*, pixels, stride)  0
#define SYS_GFX_PRESENT 27  // (handle, SysGfxRect* or 0, x, y)       0
#define SYS_SEC_GET    28   // (pid or 0)               security mask
#define SYS_SEC_SET    29   // (pid or 0, mask)         previous mask (needs SYS_SEC_ADMIN)
#define SYS_IPC_CLOSE  30   // (handle)                 0
#define SYS_IPC_PIPE   31   // (int32_t out[2])         0
#define SYS_IPC_PORT_CREATE 32 // (name)                port handle
#define SYS_IPC_CONNECT 33  // (name)                   pipe handle
#define SYS_IPC_ACCEPT 34   // (port)                   pipe handle, SYS_EAGAIN if none
#define SYS_IPC_SEND   35   // (pipe, buf, length, attach or 0)        0
#define SYS_IPC_RECV   36   // (pipe, buf, size, SysIpcMsgInfo* or 0)  message length
#define SYS_IPC_WAIT   37   // (SysIpcWaitItem*, count, timeout_ms)    items ready
#define SYS_SHM_CREATE 38   // (size)                   shm handle
#define SYS_SHM_MAP    39   // (handle)                 address
#define SYS_SHM_UNMAP  40   // (handle)                 0
#define SYS_IPC_INFO   41   // (handle, SysIpcInfo*)    0
#define SYS_INPUT_OPEN 42   // ()                       input handle
#define SYS_INPUT_READ 43   // (handle, SysInputEvent*, max)           events read
#define SYS_RES_LOAD   44   // (path)                   0
#define SYS_RES_GET    45   // (name)                   resource handle (>= 1)
#define SYS_RES_INFO   46   // (handle, SysResInfo*)    0
#define SYS_COUNT      47

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
#define SYS_EPERM          -18  // Not allowed by the process's security mask
#define SYS_ESRCH          -19  // No such process
#define SYS_EAGAIN         -20  // Not now (nothing queued, queue full)
#define SYS_EPIPE          -21  // The other end of the pipe is closed
#define SYS_EBADFMT        -22  // Not a resource file this kernel can load

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

/* Security clearance bits (see os/security.h and the kernel's syscall.h) */
#define SYS_SEC_FILE_READ   0x00000001u
#define SYS_SEC_FILE_WRITE  0x00000002u
#define SYS_SEC_CONSOLE     0x00000004u
#define SYS_SEC_GFX         0x00000008u
#define SYS_SEC_GFX_SCREEN  0x00000010u
#define SYS_SEC_IPC         0x00000020u
#define SYS_SEC_INPUT       0x00000040u
#define SYS_SEC_ADMIN       0x00000100u
#define SYS_SEC_NONE        0x00000000u
#define SYS_SEC_VALID       (SYS_SEC_FILE_READ | SYS_SEC_FILE_WRITE | SYS_SEC_CONSOLE | \
                             SYS_SEC_GFX | SYS_SEC_GFX_SCREEN | SYS_SEC_IPC | \
                             SYS_SEC_INPUT | SYS_SEC_ADMIN)

typedef struct {
    uint32_t size;
    uint32_t type;
    uint32_t permissions;
    uint32_t created;
    uint32_t modified;
} SysStat;

_Static_assert(sizeof(SysStat) == 20, "SysStat is part of the kernel ABI");

/* Graphics surfaces and the SYS_GFX_SUBMIT command format: see the
   "Graphics" section of kernel/syscall/syscall.h, and os/gfx.h for the
   library on top of them. */
#define SYS_GFX_SURFACE_MAX     16              // Surfaces per process
#define SYS_GFX_MAX_DIM         4096            // Largest surface width / height
#define SYS_GFX_PROCESS_BYTES   (32U << 20)     // Pixel memory per process
#define SYS_GFX_SUBMIT_MAX      65536           // Bytes per SYS_GFX_SUBMIT
#define SYS_GFX_COORD_MAX       8191
#define SYS_GFX_SIZE_MAX        16384
#define SYS_GFX_RADIUS_MAX      8191
#define SYS_GFX_DEPTH_MAX       4096            // Isometric pie depth
#define SYS_GFX_TEXT_MAX        255             // Bytes per text command
#define SYS_GFX_PIE_MAX         32              // Slices per pie
#define SYS_GFX_STRIDE_MAX      65536           // Pixels per row in user buffers

//      Op                          Arguments
#define SYS_GFX_CMD_CLEAR        1  // color
#define SYS_GFX_CMD_PIXEL        2  // x y color
#define SYS_GFX_CMD_LINE         3  // x0 y0 x1 y1 color
#define SYS_GFX_CMD_RECT         4  // x y w h color                     (outline)
#define SYS_GFX_CMD_FILL_RECT    5  // x y w h color
#define SYS_GFX_CMD_GRADIENT     6  // x y w h from to direction (SYS_GFX_GRADIENT_*)
#define SYS_GFX_CMD_CIRCLE       7  // cx cy radius color
#define SYS_GFX_CMD_FILL_CIRCLE  8  // cx cy radius color
#define SYS_GFX_CMD_TEXT         9  // x y fg bg flags length, then length bytes (padded to a word)
#define SYS_GFX_CMD_CHAR        10  // x y char fg bg flags
#define SYS_GFX_CMD_PIE         11  // cx cy radius depth flags count, values[count], colors[count]
#define SYS_GFX_CMD_BLIT        12  // src_handle sx sy sw sh x y flags key
#define SYS_GFX_CMD_PIXELS      13  // user_address width height stride x y flags key
#define SYS_GFX_CMD_SET_CLIP    14  // x y w h flags
#define SYS_GFX_CMD_INTERSECT_CLIP 15 // x y w h
#define SYS_GFX_CMD_SET_ORIGIN  16  // x y                               (result within COORD_MAX)
#define SYS_GFX_CMD_TRANSLATE   17  // dx dy                             (result within COORD_MAX)
#define SYS_GFX_CMD_SET_BLEND   18  // SYS_GFX_BLEND_*
#define SYS_GFX_CMD_TEXT_SCALE  19  // 1 .. 8
#define SYS_GFX_CMD_SAVE        20  //
#define SYS_GFX_CMD_RESTORE     21  //
#define SYS_GFX_CMD_RESOURCE    22  // resource_handle sx sy sw sh x y flags key   (see "Resources")

#define SYS_GFX_CMD_HEADER(op, words)   ((uint32_t)(op) | ((uint32_t)(words) << 16))

// Command flags
#define SYS_GFX_F_OPAQUE        0x01    // TEXT, CHAR: fill each cell with bg first
#define SYS_GFX_F_ISOMETRIC     0x01    // PIE: tilted, with a `depth` pixel side
#define SYS_GFX_F_WHOLE         0x01    // BLIT: the whole source (sx sy sw sh ignored)
#define SYS_GFX_F_KEYED         0x02    // BLIT, PIXELS: skip pixels equal to key
#define SYS_GFX_F_NO_CLIP       0x01    // SET_CLIP: clip to the whole surface
#define SYS_GFX_F_ALPHA         0x04    // RESOURCE: blend by alpha, whatever the blend mode

#define SYS_GFX_BLEND_COPY      0
#define SYS_GFX_BLEND_ALPHA     1
#define SYS_GFX_GRADIENT_VERTICAL   0
#define SYS_GFX_GRADIENT_HORIZONTAL 1

typedef struct {
    int32_t x, y, w, h;
} SysGfxRect;

// Filled by SYS_GFX_INFO. 32 bytes.
typedef struct {
    uint32_t width;         // Screen size in pixels (0 when there is no display)
    uint32_t height;
    uint32_t bpp;           // 32
    uint32_t char_width;    // Built-in font cell at text scale 1
    uint32_t line_height;
    uint32_t max_dim;       // SYS_GFX_MAX_DIM
    uint32_t max_surfaces;  // SYS_GFX_SURFACE_MAX
    uint32_t max_bytes;     // SYS_GFX_PROCESS_BYTES
} SysGfxInfo;

_Static_assert(sizeof(SysGfxRect) == 16 && sizeof(SysGfxInfo) == 32, "gfx structures are part of the kernel ABI");

//
// IPC: pipes, ports and shared memory
//
// IPC objects are reached through HANDLES, private to the process, numbered
// 1 .. SYS_IPC_HANDLE_MAX (their own numbering, apart from file descriptors
// and gfx surfaces). Every handle is closed when the process exits.
//
// PIPE: two connected ends. Each end sends whole messages (1 ..
// SYS_IPC_MSG_MAX bytes) to the other and receives the ones sent to it, in
// order. Messages are never split or merged. A message may carry one handle
// (a pipe end or shared memory, not a port or input): the receiver gets a
// new handle to the same object, the sender keeps its own. An end cannot be
// sent through its own pipe (SYS_EINVAL), nor while messages queued for it
// carry pipe ends themselves (SYS_EBUSY: receive those first). That keeps
// ends from holding each other in a loop that could never be freed.
//   send: SYS_EAGAIN when the peer's queue is full (SYS_IPC_QUEUE_MAX
//         messages), SYS_EPIPE once the peer end is closed.
//   recv: the message length; SYS_EAGAIN when nothing is queued, SYS_EPIPE
//         when nothing is queued and the peer is closed (end of stream).
//         A buffer too small is SYS_ERANGE and the message STAYS queued;
//         its length is in SysIpcMsgInfo.length when info was given. If the
//         message carries a handle and this process has no free handle,
//         SYS_EMFILE, and the message stays queued too.
// Nothing blocks: wait for a handle to become ready with SYS_IPC_WAIT.
//
// PORT: a named rendezvous ("dwm", "audio.mixer"). Names are 1 ..
// SYS_IPC_NAME_MAX - 1 characters of [A-Za-z0-9._-], unique system-wide
// (SYS_EEXIST); names starting "sys." need SYS_SEC_ADMIN. SYS_IPC_CONNECT
// makes a new pipe, queues one end on the port (up to SYS_IPC_BACKLOG,
// SYS_EAGAIN beyond, SYS_ENOENT if no such port) and returns the other.
// The port's owner takes queued ends with SYS_IPC_ACCEPT. Closing the port
// handle removes the name; queued ends are closed (their clients see
// SYS_EPIPE).
//
// SHARED MEMORY: SYS_SHM_CREATE(size) makes a zeroed block of whole pages
// (at most SYS_SHM_MAX bytes; SYS_SHM_TOTAL_MAX for all blocks together).
// SYS_SHM_MAP maps it into this process, read-write, and returns the
// address (above 0x7FFFFFFF: test results with SYS_IS_ERROR()). Mapping a handle that is already mapped returns the same
// address. Pass the handle to another process in a message; it maps its own
// copy of the handle. Closing a handle unmaps it. The memory is freed when
// the last handle to it is closed.
//
// SYS_IPC_WAIT: block until at least one item is ready, the timeout passes
// (milliseconds; 0 = just poll, SYS_IPC_WAIT_FOREVER = no timeout) or the
// process is ending. Each item names a handle and the events wanted; the
// kernel fills `ready` for every item and returns how many items have a
// non-zero `ready` (0 on timeout). SYS_EBADF if any handle is not open.
//   PIPE   READABLE: a message is queued, or the peer is closed
//          WRITABLE: send would not fail with SYS_EAGAIN
//          HANGUP:   the peer is closed (always reported, even unasked)
//   PORT   READABLE: a connection is waiting for SYS_IPC_ACCEPT
//   INPUT  READABLE: an input event is waiting
//   SHM    never ready
// Waiting on a pipe, port or shm needs SYS_SEC_IPC, on input SYS_SEC_INPUT.
//

#define SYS_IPC_HANDLE_MAX      64              // Handles per process
#define SYS_IPC_MSG_MAX         512             // Bytes per message
#define SYS_IPC_QUEUE_MAX       64              // Messages queued per direction
#define SYS_IPC_GLOBAL_BYTES    (4U << 20)      // Message bytes queued system-wide
#define SYS_IPC_NAME_MAX        32              // Port name bytes, terminator included
#define SYS_IPC_BACKLOG         16              // Connections waiting per port
#define SYS_IPC_WAIT_MAX        32              // Items per SYS_IPC_WAIT
#define SYS_IPC_WAIT_FOREVER    0xFFFFFFFFu
#define SYS_SHM_MAX             (16U << 20)     // Bytes per shared block
#define SYS_SHM_TOTAL_MAX       (64U << 20)     // Bytes in all shared blocks together

// SysIpcInfo.type
#define SYS_IPC_TYPE_PIPE       1
#define SYS_IPC_TYPE_PORT       2
#define SYS_IPC_TYPE_SHM        3
#define SYS_IPC_TYPE_INPUT      4

// SysIpcWaitItem.events / .ready
#define SYS_IPC_READABLE        0x0001
#define SYS_IPC_WRITABLE        0x0002
#define SYS_IPC_HANGUP          0x0004

// SysIpcInfo.flags
#define SYS_IPC_INFO_PEER_CLOSED 0x0001         // Pipe: the other end is closed
#define SYS_IPC_INFO_MAPPED      0x0002         // Shm: this handle is mapped

// Filled by SYS_IPC_RECV when given. 16 bytes.
typedef struct {
    uint32_t length;        // Message length (also on SYS_ERANGE)
    int32_t  handle;        // Handle that came with the message, 0 if none
    uint32_t sender;        // Process id that sent it
    uint32_t reserved;
} SysIpcMsgInfo;

// One SYS_IPC_WAIT item. 8 bytes.
typedef struct {
    int32_t  handle;
    uint16_t events;        // SYS_IPC_READABLE | SYS_IPC_WRITABLE
    uint16_t ready;         // Set by the kernel
} SysIpcWaitItem;

// Filled by SYS_IPC_INFO. 32 bytes.
typedef struct {
    uint32_t type;          // SYS_IPC_TYPE_*
    uint32_t flags;         // SYS_IPC_INFO_*
    uint32_t size;          // Shm: bytes. Pipe: bytes queued to read.
    uint32_t address;       // Shm: where this handle is mapped, 0 if not
    uint32_t pending;       // Pipe: messages queued to read. Port: connections waiting.
    uint32_t reserved[3];
} SysIpcInfo;

//
// Input: keyboard and mouse
//
// SYS_INPUT_OPEN takes the keyboard and mouse for this process: from then
// on every key press and mouse change goes to it instead of the window
// manager, which stops drawing until the handle is closed (or the process
// ends) and then repaints the whole screen. One owner at a time: a second
// SYS_INPUT_OPEN is SYS_EBUSY. The first event read is always a mouse event
// with the current position and buttons.
//
// SYS_INPUT_READ copies up to `max` (at most SYS_INPUT_READ_MAX) events and
// returns how many; 0 when none are waiting. Wait for input with
// SYS_IPC_WAIT (READABLE). Mouse and key events are in order among
// themselves; a burst read in one call returns the mouse events first.
//
// Keys: presses only (releases are not reported); `key` is the character
// (ASCII, Shift applied) or one of SYS_KEY_*. `mods` holds the modifier
// keys at the time the event is read, `x` `y` `buttons` the mouse state.
//

#define SYS_INPUT_READ_MAX      32

#define SYS_INPUT_MOUSE         1
#define SYS_INPUT_KEY           2

#define SYS_INPUT_BTN_LEFT      0x01
#define SYS_INPUT_BTN_RIGHT     0x02
#define SYS_INPUT_BTN_MIDDLE    0x04

#define SYS_INPUT_MOD_SHIFT     0x01
#define SYS_INPUT_MOD_CTRL      0x02
#define SYS_INPUT_MOD_ALT       0x04

#define SYS_KEY_BACKSPACE       0x0001
#define SYS_KEY_ENTER           0x0002
#define SYS_KEY_TAB             0x0009
#define SYS_KEY_ESCAPE          0x001B
#define SYS_KEY_HOME            0x4700
#define SYS_KEY_UP              0x4800
#define SYS_KEY_PAGE_UP         0x4900
#define SYS_KEY_LEFT            0x4B00
#define SYS_KEY_RIGHT           0x4D00
#define SYS_KEY_END             0x4F00
#define SYS_KEY_DOWN            0x5000
#define SYS_KEY_PAGE_DOWN       0x5100
#define SYS_KEY_DELETE          0x5300

// One input event. 16 bytes.
typedef struct {
    uint16_t type;          // SYS_INPUT_MOUSE / SYS_INPUT_KEY
    uint16_t key;           // KEY: the key (see above), MOUSE: 0
    int16_t  x, y;          // Mouse position, screen coordinates
    uint8_t  buttons;       // SYS_INPUT_BTN_*
    uint8_t  mods;          // SYS_INPUT_MOD_*
    uint16_t reserved;
    uint32_t time_ms;       // Milliseconds since boot (mouse); read time (keys)
} SysInputEvent;

_Static_assert(sizeof(SysIpcMsgInfo) == 16 && sizeof(SysIpcWaitItem) == 8 &&
               sizeof(SysIpcInfo) == 32 && sizeof(SysInputEvent) == 16,
               "IPC structures are part of the kernel ABI");

//
// Resources
//
// SYS_RES_LOAD reads a resource file (the kernel's RSRC format, see
// resources/resource_manager.h) into this process and registers it under
// its NAME: the file's own name, the last part of the path, exactly as the
// window manager names the images in sys/images ("/x/images/icon_folder"
// is "icon_folder"). SYS_RES_GET turns a name into a handle, 1 ..
// SYS_RES_MAX, which SYS_GFX_CMD_RESOURCE draws from and SYS_RES_INFO
// describes. Names and handles are private to the process; everything it
// loaded is freed when it exits.
//
// Loading the same file again is a no-op (0). Another file with the same
// name is SYS_EEXIST. A path that is not a file is SYS_ENOENT / SYS_EISDIR;
// a file that is not a resource, holds a type this kernel cannot load, is
// damaged, or is larger than SYS_GFX_MAX_DIM either way is SYS_EBADFMT.
// The decoded image counts against the same pixel budget as surfaces
// (SYS_GFX_PROCESS_BYTES), so SYS_ENOMEM past that.
//
// Supported: sprites (SYS_RES_TYPE_SPRITE), decoded to 0xAARRGGBB pixels.
// Sprites mark see-through pixels with opaque black, SYS_GFX_SPRITE_KEY;
// the window manager draws them keyed on it and alpha blended, and so do
// the libc's gfx_draw_resource* (KEYED | ALPHA, key SYS_GFX_SPRITE_KEY).
//
// SYS_GFX_CMD_RESOURCE takes the same arguments as SYS_GFX_CMD_BLIT, with
// a resource handle as the source, plus SYS_GFX_F_ALPHA.
//
// Load needs SYS_SEC_FILE_READ and SYS_SEC_GFX; get and info SYS_SEC_GFX.
//

#define SYS_RES_MAX             64      // Resources per process
#define SYS_RES_NAME_MAX        64      // Name bytes, terminator included

#define SYS_RES_TYPE_SPRITE     1       // = RESOURCE_TYPE_SPRITE

#define SYS_GFX_SPRITE_KEY      0xFF000000u

// Filled by SYS_RES_INFO. 16 bytes.
typedef struct {
    uint32_t type;          // SYS_RES_TYPE_*
    uint32_t width;         // Pixels
    uint32_t height;
    uint32_t reserved;
} SysResInfo;

_Static_assert(sizeof(SysResInfo) == 16, "SysResInfo is part of the kernel ABI");

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

