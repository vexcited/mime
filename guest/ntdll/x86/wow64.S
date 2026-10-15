/*
 * the gate of the system call stubs on x64, as windows' 32-bit ntdll has
 * it: Wow64SystemServiceCall jumps through Wow64Transition, which
 * wow64.dll points at wow64cpu.dll's way back to 64-bit code.
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
