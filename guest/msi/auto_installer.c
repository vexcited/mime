/*
 * the installer object, the one a script creates as WindowsInstaller.Installer, and its members.
 */
#include "automation.h"
#include <mime/win32/winver.h>
#include <mime/win32/winnls.h>

#define ENTRY(id, help, flags, method, names) {id, help, flags, method, names}
#define GET DISPATCH_PROPERTYGET
#define PUT DISPATCH_PROPERTYPUT
#define CALL DISPATCH_METHOD
#define COUNT_OF(table) ((ULONG)(sizeof(table) / sizeof((table)[0])))

typedef struct IObjectSafety IObjectSafety;
typedef struct IObjectSafetyVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IObjectSafety *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IObjectSafety *This);
    ULONG(STDMETHODCALLTYPE *Release)(IObjectSafety *This);
    HRESULT(STDMETHODCALLTYPE *GetInterfaceSafetyOptions)(IObjectSafety *This, REFIID riid, DWORD *supported,
                                                          DWORD *enabled);
    HRESULT(STDMETHODCALLTYPE *SetInterfaceSafetyOptions)(IObjectSafety *This, REFIID riid, DWORD mask,
                                                          DWORD options);
} IObjectSafetyVtbl;
struct IObjectSafety {
    const IObjectSafetyVtbl *lpVtbl;
};

typedef struct AUTO_INSTALLER {
    AUTO_OBJECT Base;
    IObjectSafety Safety;
} AUTO_INSTALLER;

#define INSTALLER_OF_SAFETY(p) ((AUTO_INSTALLER *)((BYTE *)(p) - offsetof(AUTO_INSTALLER, Safety)))

/* a script host asks whether the installer may be used from untrusted script; a policy of the machine says. */
static BOOL scripting_policy(void)
{
    HKEY key;
    DWORD value = 0, size = sizeof(value), type;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Policies\\Microsoft\\Windows\\Installer", 0, KEY_QUERY_VALUE,
                      &key))
        return FALSE;
    LSTATUS status = RegQueryValueExW(key, L"SafeForScripting", nullptr, &type, (BYTE *)&value, &size);
    RegCloseKey(key);
    return !status && type == REG_DWORD && value == 1;
}

static HRESULT WINAPI safety_query_interface(IObjectSafety *self, REFIID iid, void **out)
{
    AUTO_OBJECT *object = &INSTALLER_OF_SAFETY(self)->Base;
    return object->lpVtbl->QueryInterface((IDispatch *)object, iid, out);
}

static ULONG WINAPI safety_add_ref(IObjectSafety *self)
{
    AUTO_OBJECT *object = &INSTALLER_OF_SAFETY(self)->Base;
    return object->lpVtbl->AddRef((IDispatch *)object);
}

static ULONG WINAPI safety_release(IObjectSafety *self)
{
    AUTO_OBJECT *object = &INSTALLER_OF_SAFETY(self)->Base;
    return object->lpVtbl->Release((IDispatch *)object);
}

static HRESULT WINAPI safety_get(IObjectSafety *self, REFIID iid, DWORD *supported, DWORD *enabled)
{
    if (!supported || !enabled)
        return E_POINTER;
    DWORD safe = scripting_policy() ? 1 : 0;
    *supported = safe;
    *enabled = 0;
    if (!IsEqualGUID(iid, &IID_IDispatch))
        return E_NOINTERFACE;
    *enabled = safe;
    return S_OK;
}

static HRESULT WINAPI safety_set(IObjectSafety *self, REFIID iid, DWORD mask, DWORD options)
{
    if (!IsEqualGUID(iid, &IID_IDispatch))
        return E_NOINTERFACE;
    if (scripting_policy() && mask == 1)
        return options == 1 ? S_OK : E_FAIL;
    return E_FAIL;
}

static const IObjectSafetyVtbl safety_vtbl = {
    safety_query_interface, safety_add_ref, safety_release, safety_get, safety_set,
};

static HRESULT installer_query_extra(AUTO_OBJECT *object, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_AutoObjectSafety)) {
        *out = &((AUTO_INSTALLER *)object)->Safety;
        AutoAddRef(object);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

/* ------------------------------------------------------------------ opening things */

/* the path of a package, or "#" and the handle of the database object a script gives instead. */
static HRESULT package_path(AUTO_ARGS *args, WCHAR handle_path[20], PCWSTR *path)
{
    *path = nullptr;
    HRESULT hr = AutoGetString(AutoArgument(args, 1), path);
    if (!hr)
        return S_OK;
    MSIHANDLE database = AutoGetHandle(AutoArgument(args, 1), &IID_AutoDatabase);
    if (database - 1 > 0xFFFFFFFD)
        return hr;
    _snwprintf(handle_path, 20, L"#%i", (int)database);
    *path = handle_path;
    return S_OK;
}

static HRESULT installer_open_package(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    int options = 0;
    if (AutoPresent(args, 2)) {
        HRESULT hr = AutoGetInt(AutoArgument(args, 2), &options);
        if (hr)
            return hr;
    }
    WCHAR handle_path[20];
    PCWSTR path;
    HRESULT hr = package_path(args, handle_path, &path);
    if (hr)
        return hr;
    MSIHANDLE package = 0;
    UINT error = MsiOpenPackageExW(path, (DWORD)options, &package);
    if (error)
        return (HRESULT)error;
    return AutoSetObject(args, AutoNewSession(package, object));
}

static HRESULT installer_open_product(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    MSIHANDLE package = 0;
    UINT error = MsiOpenProductW(product, &package);
    if (error)
        return (HRESULT)error;
    return AutoSetObject(args, AutoNewSession(package, object));
}

/* the mode is a number, or a string such as a path to write the database to. */
static HRESULT installer_open_database(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, path);
    PCWSTR persist = nullptr;
    int mode = 0;
    if (AutoGetInt(AutoArgument(args, 2), &mode)) {
        HRESULT hr = AutoGetString(AutoArgument(args, 2), &persist);
        if (hr)
            return hr;
    } else {
        persist = (PCWSTR)(INT_PTR)mode;
    }
    MSIHANDLE database = 0;
    UINT error = MsiOpenDatabaseW(path, persist, &database);
    if (error)
        return error == ERROR_INVALID_PARAMETER ? DISP_E_BADINDEX : (HRESULT)error;
    return AutoSetObject(args, AutoNewDatabase(database));
}

