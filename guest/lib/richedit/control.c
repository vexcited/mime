/*
 * the window procedure: creation as windows' host does it, the messages
 * of the edit control and of rich edit, ansi windows converting their
 * strings, EN_MSGFILTER, and the notifications windows defers to the end
 * of the outermost message: EN_MAXTEXT, EN_SELCHANGE, EN_ERRSPACE and
 * EN_CHANGE, in that order.
 */
#include "richedit_private.h"

static int imax(int a, int b) { return a > b ? a : b; }

static int imin(int a, int b) { return a < b ? a : b; }

static RE *re_of(HWND hwnd) { return (RE *)GetWindowLongPtrW(hwnd, 0); }

static LRESULT def_proc(RE *e, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (e ? e->ansi : FALSE)
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

UINT ReSelMin(RE *e) { return e->anchor < e->active ? e->anchor : e->active; }

UINT ReSelMax(RE *e) { return e->anchor > e->active ? e->anchor : e->active; }

void ReBeep(RE *e)
{
    (void)e;
    MessageBeep(0);
}

/* ------------------------------------------------------------------ notifications */

static HWND notify_target(RE *e)
{
    return e->parent ? e->parent : GetParent(e->hwnd);
}

void ReNotifyCommand(RE *e, UINT code)
{
    HWND parent = notify_target(e);
    if (!parent)
        return;
    UINT id = (UINT)GetWindowLongW(e->hwnd, GWL_ID);
    WPARAM w = MAKEWPARAM(id, code);
    if (IsWindowUnicode(parent))
        SendMessageW(parent, WM_COMMAND, w, (LPARAM)e->hwnd);
    else
        SendMessageA(parent, WM_COMMAND, w, (LPARAM)e->hwnd);
}

LRESULT ReNotify(RE *e, UINT code, NMHDR *hdr)
{
    HWND parent = notify_target(e);
    if (!parent)
        return 0;
    UINT id = (UINT)GetWindowLongW(e->hwnd, GWL_ID);
    hdr->hwndFrom = e->hwnd;
    hdr->idFrom = id;
    hdr->code = code;
    if (code == EN_REQUESTRESIZE) {
        /* the host moves the rectangle to where the window is in its parent. */
        REQRESIZE *rr = (REQRESIZE *)hdr;
        RECT wr;
        GetWindowRect(e->hwnd, &wr);
        MapWindowPoints(nullptr, parent, (POINT *)&wr, 2);
        rr->rc.right += wr.left;
        rr->rc.bottom += wr.top;
        rr->rc.left = wr.left;
        rr->rc.top = wr.top;
    }
    LRESULT r = IsWindowUnicode(parent) ? SendMessageW(parent, WM_NOTIFY, id, (LPARAM)hdr)
                                        : SendMessageA(parent, WM_NOTIFY, id, (LPARAM)hdr);
    return r != 0;
}

void ReSelChanged(RE *e)
{
    (void)e;
}

void ReChanged(RE *e)
{
    e->pend_change = TRUE;
}

static void flush(RE *e)
{
    BOOL change = e->pend_change, maxtext = e->pend_maxtext, errspace = e->pend_errspace;
    e->pend_change = e->pend_maxtext = e->pend_errspace = FALSE;
    if (change)
        ReScanUrls(e);
    if (e->dirty) {
        ReLayout(e);
        ReInvalidate(e);
        RePlaceCaret(e);
    }
    if (maxtext)
        ReNotifyCommand(e, EN_MAXTEXT);
    LONG smin = (LONG)ReSelMin(e), smax = (LONG)ReSelMax(e);
    if ((smin != e->sent_min || smax != e->sent_max) && !e->in_selchange) {
        e->sent_min = smin;
        e->sent_max = smax;
        if (e->mask & ENM_SELCHANGE) {
            SELCHANGE sc;
            memset(&sc, 0, sizeof(sc));
            sc.chrg.cpMin = smin;
            sc.chrg.cpMax = smax;
            sc.seltyp = smin == smax ? SEL_EMPTY : (SEL_TEXT | (smax - smin > 1 ? SEL_MULTICHAR : 0));
            e->in_selchange = TRUE;
            ReNotify(e, EN_SELCHANGE, &sc.nmhdr);
            e->in_selchange = FALSE;
        }
    }
    if (errspace)
        ReNotifyCommand(e, EN_ERRSPACE);
    if (change) {
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, e->hwnd, OBJID_CLIENT, 0);
        if ((e->mask & ENM_CHANGE) && !e->in_change) {
            e->in_change = TRUE;
            ReNotifyCommand(e, EN_CHANGE);
            e->in_change = FALSE;
        }
    }
}

/* ------------------------------------------------------------------ the selection */

/* as windows' range clamps: both ends within the story, an empty one not past the final mark. */
void ReSetSelection(RE *e, UINT anchor, UINT active)
{
    UINT len = e->len;
    if (anchor > len)
        anchor = len;
    if (active > len)
        active = len;
    if (anchor == active && active > len - 1)
        anchor = active = len - 1;
    BOOL was = e->anchor != e->active, now = anchor != active;
    BOOL moved = anchor != e->anchor || active != e->active;
    e->anchor = anchor;
    e->active = active;
    e->at_line_end = FALSE;
    e->caret_x_valid = FALSE;
    if (moved) {
        e->ins_valid = FALSE;
        ReStopGrouping(e);
        if (was || now)
            ReInvalidate(e);
    }
    ReScrollCaret(e);
    RePlaceCaret(e);
}

/* EM_SETSEL and EM_EXSETSEL; returns the larger end. */
static LRESULT set_sel(RE *e, LONG start, LONG end)
{
    UINT anchor, active;
    if (start < 0) {
        anchor = active = end < 0 ? 0x3FFFFFFF : e->active;
    } else {
        anchor = (UINT)start;
        active = end < 0 ? 0x3FFFFFFF : (UINT)end;
    }
    if (e->v1 && active < anchor) {
        UINT t = anchor;
        anchor = active;
        active = t;
    }
    ReSetSelection(e, anchor, active);
    return ReSelMax(e);
}

/* ------------------------------------------------------------------ strings */

static WCHAR *to_wide(const char *s, int n, UINT cp, UINT *count)
{
    if (!s) {
        *count = 0;
        return ReAlloc(sizeof(WCHAR));
    }
    if (n < 0)
        n = lstrlenA(s);
    int w = n ? MultiByteToWideChar(cp, 0, s, n, nullptr, 0) : 0;
    WCHAR *out = ReAlloc(((SIZE_T)w + 1) * sizeof(WCHAR));
    if (!out)
        return nullptr;
    if (w)
        MultiByteToWideChar(cp, 0, s, n, out, w);
    *count = (UINT)w;
    return out;
}

static BOOL is_rtf(const char *s)
{
    if (!s || s[0] != '{' || s[1] != '\\')
        return FALSE;
    const char *p = s + 2;
    if (*p == 'u')
        p++;
    return p[0] == 'r' && p[1] == 't' && p[2] == 'f';
}

/* turns crlf and lone line feeds into paragraph marks; a single line control keeps the first line. */
static UINT cleanse(RE *e, WCHAR *s, UINT n)
{
    UINT k = 0;
    for (UINT i = 0; i < n; i++) {
        WCHAR c = s[i];
        if (c == '\r' && i + 1 < n && s[i + 1] == '\n')
            i++;
        if (c == '\n')
            c = '\r';
        if (c == '\r' && !e->multiline)
            break;
        if (c == 0)
            break;
        s[k++] = c;
    }
    return k;
}

/* SetText: the whole story or the selection, plain or rtf; returns 1 or the characters inserted. */
static LRESULT set_text(RE *e, DWORD flags, UINT cp, const void *data)
{
    BOOL sel = (flags & ST_SELECTION) != 0;
    UINT from = sel ? ReSelMin(e) : 0;
    UINT to = sel ? ReSelMax(e) : e->len - 1;
    if (to > e->len - 1)
        to = e->len - 1;
    if (!(flags & ST_KEEPUNDO))
        ReClearUndo(e);
    int undo = (flags & ST_KEEPUNDO) ? UNDO_PASTE : UNDO_NONE;
    LONG inserted = 0;
    if (cp != 1200 && data && is_rtf((const char *)data) && !e->plain) {
        LONG error = 0;
        inserted = ReReadRtf(e, (const char *)data, (UINT)lstrlenA((const char *)data), from, to - from, &error);
    } else {
        UINT n = 0;
        WCHAR *w;
        if (cp == 1200) {
            const WCHAR *s = data;
            n = s ? (UINT)lstrlenW(s) : 0;
            w = ReAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
            if (w && n)
                memcpy(w, s, n * sizeof(WCHAR));
        } else {
            w = to_wide(data, -1, cp ? cp : CP_ACP, &n);
        }
        if (!w)
            return 0;
        n = cleanse(e, w, n);
        WORD cf = sel ? ReInsertFormat(e) : e->def_cf;
        if (!sel) {
            /* the whole story loses its formatting. */
            for (UINT i = 0; i < e->len; i++) {
                e->cfi[i] = e->def_cf;
                e->pfi[i] = e->def_pf;
            }
        }
        ReReplaceText(e, from, to - from, w, n, cf, undo);
        ReFree(w);
        inserted = (LONG)n;
        if (!sel)
            e->pend_change = TRUE;
    }
    if (e->len - 1 > e->limit)
        e->limit = e->len - 1;
    if (sel) {
        UINT at = from + (UINT)inserted;
        ReSetSelection(e, at, at);
        return inserted;
    }
    e->yscroll = e->xscroll = 0;
    ReSetSelection(e, 0, 0);
    if (!(flags & ST_KEEPUNDO))
        e->modified = FALSE;
    return 1;
}

