/*
 * the services of win32k that windows does not have, for the libraries and programs of mime that call them: the
 * win32u.dll a process loads is microsoft's and has no such export, so each caller links the stub itself. one section
 * for each stub, so that a caller keeps only the ones it calls.
 */
#include "mime_svc_win32k.h"

.macro service name, number
    .section .text$\name, "xr", one_only, \name
    .globl \name
    .p2align 4
\name:
    movq %rcx, %r10
    movl $\number, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    /* callers ask for the import, and find it here before the library of win32u is searched. */
    .section .rdata$\name, "dr", one_only, __imp_\name
    .globl __imp_\name
    .p2align 3
__imp_\name:
    .quad \name
.endm

/* win32u 26300 still exports these, as a fast fail with no system call: mime's own multiplexed requests. */
service NtUserCallHwndParam, MIME_SVC_NtUserCallHwndParam
service NtUserCallOneParam, MIME_SVC_NtUserCallOneParam
service NtUserCallTwoParam, MIME_SVC_NtUserCallTwoParam

service NtGdiMimeAudioControl, MIME_SVC_NtGdiMimeAudioControl
service NtGdiMimeAudioCreate, MIME_SVC_NtGdiMimeAudioCreate
service NtGdiMimeGamma, MIME_SVC_NtGdiMimeGamma
service NtGdiMimeGfxShare, MIME_SVC_NtGdiMimeGfxShare
service NtGdiMimeGpuAddBlock, MIME_SVC_NtGdiMimeGpuAddBlock
service NtGdiMimeGpuGetSurface, MIME_SVC_NtGdiMimeGpuGetSurface
service NtGdiMimeGpuKick, MIME_SVC_NtGdiMimeGpuKick
service NtGdiMimeGpuOpen, MIME_SVC_NtGdiMimeGpuOpen
service NtGdiMimeGpuWait, MIME_SVC_NtGdiMimeGpuWait
service NtGdiMimeSetExtent, MIME_SVC_NtGdiMimeSetExtent
service NtGdiMimeVkAlloc, MIME_SVC_NtGdiMimeVkAlloc
service NtGdiMimeVkFree, MIME_SVC_NtGdiMimeVkFree
service NtGdiMimeVkKick, MIME_SVC_NtGdiMimeVkKick
service NtGdiMimeVkOpen, MIME_SVC_NtGdiMimeVkOpen
service NtGdiMimeVkWait, MIME_SVC_NtGdiMimeVkWait
service NtUserMimeCallMenu, MIME_SVC_NtUserMimeCallMenu
service NtUserMimeCapture, MIME_SVC_NtUserMimeCapture
service NtUserMimeDnd, MIME_SVC_NtUserMimeDnd
service NtUserMimeDndData, MIME_SVC_NtUserMimeDndData
service NtUserMimeDndProvide, MIME_SVC_NtUserMimeDndProvide
service NtUserMimeExitWindows, MIME_SVC_NtUserMimeExitWindows
service NtUserMimeGetMenuItemInfo, MIME_SVC_NtUserMimeGetMenuItemInfo
service NtUserMimeIme, MIME_SVC_NtUserMimeIme
service NtUserMimeShell, MIME_SVC_NtUserMimeShell
service NtUserMimeTray, MIME_SVC_NtUserMimeTray