static HRESULT installer_create_record(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, count);
    if ((UINT)count > 0xFFFF)
        return E_INVALIDARG;
    MSIHANDLE record = MsiCreateRecord((UINT)count);
    if (!record)
        return DISP_E_TYPEMISMATCH;
    return AutoSetObject(args, AutoNewRecord(record));
}

static HRESULT installer_summary_information(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, path);
    int updates = 0;
    if (AutoPresent(args, 2)) {
        HRESULT hr = AutoGetInt(AutoArgument(args, 2), &updates);
        if (hr)
            return hr;
    }
    MSIHANDLE summary = 0;
    UINT error = MsiGetSummaryInformationW(0, path, (UINT)updates, &summary);
    if (error)
        return (HRESULT)error;
    return AutoSetObject(args, AutoNewSummaryInfo(summary));
}

static HRESULT installer_ui_level(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    if (args->Flags & PUT) {
        AUTO_INT_ARG(args, 0, level);
        return MsiSetInternalUI((INSTALLUILEVEL)level, nullptr) == INSTALLUILEVEL_NOCHANGE ? DISP_E_BADINDEX : S_OK;
    }
    return AutoSetInt(args, (int)MsiSetInternalUI(INSTALLUILEVEL_NOCHANGE, nullptr));
}

/* the letters of a log mode, in the order of the bits they set, and a plus for appending. */
static HRESULT installer_enable_log(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, mode_text);
    AUTO_STRING_ARG(args, 2, file);
    static const WCHAR letters[] = L"mewuifsoarpcvxgh!";
    DWORD mode = 0, append = 0;
    for (PCWSTR at = mode_text; at && *at; at++) {
        WCHAR letter = *at;
        if (letter == L' ')
            continue;
        if (letter == L'*') {
            mode |= 0x1EFFF;
            continue;
        }
        if (letter == L'+') {
            append = 1;
            continue;
        }
        if (letter >= L'A' && letter <= L'Z')
            letter += 32;
        PCWSTR found = wcschr(letters, letter);
        if (!found || !letter)
            return DISP_E_BADINDEX;
        mode |= 1u << (found - letters);
    }
    return (HRESULT)MsiEnableLogW(mode, file, append);
}

static HRESULT installer_install_product(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    WCHAR handle_path[20];
    PCWSTR path, properties = nullptr;
    HRESULT hr = package_path(args, handle_path, &path);
    if (hr)
        return hr;
    if (AutoPresent(args, 2)) {
        hr = AutoGetString(AutoArgument(args, 2), &properties);
        if (hr)
            return hr;
    }
    return (HRESULT)MsiInstallProductW(path, properties);
}

