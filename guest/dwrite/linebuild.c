/*
 * the text layout, part two: lines. clusters are broken into lines, trimmed, reordered for bidirectional text,
 * aligned, and measured.
 */
#include "layout_private.h"

#define EPS 0.0001f

static FLOAT sign_width(IDWriteInlineObject *sign)
{
    DWRITE_INLINE_OBJECT_METRICS m;
    if (sign && SUCCEEDED(sign->lpVtbl->GetMetrics(sign, &m)))
        return m.width;
    return 0;
}

/* the ascent and descent of a line of the default font, for lines with nothing on them. */
static void default_metrics(DwLayout *l, FLOAT *ascent, FLOAT *descent)
{
    static const WCHAR space[] = {' ', 0};
    IDWriteFontFace *face = nullptr;
    FLOAT scale = 1.0f;
    *ascent = *descent = 0;
    if (FAILED(DwFontForText(l->factory, &l->fmt, space, 1, &face, &scale)) || !face)
        return;
    DwRun r;
    memset(&r, 0, sizeof(r));
    DWRITE_FONT_METRICS m;
    if (l->gdi) {
        if (FAILED(face->lpVtbl->GetGdiCompatibleMetrics(face, l->fmt.size * scale, l->ppd, &l->transform, &m)))
            face->lpVtbl->GetMetrics(face, &m);
    } else {
        face->lpVtbl->GetMetrics(face, &m);
    }
    FLOAT k = m.designUnitsPerEm ? l->fmt.size * scale / (FLOAT)m.designUnitsPerEm : 0;
    *ascent =
        (FLOAT)(m.ascent + (l->fmt.spacing.fontLineGapUsage == DWRITE_FONT_LINE_GAP_USAGE_DISABLED || l->gdi ? 0 : m.lineGap)) *
        k;
    *descent = (FLOAT)m.descent * k;
    face->lpVtbl->Release(face);
}

typedef struct Entry {
    UINT32 cluster; /* DW_OPEN_END for the sign. */
    UINT8 level;
    FLOAT width;
} Entry;

/* reorders entries by the unicode rule L2. */
static void reorder(Entry *e, UINT32 n)
{
    UINT8 highest = 0, lowest_odd = 255;
    for (UINT32 i = 0; i < n; i++) {
        if (e[i].level > highest)
            highest = e[i].level;
        if ((e[i].level & 1) && e[i].level < lowest_odd)
            lowest_odd = e[i].level;
    }
    for (int level = highest; level >= lowest_odd && level > 0; level--) {
        for (UINT32 i = 0; i < n;) {
            if (e[i].level < level) {
                i++;
                continue;
            }
            UINT32 j = i;
            while (j < n && e[j].level >= level)
                j++;
            for (UINT32 a = i, z = j - 1; a < z; a++, z--) {
                Entry t = e[a];
                e[a] = e[z];
                e[z] = t;
            }
            i = j;
        }
    }
}

static BOOL cluster_ws(const DwCluster *c) { return (c->flags & DWC_WHITESPACE) != 0; }