/* GETTEXTEX into a wide or multibyte buffer. */
static LRESULT get_text_ex(RE *e, const GETTEXTEX *gt, void *out)
{
    if (!gt)
        return 0;
    UINT from = 0, to = e->len - 1;
    if (gt->flags & GT_SELECTION) {
        from = ReSelMin(e);
        to = imin((int)ReSelMax(e), (int)e->len - 1);
    }
    UINT n = 0;
    WCHAR *s = ReRangeText(e, from, to, (gt->flags & GT_USECRLF) != 0, &n);
    if (!s)
        return 0;
    LRESULT r = 0;
    UINT cp = gt->codepage == (UINT)-1 ? (e->ansi ? CP_ACP : 1200) : gt->codepage;
    if (cp == 1200) {
        if (out && gt->cb >= 2) {
            UINT max = gt->cb / 2 - 1;
            if (n > max)
                n = max;
            memcpy(out, s, n * sizeof(WCHAR));
            ((WCHAR *)out)[n] = 0;
            r = (LRESULT)n;
        }
    } else {
        if (gt->cb && n + 1 > gt->cb)
            n = gt->cb - 1;
        s[n] = 0;
        int k = WideCharToMultiByte(cp, 0, s, (int)n + 1, out, (int)gt->cb, gt->lpDefaultChar, gt->lpUsedDefChar);
        if (k > 0)
            r = k - 1;
        else if (out && gt->cb)
            *(char *)out = 0;
    }
    ReFree(s);
    return r;
}

static LRESULT get_text_length_ex(RE *e, const GETTEXTLENGTHEX *gl)
{
    if (!gl)
        return E_INVALIDARG;
    if ((gl->flags & GTL_NUMCHARS) && (gl->flags & GTL_NUMBYTES))
        return E_INVALIDARG;
    UINT n = 0;
    if (!(gl->flags & GTL_NUMBYTES) || gl->codepage == 1200) {
        UINT len = e->len - 1;
        n = len;
        if (gl->flags & GTL_USECRLF)
            for (UINT i = 0; i < len; i++)
                n += e->text[i] == RE_CR;
        if ((gl->flags & GTL_NUMBYTES) && gl->codepage == 1200)
            n *= 2;
        return n;
    }
    WCHAR *s = ReRangeText(e, 0, e->len - 1, (gl->flags & GTL_USECRLF) != 0, &n);
    if (!s)
        return 0;
    UINT cp = gl->codepage == (UINT)-1 ? CP_ACP : gl->codepage;
    int k = n ? WideCharToMultiByte(cp, 0, s, (int)n, nullptr, 0, nullptr, nullptr) : 0;
    ReFree(s);
    return k;
}

/* copies wide text out as the window's kind of string, terminated; returns what was copied. */
static LRESULT put_string(RE *e, const WCHAR *s, UINT n, void *out, UINT room, BOOL terminate)
{
    if (!out)
        return 0;
    if (!e->ansi) {
        if (terminate && room) {
            if (n > room - 1)
                n = room - 1;
        } else if (n > room) {
            n = room;
        }
        memcpy(out, s, n * sizeof(WCHAR));
        if (terminate)
            ((WCHAR *)out)[n] = 0;
        return n;
    }
    int k = WideCharToMultiByte(CP_ACP, 0, s, (int)n, out, (int)room, nullptr, nullptr);
    if (k <= 0 && n) {
        if (room)
            *(char *)out = 0;
        return 0;
    }
    if (terminate) {
        if ((UINT)k >= room && room)
            k = (int)room - 1;
        ((char *)out)[k] = 0;
    }
    return k;
}

/* ------------------------------------------------------------------ formats in and out */

static BOOL valid_cf_size(UINT size)
{
    return size == sizeof(CHARFORMATA) || size == sizeof(CHARFORMATW) || size == sizeof(CHARFORMAT2A) ||
           size == sizeof(CHARFORMAT2W);
}

static BOOL ansi_cf(UINT size)
{
    return size == sizeof(CHARFORMATA) || size == sizeof(CHARFORMAT2A);
}

/* a CHARFORMAT of any size as a full CHARFORMAT2W, and the mask it carries. */
static DWORD cf_in(const void *p, CHARFORMAT2W *cf)
{
    UINT size = *(const UINT *)p;
    memset(cf, 0, sizeof(*cf));
    cf->cbSize = sizeof(*cf);
    if (ansi_cf(size)) {
        const CHARFORMAT2A *a = p;
        cf->dwMask = a->dwMask;
        cf->dwEffects = a->dwEffects;
        cf->yHeight = a->yHeight;
        cf->yOffset = a->yOffset;
        cf->crTextColor = a->crTextColor;
        cf->bCharSet = a->bCharSet;
        cf->bPitchAndFamily = a->bPitchAndFamily;
        MultiByteToWideChar(CP_ACP, 0, a->szFaceName, -1, cf->szFaceName, LF_FACESIZE);
        cf->szFaceName[LF_FACESIZE - 1] = 0;
        if (size == sizeof(CHARFORMAT2A))
            memcpy(&cf->wWeight, &a->wWeight, sizeof(CHARFORMAT2A) - offsetof(CHARFORMAT2A, wWeight));
    } else {
        memcpy(cf, p, size);
        cf->cbSize = sizeof(*cf);
    }
    DWORD mask = cf->dwMask;
    if (size == sizeof(CHARFORMATA) || size == sizeof(CHARFORMATW))
        mask &= 0xF800003F;
    return mask;
}

static void cf_out(const CHARFORMAT2W *cf, DWORD mask, void *p)
{
    UINT size = *(UINT *)p;
    if (ansi_cf(size)) {
        CHARFORMAT2A *a = p;
        a->dwEffects = cf->dwEffects;
        a->yHeight = cf->yHeight;
        a->yOffset = cf->yOffset;
        a->crTextColor = cf->crTextColor;
        a->bCharSet = cf->bCharSet;
        a->bPitchAndFamily = cf->bPitchAndFamily;
        WideCharToMultiByte(CP_ACP, 0, cf->szFaceName, -1, a->szFaceName, LF_FACESIZE, nullptr, nullptr);
        a->szFaceName[LF_FACESIZE - 1] = 0;
        if (size == sizeof(CHARFORMAT2A))
            memcpy(&a->wWeight, &cf->wWeight, sizeof(CHARFORMAT2A) - offsetof(CHARFORMAT2A, wWeight));
    } else {
        memcpy((BYTE *)p + 2 * sizeof(DWORD), (const BYTE *)cf + 2 * sizeof(DWORD), size - 2 * sizeof(DWORD));
    }
    CHARFORMATW *w = p;
    if (size == sizeof(CHARFORMATA) || size == sizeof(CHARFORMATW)) {
        w->dwEffects &= 0x4000003F;
        mask &= 0xF800003F;
    }
    w->dwMask = mask;
}

static LRESULT get_char_format(RE *e, WPARAM scope, void *p)
{
    if (!p || !valid_cf_size(*(UINT *)p))
        return 0;
    CHARFORMAT2W cf;
    DWORD mask;
    if (scope & SCF_SELECTION) {
        mask = ReRangeCharFormat(e, ReSelMin(e), ReSelMax(e), &cf);
    } else {
        memcpy(&cf, &e->fmts[e->def_cf].cf, sizeof(cf));
        mask = CFM_ALL2;
    }
    cf_out(&cf, mask, p);
    return ((CHARFORMATW *)p)->dwMask;
}

static void word_at(RE *e, UINT cp, UINT *from, UINT *to)
{
    UINT a = cp, b = cp;
    while (a > 0 && ReIsWordChar(e->text[a - 1]))
        a--;
    while (b < e->len - 1 && ReIsWordChar(e->text[b]))
        b++;
    *from = a;
    *to = b;
}

