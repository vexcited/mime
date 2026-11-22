/*
 * the credentials of the credential manager, which CredWrite, CredRead, CredEnumerate and CredDelete of sechost reach
 * through the policy interface of lsarpc.c. an account has three sets: the one of its session, kept in memory, the local
 * one in AppData\Local\Microsoft\Credentials and the enterprise one in AppData\Roaming\Microsoft\Credentials. a file is
 * a head of three numbers and a blob of the data protection service that only the security authority opens, over the
 * credential laid out as windows does, and is named as windows names it. the answers and the files were read with cw.c
 * and the decoders of scratch/g2/lsa in the research repository. what is not known of windows: a number of the
 * head that is 8 in most files.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include <mime/win32/rpcndr.h>
#include <crypto/crypto.h>

#define STATUS_INVALID_PARAMETER_ ((long)0xC000000D)
#define STATUS_NO_MEMORY_ ((long)0xC0000017)
#define STATUS_NOT_FOUND_ ((long)0xC0000225)
#define STATUS_ACCESS_DENIED_ ((long)0xC0000022)
#define STATUS_NOT_IMPLEMENTED_ ((long)0xC0000002)
#define STATUS_INVALID_ACCOUNT_NAME_ ((long)0xC0000062)
#define STATUS_INVALID_PARAMETER_1_ ((long)0xC00000EF)
#define STATUS_NO_SUCH_LOGON_SESSION_ ((long)0xC000005F)
#define STATUS_UNSUCCESSFUL_ ((long)0xC0000001)

#define CRED_TYPE_GENERIC 1
#define CRED_TYPE_DOMAIN_PASSWORD 2
#define CRED_TYPE_DOMAIN_CERTIFICATE 3
#define CRED_TYPE_DOMAIN_VISIBLE_PASSWORD 4
#define CRED_TYPE_GENERIC_CERTIFICATE 5
#define CRED_PERSIST_SESSION 1
#define CRED_PERSIST_LOCAL_MACHINE 2
#define CRED_PERSIST_ENTERPRISE 3
#define CRED_PRESERVE_CREDENTIAL_BLOB 1
#define CRED_ENUMERATE_ALL_CREDENTIALS 1
#define CRED_MAX_BLOB 2560
#define CRED_MAX_ATTRIBUTES 64
#define CRED_MAX_VALUE 256
#define CRED_MAX_DOMAIN_TARGET 337
#define CRED_MAX_FILE 0x10000
#define CRED_HEAD_SIZE 48
#define CRED_SYSTEM_BLOB 0x20000000
#define CRED_FILE_ATTRIBUTES (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | 0x2000)
#define CRED_SAME_LOGON 2
/* the flags of wincred.h a caller may set, less the two windows 11 refuses in a write: 0x10 and 0x20. */
#define CRED_FLAGS_VALID_INPUT 0xF08F
#define CRED_FLAGS_WILDCARD_MATCH 0x20
#define SESSION_WILDCARD L"*Session"
#define GENERIC_PREFIX L"LegacyGeneric:target="
#define DOMAIN_PREFIX L"Domain:target="

NTSTATUS WINAPI SystemFunction040(PVOID memory, ULONG length, ULONG flags);
NTSTATUS WINAPI SystemFunction041(PVOID memory, ULONG length, ULONG flags);

/* a credential of the session set of an account. */
typedef struct SESSION_CRED {
    struct SESSION_CRED *Next;
    WCHAR Sid[96];
    DWORD Size;
    BYTE Data[];
} SESSION_CRED;

static SESSION_CRED *g_session;
static SRWLOCK g_cred_lock = SRWLOCK_INIT;

typedef struct CRED_CALLER {
    WCHAR Sid[96];
    WCHAR Profile[MAX_PATH];
} CRED_CALLER;

static BOOL domain_type(DWORD type)
{
    return type == CRED_TYPE_DOMAIN_PASSWORD || type == CRED_TYPE_DOMAIN_CERTIFICATE ||
           type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD;
}

static BOOL known_type(DWORD type)
{
    return type >= CRED_TYPE_GENERIC && type <= CRED_TYPE_GENERIC_CERTIFICATE;
}

/* a domain credential names a server or a domain, without the characters a name of the network cannot have, or all
 * the hosts of a domain with a star before its dns name or after its netbios name, or everything with a star alone. */
static BOOL valid_target(const WCHAR *target, DWORD type)
{
    if (!target || !*target)
        return FALSE;
    if (!domain_type(type))
        return wcslen(target) <= 32767;
    SIZE_T n = wcslen(target);
    if (n > CRED_MAX_DOMAIN_TARGET)
        return FALSE;
    if (!_wcsicmp(target, SESSION_WILDCARD))
        return TRUE;
    for (SIZE_T i = 0; i < n; i++) {
        if (target[i] == L'*') {
            if (!(i == 0 && (n == 1 || target[1] == L'.')) && !(i == n - 1 && i && target[i - 1] == L'\\'))
                return FALSE;
        } else if (target[i] < 0x20 || wcschr(L"\"/[]:;|=,+?<>", target[i])) {
            return FALSE;
        } else if (target[i] == L'\\' && !(i == n - 2 && target[n - 1] == L'*')) {
            return FALSE;
        }
    }
    return TRUE;
}

/* the user of a domain credential: domain\\user or user@domain, each part a name of the network. */
static BOOL valid_user(const WCHAR *user)
{
    int slashes = 0;
    for (const WCHAR *p = user; *p; p++) {
        if (*p == L'\\')
            slashes++;
        else if (*p < 0x20 || wcschr(L"\"/[]:;|=,+*?<>", *p))
            return FALSE;
    }
    return slashes <= 1 && user[0] != L'\\' && user[wcslen(user) - 1] != L'\\';
}

