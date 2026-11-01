/* the performance data of the machine as pdh reads it: the global block of the performance registry, and its names. */
#include "pdh_private.h"

#define PERF_BUFFER_START 0x10000

static SRWLOCK g_names_lock = SRWLOCK_INIT;
static PDH_NAME *g_counter_names, *g_help_names;
static DWORD g_counter_count, g_help_count;
static BOOL g_names_loaded;

static void *heap_alloc(SIZE_T size)
{
    return LocalAlloc(LMEM_ZEROINIT, size);
}

PDH_STATUS PdhiSnapshot(PDH_SNAPSHOT *snapshot)
{
    DWORD size = PERF_BUFFER_START;
    snapshot->Data = nullptr;
    snapshot->Size = 0;
    for (;;) {
        BYTE *data = heap_alloc(size);
        if (!data)
            return PDH_MEMORY_ALLOCATION_FAILURE;
        DWORD got = size, type;
        LONG error = RegQueryValueExW(HKEY_PERFORMANCE_DATA, L"Global", nullptr, &type, data, &got);
        if (error == ERROR_SUCCESS) {
            const PERF_BLOCK_ *block = (const PERF_BLOCK_ *)data;
            if (got < sizeof(*block) || block->Signature[0] != L'P' || block->Signature[1] != L'E') {
                LocalFree(data);
                return PDH_CANNOT_READ_NAME_STRINGS;
            }
            snapshot->Data = data;
            snapshot->Size = got;
            return ERROR_SUCCESS;
        }
        LocalFree(data);
        if (error != ERROR_MORE_DATA || size >= 0x4000000)
            return PDH_CANNOT_CONNECT_MACHINE;
        size *= 4;
    }
}

void PdhiFreeSnapshot(PDH_SNAPSHOT *snapshot)
{
    if (snapshot->Data)
        LocalFree(snapshot->Data);
    snapshot->Data = nullptr;
    snapshot->Size = 0;
}

const PERF_BLOCK_ *PdhiBlock(const PDH_SNAPSHOT *snapshot)
{
    return (const PERF_BLOCK_ *)snapshot->Data;
}

const PERF_OBJECT_ *PdhiObject(const PDH_SNAPSHOT *snapshot, DWORD position)
{
    const PERF_BLOCK_ *block = PdhiBlock(snapshot);
    if (position >= block->NumObjectTypes)
        return nullptr;
    const BYTE *at = snapshot->Data + block->HeaderLength;
    for (DWORD i = 0; i < position; i++) {
        const PERF_OBJECT_ *object = (const PERF_OBJECT_ *)at;
        if (!object->TotalByteLength)
            return nullptr;
        at += object->TotalByteLength;
    }
    return (const PERF_OBJECT_ *)at;
}

const PERF_OBJECT_ *PdhiFindObject(const PDH_SNAPSHOT *snapshot, DWORD index)
{
    for (DWORD i = 0;; i++) {
        const PERF_OBJECT_ *object = PdhiObject(snapshot, i);
        if (!object)
            return nullptr;
        if (object->ObjectNameTitleIndex == index)
            return object;
    }
}

const PERF_COUNTER_ *PdhiCounterAt(const PERF_OBJECT_ *object, DWORD position)
{
    if (position >= object->NumCounters)
        return nullptr;
    const BYTE *at = (const BYTE *)object + object->HeaderLength;
    for (DWORD i = 0; i < position; i++)
        at += ((const PERF_COUNTER_ *)at)->ByteLength;
    return (const PERF_COUNTER_ *)at;
}

const PERF_COUNTER_ *PdhiFindCounter(const PERF_OBJECT_ *object, DWORD index)
{
    for (DWORD i = 0; i < object->NumCounters; i++) {
        const PERF_COUNTER_ *counter = PdhiCounterAt(object, i);
        if (counter->CounterNameTitleIndex == index)
            return counter;
    }
    return nullptr;
}

