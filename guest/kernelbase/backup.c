/* backup streams: a file as the stream headers and bytes that BackupRead hands out, and BackupWrite takes back. */
#include "kernelbase_private.h"
#include <mime/ntrtlsec.h>

NTSYSCALLAPI NTSTATUS NTAPI NtQuerySecurityObject(HANDLE handle, SECURITY_INFORMATION information,
                                                  PSECURITY_DESCRIPTOR descriptor, ULONG length, PULONG needed);
NTSYSCALLAPI NTSTATUS NTAPI NtSetSecurityObject(HANDLE handle, SECURITY_INFORMATION information,
                                                PSECURITY_DESCRIPTOR descriptor);

#define BACKUP_MAGIC 0x504B4342u /* "BCKP". */
#define STREAM_HEADER 20

typedef enum BACKUP_PHASE {
    PHASE_START, /* BackupWrite waits for the header of a stream. */
    PHASE_SECURITY_HEADER,
    PHASE_SECURITY,
    PHASE_DATA_HEADER,
    PHASE_DATA,
    PHASE_DONE,
} BACKUP_PHASE;

/* what is read or written between calls. */
typedef struct BACKUP_CONTEXT {
    ULONG Magic;
    BACKUP_PHASE Phase;
    BOOL ProcessSecurity;
    BOOL IsDirectory;
    BYTE Header[STREAM_HEADER];
    ULONG Done; /* of the part of the stream the phase is in. */
    PUCHAR Security;
    ULONG SecuritySize;
    ULONGLONG DataSize;
    ULONGLONG DataDone;
    /* BackupWrite: the stream being taken in. */
    DWORD StreamId;
    ULONGLONG StreamSize;
    ULONG NameSize;
    BOOL Writing;
} BACKUP_CONTEXT;

static BACKUP_CONTEXT *context_of(LPVOID *context)
{
    BACKUP_CONTEXT *c = context ? *context : nullptr;
    return c && c->Magic == BACKUP_MAGIC ? c : nullptr;
}

static void free_context(LPVOID *context)
{
    BACKUP_CONTEXT *c = context_of(context);
    if (!c)
        return;
    c->Magic = 0;
    KbFree(c->Security);
    KbFree(c);
    *context = nullptr;
}

static void make_header(BYTE *header, DWORD id, ULONGLONG size)
{
    memset(header, 0, STREAM_HEADER);
    memcpy(header, &id, sizeof(id));
    memcpy(header + 8, &size, sizeof(size));
}

/* the security of the file as the stream holds it: the owner, the group and the access list. */
static BOOL load_security(HANDLE file, BACKUP_CONTEXT *c)
{
    ULONG need = 0;
    NTSTATUS status = NtQuerySecurityObject(file, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                                      DACL_SECURITY_INFORMATION,
                                            nullptr, 0, &need);
    if (status != STATUS_BUFFER_TOO_SMALL || !need)
        return KbSetStatus(NT_SUCCESS(status) ? STATUS_UNSUCCESSFUL : status);
    c->Security = KbAlloc(need);
    if (!c->Security) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    status = NtQuerySecurityObject(file, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                   c->Security, need, &need);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    c->SecuritySize = need;
    return TRUE;
}

static BACKUP_CONTEXT *start_reading(HANDLE file, BOOL security)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file, &info))
        return nullptr;
    BACKUP_CONTEXT *c = KbAllocZero(sizeof(*c));
    if (!c) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    c->Magic = BACKUP_MAGIC;
    c->ProcessSecurity = security;
    /* a directory has no data stream. */
    c->IsDirectory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    c->DataSize = c->IsDirectory ? 0 : ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    if (security && !load_security(file, c)) {
        KbFree(c->Security);
        KbFree(c);
        return nullptr;
    }
    if (c->Security) {
        c->Phase = PHASE_SECURITY_HEADER;
        make_header(c->Header, BACKUP_SECURITY_DATA, c->SecuritySize);
    } else if (c->IsDirectory) {
        c->Phase = PHASE_DONE;
    } else {
        c->Phase = PHASE_DATA_HEADER;
        make_header(c->Header, BACKUP_DATA, c->DataSize);
    }
    return c;
}

