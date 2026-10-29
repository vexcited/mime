/* atoms and application packages. */
#include "kernelbase_private.h"


#define MAXINTATOM 0xC000
#define MAX_ATOM_NAME 255

/* ------------------------------------------------------------------ process atom tables */

typedef struct ATOM_ENTRY {
    struct ATOM_ENTRY *Next;
    ATOM Atom;
    USHORT References;
    USHORT Length;
    WCHAR Name[1];
} ATOM_ENTRY;

typedef struct ATOM_TABLE {
    RTL_CRITICAL_SECTION Lock;
    BOOL Initialized;
    ATOM_ENTRY *Entries;
    ATOM Next;
} ATOM_TABLE;

static ATOM_TABLE g_local_atoms;
static ATOM_TABLE g_global_atoms; /* used when the kernel has no atom services. */
static RTL_RUN_ONCE g_atom_once;
static LONG g_kernel_atoms = -1; /* -1 unknown, 0 unavailable, 1 available. */

static ULONG NTAPI init_atom_tables(PRTL_RUN_ONCE once, PVOID parameter, PVOID *context)
{
    RtlInitializeCriticalSection(&g_local_atoms.Lock);
    RtlInitializeCriticalSection(&g_global_atoms.Lock);
    g_local_atoms.Next = MAXINTATOM;
    g_global_atoms.Next = MAXINTATOM;
    return TRUE;
}

static ATOM_TABLE *table(BOOL global)
{
    RtlRunOnceExecuteOnce(&g_atom_once, init_atom_tables, nullptr, nullptr);
    return global ? &g_global_atoms : &g_local_atoms;
}

/* MAKEINTATOM values and "#123" strings are integer atoms, anything else is a string name. */
static BOOL integer_atom(LPCWSTR name, ATOM *atom, BOOL *valid)
{
    *valid = TRUE;
    if ((ULONG_PTR)name < 0x10000) {
        *atom = (ATOM)(ULONG_PTR)name;
        *valid = *atom && *atom < MAXINTATOM;
        return TRUE;
    }
    if (name[0] != '#' || !name[1])
        return FALSE;
    ULONG value = 0;
    for (LPCWSTR p = name + 1; *p; p++) {
        if (*p < '0' || *p > '9')
            return FALSE;
        value = value * 10 + (ULONG)(*p - '0');
        if (value >= 0x10000)
            return FALSE;
    }
    *atom = (ATOM)value;
    *valid = value && value < MAXINTATOM;
    return TRUE;
}

static ATOM_ENTRY *lookup(ATOM_TABLE *t, LPCWSTR name, SIZE_T length)
{
    for (ATOM_ENTRY *e = t->Entries; e; e = e->Next)
        if (e->Length == length && !_wcsnicmp(e->Name, name, length))
            return e;
    return nullptr;
}

static ATOM_ENTRY *lookup_atom(ATOM_TABLE *t, ATOM atom)
{
    for (ATOM_ENTRY *e = t->Entries; e; e = e->Next)
        if (e->Atom == atom)
            return e;
    return nullptr;
}

static ATOM table_add(ATOM_TABLE *t, LPCWSTR name)
{
    ATOM atom;
    BOOL valid;
    if (integer_atom(name, &atom, &valid)) {
        if (!valid) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        return atom;
    }
    SIZE_T length = wcslen(name);
    if (!length || length > MAX_ATOM_NAME) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    RtlEnterCriticalSection(&t->Lock);
    ATOM_ENTRY *e = lookup(t, name, length);
    if (e) {
        if (e->References < 0xFFFF)
            e->References++;
        atom = e->Atom;
    } else {
        atom = 0;
        /* find a free value, wrapping around the string atom range. */
        for (ULONG tries = 0; tries < 0x4000 && !atom; tries++) {
            ATOM candidate = t->Next;
            t->Next = t->Next == 0xFFFF ? MAXINTATOM : (ATOM)(t->Next + 1);
            if (!lookup_atom(t, candidate))
                atom = candidate;
        }
        e = atom ? KbAlloc(FIELD_OFFSET(ATOM_ENTRY, Name) + (length + 1) * sizeof(WCHAR)) : nullptr;
        if (e) {
            e->Atom = atom;
            e->References = 1;
            e->Length = (USHORT)length;
            memcpy(e->Name, name, (length + 1) * sizeof(WCHAR));
            e->Next = t->Entries;
            t->Entries = e;
        } else {
            atom = 0;
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        }
    }
    RtlLeaveCriticalSection(&t->Lock);
    return atom;
}

