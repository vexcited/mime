#include "crypt32_private.h"
#include <mime/win32/winhttp.h>
#include <cabinet/cabinet.h>

#define AUTO_UPDATE_KEY L"SOFTWARE\\Microsoft\\SystemCertificates\\AuthRoot\\AutoUpdate"
#define POLICY_KEY L"SOFTWARE\\Policies\\Microsoft\\SystemCertificates\\AuthRoot"
#define DEFAULT_ROOT_DIR L"http://ctldl.windowsupdate.com/msdownload/update/v3/static/trustedr/en/"
#define CTL_CAB L"authrootstl.cab"
#define CTL_FILE "authroot.stl"
#define MAX_DOWNLOAD (8u << 20)
#define DAY 864000000000ULL
#define RETRY_TICKS (10 * 60 * 1000)

#define CTL_PROP_PREFIX "1.3.6.1.4.1.311.10.11."
#define szOID_CTL "1.3.6.1.4.1.311.10.1"
#define szOID_ROOT_LIST_SIGNER "1.3.6.1.4.1.311.10.3.9"
#define CERT_DISALLOWED_FILETIME_PROP_ID 104

static SRWLOCK lock = SRWLOCK_INIT;
static DWORD owner;
static BYTE *ctl;            /* the content of the verified list. */
static der_item subjects;    /* its trusted subjects, in ctl. */
static ULONGLONG synced;     /* when the list was fetched. */
static BOOL loaded, fetched; /* the registry was read; the list was fetched by this process. */
static ULONGLONG failed_at;  /* the tick of the last download that failed. */
static HCERTSTORE roots;     /* the roots this process brought, for engines that read the registry before. */

/* ------------------------------------------------------------------ downloads */

typedef struct {
    typeof(&WinHttpOpen) Open;
    typeof(&WinHttpConnect) Connect;
    typeof(&WinHttpOpenRequest) OpenRequest;
    typeof(&WinHttpSendRequest) SendRequest;
    typeof(&WinHttpReceiveResponse) ReceiveResponse;
    typeof(&WinHttpQueryHeaders) QueryHeaders;
    typeof(&WinHttpReadData) ReadData;
    typeof(&WinHttpCloseHandle) CloseHandle;
    typeof(&WinHttpCrackUrl) CrackUrl;
    typeof(&WinHttpSetTimeouts) SetTimeouts;
} http_api;

