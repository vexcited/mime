/*
 * the output of a program on a screen buffer: text, the control characters
 * and the escape sequences, see conscreen.h. the parser follows the usual
 * state machine of the vt500 terminals; what the sequences do follows what
 * conhost does, and every change of the cells that the terminal can be told
 * of by the same sequence is sent as it is made.
 */
#include "conscreen_p.h"


/* ------------------------------------------------------------------ character width */

typedef struct Range {
    uint32_t Lo, Hi;
} Range;

static const Range g_zero[] = {
    {0x0300, 0x036F},   {0x0483, 0x0489},   {0x0591, 0x05BD},   {0x05BF, 0x05BF},   {0x05C1, 0x05C2},
    {0x05C4, 0x05C5},   {0x05C7, 0x05C7},   {0x0610, 0x061A},   {0x064B, 0x065F},   {0x0670, 0x0670},
    {0x06D6, 0x06DC},   {0x06DF, 0x06E4},   {0x06E7, 0x06E8},   {0x06EA, 0x06ED},   {0x0711, 0x0711},
    {0x0730, 0x074A},   {0x07A6, 0x07B0},   {0x07EB, 0x07F3},   {0x0816, 0x0819},   {0x081B, 0x0823},
    {0x0825, 0x0827},   {0x0829, 0x082D},   {0x0859, 0x085B},   {0x08D3, 0x08E1},   {0x08E3, 0x0902},
    {0x093A, 0x093A},   {0x093C, 0x093C},   {0x0941, 0x0948},   {0x094D, 0x094D},   {0x0951, 0x0957},
    {0x0962, 0x0963},   {0x0981, 0x0981},   {0x09BC, 0x09BC},   {0x09C1, 0x09C4},   {0x09CD, 0x09CD},
    {0x0E31, 0x0E31},   {0x0E34, 0x0E3A},   {0x0E47, 0x0E4E},   {0x0EB1, 0x0EB1},   {0x0EB4, 0x0EBC},
    {0x0EC8, 0x0ECD},   {0x1160, 0x11FF},   {0x135D, 0x135F},   {0x1AB0, 0x1AFF},   {0x1DC0, 0x1DFF},
    {0x200B, 0x200F},   {0x202A, 0x202E},   {0x2060, 0x2064},   {0x20D0, 0x20F0},   {0x302A, 0x302D},
    {0x3099, 0x309A},   {0xFE00, 0xFE0F},   {0xFE20, 0xFE2F},   {0xFEFF, 0xFEFF},   {0xFFF9, 0xFFFB},
    {0x1D167, 0x1D169}, {0x1D173, 0x1D182}, {0x1D185, 0x1D18B}, {0x1D1AA, 0x1D1AD}, {0xE0001, 0xE0001},
    {0xE0020, 0xE007F}, {0xE0100, 0xE01EF},
};

static const Range g_wide[] = {
    {0x1100, 0x115F},   {0x231A, 0x231B},   {0x2329, 0x232A},   {0x23E9, 0x23EC},   {0x23F0, 0x23F0},
    {0x23F3, 0x23F3},   {0x25FD, 0x25FE},   {0x2614, 0x2615},   {0x2648, 0x2653},   {0x267F, 0x267F},
    {0x2693, 0x2693},   {0x26A1, 0x26A1},   {0x26AA, 0x26AB},   {0x26BD, 0x26BE},   {0x26C4, 0x26C5},
    {0x26CE, 0x26CE},   {0x26D4, 0x26D4},   {0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},
    {0x26FA, 0x26FA},   {0x26FD, 0x26FD},   {0x2705, 0x2705},   {0x270A, 0x270B},   {0x2728, 0x2728},
    {0x274C, 0x274C},   {0x274E, 0x274E},   {0x2753, 0x2755},   {0x2757, 0x2757},   {0x2795, 0x2797},
    {0x27B0, 0x27B0},   {0x27BF, 0x27BF},   {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},
    {0x2E80, 0x303E},   {0x3041, 0x33FF},   {0x3400, 0x4DBF},   {0x4E00, 0xA4CF},   {0xA960, 0xA97F},
    {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},   {0xFE10, 0xFE19},   {0xFE30, 0xFE6F},   {0xFF01, 0xFF60},
    {0xFFE0, 0xFFE6},   {0x16FE0, 0x16FE4}, {0x16FF0, 0x16FF1}, {0x17000, 0x18CD5}, {0x18D00, 0x18D08},
    {0x1AFF0, 0x1AFFE}, {0x1B000, 0x1B2FB}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E},
    {0x1F191, 0x1F19A}, {0x1F200, 0x1F202}, {0x1F210, 0x1F23B}, {0x1F240, 0x1F248}, {0x1F250, 0x1F251},
    {0x1F260, 0x1F265}, {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393},
    {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E},
    {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567},
    {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5},
    {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6DC, 0x1F6DF}, {0x1F6EB, 0x1F6EC},
    {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0}, {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945},
    {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FA7C}, {0x1FA80, 0x1FA88}, {0x1FA90, 0x1FABD}, {0x1FABF, 0x1FAC5},
    {0x1FACE, 0x1FADB}, {0x1FAE0, 0x1FAE8}, {0x1FAF0, 0x1FAF8}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
};

static bool in_ranges(const Range *r, size_t n, uint32_t c)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < r[mid].Lo)
            hi = mid;
        else if (c > r[mid].Hi)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

int CsCharWidth(uint32_t c)
{
    if (c < 0x300)
        return 1;
    if (in_ranges(g_zero, sizeof(g_zero) / sizeof(g_zero[0]), c))
        return 0;
    if (c >= 0x1100 && in_ranges(g_wide, sizeof(g_wide) / sizeof(g_wide[0]), c))
        return 2;
    return 1;
}

/* ------------------------------------------------------------------ the cells of a row */

void clear_cell_partner(CsBuffer *b, int y, int x)
{
    /* writing over one half of a wide character leaves the other half blank. */
    CsRow *row = &b->Grid.Rows[y];
    CsCell blank = CsBlank(b);
    if (row->Cells[x].Flags & CS_CELL_LEAD) {
        if (x + 1 < b->Grid.Width && (row->Cells[x + 1].Flags & CS_CELL_TRAIL)) {
            row->Cells[x + 1] = blank;
            CsMark(b, y, x + 1, x + 1, true);
        }
    } else if (row->Cells[x].Flags & CS_CELL_TRAIL) {
        if (x > 0 && (row->Cells[x - 1].Flags & CS_CELL_LEAD)) {
            row->Cells[x - 1] = blank;
            CsMark(b, y, x - 1, x - 1, true);
        }
    }
}

/* erases cells of one row in the buffer with a cell, a wide character cut by the edge goes whole. */
static void erase_with(CsBuffer *b, int y, int x0, int x1, CsCell blank)
{
    if (y < 0 || y >= b->Grid.Height)
        return;
    x0 = CS_MAX(x0, 0);
    x1 = CS_MIN(x1, b->Grid.Width - 1);
    if (x0 > x1)
        return;
    CsRow *row = &b->Grid.Rows[y];
    if (x0 > 0 && (row->Cells[x0].Flags & CS_CELL_TRAIL)) {
        row->Cells[x0 - 1] = blank;
        CsMark(b, y, x0 - 1, x0 - 1, true);
    }
    if (x1 + 1 < b->Grid.Width && (row->Cells[x1].Flags & CS_CELL_LEAD)) {
        row->Cells[x1 + 1] = blank;
        CsMark(b, y, x1 + 1, x1 + 1, true);
    }
    CsRowFill(row, x0, x1, blank);
    row->Touched = true;
}

void CsEraseCells(CsBuffer *b, int y, int x0, int x1)
{
    erase_with(b, y, x0, x1, b->EraseDefault ? CsBlankOf(b->DefaultAttr) : CsEraseBlank(b));
}

void CsEraseRect(CsBuffer *b, int y0, int y1, int x0, int x1)
{
    for (int y = y0; y <= y1; y++)
        CsEraseCells(b, y, x0, x1);
}

/* ------------------------------------------------------------------ cursor and scrolling */

void CsMoveCursor(CsBuffer *b, int x, int y)
{
    x = CS_MAX(0, CS_MIN(x, b->Grid.Width - 1));
    y = CS_MAX(0, CS_MIN(y, b->Grid.Height - 1));
    b->CursorX = x;
    b->CursorY = y;
    b->WrapPending = false;
}

static bool full_margins(const CsBuffer *b)
{
    return b->Top == 0 && b->Bottom == b->Window.Bottom - b->Window.Top;
}

/* the escape sequences are in charge of this buffer: they fill with the erase attributes. */
static bool vt_mode(const CsBuffer *b)
{
    return (b->Mode & (CS_PROCESSED_OUTPUT | CS_VIRTUAL_TERMINAL_PROCESSING)) ==
           (CS_PROCESSED_OUTPUT | CS_VIRTUAL_TERMINAL_PROCESSING);
}

/*
 * scrolls the rows of the margins by count: the viewport rows top..bottom
 * of the buffer. a scroll of the whole viewport slides the viewport down
 * over a buffer that has more rows below it, as conhost does. fill is what
 * the rows that come in are filled with.
 */
