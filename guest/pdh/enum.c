/* pdh: what the machine has to count, and the text around counter paths: enumerations, wildcards, names and paths. */
#include "pdh_private.h"

typedef struct LIST {
    WCHAR *Text;
    DWORD Length;
    DWORD Capacity;
    BOOL Failed;
} LIST;

static void list_add(LIST *list, const WCHAR *text)
{
    DWORD chars = (DWORD)wcslen(text) + 1;
    if (list->Failed)
        return;
    if (list->Length + chars + 1 > list->Capacity) {
        DWORD capacity = list->Capacity ? list->Capacity * 2 : 256;
        while (capacity < list->Length + chars + 1)
            capacity *= 2;
        WCHAR *grown = LocalAlloc(LMEM_ZEROINIT, capacity * sizeof(WCHAR));
        if (!grown) {
            list->Failed = TRUE;
            return;
        }
        if (list->Text) {
            memcpy(grown, list->Text, list->Length * sizeof(WCHAR));
            LocalFree(list->Text);
        }
        list->Text = grown;
        list->Capacity = capacity;
    }
    memcpy(list->Text + list->Length, text, chars * sizeof(WCHAR));
    list->Length += chars;
    list->Text[list->Length] = 0;
}

/* the characters of the list with its closing terminator, none for an empty one. */
static DWORD list_chars(const LIST *list)
{
    return list->Length ? list->Length + 1 : 0;
}

static PDH_STATUS list_return(const LIST *list, BOOL ansi, void *buffer, DWORD *size)
{
    DWORD chars = list_chars(list), room = *size;
    DWORD needed = chars;
    if (ansi && chars)
        needed = (DWORD)WideCharToMultiByte(CP_ACP, 0, list->Text, (int)chars, nullptr, 0, nullptr, nullptr);
    *size = needed;
    if (room < needed || (needed && !buffer))
        return PDH_MORE_DATA;
    if (!needed)
        return ERROR_SUCCESS;
    if (ansi)
        WideCharToMultiByte(CP_ACP, 0, list->Text, (int)chars, buffer, (int)room, nullptr, nullptr);
    else
        memcpy(buffer, list->Text, chars * sizeof(WCHAR));
    return ERROR_SUCCESS;
}

static BOOL detail_valid(DWORD level)
{
    return level == 100 || level == 200 || level == 300 || level == 400;
}

static BOOL counter_hidden(const PERF_COUNTER_ *counter)
{
    return (counter->CounterType & PERF_SUBTYPE_MASK_) == PERF_COUNTER_BASE_ && (counter->CounterType & 0xC00) == 0x400;
}

/* ------------------------------------------------------------------ objects, counters and instances */

static PDH_STATUS enum_objects(PDH_HLOG log, LPCWSTR machine, BOOL ansi, void *buffer, DWORD *size, DWORD detail)
{
    if (!size || (*size && !buffer) || !detail_valid(detail))
        return PDH_INVALID_ARGUMENT;
    if (log)
        return PDH_INVALID_HANDLE;
    PDH_STATUS status = PdhiCheckMachine(machine);
    if (status)
        return status;
    PDH_SNAPSHOT snapshot;
    status = PdhiSnapshot(&snapshot);
    if (status)
        return status;
    LIST list = {};
    for (DWORD i = 0;; i++) {
        const PERF_OBJECT_ *object = PdhiObject(&snapshot, i);
        if (!object)
            break;
        const WCHAR *name = PdhiNameOf(object->ObjectNameTitleIndex);
        if (name && object->DetailLevel <= detail)
            list_add(&list, name);
    }
    PdhiFreeSnapshot(&snapshot);
    status = list.Failed ? PDH_MEMORY_ALLOCATION_FAILURE : list_return(&list, ansi, buffer, size);
    LocalFree(list.Text);
    return status;
}

PDH_STATUS WINAPI PdhEnumObjectsHW(PDH_HLOG log, LPCWSTR machine, LPWSTR list, LPDWORD size, DWORD detail, BOOL refresh)
{
    (void)refresh;
    return enum_objects(log, machine, FALSE, list, size, detail);
}

PDH_STATUS WINAPI PdhEnumObjectsHA(PDH_HLOG log, LPCSTR machine, LPSTR list, LPDWORD size, DWORD detail, BOOL refresh)
{
    (void)refresh;
    WCHAR wide[1026];
    if (machine && PdhiWideOf(machine, wide, 1026))
        return PDH_INVALID_ARGUMENT;
    return enum_objects(log, machine ? wide : nullptr, TRUE, list, size, detail);
}

PDH_STATUS WINAPI PdhOpenLogW(LPCWSTR file, DWORD access, LPDWORD type, PDH_HQUERY query, DWORD size, LPCWSTR caption,
                              PDH_HLOG *log);

/* a data source other than the machine is a log file, and logs are not read here. */
static PDH_STATUS source_check(LPCWSTR source)
{
    if (!source)
        return ERROR_SUCCESS;
    if (!*source || wcslen(source) > 0x400)
        return PDH_INVALID_ARGUMENT;
    DWORD type = 0;
    PDH_HLOG log;
    PDH_STATUS status = PdhOpenLogW(source, 0x10004, &type, nullptr, 0, nullptr, &log);
    return status ? status : PDH_UNKNOWN_LOG_FORMAT;
}

