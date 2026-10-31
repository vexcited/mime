/*
 * regsvr32.exe: calls a library's DllRegisterServer, DllUnregisterServer or DllInstall.
 * exit codes: 1 bad command line, 2 OLE fails to start, 3 the library does not load,
 * 4 the function is missing, 5 the function fails.
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
    /* the program name ends at its closing quote or at the first blank, so a line that starts with one has none. */
    if (*line == L'"') {
        line = wcschr(line + 1, L'"');
        line = line ? line + 1 : L"";
    } else
        while (*line && *line != L' ' && *line != L'\t')
            line++;
    for (;;) {
        line = next_word(line, word, capacity);
        if (!word[0])
            break;
        if (word[0] != L'/' && word[0] != L'-') {
            /* the first name is the library, any other word is an error. */
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

/* the machine of a library from its headers, 0 when it is no image. */
static WORD image_machine(PCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    IMAGE_DOS_HEADER dos;
    DWORD read;
    WORD machine = 0;
    BYTE nt[6];
    if (ReadFile(file, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(file, nt, sizeof(nt), &read, nullptr) && read == sizeof(nt))
        machine = (WORD)(nt[4] | nt[5] << 8);
    CloseHandle(file);
    return machine;
}

/* a library of the other processor is registered by the regsvr32 of that processor with the same arguments, whose
   exit code this one gives. -1 when there is none to hand it to. */
static DWORD hand_over(PCWSTR library)
{
    PWSTR arguments = GetCommandLineW();
    while (*arguments && *arguments != L' ')
        arguments = CharNextW(arguments);
    WORD machine = image_machine(library);
    USHORT process = 0, native = 0;
    if (!*arguments || !machine || !IsWow64Process2(GetCurrentProcess(), &process, &native))
        return (DWORD)-1;
    WCHAR program[MAX_PATH + 16];
    UINT length;
    if (machine == native) {
        if (process == IMAGE_FILE_MACHINE_UNKNOWN)
            return (DWORD)-1;
        length = GetSystemDirectoryW(program, MAX_PATH);
    } else if (machine == IMAGE_FILE_MACHINE_I386 && process == IMAGE_FILE_MACHINE_UNKNOWN)
        length = GetSystemWow64Directory2W(program, MAX_PATH, machine);
    else
        return (DWORD)-1;
    if (!length || length >= MAX_PATH)
        return (DWORD)-1;
    wcscpy(program + length, L"\\regsvr32.exe");
    PVOID redirection = nullptr;
    BOOL redirected = Wow64DisableWow64FsRedirection(&redirection);
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION child;
    BOOL started = CreateProcessW(program, arguments, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &child);
    if (redirected)
        Wow64RevertWow64FsRedirection(redirection);
    if (!started)
        return (DWORD)-1;
    DWORD code = (DWORD)-1;
    if (WaitForSingleObject(child.hProcess, INFINITE) || !GetExitCodeProcess(child.hProcess, &code))
        code = (DWORD)-1;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return code;
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
    DWORD handed = !module && GetLastError() == ERROR_BAD_EXE_FORMAT ? hand_over(settings.Library) : (DWORD)-1;
    if (handed != (DWORD)-1) {
        OleUninitialize();
        ExitProcess(handed);
    }
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