/* the file version of this dll, four numbers. */
static HRESULT installer_version(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    WCHAR path[MAX_PATH + 1];
    if (!GetModuleFileNameW(MimeMsiModule, path, MAX_PATH))
        return E_FAIL;
    path[MAX_PATH] = 0;
    DWORD handle, size = GetFileVersionInfoSizeW(path, &handle);
    HRESULT hr = E_FAIL;
    void *block = size ? MimeMsiAllocate(size) : nullptr;
    VS_FIXEDFILEINFO *info;
    UINT length;
    if (block && GetFileVersionInfoW(path, 0, size, block) && VerQueryValueW(block, L"\\", (void **)&info, &length) &&
        length >= sizeof(*info)) {
        WCHAR text[25];
        _snwprintf(text, RTL_NUMBER_OF(text), L"%i.%i.%i.%i", HIWORD(info->dwFileVersionMS),
                   LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
        hr = AutoSetString(args, text);
    }
    MimeMsiFree(block);
    return hr;
}

static HRESULT installer_last_error_record(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    MSIHANDLE record = MsiGetLastErrorRecord();
    return AutoSetObject(args, record ? AutoNewRecord(record) : nullptr);
}

/* ------------------------------------------------------------------ the registry, files and the environment */

/* the registry value, key, class, value or subkey name a script asks for; the third argument chooses which. */
static HRESULT installer_registry_value(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    HKEY root, key = nullptr;
    BOOL remote = FALSE;
    const VARIANT *root_argument = AutoArgument(args, 1);
    if (AutoIsNumeric(root_argument)) {
        int number = 0;
        AutoGetInt(root_argument, &number);
        if (number & 0x7FFFFFF0)
            return DISP_E_BADINDEX;
        root = (HKEY)(INT_PTR)(int)(0x80000000u | (unsigned)number);
    } else {
        PCWSTR machine;
        HRESULT hr = AutoGetString(root_argument, &machine);
        if (hr)
            return hr;
        WCHAR name[260] = L"\\\\";
        if (!machine || wcslen(machine) > 258)
            return E_INVALIDARG;
        wcscat(name, machine);
        LSTATUS status = RegConnectRegistryW(name, HKEY_LOCAL_MACHINE, &root);
        if (status)
            return (HRESULT)status;
        remote = TRUE;
    }
    PCWSTR subkey, value_name = nullptr;
    HRESULT hr = AutoGetString(AutoArgument(args, 2), &subkey);
    /* what is wanted of the key: whether it exists, its default or a named value, its class, a value or subkey by position. */
    enum { EXISTS, VALUE, VALUE_AT, KEY_AT, KEY_CLASS } want = EXISTS;
    DWORD position = 0;
    if (!hr && AutoPresent(args, 3)) {
        const VARIANT *third = AutoArgument(args, 3);
        want = VALUE;
        if (third->vt != VT_NULL) {
            if (AutoIsNumeric(third)) {
                int number = 0;
                AutoGetInt(third, &number);
                if (!number) {
                    want = KEY_CLASS;
                } else if (number > 0) {
                    want = VALUE_AT;
                    position = (DWORD)number - 1;
                } else {
                    want = KEY_AT;
                    position = (DWORD)~number;
                }
            } else {
                hr = AutoGetString(third, &value_name);
            }
        }
    }
    if (hr) {
        if (remote)
            RegCloseKey(root);
        return hr;
    }
    LSTATUS status = RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &key);
    if (remote)
        RegCloseKey(root);
    if (want == EXISTS) {
        if (!status)
            RegCloseKey(key);
        return AutoSetBool(args, !status);
    }
    if (status)
        return DISP_E_BADINDEX;
    hr = S_OK;
    if (want == KEY_CLASS) {
        WCHAR class_name[261];
        DWORD size = RTL_NUMBER_OF(class_name);
        status = RegQueryInfoKeyW(key, class_name, &size, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  nullptr, nullptr);
        PWSTR big = nullptr;
        if (status == ERROR_MORE_DATA) {
            size++;
            big = MimeMsiAllocate((SIZE_T)size * sizeof(WCHAR));
            if (!big) {
                RegCloseKey(key);
                return E_OUTOFMEMORY;
            }
            status = RegQueryInfoKeyW(key, big, &size, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                      nullptr, nullptr);
        }
        RegCloseKey(key);
        if (!status)
            hr = AutoSetString(args, big ? big : class_name);
        else if (!size)
            hr = AutoSetString(args, L"");
        else
            hr = DISP_E_BADINDEX;
        MimeMsiFree(big);
        return hr;
    }
    if (want == VALUE_AT || want == KEY_AT) {
        AUTO_TEXT text;
        AutoTextBegin(&text, 130);
        for (;;) {
            DWORD size = text.Capacity;
            status = want == VALUE_AT ? RegEnumValueW(key, position, text.Buffer, &size, nullptr, nullptr, nullptr,
                                                      nullptr) :
                                        RegEnumKeyExW(key, position, text.Buffer, &size, nullptr, nullptr, nullptr,
                                                      nullptr);
            if (status != ERROR_MORE_DATA)
                break;
            text.Size = text.Capacity * 2;
            text.Size--;
            if (!AutoTextGrow(&text)) {
                break;
            }
        }
        RegCloseKey(key);
        if (!status)
            hr = AutoSetString(args, text.Buffer);
        else if (status == ERROR_NO_MORE_ITEMS)
            hr = AutoSetEmpty(args);
        else
            hr = (HRESULT)status;
        AutoTextEnd(&text);
        return hr;
    }
    /* a value, read whole whatever its length. */
    DWORD type = 0, size = 0;
    BYTE *data = nullptr;
    status = RegQueryValueExW(key, value_name, nullptr, &type, nullptr, &size);
    if (!status) {
        data = MimeMsiAllocate((SIZE_T)size + 4);
        if (!data) {
            RegCloseKey(key);
            return E_OUTOFMEMORY;
        }
        DWORD got = size;
        status = RegQueryValueExW(key, value_name, nullptr, &type, data, &got);
        size = got;
    }
    RegCloseKey(key);
    if (status) {
        MimeMsiFree(data);
        if (value_name)
            return DISP_E_BADINDEX;
        /* the default value of a key that has none is nothing. */
        return AutoSetEmpty(args);
    }
    WCHAR *text = (WCHAR *)data;
    switch (type) {
    case REG_NONE:
        hr = AutoSetEmpty(args);
        break;
    case REG_SZ:
        hr = AutoSetString(args, text);
        break;
    case REG_EXPAND_SZ: {
        DWORD needed = ExpandEnvironmentStringsW(text, nullptr, 0);
        PWSTR expanded = MimeMsiAllocate((SIZE_T)(needed + 1) * sizeof(WCHAR));
        if (!expanded) {
            hr = E_OUTOFMEMORY;
            break;
        }
        ExpandEnvironmentStringsW(text, expanded, needed + 1);
        hr = AutoSetString(args, expanded);
        MimeMsiFree(expanded);
        break;
    }
    case REG_BINARY:
        hr = AutoSetString(args, L"(REG_BINARY)");
        break;
    case REG_DWORD:
        hr = size >= sizeof(DWORD) ? AutoSetInt(args, *(int *)data) : AutoSetString(args, L"(REG_?\?)");
        break;
    case REG_DWORD_BIG_ENDIAN:
        hr = AutoSetString(args, L"(REG_DWORD_BIG_ENDIAN)");
        break;
    case REG_LINK:
        hr = AutoSetString(args, L"(REG_LINK)");
        break;
    case REG_MULTI_SZ: {
        /* the strings are separated by new lines, and the two terminators at the end stay. */
        DWORD chars = size / sizeof(WCHAR);
        for (DWORD i = 0; chars > 2 && i < chars - 2; i++)
            if (!text[i])
                text[i] = L'\n';
        hr = AutoSetString(args, text);
        break;
    }
    case REG_RESOURCE_LIST:
        hr = AutoSetString(args, L"(REG_RESOURCE_LIST)");
        break;
    case REG_FULL_RESOURCE_DESCRIPTOR:
        hr = AutoSetString(args, L"(REG_FULL_RESOURCE_DESCRIPTOR)");
        break;
    case REG_RESOURCE_REQUIREMENTS_LIST:
        hr = AutoSetString(args, L"(REG_RESOURCE_REQUIREMENTS_LIST)");
        break;
    default:
        hr = AutoSetString(args, L"(REG_?\?)");
    }
    MimeMsiFree(data);
    return hr;
}

static HRESULT installer_file_attributes(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, path);
    if (!path)
        return E_FAIL;
    return AutoSetInt(args, (int)GetFileAttributesW(path));
}

static HRESULT installer_file_size(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, path);
    if (!path)
        return E_FAIL;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return (HRESULT)GetLastError();
    DWORD type = GetFileType(file);
    if (type == FILE_TYPE_CHAR || type == FILE_TYPE_PIPE) {
        CloseHandle(file);
        return ERROR_OPEN_FAILED;
    }
    DWORD size = GetFileSize(file, nullptr);
    DWORD error = GetLastError();
    CloseHandle(file);
    if (size == INVALID_FILE_SIZE)
        return (HRESULT)error;
    return AutoSetInt(args, (int)size);
}

static HRESULT installer_file_version(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, path);
    BOOL language = FALSE;
    if (AutoPresent(args, 2)) {
        HRESULT hr = AutoGetBool(AutoArgument(args, 2), &language);
        if (hr)
            return hr;
    }
    AUTO_TEXT text;
    AutoTextBegin(&text, 128);
    UINT error;
    do
        error = language ? MsiGetFileVersionW(path, nullptr, nullptr, text.Buffer, &text.Size) :
                           MsiGetFileVersionW(path, text.Buffer, &text.Size, nullptr, nullptr);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr;
    if (!error)
        hr = AutoSetString(args, text.Buffer);
    else
        hr = error == ERROR_FILE_INVALID ? S_OK : (HRESULT)error;
    AutoTextEnd(&text);
    return hr;
}

