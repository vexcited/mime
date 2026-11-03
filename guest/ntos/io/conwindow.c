/*
 * the parts of the screen buffer module that a console window needs. a window shows a part of a buffer that has
 * thousands of rows of scrollback, sizes itself by the user, and draws cells with the colors resolved.
 */
#include "conscreen_p.h"

void CsTerminalSetWindowed(CsTerminal *t, int max_cols, int max_rows)
{
    t->Windowed = true;
    t->TerminalSized = false;
    t->Headless = true;
    t->MaxCols = max_cols;
    t->MaxRows = max_rows;
}

void CsSetDefaultAttributes(CsBuffer *b, uint16_t attributes)
{
    CsSetAttributes(b, attributes);
    b->DefaultAttr = b->Attr;
    CsCell blank = CsBlankOf(b->Attr);
    for (int y = 0; y < b->Grid.Height; y++)
        CsRowFill(&b->Grid.Rows[y], 0, b->Grid.Width - 1, blank);
}

/* moves the viewport by the least that brings the cursor into it. */
void CsFollowCursor(CsBuffer *b)
{
    int h = b->Window.Bottom - b->Window.Top + 1;
    int w = b->Window.Right - b->Window.Left + 1;
    CsRect old = b->Window;
    if (b->CursorY < b->Window.Top) {
        b->Window.Top = (int16_t)b->CursorY;
        b->Window.Bottom = (int16_t)(b->Window.Top + h - 1);
    } else if (b->CursorY > b->Window.Bottom) {
        b->Window.Bottom = (int16_t)b->CursorY;
        b->Window.Top = (int16_t)(b->Window.Bottom - h + 1);
    }
    if (b->CursorX < b->Window.Left) {
        b->Window.Left = (int16_t)b->CursorX;
        b->Window.Right = (int16_t)(b->Window.Left + w - 1);
    } else if (b->CursorX > b->Window.Right) {
        b->Window.Right = (int16_t)b->CursorX;
        b->Window.Left = (int16_t)(b->Window.Right - w + 1);
    }
    if (memcmp(&old, &b->Window, sizeof(old))) {
        b->Top = 0;
        b->Bottom = b->Window.Bottom - b->Window.Top;
        b->Term->Changed |= CS_CHANGED_WINDOW;
    }
}

/* ------------------------------------------------------------------ cells to draw */

static uint32_t xterm_color(const CsBuffer *b, unsigned index)
{
    static const unsigned levels[6] = {0, 95, 135, 175, 215, 255};
    index &= 0xFF;
    if (index < 16)
        return b->ColorTable[CsAnsiToLegacy(index)];
    if (index < 232) {
        unsigned n = index - 16;
        return levels[n / 36] | (levels[n / 6 % 6] << 8) | (levels[n % 6] << 16);
    }
    unsigned gray = 8 + 10 * (index - 232);
    return gray | (gray << 8) | (gray << 16);
}

static uint32_t resolve(const CsBuffer *b, uint8_t kind, uint32_t value, unsigned fallback, bool bright)
{
    switch (kind) {
    case CS_COLOR_DEFAULT:
        return b->ColorTable[fallback | (bright ? 8u : 0u)];
    case CS_COLOR_16:
    case CS_COLOR_256:
        if (bright && value < 8)
            value |= 8;
        return xterm_color(b, value);
    default:
        return value & 0xFFFFFF;
    }
}

