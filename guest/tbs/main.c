/*
 * tpm base services with a software tpm 2.0 behind them. the reference machine ships a firmware tpm, so the
 * device answers as one: callers check their arguments like windows, contexts are real, and the commands a
 * probe or the boot of the system sends, startup, self test, the capabilities, the pcr read and random bytes,
 * are answered by the tpm below. commands of the key hierarchy it does not carry fail with the code of an
 * unknown command.
 */
#define TBSAPI
#include <mime/win32/tbs.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/* response codes of tpm 2.0, and the tags of its messages. */
#define TPM_ST_NO_SESSIONS 0x8001
#define TPM_ST_SESSIONS 0x8002
#define TPM_RC_SUCCESS 0x000
#define TPM_RC_BAD_TAG 0x01E
#define TPM_RC_COMMAND_CODE 0x143
#define TPM_RC_VALUE 0x080
#define TPM_RC_INITIALIZE 0x100

#define TPM_CC_SELFTEST 0x143
#define TPM_CC_STARTUP 0x144
#define TPM_CC_GET_CAPABILITY 0x17A
#define TPM_CC_GET_RANDOM 0x17B
#define TPM_CC_PCR_READ 0x17E

#define TPM_CAP_ALGS 0x00000000
#define TPM_CAP_HANDLES 0x00000001
#define TPM_CAP_COMMANDS 0x00000002
#define TPM_CAP_PCRS 0x00000005
#define TPM_CAP_TPM_PROPERTIES 0x00000006

#define TPM_PT_FAMILY_INDICATOR 0x00000100
#define TPM_PT_MANUFACTURER 0x00000105
#define TPM_PT_VENDOR_STRING_1 0x00000106
#define TPM_PT_MAX_COMMAND_SIZE 0x0000010B
#define TPM_PT_MAX_RESPONSE_SIZE 0x0000010C
#define TPM_PT_NUMBER_OF_PCRS 0x00000112
#define TPM_PT_NUMBER_OF_PCR_BANKS 0x00000113
#define TPM_PT_SELECTION_MIN 0x00000114
#define TPM_PT_SELECTION_MAX 0x00000115
#define TPM_PT_MAX_NV_INDEX_SIZE 0x00000116
#define TPM_PT_MAX_NV_BUFFER_SIZE 0x00000117
#define TPM_PT_LOCKOUT_COUNTER 0x0000011F
#define TPM_PT_MAX_AUTH_FAIL 0x00000120
#define TPM_PT_LOCKOUT_INTERVAL 0x00000121
#define TPM_PT_LOCKOUT_RECOVERY 0x00000122
#define TPM_PT_NV_WRITE_RECOVERY 0x00000123
#define TPM_PT_AUDIT_COUNTER_0 0x00000124
#define TPM_PT_AUDIT_COUNTER_1 0x00000125

#define TPM_ALG_SHA1 0x0004
#define TPM_ALG_SHA256 0x000B
#define TPM_ALG_NULL 0x0010

#define TPM_RS_PW 0x40000009

#define PCR_COUNT 24
#define PCR_DIGEST 32

typedef struct TBS_CONTEXT_STATE {
    ULONG Magic;
    ULONG Locality;
} TBS_CONTEXT_STATE, *PTBS_CONTEXT_STATE;

#define CONTEXT_MAGIC 0x54504d43

static BOOLEAN g_started;
static BYTE g_pcr[PCR_COUNT][PCR_DIGEST];
static ULONG g_pcr_read_count;
static ULONG g_random_state;

typedef struct TBS_WRITER {
    PBYTE Buffer;
    UINT32 Capacity;
    UINT32 Size;
    BOOLEAN Overflow;
} TBS_WRITER;

static void put8(TBS_WRITER *w, BYTE v)
{
    if (w->Size < w->Capacity)
        w->Buffer[w->Size++] = v;
    else
        w->Overflow = TRUE;
}

static void put16(TBS_WRITER *w, USHORT v)
{
    put8(w, (BYTE)(v >> 8));
    put8(w, (BYTE)v);
}

static void put32(TBS_WRITER *w, ULONG v)
{
    put16(w, (USHORT)(v >> 16));
    put16(w, (USHORT)v);
}

static void put_bytes(TBS_WRITER *w, const void *data, UINT32 length)
{
    for (UINT32 i = 0; i < length; i++)
        put8(w, ((const BYTE *)data)[i]);
}