static HRESULT installer_environment(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, name);
    if (args->Flags & PUT) {
        PCWSTR value = nullptr;
        VARIANT *variant = AutoArgument(args, 0);
        if (variant->vt) {
            HRESULT hr = AutoGetString(variant, &value);
            if (hr)
                return hr;
        }
        return SetEnvironmentVariableW(name, value) ? S_OK : DISP_E_BADINDEX;
    }
    AUTO_TEXT text;
    AutoTextBegin(&text, 1024);
    text.Buffer[0] = 0;
    DWORD length = GetEnvironmentVariableW(name, text.Buffer, text.Capacity);
    if (length >= text.Capacity) {
        text.Size = length;
        if (!AutoTextGrow(&text)) {
            AutoTextEnd(&text);
            return E_OUTOFMEMORY;
        }
        text.Buffer[0] = 0;
        GetEnvironmentVariableW(name, text.Buffer, text.Capacity);
    }
    HRESULT hr = AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

/* ------------------------------------------------------------------ products, features and components */

static HRESULT installer_product_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    return AutoSetInt(args, (int)MsiQueryProductStateW(product));
}

static HRESULT installer_product_info(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, attribute);
    AUTO_TEXT text;
    AutoTextBegin(&text, 128);
    UINT error;
    do
        error = MsiGetProductInfoW(product, attribute, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT installer_product(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, sid);
    AUTO_INT_ARG(args, 3, context);
    AUTO_OBJECT *result = AutoNewProduct(product, sid, (DWORD)context);
    return result ? AutoSetObject(args, result) : E_OUTOFMEMORY;
}

static HRESULT installer_patch(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, patch);
    AUTO_STRING_ARG(args, 2, product);
    AUTO_STRING_ARG(args, 3, sid);
    AUTO_INT_ARG(args, 4, context);
    AUTO_OBJECT *result = AutoNewPatch(patch, product, sid, (DWORD)context);
    return result ? AutoSetObject(args, result) : E_OUTOFMEMORY;
}

static HRESULT installer_configure_product(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_INT_ARG(args, 2, level);
    AUTO_INT_ARG(args, 3, state);
    return (HRESULT)MsiConfigureProductW(product, level, (INSTALLSTATE)state);
}

static HRESULT installer_reinstall_product(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_INT_ARG(args, 2, mode);
    return (HRESULT)MsiReinstallProductW(product, (DWORD)mode);
}

static HRESULT installer_collect_user_info(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    return (HRESULT)AutoApiCollectUserInfo(product);
}

static HRESULT installer_apply_patch(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, patch);
    AUTO_STRING_ARG(args, 2, package);
    AUTO_INT_ARG(args, 3, type);
    AUTO_STRING_ARG(args, 4, command);
    return (HRESULT)MsiApplyPatchW(patch, package, (INSTALLTYPE)type, command);
}

static HRESULT installer_apply_multiple_patches(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, patches);
    PCWSTR product = nullptr, properties = nullptr;
    HRESULT hr;
    if (AutoPresent(args, 2) && (hr = AutoGetString(AutoArgument(args, 2), &product)))
        return hr;
    if (AutoPresent(args, 3) && (hr = AutoGetString(AutoArgument(args, 3), &properties)))
        return hr;
    if (product && !*product)
        product = nullptr;
    if (properties && !*properties)
        properties = nullptr;
    return (HRESULT)AutoApiApplyMultiplePatches(patches, product, properties);
}

static HRESULT installer_feature_parent(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, feature);
    UINT error = ERROR_UNKNOWN_FEATURE;
    for (DWORD index = 0;; index++) {
        WCHAR name[39], parent[39];
        UINT found = MsiEnumFeaturesW(product, index, name, parent);
        if (found) {
            error = found == ERROR_NO_MORE_ITEMS ? ERROR_UNKNOWN_FEATURE : found;
            break;
        }
        if (feature && !_wcsicmp(name, feature))
            return AutoSetString(args, parent);
    }
    return (HRESULT)error;
}

static HRESULT installer_feature_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, feature);
    return AutoSetInt(args, (int)MsiQueryFeatureStateW(product, feature));
}

static HRESULT installer_use_feature(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, feature);
    int mode = 0;
    if (AutoPresent(args, 3)) {
        HRESULT hr = AutoGetInt(AutoArgument(args, 3), &mode);
        if (hr)
            return hr;
    }
    return AutoSetInt(args, (int)AutoApiUseFeature(product, feature, mode));
}

static HRESULT installer_feature_usage_count(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, feature);
    DWORD count = 0;
    UINT error = AutoApiGetFeatureUsage(product, feature, &count, nullptr);
    return error ? (HRESULT)error : AutoSetInt(args, (int)count);
}

static HRESULT installer_feature_usage_date(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, feature);
    WORD dos_date = 0;
    UINT error = AutoApiGetFeatureUsage(product, feature, nullptr, &dos_date);
    if (error)
        return (HRESULT)error;
    double date = 0;
    if (dos_date)
        DosDateTimeToVariantTime(dos_date, 0, &date);
    return AutoSetDate(args, date);
}

static HRESULT installer_configure_feature(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, feature);
    AUTO_INT_ARG(args, 3, state);
    return (HRESULT)AutoApiConfigureFeature(product, feature, (INSTALLSTATE)state);
}

static HRESULT installer_reinstall_feature(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, feature);
    AUTO_INT_ARG(args, 3, mode);
    return (HRESULT)AutoApiReinstallFeature(product, feature, (DWORD)mode);
}

