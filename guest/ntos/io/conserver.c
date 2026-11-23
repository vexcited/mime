/*
 * the windows console protocol, as genuine kernelbase speaks it to \Device\ConDrv. a call is IOCTL_CONDRV_ISSUE_USER_IO
 * with a CD_USER_DEFINED_IO: the object handle, then input buffers and output buffers. the first input is the message
 * header and its descriptor, the first output the descriptor again, the others carry text, cells or records. on windows
 * conhost.exe serves these, here they become the device controls of mime/condrv.h on the same console.
 */
#include "console_p.h"

#define IOCTL_CONDRV_ISSUE_USER_IO 0x500016
#define IOCTL_CONDRV_GET_SERVER_PID 0x500023
#define IOCTL_CONDRV_LAUNCH_SERVER 0x500037

#define STATUS_ILLEGAL_FUNCTION ((NTSTATUS)0xC00000AFL)

#define MAX_DESCRIPTOR 256
#define MAX_PAYLOAD (64u << 20)

typedef struct CD_IO_BUFFER64 {
    ULONG Size;
    ULONG Pad;
    ULONG64 Buffer;
} CD_IO_BUFFER64;

typedef struct CD_USER_DEFINED_IO64 {
    ULONG64 Client;
    ULONG InputCount;
    ULONG OutputCount;
    CD_IO_BUFFER64 Buffers[];
} CD_USER_DEFINED_IO64;

typedef struct CON_MSG {
    PFILE_OBJECT Object; /* the console file the call is about, the connection for calls about the console */
    MIME_CONSOLE *Console;
    ULONG Api;
    ULONG BodySize;
    UCHAR Body[MAX_DESCRIPTOR];
    PUCHAR In;
    ULONG InSize;
    PUCHAR Out;
    ULONG OutSize;
    ULONG OutUsed;
} CON_MSG;

#define FIELD(m, type, offset) (*(type *)((m)->Body + (offset)))

BOOLEAN ConIsDriverControl(ULONG code)
{
    return (code >> 16) == 0x50 && ((code >> 2) & 0xFFF) < 0x800;
}

static void *pool(ULONG size)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('C', 'o', 'n', 'm'));
}

/* a device control of mime/condrv.h on the object, through a pool block that holds the request and the answer. */
static NTSTATUS call(CON_MSG *m, ULONG code, PVOID block, ULONG in_len, ULONG out_len, ULONG_PTR *information)
{
    ULONG_PTR ignored;
    return ConControl(m->Object, code, block, in_len, out_len, information ? information : &ignored);
}

static UINT code_page(CON_MSG *m, BOOLEAN input)
{
    if (!m->Console)
        return 437;
    ExAcquireFastMutex(&g_con_lock);
    UINT cp = input ? m->Console->InputCp : m->Console->OutputCp;
    ExReleaseFastMutex(&g_con_lock);
    return cp;
}

/* one character of a code page, as a wide one. */
static WCHAR byte_to_wide(UINT cp, UCHAR byte)
{
    WCHAR w = 0;
    UCHAR carry[4];
    ULONG carry_length = 0, consumed;
    if (!ConBytesToWide(cp, &byte, 1, &w, 1, carry, &carry_length, &consumed))
        return byte;
    return w;
}

/* the code page byte of a wide character, '?' for one the page lacks, as the ansi functions give. */
static UCHAR wide_to_byte(UINT cp, WCHAR w)
{
    if (w < 0x80)
        return (UCHAR)w;
    UCHAR out[4];
    ULONG consumed;
    if (ConWideToBytes(cp, &w, 1, out, sizeof(out), &consumed) == 1)
        return out[0];
    return '?';
}

/* a code page string as utf-16 in a new pool block. */
static PWCHAR to_wide(UINT cp, const UCHAR *in, ULONG length, ULONG *chars)
{
    PWCHAR out = pool(length * sizeof(WCHAR) + sizeof(WCHAR));
    if (!out)
        return nullptr;
    UCHAR carry[4];
    ULONG carry_length = 0, consumed;
    *chars = length ? ConBytesToWide(cp, in, length, out, length, carry, &carry_length, &consumed) : 0;
    return out;
}

/* ------------------------------------------------------------ layer 1 */

static NTSTATUS get_cp(CON_MSG *m)
{
    MIME_CONDRV_CODE_PAGES p = {};
    NTSTATUS status = call(m, IOCTL_CONDRV_CODE_PAGES, &p, sizeof(p), sizeof(p), nullptr);
    if (NT_SUCCESS(status))
        FIELD(m, ULONG, 0) = FIELD(m, BOOLEAN, 4) ? p.Output : p.Input;
    return status;
}

static NTSTATUS mode(CON_MSG *m, BOOLEAN set)
{
    MIME_CONDRV_MODE md = {set, FIELD(m, ULONG, 0)};
    NTSTATUS status = call(m, IOCTL_CONDRV_IN_MODE, &md, sizeof(md), sizeof(md), nullptr);
    if (NT_SUCCESS(status) && !set)
        FIELD(m, ULONG, 0) = md.Mode;
    return status;
}

static NTSTATUS input_events(CON_MSG *m)
{
    ULONG block = MIME_CONDRV_READ_PEEK | MIME_CONDRV_READ_NOWAIT;
    NTSTATUS status = call(m, IOCTL_CONDRV_IN_RECORDS, &block, sizeof(block), sizeof(block), nullptr);
    if (NT_SUCCESS(status))
        FIELD(m, ULONG, 0) = block;
    return status;
}

/* ReadConsoleInput and PeekConsoleInput: the descriptor counts the records, the room is the output buffer. */
static NTSTATUS get_input(CON_MSG *m)
{
    USHORT flags = FIELD(m, USHORT, 4);
    BOOLEAN unicode = FIELD(m, BOOLEAN, 6);
    ULONG room = m->OutSize / sizeof(MIME_CONDRV_RECORD);
    FIELD(m, ULONG, 0) = 0;
    ULONG size = sizeof(ULONG) + room * sizeof(MIME_CONDRV_RECORD);
    PUCHAR block = pool(size);
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    *(PULONG)block = ((flags & 1) ? MIME_CONDRV_READ_PEEK : 0) | ((flags & 2) ? MIME_CONDRV_READ_NOWAIT : 0);
    ULONG_PTR done = 0;
    NTSTATUS status = call(m, IOCTL_CONDRV_IN_RECORDS, block, sizeof(ULONG), size, &done);
    if (NT_SUCCESS(status)) {
        ULONG n = done > sizeof(ULONG) ? (ULONG)(done - sizeof(ULONG)) / sizeof(MIME_CONDRV_RECORD) : 0;
        MIME_CONDRV_RECORD *r = (MIME_CONDRV_RECORD *)(block + sizeof(ULONG));
        if (!unicode) {
            UINT cp = code_page(m, TRUE);
            for (ULONG i = 0; i < n; i++)
                if (r[i].EventType == CON_KEY_EVENT)
                    r[i].Key.Char = wide_to_byte(cp, r[i].Key.Char);
        }
        memcpy(m->Out, r, n * sizeof(MIME_CONDRV_RECORD));
        m->OutUsed = n * sizeof(MIME_CONDRV_RECORD);
        FIELD(m, ULONG, 0) = n;
    }
    ExFreePool(block);
    return status;
}

/*
 * ReadConsole: the input holds the name of the exe, then the text that starts the line. the descriptor gives the room in
 * bytes and gets back the bytes read and the control key state of the key that ended the line.
 */
