/* formatting values for display, canonical values and the image of an enumerated value. */
#include "propsys_private.h"

static const struct {
    UINT id;
    LPCWSTR text;
} k_ui[] = {
#include "ui_strings.h"
};

LPCWSTR PsUiString(UINT id)
{
    for (UINT i = 0; i < sizeof(k_ui) / sizeof(k_ui[0]); i++)
        if (k_ui[i].id == id)
            return k_ui[i].text;
    return nullptr;
}

/* ------------------------------------------------------------------ text */

typedef struct {
    WCHAR *data;
    SIZE_T length, capacity;
    BOOL failed;
} TEXT;

static void text_free(TEXT *t)
{
    HeapFree(GetProcessHeap(), 0, t->data);
    t->data = nullptr;
    t->length = t->capacity = 0;
}

static void text_add_n(TEXT *t, LPCWSTR s, SIZE_T n)
{
    if (t->failed)
        return;
    if (t->length + n + 1 > t->capacity) {
        SIZE_T capacity = t->capacity ? t->capacity : 64;
        while (capacity < t->length + n + 1)
            capacity *= 2;
        WCHAR *grown = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(WCHAR));
        if (!grown) {
            t->failed = TRUE;
            return;
        }
        if (t->data) {
            memcpy(grown, t->data, t->length * sizeof(WCHAR));
            HeapFree(GetProcessHeap(), 0, t->data);
        }
        t->data = grown;
        t->capacity = capacity;
    }
    memcpy(t->data + t->length, s, n * sizeof(WCHAR));
    t->length += n;
    t->data[t->length] = 0;
}

static void text_add(TEXT *t, LPCWSTR s)
{
    text_add_n(t, s, wcslen(s));
}

static HRESULT text_result(TEXT *t, LPWSTR *out)
{
    *out = nullptr;
    if (t->failed)
        return E_OUTOFMEMORY;
    SIZE_T bytes = (t->length + 1) * sizeof(WCHAR);
    WCHAR *copy = CoTaskMemAlloc(bytes);
    if (!copy)
        return E_OUTOFMEMORY;
    if (t->data)
        memcpy(copy, t->data, bytes);
    else
        copy[0] = 0;
    *out = copy;
    return S_OK;
}

typedef union {
    ULONGLONG u;
    LPCWSTR s;
    double d;
} ARG;

static void put_unsigned(TEXT *t, ULONGLONG v)
{
    WCHAR digits[24], *p = digits + 23;
    *p = 0;
    do {
        *--p = (WCHAR)('0' + v % 10);
        v /= 10;
    } while (v);
    text_add(t, p);
}

static void put_fixed(TEXT *t, double v, UINT decimals)
{
    if (v < 0) {
        text_add(t, L"-");
        v = -v;
    }
    double scale = 1;
    for (UINT i = 0; i < decimals; i++)
        scale *= 10;
    ULONGLONG whole = (ULONGLONG)v;
    ULONGLONG frac = (ULONGLONG)((v - (double)whole) * scale + 0.5);
    if (frac >= (ULONGLONG)scale) {
        whole++;
        frac -= (ULONGLONG)scale;
    }
    put_unsigned(t, whole);
    if (decimals) {
        text_add(t, L".");
        WCHAR digits[12];
        for (UINT i = 0; i < decimals; i++) {
            digits[decimals - 1 - i] = (WCHAR)('0' + frac % 10);
            frac /= 10;
        }
        text_add_n(t, digits, decimals);
    }
}

/* the conversions the resource strings use: %lu %u %d %s %3.3f %3.2f %% and %n!ls!. */
static void text_format(TEXT *t, LPCWSTR format, const ARG *args)
{
    UINT next = 0;
    for (; *format; format++) {
        if (*format != '%') {
            text_add_n(t, format, 1);
            continue;
        }
        format++;
        if (*format == '%') {
            text_add(t, L"%");
        } else if (*format >= '1' && *format <= '9' && format[1] == '!') {
            UINT index = *format - '1';
            format += 2;
            while (*format && *format != '!')
                format++;
            text_add(t, args[index].s);
        } else if (*format == 's') {
            text_add(t, args[next++].s);
        } else if (*format == 'l' && format[1] == 'u') {
            format++;
            put_unsigned(t, (ULONG)args[next++].u);
        } else if (*format == 'u' || *format == 'd') {
            put_unsigned(t, (ULONG)args[next++].u);
        } else if (*format == '3' && format[1] == '.' && format[3] == 'f') {
            put_fixed(t, args[next++].d, (UINT)(format[2] - '0'));
            format += 3;
        } else
            text_add_n(t, format - 1, 2);
    }
}

