/* 32-bit win32u system calls use ntdll's Wow64Transition on x64. */
    .text
    .globl  _Wow64SystemServiceCall
    .p2align 4
_Wow64SystemServiceCall:
    movl    __imp__Wow64Transition, %edx
    jmpl    *(%edx)
