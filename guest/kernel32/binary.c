/*
 * what kind of program a file is, and the request to put the system to sleep. The kinds are those of GetBinaryType:
 * 32 bit, DOS, 16 bit windows, PIF, POSIX, 64 bit. Windows asks the system to map the file as an image and reads its
 * kind from the section; here the headers of the file are read, which gives the same kinds for the same files.
 */
#include <mime/win32/winbase.h>
#include <mime/ntapi.h>

#define SCS_32BIT_BINARY_ 0
#define SCS_DOS_BINARY_ 1
#define SCS_WOW_BINARY_ 2
#define SCS_PIF_BINARY_ 3
#define SCS_POSIX_BINARY_ 4
#define SCS_64BIT_BINARY_ 6
#define MACHINE_I386 0x14C
#define MACHINE_AMD64 0x8664
#define MACHINE_ARM64 0xAA64
#define SUBSYSTEM_POSIX_CUI 7
#define CHARACTERISTIC_DLL 0x2000
#define ERROR_BAD_EXE_FORMAT_ 193

static BOOL read_at(HANDLE file, DWORD offset, void *buffer, DWORD size)
{
    DWORD got = 0;
    return SetFilePointer(file, (LONG)offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER && ReadFile(file, buffer, size, &got, nullptr) &&
           got == size;
}

/* a file that is no executable image may still be a DOS program or a PIF by its name, as windows decides. */
static int kind_from_name(LPCWSTR name)
{
    SIZE_T n = lstrlenW(name);
    if (n < 4)
        return -1;
    LPCWSTR end = name + n - 4;
    if (!lstrcmpiW(end, L".exe") || !lstrcmpiW(end, L".com"))
        return SCS_DOS_BINARY_;
    if (!lstrcmpiW(end, L".pif"))
        return SCS_PIF_BINARY_;
    return -1;
}

/*
 * the kind in *type. A file that cannot be opened keeps the error of the open; a dll, a machine that is not a
 * processor of Windows and a file that is no program are ERROR_BAD_EXE_FORMAT; a name that is none is
 * ERROR_INVALID_NAME.
 */
DECLSPEC_EXPORT BOOL WINAPI GetBinaryTypeW(LPCWSTR name, LPDWORD type)
{
    if (!name || !type) {
        SetLastError(!name ? ERROR_INVALID_NAME : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD result = SCS_DOS_BINARY_;
    BOOL ok = TRUE;
    BYTE dos[0x40];
    if (!read_at(file, 0, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z') {
        int kind = kind_from_name(name);
        if (kind < 0) {
            SetLastError(ERROR_BAD_EXE_FORMAT_);
            ok = FALSE;
        } else {
            result = (DWORD)kind;
        }
    } else {
        DWORD next = *(DWORD *)(dos + 0x3C);
        BYTE header[0x20 + 0x60];
        if (next >= sizeof(dos) && read_at(file, next, header, sizeof(header))) {
            if (header[0] == 'P' && header[1] == 'E' && !header[2] && !header[3]) {
                WORD machine = *(WORD *)(header + 4), characteristics = *(WORD *)(header + 22);
                WORD subsystem = *(WORD *)(header + 24 + 68);
                if (characteristics & CHARACTERISTIC_DLL)
                    machine = 0;
                if (machine == MACHINE_AMD64 || machine == MACHINE_ARM64)
                    result = subsystem == SUBSYSTEM_POSIX_CUI ? SCS_POSIX_BINARY_ : SCS_64BIT_BINARY_;
                else if (machine == MACHINE_I386)
                    result = subsystem == SUBSYSTEM_POSIX_CUI ? SCS_POSIX_BINARY_ : SCS_32BIT_BINARY_;
                else {
                    SetLastError(ERROR_BAD_EXE_FORMAT_);
                    ok = FALSE;
                }
            } else if (header[0] == 'N' && header[1] == 'E') {
                /* a new executable for windows is a 16 bit one, any other is taken for DOS. */
                result = header[0x36] == 2 ? SCS_WOW_BINARY_ : SCS_DOS_BINARY_;
            }
            /* an LE or LX header, or none: a DOS program. */
        }
    }
    CloseHandle(file);
    if (ok)
        *type = result;
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI GetBinaryTypeA(LPCSTR name, LPDWORD type)
{
    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR wide[MAX_PATH * 2];
    UINT page = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    if (!MultiByteToWideChar(page, 0, name, -1, wide, MAX_PATH * 2))
        return FALSE;
    return GetBinaryTypeW(wide, type);
}

DECLSPEC_EXPORT BOOL WINAPI GetBinaryType(LPCSTR name, LPDWORD type)
{
    return GetBinaryTypeA(name, type);
}

/*
 * sleeping asks the system for the power action of sleep, hibernating for that of hibernate, both with the
 * lightest state that asks for, and allowing the programs a say unless forced. Without the privilege that the
 * request needs it is ERROR_PRIVILEGE_NOT_HELD, and a machine that cannot enter the state ERROR_NOT_SUPPORTED.
 */
DECLSPEC_EXPORT BOOL WINAPI SetSystemPowerState(BOOL suspend, BOOL force)
{
    NTSTATUS status = NtInitiatePowerAction(3 - (suspend != 0), suspend ? 2 : 5, !force, FALSE);
    if (status >= 0)
        return TRUE;
    SetLastError(RtlNtStatusToDosError(status));
    return FALSE;
}
