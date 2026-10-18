#ifndef _OS_WM_H
#define _OS_WM_H

/*
 * Windows from the window manager ("dwm" process), for any program.
 *
 *     if (wm_connect(2000) < 0) ...                 // wait up to 2 s for the dwm
 *     wm_window_desc_t d = { .title = "Hello", .x = WM_POS_DEFAULT, .y = WM_POS_DEFAULT,
 *                            .width = 320, .height = 200, .style = WM_STYLE_RESIZABLE };
 *     wm_window_t win = wm_window_create(&d);
 *     gfx_surface_t* s = wm_window_surface(win);
 *     gfx_clear(s, GFX_RGB(20, 20, 32));
 *     gfx_draw_text(s, 8, 8, "hello", GFX_COLOR_WHITE);
 *     wm_window_commit(win, NULL);                  // the window appears now
 *
 *     wm_event_t ev;
 *     while (wm_wait_event(&ev, -1) > 0) {
 *         if (ev.type == WM_EVENT_CLOSE) break;
 *         if (ev.type == WM_EVENT_RESIZED) { redraw; wm_window_commit(win, NULL); }
 *     }
 *     wm_disconnect();                              // closes every window
 *
 * Drawing: every window has a gfx surface of its client size (os/gfx.h),
 * private to this program. Draw on it with the usual gfx calls, then
 * wm_window_commit() the part that changed: that copies it into a
 * shared-memory buffer the dwm paints the window from. Nothing reaches the
 * screen without a commit. A window stays hidden until its first commit
 * (unless created WM_STYLE_HIDDEN: then until wm_window_show()).
 *
 * Resizing: when the window changes size (the user dragging its grip, or
 * wm_window_resize), wm_wait_event() makes a new surface of the new size,
 * copies the old picture into it, and then returns WM_EVENT_RESIZED: redraw
 * and commit. Fetch the surface again after that, the old one is gone.
 * Very large windows get a smaller surface (at most SHM_MAX bytes of
 * pixels); the rest of the window shows the theme background.
 *
 * Events come only for this program's windows: input while one has the
 * focus or the pointer, close requests, moves and resizes. WM_EVENT_CLOSE
 * does nothing by itself; destroy the window (or don't).
 *
 * Theme: wm_get_theme() gives the dwm's colors, so a program can draw in
 * the same theme as the desktop. WM_EVENT_THEME_CHANGED (ev.window 0)
 * arrives when they change: fetch them again and repaint.
 *
 * Context menus: a window created WM_STYLE_CONTEXT_MENU gets right clicks
 * in its client area as WM_EVENT_CONTEXT_MENU (x, y) instead of the dwm's
 * default window menu. Open the dwm's own menu there with wm_menu_show();
 * the chosen item comes back as WM_EVENT_MENU_COMMAND, its id in ev.param.
 * Nothing comes back when the menu is dismissed.
 *
 *     case WM_EVENT_CONTEXT_MENU: {
 *         wm_menu_item_t items[] = { { "Copy", CMD_COPY, 0 },
 *                                    { NULL, 0, WM_MENU_SEPARATOR },
 *                                    { "Paste", CMD_PASTE, can_paste ? 0 : WM_MENU_DISABLED } };
 *         wm_menu_show(ev.window, ev.x, ev.y, items, 3);
 *         break;
 *     }
 *     case WM_EVENT_MENU_COMMAND: if (ev.param == CMD_COPY) ...
 *
 * Needs SEC_IPC and SEC_GFX (the default clearance has both). Not
 * thread-safe. All calls return -1 (or 0 / NULL) with errno on failure;
 * EPIPE means the dwm went away.
 */

#include <stdbool.h>
#include <stdint.h>
#include <os/gfx.h>
#include <os/wm_proto.h>

#define WM_MAX_WINDOWS      16      // Per program (each uses one gfx surface)

typedef uint32_t wm_window_t;

typedef struct {
    const char* title;
    int32_t     x, y;               // Frame top-left on screen, or WM_POS_DEFAULT
    int32_t     width, height;      // Client area
    int32_t     min_width;          // Smallest size when resized (0: the dwm's default)
    int32_t     min_height;
    uint32_t    style;              // WM_STYLE_*
} wm_window_desc_t;

// Connect to the dwm. It may still be starting: retry for up to timeout_ms
// (0: try once, < 0: forever). 0, or -1 with errno (ENOENT: no dwm,
// EBUSY: it has no room for another program, EPERM: no SEC_IPC).
int  wm_connect(int32_t timeout_ms);

// Close every window and the connection
void wm_disconnect(void);
bool wm_connected(void);

// Screen size the dwm reported
void wm_screen_size(int32_t* width, int32_t* height);

// The pipe to the dwm, to wait on it together with other handles
// (ipc_wait, IPC_READABLE). -1 when not connected.
int  wm_handle(void);

wm_window_t    wm_window_create(const wm_window_desc_t* desc);  // 0 on error
int            wm_window_destroy(wm_window_t window);
gfx_surface_t* wm_window_surface(wm_window_t window);           // NULL if no such window
void           wm_window_get_size(wm_window_t window, int32_t* width, int32_t* height);

// Show `rect` of the surface (client coordinates; NULL: all of it). Runs
// the pending drawing first; if a drawing command failed (EBADF for a bad
// resource handle, EINVAL, ...) the frame is still shown and this returns
// -1 with that errno.
int wm_window_commit(wm_window_t window, const gfx_rect_t* rect);

int wm_window_set_title(wm_window_t window, const char* title);
int wm_window_move(wm_window_t window, int32_t x, int32_t y);
int wm_window_resize(wm_window_t window, int32_t width, int32_t height);   // RESIZED follows
int wm_window_show(wm_window_t window, bool visible);
int wm_window_focus(wm_window_t window);                                   // Raise and focus

// The next event: 1, 0 when timeout_ms passed first (0: just look,
// < 0: wait forever), -1 with errno.
int wm_wait_event(wm_event_t* ev, int32_t timeout_ms);

// The dwm's current colors. 0, or -1 with errno.
int wm_get_theme(wm_theme_t* out);

typedef struct {
    const char* label;              // At most WM_MENU_LABEL_MAX - 1 bytes shown
    uint32_t    id;                 // Comes back in WM_EVENT_MENU_COMMAND's param
    uint32_t    flags;              // WM_MENU_SEPARATOR / _DISABLED / _CHECKED
} wm_menu_item_t;

// Open a dwm context menu for `window` at (x, y), client coordinates (it is
// kept on the screen). Up to WM_MENU_MAX_ITEMS items, copied at once.
// 0, or -1 with errno (EINVAL: nothing to show / window hidden, EBADF).
int wm_menu_show(wm_window_t window, int32_t x, int32_t y, const wm_menu_item_t* items, size_t count);

#endif
