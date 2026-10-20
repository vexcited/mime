/*
 * what calls on a screen buffer do to its cells: the information of the
 * buffer, the cursor and the window, and reading, writing, filling and
 * scrolling rectangles of cells. see conscreen.h.
 */
#include "conscreen_p.h"


static bool inside(const CsBuffer *b, CsCoord c)
{
    return c.X >= 0 && c.Y >= 0 && c.X < b->Grid.Width && c.Y < b->Grid.Height;
}

void CsGetInfo(CsBuffer *b, CsInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->Size.X = (int16_t)b->Grid.Width;
    info->Size.Y = (int16_t)b->Grid.Height;
    info->Cursor.X = (int16_t)b->CursorX;
    info->Cursor.Y = (int16_t)b->CursorY;
    info->Attributes = b->Attr.Attr & (uint16_t)~CS_DBCS_MASK;
    info->Window = b->Window;
    /* nothing limits the window of a console that has none but the buffer. */
    info->MaxWindow.X = (int16_t)b->Grid.Width;
    info->MaxWindow.Y = (int16_t)b->Grid.Height;
    info->PopupAttributes = b->PopupAttr;
    memcpy(info->ColorTable, b->ColorTable, sizeof(info->ColorTable));
    info->CursorSize = b->CursorSize;
    info->CursorVisible = b->CursorVisible;
}

/* the viewport changed: the terminal is shown the rows again. */
static void window_changed(CsBuffer *b)
{
    CsTerminal *t = b->Term;
    b->Top = 0;
    b->Bottom = b->Window.Bottom - b->Window.Top;
    if (b == CsDisplayed(t) && !t->Headless) {
        for (int y = b->Window.Top; y <= b->Window.Bottom; y++)
            CsMark(b, y, b->Window.Left, b->Window.Right, false);
    }
}

/* the viewport moves by the least that puts the cursor in it. */
static void follow_cursor(CsBuffer *b)
{
    int h = b->Window.Bottom - b->Window.Top + 1;
    if (b->CursorY < b->Window.Top) {
        b->Window.Top = (int16_t)b->CursorY;
        b->Window.Bottom = (int16_t)(b->Window.Top + h - 1);
    } else if (b->CursorY > b->Window.Bottom) {
        b->Window.Bottom = (int16_t)b->CursorY;
        b->Window.Top = (int16_t)(b->Window.Bottom - h + 1);
    }
    int w = b->Window.Right - b->Window.Left + 1;
    if (b->CursorX < b->Window.Left) {
        b->Window.Left = (int16_t)b->CursorX;
        b->Window.Right = (int16_t)(b->Window.Left + w - 1);
    } else if (b->CursorX > b->Window.Right) {
        b->Window.Right = (int16_t)b->CursorX;
        b->Window.Left = (int16_t)(b->Window.Right - w + 1);
    }
}

CsResult CsSetCursorPosition(CsBuffer *b, CsCoord position)
{
    b = CsEffective(b);
    if (!inside(b, position))
        return CS_INVALID_PARAMETER;
    b->CursorX = position.X;
    b->CursorY = position.Y;
    b->WrapPending = false;
    CsRect old = b->Window;
    follow_cursor(b);
    if (memcmp(&old, &b->Window, sizeof(old)))
        window_changed(b);
    CsRender(b->Term);
    return CS_OK;
}

CsResult CsSetCursorInfo(CsBuffer *b, int size, bool visible)
{
    b = CsEffective(b);
    if (size < 1 || size > 100)
        return CS_INVALID_PARAMETER;
    b->CursorSize = size;
    b->CursorVisible = visible;
    if (b->Main) {
        b->Main->CursorSize = size;
        b->Main->CursorVisible = visible;
    }
    CsRender(b->Term);
    return CS_OK;
}

void CsSetAttributes(CsBuffer *b, uint16_t attributes)
{
    /* the attributes of the buffer of the handle, not the alternate buffer's. */
    b->Attr = CsAttrFromLegacy(b->Term, attributes & (uint16_t)~CS_DBCS_MASK);
}

/* ------------------------------------------------------------------ size and window */

/*
 * a new grid of another size: the rows are kept from the top, unless the
 * cursor is below the new bottom, and then the rows that end with its row.
 */