static ULONG get32(const BYTE *b)
{
    return (ULONG)b[0] << 24 | (ULONG)b[1] << 16 | (ULONG)b[2] << 8 | b[3];
}

static USHORT get16(const BYTE *b)
{
    return (USHORT)(b[0] << 8 | b[1]);
}

static ULONG random32(void)
{
    ULONG v = g_random_state ? g_random_state : 0x9e3779b9u;
    v ^= (ULONG)(ULONG_PTR)&v ^ (ULONG)GetTickCount() ^ (ULONG)GetCurrentThreadId();
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    g_random_state = v;
    return v;
}

TBS_RESULT WINAPI Tbsi_GetDeviceInfo(UINT32 size, PVOID info)
{
    if (!info || size < sizeof(TPM_DEVICE_INFO))
        return TBS_E_BAD_PARAMETER;
    TPM_DEVICE_INFO *device = info;
    device->structVersion = 1;
    device->tpmVersion = 2; /* TPM_VERSION_20 */
    device->tpmInterfaceType = 2; /* a firmware tpm of the soc */
    device->tpmImpRevision = 0;
    return TBS_SUCCESS;
}

BOOL WINAPI Tbsi_Is_Tpm_Present(void)
{
    return TRUE;
}

TBS_RESULT WINAPI Tbsi_Context_Create(PCTBS_CONTEXT_PARAMS params, PTBS_HCONTEXT context)
{
    if (!params)
        return TBS_E_BAD_PARAMETER;
    if (!context)
        return TBS_E_INVALID_OUTPUT_POINTER;
    if (params->version != TBS_CONTEXT_VERSION_ONE && params->version != TBS_CONTEXT_VERSION_TWO)
        return TBS_E_INVALID_CONTEXT_PARAM;
    PTBS_CONTEXT_STATE state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
    if (!state)
        return TBS_E_INTERNAL_ERROR;
    state->Magic = CONTEXT_MAGIC;
    *context = state;
    return TBS_SUCCESS;
}

TBS_RESULT WINAPI Tbsip_Context_Close(TBS_HCONTEXT context)
{
    PTBS_CONTEXT_STATE state = context;
    if (!state || state->Magic != CONTEXT_MAGIC)
        return TBS_E_INVALID_CONTEXT;
    state->Magic = 0;
    HeapFree(GetProcessHeap(), 0, state);
    return TBS_SUCCESS;
}

TBS_RESULT WINAPI Tbsip_Cancel_Commands(TBS_HCONTEXT context)
{
    PTBS_CONTEXT_STATE state = context;
    return state && state->Magic == CONTEXT_MAGIC ? TBS_SUCCESS : TBS_E_INVALID_CONTEXT;
}

/* the properties of the fixed group, the ones a probe of the device asks about. */
static const struct {
    ULONG Property;
    ULONG Value;
} g_properties[] = {
    {TPM_PT_FAMILY_INDICATOR, 0x322E3000}, /* "2.0" */
    {0x00000101, 0},                       /* level */
    {0x00000102, 138},                     /* revision 1.38 */
    {0x00000103, 1},                       /* day of year */
    {0x00000104, 2022},                    /* year */
    {TPM_PT_MANUFACTURER, 0x4D534654},     /* "MSFT" */
    {TPM_PT_VENDOR_STRING_1, 0x4D534654},  /* "MSFT" */
    {0x00000107, 0x6654504D},              /* "fTPM" */
    {0x00000108, 0},
    {0x00000109, 0},
    {0x0000010A, 0},
    {TPM_PT_MAX_COMMAND_SIZE, 0x500},
    {TPM_PT_MAX_RESPONSE_SIZE, 0x500},
    {0x0000010D, 0}, /* manufacturer tpm type */
    {0x0000010E, 0}, /* manufacturer tpm revision */
    {0x0000010F, 0},
    {0x00000110, 0},
    {0x00000111, 0}, /* firmware version 1 */
    {TPM_PT_NUMBER_OF_PCRS, PCR_COUNT},
    {TPM_PT_NUMBER_OF_PCR_BANKS, 1},
    {TPM_PT_SELECTION_MIN, 2},
    {TPM_PT_SELECTION_MAX, 4},
    {TPM_PT_MAX_NV_INDEX_SIZE, 2048},
    {TPM_PT_MAX_NV_BUFFER_SIZE, 1024},
    {0x00000118, 0x400}, /* max context size */
    {0x00000119, 0},     /* max transform */
    {0x0000011A, 0},     /* max symmetric data */
    {0x0000011B, 0},     /* algorithm set */
    {0x0000011C, 0},     /* loaded curves */
    {0x0000011D, 0},     /* auth fail report */
    {0x0000011E, 0},
    {TPM_PT_LOCKOUT_COUNTER, 1},
    {TPM_PT_MAX_AUTH_FAIL, 32},
    {TPM_PT_LOCKOUT_INTERVAL, 7200},
    {TPM_PT_LOCKOUT_RECOVERY, 86400},
    {TPM_PT_NV_WRITE_RECOVERY, 0},
    {TPM_PT_AUDIT_COUNTER_0, 0},
    {TPM_PT_AUDIT_COUNTER_1, 0},
};

