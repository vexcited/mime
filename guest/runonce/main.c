#include <mime/win32/winbase.h>
#include <rtl.h>

#define RUN_ONCE L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"
#define ENTRIES_MAX 64

typedef struct ENTRY {
    WCHAR Name[260];
    WCHAR Command[2048];
} ENTRY;

static ENTRY g_entries[ENTRIES_MAX];

static void run(PCWSTR command)
{
    static WCHAR line[4096];
    if (!ExpandEnvironmentStringsW(command, line, RTL_NUMBER_OF(line)))
        return;
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        return;
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

/*
 * the values are read before any runs, as a command may add one for the restart after. each is deleted before its
 * command runs so a command that fails does not run again, but a name that starts with ! is deleted once it is done.
 */
static void run_key(HKEY root, REGSAM view)
{
    HKEY key;
    if (RegOpenKeyExW(root, RUN_ONCE, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | view, &key))
        return;
    DWORD count = 0;
    for (DWORD i = 0; count < ENTRIES_MAX; i++) {
        ENTRY *entry = &g_entries[count];
        DWORD name = RTL_NUMBER_OF(entry->Name), type = 0, size = sizeof(entry->Command) - sizeof(WCHAR);
        LSTATUS error = RegEnumValueW(key, i, entry->Name, &name, nullptr, &type, (LPBYTE)entry->Command, &size);
        if (error == ERROR_NO_MORE_ITEMS)
            break;
        if (error || !name || (type != REG_SZ && type != REG_EXPAND_SZ))
            continue;
        entry->Command[size / sizeof(WCHAR)] = 0;
        count++;
    }
    for (DWORD i = 0; i < count; i++) {
        BOOL after = g_entries[i].Name[0] == L'!';
        if (!after)
            RegDeleteValueW(key, g_entries[i].Name);
        run(g_entries[i].Command);
        if (after)
            RegDeleteValueW(key, g_entries[i].Name);
    }
    RegCloseKey(key);
}

static BOOL has_word(PCWSTR line, PCWSTR word)
{
    SIZE_T length = wcslen(word);
    for (; *line; line++)
        if (!_wcsnicmp(line, word, length) && (!line[length] || line[length] == L' '))
            return TRUE;
    return FALSE;
}

void entry(void)
{
    PCWSTR line = GetCommandLineW();
    if (has_word(line, L"/Machine")) {
        run_key(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
        run_key(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
    }
    if (has_word(line, L"/User"))
        run_key(HKEY_CURRENT_USER, 0);
    ExitProcess(0);
}