void CsDrawRow(CsBuffer *b, int y, int left, int count, CsDrawCell *out)
{
    const CsTerminal *t = b->Term;
    memset(out, 0, sizeof(*out) * (size_t)count);
    if (y < 0 || y >= b->Grid.Height)
        return;
    const CsRow *row = &b->Grid.Rows[y];
    for (int i = 0; i < count; i++) {
        int x = left + i;
        if (x < 0 || x >= b->Grid.Width) {
            out[i].Char = ' ';
            out[i].Foreground = b->ColorTable[7];
            out[i].Background = b->ColorTable[0];
            continue;
        }
        const CsCell *c = &row->Cells[x];
        const CsExt *e = CsExtGet(t, c->Ext);
        uint16_t style = (uint16_t)(e->Style | (c->Attr & CS_STYLE_LVB_MASK));
        bool bold = (e->Style & CS_STYLE_BOLD) != 0;
        out[i].Foreground = resolve(b, e->FgKind, e->Fg, 7, bold && e->FgKind <= CS_COLOR_16);
        out[i].Background = resolve(b, e->BgKind, e->Bg, 0, false);
        uint16_t flags = 0;
        uint32_t ch = c->Char;
        if (c->Flags & CS_CELL_PAIR) {
            if (c->Flags & CS_CELL_TRAIL) {
                ch = 0;
                flags |= CS_DRAW_TRAIL;
            } else if (x + 1 < b->Grid.Width) {
                ch = 0x10000 + (((uint32_t)c->Char - 0xD800) << 10) + ((uint32_t)row->Cells[x + 1].Char - 0xDC00);
                flags |= CS_DRAW_LEAD;
            }
        } else if (c->Flags & CS_CELL_LEAD) {
            flags |= CS_DRAW_LEAD;
        } else if (c->Flags & CS_CELL_TRAIL) {
            ch = 0;
            flags |= CS_DRAW_TRAIL;
        }
        if (row->Wrapped && x == b->Grid.Width - 1)
            flags |= CS_DRAW_WRAPPED;
        out[i].Char = ch;
        out[i].Style = style;
        out[i].Flags = flags;
    }
}

/* ------------------------------------------------------------------ moving the viewport */

void CsWindowScroll(CsTerminal *t, int left, int top)
{
    CsBuffer *b = CsDisplayed(t);
    if (!b)
        return;
    int w = b->Window.Right - b->Window.Left + 1, h = b->Window.Bottom - b->Window.Top + 1;
    if (top < 0 && left < 0) {
        CsFollowCursor(b);
        t->Generation++;
        return;
    }
    left = CS_MAX(0, CS_MIN(left, b->Grid.Width - w));
    top = CS_MAX(0, CS_MIN(top, b->Grid.Height - h));
    b->Window.Left = (int16_t)left;
    b->Window.Right = (int16_t)(left + w - 1);
    b->Window.Top = (int16_t)top;
    b->Window.Bottom = (int16_t)(top + h - 1);
    b->Top = 0;
    b->Bottom = h - 1;
    t->Generation++;
}

/* ------------------------------------------------------------------ resizing */

typedef struct Reflow {
    const CsBuffer *Old;
    int Width;       /* the new width */
    int Last;        /* the last old row that has anything */
    CsGrid New;
    int Rows;        /* new rows used so far */
    int Drop;        /* rows that scroll off the top of the new grid */
    int *Start;      /* new row of each old row's first cell */
    int CursorRow, CursorCol;
    bool Write;
} Reflow;

static bool blank_cell(const CsCell *c, uint16_t default_attr)
{
    return (c->Char == ' ' || c->Char == 0) && c->Attr == default_attr && !(c->Flags & (CS_CELL_LEAD | CS_CELL_TRAIL | CS_CELL_PAIR));
}

/* the cells of a row up to the last one that holds anything. */
static int row_length(const CsBuffer *b, const CsRow *row, int width)
{
    int n = width;
    while (n > 0 && blank_cell(&row->Cells[n - 1], b->DefaultAttr.Attr))
        n--;
    return n;
}

static void put_cell(Reflow *r, int row, int col, const CsCell *c, bool wrapped)
{
    int y = row - r->Drop;
    if (!r->Write || y < 0 || y >= r->New.Height)
        return;
    r->New.Rows[y].Cells[col] = *c;
    r->New.Rows[y].Touched = true;
    if (wrapped)
        r->New.Rows[y].Wrapped = true;
}