void CsScrollRegion(CsBuffer *b, int top, int bottom, int count, bool up, CsCell fill)
{
    CsTerminal *t = b->Term;
    int w = b->Window.Top;
    int height = bottom - top + 1;
    if (count <= 0 || height <= 0)
        return;
    count = CS_MIN(count, height);
    bool whole = full_margins(b);
    bool fwd = CsForwarding(b);
    if (up && whole && b->Window.Bottom < b->Grid.Height - 1 && count == 1 && !b->Main) {
        /* the viewport slides: the row below it comes into view. */
        b->Window.Top++;
        b->Window.Bottom++;
        if (fwd) {
            CsGridScroll(&t->Shadow, 0, t->Rows - 1, 1, true, fill);
            /* the revealed row may hold text the terminal has not seen. */
            CsMark(b, b->Window.Bottom, 0, b->Grid.Width - 1, true);
        } else {
            CsMarkRows(b, b->Window.Top, b->Window.Bottom, false);
        }
        return;
    }
    CsGridScroll(&b->Grid, w + top, w + bottom, count, up, fill);
    if (up && whole && w + top == 0)
        b->Scrolls += (unsigned)count;
    if (fwd && whole) {
        /* the terminal scrolls its screen, its shadow is the same. */
        CsGridScroll(&t->Shadow, top, bottom, count, up, fill);
    } else {
        CsMarkRows(b, w + top, w + bottom, false);
    }
    for (int y = w + top; y <= w + bottom; y++)
        b->Grid.Rows[y].Touched = true;
}

static CsCell scroll_fill(const CsBuffer *b)
{
    if (!vt_mode(b))
        return CsBlank(b);
    return b->EraseDefault ? CsBlankOf(b->DefaultAttr) : CsEraseBlank(b);
}

/*
 * the cursor goes down a row, to the left edge too when carriage is set;
 * the rows scroll when it was at the bottom of the margins. a terminal that
 * shows the buffer is told by the line feed it scrolls with.
 */
void CsNewline(CsBuffer *b, bool carriage)
{
    int vy = b->CursorY - b->Window.Top;
    b->Grid.Rows[b->CursorY].Wrapped = false;
    if (vy >= 0 && vy == b->Bottom) {
        bool whole = full_margins(b);
        bool slides = whole && b->Window.Bottom < b->Grid.Height - 1 && !b->Main;
        CsCell fill = scroll_fill(b);
        if (CsForwarding(b) && whole) {
            CsTerminal *t = b->Term;
            CsHostGoto(t, b->CursorX - b->Window.Left, vy);
            CsHostSetAttr(t, b, (CsAttr){fill.Attr, fill.Ext});
            CsOutString(t, carriage ? "\r\n" : "\n");
            if (carriage)
                t->HostX = 0;
            t->HostWrapPending = false;
        }
        CsScrollRegion(b, b->Top, b->Bottom, 1, true, fill);
        if (slides)
            b->CursorY++;
    } else if (b->CursorY < b->Grid.Height - 1) {
        b->CursorY++;
    }
    if (carriage)
        b->CursorX = b->Window.Left;
    b->WrapPending = false;
}

static void reverse_index(CsBuffer *b)
{
    int vy = b->CursorY - b->Window.Top;
    if (vy >= 0 && vy == b->Top) {
        CsCell fill = scroll_fill(b);
        bool fwd = CsForwarding(b) && full_margins(b);
        CsScrollRegion(b, b->Top, b->Bottom, 1, false, fill);
        if (fwd) {
            CsTerminal *t = b->Term;
            CsHostGoto(t, b->CursorX - b->Window.Left, vy);
            CsHostSetAttr(t, b, (CsAttr){fill.Attr, fill.Ext});
            CsOutString(t, "\x1bM");
            t->HostWrapPending = false;
        }
    } else if (b->CursorY > 0 && vy != 0) {
        b->CursorY--;
    }
    b->WrapPending = false;
}

/* ------------------------------------------------------------------ printing */

/* the text goes on in the next row. */
static void wrap_row(CsBuffer *b)
{
    b->Grid.Rows[b->CursorY].Wrapped = true;
    CsNewline(b, true);
}

/* the terminal's own wrapping is what the buffer asked for. */
static void host_wrap_mode(CsBuffer *b)
{
    CsHostWrap(b->Term, (b->Mode & CS_WRAP_AT_EOL_OUTPUT) != 0);
}

/*
 * a character of the width 1 or 2 at the cursor. in the escape sequence
 * mode the cursor stays on the last column with the wrap pending, as in a
 * terminal; in the plain mode of the console it moves on at once.
 */
void CsPlaceChar(CsBuffer *b, uint32_t cp, int width)
{
    CsTerminal *t = b->Term;
    bool autowrap = (b->Mode & CS_WRAP_AT_EOL_OUTPUT) != 0;
    bool delayed = vt_mode(b);
    int right = b->Grid.Width - 1;
    if (b->WrapPending) {
        if (autowrap)
            wrap_row(b);
        b->WrapPending = false;
    }
    if (width == 2 && b->CursorX == right) {
        /* there is no room for the second half: the line wraps first. */
        if (!autowrap)
            return;
        CsCell blank = CsBlank(b);
        b->Grid.Rows[b->CursorY].Cells[b->CursorX] = blank;
        CsMark(b, b->CursorY, b->CursorX, b->CursorX, true);
        if (CsForwarding(b)) {
            /* the terminal does the same with the character it is sent next. */
            CsShadowFill(t, b->CursorY - b->Window.Top, b->CursorY - b->Window.Top, b->CursorX, b->CursorX, blank);
        }
        wrap_row(b);
    }
    int x = b->CursorX, y = b->CursorY;
    CsRow *row = &b->Grid.Rows[y];
    bool fwd = CsForwarding(b) && !b->InsertMode && CsViewRow(b, y) >= 0;
    bool cut = false;
    for (int i = 0; i < width; i++)
        if (row->Cells[x + i].Flags)
            cut = true;
    if (b->InsertMode) {
        for (int k = right; k - width >= x; k--)
            row->Cells[k] = row->Cells[k - width];
        CsMark(b, y, x, right, false);
        fwd = false;
    }
    if (cut) {
        for (int i = 0; i < width; i++)
            clear_cell_partner(b, y, x + i);
        fwd = false;
    }
    CsCell c = {0, b->Attr.Attr, b->Attr.Ext, 0};
    c.Attr &= (uint16_t)~CS_DBCS_MASK;
    CsCell second = c;
    if (cp >= 0x10000) {
        uint32_t v = cp - 0x10000;
        c.Char = (uint16_t)(0xD800 + (v >> 10));
        second.Char = (uint16_t)(0xDC00 + (v & 0x3FF));
        c.Flags = CS_CELL_LEAD | CS_CELL_PAIR;
        second.Flags = CS_CELL_TRAIL | CS_CELL_PAIR;
    } else {
        c.Char = (uint16_t)cp;
        if (width == 2) {
            c.Flags = CS_CELL_LEAD;
            second.Char = 0;
            second.Flags = CS_CELL_TRAIL;
        }
    }
    row->Cells[x] = c;
    if (width == 2)
        row->Cells[x + 1] = second;
    row->Touched = true;
    if (fwd) {
        host_wrap_mode(b);
        CsHostGoto(t, x - b->Window.Left, y - b->Window.Top);
        CsHostPrintCell(t, b, &c, width == 2 ? &second : nullptr);
        int vy = y - b->Window.Top;
        t->Shadow.Rows[vy].Cells[x - b->Window.Left] = c;
        if (width == 2)
            t->Shadow.Rows[vy].Cells[x - b->Window.Left + 1] = second;
    } else {
        CsMark(b, y, x, x + width - 1, false);
    }
    int nx = x + width;
    if (nx > right) {
        b->CursorX = right;
        if (autowrap && delayed) {
            b->WrapPending = true;
        } else if (autowrap) {
            /* the plain mode moves to the next row at once, and the terminal, which waits, is moved too. */
            bool sync = fwd;
            wrap_row(b);
            if (sync) {
                t->HostWrapPending = false;
                t->HostKnown = false;
            }
        }
    } else {
        b->CursorX = nx;
    }
    b->LastChar = cp;
    b->LastWidth = width;
}

/* ------------------------------------------------------------------ controls */

static int next_tab(const CsBuffer *b, int x)
{
    for (int k = x + 1; k < b->Grid.Width; k++)
        if (b->TabStops[k])
            return k;
    return b->Grid.Width - 1;
}

static int prev_tab(const CsBuffer *b, int x)
{
    for (int k = x - 1; k > 0; k--)
        if (b->TabStops[k])
            return k;
    return 0;
}

static void carriage_return(CsBuffer *b)
{
    /* inside the margins the cursor goes to the left margin, which is the edge of the window here. */
    b->CursorX = b->Window.Left;
    b->WrapPending = false;
}

static void bell(CsBuffer *b)
{
    b->Term->Changed |= CS_CHANGED_BELL;
    b->Term->BellCount++;
    CsOutString(b->Term, "\x07");
}

