/* property schemas: the .propdesc reader, the cache image it is kept in and the registry of schema files. */
#include "propsys_private.h"
#include <crypto/crypto.h>

#define SCHEMA_NAMESPACE L"http://schemas.microsoft.com/windows/2006/propertydescription"
#define SCHEMA_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\PropertySchema"
#define ERROR_FLAG 0x40320

MIME_DEFINE_GUID(CACHEID_SystemSchema, 0xddf571f2, 0xbe98, 0x426d, 0x82, 0x88, 0x1a, 0x9a, 0x39, 0xc3, 0xfd, 0xa2);

/* the format numbers the display info keeps, shared with desc.c. */
#define NUMBER_GENERAL 0
#define NUMBER_DURATION 100
#define BOOLEAN_YESNO 200
#define BOOLEAN_ONOFF 201
#define BOOLEAN_TRUEFALSE 202

/* ------------------------------------------------------------------ the image */

typedef struct {
    BYTE *base;
    DWORD size, capacity;
    BOOL failed;
} ARENA;

static DWORD arena_alloc(ARENA *a, DWORD bytes)
{
    DWORD at = (a->size + 7) & ~7u;
    if (a->failed || at + bytes < at) {
        a->failed = TRUE;
        return 0;
    }
    if (at + bytes > a->capacity) {
        DWORD capacity = a->capacity ? a->capacity : 4096;
        while (capacity < at + bytes)
            capacity *= 2;
        BYTE *grown = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity);
        if (!grown) {
            a->failed = TRUE;
            return 0;
        }
        if (a->base) {
            memcpy(grown, a->base, a->size);
            HeapFree(GetProcessHeap(), 0, a->base);
        }
        a->base = grown;
        a->capacity = capacity;
    }
    a->size = at + bytes;
    return at;
}

/* zero for a missing string, an empty string is kept. */
static DWORD arena_string(ARENA *a, LPCWSTR s)
{
    if (!s)
        return 0;
    SIZE_T length = wcslen(s);
    if (length > 0x10000)
        length = 0x10000;
    DWORD at = arena_alloc(a, (DWORD)(length + 1) * sizeof(WCHAR));
    if (at) {
        memcpy(a->base + at, s, length * sizeof(WCHAR));
        a->size = at + (DWORD)(length + 1) * sizeof(WCHAR);
    }
    return at;
}

/* ------------------------------------------------------------------ attributes */

typedef struct {
    BOOL bad;
} CONVERT;

static LPCWSTR attr(const XElem *e, LPCWSTR name)
{
    LPCWSTR value = e ? XmlAttr(e, name) : nullptr;
    return value && *value ? value : nullptr;
}

static BOOL attr_bool(CONVERT *c, const XElem *e, LPCWSTR name, BOOL fallback)
{
    LPCWSTR value = attr(e, name);
    if (!value)
        return fallback;
    if (!_wcsicmp(value, L"true"))
        return TRUE;
    if (!_wcsicmp(value, L"false"))
        return FALSE;
    c->bad = TRUE;
    return fallback;
}

static DWORD attr_uint(CONVERT *c, const XElem *e, LPCWSTR name, DWORD fallback)
{
    LPCWSTR value = attr(e, name);
    if (!value)
        return fallback;
    /* base 0 like wcstoul, and the whole string has to be a number. */
    ULONGLONG n = 0;
    UINT base = 10;
    LPCWSTR p = value;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (p[0] == '0' && p[1])
        base = 8;
    BOOL any = base == 8;
    for (; *p; p++) {
        UINT digit = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 :
                     *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : 99;
        if (digit >= base)
            break;
        n = n * base + digit;
        any = TRUE;
        if (n > 0xFFFFFFFFull)
            break;
    }
    if (!any || *p || n > 0xFFFFFFFFull) {
        c->bad = TRUE;
        return fallback;
    }
    return (DWORD)n;
}

typedef struct {
    LPCWSTR name;
    DWORD value;
} NAMED;

/* the value of a name from a list, the fallback for no attribute and flagged when the name is none of them. */
static DWORD attr_named(CONVERT *c, const XElem *e, LPCWSTR name, const NAMED *list, UINT count, DWORD fallback)
{
    LPCWSTR value = attr(e, name);
    if (!value)
        return fallback;
    for (UINT i = 0; i < count; i++)
        if (!_wcsicmp(list[i].name, value))
            return list[i].value;
    c->bad = TRUE;
    return fallback;
}

static const XElem *child(const XElem *e, LPCWSTR name)
{
    for (const XElem *c = e ? e->child : nullptr; c; c = c->next)
        if (XmlIs(c, name))
            return c;
    return nullptr;
}

