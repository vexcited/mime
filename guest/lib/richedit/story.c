/*
 * the story: the text, which always ends with a paragraph mark, a
 * character format and a paragraph format index per character, the
 * format tables, replacing ranges with undo records, formatting ranges,
 * word breaks and the detection of urls.
 */
#include "richedit_private.h"

void *ReAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *ReRealloc(void *p, SIZE_T size)
{
    if (!p)
        return ReAlloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, size ? size : 1);
}

void ReFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static void *dup(const void *p, SIZE_T size)
{
    void *q = ReAlloc(size);
    if (q && p && size)
        memcpy(q, p, size);
    return q;
}

/* ------------------------------------------------------------------ formats */

void ReDefaultFormat(RE *e, HFONT font, RE_FMT *out)
{
    memset(out, 0, sizeof(*out));
    CHARFORMAT2W *cf = &out->cf;
    cf->cbSize = sizeof(CHARFORMAT2W);
    cf->dwMask = CFM_ALL2;
    HFONT h = font ? font : (HFONT)GetStockObject(SYSTEM_FONT);
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    if (!GetObjectW(h, sizeof(lf), &lf)) {
        lf.lfHeight = 16;
        lf.lfWeight = FW_BOLD;
        memcpy(lf.lfFaceName, L"System", 7 * sizeof(WCHAR));
    }
    int dpi = e->dpi ? e->dpi : 96;
    LONG twips = 1440 * lf.lfHeight / dpi;
    if (twips > 0) {
        /* a cell height: windows takes the font's internal leading off. */
        HDC dc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(dc, h);
        TEXTMETRICW tm;
        if (old && GetTextMetricsW(dc, &tm))
            twips -= 1440 * tm.tmInternalLeading / dpi;
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    } else {
        twips = -twips;
    }
    if (!twips)
        twips = 200;
    cf->yHeight = twips;
    cf->dwEffects = CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR;
    if (lf.lfWeight >= FW_BOLD)
        cf->dwEffects |= CFE_BOLD;
    if (lf.lfItalic)
        cf->dwEffects |= CFE_ITALIC;
    if (lf.lfUnderline)
        cf->dwEffects |= CFE_UNDERLINE;
    if (lf.lfStrikeOut)
        cf->dwEffects |= CFE_STRIKEOUT;
    cf->wWeight = (WORD)(lf.lfWeight ? lf.lfWeight : FW_NORMAL);
    cf->bCharSet = lf.lfCharSet;
    cf->bPitchAndFamily = lf.lfPitchAndFamily;
    memcpy(cf->szFaceName, lf.lfFaceName, sizeof(cf->szFaceName));
    cf->szFaceName[LF_FACESIZE - 1] = 0;
    cf->lcid = GetSystemDefaultLCID();
}

WORD ReAddFormat(RE *e, const RE_FMT *f)
{
    RE_FMT key;
    memset(&key, 0, sizeof(key));
    memcpy(&key.cf, &f->cf, sizeof(key.cf));
    key.cf.cbSize = sizeof(CHARFORMAT2W);
    key.cf.dwMask = CFM_ALL2;
    key.autolink = f->autolink;
    for (UINT i = 0; i < e->fmt_count; i++)
        if (!memcmp(&e->fmts[i], &key, sizeof(key)))
            return (WORD)i;
    if (e->fmt_count >= 0xFFFF)
        return 0;
    if (e->fmt_count == e->fmt_cap) {
        UINT cap = e->fmt_cap ? e->fmt_cap * 2 : 16;
        RE_FMT *fmts = ReRealloc(e->fmts, cap * sizeof(RE_FMT));
        RE_FONT *fonts = ReRealloc(e->fonts, cap * sizeof(RE_FONT));
        if (fmts)
            e->fmts = fmts;
        if (fonts)
            e->fonts = fonts;
        if (!fmts || !fonts)
            return 0;
        e->fmt_cap = cap;
    }
    memcpy(&e->fmts[e->fmt_count], &key, sizeof(key));
    memset(&e->fonts[e->fmt_count], 0, sizeof(RE_FONT));
    return (WORD)e->fmt_count++;
}

WORD ReAddPara(RE *e, const PARAFORMAT2 *p)
{
    PARAFORMAT2 key;
    memcpy(&key, p, sizeof(key));
    key.cbSize = sizeof(PARAFORMAT2);
    key.dwMask = PFM_ALL2;
    for (int i = key.cTabCount; i < MAX_TAB_STOPS; i++)
        key.rgxTabs[i] = 0;
    for (UINT i = 0; i < e->para_count; i++)
        if (!memcmp(&e->paras[i], &key, sizeof(key)))
            return (WORD)i;
    if (e->para_count >= 0xFFFF)
        return 0;
    if (e->para_count == e->para_cap) {
        UINT cap = e->para_cap ? e->para_cap * 2 : 8;
        PARAFORMAT2 *paras = ReRealloc(e->paras, cap * sizeof(PARAFORMAT2));
        if (!paras)
            return 0;
        e->paras = paras;
        e->para_cap = cap;
    }
    memcpy(&e->paras[e->para_count], &key, sizeof(key));
    return (WORD)e->para_count++;
}

