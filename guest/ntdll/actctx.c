/*
 * activation contexts at run time: context objects, the per thread
 * activation stack, section lookups and queries, with the argument checks,
 * status codes and exceptions of Windows.
 */
#include "sxs.h"

#define COOKIE_TYPE_DEFAULT 1
#define FRAME_RELEASE 0x00000001
#define FRAME_RELEASE_HIGH 0x00000002
#define FRAME_HEAP 0x00000008
#define FRAME_NOT_PUSHED 0x00000010
#define FRAME_CALLER 0x00000020
#define FRAME_DEACTIVATED 0x00000040

/* the data ACTCTX_EMPTY stands for: a header and nothing else. */
static const ACTCTX_DATA g_empty_data = {ACTCTX_DATA_MAGIC, sizeof(ACTCTX_DATA), 1, sizeof(ACTCTX_DATA), 0, 0, 0, 0};
static ACTIVATION_CONTEXT g_empty_context = {0x7FFFFFFF, ACTCTX_FLAG_STATIC, nullptr, nullptr, &g_empty_data,
                                             nullptr, nullptr, 0, 0};
static LONG g_next_stack_id;

/* a stack frame the activation functions allocate, with its cookie. */
typedef struct HEAP_FRAME {
    RTL_ACTIVATION_CONTEXT_STACK_FRAME Frame;
    ULONG_PTR Cookie;
} HEAP_FRAME;

static BOOLEAN is_pseudo(PACTIVATION_CONTEXT context)
{
    return !context || ((ULONG_PTR)context - 1 | 7) == (ULONG_PTR)-1;
}

static PACTIVATION_CONTEXT_STACK teb_stack(PTEB teb)
{
    return teb->ActivationContextStackPointer;
}

static PACTIVATION_CONTEXT_STACK current_stack(void)
{
    return teb_stack(NtCurrentTeb());
}

static PRTL_ACTIVATION_CONTEXT_STACK_FRAME active_frame(void)
{
    PACTIVATION_CONTEXT_STACK stack = current_stack();
    return stack ? stack->ActiveFrame : nullptr;
}

/* the data behind a handle: NULL is the process default, -4 the system default. */
static const ACTCTX_DATA *context_data(PACTIVATION_CONTEXT context)
{
    if (!context)
        return NtCurrentPeb()->ActivationContextData;
    if (context == ACTCTX_SYSTEM_DEFAULT)
        return NtCurrentPeb()->SystemDefaultActivationContextData;
    if (context == ACTCTX_EMPTY || context == &g_empty_context)
        return &g_empty_data;
    return context->Data;
}

static void raise(NTSTATUS code, ULONG flags, ULONG count, ULONG_PTR p0, ULONG_PTR p1, ULONG_PTR p2, ULONG_PTR p3)
{
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = code;
    record.ExceptionFlags = flags;
    record.NumberParameters = count;
    record.ExceptionInformation[0] = p0;
    record.ExceptionInformation[1] = p1;
    record.ExceptionInformation[2] = p2;
    record.ExceptionInformation[3] = p3;
    RtlRaiseException(&record);
}

/* ------------------------------------------------------------------ data validation */

static NTSTATUS validate_data(const ACTCTX_DATA *data)
{
    if (data->Magic != ACTCTX_DATA_MAGIC || data->FormatVersion != 1)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    ULONG toc = data->DefaultTocOffset, roster = data->AssemblyRosterOffset, total = data->TotalSize;
    if (!toc || (toc & 3) || !roster || (roster & 3))
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    if (toc >= total || (ULONGLONG)toc + 16 > total)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    const ACTCTX_TOC_HEADER *h = (const void *)((const UCHAR *)data + toc);
    if (h->HeaderSize < 16 || h->FirstEntryOffset >= total || (h->FirstEntryOffset & 3) ||
        h->FirstEntryOffset + 16ull * h->EntryCount > total)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    if (roster >= total || (ULONGLONG)roster + 20 > total)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    if (((const ACTCTX_ROSTER_HEADER *)((const UCHAR *)data + roster))->HeaderSize < 20)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    return STATUS_SUCCESS;
}