static void set_folder(const CRED_CALLER *caller, DWORD persist, WCHAR *out)
{
    wcscpy(out, caller->Profile);
    wcscat(out, persist == CRED_PERSIST_ENTERPRISE ? L"\\AppData\\Roaming" : L"\\AppData\\Local");
    wcscat(out, L"\\Microsoft\\Credentials");
}

static void make_folders(WCHAR *path)
{
    for (WCHAR *p = wcschr(path + 3, L'\\'); p; p = wcschr(p + 1, L'\\')) {
        *p = 0;
        CreateDirectoryW(path, nullptr);
        *p = L'\\';
    }
    CreateDirectoryW(path, nullptr);
}

/* ------------------------------------------------------------------ the layout of a credential */

typedef struct WRITER {
    BYTE *Data;
    DWORD Size, Room;
} WRITER;

static void put(WRITER *w, const void *data, DWORD size)
{
    if (w->Size + size <= w->Room && size)
        memcpy(w->Data + w->Size, data, size);
    w->Size += size;
}

static void put32(WRITER *w, DWORD value)
{
    put(w, &value, 4);
}

/* a counted run of bytes, kept even. */
static void put_bytes(WRITER *w, const void *data, DWORD size)
{
    static const BYTE zero;
    put32(w, size);
    put(w, data, size);
    if (size & 1)
        put(w, &zero, 1);
}

static void put_text(WRITER *w, const WCHAR *prefix, const WCHAR *text)
{
    if (!text) {
        put32(w, 0);
        return;
    }
    DWORD head = prefix ? (DWORD)wcslen(prefix) * sizeof(WCHAR) : 0;
    DWORD bytes = (DWORD)(wcslen(text) + 1) * sizeof(WCHAR);
    put32(w, head + bytes);
    put(w, prefix, head);
    put(w, text, bytes);
}

static const WCHAR *target_prefix(DWORD type)
{
    return domain_type(type) ? DOMAIN_PREFIX : GENERIC_PREFIX;
}

/* the credential as the files hold it: a head, the texts, the secret and the attributes, each counted. */
static BYTE *encode(const CRED_RECORD *c, DWORD *size)
{
    WRITER w = {nullptr, 0, 0};
    for (int pass = 0; pass < 2; pass++) {
        w.Size = 0;
        put32(&w, CRED_HEAD_SIZE);
        put32(&w, w.Room);
        put32(&w, 0);
        put32(&w, c->Type);
        put32(&w, c->Flags);
        put(&w, &c->LastWritten, sizeof(c->LastWritten));
        put32(&w, 8);
        put32(&w, c->Persist);
        put32(&w, c->AttributeCount);
        put32(&w, 0);
        put32(&w, 0);
        put_text(&w, target_prefix(c->Type), c->TargetName);
        put_text(&w, nullptr, c->TargetAlias);
        put_text(&w, nullptr, c->Comment);
        put32(&w, 0);
        put_text(&w, nullptr, c->UserName);
        put_bytes(&w, c->CredentialBlob, c->CredentialBlobSize);
        for (DWORD i = 0; i < c->AttributeCount; i++) {
            put32(&w, c->Attributes[i].Flags);
            put_text(&w, nullptr, c->Attributes[i].Keyword);
            put_bytes(&w, c->Attributes[i].Value, c->Attributes[i].ValueSize);
        }
        if (!pass) {
            w.Room = w.Size;
            w.Data = SvcAllocZero(w.Room ? w.Room : 1);
            if (!w.Data)
                return nullptr;
        }
    }
    *size = w.Size;
    return w.Data;
}

typedef struct READER {
    const BYTE *At, *End;
    BOOL Bad;
} READER;

static DWORD take32(READER *r)
{
    DWORD value = 0;
    if (r->End - r->At < 4) {
        r->Bad = TRUE;
        return 0;
    }
    memcpy(&value, r->At, 4);
    r->At += 4;
    return value;
}

static const BYTE *take_bytes(READER *r, DWORD *size)
{
    *size = take32(r);
    DWORD stored = *size + (*size & 1);
    if (r->Bad || (DWORD)(r->End - r->At) < *size) {
        r->Bad = TRUE;
        *size = 0;
        return nullptr;
    }
    const BYTE *data = r->At;
    r->At += stored <= (DWORD)(r->End - r->At) ? stored : *size;
    return data;
}

static void *rpc_copy(const void *data, DWORD size)
{
    BYTE *copy = MIDL_user_allocate(size ? size : 1);
    if (copy && size)
        memcpy(copy, data, size);
    return copy;
}

/* a text of the file as a string of its own, without the prefix windows puts before the name of a target. */
static BOOL take_text(READER *r, BOOL target, WCHAR **out)
{
    DWORD size;
    const BYTE *data = take_bytes(r, &size);
    *out = nullptr;
    if (r->Bad || size % sizeof(WCHAR))
        return FALSE;
    if (!size)
        return TRUE;
    const WCHAR *text = (const WCHAR *)data;
    DWORD chars = size / sizeof(WCHAR);
    if (text[chars - 1])
        return FALSE;
    static const WCHAR *prefixes[] = {GENERIC_PREFIX, DOMAIN_PREFIX};
    for (int i = 0; target && i < 2; i++) {
        DWORD n = (DWORD)wcslen(prefixes[i]);
        if (chars > n && !memcmp(text, prefixes[i], n * sizeof(WCHAR))) {
            text += n;
            chars -= n;
            break;
        }
    }
    *out = rpc_copy(text, chars * sizeof(WCHAR));
    return *out != nullptr;
}

void CredFreeRecord(CRED_RECORD *c)
{
    if (!c)
        return;
    MIDL_user_free(c->TargetName);
    MIDL_user_free(c->Comment);
    MIDL_user_free(c->TargetAlias);
    MIDL_user_free(c->UserName);
    if (c->CredentialBlob) {
        mime_wipe(c->CredentialBlob, c->CredentialBlobSize);
        MIDL_user_free(c->CredentialBlob);
    }
    for (DWORD i = 0; c->Attributes && i < c->AttributeCount; i++) {
        MIDL_user_free(c->Attributes[i].Keyword);
        MIDL_user_free(c->Attributes[i].Value);
    }
    MIDL_user_free(c->Attributes);
    memset(c, 0, sizeof(*c));
}

