/*
 * the generator of activation contexts of guest/ntdll, compiled into the kernel, which answers for csrss: what the
 * generator takes from a process of its own is taken from the kernel here.
 */
#ifndef MIME_NTOS_SXS_KSHIM_H
#define MIME_NTOS_SXS_KSHIM_H

#define MIME_SXS_KERNEL 1
#define MIME_BUILD_KERNELBASE 1
#include "../../ntdll/sxs.h"

#include "sxsk.h"

NTSTATUS NTAPI SxsKFindResourceDirectory(PVOID module, PLDR_RESOURCE_INFO info, ULONG level, IMAGE_RESOURCE_DIRECTORY **dir);
NTSTATUS NTAPI SxsKAccessResource(PVOID module, IMAGE_RESOURCE_DATA_ENTRY *entry, PVOID *data, PULONG size);
RTL_PATH_TYPE NTAPI SxsKPathType(PCWSTR path);
NTSTATUS NTAPI SxsKMultiByteToUnicode(PWSTR dst, ULONG dst_bytes, PULONG written, const CHAR *src, ULONG src_bytes);

void KiLogPrintf(const char *fmt, ...);

#define DbgPrint KiLogPrintf
#define RtlAllocateHeap(heap, flags, size) SxsKAlloc((size), (flags))
#define RtlFreeHeap(heap, flags, p) SxsKFree(p)
#define NtAllocateVirtualMemory(process, base, zero, size, type, protect) SxsKDataAlloc((base), (size))
#define NtProtectVirtualMemory(process, base, size, protect, old) ((void)(base), (void)(size), (void)(old), STATUS_SUCCESS)
#define NtFreeVirtualMemory(process, base, size, type) (SxsKDataFree(*(base)), (void)(size), STATUS_SUCCESS)
#define NtOpenKey SxsKOpenKey
#define NtQueryValueKey SxsKQueryValueKey
#define NtClose SxsKClose
#define LdrFindResourceDirectory_U SxsKFindResourceDirectory
#define LdrAccessResource SxsKAccessResource
#define RtlDetermineDosPathNameType_U SxsKPathType
#define RtlMultiByteToUnicodeN SxsKMultiByteToUnicode

#endif
