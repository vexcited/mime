/*
 * the services of win32k that windows does not have, for the 32-bit libraries and programs of mime that call them:
 * the win32u.dll a 32-bit process loads is microsoft's and has no such export, and its wow64win.dll has no thunk for
 * them, so each caller links the stub itself, as guest/win32u/mime_x64.S does for 64-bit callers.
 *
 * on an x64 machine the stub calls the 64-bit service itself: it enters the 64-bit code segment with a far call,
 * makes the system call with the arguments widened, and comes back with a far return. none of these services takes a
 * structure whose layout depends on the caller's pointers, and none calls back into user mode. on arm64, where xta64
 * runs 32-bit code and serves every service, the stub calls Wow64Transition as the stubs of the 32-bit ntdll do.
 * written by the generator in scratch/dlls/wow/genmime86.py of the research repository.
 */
#include "mime_svc_win32k.h"

#define NATIVE_ARCHITECTURE 0x7FFE026A /* KUSER_SHARED_DATA.NativeProcessorArchitecture */
#define SYSTEM_CALL 0x7FFE0308         /* KUSER_SHARED_DATA.SystemCall */


    .section .text$NtGdiMimeAudioControl, "xr", one_only, "_NtGdiMimeAudioControl@8"
    .code32
    .globl "_NtGdiMimeAudioControl@8"
    .p2align 4
"_NtGdiMimeAudioControl@8":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $8
3:  movl $MIME_SVC_NtGdiMimeAudioControl, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $8
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl $MIME_SVC_NtGdiMimeAudioControl, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeAudioControl, "dr", one_only, "__imp__NtGdiMimeAudioControl@8"
    .globl "__imp__NtGdiMimeAudioControl@8"
    .p2align 2
"__imp__NtGdiMimeAudioControl@8":
    .long "_NtGdiMimeAudioControl@8"

    .section .text$NtGdiMimeAudioCreate, "xr", one_only, "_NtGdiMimeAudioCreate@8"
    .code32
    .globl "_NtGdiMimeAudioCreate@8"
    .p2align 4
"_NtGdiMimeAudioCreate@8":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $8
3:  movl $MIME_SVC_NtGdiMimeAudioCreate, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $8
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl $MIME_SVC_NtGdiMimeAudioCreate, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeAudioCreate, "dr", one_only, "__imp__NtGdiMimeAudioCreate@8"
    .globl "__imp__NtGdiMimeAudioCreate@8"
    .p2align 2
"__imp__NtGdiMimeAudioCreate@8":
    .long "_NtGdiMimeAudioCreate@8"

    .section .text$NtGdiMimeGamma, "xr", one_only, "_NtGdiMimeGamma@8"
    .code32
    .globl "_NtGdiMimeGamma@8"
    .p2align 4
"_NtGdiMimeGamma@8":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $8
3:  movl $MIME_SVC_NtGdiMimeGamma, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $8
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl $MIME_SVC_NtGdiMimeGamma, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeGamma, "dr", one_only, "__imp__NtGdiMimeGamma@8"
    .globl "__imp__NtGdiMimeGamma@8"
    .p2align 2
"__imp__NtGdiMimeGamma@8":
    .long "_NtGdiMimeGamma@8"

    .section .text$NtGdiMimeGfxShare, "xr", one_only, "_NtGdiMimeGfxShare@8"
    .code32
    .globl "_NtGdiMimeGfxShare@8"
    .p2align 4
"_NtGdiMimeGfxShare@8":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $8
3:  movl $MIME_SVC_NtGdiMimeGfxShare, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $8
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl $MIME_SVC_NtGdiMimeGfxShare, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeGfxShare, "dr", one_only, "__imp__NtGdiMimeGfxShare@8"
    .globl "__imp__NtGdiMimeGfxShare@8"
    .p2align 2
"__imp__NtGdiMimeGfxShare@8":
    .long "_NtGdiMimeGfxShare@8"

    .section .text$NtGdiMimeGpuAddBlock, "xr", one_only, "_NtGdiMimeGpuAddBlock@12"
    .code32
    .globl "_NtGdiMimeGpuAddBlock@12"
    .p2align 4
"_NtGdiMimeGpuAddBlock@12":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $12
3:  movl $MIME_SVC_NtGdiMimeGpuAddBlock, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $12
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl $MIME_SVC_NtGdiMimeGpuAddBlock, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeGpuAddBlock, "dr", one_only, "__imp__NtGdiMimeGpuAddBlock@12"
    .globl "__imp__NtGdiMimeGpuAddBlock@12"
    .p2align 2
