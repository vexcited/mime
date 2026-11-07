/*
 * system stores: the registry stores under Software\Microsoft\SystemCertificates and its siblings, and the logical
 * stores windows 11 builds from them.
 *
 * a registry store is a memory store read from <store>\Certificates\<sha-1> keys, each with a Blob value holding
 * the serialized element; changes are written through as they happen. a logical store is a collection of the
 * physical stores windows predefines for its location and name.
 */
#include "crypt32_private.h"

LONG CrRegGeneration;

#define CERT_STORE_PROV_DELETED_FLAG 0x2
#define BLOB_CHUNK 0x80000
#define BLOB_MAX 0xFFFFFF
#define BLOBS_MAX 0xBEBC200
/* the store flags a logical store passes on to its physical stores. */
#define PHYSICAL_INHERITED_FLAGS 0xCFC0

/* ------------------------------------------------------------------ names and paths */

/* a parsed pvSystemStore: location, optional relocation base, the service or user and the store's name. */
typedef struct {
    DWORD location;
    HKEY base;
    WCHAR *prefix;
    WCHAR *name;
    WCHAR *buffer;
} sys_name;

#define LOC_CURRENT_USER 1
#define LOC_LOCAL_MACHINE 2
#define LOC_CURRENT_SERVICE 4
#define LOC_SERVICES 5
#define LOC_USERS 6
#define LOC_CU_GROUP_POLICY 7
#define LOC_LM_GROUP_POLICY 8
#define LOC_LM_ENTERPRISE 9

static const struct {
    HKEY root;
    PCWSTR path;
} location_keys[] = {
    [LOC_CURRENT_USER] = {HKEY_CURRENT_USER, L"Software\\Microsoft\\SystemCertificates"},
    [LOC_LOCAL_MACHINE] = {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\SystemCertificates"},
    [LOC_SERVICES] = {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Cryptography\\Services"},
    [LOC_USERS] = {HKEY_USERS, L"Software\\Microsoft\\SystemCertificates"},
    [LOC_CU_GROUP_POLICY] = {HKEY_CURRENT_USER, L"Software\\Policies\\Microsoft\\SystemCertificates"},
    [LOC_LM_GROUP_POLICY] = {HKEY_LOCAL_MACHINE, L"Software\\Policies\\Microsoft\\SystemCertificates"},
    [LOC_LM_ENTERPRISE] = {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\EnterpriseCertificates"},
};

static BOOL valid_location(DWORD location)
{
    return location == LOC_CURRENT_USER || location == LOC_LOCAL_MACHINE ||
           (location >= LOC_CURRENT_SERVICE && location <= LOC_LM_ENTERPRISE);
}

static void free_name(sys_name *n)
{
    CrFree(n->buffer);
    n->buffer = n->name = n->prefix = nullptr;
}

/* splits pvSystemStore; with need_name the store's name must be there, else only the location is. */
static BOOL parse_name(const void *para, DWORD flags, BOOL ansi, BOOL need_name, sys_name *n)
{
    memset(n, 0, sizeof(*n));
    n->location = (flags & CERT_SYSTEM_STORE_LOCATION_MASK) >> CERT_SYSTEM_STORE_LOCATION_SHIFT;
    if (!valid_location(n->location))
        return CrFail(E_INVALIDARG);
    if (flags & CERT_SYSTEM_STORE_RELOCATE_FLAG) {
        const CERT_SYSTEM_STORE_RELOCATE_PARA *relocate = para;
        if (!relocate || !relocate->hKeyBase)
            return CrFail(E_INVALIDARG);
        n->base = relocate->hKeyBase;
        para = relocate->pvSystemStore;
    }
    if (!para) {
        if (need_name)
            return CrFail(E_INVALIDARG);
        return TRUE;
    }
    DWORD chars = ansi ? (DWORD)MultiByteToWideChar(CP_ACP, 0, para, -1, nullptr, 0) : (DWORD)wcslen(para) + 1;
    if (!chars)
        return FALSE;
    n->buffer = CrAlloc(chars * sizeof(WCHAR));
    if (!n->buffer)
        return CrFail(ERROR_OUTOFMEMORY);
    if (ansi)
        MultiByteToWideChar(CP_ACP, 0, para, -1, n->buffer, (int)chars);
    else
        memcpy(n->buffer, para, chars * sizeof(WCHAR));
    /* other computers' registries are not reachable. */
    if (n->buffer[0] == '\\' && n->buffer[1] == '\\') {
        free_name(n);
        return CrFail(ERROR_NOT_SUPPORTED);
    }
    WCHAR *text = n->buffer;
    if (n->location == LOC_SERVICES || n->location == LOC_USERS) {
        WCHAR *slash = wcschr(text, '\\');
        if (slash) {
            *slash = 0;
            n->prefix = text;
            text = slash + 1;
        } else if (!need_name) {
            n->prefix = text;
            text = nullptr;
        }
    }
    n->name = text && *text ? text : nullptr;
    if (need_name && !n->name) {
        free_name(n);
        return CrFail(E_INVALIDARG);
    }
    return TRUE;
}

/* the key of a location, or of a store in it: its root and its path below the root. */
static WCHAR *store_path(const sys_name *n, PCWSTR name, HKEY *root)
{
    WCHAR *path = CrAlloc(1024 * sizeof(WCHAR));
    if (!path) {
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    int length;
    switch (n->location) {
    case LOC_CURRENT_SERVICE:
        /* no mime process runs as a service, so there is no current service to name. */
        CrFree(path);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return nullptr;
    case LOC_SERVICES:
        if (!n->prefix) {
            CrFree(path);
            SetLastError(E_INVALIDARG);
            return nullptr;
        }
        length = _snwprintf(path, 1023, L"%ls\\%ls\\SystemCertificates", location_keys[LOC_SERVICES].path, n->prefix);
        break;
    case LOC_USERS:
        if (!n->prefix) {
            CrFree(path);
            SetLastError(E_INVALIDARG);
            return nullptr;
        }
        length = _snwprintf(path, 1023, L"%ls\\%ls", n->prefix, location_keys[LOC_USERS].path);
        break;
    default:
        length = _snwprintf(path, 1023, L"%ls", location_keys[n->location].path);
        break;
    }
    if (length >= 0 && name)
        length = _snwprintf(path + length, 1023 - length, L"\\%ls", name);
    if (length < 0) {
        CrFree(path);
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return nullptr;
    }
    path[1023] = 0;
    *root = n->base ? n->base : location_keys[n->location].root;
    return path;
}

/* opens a key the way crypt32 does: read only when asked, created unless it must exist, refused if it must not. */
static LSTATUS open_key(HKEY root, PCWSTR path, DWORD flags, HKEY *out)
{
    *out = nullptr;
    if (flags & CERT_STORE_CREATE_NEW_FLAG) {
        HKEY existing;
        if (!RegOpenKeyExW(root, path, 0, KEY_READ, &existing)) {
            RegCloseKey(existing);
            return ERROR_FILE_EXISTS;
        }
    } else if (flags & (CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG)) {
        REGSAM access = (flags & CERT_STORE_READONLY_FLAG) ? KEY_READ : KEY_READ | KEY_WRITE;
        return RegOpenKeyExW(root, path, 0, access, out);
    }
    return RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, out, nullptr);
}

static void thumbprint(PCCERT_CONTEXT cert, WCHAR *out)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    BYTE hash[20];
    DWORD size = sizeof(hash);
    if (CrIsCrl(CrCert(cert)) || !CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, hash, &size))
        CrHash(CALG_SHA1, cert->pbCertEncoded, cert->cbCertEncoded, hash);
    for (DWORD i = 0; i < 20; i++) {
        out[2 * i] = hex[hash[i] >> 4];
        out[2 * i + 1] = hex[hash[i] & 15];
    }
    out[40] = 0;
}

/* ------------------------------------------------------------------ the registry provider */

static LSTATUS query_dword(HKEY key, PCWSTR name, DWORD *value)
{
    DWORD type, size = sizeof(*value);
    LSTATUS error = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)value, &size);
    if (!error && (type != REG_DWORD || size != sizeof(*value)))
        error = ERROR_INVALID_DATA;
    return error;
}