static NTSTATUS read_console(CON_MSG *m)
{
    BOOLEAN unicode = FIELD(m, BOOLEAN, 0);
    ULONG exe_chars = FIELD(m, USHORT, 2);
    ULONG initial_bytes = FIELD(m, ULONG, 4);
    ULONG mask = FIELD(m, ULONG, 8);
    ULONG elem = unicode ? sizeof(WCHAR) : 1;
    ULONG room = m->OutSize / elem;
    FIELD(m, ULONG, 16) = 0;
    if ((ULONG64)exe_chars * sizeof(WCHAR) + initial_bytes > m->InSize || initial_bytes > m->OutSize ||
        (initial_bytes && !unicode))
        return STATUS_INVALID_PARAMETER;
    ULONG initial_chars = initial_bytes / sizeof(WCHAR);
    ULONG in_size = sizeof(MIME_CONDRV_READ) + (initial_chars + exe_chars) * sizeof(WCHAR);
    ULONG out_size = sizeof(MIME_CONDRV_READ_RESULT) + room * elem;
    PUCHAR block = pool(in_size > out_size ? in_size : out_size);
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    MIME_CONDRV_READ *rq = (MIME_CONDRV_READ *)block;
    rq->Flags = unicode ? MIME_CONDRV_READ_WIDE : 0;
    rq->Length = room;
    rq->InitialChars = initial_chars;
    rq->WakeupMask = mask;
    rq->Control = mask || initial_chars;
    rq->ExeNameLength = exe_chars;
    PWCHAR text = (PWCHAR)(rq + 1);
    memcpy(text, m->In + exe_chars * sizeof(WCHAR), initial_chars * sizeof(WCHAR));
    memcpy(text + initial_chars, m->In, exe_chars * sizeof(WCHAR));
    ULONG_PTR done = 0;
    NTSTATUS status = call(m, IOCTL_CONDRV_IN_READ, block, in_size, out_size, &done);
    if (NT_SUCCESS(status) && done >= sizeof(MIME_CONDRV_READ_RESULT)) {
        ULONG bytes = (ULONG)(done - sizeof(MIME_CONDRV_READ_RESULT));
        memcpy(m->Out, block + sizeof(MIME_CONDRV_READ_RESULT), bytes);
        m->OutUsed = bytes;
        FIELD(m, ULONG, 12) = ((MIME_CONDRV_READ_RESULT *)block)->ControlKeyState;
        FIELD(m, ULONG, 16) = bytes;
    }
    ExFreePool(block);
    return status;
}

static NTSTATUS write_console(CON_MSG *m)
{
    BOOLEAN unicode = FIELD(m, BOOLEAN, 4);
    ULONG length = unicode ? m->InSize / sizeof(WCHAR) : m->InSize;
    FIELD(m, ULONG, 0) = 0;
    ULONG size = sizeof(MIME_CONDRV_WRITE) + m->InSize;
    PUCHAR block = pool(size);
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    MIME_CONDRV_WRITE *rq = (MIME_CONDRV_WRITE *)block;
    rq->Flags = unicode ? 0 : MIME_CONDRV_WRITE_BYTES;
    rq->Length = length;
    memcpy(rq + 1, m->In, m->InSize);
    NTSTATUS status = call(m, IOCTL_CONDRV_OUT_WRITE, block, size, 0, nullptr);
    ExFreePool(block);
    if (NT_SUCCESS(status))
        FIELD(m, ULONG, 0) = length * (unicode ? sizeof(WCHAR) : 1);
    return status;
}

/* conhost knows a language only for the east asian code pages. */
static NTSTATUS lang_id(CON_MSG *m)
{
    switch (code_page(m, FALSE)) {
    case 932:
        FIELD(m, USHORT, 0) = 0x411;
        return STATUS_SUCCESS;
    case 936:
        FIELD(m, USHORT, 0) = 0x804;
        return STATUS_SUCCESS;
    case 949:
        FIELD(m, USHORT, 0) = 0x412;
        return STATUS_SUCCESS;
    case 950:
        FIELD(m, USHORT, 0) = 0x404;
        return STATUS_SUCCESS;
    default:
        return STATUS_NOT_SUPPORTED;
    }
}

/* ------------------------------------------------------------ layer 2 */

#define CONSOLE_ASCII 1
#define CONSOLE_REAL_UNICODE 2
#define CONSOLE_ATTRIBUTE 3
#define CONSOLE_FALSE_UNICODE 4

static NTSTATUS fill(CON_MSG *m)
{
    ULONG type = FIELD(m, ULONG, 4);
    USHORT value = FIELD(m, USHORT, 8);
    if (type < CONSOLE_ASCII || type > CONSOLE_FALSE_UNICODE)
        return STATUS_INVALID_PARAMETER;
    if (type == CONSOLE_ASCII)
        value = byte_to_wide(code_page(m, FALSE), (UCHAR)value);
    PUCHAR block = pool(sizeof(MIME_CONDRV_FILL));
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    MIME_CONDRV_FILL *rq = (MIME_CONDRV_FILL *)block;
    rq->Kind = type == CONSOLE_ATTRIBUTE ? MIME_CONDRV_RUN_ATTRIBUTES : MIME_CONDRV_RUN_CHARS;
    rq->Start.X = FIELD(m, SHORT, 0);
    rq->Start.Y = FIELD(m, SHORT, 2);
    rq->Count = FIELD(m, ULONG, 12);
    rq->Value = value;
    NTSTATUS status = call(m, IOCTL_CONDRV_OUT_FILL, block, sizeof(*rq), sizeof(*rq), nullptr);
    FIELD(m, ULONG, 12) = NT_SUCCESS(status) ? *(PULONG)block : 0;
    ExFreePool(block);
    return status;
}

static NTSTATUS ctrl_event(CON_MSG *m)
{
    ULONG rq[2] = {FIELD(m, ULONG, 0), FIELD(m, ULONG, 4)};
    return call(m, IOCTL_CONDRV_GENERATE, rq, sizeof(rq), 0, nullptr);
}

static BOOLEAN valid_code_page(UINT cp)
{
    static const USHORT known[] = {37,   437,  500,  708,  720,  737,  775,  850,  852,  855,  857,  858,  860,  861,
                                   862,  863,  864,  865,  866,  869,  870,  874,  875,  932,  936,  949,  950,  1026,
                                   1047, 1140, 1141, 1142, 1143, 1144, 1145, 1146, 1147, 1148, 1149, 1250, 1251, 1252,
                                   1253, 1254, 1255, 1256, 1257, 1258, 1361, 10000, 10001, 10002, 10003, 10004, 10005,
                                   10006, 10007, 10008, 10010, 10017, 10021, 10029, 10079, 10081, 10082, 20127, 20866,
                                   20932, 20936, 20949, 21866, 28591, 28592, 28593, 28594, 28595, 28596, 28597, 28598,
                                   28599, 28603, 28605, 65000, 65001};
    for (ULONG i = 0; i < RTL_NUMBER_OF(known); i++)
        if (known[i] == cp)
            return TRUE;
    return FALSE;
}

static NTSTATUS set_cp(CON_MSG *m)
{
    UINT cp = FIELD(m, ULONG, 0);
    if (!valid_code_page(cp))
        return STATUS_INVALID_PARAMETER;
    BOOLEAN output = FIELD(m, BOOLEAN, 4);
    MIME_CONDRV_CODE_PAGES p = {output ? 2u : 1u, cp, cp};
    return call(m, IOCTL_CONDRV_CODE_PAGES, &p, sizeof(p), sizeof(p), nullptr);
}