static HRESULT installer_provide_component(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, feature);
    AUTO_STRING_ARG(args, 3, component);
    AUTO_INT_ARG(args, 4, mode);
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    UINT error;
    do
        error = AutoApiProvideComponent(product, feature, component, (DWORD)mode, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

/* a path of a component that is installed locally or from the source, and nothing otherwise. */
static HRESULT installer_component_path(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, component);
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    INSTALLSTATE state;
    do
        state = MsiGetComponentPathW(product, component, text.Buffer, &text.Size);
    while (state == INSTALLSTATE_MOREDATA && AutoTextGrow(&text));
    HRESULT hr = state == INSTALLSTATE_LOCAL || state == INSTALLSTATE_SOURCE ? AutoSetString(args, text.Buffer) :
                                                                              AutoSetEmpty(args);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT installer_provide_qualified_component(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, category);
    AUTO_STRING_ARG(args, 2, qualifier);
    AUTO_INT_ARG(args, 3, mode);
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    UINT error;
    do
        error = AutoApiProvideQualifiedComponent(category, qualifier, (DWORD)mode, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT installer_qualifier_description(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, category);
    AUTO_STRING_ARG(args, 2, qualifier);
    AUTO_TEXT text;
    AutoTextBegin(&text, 128);
    UINT error;
    do
        error = AutoApiGetQualifierDescription(category, qualifier, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

/* ------------------------------------------------------------------ lists */

static HRESULT installer_component_qualifiers(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, category);
    return AutoEnumerateStrings(args, AUTO_ENUM_QUALIFIERS, category, nullptr, nullptr, 0, 0);
}

static HRESULT installer_products(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoEnumerateStrings(args, AUTO_ENUM_PRODUCTS, nullptr, nullptr, nullptr, 0, 0);
}

static HRESULT installer_features(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    return AutoEnumerateStrings(args, AUTO_ENUM_FEATURES, product, nullptr, nullptr, 0, 0);
}

static HRESULT installer_components(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoEnumerateStrings(args, AUTO_ENUM_COMPONENTS, nullptr, nullptr, nullptr, 0, 0);
}

static HRESULT installer_component_clients(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, component);
    return AutoEnumerateStrings(args, AUTO_ENUM_CLIENTS, component, nullptr, nullptr, 0, 0);
}

static HRESULT installer_patches(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    return AutoEnumerateStrings(args, AUTO_ENUM_PATCHES, product, nullptr, nullptr, MSIINSTALLCONTEXT_NONE, 0);
}

static HRESULT installer_related_products(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, upgrade_code);
    return AutoEnumerateStrings(args, AUTO_ENUM_RELATED, upgrade_code, nullptr, nullptr, 0, 0);
}

static HRESULT installer_patch_files(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, patches);
    return AutoEnumerateStrings(args, AUTO_ENUM_PATCH_FILES, product, patches, nullptr, 0, 0);
}

/* an empty string in an enumeration filter is no filter. */
static PCWSTR unless_empty(PCWSTR text)
{
    return text && !*text ? nullptr : text;
}

static HRESULT installer_products_ex(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, sid);
    AUTO_INT_ARG(args, 3, contexts);
    product = unless_empty(product);
    sid = unless_empty(sid);
    AUTO_LIST *list = AutoListCreate(TRUE);
    if (!list)
        return E_OUTOFMEMORY;
    AUTO_TEXT user;
    AutoTextBegin(&user, 100);
    UINT error;
    for (DWORD index = 0;; index++) {
        WCHAR installed[39];
        MSIINSTALLCONTEXT context = MSIINSTALLCONTEXT_NONE;
        AutoTextReset(&user);
        user.Buffer[0] = 0;
        do
            error = MsiEnumProductsExW(product, sid, (DWORD)contexts, index, installed, &context, user.Buffer,
                                       &user.Size);
        while (error == ERROR_MORE_DATA && AutoTextGrow(&user));
        if (error)
            break;
        AUTO_OBJECT *item = AutoNewProduct(installed, user.Buffer, (DWORD)context);
        if (!item || !AutoListAddObject(list, item)) {
            if (item)
                AutoRelease(item);
            error = ERROR_OUTOFMEMORY;
            break;
        }
    }
    AutoTextEnd(&user);
    if (error != ERROR_NO_MORE_ITEMS) {
        AutoListDiscard(list);
        return error == ERROR_OUTOFMEMORY ? E_OUTOFMEMORY : (error ? AutoWin32ToHresult(error) : E_FAIL);
    }
    return AutoListReturn(args, list);
}

static HRESULT installer_patches_ex(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, sid);
    AUTO_INT_ARG(args, 3, contexts);
    AUTO_INT_ARG(args, 4, filter);
    product = unless_empty(product);
    sid = unless_empty(sid);
    AUTO_LIST *list = AutoListCreate(TRUE);
    if (!list)
        return E_OUTOFMEMORY;
    AUTO_TEXT user;
    AutoTextBegin(&user, 100);
    UINT error;
    for (DWORD index = 0;; index++) {
        WCHAR patch[39], target[39];
        MSIINSTALLCONTEXT context = MSIINSTALLCONTEXT_NONE;
        AutoTextReset(&user);
        user.Buffer[0] = 0;
        do
            error = MsiEnumPatchesExW(product, sid, (DWORD)contexts, (DWORD)filter, index, patch, target, &context,
                                      user.Buffer, &user.Size);
        while (error == ERROR_MORE_DATA && AutoTextGrow(&user));
        if (error)
            break;
        AUTO_OBJECT *item = AutoNewPatch(patch, target, user.Buffer, (DWORD)context);
        if (!item || !AutoListAddObject(list, item)) {
            if (item)
                AutoRelease(item);
            error = ERROR_OUTOFMEMORY;
            break;
        }
    }
    AutoTextEnd(&user);
    if (error != ERROR_NO_MORE_ITEMS) {
        AutoListDiscard(list);
        return error == ERROR_OUTOFMEMORY ? E_OUTOFMEMORY : (error ? AutoWin32ToHresult(error) : E_FAIL);
    }
    return AutoListReturn(args, list);
}

static HRESULT installer_components_ex(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, sid);
    AUTO_INT_ARG(args, 2, contexts);
    sid = unless_empty(sid);
    AUTO_LIST *list = AutoListCreate(TRUE);
    if (!list)
        return E_OUTOFMEMORY;
    AUTO_TEXT user;
    AutoTextBegin(&user, 260);
    UINT error;
    for (DWORD index = 0;; index++) {
        WCHAR component[39];
        MSIINSTALLCONTEXT context = MSIINSTALLCONTEXT_NONE;
        AutoTextReset(&user);
        user.Buffer[0] = 0;
        do
            error = MsiEnumComponentsExW(sid, (DWORD)contexts, index, component, &context, user.Buffer, &user.Size);
        while (error == ERROR_MORE_DATA && AutoTextGrow(&user));
        if (error)
            break;
        AUTO_OBJECT *item = AutoNewComponent(component, user.Buffer, context);
        if (!item || !AutoListAddObject(list, item)) {
            if (item)
                AutoRelease(item);
            error = ERROR_OUTOFMEMORY;
            break;
        }
    }
    AutoTextEnd(&user);
    if (error != ERROR_NO_MORE_ITEMS) {
        AutoListDiscard(list);
        return error == ERROR_OUTOFMEMORY ? E_OUTOFMEMORY : E_FAIL;
    }
    return AutoListReturn(args, list);
}

