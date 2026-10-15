#ifndef _OS_INPUT_H
#define _OS_INPUT_H

/*
 * Keyboard and mouse. Needs SEC_INPUT (os/security.h).
 *
 * input_open() takes both devices for this process: the window manager
 * stops reading input and stops drawing until the handle is closed (or the
 * process exits), then repaints the screen. Only one process can hold them
 * (EBUSY otherwise). The first event is always a mouse event with the
 * current position and buttons, so a program knows where the pointer is
 * without waiting for it to move.
 *
 *     int in = input_open();
 *     input_event_t ev[16];
 *     while (input_wait(in, IPC_FOREVER) > 0) {
 *         int n = input_read(in, ev, 16);
 *         for (int i = 0; i < n; i++) ...
 *     }
 *
 * Keys: presses only; `key` is the character (Shift applied) or KEY_*.
 * Mouse: absolute screen position and the buttons held (INPUT_BTN_*).
 * `mods` (INPUT_MOD_*) is the modifier keys when the event was read.
 */

#include <stddef.h>
#include <stdint.h>
#include <os/syscall.h>
#include <os/ipc.h>

typedef SysInputEvent input_event_t;

#define INPUT_MOUSE         SYS_INPUT_MOUSE
#define INPUT_KEY           SYS_INPUT_KEY

#define INPUT_BTN_LEFT      SYS_INPUT_BTN_LEFT
#define INPUT_BTN_RIGHT     SYS_INPUT_BTN_RIGHT
#define INPUT_BTN_MIDDLE    SYS_INPUT_BTN_MIDDLE

#define INPUT_MOD_SHIFT     SYS_INPUT_MOD_SHIFT
#define INPUT_MOD_CTRL      SYS_INPUT_MOD_CTRL
#define INPUT_MOD_ALT       SYS_INPUT_MOD_ALT

#define INPUT_READ_MAX      SYS_INPUT_READ_MAX

#define KEY_BACKSPACE       SYS_KEY_BACKSPACE
#define KEY_ENTER           SYS_KEY_ENTER
#define KEY_TAB             SYS_KEY_TAB
#define KEY_ESCAPE          SYS_KEY_ESCAPE
#define KEY_HOME            SYS_KEY_HOME
#define KEY_UP              SYS_KEY_UP
#define KEY_PAGE_UP         SYS_KEY_PAGE_UP
#define KEY_LEFT            SYS_KEY_LEFT
#define KEY_RIGHT           SYS_KEY_RIGHT
#define KEY_END             SYS_KEY_END
#define KEY_DOWN            SYS_KEY_DOWN
#define KEY_PAGE_DOWN       SYS_KEY_PAGE_DOWN
#define KEY_DELETE          SYS_KEY_DELETE

// Take the keyboard and mouse. Handle, or -1 with errno (EBUSY, EPERM).
int input_open(void);

// Up to `max` queued events (at most INPUT_READ_MAX per call). Returns
// how many, 0 when none are waiting.
int input_read(int handle, input_event_t* events, size_t max);

// 1 when events are waiting, 0 on timeout (timeout_ms < 0: forever)
int input_wait(int handle, int32_t timeout_ms);

// Give the devices back (same as ipc_close)
int input_close(int handle);

#endif
