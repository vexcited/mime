/*
 * the x86 halves msvcrt writes in assembly: the jump buffer's registers,
 * _EH_prolog, and calls into the code of a function's own frame, as
 * filters, __finally and __except blocks run with that function's ebp.
 *
 * the jump buffer: Ebp, Ebx, Edi, Esi, Esp, Eip, Registration, TryLevel,
 * Cookie, UnwindFunc, UnwindData[6].
 */
    .text

.macro SAVE_REGISTERS
    movl    4(%esp), %edx
    movl    %ebp, 0(%edx)
    movl    %ebx, 4(%edx)
    movl    %edi, 8(%edx)
    movl    %esi, 12(%edx)
    leal    4(%esp), %eax
    movl    %eax, 16(%edx)
    movl    (%esp), %eax
    movl    %eax, 20(%edx)
.endm

/* int _setjmp(jmp_buf): the registration then and its try level, without the cookie of _setjmp3. */
    .globl  __setjmp
__setjmp:
    SAVE_REGISTERS
    pushl   %edx
    calll   _MsvcrtSetjmp
    addl    $4, %esp
    xorl    %eax, %eax
    retl

/* int _setjmp3(jmp_buf, count, ...): the unwind function and data the compiler passes. */
    .globl  __setjmp3
__setjmp3:
    SAVE_REGISTERS
    leal    12(%esp), %eax
    pushl   %eax
    pushl   12(%esp)
    pushl   %edx
    calll   _MsvcrtSetjmp3
    addl    $12, %esp
    xorl    %eax, %eax
    retl

/* void MsvcrtLongjmpRestore(jmp_buf, value): the registers of the buffer, never returns. */
    .globl  _MsvcrtLongjmpRestore
_MsvcrtLongjmpRestore:
    movl    4(%esp), %edx
    movl    8(%esp), %eax
    movl    0(%edx), %ebp
    movl    4(%edx), %ebx
    movl    8(%edx), %edi
    movl    12(%edx), %esi
    movl    16(%edx), %esp
    jmpl    *20(%edx)

/*
 * _EH_prolog: the c++ frame of the caller, eax its handler: the try level
 * -1, the handler and the previous registration pushed, the registration
 * linked, then ebp set up as the caller would, its return address kept.
 */
    .globl  __EH_prolog
__EH_prolog:
    pushl   $-1
    pushl   %eax
    movl    %fs:0, %eax
    pushl   %eax
    movl    12(%esp), %eax
    movl    %esp, %fs:0
    movl    %ebp, 12(%esp)
    leal    12(%esp), %ebp
    pushl   %eax
    retl

/* int _abnormal_termination(void), the caller's ebp handed on. */
    .globl  __abnormal_termination
__abnormal_termination:
    pushl   %ebp
    calll   _MsvcrtAbnormalTermination
    addl    $4, %esp
    retl

/* ULONG MsvcrtCallWithFrame(code, ebp): calls code of a function with its frame pointer. */
    .globl  _MsvcrtCallWithFrame
_MsvcrtCallWithFrame:
    pushl   %ebp
    pushl   %ebx
    pushl   %esi
    pushl   %edi
    movl    20(%esp), %eax
    movl    24(%esp), %ebp
    calll   *%eax
    popl    %edi
    popl    %esi
    popl    %ebx
    popl    %ebp
    retl

/* ULONG __fastcall MsvcrtCallFastcall(function, value): calls a fastcall function of one argument, ecx. */
    .globl  @MsvcrtCallFastcall@8
@MsvcrtCallFastcall@8:
    pushl   %ebx
    pushl   %esi
    pushl   %edi
    movl    %ecx, %eax
    movl    %edx, %ecx
    calll   *%eax
    popl    %edi
    popl    %esi
    popl    %ebx
    retl

/* void MsvcrtJumpWithFrame(code, ebp): goes on at code with the function's frame, never returns. */
    .globl  _MsvcrtJumpWithFrame
_MsvcrtJumpWithFrame:
    movl    4(%esp), %eax
    movl    8(%esp), %ebp
    jmpl    *%eax

/*
 * __CxxFrameHandler(record, registration, context, dispatcher), eax the
 * FuncInfo the function's handler thunk loads: handed on as a fifth
 * argument.
 */
    .globl  ___CxxFrameHandler
    .globl  ___CxxFrameHandler2
    .globl  ___CxxFrameHandler3
___CxxFrameHandler:
___CxxFrameHandler2:
___CxxFrameHandler3:
    pushl   %eax
    pushl   20(%esp)
    pushl   20(%esp)
    pushl   20(%esp)
    pushl   20(%esp)
    calll   _MsvcrtCxxFrameHandler32
    addl    $20, %esp
    retl

/*
 * MsvcrtContinueCatch(continuation, node, registration): the function goes
 * on after its catch block with its frame and saved stack pointer, the
 * registration list back to the one given; never returns.
 */
    .globl  _MsvcrtContinueCatch
_MsvcrtContinueCatch:
    movl    4(%esp), %eax
    movl    12(%esp), %edx
    movl    %edx, %fs:0
    movl    8(%esp), %edx
    leal    0xC(%edx), %ebp
    movl    -4(%edx), %esp
    jmpl    *%eax
