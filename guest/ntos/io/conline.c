/*
 * the line a program reads in line input mode, each program's command history and the aliases that rewrite lines.
 *
 * the echo goes to the screen buffer like a console's: text from the first changed character is written again,
 * cells the line no longer uses are blanked, and the cursor goes to the edit position. it is written without escape
 * sequences, so a typed escape shows as ^[.
 */
#include "conline.h"
#include "conscreen_p.h"


#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
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

#define KS_RIGHT_ALT 0x1
#define KS_LEFT_ALT 0x2
#define KS_RIGHT_CTRL 0x4
#define KS_LEFT_CTRL 0x8

#define DEFAULT_HISTORY_SIZE 25
#define DEFAULT_HISTORY_BUFFERS 4

/* ------------------------------------------------------------------ strings */

static ConChar lower(ConChar c)
{
    if (c >= 'A' && c <= 'Z')
        return (ConChar)(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
        return (ConChar)(c + 32);
    return c;
}

static bool equal_fold(const ConChar *a, size_t al, const ConChar *b, size_t bl)
{
    if (al != bl)
        return false;
    for (size_t i = 0; i < al; i++)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

static ConChar *duplicate(const ConChar *s, size_t n)
{
    ConChar *d = CsAllocate((n + 1) * sizeof(ConChar));
    if (!d)
        return nullptr;
    if (n)
        memcpy(d, s, n * sizeof(ConChar));
    d[n] = 0;
    return d;
}

static ConChar *duplicate_lower(const ConChar *s, size_t n)
{
    ConChar *d = duplicate(s, n);
    for (size_t i = 0; d && i < n; i++)
        d[i] = lower(d[i]);
    return d;
}

/* ------------------------------------------------------------------ history */

typedef struct ExeHistory {
    struct ExeHistory *Next;
    ConChar *Exe;
    size_t ExeLength;
    ConChar **Commands; /* oldest first. */
    size_t *Lengths;
    long Count;
    long Capacity;
    long Limit;         /* the most commands kept. */
    long LastDisplayed; /* the command the arrow keys are at, -1 for none. */
    bool JustReset;     /* the next up key shows the newest command, not the one before. */
} ExeHistory;

typedef struct Alias {
    struct Alias *Next;
    ConChar *Source; /* lower case. */
    size_t SourceLength;
    ConChar *Target;
    size_t TargetLength;
} Alias;

typedef struct ExeAliases {
    struct ExeAliases *Next;
    ConChar *Exe; /* lower case. */
    size_t ExeLength;
    Alias *Aliases;
} ExeAliases;

struct ConLineState {
    unsigned BufferSize; /* commands kept per program. */
    unsigned Buffers;    /* programs whose commands are kept. */
    unsigned Flags;
    ExeHistory *Histories;
    ExeAliases *Exes;
};

ConLineState *ConLineStateCreate(void)
{
    ConLineState *s = CsAllocate(sizeof(*s));
    if (!s)
        return nullptr;
    memset(s, 0, sizeof(*s));
    s->BufferSize = DEFAULT_HISTORY_SIZE;
    s->Buffers = DEFAULT_HISTORY_BUFFERS;
    return s;
}

static void free_history(ExeHistory *h)
{
    for (long i = 0; i < h->Count; i++)
        CsFree(h->Commands[i]);
    CsFree(h->Commands);
    CsFree(h->Lengths);
    CsFree(h->Exe);
    CsFree(h);
}

static void free_exe_aliases(ExeAliases *e)
{
    while (e->Aliases) {
        Alias *a = e->Aliases;
        e->Aliases = a->Next;
        CsFree(a->Source);
        CsFree(a->Target);
        CsFree(a);
    }
    CsFree(e->Exe);
    CsFree(e);
}

void ConLineStateDestroy(ConLineState *s)
{
    if (!s)
        return;
    while (s->Histories) {
        ExeHistory *h = s->Histories;
        s->Histories = h->Next;
        free_history(h);
    }
    while (s->Exes) {
        ExeAliases *e = s->Exes;
        s->Exes = e->Next;
        free_exe_aliases(e);
    }
    CsFree(s);
}

static void drop_from(ExeHistory *h, long from)
{
    for (long i = from; i < h->Count; i++)
        CsFree(h->Commands[i]);
    if (from < h->Count)
        h->Count = from;
}

/* keeps at most n commands: the oldest stay, the newest beyond n go. */
static void realloc_history(ExeHistory *h, long n)
{
    if (n == h->Limit)
        return;
    if (n < 0)
        n = 0;
    drop_from(h, n);
    h->Limit = n;
    h->JustReset = true;
    h->LastDisplayed = h->Count - 1;
}

void ConHistoryGetInfo(const ConLineState *s, unsigned *buffer_size, unsigned *buffers, unsigned *flags)
{
    *buffer_size = s->BufferSize;
    *buffers = s->Buffers;
    *flags = s->Flags;
}

bool ConHistorySetInfo(ConLineState *s, unsigned buffer_size, unsigned buffers, unsigned flags)
{
    if (buffer_size > 0x7FFF || buffers > 0x7FFF || (flags & ~(unsigned)CON_HISTORY_NO_DUP))
        return false;
    s->BufferSize = buffer_size;
    s->Buffers = buffers;
    s->Flags = flags;
    for (ExeHistory *h = s->Histories; h; h = h->Next)
        realloc_history(h, (long)buffer_size);
    return true;
}

static ExeHistory *find_history(const ConLineState *s, const ConChar *exe, size_t n)
{
    for (ExeHistory *h = s->Histories; h; h = h->Next)
        if (equal_fold(h->Exe, h->ExeLength, exe, n))
            return h;
    return nullptr;
}

/* finds or makes a program's history. when all buffers are taken, an empty one or else the least recently used goes. */
static ExeHistory *get_history(ConLineState *s, const ConChar *exe, size_t n)
{
    ExeHistory *h = find_history(s, exe, n);
    if (h)
        return h;
    /* no buffers at all keeps no history. */
    if (!s->Buffers)
        return nullptr;
    unsigned count = 0;
    for (ExeHistory *x = s->Histories; x; x = x->Next)
        count++;
    if (count >= s->Buffers) {
        ExeHistory **victim = nullptr;
        for (ExeHistory **p = &s->Histories; *p; p = &(*p)->Next)
            if (!(*p)->Count)
                victim = p;
        if (!victim) {
            victim = &s->Histories;
            while ((*victim)->Next)
                victim = &(*victim)->Next;
        }
        if (!victim || !*victim)
            return nullptr;
        ExeHistory *old = *victim;
        *victim = old->Next;
        free_history(old);
    }
    h = CsAllocate(sizeof(*h));
    if (!h)
        return nullptr;
    memset(h, 0, sizeof(*h));
    h->Exe = duplicate(exe, n);
    h->ExeLength = n;
    h->Limit = (long)s->BufferSize;
    h->LastDisplayed = -1;
    if (!h->Exe) {
        CsFree(h);
        return nullptr;
    }
    h->Next = s->Histories;
    s->Histories = h;
    return h;
}

/* the most recently used histories come first. */
static void to_front(ConLineState *s, ExeHistory *h)
{
    for (ExeHistory **p = &s->Histories; *p; p = &(*p)->Next) {
        if (*p == h) {
            *p = h->Next;
            h->Next = s->Histories;
            s->Histories = h;
            return;
        }
    }
}

static bool same(const ExeHistory *h, long i, const ConChar *line, size_t length)
{
    return h->Lengths[i] == length && !memcmp(h->Commands[i], line, length * sizeof(ConChar));
}

void ConHistoryAdd(ConLineState *s, const ConChar *exe, size_t exe_length, const ConChar *line, size_t length)
{
    if (!length)
        return;
    ExeHistory *h = get_history(s, exe, exe_length);
    if (!h || h->Limit <= 0)
        return;
    to_front(s, h);
    /* the same command twice in a row is kept once. */
    if (h->Count && same(h, h->Count - 1, line, length)) {
        h->JustReset = true;
        return;
    }
    if (s->Flags & CON_HISTORY_NO_DUP) {
        long at = h->LastDisplayed < 0 ? h->Count - 1 : h->LastDisplayed;
        for (long k = 0; k < h->Count; k++) {
            long i = ((at - k) % h->Count + h->Count) % h->Count;
            if (same(h, i, line, length)) {
                CsFree(h->Commands[i]);
                memmove(h->Commands + i, h->Commands + i + 1, (size_t)(h->Count - i - 1) * sizeof(h->Commands[0]));
                memmove(h->Lengths + i, h->Lengths + i + 1, (size_t)(h->Count - i - 1) * sizeof(h->Lengths[0]));
                h->Count--;
                if (h->LastDisplayed >= i)
                    h->LastDisplayed--;
                break;
            }
        }
    }
    if (h->Count >= h->Limit && h->Count) {
        CsFree(h->Commands[0]);
        memmove(h->Commands, h->Commands + 1, (size_t)(h->Count - 1) * sizeof(h->Commands[0]));
        memmove(h->Lengths, h->Lengths + 1, (size_t)(h->Count - 1) * sizeof(h->Lengths[0]));
        h->Count--;
        h->LastDisplayed--;
    }
    if (h->Count == h->Capacity) {
        long cap = h->Capacity ? h->Capacity * 2 : 8;
        ConChar **c = CsAllocate((size_t)cap * sizeof(*c));
        size_t *l = CsAllocate((size_t)cap * sizeof(*l));
        if (!c || !l) {
            CsFree(c);
            CsFree(l);
            return;
        }
        if (h->Count) {
            memcpy(c, h->Commands, (size_t)h->Count * sizeof(*c));
            memcpy(l, h->Lengths, (size_t)h->Count * sizeof(*l));
        }
        CsFree(h->Commands);
        CsFree(h->Lengths);
        h->Commands = c;
        h->Lengths = l;
        h->Capacity = cap;
    }
    ConChar *copy = duplicate(line, length);
    if (!copy)
        return;
    h->Commands[h->Count] = copy;
    h->Lengths[h->Count] = length;
    h->Count++;
    if (h->LastDisplayed >= 0 && h->LastDisplayed < h->Count && same(h, h->LastDisplayed, line, length)) {
        /* a recalled command that was run keeps the keys where they were. */
    } else {
        h->LastDisplayed = h->Count - 1;
        h->JustReset = true;
    }
}

size_t ConHistoryGet(const ConLineState *s, const ConChar *exe, size_t exe_length, ConChar *out, size_t room)
{
    ExeHistory *h = find_history(s, exe, exe_length);
    size_t total = 0;
    if (!h)
        return 0;
    for (long i = 0; i < h->Count; i++) {
        if (out && total + h->Lengths[i] + 1 <= room) {
            memcpy(out + total, h->Commands[i], h->Lengths[i] * sizeof(ConChar));
            out[total + h->Lengths[i]] = 0;
        }
        total += h->Lengths[i] + 1;
    }
    return total;
}

void ConHistoryExpunge(ConLineState *s, const ConChar *exe, size_t exe_length)
{
    ExeHistory *h = find_history(s, exe, exe_length);
    if (!h)
        return;
    drop_from(h, 0);
    h->LastDisplayed = -1;
    h->JustReset = true;
}

bool ConHistorySetCount(ConLineState *s, const ConChar *exe, size_t exe_length, unsigned count)
{
    ExeHistory *h = find_history(s, exe, exe_length);
    if (!h)
        return true;
    to_front(s, h);
    realloc_history(h, (long)count);
    return true;
}

unsigned ConHistoryGetCount(const ConLineState *s, const ConChar *exe, size_t exe_length)
{
    ExeHistory *h = find_history(s, exe, exe_length);
    return h ? (unsigned)h->Limit : s->BufferSize;
}

/* ------------------------------------------------------------------ aliases */

static ExeAliases *find_exe(const ConLineState *s, const ConChar *exe, size_t n)
{
    for (ExeAliases *e = s->Exes; e; e = e->Next)
        if (equal_fold(e->Exe, e->ExeLength, exe, n))
            return e;
    return nullptr;
}

bool ConAliasAdd(ConLineState *s, const ConChar *exe, size_t exe_length, const ConChar *source, size_t source_length,
                 const ConChar *target, size_t target_length)
{
    if (!source_length)
        return false;
    ExeAliases *e = find_exe(s, exe, exe_length);
    if (!e) {
        e = CsAllocate(sizeof(*e));
        if (!e)
            return false;
        memset(e, 0, sizeof(*e));
        e->Exe = duplicate_lower(exe, exe_length);
        e->ExeLength = exe_length;
        if (!e->Exe) {
            CsFree(e);
            return false;
        }
        ExeAliases **tail = &s->Exes;
        while (*tail)
            tail = &(*tail)->Next;
        *tail = e;
    }
    Alias **p = &e->Aliases;
    for (; *p; p = &(*p)->Next)
        if (equal_fold((*p)->Source, (*p)->SourceLength, source, source_length))
            break;
    /* an empty target removes the alias, and an exe is never forgotten. */
    if (!target || !target_length) {
        if (*p) {
            Alias *a = *p;
            *p = a->Next;
            CsFree(a->Source);
            CsFree(a->Target);
            CsFree(a);
        }
        return true;
    }
    ConChar *copy = duplicate(target, target_length);
    if (!copy)
        return false;
    if (*p) {
        CsFree((*p)->Target);
        (*p)->Target = copy;
        (*p)->TargetLength = target_length;
        return true;
    }
    Alias *a = CsAllocate(sizeof(*a));
    ConChar *src = duplicate_lower(source, source_length);
    if (!a || !src) {
        CsFree(a);
        CsFree(src);
        CsFree(copy);
        return false;
    }
    a->Source = src;
    a->SourceLength = source_length;
    a->Target = copy;
    a->TargetLength = target_length;
    a->Next = nullptr;
    *p = a;
    return true;
}

long ConAliasGet(const ConLineState *s, const ConChar *exe, size_t exe_length, const ConChar *source,
                 size_t source_length, ConChar *out, size_t room)
{
    ExeAliases *e = find_exe(s, exe, exe_length);
    if (!e)
        return -1;
    for (Alias *a = e->Aliases; a; a = a->Next) {
        if (equal_fold(a->Source, a->SourceLength, source, source_length)) {
            size_t n = a->TargetLength + 1;
            if (out && n <= room) {
                memcpy(out, a->Target, a->TargetLength * sizeof(ConChar));
                out[a->TargetLength] = 0;
            }
            return (long)n;
        }
    }
    return -1;
}

size_t ConAliasList(const ConLineState *s, const ConChar *exe, size_t exe_length, ConChar *out, size_t room)
{
    ExeAliases *e = find_exe(s, exe, exe_length);
    size_t total = 0;
    if (!e)
        return 0;
    for (Alias *a = e->Aliases; a; a = a->Next) {
        size_t n = a->SourceLength + 1 + a->TargetLength + 1;
        if (out && total + n <= room) {
            memcpy(out + total, a->Source, a->SourceLength * sizeof(ConChar));
            out[total + a->SourceLength] = '=';
            memcpy(out + total + a->SourceLength + 1, a->Target, a->TargetLength * sizeof(ConChar));
            out[total + n - 1] = 0;
        }
        total += n;
    }
    return total;
}

size_t ConAliasExes(const ConLineState *s, ConChar *out, size_t room)
{
    size_t total = 0;
    for (ExeAliases *e = s->Exes; e; e = e->Next) {
        size_t n = e->ExeLength + 1;
        if (out && total + n <= room) {
            memcpy(out + total, e->Exe, e->ExeLength * sizeof(ConChar));
            out[total + e->ExeLength] = 0;
        }
        total += n;
    }
    return total;
}

void ConAliasExpunge(ConLineState *s, const ConChar *exe, size_t exe_length)
{
    ExeAliases *e = find_exe(s, exe, exe_length);
    if (!e)
        return;
    while (e->Aliases) {
        Alias *a = e->Aliases;
        e->Aliases = a->Next;
        CsFree(a->Source);
        CsFree(a->Target);
        CsFree(a);
    }
}

/*
 * the line is split at single blanks: the first word is the alias, the rest after the first blank its arguments.
 */
bool ConAliasExpand(const ConLineState *s, const ConChar *exe, size_t exe_length, const ConChar *line, size_t length,
                    ConChar *out, size_t room, size_t *out_length)
{
    ExeAliases *e = find_exe(s, exe, exe_length);
    if (!e)
        return false;
    size_t end = 0;
    while (end < length && line[end] != ' ')
        end++;
    if (!end)
        return false;
    Alias *found = nullptr;
    for (Alias *a = e->Aliases; a; a = a->Next) {
        if (equal_fold(a->Source, a->SourceLength, line, end)) {
            found = a;
            break;
        }
    }
    if (!found)
        return false;
    size_t args = end < length ? end + 1 : length;
    size_t arg_start[9], arg_end[9];
    unsigned argc = 0;
    size_t i = args;
    while (i <= length && argc < 9) {
        arg_start[argc] = i;
        while (i < length && line[i] != ' ')
            i++;
        arg_end[argc++] = i;
        if (i >= length)
            break;
        i++;
    }
    size_t n = 0;
#define PUT(c)                      \
    do {                            \
        if (n < room)               \
            out[n] = (ConChar)(c);  \
        n++;                        \
    } while (0)
    for (size_t t = 0; t < found->TargetLength; t++) {
        ConChar c = found->Target[t];
        if (c != '$' || t + 1 >= found->TargetLength) {
            PUT(c);
            continue;
        }
        ConChar m = found->Target[t + 1];
        if (m >= '1' && m <= '9') {
            unsigned k = m - '1';
            if (k < argc && args < length)
                for (size_t q = arg_start[k]; q < arg_end[k]; q++)
                    PUT(line[q]);
            t++;
        } else if (m == '*') {
            for (size_t q = args; q < length; q++)
                PUT(line[q]);
            t++;
        } else if (m == 'G' || m == 'g') {
            PUT('>');
            t++;
        } else if (m == 'L' || m == 'l') {
            PUT('<');
            t++;
        } else if (m == 'B' || m == 'b') {
            PUT('|');
            t++;
        } else if (m == 'T' || m == 't') {
            PUT('\r');
            PUT('\n');
            t++;
        } else {
            PUT(c);
        }
    }
    PUT('\r');
    PUT('\n');
#undef PUT
    *out_length = n;
    return n <= room;
}

/* ------------------------------------------------------------------ the line */

struct ConLine {
    CsBuffer *Screen;
    ConLineState *State;
    ConChar *Exe;
    size_t ExeLength;
    bool Echo;
    bool Processed;
    bool Insert;
    bool Vt;
    uint32_t WakeMask;
    ConChar *Text;
    size_t Length;
    size_t Allocated;
    size_t Cursor;
    int StartX, StartY; /* where the line starts on the screen. */
    size_t Displayed;   /* cells the line took when last drawn. */
    bool Done;
    uint32_t KeyState;
    ConChar *Result;
    size_t ResultLength;
    unsigned ScrollsSeen;
    size_t Lines; /* more than one when an alias made several lines. */

    /* the command history popup of F7, drawn over the screen until a key closes it. */
    bool Popup;
    long PopupSelected, PopupTop;
    CsRect PopupRect;
    CsCharInfo *PopupSaved;
};

/* the cells a character takes in the echo when it starts at offset. */
static unsigned cells_of(const ConLine *l, ConChar c, unsigned offset)
{
    if (c == '\t') {
        unsigned width = (unsigned)l->Screen->Grid.Width;
        unsigned col = ((unsigned)l->StartX + offset) % (width ? width : 1);
        unsigned n = 8 - (col & 7);
        return n > width - col ? width - col : n;
    }
    if (c < 0x20)
        return 2;
    unsigned w = (unsigned)CsCharWidth(c);
    return w ? w : 1;
}

static size_t offset_of(const ConLine *l, size_t index)
{
    unsigned offset = 0;
    for (size_t i = 0; i < index; i++)
        offset += cells_of(l, l->Text[i], offset);
    return offset;
}

/* echoes from..to: controls as ^X, a tab as the blanks it stands for. */
static void echo_range(ConLine *l, size_t from, size_t to, unsigned offset)
{
    uint16_t chunk[256];
    size_t n = 0;
    CsBuffer *s = l->Screen;
    for (size_t i = from; i < to; i++) {
        ConChar c = l->Text[i];
        if (n + 9 >= sizeof(chunk) / sizeof(chunk[0])) {
            CsPutLegacy(s, chunk, n);
            n = 0;
        }
        if (c == '\t') {
            unsigned cells = cells_of(l, c, offset);
            for (unsigned k = 0; k < cells; k++)
                chunk[n++] = ' ';
            offset += cells;
        } else if (c < 0x20) {
            chunk[n++] = '^';
            chunk[n++] = (uint16_t)(c + '@');
            offset += 2;
        } else {
            chunk[n++] = c;
            offset += cells_of(l, c, offset);
        }
    }
    if (n)
        CsPutLegacy(s, chunk, n);
}

/* the screen cell at an offset in cells from the start of the line. */
static void cell_of(const ConLine *l, size_t offset, int *x, int *y)
{
    int width = l->Screen->Grid.Width;
    size_t abs = (size_t)l->StartX + offset;
    *x = (int)(abs % (size_t)width);
    *y = l->StartY + (int)(abs / (size_t)width);
}

static void put_cursor(ConLine *l, size_t offset)
{
    CsBuffer *s = l->Screen;
    int x, y;
    cell_of(l, offset, &x, &y);
    if (y >= s->Grid.Height)
        y = s->Grid.Height - 1;
    CsSetCursorPosition(s, (CsCoord){(int16_t)x, (int16_t)y});
}

static void note_scrolls(ConLine *l)
{
    unsigned now = l->Screen->Scrolls;
    if (now != l->ScrollsSeen) {
        l->StartY -= (int)(now - l->ScrollsSeen);
        l->ScrollsSeen = now;
    }
}

static void redraw(ConLine *l, size_t from)
{
    if (!l->Echo)
        return;
    CsBuffer *s = l->Screen;
    unsigned offset = (unsigned)offset_of(l, from);
    int x, y;
    cell_of(l, offset, &x, &y);
    s->CursorX = x;
    s->CursorY = y < s->Grid.Height ? y : s->Grid.Height - 1;
    s->WrapPending = false;
    echo_range(l, from, l->Length, offset);
    note_scrolls(l);
    size_t total = offset_of(l, l->Length);
    if (total < l->Displayed) {
        size_t extra = l->Displayed - total;
        uint16_t blanks[64];
        for (size_t k = 0; k < sizeof(blanks) / sizeof(blanks[0]); k++)
            blanks[k] = ' ';
        while (extra) {
            size_t n = extra < 64 ? extra : 64;
            CsPutLegacy(s, blanks, n);
            extra -= n;
        }
        note_scrolls(l);
    }
    l->Displayed = total;
    put_cursor(l, offset_of(l, l->Cursor));
}

static bool grow(ConLine *l, size_t need)
{
    if (need <= l->Allocated)
        return true;
    size_t cap = l->Allocated ? l->Allocated : 64;
    while (cap < need)
        cap *= 2;
    ConChar *t = CsAllocate(cap * sizeof(ConChar));
    if (!t)
        return false;
    if (l->Length)
        memcpy(t, l->Text, l->Length * sizeof(ConChar));
    CsFree(l->Text);
    l->Text = t;
    l->Allocated = cap;
    return true;
}

ConLine *ConLineBegin(CsBuffer *screen, ConLineState *state, const ConChar *exe, size_t exe_length, unsigned flags,
                      const ConChar *initial, size_t initial_length, uint32_t wake_mask)
{
    ConLine *l = CsAllocate(sizeof(*l));
    if (!l)
        return nullptr;
    memset(l, 0, sizeof(*l));
    l->Screen = CsEffective(screen);
    l->State = state;
    static const ConChar none[1] = {0};
    l->Exe = duplicate(exe ? exe : none, exe ? exe_length : 0);
    l->ExeLength = exe ? exe_length : 0;
    l->Echo = (flags & CONLINE_ECHO) != 0;
    l->Processed = (flags & CONLINE_PROCESSED) != 0;
    l->Insert = (flags & CONLINE_INSERT) != 0;
    l->Vt = (flags & CONLINE_VT) != 0;
    l->WakeMask = wake_mask;
    l->StartX = l->Screen->CursorX;
    l->StartY = l->Screen->CursorY;
    l->ScrollsSeen = l->Screen->Scrolls;
    ExeHistory *h = find_history(state, l->Exe, l->ExeLength);
    if (h)
        to_front(state, h);
    if (initial_length && grow(l, initial_length)) {
        memcpy(l->Text, initial, initial_length * sizeof(ConChar));
        l->Length = l->Cursor = initial_length;
    }
    if (l->Length) {
        /*
         * the text a read starts with is already on the screen, ending where the cursor is: the console does not write it
         * again, it finds where the line starts by going back over it. a program such as PowerShell that completes a word
         * writes the completed line itself and reads on with it.
         */
        unsigned offset = offset_of(l, l->Length);
        long width = l->Screen->Grid.Width ? l->Screen->Grid.Width : 1;
        long position = (long)l->Screen->CursorY * width + l->Screen->CursorX - (long)offset;
        if (position < 0)
            position = 0;
        l->StartY = (int)(position / width);
        l->StartX = (int)(position % width);
        l->Displayed = offset;
    }
    return l;
}

static void popup_close(ConLine *l);

void ConLineEnd(ConLine *l)
{
    if (!l)
        return;
    popup_close(l);
    CsFree(l->Text);
    CsFree(l->Exe);
    CsFree(l->Result);
    CsFree(l);
}

/* replaces the whole line, as the history keys do. */
static void set_line(ConLine *l, const ConChar *text, size_t n)
{
    if (!grow(l, n))
        return;
    if (n)
        memcpy(l->Text, text, n * sizeof(ConChar));
    l->Length = n;
    l->Cursor = n;
    redraw(l, 0);
}

static void insert_char(ConLine *l, ConChar c)
{
    if (l->Insert || l->Cursor >= l->Length) {
        if (!grow(l, l->Length + 1))
            return;
        memmove(l->Text + l->Cursor + 1, l->Text + l->Cursor, (l->Length - l->Cursor) * sizeof(ConChar));
        l->Text[l->Cursor] = c;
        l->Length++;
    } else {
        /* a surrogate pair is one character. */
        if (c >= 0xD800 && c <= 0xDBFF && l->Cursor + 1 < l->Length) {
            l->Text[l->Cursor] = c;
            l->Cursor++;
            l->Text[l->Cursor] = 0;
            redraw(l, l->Cursor - 1);
            return;
        }
        l->Text[l->Cursor] = c;
    }
    size_t from = l->Cursor;
    l->Cursor++;
    redraw(l, from);
}

static bool is_low(ConChar c)
{
    return c >= 0xDC00 && c <= 0xDFFF;
}

static void delete_range(ConLine *l, size_t from, size_t to)
{
    if (from >= to || to > l->Length)
        return;
    memmove(l->Text + from, l->Text + to, (l->Length - to) * sizeof(ConChar));
    l->Length -= to - from;
    l->Cursor = from;
    redraw(l, from);
}

/* blank, delimiter or word: the word keys skip a run of one class. */
static int char_class(ConChar c)
{
    if (c == ' ' || c == '\t')
        return 0;
    if (c < 0x80 && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
        return 1;
    return 2;
}

static size_t word_left(const ConLine *l)
{
    size_t i = l->Cursor;
    while (i > 0 && char_class(l->Text[i - 1]) == 0)
        i--;
    if (i > 0) {
        int cls = char_class(l->Text[i - 1]);
        while (i > 0 && char_class(l->Text[i - 1]) == cls)
            i--;
    }
    return i;
}

static size_t word_right(const ConLine *l)
{
    size_t i = l->Cursor;
    if (i < l->Length) {
        int cls = char_class(l->Text[i]);
        if (cls != 0)
            while (i < l->Length && char_class(l->Text[i]) == cls)
                i++;
    }
    while (i < l->Length && char_class(l->Text[i]) == 0)
        i++;
    return i;
}

static void move_cursor(ConLine *l, size_t to)
{
    l->Cursor = to;
    if (!l->Echo)
        return;
    put_cursor(l, offset_of(l, to));
}

static size_t step_left(const ConLine *l, size_t i)
{
    if (i == 0)
        return 0;
    i--;
    if (i > 0 && is_low(l->Text[i]))
        i--;
    return i;
}

static size_t step_right(const ConLine *l, size_t i)
{
    if (i >= l->Length)
        return i;
    if (l->Text[i] >= 0xD800 && l->Text[i] <= 0xDBFF && i + 1 < l->Length)
        return i + 2;
    return i + 1;
}

/*
 * ends the line, with a line ending when enter did it, which also moves the cursor to the next row. a program that
 * echoes gets its lines added to its history and run through the aliases.
 */
static void finish(ConLine *l, bool newline, uint32_t state)
{
    CsBuffer *s = l->Screen;
    if (newline) {
        /* the cursor goes to the end of the line before the line ending is written. */
        put_cursor(l, offset_of(l, l->Length));
        uint16_t crlf[2] = {'\r', '\n'};
        CsPutLegacy(s, crlf, l->Processed ? 2 : 1);
        note_scrolls(l);
        CsRender(s->Term);
    }
    size_t ending = newline ? (l->Processed ? 2 : 1) : 0;
    size_t n = l->Length + ending;
    l->Result = CsAllocate((n + 1) * sizeof(ConChar));
    if (l->Result) {
        memcpy(l->Result, l->Text, l->Length * sizeof(ConChar));
        if (newline) {
            l->Result[l->Length] = '\r';
            if (l->Processed)
                l->Result[l->Length + 1] = '\n';
        }
        l->ResultLength = n;
        l->Lines = 1;
    }
    l->Done = true;
    l->KeyState = state;
    if (newline && l->Echo) {
        ConHistoryAdd(l->State, l->Exe, l->ExeLength, l->Text, l->Length);
        ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
        if (h)
            h->JustReset = h->JustReset;
        size_t room = l->Length * 4 + 4096;
        ConChar *expanded = CsAllocate(room * sizeof(ConChar));
        size_t expanded_length = 0;
        if (expanded && ConAliasExpand(l->State, l->Exe, l->ExeLength, l->Text, l->Length, expanded, room, &expanded_length)) {
            CsFree(l->Result);
            l->Result = expanded;
            l->ResultLength = expanded_length;
            l->Lines = 0;
            for (size_t i = 0; i + 1 < expanded_length; i++)
                if (expanded[i] == '\r' && expanded[i + 1] == '\n')
                    l->Lines++;
        } else {
            CsFree(expanded);
        }
    }
}

const ConChar *ConLineText(const ConLine *l, size_t *length)
{
    *length = l->ResultLength;
    return l->Result;
}

size_t ConLineLines(const ConLine *l)
{
    return l->Lines;
}

uint32_t ConLineKeyState(const ConLine *l)
{
    return l->KeyState;
}

/* ---- history keys */

static void show_entry(ConLine *l, ExeHistory *h, long index)
{
    if (h->Count == 0) {
        h->LastDisplayed = 0;
        set_line(l, nullptr, 0);
        return;
    }
    if (index < 0)
        index = 0;
    if (index > h->Count - 1)
        index = h->Count - 1;
    h->LastDisplayed = index;
    set_line(l, h->Commands[index], h->Lengths[index]);
}

static void history_up(ConLine *l)
{
    ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
    if (!h)
        return;
    if (!h->JustReset && (h->LastDisplayed == 0 || h->LastDisplayed == h->Count))
        return;
    if (h->JustReset)
        h->JustReset = false;
    else
        h->LastDisplayed--;
    show_entry(l, h, h->LastDisplayed);
}

static void history_down(ConLine *l)
{
    ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
    if (!h || h->LastDisplayed == h->Count - 1)
        return;
    h->LastDisplayed++;
    show_entry(l, h, h->LastDisplayed);
}

static void history_first(ConLine *l)
{
    ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
    if (!h || h->LastDisplayed == 0)
        return;
    show_entry(l, h, 0);
}

static void history_last(ConLine *l)
{
    ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
    if (!h || h->LastDisplayed == h->Count - 1)
        return;
    show_entry(l, h, h->Count - 1);
}

/* the newest command that begins with the text before the cursor, searching back from the one shown. */
static void history_search(ConLine *l)
{
    ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
    if (!h || !h->Count)
        return;
    long at = h->LastDisplayed;
    if (!h->JustReset) {
        at--;
        if (at < 0)
            at = h->Count - 1;
    } else {
        h->JustReset = false;
        if (at < 0)
            at = h->Count - 1;
    }
    size_t prefix = l->Cursor;
    for (long k = 0; k < h->Count; k++) {
        long i = ((at - k) % h->Count + h->Count) % h->Count;
        if (h->Lengths[i] >= prefix && !memcmp(h->Commands[i], l->Text, prefix * sizeof(ConChar))) {
            size_t cursor = l->Cursor;
            h->LastDisplayed = i;
            set_line(l, h->Commands[i], h->Lengths[i]);
            l->Cursor = cursor > l->Length ? l->Length : cursor;
            move_cursor(l, l->Cursor);
            return;
        }
    }
}

/* the command the keys are at, which the copy keys take text from. */
static ExeHistory *current_entry(ConLine *l, const ConChar **text, size_t *length)
{
    ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
    if (!h || h->LastDisplayed < 0 || h->LastDisplayed >= h->Count)
        return nullptr;
    *text = h->Commands[h->LastDisplayed];
    *length = h->Lengths[h->LastDisplayed];
    return h;
}

/* ------------------------------------------------------------------ the history popup */

#define POPUP_WIDTH 40
#define POPUP_ROWS 10

static void popup_close(ConLine *l)
{
    if (!l->Popup)
        return;
    CsRect r = l->PopupRect;
    if (l->PopupSaved)
        CsWriteOutput(l->Screen, l->PopupSaved, &r);
    CsFree(l->PopupSaved);
    l->PopupSaved = nullptr;
    l->Popup = false;
}

/* draws the box and the commands from PopupTop on, the selected one in the colors the other way round. */
static void popup_draw(ConLine *l, const ExeHistory *h)
{
    int width = l->PopupRect.Right - l->PopupRect.Left + 1, height = l->PopupRect.Bottom - l->PopupRect.Top + 1;
    CsCharInfo *cells = CsAllocate(sizeof(CsCharInfo) * (size_t)width * (size_t)height);
    if (!cells)
        return;
    uint16_t attr = l->Screen->PopupAttr;
    uint16_t selected = (uint16_t)(((attr & 0x0F) << 4) | ((attr & 0xF0) >> 4));
    for (int i = 0; i < width * height; i++) {
        cells[i].Char = ' ';
        cells[i].Attributes = attr;
    }
    for (int x = 1; x < width - 1; x++) {
        cells[x].Char = 0x2500;
        cells[(height - 1) * width + x].Char = 0x2500;
    }
    for (int y = 1; y < height - 1; y++) {
        cells[y * width].Char = 0x2502;
        cells[y * width + width - 1].Char = 0x2502;
    }
    cells[0].Char = 0x250C;
    cells[width - 1].Char = 0x2510;
    cells[(height - 1) * width].Char = 0x2514;
    cells[(height - 1) * width + width - 1].Char = 0x2518;
    for (int row = 0; row < height - 2; row++) {
        long index = l->PopupTop + row;
        if (index >= h->Count)
            break;
        bool on = index == l->PopupSelected;
        CsCharInfo *line = cells + (size_t)(row + 1) * (size_t)width + 1;
        char number[16];
        int n = 0;
        long v = index + 1;
        char digits[12];
        int d = 0;
        do {
            digits[d++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        while (d)
            number[n++] = digits[--d];
        number[n++] = ':';
        number[n++] = ' ';
        int x = 0;
        for (int i = 0; i < n && x < width - 2; i++)
            line[x++].Char = (uint16_t)number[i];
        for (size_t i = 0; i < h->Lengths[index] && x < width - 2; i++) {
            ConChar c = h->Commands[index][i];
            line[x++].Char = c < 0x20 ? ' ' : c;
        }
        if (on)
            for (int i = 0; i < width - 2; i++)
                line[i].Attributes = selected;
    }
    CsRect r = l->PopupRect;
    CsWriteOutput(l->Screen, cells, &r);
    CsFree(cells);
}

static void popup_open(ConLine *l)
{
    ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
    if (!h || !h->Count || l->Popup)
        return;
    CsBuffer *s = l->Screen;
    int win_w = s->Window.Right - s->Window.Left + 1, win_h = s->Window.Bottom - s->Window.Top + 1;
    int width = POPUP_WIDTH < win_w - 2 ? POPUP_WIDTH : win_w - 2;
    long rows = h->Count < POPUP_ROWS ? h->Count : POPUP_ROWS;
    if (rows > win_h - 2)
        rows = win_h - 2;
    int height = (int)rows + 2;
    if (width < 12 || height < 3)
        return;
    l->PopupRect.Left = (int16_t)(s->Window.Left + (win_w - width) / 2);
    l->PopupRect.Top = (int16_t)(s->Window.Top + (win_h - height) / 2);
    l->PopupRect.Right = (int16_t)(l->PopupRect.Left + width - 1);
    l->PopupRect.Bottom = (int16_t)(l->PopupRect.Top + height - 1);
    l->PopupSaved = CsAllocate(sizeof(CsCharInfo) * (size_t)width * (size_t)height);
    if (!l->PopupSaved)
        return;
    CsRect r = l->PopupRect;
    CsReadOutput(s, l->PopupSaved, &r);
    l->Popup = true;
    l->PopupSelected = h->LastDisplayed >= 0 && h->LastDisplayed < h->Count ? h->LastDisplayed : h->Count - 1;
    l->PopupTop = l->PopupSelected - rows + 1;
    if (l->PopupTop < 0)
        l->PopupTop = 0;
    popup_draw(l, h);
}

/* the keys of the popup: the arrows and pages choose, enter runs the command, the side arrows and escape leave it. */
static ConLineResult popup_key(ConLine *l, const ConKey *key)
{
    ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
    if (!h || !h->Count) {
        popup_close(l);
        return CONLINE_MORE;
    }
    long rows = l->PopupRect.Bottom - l->PopupRect.Top - 1;
    long selected = l->PopupSelected;
    switch (key->Vk) {
    case VK_UP:
        selected--;
        break;
    case VK_DOWN:
        selected++;
        break;
    case VK_PRIOR:
        selected -= rows;
        break;
    case VK_NEXT:
        selected += rows;
        break;
    case VK_HOME:
        selected = 0;
        break;
    case VK_END:
        selected = h->Count - 1;
        break;
    case VK_ESCAPE:
        popup_close(l);
        return CONLINE_MORE;
    case VK_RETURN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_F9: {
        long pick = l->PopupSelected;
        popup_close(l);
        h->LastDisplayed = pick;
        h->JustReset = false;
        set_line(l, h->Commands[pick], h->Lengths[pick]);
        if (key->Vk == VK_RETURN) {
            l->Cursor = l->Length;
            finish(l, true, key->State);
            return CONLINE_DONE;
        }
        return CONLINE_MORE;
    }
    default:
        return CONLINE_MORE;
    }
    if (selected < 0)
        selected = 0;
    if (selected >= h->Count)
        selected = h->Count - 1;
    l->PopupSelected = selected;
    if (selected < l->PopupTop)
        l->PopupTop = selected;
    if (selected >= l->PopupTop + rows)
        l->PopupTop = selected - rows + 1;
    popup_draw(l, h);
    return CONLINE_MORE;
}

/* false when the key is a character for the line. */
static bool editing_key(const ConKey *key)
{
    bool ctrl = (key->State & (KS_LEFT_CTRL | KS_RIGHT_CTRL)) != 0;
    bool alt = (key->State & (KS_LEFT_ALT | KS_RIGHT_ALT)) != 0;
    uint16_t vk = key->Vk;
    if (!ctrl && !alt)
        return vk == VK_ESCAPE || (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE ||
               (vk >= VK_F1 && vk <= VK_F9);
    if (ctrl && !alt)
        return vk == VK_END || vk == VK_HOME || vk == VK_LEFT || vk == VK_RIGHT;
    if (alt && !ctrl)
        return vk == VK_F7 || vk == VK_F10;
    return false;
}

static ConLineResult edit_key(ConLine *l, const ConKey *key)
{
    bool ctrl = (key->State & (KS_LEFT_CTRL | KS_RIGHT_CTRL)) != 0;
    bool alt = (key->State & (KS_LEFT_ALT | KS_RIGHT_ALT)) != 0;
    const ConChar *entry;
    size_t entry_length;
    switch (key->Vk) {
    case VK_LEFT:
        move_cursor(l, ctrl ? word_left(l) : step_left(l, l->Cursor));
        break;
    case VK_RIGHT:
    case VK_F1:
        if (ctrl && key->Vk == VK_RIGHT) {
            move_cursor(l, word_right(l));
        } else if (l->Cursor >= l->Length) {
            /* at the end of the line, copy the next character of the command the keys are at. */
            if (current_entry(l, &entry, &entry_length) && l->Cursor < entry_length) {
                insert_char(l, entry[l->Cursor]);
                if (entry[l->Cursor - 1] >= 0xD800 && entry[l->Cursor - 1] <= 0xDBFF && l->Cursor < entry_length)
                    insert_char(l, entry[l->Cursor]);
            }
        } else {
            move_cursor(l, step_right(l, l->Cursor));
        }
        break;
    case VK_HOME:
        if (ctrl)
            delete_range(l, 0, l->Cursor);
        else
            move_cursor(l, 0);
        break;
    case VK_END:
        if (ctrl)
            delete_range(l, l->Cursor, l->Length);
        else
            move_cursor(l, l->Length);
        break;
    case VK_UP:
    case VK_F5:
        history_up(l);
        break;
    case VK_DOWN:
        history_down(l);
        break;
    case VK_PRIOR:
        history_first(l);
        break;
    case VK_NEXT:
        history_last(l);
        break;
    case VK_ESCAPE:
        if (l->Length)
            set_line(l, nullptr, 0);
        break;
    case VK_DELETE:
        delete_range(l, l->Cursor, step_right(l, l->Cursor));
        break;
    case VK_INSERT:
        l->Insert = !l->Insert;
        break;
    case VK_F3:
        if (current_entry(l, &entry, &entry_length) && entry_length > l->Cursor) {
            /* the text from the cursor on is replaced by the same part of the command. */
            size_t cursor = l->Cursor;
            if (grow(l, entry_length)) {
                memcpy(l->Text + cursor, entry + cursor, (entry_length - cursor) * sizeof(ConChar));
                l->Length = entry_length;
                l->Cursor = entry_length;
                redraw(l, cursor);
            }
        }
        break;
    case VK_F6:
        insert_char(l, 0x1A);
        break;
    case VK_F7:
        if (!alt) {
            popup_open(l);
        } else {
            ExeHistory *h = find_history(l->State, l->Exe, l->ExeLength);
            if (h) {
                drop_from(h, 0);
                h->LastDisplayed = -1;
                h->JustReset = true;
            }
        }
        break;
    case VK_F8:
        history_search(l);
        break;
    case VK_F10:
        if (alt) {
            static const ConChar cmd[] = {'c', 'm', 'd', '.', 'e', 'x', 'e'};
            ConAliasExpunge(l->State, cmd, 7);
        }
        break;
    default:
        break;
    }
    return CONLINE_MORE;
}

ConLineResult ConLineKey(ConLine *l, const ConKey *key)
{
    if (l->Done)
        return CONLINE_DONE;
    if (!key->Down)
        return CONLINE_MORE;
    if (l->Popup)
        return popup_key(l, key);
    ConChar c = key->Char;

    if (!l->Vt && editing_key(key) && (!c || key->Vk == VK_ESCAPE))
        return edit_key(l, key);
    if (!c)
        return CONLINE_MORE;
    /* a control character in the wake mask ends the read but not the line. */
    if (c < 32 && ((l->WakeMask >> c) & 1)) {
        if (l->Cursor < l->Length) {
            l->Length = l->Cursor;
            redraw(l, l->Cursor);
        }
        if (grow(l, l->Length + 1)) {
            l->Text[l->Length++] = c;
            l->Cursor = l->Length;
        }
        finish(l, false, key->State);
        return CONLINE_WAKEUP;
    }
    if (c == 0x0D) {
        l->Cursor = l->Length;
        finish(l, true, key->State);
        return CONLINE_DONE;
    }
    if (c == 0x0A)
        return CONLINE_MORE;
    if (c == 0x08 && l->Processed) {
        if (l->Cursor)
            delete_range(l, step_left(l, l->Cursor), l->Cursor);
        return CONLINE_MORE;
    }
    if (c == 0x7F && l->Processed) {
        if (l->Cursor)
            delete_range(l, word_left(l), l->Cursor);
        return CONLINE_MORE;
    }
    insert_char(l, c);
    return CONLINE_MORE;
}