static BOOL load_http(http_api *api)
{
    static const char *names[] = {"WinHttpOpen",         "WinHttpConnect",      "WinHttpOpenRequest",
                                  "WinHttpSendRequest",  "WinHttpReceiveResponse", "WinHttpQueryHeaders",
                                  "WinHttpReadData",     "WinHttpCloseHandle",  "WinHttpCrackUrl",
                                  "WinHttpSetTimeouts"};
    HMODULE module = LoadLibraryExW(L"winhttp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
        return FALSE;
    FARPROC *slots = (FARPROC *)api;
    for (DWORD i = 0; i < RTL_NUMBER_OF(names); i++)
        if (!(slots[i] = GetProcAddress(module, names[i])))
            return FALSE;
    return TRUE;
}

/* the body of a 200 answer, or null. */
static BYTE *download(PCWSTR url, DWORD *size)
{
    http_api api;
    if (!load_http(&api))
        return nullptr;
    WCHAR host[256], path[1024];
    URL_COMPONENTS parts = {sizeof(parts)};
    parts.lpszHostName = host;
    parts.dwHostNameLength = RTL_NUMBER_OF(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = RTL_NUMBER_OF(path);
    if (!api.CrackUrl(url, 0, 0, &parts))
        return nullptr;
    BYTE *data = nullptr;
    DWORD used = 0;
    BOOL ok = FALSE;
    HINTERNET session = api.Open(L"Microsoft-CryptoAPI/10.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = nullptr, request = nullptr;
    if (session) {
        api.SetTimeouts(session, 10000, 10000, 15000, 15000);
        connection = api.Connect(session, host, parts.nPort, 0);
    }
    if (connection)
        request = api.OpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    DWORD status = 0, length = sizeof(status);
    if (request && api.SendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        api.ReceiveResponse(request, nullptr) &&
        api.QueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                         &status, &length, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        DWORD capacity = 0;
        for (;;) {
            if (capacity - used < 16384) {
                if (capacity >= MAX_DOWNLOAD)
                    break;
                capacity = capacity ? capacity * 2 : 65536;
                BYTE *grown = CrAlloc(capacity);
                if (!grown)
                    break;
                if (data)
                    memcpy(grown, data, used);
                CrFree(data);
                data = grown;
            }
            DWORD got = 0;
            if (!api.ReadData(request, data + used, capacity - used, &got))
                break;
            if (!got) {
                ok = used != 0;
                break;
            }
            used += got;
        }
    }
    if (request)
        api.CloseHandle(request);
    if (connection)
        api.CloseHandle(connection);
    if (session)
        api.CloseHandle(session);
    if (!ok) {
        CrFree(data);
        return nullptr;
    }
    *size = used;
    return data;
}

/* the directory the list and the roots are downloaded from, which the registry may change. */
static void root_dir(WCHAR *out, DWORD capacity)
{
    DWORD size = (capacity - 2) * sizeof(WCHAR), type;
    HKEY key;
    BOOL found = FALSE;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, AUTO_UPDATE_KEY, 0, KEY_READ, &key)) {
        found = !RegQueryValueExW(key, L"RootDirUrl", nullptr, &type, (BYTE *)out, &size) && type == REG_SZ &&
                size >= 2 * sizeof(WCHAR);
        RegCloseKey(key);
    }
    if (!found) {
        wcscpy(out, DEFAULT_ROOT_DIR);
        return;
    }
    DWORD n = size / sizeof(WCHAR);
    out[n] = 0;
    n = (DWORD)wcslen(out);
    if (n && out[n - 1] != '/') {
        out[n] = '/';
        out[n + 1] = 0;
    }
}

static BYTE *download_file(PCWSTR name, DWORD *size)
{
    WCHAR url[1024];
    root_dir(url, RTL_NUMBER_OF(url) - 64);
    wcscat(url, name);
    return download(url, size);
}

/* ------------------------------------------------------------------ the list */

static void *cab_alloc(size_t size)
{
    return CrAlloc(size);
}

static void cab_free(void *memory)
{
    CrFree(memory);
}

typedef struct {
    BYTE *data;
    DWORD size, used;
} cab_out;

static void *cab_begin(void *context, const MIME_CAB_FILE *file)
{
    cab_out *out = context;
    if (out->data || _stricmp(file->Name, CTL_FILE) || !file->Size || file->Size > MAX_DOWNLOAD)
        return nullptr;
    out->data = CrAlloc(file->Size);
    out->size = file->Size;
    return out->data ? out : nullptr;
}

static int cab_write(void *context, void *file_context, const uint8_t *data, size_t length)
{
    cab_out *out = file_context;
    if (length > out->size - out->used)
        return 1;
    memcpy(out->data + out->used, data, length);
    out->used += (DWORD)length;
    return 0;
}

static int cab_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    return 0;
}

static BYTE *extract_list(const BYTE *cab, DWORD size, DWORD *out_size)
{
    MIME_CAB *handle;
    if (MimeCabOpen(&handle, cab, size, cab_alloc, cab_free))
        return nullptr;
    static const MIME_CAB_FILE_SINK sink = {cab_begin, cab_write, cab_end};
    cab_out out = {};
    int error = MimeCabExtract(handle, &sink, &out);
    MimeCabClose(handle);
    if (error || !out.data || out.used != out.size) {
        CrFree(out.data);
        return nullptr;
    }
    *out_size = out.size;
    return out.data;
}

static BOOL oid_is(const der_item *item, LPCSTR want)
{
    char name[128];
    return item->tag == ASN_OID && !DerParseOid(item, name, sizeof(name)) && !strcmp(name, want);
}

