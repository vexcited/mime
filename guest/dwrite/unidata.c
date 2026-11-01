/*
 * the character properties of the text layout: scripts as directwrite numbers them, line break, bidirectional and
 * grapheme classes, and the algorithms on them. the tables are in gen/dwrite_textdata.h.
 */
#include "dw_private.h"
#include "text_private.h"
#include "dwrite_textdata.h"

static UINT32 find_start(const UINT32 *starts, UINT32 count, UINT32 cp)
{
    UINT32 lo = 0, hi = count;
    while (hi - lo > 1) {
        UINT32 mid = (lo + hi) / 2;
        if (starts[mid] <= cp)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

UINT32 DwScriptCount(void) { return sizeof(g_dw_scripts) / sizeof(g_dw_scripts[0]); }

UINT32 DwCharScript(UINT32 cp)
{
    if (cp > 0x10FFFF)
        return 0;
    return g_dw_script_value[find_start(g_dw_script_start, sizeof(g_dw_script_start) / 4, cp)];
}

BOOL DwScriptProperties(UINT32 script, DWRITE_SCRIPT_PROPERTIES *p)
{
    if (script >= DwScriptCount())
        return FALSE;
    const DwScriptInfo *s = &g_dw_scripts[script];
    memset(p, 0, sizeof(*p));
    p->isoScriptCode = s->tag;
    p->isoScriptNumber = s->number;
    p->clusterLookahead = s->lookahead;
    p->justificationCharacter = s->justification;
    p->restrictCaretToClusters = s->flags & 1;
    p->usesWordDividers = (s->flags >> 1) & 1;
    p->isDiscreteWriting = (s->flags >> 2) & 1;
    p->isBlockWriting = (s->flags >> 3) & 1;
    p->isDistributedWithinCluster = (s->flags >> 4) & 1;
    p->isConnectedWriting = (s->flags >> 5) & 1;
    p->isCursiveWriting = (s->flags >> 6) & 1;
    return TRUE;
}

UINT32 DwScriptTag(UINT32 script) { return script < DwScriptCount() ? g_dw_scripts[script].tag : g_dw_scripts[0].tag; }

UINT8 DwLineBreakClass(UINT32 cp)
{
    if (cp > 0x10FFFF)
        return DW_LB_XX;
    return g_dw_lb_value[find_start(g_dw_lb_start, sizeof(g_dw_lb_start) / 4, cp)];
}

UINT8 DwBidiClass(UINT32 cp)
{
    if (cp > 0x10FFFF)
        return DW_BIDI_L;
    return g_dw_bidi_value[find_start(g_dw_bidi_start, sizeof(g_dw_bidi_start) / 4, cp)];
}

UINT8 DwGraphemeClass(UINT32 cp)
{
    if (cp > 0x10FFFF)
        return DW_GCB_OTHER;
    return g_dw_gcb_value[find_start(g_dw_gcb_start, sizeof(g_dw_gcb_start) / 4, cp)];
}

UINT8 DwCategory(UINT32 cp)
{
    if (cp > 0x10FFFF)
        return DW_GC_CN;
    return g_dw_cat_value[find_start(g_dw_cat_start, sizeof(g_dw_cat_start) / 4, cp)];
}

/* the code point two combine into, or zero. */
UINT32 DwCompose(UINT32 first, UINT32 second)
{
    UINT32 n = sizeof(g_dw_comp_first) / 4;
    UINT32 lo = 0, hi = n;
    while (lo < hi) {
        UINT32 mid = (lo + hi) / 2;
        if (g_dw_comp_first[mid] < first || (g_dw_comp_first[mid] == first && g_dw_comp_second[mid] < second))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < n && g_dw_comp_first[lo] == first && g_dw_comp_second[lo] == second ? g_dw_comp_result[lo] : 0;
}

UINT32 DwMirror(UINT32 cp)
{
    UINT32 n = sizeof(g_dw_mirror_from) / 4;
    UINT32 lo = 0, hi = n;
    while (lo < hi) {
        UINT32 mid = (lo + hi) / 2;
        if (g_dw_mirror_from[mid] < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < n && g_dw_mirror_from[lo] == cp ? g_dw_mirror_to[lo] : cp;
}

/* the paired bracket of a code point, and whether it opens; zero when it is not a bracket. */
static UINT32 bracket_of(UINT32 cp, BOOL *opens)
{
    UINT32 n = sizeof(g_dw_bracket_cp) / 4;
    UINT32 lo = 0, hi = n;
    while (lo < hi) {
        UINT32 mid = (lo + hi) / 2;
        if (g_dw_bracket_cp[mid] < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < n && g_dw_bracket_cp[lo] == cp) {
        *opens = g_dw_bracket_open[lo];
        return g_dw_bracket_pair[lo];
    }
    return 0;
}

UINT32 DwCodePointAt(const WCHAR *text, UINT32 length, UINT32 pos, UINT32 *units)
{
    UINT32 c = text[pos];
    *units = 1;
    if (c >= 0xD800 && c < 0xDC00 && pos + 1 < length && text[pos + 1] >= 0xDC00 && text[pos + 1] < 0xE000) {
        *units = 2;
        return 0x10000 + ((c - 0xD800) << 10) + (text[pos + 1] - 0xDC00);
    }
    return c;
}

BOOL DwIsWhitespace(UINT32 cp)
{
    switch (cp) {
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
    case 0x20:
    case 0x85:
    case 0xA0:
    case 0x1680:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
        return TRUE;
    }
    return cp >= 0x2000 && cp <= 0x200A;
}

BOOL DwIsNewline(UINT32 cp)
{
    return cp == 0x0A || cp == 0x0B || cp == 0x0C || cp == 0x0D || cp == 0x85 || cp == 0x2028 || cp == 0x2029;
}

/* ------------------------------------------------------------------ grapheme clusters */

/* the code units from pos that make one cluster: a character with the marks and joiners after it. */
UINT32 DwClusterLength(const WCHAR *text, UINT32 length, UINT32 pos)
{
    UINT32 units;
    UINT32 cp = DwCodePointAt(text, length, pos, &units);
    UINT32 at = pos + units;
    UINT8 prev = DwGraphemeClass(cp);
    prev &= 0x3F;
    if (prev == DW_GCB_CR || prev == DW_GCB_LF || prev == DW_GCB_CONTROL)
        return units;
    UINT32 ri_count = prev == DW_GCB_REGIONAL_INDICATOR ? 1 : 0;
    while (at < length) {
        UINT32 u2;
        UINT32 next = DwCodePointAt(text, length, at, &u2);
        UINT8 cls = DwGraphemeClass(next);
        cls &= 0x3F;
        BOOL join = FALSE;
        if (cls == DW_GCB_EXTEND || cls == DW_GCB_ZWJ || cls == DW_GCB_SPACINGMARK)
            join = TRUE;
        else if (prev == DW_GCB_PREPEND && cls != DW_GCB_CONTROL && cls != DW_GCB_CR && cls != DW_GCB_LF)
            join = TRUE;
        else if (prev == DW_GCB_L && (cls == DW_GCB_L || cls == DW_GCB_V || cls == DW_GCB_LV || cls == DW_GCB_LVT))
            join = TRUE;
        else if ((prev == DW_GCB_LV || prev == DW_GCB_V) && (cls == DW_GCB_V || cls == DW_GCB_T))
            join = TRUE;
        else if ((prev == DW_GCB_LVT || prev == DW_GCB_T) && cls == DW_GCB_T)
            join = TRUE;
        else if (prev == DW_GCB_REGIONAL_INDICATOR && cls == DW_GCB_REGIONAL_INDICATOR && (ri_count & 1))
            join = TRUE;
        if (!join)
            break;
        if (cls == DW_GCB_REGIONAL_INDICATOR)
            ri_count++;
        prev = cls;
        at += u2;
    }
    return at - pos;
}

/* ------------------------------------------------------------------ line breaking */

/*
 * the unicode line breaking rules, with the codes of the characters around the gap. breaks[i] is set for the gap after
 * code unit i: 0 no break, 1 break allowed, 2 break required.
 */
void DwLineBreaks(const WCHAR *text, UINT32 length, UINT8 *breaks)
{
    memset(breaks, 0, length);
    if (!length)
        return;
    /* the class of each code point, resolved by rule 1. */
    UINT32 *cp_at = DwAlloc(length * sizeof(UINT32));
    UINT8 *cls = DwAlloc(length);
    UINT8 *flags = DwAlloc(length);
    UINT32 *start = DwAlloc(length * sizeof(UINT32));
    UINT32 n = 0;
    if (!cp_at || !cls || !flags || !start) {
        DwFree(cp_at);
        DwFree(cls);
        DwFree(flags);
        DwFree(start);
        return;
    }
    for (UINT32 i = 0; i < length;) {
        UINT32 units;
        UINT32 cp = DwCodePointAt(text, length, i, &units);
        UINT8 raw = DwLineBreakClass(cp);
        UINT8 c = raw & 0x3F;
        UINT8 f = raw & 0xC0;
        UINT8 gc = DwCategory(cp);
        if (c == DW_LB_AI || c == DW_LB_SG || c == DW_LB_XX)
            c = DW_LB_AL;
        else if (c == DW_LB_EM)
            c = DW_LB_CM; /* windows treats the emoji modifiers as combining marks. */
        else if (c == DW_LB_SA)
            c = (gc == DW_GC_MN || gc == DW_GC_MC) ? DW_LB_CM : DW_LB_AL;
        else if (c == DW_LB_CJ)
            c = DW_LB_NS;
        cp_at[n] = cp;
        cls[n] = c;
        flags[n] = f;
        start[n] = i;
        n++;
        i += units;
    }
    /* the break opportunity after each code point, indexed like cls. */
    UINT8 *brk = DwAllocZero(n);
    if (!brk) {
        DwFree(cp_at);
        DwFree(cls);
        DwFree(flags);
        DwFree(start);
        return;
    }
    UINT32 ri_run = 0;
    for (UINT32 i = 0; i + 1 < n; i++) {
        UINT8 a = cls[i], b = cls[i + 1];
        if ((b == DW_LB_CM || b == DW_LB_ZWJ) && a == DW_LB_SP)
            b = DW_LB_AL;
        /* the class before the spaces, for rules 14 to 17, with marks taken into their base. */
        UINT32 k = i;
        while (k > 0 && cls[k] == DW_LB_SP)
            k--;
        UINT8 before_sp = cls[k];
        if (cls[k] == DW_LB_SP) {
            before_sp = DW_LB_XX; /* only spaces back to the start. */
        } else {
            UINT32 j = k;
            while (j > 0 && (cls[j] == DW_LB_CM || cls[j] == DW_LB_ZWJ))
                j--;
            if (cls[j] != DW_LB_CM && cls[j] != DW_LB_ZWJ && cls[j] != DW_LB_SP && cls[j] != DW_LB_BK &&
                cls[j] != DW_LB_CR && cls[j] != DW_LB_LF && cls[j] != DW_LB_NL && cls[j] != DW_LB_ZW)
                before_sp = cls[j];
            else if (cls[k] == DW_LB_CM || cls[k] == DW_LB_ZWJ)
                before_sp = DW_LB_AL;
        }
        UINT8 res;
        if (a == DW_LB_RI)
            ri_run++;
        else if (a != DW_LB_CM && a != DW_LB_ZWJ)
            ri_run = 0;
        /* combining marks take the class of the base before them, rule 9. */
        UINT8 aa = a;
        if (a == DW_LB_CM || a == DW_LB_ZWJ) {
            UINT32 j = i;
            while (j > 0 && (cls[j] == DW_LB_CM || cls[j] == DW_LB_ZWJ))
                j--;
            if (cls[j] != DW_LB_CM && cls[j] != DW_LB_ZWJ && cls[j] != DW_LB_BK && cls[j] != DW_LB_CR &&
                cls[j] != DW_LB_LF && cls[j] != DW_LB_NL && cls[j] != DW_LB_SP && cls[j] != DW_LB_ZW)
                aa = cls[j];
            else
                aa = DW_LB_AL;
            if (j == i && (a == DW_LB_CM || a == DW_LB_ZWJ))
                aa = DW_LB_AL;
        }
        BOOL joiner_tail = FALSE;
        if (a == DW_LB_CM) {
            UINT32 j = i;
            while (j > 0 && cls[j] == DW_LB_CM)
                j--;
            joiner_tail = cls[j] == DW_LB_ZWJ;
        }
        if (a == DW_LB_BK || a == DW_LB_LF || a == DW_LB_NL) {
            res = 2; /* rules 4 and 5. */
        } else if (a == DW_LB_CR) {
            res = b == DW_LB_LF ? 0 : 2;
        } else if (b == DW_LB_BK || b == DW_LB_CR || b == DW_LB_LF || b == DW_LB_NL) {
            res = 0; /* rule 6. */
        } else if (b == DW_LB_SP || b == DW_LB_ZW) {
            res = 0; /* rule 7. */
        } else if (a == DW_LB_ZW || (before_sp == DW_LB_ZW && a == DW_LB_SP)) {
            res = 1; /* rule 8. */
        } else if (a == DW_LB_ZWJ || joiner_tail) {
            res = 0; /* rule 8a. */
        } else if ((b == DW_LB_CM || b == DW_LB_ZWJ) && a != DW_LB_SP) {
            /* rule 9, but windows lets a break stand between a hyphen and a joiner. */
            res = (b == DW_LB_ZWJ && (a == DW_LB_BA || a == DW_LB_HY) && !(i > 0 && cls[i - 1] == DW_LB_HL)) ? 1 : 0;
        } else if (aa == DW_LB_WJ || b == DW_LB_WJ) {
            res = 0; /* rule 11. */
        } else if (aa == DW_LB_GL) {
            res = 0; /* rule 12. */
        } else if (b == DW_LB_GL && aa != DW_LB_SP && aa != DW_LB_BA && aa != DW_LB_HY) {
            res = 0; /* rule 12a. */
        } else if (b == DW_LB_CL || b == DW_LB_CP || b == DW_LB_EX || b == DW_LB_IS || b == DW_LB_SY) {
            res = 0; /* rule 13. */
        } else if (before_sp == DW_LB_OP) {
            res = 0; /* rule 14. */
        } else if (before_sp == DW_LB_QU && b == DW_LB_OP) {
            res = 0; /* rule 15. */
        } else if ((before_sp == DW_LB_CL || before_sp == DW_LB_CP) && b == DW_LB_NS) {
            res = 0; /* rule 16. */
        } else if (before_sp == DW_LB_B2 && b == DW_LB_B2) {
            res = 0; /* rule 17. */
        } else if (aa == DW_LB_SP) {
            res = 1; /* rule 18. */
        } else if (aa == DW_LB_QU || b == DW_LB_QU) {
            res = 0; /* rule 19. */
        } else if (aa == DW_LB_CB || b == DW_LB_CB) {
            res = 1; /* rule 20. */
        } else if (b == DW_LB_BA || b == DW_LB_HY || b == DW_LB_NS || aa == DW_LB_BB) {
            res = 0; /* rule 21. */
        } else if (i > 0 && cls[i - 1] == DW_LB_HL && (a == DW_LB_HY || a == DW_LB_BA)) {
            res = 0; /* rule 21a. */
        } else if (b == DW_LB_IN && (aa == DW_LB_AL || aa == DW_LB_HL || aa == DW_LB_ID || aa == DW_LB_EB ||
                                     aa == DW_LB_IN || aa == DW_LB_NU || aa == DW_LB_H2 || aa == DW_LB_H3 ||
                                     aa == DW_LB_JL || aa == DW_LB_JV || aa == DW_LB_JT)) {
            res = 0; /* rule 22. */
        } else if (((aa == DW_LB_AL || aa == DW_LB_HL) && b == DW_LB_NU) ||
                   (aa == DW_LB_NU && (b == DW_LB_AL || b == DW_LB_HL))) {
            res = 0; /* rule 23. */
        } else if ((aa == DW_LB_PR && (b == DW_LB_ID || b == DW_LB_EB || b == DW_LB_EM)) ||
                   ((aa == DW_LB_ID || aa == DW_LB_EB || aa == DW_LB_EM) && b == DW_LB_PO)) {
            res = 0; /* rule 23a. */
        } else if ((aa == DW_LB_PR || aa == DW_LB_PO) && (b == DW_LB_AL || b == DW_LB_HL)) {
            res = 0; /* rule 24, one way only. */
        } else if (((aa == DW_LB_CL || aa == DW_LB_CP || aa == DW_LB_NU) && (b == DW_LB_PO || b == DW_LB_PR)) ||
                   ((aa == DW_LB_PO || aa == DW_LB_PR) && b == DW_LB_OP) ||
                   ((aa == DW_LB_PO || aa == DW_LB_PR) && b == DW_LB_NU) ||
                   ((aa == DW_LB_OP || aa == DW_LB_HY || aa == DW_LB_IS || aa == DW_LB_NU || aa == DW_LB_SY) &&
                    b == DW_LB_NU) ||
                   ((aa == DW_LB_PO || aa == DW_LB_PR) && b == DW_LB_HY && i + 2 < n && cls[i + 2] == DW_LB_NU)) {
            res = 0; /* rule 25. */
        } else if ((aa == DW_LB_JL && b == DW_LB_JV) || (aa == DW_LB_JV && b == DW_LB_JT)) {
            res = 0; /* rule 26 as far as windows keeps it. */
        } else if (((aa == DW_LB_JL || aa == DW_LB_JV || aa == DW_LB_JT || aa == DW_LB_H2 || aa == DW_LB_H3) &&
                    b == DW_LB_PO) ||
                   (aa == DW_LB_PR &&
                    (b == DW_LB_JL || b == DW_LB_JV || b == DW_LB_JT || b == DW_LB_H2 || b == DW_LB_H3))) {
            res = 0; /* rule 27. */
        } else if ((aa == DW_LB_AL || aa == DW_LB_HL) && (b == DW_LB_AL || b == DW_LB_HL)) {
            res = 0; /* rule 28. */
        } else if (aa == DW_LB_IS && (b == DW_LB_AL || b == DW_LB_HL)) {
            res = 0; /* rule 29. */
        } else if ((aa == DW_LB_AP && (b == DW_LB_AK || b == DW_LB_AS || b == DW_LB_AL || b == DW_LB_HL)) ||
                   ((aa == DW_LB_AK || aa == DW_LB_AS) && b == DW_LB_VF) ||
                   (aa == DW_LB_VI && (b == DW_LB_AK || b == DW_LB_AS))) {
            res = 0; /* rules 28a approximations. */
        } else if ((aa == DW_LB_AL || aa == DW_LB_HL || aa == DW_LB_NU) && b == DW_LB_OP) {
            res = 0; /* rule 30, wide opening punctuation included. */
        } else if (aa == DW_LB_CP && !(flags[i] & 0x80) && (b == DW_LB_AL || b == DW_LB_HL || b == DW_LB_NU)) {
            res = 0; /* rule 30. */
        } else if (aa == DW_LB_RI && b == DW_LB_RI && (ri_run & 1)) {
            res = 0; /* rule 30a. */
        } else if (aa == DW_LB_EB && b == DW_LB_EM) {
            res = 0; /* rule 30b. */
        } else if ((flags[i] & 0x40) && DwCategory(cp_at[i]) == DW_GC_CN && b == DW_LB_EM) {
            res = 0; /* rule 30b. */
        } else {
            res = 1; /* rule 31. */
        }
        brk[i] = res;
    }
    /* the code units: inside a surrogate pair there is no break, after the last unit of a code point the code point's.
     */
    for (UINT32 i = 0; i < n; i++) {
        UINT32 last = (i + 1 < n ? start[i + 1] : length) - 1;
        brk[i] = i + 1 < n ? brk[i] : 0;
        for (UINT32 u = start[i]; u < last; u++)
            breaks[u] = 0;
        breaks[last] = brk[i];
    }
    /* the end of the text: a required break only after a hard break character. */
    if (n && (cls[n - 1] == DW_LB_BK || cls[n - 1] == DW_LB_LF || cls[n - 1] == DW_LB_NL || cls[n - 1] == DW_LB_CR))
        breaks[length - 1] = 2;
    DwFree(brk);
    DwFree(cp_at);
    DwFree(cls);
    DwFree(flags);
    DwFree(start);
}

/* ------------------------------------------------------------------ bidirectional text */

#define BIDI_MAX_DEPTH 125

typedef struct IsoRun {
    UINT32 *index; /* the characters of an isolating run sequence. */
    UINT32 count;
    UINT32 capacity;
    UINT8 level;
    UINT8 sos, eos;
} IsoRun;

static BOOL is_isolate_initiator(UINT8 c) { return c == DW_BIDI_LRI || c == DW_BIDI_RLI || c == DW_BIDI_FSI; }

static void iso_push(IsoRun *r, UINT32 v)
{
    if (r->count == r->capacity) {
        UINT32 cap = r->capacity ? r->capacity * 2 : 32;
        UINT32 *grown = DwAlloc(cap * sizeof(UINT32));
        if (!grown)
            return;
        if (r->count)
            memcpy(grown, r->index, r->count * sizeof(UINT32));
        DwFree(r->index);
        r->index = grown;
        r->capacity = cap;
    }
    r->index[r->count++] = v;
}

static UINT8 level_dir(UINT8 level) { return (level & 1) ? DW_BIDI_R : DW_BIDI_L; }

/* the first strong class after pos up to the matching pdi, or the paragraph's end: for first strong isolates. */
static UINT8 first_strong(const UINT8 *cls, UINT32 from, UINT32 to)
{
    UINT32 depth = 0;
    for (UINT32 i = from; i < to; i++) {
        UINT8 c = cls[i];
        if (is_isolate_initiator(c))
            depth++;
        else if (c == DW_BIDI_PDI) {
            if (!depth)
                break;
            depth--;
        } else if (!depth) {
            if (c == DW_BIDI_L)
                return 0;
            if (c == DW_BIDI_R || c == DW_BIDI_AL)
                return 1;
        }
    }
    return 0;
}

/* the weak, neutral and implicit rules over one sequence: the resolved levels go back into cls. */
static void resolve_sequence(const UINT32 *cps, UINT8 *cls, const UINT8 *orig, const IsoRun *run)
{
    UINT32 n = run->count;
    UINT8 sos = run->sos, eos = run->eos;
    UINT8 *t = DwAlloc(n);
    if (!t)
        return;
    for (UINT32 k = 0; k < n; k++)
        t[k] = cls[run->index[k]];
    /* W1: marks take the class before them. */
    for (UINT32 k = 0; k < n; k++) {
        if (t[k] == DW_BIDI_NSM) {
            if (k == 0)
                t[k] = sos;
            else if (is_isolate_initiator(t[k - 1]) || t[k - 1] == DW_BIDI_PDI)
                t[k] = DW_BIDI_ON;
            else
                t[k] = t[k - 1];
        }
    }
    /* W2: european numbers after arabic letters become arabic numbers. */
    UINT8 strong = sos;
    for (UINT32 k = 0; k < n; k++) {
        if (t[k] == DW_BIDI_L || t[k] == DW_BIDI_R || t[k] == DW_BIDI_AL)
            strong = t[k];
        else if (t[k] == DW_BIDI_EN && strong == DW_BIDI_AL)
            t[k] = DW_BIDI_AN;
    }
    /* W3. */
    for (UINT32 k = 0; k < n; k++)
        if (t[k] == DW_BIDI_AL)
            t[k] = DW_BIDI_R;
    /* W4: single separators between numbers. */
    for (UINT32 k = 1; k + 1 < n; k++) {
        if (t[k] == DW_BIDI_ES && t[k - 1] == DW_BIDI_EN && t[k + 1] == DW_BIDI_EN)
            t[k] = DW_BIDI_EN;
        else if (t[k] == DW_BIDI_CS && t[k - 1] == DW_BIDI_EN && t[k + 1] == DW_BIDI_EN)
            t[k] = DW_BIDI_EN;
        else if (t[k] == DW_BIDI_CS && t[k - 1] == DW_BIDI_AN && t[k + 1] == DW_BIDI_AN)
            t[k] = DW_BIDI_AN;
    }
    /* W5: terminators next to european numbers. */
    for (UINT32 k = 0; k < n;) {
        if (t[k] == DW_BIDI_ET) {
            UINT32 e = k;
            while (e < n && t[e] == DW_BIDI_ET)
                e++;
            BOOL en = (k > 0 && t[k - 1] == DW_BIDI_EN) || (e < n && t[e] == DW_BIDI_EN);
            if (en)
                for (UINT32 j = k; j < e; j++)
                    t[j] = DW_BIDI_EN;
            k = e;
        } else {
            k++;
        }
    }
    /* W6. */
    for (UINT32 k = 0; k < n; k++)
        if (t[k] == DW_BIDI_ES || t[k] == DW_BIDI_ET || t[k] == DW_BIDI_CS)
            t[k] = DW_BIDI_ON;
    /* W7. */
    strong = sos;
    for (UINT32 k = 0; k < n; k++) {
        if (t[k] == DW_BIDI_L || t[k] == DW_BIDI_R)
            strong = t[k];
        else if (t[k] == DW_BIDI_EN && strong == DW_BIDI_L)
            t[k] = DW_BIDI_L;
    }
    /* N0: paired brackets. */
    {
        typedef struct {
            UINT32 pos;
            UINT32 closer;
        } Open;
        Open stack[63];
        UINT32 depth = 0;
        UINT32 *pairs_open = DwAlloc(n * sizeof(UINT32) * 2 + 8);
        UINT32 npairs = 0;
        BOOL overflow = FALSE;
        if (pairs_open) {
            for (UINT32 k = 0; k < n && !overflow; k++) {
                if (t[k] != DW_BIDI_ON)
                    continue;
                UINT32 cp = cps[run->index[k]];
                BOOL opens;
                UINT32 mate = bracket_of(cp, &opens);
                if (!mate)
                    continue;
                if (opens) {
                    if (depth == 63) {
                        overflow = TRUE;
                        break;
                    }
                    /* canonical equivalents of angle brackets. */
                    stack[depth].pos = k;
                    stack[depth].closer = mate == 0x232A ? 0x3009 : mate;
                    depth++;
                } else {
                    UINT32 me = cp == 0x232A ? 0x3009 : cp;
                    for (UINT32 d = depth; d > 0; d--) {
                        if (stack[d - 1].closer == me) {
                            pairs_open[npairs * 2] = stack[d - 1].pos;
                            pairs_open[npairs * 2 + 1] = k;
                            npairs++;
                            depth = d - 1;
                            break;
                        }
                    }
                }
            }
            /* sort pairs by the opening bracket. */
            for (UINT32 a = 1; a < npairs; a++) {
                UINT32 o = pairs_open[a * 2], c = pairs_open[a * 2 + 1];
                UINT32 b = a;
                while (b > 0 && pairs_open[(b - 1) * 2] > o) {
                    pairs_open[b * 2] = pairs_open[(b - 1) * 2];
                    pairs_open[b * 2 + 1] = pairs_open[(b - 1) * 2 + 1];
                    b--;
                }
                pairs_open[b * 2] = o;
                pairs_open[b * 2 + 1] = c;
            }
            UINT8 embed = level_dir(run->level);
            for (UINT32 p = 0; p < (overflow ? 0 : npairs); p++) {
                UINT32 o = pairs_open[p * 2], c = pairs_open[p * 2 + 1];
                BOOL found_e = FALSE, found_o = FALSE;
                for (UINT32 k = o + 1; k < c; k++) {
                    UINT8 d = t[k];
                    if (d == DW_BIDI_EN || d == DW_BIDI_AN)
                        d = DW_BIDI_R;
                    if (d == DW_BIDI_L || d == DW_BIDI_R) {
                        if (d == embed)
                            found_e = TRUE;
                        else
                            found_o = TRUE;
                    }
                }
                UINT8 resolved = DW_BIDI_ON;
                if (found_e) {
                    resolved = embed;
                } else if (found_o) {
                    UINT8 ctx = sos;
                    for (UINT32 k = o; k > 0; k--) {
                        UINT8 d = t[k - 1];
                        if (d == DW_BIDI_EN || d == DW_BIDI_AN)
                            d = DW_BIDI_R;
                        if (d == DW_BIDI_L || d == DW_BIDI_R) {
                            ctx = d;
                            break;
                        }
                    }
                    resolved = ctx;
                }
                if (resolved != DW_BIDI_ON) {
                    t[o] = t[c] = resolved;
                    /* marks that followed a bracket take its class. */
                    for (UINT32 k = o + 1; k < n && orig[run->index[k]] == DW_BIDI_NSM; k++)
                        t[k] = resolved;
                    for (UINT32 k = c + 1; k < n && orig[run->index[k]] == DW_BIDI_NSM; k++)
                        t[k] = resolved;
                }
            }
        }
        DwFree(pairs_open);
    }
    /* N1 and N2. */
    for (UINT32 k = 0; k < n;) {
        UINT8 c = t[k];
        BOOL neutral = c == DW_BIDI_B || c == DW_BIDI_S || c == DW_BIDI_WS || c == DW_BIDI_ON ||
                       is_isolate_initiator(c) || c == DW_BIDI_PDI;
        if (!neutral) {
            k++;
            continue;
        }
        UINT32 e = k;
        while (e < n) {
            UINT8 d = t[e];
            if (!(d == DW_BIDI_B || d == DW_BIDI_S || d == DW_BIDI_WS || d == DW_BIDI_ON || is_isolate_initiator(d) ||
                  d == DW_BIDI_PDI))
                break;
            e++;
        }
        UINT8 lead = k == 0 ? sos : t[k - 1];
        UINT8 trail = e == n ? eos : t[e];
        if (lead == DW_BIDI_EN || lead == DW_BIDI_AN)
            lead = DW_BIDI_R;
        if (trail == DW_BIDI_EN || trail == DW_BIDI_AN)
            trail = DW_BIDI_R;
        UINT8 res = lead == trail ? lead : level_dir(run->level);
        for (UINT32 j = k; j < e; j++)
            t[j] = res;
        k = e;
    }
    /* I1 and I2. */
    for (UINT32 k = 0; k < n; k++) {
        UINT32 idx = run->index[k];
        UINT8 level = run->level;
        UINT8 c = t[k];
        if (!(level & 1)) {
            if (c == DW_BIDI_R)
                level += 1;
            else if (c == DW_BIDI_AN || c == DW_BIDI_EN)
                level += 2;
        } else if (c == DW_BIDI_L || c == DW_BIDI_EN || c == DW_BIDI_AN) {
            level += 1;
        }
        cls[idx] = level;
    }
    DwFree(t);
}

/*
 * levels of the text by the unicode bidirectional algorithm: explicit gets the embedding level of each code unit before
 * the implicit rules and resolved the final level. base is the paragraph direction, zero for left to right.
 */
void DwBidiLevels(const WCHAR *text, UINT32 length, UINT8 base, UINT8 *explicit_levels, UINT8 *resolved)
{
    if (!length)
        return;
    UINT32 *cps = DwAlloc(length * sizeof(UINT32));
    UINT8 *cls = DwAlloc(length);
    UINT8 *orig = DwAlloc(length);
    UINT8 *lev = DwAlloc(length);
    UINT32 *matching = DwAlloc(length * sizeof(UINT32));
    UINT32 *stack_pos = DwAlloc((length + 1) * sizeof(UINT32));
    if (!cps || !cls || !orig || !lev || !matching || !stack_pos) {
        DwFree(cps);
        DwFree(cls);
        DwFree(orig);
        DwFree(lev);
        DwFree(matching);
        DwFree(stack_pos);
        memset(explicit_levels, base, length);
        memset(resolved, base, length);
        return;
    }
    for (UINT32 i = 0; i < length;) {
        UINT32 units;
        UINT32 cp = DwCodePointAt(text, length, i, &units);
        UINT8 c = DwBidiClass(cp);
        /* windows has no isolates: their characters are neutrals. */
        if (c == DW_BIDI_LRI || c == DW_BIDI_RLI || c == DW_BIDI_FSI || c == DW_BIDI_PDI)
            c = DW_BIDI_ON;
        for (UINT32 u = 0; u < units; u++) {
            cps[i + u] = cp;
            cls[i + u] = c;
        }
        i += units;
    }
    memcpy(orig, cls, length);
    /* matching pdi of each isolate initiator, and the other way. */
    for (UINT32 i = 0; i < length; i++)
        matching[i] = 0xFFFFFFFF;
    {
        UINT32 sp = 0;
        for (UINT32 i = 0; i < length; i++) {
            if (is_isolate_initiator(cls[i]))
                stack_pos[sp++] = i;
            else if (cls[i] == DW_BIDI_PDI && sp) {
                UINT32 o = stack_pos[--sp];
                matching[o] = i;
                matching[i] = o;
            } else if (cls[i] == DW_BIDI_B) {
                sp = 0;
            }
        }
    }
    /* paragraphs end at separators; each is processed with the base direction. */
    UINT32 para_start = 0;
    while (para_start < length) {
        UINT32 para_end = para_start;
        while (para_end < length && cls[para_end] != DW_BIDI_B)
            para_end++;
        if (para_end < length)
            para_end++; /* the separator belongs to the paragraph. */
        UINT8 level = base;
        /* X1 to X8. */
        struct {
            UINT8 level;
            UINT8 override;
            BOOL isolate;
        } stack[BIDI_MAX_DEPTH + 2];
        UINT32 sp = 0;
        stack[0].level = level;
        stack[0].override = DW_BIDI_ON;
        stack[0].isolate = FALSE;
        sp = 1;
        UINT32 overflow_isolate = 0, overflow_embedding = 0, valid_isolate = 0;
        for (UINT32 i = para_start; i < para_end; i++) {
            UINT8 c = cls[i];
            lev[i] = stack[sp - 1].level;
            if (c == DW_BIDI_RLE || c == DW_BIDI_LRE || c == DW_BIDI_RLO || c == DW_BIDI_LRO) {
                UINT8 top = stack[sp - 1].level;
                UINT8 nl = (c == DW_BIDI_RLE || c == DW_BIDI_RLO) ? (UINT8)((top + 1) | 1) : (UINT8)((top + 2) & ~1);
                lev[i] = top;
                if (nl <= BIDI_MAX_DEPTH && !overflow_isolate && !overflow_embedding) {
                    stack[sp].level = nl;
                    stack[sp].override = c == DW_BIDI_RLO ? DW_BIDI_R : c == DW_BIDI_LRO ? DW_BIDI_L : DW_BIDI_ON;
                    stack[sp].isolate = FALSE;
                    sp++;
                    lev[i] = nl;
                } else if (!overflow_isolate) {
                    overflow_embedding++;
                }
                cls[i] = DW_BIDI_BN;
            } else if (is_isolate_initiator(c)) {
                lev[i] = stack[sp - 1].level;
                if (stack[sp - 1].override != DW_BIDI_ON)
                    cls[i] = stack[sp - 1].override; /* an isolate initiator keeps no class under an override. */
                BOOL rtl;
                if (c == DW_BIDI_FSI) {
                    UINT32 end = matching[i] != 0xFFFFFFFF ? matching[i] : para_end;
                    rtl = first_strong(orig, i + 1, end);
                } else {
                    rtl = c == DW_BIDI_RLI;
                }
                UINT8 top = stack[sp - 1].level;
                UINT8 nl = rtl ? (UINT8)((top + 1) | 1) : (UINT8)((top + 2) & ~1);
                if (nl <= BIDI_MAX_DEPTH && !overflow_isolate && !overflow_embedding) {
                    valid_isolate++;
                    stack[sp].level = nl;
                    stack[sp].override = DW_BIDI_ON;
                    stack[sp].isolate = TRUE;
                    sp++;
                } else {
                    overflow_isolate++;
                }
            } else if (c == DW_BIDI_PDI) {
                if (overflow_isolate) {
                    overflow_isolate--;
                } else if (valid_isolate) {
                    overflow_embedding = 0;
                    while (!stack[sp - 1].isolate)
                        sp--;
                    sp--;
                    valid_isolate--;
                }
                lev[i] = stack[sp - 1].level;
                if (stack[sp - 1].override != DW_BIDI_ON)
                    cls[i] = stack[sp - 1].override;
            } else if (c == DW_BIDI_PDF) {
                lev[i] = stack[sp - 1].level;
                if (overflow_isolate) {
                } else if (overflow_embedding) {
                    overflow_embedding--;
                } else if (!stack[sp - 1].isolate && sp >= 2) {
                    sp--;
                }
                cls[i] = DW_BIDI_BN;
            } else if (c == DW_BIDI_B) {
                lev[i] = level;
            } else if (c == DW_BIDI_BN) {
                lev[i] = stack[sp - 1].level;
            } else {
                lev[i] = stack[sp - 1].level;
                if (stack[sp - 1].override != DW_BIDI_ON)
                    cls[i] = stack[sp - 1].override;
            }
        }
        /* levels of the explicit codes: those of the character before, X9 removes them. */
        for (UINT32 i = para_start; i < para_end; i++)
            explicit_levels[i] = lev[i];
        /* the characters X9 keeps, in level runs, then the isolating run sequences of them. */
        UINT32 span = para_end - para_start;
        UINT32 *kept = DwAlloc(span * sizeof(UINT32));
        UINT32 *run_of = DwAlloc(span * sizeof(UINT32));
        UINT32 *run_first = DwAlloc((span + 1) * sizeof(UINT32));
        UINT8 *used = DwAllocZero(span + 1);
        UINT32 kc = 0, runs = 0;
        if (kept && run_of && run_first && used) {
            for (UINT32 i = para_start; i < para_end; i++) {
                UINT8 o = orig[i];
                if (o == DW_BIDI_BN || o == DW_BIDI_LRE || o == DW_BIDI_RLE || o == DW_BIDI_LRO || o == DW_BIDI_RLO ||
                    o == DW_BIDI_PDF)
                    continue;
                if (!kc || lev[kept[kc - 1]] != lev[i])
                    run_first[runs++] = kc;
                run_of[i - para_start] = runs - 1;
                kept[kc++] = i;
            }
            run_first[runs] = kc;
            for (UINT32 r = 0; r < runs; r++) {
                if (used[r])
                    continue;
                UINT32 first_char = kept[run_first[r]];
                if (orig[first_char] == DW_BIDI_PDI && matching[first_char] != 0xFFFFFFFF &&
                    matching[first_char] < first_char)
                    continue;
                IsoRun run = {0};
                run.level = lev[first_char];
                UINT32 cur = r;
                for (;;) {
                    used[cur] = 1;
                    for (UINT32 k = run_first[cur]; k < run_first[cur + 1]; k++)
                        iso_push(&run, kept[k]);
                    UINT32 last = kept[run_first[cur + 1] - 1];
                    if (is_isolate_initiator(orig[last]) && matching[last] != 0xFFFFFFFF && matching[last] < para_end) {
                        cur = run_of[matching[last] - para_start];
                        if (used[cur])
                            break;
                        continue;
                    }
                    break;
                }
                if (!run.count)
                    continue;
                UINT32 first = run.index[0], last = run.index[run.count - 1];
                UINT8 prev_level = base, next_level = base;
                for (UINT32 q = first; q > para_start; q--) {
                    UINT8 o = orig[q - 1];
                    if (o == DW_BIDI_BN || o == DW_BIDI_LRE || o == DW_BIDI_RLE || o == DW_BIDI_LRO ||
                        o == DW_BIDI_RLO || o == DW_BIDI_PDF)
                        continue;
                    prev_level = lev[q - 1];
                    break;
                }
                BOOL unmatched = is_isolate_initiator(orig[last]) && matching[last] == 0xFFFFFFFF;
                if (!unmatched) {
                    for (UINT32 q = last + 1; q < para_end; q++) {
                        UINT8 o = orig[q];
                        if (o == DW_BIDI_BN || o == DW_BIDI_LRE || o == DW_BIDI_RLE || o == DW_BIDI_LRO ||
                            o == DW_BIDI_RLO || o == DW_BIDI_PDF)
                            continue;
                        next_level = lev[q];
                        break;
                    }
                }
                run.sos = level_dir(prev_level > run.level ? prev_level : run.level);
                run.eos = level_dir(next_level > run.level ? next_level : run.level);
                UINT8 *work = DwAlloc(length);
                if (work) {
                    memcpy(work, cls, length);
                    resolve_sequence(cps, work, orig, &run);
                    for (UINT32 q = 0; q < run.count; q++)
                        resolved[run.index[q]] = work[run.index[q]];
                    DwFree(work);
                }
                DwFree(run.index);
            }
        }
        DwFree(kept);
        DwFree(run_of);
        DwFree(run_first);
        DwFree(used);
        /* characters X9 removed take the level of the one before, or the paragraph's at the start. */
        for (UINT32 i = para_start; i < para_end; i++) {
            UINT8 o = orig[i];
            if (o == DW_BIDI_BN || o == DW_BIDI_LRE || o == DW_BIDI_RLE || o == DW_BIDI_LRO || o == DW_BIDI_RLO ||
                o == DW_BIDI_PDF)
                resolved[i] = i > para_start ? resolved[i - 1] : base;
        }
        /* a paragraph separator is at the paragraph level, and so is a segment separator. */
        if (para_end > para_start && orig[para_end - 1] == DW_BIDI_B)
            resolved[para_end - 1] = base;
        for (UINT32 i = para_start; i < para_end; i++)
            if (orig[i] == DW_BIDI_S)
                resolved[i] = base;
        para_start = para_end;
    }
    DwFree(cps);
    DwFree(cls);
    DwFree(orig);
    DwFree(lev);
    DwFree(matching);
    DwFree(stack_pos);
}
