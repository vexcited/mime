/*
 * the gate of win32u's system call stubs on x64, as windows' 32-bit
 * win32u has it: through the 32-bit ntdll's Wow64Transition.
 */
    .text
    .globl  _Wow64SystemServiceCall
    .p2align 4
_Wow64SystemServiceCall:
    movl    __imp__Wow64Transition, %edx
    jmpl    *(%edx)
