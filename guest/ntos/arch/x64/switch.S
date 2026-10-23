/* thread switching, kernel to user transitions and nested user mode callbacks, KeUserModeCallback. */
#include "ktrap.h"

    .text

/* the callee saved state of the windows x64 abi, below the return address. */
.macro PUSH_CALLEE
    subq    $KSWITCH_FRAME_SIZE, %rsp
    movq    %rbx, 0x00(%rsp)
    movq    %rbp, 0x08(%rsp)
    movq    %rdi, 0x10(%rsp)
    movq    %rsi, 0x18(%rsp)
    movq    %r12, 0x20(%rsp)
    movq    %r13, 0x28(%rsp)
    movq    %r14, 0x30(%rsp)
    movq    %r15, 0x38(%rsp)
    movups  %xmm6, 0x40(%rsp)
    movups  %xmm7, 0x50(%rsp)
    movups  %xmm8, 0x60(%rsp)
    movups  %xmm9, 0x70(%rsp)
    movups  %xmm10, 0x80(%rsp)
    movups  %xmm11, 0x90(%rsp)
    movups  %xmm12, 0xA0(%rsp)
    movups  %xmm13, 0xB0(%rsp)
    movups  %xmm14, 0xC0(%rsp)
    movups  %xmm15, 0xD0(%rsp)
.endm

.macro POP_CALLEE
    movq    0x00(%rsp), %rbx
    movq    0x08(%rsp), %rbp
    movq    0x10(%rsp), %rdi
    movq    0x18(%rsp), %rsi
    movq    0x20(%rsp), %r12
    movq    0x28(%rsp), %r13
    movq    0x30(%rsp), %r14
    movq    0x38(%rsp), %r15
    movups  0x40(%rsp), %xmm6
    movups  0x50(%rsp), %xmm7
    movups  0x60(%rsp), %xmm8
    movups  0x70(%rsp), %xmm9
    movups  0x80(%rsp), %xmm10
    movups  0x90(%rsp), %xmm11
    movups  0xA0(%rsp), %xmm12
    movups  0xB0(%rsp), %xmm13
    movups  0xC0(%rsp), %xmm14
    movups  0xD0(%rsp), %xmm15
    addq    $KSWITCH_FRAME_SIZE, %rsp
.endm

/*
 * void KiSwapContextInternal(ULONG64 *old_sp, ULONG64 new_sp, PVOID old_xsave, PVOID new_xsave)
 * a user thread's extended state is saved after its callee saved registers
 * and restored before the next thread's, so both match the switch point.
 */
    .globl  KiSwapContextInternal
    .p2align 4
KiSwapContextInternal:
    PUSH_CALLEE
    movq    %rdx, %r10
    testq   %r8, %r8
    jz      1f
    movl    KiXsaveMask(%rip), %eax
    movl    KiXsaveMask+4(%rip), %edx
    xsaveopt64 (%r8)
1:  movq    %rsp, (%rcx)
    movq    %r10, %rsp
    testq   %r9, %r9
    jz      2f
    movl    KiXsaveMask(%rip), %eax
    movl    KiXsaveMask+4(%rip), %edx
    xrstor64 (%r9)
2:  POP_CALLEE
    ret

/* first code run by a new thread: rbx = start routine, rsi = context. */
    .globl  KiThreadStartup
    .p2align 4
KiThreadStartup:
    movq    %rbx, %rcx
    movq    %rsi, %rdx
    xorl    %ebp, %ebp
    andq    $-16, %rsp
    subq    $0x20, %rsp
    call    KiThreadStartupC
1:  hlt
    jmp     1b

/* void KiEnterUserMode(PKTRAP_FRAME frame): frame becomes the stack top. */
    .globl  KiEnterUserMode
    .p2align 4
KiEnterUserMode:
    movq    %rcx, %rsp
    jmp     KiExceptionExit

/*
 * NTSTATUS KiCallUserMode(ULONG64 *callback_sp_slot, PKTRAP_FRAME frame)
 * saves the callee saved state, records the sp NtCallbackReturn resumes
 * from, copies the prepared user frame below it and returns to user mode
 * through it. entries during the callback build their frames below this
 * one, see KiExceptionExit.
 */
    .globl  KiCallUserMode
    .p2align 4
