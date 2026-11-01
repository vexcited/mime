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

/* the frame of the procedure that is running is the head of the exception list. */
static ULONG *current_frame(void)
{
    ULONG *frame;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(frame));
    return frame;
}

/* On Error stores where to go in the frame of the procedure. -3 ends the handling of an error that is in progress. */
VB_STD ULONG vb___vbaOnError(LONG target)
{
    ULONG *frame = current_frame();
    VbThread *t = VbThreadState();
    t->err_number = 0;
    if (target == -3) {
        frame[4] &= ~2u;
        return (ULONG)(ULONG_PTR)frame;
    }
    frame[5] = (ULONG)target;
    return (ULONG)target;
}

VB_STD ULONG vb___vbaExitProc(void)
{
    ULONG *frame = current_frame();
    if (frame[4] & 2)
        VbThreadState()->err_number = 0;
    frame[4] = 0;
    return (ULONG)(ULONG_PTR)frame;
}

/* the result of a call of a method of an object, which is an error when it failed. */
VB_STD LONG vb___vbaHresultCheckObj(LONG hr, IUnknown *obj, const IID *iid, LONG index)
{
    if (hr < 0)
        VbRaise(VbErrorOfHresult(hr));
    return hr;
}

VB_STD LONG vb_rtcMsgBox(BSTR prompt, LONG flags, VARIANT *title, VARIANT *help, VARIANT *context)
{
    WCHAR caption[128] = L"Project1";
    if (title && V_VT(title) == VT_BSTR && V_BSTR(title))
        lstrcpynW(caption, V_BSTR(title), 128);
    HWND owner = GetActiveWindow();
    return MessageBoxW(owner, prompt ? prompt : L"", caption, (UINT)flags & ~MB_TOPMOST);
}