static void add_ui(TEXT *t, UINT id, const ARG *args)
{
    LPCWSTR format = PsUiString(id);
    if (format)
        text_format(t, format, args);
}

/* ------------------------------------------------------------------ the parameters of a format */

typedef struct {
    DWORD display_type, format_as, date_as, time_as, duration_as;
    const ENUMINFO *enums;
    DWORD enum_count, enum_use_default;
    LPCWSTR enum_default_text;
    VARTYPE vt;
    SCHEMA *schema;
    BOOL multiple, tree;
    DWORD aggregation;
    DWORD flags;
} FORMAT;

static HRESULT format_value(const FORMAT *f, const PROPVARIANT *v, TEXT *out);

static BOOL supported_type(VARTYPE vt)
{
    switch (vt & 0xEFFF) {
    case VT_UNKNOWN: case VT_BLOB: case VT_STREAM: case VT_STORAGE: case VT_STREAMED_OBJECT: case VT_STORED_OBJECT:
    case VT_BLOBOBJECT: case VT_CF: case VT_VERSIONED_STREAM:
        return FALSE;
    }
    return TRUE;
}

static void put_two(TEXT *t, ULONGLONG v, UINT width)
{
    WCHAR digits[24], *p = digits + 23;
    *p = 0;
    do {
        *--p = (WCHAR)('0' + v % 10);
        v /= 10;
    } while (v);
    while (digits + 23 - p < (int)width)
        *--p = '0';
    text_add(t, p);
}

static HRESULT format_duration(const FORMAT *f, const PROPVARIANT *v, TEXT *out)
{
    ULONGLONG ticks = PropVariantToUInt64WithDefault(v, 0);
    ULONGLONG seconds = ticks / 10000000, millis = (ticks / 10000) % 1000;
    put_two(out, seconds / 3600, 2);
    text_add(out, L":");
    put_two(out, (seconds / 60) % 60, 2);
    if (f->duration_as >= 1) {
        text_add(out, L":");
        put_two(out, seconds % 60, 2);
    }
    if (f->duration_as == 2) {
        text_add(out, L".");
        put_two(out, millis, 3);
    }
    return S_OK;
}

static HRESULT format_number(const FORMAT *f, const PROPVARIANT *v, TEXT *out)
{
    WCHAR buffer[512];
    ARG arg;
    ULONG n = 0;
    HRESULT hr;
    switch (f->format_as) {
    case 100:
        return format_duration(f, v, out);
    case 101: case 104: case 105: case 108: case 109: {
        hr = PropVariantToUInt32(v, &n);
        if (FAILED(hr))
            return hr;
        UINT id = f->format_as == 101 ? 193 : f->format_as == 104 ? 181 : f->format_as == 105 ? 172 :
                  f->format_as == 108 ? 182 : 183;
        if (f->format_as == 105)
            n /= 1000;
        arg.u = n;
        add_ui(out, id, &arg);
        return S_OK;
    }
    case 106: case 107:
        hr = PropVariantToUInt32(v, &n);
        if (FAILED(hr))
            return hr;
        arg.d = (double)(float)(LONG)n / 1000.0;
        add_ui(out, f->format_as == 106 ? 179 : 180, &arg);
        return S_OK;
    case 102: case 103: {
        ULONGLONG size = PropVariantToUInt64WithDefault(v, 0);
        if ((f->flags & PDFF_ALWAYSKB) || f->format_as == 103)
            StrFormatKBSizeW((LONGLONG)size, buffer, 32);
        else
            StrFormatByteSizeW((LONGLONG)size, buffer, 32);
        text_add(out, buffer);
        return S_OK;
    }
    }
    hr = PropVariantToString(v, buffer, 512);
    if (FAILED(hr))
        return hr;
    text_add(out, buffer);
    return S_OK;
}

static HRESULT format_boolean(const FORMAT *f, const PROPVARIANT *v, TEXT *out)
{
    BOOL value;
    HRESULT hr = PropVariantToBoolean(v, &value);
    if (FAILED(hr))
        return hr;
    value = value != 0;
    ARG none = {};
    switch (f->format_as) {
    case 201: add_ui(out, 176 - value, &none); break;
    case 202: add_ui(out, 178 - value, &none); break;
    default: add_ui(out, 174 - value, &none); break;
    }
    return S_OK;
}

static BOOL is_today(const SYSTEMTIME *a, const SYSTEMTIME *b)
{
    return a->wYear == b->wYear && a->wMonth == b->wMonth && a->wDay == b->wDay;
}

