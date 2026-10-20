/*
 * the line a program reads from a console in line input mode: the keys that
 * edit it, the history of the lines of each program and the aliases that
 * rewrite them. see conline.c.
 *
 * the module needs nothing of the kernel, so a host program can test it.
 */
#ifndef MIME_CONLINE_H
#define MIME_CONLINE_H

#include "conscreen.h"

typedef uint16_t ConChar;

typedef struct ConLineState ConLineState;
typedef struct ConLine ConLine;

ConLineState *ConLineStateCreate(void);
void ConLineStateDestroy(ConLineState *state);

/* ---- history */

#define CON_HISTORY_NO_DUP 0x1
void ConHistoryGetInfo(const ConLineState *state, unsigned *buffer_size, unsigned *buffers, unsigned *flags);
bool ConHistorySetInfo(ConLineState *state, unsigned buffer_size, unsigned buffers, unsigned flags);
void ConHistoryAdd(ConLineState *state, const ConChar *exe, size_t exe_length, const ConChar *line, size_t length);
/* the commands, each followed by a NUL, as many as fit; the return is the characters they take. */
size_t ConHistoryGet(const ConLineState *state, const ConChar *exe, size_t exe_length, ConChar *out, size_t room);
void ConHistoryExpunge(ConLineState *state, const ConChar *exe, size_t exe_length);
bool ConHistorySetCount(ConLineState *state, const ConChar *exe, size_t exe_length, unsigned count);
unsigned ConHistoryGetCount(const ConLineState *state, const ConChar *exe, size_t exe_length);

/* ---- aliases */

/* a null target removes the alias. */
bool ConAliasAdd(ConLineState *state, const ConChar *exe, size_t exe_length, const ConChar *source, size_t source_length,
                 const ConChar *target, size_t target_length);
/* the target of an alias, or -1 when there is none; copies as many as fit. */
long ConAliasGet(const ConLineState *state, const ConChar *exe, size_t exe_length, const ConChar *source,
                 size_t source_length, ConChar *out, size_t room);
/* all the aliases of an exe as source=target, each followed by a NUL; the return is the characters they take. */
size_t ConAliasList(const ConLineState *state, const ConChar *exe, size_t exe_length, ConChar *out, size_t room);
size_t ConAliasExes(const ConLineState *state, ConChar *out, size_t room);
void ConAliasExpunge(ConLineState *state, const ConChar *exe, size_t exe_length);
/* rewrites a line by the aliases of the exe; false when none applies. */
bool ConAliasExpand(const ConLineState *state, const ConChar *exe, size_t exe_length, const ConChar *line, size_t length,
                    ConChar *out, size_t room, size_t *out_length);

/* ---- editing */

typedef struct ConKey {
    bool Down;
    uint16_t Vk;
    ConChar Char;
    uint32_t State;
    uint16_t Repeat;
} ConKey;

typedef enum ConLineResult {
    CONLINE_MORE,
    CONLINE_DONE,   /* the line is complete */
    CONLINE_WAKEUP, /* a control character of the wake up mask ended the read */
} ConLineResult;

#define CONLINE_ECHO 0x1      /* the line is shown, and goes to the history and the aliases */
#define CONLINE_PROCESSED 0x2 /* backspace edits, and the line ends with a line feed too */
#define CONLINE_INSERT 0x4    /* typed characters are put in, not over the ones there */
#define CONLINE_VT 0x8        /* the keys are the characters they send: none edits */

/*
 * starts a line at the cursor of the screen. the characters of initial are
 * the start of the line; a control character whose bit is in the wake up mask
 * ends the read.
 */
ConLine *ConLineBegin(CsBuffer *screen, ConLineState *state, const ConChar *exe, size_t exe_length, unsigned flags,
                      const ConChar *initial, size_t initial_length, uint32_t wake_mask);
ConLineResult ConLineKey(ConLine *line, const ConKey *key);
/* the finished line, with its line ending when it ended with enter, or what an alias made of it. */
const ConChar *ConLineText(const ConLine *line, size_t *length);
/* the lines that text is, more than one when an alias made several. */
size_t ConLineLines(const ConLine *line);
uint32_t ConLineKeyState(const ConLine *line);
void ConLineEnd(ConLine *line);

#endif
