#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

#define ERROR_BAD_DEVICE 1200L
#define ERROR_NOT_CONNECTED 2250L
#ifndef ERROR_MORE_DATA
#define ERROR_MORE_DATA 234L
#endif

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

DECLSPEC_EXPORT DWORD WINAPI WNetGetConnectionW(LPCWSTR local, LPWSTR remote, LPDWORD size)
{
    if (!local || !size || (!remote && *size))
        return ERROR_INVALID_PARAMETER;
    WCHAR drive = local[0];
    if (drive >= L'a' && drive <= L'z')
        drive -= L'a' - L'A';
    if (drive < L'A' || drive > L'Z' || local[1] != L':' || local[2])
        return ERROR_BAD_DEVICE;
    return ERROR_NOT_CONNECTED;
}

DECLSPEC_EXPORT DWORD WINAPI WNetGetConnectionA(LPCSTR local, LPSTR remote, LPDWORD size)
{
    if (!local || !size || (!remote && *size))
        return ERROR_INVALID_PARAMETER;
    char drive = local[0];
    if (drive >= 'a' && drive <= 'z')
        drive -= 'a' - 'A';
    if (drive < 'A' || drive > 'Z' || local[1] != ':' || local[2])
        return ERROR_BAD_DEVICE;
    return ERROR_NOT_CONNECTED;
}

typedef struct {
    DWORD Scope, Type, DisplayType, Usage;
    LPWSTR LocalName, RemoteName, Comment, Provider;
} NETRESOURCE_W;

#define ERROR_NO_NETWORK 1222L

/* with no network provider, connections are never made or found. */
DECLSPEC_EXPORT DWORD WINAPI WNetUseConnectionW(HWND window, NETRESOURCE_W *resource, LPCWSTR password, LPCWSTR user,
                                                DWORD flags, LPWSTR access_name, LPDWORD access_size, LPDWORD result)
{
    if (!resource)
        return ERROR_INVALID_PARAMETER;
    if (result)
        *result = 0;
    return ERROR_NO_NETWORK;
}

DECLSPEC_EXPORT DWORD WINAPI WNetCancelConnectionW(LPCWSTR name, BOOL force)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_CONNECTED;
}

/* ------------------------------------------------------------------ the rest of the connection functions */

typedef struct {
    DWORD Scope, Type, DisplayType, Usage;
    LPSTR LocalName, RemoteName, Comment, Provider;
} NETRESOURCE_A;

typedef struct {
    DWORD StructSize, Flags, Speed, Delay, OptDataSize;
} NETCONNECTINFOSTRUCT;

#define ERROR_BAD_DEVICE_ 1200L
#define RESOURCE_CONNECTED 1
#define RESOURCE_GLOBALNET 2
#define RESOURCE_REMEMBERED 3
#define RESOURCE_RECENT 4
#define RESOURCE_CONTEXT 5
#define RESOURCE_SHAREABLE 6
#define REMEMBER_MAGIC 0x626D6572 /* "remb" */

DECLSPEC_EXPORT DWORD WINAPI WNetUseConnectionA(HWND window, NETRESOURCE_A *resource, LPCSTR password, LPCSTR user,
                                                DWORD flags, LPSTR access_name, LPDWORD access_size, LPDWORD result)
{
    if (!resource)
        return ERROR_INVALID_PARAMETER;
    if (result)
        *result = 0;
    return ERROR_NO_NETWORK;
}

DECLSPEC_EXPORT DWORD WINAPI WNetAddConnection2W(NETRESOURCE_W *resource, LPCWSTR password, LPCWSTR user, DWORD flags)
{
    return WNetUseConnectionW(nullptr, resource, password, user, flags, nullptr, nullptr, nullptr);
}

DECLSPEC_EXPORT DWORD WINAPI WNetAddConnection2A(NETRESOURCE_A *resource, LPCSTR password, LPCSTR user, DWORD flags)
{
    return WNetUseConnectionA(nullptr, resource, password, user, flags, nullptr, nullptr, nullptr);
}

DECLSPEC_EXPORT DWORD WINAPI WNetAddConnection3W(HWND window, NETRESOURCE_W *resource, LPCWSTR password, LPCWSTR user,
                                                 DWORD flags)
{
    return WNetUseConnectionW(window, resource, password, user, flags, nullptr, nullptr, nullptr);
}

DECLSPEC_EXPORT DWORD WINAPI WNetAddConnection3A(HWND window, NETRESOURCE_A *resource, LPCSTR password, LPCSTR user,
                                                 DWORD flags)
{
    return WNetUseConnectionA(window, resource, password, user, flags, nullptr, nullptr, nullptr);
}

DECLSPEC_EXPORT DWORD WINAPI WNetCancelConnectionA(LPCSTR name, BOOL force)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_CONNECTED;
}

