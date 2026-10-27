#include "regsys.h"
#include <mime/win32/winnls.h>
#include <rtl.h>

static const WCHAR *const root_names[MIME_REG_ROOTS] = {L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_USER",
                                                         L"HKEY_LOCAL_MACHINE", L"HKEY_USERS",
                                                         L"HKEY_CURRENT_CONFIG"};

HKEY MimeRegSysRootKey(int root)
{
    switch (root) {
    case MIME_REG_ROOT_CLASSES:
        return HKEY_CLASSES_ROOT;
    case MIME_REG_ROOT_USER:
        return HKEY_CURRENT_USER;
    case MIME_REG_ROOT_MACHINE:
        return HKEY_LOCAL_MACHINE;
    case MIME_REG_ROOT_USERS:
        return HKEY_USERS;
    case MIME_REG_ROOT_CONFIG:
        return HKEY_CURRENT_CONFIG;
    }
    return nullptr;
}

const WCHAR *MimeRegSysRootName(int root)
{
    return root >= 0 && root < MIME_REG_ROOTS ? root_names[root] : nullptr;
}

int MimeRegSysFindRoot(const WCHAR *name, SIZE_T length)
{
    for (int i = 0; i < MIME_REG_ROOTS; i++) {
        const WCHAR *n = root_names[i];
        SIZE_T k = 0;
        while (k < length && n[k] && towupper(name[k]) == n[k])
            k++;
        if (k == length && !n[k])
            return i;
    }
    return -1;
}

LSTATUS MimeRegSysDeleteTree(HKEY root, const WCHAR *subkey, DWORD view)
{
    HKEY key;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | view, &key) == ERROR_SUCCESS) {
        WCHAR name[260];
        DWORD index = 0;
        for (;;) {
            DWORD length = RTL_NUMBER_OF(name);
            if (RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            /* a key that stays, a protected one, is stepped over. */
            if (MimeRegSysDeleteTree(key, name, view) != ERROR_SUCCESS)
                index++;
        }
        RegCloseKey(key);
    }
    return view ? RegDeleteKeyExW(root, subkey, view, 0) : RegDeleteKeyW(root, subkey);
}

/* ------------------------------------------------------------------ the reader */

typedef struct IMPORT {
    HANDLE File;
    const MimeRegSysOptions *Options;
} IMPORT;

static long import_read(void *context, void *buffer, uint32_t size)
{
    DWORD read;
    if (!ReadFile(((IMPORT *)context)->File, buffer, size, &read, nullptr))
        return -1;
    return (long)read;
}

static void *import_realloc(void *context, void *memory, SIZE_T size)
{
    HANDLE heap = GetProcessHeap();
    if (!size) {
        if (memory)
            HeapFree(heap, 0, memory);
        return nullptr;
    }
    return memory ? HeapReAlloc(heap, 0, memory, size) : HeapAlloc(heap, 0, size);
}

static uint32_t import_ansi(void *context, const char *text, uint32_t bytes, MimeRegChar *out, uint32_t capacity)
{
    int n = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, text, (int)bytes, out, (int)capacity);
    return n > 0 ? (uint32_t)n : 0;
}

static int import_create(void *context, int root, const MimeRegChar *subkey, void **key)
{
    IMPORT *import = context;
    HKEY result;
    LSTATUS status = RegCreateKeyExW(MimeRegSysRootKey(root), (const WCHAR *)subkey, 0, nullptr, 0,
                                     import->Options->CreateAccess | import->Options->View, nullptr, &result, nullptr);
    if (status == ERROR_SUCCESS)
        *key = result;
    return status;
}

static void import_delete_key(void *context, int root, const MimeRegChar *subkey)
{
    MimeRegSysDeleteTree(MimeRegSysRootKey(root), (const WCHAR *)subkey, ((IMPORT *)context)->Options->View);
}

static void import_close(void *context, void *key)
{
    RegCloseKey((HKEY)key);
}

static int import_set_value(void *context, void *key, const MimeRegChar *name, uint32_t type, const void *data,
                            uint32_t size, int ansi)
{
    if (!ansi)
        return RegSetValueExW((HKEY)key, (const WCHAR *)name, 0, type, data, size);
    /* the 4.0 format wrote the names and the data of these types in the code page of the machine. */
    HANDLE heap = GetProcessHeap();
    char *narrow = (char *)"";
    int bytes = 0;
    if (name && *name) {
        int length = (int)wcslen((const WCHAR *)name);
        bytes = WideCharToMultiByte(CP_ACP, 0, (const WCHAR *)name, length, nullptr, 0, nullptr, nullptr);
        narrow = HeapAlloc(heap, 0, (SIZE_T)bytes + 1);
        if (!narrow)
            return ERROR_NOT_ENOUGH_MEMORY;
        WideCharToMultiByte(CP_ACP, 0, (const WCHAR *)name, length, narrow, bytes, nullptr, nullptr);
        narrow[bytes] = 0;
    }
    LSTATUS status = RegSetValueExA((HKEY)key, narrow, 0, type, data, size);
    if (bytes)
        HeapFree(heap, 0, narrow);
    return status;
}

static void import_delete_value(void *context, void *key, const MimeRegChar *name)
{
    RegDeleteValueW((HKEY)key, (const WCHAR *)name);
}

