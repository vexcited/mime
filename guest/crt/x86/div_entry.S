    .text
    .globl __aulldiv
__aulldiv:
    jmp _MimeDivideUnsigned64@16
    .globl __alldiv
__alldiv:
    jmp _MimeDivideSigned64@16
    .globl __allrem
__allrem:
    jmp _MimeRemainderSigned64@16
    .globl __aullrem
__aullrem:
    jmp _MimeRemainderUnsigned64@16