DECLSPEC_EXPORT DWORD WINAPI WNetCancelConnection2W(LPCWSTR name, DWORD flags, BOOL force)
{
    return WNetCancelConnectionW(name, force);
}

DECLSPEC_EXPORT DWORD WINAPI WNetCancelConnection2A(LPCSTR name, DWORD flags, BOOL force)
{
    return WNetCancelConnectionA(name, force);
}

DECLSPEC_EXPORT DWORD WINAPI MultinetGetConnectionPerformanceW(NETRESOURCE_W *resource, NETCONNECTINFOSTRUCT *info)
{
    return ERROR_NO_NETWORK;
}

DECLSPEC_EXPORT DWORD WINAPI MultinetGetConnectionPerformanceA(NETRESOURCE_A *resource, NETCONNECTINFOSTRUCT *info)
{
    return ERROR_NO_NETWORK;
}

/* ------------------------------------------------------------------ enumeration */

typedef struct {
    DWORD Magic;
} ENUM_HANDLE;

/*
 * only the remembered and recent connections can be enumerated without a provider, and there are none to list. the
 * other scopes need the network.
 */
static DWORD open_enum(DWORD scope, DWORD type, DWORD usage, BOOL named, HANDLE *out)
{
    if ((type & ~3u) || (scope == RESOURCE_GLOBALNET && (usage & ~0x13u)))
        return ERROR_INVALID_PARAMETER;
    if (scope < RESOURCE_CONNECTED || scope > RESOURCE_SHAREABLE)
        return ERROR_INVALID_PARAMETER;
    /* a container to list is only given for the network and for shareable resources. */
    if (named && scope != RESOURCE_GLOBALNET && scope != RESOURCE_SHAREABLE)
        return ERROR_INVALID_PARAMETER;
    if (scope != RESOURCE_REMEMBERED && scope != RESOURCE_RECENT)
        return ERROR_NO_NETWORK;
    ENUM_HANDLE *handle = LocalAlloc(LMEM_FIXED, sizeof(*handle));
    if (!handle)
        return ERROR_NOT_ENOUGH_MEMORY;
    handle->Magic = REMEMBER_MAGIC;
    *out = handle;
    return NO_ERROR;
}

DECLSPEC_EXPORT DWORD WINAPI WNetOpenEnumW(DWORD scope, DWORD type, DWORD usage, NETRESOURCE_W *resource, HANDLE *out)
{
    BOOL named = resource && ((resource->Provider && *resource->Provider) || (resource->RemoteName && *resource->RemoteName));
    DWORD error = open_enum(scope, type, usage, named, out);
    if (error)
        SetLastError(error);
    return error;
}

DECLSPEC_EXPORT DWORD WINAPI WNetOpenEnumA(DWORD scope, DWORD type, DWORD usage, NETRESOURCE_A *resource, HANDLE *out)
{
    BOOL named = resource && ((resource->Provider && *resource->Provider) || (resource->RemoteName && *resource->RemoteName));
    DWORD error = open_enum(scope, type, usage, named, out);
    if (error)
        SetLastError(error);
    return error;
}

static DWORD enum_resource(HANDLE handle, LPDWORD count, LPVOID buffer, LPDWORD size)
{
    if (!buffer)
        return ERROR_INVALID_ADDRESS;
    if (((ENUM_HANDLE *)handle)->Magic != REMEMBER_MAGIC)
        return ERROR_INVALID_HANDLE;
    *count = 0;
    return ERROR_NO_MORE_ITEMS;
}

DECLSPEC_EXPORT DWORD WINAPI WNetEnumResourceW(HANDLE handle, LPDWORD count, LPVOID buffer, LPDWORD size)
{
    DWORD error = enum_resource(handle, count, buffer, size);
    SetLastError(error);
    return error;
}

DECLSPEC_EXPORT DWORD WINAPI WNetEnumResourceA(HANDLE handle, LPDWORD count, LPVOID buffer, LPDWORD size)
{
    DWORD error = enum_resource(handle, count, buffer, size);
    SetLastError(error);
    return error;
}

DECLSPEC_EXPORT DWORD WINAPI WNetCloseEnum(HANDLE handle)
{
    if (!handle || ((ENUM_HANDLE *)handle)->Magic != REMEMBER_MAGIC) {
        SetLastError(ERROR_INVALID_HANDLE);
        return ERROR_INVALID_HANDLE;
    }
    ((ENUM_HANDLE *)handle)->Magic = 0;
    LocalFree(handle);
    return NO_ERROR;
}

/* ------------------------------------------------------------------ names and errors */

