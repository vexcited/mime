/*
 * calls of the installer that name a context, the user or the machine whose installation they look at, and a few
 * that the scripting objects need: the state of a database, a hash of a file, the last error record.
 * mime keeps one catalog for the machine and one for each user, see catalog.c and enumex.c, so only those contexts
 * have anything in them.
 */
#include "msi_private.h"
#include <crypto/crypto.h>

#define MSIDBSTATE_ERROR (-1)
#define MSIDBSTATE_READ 0

/*
 * the arguments the calls of a context share, checked in the order Windows checks them: a context that is one of
 * the three, a user that goes with it. the product is looked up then, in the context and for the user.
 */
static UINT check_context(PCWSTR sid, DWORD context)
{
    if (sid && (context == MSIINSTALLCONTEXT_MACHINE || !*sid))
        return ERROR_INVALID_PARAMETER;
    if (context - 1 > 3 || context == (MSIINSTALLCONTEXT_USERUNMANAGED | MSIINSTALLCONTEXT_USERMANAGED))
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

static BOOL guid_text(PCWSTR text, WCHAR out[39])
{
    if (!text || wcslen(text) != 38)
        return FALSE;
    for (ULONG i = 0; i < 38; i++)
        out[i] = text[i] >= L'a' && text[i] <= L'z' ? text[i] - (L'a' - L'A') : text[i];
    out[38] = 0;
    WCHAR packed[33];
    return MimeMsiPackGuid(out, packed);
}

/* whether the product is installed for that context and user: 0, or the error Windows answers with. */
static UINT product_in_context(PCWSTR product, PCWSTR sid, DWORD context)
{
    WCHAR installed[39];
    MSIINSTALLCONTEXT found;
    UINT error = MsiEnumProductsExW(product, sid, context, 0, installed, &found, nullptr, nullptr);
    if (error == ERROR_NO_MORE_ITEMS)
        return ERROR_UNKNOWN_PRODUCT;
    return error;
}

UINT WINAPI MsiGetProductInfoExW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, LPCWSTR property,
                                 LPWSTR value, LPDWORD size)
{
    WCHAR code[39];
    if (!guid_text(product, code) || !property || !*property || (value && !size) ||
        (sid && (!_wcsicmp(sid, L"S-1-5-18") || !_wcsicmp(sid, L"S-1-1-0"))))
        return ERROR_INVALID_PARAMETER;
    UINT error = check_context(sid, context);
    if (error)
        return error;
    error = product_in_context(code, sid, context);
    if (error)
        return error;
    return MsiGetProductInfoW(code, property, value, size);
}

UINT WINAPI MsiQueryFeatureStateExW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, LPCWSTR feature,
                                    INSTALLSTATE *state)
{
    WCHAR code[39];
    if (!guid_text(product, code) || !feature || !*feature)
        return ERROR_INVALID_PARAMETER;
    UINT error = check_context(sid, context);
    if (error)
        return error;
    error = product_in_context(code, sid, context);
    if (error)
        return error;
    INSTALLSTATE found = MsiQueryFeatureStateW(code, feature);
    if (state)
        *state = found;
    return found == INSTALLSTATE_UNKNOWN ? ERROR_UNKNOWN_FEATURE : ERROR_SUCCESS;
}

UINT WINAPI MsiQueryComponentStateW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, LPCWSTR component,
                                    INSTALLSTATE *state)
{
    WCHAR code[39], id[39];
    if (!guid_text(product, code) || !guid_text(component, id))
        return ERROR_INVALID_PARAMETER;
    UINT error = check_context(sid, context);
    if (error)
        return error;
    error = product_in_context(code, sid, context);
    if (error)
        return error;
    INSTALLSTATE found = MsiGetComponentPathW(code, id, nullptr, nullptr);
    if (state)
        *state = found;
    return found == INSTALLSTATE_UNKNOWN ? ERROR_UNKNOWN_COMPONENT : ERROR_SUCCESS;
}

INSTALLSTATE WINAPI MsiGetComponentPathExW(LPCWSTR product, LPCWSTR component, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                           LPWSTR path, LPDWORD size)
{
    WCHAR code[39], id[39];
    if (!guid_text(product, code) || !guid_text(component, id) || (path && !size) || check_context(sid, context))
        return INSTALLSTATE_INVALIDARG;
    if (product_in_context(code, sid, context))
        return INSTALLSTATE_UNKNOWN;
    return MsiGetComponentPathW(code, id, path, size);
}

/* the products that use a component, for the contexts and the user asked about. */
UINT WINAPI MsiEnumClientsExW(LPCWSTR component, LPCWSTR sid, DWORD contexts, DWORD index, WCHAR product[39],
                              MSIINSTALLCONTEXT *context, LPWSTR out_sid, LPDWORD sid_size)
{
    WCHAR id[39];
    if (!guid_text(component, id) || !contexts || (contexts & ~(DWORD)MSIINSTALLCONTEXT_ALL) || (sid && !*sid) ||
        (out_sid && !sid_size))
        return ERROR_INVALID_PARAMETER;
    for (DWORD client = 0, seen = 0;; client++) {
        WCHAR code[39], user[256];
        MSIINSTALLCONTEXT found;
        DWORD user_size = RTL_NUMBER_OF(user);
        UINT error = MsiEnumClientsW(id, client, code);
        if (error)
            return error;
        error = MsiEnumProductsExW(code, sid, contexts, 0, code, &found, user, &user_size);
        if (error == ERROR_NO_MORE_ITEMS)
            continue;
        if (error)
            return error;
        if (seen++ < index)
            continue;
        if (product)
            memcpy(product, code, sizeof(code));
        if (context)
            *context = found;
        if (!out_sid)
            return ERROR_SUCCESS;
        if (*sid_size <= user_size) {
            *sid_size = user_size;
            return ERROR_MORE_DATA;
        }
        memcpy(out_sid, user, ((SIZE_T)user_size + 1) * sizeof(WCHAR));
        *sid_size = user_size;
        return ERROR_SUCCESS;
    }
}