KiCallUserMode:
    PUSH_CALLEE
    movq    (%rcx), %rax
    movq    %rax, KCALLOUT_PREVIOUS_SP(%rsp)
    movq    %rsp, (%rcx)
    andq    $-16, %rsp
    subq    $KTRAP_FRAME_SIZE, %rsp
    movq    %rsp, %rdi
    movq    %rdx, %rsi
    movl    $(KTRAP_FRAME_SIZE / 8), %ecx
    cld
    rep movsq
    jmp     KiExceptionExit

/* void KiCallbackReturnInternal(ULONG64 callback_sp, NTSTATUS status) */
    .globl  KiCallbackReturnInternal
    .p2align 4
KiCallbackReturnInternal:
    movq    %rcx, %rsp
    movl    %edx, %eax
    POP_CALLEE
    ret

/* int KiSetJump(KJUMP_BUFFER *buf) / void KiLongJump(KJUMP_BUFFER *buf, int value) */
    .globl  KiSetJump
    .p2align 4
KiSetJump:
    movq    %rbx, 0x00(%rcx)
    movq    %rbp, 0x08(%rcx)
    movq    %rdi, 0x10(%rcx)
    movq    %rsi, 0x18(%rcx)
    movq    %r12, 0x20(%rcx)
    movq    %r13, 0x28(%rcx)
    movq    %r14, 0x30(%rcx)
    movq    %r15, 0x38(%rcx)
    leaq    8(%rsp), %rax
    movq    %rax, 0x40(%rcx)
    movq    (%rsp), %rax
    movq    %rax, 0x48(%rcx)
    movups  %xmm6, 0x50(%rcx)
    movups  %xmm7, 0x60(%rcx)
    movups  %xmm8, 0x70(%rcx)
    movups  %xmm9, 0x80(%rcx)
    movups  %xmm10, 0x90(%rcx)
    movups  %xmm11, 0xA0(%rcx)
    movups  %xmm12, 0xB0(%rcx)
    movups  %xmm13, 0xC0(%rcx)
    movups  %xmm14, 0xD0(%rcx)
    movups  %xmm15, 0xE0(%rcx)
    xorl    %eax, %eax
    ret

    .globl  KiLongJump
    .p2align 4
KiLongJump:
    movq    0x00(%rcx), %rbx
    movq    0x08(%rcx), %rbp
    movq    0x10(%rcx), %rdi
    movq    0x18(%rcx), %rsi
    movq    0x20(%rcx), %r12
    movq    0x28(%rcx), %r13
    movq    0x30(%rcx), %r14
    movq    0x38(%rcx), %r15
    movups  0x50(%rcx), %xmm6
    movups  0x60(%rcx), %xmm7
    movups  0x70(%rcx), %xmm8
    movups  0x80(%rcx), %xmm9
    movups  0x90(%rcx), %xmm10
    movups  0xA0(%rcx), %xmm11
    movups  0xB0(%rcx), %xmm12
    movups  0xC0(%rcx), %xmm13
    movups  0xD0(%rcx), %xmm14
    movups  0xE0(%rcx), %xmm15
    movl    %edx, %eax
    testl   %eax, %eax
    jnz     1f
    incl    %eax
1:  movq    0x40(%rcx), %rsp
    jmpq    *0x48(%rcx)

/*
 * ULONG64 KiCallService(PVOID routine, ULONG64 *args, ULONG stack_args)
 * calls a system service with the first four arguments in registers and
 * the rest past the home area, up to sixteen.
 */
    .globl  KiCallService
    .p2align 4
KiCallService:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rsi
    pushq   %rdi
    movq    %rcx, %rax
    movq    %rdx, %rsi
    movl    %r8d, %ecx
    leaq    0x20(,%rcx,8), %r9
    addq    $15, %r9
    andq    $-16, %r9
    subq    %r9, %rsp
    andq    $-16, %rsp
    leaq    0x20(%rsp), %rdi
    leaq    0x20(%rsi), %r10
    xchgq   %rsi, %r10
    cld
    rep movsq
    movq    %r10, %rsi
    movq    0x00(%rsi), %rcx
    movq    0x08(%rsi), %rdx
    movq    0x10(%rsi), %r8
    movq    0x18(%rsi), %r9
    call    *%rax
    leaq    -0x10(%rbp), %rsp
    popq    %rdi
    popq    %rsi
    popq    %rbp
    ret