void ReApplyCharFormat(CHARFORMAT2W *d, const CHARFORMAT2W *s, DWORD mask)
{
    static const DWORD effects = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_PROTECTED | CFM_LINK |
                                 CFM_SMALLCAPS | CFM_ALLCAPS | CFM_HIDDEN | CFM_OUTLINE | CFM_SHADOW | CFM_EMBOSS |
                                 CFM_IMPRINT | CFM_DISABLED | CFM_REVISED | CFM_SUBSCRIPT;
    DWORD bits = mask & effects;
    d->dwEffects = (d->dwEffects & ~bits) | (s->dwEffects & bits);
    if (mask & CFM_BOLD)
        d->wWeight = (s->dwEffects & CFE_BOLD) ? FW_BOLD : FW_NORMAL;
    if ((mask & CFM_SUBSCRIPT) == CFM_SUBSCRIPT && (s->dwEffects & CFE_SUBSCRIPT) && (s->dwEffects & CFE_SUPERSCRIPT))
        d->dwEffects &= ~CFE_SUPERSCRIPT;
    if (mask & CFM_SIZE)
        d->yHeight = s->yHeight;
    if (mask & CFM_OFFSET)
        d->yOffset = s->yOffset;
    if (mask & CFM_COLOR) {
        d->crTextColor = s->crTextColor;
        d->dwEffects = (d->dwEffects & ~CFE_AUTOCOLOR) | (s->dwEffects & CFE_AUTOCOLOR);
    }
    if (mask & CFM_BACKCOLOR) {
        d->crBackColor = s->crBackColor;
        d->dwEffects = (d->dwEffects & ~CFE_AUTOBACKCOLOR) | (s->dwEffects & CFE_AUTOBACKCOLOR);
    }
    if (mask & CFM_FACE) {
        memcpy(d->szFaceName, s->szFaceName, sizeof(d->szFaceName));
        d->szFaceName[LF_FACESIZE - 1] = 0;
        d->bPitchAndFamily = s->bPitchAndFamily;
    }
    if (mask & CFM_CHARSET)
        d->bCharSet = s->bCharSet;
    if (mask & CFM_WEIGHT) {
        d->wWeight = s->wWeight;
        d->dwEffects = (d->dwEffects & ~CFE_BOLD) | (s->wWeight >= FW_SEMIBOLD ? CFE_BOLD : 0);
    }
    if (mask & CFM_SPACING)
        d->sSpacing = s->sSpacing;
    if (mask & CFM_LCID)
        d->lcid = s->lcid;
    if (mask & CFM_COOKIE)
        d->dwReserved = s->dwReserved;
    if (mask & CFM_STYLE)
        d->sStyle = s->sStyle;
    if (mask & CFM_KERNING)
        d->wKerning = s->wKerning;
    if (mask & CFM_UNDERLINETYPE) {
        d->bUnderlineType = s->bUnderlineType;
        d->bUnderlineColor = s->bUnderlineColor;
    }
    if (mask & CFM_ANIMATION)
        d->bAnimation = s->bAnimation;
    if (mask & CFM_REVAUTHOR)
        d->bRevAuthor = s->bRevAuthor;
    if (d->wWeight == 0)
        d->wWeight = FW_NORMAL;
}

void ReApplyParaFormat(PARAFORMAT2 *d, const PARAFORMAT2 *s, DWORD mask)
{
    if (mask & PFM_STARTINDENT)
        d->dxStartIndent = s->dxStartIndent;
    else if (mask & PFM_OFFSETINDENT)
        d->dxStartIndent += s->dxStartIndent;
    if (mask & PFM_RIGHTINDENT)
        d->dxRightIndent = s->dxRightIndent;
    if (mask & PFM_OFFSET)
        d->dxOffset = s->dxOffset;
    if (mask & PFM_ALIGNMENT)
        d->wAlignment = s->wAlignment;
    if (mask & PFM_TABSTOPS) {
        int n = s->cTabCount;
        if (n < 0)
            n = 0;
        if (n > MAX_TAB_STOPS)
            n = MAX_TAB_STOPS;
        d->cTabCount = (SHORT)n;
        memset(d->rgxTabs, 0, sizeof(d->rgxTabs));
        memcpy(d->rgxTabs, s->rgxTabs, n * sizeof(LONG));
    }
    if (mask & PFM_NUMBERING)
        d->wNumbering = s->wNumbering;
    if (mask & PFM_SPACEBEFORE)
        d->dySpaceBefore = s->dySpaceBefore;
    if (mask & PFM_SPACEAFTER)
        d->dySpaceAfter = s->dySpaceAfter;
    if (mask & PFM_LINESPACING) {
        d->dyLineSpacing = s->dyLineSpacing;
        d->bLineSpacingRule = s->bLineSpacingRule;
    }
    if (mask & PFM_STYLE)
        d->sStyle = s->sStyle;
    if (mask & PFM_SHADING) {
        d->wShadingWeight = s->wShadingWeight;
        d->wShadingStyle = s->wShadingStyle;
    }
    if (mask & PFM_NUMBERINGSTART)
        d->wNumberingStart = s->wNumberingStart;
    if (mask & PFM_NUMBERINGSTYLE)
        d->wNumberingStyle = s->wNumberingStyle;
    if (mask & PFM_NUMBERINGTAB)
        d->wNumberingTab = s->wNumberingTab;
    if (mask & PFM_BORDER) {
        d->wBorderSpace = s->wBorderSpace;
        d->wBorderWidth = s->wBorderWidth;
        d->wBorders = s->wBorders;
    }
    if (mask & PFM_OUTLINELEVEL)
        d->bOutlineLevel = s->bOutlineLevel;
    WORD bits = (WORD)((mask & PFM_EFFECTS) >> 16);
    d->wEffects = (WORD)((d->wEffects & ~bits) | (s->wEffects & bits));
}

