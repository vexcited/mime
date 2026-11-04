/*
 * the global atom table shared by every process of the session: integer
 * atoms below 0xC000 named "#n", string atoms from 0xC000 compared without
 * case and reference counted, as in windows' rtl atom tables.
 */
#include "../ntos.h"

#define MAX_ATOMS 0x4000
#define MAX_NAME 255
#define FIRST_STRING_ATOM 0xC000
#define ATOM_PINNED 0x01

enum { AtomBasicInformation, AtomTableInformation };

typedef struct ATOM_ENTRY {
    USHORT Count; /* 0 for a free slot */
    USHORT Flags;
    USHORT Length;
    WCHAR Name[MAX_NAME];
} ATOM_ENTRY;

typedef struct ATOM_TABLE {
    ATOM_ENTRY *Atoms[MAX_ATOMS];
    ULONG Used;
} ATOM_TABLE;

/* the session table; jobs that restrict global atoms get a private one, made on first use. */
static ATOM_TABLE g_global;
static KGUARDED_MUTEX g_lock;

void ExpInitializeAtoms(void)
{
    KeInitializeGuardedMutex(&g_lock);
}

void ExpFreeAtomTable(PVOID table)
{
    ATOM_TABLE *t = table;
    for (ULONG i = 0; i < MAX_ATOMS; i++)
        if (t->Atoms[i])
            ExFreePool(t->Atoms[i]);
    ExFreePool(t);
}

/* the job's private table when the job keeps global atoms to itself. */
static ATOM_TABLE *table_of_caller(void)
{
    PVOID *slot = PspJobAtomTableSlot(PsCurrentProcess());
    if (!slot)
        return &g_global;
    KeAcquireGuardedMutex(&g_lock);
    if (!*slot)
        *slot = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ATOM_TABLE), KI_TAG('A', 't', 'o', 'T'));
    ATOM_TABLE *t = *slot;
    KeReleaseGuardedMutex(&g_lock);
    return t ? t : &g_global;
}

static void lock(void)
{
    KeAcquireGuardedMutex(&g_lock);
}

static void unlock(void)
{
    KeReleaseGuardedMutex(&g_lock);
}

/* "#n" with decimal digits only names integer atom n, which must be below 0xC000. */
static BOOLEAN integer_atom(const WCHAR *name, ULONG chars, USHORT *atom, NTSTATUS *status)
{
    if (!chars || name[0] != L'#')
        return FALSE;
    ULONG value = 0;
    for (ULONG i = 1; i < chars; i++) {
        if (name[i] < L'0' || name[i] > L'9')
            return FALSE;
        value = value * 10 + (name[i] - L'0');
        if (value > 0xFFFF)
            value = 0x10000;
    }
    if (chars == 1)
        return FALSE;
    *status = value && value < FIRST_STRING_ATOM ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    *atom = (USHORT)value;
    return TRUE;
}

static BOOLEAN same(const ATOM_ENTRY *e, const WCHAR *name, ULONG chars)
{
    if (e->Length != chars)
        return FALSE;
    for (ULONG i = 0; i < chars; i++)
        if (RtlUpcaseUnicodeChar(e->Name[i]) != RtlUpcaseUnicodeChar(name[i]))
            return FALSE;
    return TRUE;
}

static int lookup(ATOM_TABLE *t, const WCHAR *name, ULONG chars)
{
    for (ULONG i = 0; i < MAX_ATOMS; i++)
        if (t->Atoms[i] && t->Atoms[i]->Count && same(t->Atoms[i], name, chars))
            return (int)i;
    return -1;
}

static ATOM_ENTRY *entry_of(ATOM_TABLE *t, USHORT atom)
{
    if (atom < FIRST_STRING_ATOM || atom - FIRST_STRING_ATOM >= MAX_ATOMS)
        return nullptr;
    ATOM_ENTRY *e = t->Atoms[atom - FIRST_STRING_ATOM];
    return e && e->Count ? e : nullptr;
}