/* reads the layout back, every part in memory of its own. */
static BOOL decode(const BYTE *data, DWORD size, CRED_RECORD *c)
{
    READER r = {data, data + size, FALSE};
    WCHAR *unused = nullptr;
    memset(c, 0, sizeof(*c));
    DWORD head = take32(&r), total = take32(&r);
    take32(&r);
    c->Type = take32(&r);
    c->Flags = take32(&r);
    c->LastWritten.dwLowDateTime = take32(&r);
    c->LastWritten.dwHighDateTime = take32(&r);
    take32(&r);
    c->Persist = take32(&r);
    DWORD attributes = take32(&r);
    take32(&r);
    take32(&r);
    if (r.Bad || head != CRED_HEAD_SIZE || total > size || attributes > CRED_MAX_ATTRIBUTES)
        return FALSE;
    BOOL ok = take_text(&r, TRUE, &c->TargetName) && take_text(&r, FALSE, &c->TargetAlias) &&
              take_text(&r, FALSE, &c->Comment) && take_text(&r, FALSE, &unused) && take_text(&r, FALSE, &c->UserName);
    MIDL_user_free(unused);
    DWORD blob_size = 0;
    const BYTE *blob = ok ? take_bytes(&r, &blob_size) : nullptr;
    ok = ok && !r.Bad && blob_size <= CRED_MAX_BLOB && c->TargetName;
    if (ok && blob_size) {
        /* the secret goes back sealed for the session in blocks of eight bytes. */
        c->CredentialBlob = MIDL_user_allocate((blob_size + 7) & ~7u);
        ok = c->CredentialBlob != nullptr;
        if (ok) {
            memset(c->CredentialBlob, 0, (blob_size + 7) & ~7u);
            memcpy(c->CredentialBlob, blob, blob_size);
            c->CredentialBlobSize = blob_size;
        }
    }
    if (ok && attributes) {
        c->Attributes = MIDL_user_allocate(attributes * sizeof(CRED_ATTRIBUTE));
        ok = c->Attributes != nullptr;
        if (ok)
            memset(c->Attributes, 0, attributes * sizeof(CRED_ATTRIBUTE));
    }
    for (DWORD i = 0; ok && i < attributes; i++) {
        CRED_ATTRIBUTE *a = &c->Attributes[i];
        c->AttributeCount = i + 1;
        a->Flags = take32(&r);
        ok = take_text(&r, FALSE, &a->Keyword);
        DWORD value_size = 0;
        const BYTE *value = ok ? take_bytes(&r, &value_size) : nullptr;
        ok = ok && !r.Bad && value_size <= CRED_MAX_VALUE;
        if (ok && value_size) {
            a->Value = rpc_copy(value, value_size);
            a->ValueSize = value_size;
            ok = a->Value != nullptr;
        }
    }
    if (!ok)
        CredFreeRecord(c);
    return ok;
}

/* ------------------------------------------------------------------ the sets */

/*
 * windows holds one credential of a name and a type, and when a write changes its persistence between the local and
 * the enterprise set the file of the old set stays on the disk: cw.c shows the new one alone, a second delete right
 * after the first finds nothing, and a later listing shows the old one again. the files left behind are remembered
 * here by their path. one is skipped until the credential that replaced it is deleted and then the folders are listed
 * again or a moment passes. the service forgets them when it stops, as windows shows them again after a restart. this is what a generic
 * credential does. a domain password does otherwise, in a way cw.c does not explain, and moves here with its file.
 */
typedef struct STALE_FILE {
    struct STALE_FILE *Next;
    BOOL Returning;
    ULONGLONG Since;
    WCHAR Path[MAX_PATH];
} STALE_FILE;

/* how long after the delete a call sees the file again without a listing. windows takes some time that was not
 * measured: two deletes in a row do not see it, the next program does. */
#define STALE_RETURN_MS 150

static STALE_FILE *g_stale;

static STALE_FILE **stale_link(const WCHAR *path)
{
    for (STALE_FILE **link = &g_stale; *link; link = &(*link)->Next)
        if (!_wcsicmp((*link)->Path, path))
            return link;
    return nullptr;
}

/* true for a file left behind that the calls do not see yet. */
static BOOL stale_hidden(const WCHAR *path)
{
    STALE_FILE **link = stale_link(path);
    if (!link)
        return FALSE;
    STALE_FILE *file = *link;
    if (!file->Returning || GetTickCount64() - file->Since < STALE_RETURN_MS)
        return TRUE;
    *link = file->Next;
    SvcFree(file);
    return FALSE;
}

static void stale_add(const WCHAR *path)
{
    if (stale_link(path) || wcslen(path) >= MAX_PATH)
        return;
    STALE_FILE *file = SvcAllocZero(sizeof(*file));
    if (!file)
        return;
    wcscpy(file->Path, path);
    file->Next = g_stale;
    g_stale = file;
}

static void stale_remove(const WCHAR *path)
{
    STALE_FILE **link = stale_link(path);
    if (link) {
        STALE_FILE *file = *link;
        *link = file->Next;
        SvcFree(file);
    }
}

static void stale_return(BOOL all, const WCHAR *path)
{
    for (STALE_FILE **link = &g_stale; *link;) {
        STALE_FILE *file = *link;
        if (!all && !_wcsicmp(file->Path, path) && !file->Returning) {
            file->Returning = TRUE;
            file->Since = GetTickCount64();
        }
        if (all && file->Returning) {
            *link = file->Next;
            SvcFree(file);
            continue;
        }
        link = &file->Next;
    }
}

