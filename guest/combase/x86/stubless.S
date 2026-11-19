/*
 * ObjectStublessClient3 to 32 of the 32-bit combase: each puts its method number in eax. the common code asks
 * CbStublessPrepare of guest/combase/stubless.c for the procedure, copies the arguments below its frame for
 * NdrClientCall2, whose variable arguments they are, and pops them off the caller's stack as a stdcall method does.
 */
    .altmacro
    .text

    .macro  stubless n
    .globl  _ObjectStublessClient\n
    .p2align 4
_ObjectStublessClient\n:
    movl    $\n, %eax
    jmp     stubless_common
    .endm

    .macro  stublesses first, last
    stubless %first
    .if \first < \last
    stublesses %(\first + 1), \last
    .endif
    .endm

    stublesses 3, 17
    stublesses 18, 32

/* the frame: the saved ebp, then esi, edi and the call: descriptor, format, argument bytes, NdrClientCall2. */
    .p2align 4
stubless_common:
    pushl   %ebp
    movl    %esp, %ebp
    pushl   %esi
    pushl   %edi
    subl    $16, %esp
    movl    %esp, %ecx
    pushl   %ecx
    leal    8(%ebp), %ecx
    pushl   %ecx
    pushl   %eax
    call    _CbStublessPrepare
    addl    $12, %esp
    movl    8(%esp), %ecx
    subl    %ecx, %esp
    movl    %esp, %edi
    leal    8(%ebp), %esi
    cld
    rep movsb
    pushl   -20(%ebp)
    pushl   -24(%ebp)
    call    *-12(%ebp)
    movl    -16(%ebp), %ecx
    leal    -8(%ebp), %esp
    popl    %edi
    popl    %esi
    popl    %ebp
    popl    %edx
    addl    %ecx, %esp
    jmpl    *%edx

/*
 * NdrProxyForwardingFunction3 to 32: a method a delegating proxy leaves to the proxy of its base interface. the
 * interface pointer of a CStdProxyBuffer2 is its second field, so its pBaseProxy is 0x10 past it.
 */
    .macro  forwarding n
    .globl  _NdrProxyForwardingFunction\n
    .p2align 4
_NdrProxyForwardingFunction\n:
    movl    4(%esp), %eax
    movl    0x10(%eax), %eax
    movl    %eax, 4(%esp)
    movl    (%eax), %eax
    jmp     *(\n * 4)(%eax)
    .endm

    .macro  forwardings first, last
    forwarding %first
    .if \first < \last
    forwardings %(\first + 1), \last
    .endif
    .endm

    forwardings 3, 17
    forwardings 18, 32
