/*
 * features on demand, where a program meets them. a feature is a runtime windows 11 comes with and mime fetches the
 * first time a program needs it: the WebView2 runtime, the .NET Framework, Windows PowerShell, the catalogs of the
 * system files. the Visual C++ runtime is one too, as near every machine has it from some program's installer and a
 * program or a plugin that does not bring its own takes it for granted. the system dlls are microsoft's, so the need is
 * seen here, in the two services a program reaches for a feature with:
 *
 *   - a file in the folders of a feature that is opened to be read or run and is not there. a look at a name, the
 *     attributes of a file or the listing of a folder, asks for nothing, except for the one program a feature is
 *     started by and the dlls of the Visual C++ runtime in System32, which are searched for by their attributes;
 *   - the registry key the updater of a feature writes and its programs read to find it, when it is not there.
 *
 * the caller then waits for OptionalFeatures.exe, which asks the user and installs the feature, and the open is tried
 * again. a process asks once for a feature, whatever the answer. nothing declares a feature that has no files: a
 * feature the user or a program's own installer put on the drive, in any version, is the feature.
 */
#include "../ntos.h"

#define WEBVIEW2_APP L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
#define WEBVIEW2_MACHINE_KEY L"\\REGISTRY\\MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\" WEBVIEW2_APP
#define WEBVIEW2_USER_KEY_FORMAT L"\\REGISTRY\\USER\\S-1-5-21-%u-%u-%u-1001\\Software\\Microsoft\\EdgeUpdate\\Clients\\" WEBVIEW2_APP

typedef struct FEATURE {
    PCWSTR Name; /* of the catalog of OptionalFeatures.exe. */
    BOOLEAN (*Present)(void);
} FEATURE;

typedef struct FOLDER {
    PCWSTR Path; /* below the drive, with a closing backslash. */
    ULONG Feature;
} FOLDER;

static BOOLEAN file_exists(PCWSTR path)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    PFILE_OBJECT file;
    if (!NT_SUCCESS(IopOpenFileObject(&name, FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0, &file)))
        return FALSE;
    ObDereferenceObject(file);
    return TRUE;
}

static BOOLEAN value_exists(PCWSTR key, PCWSTR value)
{
    WCHAR data[64];
    ULONG type, length = sizeof(data);
    NTSTATUS status = CmQueryValue(key, value, &type, data, &length);
    return NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL;
}

/* the runtime of the machine or of the user, as its updater records the version it installed. */
static BOOLEAN webview2_present(void)
{
    if (value_exists(WEBVIEW2_MACHINE_KEY, L"pv"))
        return TRUE;
    WCHAR key[200];
    _snwprintf(key, RTL_NUMBER_OF(key) - 1, WEBVIEW2_USER_KEY_FORMAT, HalBootInfo->user_sid[0], HalBootInfo->user_sid[1],
               HalBootInfo->user_sid[2]);
    key[RTL_NUMBER_OF(key) - 1] = 0;
    return value_exists(key, L"pv");
}

static BOOLEAN netfx_present(void)
{
    return file_exists(L"\\??\\C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\clr.dll");
}

#define POWERSHELL_PROGRAM L"\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"

static BOOLEAN powershell_present(void)
{
    return file_exists(L"\\??\\C:" POWERSHELL_PROGRAM);
}

#define CATALOG_STORE L"\\Windows\\System32\\CatRoot\\{F750E6C3-38EE-11D1-85E5-00C04FC295EE}\\"

/* the catalog of the build's rollup, which names most of the system files. the catalogs are those of amd64 only. */
static BOOLEAN catalogs_present(void)
{
#ifdef __x86_64__
    return file_exists(L"\\??\\C:" CATALOG_STORE L"Package_for_RollupFix~31bf3856ad364e35~amd64~~26100.9457.1.0.cat");
#else
    return TRUE;
#endif
}

#define SYSTEM_FOLDER L"\\Windows\\System32\\"

