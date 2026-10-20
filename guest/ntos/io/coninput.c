/*
 * the records of the console input buffer. a terminal sends characters and
 * escape sequences; the console reports keys, so input is decoded into a
 * press and a release per key, with the us layout's virtual keys and scan
 * codes. a program that set ENABLE_VIRTUAL_TERMINAL_INPUT gets the
 * characters as they came instead, and keys written to it become the
 * sequences a terminal would send, as conhost does.
 */
#include "../ntos.h"
#include "coninput.h"

#define VK_CANCEL 0x03
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_OEM_2 0xBF

/* the virtual key of the key that types NUL with ctrl, 2 on the us layout. */
#define VK_NUL_KEY 0x32

#define ALT (CON_LEFT_ALT | CON_RIGHT_ALT)
#define CTRL (CON_LEFT_CTRL | CON_RIGHT_CTRL)

/* ------------------------------------------------------------------ decoding */

static void key_record(MIME_CONDRV_RECORD *r, BOOLEAN down, USHORT vk, USHORT scan, WCHAR ch, ULONG state)
{
    memset(r, 0, sizeof(*r));
    r->EventType = CON_KEY_EVENT;
    r->Key.KeyDown = down;
    r->Key.RepeatCount = 1;
    r->Key.VirtualKey = vk;
    r->Key.ScanCode = scan;
    r->Key.Char = ch;
    r->Key.ControlKeyState = state;
}

/*
 * a key press and release, as the console reports a typed key. when the key is typed with shift, alt or
 * ctrl the presses of those keys go around it, in that order, and their releases after it in the other.
 */
static ULONG key_pair(MIME_CONDRV_RECORD *out, ULONG room, USHORT vk, USHORT scan, WCHAR ch, ULONG state)
{
    static const struct {
        ULONG bit;
        USHORT vk, scan;
    } modifiers[] = {{CON_SHIFT, 0x10, 0x2A}, {CON_LEFT_ALT, 0x12, 0x38}, {CON_LEFT_CTRL, 0x11, 0x1D}};
    ULONG held = state & (CON_SHIFT | CON_LEFT_ALT | CON_LEFT_CTRL);
    ULONG count = 0;
    for (ULONG i = 0; i < 3; i++)
        if (held & modifiers[i].bit)
            count++;
    if (room < 2 + 2 * count)
        return 0;
    ULONG n = 0, accumulated = 0;
    for (ULONG i = 0; i < 3; i++) {
        if (!(held & modifiers[i].bit))
            continue;
        accumulated |= modifiers[i].bit;
        key_record(&out[n++], TRUE, modifiers[i].vk, modifiers[i].scan, 0, accumulated);
    }
    key_record(&out[n++], TRUE, vk, scan, ch, state);
    key_record(&out[n++], FALSE, vk, scan, ch, state);
    for (ULONG i = 3; i-- > 0;) {
        if (!(held & modifiers[i].bit))
            continue;
        accumulated &= ~modifiers[i].bit;
        key_record(&out[n++], FALSE, modifiers[i].vk, modifiers[i].scan, 0, accumulated);
    }
    return n;
}

/* a key press and release with no keys around it, as the console reports ctrl+c. */
static ULONG plain_pair(MIME_CONDRV_RECORD *out, ULONG room, USHORT vk, USHORT scan, WCHAR ch, ULONG state)
{
    if (room < 2)
        return 0;
    key_record(&out[0], TRUE, vk, scan, ch, state);
    key_record(&out[1], FALSE, vk, scan, ch, state);
    return 2;
}

