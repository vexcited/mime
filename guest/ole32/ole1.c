#include "ole32_private.h"

/*
 * the names of the ole 1 programs windows knows, read from rgentMap of combase 26300. the class of the entry n is
 * {000300nn-0000-0000-C000-000000000046}, and 0x24 has none.
 */
static const WCHAR *const g_ole1_names[] = {
    L"ExcelWorksheet", L"ExcelChart", L"ExcelMacrosheet", L"WordDocument", L"MSPowerPoint", L"MSPowerPointSho",
    L"MSGraph", L"MSDraw", L"Note-It", L"WordArt", L"PBrush", L"Equation", L"Package", L"SoundRec", L"MPlayer",
    L"ServerDemo", L"Srtest", L"SrtInv", L"OleDemo", L"CoromandelIntegra", L"CoromandelObjServer",
    L"StanfordGraphics", L"DGraphCHART", L"DGraphDATA", L"PhotoPaint", L"CShow", L"CorelChart", L"CDraw", L"HJWIN1.0",
    L"ObjMakerOLE", L"FYI", L"FYIView", L"Stickynote", L"ShapewareVISIO10", L"ImportServer", L"SrvrTest", nullptr,
    L"Cltest", L"MS_ClipArt_Gallery", L"MSProject", L"MSWorksChart", L"MSWorksSpreadsheet", L"MinSvr",
    L"HierarchyList", L"BibRef", L"MinSvrMI", L"TestServ", L"AmiProDocument", L"WPGraphics", L"WPCharts", L"Charisma",
    L"Charisma_30", L"CharPres_30", L"Draw", L"Designer_40",
};

/* the default value of a subkey, made when it is not there. */
static LSTATUS set_default(HKEY key, LPCWSTR subkey, LPCWSTR text)
{
    HKEY made;
    LSTATUS result = RegCreateKeyExW(key, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &made, nullptr);
    if (result)
        return result;
    result = RegSetValueExW(made, nullptr, 0, REG_SZ, (const BYTE *)(text ? text : L""),
                            (DWORD)((text ? wcslen(text) : 0) + 1) * sizeof(WCHAR));
    RegCloseKey(made);
    return result;
}

/* gives the program its class in the registry, and the class its key, under the machine's classes or the user's. */
static HRESULT write_registration(LPCWSTR name, LPCWSTR class_text, BOOL per_user)
{
    HKEY read = nullptr, root = nullptr, write = nullptr;
    WCHAR user_type[256], subkey[256];
    DWORD size = sizeof(user_type), made = 0;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, name, 0, MAXIMUM_ALLOWED, &read) ||
        RegQueryValueExW(read, nullptr, nullptr, nullptr, (BYTE *)user_type, &size))
        goto failed;
    if (per_user) {
        /* only a program the user already has a key for. */
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, MAXIMUM_ALLOWED, &root) ||
            RegCreateKeyExW(root, name, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, &write, &made) ||
            made == REG_CREATED_NEW_KEY)
            goto failed;
        RegCloseKey(read);
    } else {
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, nullptr, 0, MAXIMUM_ALLOWED, &root))
            goto failed;
        write = read;
    }
    read = nullptr;
    if (set_default(write, L"CLSID", class_text))
        goto failed;
    if (!wcscmp(class_text, L"{00030003-0000-0000-C000-000000000046}"))
        set_default(write, L"PackageOnFileDrop", nullptr);
    RegCloseKey(write);
    write = nullptr;
    _snwprintf(subkey, RTL_NUMBER_OF(subkey), L"CLSID\\%ls", class_text);
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, &write, nullptr) ||
        RegSetValueExW(write, nullptr, 0, REG_SZ, (const BYTE *)user_type, size) ||
        set_default(write, L"Ole1Class", name) || set_default(write, L"ProgID", name))
        goto failed;
    RegCloseKey(write);
    RegCloseKey(root);
    return S_OK;
failed:
    if (read)
        RegCloseKey(read);
    if (root)
        RegCloseKey(root);
    if (write)
        RegCloseKey(write);
    return REGDB_E_WRITEREGDB;
}

/* a program without a class gets one from the table or from a hash of its name, which the registry then keeps. */
static HRESULT assign_class(LPCWSTR name, CLSID *clsid, BOOL force)
{
    WCHAR text[80] = {};
    memset(clsid, 0, sizeof(*clsid));
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_ole1_names); i++) {
        if (g_ole1_names[i] && !_wcsicmp(name, g_ole1_names[i])) {
            *clsid = (CLSID){0x00030000 + i, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
            OleFormatGuid(clsid, text);
            break;
        }
    }
    if (!text[0]) {
        if (!force) {
            /* an ole 1 program is one that registered the protocol. */
            HKEY program, protocol;
            if (RegOpenKeyExW(HKEY_CLASSES_ROOT, name, 0, MAXIMUM_ALLOWED, &program))
                return CO_E_CLASSSTRING;
            LSTATUS result = RegOpenKeyExW(program, L"protocol\\StdFileEditing", 0, KEY_READ, &protocol);
            RegCloseKey(program);
            if (result)
                return CO_E_CLASSSTRING;
            RegCloseKey(protocol);
        }
        USHORT hash = 0;
        for (const WCHAR *p = name; *p; p++)
            hash = (USHORT)(*p + 257 * hash);
        _snwprintf(text, RTL_NUMBER_OF(text), L"{0004%02X%02X-0000-0000-C000-000000000046}", hash & 0xFF, hash >> 8);
        OleParseGuid(text, clsid);
    }
    HRESULT written = write_registration(name, text, FALSE);
    if (FAILED(written))
        written = write_registration(name, text, TRUE);
    return force ? S_OK : written;
}