"__imp__NtGdiMimeGpuAddBlock@12":
    .long "_NtGdiMimeGpuAddBlock@12"

    .section .text$NtGdiMimeGpuGetSurface, "xr", one_only, "_NtGdiMimeGpuGetSurface@8"
    .code32
    .globl "_NtGdiMimeGpuGetSurface@8"
    .p2align 4
"_NtGdiMimeGpuGetSurface@8":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $8
3:  movl $MIME_SVC_NtGdiMimeGpuGetSurface, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $8
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl $MIME_SVC_NtGdiMimeGpuGetSurface, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeGpuGetSurface, "dr", one_only, "__imp__NtGdiMimeGpuGetSurface@8"
    .globl "__imp__NtGdiMimeGpuGetSurface@8"
    .p2align 2
"__imp__NtGdiMimeGpuGetSurface@8":
    .long "_NtGdiMimeGpuGetSurface@8"

    .section .text$NtGdiMimeGpuKick, "xr", one_only, "_NtGdiMimeGpuKick@0"
    .code32
    .globl "_NtGdiMimeGpuKick@0"
    .p2align 4
"_NtGdiMimeGpuKick@0":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $0
3:  movl $MIME_SVC_NtGdiMimeGpuKick, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $0
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl $MIME_SVC_NtGdiMimeGpuKick, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeGpuKick, "dr", one_only, "__imp__NtGdiMimeGpuKick@0"
    .globl "__imp__NtGdiMimeGpuKick@0"
    .p2align 2
"__imp__NtGdiMimeGpuKick@0":
    .long "_NtGdiMimeGpuKick@0"

    .section .text$NtGdiMimeGpuOpen, "xr", one_only, "_NtGdiMimeGpuOpen@8"
    .code32
    .globl "_NtGdiMimeGpuOpen@8"
    .p2align 4
"_NtGdiMimeGpuOpen@8":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $8
3:  movl $MIME_SVC_NtGdiMimeGpuOpen, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $8
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl $MIME_SVC_NtGdiMimeGpuOpen, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeGpuOpen, "dr", one_only, "__imp__NtGdiMimeGpuOpen@8"
    .globl "__imp__NtGdiMimeGpuOpen@8"
    .p2align 2
"__imp__NtGdiMimeGpuOpen@8":
    .long "_NtGdiMimeGpuOpen@8"

    .section .text$NtGdiMimeGpuWait, "xr", one_only, "_NtGdiMimeGpuWait@12"
    .code32
    .globl "_NtGdiMimeGpuWait@12"
    .p2align 4
"_NtGdiMimeGpuWait@12":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $12
3:  movl $MIME_SVC_NtGdiMimeGpuWait, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $12
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl $MIME_SVC_NtGdiMimeGpuWait, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeGpuWait, "dr", one_only, "__imp__NtGdiMimeGpuWait@12"
    .globl "__imp__NtGdiMimeGpuWait@12"
    .p2align 2
"__imp__NtGdiMimeGpuWait@12":
    .long "_NtGdiMimeGpuWait@12"

    .section .text$NtGdiMimeSetExtent, "xr", one_only, "_NtGdiMimeSetExtent@20"
    .code32
    .globl "_NtGdiMimeSetExtent@20"
    .p2align 4
"_NtGdiMimeSetExtent@20":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x28, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $20
3:  movl $MIME_SVC_NtGdiMimeSetExtent, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $20
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movslq 0x10(%rbp), %rax
    movq %rax, %r8
    movslq 0x14(%rbp), %rax
    movq %rax, %r9
    movl 0x18(%rbp), %eax
    movq %rax, 0x28(%rsp)
    movl $MIME_SVC_NtGdiMimeSetExtent, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeSetExtent, "dr", one_only, "__imp__NtGdiMimeSetExtent@20"
    .globl "__imp__NtGdiMimeSetExtent@20"
    .p2align 2
"__imp__NtGdiMimeSetExtent@20":
    .long "_NtGdiMimeSetExtent@20"

    .section .text$NtGdiMimeVkAlloc, "xr", one_only, "_NtGdiMimeVkAlloc@12"
    .code32
    .globl "_NtGdiMimeVkAlloc@12"
    .p2align 4
