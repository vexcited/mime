/*
 * _setjmp and the register half of longjmp for x64, with msvcrt's jump
 * buffer: Frame, Rbx, Rsp, Rbp, Rsi, Rdi, R12 to R15, Rip, MxCsr, FpCsr,
 * Spare, then Xmm6 to Xmm15 from offset 0x60.
 *
 * _setjmp     rcx = jump buffer, rdx = frame for unwinding or 0
 * MsvcrtLongjmpRestore  rcx = jump buffer, edx = value, never returns
 * MsvcrtLongjmpLanding  reached by RtlUnwind with rax = &{buffer, value}
 */
    .text

    .globl  _setjmp
    .globl  _setjmpex
    .p2align 4
_setjmp:
_setjmpex:
    movq    %rdx, 0x00(%rcx)
    movq    %rbx, 0x08(%rcx)
    leaq    8(%rsp), %rax           /* the caller's stack pointer after return */
    movq    %rax, 0x10(%rcx)
    movq    %rbp, 0x18(%rcx)
    movq    %rsi, 0x20(%rcx)
    movq    %rdi, 0x28(%rcx)
    movq    %r12, 0x30(%rcx)
    movq    %r13, 0x38(%rcx)
    movq    %r14, 0x40(%rcx)
    movq    %r15, 0x48(%rcx)
    movq    (%rsp), %rax
    movq    %rax, 0x50(%rcx)
    stmxcsr 0x58(%rcx)
    fnstcw  0x5c(%rcx)
    movdqu  %xmm6, 0x60(%rcx)
    movdqu  %xmm7, 0x70(%rcx)
    movdqu  %xmm8, 0x80(%rcx)
    movdqu  %xmm9, 0x90(%rcx)
    movdqu  %xmm10, 0xa0(%rcx)
    movdqu  %xmm11, 0xb0(%rcx)
    movdqu  %xmm12, 0xc0(%rcx)
    movdqu  %xmm13, 0xd0(%rcx)
    movdqu  %xmm14, 0xe0(%rcx)
    movdqu  %xmm15, 0xf0(%rcx)
    xorl    %eax, %eax
    ret

    .globl  MsvcrtLongjmpLanding
    .p2align 4
MsvcrtLongjmpLanding:
    movl    8(%rax), %edx
    movq    (%rax), %rcx

    .globl  MsvcrtLongjmpRestore
MsvcrtLongjmpRestore:
    movl    %edx, %eax
    movq    0x08(%rcx), %rbx
    movq    0x20(%rcx), %rsi
    movq    0x28(%rcx), %rdi
    movq    0x30(%rcx), %r12
    movq    0x38(%rcx), %r13
    movq    0x40(%rcx), %r14
    movq    0x48(%rcx), %r15
    ldmxcsr 0x58(%rcx)
    fnclex
    fldcw   0x5c(%rcx)
    movdqu  0x60(%rcx), %xmm6
    movdqu  0x70(%rcx), %xmm7
    movdqu  0x80(%rcx), %xmm8
    movdqu  0x90(%rcx), %xmm9
    movdqu  0xa0(%rcx), %xmm10
    movdqu  0xb0(%rcx), %xmm11
    movdqu  0xc0(%rcx), %xmm12
    movdqu  0xd0(%rcx), %xmm13
    movdqu  0xe0(%rcx), %xmm14
    movdqu  0xf0(%rcx), %xmm15
    movq    0x50(%rcx), %rdx
    movq    0x18(%rcx), %rbp
    movq    0x10(%rcx), %rsp
    jmp     *%rdx
