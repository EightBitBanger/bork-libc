#ifndef _OS_WM_PROTO_H
#define _OS_WM_PROTO_H

/*
 * Window manager protocol: what goes over the pipe between a program and
 * the "dwm" process. Programs normally use os/wm.h instead, which speaks
 * this for them; the window manager (src/dwm/server.c) is the other end.
 *
 *   connect      ipc_connect(WM_PORT_NAME). The port name starts "sys.",
 *                so only an administrator process can register it: no
 *                other program can pose as the window manager.
 *   greeting     the dwm sends WM_MSG_WELCOME (protocol version, screen
 *                size) as soon as it accepts the connection.
 *   requests     the program sends wm_request_t, one per message, exactly
 *                sizeof(wm_request_t) bytes. A non-zero seq asks for a
 *                WM_MSG_REPLY with the same seq; seq 0 gets a reply only
 *                for CREATE (which always answers).
 *   events       the dwm sends WM_MSG_EVENT for each window event (input,
 *                focus, moves, resizes, close requests).
 *   pixels       each window has a buffer of 0xAARRGGBB pixels in shared
 *                memory (stride = buffer width), handed to the dwm with
 *                WM_REQ_ATTACH (the shm handle rides along with the
 *                message). The program draws into it and sends
 *                WM_REQ_COMMIT for the part that changed; the dwm then
 *                repaints that part of the window from the buffer.
 *   goodbye      closing the pipe (or the program exiting) destroys every
 *                window the program has.
 *   theme        WM_REQ_THEME is answered with the dwm's colors in the
 *                reply's `theme`; WM_EVENT_THEME_CHANGED (window 0) tells
 *                every program when they change.
 *   notify       WM_REQ_NOTIFY (a wm_notify_request_t) shows a notification
 *                in the dwm's corner stack; it needs no window. Replied to
 *                like wm_request_t when seq is non-zero.
 *   clipboard    WM_REQ_CLIP_SET / GET / INFO / CLEAR (a wm_clip_request_t):
 *                one typed item for the whole desktop. SET carries the data
 *                in a shared memory block attached to the message (the dwm
 *                copies it and lets go); the reply to GET carries a fresh
 *                block holding a copy. WM_EVENT_CLIP_CHANGED (window 0,
 *                param = the new serial) tells every program it changed.
 *   files        WM_REQ_FS_CHANGED (a wm_path_request_t): a program that
 *                changed files (copied, moved, deleted) says where; every
 *                program gets WM_EVENT_FS_CHANGED (window 0) to re-read what
 *                it shows, and the desktop re-reads its icons.
 *   menus        a window created WM_STYLE_CONTEXT_MENU gets right clicks in
 *                its client area as WM_EVENT_CONTEXT_MENU (instead of the
 *                dwm opening its default window menu there) and answers with
 *                a wm_menu_request_t: the dwm draws the menu and sends
 *                WM_EVENT_MENU_COMMAND with the chosen item's id.
 *
 * Window ids are the dwm's own handles. A program can only touch windows it
 * created over its own connection.
 */

#include <stdint.h>

#define WM_PORT_NAME            "sys.dwm"
#define WM_PROTOCOL_VERSION     1           // Bumped on every protocol change once 0.1.0 is out
#define WM_TITLE_MAX            128         // Bytes, terminator included

// Position the window manager picks (cascading)
#define WM_POS_DEFAULT          INT32_MIN

// Styles: same meaning and values as DWM_STYLE_* (src/dwm/window.h)
#define WM_STYLE_TOPMOST        0x0001      // Always above other windows
#define WM_STYLE_NOBORDER       0x0002      // No frame at all
#define WM_STYLE_NOTITLEBAR     0x0004      // Border only
#define WM_STYLE_NOCLOSEBOX     0x0008      // No close box
#define WM_STYLE_RESIZABLE      0x0010      // Resize grip
#define WM_STYLE_HIDDEN         0x0020      // Created hidden
#define WM_STYLE_NOFOCUS        0x0040      // Never takes the focus
// Protocol only (not a DWM_STYLE_*): right clicks in the client area go to
// the program as WM_EVENT_CONTEXT_MENU, the dwm's window menu stays shut
#define WM_STYLE_CONTEXT_MENU   0x0100
#define WM_STYLE_VALID          0x017F