/* the us layout key of a printable ascii character: virtual key, scan code, shift. */
static void ascii_key(UCHAR c, USHORT *vk, USHORT *scan, BOOLEAN *shift)
{
    static const char row1[] = "1234567890", shifted1[] = "!@#$%^&*()";
    static const struct {
        char plain, shifted;
        USHORT vk, scan;
    } oem[] = {
        {';', ':', 0xBA, 0x27}, {'=', '+', 0xBB, 0x0D}, {',', '<', 0xBC, 0x33}, {'-', '_', 0xBD, 0x0C},
        {'.', '>', 0xBE, 0x34}, {'/', '?', 0xBF, 0x35}, {'`', '~', 0xC0, 0x29}, {'[', '{', 0xDB, 0x1A},
        {'\\', '|', 0xDC, 0x2B}, {']', '}', 0xDD, 0x1B}, {'\'', '"', 0xDE, 0x28},
    };
    static const UCHAR letter_scan[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                          0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    *vk = 0;
    *scan = 0;
    *shift = FALSE;
    if (c >= 'a' && c <= 'z') {
        *vk = (USHORT)(c - 'a' + 'A');
        *scan = letter_scan[c - 'a'];
    } else if (c >= 'A' && c <= 'Z') {
        *vk = c;
        *scan = letter_scan[c - 'A'];
        *shift = TRUE;
    } else if (c == ' ') {
        *vk = VK_SPACE;
        *scan = 0x39;
    } else {
        for (int i = 0; i < 10; i++) {
            if (c == row1[i] || c == shifted1[i]) {
                *vk = (USHORT)row1[i];
                *scan = (USHORT)(i + 2);
                *shift = c == shifted1[i];
                return;
            }
        }
        for (ULONG i = 0; i < RTL_NUMBER_OF(oem); i++) {
            if (c == (UCHAR)oem[i].plain || c == (UCHAR)oem[i].shifted) {
                *vk = oem[i].vk;
                *scan = oem[i].scan;
                *shift = c == (UCHAR)oem[i].shifted;
                return;
            }
        }
    }
}

/* the escape sequences terminals send for the keys without a character. */
static const struct {
    const char *seq;
    USHORT vk, scan;
} g_escape_keys[] = {
    {"[A", VK_UP, 0x48},       {"[B", VK_DOWN, 0x50},    {"[C", VK_RIGHT, 0x4D},   {"[D", VK_LEFT, 0x4B},
    {"[H", VK_HOME, 0x47},     {"[F", VK_END, 0x4F},     {"OA", VK_UP, 0x48},      {"OB", VK_DOWN, 0x50},
    {"OC", VK_RIGHT, 0x4D},    {"OD", VK_LEFT, 0x4B},    {"OH", VK_HOME, 0x47},    {"OF", VK_END, 0x4F},
    {"OP", VK_F1, 0x3B},       {"OQ", VK_F2, 0x3C},      {"OR", VK_F3, 0x3D},      {"OS", VK_F4, 0x3E},
    {"[1~", VK_HOME, 0x47},    {"[2~", VK_INSERT, 0x52}, {"[3~", VK_DELETE, 0x53}, {"[4~", VK_END, 0x4F},
    {"[5~", VK_PRIOR, 0x49},   {"[6~", VK_NEXT, 0x51},
    {"[15~", VK_F5, 0x3F},     {"[17~", VK_F6, 0x40},    {"[18~", VK_F7, 0x41},    {"[19~", VK_F8, 0x42},
    {"[20~", VK_F9, 0x43},     {"[21~", VK_F10, 0x44},   {"[23~", VK_F11, 0x57},   {"[24~", VK_F12, 0x58},
    {"[Z", VK_TAB, 0x0F},
};

/*
 * decodes the escape sequence at p into a key. returns FALSE for one it
 * does not know, and sets *partial when the input ends before its final byte.
 */
static BOOLEAN escape_key(const UCHAR *p, ULONG length, ULONG *used, USHORT *vk, USHORT *scan, ULONG *state,
                          BOOLEAN *partial)
{
    /* modified keys carry ;<modifier> before the final byte: ESC [ 1 ; 5 C is ctrl+right. */
    char seq[16];
    ULONG n = 0, modifier = 0;
    ULONG i = 1;
    BOOLEAN final = FALSE;
    *partial = FALSE;
    while (i < length && n < sizeof(seq) - 1) {
        UCHAR c = p[i++];
        if (c == ';') {
            while (i < length && p[i] >= '0' && p[i] <= '9')
                modifier = modifier * 10 + (p[i++] - '0');
            continue;
        }
        seq[n++] = (char)c;
        if (n > 1 && (c == '~' || (c >= 'A' && c <= 'Z'))) {
            final = TRUE;
            break;
        }
    }
    seq[n] = 0;
    if (!final) {
        *partial = i >= length && n < sizeof(seq) - 1;
        return FALSE;
    }
    /* ESC [ 1 ; m X keeps the 1 only for the modifier. */
    if (modifier && n == 3 && seq[0] == '[' && seq[1] == '1' && seq[2] != '~') {
        seq[1] = seq[2];
        seq[2] = 0;
    }
    for (ULONG k = 0; k < RTL_NUMBER_OF(g_escape_keys); k++) {
        if (strcmp(seq, g_escape_keys[k].seq))
            continue;
        *vk = g_escape_keys[k].vk;
        *scan = g_escape_keys[k].scan;
        /* the keys of the cursor pad are enhanced when a csi sequence sends them, the function keys never. */
        BOOLEAN pad = (g_escape_keys[k].vk >= VK_PRIOR && g_escape_keys[k].vk <= VK_DOWN) ||
                      g_escape_keys[k].vk == VK_INSERT || g_escape_keys[k].vk == VK_DELETE;
        *state = g_escape_keys[k].vk == VK_TAB ? CON_SHIFT : pad && seq[0] == '[' ? CON_ENHANCED_KEY : 0;
        if (modifier > 1) {
            ULONG m = modifier - 1;
            if (m & 1)
                *state |= CON_SHIFT;
            if (m & 2)
                *state |= CON_LEFT_ALT;
            if (m & 4)
                *state |= CON_LEFT_CTRL;
        }
        *used = i;
        return TRUE;
    }
    return FALSE;
}

/* the utf-16 units of the utf-8 character at p, 0 when it is incomplete. */
static ULONG utf8_char(const UCHAR *p, ULONG length, BOOLEAN more, WCHAR w[2], ULONG *used)
{
    UCHAR c = p[0];
    ULONG need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (need > length) {
        if (more)
            return 0;
        need = 1;
    }
    ULONG written = 0;
    NTSTATUS status = RtlUTF8ToUnicodeN(w, 2 * sizeof(WCHAR), &written, (PCCH)p, need);
    *used = need;
    if (!NT_SUCCESS(status) || !written) {
        w[0] = 0xFFFD;
        return 1;
    }
    return written / sizeof(WCHAR);
}

/* ---- mouse, focus, paste and win32 input reports */

#define BUTTON_LEFT 0x1
#define BUTTON_RIGHT 0x2
#define BUTTON_MIDDLE 0x4
#define BUTTON_FOURTH 0x8
#define BUTTON_FIFTH 0x10
#define WHEEL_DELTA 128

/*
 * the sequence that starts at p, ESC [ then parameters then a final byte.
 * returns 1 and the length when it is whole, 0 when the input ends inside
 * it, -1 when it is no sequence.
 */
static int csi_scan(const UCHAR *p, ULONG length, ULONG *used, LONG params[8], ULONG *count, UCHAR *prefix, UCHAR *final)
{
    ULONG i = 2;
    *count = 0;
    *prefix = 0;
    BOOLEAN have = FALSE;
    LONG value = 0;
    if (i < length && p[i] >= 0x3C && p[i] <= 0x3F)
        *prefix = p[i++];
    for (; i < length; i++) {
        UCHAR c = p[i];
        if (c >= '0' && c <= '9') {
            value = value > 100000000 ? value : value * 10 + (c - '0');
            have = TRUE;
        } else if (c == ';' || c == ':') {
            if (*count < 8)
                params[(*count)++] = have ? value : -1;
            value = 0;
            have = FALSE;
        } else if (c >= 0x40 && c <= 0x7E) {
            if (*count < 8 && (have || *count))
                params[(*count)++] = have ? value : -1;
            *final = c;
            *used = i + 1;
            return 1;
        } else {
            return -1;
        }
    }
    return 0;
}

static ULONG mouse_modifiers(LONG b)
{
    ULONG state = 0;
    if (b & 4)
        state |= CON_SHIFT;
    if (b & 8)
        state |= CON_LEFT_ALT;
    if (b & 16)
        state |= CON_LEFT_CTRL;
    return state;
}

/* a mouse report: b is the button code of the report, x and y the 1 based cell, release when it is a release. */
static ULONG mouse_records(CON_DECODE *st, LONG b, LONG x, LONG y, BOOLEAN release, MIME_CONDRV_RECORD *out, ULONG room)
{
    if (!st->Mouse || room < 1)
        return 0;
    MIME_CONDRV_RECORD *r = &out[0];
    memset(r, 0, sizeof(*r));
    r->EventType = CON_MOUSE_EVENT;
    r->Mouse.X = (SHORT)(x - 1 + st->OriginX);
    r->Mouse.Y = (SHORT)(y - 1 + st->OriginY);
    r->Mouse.ControlKeyState = mouse_modifiers(b);
    if (b & 64) {
        /* the wheel: the sign of the high word says which way, and the flag whether it is the horizontal wheel. */
        /* the console of windows ignores the horizontal wheel. */
        if ((b & 3) >= 2)
            return 0;
        LONG delta = (b & 1) ? -WHEEL_DELTA : WHEEL_DELTA;
        r->Mouse.ButtonState = st->Buttons | ((ULONG)(delta & 0xFFFF) << 16);
        r->Mouse.EventFlags = CON_MOUSE_WHEELED;
        return 1;
    }
    ULONG button = 0;
    LONG code = b & 3;
    if (b & 128)
        button = code == 0 ? BUTTON_FOURTH : BUTTON_FIFTH;
    else
        button = code == 0 ? BUTTON_LEFT : code == 1 ? BUTTON_MIDDLE : code == 2 ? BUTTON_RIGHT : 0;
    if (b & 32) {
        /* a move, with the buttons that are down. */
        r->Mouse.ButtonState = st->Buttons;
        r->Mouse.EventFlags = CON_MOUSE_MOVED;
        return 1;
    }
    if (release) {
        if (button)
            st->Buttons &= ~button;
        else
            st->Buttons = 0;
        r->Mouse.ButtonState = st->Buttons;
        return 1;
    }
    st->Buttons |= button;
    r->Mouse.ButtonState = st->Buttons;
    /* a second press of the same button in the same cell soon after the first is a double click. */
    if (st->LastClickButton == button && st->LastClickX == r->Mouse.X && st->LastClickY == r->Mouse.Y &&
        st->Now - st->LastClick <= 500) {
        r->Mouse.EventFlags = CON_DOUBLE_CLICK;
        st->LastClickButton = 0;
    } else {
        st->LastClickButton = button;
        st->LastClick = st->Now;
        st->LastClickX = r->Mouse.X;
        st->LastClickY = r->Mouse.Y;
    }
    return 1;
}

/* the sequences that are no keys. returns the records made, or -1 when the sequence is not one of them. */
static int special_csi(CON_DECODE *st, const UCHAR *p, ULONG length, BOOLEAN more, ULONG *used, MIME_CONDRV_RECORD *out,
                       ULONG room, BOOLEAN *partial)
{
    LONG params[8];
    ULONG count, n;
    UCHAR prefix, final;
    *partial = FALSE;
    int scan = csi_scan(p, length, used, params, &count, &prefix, &final);
    if (scan == 0) {
        *partial = more;
        return -1;
    }
    if (scan < 0)
        return -1;
    if (prefix == '<' && (final == 'M' || final == 'm') && count == 3) {
        n = mouse_records(st, params[0], params[1], params[2], final == 'm', out, room);
        return (int)n;
    }
    if (!prefix && !count && (final == 'I' || final == 'O')) {
        if (room < 1)
            return -1;
        memset(&out[0], 0, sizeof(out[0]));
        out[0].EventType = CON_FOCUS_EVENT;
        out[0].Value = final == 'I';
        return 1;
    }
    if (!prefix && count == 1 && final == '~' && (params[0] == 200 || params[0] == 201)) {
        st->Paste = params[0] == 200;
        return 0;
    }
    /* the win32 input mode: virtual key, scan code, character, down, control keys and repeat count */
    if (!prefix && final == '_' && count >= 4 && room >= 1) {
        MIME_CONDRV_RECORD *r = &out[0];
        key_record(r, params[3] > 0, params[0] > 0 ? (USHORT)params[0] : 0, params[1] > 0 ? (USHORT)params[1] : 0,
                   params[2] > 0 ? (WCHAR)params[2] : 0, count > 4 && params[4] > 0 ? (ULONG)params[4] : 0);
        r->Key.RepeatCount = count > 5 && params[5] > 0 ? (USHORT)params[5] : 1;
        return 1;
    }
    /* the old report: ESC [ M and three bytes */
    return -1;
}

ULONG ConDecodeInput(CON_DECODE *st, const UCHAR *p, ULONG length, BOOLEAN more, MIME_CONDRV_RECORD *out, ULONG room,
                     ULONG *count)
{
    ULONG i = 0;
    BOOLEAN vt = st->Vt;
    *count = 0;
    while (i < length) {
        UCHAR c = p[i];
        ULONG used = 1, made = 0;
        USHORT vk, scan;
        ULONG state = 0;
        BOOLEAN shift;
        if (st->Paste && !vt) {
            /* inside a bracketed paste the text is typed as it is, until the end mark. */
            if (c == 0x1B && length - i >= 6 && !memcmp(p + i, "\x1b[201~", 6)) {
                st->Paste = FALSE;
                i += 6;
                continue;
            }
            if (c == 0x1B && length - i < 6 && more)
                break;
        }
        if (vt) {
            /* the characters as they came: a press of no key for each unit, the program reads the sequences itself. */
            WCHAR w[2] = {c, 0};
            ULONG units = 1;
            if (c >= 0x80) {
                units = utf8_char(p + i, length - i, more, w, &used);
                if (!units)
                    break;
            }
            if (room - *count < units)
                break;
            for (ULONG u = 0; u < units; u++)
                key_record(&out[*count + made++], TRUE, 0, 0, w[u], 0);
        } else if (c == 0x1B && !st->Paste) {
            BOOLEAN partial = FALSE;
            int special = -1;
            if (i + 1 < length && p[i + 1] == '[') {
                special = special_csi(st, p + i, length - i, more, &used, out + *count, room - *count, &partial);
                if (partial)
                    break;
                /* the old mouse report has the position in bytes after the final. */
                if (special < 0 && length - i >= 3 && p[i + 2] == 'M') {
                    if (length - i < 6) {
                        if (more)
                            break;
                    } else if (st->Mouse && room - *count >= 1) {
                        LONG b = p[i + 3] - 32;
                        BOOLEAN release = (b & 3) == 3 && !(b & 64);
                        special = (int)mouse_records(st, release ? (b & ~3) : b, p[i + 4] - 32, p[i + 5] - 32, release,
                                                     out + *count, room - *count);
                        used = 6;
                    } else {
                        special = 0;
                        used = 6;
                    }
                }
            }
            if (special >= 0) {
                made = (ULONG)special;
                *count += made;
                i += used;
                continue;
            }
            used = 1;
            if (i + 1 < length && (p[i + 1] == '[' || p[i + 1] == 'O') &&
                escape_key(p + i, length - i, &used, &vk, &scan, &state, &partial)) {
                made = key_pair(out + *count, room - *count, vk, scan, 0, state);
            } else if (partial && more) {
                break;
            } else if (i + 1 < length && p[i + 1] >= 0x20 && p[i + 1] < 0x7F) {
                /* option+key on a terminal is escape then the key: alt+key. */
                ascii_key(p[i + 1], &vk, &scan, &shift);
                made = key_pair(out + *count, room - *count, vk, scan, p[i + 1],
                                CON_LEFT_ALT | (shift ? CON_SHIFT : 0));
                used = 2;
            } else {
                made = key_pair(out + *count, room - *count, VK_ESCAPE, 0x01, 0x1B, 0);
            }
        } else if (c == 0x1B) {
            made = key_pair(out + *count, room - *count, VK_ESCAPE, 0x01, 0x1B, 0);
        } else if (c == '\r' || c == '\n') {
            made = key_pair(out + *count, room - *count, VK_RETURN, 0x1C, '\r', 0);
            if (c == '\r' && i + 1 < length && p[i + 1] == '\n')
                used = 2;
        } else if (c == 0x7F || c == 0x08) {
            made = key_pair(out + *count, room - *count, VK_BACK, 0x0E, 0x08, 0);
        } else if (c == '\t') {
            made = key_pair(out + *count, room - *count, VK_TAB, 0x0F, '\t', 0);
        } else if (c == 0) {
            made = key_pair(out + *count, room - *count, VK_SPACE, 0x39, 0, CON_LEFT_CTRL);
        } else if (c < 0x20) {
            /* ctrl+letter. */
            ascii_key((UCHAR)(c + 'a' - 1), &vk, &scan, &shift);
            made = c == 3 ? plain_pair(out + *count, room - *count, vk, scan, c, CON_LEFT_CTRL)
                          : key_pair(out + *count, room - *count, vk, scan, c, CON_LEFT_CTRL);
        } else if (c < 0x80) {
            ascii_key(c, &vk, &scan, &shift);
            made = key_pair(out + *count, room - *count, vk, scan, c, shift ? CON_SHIFT : 0);
        } else {
            /* a utf-8 character: one event per utf-16 unit. */
            WCHAR w[2];
            ULONG units = utf8_char(p + i, length - i, more, w, &used);
            if (!units || room - *count < units * 2)
                break;
            for (ULONG u = 0; u < units; u++)
                made += key_pair(out + *count + made, room - *count - made, 0, 0, w[u], 0);
        }
        if (!made)
            break;
        *count += made;
        i += used;
    }
    return i;
}

/* ------------------------------------------------------------------ vt input */

typedef struct VT_KEY {
    USHORT vk;
    UCHAR modifiers; /* 0 for any, else the shift, alt and ctrl a key must have exactly */
    const char *seq;
} VT_KEY;

static const VT_KEY g_cursor_normal[] = {
    {VK_UP, 0, "\x1b[A"}, {VK_DOWN, 0, "\x1b[B"}, {VK_RIGHT, 0, "\x1b[C"},
    {VK_LEFT, 0, "\x1b[D"}, {VK_HOME, 0, "\x1b[H"}, {VK_END, 0, "\x1b[F"},
};

static const VT_KEY g_cursor_application[] = {
    {VK_UP, 0, "\x1bOA"}, {VK_DOWN, 0, "\x1bOB"}, {VK_RIGHT, 0, "\x1bOC"},
    {VK_LEFT, 0, "\x1bOD"}, {VK_HOME, 0, "\x1bOH"}, {VK_END, 0, "\x1bOF"},
};

/* the same in numeric and application keypad modes. */
static const VT_KEY g_keypad[] = {
    {VK_TAB, 0, "\t"},          {VK_BACK, 0, "\x7f"},       {CON_VK_PAUSE, 0, "\x1a"},  {VK_ESCAPE, 0, "\x1b"},
    {VK_INSERT, 0, "\x1b[2~"},  {VK_DELETE, 0, "\x1b[3~"},  {VK_PRIOR, 0, "\x1b[5~"},   {VK_NEXT, 0, "\x1b[6~"},
    {VK_F1, 0, "\x1bOP"},       {VK_F2, 0, "\x1bOQ"},       {VK_F3, 0, "\x1bOR"},       {VK_F4, 0, "\x1bOS"},
    {VK_F5, 0, "\x1b[15~"},     {VK_F6, 0, "\x1b[17~"},     {VK_F7, 0, "\x1b[18~"},     {VK_F8, 0, "\x1b[19~"},
    {VK_F9, 0, "\x1b[20~"},     {VK_F10, 0, "\x1b[21~"},    {VK_F11, 0, "\x1b[23~"},    {VK_F12, 0, "\x1b[24~"},
};

/* with a modifier: the m becomes 1 plus 1 for shift, 2 for alt and 4 for ctrl. */
static const VT_KEY g_modified[] = {
    {VK_UP, 0, "\x1b[1;mA"},    {VK_DOWN, 0, "\x1b[1;mB"},  {VK_RIGHT, 0, "\x1b[1;mC"},  {VK_LEFT, 0, "\x1b[1;mD"},
    {VK_HOME, 0, "\x1b[1;mH"},  {VK_END, 0, "\x1b[1;mF"},   {VK_F1, 0, "\x1b[1;mP"},     {VK_F2, 0, "\x1b[1;mQ"},
    {VK_F3, 0, "\x1b[1;mR"},    {VK_F4, 0, "\x1b[1;mS"},    {VK_INSERT, 0, "\x1b[2;m~"}, {VK_DELETE, 0, "\x1b[3;m~"},
    {VK_PRIOR, 0, "\x1b[5;m~"}, {VK_NEXT, 0, "\x1b[6;m~"},  {VK_F5, 0, "\x1b[15;m~"},    {VK_F6, 0, "\x1b[17;m~"},
    {VK_F7, 0, "\x1b[18;m~"},   {VK_F8, 0, "\x1b[19;m~"},   {VK_F9, 0, "\x1b[20;m~"},    {VK_F10, 0, "\x1b[21;m~"},
    {VK_F11, 0, "\x1b[23;m~"},  {VK_F12, 0, "\x1b[24;m~"},
};

static const VT_KEY g_simple_modified[] = {
    {VK_BACK, CTRL, "\x08"},          {VK_BACK, ALT, "\x1b\x7f"}, {VK_BACK, CTRL | ALT, "\x1b\x08"},
    {VK_TAB, CTRL, "\t"},             {VK_TAB, CON_SHIFT, "\x1b[Z"}, {VK_DIVIDE, CTRL, "\x1f"},
};

static const VT_KEY *find_key(const VT_KEY *keys, ULONG count, USHORT vk, ULONG state)
{
    for (ULONG i = 0; i < count; i++) {
        if (keys[i].vk != vk)
            continue;
        ULONG m = keys[i].modifiers;
        if (!m || (!(m & CON_SHIFT) == !(state & CON_SHIFT) && !(m & ALT) == !(state & ALT) &&
                   !(m & CTRL) == !(state & CTRL)))
            return &keys[i];
    }
    return nullptr;
}

/* a typed character as vt input reports it: a press of no key. */
static void vt_char(MIME_CONDRV_RECORD *out, WCHAR ch)
{
    key_record(out, TRUE, 0, 0, ch, 0);
}

static ULONG vt_sequence(MIME_CONDRV_RECORD *out, const char *seq, char modifier)
{
    ULONG n = 0;
    for (; *seq && n < CON_MAX_SEQUENCE; seq++)
        vt_char(&out[n++], (WCHAR)(UCHAR)(*seq == 'm' && modifier ? modifier : *seq));
    return n;
}

BOOLEAN ConTranslateKey(const MIME_CONDRV_RECORD *r, BOOLEAN cursor_application, MIME_CONDRV_RECORD *out,
                        ULONG *count)
{
    *count = 0;
    if (r->EventType != CON_KEY_EVENT || !r->Key.KeyDown)
        return FALSE;
    USHORT vk = r->Key.VirtualKey;
    WCHAR ch = r->Key.Char;
    ULONG state = r->Key.ControlKeyState;
    /* altgr is right alt with left ctrl: neither counts as a modifier. */
    if ((state & CON_LEFT_CTRL) && (state & CON_RIGHT_ALT))
        state &= ~(CON_LEFT_CTRL | CON_RIGHT_ALT);
    BOOLEAN alt = (state & ALT) != 0, ctrl = (state & CTRL) != 0, shift = (state & CON_SHIFT) != 0;

    /* ctrl+alt+letter is escape then the control character. */
    if (alt && ctrl && (ch & ~0x20) == 0 && (vk == VK_SPACE || (vk >= 'A' && vk <= 'Z'))) {
        vt_char(&out[0], 0x1B);
        vt_char(&out[1], vk == VK_SPACE ? 0 : (WCHAR)(vk - '@'));
        *count = 2;
        return TRUE;
    }
    if (state & (ALT | CTRL | CON_SHIFT)) {
        const VT_KEY *k = find_key(g_modified, RTL_NUMBER_OF(g_modified), vk, state);
        if (k) {
            *count = vt_sequence(out, k->seq, (char)('1' + shift + 2 * alt + 4 * ctrl));
            return TRUE;
        }
        k = find_key(g_simple_modified, RTL_NUMBER_OF(g_simple_modified), vk, state);
        if (k) {
            *count = vt_sequence(out, k->seq, 0);
            return TRUE;
        }
        if (vk == VK_OEM_2 && ctrl) {
            vt_char(&out[0], 0x1F);
            *count = 1;
            return TRUE;
        }
    }
    if (ch && alt) {
        vt_char(&out[0], 0x1B);
        vt_char(&out[1], ch);
        *count = 2;
        return TRUE;
    }
    if (ctrl && (ch == ' ' || (!ch && vk == VK_NUL_KEY))) {
        key_record(&out[0], TRUE, VK_NUL_KEY, 0, 0, state);
        *count = 1;
        return TRUE;
    }
    if (vk == VK_CANCEL || (vk >= '0' && vk <= 'Z')) {
        if (ch)
            vt_char(&out[(*count)++], ch);
        return TRUE;
    }
    const VT_KEY *k = vk >= VK_END && vk <= VK_DOWN
                          ? find_key(cursor_application ? g_cursor_application : g_cursor_normal, 6, vk, state)
                          : find_key(g_keypad, RTL_NUMBER_OF(g_keypad), vk, state);
    if (!k)
        return FALSE;
    *count = vt_sequence(out, k->seq, 0);
    return TRUE;
}

/* ------------------------------------------------------------------ merging */

/* east asian wide characters take two cells and are not merged. */
static BOOLEAN is_wide(WCHAR c)
{
    static const struct {
        WCHAR first, last;
    } wide[] = {
        {0x1100, 0x115F}, {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF},
        {0xA000, 0xA4CF}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE30, 0xFE4F}, {0xFF00, 0xFF60},
        {0xFFE0, 0xFFE6},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(wide); i++) {
        if (c >= wide[i].first && c <= wide[i].last)
            return TRUE;
    }
    return FALSE;
}