static LRESULT set_char_format(RE *e, WPARAM scope, const void *p)
{
    if (!p || !valid_cf_size(*(const UINT *)p))
        return 0;
    CHARFORMAT2W cf;
    DWORD mask = cf_in(p, &cf);
    if (scope & SCF_ALL || e->plain) {
        ReSetFormatRange(e, 0, e->len, &cf, mask, FALSE);
        RE_FMT f;
        memcpy(&f, &e->fmts[e->def_cf], sizeof(f));
        ReApplyCharFormat(&f.cf, &cf, mask);
        e->def_cf = ReAddFormat(e, &f);
        e->ins_valid = FALSE;
    } else if (scope & (SCF_SELECTION | SCF_WORD)) {
        UINT from = ReSelMin(e), to = ReSelMax(e);
        if (from == to && (scope & SCF_WORD))
            word_at(e, from, &from, &to);
        if (from == to) {
            RE_FMT f;
            memcpy(&f, &e->fmts[ReInsertFormat(e)], sizeof(f));
            ReApplyCharFormat(&f.cf, &cf, mask);
            e->ins_cf = ReAddFormat(e, &f);
            e->ins_valid = TRUE;
            if (e->len == 1)
                e->cfi[0] = e->ins_cf;
        } else {
            ReSetFormatRange(e, from, to, &cf, mask, TRUE);
            e->ins_valid = FALSE;
        }
    } else {
        RE_FMT f;
        memcpy(&f, &e->fmts[e->def_cf], sizeof(f));
        ReApplyCharFormat(&f.cf, &cf, mask);
        WORD old = e->def_cf;
        e->def_cf = ReAddFormat(e, &f);
        /* text still in the default format follows it. */
        for (UINT i = 0; i < e->len; i++)
            if (e->cfi[i] == old)
                e->cfi[i] = e->def_cf;
        e->ins_valid = FALSE;
        ReInvalidateLayout(e);
    }
    return 1;
}

static BOOL valid_pf_size(UINT size)
{
    return size == sizeof(PARAFORMAT) || size == sizeof(PARAFORMAT2);
}

static LRESULT get_para_format(RE *e, void *p)
{
    if (!p || !valid_pf_size(*(UINT *)p))
        return 0;
    UINT size = *(UINT *)p;
    PARAFORMAT2 pf;
    DWORD mask = ReRangeParaFormat(e, ReSelMin(e), ReSelMax(e), &pf);
    if (size == sizeof(PARAFORMAT))
        mask &= 0x8001003F;
    memcpy((BYTE *)p + 2 * sizeof(DWORD), (const BYTE *)&pf + 2 * sizeof(DWORD), size - 2 * sizeof(DWORD));
    ((PARAFORMAT *)p)->dwMask = mask;
    return mask;
}

static LRESULT set_para_format(RE *e, WPARAM wparam, const void *p)
{
    if (!p || !valid_pf_size(*(const UINT *)p))
        return 0;
    UINT size = *(const UINT *)p;
    PARAFORMAT2 pf;
    memset(&pf, 0, sizeof(pf));
    memcpy(&pf, p, size);
    DWORD mask = pf.dwMask;
    if (mask & 0x43000000)
        return 0;
    if (mask & PFM_TABSTOPS) {
        int n = imin(pf.cTabCount, MAX_TAB_STOPS);
        for (int i = 0; i < n; i++)
            if (((DWORD)pf.rgxTabs[i] & 0x0F000000) > 0x04000000)
                return 0;
        pf.cTabCount = (SHORT)imax(n, 0);
    }
    if (size == sizeof(PARAFORMAT))
        mask = (mask & 0x8001003F) | 0x20000000;
    mask &= ~0x20000000;
    if (wparam & SPF_SETDEFAULT) {
        PARAFORMAT2 d;
        memcpy(&d, &e->paras[e->def_pf], sizeof(d));
        ReApplyParaFormat(&d, &pf, mask);
        WORD old = e->def_pf;
        e->def_pf = ReAddPara(e, &d);
        for (UINT i = 0; i < e->len; i++)
            if (e->pfi[i] == old)
                e->pfi[i] = e->def_pf;
        ReInvalidateLayout(e);
        return 1;
    }
    if (e->plain)
        ReSetParaRange(e, 0, e->len - 1, &pf, mask, FALSE);
    else
        ReSetParaRange(e, ReSelMin(e), ReSelMax(e), &pf, mask, TRUE);
    return 1;
}

/* ------------------------------------------------------------------ finding */

static WCHAR fold(WCHAR c)
{
    WCHAR s[2] = {c, 0};
    CharLowerBuffW(s, 1);
    return s[0];
}

static BOOL match_at(RE *e, UINT at, const WCHAR *s, UINT n, DWORD flags)
{
    if (at + n > e->len - 1)
        return FALSE;
    for (UINT i = 0; i < n; i++) {
        WCHAR a = e->text[at + i], b = s[i];
        if (b == '\n')
            b = '\r';
        if (a != b && ((flags & FR_MATCHCASE) || fold(a) != fold(b)))
            return FALSE;
    }
    if (flags & FR_WHOLEWORD) {
        if (at > 0 && ReIsWordChar(e->text[at - 1]) && ReIsWordChar(s[0]))
            return FALSE;
        if (at + n < e->len && ReIsWordChar(e->text[at + n]) && ReIsWordChar(s[n - 1]))
            return FALSE;
    }
    return TRUE;
}

static LONG find_text(RE *e, DWORD flags, LONG min, LONG max, const WCHAR *s, LONG *end)
{
    if (!s)
        return -1;
    UINT n = (UINT)lstrlenW(s);
    LONG len = (LONG)e->len - 1;
    if (!n)
        return -1;
    if ((flags & FR_DOWN) || e->v1) {
        LONG lo = imax(min, 0), hi = (max < 0 || max > len) ? len : max;
        for (LONG at = lo; at + (LONG)n <= hi; at++)
            if (match_at(e, (UINT)at, s, n, flags)) {
                *end = at + (LONG)n;
                return at;
            }
        return -1;
    }
    LONG hi = (min < 0 || min > len) ? len : min, lo = max < 0 ? 0 : max;
    for (LONG at = hi - (LONG)n; at >= lo; at--)
        if (match_at(e, (UINT)at, s, n, flags)) {
            *end = at + (LONG)n;
            return at;
        }
    return -1;
}

static LRESULT on_find(RE *e, UINT msg, WPARAM flags, LPARAM lparam)
{
    if (!lparam)
        return -1;
    BOOL ex = msg == EM_FINDTEXTEX || msg == EM_FINDTEXTEXW;
    BOOL wide = msg == EM_FINDTEXTW || msg == EM_FINDTEXTEXW || !e->ansi;
    FINDTEXTEXW *ft = (FINDTEXTEXW *)lparam;
    WCHAR *own = nullptr;
    const WCHAR *s = ft->lpstrText;
    if (!wide) {
        UINT n;
        own = to_wide(((FINDTEXTEXA *)lparam)->lpstrText, -1, CP_ACP, &n);
        s = own;
    }
    LONG end = -1;
    LONG at = find_text(e, (DWORD)flags, ft->chrg.cpMin, ft->chrg.cpMax, s, &end);
    ReFree(own);
    if (ex) {
        ft->chrgText.cpMin = at;
        ft->chrgText.cpMax = at < 0 ? -1 : end;
    }
    return at;
}

/* ------------------------------------------------------------------ lines */

static UINT line_count(RE *e)
{
    ReLayout(e);
    return e->line_count ? e->line_count : 1;
}

static LRESULT line_index(RE *e, LONG line)
{
    ReLayout(e);
    if (line < 0)
        line = (LONG)ReLineOf(e, ReSelMin(e), e->anchor == e->active && e->at_line_end);
    if ((UINT)line >= e->line_count)
        return -1;
    return e->lines[line].cp;
}

static UINT eop_length(RE *e, const RE_LINE *line)
{
    UINT end = line->cp + line->len;
    return end > line->cp && (e->text[end - 1] == RE_CR || e->text[end - 1] == RE_VT || e->text[end - 1] == RE_FF);
}

static LRESULT line_length(RE *e, LONG cp)
{
    ReLayout(e);
    if (cp >= 0) {
        if ((UINT)cp > e->len - 1)
            return 0;
        const RE_LINE *line = &e->lines[ReLineOf(e, (UINT)cp, FALSE)];
        return line->len - eop_length(e, line);
    }
    UINT smin = ReSelMin(e), smax = ReSelMax(e);
    UINT a = ReLineOf(e, smin, FALSE), b = ReLineOf(e, smax, smin != smax || e->at_line_end);
    const RE_LINE *first = &e->lines[a], *last = &e->lines[b];
    if (smin == smax)
        return first->len - eop_length(e, first);
    UINT before = smin - first->cp;
    UINT last_end = last->cp + last->len - eop_length(e, last);
    UINT after = smax < last_end ? last_end - smax : 0;
    return before + after;
}