/* a control character of the escape sequence mode. */
static void execute(CsBuffer *b, uint32_t c)
{
    b->LastChar = 0;
    switch (c) {
    case 0x07:
        bell(b);
        break;
    case 0x08:
        if (b->CursorX > b->Window.Left)
            b->CursorX--;
        b->WrapPending = false;
        break;
    case 0x09:
        b->CursorX = next_tab(b, b->CursorX);
        b->WrapPending = false;
        break;
    case 0x0A:
    case 0x0B:
    case 0x0C:
        CsNewline(b, !(b->Mode & CS_DISABLE_NEWLINE_AUTO_RETURN));
        break;
    case 0x0D:
        carriage_return(b);
        break;
    case 0x0E:
        b->ShiftOut = true;
        break;
    case 0x0F:
        b->ShiftOut = false;
        break;
    default:
        break;
    }
}

/* the glyph of a control character in the oem code page 437 and its relatives. */
static const uint16_t g_oem_glyphs[32] = {
    0x0020, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8, 0x25CB, 0x25D9,
    0x2642, 0x2640, 0x266A, 0x266B, 0x263C, 0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7,
    0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
};

static const uint16_t g_line_drawing[32] = {
    0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518,
    0x2510, 0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524,
    0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
};

static void place(CsBuffer *b, uint32_t c)
{
    int w = CsCharWidth(c);
    if (c >= 0x10000 && w == 1)
        w = 2;
    if (w == 0) {
        /* a mark joins the character before it, which the cells cannot show, but the terminal can. */
        if (CsForwarding(b))
            CsOutUtf8(b->Term, c);
        return;
    }
    CsPlaceChar(b, c, w);
}

/* a printable character, mapped through the character set in use. */
static void print(CsBuffer *b, uint32_t c)
{
    bool graphics = b->ShiftOut ? b->Charset1Graphics : b->Charset0Graphics;
    if (graphics && c >= 0x5F && c <= 0x7E)
        c = c == 0x5F ? 0x00A0 : g_line_drawing[c - 0x60];
    place(b, c);
}

/* ------------------------------------------------------------------ escape sequences */

enum {
    ST_GROUND,
    ST_ESC,
    ST_ESC_INT,
    ST_CSI_ENTRY,
    ST_CSI_PARAM,
    ST_CSI_INT,
    ST_CSI_IGNORE,
    ST_OSC,
    ST_STRING,
    ST_STRING_ESC,
    ST_OSC_ESC,
};

void CsParserReset(CsBuffer *b)
{
    b->ParserState = ST_GROUND;
    b->ParserParamCount = 0;
    b->ParserPrefix = 0;
    b->ParserIntermediate = 0;
    b->ParserIntermediate2 = 0;
    b->ParserHasParam = false;
    b->OscLength = 0;
}

static void clear_params(CsBuffer *b)
{
    memset(b->ParserParamSub, 0, sizeof(b->ParserParamSub));
    b->ParserParamCount = 0;
    b->ParserHasParam = false;
    b->ParserPrefix = 0;
    b->ParserIntermediate = 0;
    b->ParserIntermediate2 = 0;
}

/* parameter i, with a default for an omitted one. */
static int param(const CsBuffer *b, int i, int def)
{
    if (i >= b->ParserParamCount || b->ParserParams[i] < 0)
        return def;
    return b->ParserParams[i];
}

/* a missing, zero or negative parameter is 1. */
static int param_nz(const CsBuffer *b, int i, int def)
{
    int v = param(b, i, def);
    return v <= 0 ? def : v;
}

static void finish_param(CsBuffer *b)
{
    if (b->ParserParamCount < CS_MAX_PARAMS) {
        if (!b->ParserHasParam)
            b->ParserParams[b->ParserParamCount] = -1;
        b->ParserParamCount++;
    }
    b->ParserHasParam = false;
}

static void cursor_to(CsBuffer *b, int col, int row)
{
    /* the coordinates are the viewport's, or the margins' in the origin mode. */
    int top = b->OriginMode ? b->Top : 0;
    int bottom = b->OriginMode ? b->Bottom : b->Window.Bottom - b->Window.Top;
    row = CS_MAX(top, CS_MIN(top + row, bottom));
    col = CS_MAX(0, CS_MIN(col, b->Window.Right - b->Window.Left));
    CsMoveCursor(b, b->Window.Left + col, b->Window.Top + row);
}

static void cursor_rel(CsBuffer *b, int dx, int dy)
{
    int vy = b->CursorY - b->Window.Top;
    int top = 0, bottom = b->Window.Bottom - b->Window.Top;
    /* a move that starts inside the margins stops at them. */
    if (vy >= b->Top && vy <= b->Bottom) {
        top = b->Top;
        bottom = b->Bottom;
    }
    int nx = CS_MAX(b->Window.Left, CS_MIN(b->CursorX + dx, b->Window.Right));
    int ny = CS_MAX(top, CS_MIN(vy + dy, bottom));
    CsMoveCursor(b, nx, b->Window.Top + ny);
}

/* ---- erasing and editing */

static void host_prepare_erase(CsBuffer *b, CsCell blank)
{
    CsTerminal *t = b->Term;
    CsHostGoto(t, b->CursorX - b->Window.Left, b->CursorY - b->Window.Top);
    CsHostSetAttr(t, b, (CsAttr){blank.Attr, blank.Ext});
}

static void shadow_cells(CsBuffer *b, int vy, int x0, int x1, CsCell cell)
{
    CsShadowFill(b->Term, vy, vy, x0 - b->Window.Left, x1 - b->Window.Left, cell);
}

static CsCell erase_cell(const CsBuffer *b)
{
    return b->EraseDefault ? CsBlankOf(b->DefaultAttr) : CsEraseBlank(b);
}

/* returns false for a mode the sequence does not have. */
static bool erase_display(CsBuffer *b, int mode)
{
    if (mode < 0 || mode > 3)
        return false;
    int w = b->Window.Top;
    int rows = b->Window.Bottom - w + 1;
    int vy = b->CursorY - w;
    int left = b->Window.Left, right = b->Window.Right;
    bool fwd = CsForwarding(b);
    CsCell blank = erase_cell(b);
    if (mode == 3) {
        if (fwd)
            CsOutString(b->Term, "\x1b[3J");
        return true;
    }
    if (fwd) {
        host_prepare_erase(b, blank);
        CsOutString(b->Term, mode == 0 ? "\x1b[J" : mode == 1 ? "\x1b[1J" : "\x1b[2J");
        b->Term->HostWrapPending = false;
    }
    b->WrapPending = false;
    int y0, y1;
    if (mode == 0) {
        erase_with(b, b->CursorY, b->CursorX, right, blank);
        if (fwd)
            shadow_cells(b, vy, b->CursorX, right, blank);
        else
            CsMark(b, b->CursorY, b->CursorX, right, true);
        y0 = vy + 1;
        y1 = rows - 1;
    } else if (mode == 1) {
        erase_with(b, b->CursorY, left, b->CursorX, blank);
        if (fwd)
            shadow_cells(b, vy, left, b->CursorX, blank);
        else
            CsMark(b, b->CursorY, left, b->CursorX, true);
        y0 = 0;
        y1 = vy - 1;
    } else {
        y0 = 0;
        y1 = rows - 1;
    }
    for (int y = y0; y <= y1; y++) {
        erase_with(b, w + y, left, right, blank);
        b->Grid.Rows[w + y].Wrapped = false;
        if (fwd)
            shadow_cells(b, y, left, right, blank);
        else
            CsMark(b, w + y, left, right, true);
    }
    return true;
}

static bool erase_line(CsBuffer *b, int mode)
{
    if (mode < 0 || mode > 2)
        return false;
    int y = b->CursorY, vy = y - b->Window.Top;
    int left = b->Window.Left, right = b->Window.Right;
    bool fwd = CsForwarding(b);
    CsCell blank = erase_cell(b);
    int x0 = mode == 0 ? b->CursorX : left, x1 = mode == 1 ? b->CursorX : right;
    b->WrapPending = false;
    if (fwd) {
        host_prepare_erase(b, blank);
        CsOutString(b->Term, mode == 0 ? "\x1b[K" : mode == 1 ? "\x1b[1K" : "\x1b[2K");
        b->Term->HostWrapPending = false;
    }
    erase_with(b, y, x0, x1, blank);
    if (fwd)
        shadow_cells(b, vy, x0, x1, blank);
    else
        CsMark(b, y, x0, x1, true);
    return true;
}

static void erase_chars(CsBuffer *b, int n)
{
    int y = b->CursorY, vy = y - b->Window.Top;
    int x0 = b->CursorX, x1 = CS_MIN(b->CursorX + n - 1, b->Window.Right);
    bool fwd = CsForwarding(b);
    CsCell blank = erase_cell(b);
    b->WrapPending = false;
    if (fwd) {
        host_prepare_erase(b, blank);
        CsOutString(b->Term, "\x1b[");
        CsOutNumber(b->Term, (unsigned)n);
        CsOutString(b->Term, "X");
        b->Term->HostWrapPending = false;
    }
    erase_with(b, y, x0, x1, blank);
    if (fwd)
        shadow_cells(b, vy, x0, x1, blank);
    else
        CsMark(b, y, x0, x1, true);
}