static HRESULT format_date_time(const FORMAT *f, const PROPVARIANT *v, TEXT *out)
{
    SYSTEMTIME local;
    if (v->vt == VT_DATE) {
        if (!VariantTimeToSystemTime(v->date, &local))
            return E_INVALIDARG;
    } else {
        PROPVARIANT converted = {};
        const FILETIME *time;
        if (v->vt == VT_FILETIME)
            time = &v->filetime;
        else {
            HRESULT hr = PropVariantChangeType(&converted, v, 0, VT_FILETIME);
            if (FAILED(hr))
                return hr;
            time = &converted.filetime;
        }
        FILETIME shifted;
        BOOL ok = FileTimeToLocalFileTime(time, &shifted) && FileTimeToSystemTime(&shifted, &local);
        PropVariantClear(&converted);
        if (!ok)
            return E_INVALIDARG;
    }
    DWORD flags = f->flags;
    if (!(flags & (PDFF_SHORTDATE | PDFF_LONGDATE | PDFF_HIDEDATE))) {
        switch (f->date_as) {
        case 1: flags |= PDFF_SHORTDATE; break;
        case 2: flags |= PDFF_LONGDATE; break;
        case 3: flags |= PDFF_HIDEDATE; break;
        case 4: flags |= PDFF_RELATIVEDATE | PDFF_SHORTDATE; break;
        case 5: flags |= PDFF_RELATIVEDATE | PDFF_LONGDATE; break;
        default: flags |= PDFF_SHORTDATE; break;
        }
    }
    if (!(flags & (PDFF_SHORTTIME | PDFF_LONGTIME | PDFF_HIDETIME))) {
        switch (f->time_as) {
        case 2: flags |= PDFF_LONGTIME; break;
        case 3: flags |= PDFF_HIDETIME; break;
        default: flags |= PDFF_SHORTTIME; break;
        }
    }
    WCHAR buffer[260];
    BOOL wrote = FALSE;
    if (!(flags & PDFF_HIDEDATE)) {
        ARG none = {};
        SYSTEMTIME now;
        GetLocalTime(&now);
        FILETIME a, b;
        SystemTimeToFileTime(&now, &a);
        SYSTEMTIME day = now;
        BOOL named = FALSE;
        if (flags & PDFF_RELATIVEDATE) {
            if (is_today(&local, &now)) {
                add_ui(out, 106, &none);
                named = TRUE;
            } else {
                ULARGE_INTEGER u;
                u.LowPart = a.dwLowDateTime;
                u.HighPart = a.dwHighDateTime;
                u.QuadPart -= 864000000000ull;
                b.dwLowDateTime = u.LowPart;
                b.dwHighDateTime = u.HighPart;
                FileTimeToSystemTime(&b, &day);
                if (is_today(&local, &day)) {
                    add_ui(out, 105, &none);
                    named = TRUE;
                } else {
                    u.QuadPart += 2 * 864000000000ull;
                    b.dwLowDateTime = u.LowPart;
                    b.dwHighDateTime = u.HighPart;
                    FileTimeToSystemTime(&b, &day);
                    if (is_today(&local, &day)) {
                        add_ui(out, 107, &none);
                        named = TRUE;
                    }
                }
            }
        }
        if (!named) {
            LPCWSTR pattern = nullptr;
            DWORD date_flags = (flags & PDFF_LONGDATE) ? DATE_LONGDATE : DATE_SHORTDATE;
            if (f->format_as == 1)
                pattern = L"MMMM yyyy", date_flags = 0;
            else if (f->format_as == 2)
                pattern = L"MMMM", date_flags = 0;
            if (GetDateFormatEx(nullptr, date_flags, &local, pattern, buffer, 260, nullptr) <= 0)
                return E_FAIL;
            text_add(out, buffer);
        }
        wrote = TRUE;
    }
    if (!(flags & PDFF_HIDETIME) && f->format_as == 0) {
        DWORD time_flags = (flags & PDFF_LONGTIME) ? 0 : TIME_NOSECONDS;
        if (GetTimeFormatEx(nullptr, time_flags, &local, nullptr, buffer, 260) <= 0)
            return E_FAIL;
        if (wrote)
            text_add(out, L" ");
        text_add(out, buffer);
    }
    return S_OK;
}

static BOOL enum_value_matches(const FORMAT *f, const ENUMINFO *e, const PROPVARIANT *v, BOOL *range_next)
{
    PROPVARIANT value = {};
    VARTYPE vt = f->vt & 0xEFFF;
    LPCWSTR text = OFS_STR(f->schema, e->type == PET_DISCRETEVALUE ? e->value : e->min);
    BOOL match = FALSE;
    (void)range_next;
    if (!text)
        return FALSE;
    PROPVARIANT source = {};
    source.vt = VT_LPWSTR;
    source.pwszVal = (LPWSTR)text;
    if (!*text || vt == VT_LPWSTR || vt == VT_EMPTY || vt == VT_NULL)
        PropVariantCopy(&value, &source);
    else if (FAILED(PropVariantChangeType(&value, &source, 0, vt)))
        return FALSE;
    match = !PropVariantCompareEx(v, &value, PVCU_DEFAULT, 0);
    PropVariantClear(&value);
    return match;
}