/* what is uniform across a range of formats, as EM_GETCHARFORMAT reports. */
static DWORD compare_formats(const RE_FMT *a, const RE_FMT *b)
{
    DWORD differ = 0;
    DWORD ea = a->cf.dwEffects | (a->autolink ? CFE_LINK : 0);
    DWORD eb = b->cf.dwEffects | (b->autolink ? CFE_LINK : 0);
    DWORD x = ea ^ eb;
    differ |= x & (CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_PROTECTED | CFM_LINK | CFM_SMALLCAPS |
                   CFM_ALLCAPS | CFM_HIDDEN | CFM_OUTLINE | CFM_SHADOW | CFM_EMBOSS | CFM_IMPRINT | CFM_DISABLED |
                   CFM_REVISED);
    if (x & (CFE_SUBSCRIPT | CFE_SUPERSCRIPT))
        differ |= CFM_SUBSCRIPT;
    if (a->cf.yHeight != b->cf.yHeight)
        differ |= CFM_SIZE;
    if (a->cf.yOffset != b->cf.yOffset)
        differ |= CFM_OFFSET;
    if ((x & CFE_AUTOCOLOR) || (!(ea & CFE_AUTOCOLOR) && a->cf.crTextColor != b->cf.crTextColor))
        differ |= CFM_COLOR;
    if ((x & CFE_AUTOBACKCOLOR) || (!(ea & CFE_AUTOBACKCOLOR) && a->cf.crBackColor != b->cf.crBackColor))
        differ |= CFM_BACKCOLOR;
    if (lstrcmpiW(a->cf.szFaceName, b->cf.szFaceName))
        differ |= CFM_FACE;
    if (a->cf.bCharSet != b->cf.bCharSet)
        differ |= CFM_CHARSET;
    if (a->cf.wWeight != b->cf.wWeight)
        differ |= CFM_WEIGHT;
    if (a->cf.sSpacing != b->cf.sSpacing)
        differ |= CFM_SPACING;
    if (a->cf.lcid != b->cf.lcid)
        differ |= CFM_LCID;
    if (a->cf.sStyle != b->cf.sStyle)
        differ |= CFM_STYLE;
    if (a->cf.wKerning != b->cf.wKerning)
        differ |= CFM_KERNING;
    if (a->cf.bUnderlineType != b->cf.bUnderlineType)
        differ |= CFM_UNDERLINETYPE;
    if (a->cf.bAnimation != b->cf.bAnimation)
        differ |= CFM_ANIMATION;
    if (a->cf.bRevAuthor != b->cf.bRevAuthor)
        differ |= CFM_REVAUTHOR;
    if (a->cf.dwReserved != b->cf.dwReserved)
        differ |= CFM_COOKIE;
    return differ;
}

DWORD ReRangeCharFormat(RE *e, UINT from, UINT to, CHARFORMAT2W *out)
{
    if (to > e->len)
        to = e->len;
    WORD first = from < to ? e->cfi[from] : ReInsertFormat(e);
    const RE_FMT *f = &e->fmts[first];
    memcpy(out, &f->cf, sizeof(*out));
    if (f->autolink)
        out->dwEffects |= CFE_LINK;
    DWORD differ = 0;
    WORD last = first;
    for (UINT i = from + 1; i < to; i++) {
        WORD k = e->cfi[i];
        if (k == last || k == first)
            continue;
        last = k;
        differ |= compare_formats(f, &e->fmts[k]);
    }
    return CFM_ALL2 & ~differ;
}

DWORD ReRangeParaFormat(RE *e, UINT from, UINT to, PARAFORMAT2 *out)
{
    UINT ps = ReParaStart(e, from);
    UINT pe = ReParaEnd(e, to > from ? to - 1 : from);
    const PARAFORMAT2 *p = &e->paras[e->pfi[ReParaEnd(e, ps)]];
    memcpy(out, p, sizeof(*out));
    DWORD mask = PFM_ALL2;
    for (UINT i = ps; i <= pe && i < e->len; i++) {
        if (e->text[i] != RE_CR)
            continue;
        const PARAFORMAT2 *q = &e->paras[e->pfi[i]];
        if (q == p)
            continue;
        if (q->dxStartIndent != p->dxStartIndent)
            mask &= ~(PFM_STARTINDENT | PFM_OFFSETINDENT);
        if (q->dxRightIndent != p->dxRightIndent)
            mask &= ~PFM_RIGHTINDENT;
        if (q->dxOffset != p->dxOffset)
            mask &= ~PFM_OFFSET;
        if (q->wAlignment != p->wAlignment)
            mask &= ~PFM_ALIGNMENT;
        if (q->cTabCount != p->cTabCount || memcmp(q->rgxTabs, p->rgxTabs, sizeof(q->rgxTabs)))
            mask &= ~PFM_TABSTOPS;
        if (q->wNumbering != p->wNumbering)
            mask &= ~PFM_NUMBERING;
        if (q->dySpaceBefore != p->dySpaceBefore)
            mask &= ~PFM_SPACEBEFORE;
        if (q->dySpaceAfter != p->dySpaceAfter)
            mask &= ~PFM_SPACEAFTER;
        if (q->dyLineSpacing != p->dyLineSpacing || q->bLineSpacingRule != p->bLineSpacingRule)
            mask &= ~PFM_LINESPACING;
        if (q->sStyle != p->sStyle)
            mask &= ~PFM_STYLE;
        if (q->wShadingWeight != p->wShadingWeight || q->wShadingStyle != p->wShadingStyle)
            mask &= ~PFM_SHADING;
        if (q->wNumberingStart != p->wNumberingStart)
            mask &= ~PFM_NUMBERINGSTART;
        if (q->wNumberingStyle != p->wNumberingStyle)
            mask &= ~PFM_NUMBERINGSTYLE;
        if (q->wNumberingTab != p->wNumberingTab)
            mask &= ~PFM_NUMBERINGTAB;
        if (q->wBorders != p->wBorders || q->wBorderWidth != p->wBorderWidth || q->wBorderSpace != p->wBorderSpace)
            mask &= ~PFM_BORDER;
        mask &= ~((DWORD)(q->wEffects ^ p->wEffects) << 16);
    }
    return mask;
}