static BOOL number_vt(VARTYPE vt)
{
    switch (vt & 0xEFFF) {
    case VT_I2: case VT_I4: case VT_R8: case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8:
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ one description */

static const NAMED k_types[] = {
    {L"String", VT_LPWSTR}, {L"Boolean", VT_BOOL}, {L"Any", VT_NULL}, {L"Null", VT_NULL}, {L"Int32", VT_I4},
    {L"UInt32", VT_UI4}, {L"DateTime", VT_FILETIME}, {L"Int64", VT_I8}, {L"UInt64", VT_UI8}, {L"Double", VT_R8},
    {L"Byte", VT_UI1}, {L"Int16", VT_I2}, {L"UInt16", VT_UI2}, {L"Guid", VT_CLSID}, {L"Blob", VT_BLOB},
    {L"Buffer", VT_VECTOR | VT_UI1}, {L"Object", VT_UNKNOWN}, {L"Stream", VT_STREAM}, {L"Clipboard", VT_CF},
};
static const NAMED k_grouping[] = {
    {L"Discrete", PDGR_DISCRETE}, {L"Alphanumeric", PDGR_ALPHANUMERIC}, {L"Dynamic", PDGR_DYNAMIC},
    {L"Date", PDGR_DATE}, {L"Size", PDGR_SIZE}, {L"Percent", PDGR_PERCENT}, {L"Enumerated", PDGR_ENUMERATED},
};
static const NAMED k_aggregation[] = {
    {L"Default", PDAT_DEFAULT}, {L"First", PDAT_FIRST}, {L"Sum", PDAT_SUM}, {L"Average", PDAT_AVERAGE},
    {L"DateRange", PDAT_DATERANGE}, {L"Union", PDAT_UNION}, {L"Maximum", PDAT_MAX}, {L"Minimum", PDAT_MIN},
};
static const NAMED k_condition[] = {
    {L"None", PDCOT_NONE}, {L"String", PDCOT_STRING}, {L"Size", PDCOT_SIZE}, {L"DateTime", PDCOT_DATETIME},
    {L"Boolean", PDCOT_BOOLEAN}, {L"Number", PDCOT_NUMBER},
};
static const NAMED k_operation[] = {
    {L"None", COP_EQUAL}, {L"Equal", COP_EQUAL}, {L"NotEqual", COP_NOTEQUAL}, {L"LessThan", COP_LESSTHAN},
    {L"GreaterThan", COP_GREATERTHAN}, {L"Contains", COP_VALUE_CONTAINS},
};
static const NAMED k_display[] = {
    {L"String", PDDT_STRING}, {L"Number", PDDT_NUMBER}, {L"Boolean", PDDT_BOOLEAN}, {L"DateTime", PDDT_DATETIME},
    {L"Enumerated", PDDT_ENUMERATED},
};
static const NAMED k_relative[] = {
    {L"General", PDRDT_GENERAL}, {L"Date", PDRDT_DATE}, {L"Size", PDRDT_SIZE}, {L"Count", PDRDT_COUNT},
    {L"Revision", PDRDT_REVISION}, {L"Length", PDRDT_LENGTH}, {L"Duration", PDRDT_DURATION},
    {L"Speed", PDRDT_SPEED}, {L"Rate", PDRDT_RATE}, {L"Rating", PDRDT_RATING}, {L"Priority", PDRDT_PRIORITY},
};
static const NAMED k_sort[] = {
    {L"General", PDSD_GENERAL}, {L"AToZ", PDSD_A_Z}, {L"LowestHighest", PDSD_LOWEST_HIGHEST},
    {L"SmallestLargest", PDSD_SMALLEST_BIGGEST}, {L"OldestNewest", PDSD_OLDEST_NEWEST},
};
static const NAMED k_number_format[] = {
    {L"General", 0}, {L"Percentage", 101}, {L"ByteSize", 102}, {L"KBSize", 103}, {L"SampleSize", 104},
    {L"Bitrate", 105}, {L"SampleRate", 106}, {L"FrameRate", 107}, {L"Pixels", 108}, {L"DPI", 109}, {L"Duration", 100},
};
static const NAMED k_duration_format[] = {{L"hh:mm", 0}, {L"hh:mm:ss", 1}, {L"hh:mm:ss.fff", 2}};
static const NAMED k_string_format[] = {{L"General", 0}, {L"FileName", 1}, {L"FilePath", 2}, {L"Hyperlink", 3}};
static const NAMED k_boolean_format[] = {{L"YesNo", BOOLEAN_YESNO}, {L"OnOff", BOOLEAN_ONOFF}, {L"TrueFalse", BOOLEAN_TRUEFALSE}};
static const NAMED k_date_format[] = {
    {L"ShortDate", 1}, {L"LongDate", 2}, {L"HideDate", 3}, {L"RelativeShortDate", 4}, {L"RelativeLongDate", 5},
};
static const NAMED k_time_format[] = {{L"ShortTime", 1}, {L"LongTime", 2}, {L"HideTime", 3}};
static const NAMED k_date_time_as[] = {{L"Default", 0}, {L"YearMonth", 1}, {L"Month", 2}};
static const NAMED k_alignment[] = {{L"Left", 0}, {L"Center", 1}, {L"Right", 2}};
static const NAMED k_direction[] = {{L"Ascending", 0}, {L"Descending", 1}};
static const NAMED k_index_type[] = {
    {L"OnDisk", PDCIT_ONDISK}, {L"NotIndexed", PDCIT_NONE}, {L"OnDemand", PDCIT_ONDEMAND},
    {L"OnDiskAll", PDCIT_ONDISKALL}, {L"OnDiskVector", PDCIT_ONDISKVECTOR},
};

#define LIST(x) x, sizeof(x) / sizeof(x[0])

/* a value of an enumerated list has to read as the type of the property. */
static BOOL enum_value_ok(LPCWSTR text, VARTYPE vt)
{
    if (!text || !*text)
        return TRUE;
    vt &= 0xEFFF;
    if (vt == VT_LPWSTR || vt == VT_EMPTY)
        return TRUE;
    PROPVARIANT source = {}, dest = {};
    source.vt = VT_LPWSTR;
    source.pwszVal = (LPWSTR)text;
    HRESULT hr = PropVariantChangeType(&dest, &source, 0, vt);
    PropVariantClear(&dest);
    return SUCCEEDED(hr);
}

static void canonical_enum_name(ARENA *a, DWORD index, const XElem *e, DWORD type, DWORD *name)
{
    LPCWSTR given = attr(e, L"name");
    WCHAR text[19];
    if (given) {
        *name = arena_string(a, given);
        return;
    }
    if (type == PET_ENDRANGE)
        wcscpy(text, L"NamedValueEnd");
    else {
        wcscpy(text, L"NamedValue");
        WCHAR digits[5] = {'0', '0', '0', '0', 0};
        for (UINT i = 0, v = index; i < 4; i++, v /= 10)
            digits[3 - i] = (WCHAR)('0' + v % 10);
        wcscat(text, digits);
    }
    *name = arena_string(a, text);
}

static HRESULT convert_enums(CONVERT *c, ARENA *a, const XElem *list, VARTYPE vt, PDESC *out)
{
    UINT count = 0;
    for (const XElem *e = list->child; e; e = e->next)
        if (XmlIs(e, L"enum") || XmlIs(e, L"enumRange"))
            count++;
    if (!count)
        return S_OK;
    VARTYPE base = vt & 0xEFFF;
    if (!(base == VT_LPWSTR || base == VT_BOOL || base == VT_EMPTY || number_vt(base)))
        return E_FAIL;
    DWORD array = arena_alloc(a, count * sizeof(ENUMINFO));
    if (!array)
        return E_OUTOFMEMORY;
    UINT index = 0;
    const XElem *ranges[64] = {};
    UINT range_count = 0;
    for (const XElem *e = list->child; e; e = e->next) {
        if (XmlIs(e, L"enumRange") && range_count < 64)
            ranges[range_count++] = e;
    }
    /* the discrete values come first, then the ranges as the windows builder orders them. */
    for (const XElem *e = list->child; e; e = e->next) {
        if (!XmlIs(e, L"enum"))
            continue;
        LPCWSTR value = attr(e, L"value");
        if (!enum_value_ok(value, vt))
            return E_FAIL;
        ENUMINFO info = {};
        info.type = PET_DISCRETEVALUE;
        info.value = arena_string(a, value ? value : L"");
        info.text = arena_string(a, attr(e, L"text") ? attr(e, L"text") : L"");
        info.image = arena_string(a, attr(e, L"image") ? attr(e, L"image") : L"");
        canonical_enum_name(a, index, e, PET_DISCRETEVALUE, &info.name);
        if (a->failed)
            return E_OUTOFMEMORY;
        memcpy(a->base + array + index * sizeof(ENUMINFO), &info, sizeof(info));
        index++;
    }
    for (UINT r = 0; r < range_count; r++) {
        const XElem *e = ranges[r];
        LPCWSTR min = attr(e, L"minValue"), set = attr(e, L"setValue");
        if (!min || !enum_value_ok(min, vt) || !enum_value_ok(set, vt))
            return E_FAIL;
        BOOL end = r == range_count - 1 && !set;
        ENUMINFO info = {};
        info.type = end ? PET_ENDRANGE : PET_RANGEDVALUE;
        info.min = arena_string(a, min);
        info.set = arena_string(a, set ? set : L"");
        info.text = arena_string(a, attr(e, L"text") ? attr(e, L"text") : L"");
        info.image = arena_string(a, attr(e, L"image") ? attr(e, L"image") : L"");
        canonical_enum_name(a, index, e, info.type, &info.name);
        if (a->failed)
            return E_OUTOFMEMORY;
        memcpy(a->base + array + index * sizeof(ENUMINFO), &info, sizeof(info));
        index++;
    }
    out->enum_count = index;
    out->enums = array;
    (void)c;
    return S_OK;
}

static HRESULT convert_description(CONVERT *c, ARENA *a, const XElem *pd, BOOL system, PDESC *out)
{
    memset(out, 0, sizeof(*out));
    c->bad = FALSE;

    LPCWSTR name = attr(pd, L"name");
    if (!name || wcslen(name) >= 64 || (!system && !_wcsnicmp(name, L"System.", 7)))
        return E_FAIL;
    out->name = arena_string(a, name);

    LPCWSTR format = attr(pd, L"formatID"), pid = attr(pd, L"propID");
    if (!format || !pid || !PsGuidFromString(format, &out->key.fmtid))
        return E_FAIL;
    out->key.pid = (DWORD)StrToIntW(pid);

    const XElem *type = child(pd, L"typeInfo");
    const XElem *label = child(pd, L"labelInfo");
    const XElem *display = child(pd, L"displayInfo");
    const XElem *search = child(pd, L"searchInfo");
    const XElem *alias = child(pd, L"aliasInfo");
    const XElem *related = child(pd, L"relatedPropertyInfo");

    /* the type flags and the variant type. */
    DWORD flags = 0;
    if (attr_bool(c, type, L"isInnate", FALSE))
        flags |= PDTF_ISINNATE;
    if (attr_bool(c, type, L"canBePurged", !(flags & PDTF_ISINNATE)) || !(flags & PDTF_ISINNATE))
        flags |= PDTF_CANBEPURGED;
    if (attr_bool(c, type, L"isGroup", FALSE))
        flags |= PDTF_ISGROUP;
    if (attr_bool(c, type, L"canGroupBy", TRUE))
        flags |= PDTF_CANGROUPBY;
    if (attr_bool(c, type, L"canStackBy", TRUE))
        flags |= PDTF_CANSTACKBY;
    BOOL multiple = attr_bool(c, type, L"multipleValues", FALSE);
    if (multiple)
        flags |= PDTF_MULTIPLEVALUES;
    if (attr_bool(c, type, L"isTreeProperty", FALSE))
        flags |= PDTF_ISTREEPROPERTY;
    if (attr_bool(c, type, L"includeInFullTextQuery", FALSE))
        flags |= PDTF_INCLUDEINFULLTEXTQUERY;
    if (attr_bool(c, type, L"isViewable", FALSE))
        flags |= PDTF_ISVIEWABLE;
    if (attr_bool(c, type, L"searchRawValue", FALSE))
        flags |= PDTF_SEARCHRAWVALUE;
    if (attr_bool(c, type, L"isQueryable", FALSE))
        flags |= PDTF_ISQUERYABLE;
    if (attr_bool(c, type, L"dontCoerceEmptyStrings", FALSE))
        flags |= PDTF_DONTCOERCEEMPTYSTRINGS;
    if (attr_bool(c, type, L"alwaysInSupplementalStore", FALSE))
        flags |= PDTF_ALWAYSINSUPPLEMENTALSTORE;
    if (system)
        flags |= PDTF_ISSYSTEMPROPERTY;

    VARTYPE vt = (VARTYPE)attr_named(c, type, L"type", LIST(k_types), VT_NULL);
    if (vt != VT_NULL && multiple)
        vt |= VT_VECTOR;
    if (vt & VT_VECTOR)
        flags |= PDTF_MULTIPLEVALUES;
    out->vt = vt;
    out->type_flags = flags;

    out->grouping = attr(type, L"groupingRange") ?
        attr_named(c, type, L"groupingRange", LIST(k_grouping), PDGR_DISCRETE) :
        (vt & 0xEFFF) == VT_FILETIME ? PDGR_DATE : PDGR_DISCRETE;
    out->aggregation = PDAT_DEFAULT;
    if (attr(type, L"aggregationType")) {
        out->aggregation = attr_named(c, type, L"aggregationType", LIST(k_aggregation), PDAT_DEFAULT);
        if ((out->aggregation == PDAT_SUM || out->aggregation == PDAT_AVERAGE) && !number_vt(vt))
            c->bad = TRUE;
        else if (out->aggregation == PDAT_DATERANGE && (vt & 0xEFFF) != VT_FILETIME)
            c->bad = TRUE;
    } else if ((vt & 0xEFFF) == VT_LPWSTR && (vt & VT_VECTOR))
        out->aggregation = PDAT_UNION;
    else if ((vt & 0xEFFF) == VT_FILETIME)
        out->aggregation = PDAT_DATERANGE;

    if (attr(type, L"conditionType"))
        out->cond_type = attr_named(c, type, L"conditionType", LIST(k_condition), PDCOT_NONE);
    else if (number_vt(vt))
        out->cond_type = PDCOT_NUMBER;
    else if ((vt & 0xEFFF) == VT_BOOL)
        out->cond_type = PDCOT_BOOLEAN;
    else if ((vt & 0xEFFF) == VT_LPWSTR)
        out->cond_type = PDCOT_STRING;
    else if ((vt & 0xEFFF) == VT_FILETIME)
        out->cond_type = PDCOT_DATETIME;
    out->cond_op = COP_EQUAL;
    if (attr(type, L"defaultOperation"))
        out->cond_op = attr_named(c, type, L"defaultOperation", LIST(k_operation), COP_EQUAL);
    else if (out->cond_type == PDCOT_STRING)
        out->cond_op = COP_WORD_EQUAL;
    else if (out->cond_type == PDCOT_SIZE)
        out->cond_op = COP_GREATERTHAN;

    /* the labels. */
    LPCWSTR text = attr(label, L"label");
    out->label = text ? arena_string(a, text) : 0;
    text = attr(label, L"invitationText");
    out->invitation = text ? arena_string(a, text) : 0;
    if (attr_bool(c, label, L"hideLabel", FALSE))
        out->view_flags |= PDVF_HIDELABEL;
    if (attr(label, L"sortDescription"))
        out->sort_desc = attr_named(c, label, L"sortDescription", LIST(k_sort), PDSD_GENERAL);
    else if (number_vt(vt))
        out->sort_desc = PDSD_SMALLEST_BIGGEST;
    else if ((vt & 0xEFFF) == VT_BOOL)
        out->sort_desc = PDSD_SMALLEST_BIGGEST;
    else if ((vt & 0xEFFF) == VT_LPWSTR)
        out->sort_desc = PDSD_A_Z;
    else if ((vt & 0xEFFF) == VT_FILETIME)
        out->sort_desc = PDSD_OLDEST_NEWEST;

    /* the aliases. */
    text = attr(alias, L"sortByAlias");
    out->sort_alias = text ? arena_string(a, text) : 0;
    text = attr(alias, L"additionalSortByAliases");
    out->more_aliases = text ? arena_string(a, text) : 0;

    /* the display. */
    VARTYPE base = vt & 0xEFFF;
    DWORD fallback_display = base == VT_BOOL ? PDDT_BOOLEAN : base == VT_FILETIME ? PDDT_DATETIME :
                             number_vt(base) ? PDDT_NUMBER : PDDT_STRING;
    out->display_type = attr_named(c, display, L"displayType", LIST(k_display), fallback_display);
    out->column_state = out->display_type == PDDT_NUMBER || out->display_type == PDDT_BOOLEAN ? 2 :
                        out->display_type == PDDT_DATETIME ? 3 : 1;
    DWORD alignment = attr_named(c, display, L"alignment", LIST(k_alignment), 0);
    out->view_flags = (out->view_flags & ~3u) | alignment;
    DWORD direction = attr_named(c, display, L"defaultSortDirection", LIST(k_direction), base == VT_FILETIME);
    if (direction)
        out->view_flags |= PDVF_SORTDESCENDING;
    out->col_width = attr_uint(c, display, L"defaultColumnWidth", 20);
    out->relative_type = attr_named(c, display, L"relativeDescriptionType", LIST(k_relative), PDRDT_GENERAL);
    if (out->relative_type == PDRDT_GENERAL && base == VT_FILETIME)
        out->relative_type = PDRDT_DATE;

    switch (out->display_type) {
    case PDDT_STRING:
        out->format_as = attr_named(c, child(display, L"stringFormat"), L"formatAs", LIST(k_string_format), 0);
        break;
    case PDDT_NUMBER:
        out->format_as = attr_named(c, child(display, L"numberFormat"), L"formatAs", LIST(k_number_format), 0);
        out->duration_as = attr_named(c, child(display, L"numberFormat"), L"formatDurationAs",
                                      LIST(k_duration_format), 0);
        break;
    case PDDT_BOOLEAN:
        out->format_as = attr_named(c, child(display, L"booleanFormat"), L"formatAs", LIST(k_boolean_format),
                                    BOOLEAN_YESNO);
        break;
    case PDDT_DATETIME:
        out->format_as = attr_named(c, child(display, L"dateTimeFormat"), L"formatAs", LIST(k_date_time_as), 0);
        out->date_as = attr_named(c, child(display, L"dateTimeFormat"), L"formatDateAs", LIST(k_date_format), 0);
        out->time_as = attr_named(c, child(display, L"dateTimeFormat"), L"formatTimeAs", LIST(k_time_format), 0);
        break;
    }

    const XElem *list = child(display, L"enumeratedList");
    HRESULT hr = S_OK;
    if (list) {
        text = attr(list, L"defaultText");
        out->enum_default_text = text ? arena_string(a, text) : 0;
        out->enum_use_default = attr_bool(c, list, L"useValueForDefault", FALSE);
        hr = convert_enums(c, a, list, vt, out);
        if (out->enum_count && !attr(type, L"groupingRange"))
            out->grouping = PDGR_ENUMERATED;
    }

    /* the search. */
    DWORD sflags = 0;
    BOOL is_column = attr_bool(c, search, L"isColumn", FALSE);
    if (is_column)
        sflags |= PDSIF_ISCOLUMN;
    if (attr_bool(c, search, L"inInvertedIndex", FALSE))
        sflags |= PDSIF_ININVERTEDINDEX;
    if (attr_bool(c, search, L"alwaysInclude", FALSE))
        sflags |= PDSIF_ALWAYSINCLUDE;
    if ((flags & PDTF_MULTIPLEVALUES) || attr_bool(c, search, L"isColumnSparse", TRUE))
        sflags |= PDSIF_ISCOLUMNSPARSE;
    out->search_flags = sflags;
    out->col_index = attr_named(c, search, L"columnIndexType", LIST(k_index_type), is_column ? PDCIT_ONDEMAND : 0);
    out->max_size = attr_uint(c, search, L"maxSize", 512);
    if (attr_bool(c, search, L"processReIndexPatternsImmediately", TRUE))
        out->true_text = 1;

    /* the related properties. */
    UINT related_count = 0;
    for (const XElem *e = related ? related->child : nullptr; e; e = e->next)
        if (XmlIs(e, L"relatedProperty") && attr(e, L"relationshipName") && attr(e, L"propertyName"))
            related_count++;
    if (related_count) {
        DWORD array = arena_alloc(a, related_count * sizeof(RELPROP));
        UINT i = 0;
        for (const XElem *e = related->child; e && array; e = e->next) {
            if (!XmlIs(e, L"relatedProperty") || !attr(e, L"relationshipName") || !attr(e, L"propertyName"))
                continue;
            RELPROP rel;
            rel.relationship = arena_string(a, attr(e, L"relationshipName"));
            rel.property = arena_string(a, attr(e, L"propertyName"));
            if (a->failed)
                return E_OUTOFMEMORY;
            memcpy(a->base + array + i++ * sizeof(RELPROP), &rel, sizeof(rel));
        }
        out->rel_count = related_count;
        out->rels = array;
    }
    if (a->failed)
        return E_OUTOFMEMORY;
    if (FAILED(hr) || c->bad)
        return E_FAIL;
    return S_OK;
}

/* ------------------------------------------------------------------ one schema */

HRESULT SchemaBuild(const BYTE *xml, SIZE_T size, const GUID *id, BOOL system, BYTE **image, DWORD *image_size)
{
    *image = nullptr;
    *image_size = 0;
    XElem *root;
    HRESULT hr = XmlParse(xml, size, &root);
    if (FAILED(hr))
        return hr;
    ARENA a = {};
    DWORD header_at = arena_alloc(&a, sizeof(CACHE_HEADER));
    DWORD invalid = 0;
    PDESC *list = nullptr;
    UINT count = 0, capacity = 0;
    (void)header_at;

    LPCWSTR ns = XmlAttr(root, L"xmlns"), version = XmlAttr(root, L"schemaVersion");
    if (!XmlIs(root, L"schema") || !ns || !version || !*version || _wcsicmp(ns, SCHEMA_NAMESPACE))
        invalid = 1;
    else {
        for (const XElem *l = root->child; l; l = l->next) {
            if (!XmlIs(l, L"propertyDescriptionList"))
                continue;
            for (const XElem *pd = l->child; pd; pd = pd->next) {
                if (!XmlIs(pd, L"propertyDescription"))
                    continue;
                CONVERT c = {};
                PDESC desc;
                if (FAILED(convert_description(&c, &a, pd, system, &desc))) {
                    invalid = 1;
                    continue;
                }
                BOOL duplicate = FALSE;
                for (UINT i = 0; i < count; i++)
                    if ((list[i].key.pid == desc.key.pid && IsEqualGUID(&list[i].key.fmtid, &desc.key.fmtid)) ||
                        !_wcsicmp((LPCWSTR)(a.base + list[i].name), (LPCWSTR)(a.base + desc.name)))
                        duplicate = TRUE;
                if (duplicate)
                    continue;
                if (count == capacity) {
                    UINT grown = capacity ? capacity * 2 : 16;
                    PDESC *more = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(PDESC));
                    if (!more) {
                        hr = E_OUTOFMEMORY;
                        goto fail;
                    }
                    if (list) {
                        memcpy(more, list, count * sizeof(PDESC));
                        HeapFree(GetProcessHeap(), 0, list);
                    }
                    list = more;
                    capacity = grown;
                }
                list[count++] = desc;
            }
        }
    }
    DWORD array = arena_alloc(&a, (count ? count : 1) * sizeof(PDESC));
    if (a.failed) {
        hr = E_OUTOFMEMORY;
        goto fail;
    }
    if (count)
        memcpy(a.base + array, list, count * sizeof(PDESC));
    CACHE_HEADER *header = (CACHE_HEADER *)a.base;
    header->magic = CACHE_MAGIC;
    header->version = CACHE_VERSION;
    header->size = a.size;
    header->id = *id;
    header->invalid = invalid;
    header->system = system;
    header->count = count;
    header->descs = array;
    *image = a.base;
    *image_size = a.size;
    HeapFree(GetProcessHeap(), 0, list);
    XmlFree(root);
    return S_OK;
fail:
    HeapFree(GetProcessHeap(), 0, a.base);
    HeapFree(GetProcessHeap(), 0, list);
    XmlFree(root);
    return hr;
}

/* ------------------------------------------------------------------ the loaded form */

static int compare_keys(const PROPERTYKEY *a, const PROPERTYKEY *b)
{
    int d = memcmp(&a->fmtid, &b->fmtid, sizeof(GUID));
    if (d)
        return d;
    return a->pid < b->pid ? -1 : a->pid > b->pid;
}

static void sort_indices(SCHEMA *s, DWORD *index, DWORD count, BOOL by_name)
{
    for (DWORD i = 1; i < count; i++) {
        DWORD v = index[i];
        DWORD j = i;
        while (j > 0) {
            const PDESC *p = &s->descs[index[j - 1]], *q = &s->descs[v];
            int d = by_name ? _wcsicmp(OFS_STR(s, p->name), OFS_STR(s, q->name)) : compare_keys(&p->key, &q->key);
            if (d <= 0)
                break;
            index[j] = index[j - 1];
            j--;
        }
        index[j] = v;
    }
}

SCHEMA *SchemaFromImage(BYTE *image)
{
    const CACHE_HEADER *header = (const CACHE_HEADER *)image;
    SCHEMA *s = PsAllocZero(sizeof(*s));
    if (!s)
        return nullptr;
    s->refs = 1;
    s->base = image;
    s->header = header;
    s->descs = (const PDESC *)(image + header->descs);
    DWORD count = header->count;
    s->by_key = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(DWORD) * 2);
    if (!s->by_key) {
        HeapFree(GetProcessHeap(), 0, s);
        return nullptr;
    }
    s->by_name = s->by_key + count + 1;
    for (DWORD i = 0; i < count; i++)
        s->by_key[i] = s->by_name[i] = i;
    sort_indices(s, s->by_key, count, FALSE);
    sort_indices(s, s->by_name, count, TRUE);
    return s;
}