static CsResult resize_grid(CsBuffer *b, int width, int height)
{
    CsGrid fresh;
    CsCell blank = CsBlank(b);
    if ((uint64_t)width * (uint64_t)height > CS_MAX_CELLS)
        return CS_NO_MEMORY;
    if (!CsGridInit(&fresh, width, height, blank))
        return CS_NO_MEMORY;
    uint8_t *tabs = CsAllocate((size_t)width);
    if (!tabs) {
        CsGridFree(&fresh);
        return CS_NO_MEMORY;
    }
    int skip = 0;
    if (height < b->Grid.Height && b->CursorY >= height)
        skip = b->CursorY - height + 1;
    int cw = CS_MIN(width, b->Grid.Width), ch = CS_MIN(height, b->Grid.Height - skip);
    for (int y = 0; y < ch; y++) {
        CsRow *from = &b->Grid.Rows[y + skip];
        memcpy(fresh.Rows[y].Cells, from->Cells, sizeof(CsCell) * (size_t)cw);
        /* a wide character cut in two is not one. */
        if (cw < b->Grid.Width && cw > 0 && (fresh.Rows[y].Cells[cw - 1].Flags & (CS_CELL_LEAD | CS_CELL_PAIR)))
            fresh.Rows[y].Cells[cw - 1] = blank;
        fresh.Rows[y].Wrapped = from->Wrapped;
        fresh.Rows[y].Touched = from->Touched;
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
    b->Dirty = false;
    return CS_OK;
}

/* a terminal that can be asked is asked to be as big as the window the program made. */
static void follow_window(CsBuffer *b)
{
    CsTerminal *t = b->Term;
    if (!t->ResizableHost || t->Headless || b != CsDisplayed(t))
        return;
    int w = b->Window.Right - b->Window.Left + 1, h = b->Window.Bottom - b->Window.Top + 1;
    if (w == t->Cols && h == t->Rows)
        return;
    CsOutString(t, "\x1b[8;");
    CsOutNumber(t, (unsigned)h);
    CsOutString(t, ";");
    CsOutNumber(t, (unsigned)w);
    CsOutString(t, "t");
    t->Cols = w;
    t->Rows = h;
    CsGridFree(&t->Shadow);
    t->ShadowValid = false;
    t->HostKnown = false;
    t->HostAttrKnown = false;
    CsMarkRows(b, b->Window.Top, b->Window.Bottom, true);
}

/* the viewport stays inside the buffer, moved back by as much as it stuck out; else the cursor is kept inside. */
static void after_resize(CsBuffer *b)
{
    int over_x = b->Window.Right + 1 - b->Grid.Width, over_y = b->Window.Bottom + 1 - b->Grid.Height;
    if (over_x > 0) {
        b->Window.Left = (int16_t)CS_MAX(0, b->Window.Left - over_x);
        b->Window.Right = (int16_t)(b->Grid.Width - 1);
    }
    if (over_y > 0) {
        b->Window.Top = (int16_t)CS_MAX(0, b->Window.Top - over_y);
        b->Window.Bottom = (int16_t)(b->Grid.Height - 1);
    }
    if (b->Window.Right < b->Window.Left)
        b->Window.Right = b->Window.Left;
    if (b->Window.Bottom < b->Window.Top)
        b->Window.Bottom = b->Window.Top;
    b->CursorX = CS_MIN(b->CursorX, b->Grid.Width - 1);
    b->CursorY = CS_MIN(b->CursorY, b->Grid.Height - 1);
    b->WrapPending = false;
}

CsResult CsResizeBuffer(CsBuffer *b, int width, int height)
{
    if (width == b->Grid.Width && height == b->Grid.Height)
        return CS_OK;
    CsResult r = resize_grid(b, width, height);
    if (r != CS_OK)
        return r;
    after_resize(b);
    window_changed(b);
    return CS_OK;
}

CsResult CsSetSize(CsBuffer *b, CsCoord size)
{
    b = CsEffective(b);
    int ww = b->Window.Right - b->Window.Left + 1, wh = b->Window.Bottom - b->Window.Top + 1;
    if (size.X < ww || size.Y < wh || size.X < 1 || size.Y < 1 || size.X == 0x7FFF || size.Y == 0x7FFF)
        return CS_INVALID_PARAMETER;
    CsResult r = CsResizeBuffer(b, size.X, size.Y);
    if (r != CS_OK)
        return r;
    CsMarkRows(b, b->Window.Top, b->Window.Bottom, false);
    CsRender(b->Term);
    return CS_OK;
}

CsResult CsSetWindow(CsBuffer *b, bool absolute, CsRect w)
{
    b = CsEffective(b);
    CsRect r = w;
    if (!absolute) {
        r.Left = (int16_t)(b->Window.Left + w.Left);
        r.Right = (int16_t)(b->Window.Right + w.Right);
        r.Top = (int16_t)(b->Window.Top + w.Top);
        r.Bottom = (int16_t)(b->Window.Bottom + w.Bottom);
    }
    if (r.Right < r.Left || r.Bottom < r.Top)
        return CS_INVALID_PARAMETER;
    int width = r.Right - r.Left + 1, height = r.Bottom - r.Top + 1;
    /* a console that is a terminal's has one size for its buffer and its window. */
    if (b->Term->TerminalSized && (width != b->Grid.Width || height != b->Grid.Height)) {
        CsResult res = CsResizeBuffer(b, width, height);
        if (res != CS_OK)
            return res;
        r.Left = r.Top = 0;
        r.Right = (int16_t)(width - 1);
        r.Bottom = (int16_t)(height - 1);
    }
    /* the window is clamped to the buffer and then moved to lie inside it. */
    width = CS_MIN(CS_MAX(width, 1), b->Grid.Width);
    height = CS_MIN(CS_MAX(height, 1), b->Grid.Height);
    int left = CS_MAX(0, CS_MIN(r.Left, b->Grid.Width - width)), top = CS_MAX(0, CS_MIN(r.Top, b->Grid.Height - height));
    b->Window.Left = (int16_t)left;
    b->Window.Top = (int16_t)top;
    b->Window.Right = (int16_t)(left + width - 1);
    b->Window.Bottom = (int16_t)(top + height - 1);
    window_changed(b);
    follow_window(b);
    CsRender(b->Term);
    return CS_OK;
}

CsResult CsSetInfo(CsBuffer *b, const CsInfo *info)
{
    b = CsEffective(b);
    if (info->Size.X <= 0 || info->Size.Y <= 0 || info->Size.X == 0x7FFF || info->Size.Y == 0x7FFF)
        return CS_INVALID_PARAMETER;
    /* a resize that fails is not told: the rest is set. */
    if (info->Size.X != b->Grid.Width || info->Size.Y != b->Grid.Height)
        (void)CsResizeBuffer(b, info->Size.X, info->Size.Y);
    memcpy(b->ColorTable, info->ColorTable, sizeof(b->ColorTable));
    CsAttr attr = CsAttrFromLegacy(b->Term, info->Attributes & (uint16_t)~CS_DBCS_MASK);
    b->DefaultAttr = attr;
    b->Attr = attr;
    if (b->Main) {
        b->Main->DefaultAttr = attr;
        b->Main->Attr = attr;
    }
    b->PopupAttr = info->PopupAttributes;
    int width = CS_MIN(info->Window.Right - info->Window.Left + 1, b->Grid.Width);
    int height = CS_MIN(info->Window.Bottom - info->Window.Top + 1, b->Grid.Height);
    if (width > 0 && height > 0) {
        if (b->Term->TerminalSized && (width != b->Grid.Width || height != b->Grid.Height))
            (void)CsResizeBuffer(b, width, height);
        int ww = b->Window.Right - b->Window.Left + 1, wh = b->Window.Bottom - b->Window.Top + 1;
        if (width != ww || height != wh) {
            b->Window.Right = (int16_t)(b->Window.Left + width - 1);
            b->Window.Bottom = (int16_t)(b->Window.Top + height - 1);
        }
    }
    after_resize(b);
    window_changed(b);
    b->Dirty = false;
    CsMarkRows(b, b->Window.Top, b->Window.Bottom, true);
    CsRender(b->Term);
    return CS_OK;
}

/* ------------------------------------------------------------------ rectangles */

/* the half of a wide character as an attribute word and a character. */
static CsCharInfo cell_info(const CsRow *row, int x, int width)
{
    const CsCell *c = &row->Cells[x];
    CsCharInfo i;
    i.Char = c->Char;
    i.Attributes = (uint16_t)(c->Attr & ~CS_DBCS_MASK);
    if (c->Flags & CS_CELL_PAIR) {
        /* a pair of units cannot be told in one: it is the replacement character. */
        i.Char = 0xFFFD;
    }
    if (c->Flags & CS_CELL_LEAD)
        i.Attributes |= CS_LEADING_BYTE;
    if (c->Flags & CS_CELL_TRAIL) {
        i.Attributes |= CS_TRAILING_BYTE;
        /* both halves carry the character. */
        if (!(c->Flags & CS_CELL_PAIR) && x > 0)
            i.Char = row->Cells[x - 1].Char;
    }
    (void)width;
    return i;
}

/*
 * the rectangle is clipped to the buffer; data has the width of the
 * rectangle that was asked for and the cells that were clipped from the left
 * and the top are left alone in it. the clipped rectangle is stored back.
 */
CsResult CsReadOutput(CsBuffer *b, CsCharInfo *data, CsRect *rect)
{
    b = CsEffective(b);
    int w = rect->Right - rect->Left + 1, h = rect->Bottom - rect->Top + 1;
    if (w <= 0 || h <= 0) {
        rect->Right = (int16_t)(rect->Left - 1);
        rect->Bottom = (int16_t)(rect->Top - 1);
        return CS_OK;
    }
    int left = rect->Left, top = rect->Top;
    int right = CS_MIN(rect->Right, b->Grid.Width - 1), bottom = CS_MIN(rect->Bottom, b->Grid.Height - 1);
    int clip_left = CS_MAX(left, 0), clip_top = CS_MAX(top, 0);
    for (int y = clip_top; y <= bottom; y++)
        for (int x = clip_left; x <= right; x++)
            data[(size_t)(y - top) * (size_t)w + (size_t)(x - left)] = cell_info(&b->Grid.Rows[y], x, b->Grid.Width);
    rect->Left = (int16_t)clip_left;
    rect->Top = (int16_t)clip_top;
    rect->Right = (int16_t)right;
    rect->Bottom = (int16_t)bottom;
    return CS_OK;
}

CsResult CsWriteOutput(CsBuffer *b, const CsCharInfo *data, CsRect *rect)
{
    b = CsEffective(b);
    int w = rect->Right - rect->Left + 1, h = rect->Bottom - rect->Top + 1;
    if (w <= 0 || h <= 0) {
        rect->Right = (int16_t)(rect->Left - 1);
        rect->Bottom = (int16_t)(rect->Top - 1);
        return CS_OK;
    }
    int left = rect->Left, top = rect->Top;
    if (left >= b->Grid.Width || top >= b->Grid.Height)
        return CS_OK;
    int right = CS_MIN(rect->Right, b->Grid.Width - 1), bottom = CS_MIN(rect->Bottom, b->Grid.Height - 1);
    int clip_left = CS_MAX(left, 0), clip_top = CS_MAX(top, 0);
    if (right < clip_left || bottom < clip_top)
        return CS_INVALID_PARAMETER;
    for (int y = clip_top; y <= bottom; y++) {
        CsRow *row = &b->Grid.Rows[y];
        const CsCharInfo *in = &data[(size_t)(y - top) * (size_t)w + (size_t)(clip_left - left)];
        for (int x = clip_left; x <= right; x++, in++) {
            CsCell c = {in->Char, (uint16_t)(in->Attributes & ~CS_DBCS_MASK), 0, 0};
            CsAttr a = CsAttrFromLegacy(b->Term, c.Attr);
            c.Attr = a.Attr;
            c.Ext = a.Ext;
            if (in->Attributes & CS_LEADING_BYTE) {
                /* a lead half pairs with the trail half in the next cell of the rectangle. */
                if (x < right && (in[1].Attributes & CS_TRAILING_BYTE)) {
                    c.Flags = CS_CELL_LEAD;
                    CsCell t2 = c;
                    t2.Flags = CS_CELL_TRAIL;
                    t2.Char = 0;
                    clear_cell_partner(b, y, x);
                    clear_cell_partner(b, y, x + 1);
                    row->Cells[x] = c;
                    row->Cells[x + 1] = t2;
                    x++;
                    in++;
                    continue;
                }
                c.Char = ' ';
            } else if (in->Attributes & CS_TRAILING_BYTE) {
                c.Char = ' ';
            }
            clear_cell_partner(b, y, x);
            row->Cells[x] = c;
        }
        CsMark(b, y, clip_left - (clip_left > 0), right + (right + 1 < b->Grid.Width), true);
    }
    rect->Left = (int16_t)clip_left;
    rect->Top = (int16_t)clip_top;
    rect->Right = (int16_t)right;
    rect->Bottom = (int16_t)bottom;
    CsRender(b->Term);
    return CS_OK;
}

/* the cells from a coordinate on, row after row. */
static size_t span(const CsBuffer *b, CsCoord start, size_t count)
{
    if (!inside(b, start))
        return 0;
    size_t total = (size_t)b->Grid.Width * (size_t)b->Grid.Height;
    size_t first = (size_t)start.Y * (size_t)b->Grid.Width + (size_t)start.X;
    return CS_MIN(count, total - first);
}

#define CELL_AT(b, index) (&(b)->Grid.Rows[(index) / (size_t)(b)->Grid.Width].Cells[(index) % (size_t)(b)->Grid.Width])

/* the characters of cells: the right half of a wide character gives none, and a half cut by the ends is a blank. */
size_t CsReadChars(CsBuffer *b, CsCoord start, uint16_t *out, size_t count)
{
    b = CsEffective(b);
    size_t n = span(b, start, count);
    size_t first = (size_t)start.Y * (size_t)b->Grid.Width + (size_t)start.X;
    size_t made = 0;
    for (size_t i = 0; i < n; i++) {
        const CsCell *c = CELL_AT(b, first + i);
        if (c->Flags & CS_CELL_TRAIL) {
            if (i == 0)
                out[made++] = ' ';
            continue;
        }
        if ((c->Flags & CS_CELL_LEAD) && i + 1 == n) {
            out[made++] = ' ';
            continue;
        }
        out[made++] = (c->Flags & CS_CELL_PAIR) ? 0xFFFD : c->Char;
    }
    return made;
}

size_t CsReadAttributes(CsBuffer *b, CsCoord start, uint16_t *out, size_t count)
{
    b = CsEffective(b);
    size_t n = span(b, start, count);
    size_t first = (size_t)start.Y * (size_t)b->Grid.Width + (size_t)start.X;
    for (size_t i = 0; i < n; i++) {
        const CsCell *c = CELL_AT(b, first + i);
        uint16_t w = (uint16_t)(c->Attr & ~CS_DBCS_MASK);
        if ((c->Flags & CS_CELL_LEAD) && i + 1 < n)
            w |= CS_LEADING_BYTE;
        if ((c->Flags & CS_CELL_TRAIL) && i > 0)
            w |= CS_TRAILING_BYTE;
        out[i] = w;
    }
    return n;
}

static void mark_span(CsBuffer *b, size_t first, size_t n)
{
    size_t w = (size_t)b->Grid.Width;
    size_t last = first + n - 1;
    for (size_t y = first / w; y <= last / w; y++) {
        size_t x0 = y == first / w ? first % w : 0, x1 = y == last / w ? last % w : w - 1;
        CsMark(b, (int)y, (int)x0 - (x0 > 0), (int)x1 + (x1 + 1 < w), true);
    }
}

/* characters written over the cells, the attributes kept; a wide one takes two cells. returns the units taken. */
CsResult CsWriteChars(CsBuffer *b, CsCoord start, const uint16_t *text, size_t count, size_t *written)
{
    b = CsEffective(b);
    *written = 0;
    if (!count)
        return CS_OK;
    if (!inside(b, start))
        return CS_INVALID_PARAMETER;
    size_t total = (size_t)b->Grid.Width * (size_t)b->Grid.Height;
    size_t pos = (size_t)start.Y * (size_t)b->Grid.Width + (size_t)start.X;
    size_t first = pos, i = 0;
    while (i < count && pos < total) {
        uint32_t cp = text[i];
        size_t units = 1;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < count && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00u);
            units = 2;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        int width = CsCharWidth(cp);
        if (cp >= 0x10000 && width == 1)
            width = 2;
        if (width == 0)
            width = 1;
        if (width == 2 && (pos % (size_t)b->Grid.Width) == (size_t)b->Grid.Width - 1) {
            /* no room for the second half at the end of a row: a blank is left and the glyph goes to the next row. */
            CsCell *edge = CELL_AT(b, pos);
            edge->Char = ' ';
            edge->Flags = 0;
            pos++;
            if (pos >= total)
                break;
        }
        CsCell *c = CELL_AT(b, pos);
        int y = (int)(pos / (size_t)b->Grid.Width), x = (int)(pos % (size_t)b->Grid.Width);
        clear_cell_partner(b, y, x);
        if (width == 2 && pos + 1 < total)
            clear_cell_partner(b, y, x + 1);
        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000;
            c->Char = (uint16_t)(0xD800 + (v >> 10));
            c->Flags = CS_CELL_LEAD | CS_CELL_PAIR;
            CsCell *c2 = CELL_AT(b, pos + 1);
            c2->Char = (uint16_t)(0xDC00 + (v & 0x3FF));
            c2->Flags = CS_CELL_TRAIL | CS_CELL_PAIR;
        } else if (width == 2) {
            c->Char = (uint16_t)cp;
            c->Flags = CS_CELL_LEAD;
            CsCell *c2 = CELL_AT(b, pos + 1);
            c2->Char = 0;
            c2->Flags = CS_CELL_TRAIL;
        } else {
            c->Char = (uint16_t)cp;
            c->Flags = 0;
        }
        pos += (size_t)width;
        i += units;
    }
    *written = i;
    if (pos > first)
        mark_span(b, first, pos - first > total - first ? total - first : pos - first);
    CsRender(b->Term);
    return CS_OK;
}