/*
 * the dlls of the x64 packages of the redistributable that programs load. the runtime is the first three, which every
 * program built with it loads. an arm64 machine keeps them in SysX64 and does not ask.
 */
static const PCWSTR g_vcruntime[] = {
    L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll", L"vcruntime140_threads.dll", L"msvcp140_1.dll",
    L"msvcp140_2.dll", L"msvcp140_atomic_wait.dll", L"msvcp140_codecvt_ids.dll", L"concrt140.dll", L"vccorlib140.dll",
    L"vcomp140.dll", L"vcamp140.dll", L"mfc140.dll", L"mfc140u.dll", L"mfcm140.dll", L"mfcm140u.dll",
};

static BOOLEAN vcruntime_present(void)
{
#ifdef __x86_64__
    WCHAR path[64];
    for (ULONG i = 0; i < 3; i++) {
        _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"\\??\\C:" SYSTEM_FOLDER L"%ls", g_vcruntime[i]);
        path[RTL_NUMBER_OF(path) - 1] = 0;
        if (!file_exists(path))
            return FALSE;
    }
#endif
    return TRUE;
}

enum { FEATURE_WEBVIEW2, FEATURE_NETFX, FEATURE_POWERSHELL, FEATURE_CATALOGS, FEATURE_VCRUNTIME };

static const FEATURE g_features[] = {
    [FEATURE_WEBVIEW2] = {L"WebView2", webview2_present},
    [FEATURE_NETFX] = {L"NetFx4", netfx_present},
    [FEATURE_POWERSHELL] = {L"PowerShell", powershell_present},
    [FEATURE_CATALOGS] = {L"SystemCatalogs", catalogs_present},
    [FEATURE_VCRUNTIME] = {L"VCRuntime.x64", vcruntime_present},
};

/* the folders programs look into for what a feature holds. the shell comes before the folders it shares. */
static const FOLDER g_folders[] = {
    {L"\\Windows\\System32\\WindowsPowerShell\\", FEATURE_POWERSHELL},
    {L"\\Windows\\SysWOW64\\WindowsPowerShell\\", FEATURE_POWERSHELL},
    {L"\\Program Files (x86)\\Microsoft\\EdgeWebView\\", FEATURE_WEBVIEW2},
    {L"\\Windows\\Microsoft.NET\\", FEATURE_NETFX},
    {L"\\Windows\\assembly\\", FEATURE_NETFX},
    {CATALOG_STORE, FEATURE_CATALOGS},
};

/* the programs a feature is started by: a search for one looks at its attributes and opens nothing. */
static const FOLDER g_programs[] = {
    {POWERSHELL_PROGRAM, FEATURE_POWERSHELL},
    {L"\\Windows\\SysWOW64\\WindowsPowerShell\\v1.0\\powershell.exe", FEATURE_POWERSHELL},
};

/*
 * the folders of the cache of assemblies the runtime looks for an assembly of a feature in, by name and without opening
 * it. the folder of the shell's engine belongs to the shell, not to the framework it is in.
 */
static const FOLDER g_assemblies[] = {
    {L"\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Management.Automation", FEATURE_POWERSHELL},
};

/* the keys a feature is found by, as the last parts of their names. */
static const FOLDER g_keys[] = {
    {L"\\EdgeUpdate\\Clients\\" WEBVIEW2_APP, FEATURE_WEBVIEW2},
    {L"\\EdgeUpdate\\ClientState\\" WEBVIEW2_APP, FEATURE_WEBVIEW2},
};

static BOOLEAN starts_with(PCWSTR text, SIZE_T chars, PCWSTR head)
{
    SIZE_T n = wcslen(head);
    return chars >= n && !_wcsnicmp(text, head, n);
}

static BOOLEAN ends_with(PCWSTR text, SIZE_T chars, PCWSTR tail)
{
    SIZE_T n = wcslen(tail);
    return chars >= n && !_wcsnicmp(text + chars - n, tail, n);
}

