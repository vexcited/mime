/*
 * providing a component: the path of the key file of a component, which a program that runs through an advertised
 * shortcut or a registered class asks of the installer. the feature of the component is checked first. one that is
 * not installed is installed, one whose key file is gone is repaired, unless the caller asked not to.
 */
#include "msi_private.h"
#include <mime/win32/objbase.h>
#include <mime/win32/shobjidl.h>

#define ERROR_INSTALL_NOTUSED_ 1634L
#define ORDINARY_REPAIR 0x3A6
/* the component of the common language runtime, which has no file of its own to find. */
#define OLEAUT32_COMPONENT L"{997FA962-E067-11D1-9396-00A0C90F27F9}"

/* the modes that look at what is installed and change nothing. */
static BOOL looks_only(DWORD mode)
{
    return mode == (DWORD)INSTALLMODE_NODETECTION || mode == (DWORD)INSTALLMODE_EXISTING ||
           mode == (DWORD)INSTALLMODE_NOSOURCERESOLUTION || mode == (DWORD)INSTALLMODE_NODETECTION_ANY;
}

/* the modes that trust the registration, and do not look for the key file. */
static BOOL trusts_registration(DWORD mode)
{
    return mode == (DWORD)INSTALLMODE_NODETECTION || mode == (DWORD)INSTALLMODE_NOSOURCERESOLUTION ||
           mode == (DWORD)INSTALLMODE_NODETECTION_ANY;
}

/* the first component the product published for a feature. */
static UINT first_component(PCWSTR product, PCWSTR feature, WCHAR component[39])
{
    HKEY key, list;
    UINT error = MimeMsiOpenProductKey(product, &key, nullptr);
    if (error)
        return error;
    error = RegOpenKeyExW(key, L"FeatureComponents", 0, KEY_READ, &list);
    RegCloseKey(key);
    if (error)
        return ERROR_UNKNOWN_COMPONENT;
    PWSTR text = nullptr;
    error = MimeMsiRegistryText(list, feature, &text);
    RegCloseKey(list);
    if (error)
        return ERROR_UNKNOWN_COMPONENT;
    error = wcslen(text) >= 20 && MimeMsiExpandGuid(text, component) ? ERROR_SUCCESS : ERROR_BAD_CONFIGURATION;
    MimeMsiFree(text);
    return error;
}

UINT MimeMsiProvideComponent(PCWSTR product, PCWSTR feature, PCWSTR component, DWORD mode, PWSTR path, LPDWORD size)
{
    WCHAR packed[33], code[39];
    if (!product || !feature || wcslen(feature) > MAX_FEATURE_CHARS || !MimeMsiPackGuid(product, packed) ||
        (component && *component && !MimeMsiPackGuid(component, packed)) || (path && !size))
        return ERROR_INVALID_PARAMETER;
    INSTALLSTATE state = MsiQueryFeatureStateW(product, feature);
    switch (state) {
    case INSTALLSTATE_UNKNOWN:
        return MsiQueryProductStateW(product) == INSTALLSTATE_UNKNOWN ? ERROR_UNKNOWN_PRODUCT
                                                                      : ERROR_UNKNOWN_FEATURE;
    case INSTALLSTATE_INVALIDARG:
        return ERROR_INVALID_PARAMETER;
    case INSTALLSTATE_BADCONFIG:
        return ERROR_BAD_CONFIGURATION;
    default:
        break;
    }
    if (component && *component) {
        wcscpy(code, component);
    } else {
        UINT error = first_component(product, feature, code);
        if (error)
            return error;
    }
    /* a mode of reinstalling is a repair asked for, whatever the state. */
    if ((LONG)mode > 0) {
        UINT error = MsiReinstallFeatureW(product, feature, mode);
        if (error)
            return error;
        state = MsiQueryFeatureStateW(product, feature);
    }
    if ((state == INSTALLSTATE_LOCAL || state == INSTALLSTATE_SOURCE || state == INSTALLSTATE_DEFAULT) &&
        !trusts_registration(mode) && MsiGetComponentPathW(product, code, nullptr, nullptr) == INSTALLSTATE_ABSENT)
        state = INSTALLSTATE_BROKEN;
    UINT error = ERROR_SUCCESS;
    switch (state) {
    case INSTALLSTATE_BROKEN:
        /* the key file is gone. */
        if (looks_only(mode))
            return ERROR_FILE_NOT_FOUND;
        error = MsiReinstallFeatureW(product, feature, ORDINARY_REPAIR);
        break;
    case INSTALLSTATE_ADVERTISED:
    case INSTALLSTATE_ABSENT:
        /* a feature that is not installed is installed on demand. */
        if (looks_only(mode))
            return ERROR_FILE_NOT_FOUND;
        error = MsiConfigureFeatureW(product, feature, INSTALLSTATE_DEFAULT);
        break;
    case INSTALLSTATE_SOURCE:
        if (mode == (DWORD)INSTALLMODE_NOSOURCERESOLUTION)
            return ERROR_INSTALL_SOURCE_ABSENT;
        break;
    default:
        break;
    }
    if (error)
        return error;
    WCHAR probe[1];
    DWORD none = 0;
    state = MsiGetComponentPathW(product, code, path ? path : probe, path ? size : &none);
    if (!path && size)
        *size = none;
    switch (state) {
    case INSTALLSTATE_LOCAL:
    case INSTALLSTATE_SOURCE:
        return ERROR_SUCCESS;
    case INSTALLSTATE_ABSENT:
        return trusts_registration(mode) ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
    case INSTALLSTATE_MOREDATA:
        return path ? ERROR_MORE_DATA : ERROR_SUCCESS;
    case INSTALLSTATE_NOTUSED:
        return ERROR_INSTALL_NOTUSED_;
    default:
        return ERROR_INSTALL_FAILURE;
    }
}

