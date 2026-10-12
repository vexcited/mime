/* the native call escape loading opengl32's native image, see mime/mx64.h. */
	.text
	.globl	MxNativeLoad
	.p2align	4, 0x90
MxNativeLoad:
	movq	%rcx, %r10
	movl	$0x7EFF0000, %eax
	syscall
	retq
