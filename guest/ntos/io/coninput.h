/*
 * the records of the console input buffer, io/coninput.c: what a terminal
 * sends decoded into key events, the vt sequences keys stand for when a
 * program asked for vt input, and the rules for merging an event into the
 * last one waiting.
 */
#ifndef MIME_CONINPUT_H
#define MIME_CONINPUT_H

#include <mime/condrv.h>

#define CON_KEY_EVENT 0x1
#define CON_MOUSE_EVENT 0x2
#define CON_WINDOW_BUFFER_SIZE_EVENT 0x4
#define CON_MENU_EVENT 0x8
#define CON_FOCUS_EVENT 0x10

#define CON_RIGHT_ALT 0x1
#define CON_LEFT_ALT 0x2
#define CON_RIGHT_CTRL 0x4
#define CON_LEFT_CTRL 0x8
#define CON_SHIFT 0x10
#define CON_ENHANCED_KEY 0x100
#define CON_IME_CONVERSION 0x800000

#define CON_MOUSE_MOVED 0x1

#define CON_PROCESSED_INPUT 0x1
#define CON_LINE_INPUT 0x2
#define CON_VIRTUAL_TERMINAL_INPUT 0x200

#define CON_VK_PAUSE 0x13

/* the most records a key becomes in vt input. */
#define CON_MAX_SEQUENCE 16

/*
 * decodes terminal input into key presses and releases, plain characters
 * when vt is set. returns the bytes used; with more set, an incomplete
 * character or escape sequence at the end is left for the next input.
 */
ULONG ConDecodeInput(const UCHAR *p, ULONG length, BOOLEAN vt, BOOLEAN more, MIME_CONDRV_RECORD *out, ULONG room,
                     ULONG *count);

/* the records a key stands for in vt input, in out; FALSE keeps the event as it is. */
BOOLEAN ConTranslateKey(const MIME_CONDRV_RECORD *r, BOOLEAN cursor_application, MIME_CONDRV_RECORD *out,
                        ULONG *count);

/* merges next into last as one event written alone does, a mouse move or a repeated key. */
BOOLEAN ConMergeRecord(MIME_CONDRV_RECORD *last, const MIME_CONDRV_RECORD *next);

/* the keys that leave paused output paused. */
BOOLEAN ConIsSystemKey(USHORT vk);

#endif