static int import_class_default(void *context, const MimeRegChar *subkey, const MimeRegChar *text, uint32_t bytes)
{
    IMPORT *import = context;
    HKEY key;
    LSTATUS status = RegCreateKeyExW(HKEY_CLASSES_ROOT, (const WCHAR *)subkey, 0, nullptr, 0,
                                     KEY_SET_VALUE | import->Options->View, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS)
        return status;
    status = RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE *)text, bytes + 2);
    RegCloseKey(key);
    return status;
}

MimeRegImportStatus MimeRegSysImport(HANDLE file, const MimeRegSysOptions *options)
{
    IMPORT import = {file, options};
    MimeRegImportHost host = {
        .Context = &import,
        .Read = import_read,
        .Realloc = import_realloc,
        .AnsiToWide = import_ansi,
        .CreateKey = import_create,
        .DeleteKey = import_delete_key,
        .CloseKey = import_close,
        .SetValue = import_set_value,
        .DeleteValue = import_delete_value,
        .SetClassDefault = import_class_default,
        .KeyNameLimit = options->RegExe ? 2960 : 10640,
        .Utf8 = !options->RegExe,
    };
    return MimeRegImport(&host);
}

/* ------------------------------------------------------------------ the writer */

static int export_write(void *context, const void *data, uint32_t bytes)
{
    DWORD written;
    return WriteFile((HANDLE)context, data, bytes, &written, nullptr) && written == bytes;
}

static uint32_t export_ansi(void *context, const MimeRegChar *text, uint32_t count, char *out, uint32_t capacity)
{
    int n = WideCharToMultiByte(CP_ACP, 0, (const WCHAR *)text, (int)count, out, (int)capacity, nullptr, nullptr);
    return n > 0 ? (uint32_t)n : 0;
}

void MimeRegSysInitWriter(MimeRegWriter *writer, HANDLE file)
{
    writer->Context = file;
    writer->Write = export_write;
    writer->WideToAnsi = export_ansi;
    writer->MultiLineStrings = 0;
}

BOOL MimeRegSysExportKey(MimeRegWriter *writer, HKEY key, const WCHAR *path, DWORD view)
{
    HANDLE heap = GetProcessHeap();
    MimeRegWriteKey(writer, (const MimeRegChar *)path);

    DWORD subkeys = 0, longest_subkey = 0, values = 0, longest_name = 0, longest_data = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, &longest_subkey, nullptr, &values, &longest_name,
                         &longest_data, nullptr, nullptr) != ERROR_SUCCESS)
        subkeys = values = 0;

    /* the data has two zero bytes behind it, so that a string without its end is read to the end. */
    DWORD name_capacity = longest_name + 2, data_capacity = longest_data + 4;
    WCHAR *name = values ? HeapAlloc(heap, 0, name_capacity * sizeof(WCHAR)) : nullptr;
    BYTE *data = values ? HeapAlloc(heap, 0, data_capacity) : nullptr;
    if (name && data) {
        for (DWORD index = 0;;) {
            DWORD name_size = name_capacity, type, data_size = data_capacity - 2;
            LSTATUS status = RegEnumValueW(key, index, name, &name_size, nullptr, &type, data, &data_size);
            if (status == ERROR_MORE_DATA) {
                BYTE *bigger = HeapReAlloc(heap, 0, data, (SIZE_T)data_size + 4);
                WCHAR *longer = bigger ? HeapReAlloc(heap, 0, name, (SIZE_T)(name_capacity * 2) * sizeof(WCHAR)) : nullptr;
                if (!bigger || !longer) {
                    if (bigger)
                        data = bigger;
                    break;
                }
                data = bigger;
                name = longer;
                data_capacity = data_size + 4;
                name_capacity *= 2;
                continue;
            }
            if (status != ERROR_SUCCESS)
                break;
            data[data_size] = data[data_size + 1] = 0;
            MimeRegWriteValue(writer, (const MimeRegChar *)name, type, data, data_size);
            if (writer->Failed)
                break;
            index++;
        }
    }
    if (name)
        HeapFree(heap, 0, name);
    if (data)
        HeapFree(heap, 0, data);
    if (writer->Failed)
        return FALSE;
    MimeRegWriteEnd(writer);

    if (subkeys) {
        SIZE_T length = wcslen(path);
        SIZE_T capacity = length + 2 + 256;
        WCHAR *child = HeapAlloc(heap, 0, capacity * sizeof(WCHAR));
        if (!child)
            return TRUE;
        memcpy(child, path, length * sizeof(WCHAR));
        child[length] = L'\\';
        for (DWORD index = 0;; index++) {
            DWORD size = 256;
            if (RegEnumKeyExW(key, index, child + length + 1, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            HKEY sub;
            if (RegOpenKeyExW(key, child + length + 1, 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | view, &sub) !=
                ERROR_SUCCESS)
                continue;
            BOOL ok = MimeRegSysExportKey(writer, sub, child, view);
            RegCloseKey(sub);
            if (!ok) {
                HeapFree(heap, 0, child);
                return FALSE;
            }
        }
        HeapFree(heap, 0, child);
    }
    return TRUE;
}
