#include "msi_private.h"

static void clear_field(MIME_MSI_FIELD *field)
{
    MimeMsiFree(field->String);
    MimeMsiBlobRelease(field->Stream);
    memset(field, 0, sizeof(*field));
}

MIME_MSI_BLOB *MimeMsiBlobCreate(const void *data, SIZE_T size)
{
    MIME_MSI_BLOB *blob = MimeMsiAllocate(sizeof(*blob) + size);
    if (!blob)
        return nullptr;
    blob->References = 1;
    blob->Size = size;
    if (size && data)
        memcpy(blob->Data, data, size);
    return blob;
}

void MimeMsiBlobRelease(MIME_MSI_BLOB *blob)
{
    if (blob && !--blob->References)
        MimeMsiFree(blob);
}

UINT MimeMsiFieldStream(MIME_MSI_FIELD *field, MIME_MSI_BLOB *blob)
{
    clear_field(field);
    field->Kind = FIELD_STREAM;
    field->Stream = blob;
    blob->References++;
    return ERROR_SUCCESS;
}

static void destroy_record(MIME_MSI_OBJECT *object)
{
    MIME_MSI_RECORD *record = (void *)object;
    for (UINT i = 0; i <= record->Count; i++)
        clear_field(&record->Fields[i]);
    MimeMsiFree(record);
}

MIME_MSI_RECORD *MimeMsiRecordCreate(UINT count)
{
    if (count > 65535)
        return 0;
    MIME_MSI_RECORD *record = MimeMsiAllocate(sizeof(*record) + (count + 1) * sizeof(MIME_MSI_FIELD));
    if (!record)
        return 0;
    record->Object.Kind = MIME_MSI_KIND_RECORD;
    record->Object.Destroy = destroy_record;
    record->Count = count;
    return record;
}

MSIHANDLE WINAPI MsiCreateRecord(UINT count)
{
    MIME_REMOTE(PROXY_CreateRecord, MSIHANDLE, MIME_ARG(count));
    MIME_MSI_RECORD *record = MimeMsiRecordCreate(count);
    if (!record)
        return 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSIHANDLE handle = MimeMsiAddHandle(&record->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!handle)
        destroy_record(&record->Object);
    return handle;
}

UINT WINAPI MsiRecordGetFieldCount(MSIHANDLE handle)
{
    MIME_REMOTE(PROXY_RecordGetFieldCount, UINT, MIME_ARG(handle));
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    UINT count = record ? record->Count : 0;
    ReleaseSRWLockShared(&MimeMsiLock);
    return count;
}

