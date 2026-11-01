/*
 * the frame of a procedure, the stack probe of a procedure with many locals, and message boxes.
 */
#include "vb.h"

/*
 * a procedure with more than a page of locals asks for them in eax. the stack is probed page by page, lowered by the
 * size and the new locals are cleared, which is what lets a program read a local it never set as zero.
 */
__attribute__((naked)) void vb___vbaChkstk(void)
{
    __asm__(".intel_syntax noprefix\n\t"
            "push ecx\n\tpush edi\n\tpush eax\n\tlea ecx, [esp+0x10]\n"
            "1:\n\tcmp eax, 0x1000\n\tjb 2f\n\tsub ecx, 0x1000\n\ttest [ecx], eax\n\tsub eax, 0x1000\n\tjmp 1b\n"
            "2:\n\tsub ecx, eax\n\tmov eax, esp\n\ttest [ecx], eax\n\tlea esp, [ecx-0x10]\n"
            "\tmov ecx, [eax]\n\tmov [esp], ecx\n\tmov ecx, [eax+4]\n\tmov [esp+4], ecx\n"
            "\tmov ecx, [eax+8]\n\tmov [esp+8], ecx\n\tmov ecx, [eax+0xC]\n\tmov [esp+0xC], ecx\n"
            "\tpop ecx\n\tlea edi, [esp+0xC]\n\tshr ecx, 2\n\txor eax, eax\n\trep stosd\n\tpop edi\n\tpop ecx\n\tret\n\t.att_syntax");
}

/*
 * the frame a compiled procedure registers in the exception list, as the runtime reads it: the next frame, the handler,
 * the stack pointer to return to, the description of the procedure, the state, the target of On Error, the statement an
 * error happened in and the statement that runs.
 */
typedef struct VbFrame {
    struct VbFrame *next;
    void *handler;
    ULONG stack;
    const struct VbProc *proc;
    ULONG state;
    LONG on_error;
    ULONG error_statement;
    ULONG statement;
} VbFrame;

typedef struct VbLabels {
    LONG count;
    struct {
        LONG key;
        ULONG address;
    } entry[1];
} VbLabels;

/* flags of a procedure: 0x01 ends it on any error, 0x02 and 0x04 and 0x08 have cleanup, 0x30 can handle errors. */
typedef struct VbProc {
    USHORT flags;
    SHORT locals;
    ULONG exit;
    ULONG cleanup_a;
    ULONG cleanup_b;
    const VbLabels *labels;
    const ULONG *resume;
    const USHORT *lines;
} VbProc;

enum { FRAME_TOP = 1, FRAME_IN_HANDLER = 2 };

VB_C int vb___vbaExceptHandler(EXCEPTION_RECORD *record, VbFrame *frame);

static VbFrame *current_frame(void)
{
    VbFrame *frame;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(frame));
    return frame;
}

/* the local variables of a procedure are addressed from the base register the frame sits below. */
static ULONG locals_of(const VbFrame *frame)
{
    return (ULONG)(ULONG_PTR)frame + (ULONG)(LONG)frame->proc->locals + 12;
}

/* runs a routine of the procedure with its base register. */
static void call_with_base(ULONG routine, ULONG base)
{
    /* the routine and the base must not sit in the register that is overwritten with the base. */
    __asm__ volatile("pushl %%ebp\n\tmovl %%ecx, %%ebp\n\tcall *%%eax\n\tpopl %%ebp" ::"a"(routine), "c"(base) : "edx", "memory", "cc");
}

/* leaves the handler for code of the procedure, with its frame back on top of the list. */
__attribute__((noreturn)) static void transfer(const VbFrame *frame, ULONG target)
{
    ULONG base = locals_of(frame), stack = frame->stack;
    __asm__ volatile("movl %0, %%fs:0" ::"r"(frame));
    __asm__ volatile("movl %1, %%ebp\n\tmovl %2, %%esp\n\tjmp *%0" ::"a"(target), "c"(base), "d"(stack));
    __builtin_unreachable();
}