BOOLEAN ConMergeRecord(MIME_CONDRV_RECORD *last, const MIME_CONDRV_RECORD *next)
{
    /* a mouse move only moves the one before it. */
    if (last->EventType == CON_MOUSE_EVENT && next->EventType == CON_MOUSE_EVENT) {
        if (last->Mouse.EventFlags != CON_MOUSE_MOVED || next->Mouse.EventFlags != CON_MOUSE_MOVED)
            return FALSE;
        last->Mouse.X = next->Mouse.X;
        last->Mouse.Y = next->Mouse.Y;
        return TRUE;
    }
    if (last->EventType != CON_KEY_EVENT || next->EventType != CON_KEY_EVENT)
        return FALSE;
    if (!last->Key.KeyDown || !next->Key.KeyDown || is_wide(next->Key.Char))
        return FALSE;
    /* the same key pressed again: by its character under an ime, else by its scan code too. */
    ULONG state = next->Key.ControlKeyState;
    BOOLEAN same = next->Key.Char == last->Key.Char && state == last->Key.ControlKeyState &&
                   ((state & CON_IME_CONVERSION) || next->Key.ScanCode == last->Key.ScanCode);
    if (!same)
        return FALSE;
    last->Key.RepeatCount += next->Key.RepeatCount;
    return TRUE;
}

BOOLEAN ConIsSystemKey(USHORT vk)
{
    /* shift, ctrl, alt, pause, caps lock, the windows keys, num lock and scroll lock. */
    return (vk >= 0x10 && vk <= 0x14) || vk == 0x5B || vk == 0x5C || vk == 0x90 || vk == 0x91;
}