static NTSTATUS cursor_info(CON_MSG *m, BOOLEAN set)
{
    MIME_CONDRV_CURSOR c = {};
    if (set) {
        ULONG size = FIELD(m, ULONG, 0);
        if (size < 1 || size > 100)
            return STATUS_INVALID_PARAMETER;
        c.Set = 1;
        c.Size = size;
        c.Visible = FIELD(m, BOOLEAN, 4) != 0;
    }
    NTSTATUS status = call(m, IOCTL_CONDRV_OUT_CURSOR, &c, sizeof(c), sizeof(c), nullptr);
    if (NT_SUCCESS(status) && !set) {
        FIELD(m, ULONG, 0) = c.Size;
        FIELD(m, BOOLEAN, 4) = c.Visible != 0;
    }
    return status;
}

static NTSTATUS screen_info(CON_MSG *m, MIME_CONDRV_INFO *info)
{
    UCHAR block[sizeof(MIME_CONDRV_INFO)] = {};
    NTSTATUS status = call(m, IOCTL_CONDRV_OUT_INFO, block, sizeof(MIME_CONDRV_INFO_REQUEST), sizeof(block), nullptr);
    if (NT_SUCCESS(status))
        memcpy(info, block, sizeof(*info));
    return status;
}

/* the descriptor has the window as its origin and size, which kernelbase turns into the inclusive rectangle. */
static NTSTATUS get_screen_info(CON_MSG *m)
{
    MIME_CONDRV_INFO i;
    NTSTATUS status = screen_info(m, &i);
    if (!NT_SUCCESS(status))
        return status;
    FIELD(m, MIME_CONDRV_COORD, 0) = i.Size;
    FIELD(m, MIME_CONDRV_COORD, 4) = i.Cursor;
    FIELD(m, SHORT, 8) = i.Window.Left;
    FIELD(m, SHORT, 10) = i.Window.Top;
    FIELD(m, USHORT, 12) = i.Attributes;
    FIELD(m, SHORT, 14) = (SHORT)(i.Window.Right - i.Window.Left + 1);
    FIELD(m, SHORT, 16) = (SHORT)(i.Window.Bottom - i.Window.Top + 1);
    FIELD(m, MIME_CONDRV_COORD, 18) = i.MaxWindow;
    FIELD(m, USHORT, 22) = i.PopupAttributes;
    FIELD(m, BOOLEAN, 24) = FALSE;
    memcpy(m->Body + 28, i.ColorTable, sizeof(i.ColorTable));
    return STATUS_SUCCESS;
}

/* SetConsoleScreenBufferInfoEx sends the window size as right minus left and no origin: the window keeps its place. */
static NTSTATUS set_screen_info(CON_MSG *m)
{
    MIME_CONDRV_INFO now;
    NTSTATUS status = screen_info(m, &now);
    if (!NT_SUCCESS(status))
        return status;
    UCHAR block[sizeof(MIME_CONDRV_INFO_REQUEST) + sizeof(MIME_CONDRV_INFO)] = {};
    ((MIME_CONDRV_INFO_REQUEST *)block)->Set = 1;
    MIME_CONDRV_INFO *i = (MIME_CONDRV_INFO *)(block + sizeof(MIME_CONDRV_INFO_REQUEST));
    i->Size = FIELD(m, MIME_CONDRV_COORD, 0);
    i->Cursor = FIELD(m, MIME_CONDRV_COORD, 4);
    i->Attributes = FIELD(m, USHORT, 12);
    i->Window.Left = now.Window.Left;
    i->Window.Top = now.Window.Top;
    i->Window.Right = (SHORT)(now.Window.Left + FIELD(m, SHORT, 14) - 1);
    i->Window.Bottom = (SHORT)(now.Window.Top + FIELD(m, SHORT, 16) - 1);
    i->MaxWindow = FIELD(m, MIME_CONDRV_COORD, 18);
    i->PopupAttributes = FIELD(m, USHORT, 22);
    memcpy(i->ColorTable, m->Body + 28, sizeof(i->ColorTable));
    return call(m, IOCTL_CONDRV_OUT_INFO, block, sizeof(block), 0, nullptr);
}

static NTSTATUS set_size(CON_MSG *m)
{
    MIME_CONDRV_COORD size = FIELD(m, MIME_CONDRV_COORD, 0);
    return call(m, IOCTL_CONDRV_OUT_SIZE, &size, sizeof(size), 0, nullptr);
}

static NTSTATUS set_cursor_position(CON_MSG *m)
{
    MIME_CONDRV_CURSOR c = {1, FIELD(m, MIME_CONDRV_COORD, 0), 0, 0};
    return call(m, IOCTL_CONDRV_OUT_CURSOR, &c, sizeof(c), sizeof(c), nullptr);
}

static NTSTATUS largest_window(CON_MSG *m)
{
    MIME_CONDRV_INFO i;
    NTSTATUS status = screen_info(m, &i);
    if (NT_SUCCESS(status))
        FIELD(m, MIME_CONDRV_COORD, 0) = i.MaxWindow;
    return status;
}

static NTSTATUS scroll(CON_MSG *m)
{
    MIME_CONDRV_SCROLL rq = {};
    rq.Source = FIELD(m, MIME_CONDRV_RECT, 0);
    rq.Clip = FIELD(m, MIME_CONDRV_RECT, 8);
    rq.HasClip = FIELD(m, BOOLEAN, 16);
    rq.Destination = FIELD(m, MIME_CONDRV_COORD, 18);
    rq.Fill.Char = FIELD(m, USHORT, 22);
    rq.Fill.Attributes = FIELD(m, USHORT, 24);
    if (!FIELD(m, BOOLEAN, 17))
        rq.Fill.Char = byte_to_wide(code_page(m, FALSE), (UCHAR)rq.Fill.Char);
    return call(m, IOCTL_CONDRV_OUT_SCROLL, &rq, sizeof(rq), 0, nullptr);
}

static NTSTATUS set_attribute(CON_MSG *m)
{
    USHORT attributes = FIELD(m, USHORT, 0);
    if (attributes & 0x2000)
        return STATUS_INVALID_PARAMETER;
    return call(m, IOCTL_CONDRV_OUT_ATTRIBUTE, &attributes, sizeof(attributes), 0, nullptr);
}

static NTSTATUS set_window(CON_MSG *m)
{
    MIME_CONDRV_WINDOW w = {FIELD(m, BOOLEAN, 0) != 0, FIELD(m, MIME_CONDRV_RECT, 2)};
    return call(m, IOCTL_CONDRV_OUT_WINDOW, &w, sizeof(w), 0, nullptr);
}

