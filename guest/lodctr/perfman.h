/*
 * the performance counter manifests that lodctr and unlodctr read: a provider with counter sets and the counters of each.
 * the manifest is xml in utf-8 or utf-16; only the tags and attributes the registration needs are kept.
 */
#ifndef MIME_PERFMAN_H
#define MIME_PERFMAN_H

#include "tool.h"

static int pm_nicmp(const WCHAR *a, const WCHAR *b, SIZE_T n)
{
    for (; n; n--, a++, b++) {
        WCHAR x = tu_lower(*a), y = tu_lower(*b);
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            break;
    }
    return 0;
}

static int pm_icmp(const WCHAR *a, const WCHAR *b)
{
    return pm_nicmp(a, b, (SIZE_T)-1);
}

typedef struct PM_COUNTER {
    DWORD Id;
    DWORD Type;
    DWORD DetailLevel;
    LONGLONG DefaultScale;
} PM_COUNTER;

typedef struct PM_SET {
    WCHAR *Guid, *Name;
    DWORD InstanceType;
    PM_COUNTER *Counters;
    DWORD Count, Capacity;
} PM_SET;

typedef struct PM_PROVIDER {
    WCHAR *Guid, *Name, *Application;
    DWORD Type;
    PM_SET *Sets;
    DWORD SetCount, SetCapacity;
} PM_PROVIDER;

typedef struct PM_MANIFEST {
    PM_PROVIDER *Providers;
    DWORD Count, Capacity;
} PM_MANIFEST;

static const struct {
    const WCHAR *Name;
    DWORD Value;
} pm_types[] = {
    {L"perf_counter_counter", 0x10410400}, {L"perf_counter_timer", 0x20410500},
    {L"perf_counter_queuelen_type", 0x00450400}, {L"perf_counter_large_queuelen_type", 0x00450500},
    {L"perf_counter_100ns_queuelen_type", 0x00550500}, {L"perf_counter_obj_time_queuelen_type", 0x00650500},
    {L"perf_counter_bulk_count", 0x10410500}, {L"perf_counter_text", 0x00B00000},
    {L"perf_counter_rawcount", 0x00010000}, {L"perf_counter_large_rawcount", 0x00010100},
    {L"perf_counter_rawcount_hex", 0x00000000}, {L"perf_counter_large_rawcount_hex", 0x00000100},
    {L"perf_sample_fraction", 0x20C20400}, {L"perf_sample_counter", 0x00410400},
    {L"perf_counter_timer_inv", 0x21410500}, {L"perf_sample_base", 0x40030401},
    {L"perf_average_timer", 0x30020400}, {L"perf_average_base", 0x40030402}, {L"perf_average_bulk", 0x40020500},
    {L"perf_obj_time_timer", 0x20610500}, {L"perf_100nsec_timer", 0x20510500},
    {L"perf_100nsec_timer_inv", 0x21510500}, {L"perf_counter_multi_timer", 0x22410500},
    {L"perf_counter_multi_timer_inv", 0x23410500}, {L"perf_counter_multi_base", 0x42030500},
    {L"perf_100nsec_multi_timer", 0x22510500}, {L"perf_100nsec_multi_timer_inv", 0x23510500},
    {L"perf_raw_fraction", 0x20020400}, {L"perf_raw_base", 0x40030403}, {L"perf_elapsed_time", 0x30240500},
    {L"perf_counter_histogram_type", 0x80000000}, {L"perf_counter_delta", 0x00400400},
    {L"perf_counter_large_delta", 0x00400500}, {L"perf_precision_system_timer", 0x46500500},
    {L"perf_precision_100ns_timer", 0x46510500}, {L"perf_precision_object_timer", 0x46610500},
};

static BOOL pm_name_is(const WCHAR *name, SIZE_T length, const WCHAR *wanted)
{
    SIZE_T n = wcslen(wanted);
    return length == n && !pm_nicmp(name, wanted, n);
}

static DWORD pm_lookup(const WCHAR *text, DWORD fallback, const WCHAR *const *names, const DWORD *values, int count)
{
    for (int i = 0; i < count; i++)
        if (!pm_icmp(text, names[i]))
            return values[i];
    return fallback;
}

