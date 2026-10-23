/*
 * string formats: alignment, trimming, tab stops and measurable ranges, plus the two generic formats.
 */
#include "text_private.h"

/* the default margin: a sixth of an em on each side. */
#define DEFAULT_MARGIN (1.0f / 6.0f)
#define DEFAULT_TRACKING 1.03f

#define TYPOGRAPHIC_FLAGS \
    (StringFormatFlagsNoFitBlackBox | StringFormatFlagsLineLimit | StringFormatFlagsNoClip)

static GStrFmt g_default = {
    .tag = GP_TAG_STRFMT,
    .shared = TRUE,
    .trimming = StringTrimmingCharacter,
    .lead_margin = DEFAULT_MARGIN,
    .trail_margin = DEFAULT_MARGIN,
    .tracking = DEFAULT_TRACKING,
};

static GStrFmt g_typographic = {
    .tag = GP_TAG_STRFMT,
    .shared = TRUE,
    .flags = TYPOGRAPHIC_FLAGS,
    .trimming = StringTrimmingNone,
    .tracking = 1.0f,
};

GStrFmt *gp_strfmt_cast(const void *p) { return GP_CAST(GStrFmt, GP_TAG_STRFMT, p); }

const GStrFmt *gp_strfmt_default(void) { return &g_default; }

/* ------------------------------------------------------------------ making and copying */

static GStrFmt *format_new(void)
{
    GStrFmt *f = gp_alloc_zero(sizeof(*f));
    if (!f)
        return nullptr;
    f->tag = GP_TAG_STRFMT;
    f->trimming = StringTrimmingCharacter;
    f->lead_margin = DEFAULT_MARGIN;
    f->trail_margin = DEFAULT_MARGIN;
    f->tracking = DEFAULT_TRACKING;
    return f;
}

WINGDIPAPI GpStatus WINAPI GdipCreateStringFormat(INT flags, LANGID language, GpStringFormat **format)
{
    GP_REQUIRE_INIT();
    if (!format)
        return InvalidParameter;
    GStrFmt *f = format_new();
    if (!f)
        return OutOfMemory;
    f->flags = flags;
    f->digit_language = language;
    *format = f;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipStringFormatGetGenericDefault(GpStringFormat **format)
{
    if (!format)
        return InvalidParameter;
    *format = &g_default;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipStringFormatGetGenericTypographic(GpStringFormat **format)
{
    if (!format)
        return InvalidParameter;
    *format = &g_typographic;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteStringFormat(GpStringFormat *format)
{
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    if (f->shared)
        return Ok;
    f->tag = 0;
    gp_free(f->tabs);
    gp_free(f);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneStringFormat(const GpStringFormat *format, GpStringFormat **clone)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !clone)
        return InvalidParameter;
    GStrFmt *copy = gp_alloc(sizeof(*copy));
    if (!copy)
        return OutOfMemory;
    *copy = *f;
    copy->shared = FALSE;
    copy->tabs = nullptr;
    if (f->tab_count) {
        copy->tabs = gp_dup(f->tabs, sizeof(float) * (SIZE_T)f->tab_count);
        if (!copy->tabs) {
            gp_free(copy);
            return OutOfMemory;
        }
    }
    *clone = copy;
    return Ok;
}

/* ------------------------------------------------------------------ values */

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatFlags(GpStringFormat *format, INT flags)
{
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    f->flags = flags;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatFlags(const GpStringFormat *format, INT *flags)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !flags)
        return InvalidParameter;
    *flags = f->flags;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatAlign(GpStringFormat *format, StringAlignment align)
{
    if ((UINT)align > StringAlignmentFar)
        return InvalidParameter;
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    f->align = align;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatAlign(const GpStringFormat *format, StringAlignment *align)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !align)
        return InvalidParameter;
    *align = f->align;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatLineAlign(GpStringFormat *format, StringAlignment align)
{
    if ((UINT)align > StringAlignmentFar)
        return InvalidParameter;
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    f->line_align = align;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatLineAlign(const GpStringFormat *format, StringAlignment *align)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !align)
        return InvalidParameter;
    *align = f->line_align;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatTrimming(GpStringFormat *format, StringTrimming trimming)
{
    if ((UINT)trimming > StringTrimmingEllipsisPath)
        return InvalidParameter;
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    f->trimming = trimming;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTrimming(const GpStringFormat *format, StringTrimming *trimming)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !trimming)
        return InvalidParameter;
    *trimming = f->trimming;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatHotkeyPrefix(GpStringFormat *format, INT prefix)
{
    if ((UINT)prefix > HotkeyPrefixHide)
        return InvalidParameter;
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    f->hotkey = prefix;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatHotkeyPrefix(const GpStringFormat *format, INT *prefix)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !prefix)
        return InvalidParameter;
    *prefix = f->hotkey;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatDigitSubstitution(GpStringFormat *format, LANGID language,
                                                                StringDigitSubstitute substitute)
{
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    f->digit_language = language;
    f->digit_substitute = substitute;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatDigitSubstitution(const GpStringFormat *format, LANGID *language,
                                                                StringDigitSubstitute *substitute)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    if (language)
        *language = f->digit_language;
    if (substitute)
        *substitute = f->digit_substitute;
    return Ok;
}

/* ------------------------------------------------------------------ tab stops and ranges */

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatTabStops(GpStringFormat *format, REAL first, INT count, const REAL *stops)
{
    if (!stops)
        return InvalidParameter;
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    if (count <= 0)
        return Ok;
    if (first < 0.0f)
        return NotImplemented;
    for (INT i = 0; i < count; i++) {
        if (stops[i] < 0.0f)
            return NotImplemented;
    }
    float *copy = gp_dup(stops, sizeof(float) * (SIZE_T)count);
    if (!copy)
        return OutOfMemory;
    gp_free(f->tabs);
    f->tabs = copy;
    f->tab_count = count;
    f->first_tab = first;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTabStopCount(const GpStringFormat *format, INT *count)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !count)
        return InvalidParameter;
    *count = f->tab_count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTabStops(const GpStringFormat *format, INT count, REAL *first,
                                                       REAL *stops)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !first || !stops)
        return InvalidParameter;
    *first = f->first_tab;
    INT n = count < f->tab_count ? count : f->tab_count;
    if (n > 0)
        memcpy(stops, f->tabs, sizeof(float) * (SIZE_T)n);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatMeasurableCharacterRangeCount(const GpStringFormat *format, INT *count)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !count)
        return InvalidParameter;
    *count = f->range_count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatMeasurableCharacterRanges(GpStringFormat *format, INT count,
                                                                        const CharacterRange *ranges)
{
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f || !ranges)
        return InvalidParameter;
    if (count > GP_MAX_RANGES)
        return ValueOverflow;
    if (count < 0)
        count = 0;
    if (count)
        memcpy(f->ranges, ranges, sizeof(CharacterRange) * (SIZE_T)count);
    f->range_count = count;
    return Ok;
}