/* ------------------------------------------------------------------ the text */

UINT ReTextLength(RE *e)
{
    return e->len - 1;
}

WORD ReFormatAt(RE *e, UINT cp)
{
    if (cp >= e->len)
        cp = e->len - 1;
    return e->cfi[cp];
}

static BOOL is_break(WCHAR c)
{
    return c == RE_CR || c == RE_VT || c == RE_FF;
}

/* the format typing takes: the character before the caret's, or the one after it at a paragraph's start. */
WORD ReInsertFormat(RE *e)
{
    if (e->ins_valid)
        return e->ins_cf;
    UINT cp = ReSelMin(e);
    WORD k = (cp > 0 && e->text[cp - 1] != RE_CR) ? e->cfi[cp - 1] : e->cfi[cp < e->len ? cp : e->len - 1];
    if (e->fmts[k].autolink) {
        RE_FMT f;
        memcpy(&f, &e->fmts[k], sizeof(f));
        f.autolink = 0;
        k = ReAddFormat(e, &f);
    }
    return k;
}

UINT ReParaStart(RE *e, UINT cp)
{
    if (cp > e->len)
        cp = e->len;
    while (cp > 0 && e->text[cp - 1] != RE_CR)
        cp--;
    return cp;
}

UINT ReParaEnd(RE *e, UINT cp)
{
    if (cp >= e->len)
        return e->len - 1;
    while (cp < e->len - 1 && e->text[cp] != RE_CR)
        cp++;
    return cp;
}

static BOOL reserve(RE *e, UINT length)
{
    if (length + 1 <= e->cap)
        return TRUE;
    UINT cap = e->cap ? e->cap : 64;
    while (cap < length + 1)
        cap *= 2;
    WCHAR *text = ReRealloc(e->text, cap * sizeof(WCHAR));
    if (!text)
        return FALSE;
    e->text = text;
    WORD *cfi = ReRealloc(e->cfi, cap * sizeof(WORD));
    if (!cfi)
        return FALSE;
    e->cfi = cfi;
    WORD *pfi = ReRealloc(e->pfi, cap * sizeof(WORD));
    if (!pfi)
        return FALSE;
    e->pfi = pfi;
    e->cap = cap;
    return TRUE;
}

/* ------------------------------------------------------------------ undo */

static void free_record(RE_UNDO *u)
{
    ReFree(u->old_text);
    ReFree(u->new_text);
    ReFree(u->old_cf);
    ReFree(u->new_cf);
    ReFree(u->old_pf);
    ReFree(u->new_pf);
    memset(u, 0, sizeof(*u));
}

static void clear_stack(RE_UNDO *stack, UINT *count)
{
    for (UINT i = 0; i < *count; i++)
        free_record(&stack[i]);
    *count = 0;
}

void ReClearUndo(RE *e)
{
    clear_stack(e->undo, &e->undo_count);
    clear_stack(e->redo, &e->redo_count);
    e->typing_group = FALSE;
}

void ReStopGrouping(RE *e)
{
    e->typing_group = FALSE;
}

static BOOL push(RE_UNDO **stack, UINT *count, UINT *cap, UINT limit, RE_UNDO *u)
{
    if (!limit) {
        free_record(u);
        return FALSE;
    }
    if (*count >= limit) {
        free_record(&(*stack)[0]);
        memmove(&(*stack)[0], &(*stack)[1], (*count - 1) * sizeof(RE_UNDO));
        (*count)--;
    }
    if (*count == *cap) {
        UINT n = *cap ? *cap * 2 : 16;
        RE_UNDO *s = ReRealloc(*stack, n * sizeof(RE_UNDO));
        if (!s) {
            free_record(u);
            return FALSE;
        }
        *stack = s;
        *cap = n;
    }
    (*stack)[(*count)++] = *u;
    return TRUE;
}