void SchemaAddRef(SCHEMA *schema)
{
    InterlockedIncrement(&schema->refs);
}

void SchemaRelease(SCHEMA *schema)
{
    if (InterlockedDecrement(&schema->refs))
        return;
    HeapFree(GetProcessHeap(), 0, schema->base);
    HeapFree(GetProcessHeap(), 0, schema->by_key);
    HeapFree(GetProcessHeap(), 0, schema);
}

/* ------------------------------------------------------------------ the cache files */

static BOOL caches_directory(LPWSTR path)
{
    WCHAR base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH);
    if (!n || n >= MAX_PATH)
        wcscpy(base, L"C:\\ProgramData");
    if (wcslen(base) + 28 >= MAX_PATH)
        return FALSE;
    wcscpy(path, base);
    wcscat(path, L"\\Microsoft\\Windows\\Caches");
    return TRUE;
}

static void create_directories(LPWSTR path)
{
    for (WCHAR *p = path + 3; *p; p++)
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryW(path, nullptr);
            *p = '\\';
        }
    CreateDirectoryW(path, nullptr);
}

/* "{id}.2.ver0x%08x%08x.db", the number of the newest of the cache files of a schema. */
static void cache_name(const GUID *id, ULONGLONG version, LPWSTR name)
{
    WCHAR guid[40];
    StringFromGUID2(id, guid, 40);
    static const WCHAR hex[] = L"0123456789abcdef";
    wcscpy(name, guid);
    wcscat(name, L".2.ver0x");
    SIZE_T at = wcslen(name);
    for (int i = 15; i >= 0; i--)
        name[at++] = hex[(version >> (i * 4)) & 15];
    name[at] = 0;
    wcscat(name, L".db");
}