static LRESULT get_line(RE *e, LONG line, void *buffer)
{
    ReLayout(e);
    if (!buffer)
        return 0;
    UINT room = *(WORD *)buffer;
    if (line < 0 || (UINT)line >= e->line_count) {
        if (e->ansi)
            *(char *)buffer = 0;
        else
            *(WCHAR *)buffer = 0;
        return 0;
    }
    const RE_LINE *l = &e->lines[line];
    UINT n = l->len;
    if (l->cp + n > e->len - 1 && n)
        n = e->len - 1 - l->cp + 1;
    if (!room)
        return 0;
    return put_string(e, e->text + l->cp, imin((int)n, (int)room), buffer, room, FALSE);
}

static LRESULT text_range(RE *e, LONG min, LONG max, void *out)
{
    if (!out)
        return 0;
    LONG len = (LONG)e->len - 1;
    if (max < 0 || max > len + 1)
        max = len + 1;
    if (min < 0 || min > max)
        return 0;
    UINT n = (UINT)(max - min);
    WCHAR *tmp = ReAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!tmp)
        return 0;
    memcpy(tmp, e->text + min, n * sizeof(WCHAR));
    LRESULT r;
    if (!e->ansi) {
        memcpy(out, tmp, n * sizeof(WCHAR));
        ((WCHAR *)out)[n] = 0;
        r = n;
    } else {
        int k = WideCharToMultiByte(CP_ACP, 0, tmp, (int)n, out, 0x7FFFFFF, nullptr, nullptr);
        ((char *)out)[k > 0 ? k : 0] = 0;
        r = k > 0 ? k : 0;
    }
    ReFree(tmp);
    return r;
}

/* ------------------------------------------------------------------ options */

#define OPTION_BITS 0x010C99C0

static DWORD get_options(RE *e)
{
    DWORD o = e->style & OPTION_BITS;
    if (e->autoword)
        o |= ECO_AUTOWORDSELECTION;
    return o;
}

static void apply_style(RE *e)
{
    DWORD s = e->style;
    e->readonly = (s & ES_READONLY) != 0;
    e->nohidesel = (s & ES_NOHIDESEL) != 0;
    e->wantreturn = (s & ES_WANTRETURN) != 0;
    e->selbar = e->multiline && (s & ES_SELECTIONBAR) != 0;
    e->wrap = !(s & ES_AUTOHSCROLL);
}

static LRESULT set_options(RE *e, WORD op, DWORD value)
{
    DWORD bits = value & OPTION_BITS;
    if (!e->multiline)
        bits &= ~ECO_SELECTIONBAR;
    DWORD old = e->style;
    BOOL word = (value & ECO_AUTOWORDSELECTION) != 0;
    switch (op) {
    case ECOOP_SET:
        e->style = (e->style & ~OPTION_BITS) | bits;
        e->autoword = word;
        break;
    case ECOOP_OR:
        e->style |= bits;
        e->autoword |= word;
        break;
    case ECOOP_AND:
        e->style &= bits | ~OPTION_BITS;
        e->autoword &= word;
        break;
    case ECOOP_XOR:
        e->style ^= bits;
        e->autoword ^= word;
        break;
    }
    if (e->style != old) {
        DWORD window = (DWORD)GetWindowLongW(e->hwnd, GWL_STYLE);
        window = (window & ~OPTION_BITS) | (e->style & OPTION_BITS);
        SetWindowLongW(e->hwnd, GWL_STYLE, (LONG)window);
        apply_style(e);
        ReInvalidateLayout(e);
        ReInvalidate(e);
    }
    return get_options(e);
}

/* ------------------------------------------------------------------ creation */

static void set_insets(RE *e, BOOL border)
{
    int cx = GetSystemMetrics(SM_CXBORDER), cy = GetSystemMetrics(SM_CYBORDER);
    e->inset_l = e->inset_r = cx;
    e->inset_t = border ? cy : 0;
    e->inset_b = border ? cy - 1 : 0;
}

static BOOL nc_create(HWND hwnd, const CREATESTRUCTW *cs, BOOL ansi, BOOL v1, BOOL v41)
{
    RE *e = ReCreate(hwnd, cs, ansi, v1, v41);
    if (!e)
        return FALSE;
    SetWindowLongPtrW(hwnd, 0, (LONG_PTR)e);
    DWORD style = cs->style, ex = cs->dwExStyle;
    if (!v1 && (style & WS_HSCROLL))
        style |= ES_AUTOHSCROLL;
    if (style & WS_VSCROLL)
        style |= ES_AUTOVSCROLL;
    BOOL border = (style & WS_BORDER) || (ex & WS_EX_CLIENTEDGE) || (style & ES_SUNKEN);
    if (border && !(ex & WS_EX_CLIENTEDGE)) {
        ex |= WS_EX_CLIENTEDGE;
        SetWindowLongW(hwnd, GWL_EXSTYLE, (LONG)ex);
    }
    e->style = style;
    e->exstyle = ex;
    e->multiline = (style & ES_MULTILINE) != 0;
    e->disabled = (style & WS_DISABLED) != 0;
    if (style & ES_PASSWORD)
        e->password = '*';
    if (style & ES_LOWERCASE)
        e->edit_style |= SES_LOWERCASE;
    if (style & ES_NOIME)
        e->edit_style |= SES_NOIME;
    e->lang_options = IMF_AUTOFONT | IMF_DUALFONT;
    apply_style(e);
    set_insets(e, border);
    if ((style & (ES_CENTER | ES_RIGHT)) || (ex & WS_EX_RIGHT)) {
        PARAFORMAT2 pf;
        memset(&pf, 0, sizeof(pf));
        pf.wAlignment = (style & ES_CENTER) ? PFA_CENTER : PFA_RIGHT;
        PARAFORMAT2 d;
        memcpy(&d, &e->paras[e->def_pf], sizeof(d));
        ReApplyParaFormat(&d, &pf, PFM_ALIGNMENT);
        e->def_pf = ReAddPara(e, &d);
        e->pfi[0] = e->def_pf;
    }
    if (ex & WS_EX_RTLREADING) {
        PARAFORMAT2 d;
        memcpy(&d, &e->paras[e->def_pf], sizeof(d));
        d.wEffects |= PFE_RTLPARA;
        e->def_pf = ReAddPara(e, &d);
        e->pfi[0] = e->def_pf;
    }
    e->vbar = (style & WS_VSCROLL) != 0;
    e->hbar = (style & WS_HSCROLL) != 0;
    if (cs->lpszName && !IS_INTRESOURCE(cs->lpszName)) {
        if (ansi)
            set_text(e, ST_DEFAULT, CP_ACP, cs->lpszName);
        else
            set_text(e, ST_DEFAULT, 1200, cs->lpszName);
        e->pend_change = FALSE;
    }
    e->sent_min = e->sent_max = 0;
    return TRUE;
}

static LRESULT on_create(RE *e)
{
    if (!(e->style & ES_DISABLENOSCROLL)) {
        DWORD window = (DWORD)GetWindowLongW(e->hwnd, GWL_STYLE);
        if (window & (WS_VSCROLL | WS_HSCROLL)) {
            SetScrollRange(e->hwnd, SB_VERT, 0, 0, TRUE);
            SetScrollRange(e->hwnd, SB_HORZ, 0, 0, TRUE);
            SetWindowLongW(e->hwnd, GWL_STYLE, (LONG)(window & ~(WS_VSCROLL | WS_HSCROLL)));
        }
        e->vbar = e->hbar = FALSE;
    }
    ReInvalidateLayout(e);
    return 0;
}

/* WM_SETFONT: the font's format for all the text, or as the default when there is none. */
static void set_font(RE *e, HFONT font, BOOL redraw)
{
    RE_FMT f;
    ReDefaultFormat(e, font, &f);
    if (e->len > 1) {
        ReSetFormatRange(e, 0, e->len, &f.cf, 0xF800003F, FALSE);
        e->pend_change = FALSE;
    }
    RE_FMT d;
    memcpy(&d, &e->fmts[e->def_cf], sizeof(d));
    ReApplyCharFormat(&d.cf, &f.cf, 0xF800003F);
    WORD old = e->def_cf;
    e->def_cf = ReAddFormat(e, &d);
    for (UINT i = 0; i < e->len; i++)
        if (e->cfi[i] == old)
            e->cfi[i] = e->def_cf;
    e->ins_valid = FALSE;
    ReInvalidateLayout(e);
    if (redraw)
        ReInvalidate(e);
    if (e->focus)
        ReCreateCaret(e);
}

/* ------------------------------------------------------------------ the ole interface */

typedef struct RE_OLE {
    const void *vtbl;
    LONG refs;
} RE_OLE;