/*
 * the file of a credential, as windows names it: the md5 of the namespace of its name, the number of the attribute, which
 * is 0 for a target, the name and the type, in upper case digits.
 */
static void file_name(const WCHAR *target, DWORD type, WCHAR out[33])
{
    static const WCHAR digits[] = L"0123456789ABCDEF";
    const WCHAR *space = domain_type(type) ? L"Domain" : L"LegacyGeneric";
    DWORD attribute = 0;
    BYTE digest[16];
    MimeHashCtx ctx;
    mime_md5.init(&ctx);
    mime_md5.update(&ctx, space, wcslen(space) * sizeof(WCHAR));
    mime_md5.update(&ctx, &attribute, sizeof(attribute));
    mime_md5.update(&ctx, target, wcslen(target) * sizeof(WCHAR));
    mime_md5.update(&ctx, &type, sizeof(type));
    mime_md5.final(&ctx, digest);
    for (int i = 0; i < 16; i++) {
        out[2 * i] = digits[digest[i] >> 4];
        out[2 * i + 1] = digits[digest[i] & 15];
    }
    out[32] = 0;
}

static BOOL read_cred_file(const WCHAR *path, CRED_RECORD *c)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD size = GetFileSize(file, nullptr), got = 0;
    BYTE *data = size >= 12 && size <= CRED_MAX_FILE ? SvcAlloc(size) : nullptr;
    BOOL ok = data && ReadFile(file, data, size, &got, nullptr) && got == size;
    CloseHandle(file);
    DWORD head[3] = {};
    if (ok)
        memcpy(head, data, sizeof(head));
    BYTE *clear = nullptr;
    DWORD clear_size = 0;
    ok = ok && head[0] == 1 && head[1] <= size - 12 &&
         !DpapiUnprotect(data + 12, head[1], nullptr, 0, TRUE, &clear, &clear_size, nullptr);
    ok = ok && decode(clear, clear_size, c);
    if (clear) {
        mime_wipe(clear, clear_size);
        MIDL_user_free(clear);
    }
    SvcFree(data);
    return ok;
}

static BOOL write_cred_file(const CRED_CALLER *caller, const CRED_RECORD *c)
{
    WCHAR folder[MAX_PATH + 64], path[MAX_PATH + 100], name[33];
    DWORD clear_size = 0, blob_size = 0;
    BYTE *clear = encode(c, &clear_size), *blob = nullptr;
    if (!clear)
        return FALSE;
    DWORD error = DpapiProtect(CRED_SYSTEM_BLOB,
                               c->Persist == CRED_PERSIST_ENTERPRISE ? L"Enterprise Credential Data\r\n"
                                                                     : L"Local Credential Data\r\n",
                               clear, clear_size, nullptr, 0, &blob, &blob_size);
    mime_wipe(clear, clear_size);
    SvcFree(clear);
    if (error)
        return FALSE;
    set_folder(caller, c->Persist, folder);
    make_folders(folder);
    file_name(c->TargetName, c->Type, name);
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", folder, name);
    stale_remove(path);
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, CRED_FILE_ATTRIBUTES, nullptr);
    BOOL ok = file != INVALID_HANDLE_VALUE;
    if (ok) {
        DWORD head[3] = {1, blob_size, 0}, put_size = 0;
        ok = WriteFile(file, head, sizeof(head), &put_size, nullptr) && WriteFile(file, blob, blob_size, &put_size, nullptr);
        CloseHandle(file);
        if (!ok)
            DeleteFileW(path);
    }
    MIDL_user_free(blob);
    return ok;
}

/* a name may be asked for with the prefix its file has. */
static const WCHAR *bare_target(const WCHAR *target, DWORD type)
{
    const WCHAR *prefix = target_prefix(type);
    SIZE_T n = wcslen(prefix);
    return !_wcsnicmp(target, prefix, n) && target[n] ? target + n : target;
}

static BOOL same_cred(const CRED_RECORD *c, const WCHAR *target, DWORD type)
{
    return c->Type == type && !_wcsicmp(c->TargetName, target);
}

typedef BOOL (*CRED_VISIT)(void *context, CRED_RECORD *c, const WCHAR *path, SESSION_CRED *session);

/*
 * every credential of the caller, the session's first, then the local set and the enterprise set, each file in the order
 * of the folder. the visitor keeps a record by clearing it, and stops the walk by returning false.
 */
static void walk(const CRED_CALLER *caller, CRED_VISIT visit, void *context)
{
    for (SESSION_CRED *s = g_session, *next; s; s = next) {
        next = s->Next;
        CRED_RECORD c;
        if (wcscmp(s->Sid, caller->Sid) || !decode(s->Data, s->Size, &c))
            continue;
        BOOL more = visit(context, &c, nullptr, s);
        CredFreeRecord(&c);
        if (!more)
            return;
    }
    for (DWORD persist = CRED_PERSIST_LOCAL_MACHINE; persist <= CRED_PERSIST_ENTERPRISE; persist++) {
        WCHAR folder[MAX_PATH + 64], path[MAX_PATH + 400];
        WIN32_FIND_DATAW find;
        set_folder(caller, persist, folder);
        _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\*", folder);
        HANDLE h = FindFirstFileW(path, &find);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        BOOL more = TRUE;
        do {
            if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            CRED_RECORD c;
            _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", folder, find.cFileName);
            if (stale_hidden(path) || !read_cred_file(path, &c))
                continue;
            c.Persist = persist;
            more = visit(context, &c, path, nullptr);
            CredFreeRecord(&c);
        } while (more && FindNextFileW(h, &find));
        FindClose(h);
        if (!more)
            return;
    }
}

typedef struct FIND_CONTEXT {
    const WCHAR *Target;
    DWORD Type;
    CRED_RECORD *Found;
    BOOL Remove, Any;
    DWORD NewPersist;
} FIND_CONTEXT;

