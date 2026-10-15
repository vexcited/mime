/*
 * the environment block of new processes, built from the registry the way
 * userenv builds it at logon: system variables of the session manager,
 * the user's profile variables, then the user's own Environment key.
 */
#include "../ntos.h"

#define MAX_VARS 128
#define MAX_VALUE 4096

typedef struct ENV_VAR {
    PWCHAR Name;
    PWCHAR Value;
} ENV_VAR;

typedef struct ENV {
    ENV_VAR Vars[MAX_VARS];
    ULONG Count;
    BOOLEAN Expand; /* this pass takes REG_EXPAND_SZ values, the previous one REG_SZ */
    BOOLEAN AppendPath;
} ENV;

static PWCHAR dup(const WCHAR *s, ULONG chars)
{
    PWCHAR d = ExAllocatePool2(POOL_FLAG_NON_PAGED, (chars + 1) * sizeof(WCHAR), KI_TAG('P', 's', 'E', 'v'));
    if (d) {
        memcpy(d, s, chars * sizeof(WCHAR));
        d[chars] = 0;
    }
    return d;
}

static ENV_VAR *find(ENV *env, const WCHAR *name, ULONG chars)
{
    for (ULONG i = 0; i < env->Count; i++)
        if (_wcsnicmp(env->Vars[i].Name, name, chars) == 0 && env->Vars[i].Name[chars] == 0)
            return &env->Vars[i];
    return NULL;
}

static void set(ENV *env, const WCHAR *name, ULONG name_chars, const WCHAR *value, ULONG value_chars)
{
    ENV_VAR *v = find(env, name, name_chars);
    if (!v) {
        if (env->Count == MAX_VARS)
            return;
        v = &env->Vars[env->Count];
        v->Name = dup(name, name_chars);
        v->Value = NULL;
        if (!v->Name)
            return;
        env->Count++;
    }
    PWCHAR old = v->Value;
    if (old && env->AppendPath && name_chars == 4 && _wcsnicmp(name, L"Path", 4) == 0) {
        /* the user's Path extends the system one. */
        ULONG n = (ULONG)wcslen(old);
        PWCHAR joined = ExAllocatePool2(POOL_FLAG_NON_PAGED, (n + 1 + value_chars + 1) * sizeof(WCHAR), KI_TAG('P', 's', 'E', 'v'));
        if (!joined)
            return;
        memcpy(joined, old, n * sizeof(WCHAR));
        ULONG at = n;
        if (n && old[n - 1] != ';')
            joined[at++] = ';';
        memcpy(joined + at, value, value_chars * sizeof(WCHAR));
        joined[at + value_chars] = 0;
        v->Value = joined;
    } else {
        v->Value = dup(value, value_chars);
    }
    if (old)
        ExFreePool(old);
}

static void set_z(ENV *env, PCWSTR name, PCWSTR value)
{
    set(env, name, (ULONG)wcslen(name), value, (ULONG)wcslen(value));
}

/* expands %VAR% against the variables so far; unknown references stay as they are. */
static ULONG expand(ENV *env, const WCHAR *in, ULONG chars, PWCHAR out, ULONG capacity)
{
    ULONG n = 0;
    for (ULONG i = 0; i < chars && n + 1 < capacity;) {
        if (in[i] == '%') {
            ULONG end = i + 1;
            while (end < chars && in[end] != '%')
                end++;
            ENV_VAR *v = end < chars ? find(env, in + i + 1, end - i - 1) : NULL;
            if (v) {
                for (PCWSTR p = v->Value; *p && n + 1 < capacity; p++)
                    out[n++] = *p;
                i = end + 1;
                continue;
            }
        }
        out[n++] = in[i++];
    }
    out[n] = 0;
    return n;
}

static void from_registry(PVOID ctx, PCWSTR name, ULONG name_chars, ULONG type, const void *data, ULONG length)
{
    ENV *env = ctx;
    if (type != (env->Expand ? 2 /* REG_EXPAND_SZ */ : 1 /* REG_SZ */) || !name_chars)
        return;
    const WCHAR *text = data;
    ULONG chars = length / sizeof(WCHAR);
    while (chars && !text[chars - 1])
        chars--;
    if (env->Expand) {
        PWCHAR expanded = ExAllocatePool2(POOL_FLAG_NON_PAGED, MAX_VALUE * sizeof(WCHAR), KI_TAG('P', 's', 'E', 'x'));
        if (!expanded)
            return;
        ULONG n = expand(env, text, chars, expanded, MAX_VALUE);
        set(env, name, name_chars, expanded, n);
        ExFreePool(expanded);
    } else {
        set(env, name, name_chars, text, chars);
    }
}

static void load_key(ENV *env, PCWSTR path, BOOLEAN append_path)
{
    env->AppendPath = append_path;
    env->Expand = FALSE;
    CmEnumerateValues(path, from_registry, env);
    env->Expand = TRUE;
    CmEnumerateValues(path, from_registry, env);
    env->AppendPath = FALSE;
}

static int compare(const ENV_VAR *a, const ENV_VAR *b)
{
    return _wcsicmp(a->Name, b->Name);
}