/* the trusted subjects of a root list; the content is the CertificateTrustList sequence. */
static BOOL parse_list(const BYTE *content, DWORD size, der_item *out)
{
    der_reader input = {content, size}, fields;
    der_item list, item;
    if (DerExpect(&input, ASN_SEQUENCE, &list))
        return FALSE;
    DerOpen(&fields, &list);
    if (DerPeek(&fields, ASN_INTEGER) && DerRead(&fields, &item))
        return FALSE;
    /* the usage the list is for, then up to its subject algorithm, then the subjects. */
    der_item usage;
    if (DerExpect(&fields, ASN_SEQUENCE, &usage))
        return FALSE;
    der_reader usages;
    DerOpen(&usages, &usage);
    BOOL roots = FALSE;
    while (usages.n && !DerRead(&usages, &item))
        roots |= oid_is(&item, szOID_ROOT_LIST_SIGNER);
    if (!roots)
        return FALSE;
    while (fields.n && !DerPeek(&fields, ASN_SEQUENCE))
        if (DerRead(&fields, &item))
            return FALSE;
    return !DerExpect(&fields, ASN_SEQUENCE, &item) && !DerExpect(&fields, ASN_SEQUENCE, out);
}

/* the list's content when it is signed for the root list, by a certificate chaining to a Microsoft root. */
static BYTE *verify_list(const BYTE *data, DWORD size, DWORD *content_size)
{
    HCRYPTMSG message = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, nullptr, nullptr);
    if (!message)
        return nullptr;
    BYTE *content = nullptr;
    char type[64];
    DWORD n = sizeof(type);
    PCCERT_CONTEXT signer = nullptr;
    BOOL ok = CryptMsgUpdate(message, data, size, TRUE) &&
              CryptMsgGetParam(message, CMSG_INNER_CONTENT_TYPE_PARAM, 0, type, &n) && !strcmp(type, szOID_CTL) &&
              CryptMsgGetAndVerifySigner(message, 0, nullptr, 0, &signer, nullptr);
    if (ok) {
        HCERTSTORE certs = CryptGetMessageCertificates(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, data, size);
        LPSTR usages[] = {(LPSTR)szOID_ROOT_LIST_SIGNER};
        CERT_CHAIN_PARA para = {sizeof(para)};
        para.RequestedUsage.Usage.cUsageIdentifier = 1;
        para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        CERT_CHAIN_POLICY_PARA policy = {sizeof(policy)};
        CERT_CHAIN_POLICY_STATUS status = {sizeof(status)};
        ok = CertGetCertificateChain(HCCE_LOCAL_MACHINE, signer, nullptr, certs, &para,
                                     CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE, nullptr, &chain) &&
             CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT, chain, &policy, &status) &&
             !status.dwError;
        if (chain)
            CertFreeCertificateChain(chain);
        if (certs)
            CertCloseStore(certs, 0);
        CertFreeCertificateContext(signer);
    }
    n = 0;
    if (ok && CryptMsgGetParam(message, CMSG_CONTENT_PARAM, 0, nullptr, &n) && (content = CrAlloc(n)) &&
        !CryptMsgGetParam(message, CMSG_CONTENT_PARAM, 0, content, &n)) {
        CrFree(content);
        content = nullptr;
    }
    CryptMsgClose(message);
    *content_size = n;
    return content;
}

/* takes the list if it holds; it is then owned here. */
static BOOL use_list(BYTE *data, DWORD size)
{
    DWORD content_size;
    BYTE *content = verify_list(data, size, &content_size);
    CrFree(data);
    der_item found;
    if (!content || !parse_list(content, content_size, &found)) {
        CrFree(content);
        return FALSE;
    }
    CrFree(ctl);
    ctl = content;
    subjects = found;
    return TRUE;
}

static ULONGLONG now(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
}