static void insert_chars(CsBuffer *b, int n)
{
    int y = b->CursorY, x = b->CursorX, right = b->Window.Right;
    CsRow *row = &b->Grid.Rows[y];
    n = CS_MIN(n, right - x + 1);
    b->WrapPending = false;
    for (int k = right; k - n >= x; k--)
        row->Cells[k] = row->Cells[k - n];
    erase_with(b, y, x, x + n - 1, erase_cell(b));
    CsMark(b, y, x, right, false);
}

static void delete_chars(CsBuffer *b, int n)
{
    int y = b->CursorY, x = b->CursorX, right = b->Window.Right;
    CsRow *row = &b->Grid.Rows[y];
    n = CS_MIN(n, right - x + 1);
    b->WrapPending = false;
    for (int k = x; k + n <= right; k++)
        row->Cells[k] = row->Cells[k + n];
    erase_with(b, y, right - n + 1, right, erase_cell(b));
    CsMark(b, y, x, right, false);
}

static void insert_lines(CsBuffer *b, int n, bool insert)
{
    int vy = b->CursorY - b->Window.Top;
    if (vy < b->Top || vy > b->Bottom)
        return;
    n = CS_MIN(n, b->Bottom - vy + 1);
    bool fwd = CsForwarding(b) && full_margins(b);
    CsCell fill = erase_cell(b);
    b->WrapPending = false;
    if (fwd) {
        CsTerminal *t = b->Term;
        CsHostGoto(t, b->CursorX - b->Window.Left, vy);
        CsHostSetAttr(t, b, (CsAttr){fill.Attr, fill.Ext});
        CsOutString(t, "\x1b[");
        CsOutNumber(t, (unsigned)n);
        CsOutString(t, insert ? "L" : "M");
        t->HostX = 0;
        t->HostWrapPending = false;
        CsGridScroll(&t->Shadow, vy, b->Bottom, n, !insert, fill);
        CsGridScroll(&b->Grid, b->Window.Top + vy, b->Window.Top + b->Bottom, n, !insert, fill);
    } else {
        CsGridScroll(&b->Grid, b->Window.Top + vy, b->Window.Top + b->Bottom, n, !insert, fill);
        CsMarkRows(b, b->Window.Top + vy, b->Window.Top + b->Bottom, false);
    }
    /* the cursor goes to the left edge, as in the terminals that the escape sequences come from. */
    b->CursorX = b->Window.Left;
}

static void scroll_cmd(CsBuffer *b, int n, bool up)
{
    CsCell fill = erase_cell(b);
    bool fwd = CsForwarding(b) && full_margins(b);
    CsScrollRegion(b, b->Top, b->Bottom, n, up, fill);
    if (fwd) {
        CsTerminal *t = b->Term;
        CsHostSetAttr(t, b, (CsAttr){fill.Attr, fill.Ext});
        CsOutString(t, "\x1b[");
        CsOutNumber(t, (unsigned)n);
        CsOutString(t, up ? "S" : "T");
    }
}

/* ---- attributes */

static void set_underline(CsExt *e, unsigned style)
{
    e->Style = (uint16_t)((e->Style & ~CS_STYLE_UNDERLINE_MASK) | (style << CS_STYLE_UNDERLINE_SHIFT));
}

/*
 * the select graphic rendition: every parameter is applied in turn to a
 * copy of the attribute, which then becomes the attribute of the text.
 */
static void sgr(CsBuffer *b)
{
    CsTerminal *t = b->Term;
    int count = b->ParserParamCount;
    CsExt e = *CsExtGet(t, b->Attr.Ext);
    if (count == 0) {
        b->Attr = b->DefaultAttr;
        return;
    }
    for (int i = 0; i < count; i++) {
        int p = b->ParserParams[i] < 0 ? 0 : b->ParserParams[i];
        bool sub = b->ParserParamSub[i] != 0;
        /* a parameter after a colon belongs to the one before it. */
        if (sub)
            continue;
        bool next_sub = i + 1 < count && b->ParserParamSub[i + 1];
        if (next_sub && p != 4 && p != 38 && p != 48 && p != 58) {
            while (i + 1 < count && b->ParserParamSub[i + 1])
                i++;
            continue;
        }
        switch (p) {
        case 0:
            e = *CsExtGet(t, b->DefaultAttr.Ext);
            break;
        case 1:
            e.Style |= CS_STYLE_BOLD;
            break;
        case 2:
            e.Style |= CS_STYLE_FAINT;
            break;
        case 3:
            e.Style |= CS_STYLE_ITALIC;
            break;
        case 4:
            if (next_sub) {
                int style = b->ParserParams[i + 1];
                i++;
                while (i + 1 < count && b->ParserParamSub[i + 1])
                    i++;
                if (style >= 0 && style <= 5)
                    set_underline(&e, (unsigned)style);
            } else {
                set_underline(&e, 1);
            }
            break;
        case 5:
        case 6:
            e.Style |= CS_STYLE_BLINK;
            break;
        case 7:
            e.Style |= CS_REVERSE_VIDEO;
            break;
        case 8:
            e.Style |= CS_STYLE_CONCEAL;
            break;
        case 9:
            e.Style |= CS_STYLE_STRIKE;
            break;
        case 21:
            set_underline(&e, 2);
            break;
        case 22:
            e.Style &= (uint16_t)~(CS_STYLE_BOLD | CS_STYLE_FAINT);
            break;
        case 23:
            e.Style &= (uint16_t)~CS_STYLE_ITALIC;
            break;
        case 24:
            set_underline(&e, 0);
            break;
        case 25:
            e.Style &= (uint16_t)~CS_STYLE_BLINK;
            break;
        case 27:
            e.Style &= (uint16_t)~CS_REVERSE_VIDEO;
            break;
        case 28:
            e.Style &= (uint16_t)~CS_STYLE_CONCEAL;
            break;
        case 29:
            e.Style &= (uint16_t)~CS_STYLE_STRIKE;
            break;
        case 53:
            e.Style |= CS_STYLE_OVERLINE;
            break;
        case 55:
            e.Style &= (uint16_t)~CS_STYLE_OVERLINE;
            break;
        case 39:
            e.FgKind = CS_COLOR_DEFAULT;
            break;
        case 49:
            e.BgKind = CS_COLOR_DEFAULT;
            break;
        case 38:
        case 48:
        case 58: {
            bool colon = next_sub;
            int kind = i + 1 < count ? b->ParserParams[i + 1] : -1;
            uint8_t ckind = 0;
            uint32_t value = 0;
            bool ok = false;
            int used = 1;
            if (kind == 5 && i + 2 < count) {
                int n = b->ParserParams[i + 2];
                used = 3;
                if (n >= 0 && n <= 255) {
                    ckind = CS_COLOR_256;
                    value = (uint32_t)n;
                    ok = true;
                }
            } else if (kind == 2) {
                int at = i + 2;
                /* the colon form may carry the color space id, empty or not. */
                if (colon && count - at >= 4)
                    at++;
                used = at + 3 - i;
                if (count - at >= 3 || colon || true) {
                    int rgb[3];
                    bool valid = true;
                    for (int k = 0; k < 3; k++) {
                        int v = at + k < count ? b->ParserParams[at + k] : 0;
                        if (v < 0)
                            v = 0;
                        if (v > 255)
                            valid = false;
                        rgb[k] = v;
                    }
                    if (valid) {
                        ckind = CS_COLOR_RGB;
                        value = (uint32_t)rgb[0] | ((uint32_t)rgb[1] << 8) | ((uint32_t)rgb[2] << 16);
                        ok = true;
                    }
                }
            } else {
                used = kind < 0 ? 1 : 2;
            }
            if (ok && p == 38) {
                e.FgKind = ckind;
                e.Fg = value;
            } else if (ok && p == 48) {
                e.BgKind = ckind;
                e.Bg = value;
            }
            i += used - 1;
            if (i >= count)
                i = count - 1;
            break;
        }
        case 59:
            break;
        default:
            if (p >= 30 && p <= 37) {
                e.FgKind = CS_COLOR_16;
                e.Fg = (uint32_t)(p - 30);
            } else if (p >= 40 && p <= 47) {
                e.BgKind = CS_COLOR_16;
                e.Bg = (uint32_t)(p - 40);
            } else if (p >= 90 && p <= 97) {
                e.FgKind = CS_COLOR_16;
                e.Fg = (uint32_t)(p - 90 + 8);
            } else if (p >= 100 && p <= 107) {
                e.BgKind = CS_COLOR_16;
                e.Bg = (uint32_t)(p - 100 + 8);
            }
            break;
        }
    }
    b->Attr = CsAttrOf(t, &e);
}

/* ---- modes */

static void alt_screen(CsBuffer *b, bool enter);