NTSTATUS PspBuildEnvironment(USHORT machine, PWCHAR *block, SIZE_T *bytes)
{
    ENV *env = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ENV), KI_TAG('P', 's', 'E', 'n'));
    if (!env)
        return STATUS_INSUFFICIENT_RESOURCES;
    WCHAR computer[16], user[24], sid[96] = {0}, path[192] = {0};
    for (ULONG i = 0; i < 16; i++)
        computer[i] = (UCHAR)HalBootInfo->computer_name[i];
    for (ULONG i = 0; i < 24; i++)
        user[i] = (UCHAR)HalBootInfo->user_name[i];
    computer[15] = user[23] = 0;
    _snwprintf(sid, RTL_NUMBER_OF(sid) - 1, L"S-1-5-21-%u-%u-%u-1001", HalBootInfo->user_sid[0],
               HalBootInfo->user_sid[1], HalBootInfo->user_sid[2]);

    /* the variables userenv sets before reading the registry. */
    set_z(env, L"ALLUSERSPROFILE", L"C:\\ProgramData");
    set_z(env, L"CommonProgramFiles", L"C:\\Program Files\\Common Files");
    set_z(env, L"CommonProgramFiles(x86)", L"C:\\Program Files (x86)\\Common Files");
    set_z(env, L"CommonProgramW6432", L"C:\\Program Files\\Common Files");
    set_z(env, L"COMPUTERNAME", computer);
    set_z(env, L"ProgramData", L"C:\\ProgramData");
    set_z(env, L"ProgramFiles", L"C:\\Program Files");
    set_z(env, L"ProgramFiles(x86)", L"C:\\Program Files (x86)");
    set_z(env, L"ProgramW6432", L"C:\\Program Files");
    set_z(env, L"PUBLIC", L"C:\\Users\\Public");
    set_z(env, L"SystemDrive", L"C:");
    set_z(env, L"SystemRoot", L"C:\\Windows");
    /* only arm64 windows has a folder for arm64 programs apart from x64 ones. */
    if (MIME_NATIVE_ARCHITECTURE == PROCESSOR_ARCHITECTURE_ARM64) {
        set_z(env, L"CommonProgramFiles(Arm)", L"C:\\Program Files (Arm)\\Common Files");
        set_z(env, L"ProgramFiles(Arm)", L"C:\\Program Files (Arm)");
    }

    load_key(env, L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", FALSE);
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"\\REGISTRY\\USER\\%ls\\Volatile Environment", sid);
    load_key(env, path, FALSE);
    memset(path, 0, sizeof(path));
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"\\REGISTRY\\USER\\%ls\\Volatile Environment\\1", sid);
    load_key(env, path, FALSE);
    set_z(env, L"USERNAME", user); /* the system value names SYSTEM, the logon replaces it */
    memset(path, 0, sizeof(path));
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"\\REGISTRY\\USER\\%ls\\Environment", sid);
    load_key(env, path, TRUE);
    /* x64 programs run by the translator see the processor they were built for. */
    if (machine == IMAGE_FILE_MACHINE_AMD64)
        set_z(env, L"PROCESSOR_ARCHITECTURE", L"AMD64");

    /* 32-bit programs get the 32-bit program folders, as wow64 gives them. */
    if (machine == IMAGE_FILE_MACHINE_I386) {
        set_z(env, L"PROCESSOR_ARCHITECTURE", L"x86");
        set_z(env, L"PROCESSOR_ARCHITEW6432", MIME_NATIVE_ARCHITECTURE_NAME);
        set_z(env, L"ProgramFiles", L"C:\\Program Files (x86)");
        set_z(env, L"CommonProgramFiles", L"C:\\Program Files (x86)\\Common Files");
    }

    /* sorted, case insensitive, like RtlSetEnvironmentVariable keeps it. */
    for (ULONG i = 1; i < env->Count; i++)
        for (ULONG j = i; j > 0 && compare(&env->Vars[j - 1], &env->Vars[j]) > 0; j--) {
            ENV_VAR t = env->Vars[j];
            env->Vars[j] = env->Vars[j - 1];
            env->Vars[j - 1] = t;
        }
    SIZE_T chars = 1;
    for (ULONG i = 0; i < env->Count; i++)
        chars += wcslen(env->Vars[i].Name) + 1 + (env->Vars[i].Value ? wcslen(env->Vars[i].Value) : 0) + 1;
    PWCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, chars * sizeof(WCHAR), KI_TAG('P', 's', 'E', 'b'));
    SIZE_T at = 0;
    for (ULONG i = 0; i < env->Count; i++) {
        if (out) {
            SIZE_T n = wcslen(env->Vars[i].Name);
            memcpy(out + at, env->Vars[i].Name, n * sizeof(WCHAR));
            at += n;
            out[at++] = '=';
            PCWSTR value = env->Vars[i].Value ? env->Vars[i].Value : L"";
            n = wcslen(value);
            memcpy(out + at, value, n * sizeof(WCHAR));
            at += n;
            out[at++] = 0;
        }
        ExFreePool(env->Vars[i].Name);
        if (env->Vars[i].Value)
            ExFreePool(env->Vars[i].Value);
    }
    ExFreePool(env);
    if (!out)
        return STATUS_INSUFFICIENT_RESOURCES;
    out[at++] = 0;
    *block = out;
    *bytes = at * sizeof(WCHAR);
    return STATUS_SUCCESS;
}