PDH_STATUS WINAPI PdhEnumObjectsW(LPCWSTR source, LPCWSTR machine, LPWSTR list, LPDWORD size, DWORD detail, BOOL refresh)
{
    if (!size)
        return PDH_INVALID_ARGUMENT;
    if (*size && !list)
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = source_check(source);
    return status ? status : PdhEnumObjectsHW(nullptr, machine, list, size, detail, refresh);
}

PDH_STATUS WINAPI PdhEnumObjectsA(LPCSTR source, LPCSTR machine, LPSTR list, LPDWORD size, DWORD detail, BOOL refresh)
{
    if (!size)
        return PDH_INVALID_ARGUMENT;
    if (*size && !list)
        return PDH_INVALID_ARGUMENT;
    WCHAR wide[1026];
    if (source) {
        if (PdhiWideOf(source, wide, 1026))
            return PDH_INVALID_ARGUMENT;
        PDH_STATUS status = source_check(wide);
        if (status)
            return status;
    }
    return PdhEnumObjectsHA(nullptr, machine, list, size, detail, refresh);
}

static PDH_STATUS enum_items(PDH_HLOG log, LPCWSTR machine, LPCWSTR object_name, BOOL ansi, void *counters, DWORD *counter_size,
                             void *instances, DWORD *instance_size, DWORD detail)
{
    if (!counter_size || !instance_size || (*counter_size && !counters) || (*instance_size && !instances) || !object_name ||
        !*object_name || !detail_valid(detail))
        return PDH_INVALID_ARGUMENT;
    if (log)
        return PDH_INVALID_HANDLE;
    PDH_STATUS status = PdhiCheckMachine(machine);
    if (status)
        return status;
    PDH_SNAPSHOT snapshot;
    status = PdhiSnapshot(&snapshot);
    if (status)
        return status;
    DWORD index = PdhiIndexOfName(object_name);
    const PERF_OBJECT_ *object = index ? PdhiFindObject(&snapshot, index) : nullptr;
    if (!object) {
        PdhiFreeSnapshot(&snapshot);
        return PDH_CSTATUS_NO_OBJECT;
    }
    LIST names = {}, shown = {};
    for (DWORD i = 0; i < object->NumCounters; i++) {
        const PERF_COUNTER_ *counter = PdhiCounterAt(object, i);
        const WCHAR *name = PdhiNameOf(counter->CounterNameTitleIndex);
        if (name && counter->DetailLevel <= detail && !counter_hidden(counter))
            list_add(&names, name);
    }
    if (object->NumInstances != PERF_NO_INSTANCES_) {
        for (LONG i = 0; i < object->NumInstances; i++) {
            WCHAR name[140];
            DWORD duplicate;
            PdhiInstanceDisplayName(object, i, name, &duplicate);
            if (duplicate)
                wsprintfW(name + wcslen(name), L"#%lu", duplicate);
            list_add(&shown, name);
        }
    }
    PdhiFreeSnapshot(&snapshot);
    if (names.Failed || shown.Failed) {
        status = PDH_MEMORY_ALLOCATION_FAILURE;
    } else {
        /* both lists say what they need; the call is more data when either does not fit. */
        PDH_STATUS first = list_return(&names, ansi, counters, counter_size);
        PDH_STATUS second = list_return(&shown, ansi, instances, instance_size);
        status = first ? first : second;
    }
    LocalFree(names.Text);
    LocalFree(shown.Text);
    return status;
}

PDH_STATUS WINAPI PdhEnumObjectItemsHW(PDH_HLOG log, LPCWSTR machine, LPCWSTR object, LPWSTR counters,
                                       LPDWORD counter_size, LPWSTR instances, LPDWORD instance_size, DWORD detail,
                                       DWORD flags)
{
    (void)flags;
    return enum_items(log, machine, object, FALSE, counters, counter_size, instances, instance_size, detail);
}

PDH_STATUS WINAPI PdhEnumObjectItemsHA(PDH_HLOG log, LPCSTR machine, LPCSTR object, LPSTR counters, LPDWORD counter_size,
                                       LPSTR instances, LPDWORD instance_size, DWORD detail, DWORD flags)
{
    (void)flags;
    WCHAR wide_machine[1026], wide_object[1026];
    if ((machine && PdhiWideOf(machine, wide_machine, 1026)) || (object && PdhiWideOf(object, wide_object, 1026)))
        return PDH_INVALID_ARGUMENT;
    return enum_items(log, machine ? wide_machine : nullptr, object ? wide_object : nullptr, TRUE, counters, counter_size,
                      instances, instance_size, detail);
}

PDH_STATUS WINAPI PdhEnumObjectItemsW(LPCWSTR source, LPCWSTR machine, LPCWSTR object, LPWSTR counters,
                                      LPDWORD counter_size, LPWSTR instances, LPDWORD instance_size, DWORD detail,
                                      DWORD flags)
{
    if (!counter_size || !instance_size || (*counter_size && !counters) || (*instance_size && !instances))
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = source_check(source);
    return status ? status : PdhEnumObjectItemsHW(nullptr, machine, object, counters, counter_size, instances,
                                                   instance_size, detail, flags);
}