/* the Blob value of a key, or the Blob values of its Blob0, Blob1... subkeys when it was too big for one. */
static BYTE *read_blob(HKEY key, DWORD *size)
{
    DWORD type = 0, n = 0;
    LSTATUS error = RegQueryValueExW(key, L"Blob", nullptr, &type, nullptr, &n);
    if ((!error || error == ERROR_MORE_DATA) && n) {
        if (type != REG_BINARY || n > BLOB_MAX)
            return nullptr;
        BYTE *data = CrAlloc(n);
        if (data && !RegQueryValueExW(key, L"Blob", nullptr, &type, data, &n)) {
            *size = n;
            return data;
        }
        CrFree(data);
        return nullptr;
    }
    DWORD count = 0, length = 0;
    if (query_dword(key, L"BlobCount", &count) || query_dword(key, L"BlobLength", &length) || !count || !length ||
        length > BLOBS_MAX)
        return nullptr;
    BYTE *data = CrAlloc(length);
    if (!data)
        return nullptr;
    DWORD used = 0;
    for (DWORD i = 0; i < count; i++) {
        WCHAR name[16];
        _snwprintf(name, RTL_NUMBER_OF(name), L"Blob%u", i);
        HKEY part;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &part))
            break;
        n = length - used;
        error = n ? RegQueryValueExW(part, L"Blob", nullptr, &type, data + used, &n) : ERROR_INVALID_DATA;
        RegCloseKey(part);
        if (error || type != REG_BINARY)
            break;
        used += n;
    }
    if (used != length) {
        CrFree(data);
        return nullptr;
    }
    *size = length;
    return data;
}

/* adds the certificate or list of an element, which crypt32 accepts only under the key its sha-1 names. */
static void load_element(cr_store *store, PCWSTR name, const BYTE *data, DWORD size)
{
    PCCERT_CONTEXT cert = nullptr;
    if (CrAddElements(nullptr, data, size, CERT_STORE_ADD_ALWAYS, &cert) || !cert)
        return;
    WCHAR hash[41];
    thumbprint(cert, hash);
    if (!name || !_wcsicmp(name, hash))
        CrAddContext(store, CrCert(cert)->kind, cert, CERT_STORE_ADD_ALWAYS, nullptr);
    CertFreeCertificateContext(cert);
}

/* what changes when certificates come and go: the time and counts of the Certificates key. */
static ULONGLONG registry_stamp(HKEY key)
{
    HKEY certs;
    FILETIME time = {};
    DWORD subkeys = 0, values = 0;
    if (!RegOpenKeyExW(key, L"Certificates", 0, KEY_READ, &certs)) {
        RegQueryInfoKeyW(certs, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr,
                         nullptr, &time);
        RegCloseKey(certs);
    } else {
        RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr,
                         nullptr, &time);
        subkeys |= 0x80000000;
    }
    ULONGLONG stamp = ((ULONGLONG)time.dwHighDateTime << 32 | time.dwLowDateTime) * 0x100000001B3ULL;
    return stamp ^ ((ULONGLONG)subkeys << 32 | values);
}

/* reads the elements under one kind's key: its subkeys, and values left there by old versions. */
static void load_kind(cr_store *store, PCWSTR kind)
{
    cr_reg *reg = store->reg;
    HKEY certs;
    if (RegOpenKeyExW(reg->key, kind, 0, KEY_READ, &certs))
        return;
    reg->loading = TRUE;
    WCHAR name[256];
    for (DWORD i = 0;; i++) {
        DWORD chars = RTL_NUMBER_OF(name), type, size = 0;
        LSTATUS error = RegEnumValueW(certs, i, name, &chars, nullptr, &type, nullptr, &size);
        if (error == ERROR_NO_MORE_ITEMS)
            break;
        if (error || type != REG_BINARY || !size || size > BLOB_MAX)
            continue;
        BYTE *data = CrAlloc(size);
        chars = RTL_NUMBER_OF(name);
        if (data && !RegEnumValueW(certs, i, name, &chars, nullptr, &type, data, &size))
            load_element(store, nullptr, data, size);
        CrFree(data);
    }
    for (DWORD i = 0;; i++) {
        DWORD chars = RTL_NUMBER_OF(name);
        LSTATUS error = RegEnumKeyExW(certs, i, name, &chars, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS)
            break;
        HKEY key;
        if (error || RegOpenKeyExW(certs, name, 0, KEY_READ, &key))
            continue;
        DWORD size = 0;
        BYTE *data = read_blob(key, &size);
        RegCloseKey(key);
        if (data)
            load_element(store, name, data, size);
        CrFree(data);
    }
    reg->loading = FALSE;
    RegCloseKey(certs);
}