/* ReadConsoleOutputCharacter and ReadConsoleOutputAttribute. */
static NTSTATUS read_string(CON_MSG *m)
{
    ULONG type = FIELD(m, ULONG, 4);
    FIELD(m, ULONG, 8) = 0;
    if (type < CONSOLE_ASCII || type > CONSOLE_FALSE_UNICODE)
        return STATUS_INVALID_PARAMETER;
    BOOLEAN ascii = type == CONSOLE_ASCII;
    ULONG count = ascii ? m->OutSize : m->OutSize / sizeof(USHORT);
    ULONG size = sizeof(MIME_CONDRV_RUN) + count * sizeof(USHORT) + sizeof(ULONG);
    PUCHAR block = pool(size);
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    MIME_CONDRV_RUN *rq = (MIME_CONDRV_RUN *)block;
    rq->Kind = type == CONSOLE_ATTRIBUTE ? MIME_CONDRV_RUN_ATTRIBUTES : MIME_CONDRV_RUN_CHARS;
    rq->Start = FIELD(m, MIME_CONDRV_COORD, 0);
    rq->Count = count;
    NTSTATUS status = call(m, IOCTL_CONDRV_OUT_READ_RUN, block, sizeof(*rq), size, nullptr);
    if (NT_SUCCESS(status)) {
        ULONG n = *(PULONG)block;
        const USHORT *got = (const USHORT *)(block + sizeof(ULONG));
        if (ascii) {
            UINT cp = code_page(m, FALSE);
            for (ULONG i = 0; i < n; i++)
                m->Out[i] = wide_to_byte(cp, got[i]);
            m->OutUsed = n;
        } else {
            memcpy(m->Out, got, n * sizeof(USHORT));
            m->OutUsed = n * sizeof(USHORT);
        }
        FIELD(m, ULONG, 8) = n;
    }
    ExFreePool(block);
    return status;
}

/* WriteConsoleInput: records with ansi characters carry a byte of the input code page. */
static NTSTATUS write_input(CON_MSG *m)
{
    BOOLEAN unicode = FIELD(m, BOOLEAN, 4), append = FIELD(m, BOOLEAN, 5);
    ULONG count = m->InSize / sizeof(MIME_CONDRV_RECORD);
    FIELD(m, ULONG, 0) = 0;
    ULONG size = sizeof(MIME_CONDRV_WRITE_INPUT) + count * sizeof(MIME_CONDRV_RECORD);
    PUCHAR block = pool(size > sizeof(ULONG) ? size : sizeof(ULONG));
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    MIME_CONDRV_WRITE_INPUT *rq = (MIME_CONDRV_WRITE_INPUT *)block;
    rq->Flags = append ? MIME_CONDRV_WRITE_APPEND : 0;
    rq->Reserved = 0;
    MIME_CONDRV_RECORD *r = (MIME_CONDRV_RECORD *)(rq + 1);
    memcpy(r, m->In, count * sizeof(MIME_CONDRV_RECORD));
    if (!unicode) {
        UINT cp = code_page(m, TRUE);
        for (ULONG i = 0; i < count; i++)
            if (r[i].EventType == CON_KEY_EVENT)
                r[i].Key.Char = byte_to_wide(cp, (UCHAR)r[i].Key.Char);
    }
    NTSTATUS status = call(m, IOCTL_CONDRV_IN_WRITE, block, size, sizeof(ULONG), nullptr);
    FIELD(m, ULONG, 0) = *(PULONG)block;
    ExFreePool(block);
    return status;
}

/* WriteConsoleOutput: kernelbase sends the cells of the rectangle it clipped to the caller's array, row after row. */
static NTSTATUS write_output(CON_MSG *m)
{
    MIME_CONDRV_RECT rect = FIELD(m, MIME_CONDRV_RECT, 0);
    LONG w = (LONG)rect.Right - rect.Left + 1, h = (LONG)rect.Bottom - rect.Top + 1;
    ULONG64 cells = w > 0 && h > 0 ? (ULONG64)w * (ULONG64)h : 0;
    if (cells * sizeof(MIME_CONDRV_CELL) > m->InSize)
        return STATUS_INVALID_PARAMETER;
    ULONG size = sizeof(MIME_CONDRV_RECT_IO) + (ULONG)cells * sizeof(MIME_CONDRV_CELL);
    PUCHAR block = pool(size);
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    ((MIME_CONDRV_RECT_IO *)block)->Rect = rect;
    MIME_CONDRV_CELL *c = (MIME_CONDRV_CELL *)(block + sizeof(MIME_CONDRV_RECT_IO));
    memcpy(c, m->In, (SIZE_T)cells * sizeof(MIME_CONDRV_CELL));
    if (!FIELD(m, BOOLEAN, 8)) {
        UINT cp = code_page(m, FALSE);
        for (ULONG64 i = 0; i < cells; i++)
            c[i].Char = byte_to_wide(cp, (UCHAR)c[i].Char);
    }
    NTSTATUS status = call(m, IOCTL_CONDRV_OUT_WRITE_RECT, block, size, sizeof(MIME_CONDRV_RECT), nullptr);
    if (NT_SUCCESS(status))
        FIELD(m, MIME_CONDRV_RECT, 0) = *(MIME_CONDRV_RECT *)block;
    ExFreePool(block);
    return status;
}

/* WriteConsoleOutputCharacter and WriteConsoleOutputAttribute. */
static NTSTATUS write_string(CON_MSG *m)
{
    ULONG type = FIELD(m, ULONG, 4);
    FIELD(m, ULONG, 8) = 0;
    if (type < CONSOLE_ASCII || type > CONSOLE_FALSE_UNICODE)
        return STATUS_INVALID_PARAMETER;
    ULONG count;
    PWCHAR wide = nullptr;
    const void *data = m->In;
    if (type == CONSOLE_ASCII) {
        wide = to_wide(code_page(m, FALSE), m->In, m->InSize, &count);
        if (!wide)
            return STATUS_INSUFFICIENT_RESOURCES;
        data = wide;
    } else {
        count = m->InSize / sizeof(USHORT);
    }
    ULONG size = sizeof(MIME_CONDRV_RUN) + count * sizeof(USHORT);
    PUCHAR block = pool(size > sizeof(ULONG) ? size : sizeof(ULONG));
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    if (block) {
        MIME_CONDRV_RUN *rq = (MIME_CONDRV_RUN *)block;
        rq->Kind = type == CONSOLE_ATTRIBUTE ? MIME_CONDRV_RUN_ATTRIBUTES : MIME_CONDRV_RUN_CHARS;
        rq->Start = FIELD(m, MIME_CONDRV_COORD, 0);
        rq->Count = count;
        memcpy(rq + 1, data, count * sizeof(USHORT));
        status = call(m, IOCTL_CONDRV_OUT_WRITE_RUN, block, size, sizeof(ULONG), nullptr);
        if (NT_SUCCESS(status))
            FIELD(m, ULONG, 8) = *(PULONG)block;
        ExFreePool(block);
    }
    if (wide)
        ExFreePool(wide);
    return status;
}

static NTSTATUS read_output(CON_MSG *m)
{
    MIME_CONDRV_RECT rect = FIELD(m, MIME_CONDRV_RECT, 0);
    LONG w = (LONG)rect.Right - rect.Left + 1, h = (LONG)rect.Bottom - rect.Top + 1;
    ULONG64 cells = w > 0 && h > 0 ? (ULONG64)w * (ULONG64)h : 0;
    if (cells * sizeof(MIME_CONDRV_CELL) > m->OutSize)
        return STATUS_INVALID_PARAMETER;
    ULONG size = sizeof(MIME_CONDRV_RECT) + (ULONG)cells * sizeof(MIME_CONDRV_CELL);
    PUCHAR block = pool(size > sizeof(MIME_CONDRV_RECT_IO) ? size : sizeof(MIME_CONDRV_RECT_IO));
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    ((MIME_CONDRV_RECT_IO *)block)->Rect = rect;
    NTSTATUS status = call(m, IOCTL_CONDRV_OUT_READ_RECT, block, sizeof(MIME_CONDRV_RECT_IO), size, nullptr);
    if (NT_SUCCESS(status)) {
        MIME_CONDRV_CELL *c = (MIME_CONDRV_CELL *)(block + sizeof(MIME_CONDRV_RECT));
        if (!FIELD(m, BOOLEAN, 8)) {
            UINT cp = code_page(m, FALSE);
            for (ULONG64 i = 0; i < cells; i++)
                c[i].Char = wide_to_byte(cp, c[i].Char);
        }
        memcpy(m->Out, c, (SIZE_T)cells * sizeof(MIME_CONDRV_CELL));
        m->OutUsed = (ULONG)cells * sizeof(MIME_CONDRV_CELL);
        FIELD(m, MIME_CONDRV_RECT, 0) = *(MIME_CONDRV_RECT *)block;
    }
    ExFreePool(block);
    return status;
}