/*
 * the installer of a feature must not be asked for what it is installing: the feature service and what it runs, and
 * the updater of the vendor when the user or a program runs it. neither are the services, which no user sits behind.
 */
static BOOLEAN installs_features(PEPROCESS process)
{
    if (!process->SessionId)
        return TRUE;
    static const char *const installers[] = {"OptionalFeature", "dism.exe", "fondue.exe", "MicrosoftEdge"};
    static const UCHAR whole[] = {1, 1, 1, 0};
    ObReferenceObject(process);
    BOOLEAN found = FALSE;
    for (int depth = 0; process && depth < 8 && !found; depth++) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(installers) && !found; i++)
            found = whole[i] ? !_stricmp(process->ImageFileName, installers[i])
                             : !_strnicmp(process->ImageFileName, installers[i], strlen(installers[i]));
        HANDLE parent = process->InheritedFromUniqueProcessId;
        ObDereferenceObject(process);
        process = nullptr;
        if (!found && parent && !NT_SUCCESS(PsLookupProcessByProcessId(parent, &process)))
            process = nullptr;
    }
    if (process)
        ObDereferenceObject(process);
    return found;
}

/* the name of the program that asks, for the question the user is shown. */
static ULONG caller_name(PEPROCESS process, PWCHAR out, ULONG chars)
{
    PCWSTR path = process->ImagePath.Buffer;
    ULONG n = process->ImagePath.Length / sizeof(WCHAR), start = n, used = 0;
    while (start && path[start - 1] != L'\\')
        start--;
    for (; start < n && used + 1 < chars; start++)
        out[used++] = path[start] == L' ' ? L'_' : path[start];
    out[used] = 0;
    return used;
}

/* asks for a feature and waits for the answer. TRUE when the feature is there afterwards. */
static BOOLEAN ask(ULONG index)
{
    const FEATURE *feature = &g_features[index];
    PEPROCESS process = PsCurrentProcess();
    UCHAR bit = (UCHAR)(1u << index);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || process->Terminating)
        return FALSE;
    if (feature->Present() || installs_features(process))
        return FALSE;
    if (__atomic_fetch_or(&process->FeaturesAsked, bit, __ATOMIC_SEQ_CST) & bit) {
        /* another thread of the program is asking: its answer is this one's. */
        LARGE_INTEGER tenth = {.QuadPart = -1000000};
        while ((__atomic_load_n(&process->FeaturesAsking, __ATOMIC_SEQ_CST) & bit) && !process->Terminating)
            KeDelayExecutionThread(UserMode, FALSE, &tenth);
        return feature->Present();
    }
    __atomic_fetch_or(&process->FeaturesAsking, bit, __ATOMIC_SEQ_CST);
    WCHAR line[200], name[64];
    caller_name(process, name, RTL_NUMBER_OF(name));
    _snwprintf(line, RTL_NUMBER_OF(line) - 1, L"OptionalFeatures.exe /enable-feature:%ls /caller-pid:%u%ls%ls", feature->Name,
               (ULONG)(ULONG_PTR)process->UniqueProcessId, name[0] ? L" /caller-name:" : L"", name);
    line[RTL_NUMBER_OF(line) - 1] = 0;
    UNICODE_STRING image = RTL_CONSTANT_STRING(L"C:\\Windows\\System32\\OptionalFeatures.exe"), command;
    RtlInitUnicodeString(&command, line);
    PEPROCESS service = nullptr;
    /* the call that failed came from the program, the service is started by the kernel. */
    PKTHREAD thread = KiCurrentThread();
    KPROCESSOR_MODE mode = thread->PreviousMode;
    thread->PreviousMode = KernelMode;
    NTSTATUS status = PsCreateUserProcess(&image, &command, nullptr, 0, (MIME_CONSOLE *)(LONG_PTR)-1, FALSE, &service);
    thread->PreviousMode = mode;
    if (NT_SUCCESS(status) && service) {
        KeWaitForSingleObject(service, UserRequest, UserMode, FALSE, nullptr);
        ObDereferenceObject(service);
    }
    __atomic_fetch_and(&process->FeaturesAsking, (UCHAR)~bit, __ATOMIC_SEQ_CST);
    return feature->Present();
}

