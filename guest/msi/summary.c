#include "msi_private.h"
#include <mime/win32/winnls.h>

typedef struct {
    UINT Type;
    int Number;
    FILETIME Time;
    PWSTR Text;
} MIME_MSI_SUMMARY_PROPERTY;

typedef struct {
    MIME_MSI_OBJECT Object;
    MIME_MSI_SUMMARY_PROPERTY Properties[20];
} MIME_MSI_SUMMARY;

static ULONG read32(const BYTE *data)
{
    return (ULONG)data[0] | (ULONG)data[1] << 8 | (ULONG)data[2] << 16 | (ULONG)data[3] << 24;
}

static void destroy_summary(MIME_MSI_OBJECT *object)
{
    MIME_MSI_SUMMARY *summary = (void *)object;
    for (ULONG i = 0; i < RTL_NUMBER_OF(summary->Properties); i++)
        MimeMsiFree(summary->Properties[i].Text);
    MimeMsiFree(summary);
}

static UINT read_summary(MIME_MSI_DATABASE *database, MIME_MSI_SUMMARY *summary)
{
    BYTE *data;
    SIZE_T length;
    UINT error = MimeMsiReadStream(database, L"\x0005SummaryInformation", &data, &length);
    if (error)
        return error;
    static const BYTE format[] = {0xe0, 0x85, 0x9f, 0xf2, 0xf9, 0x4f, 0x68, 0x10,
                                  0xab, 0x91, 0x08, 0, 0x2b, 0x27, 0xb3, 0xd9};
    error = ERROR_INSTALL_PACKAGE_INVALID;
    if (length < 48 || data[0] != 0xfe || data[1] != 0xff || data[2] > 1 || data[3] ||
        read32(data + 24) != 1 || memcmp(data + 28, format, sizeof(format)))
        goto done;
    ULONG offset = read32(data + 44);
    if (offset < 48 || offset > length - 8 || offset % 4)
        goto done;
    const BYTE *section = data + offset;
    ULONG size = read32(section), count = read32(section + 4);
    if (size < 8 || size > length - offset || count > (size - 8) / 8)
        goto done;
    UINT codepage = 1252;
    for (ULONG i = 0; i < count; i++) {
        ULONG id = read32(section + 8 + i * 8);
        ULONG at = read32(section + 12 + i * 8);
        if (at < 8 + count * 8 || at > size - 4 || at % 4)
            goto done;
        if (id == 1) {
            if (size - at < 6 || read32(section + at) != 2)
                goto done;
            codepage = (UINT)section[at + 4] | (UINT)section[at + 5] << 8;
        }
    }
    if (!codepage)
        codepage = GetACP();
    if (codepage != 1200 && !IsValidCodePage(codepage)) {
        error = ERROR_CALL_NOT_IMPLEMENTED;
        goto done;
    }
    for (ULONG i = 0; i < count; i++) {
        ULONG id = read32(section + 8 + i * 8);
        ULONG at = read32(section + 12 + i * 8);
        if (!id || id == 17 || id >= RTL_NUMBER_OF(summary->Properties))
            continue;
        MIME_MSI_SUMMARY_PROPERTY *property = &summary->Properties[id];
        if (property->Type)
            goto done;
        ULONG type = read32(section + at);
        const BYTE *value = section + at + 4;
        ULONG remaining = size - at - 4;
        if (type == 2 && remaining >= 2)
            property->Number = (SHORT)((USHORT)value[0] | (USHORT)value[1] << 8);
        else if (type == 3 && remaining >= 4)
            property->Number = (int)read32(value);
        else if (type == 64 && remaining >= 8) {
            property->Time.dwLowDateTime = read32(value);
            property->Time.dwHighDateTime = read32(value + 4);
        } else if (type == 30 && remaining >= 4) {
            ULONG bytes = read32(value);
            if (bytes > remaining - 4 || bytes > INT32_MAX)
                goto done;
            value += 4;
            if (!bytes)
                property->Text = MimeMsiDuplicate(L"");
            else if (codepage == 1200) {
                if (bytes % 2 || bytes < 2 || value[bytes - 1] || value[bytes - 2])
                    goto done;
                property->Text = MimeMsiAllocate(bytes);
                if (property->Text)
                    memcpy(property->Text, value, bytes);
            } else {
                if (value[bytes - 1])
                    goto done;
                int characters = MultiByteToWideChar(codepage, 0, (PCSTR)value, (int)bytes, nullptr, 0);
                if (!characters)
                    goto done;
                property->Text = MimeMsiAllocate((SIZE_T)characters * sizeof(WCHAR));
                if (property->Text && !MultiByteToWideChar(codepage, 0, (PCSTR)value, (int)bytes, property->Text, characters))
                    goto done;
            }
            if (!property->Text) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                goto done;
            }
        } else
            goto done;
        property->Type = type;
    }
    error = ERROR_SUCCESS;
done:
    MimeMsiFree(data);
    return error;
}

UINT MimeMsiCreateSummary(MIME_MSI_DATABASE *database, MSIHANDLE *out)
{
    *out = 0;
    MIME_MSI_SUMMARY *summary = database ? MimeMsiAllocate(sizeof(*summary)) : nullptr;
    UINT error = !database ? ERROR_INVALID_HANDLE : !summary ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    if (!error) {
        summary->Object.Kind = MIME_MSI_KIND_SUMMARY;
        summary->Object.Destroy = destroy_summary;
        error = read_summary(database, summary);
        if (!error && !(*out = MimeMsiAddHandle(&summary->Object)))
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error && summary)
        destroy_summary(&summary->Object);
    return error;
}

UINT WINAPI MsiGetSummaryInformationW(MSIHANDLE handle, LPCWSTR path, UINT updates, MSIHANDLE *out)
{
    if (!out || (!handle && (!path || !*path)))
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    if (updates)
        return ERROR_CALL_NOT_IMPLEMENTED;
    MIME_MSI_DATABASE *storage = nullptr;
    if (path && *path) {
        UINT error = MimeMsiOpenStorageFile(path, 0, &storage);
        if (error)
            return error;
    }
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = storage ? storage : (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    UINT error = MimeMsiCreateSummary(database, out);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (storage)
        storage->Object.Destroy(&storage->Object);
    return error;
}

UINT WINAPI MsiSummaryInfoGetPropertyW(MSIHANDLE handle, UINT id, PUINT type, int *number,
                                      FILETIME *time, LPWSTR text, LPDWORD size)
{
    if (!id || id == 17 || id >= 20 || !type)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_SUMMARY *summary = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_SUMMARY);
    UINT error = summary ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
    if (!error) {
        MIME_MSI_SUMMARY_PROPERTY *property = &summary->Properties[id];
        *type = property->Type;
        if ((property->Type == 2 || property->Type == 3) && number)
            *number = property->Number;
        if (property->Type == 64 && time)
            *time = property->Time;
        if (property->Type == 30 && text)
            error = MimeMsiCopyString(property->Text, text, size);
    }
    ReleaseSRWLockShared(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiSummaryInfoGetPropertyCount(MSIHANDLE handle, PUINT count)
{
    if (!count)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_SUMMARY *summary = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_SUMMARY);
    if (summary) {
        *count = 0;
        for (ULONG i = 0; i < RTL_NUMBER_OF(summary->Properties); i++)
            *count += !!summary->Properties[i].Type;
    }
    ReleaseSRWLockShared(&MimeMsiLock);
    return summary ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}
