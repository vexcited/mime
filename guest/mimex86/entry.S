/*
 * native helpers of the translator: a private setjmp pair, and one system
 * service stub per service number so a call can pick the number at run time.
 */
#include "mime_svc_nt.h"
#include "mime_svc_win32k.h"

    .text

/* int mx_setjmp(MX_JUMP *jump) */
    .globl  mx_setjmp
    .p2align 4
mx_setjmp:
    stp     x19, x20, [x0, #0x00]
    stp     x21, x22, [x0, #0x10]
    stp     x23, x24, [x0, #0x20]
    stp     x25, x26, [x0, #0x30]
    stp     x27, x28, [x0, #0x40]
    stp     x29, x30, [x0, #0x50]
    mov     x1, sp
    str     x1, [x0, #0x60]
    stp     d8, d9, [x0, #0x68]
    stp     d10, d11, [x0, #0x78]
    stp     d12, d13, [x0, #0x88]
    stp     d14, d15, [x0, #0x98]
    mov     w0, #0
    ret

/* void mx_longjmp(MX_JUMP *jump, int value) */
    .globl  mx_longjmp
    .p2align 4
mx_longjmp:
    ldp     x19, x20, [x0, #0x00]
    ldp     x21, x22, [x0, #0x10]
    ldp     x23, x24, [x0, #0x20]
    ldp     x25, x26, [x0, #0x30]
    ldp     x27, x28, [x0, #0x40]
    ldp     x29, x30, [x0, #0x50]
    ldr     x2, [x0, #0x60]
    mov     sp, x2
    ldp     d8, d9, [x0, #0x68]
    ldp     d10, d11, [x0, #0x78]
    ldp     d12, d13, [x0, #0x88]
    ldp     d14, d15, [x0, #0x98]
    cmp     w1, #0
    csinc   w0, w1, wzr, ne
    ret

/* stub n of each table is at table + 8 * n. */
    .p2align 3
    .globl  mx_svc_nt
mx_svc_nt:
    .set    n, MIME_NT_SERVICES_BASE
    .rept   MIME_NT_SERVICES_COUNT
    svc     #n
    ret
    .set    n, n + 1
    .endr

    .p2align 3
    .globl  mx_svc_win32k
mx_svc_win32k:
    .set    n, MIME_WIN32K_SERVICES_BASE
    .rept   MIME_WIN32K_SERVICES_COUNT
    svc     #n
    ret
    .set    n, n + 1
    .endr
