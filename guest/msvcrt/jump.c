/*
 * longjmp: the unwinding half. a jump buffer with a frame unwinds the
 * frames in between through RtlUnwind, so their handlers and destructors
 * run, then the registers come back from the buffer.
 */
#include "msvcrt_private.h"

DECLSPEC_NORETURN void MsvcrtLongjmpRestore(void *buf, int value);
void MsvcrtLongjmpLanding(void);

#define STATUS_LONGJUMP ((DWORD)0x80000026)

typedef struct {
    void *buf;
    int value;
} LONGJMP_STATE;

void __cdecl longjmp(void *buf, int value)
{
    if (!value)
        value = 1;
#if defined(__x86_64__)
    ULONG_PTR frame = ((ULONG_PTR *)buf)[0];
    if (frame) {
        /*
         * the record carries a copy of the buffer resuming at the landing
         * code, like msvcrt's, so an ntdll that restores registers from it
         * and one that only resumes at the target address both arrive
         * there with rax pointing at the state.
         */
        DECLSPEC_ALIGN(16) ULONG_PTR copy[32];
        memcpy(copy, buf, sizeof(copy));
        copy[10] = (ULONG_PTR)MsvcrtLongjmpLanding;
        LONGJMP_STATE state = {buf, value};
        EXCEPTION_RECORD record;
        memset(&record, 0, sizeof(record));
        record.ExceptionCode = STATUS_LONGJUMP;
        record.NumberParameters = 1;
        record.ExceptionInformation[0] = (ULONG_PTR)copy;
        RtlUnwind((PVOID)frame, (PVOID)MsvcrtLongjmpLanding, &record, &state);
    }
#endif
    /* arm64 restores directly: mime's arm64 ntdll has no frame based unwinding yet. */
    MsvcrtLongjmpRestore(buf, value);
}
