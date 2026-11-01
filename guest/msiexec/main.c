/*
 * msiexec.exe, the Windows Installer command line: parses the switches, calls msi.dll and exits with the result. the
 * 32-bit copy in SysWOW64 also hosts a package's 32-bit custom actions, see remote.c in msi.dll.
 */
#include <mime/win32/msi.h>
#include <mime/win32/winuser.h>
#include <rtl.h>

#ifndef ADVERTISEFLAGS_MACHINEASSIGN
MSIAPI UINT WINAPI MsiAdvertiseProductExW(LPCWSTR package, LPCWSTR script, LPCWSTR transforms, LANGID language, DWORD platform,
                                          DWORD options);
#endif

#define ERROR_SUCCESS_REBOOT_REQUIRED 3010L
#define ERROR_SUCCESS_REBOOT_INITIATED 1641L

MSIAPI UINT WINAPI MsiCustomActionServer(LPCWSTR pipe);

enum { ACTION_NONE, ACTION_INSTALL, ACTION_REMOVE, ACTION_REPAIR, ACTION_ADMIN, ACTION_ADVERTISE, ACTION_PATCH,
       ACTION_SERVER, ACTION_HELP, ACTION_REGISTER };

typedef struct COMMAND {
    int Action;
    PWSTR Target;         /* the package or product code. */
    PWSTR Properties;     /* PROPERTY=value words as typed. */
    SIZE_T PropertiesLength;
    PWSTR RepairOptions;  /* the letters after /f. */
    PWSTR LogPath;
    DWORD LogMode;
    DWORD LogAttributes;
    INSTALLUILEVEL Level;
    BOOL LevelSet;
    PWSTR Reboot;         /* the REBOOT value a switch asked for. */
    PWSTR PatchPath;
    PWSTR Pipe;
    PWSTR Transforms;     /* the list /t gives an advertisement. */
    PWSTR AdvertiseFor;   /* the letter after /j, who the product is advertised to. */
    int Language;
    BOOL NewInstance;
    BOOL Bad;
} COMMAND;

