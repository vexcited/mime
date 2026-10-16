/*
 * system service dispatch and user memory access.
 */
#include "../ntos.h"

#define MIME_NT_SERVICES_DEFINE_NAMES
#include "mime_svc_nt.h"
#define MIME_WIN32K_SERVICES_DEFINE_NAMES
#include "mime_svc_win32k.h"

typedef struct KI_SERVICE_TABLE {
    PKSERVICE_DESCRIPTOR Table;
    ULONG Count;
} KI_SERVICE_TABLE;

static KSERVICE_DESCRIPTOR g_nt_services[MIME_NT_SERVICES_COUNT];
static KI_SERVICE_TABLE g_tables[2] = {{g_nt_services, MIME_NT_SERVICES_COUNT}, {nullptr, 0}};
static UCHAR g_reported[2][4096 / 8];
/* calls of each service, for the thread dump; racy counts are good enough for that. */
static ULONG64 g_calls[2][4096];

typedef struct _WIN32_CALLOUTS {
    NTSTATUS(NTAPI *ProcessCallout)(PEPROCESS process, BOOLEAN initialize);
    NTSTATUS(NTAPI *ThreadCallout)(PETHREAD thread, BOOLEAN initialize);
} WIN32_CALLOUTS;

static WIN32_CALLOUTS g_win32;

void NTAPI PsEstablishWin32Callouts(WIN32_CALLOUTS *callouts)
{
    g_win32 = *callouts;
}

NTSTATUS PsWin32ProcessCallout(PEPROCESS process, BOOLEAN initialize)
{
    return g_win32.ProcessCallout ? g_win32.ProcessCallout(process, initialize) : STATUS_SUCCESS;
}

NTSTATUS PsWin32ThreadCallout(PETHREAD thread, BOOLEAN initialize)
{
    return g_win32.ThreadCallout ? g_win32.ThreadCallout(thread, initialize) : STATUS_SUCCESS;
}

static NTSTATUS convert_to_gui_thread(void)
{
    PETHREAD thread = PsCurrentThread();
    PEPROCESS process = PsCurrentProcess();
    if (!g_win32.ThreadCallout)
        return STATUS_INVALID_SYSTEM_SERVICE;
    if (!process->Win32Process) {
        NTSTATUS status = g_win32.ProcessCallout(process, TRUE);
        if (!NT_SUCCESS(status))
            return status;
    }
    return g_win32.ThreadCallout(thread, TRUE);
}