/*
 * a file a program asked for by its full name was not found. look says the program asked for its attributes only.
 * TRUE when a feature arrived and the name is worth another try.
 */
BOOLEAN ExFeatureFileFault(PCUNICODE_STRING name, BOOLEAN look)
{
    if (!name || !name->Buffer)
        return FALSE;
    PCWSTR text = name->Buffer;
    SIZE_T chars = name->Length / sizeof(WCHAR);
    /* \??\C:\ and what follows. */
    if (chars < 7 || text[0] != L'\\' || text[1] != L'?' || text[2] != L'?' || text[3] != L'\\' || text[5] != L':' ||
        (text[4] | 0x20) != L'c')
        return FALSE;
    text += 6;
    chars -= 6;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_assemblies); i++) {
        SIZE_T n = wcslen(g_assemblies[i].Path);
        if (starts_with(text, chars, g_assemblies[i].Path) && (chars == n || text[n] == L'\\'))
            return ask(g_assemblies[i].Feature);
    }
#ifdef __x86_64__
    /* the loader looks for a dll in each folder of its search by its attributes, and opens only the one it found. */
    if (starts_with(text, chars, SYSTEM_FOLDER))
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_vcruntime); i++)
            if (chars - wcslen(SYSTEM_FOLDER) == wcslen(g_vcruntime[i]) && ends_with(text, chars, g_vcruntime[i]))
                return ask(FEATURE_VCRUNTIME);
#endif
    if (look) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_programs); i++)
            if (chars == wcslen(g_programs[i].Path) && starts_with(text, chars, g_programs[i].Path))
                return ask(g_programs[i].Feature);
        return FALSE;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_folders); i++)
        if (starts_with(text, chars, g_folders[i].Path))
            return ask(g_folders[i].Feature);
    return FALSE;
}

NTSTATUS NTAPI ZwQueryKey(HANDLE key, ULONG klass, PVOID info, ULONG length, PULONG returned);

/* a key a program asked for was not found: root is the key its name is relative to, or none. */
BOOLEAN ExFeatureKeyFault(HANDLE root, PCUNICODE_STRING name)
{
    if (!name || !name->Buffer || !name->Length)
        return FALSE;
    /* the keys of features end with the identifier of an application: nothing else is looked at further. */
    PCWSTR text = name->Buffer;
    SIZE_T chars = name->Length / sizeof(WCHAR);
    while (chars && text[chars - 1] == L'\\')
        chars--;
    if (chars < 38 || text[chars - 1] != L'}' || text[chars - 38] != L'{')
        return FALSE;
    struct {
        ULONG Length;
        WCHAR Name[400];
    } full = {};
    ULONG used = 0;
    if (root) {
        ULONG returned;
        if (!NT_SUCCESS(ZwQueryKey(root, 3 /* KeyNameInformation */, &full, sizeof(full) - 2 * sizeof(WCHAR), &returned)))
            return FALSE;
        used = full.Length / sizeof(WCHAR);
        full.Name[used++] = L'\\';
    }
    if (used + chars >= RTL_NUMBER_OF(full.Name))
        return FALSE;
    memcpy(full.Name + used, text, chars * sizeof(WCHAR));
    used += (ULONG)chars;
    if (!starts_with(full.Name, used, L"\\REGISTRY\\MACHINE\\"))
        return FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_keys); i++)
        if (ends_with(full.Name, used, g_keys[i].Path)) {
            /* the tools that show the registry look and use nothing. */
            const char *program = PsCurrentProcess()->ImageFileName;
            if (!_stricmp(program, "regedit.exe") || !_stricmp(program, "reg.exe"))
                return FALSE;
            return ask(g_keys[i].Feature);
        }
    return FALSE;
}
