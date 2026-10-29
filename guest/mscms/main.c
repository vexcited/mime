/*
 * mscms.dll: color management. profiles are read into memory, from a file or a buffer, and checked and described by
 * their headers. there is no color matching module here, so a transform cannot be made and every transform handle is
 * not one. profiles are listed and installed only as far as the color directory goes.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define PROFILE_FILENAME 1
#define PROFILE_MEMBUFFER 2
#define PROFILE_READ 1
#define PROFILE_READWRITE 2
#define PROFILE_MAGIC 0x504D4349
#define ICC_HEADER_SIZE 128
#define ERROR_INVALID_PROFILE_ 2022

typedef struct {
    DWORD dwType;
    PVOID pProfileData;
    DWORD cbDataSize;
} PROFILE;

typedef struct {
    DWORD phSize, phCMMType, phVersion, phClass, phDataColorSpace, phConnectionSpace;
    DWORD phDateTime[3];
    DWORD phSignature, phPlatform, phProfileFlags, phManufacturer, phModel;
    DWORD phAttributes[2];
    DWORD phRenderingIntent;
    LONG phIlluminant[3];
    DWORD phCreator;
    BYTE phReserved[44];
} PROFILEHEADER;

typedef struct COLOR_PROFILE {
    DWORD Magic;
    BYTE *Data;
    DWORD Size;
    DWORD Access;
} COLOR_PROFILE;

static DWORD big_endian(const BYTE *p)
{
    return (DWORD)p[0] << 24 | (DWORD)p[1] << 16 | (DWORD)p[2] << 8 | p[3];
}

static COLOR_PROFILE *profile_of(HANDLE handle)
{
    COLOR_PROFILE *profile = handle;
    return profile && profile->Magic == PROFILE_MAGIC ? profile : nullptr;
}

static HANDLE make_profile(const BYTE *data, DWORD size, DWORD access)
{
    COLOR_PROFILE *profile = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*profile));
    BYTE *copy = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!profile || !copy) {
        HeapFree(GetProcessHeap(), 0, profile);
        HeapFree(GetProcessHeap(), 0, copy);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    memcpy(copy, data, size);
    profile->Magic = PROFILE_MAGIC;
    profile->Data = copy;
    profile->Size = size;
    profile->Access = access;
    return profile;
}

static HANDLE open_profile(PROFILE *description, DWORD access, DWORD share, DWORD creation, BOOL wide)
{
    if (!description || (access != PROFILE_READ && access != PROFILE_READWRITE) || !description->pProfileData) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (description->dwType == PROFILE_MEMBUFFER) {
        if (!description->cbDataSize) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        return make_profile(description->pProfileData, description->cbDataSize, access);
    }
    if (description->dwType != PROFILE_FILENAME) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HANDLE file = wide ? CreateFileW(description->pProfileData, GENERIC_READ, share, nullptr, creation ? creation : OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr)
                       : CreateFileA(description->pProfileData, GENERIC_READ, share, nullptr, creation ? creation : OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    BYTE *data = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    HANDLE result = nullptr;
    if (!data) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } else if (size == INVALID_FILE_SIZE || !ReadFile(file, data, size, &read, nullptr) || read != size) {
        if (size != INVALID_FILE_SIZE)
            SetLastError(ERROR_READ_FAULT);
    } else {
        result = make_profile(data, size, access);
    }
    HeapFree(GetProcessHeap(), 0, data);
    CloseHandle(file);
    return result;
}

HANDLE WINAPI OpenColorProfileA(PROFILE *description, DWORD access, DWORD share, DWORD creation)
{
    return open_profile(description, access, share, creation, FALSE);
}

HANDLE WINAPI OpenColorProfileW(PROFILE *description, DWORD access, DWORD share, DWORD creation)
{
    return open_profile(description, access, share, creation, TRUE);
}

BOOL WINAPI CloseColorProfile(HANDLE handle)
{
    COLOR_PROFILE *profile = profile_of(handle);
    if (!profile) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    profile->Magic = 0;
    HeapFree(GetProcessHeap(), 0, profile->Data);
    HeapFree(GetProcessHeap(), 0, profile);
    return TRUE;
}

/* a profile is valid when its header says how long it is and carries the signature of one. */
BOOL WINAPI IsColorProfileValid(HANDLE handle, BOOL *valid)
{
    COLOR_PROFILE *profile = profile_of(handle);
    if (!profile || !valid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *valid = profile->Size >= ICC_HEADER_SIZE && big_endian(profile->Data) <= profile->Size &&
             big_endian(profile->Data + 36) == 0x61637370;
    return TRUE;
}

BOOL WINAPI GetColorProfileHeader(HANDLE handle, PROFILEHEADER *header)
{
    COLOR_PROFILE *profile = profile_of(handle);
    if (!profile || !header) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (profile->Size < ICC_HEADER_SIZE) {
        SetLastError(ERROR_INVALID_PROFILE_);
        return FALSE;
    }
    DWORD *out = (DWORD *)header;
    /* every field up to the creator is a 32 bit number in the big endian order of the file. */
    for (int i = 0; i < 21; i++)
        out[i] = big_endian(profile->Data + 4 * i);
    memcpy(header->phReserved, profile->Data + 84, sizeof(header->phReserved));
    return TRUE;
}

/* ------------------------------------------------------------------ the color directory */

#define COLOR_DIRECTORY "\\spool\\drivers\\color"

BOOL WINAPI GetColorDirectoryA(const char *machine, char *buffer, DWORD *size)
{
    if (machine || !size) {
        SetLastError(machine ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, sizeof(path) - sizeof(COLOR_DIRECTORY));
    if (!n) {
        return FALSE;
    }
    memcpy(path + n, COLOR_DIRECTORY, sizeof(COLOR_DIRECTORY));
    DWORD needed = (DWORD)(n + sizeof(COLOR_DIRECTORY));
    if (!buffer || *size < needed) {
        *size = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, path, needed);
    *size = needed;
    return TRUE;
}

BOOL WINAPI GetColorDirectoryW(const WCHAR *machine, WCHAR *buffer, DWORD *size)
{
    if (machine || !size) {
        SetLastError(machine ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    char narrow[MAX_PATH];
    DWORD length = sizeof(narrow);
    if (!GetColorDirectoryA(nullptr, narrow, &length))
        return FALSE;
    DWORD needed = (length) * sizeof(WCHAR);
    if (!buffer || *size < needed) {
        *size = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    MultiByteToWideChar(CP_ACP, 0, narrow, length, buffer, length);
    *size = needed;
    return TRUE;
}

/* ------------------------------------------------------------------ transforms: there is no color matching module */

HANDLE WINAPI CreateMultiProfileTransform(HANDLE *profiles, DWORD count, DWORD *intents, DWORD intent_count, DWORD flags,
                                          DWORD preferred)
{
    SetLastError(!profiles || !count ? ERROR_INVALID_PARAMETER : ERROR_NOT_SUPPORTED);
    return nullptr;
}

BOOL WINAPI DeleteColorTransform(HANDLE transform)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL WINAPI TranslateBitmapBits(HANDLE transform, PVOID input, DWORD input_format, DWORD width, DWORD height,
                                DWORD input_stride, PVOID output, DWORD output_format, DWORD output_stride,
                                PVOID callback, ULONG_PTR data)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL WINAPI TranslateColors(HANDLE transform, PVOID input, DWORD count, DWORD input_type, PVOID output, DWORD output_type)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}
