/*
 * the structured exception handling of x86 programs compiled against
 * msvcrt: the frame handlers __try blocks register (_except_handler3 and
 * the cookie checked _except_handler4_common), the unwinds of their scope
 * tables, and longjmp, which runs the __finally blocks and destructors of
 * the frames it leaves.
 *
 * a frame's registration sits 0x10 below its ebp: the previous one, the
 * handler, the scope table and the try level, with the stack pointer of
 * the function 8 bytes below and the exception pointers its filters read
 * 4 bytes below.
 */
#include "msvcrt_private.h"

typedef struct SCOPE {
    int enclosing;
    void *filter;
    void *handler;
} SCOPE;

typedef struct EH3_FRAME {
    struct EH3_FRAME *next;
    void *handler;
    const SCOPE *scopes;
    int level;
} EH3_FRAME;

/* the scope table of _except_handler4, xored with the module's cookie. */
typedef struct EH4_SCOPES {
    int gs_cookie_offset;
    int gs_cookie_xor_offset;
    int eh_cookie_offset;
    int eh_cookie_xor_offset;
    SCOPE scopes[1];
} EH4_SCOPES;

/* the jump buffer of _setjmp3. */
typedef struct JUMP_BUFFER {
    ULONG Ebp, Ebx, Edi, Esi, Esp, Eip;
    EH3_FRAME *Registration;
    int TryLevel;
    ULONG Cookie;
    void(__stdcall *UnwindFunc)(struct JUMP_BUFFER *);
    ULONG UnwindData[6];
} JUMP_BUFFER;

/* the registration local unwinds push, which _abnormal_termination looks for. */
typedef struct UNWIND_FRAME {
    void *next;
    void *handler;
    EH3_FRAME *frame;
} UNWIND_FRAME;

typedef enum {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind,
} EXCEPTION_DISPOSITION;

/* whether the processor has the pentium fdiv flaw, which the _adj_ helpers work around: never. */
int MsvcrtAdjustFdiv = 0;

#define TOP_LEVEL3 (-1)
#define TOP_LEVEL4 (-2)
#define JUMP_COOKIE 0x56433230 /* 'VC20' */
#define UNWIND_FLAGS (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)

ULONG MsvcrtCallWithFrame(void *code, ULONG ebp);
DECLSPEC_NORETURN void MsvcrtJumpWithFrame(void *code, ULONG ebp);
ULONG __fastcall MsvcrtCallFastcall(void *function, ULONG value);
DECLSPEC_NORETURN void MsvcrtLongjmpRestore(void *buf, int value);

static ULONG frame_ebp(const EH3_FRAME *frame) { return (ULONG)frame + 0x10; }

static void *read_fs0(void)
{
    void *r;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(r));
    return r;
}

static void write_fs0(void *r) { __asm__ volatile("movl %0, %%fs:0" : : "r"(r) : "memory"); }

/* a nested exception or collided unwind during a local unwind leaves this frame to the outer unwind. */
static EXCEPTION_DISPOSITION __cdecl unwind_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context,
                                                   void *dispatcher)
{
    if (!(record->ExceptionFlags & UNWIND_FLAGS))
        return ExceptionContinueSearch;
    *(void **)dispatcher = frame;
    return ExceptionCollidedUnwind;
}

/* the __finally blocks of a frame from its try level down to stop, each level taken off before its block runs. */
static void local_unwind(EH3_FRAME *frame, const SCOPE *scopes, int stop, int top)
{
    UNWIND_FRAME guard = {read_fs0(), (void *)unwind_handler, frame};
    write_fs0(&guard);
    while (frame->level != top && frame->level != stop) {
        const SCOPE *s = &scopes[frame->level];
        frame->level = s->enclosing;
        if (!s->filter)
            MsvcrtCallWithFrame(s->handler, frame_ebp(frame));
    }
    write_fs0(guard.next);
}

void __cdecl _local_unwind2(EH3_FRAME *frame, int stop) { local_unwind(frame, frame->scopes, stop, TOP_LEVEL3); }

void __cdecl _global_unwind2(EH3_FRAME *frame) { RtlUnwind(frame, NULL, NULL, NULL); }

/*
 * whether the __finally block running was reached by an unwind rather
 * than by leaving its __try. _abnormal_termination passes the block's
 * frame pointer, still in ebp.
 */
int __cdecl MsvcrtAbnormalTermination(ULONG ebp)
{
    UNWIND_FRAME *top = read_fs0();
    return top && top != (void *)~0u && top->handler == (void *)unwind_handler && frame_ebp(top->frame) == ebp;
}

static EXCEPTION_DISPOSITION except_handler3(EXCEPTION_RECORD *record, EH3_FRAME *frame, CONTEXT *context)
{
    if (record->ExceptionFlags & UNWIND_FLAGS) {
        _local_unwind2(frame, TOP_LEVEL3);
        return ExceptionContinueSearch;
    }
    EXCEPTION_POINTERS pointers = {record, context};
    ((EXCEPTION_POINTERS **)frame)[-1] = &pointers;
    for (int level = frame->level; level != TOP_LEVEL3; level = frame->scopes[level].enclosing) {
        const SCOPE *s = &frame->scopes[level];
        if (!s->filter)
            continue;
        LONG verdict = (LONG)MsvcrtCallWithFrame(s->filter, frame_ebp(frame));
        if (verdict < 0)
            return ExceptionContinueExecution;
        if (verdict > 0) {
            _global_unwind2(frame);
            _local_unwind2(frame, level);
            frame->level = s->enclosing;
            MsvcrtJumpWithFrame(s->handler, frame_ebp(frame));
        }
    }
    return ExceptionContinueSearch;
}

