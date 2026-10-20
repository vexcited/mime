/*
 * regsvr32.exe, which registers a library by calling its DllRegisterServer or DllUnregisterServer, or its DllInstall.
 * The exit code says how far it got: 1 for a line it cannot read, 2 when OLE cannot start, 3 when the library does not
 * load, 4 when it has no such function and 5 when the function fails.
 */
#include <mime/win32/ole2.h>
#include <mime/win32/winuser.h>
#include <rtl.h>

#define EXIT_BAD_LINE 1
#define EXIT_OLE 2
#define EXIT_LOAD 3
#define EXIT_NO_FUNCTION 4
#define EXIT_FAILED 5

typedef HRESULT(WINAPI *DLLFUNCTION)(void);
typedef HRESULT(WINAPI *DLLINSTALL)(BOOL install, PCWSTR command);

typedef struct SETTINGS {
    BOOL Unregister, Silent, Install, NoRegister, NoErrors;
    PCWSTR InstallLine;
    PCWSTR Library;
} SETTINGS;

static PCWSTR next_word(PCWSTR line, PWSTR word, SIZE_T capacity)
{
    while (*line == L' ' || *line == L'\t')
        line++;
    SIZE_T length = 0;
    BOOL quoted = FALSE;
    while (*line && (quoted || (*line != L' ' && *line != L'\t'))) {
        if (*line == L'"') {
            quoted = !quoted;
            line++;
            continue;
        }
        if (length + 1 < capacity)
            word[length++] = *line;
        line++;
    }
    word[length] = 0;
    return line;
}

static BOOL parse(SETTINGS *settings, PCWSTR line, PWSTR word, SIZE_T capacity, PWSTR install_line)
{
    /* the program name is the first word. */
    line = next_word(line, word, capacity);
    for (;;) {
        line = next_word(line, word, capacity);
        if (!word[0])
            break;
        if (word[0] != L'/' && word[0] != L'-') {
            /* the first name is the library, any other word is not understood. */
            if (settings->Library)
                return FALSE;
            wcscpy(install_line + 1024, word);
            settings->Library = install_line + 1024;
            continue;
        }
        switch (word[1] | 0x20) {
        case 'u':
            settings->Unregister = TRUE;
            break;
        case 's':
            settings->Silent = TRUE;
            break;
        case 'c':
            break;
        case 'n':
            settings->NoRegister = TRUE;
            break;
        case 'e':
            settings->NoErrors = TRUE;
            break;
        case 'i':
            settings->Install = TRUE;
            if (word[2] == L':') {
                wcscpy(install_line, word + 3);
                settings->InstallLine = install_line;
            }
            break;
        default:
            return FALSE;
        }
    }
    return settings->Library != nullptr && (!settings->NoRegister || settings->Install);
}

static void say(const SETTINGS *settings, UINT flags, PCWSTR text)
{
    if (settings->Silent || (flags == MB_ICONERROR && settings->NoErrors))
        return;
    MessageBoxW(nullptr, text, L"RegSvr32", flags);
}

static int call(const SETTINGS *settings, HMODULE module, PCSTR name, BOOL install)
{
    WCHAR wide[32], text[1200];
    for (int i = 0;; i++) {
        wide[i] = (WCHAR)name[i];
        if (!name[i])
            break;
    }
    FARPROC function = GetProcAddress(module, name);
    if (!function) {
        _snwprintf(text, RTL_NUMBER_OF(text), L"The module \"%s\" was loaded but the entry-point %s was not found.",
                   settings->Library, wide);
        text[RTL_NUMBER_OF(text) - 1] = 0;
        say(settings, MB_ICONERROR, text);
        return EXIT_NO_FUNCTION;
    }
    HRESULT result = install ? ((DLLINSTALL)function)(!settings->Unregister, settings->InstallLine)
                             : ((DLLFUNCTION)function)();
    if (FAILED(result)) {
        _snwprintf(text, RTL_NUMBER_OF(text), L"The module \"%s\" was loaded but the call to %s failed with error code 0x%08x.",
                   settings->Library, wide, (unsigned)result);
        text[RTL_NUMBER_OF(text) - 1] = 0;
        say(settings, MB_ICONERROR, text);
        return EXIT_FAILED;
    }
    _snwprintf(text, RTL_NUMBER_OF(text), L"%s in %s succeeded.", wide, settings->Library);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    say(settings, MB_ICONINFORMATION, text);
    return 0;
}

void entry(void)
{
    SETTINGS settings = {};
    WCHAR word[1024], storage[2048];
    storage[0] = 0;
    if (!parse(&settings, GetCommandLineW(), word, RTL_NUMBER_OF(word), storage)) {
        say(&settings, MB_ICONINFORMATION,
            L"Usage: regsvr32 [/u] [/s] [/n] [/i[:cmdline]] dllname\n\n/u - Unregister server\n/s - Silent; display no message boxes\n"
            L"/i - Call DllInstall passing it an optional [cmdline]; when used with /u calls dll uninstall\n"
            L"/n - do not call DllRegisterServer; this option must be used with /i");
        ExitProcess(EXIT_BAD_LINE);
    }
    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) {
        WCHAR text[80];
        _snwprintf(text, RTL_NUMBER_OF(text), L"OleInitialize failed with error code 0x%08x", (unsigned)ole);
        text[RTL_NUMBER_OF(text) - 1] = 0;
        say(&settings, MB_ICONERROR, text);
        ExitProcess(EXIT_OLE);
    }
    HMODULE module = LoadLibraryExW(settings.Library, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    int code = 0;
    if (!module) {
        WCHAR text[1200];
        _snwprintf(text, RTL_NUMBER_OF(text), L"The module \"%s\" failed to load.", settings.Library);
        text[RTL_NUMBER_OF(text) - 1] = 0;
        say(&settings, MB_ICONERROR, text);
        code = EXIT_LOAD;
    } else {
        if (!settings.NoRegister)
            code = call(&settings, module, settings.Unregister ? "DllUnregisterServer" : "DllRegisterServer", FALSE);
        if (!code && settings.Install)
            code = call(&settings, module, "DllInstall", TRUE);
        FreeLibrary(module);
    }
    OleUninitialize();
    ExitProcess(code);
}