static ULONGLONG newest_version(LPCWSTR directory, const GUID *id, WCHAR *found)
{
    WCHAR pattern[MAX_PATH], guid[40];
    StringFromGUID2(id, guid, 40);
    wcscpy(pattern, directory);
    wcscat(pattern, L"\\");
    wcscat(pattern, guid);
    wcscat(pattern, L".2.ver0x*");
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    ULONGLONG best = 0;
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    do {
        LPCWSTR marker = wcsstr(data.cFileName, L".ver0x");
        if (!marker)
            continue;
        ULONGLONG v = 0;
        for (marker += 6; *marker && *marker != '.'; marker++)
            v = v * 16 + (*marker >= 'a' ? *marker - 'a' + 10 : *marker >= 'A' ? *marker - 'A' + 10 : *marker - '0');
        if (v > best) {
            best = v;
            if (found)
                wcscpy(found, data.cFileName);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return best;
}

static BYTE *read_cache(LPCWSTR directory, const GUID *id, const FILETIME *source_time, DWORD source_size, DWORD *size)
{
    WCHAR file[MAX_PATH];
    if (!newest_version(directory, id, file))
        return nullptr;
    WCHAR path[MAX_PATH];
    wcscpy(path, directory);
    wcscat(path, L"\\");
    wcscat(path, file);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD length = GetFileSize(h, nullptr), got = 0;
    BYTE *data = length >= sizeof(CACHE_HEADER) && length < 0x10000000 ? HeapAlloc(GetProcessHeap(), 0, length) : nullptr;
    if (data && (!ReadFile(h, data, length, &got, nullptr) || got != length))
        HeapFree(GetProcessHeap(), 0, data), data = nullptr;
    CloseHandle(h);
    if (!data)
        return nullptr;
    const CACHE_HEADER *header = (const CACHE_HEADER *)data;
    if (header->magic != CACHE_MAGIC || header->version != CACHE_VERSION || header->size != length ||
        !IsEqualGUID(&header->id, id) || header->descs + (SIZE_T)header->count * sizeof(PDESC) > length ||
        (source_time && (header->source_time.dwLowDateTime != source_time->dwLowDateTime ||
                         header->source_time.dwHighDateTime != source_time->dwHighDateTime ||
                         header->source_size != source_size))) {
        HeapFree(GetProcessHeap(), 0, data);
        return nullptr;
    }
    *size = length;
    return data;
}

static void write_cache(LPCWSTR directory, const GUID *id, const BYTE *image, DWORD size)
{
    WCHAR dir[MAX_PATH], name[96], path[MAX_PATH], old[96];
    wcscpy(dir, directory);
    create_directories(dir);
    ULONGLONG previous = newest_version(directory, id, old);
    cache_name(id, previous + 1, name);
    wcscpy(path, directory);
    wcscat(path, L"\\");
    wcscat(path, name);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    BOOL ok = WriteFile(h, image, size, &written, nullptr) && written == size;
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(path);
        return;
    }
    if (previous) {
        wcscpy(path, directory);
        wcscat(path, L"\\");
        wcscat(path, old);
        DeleteFileW(path);
    }
}

static void delete_cache(LPCWSTR directory, const GUID *id)
{
    WCHAR file[MAX_PATH], path[MAX_PATH];
    while (newest_version(directory, id, file)) {
        wcscpy(path, directory);
        wcscat(path, L"\\");
        wcscat(path, file);
        if (!DeleteFileW(path))
            break;
    }
}

/* ------------------------------------------------------------------ the registrations */

typedef struct {
    GUID id;
    WCHAR uri[MAX_PATH];
    WCHAR compact[65];
    WCHAR path[MAX_PATH];
    WCHAR key[8];
    BOOL view_64;
} REGISTRATION;

static BYTE *read_file(LPCWSTR path, SIZE_T *size, FILETIME *time)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD length = GetFileSize(h, nullptr), got = 0;
    BYTE *data = length && length < 0x10000000 ? HeapAlloc(GetProcessHeap(), 0, length) : nullptr;
    if (data && (!ReadFile(h, data, length, &got, nullptr) || got != length))
        HeapFree(GetProcessHeap(), 0, data), data = nullptr;
    if (data && time)
        GetFileTime(h, nullptr, nullptr, time);
    CloseHandle(h);
    *size = length;
    return data;
}