/* the components of the machine, then those of the user. */
UINT WINAPI MsiEnumComponentsExW(LPCWSTR sid, DWORD contexts, DWORD index, WCHAR component[39],
                                 MSIINSTALLCONTEXT *context, LPWSTR out_sid, LPDWORD sid_size)
{
    if (!contexts || (contexts & ~(DWORD)MSIINSTALLCONTEXT_ALL) || (sid && !*sid) || (out_sid && !sid_size))
        return ERROR_INVALID_PARAMETER;
    WCHAR user[256];
    UINT error = MimeMsiCurrentSid(user);
    if (error)
        return error;
    static const struct {
        HKEY Root;
        MSIINSTALLCONTEXT Context;
    } roots[] = {{HKEY_LOCAL_MACHINE, MSIINSTALLCONTEXT_MACHINE}, {HKEY_CURRENT_USER, MSIINSTALLCONTEXT_USERUNMANAGED}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(roots); i++) {
        if (!(contexts & roots[i].Context))
            continue;
        if (roots[i].Context != MSIINSTALLCONTEXT_MACHINE && sid && wcscmp(sid, L"S-1-1-0") && _wcsicmp(sid, user))
            continue;
        HKEY catalog;
        error = RegOpenKeyExW(roots[i].Root, L"Software\\Classes\\Installer\\Components", 0, KEY_READ, &catalog);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            continue;
        if (error)
            return error;
        for (DWORD entry = 0;; entry++) {
            WCHAR packed[33], code[39];
            DWORD length = RTL_NUMBER_OF(packed);
            LSTATUS status = RegEnumKeyExW(catalog, entry, packed, &length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS)
                break;
            if (status || !MimeMsiUnpackGuid(packed, code)) {
                RegCloseKey(catalog);
                return ERROR_BAD_CONFIGURATION;
            }
            if (index) {
                index--;
                continue;
            }
            RegCloseKey(catalog);
            if (component)
                memcpy(component, code, sizeof(code));
            if (context)
                *context = roots[i].Context;
            if (!out_sid)
                return ERROR_SUCCESS;
            PCWSTR owner = roots[i].Context == MSIINSTALLCONTEXT_MACHINE ? L"" : user;
            DWORD needed = (DWORD)wcslen(owner);
            if (*sid_size <= needed) {
                *sid_size = needed;
                return ERROR_MORE_DATA;
            }
            memcpy(out_sid, owner, ((SIZE_T)needed + 1) * sizeof(WCHAR));
            *sid_size = needed;
            return ERROR_SUCCESS;
        }
        RegCloseKey(catalog);
    }
    return ERROR_NO_MORE_ITEMS;
}

/* the product of a component is the first of the products that use it. */
UINT WINAPI MsiGetProductCodeW(LPCWSTR component, LPWSTR product)
{
    if (!product || !component || wcslen(component) != 38)
        return ERROR_INVALID_PARAMETER;
    WCHAR code[39];
    UINT error = MsiEnumClientsW(component, 0, code);
    if (error == ERROR_NO_MORE_ITEMS)
        return ERROR_UNKNOWN_COMPONENT;
    if (!error)
        memcpy(product, code, sizeof(code));
    return error;
}

/* ------------------------------------------------------------------ databases */

/* a database of this installer is read only. */
int WINAPI MsiGetDatabaseState(MSIHANDLE handle)
{
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_OBJECT *object = MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    ReleaseSRWLockShared(&MimeMsiLock);
    return object ? MSIDBSTATE_READ : MSIDBSTATE_ERROR;
}

/* a table of a stored database is persistent, one the database does not have is none. */
MSICONDITION WINAPI MsiDatabaseIsTablePersistentW(MSIHANDLE handle, LPCWSTR table)
{
    if (!table)
        return MSICONDITION_ERROR;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!database)
        return MSICONDITION_ERROR;
    MIME_MSI_TABLE *loaded;
    UINT error = MimeMsiLoadTable(database, table, &loaded);
    return error ? MSICONDITION_NONE : MSICONDITION_TRUE;
}

/* the last error record: the installer keeps none, so there is never one. */
MSIHANDLE WINAPI MsiGetLastErrorRecord(void)
{
    return 0;
}

/* ------------------------------------------------------------------ files */

/* the md5 of the contents of a file, as four integers. */
UINT WINAPI MsiGetFileHashW(LPCWSTR path, DWORD options, MSIFILEHASHINFO *hash)
{
    if (!path || options || !hash || hash->dwFileHashInfoSize != sizeof(*hash))
        return ERROR_INVALID_PARAMETER;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    MimeHashCtx context;
    mime_md5.init(&context);
    UINT error = ERROR_SUCCESS;
    BYTE buffer[16384];
    for (;;) {
        DWORD read;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
            error = GetLastError();
            break;
        }
        if (!read)
            break;
        mime_md5.update(&context, buffer, read);
    }
    CloseHandle(file);
    if (error)
        return error;
    BYTE digest[16];
    mime_md5.final(&context, digest);
    memcpy(hash->dwData, digest, sizeof(digest));
    return ERROR_SUCCESS;
}