static BOOL find_visit(void *context, CRED_RECORD *c, const WCHAR *path, SESSION_CRED *session)
{
    FIND_CONTEXT *f = context;
    if (!same_cred(c, f->Target, f->Type))
        return TRUE;
    f->Any = TRUE;
    if (f->Remove) {
        if (path && c->Type == CRED_TYPE_GENERIC && f->NewPersist > CRED_PERSIST_SESSION && f->NewPersist != c->Persist) {
            stale_add(path);
        } else if (path) {
            SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(path);
        } else {
            for (SESSION_CRED **link = &g_session; *link; link = &(*link)->Next) {
                if (*link == session) {
                    *link = session->Next;
                    mime_wipe(session->Data, session->Size);
                    SvcFree(session);
                    break;
                }
            }
        }
        return TRUE;
    }
    if (f->Found) {
        *f->Found = *c;
        memset(c, 0, sizeof(*c));
    }
    return FALSE;
}

/* the caller of the call being served: its account and the folder of its profile. */
static long caller_of(CRED_CALLER *caller)
{
    memset(caller, 0, sizeof(*caller));
    if (RpcImpersonateClient(nullptr) != RPC_S_OK)
        return STATUS_NO_SUCH_LOGON_SESSION_;
    HANDLE token = nullptr;
    BYTE user[96];
    DWORD size = 0;
    WCHAR *text = nullptr;
    BOOL known = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) &&
                 GetTokenInformation(token, TokenUser, user, sizeof(user), &size) &&
                 ConvertSidToStringSidW(*(PSID *)user, &text) && wcslen(text) < RTL_NUMBER_OF(caller->Sid);
    RpcRevertToSelf();
    if (known) {
        WCHAR key[200];
        DWORD bytes = sizeof(caller->Profile), type;
        wcscpy(caller->Sid, text);
        wcscpy(key, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\");
        wcscat(key, text);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"ProfileImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type,
                         caller->Profile, &bytes))
            caller->Profile[0] = 0;
    }
    if (text)
        LocalFree(text);
    if (token)
        CloseHandle(token);
    /* an account without a profile has no credential sets. */
    return known && caller->Profile[0] ? 0 : STATUS_NO_SUCH_LOGON_SESSION_;
}

/* the secret of a call is sealed for the caller's logon session, so it opens and seals as the caller. */
static BOOL session_crypt(BYTE *data, DWORD size, BOOL seal)
{
    if (!size)
        return TRUE;
    if (size % 8 || RpcImpersonateClient(nullptr) != RPC_S_OK)
        return FALSE;
    NTSTATUS status = seal ? SystemFunction040(data, size, CRED_SAME_LOGON) : SystemFunction041(data, size, CRED_SAME_LOGON);
    RpcRevertToSelf();
    return status >= 0;
}

/* what a caller gets of a credential: the secret of a domain password stays with the security authority. */
static BOOL seal_for_caller(CRED_RECORD *c, DWORD *clear_size)
{
    if (c->Type == CRED_TYPE_DOMAIN_PASSWORD || c->Type == CRED_TYPE_DOMAIN_CERTIFICATE) {
        if (c->CredentialBlob) {
            mime_wipe(c->CredentialBlob, c->CredentialBlobSize);
            MIDL_user_free(c->CredentialBlob);
        }
        c->CredentialBlob = nullptr;
        c->CredentialBlobSize = 0;
    }
    *clear_size = c->CredentialBlobSize;
    c->CredentialBlobSize = (c->CredentialBlobSize + 7) & ~7u;
    return session_crypt(c->CredentialBlob, c->CredentialBlobSize, TRUE);
}

/* ------------------------------------------------------------------ the calls */

