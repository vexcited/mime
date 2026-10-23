/*
 * the system call gate on x64, like windows' 32-bit ntdll:
 * Wow64SystemServiceCall jumps through Wow64Transition, which wow64.dll
 * points at wow64cpu.dll's switch back to 64-bit code.
 */
    .data
    .globl  _Wow64Transition
    .p2align 2
_Wow64Transition:
    .long   0

    .text
    .globl  _Wow64SystemServiceCall
    .p2align 4
_Wow64SystemServiceCall:
    jmpl    *_Wow64Transition