static BOOL append(WCHAR **text, WORD **cf, WORD **pf, UINT len, const WCHAR *t, const WORD *c, const WORD *p, UINT n,
                   BOOL front)
{
    WCHAR *nt = ReAlloc((len + n) * sizeof(WCHAR));
    WORD *nc = ReAlloc((len + n) * sizeof(WORD));
    WORD *np = ReAlloc((len + n) * sizeof(WORD));
    if (!nt || !nc || !np) {
        ReFree(nt);
        ReFree(nc);
        ReFree(np);
        return FALSE;
    }
    UINT a = front ? n : 0, b = front ? 0 : len;
    if (len) {
        memcpy(nt + a, *text, len * sizeof(WCHAR));
        memcpy(nc + a, *cf, len * sizeof(WORD));
        memcpy(np + a, *pf, len * sizeof(WORD));
    }
    memcpy(nt + b, t, n * sizeof(WCHAR));
    memcpy(nc + b, c, n * sizeof(WORD));
    memcpy(np + b, p, n * sizeof(WORD));
    ReFree(*text);
    ReFree(*cf);
    ReFree(*pf);
    *text = nt;
    *cf = nc;
    *pf = np;
    return TRUE;
}

/* folds typing and deleting into the record before it, as one undo step. */
static BOOL merge(RE *e, UINT cp, UINT count, const WCHAR *s, UINT n, const WORD *cf, const WORD *pf, int kind)
{
    if (!e->typing_group || !e->undo_count)
        return FALSE;
    RE_UNDO *u = &e->undo[e->undo_count - 1];
    if (u->kind != kind)
        return FALSE;
    if (kind == UNDO_TYPING && !count && cp == u->cp + u->new_len) {
        if (!append(&u->new_text, &u->new_cf, &u->new_pf, u->new_len, s, cf, pf, n, FALSE))
            return FALSE;
        u->new_len += n;
        return TRUE;
    }
    if (kind == UNDO_DELETE && !n && !u->new_len) {
        if (cp + count == u->cp) {
            if (!append(&u->old_text, &u->old_cf, &u->old_pf, u->old_len, e->text + cp, e->cfi + cp, e->pfi + cp,
                        count, TRUE))
                return FALSE;
            u->cp = cp;
            u->old_len += count;
            return TRUE;
        }
        if (cp == u->cp) {
            if (!append(&u->old_text, &u->old_cf, &u->old_pf, u->old_len, e->text + cp, e->cfi + cp, e->pfi + cp,
                        count, FALSE))
                return FALSE;
            u->old_len += count;
            return TRUE;
        }
    }
    return FALSE;
}

static void record(RE *e, UINT cp, UINT count, const WCHAR *s, UINT n, const WORD *cf, const WORD *pf, int kind)
{
    clear_stack(e->redo, &e->redo_count);
    if (!e->undo_limit)
        return;
    if ((kind == UNDO_TYPING || kind == UNDO_DELETE) && merge(e, cp, count, s, n, cf, pf, kind))
        return;
    RE_UNDO u;
    memset(&u, 0, sizeof(u));
    u.cp = cp;
    u.old_len = count;
    u.new_len = n;
    u.old_text = dup(e->text + cp, count * sizeof(WCHAR));
    u.old_cf = dup(e->cfi + cp, count * sizeof(WORD));
    u.old_pf = dup(e->pfi + cp, count * sizeof(WORD));
    u.new_text = dup(s, n * sizeof(WCHAR));
    u.new_cf = dup(cf, n * sizeof(WORD));
    u.new_pf = dup(pf, n * sizeof(WORD));
    u.sel_anchor = e->anchor;
    u.sel_active = e->active;
    u.kind = kind;
    if (!u.old_text || !u.old_cf || !u.old_pf || !u.new_text || !u.new_cf || !u.new_pf) {
        free_record(&u);
        return;
    }
    push(&e->undo, &e->undo_count, &e->undo_cap, e->undo_limit, &u);
    e->typing_group = kind == UNDO_TYPING || kind == UNDO_DELETE;
}

/* ------------------------------------------------------------------ replacing */

static UINT shift(UINT p, UINT cp, UINT count, UINT n)
{
    if (p >= cp + count)
        return p - count + n;
    if (p > cp)
        return cp + n;
    return p;
}

static BOOL replace_raw(RE *e, UINT cp, UINT count, const WCHAR *s, UINT n, const WORD *cf, const WORD *pf)
{
    if (!reserve(e, e->len - count + n))
        return FALSE;
    UINT tail = e->len - cp - count;
    memmove(e->text + cp + n, e->text + cp + count, tail * sizeof(WCHAR));
    memmove(e->cfi + cp + n, e->cfi + cp + count, tail * sizeof(WORD));
    memmove(e->pfi + cp + n, e->pfi + cp + count, tail * sizeof(WORD));
    memcpy(e->text + cp, s, n * sizeof(WCHAR));
    memcpy(e->cfi + cp, cf, n * sizeof(WORD));
    memcpy(e->pfi + cp, pf, n * sizeof(WORD));
    e->len = e->len - count + n;
    e->text[e->len] = 0;
    e->anchor = shift(e->anchor, cp, count, n);
    e->active = shift(e->active, cp, count, n);
    e->ins_valid = FALSE;
    e->modified = TRUE;
    e->pend_change = TRUE;
    ReInvalidateLayout(e);
    return TRUE;
}