"_NtGdiMimeVkAlloc@12":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $12
3:  movl $MIME_SVC_NtGdiMimeVkAlloc, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $12
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl $MIME_SVC_NtGdiMimeVkAlloc, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeVkAlloc, "dr", one_only, "__imp__NtGdiMimeVkAlloc@12"
    .globl "__imp__NtGdiMimeVkAlloc@12"
    .p2align 2
"__imp__NtGdiMimeVkAlloc@12":
    .long "_NtGdiMimeVkAlloc@12"

    .section .text$NtGdiMimeVkFree, "xr", one_only, "_NtGdiMimeVkFree@4"
    .code32
    .globl "_NtGdiMimeVkFree@4"
    .p2align 4
"_NtGdiMimeVkFree@4":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $4
3:  movl $MIME_SVC_NtGdiMimeVkFree, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $4
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl $MIME_SVC_NtGdiMimeVkFree, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeVkFree, "dr", one_only, "__imp__NtGdiMimeVkFree@4"
    .globl "__imp__NtGdiMimeVkFree@4"
    .p2align 2
"__imp__NtGdiMimeVkFree@4":
    .long "_NtGdiMimeVkFree@4"

    .section .text$NtGdiMimeVkKick, "xr", one_only, "_NtGdiMimeVkKick@0"
    .code32
    .globl "_NtGdiMimeVkKick@0"
    .p2align 4
"_NtGdiMimeVkKick@0":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $0
3:  movl $MIME_SVC_NtGdiMimeVkKick, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $0
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl $MIME_SVC_NtGdiMimeVkKick, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeVkKick, "dr", one_only, "__imp__NtGdiMimeVkKick@0"
    .globl "__imp__NtGdiMimeVkKick@0"
    .p2align 2
"__imp__NtGdiMimeVkKick@0":
    .long "_NtGdiMimeVkKick@0"

    .section .text$NtGdiMimeVkOpen, "xr", one_only, "_NtGdiMimeVkOpen@4"
    .code32
    .globl "_NtGdiMimeVkOpen@4"
    .p2align 4
"_NtGdiMimeVkOpen@4":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $4
3:  movl $MIME_SVC_NtGdiMimeVkOpen, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $4
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl $MIME_SVC_NtGdiMimeVkOpen, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeVkOpen, "dr", one_only, "__imp__NtGdiMimeVkOpen@4"
    .globl "__imp__NtGdiMimeVkOpen@4"
    .p2align 2
"__imp__NtGdiMimeVkOpen@4":
    .long "_NtGdiMimeVkOpen@4"

    .section .text$NtGdiMimeVkWait, "xr", one_only, "_NtGdiMimeVkWait@12"
    .code32
    .globl "_NtGdiMimeVkWait@12"
    .p2align 4
"_NtGdiMimeVkWait@12":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $12
3:  movl $MIME_SVC_NtGdiMimeVkWait, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $12
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl $MIME_SVC_NtGdiMimeVkWait, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtGdiMimeVkWait, "dr", one_only, "__imp__NtGdiMimeVkWait@12"
    .globl "__imp__NtGdiMimeVkWait@12"
    .p2align 2
"__imp__NtGdiMimeVkWait@12":
    .long "_NtGdiMimeVkWait@12"

    .section .text$NtUserMimeCallMenu, "xr", one_only, "_NtUserMimeCallMenu@12"
    .code32
    .globl "_NtUserMimeCallMenu@12"
    .p2align 4
"_NtUserMimeCallMenu@12":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $12
3:  movl $MIME_SVC_NtUserMimeCallMenu, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $12
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl $MIME_SVC_NtUserMimeCallMenu, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeCallMenu, "dr", one_only, "__imp__NtUserMimeCallMenu@12"
    .globl "__imp__NtUserMimeCallMenu@12"
    .p2align 2
"__imp__NtUserMimeCallMenu@12":
    .long "_NtUserMimeCallMenu@12"

    .section .text$NtUserMimeCapture, "xr", one_only, "_NtUserMimeCapture@20"
    .code32
    .globl "_NtUserMimeCapture@20"
    .p2align 4
"_NtUserMimeCapture@20":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x28, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $20
3:  movl $MIME_SVC_NtUserMimeCapture, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $20
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl 0x14(%rbp), %eax
    movq %rax, %r9
    movl 0x18(%rbp), %eax
    movq %rax, 0x28(%rsp)
    movl $MIME_SVC_NtUserMimeCapture, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeCapture, "dr", one_only, "__imp__NtUserMimeCapture@20"
    .globl "__imp__NtUserMimeCapture@20"
    .p2align 2
