/*
 * the device of the console driver, \Device\ConDrv, see console.c for the
 * consoles. a program opens its console as files: \CurrentIn is the input,
 * \CurrentOut the screen buffer that is shown, \ScreenBuffer makes a
 * screen buffer and \Pty a pseudo console. the file objects carry what they
 * belong to, and kernelbase asks them its questions with device controls
 * (mime/condrv.h), reads and writes them with ReadFile and WriteFile.
 *
 * a standard stream `mime run` was given a pipe or a file for is no
 * console: \Device\ConDrvStream\Input, Output and Error are its ends,
 * files of a named pipe device as a redirected handle is on windows, read
 * and written in bytes and asked the questions of a pipe.
 */
#include "console_p.h"

#define STATUS_PIPE_BROKEN ((NTSTATUS)0xC000014BL)

static PDEVICE_OBJECT g_device;
static PDEVICE_OBJECT g_stream_device;

#define STREAM_INPUT 1
#define STREAM_OUTPUT 2
#define STREAM_ERROR 3

#define FSCTL_PIPE_PEEK 0x0011400C
#define FILE_PIPE_CONNECTED_STATE 3
#define FILE_DEVICE_NAMED_PIPE 0x11

#include <mime/cp1252.h>
#include <mime/cp437.h>

static NTSTATUS complete(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

/* ------------------------------------------------------------ code pages */

static const WCHAR *high_table(UINT cp)
{
    switch (cp) {
    case 437:
        return g_cp437;
    case 1252:
        return g_cp1252;
    default:
        return nullptr;
    }
}

/*
 * bytes of a code page to utf-16. a character cut at the end of the input
 * waits in carry for the next call. consumed is the bytes taken, the
 * return is the characters made.
 */
ULONG ConBytesToWide(UINT cp, const UCHAR *in, ULONG length, PWCHAR out, ULONG room, UCHAR carry[4],
                     PULONG carry_length, PULONG consumed)
{
    ULONG n = 0, i = 0;
    if (cp == 65001) {
        UCHAR joined[8];
        while (i < length || *carry_length) {
            ULONG have = 0;
            for (ULONG k = 0; k < *carry_length; k++)
                joined[have++] = carry[k];
            ULONG fresh = 0;
            while (have < 4 && i + fresh < length) {
                joined[have++] = in[i + fresh];
                fresh++;
                if (have >= 1) {
                    UCHAR lead = joined[0];
                    ULONG need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
                    if (have >= need)
                        break;
                }
            }
            UCHAR lead = joined[0];
            ULONG need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (have < need) {
                /* the character goes on in the next write. */
                memcpy(carry, joined, have);
                *carry_length = have;
                i += fresh;
                break;
            }
            ULONG c;
            ULONG take = need;
            if (need == 1) {
                c = lead < 0x80 ? lead : 0xFFFD;
            } else {
                BOOLEAN ok = TRUE;
                for (ULONG k = 1; k < need; k++)
                    ok = ok && (joined[k] & 0xC0) == 0x80;
                if (!ok) {
                    c = 0xFFFD;
                    take = 1;
                } else if (need == 2) {
                    c = ((joined[0] & 0x1Fu) << 6) | (joined[1] & 0x3Fu);
                } else if (need == 3) {
                    c = ((joined[0] & 0xFu) << 12) | ((joined[1] & 0x3Fu) << 6) | (joined[2] & 0x3Fu);
                } else {
                    c = ((joined[0] & 0x7u) << 18) | ((joined[1] & 0x3Fu) << 12) | ((joined[2] & 0x3Fu) << 6) | (joined[3] & 0x3Fu);
                }
            }
            ULONG units = c >= 0x10000 ? 2 : 1;
            if (n + units > room)
                break;
            if (c >= 0x10000) {
                c -= 0x10000;
                out[n++] = (WCHAR)(0xD800 + (c >> 10));
                out[n++] = (WCHAR)(0xDC00 + (c & 0x3FF));
            } else {
                out[n++] = (WCHAR)c;
            }
            /* what was taken from the carry first, then from the input. */
            ULONG from_carry = *carry_length < take ? *carry_length : take;
            ULONG from_input = take - from_carry;
            memmove(carry, carry + from_carry, *carry_length - from_carry);
            *carry_length -= from_carry;
            i += from_input;
            if (take < have && from_input < fresh) {
                /* a malformed lead byte: the bytes after it are read again. */
            }
        }
        *consumed = i;
        return n;
    }
    const WCHAR *high = high_table(cp);
    while (i < length && n < room) {
        UCHAR b = in[i++];
        if (b < 0x80)
            out[n++] = b;
        else if (high)
            out[n++] = high[b - 0x80];
        else if (cp == 28591)
            out[n++] = b;
        else
            out[n++] = b;
    }
    *consumed = i;
    return n;
}

/* utf-16 to the bytes of a code page; stops before a character that does not fit in the room. */
ULONG ConWideToBytes(UINT cp, const WCHAR *in, ULONG count, PUCHAR out, ULONG room, PULONG consumed)
{
    ULONG n = 0, i = 0;
    const WCHAR *high = high_table(cp);
    while (i < count) {
        ULONG c = in[i];
        ULONG units = 1;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < count && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (in[i + 1] - 0xDC00u);
            units = 2;
        }
        UCHAR bytes[4];
        ULONG size;
        if (cp == 65001) {
            if (c >= 0xD800 && c <= 0xDFFF)
                c = 0xFFFD;
            if (c < 0x80) {
                bytes[0] = (UCHAR)c;
                size = 1;
            } else if (c < 0x800) {
                bytes[0] = (UCHAR)(0xC0 | (c >> 6));
                bytes[1] = (UCHAR)(0x80 | (c & 0x3F));
                size = 2;
            } else if (c < 0x10000) {
                bytes[0] = (UCHAR)(0xE0 | (c >> 12));
                bytes[1] = (UCHAR)(0x80 | ((c >> 6) & 0x3F));
                bytes[2] = (UCHAR)(0x80 | (c & 0x3F));
                size = 3;
            } else {
                bytes[0] = (UCHAR)(0xF0 | (c >> 18));
                bytes[1] = (UCHAR)(0x80 | ((c >> 12) & 0x3F));
                bytes[2] = (UCHAR)(0x80 | ((c >> 6) & 0x3F));
                bytes[3] = (UCHAR)(0x80 | (c & 0x3F));
                size = 4;
            }
        } else {
            size = 1;
            if (c < 0x80) {
                bytes[0] = (UCHAR)c;
            } else if (cp == 28591 && c < 0x100) {
                bytes[0] = (UCHAR)c;
            } else {
                bytes[0] = '?';
                if (high) {
                    for (ULONG k = 0; k < 128; k++) {
                        if (high[k] == c) {
                            bytes[0] = (UCHAR)(0x80 + k);
                            break;
                        }
                    }
                }
            }
        }
        if (n + size > room)
            break;
        memcpy(out + n, bytes, size);
        n += size;
        i += units;
    }
    *consumed = i;
    return n;
}

/* ------------------------------------------------------------ reading text */

/* the characters of key presses waiting, each as many times as its repeat count; the other events are dropped. */
static ULONG take_chars(MIME_CONSOLE *c, PWCHAR out, ULONG room, ULONG *acked, BOOLEAN *eof)
{
    ULONG done = 0;
    *acked = 0;
    *eof = FALSE;
    PLIST_ENTRY e = c->Input.Flink;
    while (e != &c->Input && done < room) {
        CON_CHUNK *k = CONTAINING_RECORD(e, CON_CHUNK, Links);
        e = e->Flink;
        if (!k->Records) {
            /* the bytes of a console fed by a stream: utf-8 characters. */
            if (!k->Length) {
                if (done)
                    break;
                *eof = TRUE;
                ConFreeChunk(c, k, acked);
                break;
            }
            ULONG used = 0;
            UCHAR carry[4];
            ULONG carry_length = 0, consumed = 0;
            (void)used;
            done += ConBytesToWide(65001, k->Data + k->First, k->Length - k->First, out + done, room - done, carry,
                                   &carry_length, &consumed);
            k->First += consumed;
            c->InputBytes -= consumed;
            if (k->First >= k->Length)
                ConFreeChunk(c, k, acked);
            continue;
        }
        while (k->First < k->Length && done < room) {
            MIME_CONDRV_RECORD *r = &k->Record[k->First];
            if (r->EventType == CON_KEY_EVENT && r->Key.KeyDown && r->Key.Char) {
                USHORT repeat = r->Key.RepeatCount ? r->Key.RepeatCount : 1;
                while (repeat && done < room) {
                    out[done++] = r->Key.Char;
                    repeat--;
                }
                if (repeat) {
                    r->Key.RepeatCount = repeat;
                    break;
                }
            }
            k->First++;
            c->InputEvents--;
        }
        if (k->First >= k->Length)
            ConFreeChunk(c, k, acked);
    }
    return done;
}

/* a key press of the records waiting, for a line being edited; false when there is none. */
static BOOLEAN next_key(MIME_CONSOLE *c, ConKey *key, BOOLEAN *eof, ULONG *acked)
{
    *eof = FALSE;
    *acked = 0;
    MIME_CONDRV_RECORD r;
    for (;;) {
        BOOLEAN end;
        ULONG a;
        ULONG n = ConTakeRecords(c, &r, 1, FALSE, &end, &a);
        *acked += a;
        if (end) {
            *eof = TRUE;
            return FALSE;
        }
        if (!n)
            return FALSE;
        if (r.EventType != CON_KEY_EVENT || !r.Key.KeyDown)
            continue;
        key->Down = TRUE;
        key->Vk = r.Key.VirtualKey;
        key->Char = r.Key.Char;
        key->State = r.Key.ControlKeyState;
        key->Repeat = r.Key.RepeatCount ? r.Key.RepeatCount : 1;
        return TRUE;
    }
}