static void load_saved(void)
{
    loaded = TRUE;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, AUTO_UPDATE_KEY, 0, KEY_READ, &key))
        return;
    DWORD size = 0, type;
    FILETIME time;
    DWORD time_size = sizeof(time);
    if (!RegQueryValueExW(key, L"EncodedCtl", nullptr, &type, nullptr, &size) && type == REG_BINARY && size &&
        size <= MAX_DOWNLOAD &&
        !RegQueryValueExW(key, L"LastSyncTime", nullptr, &type, (BYTE *)&time, &time_size) && type == REG_BINARY &&
        time_size == sizeof(time)) {
        BYTE *data = CrAlloc(size);
        if (data && RegQueryValueExW(key, L"EncodedCtl", nullptr, nullptr, data, &size)) {
            CrFree(data);
            data = nullptr;
        }
        if (data && use_list(data, size))
            synced = (ULONGLONG)time.dwHighDateTime << 32 | time.dwLowDateTime;
    }
    RegCloseKey(key);
}

static void save(const BYTE *data, DWORD size)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, AUTO_UPDATE_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr))
        return;
    FILETIME time = {(DWORD)synced, (DWORD)(synced >> 32)};
    RegSetValueExW(key, L"EncodedCtl", 0, REG_BINARY, data, size);
    RegSetValueExW(key, L"LastSyncTime", 0, REG_BINARY, (const BYTE *)&time, sizeof(time));
    RegCloseKey(key);
}

/* fetches the list once per process, and again only after a failure has had time to clear. */
static BOOL fetch_list(void)
{
    if (fetched || (failed_at && GetTickCount64() - failed_at < RETRY_TICKS))
        return FALSE;
    DWORD cab_size, size = 0;
    BYTE *cab = download_file(CTL_CAB, &cab_size);
    BYTE *data = cab ? extract_list(cab, cab_size, &size) : nullptr;
    CrFree(cab);
    BYTE *copy = data ? CrAlloc(size) : nullptr;
    if (copy)
        memcpy(copy, data, size);
    if (!copy || !use_list(copy, size)) {
        CrFree(data);
        failed_at = GetTickCount64();
        return FALSE;
    }
    fetched = TRUE;
    synced = now();
    save(data, size);
    CrFree(data);
    return TRUE;
}

static BOOL disabled_by_policy(void)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, POLICY_KEY, 0, KEY_READ, &key))
        return FALSE;
    DWORD value = 0, size = sizeof(value), type;
    BOOL off = !RegQueryValueExW(key, L"DisableRootAutoUpdate", nullptr, &type, (BYTE *)&value, &size) &&
               type == REG_DWORD && value;
    RegCloseKey(key);
    return off;
}

/* ------------------------------------------------------------------ entries */

/* the property an attribute's oid names, its arc after the prefix. */
static DWORD property_id(const char *oid)
{
    DWORD id = 0;
    for (const char *p = oid + sizeof(CTL_PROP_PREFIX) - 1; *p >= '0' && *p <= '9'; p++)
        id = id * 10 + (DWORD)(*p - '0');
    return id;
}

typedef struct {
    der_item id;
    der_item attributes; /* tag 0 when the entry has none. */
} list_entry;

static BOOL next_entry(der_reader *r, list_entry *entry)
{
    der_item item;
    der_reader fields;
    while (r->n) {
        if (DerExpect(r, ASN_SEQUENCE, &item))
            return FALSE;
        DerOpen(&fields, &item);
        memset(entry, 0, sizeof(*entry));
        if (DerExpect(&fields, ASN_OCTETSTRING, &entry->id) || entry->id.length != 20)
            continue;
        if (fields.n && DerExpect(&fields, ASN_SET, &entry->attributes))
            continue;
        return TRUE;
    }
    return FALSE;
}

/* a property the entry gives its root, by its id, the last arc of the attribute's oid. */
static BOOL entry_property(const list_entry *entry, DWORD want, der_item *value)
{
    if (!entry->attributes.tag)
        return FALSE;
    der_reader attrs;
    DerOpen(&attrs, &entry->attributes);
    der_item attribute, oid, values;
    while (attrs.n && !DerExpect(&attrs, ASN_SEQUENCE, &attribute)) {
        der_reader one;
        DerOpen(&one, &attribute);
        char name[64];
        if (DerExpect(&one, ASN_OID, &oid) || DerExpect(&one, ASN_SET, &values) || DerParseOid(&oid, name, sizeof(name)))
            return FALSE;
        if (strncmp(name, CTL_PROP_PREFIX, sizeof(CTL_PROP_PREFIX) - 1))
            continue;
        DWORD id = property_id(name);
        der_reader list;
        DerOpen(&list, &values);
        if (id == want && !DerExpect(&list, ASN_OCTETSTRING, value))
            return TRUE;
    }
    return FALSE;
}