/* frees what the procedure holds in temporaries: its two cleanup routines, as the flags of the procedure ask. */
static void clean_up(VbFrame *frame, const VbProc *proc, int a3, int a4, int a5)
{
    ULONG saved = a5;
    BOOL keeps_state = (ULONG)(LONG)proc->locals + 12 > 0x10;
    if (keeps_state) {
        saved = frame->state;
        frame->state = a5 ? 4 : 0;
    }
    ULONG base = locals_of(frame);
    if ((a5 && (proc->flags & 8)) || (a4 && (proc->flags & 4)))
        call_with_base(proc->cleanup_b, base);
    if (a3 && (proc->flags & 2))
        call_with_base(proc->cleanup_a, base);
    if (keeps_state)
        frame->state = saved;
}

/* a procedure is left with the error it ends with as its result. */
__attribute__((noreturn)) static void leave_with_error(VbFrame *frame)
{
    VbThread *t = VbThreadState();
    frame->state = (ULONG)t->hresult;
    t->err_number = 0;
    t->hresult = 0;
    t->erl = 0;
    transfer(frame, frame->proc->exit);
}

/* the frames between the top of the list and the one that handles the error are left, which frees their temporaries. */
static void unwind_to(VbFrame *target)
{
    VbFrame *frame = current_frame();
    while (frame && (ULONG_PTR)frame != 0xFFFFFFFFu && frame != target) {
        VbFrame *next = frame->next;
        if (frame->handler == (void *)vb___vbaExceptHandler && frame->proc)
            clean_up(frame, frame->proc, 1, 1, 1);
        frame = next;
    }
    __asm__ volatile("movl %0, %%fs:0" ::"r"(target));
}

/* the runtime error a machine exception stands for. */
static LONG error_of_exception(const EXCEPTION_RECORD *record)
{
    switch ((ULONG)record->ExceptionCode) {
    case 0xC000008E:
    case 0xC0000094: return VBERR_DIVISION_BY_ZERO;
    case 0xC000008F: return 51;
    case 0xC000008D:
    case 0xC0000090:
    case 0xC0000091:
    case 0xC0000093:
    case 0xC0000095: return VBERR_OVERFLOW;
    case 0xC0000092:
    case 0xC00000FD: return 28;
    case 0xC0000005: {
        ULONG_PTR address = record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0;
        return address <= 0xFFFF || address >= 0xFFFFF000u ? 91 : 0;
    }
    }
    return 0;
}

/* On Error stores where to go in the frame of the procedure. -3 ends the handling of an error that is in progress. */
VB_STD ULONG vb___vbaOnError(LONG target)
{
    VbFrame *frame = current_frame();
    VbClearError();
    if (target == -3) {
        frame->state &= ~(ULONG)FRAME_IN_HANDLER;
        return (ULONG)(ULONG_PTR)frame;
    }
    frame->on_error = target;
    return (ULONG)target;
}

VB_STD ULONG vb___vbaExitProc(void)
{
    VbFrame *frame = current_frame();
    if (frame->state & FRAME_IN_HANDLER) {
        VbClearError();
    }
    frame->state = 0;
    return (ULONG)(ULONG_PTR)frame;
}

/* the address of the code of the handler for the label a procedure named in On Error. */
static ULONG label_target(const VbFrame *frame)
{
    const VbLabels *labels = frame->proc->labels;
    for (LONG i = 0; i < labels->count; i++)
        if (labels->entry[i].key == frame->on_error)
            return labels->entry[i].address;
    return 0;
}