static LPCWSTR enum_text(const FORMAT *f, const PROPVARIANT *v)
{
    for (DWORD i = 0; i < f->enum_count; i++) {
        const ENUMINFO *e = &f->enums[i];
        if (e->type == PET_DISCRETEVALUE) {
            if (enum_value_matches(f, e, v, nullptr))
                return OFS_STR(f->schema, e->text);
        } else {
            PROPVARIANT min = {};
            PROPVARIANT source = {};
            LPCWSTR text = OFS_STR(f->schema, e->min);
            VARTYPE vt = f->vt & 0xEFFF;
            BOOL ok = FALSE;
            if (text) {
                source.vt = VT_LPWSTR;
                source.pwszVal = (LPWSTR)text;
                ok = (vt == VT_LPWSTR || vt == VT_EMPTY || vt == VT_NULL) ? SUCCEEDED(PropVariantCopy(&min, &source)) :
                     SUCCEEDED(PropVariantChangeType(&min, &source, 0, vt));
            }
            BOOL match = ok && PropVariantCompareEx(v, &min, PVCU_DEFAULT, 0) >= 0;
            PropVariantClear(&min);
            if (match && i + 1 < f->enum_count && f->enums[i + 1].type != PET_DISCRETEVALUE) {
                PROPVARIANT next = {};
                LPCWSTR next_text = OFS_STR(f->schema, f->enums[i + 1].min);
                if (next_text) {
                    source.pwszVal = (LPWSTR)next_text;
                    if (SUCCEEDED((vt == VT_LPWSTR || vt == VT_EMPTY || vt == VT_NULL) ? PropVariantCopy(&next, &source) :
                                  PropVariantChangeType(&next, &source, 0, vt)))
                        match = PropVariantCompareEx(v, &next, PVCU_DEFAULT, 0) < 0;
                }
                PropVariantClear(&next);
            }
            if (match)
                return OFS_STR(f->schema, e->text);
        }
    }
    return nullptr;
}

static HRESULT format_enumerated(const FORMAT *f, const PROPVARIANT *v, TEXT *out)
{
    ULONG count = PropVariantGetElementCount(v);
    BOOL found = FALSE;
    if (count > 1) {
        UINT shown = 0;
        for (ULONG i = 0; i < count; i++) {
            PROPVARIANT element = {};
            if (FAILED(InitPropVariantFromPropVariantVectorElem(v, i, &element)))
                break;
            LPCWSTR text = enum_text(f, &element);
            if (text) {
                LPWSTR loaded;
                if (shown++)
                    text_add(out, L"; ");
                if (SUCCEEDED(PsLoadIndirect(text, &loaded))) {
                    text_add(out, loaded);
                    CoTaskMemFree(loaded);
                }
            }
            PropVariantClear(&element);
        }
        found = shown != 0;
    } else {
        LPCWSTR text = enum_text(f, v);
        if (text) {
            LPWSTR loaded;
            if (SUCCEEDED(PsLoadIndirect(text, &loaded))) {
                text_add(out, loaded);
                CoTaskMemFree(loaded);
            }
            found = TRUE;
        }
    }
    if (found)
        return S_OK;
    if (f->enum_use_default) {
        LPWSTR text;
        HRESULT hr = PropVariantToStringAlloc(v, &text);
        if (SUCCEEDED(hr)) {
            text_add(out, text);
            CoTaskMemFree(text);
        }
        return hr;
    }
    if (f->enum_default_text) {
        LPWSTR loaded;
        if (SUCCEEDED(PsLoadIndirect(f->enum_default_text, &loaded))) {
            text_add(out, loaded);
            CoTaskMemFree(loaded);
        }
        return S_OK;
    }
    return count ? E_FAIL : S_FALSE;
}