static DWORD pm_type_value(const WCHAR *text, BOOL *known)
{
    for (SIZE_T i = 0; i < TU_ARRAY(pm_types); i++) {
        if (!pm_icmp(text, pm_types[i].Name)) {
            *known = TRUE;
            return pm_types[i].Value;
        }
    }
    *known = FALSE;
    return 0;
}

static DWORD pm_instance_type(const WCHAR *text)
{
    static const WCHAR *const names[] = {L"single", L"multiple", L"singleAggregate", L"multipleAggregate", L"instanceAggregate",
                                         L"globalAggregate", L"globalAggregateWithHistory", L"multipleAggregateHistory"};
    static const DWORD values[] = {0, 2, 4, 6, 22, 4, 12, 12};
    return pm_lookup(text, 0, names, values, TU_ARRAY(values));
}

static DWORD pm_provider_type(const WCHAR *text)
{
    static const WCHAR *const names[] = {L"userMode", L"kernelMode", L"driver"};
    static const DWORD values[] = {0, 1, 2};
    return pm_lookup(text, 0, names, values, 3);
}

static DWORD pm_detail(const WCHAR *text)
{
    static const WCHAR *const names[] = {L"novice", L"advanced", L"expert", L"wizard"};
    static const DWORD values[] = {100, 200, 300, 400};
    return pm_lookup(text, 100, names, values, 4);
}

/* the attributes of a tag, as names and values in one block. */
typedef struct PM_TAG {
    const WCHAR *Name;
    SIZE_T NameLength;
    BOOL Closing, SelfClosing;
    struct {
        const WCHAR *Name;
        SIZE_T NameLength;
        WCHAR *Value;
    } Attributes[24];
    int AttributeCount;
} PM_TAG;

static void pm_tag_free(PM_TAG *tag)
{
    for (int i = 0; i < tag->AttributeCount; i++)
        tu_free(tag->Attributes[i].Value);
    tag->AttributeCount = 0;
}

static WCHAR *pm_attribute(const PM_TAG *tag, const WCHAR *name)
{
    for (int i = 0; i < tag->AttributeCount; i++)
        if (pm_name_is(tag->Attributes[i].Name, tag->Attributes[i].NameLength, name))
            return tag->Attributes[i].Value;
    return nullptr;
}

/* a value with the five predefined entities resolved. */
static WCHAR *pm_decode(const WCHAR *text, SIZE_T length)
{
    WCHAR *out = tu_alloc((length + 1) * sizeof(WCHAR));
    SIZE_T n = 0;
    for (SIZE_T i = 0; i < length; i++) {
        if (text[i] == L'&') {
            static const struct {
                const WCHAR *Entity;
                WCHAR Char;
            } entities[] = {{L"&amp;", L'&'}, {L"&lt;", L'<'}, {L"&gt;", L'>'}, {L"&quot;", L'"'}, {L"&apos;", L'\''}};
            BOOL done = FALSE;
            for (int e = 0; e < 5 && !done; e++) {
                SIZE_T m = wcslen(entities[e].Entity);
                if (i + m <= length && !wcsncmp(text + i, entities[e].Entity, m)) {
                    out[n++] = entities[e].Char;
                    i += m - 1;
                    done = TRUE;
                }
            }
            if (done)
                continue;
        }
        out[n++] = text[i];
    }
    out[n] = 0;
    return out;
}