static void load_registry(cr_store *store)
{
    store->reg->stamp = registry_stamp(store->reg->key);
    load_kind(store, L"Certificates");
    load_kind(store, L"CRLs");
}

static HCERTSTORE open_registry(DWORD flags, const sys_name *n)
{
    if ((flags & (CERT_STORE_DEFER_CLOSE_UNTIL_LAST_FREE_FLAG | 0x8)) ||
        ((flags & CERT_STORE_UNSAFE_PHYSICAL_FLAG) && (flags & CERT_SYSTEM_STORE_RELOCATE_FLAG))) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    HKEY root;
    WCHAR *path = store_path(n, n->name, &root);
    if (!path)
        return nullptr;
    HKEY key;
    LSTATUS error = open_key(root, path, flags, &key);
    CrFree(path);
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    DWORD kept = flags & ~(CERT_SYSTEM_STORE_MASK | CERT_STORE_CREATE_NEW_FLAG | CERT_STORE_OPEN_EXISTING_FLAG |
                           CERT_STORE_UNSAFE_PHYSICAL_FLAG);
    cr_store *store = CrOpenMemoryStore(kept);
    cr_reg *reg = CrAllocZero(sizeof(cr_reg));
    if (!store || !reg) {
        RegCloseKey(key);
        CrFree(reg);
        if (store)
            CertCloseStore(store, 0);
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    reg->key = key;
    store->reg = reg;
    store->system = TRUE;
    /* windows makes the three context keys of a writable store as it opens it. */
    if (!(flags & CERT_STORE_READONLY_FLAG)) {
        static const PCWSTR kinds[] = {L"Certificates", L"CRLs", L"CTLs"};
        for (DWORD i = 0; i < RTL_NUMBER_OF(kinds); i++) {
            HKEY sub;
            if (!RegCreateKeyExW(key, kinds[i], 0, nullptr, 0, KEY_READ, nullptr, &sub, nullptr))
                RegCloseKey(sub);
        }
    }
    load_registry(store);
    return store;
}

BOOL CrRegCanWrite(cr_store *store)
{
    return !(store->flags & CERT_STORE_READONLY_FLAG);
}

static void free_change(cr_reg *reg);

void CrRegClose(cr_store *store)
{
    if (!store->reg)
        return;
    free_change(store->reg);
    if (store->reg->internal)
        CloseHandle(store->reg->internal);
    RegCloseKey(store->reg->key);
    CrFree(store->reg);
    store->reg = nullptr;
}

/* drops the Blob0, Blob1... subkeys and their count, left by an element too big for one value. */
static void delete_blob_parts(HKEY key)
{
    DWORD count = 0;
    if (query_dword(key, L"BlobCount", &count))
        return;
    for (DWORD i = 0; i < count; i++) {
        WCHAR name[16];
        _snwprintf(name, RTL_NUMBER_OF(name), L"Blob%u", i);
        RegDeleteTreeW(key, name);
    }
    RegDeleteValueW(key, L"BlobCount");
    RegDeleteValueW(key, L"BlobLength");
}

static LSTATUS write_blob(HKEY key, const BYTE *data, DWORD size)
{
    if (size <= BLOB_CHUNK) {
        LSTATUS error = RegSetValueExW(key, L"Blob", 0, REG_BINARY, data, size);
        if (!error)
            delete_blob_parts(key);
        return error;
    }
    RegDeleteValueW(key, L"Blob");
    delete_blob_parts(key);
    DWORD count = (size + BLOB_CHUNK - 1) / BLOB_CHUNK;
    for (DWORD i = 0; i < count; i++) {
        WCHAR name[16];
        _snwprintf(name, RTL_NUMBER_OF(name), L"Blob%u", i);
        HKEY part;
        LSTATUS error = RegCreateKeyExW(key, name, 0, nullptr, 0, KEY_WRITE, nullptr, &part, nullptr);
        if (error)
            return error;
        DWORD chunk = size - i * BLOB_CHUNK > BLOB_CHUNK ? BLOB_CHUNK : size - i * BLOB_CHUNK;
        error = RegSetValueExW(part, L"Blob", 0, REG_BINARY, data + i * BLOB_CHUNK, chunk);
        RegCloseKey(part);
        if (error)
            return error;
    }
    LSTATUS error = RegSetValueExW(key, L"BlobCount", 0, REG_DWORD, (const BYTE *)&count, sizeof(count));
    if (!error)
        error = RegSetValueExW(key, L"BlobLength", 0, REG_DWORD, (const BYTE *)&size, sizeof(size));
    return error;
}

static void wrote(cr_store *store)
{
    InterlockedIncrement(&CrRegGeneration);
    store->reg->stamp = registry_stamp(store->reg->key);
}

BOOL CrRegWriteCert(cr_store *store, PCCERT_CONTEXT cert)
{
    if (!store->reg || store->reg->loading)
        return TRUE;
    if (!CrRegCanWrite(store))
        return CrFail(E_ACCESSDENIED);
    der_writer w;
    DerWriteInit(&w);
    if (CrIsCrl(CrCert(cert)))
        CrSerializeCrl(&w, (PCCRL_CONTEXT)cert);
    else
        CrSerializeCert(&w, cert);
    if (w.error) {
        DerWriteFree(&w);
        return CrFail(ERROR_OUTOFMEMORY);
    }
    WCHAR name[41];
    thumbprint(cert, name);
    HKEY certs, key;
    LSTATUS error = RegCreateKeyExW(store->reg->key, CrIsCrl(CrCert(cert)) ? L"CRLs" : L"Certificates", 0, nullptr, 0,
                                    KEY_READ | KEY_WRITE, nullptr, &certs, nullptr);
    if (!error) {
        error = RegCreateKeyExW(certs, name, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
        if (!error) {
            error = write_blob(key, w.data, w.size);
            RegCloseKey(key);
        }
        RegCloseKey(certs);
    }
    DerWriteFree(&w);
    if (error)
        return CrFail(error);
    wrote(store);
    return TRUE;
}

BOOL CrRegDeleteCert(cr_store *store, PCCERT_CONTEXT cert)
{
    if (!store->reg || store->reg->loading)
        return TRUE;
    if (!CrRegCanWrite(store))
        return CrFail(E_ACCESSDENIED);
    WCHAR name[41];
    thumbprint(cert, name);
    HKEY certs;
    LSTATUS error = RegOpenKeyExW(store->reg->key, CrIsCrl(CrCert(cert)) ? L"CRLs" : L"Certificates", 0,
                                  KEY_READ | KEY_WRITE, &certs);
    if (error == ERROR_FILE_NOT_FOUND)
        return TRUE;
    if (!error) {
        error = RegDeleteTreeW(certs, name);
        if (error == ERROR_FILE_NOT_FOUND)
            error = 0;
        RegDeleteValueW(certs, name);
        RegCloseKey(certs);
    }
    if (error)
        return CrFail(error);
    wrote(store);
    return TRUE;
}

BOOL CrResyncStore(cr_store *store)
{
    if (store->reg) {
        CrEmptyStore(store);
        load_registry(store);
    }
    for (DWORD i = 0; store->collection && i < store->child_count; i++)
        CrResyncStore(store->children[i]);
    return TRUE;
}

/* ------------------------------------------------------------------ change notification */

typedef struct TP_WAIT *PTP_WAIT;
typedef struct TP_CALLBACK_INSTANCE *PTP_CALLBACK_INSTANCE;
typedef void(WINAPI *PTP_WAIT_CALLBACK)(PTP_CALLBACK_INSTANCE instance, void *context, PTP_WAIT wait, ULONG result);
WINBASEAPI PTP_WAIT WINAPI CreateThreadpoolWait(PTP_WAIT_CALLBACK callback, void *context, void *environment);
WINBASEAPI void WINAPI SetThreadpoolWait(PTP_WAIT wait, HANDLE handle, FILETIME *timeout);
WINBASEAPI void WINAPI WaitForThreadpoolWaitCallbacks(PTP_WAIT wait, BOOL cancel_pending);
WINBASEAPI void WINAPI CloseThreadpoolWait(PTP_WAIT wait);

#define CANCEL_REQUEST 0x80000000
/* thread agnostic, the whole subtree, names and values, as crypt32 watches a store's key. */
#define WATCH_FILTER 0x10000005

/* a program's event waiting for the next change, and the handle it is signaled through. */
typedef struct {
    HANDLE event;
    HANDLE copy;
} cr_listener;

struct cr_change {
    SRWLOCK lock;
    HANDLE event;
    PTP_WAIT wait;
    cr_listener *listeners;
    DWORD count;
};

static BOOL watch_key(HKEY key, HANDLE event)
{
    LSTATUS error = RegNotifyChangeKeyValue(key, TRUE, WATCH_FILTER, event, TRUE);
    if (error && error != ERROR_KEY_DELETED) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

static void signal_listeners(cr_listener *list, DWORD count)
{
    while (count--) {
        if (list[count].copy) {
            SetEvent(list[count].copy);
            CloseHandle(list[count].copy);
        } else {
            SetEvent(list[count].event);
        }
    }
    CrFree(list);
}

/* the registry changed: it is watched again first, then every event waiting is signaled, once. */
static void WINAPI change_callback(PTP_CALLBACK_INSTANCE instance, void *context, PTP_WAIT wait, ULONG result)
{
    cr_reg *reg = context;
    cr_change *change = reg->change;
    BOOL armed = watch_key(reg->key, change->event);
    AcquireSRWLockExclusive(&change->lock);
    cr_listener *list = change->listeners;
    DWORD count = change->count;
    change->listeners = nullptr;
    change->count = 0;
    ReleaseSRWLockExclusive(&change->lock);
    signal_listeners(list, count);
    if (armed)
        SetThreadpoolWait(wait, change->event, nullptr);
}

static void free_change(cr_reg *reg)
{
    cr_change *change = reg->change;
    if (!change)
        return;
    reg->change = nullptr;
    if (change->wait) {
        WaitForThreadpoolWaitCallbacks(change->wait, TRUE);
        CloseThreadpoolWait(change->wait);
    }
    if (change->event)
        CloseHandle(change->event);
    /* closing the store tells those still waiting. */
    signal_listeners(change->listeners, change->count);
    CrFree(change);
}

static cr_change *start_change(cr_reg *reg)
{
    if (reg->change)
        return reg->change;
    cr_change *change = CrAllocZero(sizeof(*change));
    if (!change) {
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    InitializeSRWLock(&change->lock);
    change->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    reg->change = change;
    if (change->event && watch_key(reg->key, change->event)) {
        change->wait = CreateThreadpoolWait(change_callback, reg, nullptr);
        if (change->wait) {
            SetThreadpoolWait(change->wait, change->event, nullptr);
            return change;
        }
    }
    DWORD error = GetLastError();
    free_change(reg);
    SetLastError(error);
    return nullptr;
}

/* registers an event for the next registry change, or unregisters it. */
static BOOL listen(cr_reg *reg, HANDLE event, DWORD flags)
{
    cr_change *change = start_change(reg);
    if (!change)
        return FALSE;
    AcquireSRWLockExclusive(&change->lock);
    for (DWORD i = 0; i < change->count; i++) {
        if (change->listeners[i].event != event)
            continue;
        if (flags & CANCEL_REQUEST) {
            if (change->listeners[i].copy)
                CloseHandle(change->listeners[i].copy);
            memmove(&change->listeners[i], &change->listeners[i + 1], (change->count - i - 1) * sizeof(cr_listener));
            change->count--;
        }
        ReleaseSRWLockExclusive(&change->lock);
        return TRUE;
    }
    BOOL ok = TRUE;
    if (!(flags & CANCEL_REQUEST)) {
        HANDLE copy = nullptr;
        cr_listener *list = nullptr;
        if (!(flags & CERT_STORE_CTRL_INHIBIT_DUPLICATE_HANDLE_FLAG) &&
            !DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
            ok = FALSE;
        if (ok && change->count < 0x100000) {
            list = CrAlloc((change->count + 1) * sizeof(cr_listener));
            ok = list != nullptr;
            if (!ok)
                SetLastError(ERROR_OUTOFMEMORY);
        } else if (ok) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            ok = FALSE;
        }
        if (ok) {
            memcpy(list, change->listeners, change->count * sizeof(cr_listener));
            CrFree(change->listeners);
            change->listeners = list;
            list[change->count].event = event;
            list[change->count].copy = copy;
            change->count++;
        } else if (copy) {
            DWORD error = GetLastError();
            CloseHandle(copy);
            SetLastError(error);
        }
    }
    ReleaseSRWLockExclusive(&change->lock);
    return ok;
}

/* CERT_STORE_CTRL_RESYNC: the event, if there is one, is listed again, and the store is read if the registry changed. */
static BOOL resync_registry(cr_store *store, HANDLE event, DWORD flags)
{
    cr_reg *reg = store->reg;
    if (event && !listen(reg, event, flags))
        return FALSE;
    if (reg->internal) {
        if (WaitForSingleObject(reg->internal, 0) == WAIT_TIMEOUT)
            return TRUE;
        if (!listen(reg, reg->internal, CERT_STORE_CTRL_INHIBIT_DUPLICATE_HANDLE_FLAG))
            return FALSE;
    }
    CrEmptyStore(store);
    load_registry(store);
    return TRUE;
}

/* CERT_STORE_CTRL_NOTIFY_CHANGE and CERT_STORE_CTRL_CANCEL_NOTIFY. */
static BOOL notify_change(cr_store *store, HANDLE event, DWORD flags)
{
    cr_reg *reg = store->reg;
    BOOL created = FALSE;
    if (!reg->internal) {
        HANDLE internal = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!internal)
            return FALSE;
        if (!resync_registry(store, internal, CERT_STORE_CTRL_INHIBIT_DUPLICATE_HANDLE_FLAG)) {
            DWORD error = GetLastError();
            listen(reg, internal, CANCEL_REQUEST);
            CloseHandle(internal);
            SetLastError(error);
            return FALSE;
        }
        reg->internal = internal;
        created = TRUE;
    }
    if (!event)
        return TRUE;
    if (!created && !(flags & CANCEL_REQUEST))
        return resync_registry(store, event, flags);
    return listen(reg, event, flags);
}

BOOL CrControlStore(cr_store *store, DWORD flags, DWORD type, HANDLE *event)
{
    if (!store->reg) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    HANDLE handle = event ? *event : nullptr;
    BOOL ok;
    CrLock();
    switch (type) {
    case CERT_STORE_CTRL_RESYNC:
        ok = resync_registry(store, handle, flags);
        break;
    case CERT_STORE_CTRL_NOTIFY_CHANGE:
        ok = notify_change(store, handle, flags);
        break;
    case CERT_STORE_CTRL_COMMIT:
        ok = TRUE;
        break;
    case CERT_STORE_CTRL_CANCEL_NOTIFY:
        ok = handle ? notify_change(store, handle, CANCEL_REQUEST) : TRUE;
        break;
    default:
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        ok = FALSE;
        break;
    }
    CrUnlock();
    return ok;
}

void CrAutoResync(cr_store *store)
{
    if (store->auto_event) {
        if (WaitForSingleObject(store->auto_event, 0) == WAIT_OBJECT_0) {
            HANDLE event = store->auto_event;
            CertControlStore(store, CERT_STORE_CTRL_INHIBIT_DUPLICATE_HANDLE_FLAG, CERT_STORE_CTRL_RESYNC, &event);
        }
        return;
    }
    for (DWORD i = 0; store->collection && i < store->child_count; i++)
        CrAutoResync(store->children[i]);
}

BOOL CrStoreChanged(cr_store *store)
{
    if (store->reg && registry_stamp(store->reg->key) != store->reg->stamp)
        return TRUE;
    for (DWORD i = 0; store->collection && i < store->child_count; i++)
        if (CrStoreChanged(store->children[i]))
            return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ logical stores */

/*
 * the physical stores windows predefines, by the bit of the masks below:
 * the provider, where and how each opens, and whether adds go there.
 */
enum {
    PHYS_DEFAULT = 1,
    PHYS_AUTH_ROOT = 3,
    PHYS_GROUP_POLICY = 4,
    PHYS_LOCAL_MACHINE = 5,
    PHYS_USER_CERTIFICATE = 6,
    PHYS_LM_GROUP_POLICY = 7,
    PHYS_ENTERPRISE = 8,
    PHYS_SMART_CARD = 9,
    PHYS_COUNT = 12,
};

static const PCWSTR physical_names[PHYS_COUNT] = {
    L".WCOS",          L".Default",   L".SharedUser",  L".AuthRoot",    L".GroupPolicy", L".LocalMachine",
    L".UserCertificate", L".LocalMachineGroupPolicy", L".Enterprise", L".SmartCard", L".Flight", L".TestSign",
};

/* the store names each location knows without a key, in CertEnumSystemStore's order. */
static const PCWSTR predefined_names[] = {
    L"My",         L"Root",       L"Trust",         L"CA",          L"UserDS",       L"TrustedPublisher",
    L"Disallowed", L"AuthRoot",   L"TrustedPeople", L"ClientAuthIssuer", L"FlightRoot", L"TestSignRoot",
};

static DWORD predefined_mask(DWORD location)
{
    if (location == LOC_CURRENT_USER)
        return 0x3FF;
    return location == LOC_LOCAL_MACHINE ? 0xFEF : 0x3EF;
}

static BOOL is_predefined(PCWSTR name, DWORD location)
{
    DWORD mask = predefined_mask(location);
    for (DWORD i = 0; i < RTL_NUMBER_OF(predefined_names); i++)
        if ((mask & (1 << i)) && !_wcsicmp(name, predefined_names[i]))
            return TRUE;
    return FALSE;
}

static BOOL named(PCWSTR name, PCWSTR want)
{
    return !_wcsicmp(name, want);
}

/* the physical stores of a logical store, as CertEnumPhysicalStore lists them. */
static DWORD physical_mask(DWORD location, PCWSTR name)
{
    static const DWORD defaults[] = {0, 0x32, 0x112, 0, 0x22, 0x22, 0x22, 0x2, 0x2, 0x2};
    if (named(name, L"My") || named(name, L"Request") || named(name, L"FlightRoot") ||
        named(name, L"TestSignRoot") || named(name, L"SmartCardRoot"))
        return 1 << PHYS_DEFAULT;
    if (named(name, L"Root")) {
        /* the protected root list and the flight and test signing roots are off, as on a stock machine. */
        if (location == LOC_CURRENT_USER)
            return 1 << PHYS_DEFAULT | 1 << PHYS_LOCAL_MACHINE | 1 << PHYS_SMART_CARD;
        if (location == LOC_LOCAL_MACHINE)
            return 1 << PHYS_DEFAULT | 1 << PHYS_AUTH_ROOT | 1 << PHYS_GROUP_POLICY | 1 << PHYS_ENTERPRISE |
                   1 << PHYS_SMART_CARD;
        if (location == LOC_USERS)
            return 1 << PHYS_LOCAL_MACHINE;
    }
    if (named(name, L"UserDS"))
        return 1 << PHYS_USER_CERTIFICATE;
    return defaults[location];
}

typedef struct {
    BOOL registry;
    DWORD open_flags;
    DWORD add_flags;
    sys_name name;
} physical;

/* where a predefined physical store of a logical one opens; FALSE for those mime has no source for. */
static BOOL describe_physical(const sys_name *logical, DWORD bit, physical *out)
{
    memset(out, 0, sizeof(*out));
    out->name.base = logical->base;
    out->name.name = logical->name;
    DWORD here = logical->location << CERT_SYSTEM_STORE_LOCATION_SHIFT;
    switch (bit) {
    case PHYS_DEFAULT:
        out->registry = TRUE;
        out->open_flags = here;
        out->add_flags = CERT_PHYSICAL_STORE_ADD_ENABLE_FLAG;
        out->name.prefix = logical->prefix;
        break;
    case PHYS_AUTH_ROOT:
        out->registry = TRUE;
        out->open_flags = CERT_SYSTEM_STORE_LOCAL_MACHINE;
        out->add_flags = CERT_PHYSICAL_STORE_ADD_ENABLE_FLAG;
        out->name.name = (WCHAR *)L"AuthRoot";
        break;
    case PHYS_GROUP_POLICY:
        out->open_flags = (logical->location == LOC_LOCAL_MACHINE ? CERT_SYSTEM_STORE_LOCAL_MACHINE_GROUP_POLICY
                                                                    : CERT_SYSTEM_STORE_CURRENT_USER_GROUP_POLICY) |
                          CERT_STORE_READONLY_FLAG;
        break;
    case PHYS_LOCAL_MACHINE:
        out->open_flags = CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG;
        break;
    case PHYS_LM_GROUP_POLICY:
        out->open_flags = CERT_SYSTEM_STORE_LOCAL_MACHINE_GROUP_POLICY | CERT_STORE_READONLY_FLAG;
        break;
    case PHYS_ENTERPRISE:
        out->open_flags = CERT_SYSTEM_STORE_LOCAL_MACHINE_ENTERPRISE | CERT_STORE_READONLY_FLAG;
        break;
    case PHYS_SMART_CARD:
        out->registry = TRUE;
        out->open_flags = here | CERT_STORE_READONLY_FLAG;
        out->name.prefix = logical->prefix;
        out->name.name = (WCHAR *)L"SmartCardRoot";
        break;
    default:
        /* the shared user, wcos, ldap, flight and test signing stores. */
        return FALSE;
    }
    out->name.location = (out->open_flags & CERT_SYSTEM_STORE_LOCATION_MASK) >> CERT_SYSTEM_STORE_LOCATION_SHIFT;
    if (logical->base)
        out->open_flags |= CERT_SYSTEM_STORE_RELOCATE_FLAG;
    return TRUE;
}

static HCERTSTORE open_logical(DWORD flags, sys_name *n);

/* opens one physical store with what the logical store passes on; a missing one is not an error. */
static BOOL open_physical(HCERTSTORE collection, DWORD flags, physical *p, BOOL *opened)
{
    DWORD open_flags = p->open_flags | (flags & PHYSICAL_INHERITED_FLAGS);
    HCERTSTORE store = p->registry ? open_registry(open_flags, &p->name) : open_logical(open_flags, &p->name);
    if (!store) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PROC_NOT_FOUND || error == ERROR_GEN_FAILURE;
    }
    CertAddStoreToCollection(collection, store, p->add_flags, 0);
    CertCloseStore(store, 0);
    *opened = TRUE;
    return TRUE;
}

static HCERTSTORE open_logical(DWORD flags, sys_name *n)
{
    if ((flags & CERT_SYSTEM_STORE_RELOCATE_FLAG) && (flags & CERT_STORE_UNSAFE_PHYSICAL_FLAG)) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    flags &= ~CERT_STORE_UNSAFE_PHYSICAL_FLAG;
    if (flags & (CERT_STORE_DEFER_CLOSE_UNTIL_LAST_FREE_FLAG | 0x8)) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    if (flags & CERT_STORE_CREATE_NEW_FLAG) {
        HKEY root, key;
        WCHAR *path = store_path(n, n->name, &root);
        if (!path)
            return nullptr;
        LSTATUS error = open_key(root, path, CERT_STORE_CREATE_NEW_FLAG, &key);
        CrFree(path);
        if (error) {
            SetLastError(error);
            return nullptr;
        }
        RegCloseKey(key);
        flags &= ~CERT_STORE_CREATE_NEW_FLAG;
    }
    cr_store *collection = CrOpenMemoryStore(flags & ~(CERT_SYSTEM_STORE_MASK | CERT_STORE_OPEN_EXISTING_FLAG));
    if (!collection)
        return nullptr;
    collection->collection = TRUE;
    collection->system = TRUE;
    DWORD mask = physical_mask(n->location, n->name);
    BOOL opened = FALSE;
    for (DWORD bit = 0; bit < PHYS_COUNT; bit++) {
        physical p;
        if (!(mask & (1 << bit)) || !describe_physical(n, bit, &p))
            continue;
        if (!open_physical(collection, flags, &p, &opened)) {
            DWORD error = GetLastError();
            CertCloseStore(collection, 0);
            SetLastError(error);
            return nullptr;
        }
    }
    if (!opened && !is_predefined(n->name, n->location) && !named(n->name, L"Request")) {
        CertCloseStore(collection, 0);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return nullptr;
    }
    return collection;
}

/* a physical store by "<system store>\<physical name>". */
static HCERTSTORE open_physical_by_name(DWORD flags, sys_name *n)
{
    WCHAR *slash = wcsrchr(n->name, '\\');
    if (!slash || !slash[1]) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    *slash = 0;
    PCWSTR wanted = slash + 1;
    DWORD mask = physical_mask(n->location, n->name);
    for (DWORD bit = 0; bit < PHYS_COUNT; bit++) {
        physical p;
        if (!(mask & (1 << bit)) || _wcsicmp(wanted, physical_names[bit]) || !describe_physical(n, bit, &p))
            continue;
        DWORD open_flags = p.open_flags | (flags & ~CERT_SYSTEM_STORE_MASK);
        return p.registry ? open_registry(open_flags, &p.name) : open_logical(open_flags, &p.name);
    }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return nullptr;
}

static BOOL unregister(sys_name *n, DWORD flags)
{
    HKEY root, location;
    WCHAR *path = store_path(n, nullptr, &root);
    if (!path)
        return FALSE;
    LSTATUS error = RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WRITE, &location);
    CrFree(path);
    if (!error) {
        error = RegDeleteTreeW(location, n->name);
        RegCloseKey(location);
    }
    if (!error)
        InterlockedIncrement(&CrRegGeneration);
    return error ? CrFail(error) : TRUE;
}

HCERTSTORE CrOpenSystemStore(ULONG_PTR provider, DWORD flags, const void *para)
{
    BOOL ansi = provider == 9 || provider == 12;
    sys_name n;
    if (!parse_name(para, flags, ansi, TRUE, &n))
        return nullptr;
    HCERTSTORE store = nullptr;
    if (flags & CERT_STORE_DELETE_FLAG) {
        /* deleting returns no store and, when it worked, no error. */
        if (provider != 14 && unregister(&n, flags & 0xFFFF4810))
            SetLastError(ERROR_SUCCESS);
        else if (provider == 14)
            SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    } else if (provider == 12 || provider == 13) {
        store = open_registry(flags & ~CERT_STORE_UNSAFE_PHYSICAL_FLAG, &n);
    } else if (provider == 14) {
        store = open_physical_by_name(flags, &n);
    } else {
        store = open_logical(flags, &n);
    }
    DWORD error = GetLastError();
    free_name(&n);
    SetLastError(error);
    return store;
}

/* ------------------------------------------------------------------ the exported functions */

HCERTSTORE WINAPI CertOpenSystemStoreW(HCRYPTPROV_LEGACY hProv, LPCWSTR szSubsystemProtocol)
{
    if (!szSubsystemProtocol) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    DWORD location =
        named(szSubsystemProtocol, L"SPC") ? CERT_SYSTEM_STORE_LOCAL_MACHINE : CERT_SYSTEM_STORE_CURRENT_USER;
    return CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, hProv, CERT_STORE_NO_CRYPT_RELEASE_FLAG | location,
                         szSubsystemProtocol);
}

HCERTSTORE WINAPI CertOpenSystemStoreA(HCRYPTPROV_LEGACY hProv, LPCSTR szSubsystemProtocol)
{
    if (!szSubsystemProtocol) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    DWORD location =
        _stricmp(szSubsystemProtocol, "SPC") ? CERT_SYSTEM_STORE_CURRENT_USER : CERT_SYSTEM_STORE_LOCAL_MACHINE;
    return CertOpenStore(CERT_STORE_PROV_SYSTEM_A, 0, hProv, CERT_STORE_NO_CRYPT_RELEASE_FLAG | location,
                         szSubsystemProtocol);
}

BOOL WINAPI CertAddEncodedCertificateToSystemStoreW(LPCWSTR szCertStoreName, const BYTE *pbCertEncoded,
                                                    DWORD cbCertEncoded)
{
    HCERTSTORE store = CertOpenSystemStoreW(0, szCertStoreName);
    if (!store)
        return FALSE;
    BOOL ok = CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, pbCertEncoded, cbCertEncoded,
                                               CERT_STORE_ADD_USE_EXISTING, nullptr);
    DWORD error = GetLastError();
    CertCloseStore(store, 0);
    SetLastError(error);
    return ok;
}