/* the title goes out as far as it fits, and its whole length comes back. titles convert with the input code page. */
static NTSTATUS get_title(CON_MSG *m)
{
    BOOLEAN unicode = FIELD(m, BOOLEAN, 4), original = FIELD(m, BOOLEAN, 5);
    FIELD(m, ULONG, 0) = 0;
    ULONG size = sizeof(ULONG) + 260 * sizeof(WCHAR);
    PUCHAR block = pool(size);
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    MIME_CONDRV_TITLE *rq = (MIME_CONDRV_TITLE *)block;
    rq->Set = 0;
    rq->Original = original;
    rq->Length = 0;
    ULONG_PTR done = 0;
    NTSTATUS status = call(m, IOCTL_CONDRV_TITLE, block, sizeof(*rq), size, &done);
    if (NT_SUCCESS(status)) {
        ULONG length = *(PULONG)block;
        ULONG got = done > sizeof(ULONG) ? (ULONG)(done - sizeof(ULONG)) / sizeof(WCHAR) : 0;
        const WCHAR *title = (const WCHAR *)(block + sizeof(ULONG));
        if (unicode) {
            ULONG n = got < m->OutSize / sizeof(WCHAR) ? got : m->OutSize / sizeof(WCHAR);
            memcpy(m->Out, title, n * sizeof(WCHAR));
            m->OutUsed = n * sizeof(WCHAR);
            FIELD(m, ULONG, 0) = length;
        } else {
            UCHAR bytes[260 * 4];
            ULONG consumed;
            ULONG n = ConWideToBytes(code_page(m, TRUE), title, got, bytes, sizeof(bytes), &consumed);
            ULONG fit = m->OutSize ? (n < m->OutSize ? n : m->OutSize - 1) : 0;
            memcpy(m->Out, bytes, fit);
            if (m->OutSize)
                m->Out[fit] = 0;
            m->OutUsed = m->OutSize ? fit + 1 : 0;
            FIELD(m, ULONG, 0) = fit;
        }
    }
    ExFreePool(block);
    return status;
}

static NTSTATUS set_title(CON_MSG *m)
{
    BOOLEAN unicode = FIELD(m, BOOLEAN, 0);
    ULONG chars;
    PWCHAR wide = nullptr;
    const void *text = m->In;
    if (unicode) {
        chars = m->InSize / sizeof(WCHAR);
    } else {
        wide = to_wide(code_page(m, TRUE), m->In, m->InSize, &chars);
        if (!wide)
            return STATUS_INSUFFICIENT_RESOURCES;
        text = wide;
    }
    /* the text may end with its nul, which is no part of the title. */
    while (chars && !((const WCHAR *)text)[chars - 1])
        chars--;
    ULONG size = sizeof(MIME_CONDRV_TITLE) + chars * sizeof(WCHAR);
    PUCHAR block = pool(size);
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    if (block) {
        MIME_CONDRV_TITLE *rq = (MIME_CONDRV_TITLE *)block;
        rq->Set = 1;
        rq->Original = 0;
        rq->Length = chars;
        memcpy(rq + 1, text, chars * sizeof(WCHAR));
        status = call(m, IOCTL_CONDRV_TITLE, block, size, 0, nullptr);
        ExFreePool(block);
    }
    if (wide)
        ExFreePool(wide);
    return status;
}

/* ------------------------------------------------------------ layer 3 */

static NTSTATUS font(CON_MSG *m, MIME_CONDRV_FONT *f)
{
    UCHAR block[sizeof(MIME_CONDRV_FONT)];
    NTSTATUS status = call(m, IOCTL_CONDRV_OUT_FONT, block, 0, sizeof(block), nullptr);
    if (NT_SUCCESS(status))
        memcpy(f, block, sizeof(*f));
    return status;
}

static NTSTATUS font_size(CON_MSG *m)
{
    if (FIELD(m, ULONG, 0) != 0)
        return STATUS_INVALID_PARAMETER;
    MIME_CONDRV_FONT f;
    NTSTATUS status = font(m, &f);
    if (NT_SUCCESS(status))
        FIELD(m, MIME_CONDRV_COORD, 4) = f.Size;
    return status;
}

static NTSTATUS current_font(CON_MSG *m)
{
    MIME_CONDRV_FONT f;
    NTSTATUS status = font(m, &f);
    if (NT_SUCCESS(status)) {
        FIELD(m, ULONG, 4) = 0;
        FIELD(m, MIME_CONDRV_COORD, 8) = f.Size;
        FIELD(m, ULONG, 12) = f.Family;
        FIELD(m, ULONG, 16) = f.Weight;
        memcpy(m->Body + 20, f.Face, sizeof(f.Face));
        ((PWCHAR)(m->Body + 20))[31] = 0;
    }
    return status;
}

/* the font of a console belongs to its terminal or window: setting it succeeds on a handle that can ask for it. */
static NTSTATUS set_font(CON_MSG *m)
{
    MIME_CONDRV_FONT f;
    return font(m, &f);
}