UINT WINAPI MsiRecordSetInteger(MSIHANDLE handle, UINT index, int value)
{
    MIME_REMOTE(PROXY_RecordSetInteger, UINT, MIME_ARG(handle), MIME_ARG(index), MIME_ARG(value));
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    UINT error = !record ? ERROR_INVALID_HANDLE : index > record->Count ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!error) {
        MIME_MSI_FIELD *field = &record->Fields[index];
        MimeMsiFieldInteger(field, value);
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiRecordSetStringW(MSIHANDLE handle, UINT index, LPCWSTR value)
{
    MIME_REMOTE(PROXY_RecordSetStringW, UINT, MIME_ARG(handle), MIME_ARG(index), MIME_ARG(value));
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    UINT error = !record ? ERROR_INVALID_HANDLE : index > record->Count ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!error)
        error = MimeMsiFieldString(&record->Fields[index], value);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

static int string_integer(PCWSTR value)
{
    BOOL negative = *value == L'-';
    if (negative)
        value++;
    if (!*value)
        return MSI_NULL_INTEGER;
    ULONG result = 0;
    while (*value) {
        if (*value < L'0' || *value > L'9' || result > (0x7FFFFFFFu - (*value - L'0')) / 10)
            return MSI_NULL_INTEGER;
        result = result * 10 + (*value++ - L'0');
    }
    return negative ? -(int)result : (int)result;
}

int WINAPI MsiRecordGetInteger(MSIHANDLE handle, UINT index)
{
    MIME_REMOTE(PROXY_RecordGetInteger, int, MIME_ARG(handle), MIME_ARG(index));
    int result = MSI_NULL_INTEGER;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    if (record && index <= record->Count) {
        MIME_MSI_FIELD *field = &record->Fields[index];
        result = MimeMsiFieldGetInteger(field);
    }
    ReleaseSRWLockShared(&MimeMsiLock);
    return result;
}

UINT WINAPI MsiRecordGetStringW(MSIHANDLE handle, UINT index, LPWSTR out, LPDWORD size)
{
    MIME_REMOTE(PROXY_RecordGetStringW, UINT, MIME_ARG(handle), MIME_ARG(index), MIME_ARG(out), MIME_ARG(size));
    if (!out || !size)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    UINT error = !record ? ERROR_INVALID_HANDLE : index > record->Count ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!error) {
        MIME_MSI_FIELD *field = &record->Fields[index];
        WCHAR number[16];
        PCWSTR text = L"";
        if (field->Kind == FIELD_STRING)
            text = field->String;
        else if (field->Kind == FIELD_INTEGER) {
            _snwprintf(number, RTL_NUMBER_OF(number), L"%d", field->Integer);
            text = number;
        }
        DWORD count = (DWORD)wcslen(text), capacity = *size;
        *size = count;
        if (capacity) {
            DWORD copied = count < capacity ? count : capacity - 1;
            memcpy(out, text, copied * sizeof(WCHAR));
            out[copied] = 0;
        }
        if (count >= capacity)
            error = ERROR_MORE_DATA;
    }
    ReleaseSRWLockShared(&MimeMsiLock);
    return error;
}

BOOL WINAPI MsiRecordIsNull(MSIHANDLE handle, UINT index)
{
    MIME_REMOTE(PROXY_RecordIsNull, BOOL, MIME_ARG(handle), MIME_ARG(index));
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    BOOL result = !record || index > record->Count || record->Fields[index].Kind == FIELD_NULL;
    ReleaseSRWLockShared(&MimeMsiLock);
    return result;
}

UINT WINAPI MsiRecordClearData(MSIHANDLE handle)
{
    MIME_REMOTE(PROXY_RecordClearData, UINT, MIME_ARG(handle));
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    if (record)
        for (UINT i = 0; i <= record->Count; i++)
            clear_field(&record->Fields[i]);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return record ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

UINT WINAPI MsiRecordDataSize(MSIHANDLE handle, UINT index)
{
    MIME_REMOTE(PROXY_RecordDataSize, UINT, MIME_ARG(handle), MIME_ARG(index));
    UINT size = 0;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    if (record && index <= record->Count) {
        MIME_MSI_FIELD *field = &record->Fields[index];
        size = field->Kind == FIELD_STRING ? (UINT)wcslen(field->String) : field->Kind == FIELD_INTEGER ? sizeof(int) :
               field->Kind == FIELD_STREAM ? (UINT)field->Stream->Size : 0;
    }
    ReleaseSRWLockShared(&MimeMsiLock);
    return size;
}

void MimeMsiFieldInteger(MIME_MSI_FIELD *field, int value)
{
    clear_field(field);
    field->Kind = value == MSI_NULL_INTEGER ? FIELD_NULL : FIELD_INTEGER;
    field->Integer = value;
}

UINT MimeMsiFieldString(MIME_MSI_FIELD *field, LPCWSTR value)
{
    PWSTR copy = nullptr;
    UINT error = ERROR_SUCCESS;
    if (value && *value) {
        SIZE_T bytes = (wcslen(value) + 1) * sizeof(WCHAR);
        copy = MimeMsiAllocate(bytes);
        if (!copy)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else
            memcpy(copy, value, bytes);
    }
    if (!error) {
        clear_field(field);
        field->Kind = copy ? FIELD_STRING : FIELD_NULL;
        field->String = copy;
    }
    return error;
}

int MimeMsiFieldGetInteger(const MIME_MSI_FIELD *field)
{
    return field->Kind == FIELD_INTEGER ? field->Integer :
           field->Kind == FIELD_STRING ? string_integer(field->String) : MSI_NULL_INTEGER;
}