/* waits for the console to have input, or to close. */
static NTSTATUS wait_ready(MIME_CONSOLE *c)
{
    return KeWaitForSingleObject(&c->InputReady, UserRequest, UserMode, FALSE, nullptr);
}

/*
 * the characters a read has left from the line it took, the first line of them when an alias made several.
 * returns how many it gave. called with the lock held.
 */
static ULONG take_pending(MIME_CONSOLE *c, PWCHAR out, ULONG room)
{
    ULONG limit = c->LinePendingLength;
    if (c->LinePendingLines) {
        for (ULONG i = 0; i < c->LinePendingLength; i++) {
            if (c->LinePending[c->LinePendingFirst + i] == '\n') {
                limit = i + 1;
                break;
            }
        }
    }
    ULONG n = limit < room ? limit : room;
    memcpy(out, c->LinePending + c->LinePendingFirst, n * sizeof(WCHAR));
    c->LinePendingFirst += n;
    c->LinePendingLength -= n;
    if (c->LinePendingLines && n == limit)
        c->LinePendingLines--;
    if (!c->LinePendingLength) {
        ExFreePool(c->LinePending);
        c->LinePending = nullptr;
        c->LinePendingFirst = 0;
        c->LinePendingLines = 0;
    }
    return n;
}

/*
 * reads text for ReadConsole and ReadFile: in line input a line, with its
 * line ending, edited with the keys; else the characters waiting, at least
 * one. what does not fit stays for the next read. *got is in characters. a
 * ctrl+c or ctrl+break while the read waits ends it with STATUS_CANCELLED.
 */