// Context menus (wm_menu_request_t). Flags: same values as DWM_MENU_*.
#define WM_MENU_MAX_ITEMS       10
#define WM_MENU_LABEL_MAX       40          // Bytes, terminator included
#define WM_MENU_SEPARATOR       0x0001      // A dividing line (label, id unused)
#define WM_MENU_DISABLED        0x0002      // Dimmed, can't be chosen
#define WM_MENU_CHECKED         0x0004      // Check mark before the label

//
// Program -> dwm
//

enum {
    WM_REQ_CREATE = 1,      // x y w h min_w min_h style title -> REPLY.window
    WM_REQ_DESTROY,         // window
    WM_REQ_ATTACH,          // window, w h = buffer size; shm handle attached
    WM_REQ_COMMIT,          // window, x y w h (client coordinates) changed
    WM_REQ_SET_TITLE,       // window, title
    WM_REQ_MOVE,            // window, x y (frame top-left, screen)
    WM_REQ_RESIZE,          // window, w h (client size)
    WM_REQ_SHOW,            // window, style != 0: show, 0: hide
    WM_REQ_FOCUS,           // window: raise and focus
    WM_REQ_THEME,           // -> REPLY.theme: the dwm's colors (always answered)
    WM_REQ_MENU,            // wm_menu_request_t (not wm_request_t): open a menu
    WM_REQ_NOTIFY,          // wm_notify_request_t (not wm_request_t): a notification
    WM_REQ_CLIP_SET,        // wm_clip_request_t, shm attached when info.size > 0
    WM_REQ_CLIP_GET,        // wm_clip_request_t -> REPLY.clip, shm attached (ENOENT: empty)
    WM_REQ_CLIP_INFO,       // wm_clip_request_t -> REPLY.clip (type NONE: empty)
    WM_REQ_CLIP_CLEAR,      // wm_clip_request_t: info.serial (0: whatever is there)
    WM_REQ_FS_CHANGED,      // wm_path_request_t: files changed at or below path
};

typedef struct {
    uint32_t type;          // WM_REQ_*
    uint32_t seq;           // Echoed in the reply; 0 = no reply wanted
    uint32_t window;
    int32_t  x, y, w, h;
    int32_t  min_w, min_h;
    uint32_t style;
    char     title[WM_TITLE_MAX];
} wm_request_t;

typedef struct {
    uint32_t id;            // Sent back in WM_EVENT_MENU_COMMAND.param
    uint32_t flags;         // WM_MENU_*
    char     label[WM_MENU_LABEL_MAX];
} wm_menu_entry_t;

// WM_REQ_MENU: open a dwm context menu for `window` at x y (client
// coordinates); the dwm keeps it on the screen. Exactly
// sizeof(wm_menu_request_t) bytes. Replies like wm_request_t (status
// EINVAL when nothing could show: no items, window hidden).
typedef struct {
    uint32_t        type;   // WM_REQ_MENU
    uint32_t        seq;
    uint32_t        window;
    int32_t         x, y;
    uint32_t        count;  // 1 .. WM_MENU_MAX_ITEMS
    wm_menu_entry_t items[WM_MENU_MAX_ITEMS];
} wm_menu_request_t;

// Notifications (WM_REQ_NOTIFY). Kinds and STICKY: same values as DWM_NOTIFY_*.
#define WM_NOTIFY_INFO          0
#define WM_NOTIFY_ERROR         1           // The theme's danger colors
#define WM_NOTIFY_STICKY        0           // timeout_ms: stays until clicked
#define WM_NOTIFY_DEFAULT_MS    5000
#define WM_NOTIFY_MAX_MS        60000       // Longer timeouts are cut to this
#define WM_NOTIFY_TITLE_MAX     64          // Bytes, terminator included
#define WM_NOTIFY_TEXT_MAX      256