PDH_STATUS WINAPI PdhEnumObjectItemsA(LPCSTR source, LPCSTR machine, LPCSTR object, LPSTR counters, LPDWORD counter_size,
                                      LPSTR instances, LPDWORD instance_size, DWORD detail, DWORD flags)
{
    if (!counter_size || !instance_size || (*counter_size && !counters) || (*instance_size && !instances))
        return PDH_INVALID_ARGUMENT;
    WCHAR wide[1026];
    if (source) {
        if (PdhiWideOf(source, wide, 1026))
            return PDH_INVALID_ARGUMENT;
        PDH_STATUS status = source_check(wide);
        if (status)
            return status;
    }
    return PdhEnumObjectItemsHA(nullptr, machine, object, counters, counter_size, instances, instance_size, detail, flags);
}

/* ------------------------------------------------------------------ machines */

static PDH_STATUS machines(PDH_HLOG log, BOOL ansi, void *buffer, DWORD *size)
{
    if (!size || (*size && !buffer))
        return PDH_INVALID_ARGUMENT;
    if (log)
        return PDH_INVALID_HANDLE;
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD chars = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(name, &chars))
        return PDH_CANNOT_CONNECT_MACHINE;
    LIST list = {};
    list_add(&list, name);
    PDH_STATUS status = list.Failed ? PDH_MEMORY_ALLOCATION_FAILURE : list_return(&list, ansi, buffer, size);
    LocalFree(list.Text);
    return status;
}

PDH_STATUS WINAPI PdhEnumMachinesHW(PDH_HLOG log, LPWSTR list, LPDWORD size)
{
    return machines(log, FALSE, list, size);
}

PDH_STATUS WINAPI PdhEnumMachinesHA(PDH_HLOG log, LPSTR list, LPDWORD size)
{
    return machines(log, TRUE, list, size);
}

PDH_STATUS WINAPI PdhEnumMachinesW(LPCWSTR source, LPWSTR list, LPDWORD size)
{
    if (!size || (*size && !list))
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = source_check(source);
    return status ? status : machines(nullptr, FALSE, list, size);
}

PDH_STATUS WINAPI PdhEnumMachinesA(LPCSTR source, LPSTR list, LPDWORD size)
{
    if (!size || (*size && !list))
        return PDH_INVALID_ARGUMENT;
    WCHAR wide[1026];
    if (source) {
        if (PdhiWideOf(source, wide, 1026))
            return PDH_INVALID_ARGUMENT;
        PDH_STATUS status = source_check(wide);
        if (status)
            return status;
    }
    return machines(nullptr, TRUE, list, size);
}

PDH_STATUS WINAPI PdhConnectMachineW(LPCWSTR machine)
{
    if (!machine || !*machine || wcslen(machine) > 0x400)
        return PDH_INVALID_ARGUMENT;
    return PdhiIsLocalMachine(machine) ? ERROR_SUCCESS : PDH_CANNOT_CONNECT_MACHINE;
}

PDH_STATUS WINAPI PdhConnectMachineA(LPCSTR machine)
{
    WCHAR wide[1026];
    if (!machine || PdhiWideOf(machine, wide, 1026))
        return PDH_INVALID_ARGUMENT;
    return PdhConnectMachineW(wide);
}

/* ------------------------------------------------------------------ defaults */

static PDH_STATUS return_name(LPCWSTR text, BOOL ansi, void *buffer, DWORD *size)
{
    if (!ansi)
        return PdhiReturnText(text, buffer, size);
    DWORD needed = (DWORD)WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr), room = *size;
    *size = needed;
    if (!buffer || room < needed)
        return PDH_MORE_DATA;
    WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, (int)room, nullptr, nullptr);
    return ERROR_SUCCESS;
}

static PDH_STATUS default_object(PDH_HLOG log, LPCWSTR machine, BOOL ansi, void *buffer, DWORD *size)
{
    if (!size || (*size && !buffer))
        return PDH_INVALID_ARGUMENT;
    if (buffer && *size)
        *(BYTE *)buffer = 0;
    if (log)
        return PDH_INVALID_HANDLE;
    PDH_STATUS status = PdhiCheckMachine(machine);
    if (status)
        return status;
    PDH_SNAPSHOT snapshot;
    status = PdhiSnapshot(&snapshot);
    if (status)
        return status;
    const PERF_OBJECT_ *object = PdhiBlock(&snapshot)->DefaultObject > 0
                                     ? PdhiFindObject(&snapshot, (DWORD)PdhiBlock(&snapshot)->DefaultObject)
                                     : nullptr;
    const WCHAR *name = object ? PdhiNameOf(object->ObjectNameTitleIndex) : nullptr;
    status = name ? return_name(name, ansi, buffer, size) : PDH_CSTATUS_NO_OBJECT;
    PdhiFreeSnapshot(&snapshot);
    return status;
}

PDH_STATUS WINAPI PdhGetDefaultPerfObjectHW(PDH_HLOG log, LPCWSTR machine, LPWSTR name, LPDWORD size)
{
    return default_object(log, machine, FALSE, name, size);
}

