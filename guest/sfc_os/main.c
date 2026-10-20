/*
 * sfc_os.dll, Windows Resource Protection. Windows 11 keeps no list of protected files for the callers of this dll:
 * the watcher thread and its server are gone, most entry points answer that nothing is done, and a file is protected
 * when the servicing stack knows it. Mime has no servicing stack, so a file is protected when it is one of the files
 * of the operating system in the Windows folder, see docs/sfc.md.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winver.h>
#include <rtl.h>

#define SFC DECLSPEC_EXPORT

#define OWNER_SECURITY_INFORMATION 1
#define DACL_SECURITY_INFORMATION 4

typedef struct {
    DWORD AceCount;
    DWORD AclBytesInUse;
    DWORD AclBytesFree;
} ACL_SIZE_INFORMATION;

DECLSPEC_IMPORT BOOL WINAPI GetSecurityDescriptorDacl(PVOID descriptor, BOOL *present, PVOID *acl, BOOL *defaulted);
DECLSPEC_IMPORT BOOL WINAPI GetAclInformation(PVOID acl, PVOID information, DWORD length, DWORD klass);
DECLSPEC_IMPORT BOOL WINAPI GetAce(PVOID acl, DWORD index, PVOID *ace);
DECLSPEC_IMPORT BOOL WINAPI EqualSid(PVOID first, PVOID second);
DECLSPEC_IMPORT LONG WINAPI RegGetKeySecurity(HKEY key, DWORD information, PVOID descriptor, DWORD *length);

typedef struct {
    WCHAR FileName[MAX_PATH];
    DWORD FileNumber;
} PROTECTED_FILE_DATA;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

/* ------------------------------------------------------------------ files */

static constexpr WCHAR g_product[] = L"Microsoft® Windows® Operating System";

/* whether the version resource of the file names the operating system as its product. */
static BOOL is_operating_system_file(PCWSTR path)
{
    DWORD handle, bytes = GetFileVersionInfoSizeW(path, &handle);
    if (!bytes)
        return FALSE;
    BYTE *block = HeapAlloc(GetProcessHeap(), 0, bytes);
    BOOL result = FALSE;
    if (block && GetFileVersionInfoW(path, 0, bytes, block)) {
        WORD *translation;
        UINT length;
        WCHAR query[64];
        PCWSTR value;
        if (VerQueryValueW(block, L"\\VarFileInfo\\Translation", (void **)&translation, &length) && length >= 4) {
            _snwprintf(query, RTL_NUMBER_OF(query), L"\\StringFileInfo\\%04X%04X\\ProductName", translation[0],
                       translation[1]);
            result = VerQueryValueW(block, query, (void **)&value, &length) && !wcscmp(value, g_product);
        }
    }
    HeapFree(GetProcessHeap(), 0, block);
    return result;
}

/* a file of the Windows folder that carries the product of the operating system. */
SFC BOOL WINAPI SfcIsFileProtected(HANDLE rpc, LPCWSTR name)
{
    if (!name || !*name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR full[MAX_PATH], windows[MAX_PATH];
    DWORD length = GetFullPathNameW(name, MAX_PATH, full, nullptr);
    DWORD root = GetWindowsDirectoryW(windows, MAX_PATH);
    if (!length || length >= MAX_PATH || !root || root >= MAX_PATH) {
        SetLastError(ERROR_SUCCESS);
        return FALSE;
    }
    DWORD attributes = GetFileAttributesW(full);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    BOOL protected_file = !(attributes & FILE_ATTRIBUTE_DIRECTORY) && length > root + 1 && full[root] == L'\\' &&
                          !_wcsnicmp(full, windows, root) && is_operating_system_file(full);
    SetLastError(ERROR_SUCCESS);
    return protected_file;
}

/* the list of protected files is not kept: it ends before its first file. */
SFC BOOL WINAPI SfcGetNextProtectedFile(HANDLE rpc, PROTECTED_FILE_DATA *data)
{
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

SFC BOOL WINAPI SfpVerifyFile(LPCSTR file, LPSTR error, DWORD error_size)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* ------------------------------------------------------------------ registry keys */

/* the service sid of TrustedInstaller, the owner of the keys and files of the system. */
static const BYTE g_trusted_installer[] = {
    1, 6, 0, 0, 0, 0, 0, 5,
    0x50, 0x00, 0x00, 0x00,
    0xB5, 0x89, 0xFB, 0x38,
    0x19, 0x84, 0xC2, 0xCB,
    0x5C, 0x6C, 0x23, 0x6D,
    0x57, 0x00, 0x77, 0x6E,
    0xC0, 0x02, 0x64, 0x87,
};

typedef struct {
    BYTE Type, Flags;
    WORD Size;
    DWORD Mask;
    DWORD Sid[1];
} ACE_HEADER_;

/*
 * a key is protected when TrustedInstaller has full control of it and nobody else can change it: the first deny entry
 * for TrustedInstaller or an allow entry of another sid with a right that writes, deletes or changes the descriptor
 * ends the walk with no protection.
 */
static BOOL protects_key(PSECURITY_DESCRIPTOR descriptor)
{
    BOOL present, defaulted;
    PVOID acl;
    ACL_SIZE_INFORMATION information;
    /* the information class is the size of the list, 2. */
    if (!GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) || !acl ||
        !GetAclInformation(acl, &information, sizeof(information), 2))
        return FALSE;
    BOOL full = FALSE;
    for (DWORD i = 0; i < information.AceCount; i++) {
        ACE_HEADER_ *ace;
        if (!GetAce(acl, i, (PVOID *)&ace))
            continue;
        if (EqualSid((PSID)&ace->Sid, (PSID)g_trusted_installer)) {
            if (ace->Type == 1)
                return FALSE;
            if (ace->Type == 0 && (ace->Mask == GENERIC_ALL || ace->Mask == KEY_ALL_ACCESS))
                full = TRUE;
        } else if (ace->Type == 0 && (ace->Mask & 0x500D0006)) {
            return FALSE;
        }
    }
    return full;
}

SFC BOOL WINAPI SfcIsKeyProtected(HKEY key, LPCWSTR subkey, REGSAM sam)
{
    HKEY opened = nullptr;
    WCHAR path[MAX_PATH];
    LONG error = ERROR_SUCCESS;
    if (subkey) {
        if (wcslen(subkey) >= MAX_PATH) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        wcscpy(path, subkey);
        SIZE_T length = wcslen(path);
        /* the nearest key that exists above the one named stands in for it. */
        for (;;) {
            error = RegOpenKeyExW(key, path, 0, (sam & 0x300) | KEY_READ, &opened);
            if (!error)
                break;
            if (error != ERROR_FILE_NOT_FOUND || !length)
                break;
            while (length && path[length - 1] != L'\\')
                length--;
            if (length)
                path[--length] = 0;
            if (!length) {
                error = ERROR_SUCCESS;
                break;
            }
        }
        if (error) {
            SetLastError(error);
            return FALSE;
        }
    }
    HKEY target = opened ? opened : key;
    DWORD bytes = 0;
    BOOL protected_key = FALSE;
    error = RegGetKeySecurity(target, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, &bytes);
    if (error == ERROR_INSUFFICIENT_BUFFER) {
        PSECURITY_DESCRIPTOR descriptor = LocalAlloc(LPTR, bytes);
        if (!descriptor) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            error = RegGetKeySecurity(target, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, descriptor, &bytes);
            if (!error)
                protected_key = protects_key(descriptor);
            LocalFree(descriptor);
        }
    }
    if (opened)
        RegCloseKey(opened);
    SetLastError(error);
    return protected_key;
}