/* finds a section by id in the default table, or in an extension's table. */
static NTSTATUS locate_section(const ACTCTX_DATA *data, const GUID *extension, ULONG id, const void **section,
                               ULONG *length)
{
    const UCHAR *base = (const UCHAR *)data;
    ULONG total = data->TotalSize;
    if (total < sizeof(ACTCTX_DATA) || data->HeaderSize < sizeof(ACTCTX_DATA))
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    ULONG toc_offset = data->DefaultTocOffset;
    if (extension) {
        ULONG ext = data->ExtendedTocOffset;
        if (!ext)
            return STATUS_SXS_SECTION_NOT_FOUND;
        if (ext >= total || (ULONGLONG)ext + 16 > total)
            return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
        const ACTCTX_EXTENDED_TOC_HEADER *eh = (const void *)(base + ext);
        if (eh->FirstEntryOffset >= total || eh->FirstEntryOffset + 24ull * eh->EntryCount > total)
            return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
        const ACTCTX_EXTENDED_TOC_ENTRY *ee = (const void *)(base + eh->FirstEntryOffset);
        ULONG i = 0;
        while (i < eh->EntryCount && memcmp(&ee[i].ExtensionGuid, extension, sizeof(GUID)))
            i++;
        if (i == eh->EntryCount)
            return STATUS_SXS_SECTION_NOT_FOUND;
        toc_offset = ee[i].TocOffset;
        if (toc_offset >= total || (ULONGLONG)toc_offset + 16 > total)
            return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    }
    if (!toc_offset)
        return STATUS_SXS_SECTION_NOT_FOUND;
    const ACTCTX_TOC_HEADER *h = (const void *)(base + toc_offset);
    if (!h->EntryCount)
        return STATUS_SXS_SECTION_NOT_FOUND;
    if (h->FirstEntryOffset >= total || h->FirstEntryOffset + 16ull * h->EntryCount > total)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    const ACTCTX_TOC_ENTRY *entries = (const void *)(base + h->FirstEntryOffset);
    const ACTCTX_TOC_ENTRY *e = nullptr;
    if ((h->Flags & ACTCTX_TOC_INORDER) && (h->Flags & ACTCTX_TOC_DENSE)) {
        if (id >= entries[0].Id && id - entries[0].Id < h->EntryCount)
            e = &entries[id - entries[0].Id];
    } else {
        for (ULONG i = 0; i < h->EntryCount && !e; i++)
            if (entries[i].Id == id)
                e = &entries[i];
    }
    if (!e || !e->Offset)
        return STATUS_SXS_SECTION_NOT_FOUND;
    if (e->Offset >= total || (ULONGLONG)e->Offset + e->Length > total)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    *section = base + e->Offset;
    *length = e->Length;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ string and guid sections */

static NTSTATUS find_in_string_section(const void *section, ULONG length, PCUNICODE_STRING key,
                                       PACTIVATION_CONTEXT_SECTION_KEYED_DATA out)
{
    const UCHAR *base = section;
    const ACTCTX_STRING_SECTION_HEADER *h = section;
    if (length < sizeof(*h) || h->Magic != ACTCTX_STRING_SECTION_MAGIC)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    if (!h->ElementCount)
        return STATUS_SXS_KEY_NOT_FOUND;
    if (h->ElementListOffset >= length ||
        h->ElementListOffset + (ULONGLONG)h->ElementCount * sizeof(ACTCTX_STRING_SECTION_ENTRY) > length)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    BOOLEAN insensitive = (h->Flags & ACTCTX_STRING_SECTION_CASE_INSENSITIVE) != 0;
    BOOLEAN hashed = h->HashAlgorithm == ACTCTX_HASH_X65599;
    ULONG chars = key->Length / sizeof(WCHAR);
    ULONG pseudo = 0;
    if (hashed) {
        for (ULONG i = 0; i < chars; i++)
            pseudo = pseudo * 65599 + (insensitive ? RtlUpcaseUnicodeChar(key->Buffer[i]) : key->Buffer[i]);
    }
    const ACTCTX_STRING_SECTION_ENTRY *entries = (const void *)(base + h->ElementListOffset);
    const ACTCTX_STRING_SECTION_ENTRY *found = nullptr;
    ULONG first = 0, last = h->ElementCount;
    if (hashed && (h->Flags & ACTCTX_STRING_SECTION_INPSEUDOKEYORDER)) {
        /* the first element of the pseudo key, then the run of equal keys. */
        ULONG lo = 0, hi = h->ElementCount;
        while (lo < hi) {
            ULONG mid = (lo + hi) / 2;
            if (entries[mid].PseudoKey < pseudo)
                lo = mid + 1;
            else
                hi = mid;
        }
        first = lo;
        last = lo;
        while (last < h->ElementCount && entries[last].PseudoKey == pseudo)
            last++;
    }
    for (ULONG i = first; i < last && !found; i++) {
        const ACTCTX_STRING_SECTION_ENTRY *e = &entries[i];
        if (hashed && e->PseudoKey != pseudo)
            continue;
        if (e->KeyOffset >= length || (ULONGLONG)e->KeyOffset + e->KeyLength > length)
            return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
        if (!RtlCompareUnicodeStrings(key->Buffer, chars, (PCWCH)(base + e->KeyOffset), e->KeyLength / sizeof(WCHAR),
                                      insensitive))
            found = e;
    }
    if (!found || !found->Offset)
        return STATUS_SXS_KEY_NOT_FOUND;
    if (found->Offset >= length || (ULONGLONG)found->Offset + found->Length > length)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    if (out) {
        out->DataFormatVersion = h->DataFormatVersion;
        out->Data = (PVOID)(base + found->Offset);
        out->Length = found->Length;
        if (out->Size >= offsetof(ACTIVATION_CONTEXT_SECTION_KEYED_DATA, Flags))
            out->AssemblyRosterIndex = found->AssemblyRosterIndex;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS find_in_guid_section(const void *section, ULONG length, const GUID *key,
                                     PACTIVATION_CONTEXT_SECTION_KEYED_DATA out)
{
    const UCHAR *base = section;
    const ACTCTX_GUID_SECTION_HEADER *h = section;
    if (length < sizeof(*h) || h->Magic != ACTCTX_GUID_SECTION_MAGIC)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    const ACTCTX_GUID_SECTION_ENTRY *found = nullptr;
    if (h->FormatVersion == 1 && h->SearchStructureOffset) {
        const ACTCTX_HASH_TABLE *t = (const void *)(base + h->SearchStructureOffset);
        if (t->BucketTableEntryCount) {
            const ACTCTX_HASH_BUCKET *b =
                (const ACTCTX_HASH_BUCKET *)(base + t->BucketTableOffset) + key->Data1 % t->BucketTableEntryCount;
            const ULONG *chain = (const ULONG *)(base + b->ChainOffset);
            for (ULONG i = 0; i < b->ChainCount && !found; i++) {
                const ACTCTX_GUID_SECTION_ENTRY *e = (const void *)(base + chain[i]);
                if (!memcmp(&e->Guid, key, sizeof(GUID)))
                    found = e;
            }
        }
    } else if (h->ElementCount) {
        if (h->ElementListOffset >= length ||
            h->ElementListOffset + (ULONGLONG)h->ElementCount * sizeof(ACTCTX_GUID_SECTION_ENTRY) > length)
            return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
        const ACTCTX_GUID_SECTION_ENTRY *entries = (const void *)(base + h->ElementListOffset);
        for (ULONG i = 0; i < h->ElementCount && !found; i++)
            if (!memcmp(&entries[i].Guid, key, sizeof(GUID)))
                found = &entries[i];
    }
    if (!found || !found->Offset)
        return STATUS_SXS_KEY_NOT_FOUND;
    if (found->Offset >= length || (ULONGLONG)found->Offset + found->Length > length)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    if (out) {
        out->DataFormatVersion = h->DataFormatVersion;
        out->Data = (PVOID)(base + found->Offset);
        out->Length = found->Length;
        if (out->Size >= offsetof(ACTIVATION_CONTEXT_SECTION_KEYED_DATA, Flags))
            out->AssemblyRosterIndex = found->AssemblyRosterIndex;
    }
    return STATUS_SUCCESS;
}

/*
 * the contexts a search walks: the active frame's, then the process
 * default, then the system default. ACTCTX_EMPTY searches nothing of its
 * own but still falls back to the defaults.
 */
typedef struct SEARCH {
    ULONG Stage;
    const GUID *Extension;
    ULONG Section;
    PACTIVATION_CONTEXT Context; /* the handle the found data belongs to */
    const ACTCTX_DATA *Data;
} SEARCH;

static NTSTATUS search_next(SEARCH *s, const void **section, ULONG *length)
{
    for (;;) {
        const ACTCTX_DATA *data = nullptr;
        PACTIVATION_CONTEXT context = nullptr;
        if (s->Stage == 0) {
            s->Stage = 1;
            PRTL_ACTIVATION_CONTEXT_STACK_FRAME frame = active_frame();
            if (frame && frame->ActivationContext) {
                context = frame->ActivationContext;
                data = context_data(context);
            }
            if (!data)
                continue;
        } else if (s->Stage == 1) {
            s->Stage = 2;
            data = NtCurrentPeb()->ActivationContextData;
            if (!data)
                continue;
        } else if (s->Stage == 2) {
            s->Stage = 3;
            context = ACTCTX_SYSTEM_DEFAULT;
            data = NtCurrentPeb()->SystemDefaultActivationContextData;
            if (!data)
                return STATUS_SXS_SECTION_NOT_FOUND;
        } else {
            return STATUS_SXS_SECTION_NOT_FOUND;
        }
        NTSTATUS status = locate_section(data, s->Extension, s->Section, section, length);
        if (status == STATUS_SXS_SECTION_NOT_FOUND)
            continue;
        if (!NT_SUCCESS(status))
            return status;
        s->Context = context;
        s->Data = data;
        return STATUS_SUCCESS;
    }
}

/* tells a context's owner the first time one of its sections answers a lookup. */
static void notify_used(PACTIVATION_CONTEXT context)
{
    if (is_pseudo(context) || context == &g_empty_context || !context->Notification ||
        (context->Flags & ACTCTX_FLAG_USED_NOTIFIED))
        return;
    BOOLEAN disable = FALSE;
    context->Notification(ACTIVATION_CONTEXT_NOTIFICATION_USED, context, context->Data, context->NotificationContext,
                          nullptr, &disable);
    context->Flags |= ACTCTX_FLAG_USED_NOTIFIED;
}

static NTSTATUS check_find_parameters(ULONG flags, PACTIVATION_CONTEXT_SECTION_KEYED_DATA data)
{
    if (flags & ~7u)
        return STATUS_INVALID_PARAMETER;
    if (!data)
        return (flags & 7) ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    if (data->Size < offsetof(ACTIVATION_CONTEXT_SECTION_KEYED_DATA, AssemblyRosterIndex))
        return STATUS_INVALID_PARAMETER;
    if ((flags & FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_FLAGS) &&
        data->Size < offsetof(ACTIVATION_CONTEXT_SECTION_KEYED_DATA, AssemblyMetadata))
        return STATUS_INVALID_PARAMETER;
    if ((flags & FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_ASSEMBLY_METADATA) &&
        data->Size < sizeof(ACTIVATION_CONTEXT_SECTION_KEYED_DATA))
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

/* shared by both finds: fills in the section, context and metadata the caller asked for. */
static NTSTATUS fill_found(ULONG flags, PACTIVATION_CONTEXT_SECTION_KEYED_DATA out, const SEARCH *s,
                           const void *section, ULONG length, ULONG user_offset, ULONG user_size)
{
    PACTIVATION_CONTEXT context = s->Context == ACTCTX_SYSTEM_DEFAULT ? nullptr : s->Context;
    if (user_offset) {
        out->SectionGlobalData = (PVOID)((const UCHAR *)section + user_offset);
        out->SectionGlobalDataLength = user_size;
    }
    out->SectionBase = (PVOID)section;
    out->SectionTotalLength = length;
    if (flags & FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_ACTIVATION_CONTEXT) {
        RtlAddRefActivationContext(context);
        out->ActivationContext = context;
    }
    if (flags & FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_FLAGS)
        out->Flags = (s->Context == nullptr ? ACTIVATION_CONTEXT_SECTION_KEYED_DATA_FLAG_FOUND_IN_PROCESS_DEFAULT : 0) |
            (s->Context == ACTCTX_SYSTEM_DEFAULT ? ACTIVATION_CONTEXT_SECTION_KEYED_DATA_FLAG_FOUND_IN_SYSTEM_DEFAULT : 0);
    if (!(flags & FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_ASSEMBLY_METADATA))
        return STATUS_SUCCESS;
    const ACTCTX_DATA *data = s->Data;
    const ACTCTX_ROSTER_HEADER *roster = (const void *)((const UCHAR *)data + data->AssemblyRosterOffset);
    if (out->AssemblyRosterIndex < roster->EntryCount) {
        const ACTCTX_ROSTER_ENTRY *e =
            (const ACTCTX_ROSTER_ENTRY *)((const UCHAR *)data + roster->FirstEntryOffset) + out->AssemblyRosterIndex;
        out->AssemblyMetadata.Information = (PVOID)((const UCHAR *)data + e->AssemblyInformationOffset);
    }
    const void *info;
    ULONG info_length;
    NTSTATUS status = locate_section(data, nullptr, ACTIVATION_CONTEXT_SECTION_ASSEMBLY_INFORMATION, &info, &info_length);
    if (!NT_SUCCESS(status))
        return status;
    out->AssemblyMetadata.SectionBase = (PVOID)info;
    out->AssemblyMetadata.SectionLength = info_length;
    const ACTCTX_STRING_SECTION_HEADER *ih = info;
    if (info_length < sizeof(*ih) || ih->Magic != ACTCTX_STRING_SECTION_MAGIC)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    if (ih->UserDataOffset) {
        out->AssemblyMetadata.SectionGlobalDataBase = (PVOID)((const UCHAR *)info + ih->UserDataOffset);
        out->AssemblyMetadata.SectionGlobalDataLength = ih->UserDataSize;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN anything_to_search(void)
{
    PPEB peb = NtCurrentPeb();
    return peb->ActivationContextData || peb->SystemDefaultActivationContextData || active_frame();
}

NTDLL_EXPORT NTSTATUS NTAPI RtlFindActivationContextSectionString(ULONG flags, const GUID *extension, ULONG section_id,
                                                                  PCUNICODE_STRING key,
                                                                  PACTIVATION_CONTEXT_SECTION_KEYED_DATA data)
{
    if (!anything_to_search())
        return STATUS_SXS_SECTION_NOT_FOUND;
    if (!key)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = check_find_parameters(flags, data);
    if (!NT_SUCCESS(status))
        return status;
    SEARCH s = {.Extension = extension, .Section = section_id};
    const void *section;
    ULONG length;
    status = search_next(&s, &section, &length);
    if (!NT_SUCCESS(status))
        return status;
    for (;;) {
        if (length < sizeof(ACTCTX_STRING_SECTION_HEADER) ||
            ((const ACTCTX_STRING_SECTION_HEADER *)section)->Magic != ACTCTX_STRING_SECTION_MAGIC)
            return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
        status = find_in_string_section(section, length, key, data);
        if (NT_SUCCESS(status))
            break;
        if (status != STATUS_SXS_KEY_NOT_FOUND)
            return status;
        status = search_next(&s, &section, &length);
        if (status == STATUS_SXS_SECTION_NOT_FOUND)
            return STATUS_SXS_KEY_NOT_FOUND;
        if (!NT_SUCCESS(status))
            return status;
    }
    notify_used(s.Context);
    if (!data)
        return STATUS_SUCCESS;
    const ACTCTX_STRING_SECTION_HEADER *h = section;
    return fill_found(flags, data, &s, section, length, h->UserDataOffset, h->UserDataSize);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlFindActivationContextSectionGuid(ULONG flags, const GUID *extension, ULONG section_id,
                                                                const GUID *key,
                                                                PACTIVATION_CONTEXT_SECTION_KEYED_DATA data)
{
    if (!anything_to_search())
        return STATUS_SXS_SECTION_NOT_FOUND;
    if (!key)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = check_find_parameters(flags, data);
    if (!NT_SUCCESS(status))
        return status;
    SEARCH s = {.Extension = extension, .Section = section_id};
    const void *section;
    ULONG length;
    status = search_next(&s, &section, &length);
    if (!NT_SUCCESS(status))
        return status;
    for (;;) {
        if (length < sizeof(ACTCTX_GUID_SECTION_HEADER) ||
            ((const ACTCTX_GUID_SECTION_HEADER *)section)->Magic != ACTCTX_GUID_SECTION_MAGIC)
            return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
        status = find_in_guid_section(section, length, key, data);
        if (NT_SUCCESS(status))
            break;
        if (status != STATUS_SXS_KEY_NOT_FOUND)
            return status;
        status = search_next(&s, &section, &length);
        if (status == STATUS_SXS_SECTION_NOT_FOUND)
            return STATUS_SXS_KEY_NOT_FOUND;
        if (!NT_SUCCESS(status))
            return status;
    }
    notify_used(s.Context);
    if (!data)
        return STATUS_SUCCESS;
    const ACTCTX_GUID_SECTION_HEADER *h = section;
    return fill_found(flags, data, &s, section, length, h->UserDataOffset, h->UserDataSize);
}

/* ------------------------------------------------------------------ context objects */

NTDLL_EXPORT NTSTATUS NTAPI RtlCreateActivationContext(ULONG flags, const void *data, ULONG extra_bytes,
                                                       PACTIVATION_CONTEXT_NOTIFY_ROUTINE notification,
                                                       PVOID notification_context, PACTIVATION_CONTEXT *context)
{
    if (data == &g_empty_data)
        return STATUS_INVALID_PARAMETER;
    if (context)
        *context = nullptr;
    if (flags || !data || extra_bytes > 0x10000 || !context)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = validate_data(data);
    if (!NT_SUCCESS(status))
        return status;
    PACTIVATION_CONTEXT c = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*c) + extra_bytes);
    if (!c)
        return STATUS_NO_MEMORY;
    c->RefCount = 1;
    c->Data = data;
    c->Notification = notification;
    c->NotificationContext = notification_context;
    *context = c;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT void NTAPI RtlAddRefActivationContext(PACTIVATION_CONTEXT context)
{
    if (is_pseudo(context))
        return;
    for (;;) {
        LONG count = context->RefCount;
        if (count == 0x7FFFFFFF ||
            __atomic_compare_exchange_n(&context->RefCount, &count, count + 1, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
            return;
    }
}

NTDLL_EXPORT void NTAPI RtlReleaseActivationContext(PACTIVATION_CONTEXT context)
{
    if (is_pseudo(context) || (ULONG)(context->RefCount - 1) > 0x7FFFFFFDu)
        return;
    LONG count;
    for (;;) {
        count = context->RefCount;
        if (count == 0x7FFFFFFF)
            return;
        if (__atomic_compare_exchange_n(&context->RefCount, &count, count - 1, FALSE, __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED))
            break;
    }
    if (count != 1)
        return;
    if (context->Notification) {
        BOOLEAN disable = FALSE;
        context->Notification(ACTIVATION_CONTEXT_NOTIFICATION_DESTROY, context, context->Data,
                              context->NotificationContext, nullptr, &disable);
    }
    if (!(context->Flags & ACTCTX_FLAG_STATIC))
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, context);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlZombifyActivationContext(PACTIVATION_CONTEXT context)
{
    if (is_pseudo(context))
        return STATUS_INVALID_PARAMETER;
    if (!(context->Flags & ACTCTX_FLAG_ZOMBIE)) {
        if (context->Notification) {
            BOOLEAN disable = FALSE;
            context->Notification(ACTIVATION_CONTEXT_NOTIFICATION_ZOMBIFY, context, context->Data,
                                  context->NotificationContext, nullptr, &disable);
        }
        context->Flags |= ACTCTX_FLAG_ZOMBIE;
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ stacks */

static void init_stack(PACTIVATION_CONTEXT_STACK stack, ULONG flags)
{
    stack->ActiveFrame = nullptr;
    InitializeListHead(&stack->FrameListCache);
    stack->Flags = flags;
    stack->NextCookieSequenceNumber = 1;
    stack->StackId = (ULONG)__atomic_add_fetch(&g_next_stack_id, 1, __ATOMIC_RELAXED) * 0x9E3779B1u >> 4;
}

/* contexts waiting for the threads that inherit them. */
typedef struct INHERITANCE {
    struct INHERITANCE *Next;
    HANDLE ThreadId;
    PACTIVATION_CONTEXT Context;
} INHERITANCE;
static INHERITANCE *g_inheritances;
static RTL_SRWLOCK g_inheritance_lock;

NTDLL_EXPORT NTSTATUS NTAPI RtlInheritActivationContext(HANDLE thread_id, PACTIVATION_CONTEXT context)
{
    INHERITANCE *i = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, sizeof(*i));
    if (!i)
        return STATUS_NO_MEMORY;
    RtlAddRefActivationContext(context);
    i->ThreadId = thread_id;
    i->Context = context;
    RtlAcquireSRWLockExclusive(&g_inheritance_lock);
    i->Next = g_inheritances;
    g_inheritances = i;
    RtlReleaseSRWLockExclusive(&g_inheritance_lock);
    return STATUS_SUCCESS;
}

static void inherit(PTEB teb)
{
    if (!g_inheritances)
        return;
    INHERITANCE *found = nullptr;
    RtlAcquireSRWLockExclusive(&g_inheritance_lock);
    for (INHERITANCE **link = &g_inheritances; *link; link = &(*link)->Next) {
        if ((*link)->ThreadId == teb->ClientId.UniqueThread) {
            found = *link;
            *link = found->Next;
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_inheritance_lock);
    if (!found)
        return;
    ULONG_PTR cookie;
    RtlActivateActivationContextEx(RTL_ACTIVATE_ACTIVATION_CONTEXT_EX_FLAG_RELEASE_ON_STACK_DEALLOCATION, teb,
                                   found->Context, &cookie);
    RtlReleaseActivationContext(found->Context);
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, found);
}

void LdrpSxsInitializeThread(void)
{
    PTEB teb = NtCurrentTeb();
    if (teb->ActivationContextStackPointer)
        return;
    PACTIVATION_CONTEXT_STACK stack = (PACTIVATION_CONTEXT_STACK)teb->ActivationStack;
    /* flag 2: the stack lives in the teb, freeing it leaves the memory alone. */
    init_stack(stack, 2);
    teb->ActivationContextStackPointer = stack;
    inherit(teb);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlAllocateActivationContextStack(PACTIVATION_CONTEXT_STACK *stack)
{
    if (*stack)
        return STATUS_SUCCESS;
    PACTIVATION_CONTEXT_STACK s = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, sizeof(*s));
    if (!s)
        return STATUS_NO_MEMORY;
    init_stack(s, 0);
    *stack = s;
    return STATUS_SUCCESS;
}

static void pop_frames(PACTIVATION_CONTEXT_STACK stack, PRTL_ACTIVATION_CONTEXT_STACK_FRAME until)
{
    PRTL_ACTIVATION_CONTEXT_STACK_FRAME frame = stack->ActiveFrame;
    while (frame && frame != until) {
        PRTL_ACTIVATION_CONTEXT_STACK_FRAME previous = frame->Previous;
        if (frame->Flags & FRAME_RELEASE)
            RtlReleaseActivationContext(frame->ActivationContext);
        if (frame->Flags & FRAME_HEAP)
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, frame);
        frame = previous;
    }
    stack->ActiveFrame = until;
}

NTDLL_EXPORT void NTAPI RtlFreeActivationContextStack(PACTIVATION_CONTEXT_STACK stack)
{
    if (!stack)
        return;
    pop_frames(stack, nullptr);
    if (!(stack->Flags & 2))
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, stack);
}

NTDLL_EXPORT void NTAPI RtlFreeThreadActivationContextStack(void)
{
    PTEB teb = NtCurrentTeb();
    RtlFreeActivationContextStack(teb->ActivationContextStackPointer);
    teb->ActivationContextStackPointer = nullptr;
    LdrpSxsInitializeThread();
}

/* ------------------------------------------------------------------ activation */

static ULONG_PTR next_cookie(PACTIVATION_CONTEXT_STACK stack)
{
    ULONG sequence = stack->NextCookieSequenceNumber++;
#ifdef _WIN64
    return (ULONG_PTR)COOKIE_TYPE_DEFAULT << 60 | (ULONG_PTR)(stack->StackId & 0x0FFFFFFF) << 32 | sequence;
#else
    return (ULONG_PTR)((stack->StackId & 0xFFF) | (COOKIE_TYPE_DEFAULT << 12)) << 16 | (sequence & 0xFFFF);
#endif
}

static BOOLEAN cookie_matches_stack(ULONG_PTR cookie, PACTIVATION_CONTEXT_STACK stack)
{
#ifdef _WIN64
    return cookie >> 60 == COOKIE_TYPE_DEFAULT && !(((cookie >> 32) ^ stack->StackId) & 0x0FFFFFFF);
#else
    return (cookie & 0xF0000000) == COOKIE_TYPE_DEFAULT << 28 && !(((cookie >> 16) ^ stack->StackId) & 0xFFF);
#endif
}

NTDLL_EXPORT NTSTATUS NTAPI RtlActivateActivationContextEx(ULONG flags, PTEB teb, PACTIVATION_CONTEXT context,
                                                           PULONG_PTR cookie)
{
    if (cookie)
        *cookie = 0;
    if (context == ACTCTX_EMPTY)
        context = &g_empty_context;
    if ((flags & ~RTL_ACTIVATE_ACTIVATION_CONTEXT_EX_FLAG_RELEASE_ON_STACK_DEALLOCATION) || !teb ||
        context == (PACTIVATION_CONTEXT)(LONG_PTR)-1 || !cookie)
        return STATUS_INVALID_PARAMETER;
    PACTIVATION_CONTEXT_STACK stack = teb_stack(teb);
    if (!stack)
        return STATUS_INVALID_PARAMETER;
    HEAP_FRAME *frame = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, sizeof(*frame));
    if (!frame)
        return STATUS_NO_MEMORY;
    frame->Frame.Flags = FRAME_HEAP | FRAME_CALLER;
    if (flags & RTL_ACTIVATE_ACTIVATION_CONTEXT_EX_FLAG_RELEASE_ON_STACK_DEALLOCATION) {
        frame->Frame.Flags |= FRAME_RELEASE | FRAME_RELEASE_HIGH;
        RtlAddRefActivationContext(context);
    }
    frame->Frame.Previous = stack->ActiveFrame;
    frame->Frame.ActivationContext = context;
    frame->Cookie = next_cookie(stack);
    *cookie = frame->Cookie;
    stack->ActiveFrame = &frame->Frame;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlActivateActivationContext(ULONG flags, PACTIVATION_CONTEXT context, PULONG_PTR cookie)
{
    if (cookie)
        *cookie = 0;
    if (flags || !cookie)
        return STATUS_INVALID_PARAMETER;
    return RtlActivateActivationContextEx(0, NtCurrentTeb(), context, cookie);
}

NTDLL_EXPORT void NTAPI RtlDeactivateActivationContext(ULONG flags, ULONG_PTR cookie)
{
    if (flags & ~RTL_DEACTIVATE_ACTIVATION_CONTEXT_FLAG_FORCE_EARLY_DEACTIVATION)
        RtlRaiseStatus(STATUS_INVALID_PARAMETER);
    if (!cookie)
        return;
    PACTIVATION_CONTEXT_STACK stack = current_stack();
    if (!stack || !cookie_matches_stack(cookie, stack))
        RtlRaiseStatus(STATUS_INVALID_PARAMETER);
    PRTL_ACTIVATION_CONTEXT_STACK_FRAME top = stack->ActiveFrame;
    if (!top)
        return;
    PRTL_ACTIVATION_CONTEXT_STACK_FRAME frame = top;
    ULONG depth = 0;
    while (frame && !((frame->Flags & FRAME_HEAP) && ((HEAP_FRAME *)frame)->Cookie == cookie)) {
        frame = frame->Previous;
        depth++;
    }
    if (!frame)
        RtlRaiseStatus(STATUS_SXS_INVALID_DEACTIVATION);
    /* frames above the cookie's: Windows raises a continuable exception, then pops them all. */
    if (frame != top)
        raise(STATUS_SXS_EARLY_DEACTIVATION, 0, 3, depth - 1, (ULONG_PTR)frame, (ULONG_PTR)top, 0);
    pop_frames(stack, frame->Previous);
}

/* a caller allocated frame is sound when its complements still match. */
static BOOLEAN frame_sound(PRTL_ACTIVATION_CONTEXT_STACK_FRAME frame)
{
    if ((frame->Flags & (FRAME_NOT_PUSHED | FRAME_CALLER | FRAME_DEACTIVATED)) != FRAME_CALLER)
        return FALSE;
    if (frame->Flags & FRAME_HEAP)
        return TRUE;
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED ext =
        CONTAINING_RECORD(frame, RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED, Frame);
    if (ext->Size < sizeof(*ext))
        return TRUE;
    return (ULONG_PTR)ext->Extra1 == ~(ULONG_PTR)frame->Previous &&
        (ULONG_PTR)ext->Extra2 == ~(ULONG_PTR)frame->ActivationContext;
}

static void activate_fast(PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame,
                          PACTIVATION_CONTEXT context, PVOID caller)
{
    PACTIVATION_CONTEXT_STACK stack = current_stack();
    if (!stack) {
        frame->Frame.Flags = FRAME_CALLER | FRAME_NOT_PUSHED;
        return;
    }
    PRTL_ACTIVATION_CONTEXT_STACK_FRAME top = stack->ActiveFrame;
    if (frame->Size >= sizeof(*frame)) {
        frame->Extra1 = (PVOID)~(ULONG_PTR)top;
        frame->Extra2 = (PVOID)~(ULONG_PTR)context;
        frame->Extra3 = caller;
    }
    if (top && (top->Flags & (FRAME_NOT_PUSHED | FRAME_CALLER | FRAME_DEACTIVATED)) != FRAME_CALLER) {
        raise(STATUS_SXS_CORRUPT_ACTIVATION_STACK, EXCEPTION_NONCONTINUABLE, 4, (ULONG_PTR)stack, (ULONG_PTR)top,
              (ULONG_PTR)top, top->Flags);
        return;
    }
    frame->Frame.Previous = top;
    frame->Frame.ActivationContext = context;
    frame->Frame.Flags = FRAME_CALLER;
    /* activating what is already active pushes nothing. */
    if (top ? top->ActivationContext == context : !context) {
        frame->Frame.Flags |= FRAME_NOT_PUSHED;
        return;
    }
    stack->ActiveFrame = &frame->Frame;
}

static void deactivate_fast(PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame, PVOID caller)
{
    PACTIVATION_CONTEXT_STACK stack = current_stack();
    PRTL_ACTIVATION_CONTEXT_STACK_FRAME top = stack ? stack->ActiveFrame : nullptr;
    ULONG flags = frame->Frame.Flags;
    if (flags & FRAME_DEACTIVATED) {
        raise(STATUS_SXS_MULTIPLE_DEACTIVATION, 0, 3, 0, (ULONG_PTR)&frame->Frame, (ULONG_PTR)top, 0);
        return;
    }
    if (!(flags & FRAME_CALLER)) {
        raise(STATUS_SXS_INVALID_DEACTIVATION, EXCEPTION_NONCONTINUABLE, 3, 0, (ULONG_PTR)&frame->Frame,
              (ULONG_PTR)top, 0);
        return;
    }
    if (frame->Size >= sizeof(*frame) && ((ULONG_PTR)frame->Extra1 != ~(ULONG_PTR)frame->Frame.Previous ||
                                          (ULONG_PTR)frame->Extra2 != ~(ULONG_PTR)frame->Frame.ActivationContext)) {
        raise(STATUS_SXS_CORRUPT_ACTIVATION_STACK, EXCEPTION_NONCONTINUABLE, 4, (ULONG_PTR)stack, (ULONG_PTR)top,
              (ULONG_PTR)&frame->Frame, flags);
        return;
    }
    if (!(flags & FRAME_NOT_PUSHED)) {
        if (top && !frame_sound(top)) {
            raise(STATUS_SXS_CORRUPT_ACTIVATION_STACK, EXCEPTION_NONCONTINUABLE, 4, (ULONG_PTR)stack, (ULONG_PTR)top,
                  (ULONG_PTR)top, top->Flags);
            return;
        }
        if (top != &frame->Frame) {
            ULONG depth = 0;
            PRTL_ACTIVATION_CONTEXT_STACK_FRAME f = top;
            while (f && f != &frame->Frame) {
                f = f->Previous;
                depth++;
            }
            if (!f)
                raise(STATUS_SXS_INVALID_DEACTIVATION, EXCEPTION_NONCONTINUABLE, 3, depth, (ULONG_PTR)&frame->Frame,
                      (ULONG_PTR)top, 0);
            else
                raise(depth ? STATUS_SXS_EARLY_DEACTIVATION : STATUS_SXS_MULTIPLE_DEACTIVATION, 0, 3, depth,
                      (ULONG_PTR)&frame->Frame, (ULONG_PTR)top, 0);
        }
        stack->ActiveFrame = frame->Frame.Previous;
    }
    frame->Frame.Flags |= FRAME_DEACTIVATED;
    if (frame->Size >= sizeof(*frame))
        frame->Extra4 = caller;
}

#ifdef __i386__
NTDLL_EXPORT void __fastcall RtlActivateActivationContextUnsafeFast(
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame, PACTIVATION_CONTEXT context)
#else
NTDLL_EXPORT void NTAPI RtlActivateActivationContextUnsafeFast(
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame, PACTIVATION_CONTEXT context)
#endif
{
    activate_fast(frame, context, __builtin_return_address(0));
}

#ifdef __i386__
NTDLL_EXPORT void __fastcall RtlDeactivateActivationContextUnsafeFast(
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame)
#else
NTDLL_EXPORT void NTAPI RtlDeactivateActivationContextUnsafeFast(
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame)
#endif
{
    deactivate_fast(frame, __builtin_return_address(0));
}

void LdrpSxsActivate(LDRP_SXS_FRAME *frame, PACTIVATION_CONTEXT context)
{
    memset(frame, 0, sizeof(*frame));
    frame->Size = sizeof(*frame);
    frame->Format = RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER;
    if (current_stack())
        activate_fast(frame, context, __builtin_return_address(0));
    else
        frame->Frame.Flags = FRAME_CALLER | FRAME_NOT_PUSHED;
}

void LdrpSxsDeactivate(LDRP_SXS_FRAME *frame)
{
    if (current_stack())
        deactivate_fast(frame, __builtin_return_address(0));
}

NTDLL_EXPORT NTSTATUS NTAPI RtlGetActiveActivationContext(PACTIVATION_CONTEXT *context)
{
    if (!context)
        return STATUS_INVALID_PARAMETER;
    *context = nullptr;
    PACTIVATION_CONTEXT_STACK stack = current_stack();
    PRTL_ACTIVATION_CONTEXT_STACK_FRAME top = stack ? stack->ActiveFrame : nullptr;
    if (!top)
        return STATUS_SUCCESS;
    if (!frame_sound(top)) {
        raise(STATUS_SXS_CORRUPT_ACTIVATION_STACK, EXCEPTION_NONCONTINUABLE, 4, (ULONG_PTR)stack, (ULONG_PTR)top,
              (ULONG_PTR)top, top->Flags);
        return STATUS_INTERNAL_ERROR;
    }
    RtlAddRefActivationContext(top->ActivationContext);
    *context = top->ActivationContext;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlIsActivationContextActive(PACTIVATION_CONTEXT context)
{
    for (PRTL_ACTIVATION_CONTEXT_STACK_FRAME f = active_frame(); f; f = f->Previous)
        if (f->ActivationContext == context)
            return TRUE;
    return FALSE;
}

PACTIVATION_CONTEXT LdrpSxsActiveContext(void)
{
    PRTL_ACTIVATION_CONTEXT_STACK_FRAME top = active_frame();
    return top ? top->ActivationContext : nullptr;
}

/* ------------------------------------------------------------------ queries */

/* the assembly information section with its user data, NULL when the data has none. */
static NTSTATUS assembly_section(const ACTCTX_DATA *data, const ACTCTX_STRING_SECTION_HEADER **section,
                                 ULONG *length, const ACTCTX_ASSEMBLY_GLOBAL_INFORMATION **global)
{
    *section = nullptr;
    *global = nullptr;
    NTSTATUS status =
        locate_section(data, nullptr, ACTIVATION_CONTEXT_SECTION_ASSEMBLY_INFORMATION, (const void **)section, length);
    if (!NT_SUCCESS(status))
        return status;
    const ACTCTX_STRING_SECTION_HEADER *h = *section;
    if (*length < sizeof(*h) || h->Magic != ACTCTX_STRING_SECTION_MAGIC || h->HeaderSize < sizeof(*h) ||
        h->UserDataSize < sizeof(ACTCTX_ASSEMBLY_GLOBAL_INFORMATION) || h->UserDataOffset + h->UserDataSize > *length)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    *global = (const void *)((const UCHAR *)h + h->UserDataOffset);
    return STATUS_SUCCESS;
}

static const ACTCTX_ROSTER_HEADER *roster_of(const ACTCTX_DATA *data)
{
    return (const void *)((const UCHAR *)data + data->AssemblyRosterOffset);
}

static const ACTCTX_ROSTER_ENTRY *roster_entry(const ACTCTX_DATA *data, ULONG index)
{
    const ACTCTX_ROSTER_HEADER *r = roster_of(data);
    return (const ACTCTX_ROSTER_ENTRY *)((const UCHAR *)data + r->FirstEntryOffset) + index;
}

static ULONG root_index(const ACTCTX_DATA *data)
{
    const ACTCTX_ROSTER_HEADER *r = roster_of(data);
    for (ULONG i = 1; i < r->EntryCount; i++)
        if (roster_entry(data, i)->Flags & ACTCTX_ROSTER_ENTRY_ROOT)
            return i;
    return 0;
}

/* copies a counted string out of the data with a terminator and advances the cursor. */
static PCWSTR copy_out(UCHAR **cursor, const void *base, ULONG offset, ULONG length)
{
    if (!length)
        return nullptr;
    PWSTR s = (PWSTR)*cursor;
    memcpy(s, (const UCHAR *)base + offset, length);
    s[length / sizeof(WCHAR)] = 0;
    *cursor += length + sizeof(WCHAR);
    return s;
}

static NTSTATUS query_detailed(const ACTCTX_DATA *data, PVOID buffer, SIZE_T size, PSIZE_T written)
{
    const ACTCTX_STRING_SECTION_HEADER *section;
    const ACTCTX_ASSEMBLY_GLOBAL_INFORMATION *global;
    ULONG length;
    NTSTATUS status = assembly_section(data, &section, &length, &global);
    ACTIVATION_CONTEXT_DETAILED_INFORMATION *out = buffer;
    if (status == STATUS_SXS_SECTION_NOT_FOUND) {
        if (size < sizeof(*out))
            return STATUS_BUFFER_TOO_SMALL;
        memset(out, 0, sizeof(*out));
        if (written)
            *written = sizeof(*out);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(status))
        return status;
    ULONG root = root_index(data);
    if (!root)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    const ACTCTX_ASSEMBLY_INFORMATION *info =
        (const void *)((const UCHAR *)data + roster_entry(data, root)->AssemblyInformationOffset);
    SIZE_T needed = sizeof(*out);
    if (info->ManifestPathLength)
        needed += info->ManifestPathLength + sizeof(WCHAR);
    if (info->PolicyPathLength)
        needed += info->PolicyPathLength + sizeof(WCHAR);
    if (global->ApplicationDirectoryLength)
        needed += global->ApplicationDirectoryLength + sizeof(WCHAR);
    if (written)
        *written = needed;
    if (needed > size)
        return STATUS_BUFFER_TOO_SMALL;
    out->dwFlags = data->Flags;
    out->ulFormatVersion = data->FormatVersion;
    out->ulAssemblyCount = roster_of(data)->EntryCount - 1;
    out->ulRootManifestPathType = info->ManifestPathType;
    out->ulRootManifestPathChars = info->ManifestPathLength / sizeof(WCHAR);
    out->ulRootConfigurationPathType = info->PolicyPathType;
    out->ulRootConfigurationPathChars = info->PolicyPathLength / sizeof(WCHAR);
    out->ulAppDirPathType = global->ApplicationDirectoryPathType;
    out->ulAppDirPathChars = global->ApplicationDirectoryLength / sizeof(WCHAR);
    UCHAR *cursor = (UCHAR *)(out + 1);
    out->lpRootManifestPath = copy_out(&cursor, section, info->ManifestPathOffset, info->ManifestPathLength);
    out->lpRootConfigurationPath = copy_out(&cursor, section, info->PolicyPathOffset, info->PolicyPathLength);
    out->lpAppDirPath = copy_out(&cursor, section, global->ApplicationDirectoryOffset, global->ApplicationDirectoryLength);
    return STATUS_SUCCESS;
}

static NTSTATUS query_assembly(const ACTCTX_DATA *data, ULONG index, PVOID buffer, SIZE_T size, PSIZE_T written)
{
    if (written)
        *written = 0;
    const ACTCTX_STRING_SECTION_HEADER *section;
    const ACTCTX_ASSEMBLY_GLOBAL_INFORMATION *global;
    ULONG length;
    NTSTATUS status = assembly_section(data, &section, &length, &global);
    ACTIVATION_CONTEXT_ASSEMBLY_DETAILED_INFORMATION *out = buffer;
    if (status == STATUS_SXS_SECTION_NOT_FOUND) {
        if (size < sizeof(*out))
            return STATUS_BUFFER_TOO_SMALL;
        memset(out, 0, sizeof(*out));
        if (written)
            *written = sizeof(*out);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(status))
        return status;
    /* the index counts from 1: entry 0 of the roster is the invalid one. */
    if (!index || index >= roster_of(data)->EntryCount)
        return STATUS_INVALID_PARAMETER;
    const ACTCTX_ASSEMBLY_INFORMATION *info =
        (const void *)((const UCHAR *)data + roster_entry(data, index)->AssemblyInformationOffset);
    SIZE_T needed = sizeof(*out);
    if (info->EncodedAssemblyIdentityLength)
        needed += info->EncodedAssemblyIdentityLength + sizeof(WCHAR);
    if (info->ManifestPathLength)
        needed += info->ManifestPathLength + sizeof(WCHAR);
    if (info->PolicyPathLength)
        needed += info->PolicyPathLength + sizeof(WCHAR);
    if (info->AssemblyDirectoryNameLength)
        needed += info->AssemblyDirectoryNameLength + sizeof(WCHAR);
    if (written)
        *written = needed;
    if (needed > size)
        return STATUS_BUFFER_TOO_SMALL;
    memset(out, 0, sizeof(*out));
    out->ulFlags = info->Flags;
    out->ulEncodedAssemblyIdentityLength = info->EncodedAssemblyIdentityLength;
    out->ulManifestPathType = info->ManifestPathType;
    out->ulManifestPathLength = info->ManifestPathLength;
    out->liManifestLastWriteTime = info->ManifestLastWriteTime;
    out->ulPolicyPathType = info->PolicyPathType;
    out->ulPolicyPathLength = info->PolicyPathLength;
    out->liPolicyLastWriteTime = info->PolicyLastWriteTime;
    out->ulMetadataSatelliteRosterIndex = info->MetadataSatelliteRosterIndex;
    out->ulManifestVersionMajor = info->ManifestVersionMajor;
    out->ulManifestVersionMinor = info->ManifestVersionMinor;
    out->ulPolicyVersionMajor = info->PolicyVersionMajor;
    out->ulPolicyVersionMinor = info->PolicyVersionMinor;
    out->ulAssemblyDirectoryNameLength = info->AssemblyDirectoryNameLength;
    out->ulFileCount = info->NumOfFilesInAssembly;
    UCHAR *cursor = (UCHAR *)(out + 1);
    out->lpAssemblyEncodedAssemblyIdentity =
        copy_out(&cursor, section, info->EncodedAssemblyIdentityOffset, info->EncodedAssemblyIdentityLength);
    out->lpAssemblyManifestPath = copy_out(&cursor, section, info->ManifestPathOffset, info->ManifestPathLength);
    out->lpAssemblyPolicyPath = copy_out(&cursor, section, info->PolicyPathOffset, info->PolicyPathLength);
    out->lpAssemblyDirectoryName =
        copy_out(&cursor, section, info->AssemblyDirectoryNameOffset, info->AssemblyDirectoryNameLength);
    return STATUS_SUCCESS;
}

/* the dll redirection elements of one assembly, in section order. */
static NTSTATUS query_file(const ACTCTX_DATA *data, const ACTIVATION_CONTEXT_QUERY_INDEX *index, PVOID buffer,
                           SIZE_T size, PSIZE_T written)
{
    if (written)
        *written = 0;
    /* this class counts assemblies from 0, unlike the assembly class. */
    if (!data->AssemblyRosterOffset || index->ulAssemblyIndex >= roster_of(data)->EntryCount - 1)
        return STATUS_INVALID_PARAMETER;
    const ACTCTX_STRING_SECTION_HEADER *h;
    ULONG length;
    NTSTATUS status = locate_section(data, nullptr, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION, (const void **)&h,
                                     &length);
    ASSEMBLY_FILE_DETAILED_INFORMATION *out = buffer;
    if (status == STATUS_SXS_SECTION_NOT_FOUND) {
        if (size < sizeof(*out))
            return STATUS_BUFFER_TOO_SMALL;
        memset(out, 0, sizeof(*out));
        if (written)
            *written = sizeof(*out);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(status))
        return status;
    if (index->ulFileIndexInAssembly >= h->ElementCount)
        return STATUS_INVALID_PARAMETER;
    const ACTCTX_STRING_SECTION_ENTRY *entries = (const void *)((const UCHAR *)h + h->ElementListOffset);
    ULONG seen = 0;
    for (ULONG i = 0; i < h->ElementCount; i++) {
        const ACTCTX_STRING_SECTION_ENTRY *e = &entries[i];
        if (e->AssemblyRosterIndex != index->ulAssemblyIndex + 1)
            continue;
        if (seen++ != index->ulFileIndexInAssembly || !e->Offset)
            continue;
        const ACTCTX_DLL_REDIRECTION *r = (const void *)((const UCHAR *)h + e->Offset);
        SIZE_T needed = sizeof(*out);
        if (e->KeyLength)
            needed += e->KeyLength + sizeof(WCHAR);
        if (r->TotalPathLength)
            needed += r->TotalPathLength + sizeof(WCHAR);
        if (written)
            *written = needed;
        if (needed > size)
            return STATUS_BUFFER_TOO_SMALL;
        out->ulFlags = r->Flags;
        out->ulFilenameLength = e->KeyLength;
        out->ulPathLength = r->TotalPathLength;
        UCHAR *cursor = (UCHAR *)(out + 1);
        out->lpFileName = copy_out(&cursor, h, e->KeyOffset, e->KeyLength);
        out->lpFilePath = nullptr;
        if (r->TotalPathLength && r->PathSegmentOffset) {
            PWSTR path = (PWSTR)cursor;
            out->lpFilePath = path;
            const ACTCTX_DLL_PATH_SEGMENT *seg = (const void *)((const UCHAR *)h + r->PathSegmentOffset);
            for (ULONG k = 0; k < r->PathSegmentCount; k++) {
                memcpy(cursor, (const UCHAR *)h + seg[k].Offset, seg[k].Length);
                cursor += seg[k].Length;
            }
            *(PWSTR)cursor = 0;
            (void)path;
        }
        return STATUS_SUCCESS;
    }
    return STATUS_SXS_CORRUPTION;
}

static NTSTATUS query_run_level(const ACTCTX_DATA *data, ACTIVATION_CONTEXT_RUN_LEVEL_INFORMATION *out)
{
    const void *section;
    ULONG length;
    NTSTATUS status = locate_section(data, nullptr, ACTIVATION_CONTEXT_SECTION_ASSEMBLY_INFORMATION, &section, &length);
    if (status == STATUS_SXS_SECTION_NOT_FOUND)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(status))
        return status;
    ULONG root = root_index(data);
    if (!root)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    const ACTCTX_ASSEMBLY_INFORMATION *info =
        (const void *)((const UCHAR *)data + roster_entry(data, root)->AssemblyInformationOffset);
    if (info->Size != sizeof(*info))
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    out->ulFlags = 0;
    out->RunLevel = (ACTCTX_REQUESTED_RUN_LEVEL)info->RunLevel;
    out->UiAccess = info->UiAccess;
    return STATUS_SUCCESS;
}

static NTSTATUS query_compatibility(const ACTCTX_DATA *data, PVOID buffer, SIZE_T size, PSIZE_T written)
{
    const void *section = nullptr;
    ULONG length = 0;
    NTSTATUS status = data ? locate_section(data, nullptr, ACTIVATION_CONTEXT_SECTION_COMPATIBILITY_INFO, &section,
                                            &length)
                           : STATUS_SXS_SECTION_NOT_FOUND;
    if (status == STATUS_SXS_SECTION_NOT_FOUND) {
        if (written)
            *written = 0;
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(status))
        return status;
    if (written)
        *written = length;
    if (length > size)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy(buffer, section, length);
    return STATUS_SUCCESS;
}

static NTSTATUS query_resource_name(const ACTCTX_DATA *data, PVOID buffer, SIZE_T size, PSIZE_T written)
{
    if (written)
        *written = sizeof(ULONG);
    if (size < sizeof(ULONG))
        return STATUS_BUFFER_TOO_SMALL;
    const ACTCTX_STRING_SECTION_HEADER *section;
    const ACTCTX_ASSEMBLY_GLOBAL_INFORMATION *global;
    ULONG length;
    NTSTATUS status = data ? assembly_section(data, &section, &length, &global) : STATUS_SXS_SECTION_NOT_FOUND;
    if (status == STATUS_SXS_SECTION_NOT_FOUND) {
        *(PULONG)buffer = 0;
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(status))
        return status;
    *(PULONG)buffer = global->ResourceName;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryInformationActivationContext(ULONG flags, PACTIVATION_CONTEXT context,
                                                                 PVOID subinstance, ULONG info_class, PVOID buffer,
                                                                 SIZE_T size, PSIZE_T written)
{
    PLDR_DATA_TABLE_ENTRY module = nullptr;
    if (written)
        *written = 0;
    if (flags & 0x3FFFFFF8)
        return STATUS_INVALID_PARAMETER_1;
    /* like Windows, NO_ADDREF is only accepted for classes that return nothing to release. */
    if ((flags & RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_NO_ADDREF) &&
        info_class != ActivationContextBasicInformation && info_class != RunlevelInformationInActivationContext &&
        info_class != CompatibilityInformationInActivationContext)
        return STATUS_INVALID_PARAMETER_1;
    if (info_class - 1 > 6)
        return STATUS_INVALID_PARAMETER_3;
    if (size && !buffer)
        return STATUS_INVALID_PARAMETER_4;
    if (!size && !written)
        return STATUS_INVALID_PARAMETER_6;
    switch (flags & 7) {
    case 0:
        break;
    case RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_USE_ACTIVE_ACTIVATION_CONTEXT:
        if (context)
            return STATUS_INVALID_PARAMETER_2;
        context = LdrpSxsActiveContext();
        break;
    case RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_ACTIVATION_CONTEXT_IS_ADDRESS:
    case RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_ACTIVATION_CONTEXT_IS_MODULE:
        if (!context)
            return STATUS_INVALID_PARAMETER_2;
        if (!NT_SUCCESS(LdrFindEntryForAddress((PVOID)context, &module)) ||
            ((flags & RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_ACTIVATION_CONTEXT_IS_MODULE) &&
             module->DllBase != (PVOID)context))
            return STATUS_DLL_NOT_FOUND;
        context = module->EntryPointActivationContext;
        break;
    default:
        return STATUS_INVALID_PARAMETER_1;
    }
    const ACTCTX_DATA *data = context_data(context);
    if (!data)
        data = &g_empty_data;
    switch (info_class) {
    case ActivationContextBasicInformation: {
        if (written)
            *written = sizeof(ACTIVATION_CONTEXT_BASIC_INFORMATION);
        if (size < sizeof(ACTIVATION_CONTEXT_BASIC_INFORMATION))
            return STATUS_BUFFER_TOO_SMALL;
        ACTIVATION_CONTEXT_BASIC_INFORMATION *out = buffer;
        out->dwFlags = data->Flags;
        if (!(flags & RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_NO_ADDREF))
            RtlAddRefActivationContext(context);
        out->hActCtx = context;
        return STATUS_SUCCESS;
    }
    case ActivationContextDetailedInformation:
        return query_detailed(data, buffer, size, written);
    case AssemblyDetailedInformationInActivationContext:
        if (!subinstance)
            return STATUS_INVALID_PARAMETER;
        return query_assembly(data, *(PULONG)subinstance, buffer, size, written);
    case FileInformationInAssemblyOfAssemblyInActivationContext:
        if (!subinstance)
            return STATUS_INVALID_PARAMETER;
        return query_file(data, subinstance, buffer, size, written);
    case RunlevelInformationInActivationContext:
        if (written)
            *written = sizeof(ACTIVATION_CONTEXT_RUN_LEVEL_INFORMATION);
        if (size < sizeof(ACTIVATION_CONTEXT_RUN_LEVEL_INFORMATION))
            return STATUS_BUFFER_TOO_SMALL;
        memset(buffer, 0, sizeof(ACTIVATION_CONTEXT_RUN_LEVEL_INFORMATION));
        return query_run_level(data, buffer);
    case CompatibilityInformationInActivationContext:
        return query_compatibility(data, buffer, size, written);
    case ActivationContextManifestResourceName:
        return query_resource_name(data, buffer, size, written);
    }
    return STATUS_INTERNAL_ERROR;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryInformationActiveActivationContext(ULONG info_class, PVOID buffer, SIZE_T size,
                                                                       PSIZE_T written)
{
    return RtlQueryInformationActivationContext(
        RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_USE_ACTIVE_ACTIVATION_CONTEXT, nullptr, nullptr, info_class,
        buffer, size, written);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryActivationContextApplicationSettings(ULONG flags, PACTIVATION_CONTEXT context,
                                                                         PCWSTR ns, PCWSTR name, PWSTR buffer,
                                                                         SIZE_T chars, PSIZE_T needed)
{
    if (!ns)
        ns = L"http://schemas.microsoft.com/SMI/2005/WindowsSettings";
    if (!buffer && chars)
        return STATUS_INVALID_PARAMETER;
    const ACTCTX_DATA *data = context_data(context);
    if (!data)
        data = &g_empty_data;
    const void *section;
    ULONG length;
    NTSTATUS status =
        locate_section(data, nullptr, ACTIVATION_CONTEXT_SECTION_APPLICATION_SETTINGS, &section, &length);
    if (status == STATUS_SXS_SECTION_NOT_FOUND)
        return STATUS_SXS_KEY_NOT_FOUND;
    if (!NT_SUCCESS(status))
        return status;
    if (length < sizeof(ACTCTX_STRING_SECTION_HEADER) ||
        ((const ACTCTX_STRING_SECTION_HEADER *)section)->Magic != ACTCTX_STRING_SECTION_MAGIC)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    UNICODE_STRING key = {};
    if (name) {
        SIZE_T bytes = wcslen(name) * sizeof(WCHAR);
        key.Buffer = (PWSTR)name;
        key.Length = (USHORT)(bytes >= 0xFFFE ? 0xFFFC : bytes);
        key.MaximumLength = key.Length + sizeof(WCHAR);
    }
    ACTIVATION_CONTEXT_SECTION_KEYED_DATA found = {.Size = sizeof(found)};
    status = find_in_string_section(section, length, &key, &found);
    if (!NT_SUCCESS(status))
        return status;
    if (found.DataFormatVersion != 1)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    const ACTCTX_APPLICATION_SETTING *setting = found.Data;
    if (wcscmp((PCWSTR)((const UCHAR *)setting + setting->SettingNamespaceOffset), ns))
        return STATUS_SXS_KEY_NOT_FOUND;
    SIZE_T value_chars = setting->SettingValueLength / sizeof(WCHAR);
    PCWSTR value = (PCWSTR)((const UCHAR *)setting + setting->SettingValueOffset);
    /* Windows accepts a buffer without room for the terminator and writes it past the end; mime drops it. */
    if (chars < value_chars) {
        status = STATUS_BUFFER_TOO_SMALL;
    } else {
        memcpy(buffer, value, setting->SettingValueLength);
        if (chars > value_chars)
            buffer[value_chars] = 0;
    }
    if (needed)
        *needed = value_chars + 1;
    return status;
}