BOOL WINAPI CertAddEncodedCertificateToSystemStoreA(LPCSTR szCertStoreName, const BYTE *pbCertEncoded,
                                                    DWORD cbCertEncoded)
{
    HCERTSTORE store = CertOpenSystemStoreA(0, szCertStoreName);
    if (!store)
        return FALSE;
    BOOL ok = CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, pbCertEncoded, cbCertEncoded,
                                               CERT_STORE_ADD_USE_EXISTING, nullptr);
    DWORD error = GetLastError();
    CertCloseStore(store, 0);
    SetLastError(error);
    return ok;
}

BOOL WINAPI CertRegisterSystemStore(const void *pvSystemStore, DWORD dwFlags, PCERT_SYSTEM_STORE_INFO pStoreInfo,
                                    void *pvReserved)
{
    if (dwFlags & 0xD7FF)
        return CrFail(E_INVALIDARG);
    sys_name n;
    if (!parse_name(pvSystemStore, dwFlags, FALSE, TRUE, &n))
        return FALSE;
    HKEY root, key = nullptr;
    WCHAR *path = store_path(&n, n.name, &root);
    LSTATUS error = path ? open_key(root, path, dwFlags, &key) : (LSTATUS)GetLastError();
    CrFree(path);
    free_name(&n);
    if (key)
        RegCloseKey(key);
    return error ? CrFail(error) : TRUE;
}