static BOOL property_is(const list_entry *entry, DWORD id, const BYTE *data, DWORD size)
{
    der_item value;
    return entry_property(entry, id, &value) && value.length == size && !memcmp(value.content, data, size);
}

/* whether the root program stopped trusting the root at all. */
static BOOL entry_disabled(const list_entry *entry)
{
    der_item value;
    if (!entry_property(entry, CERT_DISALLOWED_FILETIME_PROP_ID, &value) || value.length != sizeof(ULONGLONG))
        return FALSE;
    ULONGLONG when;
    memcpy(&when, value.content, sizeof(when));
    return !when || now() >= when;
}

static void set_entry_properties(PCCERT_CONTEXT cert, const list_entry *entry)
{
    der_reader attrs;
    DerOpen(&attrs, &entry->attributes);
    der_item attribute, oid, values, value;
    while (entry->attributes.tag && attrs.n && !DerExpect(&attrs, ASN_SEQUENCE, &attribute)) {
        der_reader one, list;
        DerOpen(&one, &attribute);
        char name[64];
        if (DerExpect(&one, ASN_OID, &oid) || DerExpect(&one, ASN_SET, &values) || DerParseOid(&oid, name, sizeof(name)) ||
            strncmp(name, CTL_PROP_PREFIX, sizeof(CTL_PROP_PREFIX) - 1))
            continue;
        DWORD id = property_id(name);
        DerOpen(&list, &values);
        if (!id || id == CERT_SHA1_HASH_PROP_ID || DerExpect(&list, ASN_OCTETSTRING, &value))
            continue;
        CRYPT_DATA_BLOB blob = {value.length, (BYTE *)value.content};
        CertSetCertificateContextProperty(cert, id, CERT_SET_PROPERTY_INHIBIT_PERSIST_FLAG, &blob);
    }
}

/* ------------------------------------------------------------------ roots */

static BOOL store_has(HCERTSTORE store, const BYTE *sha1)
{
    CRYPT_HASH_BLOB blob = {20, (BYTE *)sha1};
    PCCERT_CONTEXT found =
        store ? CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_SHA1_HASH, &blob, nullptr) : nullptr;
    if (found)
        CertFreeCertificateContext(found);
    return found != nullptr;
}

/* a root brought before, by this process or an earlier one, is not fetched again. */
static BOOL have_root(const BYTE *sha1)
{
    if (store_has(roots, sha1))
        return TRUE;
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                     CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG |
                                         CERT_STORE_OPEN_EXISTING_FLAG, L"AuthRoot");
    BOOL found = store_has(store, sha1);
    if (store)
        CertCloseStore(store, 0);
    return found;
}

/* downloads the entry's root, checks it is the one listed, and adds it to the machine's AuthRoot store. */
static BOOL add_root(const list_entry *entry)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    WCHAR name[48];
    for (DWORD i = 0; i < 20; i++) {
        name[2 * i] = hex[entry->id.content[i] >> 4];
        name[2 * i + 1] = hex[entry->id.content[i] & 15];
    }
    wcscpy(name + 40, L".crt");
    DWORD size;
    BYTE *data = download_file(name, &size);
    if (!data)
        return FALSE;
    BYTE hash[20];
    PCCERT_CONTEXT cert = nullptr;
    if (CrHash(CALG_SHA1, data, size, hash) && !memcmp(hash, entry->id.content, 20))
        cert = CertCreateCertificateContext(X509_ASN_ENCODING, data, size);
    CrFree(data);
    if (!cert)
        return FALSE;
    set_entry_properties(cert, entry);
    if (!roots)
        roots = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    BOOL ok = roots && CertAddCertificateContextToStore(roots, cert, CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
    /* only an administrator may write the machine's store; the process keeps the root regardless. */
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"AuthRoot");
    if (store) {
        CertAddCertificateContextToStore(store, cert, CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
        CertCloseStore(store, 0);
    }
    CertFreeCertificateContext(cert);
    return ok;
}