/* names too long for an atom are refused. */
static NTSTATUS capture(PWSTR user_name, ULONG length, WCHAR *name, ULONG *chars)
{
    if (!user_name || (length & 1))
        return STATUS_INVALID_PARAMETER;
    if (length > MAX_NAME * sizeof(WCHAR))
        return STATUS_INVALID_PARAMETER;
    *chars = length / sizeof(WCHAR);
    if (KeGetPreviousMode() == UserMode)
        return KiCopyFromUser(name, user_name, length);
    memcpy(name, user_name, length);
    return STATUS_SUCCESS;
}

static NTSTATUS store_atom(PUSHORT user_atom, USHORT atom)
{
    if (!user_atom)
        return STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode)
        return KiCopyToUser(user_atom, &atom, sizeof(atom));
    *user_atom = atom;
    return STATUS_SUCCESS;
}

static NTSTATUS add_atom(PWSTR user_name, ULONG length, PUSHORT user_atom)
{
    WCHAR name[MAX_NAME];
    ULONG chars;
    NTSTATUS status = capture(user_name, length, name, &chars);
    if (!NT_SUCCESS(status))
        return status;
    USHORT atom;
    if (integer_atom(name, chars, &atom, &status))
        return NT_SUCCESS(status) ? store_atom(user_atom, atom) : status;
    if (!chars)
        return STATUS_INVALID_PARAMETER;
    ATOM_TABLE *t = table_of_caller();
    lock();
    int index = lookup(t, name, chars);
    if (index >= 0) {
        ATOM_ENTRY *e = t->Atoms[index];
        if (!(e->Flags & ATOM_PINNED) && e->Count < 0xFFFF)
            e->Count++;
    } else {
        for (ULONG i = 0; i < MAX_ATOMS && index < 0; i++)
            if (!t->Atoms[i] || !t->Atoms[i]->Count)
                index = (int)i;
        if (index < 0) {
            unlock();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        if (!t->Atoms[index] &&
            !(t->Atoms[index] = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ATOM_ENTRY), KI_TAG('A', 't', 'o', 'm')))) {
            unlock();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        ATOM_ENTRY *e = t->Atoms[index];
        e->Count = 1;
        e->Flags = 0;
        e->Length = (USHORT)chars;
        memcpy(e->Name, name, chars * sizeof(WCHAR));
        t->Used++;
    }
    unlock();
    return store_atom(user_atom, (USHORT)(FIRST_STRING_ATOM + index));
}

NTSTATUS NTAPI NtAddAtom(PWSTR name, ULONG length, PUSHORT atom)
{
    return add_atom(name, length, atom);
}

/* uses the caller's table: the session's, unless its job keeps its own. */
NTSTATUS NTAPI NtAddAtomEx(PWSTR name, ULONG length, PUSHORT atom, ULONG flags)
{
    return add_atom(name, length, atom);
}

NTSTATUS NTAPI NtFindAtom(PWSTR user_name, ULONG length, PUSHORT user_atom)
{
    WCHAR name[MAX_NAME];
    ULONG chars;
    NTSTATUS status = capture(user_name, length, name, &chars);
    if (!NT_SUCCESS(status))
        return status;
    USHORT atom;
    if (integer_atom(name, chars, &atom, &status))
        return NT_SUCCESS(status) ? store_atom(user_atom, atom) : status;
    if (!chars)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    ATOM_TABLE *t = table_of_caller();
    lock();
    int index = lookup(t, name, chars);
    unlock();
    if (index < 0)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    return store_atom(user_atom, (USHORT)(FIRST_STRING_ATOM + index));
}

/* for win32k, which keys the properties user32 names with global atoms. */
USHORT NTAPI MimeFindGlobalAtom(PCWSTR name)
{
    ULONG chars = (ULONG)wcslen(name);
    if (!chars || chars > MAX_NAME)
        return 0;
    ATOM_TABLE *t = table_of_caller();
    lock();
    int index = lookup(t, name, chars);
    unlock();
    return index < 0 ? 0 : (USHORT)(FIRST_STRING_ATOM + index);
}

/* drops one reference; the last frees the atom unless pinned. integer atoms are never stored. */
NTSTATUS NTAPI NtDeleteAtom(USHORT atom)
{
    if (atom && atom < FIRST_STRING_ATOM)
        return STATUS_SUCCESS;
    ATOM_TABLE *t = table_of_caller();
    lock();
    ATOM_ENTRY *e = entry_of(t, atom);
    NTSTATUS status = STATUS_INVALID_HANDLE;
    if (e) {
        status = STATUS_SUCCESS;
        if (!(e->Flags & ATOM_PINNED) && !--e->Count)
            t->Used--;
    }
    unlock();
    return status;
}