HRESULT DwLayoutBuildLines(DwLayout *l)
{
    DwFmt *f = &l->fmt;
    FLOAT avail = l->max_width;
    UINT32 ncl = l->cluster_count;
    DwCluster *cl = l->clusters;
    UINT8 para_level = DW_READS_RTL(l) ? 1 : 0;
    for (UINT32 i = 0; i < ncl; i++)
        cl[i].extra = 0;
    UINT32 cap = 8;
    DwLine *lines = DwAllocZero(cap * sizeof(DwLine));
    UINT32 nlines = 0;
    if (!lines)
        return E_OUTOFMEMORY;
    UINT32 start = 0;
    BOOL ended_with_newline = FALSE;
    do {
        FLOAT x = 0;
        UINT32 i = start;
        INT64 last_break = -1;
        BOOL newline = FALSE;
        for (; i < ncl; i++) {
            DwCluster *c = &cl[i];
            FLOAT w = c->width;
            if (c->flags & DWC_TAB) {
                FLOAT stop = f->tab;
                FLOAT n = (FLOAT)(INT64)(x / stop + 0.0001f);
                w = (n + 1.0f) * stop - x;
                c->width = w;
            }
            if (c->flags & DWC_NEWLINE) {
                c->width = 0;
                i++;
                newline = TRUE;
                break;
            }
            BOOL ws = cluster_ws(c);
            if (!ws && f->wrap != DWRITE_WORD_WRAPPING_NO_WRAP && x + w > avail + EPS && i > start) {
                if (f->wrap == DWRITE_WORD_WRAPPING_WHOLE_WORD && last_break < (INT64)start) {
                    /* a word kept whole overflows. */
                } else if (f->wrap == DWRITE_WORD_WRAPPING_CHARACTER || last_break < (INT64)start) {
                    break;
                } else {
                    i = (UINT32)last_break + 1;
                    break;
                }
            }
            x += w;
            if (c->flags & DWC_WRAP)
                last_break = i;
        }
        if (nlines == cap) {
            DwLine *grown = DwAllocZero(cap * 2 * sizeof(DwLine));
            if (!grown) {
                DwFree(lines);
                return E_OUTOFMEMORY;
            }
            memcpy(grown, lines, cap * sizeof(DwLine));
            DwFree(lines);
            lines = grown;
            cap *= 2;
        }
        DwLine *ln = &lines[nlines++];
        ln->first_cluster = start;
        ln->cluster_count = i - start;
        ln->start = i > start ? cl[start].start : (start < ncl ? cl[start].start : l->length);
        if (i > start)
            ln->length = cl[i - 1].start + cl[i - 1].length - ln->start;
        /* trailing whitespace, and the newline inside it. */
        UINT32 tws = 0;
        while (tws < ln->cluster_count && cluster_ws(&cl[i - 1 - tws]))
            tws++;
        for (UINT32 k = 0; k < tws; k++)
            ln->tws_length += cl[i - 1 - k].length;
        if (ln->cluster_count && (cl[i - 1].flags & DWC_NEWLINE)) {
            ln->newline_length = cl[i - 1].length;
            if (cl[i - 1].length == 1 && l->text[cl[i - 1].start] == 0x0A && ln->cluster_count > 1 &&
                l->text[cl[i - 2].start] == 0x0D && cl[i - 2].length == 1)
                ln->newline_length = 2;
        }
        for (UINT32 k = 0; k < ln->cluster_count; k++) {
            if (k < ln->cluster_count - tws)
                ln->width += cl[start + k].width;
            else
                ln->tws_width += cl[start + k].width;
        }
        ended_with_newline = newline;
        start = i;
    } while (start < ncl || (ended_with_newline && start == ncl && ncl));
    /* a text that ends with a newline gets an empty last line, the loop adds it. */
    l->lines = lines;
    l->line_count = nlines;

    /* line heights. */
    FLOAT def_asc = 0, def_desc = 0;
    BOOL have_default = FALSE;
    /* lines of GDI compatible layouts are as high as the text, without the line gap. */
    BOOL usage_gap = f->spacing.fontLineGapUsage != DWRITE_FONT_LINE_GAP_USAGE_DISABLED && !l->gdi;
    for (UINT32 li = 0; li < nlines; li++) {
        DwLine *ln = &lines[li];
        FLOAT asc = 0, desc = 0;
        UINT32 last_run = DW_OPEN_END;
        for (UINT32 k = 0; k < ln->cluster_count; k++) {
            const DwCluster *c = &cl[ln->first_cluster + k];
            if (c->run == last_run)
                continue;
            last_run = c->run;
            const DwRun *r = &l->runs[c->run];
            FLOAT a, d;
            if (r->object) {
                a = r->object_metrics.baseline;
                d = r->object_metrics.height - r->object_metrics.baseline;
            } else {
                a = r->ascent + (usage_gap ? r->line_gap : 0);
                d = r->descent;
            }
            if (a > asc)
                asc = a;
            if (d > desc)
                desc = d;
        }
        if (!ln->cluster_count) {
            if (!have_default) {
                default_metrics(l, &def_asc, &def_desc);
                have_default = TRUE;
            }
            /* an empty line after a newline takes the font of the text before it when there is one. */
            asc = def_asc;
            desc = def_desc;
            if (li > 0 && lines[li - 1].cluster_count) {
                const DwCluster *prev = &cl[lines[li - 1].first_cluster + lines[li - 1].cluster_count - 1];
                const DwRun *r = &l->runs[prev->run];
                if (!r->object) {
                    asc = r->ascent + (usage_gap ? r->line_gap : 0);
                    desc = r->descent;
                }
            }
        }
        ln->baseline = asc;
        ln->height = asc + desc;
        if (f->spacing.method == DWRITE_LINE_SPACING_METHOD_UNIFORM) {
            ln->height = f->spacing.height;
            ln->baseline = f->spacing.baseline;
        } else if (f->spacing.method == DWRITE_LINE_SPACING_METHOD_PROPORTIONAL) {
            ln->height = (asc + desc) * f->spacing.height;
            ln->baseline = asc * f->spacing.baseline;
            ln->leading_after = ln->height - ln->baseline - desc;
        }
    }

    /* the height the layout gives, and the lines below it. */
    FLOAT top = 0;
    BOOL trimming = f->trimming.granularity != DWRITE_TRIMMING_GRANULARITY_NONE;
    UINT32 cut = nlines;
    for (UINT32 li = 0; li < nlines; li++) {
        if (trimming && li > 0 && top + lines[li].height > l->max_height + EPS) {
            cut = li;
            break;
        }
        lines[li].top = top;
        top += lines[li].height;
    }
    for (UINT32 li = cut; li < nlines; li++) {
        lines[li].hidden = TRUE;
        lines[li].trimmed = TRUE;
        lines[li].height = 0;
        lines[li].baseline = 0;
        lines[li].top = top;
        lines[li].width = lines[li].tws_width = 0;
    }

    /* alignment, trimming, justification and the order of clusters of each line. */
    FLOAT total_height = 0;
    UINT32 max_runs = 1;
    for (UINT32 li = 0; li < nlines; li++)
        total_height += lines[li].height;
    for (UINT32 li = 0; li < nlines; li++) {
        DwLine *ln = &lines[li];
        if (ln->hidden)
            continue;
        UINT32 first = ln->first_cluster, count = ln->cluster_count;
        UINT32 content = count;
        while (content > 0 && cluster_ws(&cl[first + content - 1]))
            content--;
        BOOL force = cut < nlines && li + 1 == cut;
        UINT32 head_end = content, tail_start = content;
        FLOAT head_w = 0, tail_w = 0, sw = 0;
        if (trimming && (force || ln->width > avail + EPS)) {
            IDWriteInlineObject *sign = f->sign;
            sw = sign_width(sign);
            FLOAT allowed = avail - sw;
            /* a delimiter keeps the end of the text. */
            if (f->trimming.delimiterCount) {
                UINT32 found = 0;
                UINT32 k = content;
                while (k > 0) {
                    k--;
                    if (l->text[cl[first + k].start] == (WCHAR)f->trimming.delimiter) {
                        found++;
                        if (found == f->trimming.delimiterCount) {
                            tail_start = k;
                            break;
                        }
                    }
                }
                if (found == f->trimming.delimiterCount) {
                    FLOAT w = 0;
                    for (UINT32 q = tail_start; q < content; q++)
                        w += cl[first + q].width;
                    if (w <= allowed) {
                        tail_w = w;
                    } else {
                        tail_start = content;
                    }
                } else {
                    tail_start = content;
                }
            }
            FLOAT head_allowed = allowed - tail_w;
            FLOAT w = 0;
            UINT32 k = 0;
            while (k < tail_start && w + cl[first + k].width <= head_allowed + EPS) {
                w += cl[first + k].width;
                k++;
            }
            head_end = k;
            if (f->trimming.granularity == DWRITE_TRIMMING_GRANULARITY_WORD && head_end < tail_start) {
                UINT32 j = head_end;
                while (j > 0 && !(cl[first + j - 1].flags & DWC_WRAP))
                    j--;
                if (j > 0)
                    head_end = j;
                while (head_end > 0 && cluster_ws(&cl[first + head_end - 1]))
                    head_end--;
            }
            head_w = 0;
            for (UINT32 q = 0; q < head_end; q++)
                head_w += cl[first + q].width;
            if (head_end != tail_start || tail_start != content || force) {
                ln->trimmed = TRUE;
                ln->trim_first = first + head_end;
                ln->trim_count = tail_start - head_end;
                ln->sign = sign;
                if (sign)
                    sign->lpVtbl->AddRef(sign);
                ln->sign_width = sw;
                ln->width = head_w + sw + tail_w;
                ln->tws_width = 0;
            }
        }
        /* justification. */
        if (f->text_align == DWRITE_TEXT_ALIGNMENT_JUSTIFIED && f->wrap != DWRITE_WORD_WRAPPING_NO_WRAP &&
            !ln->trimmed && ln->newline_length == 0 && li + 1 < nlines && ln->width < avail - EPS) {
            UINT32 spaces = 0;
            for (UINT32 q = 0; q < content; q++)
                if ((cl[first + q].flags & DWC_WHITESPACE) && !(cl[first + q].flags & (DWC_TAB | DWC_NO_VISUAL)))
                    spaces++;
            if (spaces) {
                FLOAT each = (avail - ln->width) / (FLOAT)spaces;
                for (UINT32 q = 0; q < content; q++) {
                    DwCluster *c = &cl[first + q];
                    if ((c->flags & DWC_WHITESPACE) && !(c->flags & (DWC_TAB | DWC_NO_VISUAL))) {
                        c->width += each;
                        c->extra = each;
                    }
                }
                ln->width = avail;
            }
        }
        /* the entries in logical order: head, sign, tail and trailing whitespace. */
        Entry *entries = DwAlloc((count + 2) * sizeof(Entry));
        if (!entries)
            return E_OUTOFMEMORY;
        UINT32 ne = 0;
        UINT32 visible_end = ln->trimmed ? content : count;
        for (UINT32 q = 0; q < visible_end; q++) {
            UINT32 ci = first + q;
            if (ln->trimmed) {
                if (ci >= ln->trim_first && ci < ln->trim_first + ln->trim_count) {
                    if (ci == ln->trim_first && (ln->sign || 1)) {
                        if (ln->sign) {
                            entries[ne].cluster = DW_OPEN_END;
                            entries[ne].level = para_level;
                            entries[ne].width = ln->sign_width;
                            ne++;
                        }
                    }
                    continue;
                }
            }
            entries[ne].cluster = ci;
            entries[ne].level = cl[ci].level;
            entries[ne].width = cl[ci].width;
            ne++;
        }
        if (ln->trimmed && ln->trim_count == 0 && ln->sign) {
            /* the sign after the last cluster of a line trimmed for the lines below. */
            entries[ne].cluster = DW_OPEN_END;
            entries[ne].level = para_level;
            entries[ne].width = ln->sign_width;
            ne++;
        }
        /* rule L1: whitespace at the end and before tabs go back to the paragraph level. */
        {
            UINT32 q = ne;
            while (q > 0 && entries[q - 1].cluster != DW_OPEN_END && cluster_ws(&cl[entries[q - 1].cluster])) {
                entries[q - 1].level = para_level;
                q--;
            }
            for (q = 0; q < ne; q++) {
                if (entries[q].cluster != DW_OPEN_END && (cl[entries[q].cluster].flags & DWC_TAB)) {
                    entries[q].level = para_level;
                    UINT32 b = q;
                    while (b > 0 && entries[b - 1].cluster != DW_OPEN_END && cluster_ws(&cl[entries[b - 1].cluster])) {
                        entries[b - 1].level = para_level;
                        b--;
                    }
                }
            }
        }
        UINT32 runs_count = 0;
        for (UINT32 q = 0; q < ne; q++)
            if (!q || entries[q].level != entries[q - 1].level)
                runs_count++;
        ln->level_runs = runs_count ? runs_count : 1;
        if (ln->level_runs > max_runs)
            max_runs = ln->level_runs;
        reorder(entries, ne);
        /* alignment of the content box. */
        BOOL rtl = para_level != 0;
        FLOAT cx0;
        switch (f->text_align) {
        case DWRITE_TEXT_ALIGNMENT_CENTER:
            cx0 = (avail - ln->width) / 2;
            break;
        case DWRITE_TEXT_ALIGNMENT_TRAILING:
            cx0 = rtl ? 0 : avail - ln->width;
            break;
        case DWRITE_TEXT_ALIGNMENT_JUSTIFIED:
            cx0 = rtl ? avail - ln->width : 0;
            break;
        default:
            cx0 = rtl ? avail - ln->width : 0;
            break;
        }
        ln->x = cx0;
        FLOAT pen = rtl ? cx0 - ln->tws_width : cx0;
        ln->items = DwAlloc((ne + 1) * sizeof(DwItem));
        if (!ln->items) {
            DwFree(entries);
            return E_OUTOFMEMORY;
        }
        ln->item_count = ne;
        for (UINT32 q = 0; q < ne; q++) {
            ln->items[q].cluster = entries[q].cluster;
            ln->items[q].x = pen;
            ln->items[q].width = entries[q].width;
            if (entries[q].cluster == DW_OPEN_END)
                ln->sign_x = pen;
            pen += entries[q].width;
        }
        DwFree(entries);
    }

    /* vertical alignment and the metrics of the whole. */
    FLOAT offset = 0;
    if (f->para_align == DWRITE_PARAGRAPH_ALIGNMENT_FAR)
        offset = l->max_height - total_height;
    else if (f->para_align == DWRITE_PARAGRAPH_ALIGNMENT_CENTER)
        offset = (l->max_height - total_height) / 2;
    FLOAT left = 0, width = 0, wtws = 0;
    BOOL first_line = TRUE;
    for (UINT32 li = 0; li < nlines; li++) {
        DwLine *ln = &lines[li];
        ln->top += offset;
        if (ln->hidden)
            continue;
        FLOAT line_left = ln->item_count ? ln->items[0].x : ln->x;
        if (first_line || line_left < left)
            left = line_left;
        first_line = FALSE;
        if (ln->width > width)
            width = ln->width;
        if (ln->width + ln->tws_width > wtws)
            wtws = ln->width + ln->tws_width;
    }
    memset(&l->metrics, 0, sizeof(l->metrics));
    l->metrics.left = left;
    l->metrics.top = offset;
    l->metrics.width = width;
    l->metrics.widthIncludingTrailingWhitespace = wtws;
    l->metrics.height = total_height;
    l->metrics.heightIncludingTrailingWhitespace = total_height;
    l->metrics.layoutWidth = l->max_width;
    l->metrics.layoutHeight = l->max_height;
    l->metrics.maxBidiReorderingDepth = max_runs;
    l->metrics.lineCount = nlines;
    l->lines_valid = TRUE;
    return S_OK;
}