PDH_STATUS WINAPI PdhGetDefaultPerfObjectHA(PDH_HLOG log, LPCSTR machine, LPSTR name, LPDWORD size)
{
    WCHAR wide[1026];
    if (machine && PdhiWideOf(machine, wide, 1026))
        return PDH_INVALID_ARGUMENT;
    return default_object(log, machine ? wide : nullptr, TRUE, name, size);
}

PDH_STATUS WINAPI PdhGetDefaultPerfObjectW(LPCWSTR source, LPCWSTR machine, LPWSTR name, LPDWORD size)
{
    if (!size || (*size && !name))
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = source_check(source);
    return status ? status : PdhGetDefaultPerfObjectHW(nullptr, machine, name, size);
}

PDH_STATUS WINAPI PdhGetDefaultPerfObjectA(LPCSTR source, LPCSTR machine, LPSTR name, LPDWORD size)
{
    if (!size || (*size && !name))
        return PDH_INVALID_ARGUMENT;
    WCHAR wide[1026];
    if (source) {
        if (PdhiWideOf(source, wide, 1026))
            return PDH_INVALID_ARGUMENT;
        PDH_STATUS status = source_check(wide);
        if (status)
            return status;
    }
    return PdhGetDefaultPerfObjectHA(nullptr, machine, name, size);
}

static PDH_STATUS default_counter(PDH_HLOG log, LPCWSTR machine, LPCWSTR object_name, BOOL ansi, void *buffer, DWORD *size)
{
    if (!size || (*size && !buffer) || !object_name || !*object_name)
        return PDH_INVALID_ARGUMENT;
    if (buffer && *size)
        *(BYTE *)buffer = 0;
    if (log)
        return PDH_INVALID_HANDLE;
    PDH_STATUS status = PdhiCheckMachine(machine);
    if (status)
        return status;
    PDH_SNAPSHOT snapshot;
    status = PdhiSnapshot(&snapshot);
    if (status)
        return status;
    DWORD index = PdhiIndexOfName(object_name);
    const PERF_OBJECT_ *object = index ? PdhiFindObject(&snapshot, index) : nullptr;
    if (!object) {
        PdhiFreeSnapshot(&snapshot);
        return PDH_CSTATUS_NO_OBJECT;
    }
    LONG position = object->DefaultCounter >= 0 && (DWORD)object->DefaultCounter < object->NumCounters ? object->DefaultCounter : 0;
    const PERF_COUNTER_ *counter = PdhiCounterAt(object, (DWORD)position);
    const WCHAR *name = counter ? PdhiNameOf(counter->CounterNameTitleIndex) : nullptr;
    status = name ? return_name(name, ansi, buffer, size) : PDH_CSTATUS_NO_COUNTER;
    PdhiFreeSnapshot(&snapshot);
    return status;
}

PDH_STATUS WINAPI PdhGetDefaultPerfCounterHW(PDH_HLOG log, LPCWSTR machine, LPCWSTR object, LPWSTR name, LPDWORD size)
{
    return default_counter(log, machine, object, FALSE, name, size);
}

PDH_STATUS WINAPI PdhGetDefaultPerfCounterHA(PDH_HLOG log, LPCSTR machine, LPCSTR object, LPSTR name, LPDWORD size)
{
    WCHAR wide_machine[1026], wide_object[1026];
    if ((machine && PdhiWideOf(machine, wide_machine, 1026)) || (object && PdhiWideOf(object, wide_object, 1026)))
        return PDH_INVALID_ARGUMENT;
    return default_counter(log, machine ? wide_machine : nullptr, object ? wide_object : nullptr, TRUE, name, size);
}

PDH_STATUS WINAPI PdhGetDefaultPerfCounterW(LPCWSTR source, LPCWSTR machine, LPCWSTR object, LPWSTR name, LPDWORD size)
{
    if (!size || (*size && !name))
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = source_check(source);
    return status ? status : PdhGetDefaultPerfCounterHW(nullptr, machine, object, name, size);
}

PDH_STATUS WINAPI PdhGetDefaultPerfCounterA(LPCSTR source, LPCSTR machine, LPCSTR object, LPSTR name, LPDWORD size)
{
    if (!size || (*size && !name))
        return PDH_INVALID_ARGUMENT;
    WCHAR wide[1026];
    if (source) {
        if (PdhiWideOf(source, wide, 1026))
            return PDH_INVALID_ARGUMENT;
        PDH_STATUS status = source_check(wide);
        if (status)
            return status;
    }
    return PdhGetDefaultPerfCounterHA(nullptr, machine, object, name, size);
}

/* ------------------------------------------------------------------ wildcard paths */