static HRESULT WINAPI ole_query(RE_OLE *o, const GUID *iid, void **out)
{
    (void)iid;
    *out = o;
    o->refs++;
    return S_OK;
}

static ULONG WINAPI ole_add(RE_OLE *o) { return (ULONG)++o->refs; }

static ULONG WINAPI ole_release(RE_OLE *o)
{
    LONG n = --o->refs;
    if (!n)
        ReFree(o);
    return (ULONG)n;
}

static HRESULT WINAPI ole_notimpl(void) { return E_NOTIMPL; }

static LONG WINAPI ole_count(void) { return 0; }

static const void *const g_ole_vtbl[] = {
    (void *)ole_query, (void *)ole_add, (void *)ole_release,
    /* GetClientSite, GetObjectCount, GetLinkCount, GetObject, InsertObject, ConvertObject, ActivateAs,
       SetHostNames, SetLinkAvailable, SetDvaspect, HandsOffStorage, SaveCompleted, InPlaceDeactivate,
       ContextSensitiveHelp, GetClipboardData, ImportDataObject. */
    (void *)ole_notimpl, (void *)ole_count, (void *)ole_count, (void *)ole_notimpl, (void *)ole_notimpl,
    (void *)ole_notimpl, (void *)ole_notimpl, (void *)ole_notimpl, (void *)ole_notimpl, (void *)ole_notimpl,
    (void *)ole_notimpl, (void *)ole_notimpl, (void *)ole_notimpl, (void *)ole_notimpl, (void *)ole_notimpl,
    (void *)ole_notimpl,
};

static LRESULT get_ole(RE *e, void **out)
{
    if (!out)
        return TRUE;
    RE_OLE *o = ReAlloc(sizeof(RE_OLE));
    if (!o) {
        *out = nullptr;
        return FALSE;
    }
    o->vtbl = g_ole_vtbl;
    o->refs = 1;
    *out = o;
    (void)e;
    return TRUE;
}

/* ------------------------------------------------------------------ links and the cursor */

static BOOL is_link(RE *e, UINT cp)
{
    if (cp >= e->len - 1)
        return FALSE;
    const RE_FMT *f = &e->fmts[e->cfi[cp]];
    return (f->cf.dwEffects & CFE_LINK) || f->autolink;
}

static UINT link_at_point(RE *e, int x, int y)
{
    ReLayout(e);
    if (x < e->view.left || x >= e->view.right || y < e->view.top || y >= e->view.bottom)
        return (UINT)-1;
    int vy = y - e->view.top + e->yscroll;
    if (vy >= e->content_height)
        return (UINT)-1;
    BOOL at_end;
    UINT cp = ReCpFromPoint(e, x, y, &at_end);
    /* the point must be over the character itself, not past the line's end. */
    UINT l = ReLineOf(e, cp, at_end);
    const RE_LINE *line = &e->lines[l];
    int vx = x - e->view.left + e->xscroll - line->x;
    UINT k = cp;
    if (k > line->cp && (k >= e->len || e->xpos[k] > vx))
        k--;
    if (k >= e->len - 1 || k < line->cp || vx < e->xpos[k] || vx >= line->width)
        return (UINT)-1;
    return is_link(e, k) ? k : (UINT)-1;
}

BOOL ReSendLink(RE *e, UINT msg, WPARAM wparam, LPARAM lparam, UINT cp)
{
    if (!(e->mask & ENM_LINK) || cp == (UINT)-1)
        return FALSE;
    UINT a = cp, b = cp;
    while (a > 0 && is_link(e, a - 1) && e->fmts[e->cfi[a - 1]].autolink == e->fmts[e->cfi[cp]].autolink)
        a--;
    while (b < e->len - 1 && is_link(e, b) && e->fmts[e->cfi[b]].autolink == e->fmts[e->cfi[cp]].autolink)
        b++;
    ENLINK link;
    memset(&link, 0, sizeof(link));
    link.msg = msg;
    link.wParam = wparam;
    link.lParam = lparam;
    link.chrg.cpMin = (LONG)a;
    link.chrg.cpMax = (LONG)b;
    return ReNotify(e, EN_LINK, &link.nmhdr) != 0;
}

BOOL ReSetCursor(RE *e)
{
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(e->hwnd, &p);
    ReLayout(e);
    UINT link = link_at_point(e, p.x, p.y);
    if (link != (UINT)-1) {
        if (ReSendLink(e, WM_SETCURSOR, (WPARAM)e->hwnd, MAKELPARAM(p.x, p.y), link))
            return TRUE;
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    if (e->selbar && p.x < e->view.left && p.x >= 0) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    }
    if (e->anchor != e->active && !e->readonly) {
        BOOL at_end;
        UINT cp = ReCpFromPoint(e, p.x, p.y, &at_end);
        if (cp >= ReSelMin(e) && cp < ReSelMax(e) && p.y >= e->view.top && p.y < e->view.bottom) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
    }
    SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
    return TRUE;
}

/* ------------------------------------------------------------------ keyboard in dialogs */

static BOOL dialog_key(RE *e, UINT vk)
{
    if (!e->in_dialog)
        return FALSE;
    BOOL ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
    if (ctrl || alt)
        return FALSE;
    HWND parent = GetParent(e->hwnd);
    switch (vk) {
    case VK_TAB:
        SendMessageW(parent, WM_NEXTDLGCTL, GetKeyState(VK_SHIFT) < 0, 0);
        return TRUE;
    case VK_RETURN: {
        if (e->wantreturn)
            return FALSE;
        LRESULT id = SendMessageW(parent, DM_GETDEFID, 0, 0);
        if (!HIWORD(id))
            return TRUE;
        HWND button = GetDlgItem(parent, LOWORD(id));
        if (!button)
            return TRUE;
        SendMessageW(parent, WM_NEXTDLGCTL, (WPARAM)button, TRUE);
        if (GetFocus() != e->hwnd)
            PostMessageW(button, WM_KEYDOWN, VK_RETURN, 0);
        return TRUE;
    }
    case VK_ESCAPE:
        PostMessageW(parent, WM_CLOSE, 0, 0);
        return TRUE;
    }
    return FALSE;
}

static BOOL dialog_char(RE *e, WCHAR c)
{
    if (!e->in_dialog || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0)
        return FALSE;
    if (c == '\t')
        return TRUE;
    if ((c == '\r' || c == '\n') && !e->wantreturn)
        return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ the dispatch */

static BOOL filtered(RE *e, UINT msg)
{
    if ((e->mask & ENM_KEYEVENTS) && msg >= WM_KEYFIRST && msg <= WM_KEYFIRST + 9)
        return TRUE;
    if ((e->mask & ENM_MOUSEEVENTS) && (msg == WM_MOUSEACTIVATE || (msg >= 0x200 && msg <= 0x20E)))
        return TRUE;
    if ((e->mask & ENM_SCROLLEVENTS) && (msg == WM_HSCROLL || msg == WM_VSCROLL))
        return TRUE;
    return FALSE;
}

static WCHAR g_lead;

static LRESULT on_char(RE *e, WPARAM wparam)
{
    WCHAR c = (WCHAR)wparam;
    if (e->ansi) {
        BYTE b = (BYTE)wparam;
        char bytes[2];
        int n = 0;
        if (g_lead) {
            bytes[n++] = (char)g_lead;
            g_lead = 0;
        } else if (IsDBCSLeadByteEx(CP_ACP, b)) {
            g_lead = b;
            return 0;
        }
        bytes[n++] = (char)b;
        WCHAR w[2];
        if (MultiByteToWideChar(CP_ACP, 0, bytes, n, w, 2) <= 0)
            return 0;
        c = w[0];
    }
    if (dialog_char(e, c))
        return 0;
    ReChar(e, c);
    return 0;
}

static LRESULT on_mouse(RE *e, UINT msg, WPARAM wparam, LPARAM lparam)
{
    int x = (short)LOWORD(lparam), y = (short)HIWORD(lparam);
    if (!e->tracking) {
        UINT link = link_at_point(e, x, y);
        if (link != (UINT)-1 && ReSendLink(e, msg, wparam, lparam, link))
            return 0;
    }
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (GetFocus() != e->hwnd)
            SetFocus(e->hwnd);
        ReMouseDown(e, x, y, (UINT)wparam, msg == WM_LBUTTONDBLCLK);
        return 0;
    case WM_MOUSEMOVE:
        ReMouseMove(e, x, y, (UINT)wparam);
        return 0;
    case WM_LBUTTONUP:
        ReMouseUp(e, x, y);
        return 0;
    case WM_RBUTTONDOWN:
        if (GetFocus() != e->hwnd)
            SetFocus(e->hwnd);
        return 0;
    }
    return def_proc(e, e->hwnd, msg, wparam, lparam);
}