static void next_phase(BACKUP_CONTEXT *c)
{
    c->Done = 0;
    switch (c->Phase) {
    case PHASE_SECURITY_HEADER:
        c->Phase = PHASE_SECURITY;
        break;
    case PHASE_SECURITY:
        c->Phase = c->IsDirectory ? PHASE_DONE : PHASE_DATA_HEADER;
        make_header(c->Header, BACKUP_DATA, c->DataSize);
        break;
    case PHASE_DATA_HEADER:
        c->Phase = PHASE_DATA;
        break;
    default:
        c->Phase = PHASE_DONE;
    }
}

BOOL WINAPI BackupRead(HANDLE file, LPBYTE buffer, DWORD size, LPDWORD read, BOOL abort, BOOL process_security,
                       LPVOID *context)
{
    if (read)
        *read = 0;
    if (!context) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (abort) {
        free_context(context);
        return TRUE;
    }
    BACKUP_CONTEXT *c = context_of(context);
    if (!c) {
        if (*context) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        c = start_reading(file, process_security);
        if (!c)
            return FALSE;
        *context = c;
    }
    DWORD total = 0;
    while (total < size && c->Phase != PHASE_DONE) {
        DWORD room = size - total;
        if (c->Phase == PHASE_SECURITY_HEADER || c->Phase == PHASE_DATA_HEADER) {
            DWORD n = STREAM_HEADER - c->Done < room ? STREAM_HEADER - c->Done : room;
            memcpy(buffer + total, c->Header + c->Done, n);
            c->Done += n;
            total += n;
            if (c->Done == STREAM_HEADER)
                next_phase(c);
        } else if (c->Phase == PHASE_SECURITY) {
            DWORD n = c->SecuritySize - c->Done < room ? c->SecuritySize - c->Done : room;
            memcpy(buffer + total, c->Security + c->Done, n);
            c->Done += n;
            total += n;
            if (c->Done == c->SecuritySize)
                next_phase(c);
        } else {
            ULONGLONG left = c->DataSize - c->DataDone;
            DWORD want = left < room ? (DWORD)left : room, got = 0;
            if (want && !ReadFile(file, buffer + total, want, &got, nullptr))
                return FALSE;
            c->DataDone += got;
            total += got;
            if (c->DataDone == c->DataSize || !got)
                c->Phase = PHASE_DONE;
        }
    }
    if (read)
        *read = total;
    return TRUE;
}

/* forward in the stream being read, and no further than its end; the data of the file is skipped in the file. */
BOOL WINAPI BackupSeek(HANDLE file, DWORD low, DWORD high, LPDWORD low_seeked, LPDWORD high_seeked, LPVOID *context)
{
    *low_seeked = *high_seeked = 0;
    BACKUP_CONTEXT *c = context_of(context);
    if (!c) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONGLONG want = ((ULONGLONG)(high & 0x7FFFFFFF) << 32) | low, left;
    switch (c->Phase) {
    case PHASE_SECURITY_HEADER:
    case PHASE_DATA_HEADER:
        left = STREAM_HEADER - c->Done;
        break;
    case PHASE_SECURITY:
        left = c->SecuritySize - c->Done;
        break;
    case PHASE_DATA:
        left = c->DataSize - c->DataDone;
        break;
    default:
        left = 0;
    }
    ULONGLONG moved = want < left ? want : left;
    if (c->Phase == PHASE_DATA && moved) {
        LONG move_high = (LONG)(moved >> 32);
        if (SetFilePointer(file, (LONG)moved, &move_high, FILE_CURRENT) == INVALID_SET_FILE_POINTER &&
            GetLastError() != NO_ERROR)
            return FALSE;
        c->DataDone += moved;
    } else if (c->Phase != PHASE_DATA) {
        c->Done += (ULONG)moved;
    }
    if (c->Phase == PHASE_DATA && c->DataDone == c->DataSize)
        c->Phase = PHASE_DONE;
    else if (c->Phase != PHASE_DATA && c->Phase != PHASE_DONE && moved == left)
        next_phase(c);
    *low_seeked = (DWORD)moved;
    *high_seeked = (DWORD)(moved >> 32);
    if (moved != want) {
        KbSetError(ERROR_SEEK);
        return FALSE;
    }
    return TRUE;
}