static void reflow_pass(Reflow *r)
{
    const CsBuffer *b = r->Old;
    int old_w = b->Grid.Width;
    int row = 0, col = 0;
    int o = 0;
    r->CursorRow = r->CursorCol = 0;
    while (o <= r->Last) {
        int e = o;
        while (e < r->Last && b->Grid.Rows[e].Wrapped)
            e++;
        int tail = row_length(b, &b->Grid.Rows[e], old_w);
        if (b->CursorY == e && tail < b->CursorX)
            tail = b->CursorX;
        long total = (long)(e - o) * old_w + tail;
        bool cursor_here = b->CursorY >= o && b->CursorY <= e;
        long cursor_index = cursor_here ? (long)(b->CursorY - o) * old_w + b->CursorX : -1;
        for (int k = o; k <= e; k++) {
            long index = (long)(k - o) * old_w;
            if (index >= total)
                r->Start[k] = row;
        }
        col = 0;
        for (long i = 0; i < total; i++) {
            if (i % old_w == 0) {
                int k = o + (int)(i / old_w);
                r->Start[k] = row + (col >= r->Width ? 1 : 0);
            }
            const CsCell *c = &b->Grid.Rows[o + (int)(i / old_w)].Cells[i % old_w];
            bool lead = (c->Flags & (CS_CELL_LEAD | CS_CELL_PAIR)) && !(c->Flags & CS_CELL_TRAIL);
            int need = lead ? 2 : 1;
            if (col + need > r->Width) {
                if (col < r->Width) {
                    CsCell pad = CsBlankOf(b->DefaultAttr);
                    put_cell(r, row, col, &pad, true);
                }
                if (r->Write && row - r->Drop >= 0 && row - r->Drop < r->New.Height)
                    r->New.Rows[row - r->Drop].Wrapped = true;
                row++;
                col = 0;
            }
            if (i == cursor_index) {
                r->CursorRow = row;
                r->CursorCol = col;
            }
            if (need == 2 && r->Width < 2) {
                CsCell blank = CsBlankOf(b->DefaultAttr);
                put_cell(r, row, col, &blank, false);
                col++;
                continue;
            }
            put_cell(r, row, col, c, false);
            col++;
            if (lead && i + 1 < total) {
                const CsCell *t2 = &b->Grid.Rows[o + (int)((i + 1) / old_w)].Cells[(i + 1) % old_w];
                put_cell(r, row, col, t2, false);
                col++;
                i++;
            }
        }
        if (cursor_index >= total) {
            /* the cursor sits after the text. */
            if (col >= r->Width) {
                if (r->Write && row - r->Drop >= 0 && row - r->Drop < r->New.Height)
                    r->New.Rows[row - r->Drop].Wrapped = true;
                r->CursorRow = row + 1;
                r->CursorCol = 0;
                row++;
            } else {
                r->CursorRow = row;
                r->CursorCol = col + (int)(cursor_index - total);
                if (r->CursorCol >= r->Width)
                    r->CursorCol = r->Width - 1;
            }
        }
        row++;
        o = e + 1;
    }
    r->Rows = row;
}

static int last_used_row(const CsBuffer *b)
{
    int last = b->CursorY;
    for (int y = b->Grid.Height - 1; y > last; y--) {
        const CsRow *row = &b->Grid.Rows[y];
        if (row_length(b, row, b->Grid.Width) > 0) {
            last = y;
            break;
        }
    }
    return last;
}

