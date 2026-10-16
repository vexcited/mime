/*
 * calls a frame based handler of the x86 exception list with a
 * registration of its own pushed first, so an exception raised in the
 * handler is seen as nested, and an unwind running into another as
 * collided. the registration keeps the frame being dispatched after the
 * handler's two fields, for the nested handlers to report.
 *
 * EXCEPTION_DISPOSITION RtlpExecuteHandlerForException(record, frame, context, dispatcher, handler)
 * EXCEPTION_DISPOSITION RtlpExecuteHandlerForUnwind(record, frame, context, dispatcher, handler)
 */
    .text
    .globl _RtlpExecuteHandlerForException
_RtlpExecuteHandlerForException:
    movl $_RtlpNestedExceptionHandler, %edx
    jmp execute_handler

    .globl _RtlpExecuteHandlerForUnwind
_RtlpExecuteHandlerForUnwind:
    movl $_RtlpCollidedUnwindHandler, %edx

execute_handler:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl 12(%ebp)
    pushl %edx
    pushl %fs:0
    movl %esp, %fs:0
    pushl 20(%ebp)
    pushl 16(%ebp)
    pushl 12(%ebp)
    pushl 8(%ebp)
    movl 24(%ebp), %ecx
    calll *%ecx
    /* handlers may leave the stack anywhere: the registration is found from the frame. */
    movl -24(%ebp), %ecx
    movl %ecx, %fs:0
    leal -12(%ebp), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    retl