long CredStoreWrite(const CRED_RECORD *given, DWORD clear_size, DWORD flags)
{
    CRED_CALLER caller;
    if (flags & ~(DWORD)CRED_PRESERVE_CREDENTIAL_BLOB)
        return STATUS_INVALID_PARAMETER_;
    /* a write that keeps the secret brings none, and the flags are the ones a caller may set. */
    if (((flags & CRED_PRESERVE_CREDENTIAL_BLOB) && clear_size) || (given->Flags & ~(DWORD)CRED_FLAGS_VALID_INPUT))
        return STATUS_INVALID_PARAMETER_;
    if (!known_type(given->Type) || !valid_target(given->TargetName, given->Type) ||
        given->Persist < CRED_PERSIST_SESSION || given->Persist > CRED_PERSIST_ENTERPRISE ||
        given->AttributeCount > CRED_MAX_ATTRIBUTES || clear_size > given->CredentialBlobSize ||
        clear_size > CRED_MAX_BLOB || (given->CredentialBlobSize && !given->CredentialBlob) ||
        (given->AttributeCount && !given->Attributes))
        return STATUS_INVALID_PARAMETER_;
    for (DWORD i = 0; i < given->AttributeCount; i++)
        if (!given->Attributes[i].Keyword || given->Attributes[i].ValueSize > CRED_MAX_VALUE ||
            (given->Attributes[i].ValueSize && !given->Attributes[i].Value))
            return STATUS_INVALID_PARAMETER_;
    if (domain_type(given->Type)) {
        /* the credential of every session server lives with the session. */
        if (!_wcsicmp(given->TargetName, SESSION_WILDCARD) && given->Persist != CRED_PERSIST_SESSION)
            return STATUS_INVALID_PARAMETER_;
        if (!given->UserName || !*given->UserName)
            return STATUS_INVALID_ACCOUNT_NAME_;
        if (given->Type != CRED_TYPE_DOMAIN_CERTIFICATE && !valid_user(given->UserName))
            return STATUS_INVALID_ACCOUNT_NAME_;
        /* a certificate is named by a marshaled credential. */
        if (given->Type == CRED_TYPE_DOMAIN_CERTIFICATE && wcsncmp(given->UserName, L"@@", 2))
            return STATUS_INVALID_ACCOUNT_NAME_;
        if (given->Type != CRED_TYPE_DOMAIN_CERTIFICATE && (clear_size & 1))
            return STATUS_INVALID_PARAMETER_;
    }
    long status = caller_of(&caller);
    if (status)
        return status;
    CRED_RECORD c = *given, old = {};
    BYTE *secret = nullptr;
    if (given->CredentialBlobSize) {
        secret = SvcAlloc(given->CredentialBlobSize);
        if (!secret)
            return STATUS_NO_MEMORY_;
        memcpy(secret, given->CredentialBlob, given->CredentialBlobSize);
        if (!session_crypt(secret, given->CredentialBlobSize, FALSE)) {
            SvcFree(secret);
            return STATUS_UNSUCCESSFUL_;
        }
    }
    c.CredentialBlob = secret;
    c.CredentialBlobSize = clear_size;
    c.Flags = 0;
    GetSystemTimeAsFileTime(&c.LastWritten);
    AcquireSRWLockExclusive(&g_cred_lock);
    FIND_CONTEXT find = {given->TargetName, given->Type, &old, FALSE, FALSE};
    walk(&caller, find_visit, &find);
    if (flags & CRED_PRESERVE_CREDENTIAL_BLOB) {
        if (!find.Any) {
            status = STATUS_NOT_FOUND_;
        } else {
            c.CredentialBlob = old.CredentialBlob;
            c.CredentialBlobSize = old.CredentialBlobSize;
        }
    }
    if (!status) {
        /* one credential of a name is shown: the old one goes, but for its file when the persistence changes between
           the two sets of files. */
        FIND_CONTEXT remove = {given->TargetName, given->Type, nullptr, TRUE, FALSE, c.Persist};
        if (find.Any)
            walk(&caller, find_visit, &remove);
        if (c.Persist == CRED_PERSIST_SESSION) {
            DWORD size = 0;
            BYTE *data = encode(&c, &size);
            SESSION_CRED *s = data ? SvcAllocZero(sizeof(*s) + size) : nullptr;
            if (s) {
                wcscpy(s->Sid, caller.Sid);
                s->Size = size;
                memcpy(s->Data, data, size);
                SESSION_CRED **end = &g_session;
                while (*end)
                    end = &(*end)->Next;
                *end = s;
            } else {
                status = STATUS_NO_MEMORY_;
            }
            if (data) {
                mime_wipe(data, size);
                SvcFree(data);
            }
        } else if (!write_cred_file(&caller, &c)) {
            status = STATUS_UNSUCCESSFUL_;
        }
    }
    ReleaseSRWLockExclusive(&g_cred_lock);
    CredFreeRecord(&old);
    if (secret) {
        mime_wipe(secret, given->CredentialBlobSize);
        SvcFree(secret);
    }
    return status;
}

long CredStoreRead(const WCHAR *target, DWORD type, DWORD flags, CRED_RECORD *out, DWORD *clear_size)
{
    CRED_CALLER caller;
    memset(out, 0, sizeof(*out));
    *clear_size = 0;
    if (flags > 1 || !known_type(type) || !valid_target(target, type))
        return STATUS_INVALID_PARAMETER_;
    long status = caller_of(&caller);
    if (status)
        return status;
    /* the name as it was written, then without the prefix of its file when it carries one. */
    FIND_CONTEXT find = {target, type, out, FALSE, FALSE};
    AcquireSRWLockShared(&g_cred_lock);
    walk(&caller, find_visit, &find);
    if (!find.Any && bare_target(target, type) != target) {
        find.Target = bare_target(target, type);
        walk(&caller, find_visit, &find);
    }
    ReleaseSRWLockShared(&g_cred_lock);
    if (!find.Any)
        return STATUS_NOT_FOUND_;
    if (!seal_for_caller(out, clear_size)) {
        CredFreeRecord(out);
        return STATUS_UNSUCCESSFUL_;
    }
    return 0;
}

long CredStoreDelete(const WCHAR *target, DWORD type, DWORD flags)
{
    CRED_CALLER caller;
    if (flags > 1 || !known_type(type) || !valid_target(target, type))
        return STATUS_INVALID_PARAMETER_;
    long status = caller_of(&caller);
    if (status)
        return status;
    FIND_CONTEXT remove = {target, type, nullptr, TRUE, FALSE};
    AcquireSRWLockExclusive(&g_cred_lock);
    walk(&caller, find_visit, &remove);
    if (remove.Any) {
        /* a file an earlier write left behind is seen again by the next listing. */
        for (DWORD persist = CRED_PERSIST_LOCAL_MACHINE; persist <= CRED_PERSIST_ENTERPRISE; persist++) {
            WCHAR folder[MAX_PATH], name[33], path[MAX_PATH + 40];
            set_folder(&caller, persist, folder);
            file_name(target, type, name);
            _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", folder, name);
            stale_return(FALSE, path);
        }
    }
    ReleaseSRWLockExclusive(&g_cred_lock);
    return remove.Any ? 0 : STATUS_NOT_FOUND_;
}

typedef struct LIST_CONTEXT {
    const WCHAR *Filter;
    CRED_RECORD *Items;
    DWORD Count, Room;
    BOOL Failed, Whole;
} LIST_CONTEXT;

/* a filter is a name, the start of names when it ends with a star, or their end when it starts with one. */
static BOOL matches(const WCHAR *filter, const WCHAR *target)
{
    if (!filter)
        return TRUE;
    SIZE_T n = wcslen(filter), length = wcslen(target);
    if (n && filter[n - 1] == L'*')
        return !_wcsnicmp(filter, target, n - 1);
    if (filter[0] == L'*')
        return length >= n - 1 && !_wcsicmp(filter + 1, target + length - (n - 1));
    return !_wcsicmp(filter, target);
}

