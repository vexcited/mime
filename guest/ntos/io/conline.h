/*
 * the line a program reads in line input mode: the editing keys, each program's command history and the aliases
 * that rewrite lines. the module does not depend on the kernel.
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
/* the commands that fit, each followed by a NUL. returns the characters they take. */
size_t ConHistoryGet(const ConLineState *state, const ConChar *exe, size_t exe_length, ConChar *out, size_t room);
void ConHistoryExpunge(ConLineState *state, const ConChar *exe, size_t exe_length);
bool ConHistorySetCount(ConLineState *state, const ConChar *exe, size_t exe_length, unsigned count);
unsigned ConHistoryGetCount(const ConLineState *state, const ConChar *exe, size_t exe_length);

/* ---- aliases */

/* a null target removes the alias. */
bool ConAliasAdd(ConLineState *state, const ConChar *exe, size_t exe_length, const ConChar *source, size_t source_length,
                 const ConChar *target, size_t target_length);
/* copies as much of the alias target as fits. returns -1 when there is no alias. */
long ConAliasGet(const ConLineState *state, const ConChar *exe, size_t exe_length, const ConChar *source,
                 size_t source_length, ConChar *out, size_t room);
/* the exe's aliases as source=target, each followed by a NUL. returns the characters they take. */
size_t ConAliasList(const ConLineState *state, const ConChar *exe, size_t exe_length, ConChar *out, size_t room);
size_t ConAliasExes(const ConLineState *state, ConChar *out, size_t room);
void ConAliasExpunge(ConLineState *state, const ConChar *exe, size_t exe_length);
/* false when no alias applies. */
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
    CONLINE_DONE,
    CONLINE_WAKEUP, /* a control character in the wake mask ended the read. */
} ConLineResult;

#define CONLINE_ECHO 0x1      /* show the line, and add it to the history and the aliases. */
#define CONLINE_PROCESSED 0x2 /* backspace edits, and the line also ends with a line feed. */
#define CONLINE_INSERT 0x4    /* typed characters are inserted, not overwritten. */
#define CONLINE_VT 0x8        /* keys are the characters they send, none edits. */

/*
 * starts a line at the screen cursor, beginning with initial. a control character whose bit is in wake_mask ends
 * the read.
 */
ConLine *ConLineBegin(CsBuffer *screen, ConLineState *state, const ConChar *exe, size_t exe_length, unsigned flags,
                      const ConChar *initial, size_t initial_length, uint32_t wake_mask);
ConLineResult ConLineKey(ConLine *line, const ConKey *key);
/* the finished line with its line ending when enter ended it, or what an alias made of it. */
const ConChar *ConLineText(const ConLine *line, size_t *length);
/* more than one when an alias made several lines. */
size_t ConLineLines(const ConLine *line);
uint32_t ConLineKeyState(const ConLine *line);
void ConLineEnd(ConLine *line);

#endif