static void *allocate(SIZE_T bytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

/* the next word of the line without its quotes, and where it started. */
static PCWSTR next_word(PCWSTR line, PWSTR word, SIZE_T capacity, PCWSTR *start)
{
    while (*line == L' ' || *line == L'\t')
        line++;
    *start = line;
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

static BOOL match(PCWSTR word, PCWSTR name)
{
    return !_wcsicmp(word, name);
}

static BOOL starts(PCWSTR word, PCWSTR prefix)
{
    return !_wcsnicmp(word, prefix, wcslen(prefix));
}

static void parse_level(COMMAND *command, PCWSTR text)
{
    INSTALLUILEVEL level = INSTALLUILEVEL_NONE;
    command->LevelSet = TRUE;
    PCWSTR p = text;
    if (!*p || !_wcsicmp(p, L"n")) {
        command->Level = INSTALLUILEVEL_NONE;
        return;
    }
    switch (*p) {
    case L'n': case L'N': level = INSTALLUILEVEL_NONE; break;
    case L'b': case L'B': level = INSTALLUILEVEL_BASIC; break;
    case L'r': case L'R': level = INSTALLUILEVEL_REDUCED; break;
    case L'f': case L'F': level = INSTALLUILEVEL_FULL; break;
    default: break;
    }
    for (p++; *p; p++) {
        if (*p == L'+')
            level |= INSTALLUILEVEL_ENDDIALOG;
        else if (*p == L'-')
            level |= INSTALLUILEVEL_PROGRESSONLY;
        else if (*p == L'!')
            level |= INSTALLUILEVEL_HIDECANCEL;
    }
    command->Level = level;
}

static DWORD log_mode(PCWSTR text, DWORD *attributes)
{
    DWORD mode = 0;
    *attributes = 0;
    for (; *text; text++) {
        switch (*text) {
        case L'i': case L'I': mode |= 0x10; break;       /* INSTALLLOGMODE_INFO */
        case L'w': case L'W': mode |= 0x04; break;       /* WARNING */
        case L'e': case L'E': mode |= 0x02; break;       /* ERROR */
        case L'a': case L'A': mode |= 0x20; break;       /* ACTIONSTART */
        case L'r': case L'R': mode |= 0x40; break;       /* ACTIONDATA */
        case L'u': case L'U': mode |= 0x08; break;       /* USER */
        case L'c': case L'C': mode |= 0x80; break;       /* COMMONDATA */
        case L'm': case L'M': mode |= 0x01; break;       /* FATALEXIT */
        case L'o': case L'O': mode |= 0x800; break;      /* OUTOFDISKSPACE */
        case L'p': case L'P': mode |= 0x400; break;      /* PROPERTYDUMP */
        case L'v': case L'V': mode |= 0x1000; break;     /* VERBOSE */
        case L'x': case L'X': mode |= 0x2000; break;     /* EXTRADEBUG */
        case L'+': *attributes |= INSTALLLOGATTRIBUTES_APPEND; break;
        case L'!': *attributes |= INSTALLLOGATTRIBUTES_FLUSHEACHLINE; break;
        case L'*': mode |= 0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40 | 0x80 | 0x800 | 0x400; break;
        default: break;
        }
    }
    return mode;
}

static BOOL add_property(COMMAND *command, PCWSTR raw, SIZE_T length)
{
    PWSTR bigger = allocate((command->PropertiesLength + length + 2) * sizeof(WCHAR));
    if (!bigger)
        return FALSE;
    if (command->Properties)
        memcpy(bigger, command->Properties, command->PropertiesLength * sizeof(WCHAR));
    if (command->PropertiesLength)
        bigger[command->PropertiesLength++] = L' ';
    memcpy(bigger + command->PropertiesLength, raw, length * sizeof(WCHAR));
    command->PropertiesLength += length;
    bigger[command->PropertiesLength] = 0;
    HeapFree(GetProcessHeap(), 0, command->Properties);
    command->Properties = bigger;
    return TRUE;
}

static PWSTR copy_text(PCWSTR text)
{
    PWSTR copy = allocate((wcslen(text) + 1) * sizeof(WCHAR));
    if (copy)
        wcscpy(copy, text);
    return copy;
}

/* FALSE when the line cannot be parsed. */
static BOOL parse(COMMAND *command, PCWSTR line)
{
    WCHAR word[2048], argument[2048];
    PCWSTR start;
    line = next_word(line, word, RTL_NUMBER_OF(word), &start);
    for (;;) {
        PCWSTR end = next_word(line, word, RTL_NUMBER_OF(word), &start);
        if (end == start)
            break;
        line = end;
        if (word[0] != L'/' && word[0] != L'-') {
            /* NAME=value words are properties, any other word is the package. */
            if (wcschr(word, L'=')) {
                if (!add_property(command, start, (SIZE_T)(end - start)))
                    return FALSE;
            } else if (!command->Target) {
                command->Target = copy_text(word);
                if (command->Action == ACTION_NONE)
                    command->Action = ACTION_INSTALL;
            } else
                return FALSE;
            continue;
        }
        PCWSTR option = word + 1;
        BOOL takes_target = FALSE;
        if (match(option, L"?") || match(option, L"h") || match(option, L"help")) {
            command->Action = ACTION_HELP;
        } else if (match(option, L"n") && command->Action != ACTION_ADVERTISE) {
            /* /n names the product for /i, ignored since a package path carries its own code. */
            line = next_word(line, argument, RTL_NUMBER_OF(argument), &start);
        } else if ((match(option, L"n") || match(option, L"c")) && command->Action == ACTION_ADVERTISE) {
            /* an advertisement of a new instance, which has its own transform. */
            command->NewInstance = TRUE;
        } else if (match(option, L"t")) {
            line = next_word(line, argument, RTL_NUMBER_OF(argument), &start);
            command->Transforms = copy_text(argument);
        } else if (match(option, L"g")) {
            line = next_word(line, argument, RTL_NUMBER_OF(argument), &start);
            command->Language = 0;
            for (PCWSTR digit = argument; *digit >= L'0' && *digit <= L'9'; digit++)
                command->Language = command->Language * 10 + (*digit - L'0');
        } else if (match(option, L"i") || match(option, L"package")) {
            command->Action = ACTION_INSTALL;
            takes_target = TRUE;
        } else if (match(option, L"x") || match(option, L"uninstall")) {
            command->Action = ACTION_REMOVE;
            takes_target = TRUE;
        } else if (starts(option, L"f") && !starts(option, L"forcerestart")) {
            command->Action = ACTION_REPAIR;
            command->RepairOptions = copy_text(option + 1);
            takes_target = TRUE;
        } else if (match(option, L"a")) {
            command->Action = ACTION_ADMIN;
            takes_target = TRUE;
        } else if (starts(option, L"j")) {
            command->Action = ACTION_ADVERTISE;
            command->AdvertiseFor = copy_text(option + 1);
            takes_target = TRUE;
        } else if (match(option, L"p") || match(option, L"update")) {
            /* a patch of an administrative image stays an administrative installation. */
            if (command->Action != ACTION_ADMIN)
                command->Action = ACTION_PATCH;
            line = next_word(line, argument, RTL_NUMBER_OF(argument), &start);
            command->PatchPath = copy_text(argument);
        } else if (match(option, L"quiet")) {
            command->Level = INSTALLUILEVEL_NONE;
            command->LevelSet = TRUE;
        } else if (match(option, L"passive")) {
            command->Level = INSTALLUILEVEL_BASIC | INSTALLUILEVEL_HIDECANCEL;
            command->LevelSet = TRUE;
        } else if (starts(option, L"q") && !starts(option, L"quiet")) {
            parse_level(command, option + 1);
        } else if (match(option, L"norestart")) {
            command->Reboot = L"ReallySuppress";
        } else if (match(option, L"forcerestart")) {
            command->Reboot = L"Force";
        } else if (match(option, L"promptrestart")) {
            command->Reboot = L"";
        } else if (match(option, L"l") || starts(option, L"l") || match(option, L"log")) {
            PCWSTR modes = match(option, L"log") ? L"" : option + 1;
            command->LogMode = log_mode(modes, &command->LogAttributes);
            if (!command->LogMode)
                command->LogMode = log_mode(L"iwearucmop", &command->LogAttributes);
            line = next_word(line, argument, RTL_NUMBER_OF(argument), &start);
            command->LogPath = copy_text(argument);
        } else if (match(option, L"ca-server")) {
            command->Action = ACTION_SERVER;
            line = next_word(line, argument, RTL_NUMBER_OF(argument), &start);
            command->Pipe = copy_text(argument);
        } else if (match(option, L"regserver") || match(option, L"unregserver") || match(option, L"service") ||
                   match(option, L"y") || match(option, L"z") || match(option, L"embedding") || match(option, L"v") ||
                   match(option, L"m") || match(option, L"c") || match(option, L"w") || match(option, L"d")) {
            command->Action = ACTION_REGISTER;
        } else {
            /* a bad switch, but the rest is still read for the UI level it asks for. */
            command->Bad = TRUE;
        }
        if (takes_target) {
            line = next_word(line, argument, RTL_NUMBER_OF(argument), &start);
            if (!*argument)
                return FALSE;
            command->Target = copy_text(argument);
        }
    }
    return TRUE;
}

static BOOL looks_like_product(PCWSTR text)
{
    return text[0] == L'{' && wcslen(text) == 38 && text[37] == L'}';
}

/* reads a package's product code without consulting the machine. */
static BOOL product_of_package(PCWSTR path, WCHAR code[39])
{
    MSIHANDLE package;
    if (MsiOpenPackageExW(path, MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE, &package))
        return FALSE;
    DWORD size = 39;
    BOOL ok = !MsiGetPropertyW(package, L"ProductCode", code, &size);
    MsiCloseHandle(package);
    return ok;
}

static void show_help(INSTALLUILEVEL level)
{
    if ((level & 0x1f) == INSTALLUILEVEL_NONE && level != INSTALLUILEVEL_DEFAULT)
        return;
    static const WCHAR text[] =
        L"Windows Installer\n\n"
        L"msiexec /i <package|ProductCode> [PROPERTY=value] install or configure a product\n"
        L"msiexec /x <package|ProductCode>                  uninstall a product\n"
        L"msiexec /a <package> [TARGETDIR=dir] [/p patch]     make or patch an administrative image\n"
        L"msiexec /f[p|o|e|d|c|a|u|m|s|v] <package|ProductCode>  repair a product\n"
        L"msiexec /q[n|b|r|f]   user interface level: none, basic, reduced, full\n"
        L"msiexec /quiet /passive /norestart /forcerestart /promptrestart\n"
        L"msiexec /l[i|w|e|a|r|u|c|m|o|p|v|x|+|!|*] <logfile>   write a log\n";
    MessageBoxW(nullptr, text, L"Windows Installer", MB_OK);
}

static DWORD repair_mode(PCWSTR letters)
{
    DWORD mode = 0;
    if (!*letters)
        letters = L"omus";
    for (; *letters; letters++) {
        switch (*letters) {
        case L'p': case L'P': mode |= REINSTALLMODE_FILEMISSING; break;
        case L'o': case L'O': mode |= REINSTALLMODE_FILEOLDERVERSION; break;
        case L'e': case L'E': mode |= REINSTALLMODE_FILEEQUALVERSION; break;
        case L'd': case L'D': mode |= REINSTALLMODE_FILEEXACT; break;
        case L'c': case L'C': mode |= REINSTALLMODE_FILEVERIFY; break;
        case L'a': case L'A': mode |= REINSTALLMODE_FILEREPLACE; break;
        case L'u': case L'U': mode |= REINSTALLMODE_USERDATA; break;
        case L'm': case L'M': mode |= REINSTALLMODE_MACHINEDATA; break;
        case L's': case L'S': mode |= REINSTALLMODE_SHORTCUT; break;
        case L'v': case L'V': mode |= REINSTALLMODE_PACKAGE; break;
        default: return UINT32_MAX;
        }
    }
    return mode;
}

static PWSTR combine(PCWSTR properties, PCWSTR extra)
{
    SIZE_T first = properties ? wcslen(properties) : 0, second = extra ? wcslen(extra) : 0;
    PWSTR text = allocate((first + second + 2) * sizeof(WCHAR));
    if (!text)
        return nullptr;
    if (first)
        memcpy(text, properties, first * sizeof(WCHAR));
    if (first && second)
        text[first++] = L' ';
    if (second)
        memcpy(text + first, extra, second * sizeof(WCHAR));
    return text;
}

static UINT run(COMMAND *command)
{
    if (command->Level != INSTALLUILEVEL_NOCHANGE || command->LevelSet)
        MsiSetInternalUI(command->LevelSet ? command->Level : INSTALLUILEVEL_FULL, nullptr);
    else if (command->Action == ACTION_ADVERTISE)
        MsiSetInternalUI(INSTALLUILEVEL_BASIC, nullptr);
    if (command->LogPath)
        MsiEnableLogW(command->LogMode, command->LogPath, command->LogAttributes);
    WCHAR reboot[64] = {};
    if (command->Reboot && *command->Reboot)
        _snwprintf(reboot, RTL_NUMBER_OF(reboot), L"REBOOT=%s", command->Reboot);
    PWSTR properties = combine(command->Properties, reboot);
    if (!properties)
        return ERROR_NOT_ENOUGH_MEMORY;
    UINT error;
    switch (command->Action) {
    case ACTION_INSTALL:
        if (looks_like_product(command->Target)) {
            /* an installed product: configure it with the given properties, keeping its features. */
            error = MsiConfigureProductExW(command->Target, 0, INSTALLSTATE_DEFAULT, properties);
        } else
            error = MsiInstallProductW(command->Target, properties);
        break;
    case ACTION_REMOVE: {
        WCHAR code[39];
        PCWSTR product = command->Target;
        if (!looks_like_product(product)) {
            if (!product_of_package(command->Target, code)) {
                error = ERROR_INSTALL_PACKAGE_OPEN_FAILED;
                break;
            }
            product = code;
        }
        error = MsiConfigureProductExW(product, 0, INSTALLSTATE_ABSENT, properties);
        break;
    }
    case ACTION_REPAIR: {
        DWORD mode = repair_mode(command->RepairOptions ? command->RepairOptions : L"");
        if (mode == UINT32_MAX) {
            error = ERROR_INVALID_COMMAND_LINE;
            break;
        }
        if (looks_like_product(command->Target)) {
            error = MsiReinstallProductW(command->Target, mode);
        } else {
            WCHAR letters[16] = {};
            SIZE_T n = 0;
            if (mode & REINSTALLMODE_FILEMISSING) letters[n++] = L'p';
            if (mode & REINSTALLMODE_FILEOLDERVERSION) letters[n++] = L'o';
            if (mode & REINSTALLMODE_FILEEQUALVERSION) letters[n++] = L'e';
            if (mode & REINSTALLMODE_FILEEXACT) letters[n++] = L'd';
            if (mode & REINSTALLMODE_FILEVERIFY) letters[n++] = L'c';
            if (mode & REINSTALLMODE_FILEREPLACE) letters[n++] = L'a';
            if (mode & REINSTALLMODE_USERDATA) letters[n++] = L'u';
            if (mode & REINSTALLMODE_MACHINEDATA) letters[n++] = L'm';
            if (mode & REINSTALLMODE_SHORTCUT) letters[n++] = L's';
            if (mode & REINSTALLMODE_PACKAGE) letters[n++] = L'v';
            WCHAR extra[96];
            _snwprintf(extra, RTL_NUMBER_OF(extra), L"REINSTALL=ALL REINSTALLMODE=%s", letters);
            PWSTR all = combine(properties, extra);
            error = all ? MsiInstallProductW(command->Target, all) : ERROR_NOT_ENOUGH_MEMORY;
            HeapFree(GetProcessHeap(), 0, all);
        }
        break;
    }
    case ACTION_PATCH:
        error = MsiApplyPatchW(command->PatchPath, command->Target, INSTALLTYPE_DEFAULT, properties);
        break;
    case ACTION_ADMIN:
        if (command->PatchPath) {
            error = MsiApplyPatchW(command->PatchPath, command->Target, INSTALLTYPE_NETWORK_IMAGE, properties);
        } else {
            PWSTR all = combine(properties, L"ACTION=ADMIN");
            error = all ? MsiInstallProductW(command->Target, all) : ERROR_NOT_ENOUGH_MEMORY;
            HeapFree(GetProcessHeap(), 0, all);
        }
        break;
    case ACTION_ADVERTISE: {
        /* a letter says who: u for the user, m or none for everyone. */
        PCWSTR who = command->AdvertiseFor ? command->AdvertiseFor : L"";
        if (*who && (who[1] || ((*who | 0x20) != L'u' && (*who | 0x20) != L'm'))) {
            error = ERROR_INVALID_PARAMETER;
            break;
        }
        LPCWSTR context = (LPCWSTR)(UINT_PTR)((*who | 0x20) == L'u');
        error = MsiAdvertiseProductExW(command->Target, context, command->Transforms, (LANGID)command->Language, 0,
                                       command->NewInstance);
        /* a package named without its extension is looked for with one. */
        if (error == ERROR_FILE_NOT_FOUND && !wcschr(command->Target, L'.')) {
            SIZE_T length = wcslen(command->Target);
            PWSTR named = allocate((length + 5) * sizeof(WCHAR));
            if (named) {
                memcpy(named, command->Target, length * sizeof(WCHAR));
                wcscpy(named + length, L".msi");
                error = MsiAdvertiseProductExW(named, context, command->Transforms, (LANGID)command->Language, 0,
                                               command->NewInstance);
                HeapFree(GetProcessHeap(), 0, named);
            }
        }
        break;
    }
    default:
        error = ERROR_INVALID_COMMAND_LINE;
        break;
    }
    HeapFree(GetProcessHeap(), 0, properties);
    return error;
}

void entry(void)
{
    COMMAND command = {.Level = INSTALLUILEVEL_NOCHANGE};
    UINT status;
    if (!parse(&command, GetCommandLineW()) || command.Bad) {
        show_help(command.LevelSet ? command.Level : INSTALLUILEVEL_FULL);
        ExitProcess(ERROR_INVALID_COMMAND_LINE);
    }
    if (command.Action == ACTION_SERVER) {
        status = command.Pipe ? MsiCustomActionServer(command.Pipe) : ERROR_INVALID_COMMAND_LINE;
        ExitProcess(status);
    }
    if (command.Action == ACTION_HELP || command.Action == ACTION_NONE ||
        (command.Action != ACTION_REGISTER && command.Action != ACTION_PATCH && !command.Target)) {
        show_help(command.LevelSet ? command.Level : INSTALLUILEVEL_FULL);
        ExitProcess(command.Action == ACTION_HELP ? ERROR_SUCCESS : ERROR_INVALID_COMMAND_LINE);
    }
    if (command.Action == ACTION_REGISTER)
        ExitProcess(ERROR_SUCCESS);
    status = run(&command);
    /* a package that wants a restart returns a success status saying so. */
    ExitProcess(status);
}
