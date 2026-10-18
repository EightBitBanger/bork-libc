/*
 * Window manager client (os/wm.h) on the protocol in os/wm_proto.h.
 *
 * Messages read from the dwm go into a local queue first: a request that
 * waits for its reply keeps the events that arrive meanwhile, and
 * wm_wait_event() can look ahead to merge a run of RESIZED events into the
 * last one (each one would otherwise reallocate the window's buffer).
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <os/gfx.h>
#include <os/ipc.h>
#include <os/os.h>
#include <os/wm.h>
#include "internal.h"

#define QUEUE_MAX   256

typedef struct {
    wm_window_t    id;              // 0: free slot
    int32_t        width, height;   // Client size, as the dwm last said
    gfx_surface_t* surface;
    int            shm;
    uint32_t*      pixels;          // Mapped shm, buf_w * buf_h
    int32_t        buf_w, buf_h;
    bool           show_on_commit;
} window_t;

static int          wm_pipe = -1;
static int32_t      screen_w, screen_h;
static uint32_t     next_seq = 1;
static window_t     windows[WM_MAX_WINDOWS];

static wm_message_t queue[QUEUE_MAX];
static size_t       q_head, q_count;

static window_t* find(wm_window_t id) {
    if (id == 0)
        return NULL;
    for (size_t i = 0; i < WM_MAX_WINDOWS; i++)
        if (windows[i].id == id)
            return &windows[i];
    return NULL;
}

static void free_buffer(window_t* w) {
    if (w->surface) gfx_surface_destroy(w->surface);
    if (w->shm > 0) ipc_close(w->shm);     // Unmaps; the dwm keeps its own handle
    w->surface = NULL;
    w->shm = 0;
    w->pixels = NULL;
    w->buf_w = w->buf_h = 0;
}

static void free_window(window_t* w) {
    free_buffer(w);
    memset(w, 0, sizeof(*w));
}

//
// Messages
//

static void enqueue(const wm_message_t* m) {
    if (q_count == QUEUE_MAX)
        return;     // Full: the program is not reading its events; drop
    queue[(q_head + q_count) % QUEUE_MAX] = *m;
    q_count++;
}

// Read one message (0: none waiting, 1: read into *m, -1: error). A handle
// that came with it goes to *handle (0 if none); with handle NULL it is closed.
static int read_one(wm_message_t* m, int* handle) {
    union { wm_message_t msg; uint8_t raw[IPC_MSG_MAX]; } buf;
    ipc_msg_info_t info;
    if (handle != NULL)
        *handle = 0;
    for (;;) {
        info.handle = 0;
        ssize_t n = ipc_recv(wm_pipe, buf.raw, sizeof(buf.raw), &info);
        if (n < 0)
            return (errno == EAGAIN) ? 0 : -1;
        if ((size_t)n == sizeof(wm_message_t)) {
            *m = buf.msg;
            if (handle != NULL)
                *handle = info.handle;
            else if (info.handle > 0)
                ipc_close(info.handle);
            return 1;
        }
        if (info.handle > 0)
            ipc_close(info.handle);
        // Wrong size: not this protocol, skip it
    }
}

// Everything waiting, into the queue (while there is room)
static int pump(void) {
    wm_message_t m;
    while (q_count < QUEUE_MAX) {
        int r = read_one(&m, NULL);
        if (r <= 0)
            return r;
        if (m.kind == WM_MSG_EVENT)
            enqueue(&m);
    }
    return 0;
}

static int send_raw(const void* q, size_t size, int attach) {
    if (wm_pipe < 0) {
        errno = EPIPE;
        return -1;
    }
    return ipc_send_wait(wm_pipe, q, size, attach);
}

static int send_request(wm_request_t* q, int attach) {
    return send_raw(q, sizeof(*q), attach);
}

static uint32_t take_seq(void) {
    uint32_t seq = next_seq++;
    if (next_seq == 0) next_seq = 1;
    return seq;
}

// Wait for the reply with this seq (the whole message into *out when given,
// a handle sent with it into *handle, else closed). Events read meanwhile are
// queued. 0, or -1 with errno (the dwm's status, or a connection error).
static int await_reply_handle(uint32_t seq, wm_window_t* window, wm_message_t* out, int* handle) {
    if (handle != NULL)
        *handle = 0;
    for (;;) {
        wm_message_t m;
        int got = 0;
        int r = read_one(&m, &got);
        if (r < 0)
            return -1;
        if (r == 0) {
            if (ipc_wait_one(wm_pipe, IPC_READABLE, IPC_FOREVER) < 0)
                return -1;
            continue;
        }
        if (m.kind == WM_MSG_REPLY && m.seq == seq) {
            if (handle != NULL)
                *handle = got;
            else if (got > 0)
                ipc_close(got);
            if (window) *window = m.window;
            if (out) *out = m;
            if (m.status != 0) {
                errno = m.status;
                return -1;
            }
            return 0;
        }
        if (got > 0)
            ipc_close(got);
        if (m.kind == WM_MSG_EVENT)
            enqueue(&m);
        // Other replies (to seq-less errors) and stray welcomes: ignored
    }
}

static int await_reply(uint32_t seq, wm_window_t* window, wm_message_t* out) {
    return await_reply_handle(seq, window, out, NULL);
}

// Send, then wait for its reply
static int call(wm_request_t* q, int attach, wm_window_t* window) {
    q->seq = take_seq();
    if (send_request(q, attach) < 0)
        return -1;
    return await_reply(q->seq, window, NULL);
}

static void simple(uint32_t type, wm_request_t* q) {
    q->type = type;
    q->seq = 0;
}

//
// Connection
//

int wm_connect(int32_t timeout_ms) {
    if (wm_pipe >= 0)
        return 0;

    uint64_t start = os_time_ms();
    int pipe;
    for (;;) {
        pipe = ipc_connect(WM_PORT_NAME);
        if (pipe >= 0)
            break;
        if (errno != ENOENT && errno != EAGAIN)
            return -1;
        if (timeout_ms >= 0 && os_time_ms() - start >= (uint64_t)timeout_ms)
            return -1;
        os_sleep_ms(20);
    }

    // The greeting comes first
    wm_message_t m;
    for (;;) {
        int r;
        while ((r = ipc_wait_one(pipe, IPC_READABLE, IPC_FOREVER)) == 0) { }
        if (r < 0)
            break;
        ssize_t n = ipc_recv(pipe, &m, sizeof(m), NULL);
        if (n == (ssize_t)sizeof(m) && m.kind == WM_MSG_WELCOME) {
            if (m.version != WM_PROTOCOL_VERSION) {
                ipc_close(pipe);
                errno = ENOTSUP;
                return -1;
            }
            wm_pipe  = pipe;
            screen_w = m.screen_w;
            screen_h = m.screen_h;
            q_head = q_count = 0;
            return 0;
        }
        if (n < 0 && errno != EAGAIN)
            break;
    }
    int err = (errno == EPIPE) ? EBUSY : errno;     // Closed unwelcomed: the dwm is full
    ipc_close(pipe);
    errno = err;
    return -1;
}

void wm_disconnect(void) {
    for (size_t i = 0; i < WM_MAX_WINDOWS; i++)
        if (windows[i].id != 0)
            free_window(&windows[i]);
    if (wm_pipe >= 0)
        ipc_close(wm_pipe);         // The dwm closes our windows
    wm_pipe = -1;
    q_head = q_count = 0;
}

bool wm_connected(void) {
    return wm_pipe >= 0;
}

void wm_screen_size(int32_t* width, int32_t* height) {
    if (width)  *width  = screen_w;
    if (height) *height = screen_h;
}

int wm_handle(void) {
    return wm_pipe;
}

//
// Buffers
//

// A surface and shared buffer for a width x height window, handed to the
// dwm. The old picture is copied over. On failure the old buffer stays.
static int make_buffer(window_t* w, int32_t width, int32_t height, bool wait_reply) {
    int32_t bw = width  < SYS_GFX_MAX_DIM ? width  : SYS_GFX_MAX_DIM;
    int32_t bh = height < SYS_GFX_MAX_DIM ? height : SYS_GFX_MAX_DIM;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    if ((uint64_t)bw * (uint64_t)bh * 4u > SHM_MAX)
        bh = (int32_t)(SHM_MAX / ((uint32_t)bw * 4u));

    gfx_surface_t* s = gfx_surface_create(bw, bh);
    if (s == NULL)
        return -1;
    int shm = shm_create((size_t)bw * (size_t)bh * 4u);
    uint32_t* pixels = (shm > 0) ? (uint32_t*)shm_map(shm) : NULL;
    if (pixels == NULL) {
        int err = errno;
        if (shm > 0) ipc_close(shm);
        gfx_surface_destroy(s);
        errno = err;
        return -1;
    }

    if (w->surface != NULL)
        gfx_blit(s, w->surface, NULL, 0, 0);
    gfx_read_pixels(s, 0, 0, bw, bh, pixels, 0);    // The buffer starts as the picture

    wm_request_t q;
    memset(&q, 0, sizeof(q));
    simple(WM_REQ_ATTACH, &q);
    q.window = w->id;
    q.w = bw;
    q.h = bh;
    int r = wait_reply ? call(&q, shm, NULL) : send_request(&q, shm);
    if (r < 0) {
        int err = errno;
        ipc_close(shm);
        gfx_surface_destroy(s);
        errno = err;
        return -1;
    }

    free_buffer(w);
    w->surface = s;
    w->shm     = shm;
    w->pixels  = pixels;
    w->buf_w   = bw;
    w->buf_h   = bh;
    return 0;
}

//
// Windows
//

wm_window_t wm_window_create(const wm_window_desc_t* desc) {
    if (wm_pipe < 0) {
        errno = EPIPE;
        return 0;
    }
    if (desc == NULL || desc->width <= 0 || desc->height <= 0 || (desc->style & ~WM_STYLE_VALID)) {
        errno = EINVAL;
        return 0;
    }

    window_t* w = NULL;
    for (size_t i = 0; i < WM_MAX_WINDOWS && w == NULL; i++)
        if (windows[i].id == 0)
            w = &windows[i];
    if (w == NULL) {
        errno = EMFILE;
        return 0;
    }

    wm_request_t q;
    memset(&q, 0, sizeof(q));
    q.type  = WM_REQ_CREATE;
    q.x     = desc->x;
    q.y     = desc->y;
    q.w     = desc->width;
    q.h     = desc->height;
    q.min_w = desc->min_width;
    q.min_h = desc->min_height;
    q.style = desc->style | WM_STYLE_HIDDEN;        // Shown by the first commit
    if (desc->title != NULL)
        strncpy(q.title, desc->title, sizeof(q.title) - 1);

    wm_window_t id = 0;
    if (call(&q, 0, &id) < 0)
        return 0;

    memset(w, 0, sizeof(*w));
    w->id     = id;
    w->width  = desc->width;
    w->height = desc->height;
    w->show_on_commit = !(desc->style & WM_STYLE_HIDDEN);

    if (make_buffer(w, w->width, w->height, true) < 0) {
        int err = errno;
        wm_window_destroy(id);
        errno = err;
        return 0;
    }
    return id;
}

int wm_window_destroy(wm_window_t window) {
    window_t* w = find(window);
    if (w == NULL) {
        errno = EBADF;
        return -1;
    }
    wm_request_t q;
    memset(&q, 0, sizeof(q));
    simple(WM_REQ_DESTROY, &q);
    q.window = window;
    int r = send_request(&q, 0);
    free_window(w);
    return r;
}

gfx_surface_t* wm_window_surface(wm_window_t window) {
    window_t* w = find(window);
    if (w == NULL)
        errno = EBADF;
    return w ? w->surface : NULL;
}

void wm_window_get_size(wm_window_t window, int32_t* width, int32_t* height) {
    window_t* w = find(window);
    if (width)  *width  = w ? w->width  : 0;
    if (height) *height = w ? w->height : 0;
}

int wm_window_commit(wm_window_t window, const gfx_rect_t* rect) {
    window_t* w = find(window);
    if (w == NULL || w->surface == NULL) {
        errno = EBADF;
        return -1;
    }

    // Run what was drawn first and keep its first failure (a bad resource
    // handle, an out-of-range argument): the rest of the frame still shows,
    // and the failure is reported below, as gfx_present() does
    int draw_error = (gfx_flush(w->surface) < 0) ? errno : 0;

    gfx_rect_t r = rect ? *rect : gfx_rect(0, 0, w->buf_w, w->buf_h);
    int32_t x0 = r.x > 0 ? r.x : 0, y0 = r.y > 0 ? r.y : 0;
    int32_t x1 = r.x + r.w < w->buf_w ? r.x + r.w : w->buf_w;
    int32_t y1 = r.y + r.h < w->buf_h ? r.y + r.h : w->buf_h;
    if (x1 > x0 && y1 > y0) {
        // Flushes what was drawn, then copies it into the shared buffer
        if (gfx_read_pixels(w->surface, x0, y0, x1 - x0, y1 - y0,
                            w->pixels + (size_t)y0 * (size_t)w->buf_w + (size_t)x0, w->buf_w) < 0)
            return -1;

        wm_request_t q;
        memset(&q, 0, sizeof(q));
        simple(WM_REQ_COMMIT, &q);
        q.window = window;
        q.x = x0;
        q.y = y0;
        q.w = x1 - x0;
        q.h = y1 - y0;
        if (send_request(&q, 0) < 0)
            return -1;
    }

    if (w->show_on_commit) {
        w->show_on_commit = false;
        if (wm_window_show(window, true) < 0)
            return -1;
    }
    if (draw_error != 0) {
        errno = draw_error;
        return -1;
    }
    return 0;
}

static int window_request(wm_window_t window, wm_request_t* q) {
    if (find(window) == NULL) {
        errno = EBADF;
        return -1;
    }
    q->window = window;
    return send_request(q, 0);
}

int wm_window_set_title(wm_window_t window, const char* title) {
    wm_request_t q;
    memset(&q, 0, sizeof(q));
    simple(WM_REQ_SET_TITLE, &q);
    if (title != NULL)
        strncpy(q.title, title, sizeof(q.title) - 1);
    return window_request(window, &q);
}

int wm_window_move(wm_window_t window, int32_t x, int32_t y) {
    wm_request_t q;
    memset(&q, 0, sizeof(q));
    simple(WM_REQ_MOVE, &q);
    q.x = x;
    q.y = y;
    return window_request(window, &q);
}

int wm_window_resize(wm_window_t window, int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) {
        errno = EINVAL;
        return -1;
    }
    wm_request_t q;
    memset(&q, 0, sizeof(q));
    simple(WM_REQ_RESIZE, &q);
    q.w = width;
    q.h = height;
    return window_request(window, &q);
}

int wm_window_show(wm_window_t window, bool visible) {
    window_t* w = find(window);
    if (w != NULL)
        w->show_on_commit = false;      // The program decides from here on
    wm_request_t q;
    memset(&q, 0, sizeof(q));
    simple(WM_REQ_SHOW, &q);
    q.style = visible ? 1u : 0u;
    return window_request(window, &q);
}

int wm_window_focus(wm_window_t window) {
    wm_request_t q;
    memset(&q, 0, sizeof(q));
    simple(WM_REQ_FOCUS, &q);
    return window_request(window, &q);
}

//
// Events
//

// A later RESIZED for the same window is already queued
static bool resized_later(wm_window_t window) {
    for (size_t i = 0; i < q_count; i++) {
        const wm_message_t* m = &queue[(q_head + i) % QUEUE_MAX];
        if (m->event.type == WM_EVENT_RESIZED && m->event.window == window)
            return true;
    }
    return false;
}

// Take the next deliverable event off the queue (with the local work it
// needs done first). False when the queue holds none.
static bool take(wm_event_t* ev) {
    while (q_count > 0) {
        wm_message_t m = queue[q_head];
        q_head = (q_head + 1) % QUEUE_MAX;
        q_count--;

        if (m.event.type == WM_EVENT_THEME_CHANGED || m.event.type == WM_EVENT_CLIP_CHANGED ||
            m.event.type == WM_EVENT_FS_CHANGED) {
            *ev = m.event;          // For the program, not one window (window 0)
            return true;
        }

        window_t* w = find(m.event.window);
        if (w == NULL)
            continue;       // Destroyed here already

        switch (m.event.type) {
            case WM_EVENT_RESIZED:
                if (resized_later(w->id))
                    continue;
                w->width  = m.event.rect_w;
                w->height = m.event.rect_h;
                // Failing keeps the old buffer: the window shows less, still works
                make_buffer(w, w->width, w->height, false);
                break;
            case WM_EVENT_DESTROYED:
                free_window(w);
                break;
        }
        *ev = m.event;
        return true;
    }
    return false;
}

int wm_wait_event(wm_event_t* ev, int32_t timeout_ms) {
    if (ev == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (wm_pipe < 0) {
        errno = EPIPE;
        return -1;
    }

    uint64_t start = os_time_ms();
    for (;;) {
        int pumped = pump();
        if (take(ev))
            return 1;
        if (pumped < 0)
            return -1;      // EPIPE once the dwm is gone and everything was read

        int32_t left = IPC_FOREVER;
        if (timeout_ms >= 0) {
            uint64_t waited = os_time_ms() - start;
            if (waited >= (uint64_t)timeout_ms)
                return 0;
            left = (int32_t)((uint64_t)timeout_ms - waited);
        }
        int r = ipc_wait_one(wm_pipe, IPC_READABLE, left);
        if (r < 0)
            return -1;
        if (r == 0)
            return 0;
    }
}

//
// Theme and menus
//

int wm_get_theme(wm_theme_t* out) {
    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    wm_request_t q;
    memset(&q, 0, sizeof(q));
    q.type = WM_REQ_THEME;
    q.seq = take_seq();
    if (send_request(&q, 0) < 0)
        return -1;
    wm_message_t m;
    if (await_reply(q.seq, NULL, &m) < 0)
        return -1;
    *out = m.theme;
    return 0;
}

int wm_menu_show(wm_window_t window, int32_t x, int32_t y, const wm_menu_item_t* items, size_t count) {
    if (find(window) == NULL) {
        errno = EBADF;
        return -1;
    }
    if (items == NULL || count == 0 || count > WM_MENU_MAX_ITEMS) {
        errno = EINVAL;
        return -1;
    }

    wm_menu_request_t q;
    memset(&q, 0, sizeof(q));
    q.type   = WM_REQ_MENU;
    q.seq    = take_seq();
    q.window = window;
    q.x      = x;
    q.y      = y;
    q.count  = (uint32_t)count;
    for (size_t i = 0; i < count; i++) {
        q.items[i].id    = items[i].id;
        q.items[i].flags = items[i].flags;
        if (items[i].label != NULL)
            strlcpy(q.items[i].label, items[i].label, sizeof(q.items[i].label));
    }
    if (send_raw(&q, sizeof(q), 0) < 0)
        return -1;
    return await_reply(q.seq, NULL, NULL);
}

// Connect without waiting when not connected yet (calls that need no window)
static int ensure_connected(void) {
    return (wm_pipe >= 0) ? 0 : wm_connect(0);
}

//
// Notifications
//

int wm_notify_ex(uint32_t kind, const char* title, const char* message, uint32_t timeout_ms) {
    if (kind != WM_NOTIFY_INFO && kind != WM_NOTIFY_ERROR) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_connected() < 0)
        return -1;

    wm_notify_request_t q;
    memset(&q, 0, sizeof(q));
    q.type       = WM_REQ_NOTIFY;
    q.seq        = take_seq();
    q.kind       = kind;
    q.timeout_ms = timeout_ms;
    if (title != NULL)
        strlcpy(q.title, title, sizeof(q.title));
    if (message != NULL)
        strlcpy(q.text, message, sizeof(q.text));

    if (send_raw(&q, sizeof(q), 0) < 0)
        return -1;
    return await_reply(q.seq, NULL, NULL);
}

int wm_notify(const char* title, const char* message) {
    return wm_notify_ex(WM_NOTIFY_INFO, title, message, WM_NOTIFY_DEFAULT_MS);
}

//
// Clipboard
//

static int clip_request(uint32_t type, const wm_clip_info_t* info, int attach,
                        wm_message_t* reply, int* handle) {
    if (ensure_connected() < 0)
        return -1;
    wm_clip_request_t q;
    memset(&q, 0, sizeof(q));
    q.type = type;
    q.seq  = take_seq();
    if (info != NULL)
        q.info = *info;
    if (send_raw(&q, sizeof(q), attach) < 0)
        return -1;
    return await_reply_handle(q.seq, NULL, reply, handle);
}

int wm_clip_set(const wm_clip_info_t* info, const void* data) {
    if (info == NULL || info->type == WM_CLIP_NONE || info->size > WM_CLIP_MAX ||
        (info->size > 0 && data == NULL)) {
        errno = EINVAL;
        return -1;
    }

    // The data goes over in a shared block; the dwm copies it and lets go
    int shm = 0;
    if (info->size > 0) {
        shm = shm_create(info->size);
        void* p = (shm > 0) ? shm_map(shm) : NULL;
        if (p == NULL) {
            int err = errno;
            if (shm > 0) ipc_close(shm);
            errno = err;
            return -1;
        }
        memcpy(p, data, info->size);
    }

    wm_clip_info_t send = *info;
    send.format[WM_CLIP_FORMAT_MAX - 1] = '\0';
    int r = clip_request(WM_REQ_CLIP_SET, &send, shm, NULL, NULL);
    int err = errno;
    if (shm > 0)
        ipc_close(shm);         // Ours; the dwm got its own handle with the message
    errno = err;
    return r;
}

int wm_clip_set_text(const char* text) {
    wm_clip_info_t info;
    memset(&info, 0, sizeof(info));
    info.type = WM_CLIP_TEXT;
    info.size = (text != NULL) ? (uint32_t)strnlen(text, WM_CLIP_MAX + 1) : 0;
    return wm_clip_set(&info, text);
}

int wm_clip_set_files(const char* const* paths, size_t count, bool cut) {
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        if (paths == NULL || paths[i] == NULL || paths[i][0] != '/' || strchr(paths[i], '\n') != NULL) {
            errno = EINVAL;
            return -1;
        }
        total += strlen(paths[i]) + 1;
    }
    if (count == 0 || total > WM_CLIP_MAX) {
        errno = (count == 0) ? EINVAL : E2BIG;
        return -1;
    }

    char* list = (char*)malloc(total);
    if (list == NULL)
        return -1;
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        size_t n = strlen(paths[i]);
        memcpy(list + at, paths[i], n);
        at += n;
        if (i + 1 < count)
            list[at++] = '\n';
    }

    wm_clip_info_t info;
    memset(&info, 0, sizeof(info));
    info.type  = WM_CLIP_FILES;
    info.flags = cut ? WM_CLIP_CUT : 0;
    info.size  = (uint32_t)at;
    int r = wm_clip_set(&info, list);
    int err = errno;
    free(list);
    errno = err;
    return r;
}

int wm_clip_set_image(const uint32_t* pixels, int32_t width, int32_t height) {
    if (pixels == NULL || width <= 0 || height <= 0 ||
        (uint64_t)width * (uint64_t)height * 4u > WM_CLIP_MAX) {
        errno = EINVAL;
        return -1;
    }
    wm_clip_info_t info;
    memset(&info, 0, sizeof(info));
    info.type   = WM_CLIP_IMAGE;
    info.width  = (uint32_t)width;
    info.height = (uint32_t)height;
    info.size   = (uint32_t)width * (uint32_t)height * 4u;
    return wm_clip_set(&info, pixels);
}

int wm_clip_set_binary(const char* format, const void* data, size_t size) {
    if (size > WM_CLIP_MAX) {
        errno = E2BIG;
        return -1;
    }
    wm_clip_info_t info;
    memset(&info, 0, sizeof(info));
    info.type = WM_CLIP_BINARY;
    info.size = (uint32_t)size;
    if (format != NULL)
        strlcpy(info.format, format, sizeof(info.format));
    return wm_clip_set(&info, data);
}

int wm_clip_info(wm_clip_info_t* out) {
    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    wm_message_t m;
    if (clip_request(WM_REQ_CLIP_INFO, NULL, 0, &m, NULL) < 0)
        return -1;
    *out = m.clip;
    return 0;
}

void* wm_clip_get(wm_clip_info_t* out) {
    wm_message_t m;
    int shm = 0;
    if (clip_request(WM_REQ_CLIP_GET, NULL, 0, &m, &shm) < 0)
        return NULL;            // ENOENT: empty

    uint32_t size = m.clip.size;
    char* copy = (char*)malloc((size_t)size + 1);
    if (copy == NULL) {
        if (shm > 0) ipc_close(shm);
        errno = ENOMEM;
        return NULL;
    }
    if (size > 0) {
        void* p = (shm > 0 && shm_size(shm) >= size) ? shm_map(shm) : NULL;
        if (p == NULL) {
            if (shm > 0) ipc_close(shm);
            free(copy);
            errno = EIO;
            return NULL;
        }
        memcpy(copy, p, size);
    }
    copy[size] = '\0';
    if (shm > 0)
        ipc_close(shm);
    if (out != NULL)
        *out = m.clip;
    return copy;
}

int wm_clip_clear(uint32_t serial) {
    wm_clip_info_t info;
    memset(&info, 0, sizeof(info));
    info.serial = serial;
    return clip_request(WM_REQ_CLIP_CLEAR, &info, 0, NULL, NULL);
}

const char* wm_clip_type_name(uint32_t type) {
    switch (type) {
        case WM_CLIP_NONE:   return "nothing";
        case WM_CLIP_TEXT:   return "text";
        case WM_CLIP_IMAGE:  return "an image";
        case WM_CLIP_BINARY: return "data";
        case WM_CLIP_FILES:  return "files";
        default:             return "custom data";
    }
}

//
// Files changed
//

int wm_fs_changed(const char* path) {
    if (ensure_connected() < 0)
        return -1;
    wm_path_request_t q;
    memset(&q, 0, sizeof(q));
    q.type = WM_REQ_FS_CHANGED;
    q.seq  = 0;             // No reply wanted: just an announcement
    if (path != NULL)
        strlcpy(q.path, path, sizeof(q.path));
    return send_raw(&q, sizeof(q), 0);
}