static ATOM table_find(ATOM_TABLE *t, LPCWSTR name)
{
    ATOM atom;
    BOOL valid;
    if (integer_atom(name, &atom, &valid)) {
        if (!valid) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        return atom;
    }
    SIZE_T length = wcslen(name);
    if (length > MAX_ATOM_NAME) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    RtlEnterCriticalSection(&t->Lock);
    ATOM_ENTRY *e = lookup(t, name, length);
    atom = e ? e->Atom : 0;
    RtlLeaveCriticalSection(&t->Lock);
    if (!atom)
        KbSetError(ERROR_FILE_NOT_FOUND);
    return atom;
}

static ATOM table_delete(ATOM_TABLE *t, ATOM atom)
{
    if (atom < MAXINTATOM)
        return 0;
    RtlEnterCriticalSection(&t->Lock);
    ATOM_ENTRY **link = &t->Entries;
    while (*link && (*link)->Atom != atom)
        link = &(*link)->Next;
    ATOM_ENTRY *e = *link;
    if (e && --e->References == 0) {
        *link = e->Next;
        KbFree(e);
    }
    RtlLeaveCriticalSection(&t->Lock);
    if (!e) {
        KbSetError(ERROR_INVALID_HANDLE);
        return atom;
    }
    return 0;
}

static UINT copy_atom_name(LPCWSTR name, SIZE_T length, LPWSTR buffer, int size)
{
    if (size <= 0 || !buffer) {
        KbSetError(ERROR_MORE_DATA);
        return 0;
    }
    if (length >= (SIZE_T)size)
        length = (SIZE_T)size - 1;
    memcpy(buffer, name, length * sizeof(WCHAR));
    buffer[length] = 0;
    return (UINT)length;
}

static UINT table_name(ATOM_TABLE *t, ATOM atom, LPWSTR buffer, int size)
{
    if (!atom) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (atom < MAXINTATOM) {
        WCHAR text[8];
        int n = KbFormat(text, RTL_NUMBER_OF(text), L"#%u", atom);
        return copy_atom_name(text, (SIZE_T)n, buffer, size);
    }
    RtlEnterCriticalSection(&t->Lock);
    ATOM_ENTRY *e = lookup_atom(t, atom);
    UINT r = 0;
    if (e)
        r = copy_atom_name(e->Name, e->Length, buffer, size);
    RtlLeaveCriticalSection(&t->Lock);
    if (!e)
        KbSetError(ERROR_INVALID_HANDLE);
    return r;
}

/* ------------------------------------------------------------------ local atoms */

ATOM WINAPI AddAtomW(LPCWSTR name)
{
    return table_add(table(FALSE), name);
}

ATOM WINAPI FindAtomW(LPCWSTR name)
{
    return table_find(table(FALSE), name);
}

ATOM WINAPI DeleteAtom(ATOM atom)
{
    return table_delete(table(FALSE), atom);
}

UINT WINAPI GetAtomNameW(ATOM atom, LPWSTR buffer, int size)
{
    return table_name(table(FALSE), atom, buffer, size);
}

BOOL WINAPI InitAtomTable(DWORD size)
{
    table(FALSE);
    return TRUE;
}

/* ansi names pass through unchanged when they are integer atoms. */
static PWSTR atom_name_to_wide(LPCSTR name, BOOL *owned)
{
    *owned = FALSE;
    if ((ULONG_PTR)name < 0x10000)
        return (PWSTR)name;
    PWSTR w = KbAnsiToWideCp(CP_ACP, name);
    *owned = w != nullptr;
    return w;
}