/* a vector shown as one value according to the aggregation of the property. */
static HRESULT format_aggregated(const FORMAT *f, const PROPVARIANT *v, TEXT *out)
{
    ULONG count = PropVariantGetElementCount(v);
    ARG none = {};
    FORMAT single = *f;
    single.aggregation = 0;
    single.multiple = TRUE;
    HRESULT hr = S_OK;
    PROPVARIANT best = {};
    switch (f->aggregation) {
    case PDAT_DEFAULT:
        add_ui(out, 171, &none);
        return S_OK;
    case PDAT_FIRST: {
        hr = InitPropVariantFromPropVariantVectorElem(v, 0, &best);
        if (SUCCEEDED(hr))
            hr = format_value(&single, &best, out);
        PropVariantClear(&best);
        return hr;
    }
    case PDAT_SUM: case PDAT_AVERAGE: {
        double sum = 0;
        for (ULONG i = 0; i < count; i++) {
            PROPVARIANT element = {};
            double x;
            hr = InitPropVariantFromPropVariantVectorElem(v, i, &element);
            if (SUCCEEDED(hr))
                hr = PropVariantToDouble(&element, &x);
            PropVariantClear(&element);
            if (FAILED(hr))
                return hr;
            sum += x;
        }
        if (f->aggregation == PDAT_AVERAGE)
            sum /= count;
        PROPVARIANT result = {};
        result.vt = VT_R8;
        result.dblVal = sum;
        if (sum == (double)(LONGLONG)sum && sum >= 0) {
            result.vt = VT_UI8;
            result.uhVal.QuadPart = (ULONGLONG)sum;
        }
        return format_value(&single, &result, out);
    }
    case PDAT_UNION: {
        UINT shown = 0;
        for (ULONG i = 0; i < count; i++) {
            PROPVARIANT element = {};
            TEXT piece = {};
            if (SUCCEEDED(InitPropVariantFromPropVariantVectorElem(v, i, &element)) &&
                SUCCEEDED(format_value(&single, &element, &piece)) && piece.data) {
                BOOL duplicate = FALSE;
                for (SIZE_T p = 0; out->data && !duplicate;) {
                    LPCWSTR end = wcsstr(out->data + p, L"; ");
                    SIZE_T n = end ? (SIZE_T)(end - (out->data + p)) : out->length - p;
                    duplicate = n == piece.length && !wcsncmp(out->data + p, piece.data, n);
                    if (!end)
                        break;
                    p += n + 2;
                }
                if (!duplicate) {
                    if (shown++)
                        text_add(out, L"; ");
                    text_add(out, piece.data);
                }
            }
            text_free(&piece);
            PropVariantClear(&element);
        }
        return S_OK;
    }
    case PDAT_DATERANGE: case PDAT_MAX: case PDAT_MIN: {
        PROPVARIANT lowest = {}, highest = {};
        for (ULONG i = 0; i < count; i++) {
            PROPVARIANT element = {};
            if (FAILED(InitPropVariantFromPropVariantVectorElem(v, i, &element)))
                continue;
            if (lowest.vt == VT_EMPTY || PropVariantCompareEx(&element, &lowest, PVCU_DEFAULT, 0) < 0) {
                PropVariantClear(&lowest);
                PropVariantCopy(&lowest, &element);
            }
            if (highest.vt == VT_EMPTY || PropVariantCompareEx(&element, &highest, PVCU_DEFAULT, 0) > 0) {
                PropVariantClear(&highest);
                PropVariantCopy(&highest, &element);
            }
            PropVariantClear(&element);
        }
        if (f->aggregation == PDAT_MAX)
            hr = format_value(&single, &highest, out);
        else if (f->aggregation == PDAT_MIN)
            hr = format_value(&single, &lowest, out);
        else {
            TEXT a = {}, b = {};
            hr = format_value(&single, &lowest, &a);
            if (SUCCEEDED(hr))
                hr = format_value(&single, &highest, &b);
            if (SUCCEEDED(hr)) {
                ARG args[2];
                args[0].s = a.data ? a.data : L"";
                args[1].s = b.data ? b.data : L"";
                if (!wcscmp(args[0].s, args[1].s))
                    text_add(out, args[0].s);
                else
                    add_ui(out, 197, args);
            }
            text_free(&a);
            text_free(&b);
        }
        PropVariantClear(&lowest);
        PropVariantClear(&highest);
        return hr;
    }
    }
    return E_FAIL;
}