// Exactly sizeof(wm_notify_request_t) bytes. Each program has at most 3
// showing: a fourth pushes its oldest out. Status EINVAL for a bad kind.
typedef struct {
    uint32_t type;          // WM_REQ_NOTIFY
    uint32_t seq;
    uint32_t kind;          // WM_NOTIFY_*
    uint32_t timeout_ms;    // WM_NOTIFY_STICKY, or how long it shows
    char     title[WM_NOTIFY_TITLE_MAX];
    char     text[WM_NOTIFY_TEXT_MAX];      // Word wrapped, at most 4 lines shown
} wm_notify_request_t;

// Clipboard item types: same values as DWM_CLIP_* (src/dwm/clipboard.h)
#define WM_CLIP_NONE            0
#define WM_CLIP_TEXT            1           // Text bytes, no terminator needed
#define WM_CLIP_IMAGE           2           // width * height 0xAARRGGBB pixels, row after row
#define WM_CLIP_BINARY          3           // Raw bytes; `format` names them
#define WM_CLIP_FILES           4           // Absolute paths, '\n' between them
#define WM_CLIP_USER            0x100       // And up: types programs agree on

#define WM_CLIP_CUT             0x0001      // FILES: move on paste (the paster clears it)
#define WM_CLIP_FORMAT_MAX      32          // Bytes, terminator included
#define WM_CLIP_MAX             (4u << 20)  // Bytes of data

typedef struct {
    uint32_t type;                          // WM_CLIP_*
    uint32_t flags;                         // WM_CLIP_CUT
    uint32_t size;                          // Bytes of data
    uint32_t width, height;                 // IMAGE: pixels
    uint32_t serial;                        // Changes with every set / clear (set by the dwm)
    uint32_t owner;                         // pid that set it, 0: the dwm (set by the dwm)
    char     format[WM_CLIP_FORMAT_MAX];    // Optional name for the data ("audio/wav")
} wm_clip_info_t;

// Exactly sizeof(wm_clip_request_t) bytes, for all four WM_REQ_CLIP_*
typedef struct {
    uint32_t       type;
    uint32_t       seq;
    wm_clip_info_t info;    // SET: describes the attached data. CLEAR: serial.
} wm_clip_request_t;

#define WM_PATH_MAX             400         // Bytes, terminator included

// Exactly sizeof(wm_path_request_t) bytes
typedef struct {
    uint32_t type;          // WM_REQ_FS_CHANGED
    uint32_t seq;
    char     path[WM_PATH_MAX];
} wm_path_request_t;

//
// dwm -> program
//

enum {
    WM_EVENT_NONE = 0,
    WM_EVENT_CLOSE,         // Close box (or dwm_window_close): destroy it, or ignore
    WM_EVENT_MOUSE_MOVE,    // x y (client coordinates), buttons held
    WM_EVENT_MOUSE_DOWN,    // button, clicks (2: double click)
    WM_EVENT_MOUSE_UP,      // button
    WM_EVENT_MOUSE_ENTER,
    WM_EVENT_MOUSE_LEAVE,
    WM_EVENT_KEY,           // key (character or KEY_*, os/input.h), mods
    WM_EVENT_FOCUS_GAINED,
    WM_EVENT_FOCUS_LOST,
    WM_EVENT_MOVED,         // rect: new frame rectangle (screen)
    WM_EVENT_RESIZED,       // rect.w rect.h: new client size
    WM_EVENT_SHOW,
    WM_EVENT_HIDE,
    WM_EVENT_DESTROYED,     // The dwm destroyed the window (shutting down)
    WM_EVENT_CONTEXT_MENU,  // Right click in the client area (WM_STYLE_CONTEXT_MENU): x y
    WM_EVENT_MENU_COMMAND,  // A menu item was chosen: param = its id; x y = where it opened
    WM_EVENT_THEME_CHANGED, // window 0: the colors changed (ask again with WM_REQ_THEME)
    WM_EVENT_CLIP_CHANGED,  // window 0: the clipboard changed; param = its new serial
    WM_EVENT_FS_CHANGED,    // window 0: some program changed files: re-read folders shown
};