static BOOL prefixed(const WCHAR *filter, DWORD type)
{
    const WCHAR *prefix = target_prefix(type);
    return filter && !_wcsnicmp(filter, prefix, wcslen(prefix));
}

/* a filter may carry the prefix of the files. */
static BOOL matches_cred(const WCHAR *filter, const CRED_RECORD *c)
{
    if (prefixed(filter, c->Type))
        return matches(filter + wcslen(target_prefix(c->Type)), c->TargetName);
    return matches(filter, c->TargetName);
}

static BOOL list_visit(void *context, CRED_RECORD *c, const WCHAR *path, SESSION_CRED *session)
{
    LIST_CONTEXT *l = context;
    /* the name as it was written first: a credential may be named with the prefix itself. */
    BOOL plain = matches(l->Filter, c->TargetName);
    if (!plain && !matches_cred(l->Filter, c))
        return TRUE;
    if (l->Count == l->Room) {
        DWORD room = l->Room ? l->Room * 2 : 16;
        CRED_RECORD *items = SvcAllocZero(room * sizeof(*items));
        if (!items) {
            l->Failed = TRUE;
            return FALSE;
        }
        if (l->Count)
            memcpy(items, l->Items, l->Count * sizeof(*items));
        SvcFree(l->Items);
        l->Items = items;
        l->Room = room;
    }
    if ((l->Whole && c->Type != CRED_TYPE_DOMAIN_VISIBLE_PASSWORD) || (!plain && prefixed(l->Filter, c->Type))) {
        /* asked for all the credentials, or by a name with its prefix, windows gives the names whole, with the prefix
           the files have. */
        const WCHAR *prefix = target_prefix(c->Type);
        SIZE_T head = wcslen(prefix), chars = wcslen(c->TargetName) + 1;
        WCHAR *whole = MIDL_user_allocate((head + chars) * sizeof(WCHAR));
        if (!whole) {
            l->Failed = TRUE;
            return FALSE;
        }
        memcpy(whole, prefix, head * sizeof(WCHAR));
        memcpy(whole + head, c->TargetName, chars * sizeof(WCHAR));
        MIDL_user_free(c->TargetName);
        c->TargetName = whole;
    }
    l->Items[l->Count++] = *c;
    memset(c, 0, sizeof(*c));
    return TRUE;
}

/* the credentials a filter names. the caller frees each record and the list with SvcFree. */
long CredStoreEnumerate(const WCHAR *filter, DWORD flags, CRED_RECORD **items, DWORD **clear_sizes, DWORD *count)
{
    CRED_CALLER caller;
    *items = nullptr;
    *clear_sizes = nullptr;
    *count = 0;
    if (flags & ~(DWORD)CRED_ENUMERATE_ALL_CREDENTIALS)
        return STATUS_INVALID_PARAMETER_;
    if ((flags & CRED_ENUMERATE_ALL_CREDENTIALS) && filter)
        return STATUS_INVALID_PARAMETER_1_;
    long status = caller_of(&caller);
    if (status)
        return status;
    LIST_CONTEXT list = {filter, nullptr, 0, 0, FALSE, (flags & CRED_ENUMERATE_ALL_CREDENTIALS) != 0};
    AcquireSRWLockExclusive(&g_cred_lock);
    stale_return(TRUE, nullptr);
    walk(&caller, list_visit, &list);
    ReleaseSRWLockExclusive(&g_cred_lock);
    DWORD *sizes = list.Count ? SvcAllocZero(list.Count * sizeof(DWORD)) : nullptr;
    status = list.Failed || (list.Count && !sizes) ? STATUS_NO_MEMORY_ : list.Count ? 0 : STATUS_NOT_FOUND_;
    for (DWORD i = 0; !status && i < list.Count; i++)
        if (!seal_for_caller(&list.Items[i], &sizes[i]))
            status = STATUS_UNSUCCESSFUL_;
    if (status) {
        for (DWORD i = 0; i < list.Count; i++)
            CredFreeRecord(&list.Items[i]);
        SvcFree(list.Items);
        SvcFree(sizes);
        return status;
    }
    *items = list.Items;
    *clear_sizes = sizes;
    *count = list.Count;
    return 0;
}

/* ------------------------------------------------------------------ domain credentials by their target */

static BOOL same_name(const WCHAR *a, const WCHAR *b)
{
    return a && b && !_wcsicmp(a, b);
}

/* how a credential's target answers for a server: its name among the server's, then a star before the end of one of the
 * dns names, then the star alone. 0 is no answer. */
static int target_rank(const CRED_TARGET *t, const WCHAR *name)
{
    const WCHAR *exact[] = {t->TargetName,        t->DnsServerName, t->NetbiosServerName,
                            t->DnsDomainName,     t->NetbiosDomainName, t->DnsTreeName};
    for (int i = 0; i < 6; i++)
        if (same_name(exact[i], name))
            return 10 - i;
    if (name[0] == L'*' && name[1] == L'.') {
        const WCHAR *dns[] = {t->TargetName, t->DnsServerName, t->DnsDomainName, t->DnsTreeName};
        SIZE_T tail = wcslen(name + 1);
        for (int i = 0; i < 4; i++) {
            SIZE_T n = dns[i] ? wcslen(dns[i]) : 0;
            if (n > tail && !_wcsicmp(dns[i] + n - tail, name + 1))
                return 3;
        }
    }
    return !wcscmp(name, L"*") ? 1 : 0;
}

/* CredWriteDomainCredentials: a domain credential whose target is one of the names the server gave. */
long CredStoreWriteDomain(const CRED_TARGET *target, const CRED_RECORD *given, DWORD clear_size, DWORD flags)
{
    if (flags & ~(DWORD)CRED_PRESERVE_CREDENTIAL_BLOB)
        return STATUS_INVALID_PARAMETER_;
    if (!domain_type(given->Type) || !given->TargetName || !target_rank(target, given->TargetName))
        return STATUS_INVALID_PARAMETER_;
    return CredStoreWrite(given, clear_size, flags);
}

