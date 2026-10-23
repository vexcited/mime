/*
 * console input records. a terminal sends characters and escape sequences but the console reports keys, so input is
 * decoded into a press and a release per key, with us layout virtual keys and scan codes. a program that set
 * ENABLE_VIRTUAL_TERMINAL_INPUT gets the characters as they came, and keys written to it become the sequences a
 * terminal would send, as conhost does.
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
 * a typed key as a press and release. shift, alt and ctrl presses come before it in that order, and their releases
 * after it in reverse order.
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

/* a press and release with no modifier keys around it, as the console reports ctrl+c. */
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

/* the escape sequences terminals send for keys without a character. */
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
 * decodes the escape sequence at p into a key. returns FALSE for an unknown one, and sets *partial when the input
 * ends before its final byte.
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
        /* cursor pad keys sent by a csi sequence are enhanced, function keys never are. */
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
 * scans ESC [, parameters and a final byte at p. returns 1 and the length when whole, 0 when the input ends inside
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

/* a mouse report: b is the report's button code, x and y the 1-based cell. */
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
        /* the wheel: the high word's sign gives the direction. the windows console ignores the horizontal wheel. */
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
        /* a move, with the buttons held. */
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

/* sequences that are not keys. returns the records made, or -1 when the sequence is not one of them. */
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
    /* win32 input mode: virtual key, scan code, character, down, control keys and repeat count. */
    if (!prefix && final == '_' && count >= 4 && room >= 1) {
        MIME_CONDRV_RECORD *r = &out[0];
        key_record(r, params[3] > 0, params[0] > 0 ? (USHORT)params[0] : 0, params[1] > 0 ? (USHORT)params[1] : 0,
                   params[2] > 0 ? (WCHAR)params[2] : 0, count > 4 && params[4] > 0 ? (ULONG)params[4] : 0);
        r->Key.RepeatCount = count > 5 && params[5] > 0 ? (USHORT)params[5] : 1;
        return 1;
    }
    /* the old ESC [ M report has three bytes after the final, the caller decodes it. */
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
            /* raw characters: a press of no key per unit, the program parses the sequences itself. */
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


/* ------------------------------------------------------------------ keys as vt sequences */

/*
 * a key becomes the sequence a terminal sends for it. every sequence has one of four shapes, so they are built from
 * the key's shape and modifiers instead of being listed.
 *
 *   cursor    ESC [ final, or ESC O final in application mode, with modifiers ESC [ 1 ; m final
 *   function  ESC O final for F1 to F4, with modifiers ESC [ 1 ; m final
 *   tilde     ESC [ number ~, with modifiers ESC [ number ; m ~
 *   plain     a few characters
 *
 * m is the character '1' plus 1 for shift, 2 for alt and 4 for ctrl.
 */
typedef struct VT_TEXT {
    char Chars[16];
    ULONG Length;
} VT_TEXT;

static void text_char(VT_TEXT *t, char ch)
{
    t->Chars[t->Length++] = ch;
}

static void text_number(VT_TEXT *t, unsigned n)
{
    if (n >= 10)
        text_char(t, (char)('0' + n / 10));
    text_char(t, (char)('0' + n % 10));
}

typedef enum { SHAPE_NONE, SHAPE_CURSOR, SHAPE_FUNCTION, SHAPE_TILDE } KEY_SHAPE;

/* the final character of a cursor or function key, or the number of a tilde key. */
static KEY_SHAPE key_shape(USHORT vk, char *final, unsigned *number)
{
    static const char cursor_finals[] = {'F', 'H', 'D', 'A', 'C', 'B'}; /* end, home, left, up, right, down. */
    if (vk >= VK_END && vk <= VK_DOWN) {
        *final = cursor_finals[vk - VK_END];
        return SHAPE_CURSOR;
    }
    if (vk >= VK_F1 && vk <= VK_F4) {
        *final = (char)('P' + (vk - VK_F1));
        return SHAPE_FUNCTION;
    }
    if (vk == VK_PRIOR || vk == VK_NEXT)
        *number = vk - 0x1C;
    else if (vk == VK_INSERT || vk == VK_DELETE)
        *number = vk - 0x2B;
    else if (vk == VK_F5)
        *number = 15;
    else if (vk >= VK_F6 && vk <= VK_F10)
        *number = 17 + (vk - VK_F6);
    else if (vk == VK_F11 || vk == VK_F12)
        *number = 23 + (vk - VK_F11);
    else
        return SHAPE_NONE;
    return SHAPE_TILDE;
}

static void shaped_sequence(VT_TEXT *t, KEY_SHAPE shape, char final, unsigned number, char modifier, BOOLEAN application)
{
    text_char(t, 0x1B);
    if (shape == SHAPE_TILDE) {
        text_char(t, '[');
        text_number(t, number);
        if (modifier) {
            text_char(t, ';');
            text_char(t, modifier);
        }
        text_char(t, '~');
        return;
    }
    if (modifier) {
        text_char(t, '[');
        text_char(t, '1');
        text_char(t, ';');
        text_char(t, modifier);
    } else {
        text_char(t, shape == SHAPE_FUNCTION || application ? 'O' : '[');
    }
    text_char(t, final);
}

/* the keys whose sequence depends on exactly which of shift, alt and ctrl are held. */
static BOOLEAN exact_sequence(VT_TEXT *t, USHORT vk, BOOLEAN shift, BOOLEAN alt, BOOLEAN ctrl)
{
    /* ctrl with a digit or space, filled in as terminals do: 2 and space are NUL, 3 to 7 follow ESC, 8 is DEL. */
    if (ctrl && !alt && !shift && (vk == VK_SPACE || (vk >= '2' && vk <= '8'))) {
        text_char(t, vk == VK_SPACE || vk == '2' ? 0 : vk == '8' ? 0x7F : (char)(0x1B + (vk - '3')));
        return TRUE;
    }
    switch (vk) {
    case VK_BACK:
        if (shift || (!alt && !ctrl))
            return FALSE;
        if (alt)
            text_char(t, 0x1B);
        text_char(t, ctrl ? 0x08 : 0x7F);
        return TRUE;
    case VK_TAB:
        if (ctrl && !alt && !shift) {
            text_char(t, 0x09);
        } else if (shift && !alt && !ctrl) {
            text_char(t, 0x1B);
            text_char(t, '[');
            text_char(t, 'Z');
        } else {
            return FALSE;
        }
        return TRUE;
    case VK_DIVIDE:
    case VK_OEM_2:
        if (!ctrl || alt)
            return FALSE;
        if (vk == VK_OEM_2 && shift)
            text_char(t, 0x7F);
        else if (!shift)
            text_char(t, 0x1F);
        else
            return FALSE;
        return TRUE;
    }
    return FALSE;
}

/* the keys that send the same characters whatever is held. */
static BOOLEAN fixed_sequence(VT_TEXT *t, USHORT vk)
{
    switch (vk) {
    case VK_TAB:
        text_char(t, 0x09);
        return TRUE;
    case VK_BACK:
        text_char(t, 0x7F);
        return TRUE;
    case CON_VK_PAUSE:
        text_char(t, 0x1A);
        return TRUE;
    case VK_ESCAPE:
        text_char(t, 0x1B);
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN key_sequence(VT_TEXT *t, const MIME_CONDRV_RECORD *r, BOOLEAN application)
{
    ULONG state = r->Key.ControlKeyState;
    USHORT vk = r->Key.VirtualKey;
    /* right alt with left ctrl is how a layout types its third level characters. */
    if ((state & CON_RIGHT_ALT) && (state & CON_LEFT_CTRL))
        state &= ~(ULONG)(CON_RIGHT_ALT | CON_LEFT_CTRL);
    BOOLEAN shift = (state & CON_SHIFT) != 0, alt = (state & ALT) != 0, ctrl = (state & CTRL) != 0;

    if (ctrl && alt && ((vk >= 'A' && vk <= 'Z') || vk == VK_SPACE)) {
        text_char(t, 0x1B);
        text_char(t, vk == VK_SPACE ? 0 : (char)(vk - 0x40));
        return TRUE;
    }
    char final = 0;
    unsigned number = 0;
    KEY_SHAPE shape = key_shape(vk, &final, &number);
    if (shape != SHAPE_NONE) {
        char modifier = shift || alt || ctrl ? (char)('1' + shift + 2 * alt + 4 * ctrl) : 0;
        shaped_sequence(t, shape, final, number, modifier, application && shape == SHAPE_CURSOR);
        return TRUE;
    }
    if (exact_sequence(t, vk, shift, alt, ctrl) || fixed_sequence(t, vk))
        return TRUE;
    if (alt && r->Key.Char) {
        text_char(t, 0x1B);
        text_char(t, (char)r->Key.Char);
        return TRUE;
    }
    return FALSE;
}

static void sequence_records(MIME_CONDRV_RECORD *out, const VT_TEXT *t)
{
    for (ULONG i = 0; i < t->Length; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].EventType = CON_KEY_EVENT;
        out[i].Key.KeyDown = TRUE;
        out[i].Key.RepeatCount = 1;
        out[i].Key.Char = (UCHAR)t->Chars[i];
    }
}

BOOLEAN ConTranslateKey(const MIME_CONDRV_RECORD *r, BOOLEAN cursor_application, MIME_CONDRV_RECORD *out,
                        ULONG *count)
{
    if (r->EventType != CON_KEY_EVENT || !r->Key.KeyDown)
        return FALSE;
    VT_TEXT t = {};
    if (!key_sequence(&t, r, cursor_application))
        return FALSE;
    sequence_records(out, &t);
    *count = t.Length;
    return TRUE;
}

/* ------------------------------------------------------------------ merging */

static BOOLEAN is_wide(WCHAR c)
{
    static const struct {
        WCHAR First, Last;
    } ranges[] = {
        {0x1100, 0x115F}, /* hangul jamo, the leading consonants. */
        {0x2E80, 0x303E}, /* cjk radicals, kangxi, ideographic punctuation. */
        {0x3041, 0x33FF}, /* hiragana, katakana, bopomofo, hangul compatibility, enclosed cjk. */
        {0x3400, 0x4DBF}, /* cjk extension a. */
        {0x4E00, 0xA4CF}, /* cjk unified ideographs and yi. */
        {0xAC00, 0xD7A3}, /* hangul syllables. */
        {0xF900, 0xFAFF}, /* cjk compatibility ideographs. */
        {0xFE30, 0xFE6F}, /* cjk compatibility forms and small forms. */
        {0xFF00, 0xFF60}, /* full width forms. */
        {0xFFE0, 0xFFE6}, /* full width signs. */
    };
    for (ULONG i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        if (c >= ranges[i].First && c <= ranges[i].Last)
            return TRUE;
    }
    return FALSE;
}

BOOLEAN ConMergeRecord(MIME_CONDRV_RECORD *last, const MIME_CONDRV_RECORD *next)
{
    if (last->EventType != next->EventType)
        return FALSE;
    if (last->EventType == CON_MOUSE_EVENT) {
        if (last->Mouse.EventFlags != CON_MOUSE_MOVED || next->Mouse.EventFlags != CON_MOUSE_MOVED)
            return FALSE;
        last->Mouse.X = next->Mouse.X;
        last->Mouse.Y = next->Mouse.Y;
        return TRUE;
    }
    if (last->EventType != CON_KEY_EVENT || !last->Key.KeyDown || !next->Key.KeyDown)
        return FALSE;
    if (last->Key.Char != next->Key.Char || last->Key.ControlKeyState != next->Key.ControlKeyState ||
        is_wide(next->Key.Char))
        return FALSE;
    if (last->Key.ScanCode != next->Key.ScanCode && !(next->Key.ControlKeyState & CON_IME_CONVERSION))
        return FALSE;
    last->Key.RepeatCount += next->Key.RepeatCount;
    return TRUE;
}

BOOLEAN ConIsSystemKey(USHORT vk)
{
    /* shift, ctrl, alt, pause, caps lock, the windows keys, num lock and scroll lock. */
    return (vk >= 0x10 && vk <= 0x14) || vk == 0x5B || vk == 0x5C || vk == 0x90 || vk == 0x91;
}