static NTSTATUS set_display_mode(CON_MSG *m)
{
    MIME_CONDRV_INFO i;
    NTSTATUS status = screen_info(m, &i);
    if (!NT_SUCCESS(status))
        return status;
    FIELD(m, MIME_CONDRV_COORD, 4) = i.Size;
    return FIELD(m, ULONG, 0) & 3 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

#define CONSOLE_WINDOW_OF_TERMINAL 0x00FF0010

/*
 * the window of a console that has one. the terminal of a run and a pseudo console answer with a value of their own, as
 * a pseudo console does on windows, and a console that nothing shows has none.
 */
static NTSTATUS console_window(CON_MSG *m)
{
    MIME_CONSOLE *c = m->Console;
    FIELD(m, ULONG64, 0) = 0;
    if (!c)
        return STATUS_INVALID_HANDLE;
    if (c->Backend == CON_BACKEND_WINDOW) {
        ConWaitForHost(c);
        ExAcquireFastMutex(&g_con_lock);
        FIELD(m, ULONG64, 0) = c->HostWindow;
        ExReleaseFastMutex(&g_con_lock);
    } else if (c->Backend == CON_BACKEND_RUN || c->Backend == CON_BACKEND_PTY) {
        FIELD(m, ULONG64, 0) = CONSOLE_WINDOW_OF_TERMINAL;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS selection(CON_MSG *m)
{
    MIME_CONDRV_HOST_SELECTION s = {};
    NTSTATUS status = call(m, IOCTL_CONDRV_SELECTION, &s, 0, sizeof(s), nullptr);
    if (NT_SUCCESS(status))
        memcpy(m->Body, &s, sizeof(s));
    return status;
}

static NTSTATUS process_list(CON_MSG *m)
{
    ULONG room = m->OutSize / sizeof(ULONG);
    ULONG size = (room + 1) * sizeof(ULONG);
    PULONG block = pool(size);
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG_PTR done = 0;
    NTSTATUS status = call(m, IOCTL_CONDRV_PROCESSES, (PUCHAR)block, 0, size, &done);
    if (NT_SUCCESS(status)) {
        ULONG n = block[0] < room ? block[0] : room;
        memcpy(m->Out, block + 1, n * sizeof(ULONG));
        m->OutUsed = n * sizeof(ULONG);
        FIELD(m, ULONG, 0) = block[0];
    }
    ExFreePool(block);
    return status;
}

static NTSTATUS history_info(CON_MSG *m, BOOLEAN set)
{
    UCHAR block[sizeof(MIME_CONDRV_HISTORY) + sizeof(MIME_CONDRV_HISTORY_INFO)] = {};
    MIME_CONDRV_HISTORY *rq = (MIME_CONDRV_HISTORY *)block;
    rq->Op = set ? MIME_CONDRV_HISTORY_SET_INFO : MIME_CONDRV_HISTORY_GET_INFO;
    rq->Value = FIELD(m, ULONG, 0);
    rq->Value2 = FIELD(m, ULONG, 4);
    rq->ExeNameLength = FIELD(m, ULONG, 8); /* the flags, for the settings */
    NTSTATUS status = call(m, IOCTL_CONDRV_IN_HISTORY, block, sizeof(*rq), set ? 0 : sizeof(MIME_CONDRV_HISTORY_INFO), nullptr);
    if (NT_SUCCESS(status) && !set) {
        MIME_CONDRV_HISTORY_INFO *info = (MIME_CONDRV_HISTORY_INFO *)block;
        FIELD(m, ULONG, 0) = info->BufferSize;
        FIELD(m, ULONG, 4) = info->NumberOfBuffers;
        FIELD(m, ULONG, 8) = info->Flags;
    }
    return status;
}

/* a string of the input as wide characters: unicode ones are taken as they are, ansi ones convert. */
typedef struct CON_TEXT {
    const WCHAR *Text;
    ULONG Chars;
    PWCHAR Owned;
} CON_TEXT;

static BOOLEAN take_text(CON_MSG *m, BOOLEAN unicode, ULONG offset, ULONG bytes, CON_TEXT *t)
{
    t->Owned = nullptr;
    if ((ULONG64)offset + bytes > m->InSize || (unicode && (bytes & 1)))
        return FALSE;
    if (unicode) {
        t->Text = (const WCHAR *)(m->In + offset);
        t->Chars = bytes / sizeof(WCHAR);
        return TRUE;
    }
    t->Owned = to_wide(code_page(m, TRUE), m->In + offset, bytes, &t->Chars);
    t->Text = t->Owned;
    return t->Owned != nullptr;
}

static void drop_text(CON_TEXT *t)
{
    if (t->Owned)
        ExFreePool(t->Owned);
}

/* wide characters back to the caller, as code page bytes for an ansi call; the bytes written. */
static ULONG give_text(CON_MSG *m, BOOLEAN unicode, const WCHAR *text, ULONG chars)
{
    if (unicode) {
        ULONG n = chars < m->OutSize / sizeof(WCHAR) ? chars : m->OutSize / sizeof(WCHAR);
        memcpy(m->Out, text, n * sizeof(WCHAR));
        m->OutUsed = n * sizeof(WCHAR);
        return m->OutUsed;
    }
    ULONG consumed;
    m->OutUsed = ConWideToBytes(code_page(m, TRUE), text, chars, m->Out, m->OutSize, &consumed);
    return m->OutUsed;
}

/* a request of mime/condrv.h with a header and wide strings after it, for history and aliases. */
static PUCHAR string_request(ULONG header, const CON_TEXT *a, const CON_TEXT *b, const CON_TEXT *c, ULONG out_size,
                             ULONG *in_size, ULONG *block_size)
{
    ULONG chars = (a ? a->Chars : 0) + (b ? b->Chars : 0) + (c ? c->Chars : 0);
    *in_size = header + chars * sizeof(WCHAR);
    *block_size = *in_size > out_size ? *in_size : out_size;
    PUCHAR block = pool(*block_size);
    if (!block)
        return nullptr;
    PWCHAR at = (PWCHAR)(block + header);
    const CON_TEXT *parts[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        if (!parts[i])
            continue;
        memcpy(at, parts[i]->Text, parts[i]->Chars * sizeof(WCHAR));
        at += parts[i]->Chars;
    }
    return block;
}

/* the history calls name the exe in the whole input. */
static NTSTATUS history(CON_MSG *m, ULONG op)
{
    BOOLEAN unicode = FIELD(m, BOOLEAN, 4);
    CON_TEXT exe;
    if (!take_text(m, unicode, 0, m->InSize, &exe))
        return STATUS_INVALID_PARAMETER;
    ULONG out_size = op == MIME_CONDRV_HISTORY_GET ? m->OutSize * (unicode ? 1 : sizeof(WCHAR)) : sizeof(ULONG);
    if (op == MIME_CONDRV_HISTORY_LENGTH)
        out_size = sizeof(ULONG);
    ULONG in_size, block_size;
    PUCHAR block = string_request(sizeof(MIME_CONDRV_HISTORY), &exe, nullptr, nullptr, out_size, &in_size, &block_size);
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    if (block) {
        MIME_CONDRV_HISTORY *rq = (MIME_CONDRV_HISTORY *)block;
        rq->Op = op;
        rq->Value = op == MIME_CONDRV_HISTORY_SET_COUNT ? FIELD(m, ULONG, 0) : 0;
        rq->Value2 = 0;
        rq->ExeNameLength = exe.Chars;
        ULONG_PTR done = 0;
        status = call(m, IOCTL_CONDRV_IN_HISTORY, block, in_size, op == MIME_CONDRV_HISTORY_EXPUNGE || op == MIME_CONDRV_HISTORY_SET_COUNT ? 0 : out_size, &done);
        if (op == MIME_CONDRV_HISTORY_LENGTH && NT_SUCCESS(status)) {
            FIELD(m, ULONG, 0) = *(PULONG)block * (unicode ? sizeof(WCHAR) : 1);
        } else if (op == MIME_CONDRV_HISTORY_GET && (NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW)) {
            FIELD(m, ULONG, 0) = give_text(m, unicode, (const WCHAR *)block, (ULONG)done / sizeof(WCHAR));
            if (status == STATUS_BUFFER_OVERFLOW)
                status = STATUS_BUFFER_TOO_SMALL;
        }
        ExFreePool(block);
    }
    drop_text(&exe);
    return status;
}

/* the alias calls: lengths in bytes, the input holds the exe, then the source, then the target. */
static NTSTATUS alias(CON_MSG *m, ULONG op)
{
    BOOLEAN unicode;
    ULONG exe_bytes = 0, source_bytes = 0, target_bytes = 0;
    switch (op) {
    case MIME_CONDRV_ALIAS_ADD:
        source_bytes = FIELD(m, USHORT, 0);
        target_bytes = FIELD(m, USHORT, 2);
        exe_bytes = FIELD(m, USHORT, 4);
        unicode = FIELD(m, BOOLEAN, 6);
        break;
    case MIME_CONDRV_ALIAS_GET:
        source_bytes = FIELD(m, USHORT, 0);
        exe_bytes = FIELD(m, USHORT, 4);
        unicode = FIELD(m, BOOLEAN, 6);
        break;
    case MIME_CONDRV_ALIAS_LIST:
        exe_bytes = m->InSize;
        unicode = FIELD(m, BOOLEAN, 0);
        break;
    default:
        unicode = FIELD(m, BOOLEAN, 4);
        break;
    }
    CON_TEXT exe = {}, source = {}, target = {};
    if (!take_text(m, unicode, 0, exe_bytes, &exe))
        return STATUS_INVALID_PARAMETER;
    if (!take_text(m, unicode, exe_bytes, source_bytes, &source) ||
        !take_text(m, unicode, exe_bytes + source_bytes, target_bytes, &target)) {
        drop_text(&exe);
        drop_text(&source);
        return STATUS_INVALID_PARAMETER;
    }
    ULONG out_size = m->OutSize * (unicode ? 1 : sizeof(WCHAR));
    ULONG in_size, block_size;
    PUCHAR block = string_request(sizeof(MIME_CONDRV_ALIAS), &source, &target, &exe, out_size, &in_size, &block_size);
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    if (block) {
        MIME_CONDRV_ALIAS *rq = (MIME_CONDRV_ALIAS *)block;
        rq->Op = op;
        rq->SourceLength = source.Chars;
        rq->TargetLength = target.Chars;
        rq->ExeNameLength = exe.Chars;
        ULONG_PTR done = 0;
        status = call(m, IOCTL_CONDRV_IN_ALIAS, block, in_size, op == MIME_CONDRV_ALIAS_ADD ? 0 : out_size, &done);
        if (op != MIME_CONDRV_ALIAS_ADD && (NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW)) {
            ULONG bytes = give_text(m, unicode, (const WCHAR *)block, (ULONG)done / sizeof(WCHAR));
            if (op == MIME_CONDRV_ALIAS_GET)
                FIELD(m, USHORT, 2) = (USHORT)bytes;
            else if (op == MIME_CONDRV_ALIAS_LIST)
                FIELD(m, ULONG, 4) = bytes;
            else
                FIELD(m, ULONG, 0) = bytes;
            if (status == STATUS_BUFFER_OVERFLOW)
                status = STATUS_BUFFER_TOO_SMALL;
        }
        ExFreePool(block);
    }
    drop_text(&exe);
    drop_text(&source);
    drop_text(&target);
    return status;
}

/* GetConsoleAliasesLength and GetConsoleAliasExesLength: the room a full list needs. */
static NTSTATUS alias_length(CON_MSG *m, ULONG op)
{
    BOOLEAN unicode = FIELD(m, BOOLEAN, 4);
    CON_TEXT exe = {};
    if (op == MIME_CONDRV_ALIAS_LIST && !take_text(m, unicode, 0, m->InSize, &exe))
        return STATUS_INVALID_PARAMETER;
    ULONG room = 64 * 1024;
    ULONG in_size, block_size;
    PUCHAR block = string_request(sizeof(MIME_CONDRV_ALIAS), nullptr, nullptr, &exe, room * sizeof(WCHAR), &in_size, &block_size);
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    if (block) {
        MIME_CONDRV_ALIAS *rq = (MIME_CONDRV_ALIAS *)block;
        rq->Op = op;
        rq->SourceLength = 0;
        rq->TargetLength = 0;
        rq->ExeNameLength = exe.Chars;
        ULONG_PTR done = 0;
        status = call(m, IOCTL_CONDRV_IN_ALIAS, block, in_size, room * sizeof(WCHAR), &done);
        if (NT_SUCCESS(status)) {
            ULONG chars = (ULONG)done / sizeof(WCHAR);
            ULONG bytes = chars * sizeof(WCHAR);
            if (!unicode) {
                ULONG consumed;
                PUCHAR scratch = pool(chars * 4);
                bytes = scratch ? ConWideToBytes(code_page(m, TRUE), (const WCHAR *)block, chars, scratch, chars * 4, &consumed) : chars;
                if (scratch)
                    ExFreePool(scratch);
            }
            FIELD(m, ULONG, 0) = bytes;
        }
        ExFreePool(block);
    }
    drop_text(&exe);
    return status;
}

/* ------------------------------------------------------------ dispatch */

static NTSTATUS dispatch(CON_MSG *m)
{
    switch (m->Api) {
    case 0x1000000: return get_cp(m);
    case 0x1000001: return mode(m, FALSE);
    case 0x1000002: return mode(m, TRUE);
    case 0x1000003: return input_events(m);
    case 0x1000004: return get_input(m);
    case 0x1000005: return read_console(m);
    case 0x1000006: return write_console(m);
    case 0x1000008: return lang_id(m);
    case 0x2000000: return fill(m);
    case 0x2000001: return ctrl_event(m);
    case 0x2000002: return call(m, IOCTL_CONDRV_OUT_ACTIVATE, nullptr, 0, 0, nullptr);
    case 0x2000003: return call(m, IOCTL_CONDRV_IN_FLUSH, nullptr, 0, 0, nullptr);
    case 0x2000004: return set_cp(m);
    case 0x2000005: return cursor_info(m, FALSE);
    case 0x2000006: return cursor_info(m, TRUE);
    case 0x2000007: return get_screen_info(m);
    case 0x2000008: return set_screen_info(m);
    case 0x2000009: return set_size(m);
    case 0x200000A: return set_cursor_position(m);
    case 0x200000B: return largest_window(m);
    case 0x200000C: return scroll(m);
    case 0x200000D: return set_attribute(m);
    case 0x200000E: return set_window(m);
    case 0x200000F: return read_string(m);
    case 0x2000010: return write_input(m);
    case 0x2000011: return write_output(m);
    case 0x2000012: return write_string(m);
    case 0x2000013: return read_output(m);
    case 0x2000014: return get_title(m);
    case 0x2000015: return set_title(m);
    case 0x3000001: FIELD(m, ULONG, 0) = 3; return STATUS_SUCCESS;
    case 0x3000003: return font_size(m);
    case 0x3000004: return current_font(m);
    case 0x300000D: return set_display_mode(m);
    case 0x3000011: FIELD(m, ULONG, 0) = 0; return STATUS_SUCCESS;
    case 0x3000012: return alias(m, MIME_CONDRV_ALIAS_ADD);
    case 0x3000013: return alias(m, MIME_CONDRV_ALIAS_GET);
    case 0x3000014: return alias_length(m, MIME_CONDRV_ALIAS_LIST);
    case 0x3000015: return alias_length(m, MIME_CONDRV_ALIAS_EXES);
    case 0x3000016: return alias(m, MIME_CONDRV_ALIAS_LIST);
    case 0x3000017: return alias(m, MIME_CONDRV_ALIAS_EXES);
    case 0x3000018: return history(m, MIME_CONDRV_HISTORY_EXPUNGE);
    case 0x3000019: return history(m, MIME_CONDRV_HISTORY_SET_COUNT);
    case 0x300001A: return history(m, MIME_CONDRV_HISTORY_LENGTH);
    case 0x300001B: return history(m, MIME_CONDRV_HISTORY_GET);
    case 0x300001F: return console_window(m);
    case 0x3000028: return selection(m);
    case 0x3000029: return process_list(m);
    case 0x300002A: return history_info(m, FALSE);
    case 0x300002B: return history_info(m, TRUE);
    case 0x300002C: return set_font(m);
    default:
        /* what conhost has dropped, and numbers it never had. */
        return (m->Api >> 24) >= 1 && (m->Api >> 24) <= 3 && (m->Api & 0xFFFFFF) < 0x2D ? STATUS_NOT_IMPLEMENTED
                                                                                       : STATUS_ILLEGAL_FUNCTION;
    }
}

/* the object of a call: the handle it names, or the file the call came on. */
static NTSTATUS object_of(PFILE_OBJECT file, ULONG64 client, PFILE_OBJECT *out)
{
    if (!client) {
        ObReferenceObject(file);
        *out = file;
        return STATUS_SUCCESS;
    }
    PFILE_OBJECT object;
    NTSTATUS status = ObReferenceHandle((HANDLE)(ULONG_PTR)client, IoFileObjectTypeObject, UserMode, (PVOID *)&object, nullptr);
    if (!NT_SUCCESS(status))
        return STATUS_INVALID_HANDLE;
    if (object->DeviceObject != file->DeviceObject || !object->FsContext2) {
        ObDereferenceObject(object);
        return STATUS_INVALID_HANDLE;
    }
    *out = object;
    return STATUS_SUCCESS;
}

/*
 * 32-bit kernelbase sends the same 64-bit layout, with its handles and addresses sign extended: an address above 2 GiB
 * of a large address aware program is its low half.
 */
static void narrow_for_wow64(CD_USER_DEFINED_IO64 *io, ULONG buffers)
{
    if (PsCurrentProcess()->Machine != IMAGE_FILE_MACHINE_I386)
        return;
    io->Client = (ULONG)io->Client;
    for (ULONG i = 0; i < buffers; i++)
        io->Buffers[i].Buffer = (ULONG)io->Buffers[i].Buffer;
}

static NTSTATUS issue_user_io(PFILE_OBJECT file, UCHAR *system, ULONG in_len)
{
    if (in_len < sizeof(CD_USER_DEFINED_IO64))
        return STATUS_INVALID_PARAMETER;
    CD_USER_DEFINED_IO64 *io = (CD_USER_DEFINED_IO64 *)system;
    ULONG inputs = io->InputCount, outputs = io->OutputCount;
    if (!inputs || !outputs || inputs + outputs > 16 ||
        in_len < sizeof(CD_USER_DEFINED_IO64) + (inputs + outputs) * sizeof(CD_IO_BUFFER64))
        return STATUS_INVALID_PARAMETER;
    narrow_for_wow64(io, inputs + outputs);
    const CD_IO_BUFFER64 *b = io->Buffers;
    UCHAR header[8];
    if (b[0].Size < sizeof(header) || !NT_SUCCESS(KiCopyFromUser(header, (const void *)(ULONG_PTR)b[0].Buffer, sizeof(header))))
        return STATUS_INVALID_PARAMETER;
    CON_MSG *m = pool(sizeof(CON_MSG));
    if (!m)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(m, 0, sizeof(*m));
    m->Api = *(PULONG)header;
    m->BodySize = *(PULONG)(header + 4);
    NTSTATUS status = STATUS_SUCCESS;
    if (m->BodySize > MAX_DESCRIPTOR || m->BodySize > b[0].Size - sizeof(header))
        status = STATUS_ILLEGAL_FUNCTION;
    if (NT_SUCCESS(status) && m->BodySize)
        status = KiCopyFromUser(m->Body, (const UCHAR *)(ULONG_PTR)b[0].Buffer + sizeof(header), m->BodySize);
    ULONG64 in_total = 0, out_total = 0;
    for (ULONG i = 1; i < inputs; i++)
        in_total += b[i].Size;
    for (ULONG i = 1; i < outputs; i++)
        out_total += b[inputs + i].Size;
    if (NT_SUCCESS(status) && (in_total > MAX_PAYLOAD || out_total > MAX_PAYLOAD))
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status)) {
        m->InSize = (ULONG)in_total;
        m->OutSize = (ULONG)out_total;
        m->In = pool(m->InSize);
        m->Out = pool(m->OutSize);
        if (!m->In || !m->Out)
            status = STATUS_INSUFFICIENT_RESOURCES;
    }
    for (ULONG i = 1, at = 0; NT_SUCCESS(status) && i < inputs; at += b[i].Size, i++)
        if (b[i].Size)
            status = KiCopyFromUser(m->In + at, (const void *)(ULONG_PTR)b[i].Buffer, b[i].Size);
    if (NT_SUCCESS(status))
        status = object_of(file, io->Client, &m->Object);
    if (NT_SUCCESS(status)) {
        CON_HANDLE *h = m->Object->FsContext2;
        m->Console = h->Console;
        if (m->Console)
            ConReference(m->Console);
        status = dispatch(m);
        /* the descriptor goes back whatever happened, the data only with success. */
        const CD_IO_BUFFER64 *body = &b[inputs];
        ULONG n = body->Size < m->BodySize ? body->Size : m->BodySize;
        if (n)
            KiCopyToUser((void *)(ULONG_PTR)body->Buffer, m->Body, n);
        if (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL) {
            for (ULONG i = 1, at = 0; i < outputs && at < m->OutUsed; i++) {
                const CD_IO_BUFFER64 *o = &b[inputs + i];
                ULONG piece = m->OutUsed - at < o->Size ? m->OutUsed - at : o->Size;
                if (piece && !NT_SUCCESS(KiCopyToUser((void *)(ULONG_PTR)o->Buffer, m->Out + at, piece)))
                    status = STATUS_INVALID_USER_BUFFER;
                at += o->Size;
            }
        }
        if (m->Console)
            ConRelease(m->Console);
        ObDereferenceObject(m->Object);
    }
    if (m->In)
        ExFreePool(m->In);
    if (m->Out)
        ExFreePool(m->Out);
    ExFreePool(m);
    return status;
}

NTSTATUS ConDriverControl(PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    PFILE_OBJECT file = stack->FileObject;
    CON_HANDLE *h = file->FsContext2;
    NTSTATUS status;
    ULONG_PTR information = 0;
    switch (code) {
    case IOCTL_CONDRV_ISSUE_USER_IO:
        status = issue_user_io(file, irp->AssociatedIrp.SystemBuffer, stack->Parameters.DeviceIoControl.InputBufferLength);
        break;
    case IOCTL_CONDRV_LAUNCH_SERVER:
        /* conhost.exe on windows; the console here is served by this driver and shown by its own host. */
        status = h && h->Kind == CH_SERVER ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_REQUEST;
        break;
    case IOCTL_CONDRV_GET_SERVER_PID: {
        ULONG64 pid = 0;
        if (!h || !h->Console) {
            status = STATUS_INVALID_HANDLE;
            break;
        }
        if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(pid)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        ExAcquireFastMutex(&g_con_lock);
        pid = h->Console->HostPid;
        ExReleaseFastMutex(&g_con_lock);
        status = STATUS_SUCCESS;
        /* the i/o manager buffers the request of a driver of its own and copies the answer back. */
        if (irp->AssociatedIrp.SystemBuffer)
            memcpy(irp->AssociatedIrp.SystemBuffer, &pid, sizeof(pid));
        else
            status = KiCopyToUser(irp->UserBuffer, &pid, sizeof(pid));
        if (NT_SUCCESS(status))
            information = sizeof(pid);
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}