static UINT atom_name_to_ansi(UINT (WINAPI *get)(ATOM, LPWSTR, int), ATOM atom, LPSTR buffer, int size)
{
    WCHAR w[MAX_ATOM_NAME + 1];
    UINT n = get(atom, w, RTL_NUMBER_OF(w));
    if (!n)
        return 0;
    if (size <= 0 || !buffer) {
        KbSetError(ERROR_MORE_DATA);
        return 0;
    }
    int r = WideCharToMultiByte(CP_ACP, 0, w, (int)n, buffer, size - 1, nullptr, nullptr);
    if (r <= 0) {
        /* truncate to what fits, like the wide version. */
        char full[(MAX_ATOM_NAME + 1) * 2];
        int all = WideCharToMultiByte(CP_ACP, 0, w, (int)n, full, sizeof(full), nullptr, nullptr);
        r = all < size - 1 ? all : size - 1;
        memcpy(buffer, full, (SIZE_T)(r > 0 ? r : 0));
    }
    buffer[r > 0 ? r : 0] = 0;
    return (UINT)(r > 0 ? r : 0);
}

ATOM WINAPI AddAtomA(LPCSTR name)
{
    BOOL owned;
    PWSTR w = atom_name_to_wide(name, &owned);
    if (!w && name)
        return 0;
    ATOM atom = AddAtomW(w);
    if (owned)
        KbFree(w);
    return atom;
}

ATOM WINAPI FindAtomA(LPCSTR name)
{
    BOOL owned;
    PWSTR w = atom_name_to_wide(name, &owned);
    if (!w && name)
        return 0;
    ATOM atom = FindAtomW(w);
    if (owned)
        KbFree(w);
    return atom;
}

UINT WINAPI GetAtomNameA(ATOM atom, LPSTR buffer, int size)
{
    return atom_name_to_ansi(GetAtomNameW, atom, buffer, size);
}

/* ------------------------------------------------------------------ global atoms */

/* probes the kernel atom services once, falling back to a process wide table. */
static BOOL kernel_atoms(void)
{
    LONG state = __atomic_load_n(&g_kernel_atoms, __ATOMIC_ACQUIRE);
    if (state < 0) {
        USHORT atom = 0;
        NTSTATUS status = NtFindAtom(L"AtomProbe", sizeof(L"AtomProbe") - sizeof(WCHAR), &atom);
        state = status == STATUS_NOT_IMPLEMENTED ? 0 : 1;
        __atomic_store_n(&g_kernel_atoms, state, __ATOMIC_RELEASE);
    }
    return state == 1;
}

ATOM WINAPI GlobalAddAtomW(LPCWSTR name)
{
    if (!kernel_atoms())
        return table_add(table(TRUE), name);
    ATOM atom;
    BOOL valid;
    if (integer_atom(name, &atom, &valid)) {
        if (!valid)
            KbSetError(ERROR_INVALID_PARAMETER);
        return valid ? atom : 0;
    }
    SIZE_T length = wcslen(name);
    if (!length || length > MAX_ATOM_NAME) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    USHORT result = 0;
    NTSTATUS status = NtAddAtom((PWSTR)name, (ULONG)(length * sizeof(WCHAR)), &result);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status), 0;
    return result;
}

ATOM WINAPI GlobalFindAtomW(LPCWSTR name)
{
    if (!kernel_atoms())
        return table_find(table(TRUE), name);
    ATOM atom;
    BOOL valid;
    if (integer_atom(name, &atom, &valid)) {
        if (!valid)
            KbSetError(ERROR_INVALID_PARAMETER);
        return valid ? atom : 0;
    }
    SIZE_T length = wcslen(name);
    if (length > MAX_ATOM_NAME) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    USHORT result = 0;
    NTSTATUS status = NtFindAtom((PWSTR)name, (ULONG)(length * sizeof(WCHAR)), &result);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status), 0;
    return result;
}

ATOM WINAPI GlobalDeleteAtom(ATOM atom)
{
    if (!kernel_atoms())
        return table_delete(table(TRUE), atom);
    if (atom < MAXINTATOM)
        return 0;
    NTSTATUS status = NtDeleteAtom(atom);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return atom;
    }
    return 0;
}

