/*
 * exported entry points without an implementation, declared with the stack size of windows' x86 build so
 * stdcall callers stay balanced. each logs once and returns the failure its caller would see.
 */
#pragma once
#include <mime/stub.h>

#define MIME_STUB_ARGS_0 void
#define MIME_STUB_ARGS_1 ULONG_PTR a1
#define MIME_STUB_ARGS_2 MIME_STUB_ARGS_1, ULONG_PTR a2
#define MIME_STUB_ARGS_3 MIME_STUB_ARGS_2, ULONG_PTR a3
#define MIME_STUB_ARGS_4 MIME_STUB_ARGS_3, ULONG_PTR a4
#define MIME_STUB_ARGS_5 MIME_STUB_ARGS_4, ULONG_PTR a5
#define MIME_STUB_ARGS_6 MIME_STUB_ARGS_5, ULONG_PTR a6
#define MIME_STUB_ARGS_7 MIME_STUB_ARGS_6, ULONG_PTR a7
#define MIME_STUB_ARGS_8 MIME_STUB_ARGS_7, ULONG_PTR a8
#define MIME_STUB_ARGS_9 MIME_STUB_ARGS_8, ULONG_PTR a9
#define MIME_STUB_ARGS_10 MIME_STUB_ARGS_9, ULONG_PTR a10
#define MIME_STUB_ARGS_11 MIME_STUB_ARGS_10, ULONG_PTR a11
#define MIME_STUB_ARGS_12 MIME_STUB_ARGS_11, ULONG_PTR a12
#define MIME_STUB_ARGS_13 MIME_STUB_ARGS_12, ULONG_PTR a13
#define MIME_STUB_ARGS_14 MIME_STUB_ARGS_13, ULONG_PTR a14
#define MIME_STUB_ARGS_15 MIME_STUB_ARGS_14, ULONG_PTR a15
#define MIME_STUB_ARGS_16 MIME_STUB_ARGS_15, ULONG_PTR a16

/* a function of <args> pointer sized arguments returning <value> of <type>. */
#define MIME_STUB_RETURN(dll, type, name, args, value)                                                                 \
    DECLSPEC_EXPORT type WINAPI name(MIME_STUB_ARGS_##args)                                                            \
    {                                                                                                                  \
        MIME_STUB(dll);                                                                                                \
        return value;                                                                                                  \
    }

/* the same, setting the thread's last error first. */
#define MIME_STUB_ERROR(dll, type, name, args, error, value)                                                           \
    DECLSPEC_EXPORT type WINAPI name(MIME_STUB_ARGS_##args)                                                            \
    {                                                                                                                  \
        MIME_STUB(dll);                                                                                                \
        SetLastError(error);                                                                                           \
        return value;                                                                                                  \
    }

#define MIME_STUB_VOID(dll, name, args)                                                                                \
    DECLSPEC_EXPORT void WINAPI name(MIME_STUB_ARGS_##args) { MIME_STUB(dll); }