/* replaces [cp, cp + count) with n characters of formats cf; paragraph marks take the paragraph's format. */
BOOL ReReplace(RE *e, UINT cp, UINT count, const WCHAR *s, UINT n, const WORD *cf, const WORD *pf, int undo_kind)
{
    if (cp > e->len - 1)
        cp = e->len - 1;
    if (cp + count > e->len - 1)
        count = e->len - 1 - cp;
    if (!count && !n)
        return TRUE;
    WORD *own = nullptr;
    if (!pf) {
        own = ReAlloc((n + 1) * sizeof(WORD));
        if (!own)
            return FALSE;
        WORD p = e->pfi[ReParaEnd(e, cp + count)];
        for (UINT i = 0; i < n; i++)
            own[i] = p;
        pf = own;
    }
    if (undo_kind != UNDO_NONE)
        record(e, cp, count, s, n, cf, pf, undo_kind);
    else
        e->typing_group = FALSE;
    BOOL ok = replace_raw(e, cp, count, s, n, cf, pf);
    ReFree(own);
    return ok;
}

BOOL ReReplaceText(RE *e, UINT cp, UINT count, const WCHAR *s, UINT n, WORD cf, int undo_kind)
{
    WORD *cfs = ReAlloc((n + 1) * sizeof(WORD));
    if (!cfs)
        return FALSE;
    for (UINT i = 0; i < n; i++)
        cfs[i] = cf;
    BOOL ok = ReReplace(e, cp, count, s, n, cfs, nullptr, undo_kind);
    ReFree(cfs);
    return ok;
}

BOOL ReUndo(RE *e, BOOL redo)
{
    RE_UNDO *stack = redo ? e->redo : e->undo;
    UINT *count = redo ? &e->redo_count : &e->undo_count;
    if (!*count)
        return FALSE;
    RE_UNDO u = stack[--*count];
    UINT cp = u.cp;
    if (cp + u.new_len > e->len - 1) {
        free_record(&u);
        return FALSE;
    }
    replace_raw(e, cp, u.new_len, u.old_text, u.old_len, u.old_cf, u.old_pf);
    RE_UNDO inverse = u;
    inverse.old_len = u.new_len;
    inverse.new_len = u.old_len;
    inverse.old_text = u.new_text;
    inverse.new_text = u.old_text;
    inverse.old_cf = u.new_cf;
    inverse.new_cf = u.old_cf;
    inverse.old_pf = u.new_pf;
    inverse.new_pf = u.old_pf;
    if (redo)
        push(&e->undo, &e->undo_count, &e->undo_cap, e->undo_limit ? e->undo_limit : 1, &inverse);
    else
        push(&e->redo, &e->redo_count, &e->redo_cap, e->undo_limit ? e->undo_limit : 1, &inverse);
    e->typing_group = FALSE;
    if (u.kind & UNDO_FORMAT)
        e->anchor = cp, e->active = cp + u.old_len;
    else if (u.old_len)
        e->anchor = cp, e->active = cp + u.old_len;
    else
        e->anchor = e->active = cp;
    return TRUE;
}

/* ------------------------------------------------------------------ formatting ranges */

void ReSetFormatRange(RE *e, UINT from, UINT to, const CHARFORMAT2W *cf, DWORD mask, BOOL undo)
{
    if (to > e->len)
        to = e->len;
    if (from >= to)
        return;
    WORD *old = undo ? dup(e->cfi + from, (to - from) * sizeof(WORD)) : nullptr;
    WORD prev = 0xFFFF, mapped = 0;
    BOOL changed = FALSE;
    for (UINT i = from; i < to; i++) {
        WORD k = e->cfi[i];
        if (k != prev) {
            RE_FMT f;
            memcpy(&f, &e->fmts[k], sizeof(f));
            ReApplyCharFormat(&f.cf, cf, mask);
            prev = k;
            mapped = ReAddFormat(e, &f);
        }
        if (e->cfi[i] != mapped) {
            e->cfi[i] = mapped;
            changed = TRUE;
        }
    }
    if (changed && old && e->undo_limit) {
        clear_stack(e->redo, &e->redo_count);
        RE_UNDO u;
        memset(&u, 0, sizeof(u));
        u.cp = from;
        u.old_len = u.new_len = to - from;
        u.old_text = dup(e->text + from, u.old_len * sizeof(WCHAR));
        u.new_text = dup(e->text + from, u.old_len * sizeof(WCHAR));
        u.old_cf = old;
        old = nullptr;
        u.new_cf = dup(e->cfi + from, u.old_len * sizeof(WORD));
        u.old_pf = dup(e->pfi + from, u.old_len * sizeof(WORD));
        u.new_pf = dup(e->pfi + from, u.old_len * sizeof(WORD));
        u.kind = UNDO_FORMAT;
        if (u.old_text && u.new_text && u.new_cf && u.old_pf && u.new_pf)
            push(&e->undo, &e->undo_count, &e->undo_cap, e->undo_limit, &u);
        else
            free_record(&u);
        e->typing_group = FALSE;
    }
    ReFree(old);
    if (changed) {
        e->pend_change = TRUE;
        e->modified = TRUE;
        ReInvalidateLayout(e);
    }
}