static HRESULT installer_component_clients_ex(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, component);
    AUTO_STRING_ARG(args, 2, sid);
    AUTO_INT_ARG(args, 3, contexts);
    component = unless_empty(component);
    sid = unless_empty(sid);
    AUTO_LIST *list = AutoListCreate(TRUE);
    if (!list)
        return E_OUTOFMEMORY;
    AUTO_TEXT user;
    AutoTextBegin(&user, 260);
    UINT error;
    for (DWORD index = 0;; index++) {
        WCHAR product[39];
        MSIINSTALLCONTEXT context = MSIINSTALLCONTEXT_NONE;
        AutoTextReset(&user);
        user.Buffer[0] = 0;
        do
            error = MsiEnumClientsExW(component, sid, (DWORD)contexts, index, product, &context, user.Buffer,
                                         &user.Size);
        while (error == ERROR_MORE_DATA && AutoTextGrow(&user));
        if (error)
            break;
        AUTO_OBJECT *item = AutoNewComponentClient(component, product, user.Buffer, context);
        if (!item || !AutoListAddObject(list, item)) {
            if (item)
                AutoRelease(item);
            error = ERROR_OUTOFMEMORY;
            break;
        }
    }
    AutoTextEnd(&user);
    if (error != ERROR_NO_MORE_ITEMS) {
        AutoListDiscard(list);
        return error == ERROR_OUTOFMEMORY ? E_OUTOFMEMORY : E_FAIL;
    }
    return AutoListReturn(args, list);
}

static HRESULT installer_component_path_ex(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, component);
    AUTO_STRING_ARG(args, 3, sid);
    AUTO_INT_ARG(args, 4, context);
    product = unless_empty(product);
    component = unless_empty(component);
    sid = unless_empty(sid);
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    INSTALLSTATE state;
    do
        state = MsiGetComponentPathExW(product, component, sid, (MSIINSTALLCONTEXT)context, text.Buffer, &text.Size);
    while (state == INSTALLSTATE_MOREDATA && AutoTextGrow(&text));
    AUTO_OBJECT *result = AutoNewComponentPath(component, text.Buffer, state);
    AutoTextEnd(&text);
    return result ? AutoSetObject(args, result) : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ patches and sources */

static HRESULT installer_patch_info(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, patch);
    AUTO_STRING_ARG(args, 2, attribute);
    AUTO_TEXT text;
    AutoTextBegin(&text, 128);
    UINT error;
    do
        error = MsiGetPatchInfoW(patch, attribute, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

/* the transforms of the patch of a product, found by walking the patches of the product. */
static HRESULT installer_patch_transforms(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, patch);
    AUTO_TEXT transforms;
    AutoTextBegin(&transforms, 128);
    HRESULT hr = S_OK;
    UINT error = ERROR_SUCCESS;
    for (DWORD index = 0; !error; index++) {
        WCHAR code[39];
        AutoTextReset(&transforms);
        do
            error = MsiEnumPatchesW(product, index, code, transforms.Buffer, &transforms.Size);
        while (error == ERROR_MORE_DATA && AutoTextGrow(&transforms));
        if (!error && patch && !wcscmp(code, patch)) {
            hr = AutoSetString(args, transforms.Buffer);
            AutoTextEnd(&transforms);
            return hr;
        }
    }
    AutoTextEnd(&transforms);
    return (HRESULT)error;
}

static HRESULT installer_add_source(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, user);
    AUTO_STRING_ARG(args, 3, source);
    return (HRESULT)MsiSourceListAddSourceW(product, user, 0, source);
}

static HRESULT installer_clear_source_list(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, user);
    return (HRESULT)AutoApiSourceListClearAll(product, user, 0);
}

static HRESULT installer_force_source_resolution(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    AUTO_STRING_ARG(args, 2, user);
    return (HRESULT)AutoApiSourceListForceResolution(product, user, 0);
}

static HRESULT installer_shortcut_target(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, shortcut);
    WCHAR product[39] = L"", feature[39] = L"", component[39] = L"";
    UINT error = AutoApiGetShortcutTarget(shortcut, product, feature, component);
    if (error)
        return (HRESULT)error;
    MSIHANDLE record = MsiCreateRecord(3);
    MsiRecordSetStringW(record, 1, product);
    MsiRecordSetStringW(record, 2, feature);
    MsiRecordSetStringW(record, 3, component);
    return AutoSetObject(args, AutoNewRecord(record));
}

static HRESULT installer_file_hash(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, path);
    AUTO_INT_ARG(args, 2, options);
    MSIFILEHASHINFO hash = {sizeof(MSIFILEHASHINFO)};
    UINT error = MsiGetFileHashW(path, (DWORD)options, &hash);
    if (error)
        return (HRESULT)error;
    MSIHANDLE record = MsiCreateRecord(4);
    if (!record)
        return DISP_E_TYPEMISMATCH;
    for (UINT i = 0; i < 4; i++)
        MsiRecordSetInteger(record, i + 1, (int)hash.dwData[i]);
    return AutoSetObject(args, AutoNewRecord(record));
}

/* the signature information of a file: the encoded certificate or the hash, as an array of bytes. */
static HRESULT installer_file_signature_info(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, path);
    AUTO_INT_ARG(args, 2, flags);
    AUTO_INT_ARG(args, 3, format);
    if (format != 0 && format != 1)
        return E_INVALIDARG;
    BYTE *data = nullptr;
    DWORD size = 0;
    UINT error = AutoApiGetFileSignature(path, (DWORD)flags, format == 1, &data, &size);
    if (error)
        return AutoWin32ToHresult(error);
    if (!data || !args->Result) {
        if (data)
            LocalFree(data);
        return data ? S_OK : E_FAIL;
    }
    SAFEARRAYBOUND bound = {size, 0};
    SAFEARRAY *array = SafeArrayCreate(VT_UI1, 1, &bound);
    void *bytes = nullptr;
    HRESULT hr = array ? SafeArrayAccessData(array, &bytes) : E_OUTOFMEMORY;
    if (!hr) {
        memcpy(bytes, data, size);
        SafeArrayUnaccessData(array);
        args->Result->vt = VT_ARRAY | VT_UI1;
        args->Result->parray = array;
    } else if (array) {
        SafeArrayDestroy(array);
    }
    LocalFree(data);
    return hr;
}

static HRESULT installer_remove_patches(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, patches);
    AUTO_STRING_ARG(args, 2, product);
    AUTO_INT_ARG(args, 3, type);
    PCWSTR properties = nullptr;
    if (AutoPresent(args, 4)) {
        HRESULT hr = AutoGetString(AutoArgument(args, 4), &properties);
        if (hr)
            return hr;
        properties = unless_empty(properties);
    }
    return (HRESULT)MsiRemovePatchesW(patches, product, (INSTALLTYPE)type, properties);
}