typedef struct {
    uint32_t type;          // WM_EVENT_*
    uint32_t window;
    int32_t  x, y;          // Mouse position (client coordinates)
    int32_t  rect_x, rect_y, rect_w, rect_h;
    uint32_t time_ms;
    uint16_t key;
    uint8_t  buttons;       // INPUT_BTN_* held
    uint8_t  button;        // MOUSE_DOWN / MOUSE_UP
    uint8_t  clicks;
    uint8_t  mods;          // INPUT_MOD_*
    uint8_t  reserved[2];
    uint32_t param;         // MENU_COMMAND: the item's id
} wm_event_t;

// The dwm's colors (0xAARRGGBB), field for field the dwm's dwm_theme_t
// (src/dwm/theme.h, which documents each one and its default)
typedef struct {
    struct {
        uint32_t background, icon_text, icon_shadow, icon_selected,
                 icon_selected_border, band_fill, band_border;
    } desktop;
    struct {
        uint32_t border, background, title_text, active_low, active_high,
                 inactive_low, inactive_high;
    } frame;
    struct {
        uint32_t background, background_danger, text, text_soft, text_value,
                 text_muted, text_mount, text_danger, accent, divider_soft;
    } client;
    struct {
        uint32_t fill, border, border_active, text, fill_pressed, border_pressed;
    } button;
    struct {
        uint32_t background, border, text, cursor, selection;
    } edit;
    struct {
        uint32_t background, border, separator, highlight, text;
    } menu;
    struct {
        uint32_t gradient_low, gradient_high;
    } taskbar;
} wm_theme_t;

enum {
    WM_MSG_WELCOME = 0x80,  // version, screen_w, screen_h
    WM_MSG_REPLY,           // seq, status, window
    WM_MSG_EVENT,           // event
};

typedef struct {
    uint32_t   kind;        // WM_MSG_*
    uint32_t   seq;         // REPLY
    int32_t    status;      // REPLY: 0, or an errno value (EINVAL, ENOMEM ...)
    uint32_t   window;      // REPLY: the window (CREATE: the new one)
    uint32_t   version;     // WELCOME
    int32_t    screen_w;    // WELCOME
    int32_t    screen_h;
    union {
        wm_event_t event;   // EVENT
        wm_theme_t theme;   // REPLY to WM_REQ_THEME
        wm_clip_info_t clip;// REPLY to WM_REQ_CLIP_GET / WM_REQ_CLIP_INFO
    };
} wm_message_t;

_Static_assert(sizeof(wm_request_t) <= 512 && sizeof(wm_menu_request_t) <= 512 &&
               sizeof(wm_message_t) <= 512, "protocol messages must fit IPC_MSG_MAX");
_Static_assert(sizeof(wm_notify_request_t) <= 512, "protocol messages must fit IPC_MSG_MAX");
_Static_assert(sizeof(wm_path_request_t) <= 512 && sizeof(wm_path_request_t) != sizeof(wm_request_t) &&
               sizeof(wm_path_request_t) != sizeof(wm_menu_request_t) &&
               sizeof(wm_path_request_t) != sizeof(wm_notify_request_t) &&
               sizeof(wm_path_request_t) != sizeof(wm_clip_request_t),
               "the dwm tells requests apart by size");
_Static_assert(sizeof(wm_clip_request_t) != sizeof(wm_request_t) &&
               sizeof(wm_clip_request_t) != sizeof(wm_menu_request_t) &&
               sizeof(wm_clip_request_t) != sizeof(wm_notify_request_t),
               "the dwm tells requests apart by size");
_Static_assert(sizeof(wm_request_t) != sizeof(wm_menu_request_t) &&
               sizeof(wm_notify_request_t) != sizeof(wm_request_t) &&
               sizeof(wm_notify_request_t) != sizeof(wm_menu_request_t),
               "the dwm tells requests apart by size");

#endif