/* the paths a wildcard path stands for, instance by instance and counter by counter. */
static PDH_STATUS expand(LPCWSTR path, DWORD flags, LIST *out)
{
    PDH_PATH *parsed = nullptr;
    PDH_STATUS status = PdhiParsePath(path, TRUE, &parsed);
    if (status)
        return status;
    if (!PdhiIsLocalMachine(parsed->Machine)) {
        LocalFree(parsed);
        return PDH_CSTATUS_NO_MACHINE;
    }
    PDH_SNAPSHOT snapshot;
    status = PdhiSnapshot(&snapshot);
    if (status) {
        LocalFree(parsed);
        return status;
    }
    BOOL matched_object = FALSE;
    status = PDH_CSTATUS_NO_OBJECT;
    for (DWORD o = 0;; o++) {
        const PERF_OBJECT_ *object = PdhiObject(&snapshot, o);
        if (!object)
            break;
        const WCHAR *object_name = PdhiNameOf(object->ObjectNameTitleIndex);
        if (!object_name || !PdhiWildcardMatch(parsed->Object, object_name))
            continue;
        matched_object = TRUE;
        if (status == PDH_CSTATUS_NO_OBJECT)
            status = PDH_CSTATUS_NO_COUNTER;
        BOOL has_instances = object->NumInstances != PERF_NO_INSTANCES_;
        if (has_instances != (parsed->Instance != nullptr)) {
            if (status == PDH_CSTATUS_NO_COUNTER)
                status = PDH_CSTATUS_NO_INSTANCE;
            continue;
        }
        LONG instances = has_instances ? object->NumInstances : 1;
        for (LONG i = 0; i < instances; i++) {
            WCHAR instance[140] = L"";
            if (has_instances) {
                DWORD duplicate;
                PdhiInstanceDisplayName(object, i, instance, &duplicate);
                if (duplicate)
                    wsprintfW(instance + wcslen(instance), L"#%lu", duplicate);
                if (!PdhiWildcardMatch(parsed->Instance, instance) &&
                    !(parsed->Index != PDH_INDEX_NONE && FALSE))
                    continue;
            }
            for (DWORD c = 0; c < object->NumCounters; c++) {
                const PERF_COUNTER_ *counter = PdhiCounterAt(object, c);
                const WCHAR *counter_name = PdhiNameOf(counter->CounterNameTitleIndex);
                if (!counter_name || counter_hidden(counter) || !PdhiWildcardMatch(parsed->Counter, counter_name))
                    continue;
                WCHAR line[2300];
                WCHAR *at = line;
                if (parsed->Machine)
                    at += wsprintfW(at, L"\\\\%s", parsed->Machine + (parsed->Machine[0] == L'\\' ? 0 : 0));
                at += wsprintfW(at, L"\\%s", object_name);
                if (has_instances) {
                    if (parsed->Parent)
                        at += wsprintfW(at, L"(%s/%s)", parsed->Parent, instance);
                    else
                        at += wsprintfW(at, L"(%s)", instance);
                }
                wsprintfW(at, L"\\%s", counter_name);
                list_add(out, line);
                status = ERROR_SUCCESS;
            }
        }
    }
    (void)matched_object;
    (void)flags;
    PdhiFreeSnapshot(&snapshot);
    LocalFree(parsed);
    return out->Failed ? PDH_MEMORY_ALLOCATION_FAILURE : status;
}

static PDH_STATUS expand_to(LPCWSTR path, DWORD flags, BOOL ansi, void *buffer, DWORD *size)
{
    LIST list = {};
    PDH_STATUS status = expand(path, flags, &list);
    if (!status)
        status = list_return(&list, ansi, buffer, size);
    LocalFree(list.Text);
    return status;
}

PDH_STATUS WINAPI PdhExpandWildCardPathHW(PDH_HLOG log, LPCWSTR path, LPWSTR list, LPDWORD size, DWORD flags)
{
    if (!path || !*path || !size || (*size && !list) || wcslen(path) > 0x800)
        return PDH_INVALID_ARGUMENT;
    if (log)
        return PDH_INVALID_HANDLE;
    return expand_to(path, flags, FALSE, list, size);
}

PDH_STATUS WINAPI PdhExpandWildCardPathHA(PDH_HLOG log, LPCSTR path, LPSTR list, LPDWORD size, DWORD flags)
{
    WCHAR wide[0x801];
    if (!path || !size || (*size && !list) || PdhiWideOf(path, wide, 0x801))
        return PDH_INVALID_ARGUMENT;
    if (log)
        return PDH_INVALID_HANDLE;
    return expand_to(wide, flags, TRUE, list, size);
}

PDH_STATUS WINAPI PdhExpandWildCardPathW(LPCWSTR source, LPCWSTR path, LPWSTR list, LPDWORD size, DWORD flags)
{
    if (!path || !*path || !size || (*size && !list) || wcslen(path) > 0x800)
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = source_check(source);
    return status ? status : expand_to(path, flags, FALSE, list, size);
}

PDH_STATUS WINAPI PdhExpandWildCardPathA(LPCSTR source, LPCSTR path, LPSTR list, LPDWORD size, DWORD flags)
{
    WCHAR wide[0x801], wide_source[1026];
    if (!path || !size || (*size && !list) || PdhiWideOf(path, wide, 0x801))
        return PDH_INVALID_ARGUMENT;
    if (source) {
        if (PdhiWideOf(source, wide_source, 1026))
            return PDH_INVALID_ARGUMENT;
        PDH_STATUS status = source_check(wide_source);
        if (status)
            return status;
    }
    return expand_to(wide, flags, TRUE, list, size);
}

PDH_STATUS WINAPI PdhExpandCounterPathW(LPCWSTR path, LPWSTR list, LPDWORD size)
{
    return PdhExpandWildCardPathW(nullptr, path, list, size, 0);
}