/* command attributes, as pid two thirds of the capability of the commands returns. */
static const struct {
    ULONG Command;
    ULONG Attributes;
} g_commands[] = {
    {TPM_CC_SELFTEST, 0},
    {TPM_CC_STARTUP, 0},
    {TPM_CC_GET_CAPABILITY, 0},
    {TPM_CC_GET_RANDOM, 0},
    {TPM_CC_PCR_READ, 0},
};

static ULONG tpm_capability(TBS_WRITER *w, ULONG capability, ULONG property, ULONG count)
{
    switch (capability) {
    case TPM_CAP_TPM_PROPERTIES: {
        ULONG first = 0;
        while (first + 1 < RTL_NUMBER_OF(g_properties) && g_properties[first].Property < property)
            first++;
        ULONG available = (ULONG)RTL_NUMBER_OF(g_properties) - first;
        ULONG wanted = count < available ? count : available;
        put32(w, wanted);
        for (ULONG i = 0; i < wanted; i++) {
            put32(w, g_properties[first + i].Property);
            put32(w, g_properties[first + i].Value);
        }
        return wanted;
    }
    case TPM_CAP_ALGS: {
        static const ULONG algorithms[] = {TPM_ALG_SHA1, TPM_ALG_SHA256, TPM_ALG_NULL};
        ULONG available = RTL_NUMBER_OF(algorithms);
        ULONG wanted = count < available ? count : available;
        put32(w, wanted);
        for (ULONG i = 0; i < wanted; i++) {
            ULONG algorithm = algorithms[i];
            put16(w, (USHORT)algorithm);
            put16(w, algorithm == TPM_ALG_NULL ? 0 : 1); /* keyed hash */
            put16(w, algorithm == TPM_ALG_NULL ? 0 : (USHORT)algorithm); /* hash */
            put16(w, algorithm == TPM_ALG_NULL ? 0 : 0x0014); /* sign, rsassa */
        }
        return wanted;
    }
    case TPM_CAP_COMMANDS: {
        ULONG first = 0;
        while (first + 1 < RTL_NUMBER_OF(g_commands) && g_commands[first].Command < property)
            first++;
        ULONG available = (ULONG)RTL_NUMBER_OF(g_commands) - first;
        ULONG wanted = count < available ? count : available;
        put32(w, wanted);
        for (ULONG i = 0; i < wanted; i++) {
            put32(w, g_commands[first + i].Command);
            put32(w, g_commands[first + i].Attributes);
        }
        return wanted;
    }
    case TPM_CAP_PCRS: {
        /* one bank, sha256, all the pcrs selected. */
        put32(w, 1);
        put16(w, TPM_ALG_SHA256);
        put8(w, PCR_DIGEST);
        put8(w, 0xFF);
        put8(w, 0xFF);
        put8(w, 0xFF);
        return 1;
    }
    case TPM_CAP_HANDLES: {
        /* the two permanent handles windows makes. */
        static const ULONG handles[] = {0x81000001, 0x81010001};
        ULONG first = 0, available = 0, wanted;
        while (first < RTL_NUMBER_OF(handles) && handles[first] < property)
            first++;
        available = (ULONG)RTL_NUMBER_OF(handles) - first;
        wanted = count < available ? count : available;
        put32(w, wanted);
        for (ULONG i = 0; i < wanted; i++)
            put32(w, handles[first + i]);
        return wanted;
    }
    default:
        put32(w, 0);
        return 0;
    }
}

