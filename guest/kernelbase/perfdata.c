/*
 * the performance data of the registry, HKEY_PERFORMANCE_DATA: what RegQueryValueEx returns for the values Global,
 * Costly, a list of object numbers, and the name and help texts Counter and Help with a language. the one object that
 * is provided is Processor, whose counters come from the per processor times of the kernel, the others (and the
 * objects of providers that are not here) are not in the data, as on a machine without those providers.
 * the layout is the one winperf.h describes, whose pointer fields are 32 bits wide.
 */
#include "kernelbase_private.h"

enum {
    OBJECT_PROCESSOR = 238,
    COUNTER_PROCESSOR_TIME = 6,
    COUNTER_USER_TIME = 142,
    COUNTER_PRIVILEGED_TIME = 144,
    TYPE_100NSEC_TIMER = 0x20510500,
    TYPE_100NSEC_TIMER_INV = 0x21510500,
};

#pragma pack(push, 8)
typedef struct {
    WCHAR Signature[4];
    DWORD LittleEndian, Version, Revision, TotalByteLength, HeaderLength, NumObjectTypes;
    LONG DefaultObject;
    SYSTEMTIME SystemTime;
    LARGE_INTEGER PerfTime, PerfFreq, PerfTime100nSec;
    DWORD SystemNameLength, SystemNameOffset;
} PERF_BLOCK;

typedef struct {
    DWORD TotalByteLength, DefinitionLength, HeaderLength, ObjectNameTitleIndex, ObjectNameTitle, ObjectHelpTitleIndex,
        ObjectHelpTitle, DetailLevel, NumCounters;
    LONG DefaultCounter, NumInstances;
    DWORD CodePage;
    LARGE_INTEGER PerfTime, PerfFreq;
} PERF_OBJECT;

typedef struct {
    DWORD ByteLength, CounterNameTitleIndex, CounterNameTitle, CounterHelpTitleIndex, CounterHelpTitle;
    LONG DefaultScale;
    DWORD DetailLevel, CounterType, CounterSize, CounterOffset;
} PERF_COUNTER;

typedef struct {
    DWORD ByteLength, ParentObjectTitleIndex, ParentObjectInstance;
    LONG UniqueID;
    DWORD NameOffset, NameLength;
} PERF_INSTANCE;
#pragma pack(pop)

typedef struct {
    LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
    ULONG InterruptCount;
} CPU_TIMES;

/* the names, in the order of the index pairs the registry value holds. */
static const struct {
    DWORD Index;
    const WCHAR *Name;
    const WCHAR *Help;
} g_names[] = {
    {2, L"System", L"The System object type includes those counters that apply to all processors on the computer collectively."},
    {6, L"% Processor Time",
     L"% Processor Time is the percentage of elapsed time that the processor spends to execute a non-Idle thread."},
    {142, L"% User Time", L"% User Time is the percentage of elapsed time the processor spends in the user mode."},
    {144, L"% Privileged Time",
     L"% Privileged Time is the percentage of elapsed time that the process threads spent executing code in privileged mode."},
    {238, L"Processor",
     L"The Processor object type includes as instances all processors on the computer. For each processor it reports the "
     L"percentage of elapsed time spent in the idle, user and privileged modes."},
};

typedef struct {
    WCHAR *Data;
    SIZE_T Length;
} TEXT;

static void put_text(TEXT *t, const WCHAR *text, SIZE_T count)
{
    WCHAR *grown = KbAlloc((t->Length + count) * sizeof(WCHAR));
    if (!grown)
        return;
    if (t->Length)
        memcpy(grown, t->Data, t->Length * sizeof(WCHAR));
    memcpy(grown + t->Length, text, count * sizeof(WCHAR));
    KbFree(t->Data);
    t->Data = grown;
    t->Length += count;
}