CsResult CsWriteAttributes(CsBuffer *b, CsCoord start, const uint16_t *attributes, size_t count, size_t *written)
{
    b = CsEffective(b);
    *written = 0;
    if (!count)
        return CS_OK;
    if (!inside(b, start))
        return CS_INVALID_PARAMETER;
    size_t n = span(b, start, count);
    size_t first = (size_t)start.Y * (size_t)b->Grid.Width + (size_t)start.X;
    for (size_t i = 0; i < n; i++) {
        CsCell *c = CELL_AT(b, first + i);
        CsAttr a = CsAttrFromLegacy(b->Term, attributes[i] & (uint16_t)~CS_DBCS_MASK);
        c->Attr = a.Attr;
        c->Ext = a.Ext;
    }
    *written = n;
    if (n)
        mark_span(b, first, n);
    CsRender(b->Term);
    return CS_OK;
}

/* fills cells with a character, the attributes kept; a wide character takes two cells each. */
size_t CsFillChars(CsBuffer *b, CsCoord start, uint16_t ch, size_t count)
{
    b = CsEffective(b);
    size_t n = span(b, start, count);
    if (!n)
        return 0;
    size_t first = (size_t)start.Y * (size_t)b->Grid.Width + (size_t)start.X;
    int width = CsCharWidth(ch);
    for (size_t i = 0; i < n; i++) {
        CsCell *c = CELL_AT(b, first + i);
        int y = (int)((first + i) / (size_t)b->Grid.Width), x = (int)((first + i) % (size_t)b->Grid.Width);
        clear_cell_partner(b, y, x);
        if (width == 2 && i + 1 < n && x + 1 < b->Grid.Width) {
            c->Char = ch;
            c->Flags = CS_CELL_LEAD;
            CsCell *c2 = CELL_AT(b, first + i + 1);
            clear_cell_partner(b, y, x + 1);
            c2->Char = 0;
            c2->Flags = CS_CELL_TRAIL;
            i++;
        } else {
            c->Char = width == 2 ? ' ' : ch;
            c->Flags = 0;
        }
    }
    mark_span(b, first, n);
    CsRender(b->Term);
    return n;
}