BOOL WINAPI CertUnregisterSystemStore(const void *pvSystemStore, DWORD dwFlags)
{
    if (dwFlags & 0xB7EF)
        return CrFail(E_INVALIDARG);
    sys_name n;
    if (!parse_name(pvSystemStore, dwFlags, FALSE, TRUE, &n))
        return FALSE;
    BOOL ok = unregister(&n, dwFlags);
    DWORD error = GetLastError();
    free_name(&n);
    SetLastError(error);
    return ok;
}

/* calls back for a store of the location, naming it as pvSystemStore would. */
static BOOL report_store(const sys_name *n, PCWSTR name, DWORD flags, void *arg, PFN_CERT_ENUM_SYSTEM_STORE fn)
{
    WCHAR text[512];
    if (n->prefix)
        _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%ls\\%ls", n->prefix, name);
    else
        _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%ls", name);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    CERT_SYSTEM_STORE_INFO info = {sizeof(info)};
    CERT_SYSTEM_STORE_RELOCATE_PARA relocate = {};
    const void *store = text;
    if (n->base) {
        relocate.hKeyBase = n->base;
        relocate.pwszSystemStore = text;
        store = &relocate;
    }
    return fn(store, flags & CERT_SYSTEM_STORE_MASK, &info, nullptr, arg);
}

