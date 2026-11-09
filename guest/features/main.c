/*
 * OptionalFeatures.exe: turns the features that are fetched from the vendor on and off.
 *
 *   features list                           the features and what state each is in
 *   features status <name>                  one feature in full
 *   features enable <name> [--source file]  turns a feature on, getting its files when they are not on the drive
 *   features disable <name>                 turns a feature off and removes its files
 *
 * A program that needs a feature starts it as `features /enable-feature:<name> /caller-name:<program>`, the form of
 * Windows' fondue.exe, and the user is asked before anything is downloaded. The dism.exe and fondue.exe of System32
 * are this program under the names scripts know. The command line forms of dism work too:
 * `dism /online /enable-feature /featurename:<name> [/source:<file>]`.
 *
 * Changes need an administrator, a program that is not elevated starts itself again through the broker. The exit code
 * is 0 or the error, a Win32 error or an HRESULT.
 */
#include "featurep.h"

#define TOKEN_QUERY_ 0x0008
#define TokenElevation_ 20

#define EXIT_BAD_PARAMETER 87

WINBASEAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, int klass, LPVOID info, DWORD length, PDWORD returned);

typedef DWORD(WINAPI *LAUNCH_ELEVATED)(LPCWSTR application, LPCWSTR command, LPCWSTR directory, DWORD flags,
                                       const STARTUPINFOW *startup, PROCESS_INFORMATION *info, DWORD *kind);

typedef enum ACTION {
    ACTION_NONE,
    ACTION_LIST,
    ACTION_STATUS,
    ACTION_ENABLE,
    ACTION_DISABLE,
    ACTION_FONT,
    ACTION_HELP,
} ACTION;

typedef struct OPTIONS {
    ACTION Action;
    WCHAR Name[96];
    WCHAR Source[MAX_PATH];
    WCHAR Caller[64];
    DWORD CallerPid;
    BOOL Ask;       /* The form of a program that needs the feature, the user decides. */
    BOOL HideUi;
    BOOL Elevated;  /* Started by the broker for this very request. */
    BOOL Font;      /* A program asked for a font, Name is the family. */
} OPTIONS;

static const WCHAR kUsage[] =
    L"Turns the features that Mime fetches from the vendor on and off.\n"
    L"\n"
    L"  features list\n"
    L"  features status <name>\n"
    L"  features enable <name> [--source <file>]\n"
    L"  features disable <name>\n"
    L"\n"
    L"A feature is fetched the first time a program needs it, after the user agrees. Turning it on here is the\n"
    L"agreement. The files come from Microsoft and are run only when they are signed by Microsoft. With --source the\n"
    L"file is taken from the drive instead of the network, and is checked the same way.\n"
    L"\n"
    L"The forms of Windows work too: fondue /enable-feature:<name> asks before it downloads, and\n"
    L"dism /online /enable-feature /featurename:<name> [/source:<file>] does not.\n"
    L"\n"
    L"Policy: the value Consent under HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsFeatures, or under a key of the feature's name, is 0\n"
    L"to ask, 1 to download without asking and 2 to download nothing.\n";

static void narrow(const WCHAR *wide, char *narrow_text, SIZE_T chars)
{
    SIZE_T i = 0;
    for (; wide[i] && i + 1 < chars; i++)
        narrow_text[i] = wide[i] < 128 ? (char)wide[i] : '?';
    narrow_text[i] = 0;
}

/* The option a word is, without its dashes or slash, and its value after a colon or an equals sign. */
static BOOL option_of(const WCHAR *word, WCHAR *name, SIZE_T chars, const WCHAR **value)
{
    const WCHAR *p = word;
    if (*p == L'/')
        p++;
    else if (p[0] == L'-' && p[1] == L'-')
        p += 2;
    else if (*p == L'-')
        p++;
    else
        return FALSE;
    SIZE_T n = 0;
    while (*p && *p != L':' && *p != L'=' && n + 1 < chars)
        name[n++] = tu_lower(*p++);
    name[n] = 0;
    *value = *p ? p + 1 : nullptr;
    return TRUE;
}

