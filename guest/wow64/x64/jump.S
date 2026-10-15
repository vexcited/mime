/*
 * a setjmp pair private to wow64: an exception of 32-bit code and the
 * return of a callback leave the simulation they happened in through it.
 * the callee saved state of the windows x64 abi, the stack and the return.
 */
    .text

/* int Wow64SetJump(WOW64_JUMP *jump) */
    .globl  Wow64SetJump
    .p2align 4
Wow64SetJump:
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
    movdqu  %xmm6, 0x50(%rcx)
    movdqu  %xmm7, 0x60(%rcx)
    movdqu  %xmm8, 0x70(%rcx)
    movdqu  %xmm9, 0x80(%rcx)
    movdqu  %xmm10, 0x90(%rcx)
    movdqu  %xmm11, 0xA0(%rcx)
    movdqu  %xmm12, 0xB0(%rcx)
    movdqu  %xmm13, 0xC0(%rcx)
    movdqu  %xmm14, 0xD0(%rcx)
    movdqu  %xmm15, 0xE0(%rcx)
    xorl    %eax, %eax
    ret

/* void Wow64LongJump(WOW64_JUMP *jump, int value), value 0 returning 1 */
    .globl  Wow64LongJump
    .p2align 4
Wow64LongJump:
    movq    0x00(%rcx), %rbx
    movq    0x08(%rcx), %rbp
    movq    0x10(%rcx), %rdi
    movq    0x18(%rcx), %rsi
    movq    0x20(%rcx), %r12
    movq    0x28(%rcx), %r13
    movq    0x30(%rcx), %r14
    movq    0x38(%rcx), %r15
    movdqu  0x50(%rcx), %xmm6
    movdqu  0x60(%rcx), %xmm7
    movdqu  0x70(%rcx), %xmm8
    movdqu  0x80(%rcx), %xmm9
    movdqu  0x90(%rcx), %xmm10
    movdqu  0xA0(%rcx), %xmm11
    movdqu  0xB0(%rcx), %xmm12
    movdqu  0xC0(%rcx), %xmm13
    movdqu  0xD0(%rcx), %xmm14
    movdqu  0xE0(%rcx), %xmm15
    movq    0x40(%rcx), %rsp
    movl    %edx, %eax
    testl   %eax, %eax
    jnz     1f
    incl    %eax
1:  jmpq    *0x48(%rcx)