/* the pairs of index and text: the name of an object or counter is at its index, the help text of it at the next. */
static LSTATUS names_value(BOOL help, DWORD *type, BYTE *data, DWORD *size)
{
    TEXT t = {};
    if (help) {
        for (SIZE_T i = 0; i < ARRAYSIZE(g_names); i++) {
            WCHAR index[12];
            int n = 0;
            for (DWORD v = g_names[i].Index + 1, d = 1000000000; d; d /= 10)
                if (n || v / d || d == 1)
                    index[n++] = (WCHAR)(L'0' + v / d % 10);
            index[n++] = 0;
            put_text(&t, index, n);
            put_text(&t, g_names[i].Help, wcslen(g_names[i].Help) + 1);
        }
    } else {
        static const WCHAR first[] = L"1\0" L"1847\0";
        put_text(&t, first, ARRAYSIZE(first) - 1);
        for (SIZE_T i = 0; i < ARRAYSIZE(g_names); i++) {
            WCHAR index[12];
            int n = 0;
            for (DWORD v = g_names[i].Index, d = 1000000000; d; d /= 10)
                if (n || v / d || d == 1)
                    index[n++] = (WCHAR)(L'0' + v / d % 10);
            index[n++] = 0;
            put_text(&t, index, n);
            put_text(&t, g_names[i].Name, wcslen(g_names[i].Name) + 1);
        }
    }
    put_text(&t, L"", 1);
    if (type)
        *type = REG_MULTI_SZ;
    DWORD bytes = (DWORD)(t.Length * sizeof(WCHAR));
    LSTATUS error = ERROR_SUCCESS;
    if (!t.Data)
        error = ERROR_NOT_ENOUGH_MEMORY;
    else if (data && *size < bytes)
        error = ERROR_MORE_DATA;
    else if (data)
        memcpy(data, t.Data, bytes);
    /* a call without a buffer asks for the size, as it does of any value. */
    if (!error)
        *size = bytes;
    KbFree(t.Data);
    return error;
}

static void write_name(PERF_INSTANCE *instance, const WCHAR *name, LONG id)
{
    SIZE_T length = (wcslen(name) + 1) * sizeof(WCHAR);
    instance->ByteLength = (DWORD)((sizeof(*instance) + length + 7) & ~7ull);
    instance->UniqueID = id;
    instance->NameOffset = sizeof(*instance);
    instance->NameLength = (DWORD)length;
    memcpy((BYTE *)instance + sizeof(*instance), name, length);
}

/* the Processor object: an instance for each processor and one for all of them. */
static BYTE *processor_object(DWORD *length, ULONG64 time, const LARGE_INTEGER *time_100ns, const LARGE_INTEGER *freq)
{
    SYSTEM_BASIC_INFORMATION basic;
    CPU_TIMES times[64];
    ULONG got = 0;
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &basic, sizeof(basic), nullptr)))
        return nullptr;
    ULONG count = basic.NumberOfProcessors > 64 ? 64 : basic.NumberOfProcessors;
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemProcessorPerformanceInformation, times, count * sizeof(CPU_TIMES), &got)))
        return nullptr;
    count = got / sizeof(CPU_TIMES);
    static const DWORD counters[3][2] = {
        {COUNTER_PROCESSOR_TIME, TYPE_100NSEC_TIMER_INV},
        {COUNTER_USER_TIME, TYPE_100NSEC_TIMER},
        {COUNTER_PRIVILEGED_TIME, TYPE_100NSEC_TIMER},
    };
    SIZE_T definitions = sizeof(PERF_OBJECT) + sizeof(counters) / sizeof(counters[0]) * sizeof(PERF_COUNTER);
    /* each instance: its definition with the name, then the block of the counters, a length and three 64-bit values. */
    SIZE_T per_instance = 8 + 3 * 8;
    SIZE_T total = definitions + (count + 1) * (sizeof(PERF_INSTANCE) + 16 + per_instance);
    BYTE *buffer = KbAllocZero(total);
    if (!buffer)
        return nullptr;
    PERF_OBJECT *object = (PERF_OBJECT *)buffer;
    object->DefinitionLength = (DWORD)definitions;
    object->HeaderLength = sizeof(PERF_OBJECT);
    object->ObjectNameTitleIndex = OBJECT_PROCESSOR;
    object->ObjectHelpTitleIndex = OBJECT_PROCESSOR + 1;
    object->DetailLevel = 100;
    object->NumCounters = 3;
    object->DefaultCounter = 0;
    object->NumInstances = (LONG)(count + 1);
    object->CodePage = 0;
    object->PerfTime.QuadPart = (LONGLONG)time;
    object->PerfFreq = *freq;
    PERF_COUNTER *counter = (PERF_COUNTER *)(buffer + sizeof(PERF_OBJECT));
    for (int i = 0; i < 3; i++, counter++) {
        counter->ByteLength = sizeof(*counter);
        counter->CounterNameTitleIndex = counters[i][0];
        counter->CounterHelpTitleIndex = counters[i][0] + 1;
        counter->DefaultScale = 0;
        counter->DetailLevel = 100;
        counter->CounterType = counters[i][1];
        counter->CounterSize = 8;
        counter->CounterOffset = (DWORD)(8 + 8 * i);
    }
    BYTE *at = buffer + definitions;
    LARGE_INTEGER sum[3] = {};
    for (ULONG i = 0; i <= count; i++) {
        LARGE_INTEGER value[3];
        WCHAR name[16];
        if (i < count) {
            value[0] = times[i].IdleTime;
            value[1] = times[i].UserTime;
            value[2].QuadPart = times[i].KernelTime.QuadPart - times[i].IdleTime.QuadPart;
            for (int c = 0; c < 3; c++)
                sum[c].QuadPart += value[c].QuadPart;
            int n = 0;
            for (ULONG v = i, d = 100; d; d /= 10)
                if (n || v / d || d == 1)
                    name[n++] = (WCHAR)(L'0' + v / d % 10);
            name[n] = 0;
        } else {
            memcpy(value, sum, sizeof(sum));
            memcpy(name, L"_Total", sizeof(L"_Total"));
        }
        PERF_INSTANCE *instance = (PERF_INSTANCE *)at;
        write_name(instance, name, -1);
        BYTE *block = at + instance->ByteLength;
        *(DWORD *)block = 8 + 3 * 8;
        memcpy(block + 8, value, sizeof(value));
        at = block + 8 + 3 * 8;
    }
    object->TotalByteLength = (DWORD)(at - buffer);
    *length = object->TotalByteLength;
    (void)time_100ns;
    return buffer;
}

