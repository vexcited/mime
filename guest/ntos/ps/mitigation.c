/*
 * process mitigation policies: what a process was created with, what it asks for later and what the
 * kernel enforces of it. a program gives CreateProcess two words of creation flags, kernelbase repacks
 * them as the mitigation options attribute, and the flag word of each PROCESS_MITIGATION_POLICY class
 * is derived here. a policy can be turned on and never off again.
 *
 * enforced: a ban on dynamic code (memory that is writable and executable, or newly executable), for
 * the threads that did not opt out, and the ban on creating child processes. recorded and reported
 * only: the rest, among them the win32k system call ban and strict handle checks.
 */
#include "../ntos.h"

#define STATUS_DYNAMIC_CODE_BLOCKED ((NTSTATUS)0xC0000604L)

static const MIME_MITIGATION_FIELD g_fields[] = {MIME_MITIGATION_FIELDS};

/* the options of the kernel's layout as the program's two words of flags. */
void PspUnpackMitigationOptions(const ULONG64 map[3], ULONG64 flags[2])
{
    flags[0] = flags[1] = 0;
    ULONG dep = map[0] & 3;
    if (dep == 1)
        flags[0] |= 1;
    else if (dep == 3)
        flags[0] |= 3;
    if (((map[0] >> 4) & 3) == 1)
        flags[0] |= 4;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_fields); i++) {
        const MIME_MITIGATION_FIELD *f = &g_fields[i];
        flags[f->Word] |= ((map[f->KernelWord] >> f->KernelShift) & 3) << f->Shift;
    }
}

static ULONG field(const ULONG64 *flags, ULONG word, ULONG shift)
{
    return (ULONG)(flags[word] >> shift) & 3;
}

/* the flag word of every policy class, from the creation flags. */
static void derive(PEPROCESS process)
{
    const ULONG64 *w = process->MitigationFlags;
    ULONG *p = process->MitigationPolicy;
    ULONG relocate = field(w, 0, 8), code = field(w, 0, 36);
    p[MIME_POLICY_ASLR] = (field(w, 0, 16) == 1 ? MIME_ASLR_BOTTOM_UP : 0) |
                          (relocate == 1 || relocate == 3 ? MIME_ASLR_FORCE_RELOCATE : 0) |
                          (field(w, 0, 20) == 1 ? MIME_ASLR_HIGH_ENTROPY : 0) |
                          (relocate == 3 ? MIME_ASLR_DISALLOW_STRIPPED : 0);
    p[MIME_POLICY_DYNAMIC_CODE] = (code == 1 || code == 3 ? MIME_DYNAMIC_CODE_PROHIBIT : 0) |
                                  (code == 3 ? MIME_DYNAMIC_CODE_THREAD_OPT_OUT : 0) |
                                  (field(w, 1, 20) == 1 ? MIME_DYNAMIC_CODE_REMOTE_DOWNGRADE : 0);
    p[MIME_POLICY_STRICT_HANDLE_CHECK] =
        field(w, 0, 24) == 1 ? MIME_STRICT_HANDLE_RAISE | MIME_STRICT_HANDLE_PERMANENT : 0;
    p[MIME_POLICY_SYSTEM_CALL_DISABLE] = field(w, 0, 28) == 1 ? MIME_SYSTEM_CALL_DISABLE_WIN32K : 0;
    p[MIME_POLICY_EXTENSION_POINT_DISABLE] = field(w, 0, 32) == 1 ? MIME_EXTENSION_POINT_DISABLE : 0;
    ULONG cfg = field(w, 0, 40);
    p[MIME_POLICY_CONTROL_FLOW_GUARD] = (cfg == 1 || cfg == 3 ? MIME_CFG_ENABLE : 0) |
                                        (field(w, 1, 8) == 1 ? MIME_CFG_STRICT : 0);
    ULONG signature = field(w, 0, 44);
    p[MIME_POLICY_SIGNATURE] = signature == 1 || signature == 3 ? MIME_SIGNATURE_MICROSOFT_ONLY : 0;
    p[MIME_POLICY_FONT_DISABLE] = field(w, 0, 48) == 1 ? MIME_FONT_DISABLE_NONSYSTEM : 0;
    p[MIME_POLICY_IMAGE_LOAD] = (field(w, 0, 52) == 1 ? MIME_IMAGE_LOAD_NO_REMOTE : 0) |
                                (field(w, 0, 56) == 1 ? MIME_IMAGE_LOAD_NO_LOW_LABEL : 0) |
                                (field(w, 0, 60) == 1 ? MIME_IMAGE_LOAD_PREFER_SYSTEM32 : 0);
    p[MIME_POLICY_SIDE_CHANNEL_ISOLATION] = (field(w, 1, 16) == 1 ? MIME_SIDE_CHANNEL_SMT_BRANCH : 0) |
                                            (field(w, 1, 24) == 1 ? MIME_SIDE_CHANNEL_SPECULATIVE_STORE : 0);
}

