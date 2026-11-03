/*
 * console screen buffers and the host terminal that shows them: cells, attributes, terminal output and the buffer
 * calls. escape sequences and text are in convt.c.
 *
 * the terminal is kept in step two ways. program writes are applied to the cells and forwarded as they are applied,
 * so the terminal scrolls and wraps like the console and nothing that scrolls off is lost. changes from calls like
 * WriteConsoleOutput are marked, and CsRender diffs the marked cells against the shadow, a copy of what the terminal
 * shows, and sends the difference.
 */
#include "conscreen_p.h"


/* ------------------------------------------------------------------ cells and grids */

CsCell CsBlankOf(CsAttr attr)
{
    CsCell c = {' ', attr.Attr, attr.Ext, 0};
    return c;
}

CsCell CsBlank(const CsBuffer *buffer)
{
    return CsBlankOf(buffer->Attr);
}

/* erasing in an escape sequence keeps the text colors but none of its flags. */
CsCell CsEraseBlank(const CsBuffer *buffer)
{
    return CsBlankOf(CsEraseAttr(buffer->Term, buffer->Attr));
}

bool CsCellEqual(const CsCell *a, const CsCell *b)
{
    return a->Char == b->Char && a->Attr == b->Attr && a->Ext == b->Ext && a->Flags == b->Flags;
}

bool CsGridInit(CsGrid *grid, int width, int height, CsCell fill)
{
    memset(grid, 0, sizeof(*grid));
    if (width < 1 || height < 1 || width > CS_MAX_DIMENSION || height > CS_MAX_DIMENSION ||
        (uint64_t)width * (uint64_t)height > CS_MAX_CELLS)
        return false;
    grid->Rows = CsAllocate(sizeof(CsRow) * (size_t)height);
    if (!grid->Rows)
        return false;
    memset(grid->Rows, 0, sizeof(CsRow) * (size_t)height);
    /* one block of cells with rows pointing into it, so freeing is one call and rows move by pointer. */
    CsCell *block = CsAllocate(sizeof(CsCell) * (size_t)width * (size_t)height);
    if (!block) {
        CsFree(grid->Rows);
        grid->Rows = nullptr;
        return false;
    }
    for (int y = 0; y < height; y++) {
        grid->Rows[y].Cells = block + (size_t)y * (size_t)width;
        grid->Rows[y].DirtyLeft = 1;
        grid->Rows[y].DirtyRight = 0;
        for (int x = 0; x < width; x++)
            grid->Rows[y].Cells[x] = fill;
    }
    grid->Width = width;
    grid->Height = height;
    return true;
}

/* scrolling reorders the rows, so the block starts at the lowest row pointer. */
static CsCell *grid_block(const CsGrid *grid)
{
    CsCell *low = nullptr;
    for (int y = 0; y < grid->Height; y++)
        if (!low || grid->Rows[y].Cells < low)
            low = grid->Rows[y].Cells;
    return low;
}

void CsGridFree(CsGrid *grid)
{
    if (grid->Rows) {
        CsFree(grid_block(grid));
        CsFree(grid->Rows);
    }
    memset(grid, 0, sizeof(*grid));
}

void CsRowFill(CsRow *row, int from, int to, CsCell cell)
{
    for (int x = from; x <= to; x++)
        row->Cells[x] = cell;
}

/* moves rows top..bottom by count rows and fills the rows that come in. */
void CsGridScroll(CsGrid *grid, int top, int bottom, int count, bool up, CsCell fill)
{
    int height = bottom - top + 1;
    if (count <= 0 || height <= 0)
        return;
    if (count > height)
        count = height;
    CsRow *rows = grid->Rows + top;
    CsRow saved[64];
    CsRow *spare = count <= 64 ? saved : CsAllocate(sizeof(CsRow) * (size_t)count);
    if (!spare) {
        /* out of memory: blank the rows instead of moving them. */
        for (int y = top; y <= bottom; y++) {
            CsRowFill(&grid->Rows[y], 0, grid->Width - 1, fill);
            grid->Rows[y].Wrapped = false;
        }
        return;
    }
    if (up) {
        memcpy(spare, rows, sizeof(CsRow) * (size_t)count);
        memmove(rows, rows + count, sizeof(CsRow) * (size_t)(height - count));
        memcpy(rows + height - count, spare, sizeof(CsRow) * (size_t)count);
        for (int i = height - count; i < height; i++) {
            CsRowFill(&rows[i], 0, grid->Width - 1, fill);
            rows[i].Wrapped = false;
            rows[i].Touched = false;
            rows[i].Known = true;
        }
    } else {
        memcpy(spare, rows + height - count, sizeof(CsRow) * (size_t)count);
        memmove(rows + count, rows, sizeof(CsRow) * (size_t)(height - count));
        memcpy(rows, spare, sizeof(CsRow) * (size_t)count);
        for (int i = 0; i < count; i++) {
            CsRowFill(&rows[i], 0, grid->Width - 1, fill);
            rows[i].Wrapped = false;
            rows[i].Touched = false;
            rows[i].Known = true;
        }
    }
    if (spare != saved)
        CsFree(spare);
}

/* ------------------------------------------------------------------ attributes and colors */

/* the initial console colors as 0x00BBGGRR: the windows 10 campbell scheme. */
static const uint32_t g_default_table[16] = {
    0x000C0C0C, 0x00DA3700, 0x000EA113, 0x00DD963A, 0x001F0FC5, 0x00981788, 0x00009CC1, 0x00CCCCCC,
    0x00767676, 0x00FF783B, 0x000CC616, 0x00D6D661, 0x005648E7, 0x009E00B4, 0x00A5F1F9, 0x00F2F2F2,
};

/*
 * a cell's attribute word has only 16 colors, so 256 color and true color sequences are reduced to the nearest one by
 * squared sRGB distance against the colors below, in attribute word order, lowest index winning ties. the console
 * palette is not consulted, so changing it does not change what cell reads report.
 */