/* the instance at a position and the block of its counters; the block of an object without instances. */
const PERF_INSTANCE_ *PdhiInstanceAt(const PERF_OBJECT_ *object, LONG position, const BYTE **block)
{
    const BYTE *at = (const BYTE *)object + object->DefinitionLength;
    if (object->NumInstances == PERF_NO_INSTANCES_) {
        if (position)
            return nullptr;
        *block = at;
        return nullptr;
    }
    if (position < 0 || position >= object->NumInstances)
        return nullptr;
    for (LONG i = 0; i < position; i++) {
        const PERF_INSTANCE_ *instance = (const PERF_INSTANCE_ *)at;
        at += instance->ByteLength;
        at += *(const DWORD *)at;
    }
    const PERF_INSTANCE_ *instance = (const PERF_INSTANCE_ *)at;
    *block = at + instance->ByteLength;
    return instance;
}

const WCHAR *PdhiInstanceName(const PERF_INSTANCE_ *instance)
{
    return (const WCHAR *)((const BYTE *)instance + instance->NameOffset);
}

void PdhiInstanceDisplayName(const PERF_OBJECT_ *object, LONG position, WCHAR *name, DWORD *index)
{
    const BYTE *block;
    const PERF_INSTANCE_ *instance = PdhiInstanceAt(object, position, &block);
    DWORD duplicates = 0;
    lstrcpynW(name, PdhiInstanceName(instance), 128);
    for (LONG i = 0; i < position; i++) {
        const PERF_INSTANCE_ *earlier = PdhiInstanceAt(object, i, &block);
        if (!_wcsicmp(PdhiInstanceName(earlier), PdhiInstanceName(instance)))
            duplicates++;
    }
    if (index)
        *index = duplicates;
}

/* ------------------------------------------------------------------ names */

static void free_names(PDH_NAME *names, DWORD count)
{
    for (DWORD i = 0; names && i < count; i++)
        LocalFree(names[i].Text);
    LocalFree(names);
}

static BOOL parse_names(const WCHAR *list, PDH_NAME **names, DWORD *count)
{
    DWORD pairs = 0;
    for (const WCHAR *p = list; *p; p += wcslen(p) + 1, p += *p ? wcslen(p) + 1 : 0)
        pairs++;
    *names = heap_alloc((pairs ? pairs : 1) * sizeof(PDH_NAME));
    if (!*names)
        return FALSE;
    *count = 0;
    for (const WCHAR *p = list; *p;) {
        DWORD index = 0;
        for (const WCHAR *d = p; *d >= L'0' && *d <= L'9'; d++)
            index = index * 10 + (*d - L'0');
        p += wcslen(p) + 1;
        if (!*p)
            break;
        SIZE_T bytes = (wcslen(p) + 1) * sizeof(WCHAR);
        WCHAR *text = heap_alloc(bytes);
        if (!text)
            return FALSE;
        memcpy(text, p, bytes);
        (*names)[*count].Index = index;
        (*names)[*count].Text = text;
        (*count)++;
        p += wcslen(p) + 1;
    }
    return TRUE;
}

static WCHAR *read_list(PCWSTR value)
{
    DWORD size = 0, type;
    LONG error = RegQueryValueExW(HKEY_PERFORMANCE_DATA, value, nullptr, &type, nullptr, &size);
    if (error && error != ERROR_MORE_DATA)
        return nullptr;
    WCHAR *list = heap_alloc(size + 2 * sizeof(WCHAR));
    if (list && RegQueryValueExW(HKEY_PERFORMANCE_DATA, value, nullptr, &type, (BYTE *)list, &size)) {
        LocalFree(list);
        return nullptr;
    }
    return list;
}

static void load_names(void)
{
    AcquireSRWLockExclusive(&g_names_lock);
    if (!g_names_loaded) {
        WCHAR *counters = read_list(L"Counter"), *help = read_list(L"Help");
        if (counters && help && parse_names(counters, &g_counter_names, &g_counter_count) &&
            parse_names(help, &g_help_names, &g_help_count))
            g_names_loaded = TRUE;
        else {
            free_names(g_counter_names, g_counter_count);
            free_names(g_help_names, g_help_count);
            g_counter_names = g_help_names = nullptr;
            g_counter_count = g_help_count = 0;
        }
        LocalFree(counters);
        LocalFree(help);
    }
    ReleaseSRWLockExclusive(&g_names_lock);
}

static const WCHAR *lookup(const PDH_NAME *names, DWORD count, DWORD index)
{
    for (DWORD i = 0; i < count; i++)
        if (names[i].Index == index)
            return names[i].Text;
    return nullptr;
}

const WCHAR *PdhiNameOf(DWORD index)
{
    load_names();
    return lookup(g_counter_names, g_counter_count, index);
}