static BOOL parse(OPTIONS *o)
{
    BOOL dism_enable = FALSE, dism_disable = FALSE, dism_list = FALSE, dism_info = FALSE;
    int positional = 0;
    for (int i = 1; i < tu_argc; i++) {
        WCHAR name[40];
        const WCHAR *value;
        if (!option_of(tu_argv[i], name, RTL_NUMBER_OF(name), &value)) {
            if (positional++ == 0 && o->Action == ACTION_NONE) {
                if (tu_eq(tu_argv[i], L"list"))
                    o->Action = ACTION_LIST;
                else if (tu_eq(tu_argv[i], L"status"))
                    o->Action = ACTION_STATUS;
                else if (tu_eq(tu_argv[i], L"enable"))
                    o->Action = ACTION_ENABLE;
                else if (tu_eq(tu_argv[i], L"disable"))
                    o->Action = ACTION_DISABLE;
                else if (tu_eq(tu_argv[i], L"help"))
                    o->Action = ACTION_HELP;
                else
                    return FALSE;
            } else if (!o->Name[0]) {
                lstrcpynW(o->Name, tu_argv[i], RTL_NUMBER_OF(o->Name));
            } else {
                return FALSE;
            }
        } else if (tu_eq(name, L"?") || tu_eq(name, L"h") || tu_eq(name, L"help")) {
            o->Action = ACTION_HELP;
        } else if (tu_eq(name, L"enable-feature")) {
            if (value) {
                /* The form of fondue: the user is asked. */
                o->Action = ACTION_ENABLE;
                o->Ask = TRUE;
                lstrcpynW(o->Name, value, RTL_NUMBER_OF(o->Name));
            } else {
                dism_enable = TRUE;
            }
        } else if (tu_eq(name, L"disable-feature")) {
            dism_disable = TRUE;
            if (value)
                lstrcpynW(o->Name, value, RTL_NUMBER_OF(o->Name));
        } else if (tu_eq(name, L"get-features")) {
            dism_list = TRUE;
        } else if (tu_eq(name, L"get-featureinfo")) {
            dism_info = TRUE;
        } else if (tu_eq(name, L"enable-font")) {
            if (!value)
                return FALSE;
            o->Action = ACTION_FONT;
            o->Ask = TRUE;
            lstrcpynW(o->Name, value, RTL_NUMBER_OF(o->Name));
        } else if (tu_eq(name, L"featurename")) {
            if (!value)
                return FALSE;
            lstrcpynW(o->Name, value, RTL_NUMBER_OF(o->Name));
        } else if (tu_eq(name, L"source")) {
            if (value)
                lstrcpynW(o->Source, value, RTL_NUMBER_OF(o->Source));
            else if (i + 1 < tu_argc)
                lstrcpynW(o->Source, tu_argv[++i], RTL_NUMBER_OF(o->Source));
            else
                return FALSE;
        } else if (tu_eq(name, L"caller-name")) {
            if (value)
                lstrcpynW(o->Caller, value, RTL_NUMBER_OF(o->Caller));
        } else if (tu_eq(name, L"caller-pid")) {
            for (; value && *value >= L'0' && *value <= L'9'; value++)
                o->CallerPid = o->CallerPid * 10 + (DWORD)(*value - L'0');
        } else if (tu_eq(name, L"hide-ux")) {
            o->HideUi = TRUE;
        } else if (tu_eq(name, L"elevated")) {
            o->Elevated = TRUE;
        } else if (tu_eq(name, L"ui")) {
            o->Ask = TRUE;
        } else if (tu_eq(name, L"online") || tu_eq(name, L"all") || tu_eq(name, L"norestart") || tu_eq(name, L"quiet") ||
                   tu_eq(name, L"limitaccess") || tu_eq(name, L"nologo") || tu_eq(name, L"loglevel") ||
                   tu_eq(name, L"logpath")) {
            /* Said by scripts written for dism, and meaningless here. */
        } else {
            return FALSE;
        }
    }
    if (o->Action == ACTION_NONE) {
        if (dism_enable)
            o->Action = ACTION_ENABLE;
        else if (dism_disable)
            o->Action = ACTION_DISABLE;
        else if (dism_list)
            o->Action = ACTION_LIST;
        else if (dism_info)
            o->Action = ACTION_STATUS;
    }
    if (o->Action == ACTION_NONE)
        o->Action = tu_argc > 1 ? ACTION_NONE : ACTION_HELP;
    return o->Action != ACTION_NONE;
}

/* ------------------------------------------------------------------ elevation */

static BOOL is_elevated(void)
{
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY_, &token))
        return FALSE;
    DWORD elevated = 0, size;
    BOOL ok = GetTokenInformation(token, TokenElevation_, &elevated, sizeof(elevated), &size);
    CloseHandle(token);
    return ok && elevated;
}