static void save_cursor(CsBuffer *b)
{
    b->SavedValid = true;
    b->SavedX = b->CursorX - b->Window.Left;
    b->SavedY = b->CursorY - b->Window.Top;
    b->SavedAttr = b->Attr;
    b->SavedWrapPending = b->WrapPending;
    b->SavedOrigin = b->OriginMode;
    b->SavedG0 = b->Charset0Graphics;
    b->SavedG1 = b->Charset1Graphics;
    b->SavedShiftOut = b->ShiftOut;
}

static void restore_cursor(CsBuffer *b)
{
    if (!b->SavedValid) {
        b->OriginMode = false;
        cursor_to(b, 0, 0);
        b->Attr = b->DefaultAttr;
        return;
    }
    b->OriginMode = b->SavedOrigin;
    cursor_to(b, b->SavedX, b->SavedY);
    b->Attr = b->SavedAttr;
    b->WrapPending = b->SavedWrapPending;
    b->Charset0Graphics = b->SavedG0;
    b->Charset1Graphics = b->SavedG1;
    b->ShiftOut = b->SavedShiftOut;
}

static void forward_raw(CsBuffer *b, const char *text)
{
    if (CsForwarding(b))
        CsOutString(b->Term, text);
}

static void set_mode(CsBuffer *b, int mode, bool on, bool priv)
{
    CsTerminal *t = b->Term;
    if (!priv) {
        if (mode == 4)
            b->InsertMode = on;
        else if (mode == 20)
            b->Mode = on ? (b->Mode & ~(uint32_t)CS_DISABLE_NEWLINE_AUTO_RETURN) : (b->Mode | CS_DISABLE_NEWLINE_AUTO_RETURN);
        return;
    }
    uint32_t bit = 0;
    switch (mode) {
    case 1: bit = CS_MODE_APP_CURSOR; break;
    case 5:
        forward_raw(b, on ? "\x1b[?5h" : "\x1b[?5l");
        return;
    case 6:
        b->OriginMode = on;
        cursor_to(b, 0, 0);
        return;
    case 7:
        b->Mode = on ? (b->Mode | CS_WRAP_AT_EOL_OUTPUT) : (b->Mode & ~(uint32_t)CS_WRAP_AT_EOL_OUTPUT);
        if (!on)
            b->WrapPending = false;
        return;
    case 12:
        forward_raw(b, on ? "\x1b[?12h" : "\x1b[?12l");
        return;
    case 25:
        b->CursorVisible = on;
        return;
    case 66: bit = CS_MODE_APP_KEYPAD; break;
    case 117:
        b->EraseDefault = on;
        return;
    case 1000: bit = CS_MODE_MOUSE_BUTTON; break;
    case 1002: bit = CS_MODE_MOUSE_DRAG; break;
    case 1003: bit = CS_MODE_MOUSE_ANY; break;
    case 1004: bit = CS_MODE_FOCUS; break;
    case 1005: bit = CS_MODE_MOUSE_UTF8; break;
    case 1006: bit = CS_MODE_MOUSE_SGR; break;
    case 1007: bit = CS_MODE_MOUSE_ALTSCROLL; break;
    case 2004: bit = CS_MODE_BRACKETED_PASTE; break;
    case 9001: bit = CS_MODE_WIN32_INPUT; break;
    case 1049:
        if (on)
            save_cursor(b);
        alt_screen(b, on);
        return;
    default:
        return;
    }
    t->Modes = on ? (t->Modes | bit) : (t->Modes & ~bit);
    t->Changed |= CS_CHANGED_MODES;
}

/*
 * the alternate screen: a buffer of the size of the viewport that is shown
 * instead of this one until the program leaves it. the terminal gets the
 * same switch when it is the main screen that is shown.
 */
static void alt_screen(CsBuffer *b, bool enter)
{
    CsTerminal *t = b->Term;
    /* the sequences of the alternate screen reach the buffer that has it, whichever one the program wrote to. */
    if (b->Main) {
        if (enter)
            return;
        CsBuffer *alt = b;
        b = b->Main;
        if (b->Alt != alt)
            return;
    }
    if (enter) {
        if (b->Alt)
            return;
        int w = b->Window.Right - b->Window.Left + 1, h = b->Window.Bottom - b->Window.Top + 1;
        CsBuffer *alt = CsBufferCreate(t, w, h);
        if (!alt)
            return;
        alt->Main = b;
        alt->Mode = b->Mode;
        alt->DefaultAttr = b->DefaultAttr;
        alt->Attr = b->Attr;
        memcpy(alt->ColorTable, b->ColorTable, sizeof(alt->ColorTable));
        alt->PopupAttr = b->PopupAttr;
        alt->CursorX = b->CursorX - b->Window.Left;
        alt->CursorY = b->CursorY - b->Window.Top;
        alt->CursorVisible = b->CursorVisible;
        alt->CursorSize = b->CursorSize;
        CsCell blank = CsBlankOf(CsEraseAttr(t, b->Attr));
        for (int y = 0; y < alt->Grid.Height; y++)
            CsRowFill(&alt->Grid.Rows[y], 0, alt->Grid.Width - 1, blank);
        b->Alt = alt;
        bool was_shown = t->Shown == b;
        if (was_shown && !t->Headless) {
            if (!t->HostAlt && b == t->Main) {
                CsOutString(t, "\x1b[?1049h");
                t->HostAlt = true;
                t->HostKnown = false;
                t->HostAttrKnown = false;
                CsGridFree(&t->Shadow);
                t->ShadowValid = CsGridInit(&t->Shadow, t->Cols, t->Rows, CsBlankOf((CsAttr){CS_DEFAULT_ATTRIBUTES, CS_EXT_DEFAULT}));
            }
            CsMarkRows(alt, 0, alt->Grid.Height - 1, false);
        }
    } else {
        CsBuffer *alt = b->Alt;
        if (!alt)
            return;
        bool was_shown = t->Shown == b;
        b->Alt = nullptr;
        alt->Main = nullptr;
        /* the main buffer takes the mode the program left the alternate one with. */
        b->Mode = alt->Mode;
        b->CursorVisible = alt->CursorVisible;
        b->CursorSize = alt->CursorSize;
        /* the parser is still inside this buffer; the loop that feeds it destroys it. */
        alt->Detached = true;
        if (was_shown && !t->Headless) {
            if (t->HostAlt && b == t->Main) {
                CsOutString(t, "\x1b[?1049l");
                t->HostAlt = false;
                t->HostKnown = false;
                t->HostAttrKnown = false;
                /* the terminal shows the screen it had; the cells are compared with that. */
            }
            CsMarkRows(b, b->Window.Top, b->Window.Bottom, false);
        }
    }
}

/* ---- reports */