const WCHAR *PdhiHelpOf(DWORD index)
{
    load_names();
    return lookup(g_help_names, g_help_count, index + 1);
}

DWORD PdhiIndexOfName(const WCHAR *name)
{
    load_names();
    for (DWORD i = 0; i < g_counter_count; i++)
        if (!_wcsicmp(g_counter_names[i].Text, name))
            return g_counter_names[i].Index;
    return 0;
}

/* the counter of an object by its name, which may also be written as the number of the name. */
DWORD PdhiIndexOfCounterName(const PERF_OBJECT_ *object, const WCHAR *name)
{
    for (DWORD i = 0; i < object->NumCounters; i++) {
        const PERF_COUNTER_ *counter = PdhiCounterAt(object, i);
        const WCHAR *counter_name = PdhiNameOf(counter->CounterNameTitleIndex);
        if (counter_name && !_wcsicmp(counter_name, name))
            return counter->CounterNameTitleIndex;
    }
    return 0;
}

/* ------------------------------------------------------------------ machines */

BOOL PdhiIsLocalMachine(LPCWSTR machine)
{
    if (!machine || !*machine)
        return TRUE;
    while (*machine == L'\\')
        machine++;
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!_wcsicmp(machine, L".") || !_wcsicmp(machine, L"localhost"))
        return TRUE;
    return GetComputerNameW(name, &size) && !_wcsicmp(machine, name);
}

PDH_STATUS PdhiCheckMachine(LPCWSTR machine)
{
    if (!machine)
        return ERROR_SUCCESS;
    if (!*machine || wcslen(machine) > 0x400)
        return PDH_INVALID_ARGUMENT;
    return PdhiIsLocalMachine(machine) ? ERROR_SUCCESS : PDH_CANNOT_CONNECT_MACHINE;
}

/* ------------------------------------------------------------------ text */

PDH_STATUS PdhiReturnText(LPCWSTR text, LPWSTR buffer, DWORD *size)
{
    DWORD needed = (DWORD)wcslen(text) + 1, room = *size;
    *size = needed;
    if (!buffer || room < needed)
        return PDH_MORE_DATA;
    memcpy(buffer, text, needed * sizeof(WCHAR));
    return ERROR_SUCCESS;
}