size_t CsFillAttributes(CsBuffer *b, CsCoord start, uint16_t attribute, size_t count)
{
    b = CsEffective(b);
    size_t n = span(b, start, count);
    if (!n)
        return 0;
    size_t first = (size_t)start.Y * (size_t)b->Grid.Width + (size_t)start.X;
    CsAttr a = CsAttrFromLegacy(b->Term, attribute & (uint16_t)~CS_DBCS_MASK);
    for (size_t i = 0; i < n; i++) {
        CsCell *c = CELL_AT(b, first + i);
        c->Attr = a.Attr;
        c->Ext = a.Ext;
    }
    mark_span(b, first, n);
    CsRender(b->Term);
    return n;
}

static bool rect_valid(const CsRect *r)
{
    return r->Right >= r->Left && r->Bottom >= r->Top;
}

CsResult CsScroll(CsBuffer *b, CsRect src, const CsRect *clip_in, CsCoord dest, CsCharInfo fill)
{
    b = CsEffective(b);
    CsRect buffer = {0, 0, (int16_t)(b->Grid.Width - 1), (int16_t)(b->Grid.Height - 1)};
    /* the source is cut to the buffer, and the clip is cut to the buffer too. */
    CsRect s = {(int16_t)CS_MAX(src.Left, 0), (int16_t)CS_MAX(src.Top, 0), (int16_t)CS_MIN(src.Right, buffer.Right),
                (int16_t)CS_MIN(src.Bottom, buffer.Bottom)};
    if (!rect_valid(&s))
        return CS_OK;
    CsRect clip = buffer;
    if (clip_in) {
        clip.Left = (int16_t)CS_MAX(clip.Left, clip_in->Left);
        clip.Top = (int16_t)CS_MAX(clip.Top, clip_in->Top);
        clip.Right = (int16_t)CS_MIN(clip.Right, clip_in->Right);
        clip.Bottom = (int16_t)CS_MIN(clip.Bottom, clip_in->Bottom);
    }
    if (!rect_valid(&clip))
        return CS_OK;
    /* a fill of nothing at all is blanks in the colors of the text. */
    CsCell filler;
    if (fill.Char == 0 && (fill.Attributes & ~CS_DBCS_MASK) == 0) {
        filler = CsBlank(b);
    } else {
        CsAttr a = CsAttrFromLegacy(b->Term, fill.Attributes & (uint16_t)~CS_DBCS_MASK);
        filler = (CsCell){fill.Char, a.Attr, a.Ext, 0};
    }
    /* the part of the source that can be moved is inside the clip; the destination is that, moved, inside the clip. */
    CsRect from = {(int16_t)CS_MAX(s.Left, clip.Left), (int16_t)CS_MAX(s.Top, clip.Top),
                   (int16_t)CS_MIN(s.Right, clip.Right), (int16_t)CS_MIN(s.Bottom, clip.Bottom)};
    if (!rect_valid(&from))
        return CS_OK;
    int w = from.Right - from.Left + 1, h = from.Bottom - from.Top + 1;
    CsCell *copy = CsAllocate(sizeof(CsCell) * (size_t)w * (size_t)h);
    if (!copy)
        return CS_NO_MEMORY;
    for (int y = 0; y < h; y++)
        memcpy(copy + (size_t)y * (size_t)w, &b->Grid.Rows[from.Top + y].Cells[from.Left], sizeof(CsCell) * (size_t)w);
    /* the destination keeps its place relative to the whole source rectangle, whatever was cut from it. */
    int dx = dest.X + (from.Left - src.Left), dy = dest.Y + (from.Top - src.Top);
    for (int y = from.Top; y <= from.Bottom; y++)
        CsRowFill(&b->Grid.Rows[y], from.Left, from.Right, filler);
    for (int y = 0; y < h; y++) {
        int ty = dy + y;
        if (ty < clip.Top || ty > clip.Bottom)
            continue;
        for (int x = 0; x < w; x++) {
            int tx = dx + x;
            if (tx < clip.Left || tx > clip.Right)
                continue;
            b->Grid.Rows[ty].Cells[tx] = copy[(size_t)y * (size_t)w + (size_t)x];
        }
    }
    CsFree(copy);
    CsMarkRows(b, from.Top, from.Bottom, true);
    int ty0 = CS_MAX(dy, clip.Top), ty1 = CS_MIN(dy + h - 1, clip.Bottom);
    for (int y = ty0; y <= ty1; y++)
        CsMark(b, y, CS_MAX(dx, clip.Left), CS_MIN(dx + w - 1, clip.Right), true);
    CsRender(b->Term);
    return CS_OK;
}