PDH_STATUS WINAPI PdhExpandCounterPathA(LPCSTR path, LPSTR list, LPDWORD size)
{
    return PdhExpandWildCardPathA(nullptr, path, list, size, 0);
}

/* ------------------------------------------------------------------ names and indexes */

PDH_STATUS WINAPI PdhLookupPerfNameByIndexW(LPCWSTR machine, DWORD index, LPWSTR buffer, LPDWORD size)
{
    if (!size || (machine && (!*machine || wcslen(machine) > 0x400)))
        return PDH_INVALID_ARGUMENT;
    if (buffer && *size)
        *buffer = 0;
    PDH_STATUS status = PdhiCheckMachine(machine);
    if (status)
        return status;
    const WCHAR *name = PdhiNameOf(index);
    if (!name)
        return PDH_INVALID_ARGUMENT;
    return PdhiReturnText(name, buffer, size);
}

PDH_STATUS WINAPI PdhLookupPerfNameByIndexA(LPCSTR machine, DWORD index, LPSTR buffer, LPDWORD size)
{
    WCHAR wide[1026];
    if (!size || (machine && PdhiWideOf(machine, wide, 1026)))
        return PDH_INVALID_ARGUMENT;
    if (buffer && *size)
        *buffer = 0;
    PDH_STATUS status = PdhiCheckMachine(machine ? wide : nullptr);
    if (status)
        return status;
    const WCHAR *name = PdhiNameOf(index);
    if (!name)
        return PDH_INVALID_ARGUMENT;
    return return_name(name, TRUE, buffer, size);
}

PDH_STATUS WINAPI PdhLookupPerfIndexByNameW(LPCWSTR machine, LPCWSTR name, LPDWORD index)
{
    if (!name || !index)
        return PDH_INVALID_ARGUMENT;
    *index = 0;
    if ((machine && (!*machine || wcslen(machine) > 0x400)) || !*name || wcslen(name) > 0x400)
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = PdhiCheckMachine(machine);
    if (status)
        return status;
    DWORD found = PdhiIndexOfName(name);
    if (!found)
        return PDH_STRING_NOT_FOUND;
    *index = found;
    return ERROR_SUCCESS;
}

PDH_STATUS WINAPI PdhLookupPerfIndexByNameA(LPCSTR machine, LPCSTR name, LPDWORD index)
{
    WCHAR wide_machine[1026], wide_name[1026];
    if (!name || !index || (machine && PdhiWideOf(machine, wide_machine, 1026)) || PdhiWideOf(name, wide_name, 1026))
        return PDH_INVALID_ARGUMENT;
    return PdhLookupPerfIndexByNameW(machine ? wide_machine : nullptr, wide_name, index);
}

/* the help text of an object, or of a counter of it; a pointer into the names of the machine. */
const WCHAR *WINAPI PdhGetExplainText(LPCWSTR machine, LPCWSTR object_name, LPCWSTR counter_name)
{
    if (PdhiCheckMachine(machine) || !object_name)
        return nullptr;
    DWORD index = PdhiIndexOfName(object_name);
    if (!index)
        return nullptr;
    if (!counter_name)
        return PdhiHelpOf(index);
    PDH_SNAPSHOT snapshot;
    if (PdhiSnapshot(&snapshot))
        return nullptr;
    const PERF_OBJECT_ *object = PdhiFindObject(&snapshot, index);
    DWORD counter = object ? PdhiIndexOfCounterName(object, counter_name) : 0;
    PdhiFreeSnapshot(&snapshot);
    return counter ? PdhiHelpOf(counter) : nullptr;
}

/* the counter names here are the english ones, so a path is its own translation once its names are those of the data. */
static PDH_STATUS translate(LPCWSTR path, LPWSTR buffer, LPDWORD size)
{
    if (!size || (*size && !buffer) || !path || !*path || wcslen(path) > 0x800)
        return PDH_INVALID_ARGUMENT;
    if (buffer && *size)
        *buffer = 0;
    PDH_PATH *parsed = nullptr;
    PDH_STATUS status = PdhiParsePath(path, TRUE, &parsed);
    if (status)
        return status;
    DWORD object_index = PdhiIndexOfName(parsed->Object);
    PDH_SNAPSHOT snapshot;
    const PERF_OBJECT_ *object = nullptr;
    if (object_index && !PdhiSnapshot(&snapshot)) {
        object = PdhiFindObject(&snapshot, object_index);
        DWORD counter_index = object ? PdhiIndexOfCounterName(object, parsed->Counter) : 0;
        if (object && counter_index) {
            PDH_PATH_ELEMENTS_W elements = {parsed->Machine, (WCHAR *)PdhiNameOf(object_index), parsed->Instance,
                                            parsed->Parent, parsed->Index, (WCHAR *)PdhiNameOf(counter_index)};
            status = PdhiMakePath(&elements, buffer, size);
        } else {
            status = object ? PDH_CSTATUS_NO_COUNTER : PDH_CSTATUS_NO_OBJECT;
        }
        PdhiFreeSnapshot(&snapshot);
    } else {
        status = PDH_CSTATUS_NO_OBJECT;
    }
    LocalFree(parsed);
    return status;
}