/* the next tag at or after the cursor, or false at the end; comments, declarations and text are skipped. */
static BOOL pm_next_tag(const WCHAR **cursor, const WCHAR *end, PM_TAG *tag)
{
    memset(tag, 0, sizeof(*tag));
    const WCHAR *p = *cursor;
    for (;;) {
        while (p < end && *p != L'<')
            p++;
        if (p >= end)
            return FALSE;
        if (end - p >= 4 && !wcsncmp(p, L"<!--", 4)) {
            const WCHAR *q = p + 4;
            while (q + 3 <= end && wcsncmp(q, L"-->", 3))
                q++;
            p = q + 3 <= end ? q + 3 : end;
            continue;
        }
        if (p + 1 < end && (p[1] == L'?' || p[1] == L'!')) {
            while (p < end && *p != L'>')
                p++;
            continue;
        }
        break;
    }
    p++;
    if (p < end && *p == L'/') {
        tag->Closing = TRUE;
        p++;
    }
    const WCHAR *name = p;
    while (p < end && !tu_is_space(*p) && *p != L'>' && *p != L'/')
        p++;
    /* a namespace prefix is not part of the name. */
    for (const WCHAR *c = name; c < p; c++)
        if (*c == L':')
            name = c + 1;
    tag->Name = name;
    tag->NameLength = (SIZE_T)(p - name);
    while (p < end && *p != L'>') {
        while (p < end && tu_is_space(*p))
            p++;
        if (p < end && *p == L'/') {
            tag->SelfClosing = TRUE;
            p++;
            continue;
        }
        if (p >= end || *p == L'>')
            break;
        const WCHAR *attribute = p;
        while (p < end && !tu_is_space(*p) && *p != L'=' && *p != L'>' && *p != L'/')
            p++;
        SIZE_T attribute_length = (SIZE_T)(p - attribute);
        while (p < end && tu_is_space(*p))
            p++;
        if (p >= end || *p != L'=')
            continue;
        p++;
        while (p < end && tu_is_space(*p))
            p++;
        if (p >= end || (*p != L'"' && *p != L'\''))
            continue;
        WCHAR quote = *p++;
        const WCHAR *value = p;
        while (p < end && *p != quote)
            p++;
        if (tag->AttributeCount < (int)TU_ARRAY(tag->Attributes)) {
            for (const WCHAR *c = attribute; c < attribute + attribute_length; c++)
                if (*c == L':')
                    attribute_length -= (SIZE_T)(c + 1 - attribute), attribute = c + 1;
            tag->Attributes[tag->AttributeCount].Name = attribute;
            tag->Attributes[tag->AttributeCount].NameLength = attribute_length;
            tag->Attributes[tag->AttributeCount].Value = pm_decode(value, (SIZE_T)(p - value));
            tag->AttributeCount++;
        }
        if (p < end)
            p++;
    }
    if (p < end)
        p++;
    *cursor = p;
    return TRUE;
}

static void pm_grow(void **array, DWORD *capacity, DWORD count, SIZE_T item)
{
    if (count < *capacity)
        return;
    DWORD bigger = *capacity ? *capacity * 2 : 8;
    *array = tu_realloc(*array, bigger * item);
    memset((BYTE *)*array + (SIZE_T)count * item, 0, (SIZE_T)(bigger - count) * item);
    *capacity = bigger;
}

/* the text of the file as wide characters, whichever way it is written. */
static WCHAR *pm_read(const WCHAR *path, SIZE_T *length, DWORD *error)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return nullptr;
    }
    DWORD size = GetFileSize(file, nullptr);
    BYTE *bytes = tu_alloc((SIZE_T)size + 2);
    DWORD read = 0;
    BOOL ok = size != INVALID_FILE_SIZE && ReadFile(file, bytes, size, &read, nullptr);
    *error = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (!ok) {
        tu_free(bytes);
        return nullptr;
    }
    WCHAR *text;
    if (read >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        *length = (read - 2) / 2;
        text = tu_alloc((*length + 1) * sizeof(WCHAR));
        memcpy(text, bytes + 2, *length * sizeof(WCHAR));
    } else {
        SIZE_T skip = read >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
        int needed = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)bytes + skip, (int)(read - skip), nullptr, 0);
        text = tu_alloc(((SIZE_T)needed + 1) * sizeof(WCHAR));
        needed = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)bytes + skip, (int)(read - skip), text, needed);
        *length = (SIZE_T)needed;
    }
    text[*length] = 0;
    tu_free(bytes);
    return text;
}

