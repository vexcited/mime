/*
 * x64 system dll exports run natively. the heap, the locks and the memory
 * and string primitives are a large share of what x64 programs execute,
 * and the same code built for arm64 runs them many times faster than the
 * interpreter. the x64 images stay untouched: a block that starts at a
 * hooked export calls the native function, see x86_set_native.
 */
#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>

#include "native.h"
#include "x86.h"

typedef ULONG64 (*NATIVE6)(ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64);
void MxSetSuspendCritical(X86Cpu *c, ULONG critical);

/* the x64 calling convention: rcx, rdx, r8, r9, then the stack above the return address and its home space. */
static ULONG64 arg(X86Cpu *c, int i)
{
    static const int regs[4] = {X86_RCX, X86_RDX, X86_R8, X86_R9};
    return i < 4 ? c->r[regs[i]] : *(const ULONG64 *)(c->r[X86_RSP] + 0x28 + 8 * (i - 4));
}

/* returns to the x64 caller. */
static int finish(X86Cpu *c, ULONG64 value)
{
    c->r[X86_RAX] = value;
    c->rip = *(const ULONG64 *)c->r[X86_RSP];
    c->r[X86_RSP] += 8;
    return 1;
}

static int call6(X86Cpu *c, void *fn, int args)
{
    ULONG64 a[6] = {0};
    for (int i = 0; i < args; i++)
        a[i] = arg(c, i);
    MxSetSuspendCritical(c, 0);
    ULONG64 result = ((NATIVE6)fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
    MxSetSuspendCritical(c, 1);
    return finish(c, result);
}

#define NATIVE(name, args)                                                                                             \
    void Mx##name(void);                                                                                               \
    static int hook_##name(X86Cpu *c)                                                                                  \
    {                                                                                                                  \
        return call6(c, (void *)Mx##name, args);                                                                       \
    }

NATIVE(RtlCreateHeap, 6)
NATIVE(RtlDestroyHeap, 1)
NATIVE(RtlAllocateHeap, 3)
NATIVE(RtlFreeHeap, 3)
NATIVE(RtlSizeHeap, 3)
NATIVE(RtlReAllocateHeap, 4)
NATIVE(RtlLockHeap, 1)
NATIVE(RtlUnlockHeap, 1)
NATIVE(RtlValidateHeap, 3)
NATIVE(RtlCompactHeap, 2)
NATIVE(RtlGetProcessHeaps, 2)
NATIVE(RtlSetHeapInformation, 4)
NATIVE(RtlQueryHeapInformation, 5)
NATIVE(RtlInitializeCriticalSectionEx, 3)
NATIVE(RtlInitializeCriticalSectionAndSpinCount, 2)
NATIVE(RtlInitializeCriticalSection, 1)
NATIVE(RtlSetCriticalSectionSpinCount, 2)
NATIVE(RtlDeleteCriticalSection, 1)
NATIVE(RtlEnterCriticalSection, 1)
NATIVE(RtlTryEnterCriticalSection, 1)
NATIVE(RtlLeaveCriticalSection, 1)
NATIVE(RtlIsCriticalSectionLocked, 1)
NATIVE(RtlIsCriticalSectionLockedByThread, 1)
NATIVE(RtlInitializeSRWLock, 1)
NATIVE(RtlTryAcquireSRWLockExclusive, 1)
NATIVE(RtlAcquireSRWLockExclusive, 1)
NATIVE(RtlReleaseSRWLockExclusive, 1)
NATIVE(RtlTryAcquireSRWLockShared, 1)
NATIVE(RtlAcquireSRWLockShared, 1)
NATIVE(RtlReleaseSRWLockShared, 1)
NATIVE(RtlInitializeConditionVariable, 1)
NATIVE(RtlWakeConditionVariable, 1)
NATIVE(RtlWakeAllConditionVariable, 1)
NATIVE(RtlSleepConditionVariableCS, 3)
NATIVE(RtlSleepConditionVariableSRW, 4)
NATIVE(RtlRunOnceInitialize, 1)
NATIVE(RtlRunOnceBeginInitialize, 3)
NATIVE(RtlRunOnceComplete, 3)
NATIVE(RtlWaitOnAddress, 4)
NATIVE(RtlWakeAddressSingle, 1)
NATIVE(RtlWakeAddressAll, 1)
NATIVE(RtlInitializeSListHead, 1)
NATIVE(RtlInterlockedPushEntrySList, 2)
NATIVE(RtlInterlockedPopEntrySList, 1)
NATIVE(RtlInterlockedFlushSList, 1)
NATIVE(RtlQueryDepthSList, 1)
NATIVE(RtlFirstEntrySList, 1)

/* RtlRunOnceExecuteOnce calls back into x64 code and stays interpreted. */
#define ENTRY(name) {#name, hook_##name}
static const struct {
    const char *name;
    X86Native fn;
} g_ntdll[] = {
    ENTRY(RtlCreateHeap),
    ENTRY(RtlDestroyHeap),
    ENTRY(RtlAllocateHeap),
    ENTRY(RtlFreeHeap),
    ENTRY(RtlSizeHeap),
    ENTRY(RtlReAllocateHeap),
    ENTRY(RtlLockHeap),
    ENTRY(RtlUnlockHeap),
    ENTRY(RtlValidateHeap),
    ENTRY(RtlCompactHeap),
    ENTRY(RtlGetProcessHeaps),
    ENTRY(RtlSetHeapInformation),
    ENTRY(RtlQueryHeapInformation),
    ENTRY(RtlInitializeCriticalSectionEx),
    ENTRY(RtlInitializeCriticalSectionAndSpinCount),
    ENTRY(RtlInitializeCriticalSection),
    ENTRY(RtlSetCriticalSectionSpinCount),
    ENTRY(RtlDeleteCriticalSection),
    ENTRY(RtlEnterCriticalSection),
    ENTRY(RtlTryEnterCriticalSection),
    ENTRY(RtlLeaveCriticalSection),
    ENTRY(RtlIsCriticalSectionLocked),
    ENTRY(RtlIsCriticalSectionLockedByThread),
    ENTRY(RtlInitializeSRWLock),
    ENTRY(RtlTryAcquireSRWLockExclusive),
    ENTRY(RtlAcquireSRWLockExclusive),
    ENTRY(RtlReleaseSRWLockExclusive),
    ENTRY(RtlTryAcquireSRWLockShared),
    ENTRY(RtlAcquireSRWLockShared),
    ENTRY(RtlReleaseSRWLockShared),
    ENTRY(RtlInitializeConditionVariable),
    ENTRY(RtlWakeConditionVariable),
    ENTRY(RtlWakeAllConditionVariable),
    ENTRY(RtlSleepConditionVariableCS),
    ENTRY(RtlSleepConditionVariableSRW),
    ENTRY(RtlRunOnceInitialize),
    ENTRY(RtlRunOnceBeginInitialize),
    ENTRY(RtlRunOnceComplete),
    ENTRY(RtlWaitOnAddress),
    ENTRY(RtlWakeAddressSingle),
    ENTRY(RtlWakeAddressAll),
    ENTRY(RtlInitializeSListHead),
    ENTRY(RtlInterlockedPushEntrySList),
    ENTRY(RtlInterlockedPopEntrySList),
    ENTRY(RtlInterlockedFlushSList),
    ENTRY(RtlQueryDepthSList),
    ENTRY(RtlFirstEntrySList),
};

/* the crt primitives every x64 system dll carries its own copy of. */
static int hook_memmove(X86Cpu *c)
{
    __builtin_memmove((void *)arg(c, 0), (const void *)arg(c, 1), (SIZE_T)arg(c, 2));
    return finish(c, arg(c, 0));
}

static int hook_memset(X86Cpu *c)
{
    __builtin_memset((void *)arg(c, 0), (int)(UCHAR)arg(c, 1), (SIZE_T)arg(c, 2));
    return finish(c, arg(c, 0));
}

static int hook_memcmp(X86Cpu *c)
{
    return finish(c, (ULONG64)(LONG64)__builtin_memcmp((const void *)arg(c, 0), (const void *)arg(c, 1),
                                                        (SIZE_T)arg(c, 2)));
}

static int hook_strlen(X86Cpu *c)
{
    return finish(c, __builtin_strlen((const char *)arg(c, 0)));
}

static int hook_wcslen(X86Cpu *c)
{
    const WCHAR *s = (const WCHAR *)arg(c, 0), *p = s;
    while (*p)
        p++;
    return finish(c, (ULONG64)(p - s));
}

/*
 * msvcrt's allocator on the process heap. failures decline, so the x64
 * code runs the new handler and sets errno exactly as it would.
 */
#define heap_alloc ((PVOID(NTAPI *)(PVOID, ULONG, SIZE_T))(void *)MxRtlAllocateHeap)
#define heap_free ((BOOLEAN(NTAPI *)(PVOID, ULONG, PVOID))(void *)MxRtlFreeHeap)
#define heap_realloc ((PVOID(NTAPI *)(PVOID, ULONG, PVOID, SIZE_T))(void *)MxRtlReAllocateHeap)

static int crt_alloc(X86Cpu *c, SIZE_T size, ULONG flags)
{
    if (size > (SIZE_T)-32)
        return 0;
    PVOID p = heap_alloc(NtCurrentPeb()->ProcessHeap, flags, size ? size : 1);
    return p ? finish(c, (ULONG64)p) : 0;
}

static int hook_malloc(X86Cpu *c)
{
    return crt_alloc(c, (SIZE_T)arg(c, 0), 0);
}

static int hook_calloc(X86Cpu *c)
{
    SIZE_T count = (SIZE_T)arg(c, 0), size = (SIZE_T)arg(c, 1);
    if (size && count > (SIZE_T)-32 / size)
        return 0;
    return crt_alloc(c, count * size, HEAP_ZERO_MEMORY);
}

static int hook_free(X86Cpu *c)
{
    PVOID p = (PVOID)arg(c, 0);
    return !p || heap_free(NtCurrentPeb()->ProcessHeap, 0, p) ? finish(c, 0) : 0;
}

static int hook_realloc(X86Cpu *c)
{
    PVOID p = (PVOID)arg(c, 0);
    SIZE_T size = (SIZE_T)arg(c, 1);
    if (!p)
        return crt_alloc(c, size, 0);
    if (!size || size > (SIZE_T)-32)
        return 0;
    PVOID q = heap_realloc(NtCurrentPeb()->ProcessHeap, 0, p, size);
    return q ? finish(c, (ULONG64)q) : 0;
}

static const struct {
    const char *name;
    X86Native fn;
} g_crt[] = {
    {"memcpy", hook_memmove}, {"memmove", hook_memmove}, {"memset", hook_memset},
    {"memcmp", hook_memcmp},  {"strlen", hook_strlen},   {"wcslen", hook_wcslen},
}, g_msvcrt[] = {
    {"malloc", hook_malloc}, {"calloc", hook_calloc}, {"free", hook_free}, {"realloc", hook_realloc},
};

static PIMAGE_NT_HEADERS64 x64_headers(PVOID base)
{
    PIMAGE_DOS_HEADER dos = base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return NULL;
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((PUCHAR)base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return NULL;
    return nt;
}

/* an image the x64 loader mapped, not a data view of a file. */
static BOOLEAN is_image(PVOID base)
{
    MEMORY_BASIC_INFORMATION info;
    return NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), base, MemoryBasicInformation, &info, sizeof(info),
                                           NULL)) &&
           info.Type == MEM_IMAGE && info.AllocationBase == base;
}