HRESULT DwLayoutEnsure(DwLayout *l)
{
    if (l->lines_valid)
        return l->build_status;
    DWRITE_READING_DIRECTION r = l->fmt.reading;
    DWRITE_FLOW_DIRECTION fl = l->fmt.flow;
    BOOL vertical = r == DWRITE_READING_DIRECTION_TOP_TO_BOTTOM || r == DWRITE_READING_DIRECTION_BOTTOM_TO_TOP;
    BOOL flow_vertical = fl == DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM || fl == DWRITE_FLOW_DIRECTION_BOTTOM_TO_TOP;
    if (vertical == flow_vertical)
        return DWRITE_E_FLOWDIRECTIONCONFLICTS;
    HRESULT hr = S_OK;
    /* vertical text is laid out along its lines as horizontal text and then turned: the sizes swap here and the
       metrics and geometry swap back when they are asked for. */
    FLOAT width = l->max_width, height = l->max_height;
    if (vertical) {
        l->max_width = height;
        l->max_height = width;
    }
    l->vertical = vertical;
    if (!l->runs_valid)
        hr = DwLayoutBuildRuns(l);
    if (SUCCEEDED(hr))
        hr = DwLayoutBuildLines(l);
    if (vertical) {
        l->max_width = width;
        l->max_height = height;
        DWRITE_TEXT_METRICS1 *m = &l->metrics;
        FLOAT t = m->left;
        m->left = m->top;
        m->top = t;
        FLOAT trailing = m->widthIncludingTrailingWhitespace;
        t = m->width;
        m->width = m->height;
        m->height = t;
        m->widthIncludingTrailingWhitespace = m->width;
        m->layoutWidth = width;
        m->layoutHeight = height;
        if (r == DWRITE_READING_DIRECTION_BOTTOM_TO_TOP)
            m->top = height - trailing;
        if (fl == DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT)
            m->left = width - m->left - m->width;
    }
    return hr;
}
