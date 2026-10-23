/*
 * longjmp, the unwinding half: a jump buffer with a frame unwinds the
 * frames in between through RtlUnwind so their handlers and destructors
 * run, then the registers are restored from the buffer.
 */
#include "msvcrt_private.h"

/* x86 has its own in x86/seh.c, with the try levels of its frames. */
#ifndef __i386__

DECLSPEC_NORETURN void MsvcrtLongjmpRestore(void *buf, int value);
void MsvcrtLongjmpLanding(void);

#define STATUS_LONGJUMP ((DWORD)0x80000026)

typedef struct {
    void *buf;
    int value;
} LONGJMP_STATE;

/* jump buffer size in words, and the word holding setjmp's return address: rip on x64, lr on arm64. */
#ifdef __aarch64__
#define JUMP_WORDS 24
#define JUMP_RESUME 13
#else
#define JUMP_WORDS 32
#define JUMP_RESUME 10
#endif

void __cdecl longjmp(void *buf, int value)
{
    if (!value)
        value = 1;
#if defined(__x86_64__) || defined(__aarch64__)
    ULONG_PTR frame = ((ULONG_PTR *)buf)[0];
    if (frame) {
        /*
         * like msvcrt, the record carries a copy of the buffer that resumes
         * at the landing code, so an ntdll that restores registers from it
         * and one that only resumes at the target address both arrive there
         * with rax or x0 pointing at the state.
         */
        DECLSPEC_ALIGN(16) ULONG_PTR copy[JUMP_WORDS];
        memcpy(copy, buf, sizeof(copy));
        copy[JUMP_RESUME] = (ULONG_PTR)MsvcrtLongjmpLanding;
        LONGJMP_STATE state = {buf, value};
        EXCEPTION_RECORD record;
        memset(&record, 0, sizeof(record));
        record.ExceptionCode = STATUS_LONGJUMP;
        record.NumberParameters = 1;
        record.ExceptionInformation[0] = (ULONG_PTR)copy;
        RtlUnwind((PVOID)frame, (PVOID)MsvcrtLongjmpLanding, &record, &state);
    }
#endif
    MsvcrtLongjmpRestore(buf, value);
}
#endif