void ReSetParaRange(RE *e, UINT from, UINT to, const PARAFORMAT2 *pf, DWORD mask, BOOL undo)
{
    UINT ps = ReParaStart(e, from);
    UINT pe = ReParaEnd(e, to > from ? to - 1 : from);
    WORD *old = undo ? dup(e->pfi + ps, (pe + 1 - ps) * sizeof(WORD)) : nullptr;
    BOOL changed = FALSE;
    UINT start = ps;
    for (UINT i = ps; i <= pe; i++) {
        if (e->text[i] != RE_CR && i != e->len - 1)
            continue;
        PARAFORMAT2 p;
        memcpy(&p, &e->paras[e->pfi[i]], sizeof(p));
        ReApplyParaFormat(&p, pf, mask);
        WORD k = ReAddPara(e, &p);
        for (UINT j = start; j <= i; j++) {
            if (e->pfi[j] != k)
                changed = TRUE;
            e->pfi[j] = k;
        }
        start = i + 1;
    }
    if (changed && old && e->undo_limit) {
        clear_stack(e->redo, &e->redo_count);
        RE_UNDO u;
        memset(&u, 0, sizeof(u));
        u.cp = ps;
        u.old_len = u.new_len = pe + 1 - ps;
        u.old_text = dup(e->text + ps, u.old_len * sizeof(WCHAR));
        u.new_text = dup(e->text + ps, u.old_len * sizeof(WCHAR));
        u.old_cf = dup(e->cfi + ps, u.old_len * sizeof(WORD));
        u.new_cf = dup(e->cfi + ps, u.old_len * sizeof(WORD));
        u.old_pf = old;
        old = nullptr;
        u.new_pf = dup(e->pfi + ps, u.old_len * sizeof(WORD));
        u.kind = UNDO_FORMAT;
        if (pe + 1 > e->len - 1)
            free_record(&u);
        else if (u.old_text && u.new_text && u.old_cf && u.new_cf && u.new_pf)
            push(&e->undo, &e->undo_count, &e->undo_cap, e->undo_limit, &u);
        else
            free_record(&u);
        e->typing_group = FALSE;
    }
    ReFree(old);
    if (changed) {
        e->pend_change = TRUE;
        e->modified = TRUE;
        ReInvalidateLayout(e);
    }
}

/* ------------------------------------------------------------------ words */

BOOL ReIsWordChar(WCHAR c)
{
    if (c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        return TRUE;
    if (c < 0x80)
        return FALSE;
    WORD type = 0;
    GetStringTypeW(CT_CTYPE1, &c, 1, &type);
    return (type & (C1_ALPHA | C1_DIGIT)) != 0;
}

static BOOL is_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == 0xA0 || c == 0x3000;
}

static int char_class(WCHAR c)
{
    if (is_break(c))
        return 3;
    if (is_space(c))
        return 0;
    if (ReIsWordChar(c))
        return 1;
    return 2;
}

/* EM_FINDWORDBREAK over the story, or the application's procedure when it set one. */
UINT ReWordBreak(RE *e, UINT cp, int code)
{
    UINT n = e->len;
    if (cp > n)
        cp = n;
    if (e->wordbreak && code != WB_CLASSIFY) {
        int r = e->wordbreak(e->text, (int)cp, (int)n, code);
        return r < 0 ? 0 : (UINT)r;
    }
    switch (code) {
    case WB_ISDELIMITER:
        return cp < n && (is_space(e->text[cp]) || is_break(e->text[cp]));
    case WB_CLASSIFY: {
        WCHAR c = cp < n ? e->text[cp] : 0;
        if (is_break(c))
            return WBF_BREAKLINE;
        if (is_space(c))
            return WBF_ISWHITE | WBF_BREAKAFTER;
        return ReIsWordChar(c) ? 0 : 1;
    }
    case WB_LEFT:
    case WB_MOVEWORDLEFT:
    case WB_LEFTBREAK: {
        while (cp > 0 && is_space(e->text[cp - 1]))
            cp--;
        if (cp > 0 && is_break(e->text[cp - 1]))
            return cp - 1;
        if (cp > 0) {
            int k = char_class(e->text[cp - 1]);
            while (cp > 0 && char_class(e->text[cp - 1]) == k)
                cp--;
        }
        return cp;
    }
    case WB_RIGHT:
    case WB_MOVEWORDRIGHT: {
        if (cp < n && is_break(e->text[cp]))
            return cp + 1;
        if (cp < n && !is_space(e->text[cp])) {
            int k = char_class(e->text[cp]);
            while (cp < n && char_class(e->text[cp]) == k)
                cp++;
        }
        while (cp < n && is_space(e->text[cp]))
            cp++;
        return cp;
    }
    case WB_RIGHTBREAK: {
        if (cp < n && !is_space(e->text[cp]) && !is_break(e->text[cp])) {
            int k = char_class(e->text[cp]);
            while (cp < n && char_class(e->text[cp]) == k)
                cp++;
        }
        return cp;
    }
    }
    return cp;
}

/* ------------------------------------------------------------------ urls */

static const struct {
    const WCHAR *prefix;
    UINT len;
} g_schemes[] = {
    {L"http:", 5}, {L"file:", 5}, {L"mailto:", 7}, {L"ftp:", 4}, {L"https:", 6}, {L"gopher:", 7}, {L"nntp:", 5},
    {L"prospero:", 9}, {L"telnet:", 7}, {L"news:", 5}, {L"wais:", 5}, {L"outlook:", 8}, {L"www.", 4}, {L"ftp.", 4},
};

static WCHAR lower(WCHAR c)
{
    return c >= 'A' && c <= 'Z' ? (WCHAR)(c + 32) : c;
}

