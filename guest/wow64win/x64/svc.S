/*
 * one system call stub per service number, 16 bytes apart, so a call can
 * reach any number chosen at run time.
 */
#include "mime_svc_nt.h"
#include "mime_svc_win32k.h"

    .text

    .p2align 4
    .globl  Wow64WinServicesNt
Wow64WinServicesNt:
    .set    n, MIME_NT_SERVICES_BASE
    .rept   MIME_NT_SERVICES_COUNT
    movq    %rcx, %r10
    movl    $n, %eax
    syscall
    ret
    .p2align 4
    .set    n, n + 1
    .endr

    .p2align 4
    .globl  Wow64WinServicesWin32k
Wow64WinServicesWin32k:
    .set    n, MIME_WIN32K_SERVICES_BASE
    .rept   MIME_WIN32K_SERVICES_COUNT
    movq    %rcx, %r10
    movl    $n, %eax
    syscall
    ret
    .p2align 4
    .set    n, n + 1
    .endr