static HRESULT format_value(const FORMAT *f, const PROPVARIANT *v, TEXT *out)
{
    VARTYPE vt = v->vt;
    ULONG count = 1;
    if (vt == VT_ERROR && v->scode == (SCODE)0x80020011) {
        ARG none = {};
        add_ui(out, 171, &none);
        return S_OK;
    }
    if (vt & (VT_VECTOR | VT_ARRAY)) {
        count = PropVariantGetElementCount(v);
        if (!count)
            return f->display_type == PDDT_ENUMERATED ? format_enumerated(f, v, out) : S_FALSE;
        if (count > 1 && !f->multiple)
            return format_aggregated(f, v, out);
    }
    if (!supported_type(vt))
        return E_INVALIDARG;
    if (f->tree) {
        LPWSTR text;
        HRESULT hr = PropVariantToStringAlloc(v, &text);
        if (SUCCEEDED(hr)) {
            for (LPWSTR p = text; *p; p++)
                if (*p == '/' || *p == '\\')
                    *p = '\\';
            text_add(out, text);
            CoTaskMemFree(text);
        }
        return hr;
    }
    if (vt & (VT_VECTOR | VT_ARRAY)) {
        /* multiple values: each element in the format of the type, joined. */
        if (f->display_type == PDDT_ENUMERATED)
            return format_enumerated(f, v, out);
        for (ULONG i = 0; i < count; i++) {
            PROPVARIANT element = {};
            HRESULT hr = InitPropVariantFromPropVariantVectorElem(v, i, &element);
            if (SUCCEEDED(hr)) {
                if (i)
                    text_add(out, L"; ");
                FORMAT single = *f;
                single.multiple = FALSE;
                hr = format_value(&single, &element, out);
            }
            PropVariantClear(&element);
            if (FAILED(hr))
                return hr;
        }
        return S_OK;
    }
    switch (f->display_type) {
    case PDDT_NUMBER:
        return format_number(f, v, out);
    case PDDT_BOOLEAN:
        return format_boolean(f, v, out);
    case PDDT_DATETIME:
        return format_date_time(f, v, out);
    case PDDT_ENUMERATED:
        return format_enumerated(f, v, out);
    }
    {
        WCHAR buffer[1024];
        HRESULT hr = PropVariantToString(v, buffer, 1024);
        if (SUCCEEDED(hr))
            text_add(out, buffer);
        return hr;
    }
}

static DWORD display_type_of(VARTYPE vt)
{
    switch (vt & 0xEFFF) {
    case VT_BOOL: return PDDT_BOOLEAN;
    case VT_FILETIME: case VT_DATE: return PDDT_DATETIME;
    case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8:
    case VT_INT: case VT_UINT: case VT_I1:
        return PDDT_NUMBER;
    }
    return PDDT_STRING;
}

static void fill_format(FORMAT *f, SCHEMA *schema, const PDESC *d, DWORD flags)
{
    memset(f, 0, sizeof(*f));
    f->schema = schema;
    f->display_type = d->display_type;
    f->format_as = d->format_as;
    f->date_as = d->date_as;
    f->time_as = d->time_as;
    f->duration_as = d->duration_as;
    f->enums = d->enums ? (const ENUMINFO *)(schema->base + d->enums) : nullptr;
    f->enum_count = d->enum_count;
    f->enum_use_default = d->enum_use_default;
    f->enum_default_text = OFS_STR(schema, d->enum_default_text);
    f->vt = (VARTYPE)d->vt;
    f->multiple = (d->type_flags & PDTF_MULTIPLEVALUES) != 0;
    f->tree = (d->type_flags & PDTF_ISTREEPROPERTY) != 0;
    f->aggregation = d->aggregation;
    f->flags = flags;
    if (flags & PDFF_FILENAME)
        f->format_as = 1;
}

HRESULT DescFormat(SCHEMA *schema, const PDESC *desc, const PROPVARIANT *value, PROPDESC_FORMAT_FLAGS flags,
                   LPWSTR *text)
{
    *text = nullptr;
    if (!PsValidValue(value))
        return E_INVALIDARG;
    FORMAT f;
    fill_format(&f, schema, desc, flags);
    TEXT out = {};
    HRESULT hr = format_value(&f, value, &out);
    if (hr == S_OK && (flags & PDFF_PREFIXNAME)) {
        LPCWSTR label = OFS_STR(schema, desc->label);
        LPWSTR loaded;
        if (label && SUCCEEDED(PsLoadIndirect(label, &loaded))) {
            TEXT prefixed = {};
            ARG args[2];
            args[0].s = loaded;
            args[1].s = out.data ? out.data : L"";
            add_ui(&prefixed, 122, args);
            CoTaskMemFree(loaded);
            text_free(&out);
            out = prefixed;
        }
    } else if (flags & PDFF_USEEDITINVITATION) {
        if (FAILED(hr) || !out.length) {
            text_free(&out);
            if (flags & PDFF_READONLY) {
                ARG none = {};
                add_ui(&out, 120, &none);
                hr = S_OK;
            } else {
                LPCWSTR invitation = OFS_STR(schema, desc->invitation);
                LPWSTR loaded;
                if (invitation && SUCCEEDED(PsLoadIndirect(invitation, &loaded))) {
                    text_add(&out, loaded);
                    CoTaskMemFree(loaded);
                } else {
                    ARG none = {};
                    add_ui(&out, 121, &none);
                }
                hr = S_OK;
            }
        }
    }
    if (SUCCEEDED(hr)) {
        HRESULT made = text_result(&out, text);
        if (FAILED(made))
            hr = made;
    }
    text_free(&out);
    return hr;
}