static LRESULT on_paint(RE *e, HDC given)
{
    if (!e->v1 || (e->mask & ENM_UPDATE))
        ReNotifyCommand(e, EN_UPDATE);
    if (given) {
        RePaint(e, given, nullptr);
        return 0;
    }
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(e->hwnd, &ps);
    if (dc)
        RePaint(e, dc, &ps.rcPaint);
    EndPaint(e->hwnd, &ps);
    return 0;
}

static LRESULT scroll_message(RE *e, UINT msg, WPARAM wparam)
{
    int code = LOWORD(wparam), pos = (short)HIWORD(wparam);
    if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION) {
        SCROLLINFO si;
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS | SIF_POS;
        if (GetScrollInfo(e->hwnd, msg == WM_VSCROLL ? SB_VERT : SB_HORZ, &si))
            pos = code == SB_THUMBTRACK ? si.nTrackPos : si.nTrackPos;
    }
    if (msg == WM_VSCROLL)
        ReVScroll(e, code, pos);
    else
        ReHScroll(e, code, pos);
    return 0;
}

static UINT undo_name(RE *e, BOOL redo)
{
    UINT n = redo ? e->redo_count : e->undo_count;
    if (!n)
        return 0;
    int kind = (redo ? e->redo : e->undo)[n - 1].kind;
    return kind & UNDO_FORMAT ? 0 : (UINT)kind;
}