static DWORD universal_name(DWORD level, BOOL (*valid)(const void *), const void *path)
{
    DWORD error = valid(path) ? NO_ERROR : ERROR_BAD_DEVICE_;
    if (!error && level != 1 && level != 2)
        error = ERROR_INVALID_LEVEL;
    /* a path on a drive that no provider made a connection for has no universal name. */
    return error ? error : ERROR_NOT_CONNECTED;
}

static BOOL valid_path_w(const void *text)
{
    const WCHAR *p = text;
    if (p[0] == L'\\' && p[1] == L'\\')
        return TRUE;
    BOOL letter = (p[0] >= L'a' && p[0] <= L'z') || (p[0] >= L'A' && p[0] <= L'Z');
    return letter && p[1] == L':';
}

static BOOL valid_path_a(const void *text)
{
    const char *p = text;
    if (p[0] == '\\' && p[1] == '\\')
        return TRUE;
    BOOL letter = (p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z');
    return letter && p[1] == ':';
}

/* the provider that would describe a resource is not there: no network exists to ask. */
DECLSPEC_EXPORT DWORD WINAPI WNetGetResourceInformationW(NETRESOURCE_W *resource, LPVOID buffer, LPDWORD size,
                                                         LPWSTR *system)
{
    if (!resource || !buffer || !size)
        return ERROR_INVALID_PARAMETER;
    if (system)
        *system = nullptr;
    SetLastError(ERROR_NO_NETWORK);
    return ERROR_NO_NETWORK;
}

/*
 * the user of a connection. a machine with no connections has no user for one, and the user of no name is the one
 * logged on, as the environment says.
 */
DECLSPEC_EXPORT DWORD WINAPI WNetGetUserW(LPCWSTR name, LPWSTR user, LPDWORD length)
{
    if (!user || !length)
        return ERROR_INVALID_PARAMETER;
    if (name && *name)
        return ERROR_NOT_CONNECTED;
    WCHAR account[256];
    DWORD n = GetEnvironmentVariableW(L"USERNAME", account, RTL_NUMBER_OF(account));
    if (!n || n >= RTL_NUMBER_OF(account))
        return ERROR_NO_NETWORK;
    if (*length < n + 1) {
        *length = n + 1;
        return ERROR_MORE_DATA;
    }
    memcpy(user, account, (n + 1) * sizeof(WCHAR));
    *length = n + 1;
    return NO_ERROR;
}

DECLSPEC_EXPORT DWORD WINAPI WNetGetUserA(LPCSTR name, LPSTR user, LPDWORD length)
{
    if (!user || !length)
        return ERROR_INVALID_PARAMETER;
    if (name && *name)
        return ERROR_NOT_CONNECTED;
    WCHAR account[256];
    DWORD n = GetEnvironmentVariableW(L"USERNAME", account, RTL_NUMBER_OF(account));
    if (!n || n >= RTL_NUMBER_OF(account))
        return ERROR_NO_NETWORK;
    char narrow[512];
    int bytes = WideCharToMultiByte(CP_ACP, 0, account, -1, narrow, sizeof(narrow), nullptr, nullptr);
    if (!bytes)
        return ERROR_NO_NETWORK;
    if (*length < (DWORD)bytes) {
        *length = (DWORD)bytes;
        return ERROR_MORE_DATA;
    }
    memcpy(user, narrow, (size_t)bytes);
    *length = (DWORD)bytes;
    return NO_ERROR;
}

DECLSPEC_EXPORT DWORD WINAPI WNetGetUniversalNameW(LPCWSTR path, DWORD level, LPVOID buffer, LPDWORD size)
{
    DWORD error = universal_name(level, valid_path_w, path);
    SetLastError(error);
    return error;
}

DECLSPEC_EXPORT DWORD WINAPI WNetGetUniversalNameA(LPCSTR path, DWORD level, LPVOID buffer, LPDWORD size)
{
    DWORD error = universal_name(level, valid_path_a, path);
    SetLastError(error);
    return error;
}

/* no provider ever recorded an error, so there is nothing to report. */
DECLSPEC_EXPORT DWORD WINAPI WNetGetLastErrorW(LPDWORD error, LPWSTR description, DWORD description_size, LPWSTR name,
                                               DWORD name_size)
{
    *error = 0;
    if (!description || !name)
        return ERROR_INVALID_ADDRESS;
    if (description_size)
        *description = 0;
    if (name_size)
        *name = 0;
    return NO_ERROR;
}

DECLSPEC_EXPORT DWORD WINAPI WNetGetLastErrorA(LPDWORD error, LPSTR description, DWORD description_size, LPSTR name,
                                               DWORD name_size)
{
    *error = 0;
    if (!description || !name)
        return ERROR_INVALID_ADDRESS;
    if (description_size)
        *description = 0;
    if (name_size)
        *name = 0;
    return NO_ERROR;
}