/* ------------------------------------------------------------------ canonical values */

static BOOL is_space(WCHAR c)
{
    return c == ' ' || (c >= 9 && c <= 13) || c == 0x85 || c == 0xA0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A);
}

/* the string trimmed of spaces on both sides, and whether that changed it. */
static HRESULT canonical_string(LPWSTR *string, BOOL *changed)
{
    LPWSTR s = *string;
    SIZE_T length = wcslen(s), start = 0, end = length;
    while (end > start && is_space(s[end - 1]))
        end--;
    while (start < end && is_space(s[start]))
        start++;
    *changed = start != 0 || end != length;
    if (*changed) {
        memmove(s, s + start, (end - start) * sizeof(WCHAR));
        s[end - start] = 0;
    }
    return S_OK;
}

HRESULT DescCoerce(SCHEMA *schema, const PDESC *desc, PROPVARIANT *value)
{
    HRESULT result = S_OK;
    VARTYPE target = (VARTYPE)desc->vt;
    BOOL changed = FALSE;
    (void)schema;
    if (value->vt == VT_NULL) {
        value->vt = VT_EMPTY;
        return S_OK;
    }
    if (value->vt == VT_EMPTY)
        return S_OK;
    if (value->vt != target) {
        if (value->vt == VT_BSTR || value->vt == VT_LPWSTR) {
            LPCWSTR s = value->vt == VT_BSTR ? value->bstrVal : value->pwszVal;
            if (!s && value->vt == VT_LPWSTR) {
                PropVariantClear(value);
                return S_OK;
            }
            if (!s || !*s) {
                BOOL keep = (desc->type_flags & PDTF_DONTCOERCEEMPTYSTRINGS) != 0;
                if (!keep) {
                    PropVariantClear(value);
                    return 0x40320;
                }
            } else {
                LPCWSTR p = s;
                while (*p && is_space(*p))
                    p++;
                if (!*p) {
                    PropVariantClear(value);
                    return 0x40320;
                }
            }
        }
        if (value->vt == VT_FILETIME && value->uhVal.QuadPart < 0xC92A69C000ull) {
            PropVariantClear(value);
            return 0x40320;
        }
        if (target != VT_NULL) {
            PROPVARIANT converted = {};
            HRESULT hr = PropVariantChangeType(&converted, value, 0, target);
            if (FAILED(hr)) {
                PropVariantClear(value);
                return hr;
            }
            PropVariantClear(value);
            *value = converted;
            changed = TRUE;
        }
    } else if (value->vt == VT_LPWSTR) {
        if (!value->pwszVal || !*value->pwszVal) {
            if (!(desc->type_flags & PDTF_DONTCOERCEEMPTYSTRINGS)) {
                PropVariantClear(value);
                return 0x40320;
            }
        }
    }
    if (value->vt == VT_LPWSTR && value->pwszVal && !(desc->type_flags & PDTF_ISTREEPROPERTY)) {
        BOOL trimmed;
        canonical_string(&value->pwszVal, &trimmed);
        if (trimmed)
            changed = TRUE;
        if (!*value->pwszVal && !(desc->type_flags & PDTF_DONTCOERCEEMPTYSTRINGS)) {
            PropVariantClear(value);
            return 0x40320;
        }
    }
    if (value->vt == (VT_LPWSTR | VT_VECTOR)) {
        for (ULONG i = 0; i < value->calpwstr.cElems; i++)
            if (value->calpwstr.pElems[i]) {
                BOOL trimmed;
                canonical_string(&value->calpwstr.pElems[i], &trimmed);
                changed |= trimmed;
            }
    }
    if (changed)
        result = 0x40320;
    return result;
}

HRESULT DescImageReference(SCHEMA *schema, const PDESC *desc, const PROPVARIANT *value, LPWSTR *image)
{
    *image = nullptr;
    PROPVARIANT copy = {};
    HRESULT hr = PropVariantCopy(&copy, value);
    if (FAILED(hr))
        return hr;
    hr = DescCoerce(schema, desc, &copy);
    if (SUCCEEDED(hr)) {
        hr = TYPE_E_ELEMENTNOTFOUND;
        FORMAT f;
        fill_format(&f, schema, desc, 0);
        for (DWORD i = 0; i < f.enum_count; i++) {
            const ENUMINFO *e = &f.enums[i];
            BOOL match = e->type == PET_DISCRETEVALUE ? enum_value_matches(&f, e, &copy, nullptr) : FALSE;
            if (e->type != PET_DISCRETEVALUE) {
                LPCWSTR text = enum_text(&f, &copy);
                match = text && text == OFS_STR(schema, e->text);
            }
            if (match) {
                LPCWSTR path = OFS_STR(schema, e->image);
                SIZE_T bytes = ((path ? wcslen(path) : 0) + 1) * sizeof(WCHAR);
                *image = CoTaskMemAlloc(bytes);
                if (!*image) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                memcpy(*image, path ? path : L"", bytes);
                hr = S_OK;
                break;
            }
        }
    }
    PropVariantClear(&copy);
    return hr;
}