LRESULT ReDispatch(RE *e, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HWND hwnd = e->hwnd;
    switch (msg) {
    case WM_CREATE:
        return on_create(e);
    case WM_SIZE:
        ReInvalidateLayout(e);
        ReLayout(e);
        ReInvalidate(e);
        RePlaceCaret(e);
        return 0;
    case WM_PAINT:
        return on_paint(e, (HDC)wparam);
    case WM_PRINTCLIENT:
        return on_paint(e, (HDC)wparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
        e->focus = TRUE;
        e->wheel = 0;
        ReCreateCaret(e);
        if (e->anchor != e->active)
            ReInvalidate(e);
        ReNotifyCommand(e, EN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        e->wheel = 0;
        if (e->focus) {
            e->focus = FALSE;
            DestroyCaret();
            if (e->anchor != e->active && !e->nohidesel)
                ReInvalidate(e);
        }
        ReNotifyCommand(e, EN_KILLFOCUS);
        return 0;
    case WM_ENABLE:
        e->disabled = !wparam;
        ReInvalidate(e);
        return 0;
    case WM_GETDLGCODE: {
        LRESULT code = (e->style & ES_SAVESEL) ? 0x83 : 0x8B;
        if (e->multiline)
            code |= DLGC_WANTALLKEYS;
        if (lparam)
            e->in_dialog = TRUE;
        return code;
    }
    case WM_SETCURSOR:
        if ((HWND)wparam == hwnd && LOWORD(lparam) == HTCLIENT)
            return ReSetCursor(e);
        return def_proc(e, hwnd, msg, wparam, lparam);
    case WM_KEYDOWN:
        if (dialog_key(e, (UINT)wparam))
            return 0;
        ReKeyDown(e, (UINT)wparam);
        return 0;
    case WM_SYSKEYDOWN:
        if (wparam == VK_BACK && (lparam & (1 << 29))) {
            ReDispatch(e, EM_UNDO, 0, 0);
            return 0;
        }
        return def_proc(e, hwnd, msg, wparam, lparam);
    case WM_CHAR:
        return on_char(e, wparam);
    case WM_UNICHAR:
        if (wparam == UNICODE_NOCHAR)
            return TRUE;
        if (wparam < 0x10000)
            ReChar(e, (WCHAR)wparam);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_MOUSEMOVE:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return on_mouse(e, msg, wparam, lparam);
    case WM_CAPTURECHANGED:
        if ((HWND)lparam != hwnd && e->tracking) {
            e->tracking = FALSE;
            KillTimer(hwnd, RE_SCROLL_TIMER);
        }
        return 0;
    case WM_TIMER:
        if (wparam == RE_SCROLL_TIMER)
            ReMouseTimer(e);
        return 0;
    case WM_MOUSEWHEEL:
        return ReWheel(e, wparam);
    case WM_VSCROLL:
    case WM_HSCROLL:
        return scroll_message(e, msg, wparam);
    case WM_GETTEXT: {
        GETTEXTEX gt;
        memset(&gt, 0, sizeof(gt));
        gt.flags = GT_USECRLF;
        gt.codepage = e->ansi ? CP_ACP : 1200;
        gt.cb = e->ansi ? (DWORD)wparam : (DWORD)(2 * wparam);
        if (!wparam)
            return 0;
        return get_text_ex(e, &gt, (void *)lparam);
    }
    case WM_GETTEXTLENGTH: {
        GETTEXTLENGTHEX gl = {GTL_USECRLF | GTL_PRECISE | (e->ansi ? GTL_NUMBYTES : GTL_NUMCHARS),
                              e->ansi ? CP_ACP : 1200};
        return get_text_length_ex(e, &gl);
    }
    case WM_SETTEXT:
        if (e->ansi)
            return set_text(e, ST_DEFAULT, wparam ? (UINT)wparam : CP_ACP, (const void *)lparam);
        return set_text(e, ST_DEFAULT, wparam ? (UINT)wparam : 1200, (const void *)lparam);
    case WM_SETFONT:
        set_font(e, (HFONT)wparam, LOWORD(lparam));
        return 0;
    case WM_GETFONT:
        return 0;
    case WM_SETREDRAW:
        e->redraw = wparam != 0;
        if (e->redraw)
            ReInvalidate(e);
        return 0;
    case WM_SYSCOLORCHANGE:
        if (e->bk_sys)
            e->bk = GetSysColor(COLOR_WINDOW);
        ReInvalidate(e);
        return 0;
    case WM_COPY:
        ReCopy(e);
        return 0;
    case WM_CUT:
        ReCut(e);
        return 0;
    case WM_PASTE:
        RePaste(e, 0);
        return 0;
    case WM_CLEAR:
        if (e->readonly)
            ReBeep(e);
        else
            ReDeleteSelection(e, UNDO_DELETE);
        return 0;
    case WM_UNDO:
    case EM_UNDO:
        if (e->readonly)
            return 0;
        if (ReUndo(e, FALSE)) {
            ReSetSelection(e, e->anchor, e->active);
            return TRUE;
        }
        return FALSE;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_EXSTYLE)
            ReInvalidate(e);
        return 0;

    case EM_GETSEL: {
        UINT smin = ReSelMin(e), smax = ReSelMax(e);
        if (wparam)
            *(DWORD *)wparam = smin;
        if (lparam)
            *(DWORD *)lparam = smax;
        return smax > 0xFFFF ? -1 : (LRESULT)MAKELONG(smin, smax);
    }
    case EM_SETSEL:
        return set_sel(e, (LONG)wparam, (LONG)lparam);
    case EM_EXGETSEL:
        if (lparam) {
            ((CHARRANGE *)lparam)->cpMin = (LONG)ReSelMin(e);
            ((CHARRANGE *)lparam)->cpMax = (LONG)ReSelMax(e);
        }
        return 0;
    case EM_EXSETSEL:
        if (!lparam)
            return E_FAIL;
        return set_sel(e, ((CHARRANGE *)lparam)->cpMin, ((CHARRANGE *)lparam)->cpMax);
    case EM_GETRECT:
        if (lparam) {
            ReComputeView(e);
            *(RECT *)lparam = e->view;
        }
        return 0;
    case EM_SETRECT:
    case EM_SETRECTNP:
        if (!lparam) {
            e->rect_set = FALSE;
        } else {
            RECT r = *(RECT *)lparam;
            if (wparam == 1) {
                RECT c;
                GetClientRect(hwnd, &c);
                r.left += c.left + e->inset_l;
                r.top += c.top + e->inset_t;
                r.right += c.right - e->inset_r;
                r.bottom += c.bottom - e->inset_b;
            }
            e->fmt_rect = r;
            e->rect_set = TRUE;
        }
        ReInvalidateLayout(e);
        ReLayout(e);
        if (msg == EM_SETRECT)
            ReInvalidate(e);
        return 0;
    case EM_SCROLL:
        if (wparam > SB_PAGEDOWN)
            return 0;
        return ReVScroll(e, (int)wparam, 0);
    case EM_LINESCROLL: {
        ReLayout(e);
        if (!e->multiline)
            return FALSE;
        LONG lines = (LONG)lparam;
        UINT top = 0;
        while (top + 1 < e->line_count && e->lines[top + 1].y <= e->yscroll)
            top++;
        LONG target = (LONG)top + lines;
        if (target < 0)
            target = 0;
        if ((UINT)target >= e->line_count)
            target = (LONG)e->line_count - 1;
        int y = e->line_count ? e->lines[target].y : 0;
        int x = e->xscroll + (int)(LONG)wparam * imax(ReFont(e, e->def_cf)->ave, 1);
        ReScrollTo(e, x, y);
        return TRUE;
    }
    case EM_SCROLLCARET:
        ReScrollCaret(e);
        return 0;
    case EM_GETMODIFY:
        return e->modified ? -1 : 0;
    case EM_SETMODIFY:
        e->modified = wparam != 0;
        return 0;
    case EM_GETLINECOUNT:
        return line_count(e);
    case EM_LINEINDEX:
        return line_index(e, (LONG)wparam);
    case EM_LINELENGTH:
        return line_length(e, (LONG)wparam);
    case EM_GETLINE:
        return get_line(e, (LONG)wparam, (void *)lparam);
    case EM_LINEFROMCHAR:
    case EM_EXLINEFROMCHAR: {
        LONG cp = msg == EM_LINEFROMCHAR ? (LONG)wparam : (LONG)lparam;
        if (cp < 0)
            return ReLineOf(e, ReSelMin(e), e->anchor == e->active && e->at_line_end);
        if ((UINT)cp > e->len)
            cp = (LONG)e->len;
        return ReLineOf(e, (UINT)cp, FALSE);
    }
    case EM_POSFROMCHAR: {
        if (!wparam)
            return 0;
        POINTL *p = (POINTL *)wparam;
        LONG cp = (LONG)lparam;
        if (cp < 0)
            cp = 0;
        POINT q = RePosFromCp(e, (UINT)cp, FALSE);
        p->x = q.x;
        p->y = q.y;
        return 0;
    }
    case EM_CHARFROMPOS: {
        if (!lparam)
            return -1;
        const POINTL *p = (const POINTL *)lparam;
        BOOL at_end;
        UINT cp = ReCpFromPoint(e, p->x, p->y, &at_end);
        return cp > e->len - 1 ? (LRESULT)(e->len - 1) : (LRESULT)cp;
    }
    case EM_GETFIRSTVISIBLELINE: {
        ReLayout(e);
        UINT top = 0;
        while (top + 1 < e->line_count && e->lines[top + 1].y <= e->yscroll)
            top++;
        return top;
    }
    case EM_GETTHUMB:
        ReLayout(e);
        return e->yscroll;
    case EM_CANUNDO:
        return e->undo_count != 0;
    case EM_CANREDO:
        return e->redo_count != 0;
    case EM_REDO:
        if (e->readonly)
            return 0;
        if (ReUndo(e, TRUE)) {
            ReSetSelection(e, e->anchor, e->active);
            return TRUE;
        }
        return FALSE;
    case EM_EMPTYUNDOBUFFER:
        ReClearUndo(e);
        return 0;
    case EM_SETUNDOLIMIT:
        e->undo_limit = (UINT)wparam;
        if (!e->undo_limit)
            ReClearUndo(e);
        while (e->undo_count > e->undo_limit) {
            ReClearUndo(e);
            break;
        }
        return e->undo_limit;
    case EM_GETUNDONAME:
        return undo_name(e, FALSE);
    case EM_GETREDONAME:
        return undo_name(e, TRUE);
    case EM_STOPGROUPTYPING:
        ReStopGrouping(e);
        return 0;
    case EM_SETREADONLY: {
        DWORD window = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
        if (wparam) {
            e->style |= ES_READONLY;
            window |= ES_READONLY;
        } else {
            e->style &= ~ES_READONLY;
            window &= ~ES_READONLY;
        }
        apply_style(e);
        SetWindowLongW(hwnd, GWL_STYLE, (LONG)window);
        return 1;
    }
    case EM_SETPASSWORDCHAR:
        if (e->password != (WCHAR)wparam) {
            e->password = (WCHAR)wparam;
            ReInvalidateLayout(e);
            ReInvalidate(e);
        }
        return 0;
    case EM_GETPASSWORDCHAR:
        return e->password;
    case EM_SETMARGINS: {
        int l = LOWORD(lparam), r = HIWORD(lparam);
        if (l == EC_USEFONTINFO)
            l = 0;
        if (r == EC_USEFONTINFO)
            r = 0;
        if (wparam & EC_LEFTMARGIN)
            e->margin_l = l;
        if (wparam & EC_RIGHTMARGIN)
            e->margin_r = r;
        ReInvalidateLayout(e);
        ReInvalidate(e);
        return 0;
    }
    case EM_GETLIMITTEXT:
        return e->limit;
    case EM_LIMITTEXT:
    case EM_EXLIMITTEXT: {
        LONG n = msg == EM_LIMITTEXT ? (LONG)wparam : (LONG)lparam;
        if (!n)
            n = 0x10000;
        if (e->v1 && n < 0)
            n = 0;
        e->limit = (UINT)n;
        return 0;
    }
    case EM_SETTABSTOPS: {
        if (!e->multiline)
            return FALSE;
        PARAFORMAT2 pf;
        memset(&pf, 0, sizeof(pf));
        int count = (int)wparam;
        const INT *stops = (const INT *)lparam;
        if (count > MAX_TAB_STOPS)
            count = MAX_TAB_STOPS;
        if (count && !stops)
            return FALSE;
        int unit = LOWORD(GetDialogBaseUnits());
        if (count == 0) {
            pf.cTabCount = 0;
        } else if (count == 1) {
            pf.cTabCount = MAX_TAB_STOPS;
            for (int i = 0; i < MAX_TAB_STOPS; i++)
                pf.rgxTabs[i] = MulDiv(MulDiv(stops[0] * (i + 1), unit, 4), 1440, e->dpi);
        } else {
            pf.cTabCount = (SHORT)count;
            for (int i = 0; i < count; i++)
                pf.rgxTabs[i] = MulDiv(MulDiv(stops[i], unit, 4), 1440, e->dpi);
        }
        PARAFORMAT2 d;
        memcpy(&d, &e->paras[e->def_pf], sizeof(d));
        ReApplyParaFormat(&d, &pf, PFM_TABSTOPS);
        WORD old = e->def_pf;
        e->def_pf = ReAddPara(e, &d);
        for (UINT i = 0; i < e->len; i++)
            if (e->pfi[i] == old)
                e->pfi[i] = e->def_pf;
        ReInvalidateLayout(e);
        ReInvalidate(e);
        return TRUE;
    }
    case EM_FMTLINES:
    case EM_GETHANDLE:
    case EM_SETHANDLE:
        return 0;
    case EM_SETWORDBREAKPROC:
        e->wordbreak = (EDITWORDBREAKPROCW)lparam;
        return 0;
    case EM_GETWORDBREAKPROC:
        return (LRESULT)e->wordbreak;
    case EM_REPLACESEL: {
        DWORD flags = ST_SELECTION | (wparam ? ST_KEEPUNDO : 0);
        return set_text(e, flags, e->ansi ? CP_ACP : 1200, (const void *)lparam);
    }

    case EM_CANPASTE:
        return ReCanPaste(e, (UINT)wparam);
    case EM_PASTESPECIAL:
        RePaste(e, (UINT)wparam);
        return 0;
    case EM_DISPLAYBAND:
        return FALSE;
    case EM_FORMATRANGE:
        if (!lparam)
            return 0;
        return ((FORMATRANGE *)lparam)->chrg.cpMax < 0 ? (LRESULT)(e->len - 1)
                                                       : imin(((FORMATRANGE *)lparam)->chrg.cpMax, (int)e->len - 1);
    case EM_FINDTEXT:
    case EM_FINDTEXTEX:
    case EM_FINDTEXTW:
    case EM_FINDTEXTEXW:
        return on_find(e, msg, wparam, lparam);
    case EM_GETCHARFORMAT:
        return get_char_format(e, wparam, (void *)lparam);
    case EM_SETCHARFORMAT:
        return set_char_format(e, wparam, (const void *)lparam);
    case EM_GETPARAFORMAT:
        return get_para_format(e, (void *)lparam);
    case EM_SETPARAFORMAT:
        return set_para_format(e, wparam, (const void *)lparam);
    case EM_GETEVENTMASK:
        return e->mask;
    case EM_SETEVENTMASK: {
        DWORD old = e->mask;
        e->mask = (DWORD)lparam;
        if (e->mask & ENM_REQUESTRESIZE)
            ReInvalidateLayout(e);
        return old;
    }
    case EM_GETOLEINTERFACE:
        return get_ole(e, (void **)lparam);
    case EM_SETOLECALLBACK:
        e->ole_callback = (void *)lparam;
        return TRUE;
    case EM_GETSELTEXT: {
        UINT from = ReSelMin(e), to = ReSelMax(e);
        return text_range(e, (LONG)from, (LONG)to, (void *)lparam);
    }
    case EM_GETTEXTRANGE: {
        if (!lparam)
            return 0;
        TEXTRANGEW *tr = (TEXTRANGEW *)lparam;
        if (tr->chrg.cpMax >= 0 && tr->chrg.cpMin > tr->chrg.cpMax)
            return 0;
        return text_range(e, tr->chrg.cpMin, tr->chrg.cpMax, tr->lpstrText);
    }
    case EM_HIDESELECTION:
        if (lparam) {
            if (wparam)
                e->style &= ~ES_NOHIDESEL;
            else
                e->style |= ES_NOHIDESEL;
            apply_style(e);
        }
        if (wparam)
            e->nohidesel = FALSE;
        else if (e->focus || lparam)
            e->nohidesel = TRUE;
        ReInvalidate(e);
        return 0;
    case EM_REQUESTRESIZE:
        ReLayout(e);
        ReRequestResize(e, TRUE);
        return 0;
    case EM_SELECTIONTYPE: {
        UINT n = ReSelMax(e) - ReSelMin(e);
        return n == 0 ? SEL_EMPTY : (SEL_TEXT | (n > 1 ? SEL_MULTICHAR : 0));
    }
    case EM_SETBKGNDCOLOR: {
        COLORREF old = e->bk;
        e->bk_sys = wparam != 0;
        e->bk = wparam ? GetSysColor(COLOR_WINDOW) : (COLORREF)lparam;
        if (e->bk != old)
            ReInvalidate(e);
        return old;
    }
    case EM_SETTARGETDEVICE:
        if (!wparam && lparam == 1)
            e->target = -1;
        else if (!wparam && lparam == 0)
            e->target = 0;
        else
            e->target = lparam > 0 ? (LONG)lparam : 0;
        ReInvalidateLayout(e);
        ReInvalidate(e);
        return TRUE;
    case EM_STREAMIN:
        return ReStreamIn(e, (UINT)wparam, (EDITSTREAM *)lparam);
    case EM_STREAMOUT:
        return ReStreamOut(e, (UINT)wparam, (EDITSTREAM *)lparam);
    case EM_FINDWORDBREAK:
        return ReWordBreak(e, (UINT)lparam, (int)wparam);
    case EM_SETOPTIONS:
        return set_options(e, (WORD)wparam, (DWORD)lparam);
    case EM_GETOPTIONS:
        return get_options(e);
    case EM_GETWORDBREAKPROCEX:
    case EM_SETWORDBREAKPROCEX:
        return 0;
    case EM_SETTEXTMODE: {
        if (e->len > 1 || e->undo_count)
            return E_UNEXPECTED;
        if (wparam & TM_PLAINTEXT)
            e->plain = TRUE;
        else if (wparam & TM_RICHTEXT)
            e->plain = FALSE;
        return 0;
    }
    case EM_GETTEXTMODE:
        return (e->plain ? TM_PLAINTEXT : TM_RICHTEXT) | TM_MULTILEVELUNDO | TM_MULTICODEPAGE;
    case EM_AUTOURLDETECT:
        if (lparam || (wparam | 1) != 1)
            return E_INVALIDARG;
        if (e->autourl != (wparam != 0)) {
            e->autourl = wparam != 0;
            ReScanUrls(e);
            ReInvalidate(e);
        }
        return 0;
    case EM_GETAUTOURLDETECT:
        return e->autourl;
    case EM_SETPALETTE:
        return 0;
    case EM_GETTEXTEX:
        return get_text_ex(e, (const GETTEXTEX *)wparam, (void *)lparam);
    case EM_GETTEXTLENGTHEX:
        return get_text_length_ex(e, (const GETTEXTLENGTHEX *)wparam);
    case EM_SETTEXTEX: {
        if (!wparam)
            return 0;
        const SETTEXTEX *st = (const SETTEXTEX *)wparam;
        UINT cp = st->codepage;
        if (cp == (UINT)-1)
            cp = e->ansi ? CP_ACP : 1200;
        return set_text(e, st->flags, cp, (const void *)lparam);
    }
    case EM_SHOWSCROLLBAR: {
        DWORD bit = wparam == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
        if (lparam)
            e->style |= bit;
        else
            e->style &= ~bit;
        if (wparam == SB_VERT)
            e->vbar = lparam != 0;
        else
            e->hbar = lparam != 0;
        ShowScrollBar(hwnd, (int)wparam, lparam != 0);
        ReInvalidateLayout(e);
        return 0;
    }
    case EM_SETLANGOPTIONS:
        e->lang_options = (DWORD)lparam;
        return TRUE;
    case EM_GETLANGOPTIONS:
        return e->lang_options;
    case EM_SETEDITSTYLE:
        e->edit_style = (e->edit_style & ~(DWORD)lparam) | ((DWORD)wparam & (DWORD)lparam);
        return e->edit_style;
    case EM_GETEDITSTYLE:
        return e->edit_style;
    case EM_SETTYPOGRAPHYOPTIONS:
        e->typography = (e->typography & ~(DWORD)lparam) | ((DWORD)wparam & (DWORD)lparam);
        return TRUE;
    case EM_GETTYPOGRAPHYOPTIONS:
        return e->typography;
    case EM_GETSCROLLPOS:
        if (lparam) {
            ReLayout(e);
            ((POINT *)lparam)->x = e->xscroll;
            ((POINT *)lparam)->y = e->yscroll;
        }
        return 1;
    case EM_SETSCROLLPOS:
        if (lparam)
            ReScrollTo(e, ((POINT *)lparam)->x, ((POINT *)lparam)->y);
        return 1;
    case EM_SETFONTSIZE: {
        CHARFORMAT2W cf;
        ReRangeCharFormat(e, ReSelMin(e), ReSelMax(e), &cf);
        LONG pt = cf.yHeight / 20 + (SHORT)LOWORD(wparam);
        if (pt < 1)
            pt = 1;
        if (pt > 1638)
            pt = 1638;
        cf.yHeight = pt * 20;
        set_char_format(e, SCF_SELECTION, &cf);
        return TRUE;
    }
    case EM_GETZOOM:
        if (!wparam || !lparam)
            return 0;
        *(int *)wparam = e->zoom_num;
        *(int *)lparam = e->zoom_den;
        return TRUE;
    case EM_SETZOOM: {
        DWORD num = (DWORD)wparam, den = (DWORD)lparam;
        if ((num | den) >= 0x10000)
            return FALSE;
        if ((num || den) && ((int)num >= (int)(den << 6) || (int)den >= (int)(num << 6)))
            return FALSE;
        e->zoom_num = (int)num;
        e->zoom_den = (int)den;
        ReFlushFonts(e);
        ReInvalidateLayout(e);
        ReInvalidate(e);
        return TRUE;
    }
    case EM_GETIMESTATUS:
    case EM_SETIMESTATUS:
    case EM_SETPUNCTUATION:
    case EM_GETPUNCTUATION:
    case EM_SETWORDWRAPMODE:
    case EM_GETWORDWRAPMODE:
    case EM_SETIMECOLOR:
    case EM_GETIMECOLOR:
    case EM_SETIMEOPTIONS:
    case EM_GETIMEOPTIONS:
    case EM_GETVIEWKIND:
    case EM_SETVIEWKIND:
    case EM_SETBIDIOPTIONS:
    case EM_GETBIDIOPTIONS:
        return 0;
    }
    return def_proc(e, hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK ReWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi, BOOL v1, BOOL v41)
{
    RE *e = re_of(hwnd);
    if (msg == WM_NCCREATE) {
        if (e)
            return TRUE;
        const CREATESTRUCTW *cs = (const CREATESTRUCTW *)lparam;
        return cs && nc_create(hwnd, cs, ansi, v1, v41);
    }
    if (!e)
        return ansi ? DefWindowProcA(hwnd, msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
    if (msg == WM_NCDESTROY) {
        if (e->tracking)
            KillTimer(hwnd, RE_SCROLL_TIMER);
        SetWindowLongPtrW(hwnd, 0, 0);
        ReDestroy(e);
        return 0;
    }
    if (filtered(e, msg)) {
        MSGFILTER mf;
        memset(&mf, 0, sizeof(mf));
        mf.msg = msg;
        mf.wParam = wparam;
        mf.lParam = lparam;
        e->depth++;
        LRESULT stop = ReNotify(e, EN_MSGFILTER, &mf.nmhdr);
        e->depth--;
        if (re_of(hwnd) != e)
            return 0;
        if (stop)
            return def_proc(e, hwnd, msg, wparam, lparam);
        msg = mf.msg;
        wparam = mf.wParam;
        lparam = mf.lParam;
    }
    e->depth++;
    LRESULT r = ReDispatch(e, msg, wparam, lparam);
    if (re_of(hwnd) != e)
        return r;
    if (--e->depth == 0)
        flush(e);
    return r;
}