/* ------------------------------------------------------------------ what windows 11 leaves of the rest */

/* nothing runs that could be asked, and nothing needs to be started: these answer success or "not implemented". */
SFC DWORD WINAPI SfcInitProt(DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g)
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

SFC DWORD WINAPI SfpInstallCatalog(DWORD a, DWORD b, DWORD c)
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

SFC DWORD WINAPI SfpDeleteCatalog(DWORD a, DWORD b)
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

SFC DWORD WINAPI SfcTerminateWatcherThread(void)
{
    return 0;
}

SFC DWORD WINAPI SfcConnectToServer(DWORD a)
{
    return 0;
}

SFC DWORD WINAPI SfcClose(DWORD a)
{
    return 0;
}

SFC DWORD WINAPI SfcFileException(DWORD a, DWORD b, DWORD c)
{
    return 0;
}

SFC DWORD WINAPI SfcInitiateScan(DWORD a, DWORD b)
{
    return 0;
}

SFC DWORD WINAPI SfcInstallProtectedFiles(DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g)
{
    return 0;
}

/* the system restore client is loaded for the call, and without it the status says so. */
typedef struct {
    DWORD Event;
    DWORD Type;
    LONGLONG Sequence;
    CHAR Description[64];
} RESTOREPOINTINFOA_;

typedef struct {
    DWORD Event;
    DWORD Type;
    LONGLONG Sequence;
    WCHAR Description[256];
} RESTOREPOINTINFOW_;

typedef struct {
    DWORD Status;
    LONGLONG Sequence;
} STATEMGRSTATUS_;

typedef BOOL(WINAPI *SET_POINT)(void *, STATEMGRSTATUS_ *);

static BOOL set_restore_point(PCSTR name, void *spec, STATEMGRSTATUS_ *status)
{
    HMODULE client = LoadLibraryExW(L"SRCLIENT.DLL", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    BOOL result = FALSE;
    SET_POINT call = client ? (SET_POINT)GetProcAddress(client, name) : nullptr;
    if (call)
        result = call(spec, status);
    else if (status)
        status->Status = ERROR_CALL_NOT_IMPLEMENTED;
    if (client)
        FreeLibrary(client);
    return result;
}

SFC BOOL WINAPI SRSetRestorePointA(RESTOREPOINTINFOA_ *spec, STATEMGRSTATUS_ *status)
{
    return set_restore_point("SRSetRestorePointA", spec, status);
}

SFC BOOL WINAPI SRSetRestorePointW(RESTOREPOINTINFOW_ *spec, STATEMGRSTATUS_ *status)
{
    return set_restore_point("SRSetRestorePointW", spec, status);
}

/* ------------------------------------------------------------------ the file map enumeration of the servicing stack */

SFC BOOL WINAPI BeginFileMapEnumeration(DWORD flags, PVOID reserved, PVOID *handle)
{
    if (!handle || (*handle = nullptr, flags & ~1u)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

SFC BOOL WINAPI GetNextFileMapContent(DWORD flags, PVOID handle, SIZE_T size, PVOID buffer, PVOID *next)
{
    if (next)
        *next = nullptr;
    if (buffer)
        memset(buffer, 0, size);
    if (flags || !handle || !next || (size && !buffer)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

SFC BOOL WINAPI CloseFileMapEnumeration(PVOID handle)
{
    if (!handle) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}
