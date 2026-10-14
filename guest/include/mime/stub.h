/*
 * unimplemented entry points: each logs its name once to the debugger and fails cleanly.
 */
#pragma once
#include <mime/win32/winbase.h>

static inline void MimeStubLog(LONG volatile *once, const char *dll, const char *function)
{
    if (InterlockedExchange(once, 1))
        return;
    char text[160];
    int n = 0;
    for (const char *s = dll; *s && n < 60; s++)
        text[n++] = *s;
    for (const char *s = ": stub "; *s; s++)
        text[n++] = *s;
    for (const char *s = function; *s && n < 150; s++)
        text[n++] = *s;
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

/* logs the enclosing function once, the caller then returns its failure value. */
#define MIME_STUB(dll)                                                                                                \
    do {                                                                                                              \
        static LONG volatile mime_stub_once;                                                                          \
        MimeStubLog(&mime_stub_once, dll, __func__);                                                                  \
    } while (0)
