/*
 * GetBinaryType and SetSystemPowerState. Windows maps the file as an image and reads its kind from the section, here
 * the file headers are read directly, which gives the same kinds for the same files.
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

/* like windows, a file that is not an image may still be a DOS program or a PIF by its extension. */
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
 * a file that cannot be opened keeps the open's error. a dll, a machine Windows does not run on and a file that is
 * not a program give ERROR_BAD_EXE_FORMAT, a NULL name gives ERROR_INVALID_NAME.
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
                /* an NE for windows is 16-bit windows, any other is taken for DOS. */
                result = header[0x36] == 2 ? SCS_WOW_BINARY_ : SCS_DOS_BINARY_;
            }
            /* LE, LX or no new header: a DOS program. */
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
 * asks for the sleep or hibernate power action with the lightest matching state, letting programs object unless
 * forced. fails with ERROR_PRIVILEGE_NOT_HELD without the privilege, ERROR_NOT_SUPPORTED when the machine cannot
 * enter the state.
 */
DECLSPEC_EXPORT BOOL WINAPI SetSystemPowerState(BOOL suspend, BOOL force)
{
    NTSTATUS status = NtInitiatePowerAction(3 - (suspend != 0), suspend ? 2 : 5, !force, FALSE);
    if (status >= 0)
        return TRUE;
    SetLastError(RtlNtStatusToDosError(status));
    return FALSE;
}