/*
 * the entries that can end the chain: the top itself when self signed, its issuer by subject name and key id, or a
 * root with the top's own subject and key, the self signed form of a cross certificate.
 */
static BOOL matches(const list_entry *entry, BOOL self, const BYTE *issuer_md5,
                    const BYTE *issuer_key, DWORD issuer_key_size, const BYTE *subject_md5, const BYTE *key,
                    DWORD key_size, const BYTE *sha1)
{
    if (self && !memcmp(entry->id.content, sha1, 20))
        return TRUE;
    der_item id;
    BOOL keyed = entry_property(entry, CERT_KEY_IDENTIFIER_PROP_ID, &id);
    if (!self && property_is(entry, CERT_SUBJECT_NAME_MD5_HASH_PROP_ID, issuer_md5, 16) &&
        (!issuer_key_size || !keyed || (id.length == issuer_key_size && !memcmp(id.content, issuer_key, id.length))))
        return TRUE;
    return keyed && property_is(entry, CERT_SUBJECT_NAME_MD5_HASH_PROP_ID, subject_md5, 16) && id.length == key_size &&
           !memcmp(id.content, key, key_size);
}

static BOOL find_and_add(PCCERT_CONTEXT top, BOOL self, const BYTE *aki, DWORD aki_size)
{
    BYTE issuer_md5[16], subject_md5[16], sha1[20], key[CR_MAX_HASH];
    DWORD key_size = sizeof(key);
    const CERT_INFO *info = top->pCertInfo;
    CrHash(CALG_MD5, info->Issuer.pbData, info->Issuer.cbData, issuer_md5);
    CrHash(CALG_MD5, info->Subject.pbData, info->Subject.cbData, subject_md5);
    CrHash(CALG_SHA1, top->pbCertEncoded, top->cbCertEncoded, sha1);
    if (!CertGetCertificateContextProperty(top, CERT_KEY_IDENTIFIER_PROP_ID, key, &key_size))
        key_size = 0;
    der_reader r;
    DerOpen(&r, &subjects);
    list_entry entry;
    BOOL added = FALSE;
    DWORD tries = 0;
    while (tries < 4 && next_entry(&r, &entry)) {
        if (!matches(&entry, self, issuer_md5, aki, aki_size, subject_md5, key, key_size, sha1) ||
            entry_disabled(&entry))
            continue;
        if (have_root(entry.id.content))
            continue;
        tries++;
        added |= add_root(&entry);
    }
    return added;
}

/*
 * automatic root update: a chain that ends short of a trusted root looks its top up in the certificate trust list
 * of Microsoft's root program, authroot.stl in a cabinet on windows update. a root the list holds is downloaded as
 * <sha-1>.crt from the same directory, checked against its hash, given the list's properties for it, such as the
 * usages it is trusted for, and added to the machine's AuthRoot store. the list is kept in the registry under
 * AuthRoot\AutoUpdate as EncodedCtl with its LastSyncTime, and fetched again when a day old and a root is missing.
 * it must be signed for the root list by a certificate chaining to a Microsoft root. offline, nothing happens.
 */
HCERTSTORE CrAutoRoots(void)
{
    return roots;
}

BOOL CrAutoRootUpdate(PCCERT_CONTEXT top, BOOL self_signed, const BYTE *aki, DWORD aki_size)
{
    if (owner == GetCurrentThreadId())
        return FALSE;
    AcquireSRWLockExclusive(&lock);
    owner = GetCurrentThreadId();
    BOOL added = FALSE;
    if (!disabled_by_policy()) {
        if (!loaded)
            load_saved();
        if (ctl)
            added = find_and_add(top, self_signed, aki, aki_size);
        if (!added && (!ctl || now() - synced >= DAY) && fetch_list())
            added = find_and_add(top, self_signed, aki, aki_size);
    }
    owner = 0;
    ReleaseSRWLockExclusive(&lock);
    return added;
}