"__imp__NtUserMimeCapture@20":
    .long "_NtUserMimeCapture@20"

    .section .text$NtUserMimeDnd, "xr", one_only, "_NtUserMimeDnd@16"
    .code32
    .globl "_NtUserMimeDnd@16"
    .p2align 4
"_NtUserMimeDnd@16":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $16
3:  movl $MIME_SVC_NtUserMimeDnd, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $16
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl 0x14(%rbp), %eax
    movq %rax, %r9
    movl $MIME_SVC_NtUserMimeDnd, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeDnd, "dr", one_only, "__imp__NtUserMimeDnd@16"
    .globl "__imp__NtUserMimeDnd@16"
    .p2align 2
"__imp__NtUserMimeDnd@16":
    .long "_NtUserMimeDnd@16"

    .section .text$NtUserMimeDndData, "xr", one_only, "_NtUserMimeDndData@24"
    .code32
    .globl "_NtUserMimeDndData@24"
    .p2align 4
"_NtUserMimeDndData@24":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x30, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $24
3:  movl $MIME_SVC_NtUserMimeDndData, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $24
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl 0x14(%rbp), %eax
    movq %rax, %r9
    movl 0x18(%rbp), %eax
    movq %rax, 0x28(%rsp)
    movl 0x1c(%rbp), %eax
    movq %rax, 0x30(%rsp)
    movl $MIME_SVC_NtUserMimeDndData, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeDndData, "dr", one_only, "__imp__NtUserMimeDndData@24"
    .globl "__imp__NtUserMimeDndData@24"
    .p2align 2
"__imp__NtUserMimeDndData@24":
    .long "_NtUserMimeDndData@24"

    .section .text$NtUserMimeDndProvide, "xr", one_only, "_NtUserMimeDndProvide@20"
    .code32
    .globl "_NtUserMimeDndProvide@20"
    .p2align 4
"_NtUserMimeDndProvide@20":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x28, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $20
3:  movl $MIME_SVC_NtUserMimeDndProvide, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $20
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl 0x14(%rbp), %eax
    movq %rax, %r9
    movl 0x18(%rbp), %eax
    movq %rax, 0x28(%rsp)
    movl $MIME_SVC_NtUserMimeDndProvide, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeDndProvide, "dr", one_only, "__imp__NtUserMimeDndProvide@20"
    .globl "__imp__NtUserMimeDndProvide@20"
    .p2align 2
"__imp__NtUserMimeDndProvide@20":
    .long "_NtUserMimeDndProvide@20"

    .section .text$NtUserMimeExitWindows, "xr", one_only, "_NtUserMimeExitWindows@8"
    .code32
    .globl "_NtUserMimeExitWindows@8"
    .p2align 4
"_NtUserMimeExitWindows@8":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $8
3:  movl $MIME_SVC_NtUserMimeExitWindows, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $8
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl $MIME_SVC_NtUserMimeExitWindows, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeExitWindows, "dr", one_only, "__imp__NtUserMimeExitWindows@8"
    .globl "__imp__NtUserMimeExitWindows@8"
    .p2align 2
"__imp__NtUserMimeExitWindows@8":
    .long "_NtUserMimeExitWindows@8"

    .section .text$NtUserMimeGetMenuItemInfo, "xr", one_only, "_NtUserMimeGetMenuItemInfo@16"
    .code32
    .globl "_NtUserMimeGetMenuItemInfo@16"
    .p2align 4
"_NtUserMimeGetMenuItemInfo@16":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $16
3:  movl $MIME_SVC_NtUserMimeGetMenuItemInfo, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $16
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl 0x14(%rbp), %eax
    movq %rax, %r9
    movl $MIME_SVC_NtUserMimeGetMenuItemInfo, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeGetMenuItemInfo, "dr", one_only, "__imp__NtUserMimeGetMenuItemInfo@16"
    .globl "__imp__NtUserMimeGetMenuItemInfo@16"
    .p2align 2
"__imp__NtUserMimeGetMenuItemInfo@16":
    .long "_NtUserMimeGetMenuItemInfo@16"

    .section .text$NtUserMimeIme, "xr", one_only, "_NtUserMimeIme@20"
    .code32
    .globl "_NtUserMimeIme@20"
    .p2align 4