typedef struct DOMAIN_CONTEXT {
    const CRED_TARGET *Target;
    DWORD Type;
    int Rank;
    CRED_RECORD Best;
} DOMAIN_CONTEXT;

static BOOL domain_visit(void *context, CRED_RECORD *c, const WCHAR *path, SESSION_CRED *session)
{
    DOMAIN_CONTEXT *d = context;
    int rank = c->Type == d->Type ? target_rank(d->Target, c->TargetName) : 0;
    if (rank <= d->Rank)
        return TRUE;
    CredFreeRecord(&d->Best);
    d->Best = *c;
    d->Rank = rank;
    memset(c, 0, sizeof(*c));
    return TRUE;
}

/* CredReadDomainCredentials: for each type the server takes, the credential that names it best. one found through a
 * star says so in its flags. */
long CredStoreReadDomain(const CRED_TARGET *target, DWORD flags, CRED_RECORD **items, DWORD **clear_sizes, DWORD *count)
{
    static const DWORD all[] = {CRED_TYPE_DOMAIN_PASSWORD, CRED_TYPE_DOMAIN_CERTIFICATE, CRED_TYPE_DOMAIN_VISIBLE_PASSWORD};
    CRED_CALLER caller;
    *items = nullptr;
    *clear_sizes = nullptr;
    *count = 0;
    if (flags > 1 || !target->TargetName || !*target->TargetName)
        return STATUS_INVALID_PARAMETER_;
    long status = caller_of(&caller);
    if (status)
        return status;
    const DWORD *types = target->CredTypeCount && target->CredTypes ? target->CredTypes : all;
    DWORD type_count = target->CredTypeCount && target->CredTypes ? target->CredTypeCount : RTL_NUMBER_OF(all);
    CRED_RECORD *list = SvcAllocZero(type_count * sizeof(*list));
    DWORD *sizes = SvcAllocZero(type_count * sizeof(DWORD));
    if (!list || !sizes) {
        SvcFree(list);
        SvcFree(sizes);
        return STATUS_NO_MEMORY_;
    }
    DWORD found = 0;
    AcquireSRWLockShared(&g_cred_lock);
    for (DWORD i = 0; i < type_count; i++) {
        DOMAIN_CONTEXT d = {target, types[i], 0, {}};
        if (domain_type(types[i]))
            walk(&caller, domain_visit, &d);
        if (!d.Rank)
            continue;
        if (d.Rank <= 3)
            d.Best.Flags |= CRED_FLAGS_WILDCARD_MATCH;
        list[found++] = d.Best;
    }
    ReleaseSRWLockShared(&g_cred_lock);
    status = found ? 0 : STATUS_NOT_FOUND_;
    for (DWORD i = 0; !status && i < found; i++)
        if (!seal_for_caller(&list[i], &sizes[i]))
            status = STATUS_UNSUCCESSFUL_;
    if (status) {
        for (DWORD i = 0; i < found; i++)
            CredFreeRecord(&list[i]);
        SvcFree(list);
        SvcFree(sizes);
        return status;
    }
    *items = list;
    *clear_sizes = sizes;
    *count = found;
    return 0;
}

/* CredGetTargetInfo: what the authentication packages learned of a server. windows also shows, some time after it, what
 * a write of domain credentials said of one. neither is kept here, so no server is known. */
long CredStoreTargetInfo(const WCHAR *name, DWORD flags)
{
    if (flags > 1 || !name || !*name)
        return STATUS_INVALID_PARAMETER_;
    return STATUS_NOT_FOUND_;
}

/* CredGetSessionTypes: the persistence each type may have, enterprise for the six types of a session with a profile. */
long CredStoreSessionTypes(DWORD count, DWORD *persist)
{
    for (DWORD i = 0; i < count; i++)
        persist[i] = i >= CRED_TYPE_GENERIC && i <= 6 ? CRED_PERSIST_ENTERPRISE : 0;
    return 0;
}

/* CredFindBestCredential: a generic credential by its name with the namespace, which the answer carries too. */
long CredStoreFindBest(const WCHAR *target, DWORD type, DWORD flags, CRED_RECORD *out, DWORD *clear_size)
{
    memset(out, 0, sizeof(*out));
    *clear_size = 0;
    SIZE_T head = wcslen(GENERIC_PREFIX);
    if (flags || type != CRED_TYPE_GENERIC || !target || _wcsnicmp(target, GENERIC_PREFIX, head) || !target[head])
        return STATUS_INVALID_PARAMETER_;
    long status = CredStoreRead(target + head, type, 0, out, clear_size);
    if (status)
        return status;
    SIZE_T chars = wcslen(out->TargetName) + 1;
    WCHAR *whole = MIDL_user_allocate((head + chars) * sizeof(WCHAR));
    if (!whole) {
        CredFreeRecord(out);
        return STATUS_NO_MEMORY_;
    }
    memcpy(whole, GENERIC_PREFIX, head * sizeof(WCHAR));
    memcpy(whole + head, out->TargetName, chars * sizeof(WCHAR));
    MIDL_user_free(out->TargetName);
    out->TargetName = whole;
    return 0;
}

/* CredBackupCredentials is for the programs of the system. windows answers ERROR_ACCESS_DENIED to the limited and the
 * elevated token of a user, and so does this. for the system itself the backup is not served. */
long CredStoreBackup(void)
{
    CRED_CALLER caller;
    long status = caller_of(&caller);
    if (status)
        return status;
    return wcscmp(caller.Sid, L"S-1-5-18") ? STATUS_ACCESS_DENIED_ : STATUS_NOT_IMPLEMENTED_;
}
