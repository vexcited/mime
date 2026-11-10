/* helpers shared by the 32-bit system call conversions. */
#ifndef MIME_WOW32_CONVERT_H
#define MIME_WOW32_CONVERT_H

#include <mime/wow32.h>

static inline PVOID pointer32(ULONG value)
{
    return (PVOID)(ULONG_PTR)value;
}

static inline HANDLE handle32(ULONG value)
{
    return (HANDLE)(LONG_PTR)(LONG)value;
}

/* sizes and counts above 32 bits saturate, as windows' wow64 narrows them. */
static inline ULONG saturate32(ULONG64 value)
{
    return value > 0xFFFFFFFFull ? 0xFFFFFFFFu : (ULONG)value;
}

static inline void store32(ULONG address, ULONG_PTR value)
{
    if (address)
        *(PULONG)pointer32(address) = (ULONG)value;
}

static inline void store_iosb32(ULONG address, const IO_STATUS_BLOCK *io)
{
    if (address) {
        MIME_IO_STATUS32 *out = pointer32(address);
        out->Status = io->Status;
        out->Information = (ULONG)io->Information;
    }
}

static inline UNICODE_STRING string32(ULONG address)
{
    const MIME_STRING32 *s = pointer32(address);
    return (UNICODE_STRING){s->Length, s->MaximumLength, pointer32(s->Buffer)};
}

static inline OBJECT_ATTRIBUTES attributes32(ULONG address, UNICODE_STRING *name)
{
    const MIME_OBJECT_ATTRIBUTES32 *a = pointer32(address);
    OBJECT_ATTRIBUTES out = {sizeof(out)};
    if (!a)
        return out;
    out.RootDirectory = handle32(a->RootDirectory);
    out.Attributes = a->Attributes;
    if (a->ObjectName) {
        *name = string32(a->ObjectName);
        out.ObjectName = name;
    }
    out.SecurityDescriptor = pointer32(a->SecurityDescriptor);
    out.SecurityQualityOfService = pointer32(a->SecurityQualityOfService);
    return out;
}

/* native scratch memory for results before they are narrowed, anywhere in the address space. */
PVOID Mx32Scratch(SIZE_T size);
void Mx32FreeScratch(PVOID base);
/* where the 32-bit addresses of this process end: 4 GiB less the last 64 KiB for a large address aware program, 2 GiB otherwise. */
ULONG64 Mx32AddressLimit(void);

PVOID MxCurrentPeb32(void);
PVOID MxCurrentTeb32(void);

/* the 32-bit teb of a thread given its native teb and process, or 0. */
ULONG Mx32TebOf(HANDLE process, PVOID teb);

/* information classes, in wow32_query.c. each takes the 32-bit arguments of its service. */
NTSTATUS Mx32QueryInformationProcess(const ULONG *a);
NTSTATUS Mx32SetInformationProcess(const ULONG *a);
NTSTATUS Mx32QueryInformationThread(const ULONG *a);
NTSTATUS Mx32SetInformationToken(const ULONG *a);
NTSTATUS Mx32FilterToken(const ULONG *a);
NTSTATUS Mx32AdjustGroupsToken(const ULONG *a);
NTSTATUS Mx32SetInformationThread(const ULONG *a);
NTSTATUS Mx32QuerySystemInformation(const ULONG *a);
NTSTATUS Mx32QueryObject(const ULONG *a);
NTSTATUS Mx32SetInformationObject(const ULONG *a);
NTSTATUS Mx32QuerySection(const ULONG *a);
NTSTATUS Mx32QueryInformationJobObject(const ULONG *a);
NTSTATUS Mx32SetInformationJobObject(const ULONG *a);
NTSTATUS Mx32QueryInformationToken(const ULONG *a);
NTSTATUS Mx32QueryVirtualMemory(const ULONG *a);

#endif