/* the address of a code export, NULL for absent names and forwarders. */
static ULONG64 code_export(PVOID base, PIMAGE_NT_HEADERS64 nt, const char *name)
{
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || dir.VirtualAddress + dir.Size > nt->OptionalHeader.SizeOfImage)
        return 0;
    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)base + dir.VirtualAddress);
    PULONG names = (PULONG)((PUCHAR)base + exp->AddressOfNames);
    PUSHORT ordinals = (PUSHORT)((PUCHAR)base + exp->AddressOfNameOrdinals);
    PULONG functions = (PULONG)((PUCHAR)base + exp->AddressOfFunctions);
    for (ULONG i = 0; i < exp->NumberOfNames; i++) {
        if (__builtin_strcmp((const char *)base + names[i], name) != 0)
            continue;
        if (ordinals[i] >= exp->NumberOfFunctions)
            return 0;
        ULONG rva = functions[ordinals[i]];
        if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size)
            return 0;
        return (ULONG64)base + rva;
    }
    return 0;
}

/* mime's own x64 msvcrt, whose allocator is the process heap; not an msvcrt a program ships. */
static BOOLEAN is_system_msvcrt(PVOID base, PIMAGE_NT_HEADERS64 nt)
{
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || (ULONG64)base != nt->OptionalHeader.ImageBase || (ULONG64)base < 0x7FF000000000ull)
        return FALSE;
    const char *name = (const char *)base + ((PIMAGE_EXPORT_DIRECTORY)((PUCHAR)base + dir.VirtualAddress))->Name;
    static const char want[] = "msvcrt.dll";
    for (ULONG i = 0; i < sizeof(want); i++) {
        char ch = name[i];
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch != want[i])
            return FALSE;
    }
    return TRUE;
}

void MxInstallNatives(PVOID base, BOOLEAN ntdll)
{
    PIMAGE_NT_HEADERS64 nt = x64_headers(base);
    if (!nt)
        return;
    if (ntdll) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_ntdll); i++) {
            ULONG64 rip = code_export(base, nt, g_ntdll[i].name);
            if (rip)
                x86_set_native(rip, g_ntdll[i].fn);
        }
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_crt); i++) {
        ULONG64 rip = code_export(base, nt, g_crt[i].name);
        if (rip)
            x86_set_native(rip, g_crt[i].fn);
    }
    if (is_system_msvcrt(base, nt)) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_msvcrt); i++) {
            ULONG64 rip = code_export(base, nt, g_msvcrt[i].name);
            if (rip)
                x86_set_native(rip, g_msvcrt[i].fn);
        }
    }
}

void MxImageMapped(PVOID base)
{
    if (base && is_image(base))
        MxInstallNatives(base, FALSE);
}

void MxImageUnmapping(PVOID base)
{
    PIMAGE_NT_HEADERS64 nt = base && is_image(base) ? x64_headers(base) : NULL;
    if (nt)
        x86_clear_native((ULONG64)base, (ULONG64)base + nt->OptionalHeader.SizeOfImage);
}
