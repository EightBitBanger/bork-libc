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
#define WM_PROTOCOL_VERSION     2
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
    };
} wm_message_t;

_Static_assert(sizeof(wm_request_t) <= 512 && sizeof(wm_menu_request_t) <= 512 &&
               sizeof(wm_message_t) <= 512, "protocol messages must fit IPC_MSG_MAX");
_Static_assert(sizeof(wm_request_t) != sizeof(wm_menu_request_t),
               "the dwm tells requests apart by size");

#endif