UINT WINAPI GlobalGetAtomNameW(ATOM atom, LPWSTR buffer, int size)
{
    if (!kernel_atoms() || atom < MAXINTATOM)
        return table_name(table(TRUE), atom, buffer, size);
    struct {
        USHORT UsageCount;
        USHORT Flags;
        USHORT NameLength;
        WCHAR Name[MAX_ATOM_NAME + 1];
    } info;
    NTSTATUS status = NtQueryInformationAtom(atom, 0, &info, sizeof(info), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status), 0;
    return copy_atom_name(info.Name, info.NameLength / sizeof(WCHAR), buffer, size);
}

ATOM WINAPI GlobalAddAtomA(LPCSTR name)
{
    BOOL owned;
    PWSTR w = atom_name_to_wide(name, &owned);
    if (!w && name)
        return 0;
    ATOM atom = GlobalAddAtomW(w);
    if (owned)
        KbFree(w);
    return atom;
}

ATOM WINAPI GlobalFindAtomA(LPCSTR name)
{
    BOOL owned;
    PWSTR w = atom_name_to_wide(name, &owned);
    if (!w && name)
        return 0;
    ATOM atom = GlobalFindAtomW(w);
    if (owned)
        KbFree(w);
    return atom;
}

UINT WINAPI GlobalGetAtomNameA(ATOM atom, LPSTR buffer, int size)
{
    return atom_name_to_ansi(GlobalGetAtomNameW, atom, buffer, size);
}

/* ------------------------------------------------------------------ packages */

/* no process here belongs to an application package. */