/* Starts this program again with the rights of an administrator and waits for it. */
static int run_elevated(void)
{
    WCHAR self[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return ERROR_PATH_NOT_FOUND;
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    LAUNCH_ELEVATED launch = advapi32 ? (LAUNCH_ELEVATED)(void *)GetProcAddress(advapi32, "LaunchElevated") : nullptr;
    if (!launch)
        return ERROR_NOT_SUPPORTED;
    TU_STR command = {};
    tu_str_format(&command, L"\"%ls\" %ls /elevated", self, tu_tail);
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    /* a request of a program comes without a console, and the second run goes without one too. */
    DWORD flags = GetConsoleWindow() ? 0 : DETACHED_PROCESS;
    DWORD error = launch(nullptr, tu_str_text(&command), nullptr, flags, &startup, &process, nullptr);
    tu_str_free(&command);
    if (error) {
        tu_eprint(L"features: administrator rights are needed and could not be had (error %lu).\n", error);
        return (int)error;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = ERROR_GEN_FAILURE;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return (int)code;
}

/* ------------------------------------------------------------------ the work */

static DWORD work(RUN *run)
{
    if (run->Remove)
        return FeatureDisable(&run->Ops, run->Feature);
    DWORD error = FeatureEnable(&run->Ops, run->Feature, run->EnableFlags);
    /* a request from the command line finishes what an older install of the feature left out, a program that waits does not. */
    if (!error && !(run->EnableFlags & FEATURE_ENABLE_ASK))
        HandlerComplete(run, run->Feature);
    return error;
}

static HANDLE lock_feature(const FEATURE_DEF *feature, BOOL say)
{
    WCHAR name[64], mutex_name[96];
    WidenName(feature->Name, name, RTL_NUMBER_OF(name));
    _snwprintf(mutex_name, RTL_NUMBER_OF(mutex_name), L"Global\\WindowsFeature.%ls", name);
    mutex_name[RTL_NUMBER_OF(mutex_name) - 1] = 0;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name);
    if (!mutex)
        return nullptr;
    if (WaitForSingleObject(mutex, 0) == WAIT_TIMEOUT) {
        if (say) {
            tu_print(L"Another request for this feature is running, waiting for it...\n");
            tu_flush();
        }
        WaitForSingleObject(mutex, INFINITE);
    }
    return mutex;
}

static int change(const OPTIONS *o, const FEATURE_DEF *feature)
{
    if (!is_elevated() && !o->Elevated)
        return run_elevated();
    RUN run;
    RunInit(&run, feature);
    BOOL remove = o->Action == ACTION_DISABLE;
    /* The user is asked when a program needs the feature, never for what they typed themselves. */
    BOOL dialogs = o->Ask && !o->HideUi && !remove;
    run.Dialogs = dialogs;
    run.Console = !dialogs && !o->HideUi && tu_is_console(TU_STDOUT);
    run.Remove = remove;
    lstrcpynW(run.Source, o->Source, RTL_NUMBER_OF(run.Source));
    lstrcpynW(run.Caller, o->Caller, RTL_NUMBER_OF(run.Caller));
    if (o->Font)
        lstrcpynW(run.Font, o->Name, RTL_NUMBER_OF(run.Font));
    run.CallerPid = o->CallerPid;
    for (WCHAR *p = run.Caller; *p; p++)
        if (*p == L'_')
            *p = L' ';
    run.EnableFlags = dialogs ? FEATURE_ENABLE_ASK : 0;
    if (o->Ask && !remove)
        RecordRequest(feature, run.Caller);
    FillOps(&run.Ops, &run);
    HANDLE mutex = lock_feature(feature, run.Console);
    DWORD error = UiRun(&run, work);
    if (!error && run.BarShown) {
        run.Finished = TRUE;
        ConsoleProgress(&run, TRUE);
    }
    ConsoleProgressEnd(&run);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    WCHAR display[96];
    WidenName(feature->DisplayName, display, RTL_NUMBER_OF(display));
    if (!error) {
        if (!dialogs && !o->HideUi)
            tu_print(remove ? L"%ls is turned off.\n" : L"%ls is turned on.\n", display);
    } else if (!dialogs || (error != FEATURE_ERROR_CANCELLED && error != FEATURE_ERROR_ABANDONED)) {
        WCHAR message[300];
        ErrorMessage(error, message, RTL_NUMBER_OF(message));
        tu_eprint(L"features: %ls could not be turned %ls: %ls (0x%08X)\n", display, remove ? L"off" : L"on", message, error);
        if (run.Detail[0])
            tu_eprint(L"features: %ls\n", run.Detail);
    }
    tu_flush();
    return run.Told ? MIME_FEATURE_FONT_MISSING : (int)error;
}

/* ------------------------------------------------------------------ what is there */

static PCWSTR state_text(RUN *run, const FEATURE_DEF *feature, FEATURE_STATE *recorded)
{
    DWORD error;
    FeatureLoadState(feature, recorded, &error);
    if (FeatureFilesPresent(run, feature))
        return L"Enabled";
    if (HandlerAvailable(feature) != ERROR_SUCCESS)
        return L"Unavailable";
    return *recorded == FEATURE_STATE_DISABLED ? L"Disabled" : L"Not installed";
}

static int list_features(void)
{
    static const TU_COLUMN columns[] = {{L"Feature", 0, FALSE}, {L"State", 0, FALSE}, {L"Version", 0, FALSE}, {L"Description", 0, FALSE}};
    TU_TABLE table;
    tu_table_init(&table, columns, 4);
    for (size_t i = 0; i < FeatureCount(); i++) {
        const FEATURE_DEF *feature = FeatureAt(i);
        RUN run;
        RunInit(&run, feature);
        FEATURE_STATE recorded;
        WCHAR name[64], display[96], version[64];
        WidenName(feature->Name, name, RTL_NUMBER_OF(name));
        WidenName(feature->DisplayName, display, RTL_NUMBER_OF(display));
        PCWSTR state = state_text(&run, feature, &recorded);
        FeatureInstalledVersion(&run, feature, version, RTL_NUMBER_OF(version));
        const WCHAR *cells[] = {name, state, version, display};
        tu_table_row(&table, cells);
    }
    tu_table_print(&table, TU_FMT_TABLE, FALSE);
    tu_table_free(&table);
    tu_print(L"\nA feature that is not installed is fetched from Microsoft when a program needs it.\n");
    return 0;
}

static int show_status(const FEATURE_DEF *feature)
{
    RUN run;
    RunInit(&run, feature);
    FEATURE_STATE recorded;
    DWORD error;
    WCHAR name[64], display[96], version[64], vendor[96];
    WidenName(feature->Name, name, RTL_NUMBER_OF(name));
    WidenName(feature->DisplayName, display, RTL_NUMBER_OF(display));
    WidenName(feature->Vendor, vendor, RTL_NUMBER_OF(vendor));
    PCWSTR state = state_text(&run, feature, &recorded);
    FeatureInstalledVersion(&run, feature, version, RTL_NUMBER_OF(version));
    FeatureLoadState(feature, &recorded, &error);
    tu_print(L"Feature name : %ls\nDisplay name : %ls\nVendor       : %ls\nVersion      : %ls\nState        : %ls\n", name, display,
             vendor, version, state);
    if (error) {
        WCHAR message[300];
        ErrorMessage(error, message, RTL_NUMBER_OF(message));
        tu_print(L"Last error   : %ls (0x%08X)\n", message, error);
    }
    DWORD requests = FeatureRequestCount(feature);
    if (requests)
        tu_print(L"Requests     : %lu\n", requests);
    return 0;
}

int tool_main(void)
{
    /* What this program looks into must not ask for a feature, it would ask itself. */
    SetEnvironmentVariableW(MIME_FEATURE_BUSY_VARIABLE, L"1");
    OPTIONS o = {};
    if (!parse(&o)) {
        tu_eprint(L"features: the command line is not right, `features help` tells how it is used.\n");
        return EXIT_BAD_PARAMETER;
    }
    if (o.Action == ACTION_HELP) {
        tu_puts(kUsage);
        return 0;
    }
    if (o.Action == ACTION_LIST)
        return list_features();
    char name[96];
    narrow(o.Name, name, sizeof(name));
    const FEATURE_DEF *feature = o.Action == ACTION_FONT ? FeatureFindProviding(name) : FeatureFind(name);
    if (!feature) {
        if (o.Action != ACTION_FONT || !o.HideUi)
            tu_eprint(o.Action == ACTION_FONT ? L"features: no feature has the font '%ls'.\n"
                                              : L"features: '%ls' is not a feature, `features list` shows them.\n",
                      o.Name);
        return FEATURE_ERROR_NOT_FOUND;
    }
    if (o.Action == ACTION_STATUS)
        return show_status(feature);
    if (o.Action == ACTION_FONT) {
        o.Action = ACTION_ENABLE;
        o.Font = TRUE;
    }
    return change(&o, feature);
}