static const uint32_t g_reference_colors[16] = {
    0x000000, 0x000080, 0x008000, 0x008080, 0x800000, 0x800080, 0x808000, 0xC0C0C0,
    0x808080, 0x0000FF, 0x00FF00, 0x00FFFF, 0xFF0000, 0xFF00FF, 0xFFFF00, 0xFFFFFF,
};

unsigned CsReduceRgb(unsigned r, unsigned g, unsigned b)
{
    unsigned best = 0;
    uint32_t best_distance = UINT32_MAX;
    for (unsigned i = 0; i < 16; i++) {
        int dr = (int)r - (int)(g_reference_colors[i] >> 16);
        int dg = (int)g - (int)((g_reference_colors[i] >> 8) & 0xFF);
        int db = (int)b - (int)(g_reference_colors[i] & 0xFF);
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

/* only the top 3 bits of red and green and the top 2 of blue count, each spread back over the whole range. */
unsigned CsReduceTrueColor(uint32_t rgb)
{
    unsigned r = (rgb & 0xFF) >> 5, g = ((rgb >> 8) & 0xFF) >> 5, b = ((rgb >> 16) & 0xFF) >> 6;
    return CsReduceRgb(r * 255 / 7, g * 255 / 7, b * 255 / 3);
}

/* the 256 color cube uses the levels below on each axis, followed by a ramp of 24 grays. */
unsigned CsReduceIndex(unsigned n)
{
    static const unsigned levels[6] = {0, 95, 135, 175, 215, 255};
    n &= 0xFF;
    if (n < 16)
        return CsAnsiToLegacy(n);
    if (n < 232) {
        n -= 16;
        return CsReduceRgb(levels[n / 36], levels[n / 6 % 6], levels[n % 6]);
    }
    unsigned gray = 8 + 10 * (n - 232);
    return CsReduceRgb(gray, gray, gray);
}

#define LEGACY_DEFAULT_FG 7
#define LEGACY_DEFAULT_BG 0

/* terminal and console indexes of the 16 colors swap the red and blue bits. */
uint32_t CsLegacyToAnsi(uint32_t index)
{
    return (index & 8) | ((index & 1) << 2) | (index & 2) | ((index & 4) >> 2);
}

uint32_t CsAnsiToLegacy(uint32_t index)
{
    return CsLegacyToAnsi(index);
}

const CsExt *CsExtGet(const CsTerminal *term, unsigned index)
{
    return index < term->ExtCount ? &term->Ext[index] : &term->Ext[0];
}

static bool ext_equal(const CsExt *e, const CsExt *x)
{
    return e->FgKind == x->FgKind && e->BgKind == x->BgKind && e->Style == x->Style &&
           (e->FgKind == CS_COLOR_DEFAULT || e->Fg == x->Fg) && (e->BgKind == CS_COLOR_DEFAULT || e->Bg == x->Bg);
}

static unsigned ext_hash(const CsExt *e)
{
    uint32_t h = e->FgKind * 0x9E3779B1u ^ (e->BgKind + 7u) * 0x85EBCA6Bu ^ (uint32_t)e->Style * 0xC2B2AE35u;
    if (e->FgKind != CS_COLOR_DEFAULT)
        h = (h ^ e->Fg) * 0x27D4EB2Fu;
    if (e->BgKind != CS_COLOR_DEFAULT)
        h = (h ^ (e->Bg + 0x165667B1u)) * 0x9E3779B1u;
    return h ^ (h >> 15);
}

/* linear probing with at least 4 slots per attribute, a lookup ends at the first empty slot. */
static bool ext_rehash(CsTerminal *term)
{
    unsigned size = 256;
    while (size < term->ExtCap * 4)
        size *= 2;
    uint16_t *table = CsAllocate(sizeof(uint16_t) * size);
    if (!table)
        return false;
    memset(table, 0, sizeof(uint16_t) * size);
    for (unsigned i = 0; i < term->ExtCount; i++) {
        unsigned slot = ext_hash(&term->Ext[i]) & (size - 1);
        while (table[slot])
            slot = (slot + 1) & (size - 1);
        table[slot] = (uint16_t)(i + 1);
    }
    CsFree(term->ExtHash);
    term->ExtHash = table;
    term->ExtHashSize = size;
    return true;
}

uint16_t CsExtIntern(CsTerminal *term, const CsExt *ext)
{
    if (term->ExtHash) {
        unsigned slot = ext_hash(ext) & (term->ExtHashSize - 1);
        while (term->ExtHash[slot]) {
            unsigned i = term->ExtHash[slot] - 1u;
            if (ext_equal(&term->Ext[i], ext))
                return (uint16_t)i;
            slot = (slot + 1) & (term->ExtHashSize - 1);
        }
    }
    if (term->ExtCount >= CS_MAX_EXT)
        return CS_EXT_DEFAULT;
    if (term->ExtCount == term->ExtCap) {
        unsigned cap = term->ExtCap ? term->ExtCap * 2 : 32;
        CsExt *grown = CsAllocate(sizeof(CsExt) * cap);
        if (!grown)
            return CS_EXT_DEFAULT;
        if (term->Ext) {
            memcpy(grown, term->Ext, sizeof(CsExt) * term->ExtCount);
            CsFree(term->Ext);
        }
        term->Ext = grown;
        term->ExtCap = cap;
        if (!ext_rehash(term)) {
            return CS_EXT_DEFAULT;
        }
    }
    CsExt *stored = &term->Ext[term->ExtCount];
    *stored = *ext;
    if (stored->FgKind == CS_COLOR_DEFAULT)
        stored->Fg = 0;
    if (stored->BgKind == CS_COLOR_DEFAULT)
        stored->Bg = 0;
    uint16_t index = (uint16_t)term->ExtCount++;
    if (!term->ExtHash && !ext_rehash(term))
        return index;
    unsigned slot = ext_hash(stored) & (term->ExtHashSize - 1);
    while (term->ExtHash[slot])
        slot = (slot + 1) & (term->ExtHashSize - 1);
    term->ExtHash[slot] = (uint16_t)(index + 1);
    return index;
}

static unsigned legacy_color(uint8_t kind, uint32_t v, unsigned fallback)
{
    switch (kind) {
    case CS_COLOR_DEFAULT:
        return fallback;
    case CS_COLOR_16:
    case CS_COLOR_256:
        return CsReduceIndex(v);
    default:
        return CsReduceTrueColor(v);
    }
}

/* the attribute word that cell reads report. */
static uint16_t legacy_word(const CsExt *e)
{
    unsigned fg = legacy_color(e->FgKind, e->Fg, LEGACY_DEFAULT_FG), bg = legacy_color(e->BgKind, e->Bg, LEGACY_DEFAULT_BG);
    if ((e->Style & CS_STYLE_BOLD) && e->FgKind <= CS_COLOR_16)
        fg |= 8;
    return (uint16_t)((e->Style & CS_STYLE_LVB_MASK) | fg | (bg << 4));
}

CsAttr CsAttrOf(CsTerminal *term, const CsExt *ext)
{
    CsAttr a;
    a.Ext = CsExtIntern(term, ext);
    a.Attr = legacy_word(&term->Ext[a.Ext]);
    return a;
}

/*
 * attributes set through a call map to the 256 colors, except the initial gray on black, which stays the terminal's
 * own default so a program that restores it leaves the terminal theme alone.
 */
CsAttr CsAttrFromLegacy(CsTerminal *term, uint16_t attributes)
{
    CsExt e = {};
    unsigned fg = attributes & CS_FOREGROUND_MASK, bg = (attributes & CS_BACKGROUND_MASK) >> 4;
    e.Style = attributes & CS_STYLE_LVB_MASK;
    if (fg != LEGACY_DEFAULT_FG) {
        e.FgKind = CS_COLOR_256;
        e.Fg = CsLegacyToAnsi(fg);
    }
    if (bg != LEGACY_DEFAULT_BG) {
        e.BgKind = CS_COLOR_256;
        e.Bg = CsLegacyToAnsi(bg);
    }
    return CsAttrOf(term, &e);
}

/* erasing and scrolling in escape sequences fill with the colors but none of the flags. */
CsAttr CsEraseAttr(CsTerminal *term, CsAttr attr)
{
    CsExt e = *CsExtGet(term, attr.Ext);
    e.Style = 0;
    return CsAttrOf(term, &e);
}

/* ------------------------------------------------------------------ bytes for the terminal */

static bool grow(uint8_t **buffer, size_t *cap, size_t need)
{
    if (need <= *cap)
        return true;
    size_t n = *cap ? *cap : 256;
    while (n < need)
        n *= 2;
    uint8_t *bigger = CsAllocate(n);
    if (!bigger)
        return false;
    return (*buffer ? (memcpy(bigger, *buffer, *cap), CsFree(*buffer), true) : true) && ((*buffer = bigger), (*cap = n), true);
}

void CsOutBytes(CsTerminal *term, const void *data, size_t length)
{
    if (term->Headless || !length)
        return;
    if (!grow(&term->Out, &term->OutCap, term->OutLength + length)) {
        term->OutFailed = true;
        return;
    }
    memcpy(term->Out + term->OutLength, data, length);
    term->OutLength += length;
}

void CsOutString(CsTerminal *term, const char *text)
{
    CsOutBytes(term, text, strlen(text));
}

void CsEmit(CsTerminal *term, const char *text)
{
    CsOutString(term, text);
}

void CsOutNumber(CsTerminal *term, unsigned value)
{
    char digits[12];
    int n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    char out[12];
    for (int i = 0; i < n; i++)
        out[i] = digits[n - 1 - i];
    CsOutBytes(term, out, (size_t)n);
}

void CsOutUtf8(CsTerminal *term, uint32_t c)
{
    uint8_t b[4];
    size_t n;
    if (c < 0x80) {
        b[0] = (uint8_t)c;
        n = 1;
    } else if (c < 0x800) {
        b[0] = (uint8_t)(0xC0 | (c >> 6));
        b[1] = (uint8_t)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        b[0] = (uint8_t)(0xE0 | (c >> 12));
        b[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
        b[2] = (uint8_t)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        b[0] = (uint8_t)(0xF0 | (c >> 18));
        b[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3F));
        b[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
        b[3] = (uint8_t)(0x80 | (c & 0x3F));
        n = 4;
    }
    CsOutBytes(term, b, n);
}

void CsReplyString(CsTerminal *term, const char *text)
{
    size_t length = strlen(text);
    if (!grow(&term->Reply, &term->ReplyCap, term->ReplyLength + length))
        return;
    memcpy(term->Reply + term->ReplyLength, text, length);
    term->ReplyLength += length;
}

const uint8_t *CsTakeOut(CsTerminal *term, size_t *length)
{
    *length = term->OutLength;
    term->OutLength = 0;
    return term->Out;
}

const uint8_t *CsTakeReply(CsTerminal *term, size_t *length)
{
    *length = term->ReplyLength;
    term->ReplyLength = 0;
    return term->Reply;
}

/* ------------------------------------------------------------------ the host terminal's state */

CsBuffer *CsEffective(CsBuffer *buffer)
{
    return buffer->Alt ? buffer->Alt : buffer;
}

CsBuffer *CsDisplayed(const CsTerminal *term)
{
    return term->Shown ? CsEffective(term->Shown) : nullptr;
}

/* the cells of the viewport map one to one onto the terminal's screen. */
bool CsGeometryFits(const CsBuffer *b)
{
    const CsTerminal *t = b->Term;
    return b->Window.Left == 0 && b->Window.Right - b->Window.Left + 1 == t->Cols && b->Grid.Width == t->Cols &&
           b->Window.Bottom - b->Window.Top + 1 == t->Rows;
}

/* program writes can be forwarded as they are applied. */
bool CsForwarding(const CsBuffer *b)
{
    const CsTerminal *t = b->Term;
    return !t->Headless && t->ShadowValid && b == CsDisplayed(t) && CsGeometryFits(b);
}

void CsHostGoto(CsTerminal *t, int x, int y)
{
    if (t->HostKnown && !t->HostWrapPending && t->HostX == x && t->HostY == y)
        return;
    if (t->HostKnown) {
        if (y == t->HostY) {
            if (x == 0) {
                CsOutString(t, "\r");
            } else {
                CsOutString(t, "\x1b[");
                CsOutNumber(t, (unsigned)x + 1);
                CsOutString(t, "G");
            }
            t->HostX = x;
            t->HostWrapPending = false;
            return;
        }
        if (x == t->HostX && !t->HostWrapPending) {
            CsOutString(t, "\x1b[");
            CsOutNumber(t, (unsigned)(y > t->HostY ? y - t->HostY : t->HostY - y));
            CsOutString(t, y > t->HostY ? "B" : "A");
            t->HostY = y;
            return;
        }
    }
    CsOutString(t, "\x1b[");
    CsOutNumber(t, (unsigned)y + 1);
    CsOutString(t, ";");
    CsOutNumber(t, (unsigned)x + 1);
    CsOutString(t, "H");
    t->HostX = x;
    t->HostY = y;
    t->HostKnown = true;
    t->HostWrapPending = false;
}

static void out_rgb(CsTerminal *t, unsigned base, uint32_t rgb)
{
    CsOutNumber(t, base);
    CsOutString(t, ";2;");
    CsOutNumber(t, rgb & 0xFF);
    CsOutString(t, ";");
    CsOutNumber(t, (rgb >> 8) & 0xFF);
    CsOutString(t, ";");
    CsOutNumber(t, (rgb >> 16) & 0xFF);
}

static bool table_is_default(const CsBuffer *b)
{
    return memcmp(b->ColorTable, g_default_table, sizeof(g_default_table)) == 0;
}

/* a color as sgr parameters: fg is 30, bg is 40, and the bright forms 60 more. */
static void out_color(CsTerminal *t, const CsBuffer *b, unsigned base, uint8_t kind, uint32_t v)
{
    if (kind == CS_COLOR_DEFAULT)
        return;
    CsOutString(t, ";");
    if (kind == CS_COLOR_RGB) {
        out_rgb(t, base + 8, v);
    } else if (v < 16 && table_is_default(b)) {
        CsOutNumber(t, (v & 8 ? base + 60 : base) + (v & 7));
    } else if (v < 16) {
        out_rgb(t, base + 8, b->ColorTable[CsLegacyToAnsi(v)]);
    } else {
        CsOutNumber(t, base + 8);
        CsOutString(t, ";5;");
        CsOutNumber(t, v & 0xFF);
    }
}

/* sets an attribute's colors and styles with one sgr from the plain state. */
void CsHostSetAttr(CsTerminal *t, const CsBuffer *b, CsAttr a)
{
    if (t->HostAttrKnown && t->HostAttr.Attr == a.Attr && t->HostAttr.Ext == a.Ext)
        return;
    const CsExt *e = CsExtGet(t, a.Ext);
    CsOutString(t, "\x1b[0");
    out_color(t, b, 30, e->FgKind, e->Fg);
    out_color(t, b, 40, e->BgKind, e->Bg);
    unsigned style = e->Style | (a.Attr & CS_STYLE_LVB_MASK);
    if (style & CS_STYLE_BOLD)
        CsOutString(t, ";1");
    if (style & CS_STYLE_FAINT)
        CsOutString(t, ";2");
    if (style & CS_STYLE_ITALIC)
        CsOutString(t, ";3");
    unsigned underline = (style & CS_STYLE_UNDERLINE_MASK) >> CS_STYLE_UNDERLINE_SHIFT;
    if (underline == 2)
        CsOutString(t, ";21");
    else if (underline == 3)
        CsOutString(t, ";4:3");
    else if (underline == 4)
        CsOutString(t, ";4:4");
    else if (underline == 5)
        CsOutString(t, ";4:5");
    else if (underline == 1 || (style & CS_UNDERSCORE))
        CsOutString(t, ";4");
    if (style & CS_STYLE_BLINK)
        CsOutString(t, ";5");
    if (style & CS_REVERSE_VIDEO)
        CsOutString(t, ";7");
    if (style & CS_STYLE_CONCEAL)
        CsOutString(t, ";8");
    if (style & CS_STYLE_STRIKE)
        CsOutString(t, ";9");
    if (style & CS_STYLE_OVERLINE)
        CsOutString(t, ";53");
    CsOutString(t, "m");
    t->HostAttr = a;
    t->HostAttrKnown = true;
}

void CsHostWrap(CsTerminal *t, bool wrap)
{
    if (t->HostWrapMode == wrap)
        return;
    CsOutString(t, wrap ? "\x1b[?7h" : "\x1b[?7l");
    t->HostWrapMode = wrap;
}

/* prints the character of a cell, or of a lead and trail pair, at the terminal's cursor. */
void CsHostPrintCell(CsTerminal *t, const CsBuffer *b, const CsCell *lead, const CsCell *trail)
{
    CsAttr attr = {lead->Attr, lead->Ext};
    /* the lead and trail bits only say how a wide character is split. */
    attr.Attr &= (uint16_t)~CS_DBCS_MASK;
    CsHostSetAttr(t, b, attr);
    uint32_t c = lead->Char;
    int width = 1;
    if (lead->Flags & CS_CELL_PAIR) {
        if (trail)
            c = 0x10000 + (((uint32_t)lead->Char - 0xD800) << 10) + ((uint32_t)trail->Char - 0xDC00);
        width = 2;
    } else if (lead->Flags & CS_CELL_LEAD) {
        width = 2;
    }
    if (c < 0x20 || c == 0x7F)
        c = ' ';
    CsOutUtf8(t, c);
    if (t->HostWrapPending) {
        /* printing wrapped, so the terminal cursor is now in the next row. */
        t->HostWrapPending = false;
        t->HostX = 0;
        t->HostY++;
    }
    t->HostX += width;
    if (t->HostX >= t->Cols) {
        t->HostX = t->Cols - 1;
        t->HostWrapPending = t->HostWrapMode;
    }
}

/* moves the terminal cursor to the buffer cursor. */
void CsHostCursor(CsTerminal *t, const CsBuffer *b)
{
    int row = b->CursorY - b->Window.Top;
    int col = b->CursorX - b->Window.Left;
    if (row < 0)
        row = 0;
    if (row >= t->Rows)
        row = t->Rows - 1;
    if (col < 0)
        col = 0;
    if (col >= t->Cols)
        col = t->Cols - 1;
    bool want_pending = b->WrapPending && CsViewRow(b, b->CursorY) >= 0;
    if (t->HostKnown && t->HostX == col && t->HostY == row && t->HostWrapPending == want_pending)
        return;
    if (!want_pending) {
        CsHostGoto(t, col, row);
        return;
    }
    /* only printing in the last column puts the terminal in pending wrap. */
    const CsCell *cell = &b->Grid.Rows[b->CursorY].Cells[col];
    int lead = col;
    if ((cell->Flags & CS_CELL_TRAIL) && lead > 0)
        lead--;
    CsHostWrap(t, true);
    CsHostGoto(t, lead, row);
    const CsCell *l = &b->Grid.Rows[b->CursorY].Cells[lead];
    const CsCell *tr = lead + 1 < b->Grid.Width ? &b->Grid.Rows[b->CursorY].Cells[lead + 1] : nullptr;
    CsHostPrintCell(t, b, l, (l->Flags & (CS_CELL_LEAD | CS_CELL_PAIR)) ? tr : nullptr);
}

/* ------------------------------------------------------------------ marking cells for the next render */

void CsMark(CsBuffer *b, int y, int x0, int x1, bool force)
{
    if (y < 0 || y >= b->Grid.Height)
        return;
    CsRow *row = &b->Grid.Rows[y];
    row->Touched = true;
    if (x0 < 0)
        x0 = 0;
    if (x1 >= b->Grid.Width)
        x1 = b->Grid.Width - 1;
    if (x0 > x1)
        return;
    if (row->DirtyLeft > row->DirtyRight) {
        row->DirtyLeft = (int16_t)x0;
        row->DirtyRight = (int16_t)x1;
    } else {
        row->DirtyLeft = (int16_t)CS_MIN(row->DirtyLeft, x0);
        row->DirtyRight = (int16_t)CS_MAX(row->DirtyRight, x1);
    }
    row->Force |= force;
    if (b->Dirty) {
        b->DirtyTop = CS_MIN(b->DirtyTop, y);
        b->DirtyBottom = CS_MAX(b->DirtyBottom, y);
    } else {
        b->Dirty = true;
        b->DirtyTop = b->DirtyBottom = y;
    }
}

void CsMarkRows(CsBuffer *b, int y0, int y1, bool force)
{
    for (int y = y0; y <= y1; y++)
        CsMark(b, y, 0, b->Grid.Width - 1, force);
}

void CsShadowFill(CsTerminal *t, int y0, int y1, int x0, int x1, CsCell cell)
{
    if (!t->ShadowValid)
        return;
    for (int y = CS_MAX(y0, 0); y <= y1 && y < t->Shadow.Height; y++) {
        for (int x = CS_MAX(x0, 0); x <= x1 && x < t->Shadow.Width; x++)
            t->Shadow.Rows[y].Cells[x] = cell;
        /* the terminal erased the whole row. */
        if (x0 <= 0 && x1 >= t->Shadow.Width - 1)
            t->Shadow.Rows[y].Known = true;
    }
}

/* ------------------------------------------------------------------ rendering */

static void sync_modes(CsTerminal *t)
{
    uint32_t want = t->Modes | t->ApiModes;
    uint32_t diff = want ^ t->HostModes;
    if (!diff)
        return;
    static const struct {
        uint32_t bit;
        const char *set, *reset;
    } modes[] = {
        {CS_MODE_WIN32_INPUT, "\x1b[?9001h", "\x1b[?9001l"},
        {CS_MODE_MOUSE_X10, "\x1b[?9h", "\x1b[?9l"},
        {CS_MODE_MOUSE_BUTTON, "\x1b[?1000h", "\x1b[?1000l"},
        {CS_MODE_MOUSE_DRAG, "\x1b[?1002h", "\x1b[?1002l"},
        {CS_MODE_MOUSE_ANY, "\x1b[?1003h", "\x1b[?1003l"},
        {CS_MODE_MOUSE_SGR, "\x1b[?1006h", "\x1b[?1006l"},
        {CS_MODE_MOUSE_URXVT, "\x1b[?1015h", "\x1b[?1015l"},
        {CS_MODE_MOUSE_UTF8, "\x1b[?1005h", "\x1b[?1005l"},
        {CS_MODE_MOUSE_ALTSCROLL, "\x1b[?1007h", "\x1b[?1007l"},
        {CS_MODE_BRACKETED_PASTE, "\x1b[?2004h", "\x1b[?2004l"},
        {CS_MODE_FOCUS, "\x1b[?1004h", "\x1b[?1004l"},
        {CS_MODE_APP_CURSOR, "\x1b[?1h", "\x1b[?1l"},
        {CS_MODE_APP_KEYPAD, "\x1b=", "\x1b>"},
    };
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (diff & modes[i].bit)
            CsOutString(t, (want & modes[i].bit) ? modes[i].set : modes[i].reset);
    }
    t->HostModes = want;
}

static bool cell_blank_with(const CsCell *c, const CsCell *like)
{
    return c->Char == ' ' && c->Attr == like->Attr && c->Ext == like->Ext && !c->Flags;
}

/*
 * paints the cells of viewport row y from x0 to x1 that differ from the shadow, or all of them when forced. short runs
 * of equal cells between changed ones are painted too, since moving the cursor over them costs as much.
 */
static void paint_row(CsTerminal *t, CsBuffer *b, int y, int x0, int x1, bool force)
{
    CsRow *model = &b->Grid.Rows[b->Window.Top + y];
    CsRow *shadow = &t->Shadow.Rows[y];
    /* a row the console never painted is treated as all different. */
    force = !shadow->Known;
    int base = b->Window.Left;
    int width = b->Window.Right - b->Window.Left + 1;
    int x = x0;
    x1 = CS_MIN(x1 - base, width - 1);
    x -= base;
    if (x < 0)
        x = 0;
    while (x <= x1) {
        const CsCell *m = &model->Cells[base + x];
        if (!force && CsCellEqual(m, &shadow->Cells[x])) {
            x++;
            continue;
        }
        int start = x;
        if ((m->Flags & CS_CELL_TRAIL) && start > 0)
            start--;
        /* when the rest of the row is all the same blank, one erase does it. */
        const CsCell *first = &model->Cells[base + start];
        if (width == t->Cols && first->Char == ' ' && !first->Flags) {
            bool blank = true;
            for (int k = start; k < width && blank; k++)
                blank = cell_blank_with(&model->Cells[base + k], first);
            if (blank) {
                CsHostGoto(t, start, y);
                CsHostSetAttr(t, b, (CsAttr){first->Attr, first->Ext});
                CsOutString(t, "\x1b[K");
                for (int k = start; k < width; k++)
                    shadow->Cells[k] = model->Cells[base + k];
                if (x0 <= 0 && width == t->Cols)
                    shadow->Known = true;
                return;
            }
        }
        CsHostGoto(t, start, y);
        int k = start;
        for (;;) {
            const CsCell *mc = &model->Cells[base + k];
            const CsCell *trail = (mc->Flags & (CS_CELL_LEAD | CS_CELL_PAIR)) && k + 1 < width ? &model->Cells[base + k + 1] : nullptr;
            if (mc->Flags & CS_CELL_TRAIL) {
                shadow->Cells[k] = *mc;
                k++;
            } else {
                CsHostPrintCell(t, b, mc, trail);
                shadow->Cells[k] = *mc;
                k++;
                if (trail) {
                    shadow->Cells[k] = *trail;
                    k++;
                }
            }
            if (k >= width)
                break;
            /* the next changed cell within three cells keeps the run going. */
            bool more = false;
            for (int look = k; look < width && look <= k + 3 && look <= x1; look++) {
                if (force || !CsCellEqual(&model->Cells[base + look], &shadow->Cells[look])) {
                    more = true;
                    break;
                }
            }
            if (!more)
                break;
        }
        x = k;
    }
    if (force && x0 <= 0 && x1 >= width - 1)
        shadow->Known = true;
}

/* forgets what the terminal shows: the shadow becomes a blank screen, or the main buffer's screen again. */
static void shadow_reset_known(CsTerminal *t, bool known)
{
    if (t->ShadowValid) {
        CsGridFree(&t->Shadow);
        t->ShadowValid = false;
    }
    CsCell blank = CsBlankOf((CsAttr){CS_DEFAULT_ATTRIBUTES, CS_EXT_DEFAULT});
    t->ShadowValid = CsGridInit(&t->Shadow, t->Cols, t->Rows, blank);
    if (t->ShadowValid && known)
        for (int y = 0; y < t->Shadow.Height; y++)
            t->Shadow.Rows[y].Known = true;
}

static void shadow_reset(CsTerminal *t)
{
    shadow_reset_known(t, false);
}

/* the terminal shows a blank screen, as an alternate screen starts. */
void CsShadowBlank(CsTerminal *t)
{
    shadow_reset_known(t, true);
}

void CsRender(CsTerminal *t)
{
    CsBuffer *b = CsDisplayed(t);
    if (!b)
        return;
    if (t->Windowed) {
        /* a window reads the cells itself, so only the marks are cleared. */
        for (CsBuffer *x = t->Buffers; x; x = x->Next) {
            if (!x->Dirty)
                continue;
            for (int y = x->DirtyTop; y <= x->DirtyBottom && y < x->Grid.Height; y++) {
                x->Grid.Rows[y].DirtyLeft = 1;
                x->Grid.Rows[y].DirtyRight = 0;
                x->Grid.Rows[y].Force = false;
            }
            x->Dirty = false;
        }
        t->Generation++;
        return;
    }
    if (t->Headless) {
        for (CsBuffer *x = t->Buffers; x; x = x->Next) {
            for (int y = 0; y < x->Grid.Height; y++) {
                x->Grid.Rows[y].DirtyLeft = 1;
                x->Grid.Rows[y].DirtyRight = 0;
                x->Grid.Rows[y].Force = false;
            }
            x->Dirty = false;
        }
        return;
    }
    if (!t->ShadowValid)
        shadow_reset(t);
    if (!t->ShadowValid)
        return;
    CsAdjustWindow(b);
    if (b->Dirty && b->Window.Bottom - b->Window.Top + 1 <= t->Rows && b->Window.Right - b->Window.Left + 1 <= t->Cols) {
        bool hidden = false;
        for (int y = CS_MAX(b->DirtyTop, b->Window.Top); y <= b->DirtyBottom && y <= b->Window.Bottom; y++) {
            CsRow *row = &b->Grid.Rows[y];
            if (row->DirtyLeft > row->DirtyRight)
                continue;
            int vy = y - b->Window.Top;
            if (!hidden && t->HostCursorVisible) {
                CsOutString(t, "\x1b[?25l");
                t->HostCursorVisible = false;
                hidden = true;
            }
            paint_row(t, b, vy, row->DirtyLeft, row->DirtyRight, row->Force);
        }
        for (int y = b->DirtyTop; y <= b->DirtyBottom; y++) {
            b->Grid.Rows[y].DirtyLeft = 1;
            b->Grid.Rows[y].DirtyRight = 0;
            b->Grid.Rows[y].Force = false;
        }
        b->Dirty = false;
    } else if (b->Dirty) {
        for (int y = b->DirtyTop; y <= b->DirtyBottom; y++) {
            b->Grid.Rows[y].DirtyLeft = 1;
            b->Grid.Rows[y].DirtyRight = 0;
            b->Grid.Rows[y].Force = false;
        }
        b->Dirty = false;
    }
    /* the terminal's autowrap follows the buffer's wrap mode. */
    CsHostWrap(t, (b->Mode & CS_WRAP_AT_EOL_OUTPUT) != 0);
    CsHostCursor(t, b);
    sync_modes(t);
    bool visible = b->CursorVisible && CsViewRow(b, b->CursorY) >= 0;
    if (visible != t->HostCursorVisible) {
        CsOutString(t, visible ? "\x1b[?25h" : "\x1b[?25l");
        t->HostCursorVisible = visible;
    }
}

void CsRepaint(CsTerminal *t)
{
    CsBuffer *b = CsDisplayed(t);
    if (!b || t->Headless)
        return;
    shadow_reset(t);
    t->HostAttrKnown = false;
    t->HostKnown = false;
    t->HostWrapPending = false;
    for (int y = b->Window.Top; y <= b->Window.Bottom && y < b->Grid.Height; y++)
        if (b->Grid.Rows[y].Touched || t->HostAlt)
            CsMark(b, y, b->Window.Left, b->Window.Right, true);
    CsRender(t);
}

void CsSetTitle(CsTerminal *t, const uint16_t *title, size_t count)
{
    unsigned n = (unsigned)CS_MIN(count, sizeof(t->Title) / sizeof(t->Title[0]) - 1);
    memcpy(t->Title, title, n * sizeof(uint16_t));
    t->TitleLength = n;
    t->Changed &= ~(unsigned)CS_CHANGED_TITLE;
    if (t->Headless)
        return;
    CsOutString(t, "\x1b]0;");
    for (unsigned i = 0; i < n; i++) {
        uint32_t c = title[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && title[i + 1] >= 0xDC00 && title[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (title[i + 1] - 0xDC00u);
            i++;
        }
        /* control characters would end the string early. */
        if (c < 0x20 || c == 0x7F)
            c = ' ';
        CsOutUtf8(t, c);
    }
    CsOutString(t, "\x07");
}

void CsSetHostCursor(CsTerminal *t, int x, int y)
{
    CsBuffer *b = CsDisplayed(t);
    if (!b)
        return;
    x = CS_MAX(0, CS_MIN(x, t->Cols - 1));
    y = CS_MAX(0, CS_MIN(y, t->Rows - 1));
    b->CursorX = b->Window.Left + x;
    b->CursorY = b->Window.Top + y;
    t->HostX = x;
    t->HostY = y;
    t->HostKnown = true;
    t->HostWrapPending = false;
}

void CsRestoreHost(CsTerminal *t)
{
    if (t->Headless)
        return;
    t->Modes = 0;
    t->ApiModes = 0;
    sync_modes(t);
    CsOutString(t, "\x1b[0m");
    if (!t->HostCursorVisible)
        CsOutString(t, "\x1b[?25h");
    if (!t->HostWrapMode)
        CsOutString(t, "\x1b[?7h");
    if (t->HostAlt)
        CsOutString(t, "\x1b[?1049l");
    t->HostAlt = false;
    t->HostCursorVisible = true;
    t->HostWrapMode = true;
    t->HostAttrKnown = false;
}

/* ------------------------------------------------------------------ the terminal and its buffers */

bool CsTerminalInit(CsTerminal *t, int cols, int rows)
{
    memset(t, 0, sizeof(*t));
    t->Cols = cols > 0 ? cols : 80;
    t->Rows = rows > 0 ? rows : 25;
    t->Cols = CS_MIN(t->Cols, CS_MAX_DIMENSION);
    t->Rows = CS_MIN(t->Rows, CS_MAX_DIMENSION);
    CsExt none = {};
    t->Ext = nullptr;
    CsExtIntern(t, &none);
    if (t->ExtCount != 1)
        return false;
    t->HostWrapMode = true;
    t->HostCursorVisible = true;
    t->TerminalSized = true;
    memcpy(t->Palette, g_default_table, sizeof(g_default_table));
    shadow_reset(t);
    return t->ShadowValid;
}

void CsTerminalDestroy(CsTerminal *t)
{
    while (t->Buffers) {
        CsBuffer *b = t->Buffers;
        b->Alt = nullptr;
        CsBufferDestroy(b);
    }
    if (t->ShadowValid)
        CsGridFree(&t->Shadow);
    CsFree(t->Ext);
    CsFree(t->ExtHash);
    CsFree(t->Out);
    CsFree(t->Reply);
    memset(t, 0, sizeof(*t));
}

void CsDefaultTabs(CsBuffer *b)
{
    for (int x = 0; x < b->Grid.Width; x++)
        b->TabStops[x] = (x % 8) == 0 && x != 0;
}

void CsClearTabs(CsBuffer *b)
{
    memset(b->TabStops, 0, (size_t)b->Grid.Width);
}

CsBuffer *CsBufferCreate(CsTerminal *t, int width, int height)
{
    CsBuffer *b = CsAllocate(sizeof(*b));
    if (!b)
        return nullptr;
    memset(b, 0, sizeof(*b));
    b->Term = t;
    b->DefaultAttr = (CsAttr){CS_DEFAULT_ATTRIBUTES, CS_EXT_DEFAULT};
    b->Attr = b->DefaultAttr;
    if (!CsGridInit(&b->Grid, width, height, CsBlankOf(b->DefaultAttr))) {
        CsFree(b);
        return nullptr;
    }
    b->TabStops = CsAllocate((size_t)width);
    if (!b->TabStops) {
        CsGridFree(&b->Grid);
        CsFree(b);
        return nullptr;
    }
    CsDefaultTabs(b);
    b->Window.Left = 0;
    b->Window.Top = 0;
    b->Window.Right = (int16_t)(CS_MIN(width, t->Cols) - 1);
    b->Window.Bottom = (int16_t)(CS_MIN(height, t->Rows) - 1);
    b->CursorSize = 25;
    b->CursorVisible = true;
    b->PopupAttr = 0xF5;
    b->Mode = CS_PROCESSED_OUTPUT | CS_WRAP_AT_EOL_OUTPUT | CS_VIRTUAL_TERMINAL_PROCESSING;
    memcpy(b->ColorTable, g_default_table, sizeof(g_default_table));
    b->Top = 0;
    b->Bottom = b->Window.Bottom - b->Window.Top;
    b->MaxWindow.X = (int16_t)width;
    b->MaxWindow.Y = (int16_t)height;
    b->Next = t->Buffers;
    t->Buffers = b;
    return b;
}

void CsBufferDestroy(CsBuffer *b)
{
    CsTerminal *t = b->Term;
    if (b->Alt) {
        CsBuffer *alt = b->Alt;
        b->Alt = nullptr;
        CsBufferDestroy(alt);
    }
    if (b->Main)
        b->Main->Alt = nullptr;
    for (CsBuffer **p = &t->Buffers; *p; p = &(*p)->Next) {
        if (*p == b) {
            *p = b->Next;
            break;
        }
    }
    if (t->Shown == b)
        t->Shown = nullptr;
    if (t->Main == b)
        t->Main = nullptr;
    CsGridFree(&b->Grid);
    CsFree(b->TabStops);
    CsFree(b->Osc);
    CsFree(b);
}

/* shows a buffer and brings the host terminal to it. */
void CsShow(CsTerminal *t, CsBuffer *buffer)
{
    CsBuffer *old = CsDisplayed(t);
    if (!t->Main)
        t->Main = buffer;
    t->Shown = buffer;
    CsBuffer *now = CsDisplayed(t);
    if (t->Windowed && now) {
        t->Cols = now->Window.Right - now->Window.Left + 1;
        t->Rows = now->Window.Bottom - now->Window.Top + 1;
        t->Generation++;
        t->Changed |= CS_CHANGED_WINDOW;
    }
    if (!old || old == now || t->Headless)
        return;
    CsRepaint(t);
}

/* ------------------------------------------------------------------ resizing */

static bool grid_resize(CsBuffer *b, int width, int height)
{
    CsGrid fresh;
    CsCell blank = CsBlankOf(b->DefaultAttr);
    if (!CsGridInit(&fresh, width, height, blank))
        return false;
    int copy_w = CS_MIN(width, b->Grid.Width);
    int skip = 0;
    /* the rows that do not fit are dropped from above the cursor. */
    if (height < b->Grid.Height && b->CursorY >= height)
        skip = b->CursorY - height + 1;
    int copy_h = CS_MIN(height, b->Grid.Height - skip);
    for (int y = 0; y < copy_h; y++) {
        CsRow *from = &b->Grid.Rows[y + skip];
        memcpy(fresh.Rows[y].Cells, from->Cells, sizeof(CsCell) * (size_t)copy_w);
        /* a wide character cut in half becomes a blank. */
        if (copy_w < b->Grid.Width && copy_w > 0 && (fresh.Rows[y].Cells[copy_w - 1].Flags & (CS_CELL_LEAD | CS_CELL_PAIR)))
            fresh.Rows[y].Cells[copy_w - 1] = blank;
        fresh.Rows[y].Wrapped = from->Wrapped;
        fresh.Rows[y].Touched = from->Touched;
    }
    uint8_t *tabs = CsAllocate((size_t)width);
    if (!tabs) {
        CsGridFree(&fresh);
        return false;
    }
    for (int x = 0; x < width; x++)
        tabs[x] = x < b->Grid.Width ? b->TabStops[x] : ((x % 8) == 0 && x != 0);
    CsGridFree(&b->Grid);
    CsFree(b->TabStops);
    b->Grid = fresh;
    b->TabStops = tabs;
    b->CursorY -= skip;
    if (b->CursorY < 0)
        b->CursorY = 0;
    b->Window.Top = (int16_t)CS_MAX(0, b->Window.Top - skip);
    return true;
}

void CsResizeWindowFits(CsBuffer *b)
{
    CsTerminal *t = b->Term;
    int w = CS_MIN(b->Grid.Width, t->Cols), h = CS_MIN(b->Grid.Height, t->Rows);
    b->Window.Left = 0;
    b->Window.Right = (int16_t)(w - 1);
    if (b->Window.Top + h > b->Grid.Height)
        b->Window.Top = (int16_t)(b->Grid.Height - h);
    b->Window.Bottom = (int16_t)(b->Window.Top + h - 1);
    b->MaxWindow.X = (int16_t)CS_MIN(t->Cols, b->Grid.Width);
    b->MaxWindow.Y = (int16_t)CS_MIN(t->Rows, b->Grid.Height);
    b->Top = 0;
    b->Bottom = b->Window.Bottom - b->Window.Top;
}

void CsTerminalResize(CsTerminal *t, int cols, int rows)
{
    if (cols < 1 || rows < 1 || cols > CS_MAX_DIMENSION || rows > CS_MAX_DIMENSION)
        return;
    int old_cols = t->Cols, old_rows = t->Rows;
    if (cols == old_cols && rows == old_rows)
        return;
    t->Cols = cols;
    t->Rows = rows;
    for (CsBuffer *b = t->Buffers; b; b = b->Next) {
        bool follows = b->Grid.Width == old_cols && b->Grid.Height == old_rows;
        if (follows && !grid_resize(b, cols, rows))
            follows = false;
        if (b->CursorX >= b->Grid.Width)
            b->CursorX = b->Grid.Width - 1;
        if (b->CursorY >= b->Grid.Height)
            b->CursorY = b->Grid.Height - 1;
        b->WrapPending = false;
        if (follows)
            b->Window.Top = 0;
        CsResizeWindowFits(b);
        b->Dirty = false;
    }
    if (t->Headless)
        return;
    shadow_reset(t);
    t->HostKnown = false;
    t->HostAttrKnown = false;
    CsBuffer *shown = CsDisplayed(t);
    if (shown) {
        /* the terminal reflows and clips on its own, so repaint what the console had written. */
        for (int y = shown->Window.Top; y <= shown->Window.Bottom; y++)
            if (shown->Grid.Rows[y].Touched)
                CsMark(shown, y, 0, shown->Grid.Width - 1, true);
        CsRender(t);
    }
}

/* the viewport follows the cursor when the cursor is outside it. */
void CsAdjustWindow(CsBuffer *b)
{
    (void)b;
}

uint32_t CsInputModes(const CsTerminal *t)
{
    return t->Modes;
}

void CsBufferSetModes(CsTerminal *t, uint32_t modes)
{
    t->Modes = modes;
}