/*
 * the options a new process is created with. a field the creator left alone is the parent's, the rest
 * is the creator's, unless the parent marked its own field with the third bit.
 */
void PspApplyMitigationOptions(PEPROCESS process, PEPROCESS parent, const ULONG64 requested[3])
{
    ULONG64 result[3] = {};
    for (ULONG i = 0; i < 40; i++) {
        ULONG shift = (4 * i) & 63, word = (4 * i) >> 6;
        ULONG inherited = parent ? (ULONG)(parent->MitigationMap[word] >> shift) & 0xF : 0;
        ULONG own = (ULONG)(requested[word] >> shift) & 0xF;
        ULONG field = (inherited & 4) || !(own & 3) ? inherited : own;
        result[word] |= (ULONG64)field << shift;
    }
    memcpy(process->MitigationMap, result, sizeof(result));
    PspUnpackMitigationOptions(result, process->MitigationFlags);
    derive(process);
}

void PspApplyChildProcessPolicy(PEPROCESS process, ULONG policy)
{
    process->ChildProcessPolicy = policy;
    process->MitigationPolicy[MIME_POLICY_CHILD_PROCESS] =
        (policy & MIME_CHILD_PROCESS_RESTRICTED ? MIME_CHILD_PROCESS_NO_CREATION : 0) |
        (policy & MIME_CHILD_PROCESS_RESTRICTED_UNLESS_SECURE ? MIME_CHILD_PROCESS_ALLOW_SECURE : 0);
}

/* the bits each class defines. */
static BOOLEAN valid_flags(ULONG policy, ULONG flags)
{
    static const ULONG masks[MIME_POLICY_COUNT] = {
        [MIME_POLICY_ASLR] = 0xF,
        [MIME_POLICY_DYNAMIC_CODE] = 0xF,
        [MIME_POLICY_STRICT_HANDLE_CHECK] = 0x3,
        [MIME_POLICY_SYSTEM_CALL_DISABLE] = 0x3,
        [MIME_POLICY_EXTENSION_POINT_DISABLE] = 0x3,
        [MIME_POLICY_CONTROL_FLOW_GUARD] = 0x7,
        [MIME_POLICY_SIGNATURE] = 0x1F,
        [MIME_POLICY_FONT_DISABLE] = 0x3,
        [MIME_POLICY_IMAGE_LOAD] = 0x1F,
        [MIME_POLICY_CHILD_PROCESS] = 0x7,
        [MIME_POLICY_SIDE_CHANNEL_ISOLATION] = 0x1F,
        [MIME_POLICY_USER_SHADOW_STACK] = 0x3FF,
        [MIME_POLICY_REDIRECTION_TRUST] = 0x3,
    };
    return policy < MIME_POLICY_COUNT && !(flags & ~masks[policy]);
}

/*
 * NtSetInformationProcess(ProcessMitigationPolicy): the policy and its new flag word. only the policies a
 * program can ask for after it started are taken, and only for the process itself, except that a thread
 * may change the dynamic code policy of another process that allows a downgrade.
 */