PDH_STATUS WINAPI PdhTranslate009CounterW(LPCWSTR path, LPWSTR buffer, LPDWORD size)
{
    return translate(path, buffer, size);
}

PDH_STATUS WINAPI PdhTranslateLocaleCounterW(LPCWSTR path, LPWSTR buffer, LPDWORD size)
{
    return translate(path, buffer, size);
}

/* ------------------------------------------------------------------ counter paths as text */

static PDH_STATUS parse_to(LPCWSTR path, BOOL ansi, void *buffer, DWORD *size, DWORD flags)
{
    if (!size)
        return PDH_INVALID_ARGUMENT;
    DWORD room = *size;
    SIZE_T head = ansi ? sizeof(PDH_PATH_ELEMENTS_A) : sizeof(PDH_PATH_ELEMENTS_W);
    PDH_STATUS status = ERROR_SUCCESS;
    if (buffer) {
        if (room >= head)
            memset(buffer, 0, head);
        else
            status = PDH_INVALID_ARGUMENT;
    } else if (room) {
        status = PDH_INVALID_ARGUMENT;
    }
    if (!path || !*path || wcslen(path) > 0x800)
        return PDH_INVALID_ARGUMENT;
    if (status)
        return status;
    PDH_PATH *parsed = nullptr;
    status = PdhiParsePath(path, TRUE, &parsed);
    if (status)
        return PDH_INVALID_PATH;
    const WCHAR *texts[5] = {parsed->Machine, parsed->Object, parsed->Instance, parsed->Parent, parsed->Counter};
    SIZE_T needed = head;
    for (int i = 0; i < 5; i++)
        if (texts[i])
            needed += ansi ? (SIZE_T)WideCharToMultiByte(CP_ACP, 0, texts[i], -1, nullptr, 0, nullptr, nullptr)
                           : (wcslen(texts[i]) + 1) * sizeof(WCHAR);
    *size = (DWORD)needed;
    if (!buffer || room < needed) {
        LocalFree(parsed);
        return PDH_MORE_DATA;
    }
    BYTE *at = (BYTE *)buffer + head;
    void *slots[5] = {};
    for (int i = 0; i < 5; i++) {
        if (!texts[i])
            continue;
        slots[i] = at;
        if (ansi)
            at += WideCharToMultiByte(CP_ACP, 0, texts[i], -1, (char *)at, 4096, nullptr, nullptr);
        else {
            SIZE_T bytes = (wcslen(texts[i]) + 1) * sizeof(WCHAR);
            memcpy(at, texts[i], bytes);
            at += bytes;
        }
    }
    if (ansi) {
        PDH_PATH_ELEMENTS_A *e = buffer;
        e->szMachineName = slots[0];
        e->szObjectName = slots[1];
        e->szInstanceName = slots[2];
        e->szParentInstance = slots[3];
        e->dwInstanceIndex = parsed->Index;
        e->szCounterName = slots[4];
    } else {
        PDH_PATH_ELEMENTS_W *e = buffer;
        e->szMachineName = slots[0];
        e->szObjectName = slots[1];
        e->szInstanceName = slots[2];
        e->szParentInstance = slots[3];
        e->dwInstanceIndex = parsed->Index;
        e->szCounterName = slots[4];
    }
    (void)flags;
    LocalFree(parsed);
    return ERROR_SUCCESS;
}

PDH_STATUS WINAPI PdhParseCounterPathW(LPCWSTR path, PDH_PATH_ELEMENTS_W *elements, LPDWORD size, DWORD flags)
{
    return parse_to(path, FALSE, elements, size, flags);
}

PDH_STATUS WINAPI PdhParseCounterPathA(LPCSTR path, PDH_PATH_ELEMENTS_A *elements, LPDWORD size, DWORD flags)
{
    WCHAR wide[0x801];
    if (!path || PdhiWideOf(path, wide, 0x801))
        return PDH_INVALID_ARGUMENT;
    return parse_to(wide, TRUE, elements, size, flags);
}

PDH_STATUS WINAPI PdhMakeCounterPathW(PDH_PATH_ELEMENTS_W *elements, LPWSTR buffer, LPDWORD size, DWORD flags)
{
    (void)flags;
    if (!size || (*size && !buffer))
        return PDH_INVALID_ARGUMENT;
    if (buffer && *size)
        *buffer = 0;
    if (!elements)
        return PDH_INVALID_ARGUMENT;
    return PdhiMakePath(elements, buffer, size);
}