NTSTATUS NTAPI NtQueryInformationAtom(USHORT atom, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    BYTE buffer[8 + (MAX_NAME + 1) * sizeof(WCHAR)];
    ULONG size;
    NTSTATUS status = STATUS_SUCCESS;
    switch (klass) {
    case AtomBasicInformation: {
        USHORT count, flags, chars;
        WCHAR name[MAX_NAME + 1];
        if (atom && atom < FIRST_STRING_ATOM) {
            /* an integer atom reports its "#n" name, pinned, with one reference. */
            WCHAR digits[8];
            ULONG n = 0, v = atom;
            do
                digits[n++] = (WCHAR)(L'0' + v % 10);
            while (v /= 10);
            name[0] = L'#';
            for (ULONG i = 0; i < n; i++)
                name[1 + i] = digits[n - 1 - i];
            chars = (USHORT)(n + 1);
            count = 1;
            flags = ATOM_PINNED;
        } else {
            ATOM_TABLE *t = table_of_caller();
            lock();
            ATOM_ENTRY *e = entry_of(t, atom);
            if (e) {
                count = e->Count;
                flags = e->Flags;
                chars = e->Length;
                memcpy(name, e->Name, chars * sizeof(WCHAR));
            }
            unlock();
            if (!e)
                return STATUS_INVALID_HANDLE;
        }
        name[chars] = 0;
        if (length < 8)
            return STATUS_INFO_LENGTH_MISMATCH;
        /* the name is cut to fit, like rtl's copy. */
        ULONG room = (length - 6) / sizeof(WCHAR);
        if (room <= chars) {
            chars = (USHORT)(room ? room - 1 : 0);
            name[chars] = 0;
            status = STATUS_BUFFER_TOO_SMALL;
        }
        USHORT header[3] = {count, flags, (USHORT)(chars * sizeof(WCHAR))};
        memcpy(buffer, header, sizeof(header));
        memcpy(buffer + 6, name, (chars + 1) * sizeof(WCHAR));
        size = 6 + (chars + 1) * sizeof(WCHAR);
        break;
    }
    case AtomTableInformation: {
        if (length < sizeof(ULONG))
            return STATUS_INFO_LENGTH_MISMATCH;
        ULONG fits = (length - sizeof(ULONG)) / sizeof(USHORT), n = 0;
        ATOM_TABLE *t = table_of_caller();
        lock();
        ULONG total = t->Used;
        if (total > fits) {
            unlock();
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        size = sizeof(ULONG) + total * sizeof(USHORT);
        BYTE *table = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('A', 't', 'o', 'm'));
        if (!table) {
            unlock();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        for (ULONG i = 0; i < MAX_ATOMS && n < total; i++)
            if (t->Atoms[i] && t->Atoms[i]->Count) {
                USHORT value = (USHORT)(FIRST_STRING_ATOM + i);
                memcpy(table + sizeof(ULONG) + n++ * sizeof(USHORT), &value, sizeof(value));
            }
        unlock();
        memcpy(table, &n, sizeof(n));
        status = KeGetPreviousMode() == UserMode ? KiCopyToUser(info, table, size) : (memcpy(info, table, size), STATUS_SUCCESS);
        ExFreePool(table);
        if (NT_SUCCESS(status) && returned)
            status = KeGetPreviousMode() == UserMode ? KiCopyToUser(returned, &size, sizeof(size)) : (*returned = size, STATUS_SUCCESS);
        return status;
    }
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
    if (size > length)
        size = length;
    NTSTATUS copied = KeGetPreviousMode() == UserMode ? KiCopyToUser(info, buffer, size)
                                                      : (memcpy(info, buffer, size), STATUS_SUCCESS);
    if (!NT_SUCCESS(copied))
        return copied;
    if (returned) {
        copied = KeGetPreviousMode() == UserMode ? KiCopyToUser(returned, &size, sizeof(size))
                                                 : (*returned = size, STATUS_SUCCESS);
        if (!NT_SUCCESS(copied))
            return copied;
    }
    return status;
}