NTSTATUS PspSetMitigationPolicy(HANDLE handle, PVOID info, ULONG length)
{
    if (length != 8)
        return STATUS_INFO_LENGTH_MISMATCH;
    ULONG request[2];
    NTSTATUS status = KiCopyFromUser(request, info, sizeof(request));
    if (!NT_SUCCESS(status))
        return status;
    ULONG policy = request[0], flags = request[1];
    if (handle != NtCurrentProcess() && policy != MIME_POLICY_DYNAMIC_CODE)
        return STATUS_INVALID_PARAMETER;
    switch (policy) {
    case MIME_POLICY_ASLR:
    case MIME_POLICY_DYNAMIC_CODE:
    case MIME_POLICY_STRICT_HANDLE_CHECK:
    case MIME_POLICY_SYSTEM_CALL_DISABLE:
    case MIME_POLICY_EXTENSION_POINT_DISABLE:
    case MIME_POLICY_CONTROL_FLOW_GUARD:
    case MIME_POLICY_SIGNATURE:
    case MIME_POLICY_FONT_DISABLE:
    case MIME_POLICY_IMAGE_LOAD:
    case MIME_POLICY_CHILD_PROCESS:
    case MIME_POLICY_SIDE_CHANNEL_ISOLATION:
    case MIME_POLICY_USER_SHADOW_STACK:
    case MIME_POLICY_REDIRECTION_TRUST:
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
    if (!valid_flags(policy, flags))
        return STATUS_INVALID_PARAMETER;
    /* opting out and downgrading only make sense next to the ban itself. */
    if (policy == MIME_POLICY_DYNAMIC_CODE) {
        if (!(flags & MIME_DYNAMIC_CODE_PROHIBIT) && (flags & (MIME_DYNAMIC_CODE_THREAD_OPT_OUT | MIME_DYNAMIC_CODE_REMOTE_DOWNGRADE)))
            return STATUS_INVALID_PARAMETER;
    }
    PEPROCESS process;
    status = PsReferenceProcessHandle(handle, PROCESS_SET_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    ULONG current = process->MitigationPolicy[policy];
    if (policy == MIME_POLICY_DYNAMIC_CODE && process != PsCurrentProcess() &&
        !(current & MIME_DYNAMIC_CODE_REMOTE_DOWNGRADE)) {
        status = STATUS_ACCESS_DENIED;
    } else if (current & ~flags) {
        /* nothing a policy turned on is turned off. */
        status = STATUS_ACCESS_DENIED;
    } else {
        process->MitigationPolicy[policy] = current | flags;
        if (policy == MIME_POLICY_CHILD_PROCESS)
            process->ChildProcessPolicy |= MIME_CHILD_PROCESS_RESTRICTED;
    }
    ObDereferenceObject(process);
    return status;
}

/* the flag word of a class, for a caller of NtQueryInformationProcess: the class in, the flags out. */
NTSTATUS PspQueryMitigationPolicy(PEPROCESS process, PVOID info, ULONG length, PULONG returned)
{
    if (length != 8)
        return STATUS_INFO_LENGTH_MISMATCH;
    ULONG request[2];
    NTSTATUS status = KiCopyFromUser(request, info, sizeof(request));
    if (!NT_SUCCESS(status))
        return status;
    ULONG policy = request[0];
    switch (policy) {
    case MIME_POLICY_ASLR:
    case MIME_POLICY_DYNAMIC_CODE:
    case MIME_POLICY_STRICT_HANDLE_CHECK:
    case MIME_POLICY_SYSTEM_CALL_DISABLE:
    case MIME_POLICY_EXTENSION_POINT_DISABLE:
    case MIME_POLICY_CONTROL_FLOW_GUARD:
    case MIME_POLICY_SIGNATURE:
    case MIME_POLICY_FONT_DISABLE:
    case MIME_POLICY_IMAGE_LOAD:
    case MIME_POLICY_SYSTEM_CALL_FILTER:
    case MIME_POLICY_PAYLOAD_RESTRICTION:
    case MIME_POLICY_CHILD_PROCESS:
    case MIME_POLICY_SIDE_CHANNEL_ISOLATION:
    case MIME_POLICY_USER_SHADOW_STACK:
    case MIME_POLICY_REDIRECTION_TRUST:
    case MIME_POLICY_USER_POINTER_AUTH:
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
    request[1] = process->MitigationPolicy[policy];
    status = KiCopyToUser(info, request, sizeof(request));
    if (NT_SUCCESS(status) && returned) {
        ULONG size = sizeof(request);
        status = KiCopyToUser(returned, &size, sizeof(size));
    }
    return status;
}

/* NtSetInformationThread(ThreadDynamicCodePolicyInfo). */
NTSTATUS PspSetThreadDynamicCodePolicy(PETHREAD thread, PVOID info, ULONG length)
{
    ULONG value;
    if (length != sizeof(ULONG))
        return STATUS_INFO_LENGTH_MISMATCH;
    NTSTATUS status = KiCopyFromUser(&value, info, sizeof(value));
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process = (PEPROCESS)thread->Tcb.Process;
    if (value > MIME_THREAD_DYNAMIC_CODE_ALLOW)
        return STATUS_INVALID_PARAMETER;
    /* a thread opts out of a ban that allows it, and only for itself. */
    if (thread != PsCurrentThread())
        return STATUS_INVALID_PARAMETER;
    if (value && !(process->MitigationPolicy[MIME_POLICY_DYNAMIC_CODE] & MIME_DYNAMIC_CODE_THREAD_OPT_OUT))
        return STATUS_ACCESS_DENIED;
    thread->DynamicCodeAllowed = value != 0;
    return STATUS_SUCCESS;
}

/* whether the calling thread may not make memory of a process executable that was not code. */
BOOLEAN PsDynamicCodeBlocked(PEPROCESS target)
{
    ULONG policy = target->MitigationPolicy[MIME_POLICY_DYNAMIC_CODE];
    if (!(policy & MIME_DYNAMIC_CODE_PROHIBIT))
        return FALSE;
    return !((policy & MIME_DYNAMIC_CODE_THREAD_OPT_OUT) && PsCurrentThread()->DynamicCodeAllowed);
}