/* the path as registered with the environment expanded, and the file found where the schema says or under Program Files (x86). */
static BOOL expand_path(LPCWSTR registered, LPWSTR out)
{
    WCHAR expanded[MAX_PATH];
    DWORD n = ExpandEnvironmentStringsW(registered, expanded, MAX_PATH);
    if (!n || n > MAX_PATH)
        return FALSE;
    if (PathFileExistsW(expanded)) {
        wcscpy(out, expanded);
        return TRUE;
    }
    LPCWSTR mark = StrStrIW(registered, L"%PROGRAMFILES%");
    LPCWSTR slash = mark ? StrStrIW(registered, L"\\") : nullptr;
    if (!mark || !slash)
        return FALSE;
    WCHAR translated[MAX_PATH];
    if (wcslen(slash) + 20 >= MAX_PATH)
        return FALSE;
    wcscpy(translated, L"%PROGRAMFILES(x86)%");
    wcscat(translated, slash);
    n = ExpandEnvironmentStringsW(translated, expanded, MAX_PATH);
    if (!n || n > MAX_PATH || !PathFileExistsW(expanded))
        return FALSE;
    wcscpy(out, expanded);
    return TRUE;
}

static HRESULT status_hr(LSTATUS status)
{
    return status ? HRESULT_FROM_WIN32(status) : S_OK;
}