static void report(CsTerminal *t, const char *prefix, int a, int b, int c, char final)
{
    char text[48];
    size_t n = 0;
    for (const char *p = prefix; *p; p++)
        text[n++] = *p;
    int values[3] = {a, b, c};
    for (int k = 0; k < 3; k++) {
        if (values[k] < 0)
            break;
        if (k)
            text[n++] = ';';
        char digits[12];
        int d = 0;
        unsigned v = (unsigned)values[k];
        do {
            digits[d++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        while (d)
            text[n++] = digits[--d];
    }
    text[n++] = final;
    text[n] = 0;
    CsReplyString(t, text);
}

static int mode_state(const CsBuffer *b, int mode, bool priv)
{
    const CsTerminal *t = b->Term;
    if (!priv) {
        if (mode == 4)
            return b->InsertMode ? 1 : 2;
        if (mode == 20)
            return (b->Mode & CS_DISABLE_NEWLINE_AUTO_RETURN) ? 2 : 1;
        return 0;
    }
    switch (mode) {
    case 1: return (t->Modes & CS_MODE_APP_CURSOR) ? 1 : 2;
    case 2: return 1;
    case 5: case 8: case 12: case 40: case 67: case 69:
        return mode == 8 ? 1 : 2;
    case 6: return b->OriginMode ? 1 : 2;
    case 7: return (b->Mode & CS_WRAP_AT_EOL_OUTPUT) ? 1 : 2;
    case 25: return b->CursorVisible ? 1 : 2;
    case 66: return (t->Modes & CS_MODE_APP_KEYPAD) ? 1 : 2;
    case 117: return b->EraseDefault ? 1 : 2;
    case 1000: return (t->Modes & CS_MODE_MOUSE_BUTTON) ? 1 : 2;
    case 1002: return (t->Modes & CS_MODE_MOUSE_DRAG) ? 1 : 2;
    case 1003: return (t->Modes & CS_MODE_MOUSE_ANY) ? 1 : 2;
    case 1004: return (t->Modes & CS_MODE_FOCUS) ? 1 : 2;
    case 1005: return (t->Modes & CS_MODE_MOUSE_UTF8) ? 1 : 2;
    case 1006: return (t->Modes & CS_MODE_MOUSE_SGR) ? 1 : 2;
    case 1007: return (t->Modes & CS_MODE_MOUSE_ALTSCROLL) ? 1 : 2;
    case 1049: return (b->Alt || b->Main) ? 1 : 2;
    case 2004: return (t->Modes & CS_MODE_BRACKETED_PASTE) ? 1 : 2;
    case 9001: return (t->Modes & CS_MODE_WIN32_INPUT) ? 1 : 2;
    default: return 0;
    }
}

/* ---- dispatch */

static void soft_reset(CsBuffer *b)
{
    b->Attr = b->DefaultAttr;
    b->OriginMode = false;
    b->InsertMode = false;
    b->Top = 0;
    b->Bottom = b->Window.Bottom - b->Window.Top;
    b->Charset0Graphics = b->Charset1Graphics = b->ShiftOut = false;
    b->CursorVisible = true;
    b->Mode |= CS_WRAP_AT_EOL_OUTPUT;
    b->SavedValid = false;
    b->SavedX = b->SavedY = 0;
    b->Term->Modes &= ~(uint32_t)(CS_MODE_APP_CURSOR | CS_MODE_APP_KEYPAD);
    b->Term->Changed |= CS_CHANGED_MODES;
    b->EraseDefault = false;
}

static void csi_dispatch(CsBuffer *b, uint32_t final)
{
    CsTerminal *t = b->Term;
    if (b->ParserHasParam || b->ParserParamCount == 0)
        finish_param(b);
    /* an empty list is one omitted parameter, but SGR and the like read the count as it was typed. */
    if (b->ParserParamCount == 1 && b->ParserParams[0] < 0 && final == 'm')
        b->ParserParamCount = 0;
    if (final != 'b')
        b->LastChar = 0;
    bool priv = b->ParserPrefix == '?';
    char im = b->ParserIntermediate;
    /* a colon belongs to the select graphic rendition only. */
    if (final != 'm') {
        for (int i = 0; i < b->ParserParamCount; i++)
            if (b->ParserParamSub[i])
                return;
    }
    if (im == ' ') {
        if (final == 'q' && !b->ParserPrefix) {
            int style = param(b, 0, 0);
            if (style >= 0 && style <= 6) {
                static const char *const codes[] = {"\x1b[0 q", "\x1b[1 q", "\x1b[2 q", "\x1b[3 q",
                                                    "\x1b[4 q", "\x1b[5 q", "\x1b[6 q"};
                b->CursorSize = style == 0 || style <= 2 ? 100 : 25;
                forward_raw(b, codes[style]);
                t->HostCursorStyle = style;
            }
        }
        return;
    }
    if (im == '!') {
        if (final == 'p' && !b->ParserPrefix)
            soft_reset(b);
        return;
    }
    if (im == '$') {
        if (final == 'p') {
            int mode = param(b, 0, 0);
            char prefix[8] = "\x1b[";
            if (priv)
                strcat(prefix, "?");
            report(t, prefix, mode, mode_state(b, mode, priv), -1, '$');
            /* the reply ends with y after the dollar sign. */
            size_t n = t->ReplyLength;
            if (n && t->Reply[n - 1] == '$')
                t->Reply[n - 1] = '$', CsReplyString(t, "y");
        }
        return;
    }
    if (im)
        return;
    if (b->ParserPrefix && !priv) {
        if (b->ParserPrefix == '>' && final == 'c' && param(b, 0, 0) == 0)
            CsReplyString(t, "\x1b[>0;10;1c");
        else if (b->ParserPrefix == '=' && final == 'c' && param(b, 0, 0) == 0)
            CsReplyString(t, "\x1bP!|00000000\x1b\\");
        return;
    }
    int vy = b->CursorY - b->Window.Top;
    int vx = b->CursorX - b->Window.Left;
    if (priv) {
        switch (final) {
        case 'h':
        case 'l':
            for (int i = 0; i < b->ParserParamCount; i++)
                if (b->ParserParams[i] >= 0)
                    set_mode(b, b->ParserParams[i], final == 'h', true);
            break;
        case 'n':
            if (param(b, 0, 0) == 6) {
                int row = vy - (b->OriginMode ? b->Top : 0);
                report(t, "\x1b[?", row + 1, vx + 1, 1, 'R');
            } else if (param(b, 0, 0) == 15) {
                CsReplyString(t, "\x1b[?13n");
            } else if (param(b, 0, 0) == 25) {
                CsReplyString(t, "\x1b[?23n");
            } else if (param(b, 0, 0) == 26) {
                CsReplyString(t, "\x1b[?27;0;0;5n");
            }
            break;
        case 'W':
            if (param(b, 0, 0) == 5)
                CsDefaultTabs(b);
            break;
        default:
            break;
        }
        return;
    }
    switch (final) {
    case 'A':
        cursor_rel(b, 0, -param_nz(b, 0, 1));
        break;
    case 'B':
    case 'e':
        cursor_rel(b, 0, param_nz(b, 0, 1));
        break;
    case 'C':
    case 'a':
        cursor_rel(b, param_nz(b, 0, 1), 0);
        break;
    case 'D':
        cursor_rel(b, -param_nz(b, 0, 1), 0);
        break;
    case 'E':
        cursor_rel(b, 0, param_nz(b, 0, 1));
        b->CursorX = b->Window.Left;
        break;
    case 'F':
        cursor_rel(b, 0, -param_nz(b, 0, 1));
        b->CursorX = b->Window.Left;
        break;
    case 'G':
    case '`':
        cursor_to(b, param_nz(b, 0, 1) - 1, b->OriginMode ? vy - b->Top : vy);
        break;
    case 'd':
        cursor_to(b, vx, param_nz(b, 0, 1) - 1);
        break;
    case 'H':
    case 'f':
        cursor_to(b, param_nz(b, 1, 1) - 1, param_nz(b, 0, 1) - 1);
        break;
    case 'J':
        for (int i = 0; i < (b->ParserParamCount ? b->ParserParamCount : 1); i++)
            if (!erase_display(b, param(b, i, 0)))
                break;
        break;
    case 'K':
        for (int i = 0; i < (b->ParserParamCount ? b->ParserParamCount : 1); i++)
            if (!erase_line(b, param(b, i, 0)))
                break;
        break;
    case 'X':
        erase_chars(b, param_nz(b, 0, 1));
        break;
    case '@':
        insert_chars(b, param_nz(b, 0, 1));
        break;
    case 'P':
        delete_chars(b, param_nz(b, 0, 1));
        break;
    case 'L':
        insert_lines(b, param_nz(b, 0, 1), true);
        break;
    case 'M':
        insert_lines(b, param_nz(b, 0, 1), false);
        break;
    case 'S':
        scroll_cmd(b, param_nz(b, 0, 1), true);
        break;
    case 'T':
        if (b->ParserParamCount <= 1)
            scroll_cmd(b, param_nz(b, 0, 1), false);
        break;
    case 'I':
        for (int n = param_nz(b, 0, 1); n > 0; n--)
            b->CursorX = next_tab(b, b->CursorX);
        b->WrapPending = false;
        break;
    case 'Z':
        for (int n = param_nz(b, 0, 1); n > 0; n--)
            b->CursorX = prev_tab(b, b->CursorX);
        b->WrapPending = false;
        break;
    case 'g':
        for (int i = 0; i < (b->ParserParamCount ? b->ParserParamCount : 1); i++) {
            int m = param(b, i, 0);
            if (m == 0)
                b->TabStops[b->CursorX] = 0;
            else if (m == 3)
                CsClearTabs(b);
            else
                break;
        }
        break;
    case 'b': {
        int n = param_nz(b, 0, 1);
        uint32_t last = b->LastChar;
        int width = b->LastWidth;
        if (last) {
            for (int i = 0; i < CS_MIN(n, 65535); i++)
                CsPlaceChar(b, last, width ? width : 1);
        }
        break;
    }
    case 'h':
    case 'l':
        for (int i = 0; i < b->ParserParamCount; i++) {
            int m = param_nz(b, i, 1);
            if (m != 4 && m != 20)
                break;
            set_mode(b, m, final == 'h', false);
        }
        break;
    case 'm':
        sgr(b);
        break;
    case 'n': {
        int m = param(b, 0, 0);
        if (m == 5) {
            CsReplyString(t, "\x1b[0n");
        } else if (m == 6) {
            int row = vy - (b->OriginMode ? b->Top : 0);
            report(t, "\x1b[", row + 1, vx + 1, -1, 'R');
        }
        break;
    }
    case 'c':
        if (param(b, 0, 0) == 0)
            CsReplyString(t, t->Passthrough ? "\x1b[?61;6;7;21;22;23;24;28;32;42c" : "\x1b[?61;6;7;21;22;23;24;28;32;42c");
        break;
    case 'x':
        if (param(b, 0, 0) == 0)
            CsReplyString(t, "\x1b[2;1;1;128;128;1;0x");
        else if (param(b, 0, 0) == 1)
            CsReplyString(t, "\x1b[3;1;1;128;128;1;0x");
        break;
    case 'r': {
        int rows = b->Window.Bottom - b->Window.Top + 1;
        int top = param_nz(b, 0, 1) - 1;
        int bottom = param_nz(b, 1, rows);
        /* valid margins leave at least two rows and stay inside the viewport; others are ignored. */
        if (top >= 0 && top + 1 < bottom && bottom <= rows) {
            if (top == 0 && bottom == rows) {
                b->Top = 0;
                b->Bottom = rows - 1;
            } else {
                b->Top = top;
                b->Bottom = bottom - 1;
            }
            cursor_to(b, 0, 0);
        }
        break;
    }
    case 's':
        save_cursor(b);
        break;
    case 'u':
        restore_cursor(b);
        break;
    case 't': {
        int op = param(b, 0, 0);
        if (op == 18)
            report(t, "\x1b[8;", b->Window.Bottom - b->Window.Top + 1, b->Grid.Width, -1, 't');
        break;
    }
    default:
        break;
    }
}

static void esc_dispatch(CsBuffer *b, uint32_t final)
{
    b->LastChar = 0;
    if (b->ParserIntermediate == '(' || b->ParserIntermediate == ')') {
        bool graphics = final == '0';
        if (final != '0' && final != 'B' && final != 'A' && final != '1' && final != '2')
            return;
        if (b->ParserIntermediate == '(')
            b->Charset0Graphics = graphics;
        else
            b->Charset1Graphics = graphics;
        return;
    }
    if (b->ParserIntermediate == '#') {
        if (final == '8') {
            /* the screen of E's, with the colors and none of the flags. */
            CsCell cell = CsBlankOf(CsEraseAttr(b->Term, b->Attr));
            cell.Char = 'E';
            for (int y = b->Window.Top; y <= b->Window.Bottom; y++) {
                CsRowFill(&b->Grid.Rows[y], b->Window.Left, b->Window.Right, cell);
                CsMark(b, y, b->Window.Left, b->Window.Right, true);
            }
            b->OriginMode = false;
            b->Top = 0;
            b->Bottom = b->Window.Bottom - b->Window.Top;
            cursor_to(b, 0, 0);
        }
        return;
    }
    if (b->ParserIntermediate)
        return;
    switch (final) {
    case 'D':
        CsNewline(b, false);
        break;
    case 'E':
        CsNewline(b, true);
        break;
    case 'H':
        b->TabStops[b->CursorX] = 1;
        break;
    case 'M':
        reverse_index(b);
        break;
    case '7':
        save_cursor(b);
        break;
    case '8':
        restore_cursor(b);
        break;
    case '=':
        b->Term->Modes |= CS_MODE_APP_KEYPAD;
        b->Term->Changed |= CS_CHANGED_MODES;
        break;
    case '>':
        b->Term->Modes &= ~(uint32_t)CS_MODE_APP_KEYPAD;
        b->Term->Changed |= CS_CHANGED_MODES;
        break;
    case 'Z':
        CsReplyString(b->Term, "\x1b[?61;6;7;21;22;23;24;28;32;42c");
        break;
    case 'c': {
        /* a full reset: the screen is cleared, the modes are the defaults. */
        if (b->Main) {
            alt_screen(b, false);
            return;
        }
        soft_reset(b);
        b->Mode = (b->Mode & ~(uint32_t)CS_DISABLE_NEWLINE_AUTO_RETURN) | CS_WRAP_AT_EOL_OUTPUT;
        CsDefaultTabs(b);
        b->Term->Modes = 0;
        b->Term->Changed |= CS_CHANGED_MODES;
        cursor_to(b, 0, 0);
        erase_display(b, 2);
        erase_display(b, 3);
        break;
    }
    default:
        break;
    }
}

/* ---- strings */

static void osc_forward(CsBuffer *b)
{
    if (!CsForwarding(b) || !b->Osc)
        return;
    CsTerminal *t = b->Term;
    CsOutString(t, "\x1b]");
    CsOutBytes(t, b->Osc, (size_t)b->OscLength);
    CsOutString(t, "\x1b\\");
}

static void osc_dispatch(CsBuffer *b)
{
    CsTerminal *t = b->Term;
    if (!b->Osc)
        return;
    b->Osc[b->OscLength] = 0;
    b->LastChar = 0;
    char *p = b->Osc;
    int code = 0;
    bool have = false;
    while (*p >= '0' && *p <= '9') {
        code = code * 10 + (*p++ - '0');
        have = true;
        if (code > 65535)
            code = 65535;
    }
    if (!have)
        return;
    if (*p == ';')
        p++;
    else if (*p)
        return;
    switch (code) {
    case 0:
    case 1:
    case 2: {
        /* the title: utf-8 to utf-16. an empty one changes nothing. */
        unsigned n = 0;
        const uint8_t *s = (const uint8_t *)p;
        if (!*s)
            break;
        while (*s && n < sizeof(t->Title) / sizeof(t->Title[0]) - 2) {
            uint32_t c = *s++;
            if (c >= 0xF0 && s[0] && s[1] && s[2]) {
                c = ((c & 7) << 18) | ((uint32_t)(s[0] & 0x3F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (uint32_t)(s[2] & 0x3F);
                s += 3;
            } else if (c >= 0xE0 && s[0] && s[1]) {
                c = ((c & 0xF) << 12) | ((uint32_t)(s[0] & 0x3F) << 6) | (uint32_t)(s[1] & 0x3F);
                s += 2;
            } else if (c >= 0xC0 && s[0]) {
                c = ((c & 0x1F) << 6) | (uint32_t)(s[0] & 0x3F);
                s += 1;
            }
            if (c >= 0x10000) {
                c -= 0x10000;
                t->Title[n++] = (uint16_t)(0xD800 + (c >> 10));
                t->Title[n++] = (uint16_t)(0xDC00 + (c & 0x3FF));
            } else {
                t->Title[n++] = (uint16_t)c;
            }
        }
        t->TitleLength = n;
        t->Changed |= CS_CHANGED_TITLE;
        break;
    }
    case 4: {
        /* palette entries n;color pairs: the sixteen first ones are the console's table. */
        char *q = p;
        while (*q) {
            char *end;
            unsigned n = 0;
            for (end = q; *end >= '0' && *end <= '9'; end++)
                n = n * 10 + (unsigned)(*end - '0');
            if (end == q || *end != ';')
                break;
            q = end + 1;
            char *color = q;
            while (*q && *q != ';')
                q++;
            char saved = *q;
            *q = 0;
            unsigned r, g, bl;
            bool ok = false;
            if (color[0] == '#' && strlen(color) == 7) {
                unsigned v = 0;
                ok = true;
                for (int i = 1; i < 7; i++) {
                    char ch = color[i];
                    unsigned d = ch >= '0' && ch <= '9' ? (unsigned)(ch - '0') : ch >= 'a' && ch <= 'f' ? (unsigned)(ch - 'a' + 10) : ch >= 'A' && ch <= 'F' ? (unsigned)(ch - 'A' + 10) : 99;
                    if (d == 99)
                        ok = false;
                    v = v * 16 + d;
                }
                r = v >> 16;
                g = (v >> 8) & 0xFF;
                bl = v & 0xFF;
            } else if (!strncmp(color, "rgb:", 4) && strlen(color) == 12 && color[6] == '/' && color[9] == '/') {
                unsigned vals[3];
                ok = true;
                for (int k = 0; k < 3; k++) {
                    vals[k] = 0;
                    for (int i = 0; i < 2; i++) {
                        char ch = color[4 + k * 3 + i];
                        unsigned d = ch >= '0' && ch <= '9' ? (unsigned)(ch - '0') : ch >= 'a' && ch <= 'f' ? (unsigned)(ch - 'a' + 10) : ch >= 'A' && ch <= 'F' ? (unsigned)(ch - 'A' + 10) : 99;
                        if (d == 99)
                            ok = false;
                        vals[k] = vals[k] * 16 + d;
                    }
                }
                r = vals[0];
                g = vals[1];
                bl = vals[2];
            } else {
                r = g = bl = 0;
            }
            if (ok && n < 16)
                b->ColorTable[CsLegacyToAnsi(n)] = r | (g << 8) | (bl << 16);
            *q = saved;
            if (*q == ';')
                q++;
        }
        osc_forward(b);
        break;
    }
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 52:
    case 104:
    case 110:
    case 111:
    case 112:
    case 133:
    case 633:
    case 1337:
        osc_forward(b);
        break;
    default:
        break;
    }
}

static void osc_add(CsBuffer *b, uint32_t c)
{
    if (b->OscLength >= CS_MAX_OSC)
        return;
    if (!b->Osc) {
        b->Osc = CsAllocate(CS_MAX_OSC + 1);
        if (!b->Osc)
            return;
    }
    /* the string is kept as utf-8. */
    if (c < 0x80) {
        b->Osc[b->OscLength++] = (char)c;
    } else if (c < 0x800 && b->OscLength + 2 <= CS_MAX_OSC) {
        b->Osc[b->OscLength++] = (char)(0xC0 | (c >> 6));
        b->Osc[b->OscLength++] = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000 && b->OscLength + 3 <= CS_MAX_OSC) {
        b->Osc[b->OscLength++] = (char)(0xE0 | (c >> 12));
        b->Osc[b->OscLength++] = (char)(0x80 | ((c >> 6) & 0x3F));
        b->Osc[b->OscLength++] = (char)(0x80 | (c & 0x3F));
    } else if (b->OscLength + 4 <= CS_MAX_OSC) {
        b->Osc[b->OscLength++] = (char)(0xF0 | (c >> 18));
        b->Osc[b->OscLength++] = (char)(0x80 | ((c >> 12) & 0x3F));
        b->Osc[b->OscLength++] = (char)(0x80 | ((c >> 6) & 0x3F));
        b->Osc[b->OscLength++] = (char)(0x80 | (c & 0x3F));
    }
}

/* ------------------------------------------------------------------ the state machine */

static void feed(CsBuffer *b, uint32_t c)
{
    /* controls that act whatever the state is */
    if (c == 0x18 || c == 0x1A) {
        CsParserReset(b);
        if (c == 0x1A)
            place(b, 0x2426);
        return;
    }
    if (c == 0x1B) {
        if (b->ParserState == ST_OSC)
            b->ParserState = ST_OSC_ESC;
        else if (b->ParserState == ST_STRING)
            b->ParserState = ST_STRING_ESC;
        else {
            clear_params(b);
            b->ParserState = ST_ESC;
        }
        return;
    }
    switch (b->ParserState) {
    case ST_GROUND:
        if (c < 0x20)
            execute(b, c);
        else if (c == 0x7F || (c >= 0x80 && c <= 0x9F))
            ;
        else
            print(b, c);
        return;
    case ST_ESC:
        if (c < 0x20) {
            execute(b, c);
        } else if (c >= 0x20 && c <= 0x2F) {
            b->ParserIntermediate = (char)c;
            b->ParserState = ST_ESC_INT;
        } else if (c == '[') {
            clear_params(b);
            b->ParserState = ST_CSI_ENTRY;
        } else if (c == ']') {
            b->OscLength = 0;
            b->ParserState = ST_OSC;
        } else if (c == 'P' || c == 'X' || c == '^' || c == '_') {
            b->ParserState = ST_STRING;
        } else if (c >= 0x30 && c <= 0x7E) {
            esc_dispatch(b, c);
            b->ParserState = ST_GROUND;
        } else {
            b->ParserState = ST_GROUND;
        }
        return;
    case ST_ESC_INT:
        if (c < 0x20) {
            execute(b, c);
        } else if (c >= 0x20 && c <= 0x2F) {
            b->ParserIntermediate2 = (char)c;
        } else if (c >= 0x30 && c <= 0x7E) {
            esc_dispatch(b, c);
            b->ParserState = ST_GROUND;
        } else {
            b->ParserState = ST_GROUND;
        }
        return;
    case ST_CSI_ENTRY:
    case ST_CSI_PARAM:
    case ST_CSI_INT:
    case ST_CSI_IGNORE:
        if (c < 0x20) {
            execute(b, c);
            return;
        }
        if (c == 0x7F)
            return;
        if (c >= 0x40 && c <= 0x7E) {
            if (b->ParserState != ST_CSI_IGNORE)
                csi_dispatch(b, c);
            b->ParserState = ST_GROUND;
            return;
        }
        if (c >= 0x20 && c <= 0x2F) {
            if (b->ParserState != ST_CSI_IGNORE) {
                if (b->ParserIntermediate)
                    b->ParserIntermediate2 = (char)c;
                else
                    b->ParserIntermediate = (char)c;
                b->ParserState = ST_CSI_INT;
            }
            return;
        }
        if (c >= 0x30 && c <= 0x3F) {
            if (b->ParserState == ST_CSI_INT || b->ParserState == ST_CSI_IGNORE) {
                b->ParserState = ST_CSI_IGNORE;
                return;
            }
            if (c >= 0x3C) {
                if (b->ParserState == ST_CSI_ENTRY) {
                    b->ParserPrefix = (char)c;
                    b->ParserState = ST_CSI_PARAM;
                } else {
                    b->ParserState = ST_CSI_IGNORE;
                }
                return;
            }
            b->ParserState = ST_CSI_PARAM;
            if (c >= '0' && c <= '9') {
                if (b->ParserParamCount >= CS_MAX_PARAMS)
                    return;
                if (!b->ParserHasParam) {
                    b->ParserParams[b->ParserParamCount] = 0;
                    b->ParserHasParam = true;
                }
                int *v = &b->ParserParams[b->ParserParamCount];
                if (*v < 6553)
                    *v = *v * 10 + (int)(c - '0');
                else
                    *v = 65535;
            } else if (c == ';' || c == ':') {
                if (b->ParserParamCount >= CS_MAX_PARAMS)
                    return;
                if (!b->ParserHasParam)
                    b->ParserParams[b->ParserParamCount] = -1;
                b->ParserParamCount++;
                b->ParserHasParam = false;
                if (b->ParserParamCount < CS_MAX_PARAMS)
                    b->ParserParamSub[b->ParserParamCount] = c == ':';
            }
            return;
        }
        /* a character no sequence has: the sequence is abandoned and the character is text. */
        b->ParserState = ST_GROUND;
        print(b, c);
        return;
    case ST_OSC:
        if (c == 0x07) {
            osc_dispatch(b);
            b->ParserState = ST_GROUND;
        } else if (c >= 0x20) {
            osc_add(b, c);
        }
        return;
    case ST_OSC_ESC:
        if (c == '\\')
            osc_dispatch(b);
        b->ParserState = ST_GROUND;
        if (c != '\\')
            feed(b, c);
        return;
    case ST_STRING:
        return;
    case ST_STRING_ESC:
        b->ParserState = ST_GROUND;
        if (c != '\\')
            feed(b, c);
        return;
    default:
        b->ParserState = ST_GROUND;
        return;
    }
}

/* ------------------------------------------------------------------ the text of the plain mode */

/* the backspace of the plain mode goes to the start of the cell before, and never to the row before. */
static void legacy_backspace(CsBuffer *b)
{
    if (b->WrapPending)
        b->WrapPending = false;
    if (b->CursorX > b->Window.Left) {
        b->CursorX--;
        if (b->Grid.Rows[b->CursorY].Cells[b->CursorX].Flags & CS_CELL_TRAIL && b->CursorX > b->Window.Left)
            b->CursorX--;
    }
}

/* a tab writes blanks up to the next multiple of 8, over what is there. */
static void legacy_tab(CsBuffer *b)
{
    int left = b->Grid.Width - b->CursorX;
    int n = 8 - (b->CursorX % 8);
    if (n > left)
        n = left;
    for (int i = 0; i < n; i++)
        CsPlaceChar(b, ' ', 1);
}

/* text of a buffer that does not take escape sequences. */
static void legacy_char(CsBuffer *b, uint32_t c)
{
    /* a wrap the escape sequence mode left pending happens first, if the cursor has not moved. */
    if (b->WrapPending) {
        if (b->Mode & CS_WRAP_AT_EOL_OUTPUT)
            wrap_row(b);
        b->WrapPending = false;
    }
    if (!(b->Mode & CS_PROCESSED_OUTPUT)) {
        if (c < 0x20 || c == 0x7F) {
            /* the control code is kept in the cell as it is. */
            CsPlaceChar(b, c, 1);
            return;
        }
        place(b, c);
        return;
    }
    switch (c) {
    case 0x07:
        bell(b);
        return;
    case 0x08:
        legacy_backspace(b);
        return;
    case 0x09:
        legacy_tab(b);
        return;
    case 0x0A:
        CsNewline(b, !(b->Mode & CS_DISABLE_NEWLINE_AUTO_RETURN));
        return;
    case 0x0D:
        carriage_return(b);
        return;
    case 0x00:
        CsPlaceChar(b, ' ', 1);
        return;
    default:
        break;
    }
    if (c < 0x20 || c == 0x7F) {
        /* the glyphs of the oem code pages; the code page of utf-8 has none. */
        if (!b->Term->OemGlyphs)
            return;
        c = c == 0x7F ? 0x2302 : g_oem_glyphs[c];
    }
    place(b, c);
}

void CsPutText(CsBuffer *root, const uint16_t *text, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        /* the buffer that takes the text can change in the middle of it, with the alternate screen. */
        CsBuffer *b = CsEffective(root);
        bool vt = vt_mode(b);
        uint32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (b->HighSurrogate)
                c = 0xFFFD;
            else {
                b->HighSurrogate = (uint16_t)c;
                continue;
            }
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            if (b->HighSurrogate) {
                c = 0x10000 + (((uint32_t)b->HighSurrogate - 0xD800) << 10) + (c - 0xDC00);
                b->HighSurrogate = 0;
            } else {
                c = 0xFFFD;
            }
        } else if (b->HighSurrogate) {
            /* the pair never ended. */
            b->HighSurrogate = 0;
            if (vt)
                feed(b, 0xFFFD);
            else
                legacy_char(b, 0xFFFD);
        }
        if (vt) {
            feed(b, c);
        } else {
            if (b->ParserState != ST_GROUND)
                CsParserReset(b);
            legacy_char(b, c);
        }
        if (b->Detached)
            CsBufferDestroy(b);
    }
}

void CsPutLegacy(CsBuffer *root, const uint16_t *text, size_t count)
{
    CsBuffer *b = CsEffective(root);
    for (size_t i = 0; i < count; i++) {
        uint32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < count && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00u);
            i++;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        legacy_char(b, c);
    }
}

void CsWrite(CsBuffer *b, const uint16_t *text, size_t count)
{
    CsPutText(b, text, count);
    CsRender(b->Term);
}
