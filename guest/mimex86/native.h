/* x64 system dll exports run natively, see native.c. */
#ifndef MIMEX86_NATIVE_H
#define MIMEX86_NATIVE_H

/* hooks the exports of an x64 image, plus the heap and synchronization ones for ntdll. */
void MxInstallNatives(PVOID base, BOOLEAN ntdll);
/* called after the x64 loader maps an image view, and before it unmaps one. */
void MxImageMapped(PVOID base);
void MxImageUnmapping(PVOID base);
/* raises status in the x64 code whose export is running natively. */
DECLSPEC_NORETURN void NTAPI MxRaiseStatus(NTSTATUS status);

#endif