BOOL WINAPI CertEnumSystemStore(DWORD dwFlags, void *pvSystemStoreLocationPara, void *pvArg,
                                PFN_CERT_ENUM_SYSTEM_STORE pfnEnum)
{
    if (!pfnEnum || (dwFlags & 0x273F))
        return CrFail(E_INVALIDARG);
    sys_name n;
    if (!parse_name(pvSystemStoreLocationPara, dwFlags, FALSE, FALSE, &n))
        return FALSE;
    /* every service's or user's stores at once is not done. */
    if ((n.location == LOC_SERVICES || n.location == LOC_USERS) && !n.prefix) {
        free_name(&n);
        return CrFail(ERROR_NOT_SUPPORTED);
    }
    BOOL ok = TRUE;
    DWORD mask = predefined_mask(n.location);
    for (DWORD i = 0; ok && i < RTL_NUMBER_OF(predefined_names); i++)
        if (mask & (1 << i))
            ok = report_store(&n, predefined_names[i], dwFlags, pvArg, pfnEnum);
    HKEY root, key = nullptr;
    WCHAR *path = ok ? store_path(&n, nullptr, &root) : nullptr;
    if (path && !RegOpenKeyExW(root, path, 0, KEY_READ, &key)) {
        WCHAR name[256];
        for (DWORD i = 0; ok; i++) {
            DWORD chars = RTL_NUMBER_OF(name);
            LSTATUS error = RegEnumKeyExW(key, i, name, &chars, nullptr, nullptr, nullptr, nullptr);
            if (error == ERROR_NO_MORE_ITEMS)
                break;
            if (!error && chars && !is_predefined(name, n.location))
                ok = report_store(&n, name, dwFlags, pvArg, pfnEnum);
        }
        RegCloseKey(key);
    } else if (ok && !path && GetLastError() != ERROR_FILE_NOT_FOUND) {
        ok = FALSE;
    }
    CrFree(path);
    free_name(&n);
    return ok;
}