UINT WINAPI MsiProvideComponentW(LPCWSTR product, LPCWSTR feature, LPCWSTR component, DWORD mode, LPWSTR path,
                                 LPDWORD size)
{
    if (!product || wcslen(product) != 38 || !component || wcslen(component) != 38 || !feature ||
        wcslen(feature) > MAX_FEATURE_CHARS || (path && !size) ||
        (mode <= 0xFFFFFFFCu && (mode & 0xFFFFFC00u)))
        return ERROR_INVALID_PARAMETER;
    return MimeMsiProvideComponent(product, feature, component, mode, path, size);
}

/*
 * the descriptor as a program is started from it: the path of the component in quotes, and the arguments the
 * descriptor carries after it. the size counts both.
 */
UINT WINAPI MsiProvideComponentFromDescriptorW(LPCWSTR descriptor, LPWSTR path, LPDWORD size, LPDWORD args)
{
    WCHAR product[39], feature[MAX_FEATURE_CHARS + 1], component[39];
    DWORD consumed, rest;
    BOOL bar;
    if (!descriptor || (path && !size) ||
        !MimeMsiDecompose(descriptor, TRUE, product, feature, component, &consumed, &rest, &bar))
        return ERROR_INVALID_PARAMETER;
    if (args)
        *args = 0;
    DWORD capacity = size ? *size : 0;
    if (size && capacity)
        *size = capacity - 1;
    UINT error = ERROR_SUCCESS;
    WCHAR special[MAX_PATH];
    PWSTR inside = path ? path + 1 : nullptr;
    if (!wcscmp(component, OLEAUT32_COMPONENT) || bar) {
        /* a program of the runtime, or the runtime library itself, has no installed component to look for. */
        if (bar) {
            UINT length = GetSystemDirectoryW(special, MAX_PATH);
            if (!length || length + 12 >= MAX_PATH)
                return ERROR_INSTALL_FAILURE;
            wcscpy(special + length, L"\\mscoree.dll");
        } else {
            wcscpy(special, L"oleaut32.dll");
        }
        SIZE_T length = wcslen(special);
        if (size)
            *size = (DWORD)length;
        if (inside && (capacity < 2 || length + 1 > capacity - 1))
            error = ERROR_MORE_DATA;
        else if (inside)
            memcpy(inside, special, (length + 1) * sizeof(WCHAR));
    } else {
        error = MimeMsiProvideComponent(product, feature, component, 0, inside, size);
    }
    if (!error && path) {
        if (rest + 3 > capacity - *size)
            error = ERROR_MORE_DATA;
        else {
            path[0] = L'"';
            path[*size + 1] = L'"';
            memcpy(&path[*size + 2], descriptor + consumed, (rest + 1) * sizeof(WCHAR));
        }
    }
    if (size)
        *size += rest + 2;
    return error;
}

/* ------------------------------------------------------------------ the target of a shortcut */

/* the unicode form of the descriptor of an advertised shortcut, in the block the shell keeps. */
static BOOL shortcut_descriptor(PCWSTR path, PWSTR product, PWSTR feature, PWSTR component)
{
    if (!path)
        return FALSE;
    IShellLinkW *link;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link)))
        return FALSE;
    BOOL found = FALSE;
    IShellLinkDataList *list = nullptr;
    IPersistFile *file = nullptr;
    if (SUCCEEDED(link->lpVtbl->QueryInterface(link, &IID_IShellLinkDataList, (void **)&list)) &&
        SUCCEEDED(link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&file)) &&
        SUCCEEDED(file->lpVtbl->Load(file, path, STGM_READ))) {
        EXP_DARWIN_LINK *block = nullptr;
        if (SUCCEEDED(list->lpVtbl->CopyDataBlock(list, EXP_DARWIN_ID_SIG, (void **)&block)) && block) {
            found = MimeMsiDecompose(block->szwDarwinID, TRUE, product, feature, component, nullptr, nullptr, nullptr);
            LocalFree(block);
        }
    }
    if (file)
        file->lpVtbl->Release(file);
    if (list)
        list->lpVtbl->Release(list);
    link->lpVtbl->Release(link);
    return found;
}

/*
 * a shortcut that is not advertised has no descriptor, which is a failure of the call. a thread that is in the
 * multithreaded apartment cannot use the shell link and fails too.
 */
UINT WINAPI MsiGetShortcutTargetW(LPCWSTR shortcut, LPWSTR product, LPWSTR feature, LPWSTR component)
{
    if (FAILED(CoInitialize(nullptr)))
        return ERROR_INSTALL_FAILURE;
    BOOL found = shortcut_descriptor(shortcut, product, feature, component);
    CoUninitialize();
    return found ? ERROR_SUCCESS : ERROR_FUNCTION_FAILED;
}