/* applies a security stream that has been taken in whole. */
static BOOL apply_security(HANDLE file, BACKUP_CONTEXT *c)
{
    if (!c->ProcessSecurity || !c->Security)
        return TRUE;
    NTSTATUS status = NtSetSecurityObject(file, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                                    DACL_SECURITY_INFORMATION,
                                          c->Security);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

/*
 * the streams come in the order BackupRead gives them. the data stream is written to the file, the security stream is
 * set on it when asked, and the others, which this file system cannot hold, are taken in and dropped.
 */
BOOL WINAPI BackupWrite(HANDLE file, LPBYTE buffer, DWORD size, LPDWORD written, BOOL abort, BOOL process_security,
                        LPVOID *context)
{
    if (written)
        *written = 0;
    if (!context) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (abort) {
        free_context(context);
        return TRUE;
    }
    BACKUP_CONTEXT *c = context_of(context);
    if (!c) {
        c = KbAllocZero(sizeof(*c));
        if (!c) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        c->Magic = BACKUP_MAGIC;
        c->Writing = TRUE;
        c->ProcessSecurity = process_security;
        *context = c;
    }
    DWORD used = 0;
    while (used < size) {
        DWORD room = size - used;
        if (c->Phase == PHASE_START) {
            /* the header and the name of the next stream. */
            DWORD n = STREAM_HEADER - c->Done < room ? STREAM_HEADER - c->Done : room;
            memcpy(c->Header + c->Done, buffer + used, n);
            c->Done += n;
            used += n;
            if (c->Done < STREAM_HEADER)
                break;
            memcpy(&c->StreamId, c->Header, 4);
            memcpy(&c->StreamSize, c->Header + 8, 8);
            memcpy(&c->NameSize, c->Header + 16, 4);
            c->Done = 0;
            c->DataDone = 0;
            c->Phase = c->NameSize ? PHASE_DATA_HEADER : PHASE_DATA;
            if (c->Phase == PHASE_DATA && c->StreamId == BACKUP_SECURITY_DATA && c->StreamSize) {
                KbFree(c->Security);
                c->Security = KbAlloc((SIZE_T)c->StreamSize);
                c->SecuritySize = c->Security ? (ULONG)c->StreamSize : 0;
            }
            if (c->Phase == PHASE_DATA && !c->StreamSize)
                c->Phase = PHASE_START;
        } else if (c->Phase == PHASE_DATA_HEADER) {
            /* the name of a stream with one is skipped, with the alternate streams that have names. */
            DWORD n = c->NameSize - c->Done < room ? c->NameSize - c->Done : room;
            c->Done += n;
            used += n;
            if (c->Done == c->NameSize) {
                c->Done = 0;
                c->Phase = c->StreamSize ? PHASE_DATA : PHASE_START;
            }
        } else {
            ULONGLONG left = c->StreamSize - c->DataDone;
            DWORD n = left < room ? (DWORD)left : room;
            if (c->StreamId == BACKUP_DATA && !c->NameSize) {
                DWORD wrote;
                if (!WriteFile(file, buffer + used, n, &wrote, nullptr))
                    return FALSE;
                n = wrote;
            } else if (c->StreamId == BACKUP_SECURITY_DATA && c->Security && c->DataDone + n <= c->SecuritySize) {
                memcpy(c->Security + c->DataDone, buffer + used, n);
            }
            c->DataDone += n;
            used += n;
            if (c->DataDone == c->StreamSize) {
                if (c->StreamId == BACKUP_SECURITY_DATA && !apply_security(file, c))
                    return FALSE;
                c->Phase = PHASE_START;
                c->Done = 0;
            } else if (!n) {
                break;
            }
        }
    }
    if (written)
        *written = used;
    return TRUE;
}
