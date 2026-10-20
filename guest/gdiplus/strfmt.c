/*
 * string formats: how text is laid out in its rectangle, with the generic default and typographic formats that
 * every program shares and the calls that set and read each part of a format.
 */
#include "text_private.h"

/* the margins and tracking of gdi+: an sixth of an em on each side, and a widening of three percent. */
#define MARGIN_DEFAULT (1.0f / 6.0f)
#define TRACKING_DEFAULT 1.03f

static void strfmt_init(GStrFmt *f)
{
    memset(f, 0, sizeof(*f));
    f->tag = GP_TAG_STRFMT;
    f->lead_margin = f->trail_margin = MARGIN_DEFAULT;
    f->tracking = TRACKING_DEFAULT;
    f->trimming = StringTrimmingCharacter;
}

/* the two shared formats are built on first use. */
static GStrFmt g_default, g_typographic;
static volatile LONG g_generic_ready;

static void generics_init(void)
{
    if (g_generic_ready)
        return;
    strfmt_init(&g_default);
    g_default.generic = TRUE;
    strfmt_init(&g_typographic);
    g_typographic.generic = TRUE;
    g_typographic.flags = StringFormatFlagsNoFitBlackBox | 0x2000 | StringFormatFlagsNoClip;
    g_typographic.lead_margin = g_typographic.trail_margin = 0.0f;
    g_typographic.tracking = 1.0f;
    g_typographic.trimming = StringTrimmingNone;
    InterlockedExchange(&g_generic_ready, 1);
}

GStrFmt *gp_strfmt_cast(const void *p)
{
    return GP_CAST(GStrFmt, GP_TAG_STRFMT, p);
}

const GStrFmt *gp_strfmt_default(void)
{
    generics_init();
    return &g_default;
}

WINGDIPAPI GpStatus WINAPI GdipCreateStringFormat(INT flags, LANGID language, GpStringFormat **format)
{
    GP_REQUIRE_INIT();
    if (!format)
        return InvalidParameter;
    GStrFmt *f = gp_alloc(sizeof(*f));
    if (!f) {
        *format = nullptr;
        return OutOfMemory;
    }
    strfmt_init(f);
    f->flags = flags;
    f->language = language;
    *format = f;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipStringFormatGetGenericDefault(GpStringFormat **format)
{
    if (!format)
        return InvalidParameter;
    generics_init();
    *format = &g_default;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipStringFormatGetGenericTypographic(GpStringFormat **format)
{
    if (!format)
        return InvalidParameter;
    generics_init();
    *format = &g_typographic;
    return Ok;
}

static void strfmt_release(GStrFmt *f)
{
    gp_free(f->tabs);
    gp_free(f->ranges);
    f->tag = 0;
    gp_free(f);
}

WINGDIPAPI GpStatus WINAPI GdipDeleteStringFormat(GpStringFormat *format)
{
    if (!format)
        return InvalidParameter;
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    /* the shared formats stay. */
    if (!f->generic)
        strfmt_release(f);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneStringFormat(const GpStringFormat *format, GpStringFormat **clone)
{
    if (!clone)
        return InvalidParameter;
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    GStrFmt *copy = gp_alloc(sizeof(*copy));
    if (!copy)
        return OutOfMemory;
    *copy = *f;
    copy->generic = FALSE;
    copy->tabs = nullptr;
    copy->ranges = nullptr;
    if (f->tabs) {
        copy->tabs = gp_dup(f->tabs, sizeof(float) * (SIZE_T)f->tab_count);
        if (!copy->tabs) {
            strfmt_release(copy);
            return OutOfMemory;
        }
    }
    if (f->ranges) {
        copy->ranges = gp_dup(f->ranges, sizeof(CharacterRange) * (SIZE_T)f->range_count);
        if (!copy->ranges) {
            strfmt_release(copy);
            return OutOfMemory;
        }
    }
    *clone = copy;
    return Ok;
}

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
    if (!flags || !f)
        return InvalidParameter;
    *flags = f->flags;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatAlign(GpStringFormat *format, StringAlignment align)
{
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f || (UINT)align > StringAlignmentFar)
        return InvalidParameter;
    f->align = align;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatAlign(const GpStringFormat *format, StringAlignment *align)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!align || !f)
        return InvalidParameter;
    *align = f->align;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatLineAlign(GpStringFormat *format, StringAlignment align)
{
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f || (UINT)align > StringAlignmentFar)
        return InvalidParameter;
    f->line_align = align;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatLineAlign(const GpStringFormat *format, StringAlignment *align)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!align || !f)
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
    if (!trimming || !f)
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
    if (!prefix || !f)
        return InvalidParameter;
    *prefix = f->hotkey;
    return Ok;
}

/* a count of zero or less keeps the stops, negative stops are refused the way gdi+ does, with NotImplemented. */
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatTabStops(GpStringFormat *format, REAL first, INT count,
                                                       const REAL *stops)
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
    for (INT i = 0; i < count; i++)
        if (stops[i] < 0.0f)
            return NotImplemented;
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
    if (!count || !f)
        return InvalidParameter;
    *count = f->tab_count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTabStops(const GpStringFormat *format, INT count, REAL *first,
                                                       REAL *stops)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!first || !stops || !f)
        return InvalidParameter;
    INT n = count < f->tab_count ? count : f->tab_count;
    if (n > 0)
        memcpy(stops, f->tabs, sizeof(float) * (SIZE_T)n);
    *first = f->first_tab;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetStringFormatDigitSubstitution(GpStringFormat *format, LANGID language,
                                                                StringDigitSubstitute substitute)
{
    GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    f->digit_substitute = substitute;
    f->digit_language = language;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatDigitSubstitution(const GpStringFormat *format, LANGID *language,
                                                                StringDigitSubstitute *substitute)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!f)
        return InvalidParameter;
    if (substitute)
        *substitute = f->digit_substitute;
    if (language)
        *language = f->digit_language;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetStringFormatMeasurableCharacterRangeCount(const GpStringFormat *format, INT *count)
{
    const GStrFmt *f = gp_strfmt_cast(format);
    if (!count || !f)
        return InvalidParameter;
    *count = f->range_count;
    return Ok;
}

/* at most 32 ranges, and none clears them. */
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatMeasurableCharacterRanges(GpStringFormat *format, INT count,
                                                                        const CharacterRange *ranges)
{
    GStrFmt *f = gp_strfmt_cast(format);
    if (!ranges || !f)
        return InvalidParameter;
    if (count > 32)
        return ValueOverflow;
    CharacterRange *copy = nullptr;
    if (count > 0) {
        copy = gp_dup(ranges, sizeof(CharacterRange) * (SIZE_T)count);
        if (!copy)
            return OutOfMemory;
    }
    gp_free(f->ranges);
    f->ranges = copy;
    f->range_count = copy ? count : 0;
    return Ok;
}