static BOOL read_string_value(HKEY key, LPCWSTR name, LPWSTR out, DWORD chars)
{
    DWORD bytes = chars * sizeof(WCHAR);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, out, &bytes) == 0;
}

static BOOL set_string_value(HKEY key, LPCWSTR name, LPCWSTR value)
{
    return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)(wcslen(value) + 1) * sizeof(WCHAR)) == 0;
}

/* the hash of the lower case file name as lower case hex, the compact form of the uri. */
static void compact_uri(LPCWSTR uri, LPWSTR out)
{
    BYTE digest[32];
    mime_hash(&mime_sha256, uri, wcslen(uri) * sizeof(WCHAR), digest);
    static const WCHAR hex[] = L"0123456789abcdef";
    for (UINT i = 0; i < 32; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
}

static void new_registration(LPCWSTR path, REGISTRATION *r)
{
    CoCreateGuid(&r->id);
    LPCWSTR name = PathFindFileNameW(path);
    lstrcpynW(r->uri, name, MAX_PATH);
    r->uri[MAX_PATH - 1] = 0;
    CharLowerW(r->uri);
    compact_uri(r->uri, r->compact);
}

static BOOL load_registration(HKEY key, REGISTRATION *r)
{
    WCHAR text[80];
    if (!read_string_value(key, L"SchemaId", text, 80) || !PsGuidFromString(text, &r->id))
        return FALSE;
    if (!read_string_value(key, L"URI", r->uri, MAX_PATH))
        return FALSE;
    if (!read_string_value(key, L"CompactURI", r->compact, 65))
        r->compact[0] = 0;
    return TRUE;
}

static void write_registration(HKEY key, const REGISTRATION *r)
{
    WCHAR text[40];
    StringFromGUID2(&r->id, text, 40);
    set_string_value(key, L"SchemaId", text);
    set_string_value(key, L"URI", r->uri);
    set_string_value(key, L"CompactURI", r->compact);
}

/* a schema of a registration, loaded from its cache file or built from the file and cached. */
static HRESULT load_schema(const REGISTRATION *r, LPCWSTR path, SCHEMA **out)
{
    WCHAR directory[MAX_PATH];
    *out = nullptr;
    FILETIME time = {};
    SIZE_T size;
    BYTE *xml = read_file(path, &size, &time);
    if (!xml)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    BOOL have_directory = caches_directory(directory);
    DWORD image_size;
    BYTE *image = have_directory ? read_cache(directory, &r->id, &time, (DWORD)size, &image_size) : nullptr;
    if (!image) {
        HRESULT hr = SchemaBuild(xml, size, &r->id, FALSE, &image, &image_size);
        if (FAILED(hr)) {
            HeapFree(GetProcessHeap(), 0, xml);
            return hr;
        }
        CACHE_HEADER *header = (CACHE_HEADER *)image;
        header->source_time = time;
        header->source_size = (DWORD)size;
        if (have_directory)
            write_cache(directory, &r->id, image, image_size);
    }
    HeapFree(GetProcessHeap(), 0, xml);
    *out = SchemaFromImage(image);
    if (!*out) {
        HeapFree(GetProcessHeap(), 0, image);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

typedef struct {
    SCHEMA **schemas;
    UINT count, capacity;
    DWORD stamp[2];
    ULONGLONG last_check;
    BOOL loaded;
} CATALOG;

static CATALOG g_catalog;
static SRWLOCK g_lock = SRWLOCK_INIT;

static void catalog_clear(CATALOG *c)
{
    for (UINT i = 0; i < c->count; i++)
        SchemaRelease(c->schemas[i]);
    HeapFree(GetProcessHeap(), 0, c->schemas);
    c->schemas = nullptr;
    c->count = c->capacity = 0;
}

static BOOL catalog_add(CATALOG *c, SCHEMA *s)
{
    for (UINT i = 0; i < c->count; i++)
        if (IsEqualGUID(&c->schemas[i]->header->id, &s->header->id)) {
            SchemaRelease(s);
            return TRUE;
        }
    if (c->count == c->capacity) {
        UINT grown = c->capacity ? c->capacity * 2 : 8;
        SCHEMA **more = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*more));
        if (!more) {
            SchemaRelease(s);
            return FALSE;
        }
        if (c->schemas) {
            memcpy(more, c->schemas, c->count * sizeof(*more));
            HeapFree(GetProcessHeap(), 0, c->schemas);
        }
        c->schemas = more;
        c->capacity = grown;
    }
    c->schemas[c->count++] = s;
    return TRUE;
}

static REGSAM view_access(BOOL view_64)
{
    return view_64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
}

static HRESULT load_system_schema(CATALOG *c)
{
    HRSRC found = FindResourceW(g_module, L"WINDOWSPROPERTYDESCRIPTIONS", L"XML");
    if (!found)
        return S_FALSE;
    HGLOBAL global = LoadResource(g_module, found);
    DWORD length = SizeofResource(g_module, found);
    const void *data = global ? LockResource(global) : nullptr;
    if (!data || !length)
        return S_FALSE;
    WCHAR directory[MAX_PATH];
    BYTE *image = nullptr;
    DWORD image_size = 0;
    BOOL have_directory = caches_directory(directory);
    FILETIME time = {};
    WCHAR module[MAX_PATH];
    HANDLE h = GetModuleFileNameW(g_module, module, MAX_PATH) ?
        CreateFileW(module, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr) :
        INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        GetFileTime(h, nullptr, nullptr, &time);
        CloseHandle(h);
    }
    if (have_directory)
        image = read_cache(directory, &CACHEID_SystemSchema, &time, length, &image_size);
    if (!image) {
        HRESULT hr = SchemaBuild(data, length, &CACHEID_SystemSchema, TRUE, &image, &image_size);
        if (FAILED(hr))
            return hr;
        CACHE_HEADER *header = (CACHE_HEADER *)image;
        header->source_time = time;
        header->source_size = length;
        if (have_directory)
            write_cache(directory, &CACHEID_SystemSchema, image, image_size);
    }
    SCHEMA *s = SchemaFromImage(image);
    if (!s) {
        HeapFree(GetProcessHeap(), 0, image);
        return E_OUTOFMEMORY;
    }
    catalog_add(c, s);
    return S_OK;
}