/* the answer of the tpm, built from the command and the state. */
static ULONG tpm_execute(const BYTE *command, UINT32 size, TBS_WRITER *w)
{
    if (size < 10)
        return TPM_RC_COMMAND_CODE;
    USHORT tag = get16(command);
    ULONG length = get32(command + 2);
    ULONG code = get32(command + 6);
    if (tag != TPM_ST_NO_SESSIONS && tag != TPM_ST_SESSIONS)
        return TPM_RC_BAD_TAG;
    if (length != size)
        return TPM_RC_COMMAND_CODE;

    switch (code) {
    case TPM_CC_STARTUP: {
        if (size < 12)
            return TPM_RC_COMMAND_CODE;
        USHORT type = get16(command + 10);
        if (type > 1)
            return TPM_RC_VALUE;
        if (type == 0) /* TPM_SU_CLEAR */
            for (ULONG i = 0; i < PCR_COUNT; i++)
                memset(g_pcr[i], 0, PCR_DIGEST);
        g_started = TRUE;
        return TPM_RC_SUCCESS;
    }
    case TPM_CC_SELFTEST:
        return TPM_RC_SUCCESS;
    case TPM_CC_GET_CAPABILITY: {
        if (size < 22)
            return TPM_RC_COMMAND_CODE;
        ULONG more = tpm_capability(w, get32(command + 10), get32(command + 14), get32(command + 18));
        (void)more;
        return TPM_RC_SUCCESS;
    }
    case TPM_CC_GET_RANDOM: {
        if (size < 12)
            return TPM_RC_COMMAND_CODE;
        UINT32 wanted = get16(command + 10);
        if (wanted > 64)
            wanted = 64;
        put16(w, (USHORT)wanted);
        for (UINT32 i = 0; i < wanted; i++)
            put8(w, (BYTE)(random32() >> ((i % 4) * 8)));
        return TPM_RC_SUCCESS;
    }
    case TPM_CC_PCR_READ: {
        if (!g_started)
            return TPM_RC_INITIALIZE;
        if (size < 14)
            return TPM_RC_COMMAND_CODE;
        ULONG selections = get32(command + 10);
        if (!selections || selections > 1)
            return TPM_RC_VALUE;
        UINT32 at = 14;
        USHORT algorithm = get16(command + at);
        at += 2;
        UINT32 digest = command[at++];
        if (digest != PCR_DIGEST || at + 3 > size)
            return TPM_RC_VALUE;
        BYTE bitmap[3] = {command[at], command[at + 1], command[at + 2]};
        put32(w, ++g_pcr_read_count);
        put32(w, 1);
        put16(w, algorithm);
        put8(w, PCR_DIGEST);
        put8(w, bitmap[0]);
        put8(w, bitmap[1]);
        put8(w, bitmap[2]);
        for (ULONG pcr = 0; pcr < PCR_COUNT; pcr++) {
            if (!(bitmap[pcr / 8] & (1 << (pcr % 8))))
                continue;
            put_bytes(w, g_pcr[pcr], PCR_DIGEST);
        }
        return TPM_RC_SUCCESS;
    }
    default:
        return TPM_RC_COMMAND_CODE;
    }
}

/* only locality zero, and the buffers are checked before the context. */
static TBS_RESULT submit(TBS_HCONTEXT context, TBS_COMMAND_LOCALITY locality, TBS_COMMAND_PRIORITY priority,
                         const BYTE *command, UINT32 size, PBYTE result, PUINT32 result_size)
{
    if (locality || priority > 0x80000000u)
        return TBS_E_BAD_PARAMETER;
    if (!result || !result_size)
        return TBS_E_INVALID_OUTPUT_POINTER;
    if (*result_size < 10)
        return TBS_E_INSUFFICIENT_BUFFER;
    if (!command || !size)
        return TBS_E_BAD_PARAMETER;
    PTBS_CONTEXT_STATE state = context;
    if (!state || state->Magic != CONTEXT_MAGIC)
        return TBS_E_INVALID_CONTEXT;

    /* the header is answered in place: tag, the length of the whole answer, then the code. */
    TBS_WRITER w = {result + 10, *result_size - 10, 0, FALSE};
    ULONG code = tpm_execute(command, size, &w);
    USHORT tag = size >= 2 && get16(command) == TPM_ST_SESSIONS ? TPM_ST_SESSIONS : TPM_ST_NO_SESSIONS;
    UINT32 total = 10 + w.Size;
    if (w.Overflow || total > *result_size) {
        *result_size = total;
        return TBS_E_INSUFFICIENT_BUFFER;
    }
    TBS_WRITER header = {result, 10, 0, FALSE};
    put16(&header, tag);
    put32(&header, total);
    put32(&header, code);
    *result_size = total;
    return TBS_SUCCESS;
}