static HRESULT installer_extract_patch_xml(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, patch);
    AUTO_TEXT text;
    AutoTextBegin(&text, 1);
    UINT error;
    do
        error = MsiExtractPatchXMLDataW(patch, 0, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT installer_product_code(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, component);
    WCHAR product[39];
    UINT error = MsiGetProductCodeW(component, product);
    return error ? (HRESULT)error : AutoSetString(args, product);
}

static HRESULT installer_product_elevated(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, product);
    BOOL elevated = FALSE;
    UINT error = MsiIsProductElevatedW(product, &elevated);
    return error ? (HRESULT)error : AutoSetBool(args, elevated);
}

static HRESULT installer_provide_assembly(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, assembly);
    AUTO_STRING_ARG(args, 2, context);
    AUTO_INT_ARG(args, 3, mode);
    AUTO_INT_ARG(args, 4, info);
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    UINT error;
    do
        error = AutoApiProvideAssembly(assembly, context, (DWORD)mode, (DWORD)info, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT installer_product_info_from_script(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, script);
    AUTO_INT_ARG(args, 2, which);
    if ((UINT)which > 4)
        return E_INVALIDARG;
    WCHAR product[39] = L"";
    int language = 0;
    DWORD version = 0;
    AUTO_TEXT name, package;
    AutoTextBegin(&name, 260);
    AutoTextBegin(&package, 260);
    UINT error;
    do {
        AutoTextReset(&name);
        AutoTextReset(&package);
        error = AutoApiGetProductInfoFromScript(script, product, &language, &version, name.Buffer, &name.Size,
                                                package.Buffer, &package.Size);
        if (error != ERROR_MORE_DATA)
            break;
    } while (AutoTextGrow(&name) && AutoTextGrow(&package));
    HRESULT hr;
    if (error) {
        hr = (HRESULT)error;
    } else {
        switch (which) {
        case 0: hr = AutoSetString(args, product); break;
        case 1: hr = AutoSetInt(args, language); break;
        case 2: hr = AutoSetInt(args, (int)version); break;
        case 3: hr = AutoSetString(args, name.Buffer); break;
        default: hr = AutoSetString(args, package.Buffer);
        }
    }
    AutoTextEnd(&name);
    AutoTextEnd(&package);
    return hr;
}

static HRESULT installer_advertise_product(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, package);
    AUTO_INT_ARG(args, 2, context);
    if ((UINT)context > 1)
        return E_INVALIDARG;
    PCWSTR transforms = nullptr;
    int language = 0, options = 0;
    HRESULT hr;
    if (AutoPresent(args, 3)) {
        if ((hr = AutoGetString(AutoArgument(args, 3), &transforms)))
            return hr;
        transforms = unless_empty(transforms);
    }
    if (AutoPresent(args, 4) && (hr = AutoGetInt(AutoArgument(args, 4), &language)))
        return hr;
    if (AutoPresent(args, 5) && (hr = AutoGetInt(AutoArgument(args, 5), &options)))
        return hr;
    return (HRESULT)AutoApiAdvertiseProduct(package, nullptr, context != 0, transforms, (LANGID)language, 0,
                                            (DWORD)options);
}

static HRESULT installer_create_advertise_script(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, package);
    AUTO_STRING_ARG(args, 2, script);
    PCWSTR transforms = nullptr;
    int language = 0, platform = 0, options = 0;
    HRESULT hr;
    if (AutoPresent(args, 3)) {
        if ((hr = AutoGetString(AutoArgument(args, 3), &transforms)))
            return hr;
        transforms = unless_empty(transforms);
    }
    if (AutoPresent(args, 4) && (hr = AutoGetInt(AutoArgument(args, 4), &language)))
        return hr;
    if (AutoPresent(args, 5) && (hr = AutoGetInt(AutoArgument(args, 5), &platform)))
        return hr;
    if (AutoPresent(args, 6) && (hr = AutoGetInt(AutoArgument(args, 6), &options)))
        return hr;
    return (HRESULT)AutoApiAdvertiseProduct(package, script, FALSE, transforms, (LANGID)language, (DWORD)platform,
                                            (DWORD)options);
}

static HRESULT installer_advertise_script(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, script);
    AUTO_INT_ARG(args, 2, flags);
    BOOL remove;
    HRESULT hr = AutoGetBool(AutoArgument(args, 3), &remove);
    if (hr)
        return hr;
    return (HRESULT)AutoApiAdvertiseScript(script, (DWORD)flags, remove);
}

/* ------------------------------------------------------------------ the table */