BOOL WINAPI CertEnumPhysicalStore(const void *pvSystemStore, DWORD dwFlags, void *pvArg,
                                  PFN_CERT_ENUM_PHYSICAL_STORE pfnEnum)
{
    if (!pfnEnum || (dwFlags & 0x273F))
        return CrFail(E_INVALIDARG);
    sys_name n;
    if (!parse_name(pvSystemStore, dwFlags, FALSE, TRUE, &n))
        return FALSE;
    DWORD mask = physical_mask(n.location, n.name);
    BOOL ok = TRUE;
    for (DWORD bit = 0; ok && bit < PHYS_COUNT; bit++) {
        physical p;
        if (!(mask & (1 << bit)) || !describe_physical(&n, bit, &p))
            continue;
        WCHAR text[512];
        if (p.name.prefix)
            _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%ls\\%ls", p.name.prefix, p.name.name);
        else
            _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%ls", p.name.name);
        text[RTL_NUMBER_OF(text) - 1] = 0;
        CERT_PHYSICAL_STORE_INFO info = {sizeof(info)};
        info.pszOpenStoreProvider = p.registry ? (LPSTR)"SystemRegistry" : (LPSTR)"System";
        info.dwOpenFlags = p.open_flags & ~CERT_SYSTEM_STORE_RELOCATE_FLAG;
        info.OpenParameters.cbData = ((DWORD)wcslen(text) + 1) * sizeof(WCHAR);
        info.OpenParameters.pbData = (BYTE *)text;
        info.dwFlags = p.add_flags;
        ok = pfnEnum(pvSystemStore, (dwFlags & CERT_SYSTEM_STORE_MASK) | CERT_PHYSICAL_STORE_PREDEFINED_ENUM_FLAG,
                     physical_names[bit], &info, nullptr, pvArg);
    }
    free_name(&n);
    return ok;
}