/* false with a message when the file is no counters manifest. */
static BOOL pm_parse(const WCHAR *path, PM_MANIFEST *manifest, DWORD *error)
{
    memset(manifest, 0, sizeof(*manifest));
    SIZE_T length;
    WCHAR *text = pm_read(path, &length, error);
    if (!text)
        return FALSE;
    const WCHAR *cursor = text, *end = text + length;
    PM_TAG tag;
    PM_PROVIDER *provider = nullptr;
    PM_SET *set = nullptr;
    BOOL in_counters = FALSE, any = FALSE;
    while (pm_next_tag(&cursor, end, &tag)) {
        if (pm_name_is(tag.Name, tag.NameLength, L"counters")) {
            in_counters = !tag.Closing;
            any |= !tag.Closing;
        } else if (!in_counters) {
        } else if (pm_name_is(tag.Name, tag.NameLength, L"provider")) {
            if (tag.Closing) {
                provider = nullptr;
                set = nullptr;
            } else {
                pm_grow((void **)&manifest->Providers, &manifest->Capacity, manifest->Count, sizeof(PM_PROVIDER));
                provider = &manifest->Providers[manifest->Count++];
                WCHAR *guid = pm_attribute(&tag, L"providerGuid"), *name = pm_attribute(&tag, L"providerName");
                WCHAR *application = pm_attribute(&tag, L"applicationIdentity"), *kind = pm_attribute(&tag, L"providerType");
                provider->Guid = guid ? tu_dup(guid) : nullptr;
                provider->Name = name ? tu_dup(name) : nullptr;
                provider->Application = application ? tu_dup(application) : tu_dup(L"");
                provider->Type = kind ? pm_provider_type(kind) : 0;
                if (tag.SelfClosing)
                    provider = nullptr;
            }
        } else if (provider && pm_name_is(tag.Name, tag.NameLength, L"counterSet")) {
            if (tag.Closing) {
                set = nullptr;
            } else {
                pm_grow((void **)&provider->Sets, &provider->SetCapacity, provider->SetCount, sizeof(PM_SET));
                set = &provider->Sets[provider->SetCount++];
                WCHAR *guid = pm_attribute(&tag, L"guid"), *name = pm_attribute(&tag, L"name");
                WCHAR *instances = pm_attribute(&tag, L"instances");
                set->Guid = guid ? tu_dup(guid) : nullptr;
                set->Name = name ? tu_dup(name) : tu_dup(L"");
                set->InstanceType = instances ? pm_instance_type(instances) : 0;
                if (tag.SelfClosing)
                    set = nullptr;
            }
        } else if (set && !tag.Closing && pm_name_is(tag.Name, tag.NameLength, L"counter")) {
            pm_grow((void **)&set->Counters, &set->Capacity, set->Count, sizeof(PM_COUNTER));
            PM_COUNTER *counter = &set->Counters[set->Count++];
            WCHAR *id = pm_attribute(&tag, L"id"), *kind = pm_attribute(&tag, L"type");
            WCHAR *detail = pm_attribute(&tag, L"detailLevel"), *scale = pm_attribute(&tag, L"defaultScale");
            BOOL known = FALSE;
            DWORD value = 0;
            tu_parse_u32(id ? id : L"", &value);
            counter->Id = value;
            counter->Type = kind ? pm_type_value(kind, &known) : 0;
            if (kind && !known) {
                *error = ERROR_INVALID_DATA;
                pm_tag_free(&tag);
                tu_free(text);
                return FALSE;
            }
            counter->DetailLevel = detail ? pm_detail(detail) : 100;
            LONG number = 0;
            counter->DefaultScale = scale && tu_parse_int(scale, &number) ? number : 0;
        }
        pm_tag_free(&tag);
    }
    tu_free(text);
    if (!any || !manifest->Count) {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    for (DWORD i = 0; i < manifest->Count; i++) {
        if (!manifest->Providers[i].Guid || !manifest->Providers[i].Name) {
            *error = ERROR_INVALID_DATA;
            return FALSE;
        }
    }
    return TRUE;
}

static void pm_free(PM_MANIFEST *manifest)
{
    for (DWORD i = 0; i < manifest->Count; i++) {
        PM_PROVIDER *p = &manifest->Providers[i];
        for (DWORD s = 0; s < p->SetCount; s++) {
            tu_free(p->Sets[s].Guid);
            tu_free(p->Sets[s].Name);
            tu_free(p->Sets[s].Counters);
        }
        tu_free(p->Sets);
        tu_free(p->Guid);
        tu_free(p->Name);
        tu_free(p->Application);
    }
    tu_free(manifest->Providers);
    memset(manifest, 0, sizeof(*manifest));
}

/* the argument of a switch such as /m:file or -M:file, or null when the word is another switch. */
static const WCHAR *pm_switch_value(const WCHAR *word, WCHAR letter)
{
    if ((word[0] != L'/' && word[0] != L'-') || tu_lower(word[1]) != tu_lower(letter) || word[2] != L':')
        return nullptr;
    return word + 3;
}

#define PM_PERFLIB L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Perflib"

static WCHAR *pm_key_name(const WCHAR *guid)
{
    WCHAR *name = tu_dup(guid);
    for (WCHAR *c = name; *c; c++)
        *c = tu_lower(*c);
    return name;
}

#endif