const AUTO_ENTRY AutoInstallerEntries[] = {
    ENTRY(2, 9004, CALL, installer_open_package, L"OpenPackage,PackagePath,Options"),
    ENTRY(3, 9005, CALL, installer_open_product, L"OpenProduct,ProductCode"),
    ENTRY(4, 9006, CALL, installer_open_database, L"OpenDatabase,DatabasePath,OpenMode"),
    ENTRY(1, 9003, CALL, installer_create_record, L"CreateRecord,Count"),
    ENTRY(5, 9007, GET, installer_summary_information, L"SummaryInformation,DatabasePath,UpdateCount"),
    ENTRY(6, 9001, GET | PUT, installer_ui_level, L"UILevel"),
    ENTRY(7, 9002, CALL, installer_enable_log, L"EnableLog,LogMode,LogFile"),
    ENTRY(8, 9008, CALL, installer_install_product, L"InstallProduct,PackagePath,PropertyValues"),
    ENTRY(9, 9009, GET, installer_version, L"Version"),
    ENTRY(10, 9010, GET, installer_last_error_record, L"LastErrorRecord"),
    ENTRY(11, 9011, CALL, installer_registry_value, L"RegistryValue,Root,Key,Value"),
    ENTRY(13, 9013, CALL, installer_file_attributes, L"FileAttributes,FilePath"),
    ENTRY(15, 9014, CALL, installer_file_size, L"FileSize,FilePath"),
    ENTRY(16, 9015, CALL, installer_file_version, L"FileVersion,FilePath,Language"),
    ENTRY(12, 9012, GET | PUT, installer_environment, L"Environment,Variable"),
    ENTRY(17, 9017, GET, installer_product_state, L"ProductState,Product"),
    ENTRY(18, 9018, GET, installer_product_info, L"ProductInfo,Product,Attribute"),
    ENTRY(53, 9053, GET, installer_product, L"Product,Product,UserSid,Context"),
    ENTRY(56, 9056, GET, installer_patch, L"Patch,Patch,Product,UserSid,Context"),
    ENTRY(19, 9019, CALL, installer_configure_product, L"ConfigureProduct,Product,InstallLevel,InstallState"),
    ENTRY(20, 9020, CALL, installer_reinstall_product, L"ReinstallProduct,Product,ReinstallMode"),
    ENTRY(21, 9021, CALL, installer_collect_user_info, L"CollectUserInfo"),
    ENTRY(22, 9022, CALL, installer_apply_patch, L"ApplyPatch,PatchPackage,InstallPackage,InstallType,CommandLine"),
    ENTRY(51, 9051, CALL, installer_apply_multiple_patches,
          L"ApplyMultiplePatches,PatchPackages,Product,PropertiesList"),
    ENTRY(23, 9023, GET, installer_feature_parent, L"FeatureParent,Product,Feature"),
    ENTRY(24, 9024, GET, installer_feature_state, L"FeatureState,Product,Feature"),
    ENTRY(25, 9025, CALL, installer_use_feature, L"UseFeature,Product,Feature"),
    ENTRY(26, 9026, GET, installer_feature_usage_count, L"FeatureUsageCount,Product,Feature"),
    ENTRY(27, 9027, GET, installer_feature_usage_date, L"FeatureUsageDate,Product,Feature"),
    ENTRY(28, 9028, CALL, installer_configure_feature, L"ConfigureFeature,Product,Feature,InstallState"),
    ENTRY(29, 9029, CALL, installer_reinstall_feature, L"ReinstallFeature,Product,Feature,ReinstallMode"),
    ENTRY(30, 9030, CALL, installer_provide_component, L"ProvideComponent,Product,Feature,Component,InstallMode"),
    ENTRY(31, 9031, GET, installer_component_path, L"ComponentPath,Product,Component"),
    ENTRY(32, 9032, CALL, installer_provide_qualified_component,
          L"ProvideQualifiedComponent,Category,Qualifier,InstallMode"),
    ENTRY(33, 9033, GET, installer_qualifier_description, L"QualifierDescription,Category,Qualifier"),
    ENTRY(34, 9034, GET, installer_component_qualifiers, L"ComponentQualifiers"),
    ENTRY(35, 9035, GET, installer_products, L"Products"),
    ENTRY(52, 9052, GET, installer_products_ex, L"ProductsEx,Product,UserSid,Contexts"),
    ENTRY(55, 9055, GET, installer_patches_ex, L"PatchesEx,Product,UserSid,Contexts,Filter"),
    ENTRY(36, 9036, GET, installer_features, L"Features,Product"),
    ENTRY(37, 9037, GET, installer_components, L"Components"),
    ENTRY(38, 9038, GET, installer_component_clients, L"ComponentClients,Product"),
    ENTRY(39, 9039, GET, installer_patches, L"Patches,Product"),
    ENTRY(40, 9040, GET, installer_related_products, L"RelatedProducts,UpgradeCode"),
    ENTRY(41, 9041, GET, installer_patch_info, L"PatchInfo,Patch,Attribute"),
    ENTRY(42, 9042, GET, installer_patch_transforms, L"PatchTransforms,Product,Patch"),
    ENTRY(43, 9043, CALL, installer_add_source, L"AddSource,Product,User,Source"),
    ENTRY(44, 9044, CALL, installer_clear_source_list, L"ClearSourceList,Product,User"),
    ENTRY(45, 9045, CALL, installer_force_source_resolution, L"ForceSourceListResolution,Product,User"),
    ENTRY(46, 9046, GET, installer_shortcut_target, L"ShortcutTarget"),
    ENTRY(47, 9047, CALL, installer_file_hash, L"FileHash,FilePath,Options"),
    ENTRY(48, 9048, CALL, installer_file_signature_info, L"FileSignatureInfo,FilePath,Options,Format"),
    ENTRY(49, 9049, CALL, installer_remove_patches, L"RemovePatches,PatchList,Product,UninstallType,PropertyList"),
    ENTRY(57, 9057, CALL, installer_extract_patch_xml, L"ExtractPatchXMLData,PatchPath"),
    ENTRY(58, 9058, GET, installer_product_code, L"ProductCode,Component"),
    ENTRY(59, 9059, GET, installer_product_elevated, L"ProductElevated,Product"),
    ENTRY(60, 9060, CALL, installer_provide_assembly, L"ProvideAssembly,Assembly,AppContext,InstallMode,AssemblyInfo"),
    ENTRY(61, 9061, GET, installer_product_info_from_script, L"ProductInfoFromScript,ScriptFile,ProductInfo"),
    ENTRY(62, 9062, CALL, installer_advertise_product,
          L"AdvertiseProduct,PackagePath,Context,Transforms,Language,Options"),
    ENTRY(63, 9063, CALL, installer_create_advertise_script,
          L"CreateAdvertiseScript,PackagePath,ScriptFilePath,Transforms,Language,Platform,Options"),
    ENTRY(64, 9064, CALL, installer_advertise_script, L"AdvertiseScript,ScriptPath,ScriptFlags,RemoveItems"),
    ENTRY(65, 9065, GET, installer_patch_files, L"PatchFiles,ProductCode,PatchPackages"),
    ENTRY(66, 9066, GET, installer_components_ex, L"ComponentsEx,UserSid,Context"),
    ENTRY(67, 9067, GET, installer_component_clients_ex, L"ComponentClientsEx,ComponentCode,UserSid,Context"),
    ENTRY(68, 9068, GET, installer_component_path_ex, L"ComponentPathEx,ProductCode,ComponentCode,UserSid,Context"),
};

const ULONG AutoInstallerCount = COUNT_OF(AutoInstallerEntries);

AUTO_OBJECT *AutoNewInstaller(void)
{
    AUTO_INSTALLER *installer = (AUTO_INSTALLER *)AutoAllocate(sizeof(AUTO_INSTALLER), AutoInstallerEntries,
                                                              AutoInstallerCount, &IID_AutoInstaller, 0, nullptr);
    if (!installer)
        return nullptr;
    installer->Safety.lpVtbl = &safety_vtbl;
    installer->Base.QueryExtra = installer_query_extra;
    return &installer->Base;
}