LONG WINAPI GetCurrentPackageId(UINT32 *length, BYTE *buffer)
{
    if (!length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG WINAPI GetCurrentPackageFullName(UINT32 *length, PWSTR name)
{
    if (!length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG WINAPI GetCurrentPackageFamilyName(UINT32 *length, PWSTR name)
{
    if (!length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG WINAPI GetCurrentApplicationUserModelId(UINT32 *length, PWSTR id)
{
    if (!length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_APPLICATION;
}

LONG WINAPI GetPackageFullName(HANDLE process, UINT32 *length, PWSTR name)
{
    if (!process || !length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG WINAPI GetPackageFamilyName(HANDLE process, UINT32 *length, PWSTR name)
{
    if (!process || !length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG WINAPI GetApplicationUserModelId(HANDLE process, UINT32 *length, PWSTR id)
{
    if (!process || !length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_APPLICATION;
}

/* only checks arguments: no package is installed, so nothing matches. */
DECLSPEC_EXPORT LONG WINAPI GetPackagesByPackageFamily(PCWSTR family, UINT32 *count, PWSTR *full_names,
                                                       UINT32 *buffer_length, WCHAR *buffer)
{
    if (!family || !count)
        return ERROR_INVALID_PARAMETER;
    if ((*count && !full_names) || (full_names && !buffer))
        return ERROR_INVALID_PARAMETER;
    if (!buffer_length || (*buffer_length && !buffer))
        return ERROR_INVALID_PARAMETER;
    *count = 0;
    *buffer_length = 0;
    return ERROR_SUCCESS;
}

DECLSPEC_EXPORT LONG WINAPI FindPackagesByPackageFamily(PCWSTR family, UINT32 filter, UINT32 *count,
                                                        PWSTR *full_names, UINT32 *buffer_length, WCHAR *buffer,
                                                        UINT32 *properties)
{
    if (!filter)
        return ERROR_INVALID_PARAMETER;
    return GetPackagesByPackageFamily(family, count, full_names, buffer_length, buffer);
}

/* no package is installed, so nothing is found. The error code is a guess. */
DECLSPEC_EXPORT LONG WINAPI GetPackagePathByFullName(PCWSTR full_name, UINT32 *length, PWSTR path)
{
    if (!full_name || !length || (*length && !path))
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}

DECLSPEC_EXPORT LONG WINAPI GetPackagePath(const void *id, const UINT32 reserved, UINT32 *length, PWSTR path)
{
    if (!id || !length || (*length && !path))
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}

/*
 * a packaged process gets its policies from its manifest, any other one those of a classic desktop program. The values
 * are a guess based on the enumeration names in the sdk.
 */
static LONG app_policy(HANDLE token, UINT32 *policy, UINT32 value)
{
    if (!policy)
        return ERROR_INVALID_PARAMETER;
    if (!token)
        return ERROR_INVALID_PARAMETER;
    *policy = value;
    return ERROR_SUCCESS;
}

DECLSPEC_EXPORT LONG WINAPI AppPolicyGetClrCompat(HANDLE token, UINT32 *policy)
{
    return app_policy(token, policy, 1); /* AppPolicyClrCompat_ClassicDesktop */
}

DECLSPEC_EXPORT LONG WINAPI AppPolicyGetCreateFileAccess(HANDLE token, UINT32 *policy)
{
    return app_policy(token, policy, 0); /* AppPolicyCreateFileAccess_Full */
}

DECLSPEC_EXPORT LONG WINAPI AppPolicyGetLifecycleManagement(HANDLE token, UINT32 *policy)
{
    return app_policy(token, policy, 0); /* AppPolicyLifecycleManagement_Unmanaged */
}

DECLSPEC_EXPORT LONG WINAPI AppPolicyGetMediaFoundationCodecLoading(HANDLE token, UINT32 *policy)
{
    return app_policy(token, policy, 0); /* AppPolicyMediaFoundationCodecLoading_All */
}

DECLSPEC_EXPORT LONG WINAPI AppPolicyGetProcessTerminationMethod(HANDLE token, UINT32 *policy)
{
    return app_policy(token, policy, 0); /* AppPolicyProcessTerminationMethod_ExitProcess */
}

DECLSPEC_EXPORT LONG WINAPI AppPolicyGetShowDeveloperDiagnostic(HANDLE token, UINT32 *policy)
{
    return app_policy(token, policy, 1); /* AppPolicyShowDeveloperDiagnostic_ShowUI */
}

DECLSPEC_EXPORT LONG WINAPI AppPolicyGetThreadInitializationType(HANDLE token, UINT32 *policy)
{
    return app_policy(token, policy, 0); /* AppPolicyThreadInitializationType_None */
}

DECLSPEC_EXPORT LONG WINAPI AppPolicyGetWindowingModel(HANDLE token, UINT32 *policy)
{
    return app_policy(token, policy, 2); /* AppPolicyWindowingModel_ClassicDesktop */
}

/* no process or token belongs to a package. */
DECLSPEC_EXPORT LONG WINAPI GetCurrentPackageInfo(UINT32 flags, UINT32 *length, BYTE *buffer, UINT32 *count)
{
    if (!length || (*length && !buffer))
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

DECLSPEC_EXPORT LONG WINAPI GetCurrentPackagePath(UINT32 *length, PWSTR path)
{
    if (!length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

DECLSPEC_EXPORT LONG WINAPI GetPackageId(HANDLE process, UINT32 *length, BYTE *buffer)
{
    if (!process || !length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

DECLSPEC_EXPORT LONG WINAPI GetPackageFullNameFromToken(HANDLE token, UINT32 *length, PWSTR name)
{
    if (!token || !length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

DECLSPEC_EXPORT LONG WINAPI GetPackageFamilyNameFromToken(HANDLE token, UINT32 *length, PWSTR name)
{
    if (!token || !length)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

DECLSPEC_EXPORT BOOL WINAPI Wow64GetThreadContext(HANDLE thread, PVOID context)
{
#ifdef __x86_64__
    NTSTATUS status = RtlWow64GetThreadContext(thread, context);
#else
    NTSTATUS status = STATUS_NOT_SUPPORTED;
#endif
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DECLSPEC_EXPORT BOOL WINAPI Wow64SetThreadContext(HANDLE thread, const void *context)
{
#ifdef __x86_64__
    NTSTATUS status = RtlWow64SetThreadContext(thread, context);
#else
    NTSTATUS status = STATUS_NOT_SUPPORTED;
#endif
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

/* a thread of this process has no local descriptor table to ask: the 32 bit threads run under the emulator. */
BOOL WINAPI GetThreadSelectorEntry(HANDLE thread, DWORD selector, LPLDT_ENTRY entry)
{
    KbSetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