static BOOL wants_object(const WCHAR *name, DWORD id)
{
    if (!name || !*name || !_wcsnicmp(name, L"Global", 7))
        return TRUE;
    for (const WCHAR *p = name; *p;) {
        while (*p == L' ')
            p++;
        DWORD v = 0;
        BOOL digits = FALSE;
        while (*p >= L'0' && *p <= L'9') {
            v = v * 10 + (DWORD)(*p++ - L'0');
            digits = TRUE;
        }
        if (!digits)
            return FALSE;
        if (v == id)
            return TRUE;
    }
    return FALSE;
}

LSTATUS KbQueryPerformanceData(LPCWSTR name, LPDWORD type, LPBYTE data, LPDWORD size)
{
    if (name && !_wcsnicmp(name, L"Counter", 7))
        return names_value(FALSE, type, data, size);
    if (name && !_wcsnicmp(name, L"Help", 4))
        return names_value(TRUE, type, data, size);
    if (!size && data)
        return ERROR_INVALID_PARAMETER;

    LARGE_INTEGER counter, freq, time_100ns;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&freq);
    ULONGLONG interrupt;
    QueryUnbiasedInterruptTime(&interrupt);
    time_100ns.QuadPart = (LONGLONG)interrupt;

    DWORD object_length = 0;
    BYTE *object = wants_object(name, OBJECT_PROCESSOR)
                       ? processor_object(&object_length, (ULONG64)counter.QuadPart, &time_100ns, &freq)
                       : nullptr;

    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD computer_length = ARRAYSIZE(computer);
    if (!GetComputerNameW(computer, &computer_length))
        computer[0] = 0;
    DWORD name_bytes = (DWORD)((wcslen(computer) + 1) * sizeof(WCHAR));
    DWORD header = (DWORD)((sizeof(PERF_BLOCK) + name_bytes + 7) & ~7ull);
    DWORD total = header + object_length;

    if (type)
        *type = REG_BINARY;
    LSTATUS error = ERROR_SUCCESS;
    if (!data || !size || *size < total) {
        error = ERROR_MORE_DATA;
    } else {
        PERF_BLOCK *block = (PERF_BLOCK *)data;
        memset(block, 0, header);
        memcpy(block->Signature, L"PERF", sizeof(block->Signature));
        block->LittleEndian = 1;
        block->Version = 1;
        block->Revision = 1;
        block->TotalByteLength = total;
        block->HeaderLength = header;
        block->NumObjectTypes = object ? 1 : 0;
        block->DefaultObject = object ? OBJECT_PROCESSOR : 0;
        GetSystemTime(&block->SystemTime);
        block->PerfTime = counter;
        block->PerfFreq = freq;
        block->PerfTime100nSec = time_100ns;
        block->SystemNameLength = name_bytes;
        block->SystemNameOffset = sizeof(PERF_BLOCK);
        memcpy(data + sizeof(PERF_BLOCK), computer, name_bytes);
        if (object)
            memcpy(data + header, object, object_length);
        *size = total;
    }
    KbFree(object);
    return error;
}