EXCEPTION_DISPOSITION __cdecl _except_handler3(EXCEPTION_RECORD *record, EH3_FRAME *frame, CONTEXT *context,
                                               void *dispatcher)
{
    return except_handler3(record, frame, context);
}

/* the frames of older compilers, as _except_handler3's: a guess, the layout of those is not known here. */
EXCEPTION_DISPOSITION __cdecl _except_handler2(EXCEPTION_RECORD *record, EH3_FRAME *frame, CONTEXT *context,
                                               void *dispatcher)
{
    return except_handler3(record, frame, context);
}

static const EH4_SCOPES *eh4_scopes(const ULONG *cookie, const EH3_FRAME *frame)
{
    return (const EH4_SCOPES *)((ULONG)frame->scopes ^ *cookie);
}

/* the security cookies a frame keeps, checked by the module's own function, which ends the process when wrong. */
static void check_cookies(const EH4_SCOPES *scopes, ULONG ebp, void *check)
{
    if (scopes->gs_cookie_offset != TOP_LEVEL4)
        MsvcrtCallFastcall(check, *(ULONG *)(ebp + scopes->gs_cookie_offset) ^ (ebp + scopes->gs_cookie_xor_offset));
    MsvcrtCallFastcall(check, *(ULONG *)(ebp + scopes->eh_cookie_offset) ^ (ebp + scopes->eh_cookie_xor_offset));
}

void __cdecl _local_unwind4(const ULONG *cookie, EH3_FRAME *frame, int stop)
{
    local_unwind(frame, eh4_scopes(cookie, frame)->scopes, stop, TOP_LEVEL4);
}

EXCEPTION_DISPOSITION __cdecl _except_handler4_common(const ULONG *cookie, void *check, EXCEPTION_RECORD *record,
                                                      EH3_FRAME *frame, CONTEXT *context, void *dispatcher)
{
    const EH4_SCOPES *scopes = eh4_scopes(cookie, frame);
    ULONG ebp = frame_ebp(frame);
    check_cookies(scopes, ebp, check);
    if (record->ExceptionFlags & UNWIND_FLAGS) {
        if (frame->level != TOP_LEVEL4)
            _local_unwind4(cookie, frame, TOP_LEVEL4);
        return ExceptionContinueSearch;
    }
    EXCEPTION_POINTERS pointers = {record, context};
    ((EXCEPTION_POINTERS **)frame)[-1] = &pointers;
    for (int level = frame->level; level != TOP_LEVEL4; level = scopes->scopes[level].enclosing) {
        const SCOPE *s = &scopes->scopes[level];
        if (!s->filter)
            continue;
        LONG verdict = (LONG)MsvcrtCallWithFrame(s->filter, ebp);
        if (verdict < 0)
            return ExceptionContinueExecution;
        if (verdict > 0) {
            _global_unwind2(frame);
            if (frame->level != level)
                _local_unwind4(cookie, frame, level);
            frame->level = s->enclosing;
            check_cookies(scopes, ebp, check);
            MsvcrtJumpWithFrame(s->handler, ebp);
        }
    }
    return ExceptionContinueSearch;
}

/* ------------------------------------------------------------------ setjmp and longjmp */

static int frame_level(EH3_FRAME *frame)
{
    if (!frame || frame == (void *)~0u)
        return TOP_LEVEL3;
    return frame->handler == (void *)_except_handler3 ? frame->level : TOP_LEVEL3;
}

void __cdecl MsvcrtSetjmp(JUMP_BUFFER *buf)
{
    buf->Registration = read_fs0();
    buf->TryLevel = frame_level(buf->Registration);
}

/* _setjmp3's arguments after the count: the unwind function, the try level, then its data. */
void __cdecl MsvcrtSetjmp3(JUMP_BUFFER *buf, int count, const ULONG *args)
{
    buf->Registration = read_fs0();
    buf->Cookie = JUMP_COOKIE;
    buf->UnwindFunc = count >= 1 ? (void *)args[0] : NULL;
    buf->TryLevel = count >= 2 ? (int)args[1] : frame_level(buf->Registration);
    for (int i = 0; i < 6; i++)
        buf->UnwindData[i] = i + 2 < count ? args[i + 2] : 0;
}

void __stdcall _seh_longjmp_unwind(JUMP_BUFFER *buf) { _local_unwind2(buf->Registration, buf->TryLevel); }

void __stdcall _seh_longjmp_unwind4(JUMP_BUFFER *buf)
{
    _local_unwind4((const ULONG *)buf->UnwindData[0], buf->Registration, buf->TryLevel);
}

/* the frames newer than setjmp's are unwound, then the one that called setjmp down to its try level. */
void __cdecl longjmp(JUMP_BUFFER *buf, int value)
{
    if (!value)
        value = 1;
    if (buf->Registration && buf->Registration != read_fs0())
        _global_unwind2(buf->Registration);
    if (buf->Registration && buf->Registration != (void *)~0u) {
        if (buf->Cookie == JUMP_COOKIE && buf->UnwindFunc)
            buf->UnwindFunc(buf);
        else if (buf->Registration->handler == (void *)_except_handler3)
            _local_unwind2(buf->Registration, buf->TryLevel);
    }
    MsvcrtLongjmpRestore(buf, value);
}

void __cdecl _longjmpex(JUMP_BUFFER *buf, int value) { longjmp(buf, value); }