/* ------------------------------------------------------------------ the format functions of the dll */

static HRESULT format_without_description(const PROPVARIANT *value, PROPDESC_FORMAT_FLAGS flags, LPWSTR *text)
{
    *text = nullptr;
    FORMAT f;
    memset(&f, 0, sizeof(f));
    f.display_type = display_type_of(value->vt);
    f.multiple = TRUE;
    f.flags = flags;
    f.format_as = (flags & PDFF_FILENAME) ? 1 : f.display_type == PDDT_BOOLEAN ? 202 : 0;
    f.vt = VT_EMPTY;
    TEXT out = {};
    HRESULT hr = format_value(&f, value, &out);
    if (SUCCEEDED(hr)) {
        HRESULT made = text_result(&out, text);
        if (FAILED(made))
            hr = made;
    }
    text_free(&out);
    return hr;
}

HRESULT WINAPI PSFormatForDisplayAlloc(REFPROPERTYKEY key, REFPROPVARIANT value, PROPDESC_FORMAT_FLAGS flags, LPWSTR *text)
{
    if (!text)
        return E_POINTER;
    *text = nullptr;
    SCHEMA *schema;
    DWORD index;
    if (SUCCEEDED(SchemaFindKey(key, &schema, &index))) {
        HRESULT hr = DescFormat(schema, &schema->descs[index], value, flags, text);
        SchemaRelease(schema);
        return hr;
    }
    return format_without_description(value, flags, text);
}

HRESULT WINAPI PSFormatForDisplay(REFPROPERTYKEY key, REFPROPVARIANT value, PROPDESC_FORMAT_FLAGS flags, LPWSTR buffer,
                                  DWORD length)
{
    if (buffer && length)
        *buffer = 0;
    LPWSTR text;
    HRESULT hr = PSFormatForDisplayAlloc(key, value, flags, &text);
    if (SUCCEEDED(hr)) {
        if (!length || length > 0x7FFFFFFF)
            hr = E_INVALIDARG;
        else
            hr = PsCopyString(buffer, length, text);
        CoTaskMemFree(text);
    }
    return hr;
}

HRESULT WINAPI PSFormatPropertyValue(IPropertyStore *store, IPropertyDescription *description,
                                     PROPDESC_FORMAT_FLAGS flags, LPWSTR *text)
{
    PROPVARIANT value = {};
    *text = nullptr;
    HRESULT hr = PSGetPropertyValue(store, description, &value);
    if (SUCCEEDED(hr))
        hr = description->lpVtbl->FormatForDisplay(description, &value, flags, text);
    PropVariantClear(&value);
    return hr;
}

HRESULT WINAPI PSGetImageReferenceForValue(REFPROPERTYKEY key, const PROPVARIANT *value, LPWSTR *image)
{
    if (!image)
        return E_POINTER;
    *image = nullptr;
    SCHEMA *schema;
    DWORD index;
    HRESULT hr = SchemaFindKey(key, &schema, &index);
    if (FAILED(hr))
        return hr;
    hr = DescImageReference(schema, &schema->descs[index], value, image);
    SchemaRelease(schema);
    return hr;
}

HRESULT WINAPI PSCoerceToCanonicalValue(REFPROPERTYKEY key, PROPVARIANT *value)
{
    SCHEMA *schema;
    DWORD index;
    if (FAILED(SchemaFindKey(key, &schema, &index)))
        return S_FALSE;
    HRESULT hr = DescCoerce(schema, &schema->descs[index], value);
    SchemaRelease(schema);
    return hr;
}

HRESULT WINAPI PSGetPropertyValue(IPropertyStore *store, IPropertyDescription *description, PROPVARIANT *value)
{
    PROPERTYKEY key;
    memset(value, 0, sizeof(*value));
    HRESULT hr = description->lpVtbl->GetPropertyKey(description, &key);
    if (SUCCEEDED(hr))
        hr = store->lpVtbl->GetValue(store, &key, value);
    return hr;
}

HRESULT WINAPI PSSetPropertyValue(IPropertyStore *store, IPropertyDescription *description, const PROPVARIANT *value)
{
    PROPERTYKEY key;
    HRESULT hr = description->lpVtbl->GetPropertyKey(description, &key);
    if (SUCCEEDED(hr))
        hr = store->lpVtbl->SetValue(store, &key, value);
    return hr;
}