TBS_RESULT WINAPI Tbsip_Submit_Command(TBS_HCONTEXT context, TBS_COMMAND_LOCALITY locality,
                                       TBS_COMMAND_PRIORITY priority, const BYTE *command, UINT32 size, PBYTE result,
                                       PUINT32 result_size)
{
    return submit(context, locality, priority, command, size, result, result_size);
}

TBS_RESULT WINAPI Tbsip_Submit_Command_NonBlocking(TBS_HCONTEXT context, TBS_COMMAND_LOCALITY locality,
                                                   TBS_COMMAND_PRIORITY priority, const BYTE *command, UINT32 size,
                                                   PBYTE result, PUINT32 result_size)
{
    return submit(context, locality, priority, command, size, result, result_size);
}

TBS_RESULT WINAPI Tbsi_Physical_Presence_Command(TBS_HCONTEXT context, const BYTE *input, UINT32 input_size,
                                                 PBYTE output, PUINT32 output_size)
{
    if (!input)
        return TBS_E_BAD_PARAMETER;
    if (!output || !output_size)
        return TBS_E_INVALID_OUTPUT_POINTER;
    if (input_size > 0x40000 || *output_size > 0x40000)
        return TBS_E_BUFFER_TOO_LARGE;
    PTBS_CONTEXT_STATE state = context;
    return state && state->Magic == CONTEXT_MAGIC ? TBS_SUCCESS : TBS_E_INVALID_CONTEXT;
}

/* windows keeps the measured boot log in the registry. */
TBS_RESULT WINAPI Tbsi_Get_TCG_Log(TBS_HCONTEXT context, PBYTE log, PUINT32 size)
{
    if (!size)
        return TBS_E_INVALID_OUTPUT_POINTER;
    UINT32 capacity = *size;
    *size = 0;
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\IntegrityServices", 0,
                               KEY_QUERY_VALUE, &key);
    /* windows always has the key, so a missing key means a missing log. */
    if (error)
        return error == ERROR_FILE_NOT_FOUND ? TBS_E_NO_EVENT_LOG : HRESULT_FROM_WIN32((ULONG)error);
    DWORD bytes = 0;
    error = RegQueryValueExW(key, L"WBCL", nullptr, nullptr, nullptr, &bytes);
    if (error) {
        RegCloseKey(key);
        return error == ERROR_FILE_NOT_FOUND ? TBS_E_NO_EVENT_LOG : HRESULT_FROM_WIN32((ULONG)error);
    }
    if (!log || capacity < bytes) {
        RegCloseKey(key);
        *size = bytes;
        return log ? TBS_E_INSUFFICIENT_BUFFER : TBS_SUCCESS;
    }
    error = RegQueryValueExW(key, L"WBCL", nullptr, nullptr, log, &bytes);
    RegCloseKey(key);
    if (error)
        return HRESULT_FROM_WIN32((ULONG)error);
    *size = bytes;
    return TBS_SUCCESS;
}

TBS_RESULT WINAPI Tbsi_Get_TCG_Log_Ex(UINT32 type, PBYTE log, PUINT32 size)
{
    if (!size)
        return TBS_E_BAD_PARAMETER;
    return Tbsi_Get_TCG_Log(nullptr, log, size);
}

TBS_RESULT WINAPI Tbsi_Revoke_Attestation(void)
{
    return TBS_SUCCESS;
}

/* the two keys windows keeps in the tpm. */
TBS_RESULT WINAPI Tbsi_Create_Windows_Key(UINT32 key)
{
    if (key != 0x81000001u && key != 0x81010001u)
        return TBS_E_BAD_PARAMETER;
    return TBS_SUCCESS;
}