static int compare_registrations(const REGISTRATION *a, const REGISTRATION *b)
{
    return _wcsicmp(a->uri, b->uri);
}

/* loads the system schema and every registered one, in the order of their file names. */
static void catalog_fill(CATALOG *c)
{
    load_system_schema(c);
    REGISTRATION *found = nullptr;
    UINT found_count = 0, found_capacity = 0;
    SCHEMA **loaded_schemas = nullptr;
    for (UINT pass = 0; pass < 2; pass++) {
        BOOL view_64 = pass == 1;
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SCHEMA_KEY, 0, KEY_ENUMERATE_SUB_KEYS | view_access(view_64), &root))
            continue;
        WCHAR name[260];
        DWORD length = 260;
        for (DWORD i = 0; RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) == 0; i++, length = 260) {
            HKEY key;
            if (RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | view_access(view_64), &key) &&
                RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | view_access(view_64), &key))
                continue;
            WCHAR registered[MAX_PATH], path[MAX_PATH];
            REGISTRATION r = {};
            if (read_string_value(key, nullptr, registered, MAX_PATH) && expand_path(registered, path)) {
                if (!load_registration(key, &r)) {
                    new_registration(path, &r);
                    write_registration(key, &r);
                }
                wcscpy(r.path, path);
                wcscpy(r.key, name);
                r.view_64 = view_64;
                if (found_count == found_capacity) {
                    UINT grown = found_capacity ? found_capacity * 2 : 8;
                    REGISTRATION *more = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*more));
                    if (more) {
                        if (found)
                            memcpy(more, found, found_count * sizeof(*more)), HeapFree(GetProcessHeap(), 0, found);
                        found = more;
                        found_capacity = grown;
                    }
                }
                if (found_count < found_capacity)
                    found[found_count++] = r;
            }
            RegCloseKey(key);
        }
        RegCloseKey(root);
    }
    for (UINT i = 1; i < found_count; i++) {
        REGISTRATION v = found[i];
        UINT j = i;
        while (j > 0 && compare_registrations(&found[j - 1], &v) > 0) {
            found[j] = found[j - 1];
            j--;
        }
        found[j] = v;
    }
    (void)loaded_schemas;
    for (UINT i = 0; i < found_count; i++) {
        SCHEMA *s;
        if (SUCCEEDED(load_schema(&found[i], found[i].path, &s)))
            catalog_add(c, s);
    }
    HeapFree(GetProcessHeap(), 0, found);
}

static void registry_stamp(DWORD *stamp)
{
    stamp[0] = stamp[1] = 0;
    for (UINT pass = 0; pass < 2; pass++) {
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SCHEMA_KEY, 0, KEY_QUERY_VALUE | view_access(pass == 1), &root))
            continue;
        FILETIME t = {};
        DWORD subkeys = 0;
        if (RegQueryInfoKeyW(root, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &t) == 0)
            stamp[pass] = t.dwLowDateTime ^ (t.dwHighDateTime * 31) ^ (subkeys << 24);
        RegCloseKey(root);
    }
}

/* the catalog under the lock, reloaded when the registrations changed since it was read. */
static void catalog_ensure(BOOL force)
{
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_lock);
    BOOL fresh = g_catalog.loaded && !force && now - g_catalog.last_check < 2000;
    ReleaseSRWLockShared(&g_lock);
    if (fresh)
        return;
    DWORD stamp[2];
    registry_stamp(stamp);
    AcquireSRWLockExclusive(&g_lock);
    if (!g_catalog.loaded || force || stamp[0] != g_catalog.stamp[0] || stamp[1] != g_catalog.stamp[1]) {
        CATALOG next = {};
        catalog_fill(&next);
        next.stamp[0] = stamp[0];
        next.stamp[1] = stamp[1];
        next.loaded = TRUE;
        catalog_clear(&g_catalog);
        g_catalog = next;
    }
    g_catalog.last_check = now;
    ReleaseSRWLockExclusive(&g_lock);
}