NTSTATUS KeRegisterSystemServices(ULONG table, PKSERVICE_DESCRIPTOR services, ULONG count)
{
    if (table == 0) {
        for (ULONG i = 0; i < count; i++)
            g_nt_services[i] = services[i];
    }
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI KeAddSystemServiceTable(PKSERVICE_DESCRIPTOR table, PVOID unused, ULONG count, PVOID arguments,
                                      ULONG index)
{
    if (index != 1 || g_tables[1].Table)
        return FALSE;
    g_tables[1].Table = table;
    g_tables[1].Count = count;
    return TRUE;
}

void KiRegisterService(ULONG number, PVOID routine, UCHAR stack_args)
{
    if (number < MIME_NT_SERVICES_COUNT) {
        g_nt_services[number].Routine = routine;
        g_nt_services[number].StackArgs = stack_args;
        g_nt_services[number].Implemented = 1;
    }
}

static const char *service_name(ULONG table, ULONG index)
{
    if (table == 0 && index < MIME_NT_SERVICES_COUNT && MIME_NT_SERVICES_NAMES[index])
        return MIME_NT_SERVICES_NAMES[index];
    if (table == 1 && index < MIME_WIN32K_SERVICES_COUNT && MIME_WIN32K_SERVICES_NAMES[index])
        return MIME_WIN32K_SERVICES_NAMES[index];
    return "?";
}

void KiSystemService(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    KiEnableInterrupts();

    ULONG number = KI_SERVICE_NUMBER(frame);
    ULONG table = number >> 12;
    ULONG index = number & 0xFFF;
    ULONG64 result;
    if (table < 2)
        g_calls[table][index]++;
    KI_SERVICE_TABLE *t = table < 2 ? &g_tables[table] : nullptr;
    if (table == 1 && !thread->Win32Thread && g_win32.ThreadCallout) {
        NTSTATUS status = convert_to_gui_thread();
        if (!NT_SUCCESS(status))
            KiLogPrintf("cannot convert thread to a gui thread: %08x", status);
    }
    if (!t || !t->Table || index >= t->Count || !t->Table[index].Routine) {
        if (table < 2 && !(g_reported[table][index / 8] & (1u << (index % 8)))) {
            g_reported[table][index / 8] |= (UCHAR)(1u << (index % 8));
            KiLogPrintf("unimplemented system service %s (0x%04x) called by %s", service_name(table, index), number,
                        PsCurrentProcess()->ImageFileName);
        }
        result = table == 1 ? 0 : (ULONG64)(LONG64)STATUS_NOT_IMPLEMENTED;
    } else {
        ULONG64 args[16];
        for (int i = 0; i < KI_SERVICE_REGISTER_ARGS; i++)
            args[i] = KI_SERVICE_REGISTER_ARG(frame, i);
        ULONG stack = t->Table[index].StackArgs;
        if (stack > 16 - KI_SERVICE_REGISTER_ARGS)
            stack = 16 - KI_SERVICE_REGISTER_ARGS;
        if (stack && !NT_SUCCESS(KiCopyFromUser(&args[KI_SERVICE_REGISTER_ARGS], (PVOID)KI_SERVICE_STACK_ARGS(frame),
                                                stack * sizeof(ULONG64))))
            result = (ULONG64)(LONG64)STATUS_ACCESS_VIOLATION;
        else
            result = KiCallService(t->Table[index].Routine, args, stack);
    }
    KI_SERVICE_RESULT(frame) = result;
    KiExitToUserMode(frame);
}

BOOLEAN KiIsUserRange(const void *address, SIZE_T length)
{
    ULONG_PTR a = (ULONG_PTR)address;
    return a + length >= a && a + length <= MIME_USER_ADDRESS_LIMIT + 1;
}

/* copies from caller memory; kernel mode callers (Zw* services) pass kernel buffers. */
NTSTATUS KiCopyFromUser(void *dst, const void *user_src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (KiCurrentThread()->PreviousMode == KernelMode && !KiIsUserRange(user_src, length)) {
        memcpy(dst, user_src, length);
        return STATUS_SUCCESS;
    }
    if (!KiIsUserRange(user_src, length))
        return STATUS_ACCESS_VIOLATION;
    KJUMP_BUFFER jb;
    if (KI_TRY_USER(jb)) {
        memcpy(dst, user_src, length);
        KI_END_TRY_USER();
        return STATUS_SUCCESS;
    }
    return STATUS_ACCESS_VIOLATION;
}

NTSTATUS KiCopyToUser(void *user_dst, const void *src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (KiCurrentThread()->PreviousMode == KernelMode && !KiIsUserRange(user_dst, length)) {
        memcpy(user_dst, src, length);
        return STATUS_SUCCESS;
    }
    if (!KiIsUserRange(user_dst, length))
        return STATUS_ACCESS_VIOLATION;
    KJUMP_BUFFER jb;
    if (KI_TRY_USER(jb)) {
        memcpy(user_dst, src, length);
        KI_END_TRY_USER();
        return STATUS_SUCCESS;
    }
    return STATUS_ACCESS_VIOLATION;
}

/* driver facing copies that always treat the pointer as user memory, with fault recovery. */
NTSTATUS NTAPI MimeCopyFromUser(void *dst, const void *user_src, SIZE_T length)
{
    KPROCESSOR_MODE old = KiCurrentThread()->PreviousMode;
    KiCurrentThread()->PreviousMode = UserMode;
    NTSTATUS status = KiCopyFromUser(dst, user_src, length);
    KiCurrentThread()->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI MimeCopyToUser(void *user_dst, const void *src, SIZE_T length)
{
    KPROCESSOR_MODE old = KiCurrentThread()->PreviousMode;
    KiCurrentThread()->PreviousMode = UserMode;
    NTSTATUS status = KiCopyToUser(user_dst, src, length);
    KiCurrentThread()->PreviousMode = old;
    return status;
}

/* captures a caller supplied UNICODE_STRING into pool, honoring the previous mode. */
NTSTATUS KiCaptureUnicodeString(PUNICODE_STRING copy, PCUNICODE_STRING src)
{
    UNICODE_STRING s;
    copy->Buffer = nullptr;
    copy->Length = copy->MaximumLength = 0;
    if (!src)
        return STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode) {
        if (!NT_SUCCESS(KiCopyFromUser(&s, src, sizeof(s))))
            return STATUS_ACCESS_VIOLATION;
    } else {
        s = *src;
    }
    if (s.Length & 1 || s.Length > 0xFFFC)
        return STATUS_OBJECT_NAME_INVALID;
    copy->Buffer = ExAllocatePool2(POOL_FLAG_PAGED, s.Length + sizeof(WCHAR), KI_TAG('C', 'a', 'p', 'S'));
    if (!copy->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode)
        status = KiCopyFromUser(copy->Buffer, s.Buffer, s.Length);
    else
        memcpy(copy->Buffer, s.Buffer, s.Length);
    if (!NT_SUCCESS(status)) {
        ExFreePool(copy->Buffer);
        copy->Buffer = nullptr;
        return status;
    }
    copy->Buffer[s.Length / sizeof(WCHAR)] = 0;
    copy->Length = s.Length;
    copy->MaximumLength = (USHORT)(s.Length + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

void KiFreeCapturedString(PUNICODE_STRING s)
{
    if (s->Buffer)
        ExFreePool(s->Buffer);
    s->Buffer = nullptr;
}

void NTAPI ProbeForRead(PVOID address, SIZE_T length, ULONG alignment)
{
    if (length && !KiIsUserRange(address, length))
        KeBugCheckEx(0x0000001E, STATUS_ACCESS_VIOLATION, (ULONG_PTR)address, 0, 0);
}

void NTAPI ProbeForWrite(PVOID address, SIZE_T length, ULONG alignment)
{
    ProbeForRead(address, length, alignment);
}

/* the services called most since the last dump, then the counts start over. */
void KiDumpServiceCalls(void)
{
    for (int round = 0; round < 12; round++) {
        ULONG64 best = 0;
        ULONG bt = 0, bi = 0;
        for (ULONG t = 0; t < 2; t++)
            for (ULONG i = 0; i < 4096; i++)
                if (g_calls[t][i] > best)
                    best = g_calls[t][i], bt = t, bi = i;
        if (!best)
            break;
        KiLogPrintf("calls %s: %llu", service_name(bt, bi), best);
        g_calls[bt][bi] = 0;
    }
    memset(g_calls, 0, sizeof(g_calls));
}