PDH_STATUS PdhiWideOf(LPCSTR text, WCHAR *out, DWORD chars)
{
    if (!text || !MultiByteToWideChar(CP_ACP, 0, text, -1, out, (int)chars))
        return PDH_INVALID_ARGUMENT;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ wildcards */

BOOL PdhiHasWildcard(LPCWSTR text)
{
    return text && (wcschr(text, L'*') || wcschr(text, L'?'));
}

BOOL PdhiWildcardMatch(LPCWSTR pattern, LPCWSTR text)
{
    for (;; pattern++, text++) {
        if (*pattern == L'*') {
            while (pattern[1] == L'*')
                pattern++;
            for (LPCWSTR t = text;; t++) {
                if (PdhiWildcardMatch(pattern + 1, t))
                    return TRUE;
                if (!*t)
                    return FALSE;
            }
        }
        if (!*pattern || !*text)
            return !*pattern && !*text;
        if (*pattern != L'?' && towlower(*pattern) != towlower(*text))
            return FALSE;
    }
}

/* ------------------------------------------------------------------ counter paths */

static BOOL decimal_suffix(WCHAR *text, DWORD *number)
{
    SIZE_T length = wcslen(text), digits = 0;
    while (digits < length && text[length - 1 - digits] >= L'0' && text[length - 1 - digits] <= L'9')
        digits++;
    if (!digits || digits == length || text[length - 1 - digits] != L'#' || digits > 9)
        return FALSE;
    DWORD value = 0;
    for (SIZE_T i = length - digits; i < length; i++)
        value = value * 10 + (DWORD)(text[i] - L'0');
    text[length - 1 - digits] = 0;
    *number = value;
    return TRUE;
}

PDH_STATUS PdhiParsePath(LPCWSTR path, BOOL allow_wildcards, PDH_PATH **parsed)
{
    (void)allow_wildcards;
    SIZE_T length = wcslen(path);
    PDH_PATH *p = heap_alloc(sizeof(PDH_PATH) + (length + 8) * sizeof(WCHAR));
    if (!p)
        return PDH_MEMORY_ALLOCATION_FAILURE;
    WCHAR *copy = p->Storage;
    memcpy(copy, path, (length + 1) * sizeof(WCHAR));
    p->Index = PDH_INDEX_NONE;
    WCHAR *at = copy;
    if (at[0] != L'\\') {
        LocalFree(p);
        return PDH_INVALID_PATH;
    }
    if (at[1] == L'\\') {
        WCHAR *end = wcschr(at + 2, L'\\');
        if (!end || end == at + 2) {
            LocalFree(p);
            return PDH_INVALID_PATH;
        }
        *end = 0;
        p->Machine = at;
        at = end + 1;
    } else {
        at++;
    }
    /* the object ends at a parenthesis that starts an instance or at the backslash before the counter. */
    WCHAR *object = at, *stop = object;
    while (*stop && *stop != L'(' && *stop != L'\\')
        stop++;
    if (stop == object || !*stop) {
        LocalFree(p);
        return PDH_INVALID_PATH;
    }
    p->Object = object;
    if (*stop == L'(') {
        WCHAR *close = nullptr;
        for (WCHAR *s = stop + 1; *s; s++)
            if (s[0] == L')' && s[1] == L'\\')
                close = s;
        if (!close) {
            LocalFree(p);
            return PDH_INVALID_PATH;
        }
        *stop = 0;
        WCHAR *instance = stop + 1;
        *close = 0;
        at = close + 1;
        if (!*instance) {
            LocalFree(p);
            return PDH_INVALID_PATH;
        }
        WCHAR *slash = wcschr(instance, L'/');
        if (slash && slash != instance && slash[1]) {
            *slash = 0;
            p->Parent = instance;
            instance = slash + 1;
        }
        DWORD number;
        if (decimal_suffix(instance, &number))
            p->Index = number;
        p->Instance = instance;
    } else {
        if (!stop[1]) {
            LocalFree(p);
            return PDH_INVALID_PATH;
        }
        *stop = 0;
        p->Counter = stop + 1;
        *parsed = p;
        return ERROR_SUCCESS;
    }
    if (*at != L'\\' || !at[1]) {
        LocalFree(p);
        return PDH_INVALID_PATH;
    }
    p->Counter = at + 1;
    *parsed = p;
    return ERROR_SUCCESS;
}

PDH_STATUS PdhiMakePath(const PDH_PATH_ELEMENTS_W *e, WCHAR *buffer, DWORD *size)
{
    if ((e->szMachineName && !*e->szMachineName) || !e->szObjectName || !*e->szObjectName ||
        (e->szInstanceName && !*e->szInstanceName) || (e->szParentInstance && !*e->szParentInstance) || !e->szCounterName ||
        !*e->szCounterName)
        return PDH_INVALID_ARGUMENT;
    SIZE_T length = wcslen(e->szObjectName) + wcslen(e->szCounterName) + 32;
    if (e->szMachineName)
        length += wcslen(e->szMachineName) + 2;
    if (e->szInstanceName)
        length += wcslen(e->szInstanceName) + 16;
    if (e->szParentInstance)
        length += wcslen(e->szParentInstance) + 1;
    WCHAR *text = heap_alloc(length * sizeof(WCHAR));
    if (!text)
        return PDH_MEMORY_ALLOCATION_FAILURE;
    WCHAR *out = text;
    if (e->szMachineName) {
        if (e->szMachineName[0] != L'\\' || e->szMachineName[1] != L'\\') {
            *out++ = L'\\';
            *out++ = L'\\';
        }
        out += wsprintfW(out, L"%s", e->szMachineName);
    }
    *out++ = L'\\';
    out += wsprintfW(out, L"%s", e->szObjectName);
    if (e->szInstanceName) {
        *out++ = L'(';
        if (e->szParentInstance)
            out += wsprintfW(out, L"%s/", e->szParentInstance);
        out += wsprintfW(out, L"%s", e->szInstanceName);
        if (e->dwInstanceIndex != PDH_INDEX_NONE && e->dwInstanceIndex != 0)
            out += wsprintfW(out, L"#%lu", e->dwInstanceIndex);
        *out++ = L')';
    }
    *out++ = L'\\';
    out += wsprintfW(out, L"%s", e->szCounterName);
    PDH_STATUS status = PdhiReturnText(text, buffer, size);
    LocalFree(text);
    return status;
}