PDH_STATUS WINAPI PdhMakeCounterPathA(PDH_PATH_ELEMENTS_A *elements, LPSTR buffer, LPDWORD size, DWORD flags)
{
    (void)flags;
    if (!size || (*size && !buffer) || !elements)
        return PDH_INVALID_ARGUMENT;
    WCHAR machine[1026], object[1026], instance[1026], parent[1026], counter[1026], path[2200];
    if ((elements->szMachineName && PdhiWideOf(elements->szMachineName, machine, 1026)) ||
        (elements->szObjectName && PdhiWideOf(elements->szObjectName, object, 1026)) ||
        (elements->szInstanceName && PdhiWideOf(elements->szInstanceName, instance, 1026)) ||
        (elements->szParentInstance && PdhiWideOf(elements->szParentInstance, parent, 1026)) ||
        (elements->szCounterName && PdhiWideOf(elements->szCounterName, counter, 1026)))
        return PDH_INVALID_ARGUMENT;
    PDH_PATH_ELEMENTS_W wide = {elements->szMachineName ? machine : nullptr, elements->szObjectName ? object : nullptr,
                                elements->szInstanceName ? instance : nullptr, elements->szParentInstance ? parent : nullptr,
                                elements->dwInstanceIndex, elements->szCounterName ? counter : nullptr};
    DWORD chars = 2200;
    PDH_STATUS status = PdhiMakePath(&wide, path, &chars);
    if (status)
        return status;
    if (buffer && *size)
        *buffer = 0;
    return return_name(path, TRUE, buffer, size);
}

/* "parent/name#index": the pieces of an instance as a path writes it. */
static PDH_STATUS parse_instance(LPCWSTR text, WCHAR *name, DWORD *name_size, WCHAR *parent, DWORD *parent_size, DWORD *index)
{
    DWORD room_name = name_size ? *name_size : 0, room_parent = parent_size ? *parent_size : 0;
    PDH_STATUS status = ERROR_SUCCESS;
    if (!name_size || (room_name && !name))
        status = PDH_INVALID_ARGUMENT;
    else if (name && room_name)
        *name = 0;
    if (parent_size) {
        if (room_parent && !parent)
            status = PDH_INVALID_ARGUMENT;
        else if (parent && room_parent)
            *parent = 0;
    } else if (!status) {
        status = PDH_INVALID_ARGUMENT;
    }
    if (index)
        *index = 0;
    if (status)
        return status;
    if (!text || !*text || wcslen(text) > 0x400)
        return PDH_INVALID_ARGUMENT;
    WCHAR copy[0x402];
    lstrcpynW(copy, text, 0x402);
    WCHAR *own = copy, *parent_text = L"";
    DWORD number = 0;
    WCHAR *slash = wcschr(copy, L'/');
    if (slash) {
        if (slash == copy || !slash[1])
            return PDH_INVALID_INSTANCE;
        *slash = 0;
        parent_text = copy;
        own = slash + 1;
    }
    SIZE_T length = wcslen(own), digits = 0;
    while (digits < length && own[length - 1 - digits] >= L'0' && own[length - 1 - digits] <= L'9')
        digits++;
    if (digits && digits < length && own[length - 1 - digits] == L'#') {
        for (SIZE_T i = length - digits; i < length; i++)
            number = number * 10 + (DWORD)(own[i] - L'0');
        own[length - 1 - digits] = 0;
    }
    DWORD name_chars = (DWORD)wcslen(own) + 1, parent_chars = (DWORD)wcslen(parent_text) + 1;
    if (name && name_chars <= room_name)
        memcpy(name, own, name_chars * sizeof(WCHAR));
    else
        status = PDH_MORE_DATA;
    if (parent && parent_chars <= room_parent)
        memcpy(parent, parent_text, parent_chars * sizeof(WCHAR));
    else
        status = PDH_MORE_DATA;
    *name_size = name_chars;
    *parent_size = parent_chars;
    if (index)
        *index = number;
    return status;
}

PDH_STATUS WINAPI PdhParseInstanceNameW(LPCWSTR text, LPWSTR name, LPDWORD name_size, LPWSTR parent, LPDWORD parent_size,
                                        LPDWORD index)
{
    return parse_instance(text, name, name_size, parent, parent_size, index);
}

PDH_STATUS WINAPI PdhParseInstanceNameA(LPCSTR text, LPSTR name, LPDWORD name_size, LPSTR parent, LPDWORD parent_size,
                                        LPDWORD index)
{
    WCHAR wide[0x402], own[0x402], above[0x402];
    if (!name_size || !parent_size || !text || PdhiWideOf(text, wide, 0x402))
        return PDH_INVALID_ARGUMENT;
    DWORD room_name = *name_size, room_parent = *parent_size;
    if ((room_name && !name) || (room_parent && !parent))
        return PDH_INVALID_ARGUMENT;
    DWORD own_size = 0x402, above_size = 0x402;
    PDH_STATUS status = parse_instance(wide, own, &own_size, above, &above_size, index);
    if (status && status != PDH_MORE_DATA)
        return status;
    DWORD needed_name = (DWORD)WideCharToMultiByte(CP_ACP, 0, own, -1, nullptr, 0, nullptr, nullptr);
    DWORD needed_parent = (DWORD)WideCharToMultiByte(CP_ACP, 0, above, -1, nullptr, 0, nullptr, nullptr);
    status = ERROR_SUCCESS;
    if (name && needed_name <= room_name)
        WideCharToMultiByte(CP_ACP, 0, own, -1, name, (int)room_name, nullptr, nullptr);
    else
        status = PDH_MORE_DATA;
    if (parent && needed_parent <= room_parent)
        WideCharToMultiByte(CP_ACP, 0, above, -1, parent, (int)room_parent, nullptr, nullptr);
    else
        status = PDH_MORE_DATA;
    *name_size = needed_name;
    *parent_size = needed_parent;
    return status;
}