/* the main buffer takes a new width and its text flows to it. */
static bool reflow(CsBuffer *b, int width)
{
    Reflow r = {.Old = b, .Width = width};
    r.Last = last_used_row(b);
    if (width == b->Grid.Width)
        return true;
    r.Start = CsAllocate(sizeof(int) * (size_t)(r.Last + 2));
    if (!r.Start)
        return false;
    for (int i = 0; i <= r.Last + 1; i++)
        r.Start[i] = 0;
    reflow_pass(&r);
    int height = b->Grid.Height;
    r.Drop = r.Rows > height ? r.Rows - height : 0;
    CsCell blank = CsBlankOf(b->DefaultAttr);
    if (!CsGridInit(&r.New, width, height, blank)) {
        CsFree(r.Start);
        return false;
    }
    uint8_t *tabs = CsAllocate((size_t)width);
    if (!tabs) {
        CsGridFree(&r.New);
        CsFree(r.Start);
        return false;
    }
    for (int x = 0; x < width; x++)
        tabs[x] = x < b->Grid.Width ? b->TabStops[x] : ((x % 8) == 0 && x != 0);
    r.Write = true;
    int cursor_row = 0, cursor_col = 0;
    reflow_pass(&r);
    cursor_row = r.CursorRow - r.Drop;
    cursor_col = r.CursorCol;
    int top = b->Window.Top;
    int mapped = top <= r.Last ? r.Start[top] : (r.Rows + (top - r.Last - 1));
    mapped -= r.Drop;
    CsFree(r.Start);
    CsGridFree(&b->Grid);
    CsFree(b->TabStops);
    b->Grid = r.New;
    b->TabStops = tabs;
    b->Scrolls += (unsigned)r.Drop;
    b->CursorX = CS_MAX(0, CS_MIN(cursor_col, width - 1));
    b->CursorY = CS_MAX(0, CS_MIN(cursor_row, height - 1));
    b->WrapPending = false;
    b->Window.Top = (int16_t)CS_MAX(0, mapped);
    b->SavedX = CS_MIN(b->SavedX, width - 1);
    b->MaxWindow.X = (int16_t)width;
    b->Dirty = false;
    return true;
}

/* the window of a buffer: width by height cells with the top left corner inside the buffer. */
static void fit_window(CsBuffer *b, int width, int height)
{
    width = CS_MAX(1, CS_MIN(width, b->Grid.Width));
    height = CS_MAX(1, CS_MIN(height, b->Grid.Height));
    int top = CS_MAX(0, CS_MIN(b->Window.Top, b->Grid.Height - height));
    int left = CS_MAX(0, CS_MIN(b->Window.Left, b->Grid.Width - width));
    b->Window.Left = (int16_t)left;
    b->Window.Right = (int16_t)(left + width - 1);
    b->Window.Top = (int16_t)top;
    b->Window.Bottom = (int16_t)(top + height - 1);
    b->Top = 0;
    b->Bottom = height - 1;
}

void CsWindowResize(CsTerminal *t, int cols, int rows)
{
    if (cols < 1 || rows < 1 || cols > CS_MAX_DIMENSION || rows > CS_MAX_DIMENSION)
        return;
    int old_cols = t->Cols;
    for (CsBuffer *b = t->Buffers; b; b = b->Next) {
        if (b->Main) {
            /* the alternate screen is the window. */
            if (b->Grid.Width != cols || b->Grid.Height != rows)
                (void)CsResizeBuffer(b, cols, rows);
            fit_window(b, cols, rows);
            b->Window.Top = b->Window.Left = 0;
            b->Window.Right = (int16_t)(cols - 1);
            b->Window.Bottom = (int16_t)(rows - 1);
            continue;
        }
        if (b == t->Main && b->Grid.Width == old_cols && cols != old_cols) {
            if (!reflow(b, cols) && cols > b->Grid.Width)
                (void)CsResizeBuffer(b, cols, b->Grid.Height);
        } else if (b->Grid.Width < cols || b->Grid.Height < rows) {
            (void)CsResizeBuffer(b, CS_MAX(b->Grid.Width, cols), CS_MAX(b->Grid.Height, rows));
        }
        fit_window(b, cols, rows);
        CsFollowCursor(b);
        b->MaxWindow.X = (int16_t)CS_MIN(b->Grid.Width, t->MaxCols ? t->MaxCols : b->Grid.Width);
        b->MaxWindow.Y = (int16_t)CS_MIN(b->Grid.Height, t->MaxRows ? t->MaxRows : b->Grid.Height);
        b->Dirty = false;
    }
    t->Cols = cols;
    t->Rows = rows;
    t->Generation++;
    t->Changed |= CS_CHANGED_WINDOW;
}