"_NtUserMimeIme@20":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x28, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $20
3:  movl $MIME_SVC_NtUserMimeIme, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $20
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl 0x14(%rbp), %eax
    movq %rax, %r9
    movl 0x18(%rbp), %eax
    movq %rax, 0x28(%rsp)
    movl $MIME_SVC_NtUserMimeIme, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeIme, "dr", one_only, "__imp__NtUserMimeIme@20"
    .globl "__imp__NtUserMimeIme@20"
    .p2align 2
"__imp__NtUserMimeIme@20":
    .long "_NtUserMimeIme@20"

    .section .text$NtUserMimeShell, "xr", one_only, "_NtUserMimeShell@16"
    .code32
    .globl "_NtUserMimeShell@16"
    .p2align 4
"_NtUserMimeShell@16":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $16
3:  movl $MIME_SVC_NtUserMimeShell, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $16
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl 0x14(%rbp), %eax
    movq %rax, %r9
    movl $MIME_SVC_NtUserMimeShell, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeShell, "dr", one_only, "__imp__NtUserMimeShell@16"
    .globl "__imp__NtUserMimeShell@16"
    .p2align 2
"__imp__NtUserMimeShell@16":
    .long "_NtUserMimeShell@16"

    .section .text$NtUserMimeTray, "xr", one_only, "_NtUserMimeTray@12"
    .code32
    .globl "_NtUserMimeTray@12"
    .p2align 4
"_NtUserMimeTray@12":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $12
3:  movl $MIME_SVC_NtUserMimeTray, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $12
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl $MIME_SVC_NtUserMimeTray, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserMimeTray, "dr", one_only, "__imp__NtUserMimeTray@12"
    .globl "__imp__NtUserMimeTray@12"
    .p2align 2
"__imp__NtUserMimeTray@12":
    .long "_NtUserMimeTray@12"

    .section .text$NtUserCallHwndParam, "xr", one_only, "_NtUserCallHwndParam@12"
    .code32
    .globl "_NtUserCallHwndParam@12"
    .p2align 4
"_NtUserCallHwndParam@12":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $12
3:  movl $MIME_SVC_NtUserCallHwndParam, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $12
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl $MIME_SVC_NtUserCallHwndParam, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserCallHwndParam, "dr", one_only, "__imp__NtUserCallHwndParam@12"
    .globl "__imp__NtUserCallHwndParam@12"
    .p2align 2
"__imp__NtUserCallHwndParam@12":
    .long "_NtUserCallHwndParam@12"

    .section .text$NtUserCallOneParam, "xr", one_only, "_NtUserCallOneParam@8"
    .code32
    .globl "_NtUserCallOneParam@8"
    .p2align 4
"_NtUserCallOneParam@8":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $8
3:  movl $MIME_SVC_NtUserCallOneParam, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $8
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl $MIME_SVC_NtUserCallOneParam, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserCallOneParam, "dr", one_only, "__imp__NtUserCallOneParam@8"
    .globl "__imp__NtUserCallOneParam@8"
    .p2align 2
"__imp__NtUserCallOneParam@8":
    .long "_NtUserCallOneParam@8"

    .section .text$NtUserCallTwoParam, "xr", one_only, "_NtUserCallTwoParam@12"
    .code32
    .globl "_NtUserCallTwoParam@12"
    .p2align 4
"_NtUserCallTwoParam@12":
    cmpw $9, NATIVE_ARCHITECTURE
    jne 3f
    pushl %ebp
    movl %esp, %ebp
    subl $0x20, %esp
    andl $-16, %esp
    lcall $0x33, $1f
    movl %ebp, %esp
    popl %ebp
    ret $12
3:  movl $MIME_SVC_NtUserCallTwoParam, %eax
    movl __imp__Wow64Transition, %edx
    calll *(%edx)
    ret $12
    .code64
    /* the far call left its return where a near call leaves its own, the arguments are past the homes. */
1:  movl %esp, %esp
    movl %ebp, %ebp
    movl 0x8(%rbp), %eax
    movq %rax, %r10
    movl 0xc(%rbp), %eax
    movq %rax, %rdx
    movl 0x10(%rbp), %eax
    movq %rax, %r8
    movl $MIME_SVC_NtUserCallTwoParam, %eax
    testb $1, SYSTEM_CALL
    jne 2f
    syscall
    lretl
2:  int $0x2E
    lretl
    .code32
    .section .rdata$NtUserCallTwoParam, "dr", one_only, "__imp__NtUserCallTwoParam@12"
    .globl "__imp__NtUserCallTwoParam@12"
    .p2align 2
"__imp__NtUserCallTwoParam@12":
    .long "_NtUserCallTwoParam@12"