static NTSTATUS read_text(MIME_CONSOLE *c, ULONG room, PWCHAR out, PWCHAR initial, ULONG initial_length, PWCHAR exe,
                          ULONG exe_length, ULONG wake_mask, PULONG got, PULONG key_state)
{
    *got = 0;
    *key_state = 0;
    if (!room)
        return STATUS_BUFFER_TOO_SMALL;
    ConLine *line = nullptr;
    ExAcquireFastMutex(&g_con_lock);
    ULONG generation = c->AbortGeneration;
    ExReleaseFastMutex(&g_con_lock);
    for (;;) {
        ExAcquireFastMutex(&g_con_lock);
        if (c->LinePendingLength) {
            *got = take_pending(c, out, room);
            ExReleaseFastMutex(&g_con_lock);
            return STATUS_SUCCESS;
        }
        BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0;
        BOOLEAN eof = FALSE;
        ULONG acked = 0;
        if (c->AbortGeneration != generation) {
            if (line)
                ConLineEnd(line);
            ConAfterRead(c, 0, TRUE);
            ExReleaseFastMutex(&g_con_lock);
            return STATUS_CANCELLED;
        }
        if (!(c->InputMode & ENABLE_LINE_INPUT)) {
            ULONG n = take_chars(c, out, room, &acked, &eof);
            ConAfterRead(c, acked, !n && !eof);
            ExReleaseFastMutex(&g_con_lock);
            if (n || eof || closed) {
                *got = n;
                return STATUS_SUCCESS;
            }
            NTSTATUS status = wait_ready(c);
            if (status != STATUS_SUCCESS)
                return status;
            continue;
        }
        /* line input: the keys are the editor's until it has a line. */
        if (!line) {
            unsigned flags = 0;
            if (c->InputMode & ENABLE_ECHO_INPUT)
                flags |= CONLINE_ECHO;
            if (c->InputMode & ENABLE_PROCESSED_INPUT)
                flags |= CONLINE_PROCESSED;
            if (c->InsertDefault)
                flags |= CONLINE_INSERT;
            if (c->InputMode & ENABLE_VIRTUAL_TERMINAL_INPUT)
                flags |= CONLINE_VT;
            line = ConLineBegin(CsDisplayed(&c->Term), c->Lines, exe, exe_length, flags, initial, initial_length, wake_mask);
            if (!line) {
                ExReleaseFastMutex(&g_con_lock);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            ConFlush(c);
        }
        ConLineResult result = CONLINE_MORE;
        BOOLEAN any = FALSE;
        ConKey key;
        ULONG total_acked = 0;
        while (result == CONLINE_MORE && next_key(c, &key, &eof, &acked)) {
            total_acked += acked;
            any = TRUE;
            for (USHORT r = 0; r < key.Repeat && result == CONLINE_MORE; r++)
                result = ConLineKey(line, &key);
            ConFlush(c);
        }
        total_acked += acked;
        ConAfterRead(c, total_acked, !any && !eof);
        if (result != CONLINE_MORE) {
            size_t length;
            const ConChar *text = ConLineText(line, &length);
            *key_state = ConLineKeyState(line);
            size_t lines = ConLineLines(line);
            if (length) {
                c->LinePending = ExAllocatePool2(POOL_FLAG_NON_PAGED, length * sizeof(WCHAR), KI_TAG('C', 'o', 'n', 'l'));
                if (c->LinePending) {
                    memcpy(c->LinePending, text, length * sizeof(WCHAR));
                    c->LinePendingLength = (ULONG)length;
                    c->LinePendingFirst = 0;
                    c->LinePendingLines = lines > 1 ? (ULONG)lines : 0;
                }
            }
            ConLineEnd(line);
            ConFlush(c);
            if (c->LinePendingLength)
                *got = take_pending(c, out, room);
            ExReleaseFastMutex(&g_con_lock);
            return STATUS_SUCCESS;
        }
        ExReleaseFastMutex(&g_con_lock);
        if (eof || closed) {
            ExAcquireFastMutex(&g_con_lock);
            ConLineEnd(line);
            ExReleaseFastMutex(&g_con_lock);
            return STATUS_SUCCESS;
        }
        NTSTATUS status = wait_ready(c);
        if (status != STATUS_SUCCESS) {
            ExAcquireFastMutex(&g_con_lock);
            ConLineEnd(line);
            ExReleaseFastMutex(&g_con_lock);
            return status;
        }
    }
}

/* the same as bytes of the input code page, the bytes that do not fit kept for the next read. */
static NTSTATUS read_bytes(MIME_CONSOLE *c, ULONG room, PUCHAR out, PWCHAR initial, ULONG initial_length, PWCHAR exe,
                           ULONG exe_length, ULONG wake_mask, PULONG got, PULONG key_state)
{
    *got = 0;
    *key_state = 0;
    if (!room)
        return STATUS_SUCCESS;
    ExAcquireFastMutex(&g_con_lock);
    if (c->BytePendingLength) {
        ULONG n = c->BytePendingLength < room ? c->BytePendingLength : room;
        memcpy(out, c->BytePending + c->BytePendingFirst, n);
        c->BytePendingFirst += n;
        c->BytePendingLength -= n;
        if (!c->BytePendingLength) {
            ExFreePool(c->BytePending);
            c->BytePending = nullptr;
            c->BytePendingFirst = 0;
        }
        *got = n;
        ExReleaseFastMutex(&g_con_lock);
        return STATUS_SUCCESS;
    }
    UINT cp = c->InputCp;
    ExReleaseFastMutex(&g_con_lock);
    PWCHAR wide = ExAllocatePool2(POOL_FLAG_NON_PAGED, room * sizeof(WCHAR), KI_TAG('C', 'o', 'n', 'w'));
    if (!wide)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG chars = 0;
    NTSTATUS status = read_text(c, room, wide, initial, initial_length, exe, exe_length, wake_mask, &chars, key_state);
    if (NT_SUCCESS(status) && chars) {
        ULONG consumed = 0;
        ULONG n = ConWideToBytes(cp, wide, chars, out, room, &consumed);
        *got = n;
        if (consumed < chars) {
            /* the rest of the characters as bytes, for the next read. */
            ULONG left = chars - consumed;
            PUCHAR rest = ExAllocatePool2(POOL_FLAG_NON_PAGED, left * 4, KI_TAG('C', 'o', 'n', 'b'));
            if (rest) {
                ULONG used;
                ULONG m = ConWideToBytes(cp, wide + consumed, left, rest, left * 4, &used);
                ExAcquireFastMutex(&g_con_lock);
                c->BytePending = rest;
                c->BytePendingLength = m;
                c->BytePendingFirst = 0;
                ExReleaseFastMutex(&g_con_lock);
            }
        }
    }
    ExFreePool(wide);
    return status;
}

/* ------------------------------------------------------------ the handles */

/* the console of a file of the driver that is a pseudo console, with a reference, or nothing. */
MIME_CONSOLE *ConPtyFromFile(PFILE_OBJECT file)
{
    if (file->DeviceObject != g_device)
        return nullptr;
    CON_HANDLE *h = file->FsContext2;
    if (!h || h->Kind != CH_PTY)
        return nullptr;
    ConReference(h->Console);
    return h->Console;
}

/* the pseudo console a handle of the current process is, with a reference. */
MIME_CONSOLE *ConsoleOfPtyHandle(HANDLE handle)
{
    PFILE_OBJECT file;
    if (!NT_SUCCESS(ObReferenceHandle(handle, IoFileObjectTypeObject, UserMode, (PVOID *)&file, nullptr)))
        return nullptr;
    MIME_CONSOLE *c = ConPtyFromFile(file);
    ObDereferenceObject(file);
    return c;
}

static CON_HANDLE *handle_of(PIRP irp)
{
    return (CON_HANDLE *)IoGetCurrentIrpStackLocation(irp)->FileObject->FsContext2;
}

static BOOLEAN name_is(PUNICODE_STRING file, PCWSTR name)
{
    UNICODE_STRING n;
    WCHAR full[32];
    full[0] = '\\';
    wcscpy(full + 1, name);
    RtlInitUnicodeString(&n, full);
    return RtlEqualUnicodeString(file, &n, TRUE);
}

static NTSTATUS NTAPI con_create(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    if (device == g_stream_device) {
        static const struct {
            PCWSTR Name;
            ULONG Stream;
        } names[] = {{L"\\Input", STREAM_INPUT}, {L"\\Output", STREAM_OUTPUT}, {L"\\Error", STREAM_ERROR}};
        for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++) {
            UNICODE_STRING n;
            RtlInitUnicodeString(&n, names[i].Name);
            if (RtlEqualUnicodeString(&file->FileName, &n, TRUE)) {
                file->FsContext2 = (PVOID)(ULONG_PTR)names[i].Stream;
                return complete(irp, STATUS_SUCCESS, 0);
            }
        }
        return complete(irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
    }
    CON_HANDLE *h = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*h), KI_TAG('C', 'o', 'n', 'h'));
    if (!h)
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    InitializeListHead(&h->Links);
    h->File = file;
    NTSTATUS status = STATUS_SUCCESS;
    if (!file->FileName.Length) {
        h->Kind = CH_CONTROL;
    } else if (name_is(&file->FileName, MIME_CONDRV_CURRENT_IN) || name_is(&file->FileName, L"Input")) {
        MIME_CONSOLE *c = ConProcessConsole(PsCurrentProcess());
        if (!c) {
            status = STATUS_OBJECT_NAME_NOT_FOUND;
        } else {
            h->Kind = CH_INPUT;
            h->Console = c;
            file->Flags |= FO_SKIP_SET_EVENT;
            ExAcquireFastMutex(&g_con_lock);
            InsertTailList(&c->InputHandles, &h->Links);
            if (ConInputWaiting(c))
                KeSetEvent(&file->Event, 0, FALSE);
            ExReleaseFastMutex(&g_con_lock);
        }
    } else if (name_is(&file->FileName, MIME_CONDRV_CURRENT_OUT) || name_is(&file->FileName, L"Output")) {
        MIME_CONSOLE *c = ConProcessConsole(PsCurrentProcess());
        if (!c) {
            status = STATUS_OBJECT_NAME_NOT_FOUND;
        } else {
            h->Kind = CH_OUTPUT;
            h->Console = c;
            ExAcquireFastMutex(&g_con_lock);
            h->Buffer = c->Term.Shown ? c->Term.Shown : c->Main;
            h->Buffer->Refs++;
            ExReleaseFastMutex(&g_con_lock);
        }
    } else if (name_is(&file->FileName, MIME_CONDRV_SCREEN_BUFFER)) {
        MIME_CONSOLE *c = ConProcessConsole(PsCurrentProcess());
        if (!c) {
            status = STATUS_OBJECT_NAME_NOT_FOUND;
        } else {
            ExAcquireFastMutex(&g_con_lock);
            CsBuffer *shown = c->Term.Shown ? c->Term.Shown : c->Main;
            int w = shown->Window.Right - shown->Window.Left + 1, hgt = shown->Window.Bottom - shown->Window.Top + 1;
            CsBuffer *b = CsBufferCreate(&c->Term, w, hgt);
            if (b) {
                b->Refs = 1;
                h->Kind = CH_OUTPUT;
                h->Console = c;
                h->Buffer = b;
            } else {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
            ExReleaseFastMutex(&g_con_lock);
            if (!NT_SUCCESS(status))
                ConRelease(c);
        }
    } else if (name_is(&file->FileName, MIME_CONDRV_PTY)) {
        MIME_CONSOLE *c = ConCreateConsole(CON_BACKEND_PTY, 0, MIME_CONDRV_TTY_INPUT | MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR, 0);
        if (!c) {
            status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            h->Kind = CH_PTY;
            h->Console = c;
        }
    } else {
        status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (!NT_SUCCESS(status)) {
        ExFreePool(h);
        return complete(irp, status, 0);
    }
    file->FsContext2 = h;
    return complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI con_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    return complete(irp, STATUS_SUCCESS, 0);
}

/* the last handle of a file went: what it held is let go. */
static NTSTATUS NTAPI con_close(PDEVICE_OBJECT device, PIRP irp)
{
    if (device != g_device)
        return complete(irp, STATUS_SUCCESS, 0);
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    CON_HANDLE *h = file->FsContext2;
    file->FsContext2 = nullptr;
    if (!h)
        return complete(irp, STATUS_SUCCESS, 0);
    MIME_CONSOLE *c = h->Console;
    if (c) {
        ExAcquireFastMutex(&g_con_lock);
        if (h->Kind == CH_INPUT)
            RemoveEntryList(&h->Links);
        CsBuffer *b = h->Buffer;
        if (b && --b->Refs == 0 && b != c->Main) {
            if (c->Term.Shown == b)
                CsShow(&c->Term, c->Main);
            CsBufferDestroy(b);
            ConFlush(c);
        }
        ExReleaseFastMutex(&g_con_lock);
        if (h->Kind == CH_PTY)
            ConClose(c);
        ConRelease(c);
    }
    ExFreePool(h);
    return complete(irp, STATUS_SUCCESS, 0);
}

/* ------------------------------------------------------------ device controls */

/* the console a request is about: the handle's, or the process's for a handle of the device itself. */
static MIME_CONSOLE *console_for(CON_HANDLE *h, PEPROCESS self, BOOLEAN *owned)
{
    *owned = FALSE;
    if (h && h->Console)
        return h->Console;
    MIME_CONSOLE *c = ConProcessConsole(self);
    *owned = c != nullptr;
    return c;
}

static NTSTATUS to_status(CsResult r)
{
    switch (r) {
    case CS_OK:
        return STATUS_SUCCESS;
    case CS_NO_MEMORY:
        return STATUS_INSUFFICIENT_RESOURCES;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* waits while the pause key holds the output, or the owner of a pseudo console has not taken what it was given. */
static NTSTATUS wait_output(MIME_CONSOLE *c)
{
    for (;;) {
        ExAcquireFastMutex(&g_con_lock);
        BOOLEAN paused = c->OutputPaused;
        BOOLEAN full = c->Backend == CON_BACKEND_PTY && c->PtyQueued > 256 * 1024 && !(c->Flags & MIME_CONDRV_CLOSED);
        ExReleaseFastMutex(&g_con_lock);
        if (!paused && !full)
            return STATUS_SUCCESS;
        NTSTATUS status = KeWaitForSingleObject(paused ? &c->OutputResumed : &c->PtySpace, UserRequest, UserMode, FALSE, nullptr);
        if (status != STATUS_SUCCESS)
            return status;
    }
}

/* text for a screen buffer: utf-16, or bytes in the output code page, in pieces the scratch buffer holds. */
static NTSTATUS write_text(MIME_CONSOLE *c, CsBuffer *b, BOOLEAN bytes, const void *data, ULONG length)
{
    NTSTATUS status = wait_output(c);
    if (status != STATUS_SUCCESS)
        return status;
    const WCHAR *wide_in = data;
    const UCHAR *byte_in = data;
    while (length) {
        ExAcquireFastMutex(&g_con_lock);
        if (c->Flags & MIME_CONDRV_CLOSED) {
            ExReleaseFastMutex(&g_con_lock);
            return STATUS_PIPE_BROKEN;
        }
        if (!c->Scratch) {
            c->ScratchLength = 4096;
            c->Scratch = ExAllocatePool2(POOL_FLAG_NON_PAGED, c->ScratchLength * sizeof(WCHAR), KI_TAG('C', 'o', 'n', 's'));
            if (!c->Scratch) {
                ExReleaseFastMutex(&g_con_lock);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        ULONG n;
        if (bytes) {
            ULONG consumed = 0;
            n = ConBytesToWide(c->OutputCp, byte_in, length, c->Scratch, c->ScratchLength, c->WriteCarry,
                               &c->WriteCarryLength, &consumed);
            byte_in += consumed;
            length -= consumed;
            if (!consumed && !n) {
                ExReleaseFastMutex(&g_con_lock);
                break;
            }
            CsWrite(b, c->Scratch, n);
        } else {
            n = length < c->ScratchLength ? length : c->ScratchLength;
            memcpy(c->Scratch, wide_in, n * sizeof(WCHAR));
            CsWrite(b, c->Scratch, n);
            wide_in += n;
            length -= n;
        }
        ConFlush(c);
        ExReleaseFastMutex(&g_con_lock);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS history_op(MIME_CONSOLE *c, PUCHAR buf, ULONG in_len, ULONG out_len, ULONG_PTR *information)
{
    if (in_len < sizeof(MIME_CONDRV_HISTORY))
        return STATUS_INVALID_PARAMETER;
    MIME_CONDRV_HISTORY rq = *(MIME_CONDRV_HISTORY *)buf;
    const WCHAR *exe = (const WCHAR *)(buf + sizeof(rq));
    /* the settings carry their flags where the other requests have the length of a name. */
    BOOLEAN has_name = rq.Op != MIME_CONDRV_HISTORY_GET_INFO && rq.Op != MIME_CONDRV_HISTORY_SET_INFO;
    if (has_name && (ULONG64)in_len < sizeof(rq) + (ULONG64)rq.ExeNameLength * sizeof(WCHAR))
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&g_con_lock);
    switch (rq.Op) {
    case MIME_CONDRV_HISTORY_GET_INFO: {
        if (out_len < sizeof(MIME_CONDRV_HISTORY_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        MIME_CONDRV_HISTORY_INFO info;
        unsigned size, buffers, flags;
        ConHistoryGetInfo(c->Lines, &size, &buffers, &flags);
        info.BufferSize = size;
        info.NumberOfBuffers = buffers;
        info.Flags = flags;
        memcpy(buf, &info, sizeof(info));
        *information = sizeof(info);
        break;
    }
    case MIME_CONDRV_HISTORY_SET_INFO:
        if (!ConHistorySetInfo(c->Lines, rq.Value, rq.Value2, rq.ExeNameLength))
            status = STATUS_INVALID_PARAMETER;
        break;
    case MIME_CONDRV_HISTORY_GET: {
        ULONG room = out_len / sizeof(WCHAR);
        size_t n = ConHistoryGet(c->Lines, exe, rq.ExeNameLength, (ConChar *)buf, room);
        *information = (n < room ? n : room) * sizeof(WCHAR);
        if (n > room)
            status = STATUS_BUFFER_OVERFLOW;
        break;
    }
    case MIME_CONDRV_HISTORY_LENGTH: {
        ULONG n = (ULONG)ConHistoryGet(c->Lines, exe, rq.ExeNameLength, nullptr, 0);
        if (out_len < sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        memcpy(buf, &n, sizeof(n));
        *information = sizeof(n);
        break;
    }
    case MIME_CONDRV_HISTORY_EXPUNGE:
        ConHistoryExpunge(c->Lines, exe, rq.ExeNameLength);
        break;
    case MIME_CONDRV_HISTORY_SET_COUNT:
        if (!ConHistorySetCount(c->Lines, exe, rq.ExeNameLength, rq.Value))
            status = STATUS_INVALID_PARAMETER;
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }
    ExReleaseFastMutex(&g_con_lock);
    return status;
}

static NTSTATUS alias_op(MIME_CONSOLE *c, PUCHAR buf, ULONG in_len, ULONG out_len, ULONG_PTR *information)
{
    if (in_len < sizeof(MIME_CONDRV_ALIAS))
        return STATUS_INVALID_PARAMETER;
    MIME_CONDRV_ALIAS rq = *(MIME_CONDRV_ALIAS *)buf;
    const WCHAR *source = (const WCHAR *)(buf + sizeof(rq));
    const WCHAR *target = source + rq.SourceLength;
    const WCHAR *exe = target + rq.TargetLength;
    ULONG64 need = sizeof(rq) + ((ULONG64)rq.SourceLength + rq.TargetLength + rq.ExeNameLength) * sizeof(WCHAR);
    if (need > in_len)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&g_con_lock);
    switch (rq.Op) {
    case MIME_CONDRV_ALIAS_ADD:
        if (!ConAliasAdd(c->Lines, exe, rq.ExeNameLength, source, rq.SourceLength, rq.TargetLength ? target : nullptr,
                         rq.TargetLength))
            status = STATUS_INVALID_PARAMETER;
        break;
    case MIME_CONDRV_ALIAS_GET: {
        ConChar tmp[1];
        (void)tmp;
        ULONG room = out_len / sizeof(WCHAR);
        long n = ConAliasGet(c->Lines, exe, rq.ExeNameLength, source, rq.SourceLength, (ConChar *)buf, room);
        if (n < 0) {
            status = STATUS_OBJECT_NAME_NOT_FOUND;
        } else {
            *information = ((ULONG)n < room ? (ULONG)n : room) * sizeof(WCHAR);
            if ((ULONG)n > room)
                status = STATUS_BUFFER_OVERFLOW;
            /* the full length is always told in the first word of the information. */
        }
        break;
    }
    case MIME_CONDRV_ALIAS_LIST: {
        ULONG room = out_len / sizeof(WCHAR);
        size_t n = ConAliasList(c->Lines, exe, rq.ExeNameLength, (ConChar *)buf, room);
        *information = (n < room ? n : room) * sizeof(WCHAR);
        if (n > room)
            status = STATUS_BUFFER_OVERFLOW;
        break;
    }
    case MIME_CONDRV_ALIAS_EXES: {
        ULONG room = out_len / sizeof(WCHAR);
        size_t n = ConAliasExes(c->Lines, (ConChar *)buf, room);
        *information = (n < room ? n : room) * sizeof(WCHAR);
        if (n > room)
            status = STATUS_BUFFER_OVERFLOW;
        break;
    }
    case MIME_CONDRV_ALIAS_EXPUNGE:
        ConAliasExpunge(c->Lines, exe, rq.ExeNameLength);
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }
    ExReleaseFastMutex(&g_con_lock);
    return status;
}

static void fill_info(CsBuffer *b, MIME_CONDRV_INFO *out)
{
    CsInfo info;
    CsGetInfo(b, &info);
    memset(out, 0, sizeof(*out));
    out->Size.X = info.Size.X;
    out->Size.Y = info.Size.Y;
    out->Cursor.X = info.Cursor.X;
    out->Cursor.Y = info.Cursor.Y;
    out->Window.Left = info.Window.Left;
    out->Window.Top = info.Window.Top;
    out->Window.Right = info.Window.Right;
    out->Window.Bottom = info.Window.Bottom;
    out->MaxWindow.X = info.MaxWindow.X;
    out->MaxWindow.Y = info.MaxWindow.Y;
    out->Attributes = info.Attributes;
    out->PopupAttributes = info.PopupAttributes;
    memcpy(out->ColorTable, info.ColorTable, sizeof(out->ColorTable));
    out->CursorSize = (ULONG)info.CursorSize;
    out->CursorVisible = info.CursorVisible;
    out->Mode = b->Mode;
}

static NTSTATUS NTAPI con_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUCHAR buf = irp->AssociatedIrp.SystemBuffer;
    PEPROCESS self = PsCurrentProcess();
    PFILE_OBJECT file = stack->FileObject;
    CON_HANDLE *h = device == g_device ? file->FsContext2 : nullptr;
    ULONG_PTR information = 0;
    NTSTATUS status = STATUS_SUCCESS;
    if (!h)
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    /* requests that work without a console. */
    switch (code) {
    case IOCTL_CONDRV_REGISTER:
        if (in_len < sizeof(MIME_CONDRV_REGISTER))
            return complete(irp, STATUS_INVALID_PARAMETER, 0);
        self->ConsoleCtrlRoutine = (PVOID)(ULONG_PTR)((MIME_CONDRV_REGISTER *)buf)->CtrlRoutine;
        return complete(irp, STATUS_SUCCESS, 0);
    case IOCTL_CONDRV_ATTACH:
        if (in_len < sizeof(ULONG))
            return complete(irp, STATUS_INVALID_PARAMETER, 0);
        return complete(irp, ConAttachProcess(self, *(PULONG)buf), 0);
    case IOCTL_CONDRV_FREE:
        ConsoleDetach(self);
        return complete(irp, STATUS_SUCCESS, 0);
    case IOCTL_CONDRV_ALLOC:
        if (in_len < sizeof(MIME_CONDRV_ALLOC))
            return complete(irp, STATUS_INVALID_PARAMETER, 0);
        return complete(irp, ConAllocFor(self, ((MIME_CONDRV_ALLOC *)buf)->Flags), 0);
    }

    BOOLEAN owned;
    MIME_CONSOLE *c = console_for(h, self, &owned);
    if (code == IOCTL_CONDRV_STATE) {
        if (out_len < sizeof(MIME_CONDRV_STATE)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            MIME_CONDRV_STATE *st = (MIME_CONDRV_STATE *)buf;
            memset(st, 0, sizeof(*st));
            if (c) {
                ExAcquireFastMutex(&g_con_lock);
                st->Flags = c->Flags | MIME_CONDRV_ATTACHED;
                st->InputMode = ConGetInputMode(c);
                st->OutputMode = c->Main->Mode;
                st->Columns = (USHORT)c->Term.Cols;
                st->Rows = (USHORT)c->Term.Rows;
                st->InputBytes = c->InputBytes;
                st->InputEvents = c->InputEvents;
                st->ProcessGroup = self->ConsoleGroup;
                ExReleaseFastMutex(&g_con_lock);
            }
            information = sizeof(*st);
        }
        if (owned)
            ConRelease(c);
        return complete(irp, status, information);
    }
    if (!c)
        return complete(irp, STATUS_INVALID_HANDLE, 0);

    BOOLEAN can_read = file->ReadAccess, can_write = file->WriteAccess;
    CsBuffer *b = h->Buffer;
    switch (code) {
    /* ---- any handle */
    case IOCTL_CONDRV_GENERATE:
        if (in_len < 2 * sizeof(ULONG) || ((PULONG)buf)[0] > CTRL_BREAK_EVENT)
            status = STATUS_INVALID_PARAMETER;
        else
            ConDispatchEvent(c, ((PULONG)buf)[0], ((PULONG)buf)[1]);
        break;
    case IOCTL_CONDRV_PROCESSES: {
        if (out_len < sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        PULONG out = (PULONG)buf;
        ULONG max = out_len / sizeof(ULONG) - 1;
        out[0] = ConListProcesses(c, out + 1, max);
        information = (1 + (out[0] < max ? out[0] : max)) * sizeof(ULONG);
        break;
    }
    case IOCTL_CONDRV_TITLE: {
        if (in_len < sizeof(MIME_CONDRV_TITLE)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        MIME_CONDRV_TITLE rq = *(MIME_CONDRV_TITLE *)buf;
        if (rq.Set) {
            if ((ULONG64)in_len < sizeof(rq) + (ULONG64)rq.Length * sizeof(WCHAR)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            ExAcquireFastMutex(&g_con_lock);
            ConSetTitle(c, (const WCHAR *)(buf + sizeof(rq)), rq.Length);
            ExReleaseFastMutex(&g_con_lock);
        } else {
            ExAcquireFastMutex(&g_con_lock);
            const WCHAR *title = rq.Original ? c->OriginalTitle : c->Title;
            ULONG length = (ULONG)wcslen(title);
            ULONG room = out_len / sizeof(WCHAR);
            ULONG n = length < room ? length : room;
            WCHAR copy[260];
            memcpy(copy, title, (length < 259 ? length : 259) * sizeof(WCHAR));
            ExReleaseFastMutex(&g_con_lock);
            if (out_len < sizeof(ULONG)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            /* the length first, then as many characters as fit. */
            ULONG chars = (n * sizeof(WCHAR) + sizeof(ULONG)) <= out_len ? n : (out_len - sizeof(ULONG)) / sizeof(WCHAR);
            memmove(buf + sizeof(ULONG), copy, chars * sizeof(WCHAR));
            *(PULONG)buf = length;
            information = sizeof(ULONG) + chars * sizeof(WCHAR);
        }
        break;
    }
    case IOCTL_CONDRV_CODE_PAGES: {
        if (in_len < sizeof(MIME_CONDRV_CODE_PAGES) || out_len < sizeof(MIME_CONDRV_CODE_PAGES)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        MIME_CONDRV_CODE_PAGES *p = (MIME_CONDRV_CODE_PAGES *)buf;
        ExAcquireFastMutex(&g_con_lock);
        if (p->Set & 1)
            c->InputCp = p->Input;
        if (p->Set & 2)
            c->OutputCp = p->Output;
        p->Input = c->InputCp;
        p->Output = c->OutputCp;
        ExReleaseFastMutex(&g_con_lock);
        information = sizeof(*p);
        break;
    }

    /* ---- the mode, of the input or of a screen buffer */
    case IOCTL_CONDRV_IN_MODE: {
        if (in_len < sizeof(MIME_CONDRV_MODE) || out_len < sizeof(MIME_CONDRV_MODE)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (h->Kind != CH_INPUT && h->Kind != CH_OUTPUT) {
            status = STATUS_INVALID_HANDLE;
            break;
        }
        MIME_CONDRV_MODE *m = (MIME_CONDRV_MODE *)buf;
        if (m->Set ? !can_write : !can_read) {
            status = STATUS_ACCESS_DENIED;
            break;
        }
        ExAcquireFastMutex(&g_con_lock);
        if (h->Kind == CH_INPUT) {
            if (m->Set)
                status = ConSetInputMode(c, m->Mode);
            m->Mode = ConGetInputMode(c);
        } else {
            CsBuffer *e = CsEffective(h->Buffer);
            if (m->Set) {
                if (m->Mode & ~(ULONG)CS_OUTPUT_MODE_MASK) {
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    BOOLEAN vt_was = (e->Mode & CS_VIRTUAL_TERMINAL_PROCESSING) != 0;
                    e->Mode = m->Mode;
                    /* an escape sequence cut by turning the sequences off is dropped. */
                    if (vt_was && !(m->Mode & CS_VIRTUAL_TERMINAL_PROCESSING))
                        CsParserReset(e);
                }
            }
            m->Mode = e->Mode;
        }
        ExReleaseFastMutex(&g_con_lock);
        information = sizeof(*m);
        break;
    }
    case IOCTL_CONDRV_OUT_MODE:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;

    /* ---- the input */
    case IOCTL_CONDRV_IN_READ:
    case IOCTL_CONDRV_IN_PEEK:
    case IOCTL_CONDRV_IN_RECORDS:
    case IOCTL_CONDRV_IN_WRITE:
    case IOCTL_CONDRV_IN_FLUSH:
    case IOCTL_CONDRV_IN_ANSI_CARRY:
    case IOCTL_CONDRV_IN_HISTORY:
    case IOCTL_CONDRV_IN_ALIAS:
        /* the history and the aliases belong to the console, and are asked of it by any file. */
        if (h->Kind != CH_INPUT && code != IOCTL_CONDRV_IN_HISTORY && code != IOCTL_CONDRV_IN_ALIAS) {
            status = STATUS_INVALID_HANDLE;
            break;
        }
        switch (code) {
        case IOCTL_CONDRV_IN_READ: {
            if (in_len < sizeof(MIME_CONDRV_READ) || out_len < sizeof(MIME_CONDRV_READ_RESULT)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (!can_read) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_READ rq = *(MIME_CONDRV_READ *)buf;
            PWCHAR text = (PWCHAR)(buf + sizeof(rq));
            ULONG64 need = sizeof(rq) + ((ULONG64)rq.InitialChars + rq.ExeNameLength) * sizeof(WCHAR);
            if (need > in_len) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            ULONG elem = (rq.Flags & MIME_CONDRV_READ_WIDE) ? sizeof(WCHAR) : 1;
            ULONG room = (out_len - sizeof(MIME_CONDRV_READ_RESULT)) / elem;
            if (room > rq.Length)
                room = rq.Length;
            /* the buffer is shared by the request and the answer, so what the read needs is kept first. */
            PWCHAR copy = nullptr;
            ULONG copy_chars = rq.InitialChars + rq.ExeNameLength;
            if (copy_chars) {
                copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, copy_chars * sizeof(WCHAR), KI_TAG('C', 'o', 'n', 'x'));
                if (!copy) {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                memcpy(copy, text, copy_chars * sizeof(WCHAR));
            }
            ULONG got = 0, key_state = 0;
            PUCHAR data = buf + sizeof(MIME_CONDRV_READ_RESULT);
            ULONG mask = rq.Control ? rq.WakeupMask : 0;
            if (rq.Flags & MIME_CONDRV_READ_WIDE)
                status = read_text(c, room, (PWCHAR)data, copy, rq.InitialChars, copy ? copy + rq.InitialChars : nullptr,
                                   rq.ExeNameLength, mask, &got, &key_state);
            else
                status = read_bytes(c, room, data, copy, rq.InitialChars, copy ? copy + rq.InitialChars : nullptr,
                                    rq.ExeNameLength, mask, &got, &key_state);
            if (copy)
                ExFreePool(copy);
            MIME_CONDRV_READ_RESULT *res = (MIME_CONDRV_READ_RESULT *)buf;
            res->ControlKeyState = key_state;
            res->Reserved = 0;
            information = sizeof(*res) + got * elem;
            break;
        }
        case IOCTL_CONDRV_IN_PEEK: {
            BOOLEAN eof;
            ULONG acked;
            ExAcquireFastMutex(&g_con_lock);
            information = ConTakeInput(c, buf, out_len, TRUE, &eof, &acked);
            ExReleaseFastMutex(&g_con_lock);
            break;
        }
        case IOCTL_CONDRV_IN_RECORDS: {
            if (in_len < sizeof(ULONG) || out_len < sizeof(ULONG)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (!can_read) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            ULONG flags = *(PULONG)buf;
            ULONG room = (out_len - sizeof(ULONG)) / sizeof(MIME_CONDRV_RECORD);
            MIME_CONDRV_RECORD *records = (MIME_CONDRV_RECORD *)(buf + sizeof(ULONG));
            BOOLEAN peek = (flags & MIME_CONDRV_READ_PEEK) != 0;
            for (;;) {
                ExAcquireFastMutex(&g_con_lock);
                BOOLEAN eof;
                ULONG acked;
                ULONG n = ConTakeRecords(c, records, room, peek, &eof, &acked);
                *(PULONG)buf = c->InputEvents;
                BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0;
                ConAfterRead(c, acked, !n && !eof);
                ExReleaseFastMutex(&g_con_lock);
                if (n || eof || closed || !room || (flags & MIME_CONDRV_READ_NOWAIT)) {
                    information = sizeof(ULONG) + n * sizeof(MIME_CONDRV_RECORD);
                    break;
                }
                status = wait_ready(c);
                if (status != STATUS_SUCCESS)
                    break;
            }
            break;
        }
        case IOCTL_CONDRV_IN_WRITE: {
            if (!can_write) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            ULONG count = in_len >= sizeof(MIME_CONDRV_WRITE_INPUT)
                              ? (in_len - sizeof(MIME_CONDRV_WRITE_INPUT)) / sizeof(MIME_CONDRV_RECORD)
                              : 0;
            MIME_CONDRV_RECORD *records = (MIME_CONDRV_RECORD *)(buf + sizeof(MIME_CONDRV_WRITE_INPUT));
            if (in_len < sizeof(MIME_CONDRV_WRITE_INPUT) || out_len < sizeof(ULONG)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            /* events as WriteConsoleInput keeps them: known types only, the flags a console stores as booleans. */
            BOOLEAN valid = TRUE;
            for (ULONG i = 0; i < count && valid; i++) {
                MIME_CONDRV_RECORD *r = &records[i];
                r->Reserved = 0;
                switch (r->EventType) {
                case CON_KEY_EVENT:
                    r->Key.KeyDown = r->Key.KeyDown != 0;
                    break;
                case CON_MOUSE_EVENT:
                    break;
                case CON_FOCUS_EVENT:
                    r->Value = r->Value != 0;
                    [[fallthrough]];
                case CON_WINDOW_BUFFER_SIZE_EVENT:
                case CON_MENU_EVENT: {
                    ULONG value = r->Value;
                    memset(&r->Key, 0, sizeof(r->Key));
                    r->Value = value;
                    break;
                }
                default:
                    valid = FALSE;
                    break;
                }
            }
            if (!valid) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            BOOLEAN append = (((MIME_CONDRV_WRITE_INPUT *)buf)->Flags & MIME_CONDRV_WRITE_APPEND) != 0;
            ULONG written = 0;
            ExAcquireFastMutex(&g_con_lock);
            status = ConWriteRecords(c, records, count, !append, &written);
            ExReleaseFastMutex(&g_con_lock);
            *(PULONG)buf = written;
            information = sizeof(ULONG);
            break;
        }
        case IOCTL_CONDRV_IN_ANSI_CARRY: {
            MIME_CONDRV_RECORD kept;
            ExAcquireFastMutex(&g_con_lock);
            BOOLEAN had = c->HasAnsiCarry;
            kept = c->AnsiCarry;
            c->HasAnsiCarry = in_len >= sizeof(MIME_CONDRV_RECORD);
            if (c->HasAnsiCarry)
                c->AnsiCarry = *(MIME_CONDRV_RECORD *)buf;
            ExReleaseFastMutex(&g_con_lock);
            if (had && out_len >= sizeof(kept)) {
                *(MIME_CONDRV_RECORD *)buf = kept;
                information = sizeof(kept);
            }
            break;
        }
        case IOCTL_CONDRV_IN_FLUSH:
            if (!can_write) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            ExAcquireFastMutex(&g_con_lock);
            ConFlushInput(c);
            if (c->LinePending) {
                ExFreePool(c->LinePending);
                c->LinePending = nullptr;
                c->LinePendingLength = 0;
            }
            ExReleaseFastMutex(&g_con_lock);
            break;
        case IOCTL_CONDRV_IN_HISTORY:
            status = history_op(c, buf, in_len, out_len, &information);
            break;
        case IOCTL_CONDRV_IN_ALIAS:
            status = alias_op(c, buf, in_len, out_len, &information);
            break;
        }
        break;

    /* ---- the screen buffers */
    case IOCTL_CONDRV_OUT_WRITE:
    case IOCTL_CONDRV_OUT_INFO:
    case IOCTL_CONDRV_OUT_SIZE:
    case IOCTL_CONDRV_OUT_CURSOR:
    case IOCTL_CONDRV_OUT_WINDOW:
    case IOCTL_CONDRV_OUT_ATTRIBUTE:
    case IOCTL_CONDRV_OUT_READ_RECT:
    case IOCTL_CONDRV_OUT_WRITE_RECT:
    case IOCTL_CONDRV_OUT_READ_RUN:
    case IOCTL_CONDRV_OUT_WRITE_RUN:
    case IOCTL_CONDRV_OUT_FILL:
    case IOCTL_CONDRV_OUT_SCROLL:
    case IOCTL_CONDRV_OUT_ACTIVATE:
    case IOCTL_CONDRV_OUT_FONT: {
        if (h->Kind != CH_OUTPUT) {
            status = STATUS_INVALID_HANDLE;
            break;
        }
        switch (code) {
        case IOCTL_CONDRV_OUT_WRITE: {
            if (in_len < sizeof(MIME_CONDRV_WRITE) || !can_write) {
                status = in_len < sizeof(MIME_CONDRV_WRITE) ? STATUS_INVALID_PARAMETER : STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_WRITE rq = *(MIME_CONDRV_WRITE *)buf;
            BOOLEAN bytes = (rq.Flags & MIME_CONDRV_WRITE_BYTES) != 0;
            if ((ULONG64)in_len < sizeof(rq) + (ULONG64)rq.Length * (bytes ? 1 : sizeof(WCHAR))) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            status = write_text(c, b, bytes, buf + sizeof(rq), rq.Length);
            information = rq.Length;
            break;
        }
        case IOCTL_CONDRV_OUT_INFO: {
            if (in_len < sizeof(MIME_CONDRV_INFO_REQUEST)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            BOOLEAN set = ((MIME_CONDRV_INFO_REQUEST *)buf)->Set != 0;
            if (set ? !can_write : !can_read) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            ExAcquireFastMutex(&g_con_lock);
            if (set) {
                if (in_len < sizeof(MIME_CONDRV_INFO_REQUEST) + sizeof(MIME_CONDRV_INFO)) {
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    MIME_CONDRV_INFO *in = (MIME_CONDRV_INFO *)(buf + sizeof(MIME_CONDRV_INFO_REQUEST));
                    CsInfo info = {};
                    info.Size.X = in->Size.X;
                    info.Size.Y = in->Size.Y;
                    info.Cursor.X = in->Cursor.X;
                    info.Cursor.Y = in->Cursor.Y;
                    info.Window.Left = in->Window.Left;
                    info.Window.Top = in->Window.Top;
                    info.Window.Right = in->Window.Right;
                    info.Window.Bottom = in->Window.Bottom;
                    info.Attributes = in->Attributes;
                    info.PopupAttributes = in->PopupAttributes;
                    memcpy(info.ColorTable, in->ColorTable, sizeof(info.ColorTable));
                    status = to_status(CsSetInfo(b, &info));
                    ConFlush(c);
                }
            } else if (out_len < sizeof(MIME_CONDRV_INFO)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                MIME_CONDRV_INFO info;
                fill_info(CsEffective(b), &info);
                memcpy(buf, &info, sizeof(info));
                information = sizeof(info);
            }
            ExReleaseFastMutex(&g_con_lock);
            break;
        }
        case IOCTL_CONDRV_OUT_SIZE: {
            if (in_len < sizeof(MIME_CONDRV_COORD)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (!can_write) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_COORD size = *(MIME_CONDRV_COORD *)buf;
            ExAcquireFastMutex(&g_con_lock);
            status = to_status(CsSetSize(b, (CsCoord){size.X, size.Y}));
            ConFlush(c);
            ExReleaseFastMutex(&g_con_lock);
            break;
        }
        case IOCTL_CONDRV_OUT_CURSOR: {
            if (in_len < sizeof(MIME_CONDRV_CURSOR) || out_len < sizeof(MIME_CONDRV_CURSOR)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            MIME_CONDRV_CURSOR *cur = (MIME_CONDRV_CURSOR *)buf;
            if (cur->Set ? !can_write : !can_read) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            ExAcquireFastMutex(&g_con_lock);
            CsBuffer *e = CsEffective(b);
            if (cur->Set) {
                MIME_CONDRV_CURSOR rq = *cur;
                if (rq.Size)
                    status = to_status(CsSetCursorInfo(b, (int)rq.Size, rq.Visible != 0));
                else
                    status = to_status(CsSetCursorPosition(b, (CsCoord){rq.Position.X, rq.Position.Y}));
                ConFlush(c);
            } else {
                cur->Position.X = (SHORT)e->CursorX;
                cur->Position.Y = (SHORT)e->CursorY;
                cur->Size = (ULONG)e->CursorSize;
                cur->Visible = e->CursorVisible;
            }
            ExReleaseFastMutex(&g_con_lock);
            information = sizeof(*cur);
            break;
        }
        case IOCTL_CONDRV_OUT_WINDOW: {
            if (in_len < sizeof(MIME_CONDRV_WINDOW) || !can_write) {
                status = in_len < sizeof(MIME_CONDRV_WINDOW) ? STATUS_INVALID_PARAMETER : STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_WINDOW w = *(MIME_CONDRV_WINDOW *)buf;
            ExAcquireFastMutex(&g_con_lock);
            status = to_status(CsSetWindow(b, w.Absolute != 0, (CsRect){w.Rect.Left, w.Rect.Top, w.Rect.Right, w.Rect.Bottom}));
            ConFlush(c);
            ExReleaseFastMutex(&g_con_lock);
            break;
        }
        case IOCTL_CONDRV_OUT_ATTRIBUTE:
            if (in_len < sizeof(USHORT) || !can_write) {
                status = in_len < sizeof(USHORT) ? STATUS_INVALID_PARAMETER : STATUS_ACCESS_DENIED;
                break;
            }
            ExAcquireFastMutex(&g_con_lock);
            CsSetAttributes(b, *(PUSHORT)buf);
            ExReleaseFastMutex(&g_con_lock);
            break;
        case IOCTL_CONDRV_OUT_READ_RECT: {
            if (in_len < sizeof(MIME_CONDRV_RECT_IO) || !can_read) {
                status = in_len < sizeof(MIME_CONDRV_RECT_IO) ? STATUS_INVALID_PARAMETER : STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_RECT_IO rq = *(MIME_CONDRV_RECT_IO *)buf;
            LONG w = (LONG)rq.Rect.Right - rq.Rect.Left + 1, hgt = (LONG)rq.Rect.Bottom - rq.Rect.Top + 1;
            ULONG64 cells = w > 0 && hgt > 0 ? (ULONG64)w * (ULONG64)hgt : 0;
            if (out_len < sizeof(MIME_CONDRV_RECT) || cells * sizeof(MIME_CONDRV_CELL) + sizeof(MIME_CONDRV_RECT) > out_len) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            CsRect rect = {rq.Rect.Left, rq.Rect.Top, rq.Rect.Right, rq.Rect.Bottom};
            ExAcquireFastMutex(&g_con_lock);
            status = to_status(CsReadOutput(b, (CsCharInfo *)(buf + sizeof(MIME_CONDRV_RECT)), &rect));
            ExReleaseFastMutex(&g_con_lock);
            MIME_CONDRV_RECT out = {rect.Left, rect.Top, rect.Right, rect.Bottom};
            memcpy(buf, &out, sizeof(out));
            information = sizeof(out) + cells * sizeof(MIME_CONDRV_CELL);
            break;
        }
        case IOCTL_CONDRV_OUT_WRITE_RECT: {
            if (in_len < sizeof(MIME_CONDRV_RECT_IO) || out_len < sizeof(MIME_CONDRV_RECT) || !can_write) {
                status = in_len < sizeof(MIME_CONDRV_RECT_IO) || out_len < sizeof(MIME_CONDRV_RECT) ? STATUS_INVALID_PARAMETER
                                                                                                      : STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_RECT_IO rq = *(MIME_CONDRV_RECT_IO *)buf;
            LONG w = (LONG)rq.Rect.Right - rq.Rect.Left + 1, hgt = (LONG)rq.Rect.Bottom - rq.Rect.Top + 1;
            ULONG64 cells = w > 0 && hgt > 0 ? (ULONG64)w * (ULONG64)hgt : 0;
            if (sizeof(rq) + cells * sizeof(MIME_CONDRV_CELL) > in_len) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            CsRect rect = {rq.Rect.Left, rq.Rect.Top, rq.Rect.Right, rq.Rect.Bottom};
            ExAcquireFastMutex(&g_con_lock);
            status = to_status(CsWriteOutput(b, (const CsCharInfo *)(buf + sizeof(rq)), &rect));
            ConFlush(c);
            ExReleaseFastMutex(&g_con_lock);
            MIME_CONDRV_RECT out = {rect.Left, rect.Top, rect.Right, rect.Bottom};
            memcpy(buf, &out, sizeof(out));
            information = sizeof(out);
            break;
        }
        case IOCTL_CONDRV_OUT_READ_RUN: {
            if (in_len < sizeof(MIME_CONDRV_RUN) || out_len < sizeof(ULONG) || !can_read) {
                status = in_len < sizeof(MIME_CONDRV_RUN) || out_len < sizeof(ULONG) ? STATUS_INVALID_PARAMETER : STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_RUN rq = *(MIME_CONDRV_RUN *)buf;
            ULONG room = (out_len - sizeof(ULONG)) / sizeof(USHORT);
            if (rq.Count > room)
                rq.Count = room;
            ExAcquireFastMutex(&g_con_lock);
            size_t n = rq.Kind == MIME_CONDRV_RUN_CHARS
                           ? CsReadChars(b, (CsCoord){rq.Start.X, rq.Start.Y}, (uint16_t *)(buf + sizeof(ULONG)), rq.Count)
                           : CsReadAttributes(b, (CsCoord){rq.Start.X, rq.Start.Y}, (uint16_t *)(buf + sizeof(ULONG)), rq.Count);
            ExReleaseFastMutex(&g_con_lock);
            *(PULONG)buf = (ULONG)n;
            information = sizeof(ULONG) + n * sizeof(USHORT);
            break;
        }
        case IOCTL_CONDRV_OUT_WRITE_RUN: {
            if (in_len < sizeof(MIME_CONDRV_RUN) || out_len < sizeof(ULONG) || !can_write) {
                status = in_len < sizeof(MIME_CONDRV_RUN) || out_len < sizeof(ULONG) ? STATUS_INVALID_PARAMETER : STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_RUN rq = *(MIME_CONDRV_RUN *)buf;
            if ((ULONG64)in_len < sizeof(rq) + (ULONG64)rq.Count * sizeof(USHORT)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            size_t n = 0;
            ExAcquireFastMutex(&g_con_lock);
            status = to_status(rq.Kind == MIME_CONDRV_RUN_CHARS
                                   ? CsWriteChars(b, (CsCoord){rq.Start.X, rq.Start.Y}, (const uint16_t *)(buf + sizeof(rq)), rq.Count, &n)
                                   : CsWriteAttributes(b, (CsCoord){rq.Start.X, rq.Start.Y}, (const uint16_t *)(buf + sizeof(rq)), rq.Count, &n));
            ConFlush(c);
            ExReleaseFastMutex(&g_con_lock);
            *(PULONG)buf = (ULONG)n;
            information = sizeof(ULONG);
            break;
        }
        case IOCTL_CONDRV_OUT_FILL: {
            if (in_len < sizeof(MIME_CONDRV_FILL) || out_len < sizeof(ULONG) || !can_write) {
                status = in_len < sizeof(MIME_CONDRV_FILL) || out_len < sizeof(ULONG) ? STATUS_INVALID_PARAMETER : STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_FILL rq = *(MIME_CONDRV_FILL *)buf;
            ExAcquireFastMutex(&g_con_lock);
            size_t n = rq.Kind == MIME_CONDRV_RUN_CHARS ? CsFillChars(b, (CsCoord){rq.Start.X, rq.Start.Y}, rq.Value, rq.Count)
                                                        : CsFillAttributes(b, (CsCoord){rq.Start.X, rq.Start.Y}, rq.Value, rq.Count);
            ConFlush(c);
            ExReleaseFastMutex(&g_con_lock);
            *(PULONG)buf = (ULONG)n;
            information = sizeof(ULONG);
            break;
        }
        case IOCTL_CONDRV_OUT_SCROLL: {
            if (in_len < sizeof(MIME_CONDRV_SCROLL) || !can_write) {
                status = in_len < sizeof(MIME_CONDRV_SCROLL) ? STATUS_INVALID_PARAMETER : STATUS_ACCESS_DENIED;
                break;
            }
            MIME_CONDRV_SCROLL rq = *(MIME_CONDRV_SCROLL *)buf;
            CsRect source = {rq.Source.Left, rq.Source.Top, rq.Source.Right, rq.Source.Bottom};
            CsRect clip = {rq.Clip.Left, rq.Clip.Top, rq.Clip.Right, rq.Clip.Bottom};
            CsCharInfo fill = {rq.Fill.Char, rq.Fill.Attributes};
            ExAcquireFastMutex(&g_con_lock);
            status = to_status(CsScroll(b, source, rq.HasClip ? &clip : nullptr, (CsCoord){rq.Destination.X, rq.Destination.Y}, fill));
            ConFlush(c);
            ExReleaseFastMutex(&g_con_lock);
            break;
        }
        case IOCTL_CONDRV_OUT_ACTIVATE:
            if (!can_write) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            ExAcquireFastMutex(&g_con_lock);
            CsShow(&c->Term, b);
            CsRender(&c->Term);
            ConFlush(c);
            ExReleaseFastMutex(&g_con_lock);
            break;
        case IOCTL_CONDRV_OUT_FONT: {
            if (out_len < sizeof(MIME_CONDRV_FONT)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            MIME_CONDRV_FONT *font = (MIME_CONDRV_FONT *)buf;
            memset(font, 0, sizeof(*font));
            font->Size.X = 8;
            font->Size.Y = 16;
            font->Weight = 400;
            font->Family = 0x36;
            wcscpy(font->Face, L"Consolas");
            information = sizeof(*font);
            break;
        }
        }
        break;
    }

    /* ---- a pseudo console */
    case IOCTL_CONDRV_PTY_CREATE:
    case IOCTL_CONDRV_PTY_OUTPUT:
    case IOCTL_CONDRV_PTY_INPUT:
    case IOCTL_CONDRV_PTY_RESIZE:
    case IOCTL_CONDRV_PTY_CLOSE:
        if (h->Kind != CH_PTY) {
            status = STATUS_INVALID_HANDLE;
            break;
        }
        switch (code) {
        case IOCTL_CONDRV_PTY_CREATE: {
            if (in_len < sizeof(MIME_CONDRV_PTY_CREATE)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            MIME_CONDRV_PTY_CREATE rq = *(MIME_CONDRV_PTY_CREATE *)buf;
            if (rq.Size.X <= 0 || rq.Size.Y <= 0) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            ConPtyStart(c, (ULONG)(USHORT)rq.Size.X | ((ULONG)(USHORT)rq.Size.Y << 16), rq.Flags);
            break;
        }
        case IOCTL_CONDRV_PTY_OUTPUT:
            for (;;) {
                BOOLEAN empty = ConListProcesses(c, nullptr, 0) == 0;
                ExAcquireFastMutex(&g_con_lock);
                ULONG done = 0;
                while (!IsListEmpty(&c->PtyOut) && done < out_len) {
                    CON_PTY_CHUNK *k = CONTAINING_RECORD(c->PtyOut.Flink, CON_PTY_CHUNK, Links);
                    ULONG n = k->Length < out_len - done ? k->Length : out_len - done;
                    memcpy(buf + done, k->Data, n);
                    done += n;
                    if (n == k->Length) {
                        RemoveEntryList(&k->Links);
                        ExFreePool(k);
                    } else {
                        memmove(k->Data, k->Data + n, k->Length - n);
                        k->Length -= n;
                    }
                    c->PtyQueued -= n;
                }
                if (IsListEmpty(&c->PtyOut))
                    KeClearEvent(&c->PtyReady);
                if (c->PtyQueued <= 256 * 1024)
                    KeSetEvent(&c->PtySpace, 0, FALSE);
                /* a closed console goes on talking while a process is left in it. */
                BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0 && empty;
                ExReleaseFastMutex(&g_con_lock);
                if (done || closed || !out_len) {
                    information = done;
                    break;
                }
                status = KeWaitForSingleObject(&c->PtyReady, UserRequest, UserMode, FALSE, nullptr);
                if (status != STATUS_SUCCESS)
                    break;
            }
            break;
        case IOCTL_CONDRV_PTY_INPUT: {
            BOOLEAN ctrl_c = FALSE;
            ExAcquireFastMutex(&g_con_lock);
            ULONG off = 0;
            while (off < in_len) {
                ULONG n = in_len - off > 4096 ? 4096 : in_len - off;
                ConHostInputLocked(c, buf + off, n, &ctrl_c);
                off += n;
            }
            ExReleaseFastMutex(&g_con_lock);
            if (ctrl_c)
                ConDispatchEvent(c, CTRL_C_EVENT, 0);
            break;
        }
        case IOCTL_CONDRV_PTY_RESIZE:
            if (in_len < sizeof(MIME_CONDRV_COORD)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            ConResize(c, (ULONG)(USHORT)((MIME_CONDRV_COORD *)buf)->X | ((ULONG)(USHORT)((MIME_CONDRV_COORD *)buf)->Y << 16));
            break;
        case IOCTL_CONDRV_PTY_CLOSE:
            ConClose(c);
            break;
        }
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    if (owned)
        ConRelease(c);
    return complete(irp, status, information);
}

/* ------------------------------------------------------------ reads and writes of the handles */

static ULONG stream_of(PIRP irp)
{
    return (ULONG)(ULONG_PTR)IoGetCurrentIrpStackLocation(irp)->FileObject->FsContext2;
}

/* waits for input without taking it, as a zero byte read of a pipe does. */
static NTSTATUS wait_input(MIME_CONSOLE *c)
{
    for (;;) {
        ExAcquireFastMutex(&g_con_lock);
        BOOLEAN ready = !IsListEmpty(&c->Input) || (c->Flags & MIME_CONDRV_CLOSED);
        if (!ready)
            KeClearEvent(&c->InputReady);
        ExReleaseFastMutex(&g_con_lock);
        if (ready)
            return STATUS_SUCCESS;
        NTSTATUS status = wait_ready(c);
        if (status != STATUS_SUCCESS)
            return status;
    }
}

/* bytes of redirected input, waiting for some. */
static NTSTATUS stream_read(MIME_CONSOLE *c, PUCHAR out, ULONG length, ULONG_PTR *information)
{
    for (;;) {
        ExAcquireFastMutex(&g_con_lock);
        BOOLEAN eof;
        ULONG acked;
        ULONG n = ConTakeInput(c, out, length, FALSE, &eof, &acked);
        BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0;
        ConAfterRead(c, acked, !n && !eof);
        ExReleaseFastMutex(&g_con_lock);
        if (n || eof || closed || !length) {
            *information = n;
            return STATUS_SUCCESS;
        }
        NTSTATUS status = wait_ready(c);
        if (status != STATUS_SUCCESS)
            return status;
    }
}

static NTSTATUS NTAPI con_read_write(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG length = stack->Parameters.Read.Length;
    BOOLEAN write = stack->MajorFunction == IRP_MJ_WRITE;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    if (device == g_device) {
        CON_HANDLE *h = handle_of(irp);
        if (!h || (write ? h->Kind != CH_OUTPUT : h->Kind != CH_INPUT))
            return complete(irp, STATUS_INVALID_HANDLE, 0);
        MIME_CONSOLE *c = h->Console;
        if (write) {
            if (!stack->FileObject->WriteAccess)
                return complete(irp, STATUS_ACCESS_DENIED, 0);
            status = write_text(c, h->Buffer, TRUE, irp->UserBuffer, length);
            if (NT_SUCCESS(status))
                information = length;
        } else {
            if (!stack->FileObject->ReadAccess)
                return complete(irp, STATUS_ACCESS_DENIED, 0);
            ULONG got = 0, key_state = 0;
            status = read_bytes(c, length, irp->UserBuffer, nullptr, 0, nullptr, 0, 0, &got, &key_state);
            /* ReadFile, unlike ReadConsole, takes a read that starts with ctrl+z as the end of the input. */
            if (NT_SUCCESS(status) && got && (c->InputMode & ENABLE_PROCESSED_INPUT) && ((PUCHAR)irp->UserBuffer)[0] == 0x1A)
                got = 0;
            information = got;
        }
        return complete(irp, status, information);
    }
    ULONG stream = stream_of(irp);
    if (device != g_stream_device || (write ? stream == STREAM_INPUT : stream != STREAM_INPUT))
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    MIME_CONSOLE *c = ConProcessConsole(PsCurrentProcess());
    if (!c)
        return complete(irp, STATUS_PIPE_BROKEN, 0);
    if (write) {
        if (c->Flags & MIME_CONDRV_CLOSED) {
            status = STATUS_PIPE_BROKEN;
        } else if (length && (status = wait_output(c)) == STATUS_SUCCESS) {
            ExAcquireFastMutex(&g_con_lock);
            ConSendOutput(c, irp->UserBuffer, length, stream == STREAM_ERROR ? MIME_CONDRV_STDERR : MIME_CONDRV_STDOUT);
            ExReleaseFastMutex(&g_con_lock);
            information = length;
        }
    } else if (!length) {
        status = wait_input(c);
    } else {
        status = stream_read(c, irp->UserBuffer, length, &information);
        /* the end of the input is the other end of the pipe closing. */
        if (NT_SUCCESS(status) && !information)
            status = STATUS_PIPE_BROKEN;
    }
    ConRelease(c);
    return complete(irp, status, information);
}

typedef struct STREAM_PIPE_LOCAL {
    ULONG NamedPipeType;
    ULONG NamedPipeConfiguration;
    ULONG MaximumInstances;
    ULONG CurrentInstances;
    ULONG InboundQuota;
    ULONG ReadDataAvailable;
    ULONG OutboundQuota;
    ULONG WriteQuotaAvailable;
    ULONG NamedPipeState;
    ULONG NamedPipeEnd;
} STREAM_PIPE_LOCAL;

static ULONG input_available(void)
{
    MIME_CONSOLE *c = ConProcessConsole(PsCurrentProcess());
    if (!c)
        return 0;
    ExAcquireFastMutex(&g_con_lock);
    ULONG n = c->InputBytes;
    ExReleaseFastMutex(&g_con_lock);
    ConRelease(c);
    return n;
}

/* a byte mode pipe in blocking mode, the client end, always connected. */
static NTSTATUS NTAPI con_query(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG length = stack->Parameters.QueryFile.Length;
    PVOID buf = irp->AssociatedIrp.SystemBuffer;
    if (device != g_stream_device)
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    BOOLEAN input = stream_of(irp) == STREAM_INPUT;
    switch (stack->Parameters.QueryFile.FileInformationClass) {
    case FilePipeInformation:
        if (length < 2 * sizeof(ULONG))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        memset(buf, 0, 2 * sizeof(ULONG));
        return complete(irp, STATUS_SUCCESS, 2 * sizeof(ULONG));
    case FilePipeLocalInformation: {
        if (length < sizeof(STREAM_PIPE_LOCAL))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        STREAM_PIPE_LOCAL p = {0, input ? 0 : 1, 1, 1, 4096, input ? input_available() : 0, 4096, 4096,
                               FILE_PIPE_CONNECTED_STATE, 0};
        memcpy(buf, &p, sizeof(p));
        return complete(irp, STATUS_SUCCESS, sizeof(p));
    }
    case FileStandardInformation: {
        if (length < sizeof(FILE_STANDARD_INFORMATION))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        FILE_STANDARD_INFORMATION s = {};
        s.AllocationSize.QuadPart = 4096;
        s.EndOfFile.QuadPart = input ? input_available() : 0;
        s.NumberOfLinks = 1;
        memcpy(buf, &s, sizeof(s));
        return complete(irp, STATUS_SUCCESS, sizeof(s));
    }
    default:
        return complete(irp, STATUS_INVALID_PARAMETER, 0);
    }
}

/* the modes of a pipe end: byte reads, blocking or not, both accepted as they change nothing here. */
static NTSTATUS NTAPI con_set(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    if (device != g_stream_device || stack->Parameters.SetFile.FileInformationClass != FilePipeInformation)
        return complete(irp, STATUS_INVALID_PARAMETER, 0);
    return complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI con_fs_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUCHAR buf = irp->AssociatedIrp.SystemBuffer;
    if (device != g_stream_device || code != FSCTL_PIPE_PEEK)
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    /* the header: state, bytes available, messages and message length; then as much data as fits. */
    if (out_len < 4 * sizeof(ULONG))
        return complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
    ULONG *head = (ULONG *)buf;
    memset(head, 0, 4 * sizeof(ULONG));
    head[0] = FILE_PIPE_CONNECTED_STATE;
    ULONG copied = 0;
    MIME_CONSOLE *c = stream_of(irp) == STREAM_INPUT ? ConProcessConsole(PsCurrentProcess()) : nullptr;
    if (c) {
        BOOLEAN eof;
        ULONG acked;
        ExAcquireFastMutex(&g_con_lock);
        head[1] = c->InputBytes;
        copied = ConTakeInput(c, buf + 4 * sizeof(ULONG), out_len - 4 * sizeof(ULONG), TRUE, &eof, &acked);
        /* the end of the input is next when its empty chunk heads the queue, whatever room the peek has. */
        CON_CHUNK *first = IsListEmpty(&c->Input) ? nullptr : CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links);
        BOOLEAN at_end = first && !first->Records && !first->Length;
        BOOLEAN broken = at_end || (!c->InputBytes && (c->Flags & MIME_CONDRV_CLOSED));
        ExReleaseFastMutex(&g_con_lock);
        ConRelease(c);
        if (broken)
            return complete(irp, STATUS_PIPE_BROKEN, 0);
    }
    return complete(irp, copied < head[1] ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS, 4 * sizeof(ULONG) + copied);
}

static NTSTATUS NTAPI ConDrvEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = con_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = con_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = con_close;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = con_control;
    driver->MajorFunction[IRP_MJ_READ] = con_read_write;
    driver->MajorFunction[IRP_MJ_WRITE] = con_read_write;
    driver->MajorFunction[IRP_MJ_QUERY_INFORMATION] = con_query;
    driver->MajorFunction[IRP_MJ_SET_INFORMATION] = con_set;
    driver->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = con_fs_control;
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_CONDRV_DEVICE);
    NTSTATUS status = IoCreateDevice(driver, 0, &name, 0x50 /* FILE_DEVICE_CONSOLE */, 0, FALSE, &g_device);
    UNICODE_STRING stream = RTL_CONSTANT_STRING(MIME_CONDRV_STREAM_DEVICE);
    if (NT_SUCCESS(status))
        status = IoCreateDevice(driver, 0, &stream, FILE_DEVICE_NAMED_PIPE, 0, FALSE, &g_stream_device);
    return status;
}

void IopInitializeConsoleDevice(void)
{
    NTSTATUS status = IoCreateDriver(L"\\Driver\\ConDrv", ConDrvEntry, nullptr);
    if (!NT_SUCCESS(status))
        KiLogPrintf("condrv failed to start: %08x", status);
}
