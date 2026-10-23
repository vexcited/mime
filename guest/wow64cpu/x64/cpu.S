/*
 * WOW64 mode switching. native TEB TLS slot 1 holds the 32-bit CPU area; slot 0 saves the native stack while 32-bit
 * code runs. syscall stubs call Wow64CpuTransition through Wow64Transition; callbacks return to Wow64CpuReturn32.
 * both gates use far jumps limited to the low 4 GiB, where this DLL must reside.
 */
#define TEB_STACKPTR64 0x1480
#define TEB_CPU 0x1488

#define CPU_FLAGS 0x00
#define CPU_EDI 0xA0
#define CPU_ESI 0xA4
#define CPU_EBX 0xA8
#define CPU_EDX 0xAC
#define CPU_ECX 0xB0
#define CPU_EAX 0xB4
#define CPU_EBP 0xB8
#define CPU_EIP 0xBC
#define CPU_EFLAGS 0xC4
#define CPU_ESP 0xC8
#define CPU_EXTENDED 0xD0

#define CODE32 0x23
#define DATA32 0x2B
#define CODE64 0x33
#define TEB32 0x53

#define FRAME 0xB8 /* xmm6 to xmm15, the far pointer, alignment. */

    .text

/*
 * Wow64CpuRun returns 0 for a system call or 1 for a completed call into 32-bit code. the reset flag restores all
 * registers, segments and extended state; otherwise only syscall-preserved state is restored.
 */
    .globl  Wow64CpuRun
    .p2align 4
Wow64CpuRun:
    pushq   %rbp
    pushq   %rbx
    pushq   %rdi
    pushq   %rsi
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    subq    $FRAME, %rsp
    movdqu  %xmm6, 0x00(%rsp)
    movdqu  %xmm7, 0x10(%rsp)
    movdqu  %xmm8, 0x20(%rsp)
    movdqu  %xmm9, 0x30(%rsp)
    movdqu  %xmm10, 0x40(%rsp)
    movdqu  %xmm11, 0x50(%rsp)
    movdqu  %xmm12, 0x60(%rsp)
    movdqu  %xmm13, 0x70(%rsp)
    movdqu  %xmm14, 0x80(%rsp)
    movdqu  %xmm15, 0x90(%rsp)
    movq    %rsp, %gs:TEB_STACKPTR64
    movq    %gs:TEB_CPU, %r11
    btrw    $0, CPU_FLAGS(%r11)
    jc      2f

    /* back into the stub that made the call: a far jump to the saved eip. */
    movl    CPU_EIP(%r11), %eax
    movl    %eax, 0xA0(%rsp)
    movl    $CODE32, 0xA4(%rsp)
    leaq    0xA0(%rsp), %r10
    movl    CPU_EDI(%r11), %edi
    movl    CPU_ESI(%r11), %esi
    movl    CPU_EBX(%r11), %ebx
    movl    CPU_EBP(%r11), %ebp
    movl    CPU_ECX(%r11), %ecx
    movl    CPU_EDX(%r11), %edx
    movl    CPU_EAX(%r11), %eax
    movl    CPU_ESP(%r11), %esp
    ljmpl   *(%r10)

    /* everything, through an interrupt return. */
2:  fxrstor CPU_EXTENDED(%r11)
    movl    $DATA32, %eax
    movw    %ax, %ds
    movw    %ax, %es
    movl    $TEB32, %eax
    movw    %ax, %fs
    pushq   $DATA32
    movl    CPU_ESP(%r11), %eax
    pushq   %rax
    movl    CPU_EFLAGS(%r11), %eax
    pushq   %rax
    pushq   $CODE32
    movl    CPU_EIP(%r11), %eax
    pushq   %rax
    movl    CPU_EDI(%r11), %edi
    movl    CPU_ESI(%r11), %esi
    movl    CPU_EBX(%r11), %ebx
    movl    CPU_EBP(%r11), %ebp
    movl    CPU_ECX(%r11), %ecx
    movl    CPU_EDX(%r11), %edx
    movl    CPU_EAX(%r11), %eax
    iretq

/* saves 32-bit registers in the CPU area at r11. */
.macro SAVE32
    movl    %edi, CPU_EDI(%r11)
    movl    %esi, CPU_ESI(%r11)
    movl    %ebx, CPU_EBX(%r11)
    movl    %ebp, CPU_EBP(%r11)
    movl    %eax, CPU_EAX(%r11)
    movl    %ecx, CPU_ECX(%r11)
    movl    %edx, CPU_EDX(%r11)
    pushfq
    popq    %r10
    movl    %r10d, CPU_EFLAGS(%r11)
.endm

/* a system call: the stub's return address is at esp, its caller's arguments past the caller's. */
syscall_landing:
    movl    %esp, %esp
    addq    $8, %rsp
    movq    %gs:TEB_CPU, %r11
    SAVE32
    movl    (%rsp), %r10d
    movl    %r10d, CPU_EIP(%r11)
    leaq    4(%rsp), %r10
    movl    %r10d, CPU_ESP(%r11)
    movq    %gs:TEB_STACKPTR64, %rsp
    xorl    %eax, %eax
    jmp     3f

/* a call into 32-bit code returned, its result in eax. */
return_landing:
    movl    %esp, %esp
    addq    $8, %rsp
    movq    %gs:TEB_CPU, %r11
    SAVE32
    movl    %esp, CPU_ESP(%r11)
    movq    %gs:TEB_STACKPTR64, %rsp
    movl    $1, %eax

3:  movdqu  0x00(%rsp), %xmm6
    movdqu  0x10(%rsp), %xmm7
    movdqu  0x20(%rsp), %xmm8
    movdqu  0x30(%rsp), %xmm9
    movdqu  0x40(%rsp), %xmm10
    movdqu  0x50(%rsp), %xmm11
    movdqu  0x60(%rsp), %xmm12
    movdqu  0x70(%rsp), %xmm13
    movdqu  0x80(%rsp), %xmm14
    movdqu  0x90(%rsp), %xmm15
    addq    $FRAME, %rsp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rsi
    popq    %rdi
    popq    %rbx
    popq    %rbp
    ret

/*
 * 32-bit gates far-jump to the native code segment, computing their targets without relocations and preserving eax.
 */
    .code32
    .globl  Wow64CpuTransition
    .p2align 4
Wow64CpuTransition:
    pushl   $CODE64
    pushl   %eax
    calll   4f
4:  popl    %eax
    addl    $(syscall_landing - 4b), %eax
    xchgl   %eax, (%esp)
    ljmpl   *(%esp)

    .globl  Wow64CpuReturn32
    .p2align 4
Wow64CpuReturn32:
    pushl   $CODE64
    pushl   %eax
    calll   5f
5:  popl    %eax
    addl    $(return_landing - 5b), %eax
    xchgl   %eax, (%esp)
    ljmpl   *(%esp)
    .code64