static HRESULT class_of_program(LPCWSTR name, CLSID *clsid, BOOL force, ULONG depth)
{
    HKEY program, key;
    WCHAR text[256];
    DWORD size = sizeof(text);
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, name, 0, KEY_READ, &program))
        return assign_class(name, clsid, force);
    if (!RegOpenKeyExW(program, L"CLSID", 0, KEY_READ, &key)) {
        LSTATUS result = RegGetValueW(key, nullptr, nullptr, RRF_RT_REG_SZ | RRF_NOEXPAND, nullptr, text, &size);
        RegCloseKey(key);
        if (!result) {
            RegCloseKey(program);
            return OleParseGuid(text, clsid) ? S_OK : CO_E_CLASSSTRING;
        }
    }
    /* the current version names the program that has the class. */
    size = sizeof(text);
    if (!RegGetValueW(program, L"CurVer", nullptr, RRF_RT_REG_SZ | RRF_NOEXPAND, nullptr, text, &size) &&
        CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, text, -1, name, -1) != CSTR_EQUAL) {
        RegCloseKey(program);
        return depth + 1 < 10 ? class_of_program(text, clsid, force, depth + 1) : REGDB_E_INVALIDVALUE;
    }
    RegCloseKey(program);
    return assign_class(name, clsid, force);
}

/* clbcatq of windows asks for it. after CLSIDFromOle1Class and wCLSIDFromOle1Class of combase 26300. */
WINOLEAPI HRESULT WINAPI CLSIDFromOle1Class(LPCWSTR name, LPCLSID clsid, BOOL force)
{
    if (!name || !clsid)
        return E_INVALIDARG;
    if (!*name) {
        memset(clsid, 0, sizeof(*clsid));
        return CO_E_CLASSSTRING;
    }
    return class_of_program(name, clsid, force, 0);
}

static HRESULT last_error(void)
{
    DWORD error = GetLastError();
    return (LONG)error > 0 ? HRESULT_FROM_WIN32(error) : (HRESULT)error;
}

/* 0 unknown, 1 for a 32-bit module and 2 for a 64-bit one, after CoGetModuleType of combase 26300. */
WINOLEAPI HRESULT WINAPI CoGetModuleType(LPCWSTR module, ULONG *type)
{
    if (!type)
        return E_INVALIDARG;
    *type = 0;
    if (!module || *module == L'"')
        return E_INVALIDARG;
    HANDLE file =
        CreateFileW(module, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) {
        WCHAR path[MAX_PATH];
        if (SearchPathW(nullptr, module, nullptr, MAX_PATH, path, nullptr) - 1 > MAX_PATH - 1)
            return last_error();
        file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
    }
    if (file == INVALID_HANDLE_VALUE)
        return last_error();
    HRESULT hr = S_OK;
    USHORT machine = 0;
    LARGE_INTEGER size;
    HANDLE mapping = nullptr;
    const BYTE *base = nullptr;
    if (!GetFileSizeEx(file, &size) || !(mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)) ||
        !(base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0))) {
        hr = last_error();
    } else {
        const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
        ULONG64 at = size.QuadPart >= (LONGLONG)sizeof(*dos) && dos->e_magic == IMAGE_DOS_SIGNATURE ? (ULONG)dos->e_lfanew : 0;
        if (!at || at + 4 + sizeof(IMAGE_FILE_HEADER) > (ULONG64)size.QuadPart ||
            *(const ULONG *)(base + at) != IMAGE_NT_SIGNATURE)
            hr = HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
        else
            machine = ((const IMAGE_FILE_HEADER *)(base + at + 4))->Machine;
    }
    if (base)
        UnmapViewOfFile(base);
    if (mapping)
        CloseHandle(mapping);
    CloseHandle(file);
    if (FAILED(hr))
        return hr;
    /* the hybrid images count as the machine they stand in for. */
    if (machine == IMAGE_FILE_MACHINE_I386 || machine == IMAGE_FILE_MACHINE_ARMNT || machine == 0x3A64)
        *type = 1;
    else if (machine == IMAGE_FILE_MACHINE_AMD64 || machine == IMAGE_FILE_MACHINE_ARM64 || machine == 0xA641)
        *type = 2;
    else
        return HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
    return S_OK;
}