/* the handler every compiled procedure registers. an error goes to the nearest procedure that handles it. */
VB_C int vb___vbaExceptHandler(EXCEPTION_RECORD *record, VbFrame *frame)
{
    const VbProc *proc = frame->proc;
    if (!proc || IsBadReadPtr(proc, 4))
        return 1;
    if (proc->locals < 4 || proc->locals > 0x14)
        return 1;
    if (record->ExceptionFlags & 6) {
        clean_up(frame, proc, 1, 1, 1);
        return 1;
    }
    BOOL handles = (proc->flags & 0x30) && frame->on_error && !(frame->state & FRAME_IN_HANDLER);
    if (!handles && !(proc->flags & 1))
        return 1;
    LONG number;
    if ((ULONG)record->ExceptionCode == VB_EXCEPTION) {
        number = (LONG)record->ExceptionInformation[0];
    } else {
        number = error_of_exception(record);
        if (!number)
            return 1;
        /* the machine exception becomes the error, which is raised again from here. */
        VbRaise(number);
    }
    BOOL end = number == VBERR_END;
    if (end && !(proc->flags & 1))
        return 1;
    VbThread *t = VbThreadState();
    unwind_to(frame);
    if (!end) {
        if (handles) {
            clean_up(frame, proc, 0, 1, 0);
            t->erl = (proc->flags & 0x40) ? proc->lines[frame->statement + 1] : 0;
            switch (frame->on_error) {
            case -2: transfer(frame, proc->resume[frame->statement]);
            case -1: transfer(frame, proc->resume[frame->statement + 1]);
            default:
                frame->error_statement = frame->statement;
                frame->state |= FRAME_IN_HANDLER;
                transfer(frame, label_target(frame));
            }
        }
        if ((frame->state & FRAME_TOP) && !(proc->flags & 0x80))
            VbTerminateWithError(number);
    }
    clean_up(frame, proc, 0, 1, 1);
    leave_with_error(frame);
}

/* Resume and Resume Next leave the handler and go on in the statement of the error or the one after it. */
VB_STD ULONG vb___vbaResume(LONG mode)
{
    VbFrame *frame = current_frame();
    VbThreadState()->erl = 0;
    if (!(frame->state & FRAME_IN_HANDLER))
        VbRaise(20);
    VbClearError();
    frame->state &= ~(ULONG)FRAME_IN_HANDLER;
    if (mode == -2)
        transfer(frame, frame->proc->resume[frame->error_statement]);
    if (mode == -1)
        transfer(frame, frame->proc->resume[frame->error_statement + 1]);
    return 0;
}

VB_STD LONG vb_rtcGetErl(void)
{
    return VbThreadState()->erl;
}

/* the result of a call of a method of an object, which is an error when it failed. */
VB_STD LONG vb___vbaHresultCheckObj(LONG hr, IUnknown *obj, const IID *iid, LONG index)
{
    if (hr < 0)
        VbRaiseHr(hr);
    return hr;
}

/* the text of a variant argument as a string, or null when it is not there. a null value is an error. */
static BSTR text_argument(VARIANT *v)
{
    if (!v || (V_VT(v) == VT_ERROR && V_ERROR(v) == DISP_E_PARAMNOTFOUND))
        return nullptr;
    VARIANT in, out;
    VariantInit(&in);
    VariantInit(&out);
    VbCheck(VariantCopyInd(&in, v));
    if (V_VT(&in) == VT_NULL)
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    HRESULT hr = V_VT(&in) == VT_EMPTY ? S_OK : VariantChangeTypeEx(&out, &in, VbLocale(), 0, VT_BSTR);
    VariantClear(&in);
    VbCheck(hr);
    if (V_VT(&out) != VT_BSTR)
        V_BSTR(&out) = SysAllocString(L"");
    return V_BSTR(&out);
}

VB_STD LONG vb_rtcMsgBox(VARIANT *prompt, LONG flags, VARIANT *title, VARIANT *help, VARIANT *context)
{
    BSTR text = text_argument(prompt), caption = text_argument(title);
    WCHAR default_caption[64];
    VbProjectTitle(default_caption, 64);
    HWND owner = GetActiveWindow();
    LONG answer = MessageBoxW(owner, text ? text : L"", caption ? caption : default_caption, (UINT)flags & ~MB_TOPMOST);
    SysFreeString(text);
    SysFreeString(caption);
    return answer;
}