static BOOL url_delimiter(WCHAR c)
{
    if (c < 0x20 || is_space(c) || c == '<' || c == '>' || c == '"')
        return TRUE;
    WORD type = 0;
    if (c >= 0x80) {
        GetStringTypeW(CT_CTYPE1, &c, 1, &type);
        return (type & (C1_SPACE | C1_BLANK | C1_CNTRL)) != 0;
    }
    return FALSE;
}

static UINT prefix_of(const WCHAR *t, UINT n)
{
    if (n >= 3 && t[0] == '\\' && t[1] == '\\')
        return 2;
    for (UINT k = 0; k < sizeof(g_schemes) / sizeof(g_schemes[0]); k++) {
        UINT len = g_schemes[k].len;
        if (n < len)
            continue;
        UINT i = 0;
        while (i < len && lower(t[i]) == g_schemes[k].prefix[i])
            i++;
        if (i == len)
            return len;
    }
    return 0;
}

/* the end of a url starting at i, or i when there is none. */
static UINT url_end(const WCHAR *t, UINT n, UINT i)
{
    UINT plen = prefix_of(t + i, n - i);
    if (!plen)
        return i;
    UINT j = i;
    if (i > 0 && t[i - 1] == '<') {
        /* in angle brackets a url may hold spaces, up to the bracket on the same paragraph. */
        UINT k = i;
        while (k < n && k - i < 512 && !is_break(t[k]) && t[k] != '>' && t[k] != '<')
            k++;
        if (k < n && t[k] == '>')
            j = k;
    }
    if (j == i) {
        while (j < n && !url_delimiter(t[j]))
            j++;
        while (j > i) {
            WCHAR c = t[j - 1];
            if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '\'') {
                j--;
                continue;
            }
            if (c == ')' || c == ']' || c == '}') {
                WCHAR open = c == ')' ? '(' : c == ']' ? '[' : '{';
                int depth = 0;
                for (UINT k = i; k < j; k++)
                    depth += t[k] == open ? 1 : t[k] == c ? -1 : 0;
                if (depth < 0) {
                    j--;
                    continue;
                }
            }
            break;
        }
    }
    return j > i + plen ? j : i;
}

void ReScanUrls(RE *e)
{
    UINT n = e->len;
    BYTE *want = ReAlloc(n + 1);
    if (!want)
        return;
    if (e->autourl) {
        for (UINT i = 0; i < n;) {
            BOOL start = i == 0 || url_delimiter(e->text[i - 1]) || e->text[i - 1] == '(' || e->text[i - 1] == '[' ||
                         e->text[i - 1] == '\'';
            UINT j = start ? url_end(e->text, n, i) : i;
            if (j > i) {
                memset(want + i, 1, j - i);
                i = j;
            } else {
                i++;
            }
        }
    }
    BOOL changed = FALSE;
    WORD from = 0xFFFF, to = 0;
    BYTE from_flag = 0;
    for (UINT i = 0; i < n; i++) {
        WORD k = e->cfi[i];
        if (e->fmts[k].autolink == want[i])
            continue;
        if (k != from || want[i] != from_flag) {
            RE_FMT f;
            memcpy(&f, &e->fmts[k], sizeof(f));
            f.autolink = want[i];
            from = k;
            from_flag = want[i];
            to = ReAddFormat(e, &f);
        }
        e->cfi[i] = to;
        changed = TRUE;
    }
    ReFree(want);
    if (changed)
        ReInvalidateLayout(e);
}

/* ------------------------------------------------------------------ the control's story */

RE *ReCreate(HWND hwnd, const CREATESTRUCTW *cs, BOOL ansi, BOOL v1, BOOL v41)
{
    RE *e = ReAlloc(sizeof(RE));
    if (!e)
        return nullptr;
    e->hwnd = hwnd;
    e->parent = cs->hwndParent;
    e->id = (UINT)(UINT_PTR)cs->hMenu;
    e->ansi = ansi;
    e->v1 = v1;
    e->v41 = v41;
    HDC dc = GetDC(nullptr);
    e->dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    if (e->dpi <= 0)
        e->dpi = 96;
    RE_FMT f;
    ReDefaultFormat(e, nullptr, &f);
    e->def_cf = ReAddFormat(e, &f);
    PARAFORMAT2 p;
    memset(&p, 0, sizeof(p));
    p.wAlignment = PFA_LEFT;
    e->def_pf = ReAddPara(e, &p);
    if (!e->fmts || !e->paras || !reserve(e, 1)) {
        ReDestroy(e);
        return nullptr;
    }
    e->text[0] = RE_CR;
    e->text[1] = 0;
    e->cfi[0] = e->def_cf;
    e->pfi[0] = e->def_pf;
    e->len = 1;
    e->limit = RE_LIMIT_DEFAULT;
    e->undo_limit = RE_UNDO_DEFAULT;
    e->bk = GetSysColor(COLOR_WINDOW);
    e->redraw = TRUE;
    e->dirty = TRUE;
    return e;
}

void ReDestroy(RE *e)
{
    ReClearUndo(e);
    ReFree(e->undo);
    ReFree(e->redo);
    ReFlushFonts(e);
    ReFree(e->text);
    ReFree(e->cfi);
    ReFree(e->pfi);
    ReFree(e->fmts);
    ReFree(e->fonts);
    ReFree(e->paras);
    ReFree(e->lines);
    ReFree(e->xpos);
    ReFree(e);
}