HRESULT SchemaFindKey(REFPROPERTYKEY key, SCHEMA **schema, DWORD *index)
{
    catalog_ensure(FALSE);
    HRESULT hr = TYPE_E_ELEMENTNOTFOUND;
    AcquireSRWLockShared(&g_lock);
    for (UINT i = 0; i < g_catalog.count && FAILED(hr); i++) {
        SCHEMA *s = g_catalog.schemas[i];
        LONG low = 0, high = (LONG)s->header->count - 1;
        while (low <= high) {
            LONG mid = (low + high) / 2;
            int d = compare_keys(&s->descs[s->by_key[mid]].key, key);
            if (!d) {
                SchemaAddRef(s);
                *schema = s;
                *index = s->by_key[mid];
                hr = S_OK;
                break;
            }
            if (d < 0)
                low = mid + 1;
            else
                high = mid - 1;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return hr;
}

static HRESULT find_name(LPCWSTR name, SCHEMA **schema, DWORD *index)
{
    HRESULT hr = TYPE_E_ELEMENTNOTFOUND;
    AcquireSRWLockShared(&g_lock);
    for (UINT i = 0; i < g_catalog.count && FAILED(hr); i++) {
        SCHEMA *s = g_catalog.schemas[i];
        LONG low = 0, high = (LONG)s->header->count - 1;
        while (low <= high) {
            LONG mid = (low + high) / 2;
            int d = _wcsicmp(OFS_STR(s, s->descs[s->by_name[mid]].name), name);
            if (!d) {
                SchemaAddRef(s);
                *schema = s;
                *index = s->by_name[mid];
                hr = S_OK;
                break;
            }
            if (d < 0)
                low = mid + 1;
            else
                high = mid - 1;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return hr;
}

/* the names the older property system answered to and the property each stands for. */
static const struct {
    LPCWSTR name, alias;
} k_name_alias[] = {
#include "alias_table.h"
};

HRESULT SchemaFindName(LPCWSTR name, SCHEMA **schema, DWORD *index)
{
    catalog_ensure(FALSE);
    HRESULT hr = find_name(name, schema, index);
    if (FAILED(hr))
        for (UINT i = 0; i < sizeof(k_name_alias) / sizeof(k_name_alias[0]); i++)
            if (!_wcsicmp(k_name_alias[i].alias, name)) {
                hr = find_name(k_name_alias[i].name, schema, index);
                break;
            }
    return hr;
}

static BOOL passes(PROPDESC_ENUMFILTER filter, const PDESC *d)
{
    switch (filter) {
    case PDEF_ALL: return TRUE;
    case PDEF_SYSTEM: return (d->type_flags & PDTF_ISSYSTEMPROPERTY) != 0;
    case PDEF_NONSYSTEM: return !(d->type_flags & PDTF_ISSYSTEMPROPERTY);
    case PDEF_VIEWABLE: return (d->type_flags & PDTF_ISVIEWABLE) != 0;
    case PDEF_QUERYABLE: return (d->type_flags & PDTF_ISQUERYABLE) != 0;
    case PDEF_INFULLTEXTQUERY: return (d->type_flags & PDTF_INCLUDEINFULLTEXTQUERY) != 0;
    case PDEF_COLUMN: return (d->search_flags & PDSIF_ISCOLUMN) != 0;
    }
    return FALSE;
}

HRESULT SchemaEnumerate(PROPDESC_ENUMFILTER filter, SCHEMA ***schemas, DWORD **indices, DWORD *count)
{
    *schemas = nullptr;
    *indices = nullptr;
    *count = 0;
    if ((UINT)filter > PDEF_COLUMN)
        return E_INVALIDARG;
    catalog_ensure(FALSE);
    AcquireSRWLockShared(&g_lock);
    DWORD total = 0;
    for (UINT i = 0; i < g_catalog.count; i++)
        total += g_catalog.schemas[i]->header->count;
    SCHEMA **s = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(*s));
    DWORD *x = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(*x));
    if (!s || !x) {
        ReleaseSRWLockShared(&g_lock);
        HeapFree(GetProcessHeap(), 0, s);
        HeapFree(GetProcessHeap(), 0, x);
        return E_OUTOFMEMORY;
    }
    DWORD n = 0;
    for (UINT i = 0; i < g_catalog.count; i++) {
        SCHEMA *schema = g_catalog.schemas[i];
        for (DWORD j = 0; j < schema->header->count; j++)
            if (passes(filter, &schema->descs[schema->by_name[j]])) {
                SchemaAddRef(schema);
                s[n] = schema;
                x[n++] = schema->by_name[j];
            }
    }
    ReleaseSRWLockShared(&g_lock);
    *schemas = s;
    *indices = x;
    *count = n;
    return S_OK;
}

/* ------------------------------------------------------------------ registering */

static HRESULT open_registry_root(HKEY *key, REGSAM access)
{
    return status_hr(RegCreateKeyExW(HKEY_LOCAL_MACHINE, SCHEMA_KEY, 0, nullptr, 0, access, nullptr, key, nullptr));
}

HRESULT SchemaRegister(LPCWSTR path)
{
    if (!path || PathFindExtensionW(path) == path || _wcsicmp(PathFindExtensionW(path), L".propdesc"))
        return E_INVALIDARG;
    WCHAR full[MAX_PATH];
    if (!expand_path(path, full))
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    HKEY root;
    HRESULT hr = open_registry_root(&root, KEY_CREATE_SUB_KEY | KEY_ENUMERATE_SUB_KEYS);
    if (FAILED(hr))
        return hr;
    /* the key already holding this path, else the lowest number not taken. */
    WCHAR name[260], found[8] = L"";
    DWORD length = 260;
    DWORD taken = 0;
    BOOL numbers[1024] = {};
    for (DWORD i = 0; RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) == 0; i++, length = 260) {
        int n = StrToIntW(name);
        if (n >= 0 && n < 1024)
            numbers[n] = TRUE;
        WCHAR value[MAX_PATH];
        DWORD bytes = sizeof(value);
        if (RegGetValueW(root, name, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, value, &bytes) == 0 &&
            !_wcsicmp(value, path) && !found[0])
            lstrcpynW(found, name, 8);
    }
    (void)taken;
    BOOL existed = found[0] != 0;
    if (!existed) {
        UINT n = 0;
        while (n < 1024 && numbers[n])
            n++;
        for (int i = 3; i >= 0; i--, n /= 10)
            found[i] = (WCHAR)('0' + n % 10);
        found[4] = 0;
    }
    HKEY key;
    hr = status_hr(RegCreateKeyExW(root, found, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr));
    if (FAILED(hr)) {
        RegCloseKey(root);
        return hr;
    }
    REGISTRATION r = {};
    if (!existed || !load_registration(key, &r)) {
        new_registration(path, &r);
        if (!existed)
            set_string_value(key, nullptr, path);
        write_registration(key, &r);
    }
    SCHEMA *schema;
    hr = load_schema(&r, full, &schema);
    DWORD flag = 0;
    if (SUCCEEDED(hr)) {
        flag = schema->header->invalid;
        SchemaRelease(schema);
    } else {
        if (!existed) {
            RegCloseKey(key);
            RegDeleteTreeW(root, found);
            RegCloseKey(root);
            return hr;
        }
    }
    RegCloseKey(key);
    RegCloseKey(root);
    catalog_ensure(TRUE);
    return SUCCEEDED(hr) && flag ? ERROR_FLAG : hr;
}

HRESULT SchemaUnregister(LPCWSTR path)
{
    if (!path)
        return E_INVALIDARG;
    WCHAR wanted[MAX_PATH];
    DWORD n = ExpandEnvironmentStringsW(path, wanted, MAX_PATH);
    if (!n || n > MAX_PATH)
        return E_OUTOFMEMORY;
    HRESULT result = S_OK;
    for (UINT pass = 0; pass < 2; pass++) {
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SCHEMA_KEY, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | DELETE | view_access(pass == 1), &root) &&
            RegOpenKeyExW(HKEY_LOCAL_MACHINE, SCHEMA_KEY, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | view_access(pass == 1), &root))
            continue;
        WCHAR name[260];
        DWORD length = 260;
        for (DWORD i = 0; RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) == 0; i++, length = 260) {
            WCHAR value[MAX_PATH], expanded[MAX_PATH];
            DWORD bytes = sizeof(value);
            if (RegGetValueW(root, name, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, value, &bytes))
                continue;
            DWORD m = ExpandEnvironmentStringsW(value, expanded, MAX_PATH);
            if (!m || m > MAX_PATH || _wcsicmp(expanded, wanted))
                continue;
            HKEY key;
            REGISTRATION r = {};
            BOOL known = FALSE;
            if (!RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | view_access(pass == 1), &key)) {
                known = load_registration(key, &r);
                RegCloseKey(key);
            }
            LONG status = RegDeleteTreeW(root, name);
            if (status && SUCCEEDED(result))
                result = HRESULT_FROM_WIN32(status);
            if (known) {
                WCHAR directory[MAX_PATH];
                if (caches_directory(directory))
                    delete_cache(directory, &r.id);
            }
            break;
        }
        RegCloseKey(root);
    }
    catalog_ensure(TRUE);
    return result;
}

HRESULT SchemaRefresh(void)
{
    catalog_ensure(TRUE);
    return S_OK;
}
