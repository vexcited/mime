/*
 * the ntlm package, and the negotiation that picks it, for a client and a server on the same machine.
 *
 * the messages are the ones of ntlmssp, the negotiate, challenge and authenticate messages, and negotiate wraps them in
 * spnego as the package of windows does when no domain controller can give a kerberos ticket. what differs is how the
 * server learns who the client is. windows checks a response made from the client's password, or on one machine
 * trusts the logon session the client names, which is the local call flag of the messages. only that is here: the
 * client has the manager keep the token it speaks with under a ticket, puts the ticket in its response, and the server
 * asks the manager for the token the ticket stands for. a client that names another account than its own is refused.
 * there is no session key, so messages cannot be signed or sealed.
 */
#include "schannel.h"

#define WINBASEAPI_SEC DECLSPEC_IMPORT

WINBASEAPI_SEC BOOL WINAPI GetTokenInformation(HANDLE token, DWORD klass, PVOID info, DWORD length, PDWORD returned);
WINBASEAPI_SEC BOOL WINAPI LookupAccountSidW(LPCWSTR system, PVOID sid, LPWSTR name, LPDWORD name_size, LPWSTR domain,
                                             LPDWORD domain_size, PDWORD use);
WINBASEAPI_SEC BOOL WINAPI SetThreadToken(PHANDLE thread, HANDLE token);
WINBASEAPI_SEC BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL as_self, PHANDLE token);
WINBASEAPI_SEC BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
WINBASEAPI_SEC DWORD WINAPI SspiPublishToken(BYTE *ticket);
WINBASEAPI_SEC DWORD WINAPI SspiRedeemToken(const BYTE *ticket, HANDLE *token);

#define TOKEN_QUERY_ 0x0008
#define TOKEN_USER_ 1

#ifndef SEC_E_LOGON_DENIED
#define SEC_E_LOGON_DENIED ((SECURITY_STATUS)0x8009030CL)
#endif
#ifndef SEC_E_TARGET_UNKNOWN
#define SEC_E_TARGET_UNKNOWN ((SECURITY_STATUS)0x80090303L)
#endif

#define NTLM_UNICODE 0x00000001
#define NTLM_REQUEST_TARGET 0x00000004
#define NTLM_NTLM 0x00000200
#define NTLM_LOCAL_CALL 0x00004000
#define NTLM_ALWAYS_SIGN 0x00008000
#define NTLM_TARGET_TYPE_SERVER 0x00020000
#define NTLM_EXTENDED_SESSIONSECURITY 0x00080000
#define NTLM_TARGET_INFO 0x00800000
#define NTLM_VERSION 0x02000000
#define NTLM_128 0x20000000
#define NTLM_KEY_EXCH 0x40000000
#define NTLM_56 0x80000000u

#define NTLM_NEGOTIATE 1
#define NTLM_CHALLENGE 2
#define NTLM_AUTHENTICATE 3

#define AV_EOL 0
#define AV_NB_COMPUTER 1
#define AV_NB_DOMAIN 2
#define AV_TIMESTAMP 7

static const UCHAR k_signature[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
/* the object identifiers: ntlmssp, spnego, and the two of kerberos, which mime has no ticket for. */
static const UCHAR k_oid_ntlm[] = {0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0A};
static const UCHAR k_oid_spnego[] = {0x2B, 0x06, 0x01, 0x05, 0x05, 0x02};

typedef struct AUTH_CONTEXT {
    bool Server, Spnego;
    int State;
    ULONG Flags;
    ULONG Negotiated;
    UCHAR ContextId[8];
    HANDLE Token;
    PWSTR Name;
    ULONG Kind;
} AUTH_CONTEXT;

typedef struct SecPkgContext_NamesW_ {
    PWSTR UserName;
} NAMES_W;
typedef struct SecPkgContext_NamesA_ {
    PSTR UserName;
} NAMES_A;
typedef struct SecPkgContext_Sizes_ {
    ULONG MaxToken, MaxSignature, BlockSize, SecurityTrailer;
} SIZES;
typedef struct SecPkgContext_AuthorityW_ {
    PWSTR Authority;
} AUTHORITY_W;
typedef struct SecPkgContext_AuthorityA_ {
    PSTR Authority;
} AUTHORITY_A;
typedef struct SecPkgContext_NegotiationInfoW_ {
    PSecPkgInfoW PackageInfo;
    ULONG NegotiationState;
} NEGOTIATION_W;
typedef struct SecPkgContext_NegotiationInfoA_ {
    PSecPkgInfoA PackageInfo;
    ULONG NegotiationState;
} NEGOTIATION_A;
typedef struct SecPkgContext_Flags_ {
    ULONG Flags;
} FLAGS_;

#define ATTR_FLAGS 14

/* ------------------------------------------------------------------ bytes */

typedef struct BUF {
    UCHAR *Data;
    ULONG Size, Capacity;
    bool Failed;
} BUF;

static void buf_reserve(BUF *b, ULONG more)
{
    if (b->Failed)
        return;
    if (b->Size + more > b->Capacity) {
        ULONG capacity = b->Capacity ? b->Capacity : 256;
        while (capacity < b->Size + more)
            capacity *= 2;
        UCHAR *grown = SecHeapAlloc(capacity);
        if (!grown) {
            b->Failed = true;
            return;
        }
        if (b->Data) {
            memcpy(grown, b->Data, b->Size);
            SecHeapFree(b->Data);
        }
        b->Data = grown;
        b->Capacity = capacity;
    }
}

static void put(BUF *b, const void *data, ULONG n)
{
    buf_reserve(b, n);
    if (!b->Failed) {
        if (n)
            memcpy(b->Data + b->Size, data, n);
        b->Size += n;
    }
}

static void put8(BUF *b, UCHAR v)
{
    put(b, &v, 1);
}

static void put16(BUF *b, USHORT v)
{
    UCHAR bytes[2] = {(UCHAR)v, (UCHAR)(v >> 8)};
    put(b, bytes, 2);
}

static void put32(BUF *b, ULONG v)
{
    UCHAR bytes[4] = {(UCHAR)v, (UCHAR)(v >> 8), (UCHAR)(v >> 16), (UCHAR)(v >> 24)};
    put(b, bytes, 4);
}

static void patch32(BUF *b, ULONG at, ULONG v)
{
    if (!b->Failed && at + 4 <= b->Size) {
        b->Data[at] = (UCHAR)v;
        b->Data[at + 1] = (UCHAR)(v >> 8);
        b->Data[at + 2] = (UCHAR)(v >> 16);
        b->Data[at + 3] = (UCHAR)(v >> 24);
    }
}

static void patch_field(BUF *b, ULONG at, ULONG length, ULONG offset)
{
    if (!b->Failed && at + 8 <= b->Size) {
        b->Data[at] = b->Data[at + 2] = (UCHAR)length;
        b->Data[at + 1] = b->Data[at + 3] = (UCHAR)(length >> 8);
        patch32(b, at + 4, offset);
    }
}

static void put_version(BUF *b)
{
    put8(b, 10);
    put8(b, 0);
    put16(b, 26100);
    put8(b, 0);
    put8(b, 0);
    put8(b, 0);
    put8(b, 15);
}

static void put_text(BUF *b, PCWSTR text)
{
    put(b, text, (ULONG)wcslen(text) * sizeof(WCHAR));
}

/* a field of a message: its bytes are inside the message, or the field is bad. */
static bool field(const UCHAR *m, ULONG size, ULONG at, const UCHAR **data, ULONG *length)
{
    if (at + 8 > size)
        return false;
    ULONG n = m[at] | m[at + 1] << 8, offset = m[at + 4] | m[at + 5] << 8 | m[at + 6] << 16 | (ULONG)m[at + 7] << 24;
    if (n && (offset > size || n > size - offset))
        return false;
    *data = m + offset;
    *length = n;
    return true;
}

static ULONG get32(const UCHAR *m, ULONG at)
{
    return m[at] | m[at + 1] << 8 | m[at + 2] << 16 | (ULONG)m[at + 3] << 24;
}

static bool header(const UCHAR *m, ULONG size, ULONG type)
{
    return size >= 12 && !memcmp(m, k_signature, 8) && get32(m, 8) == type;
}

/* ------------------------------------------------------------------ the ntlm messages */

static void machine_name(WCHAR *name, ULONG capacity)
{
    if (!SecComputerName(name, capacity))
        name[0] = 0;
}

static bool names_match(PCWSTR a, ULONG a_length, PCWSTR b)
{
    return a_length == wcslen(b) && !_wcsnicmp(a, b, a_length);
}

static ULONG negotiate_message(BUF *b)
{
    put(b, k_signature, 8);
    put32(b, NTLM_NEGOTIATE);
    put32(b, NTLM_UNICODE | NTLM_REQUEST_TARGET | NTLM_NTLM | NTLM_LOCAL_CALL | NTLM_ALWAYS_SIGN |
                 NTLM_EXTENDED_SESSIONSECURITY | NTLM_VERSION | NTLM_128 | NTLM_KEY_EXCH | NTLM_56);
    ULONG empty[4] = {};
    put(b, empty, sizeof(empty));
    put_version(b);
    patch_field(b, 16, 0, 40);
    patch_field(b, 24, 0, 40);
    return b->Size;
}

/* the server's answer: its name, the exchange it holds open and the names that identify the machine. */
static void challenge_message(BUF *b, AUTH_CONTEXT *a, ULONG client_flags)
{
    WCHAR machine[64];
    machine_name(machine, RTL_NUMBER_OF(machine));
    ULONG flags = NTLM_UNICODE | NTLM_REQUEST_TARGET | NTLM_NTLM | NTLM_ALWAYS_SIGN | NTLM_TARGET_TYPE_SERVER |
                  NTLM_EXTENDED_SESSIONSECURITY | NTLM_TARGET_INFO | NTLM_VERSION | NTLM_128 | NTLM_KEY_EXCH | NTLM_56;
    if (client_flags & NTLM_LOCAL_CALL)
        flags |= NTLM_LOCAL_CALL;
    a->Negotiated = flags;
    UCHAR server_challenge[8];
    SchRandom(server_challenge, sizeof(server_challenge));
    SchRandom(a->ContextId, sizeof(a->ContextId));
    put(b, k_signature, 8);
    put32(b, NTLM_CHALLENGE);
    ULONG target_at = b->Size;
    ULONG zero[2] = {};
    put(b, zero, 8);
    put32(b, flags);
    put(b, server_challenge, 8);
    put(b, a->ContextId, 8);
    ULONG info_at = b->Size;
    put(b, zero, 8);
    put_version(b);
    ULONG target_offset = b->Size;
    put_text(b, machine);
    ULONG target_length = b->Size - target_offset;
    ULONG info_offset = b->Size;
    put16(b, AV_NB_DOMAIN);
    put16(b, (USHORT)(wcslen(machine) * sizeof(WCHAR)));
    put_text(b, machine);
    put16(b, AV_NB_COMPUTER);
    put16(b, (USHORT)(wcslen(machine) * sizeof(WCHAR)));
    put_text(b, machine);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    put16(b, AV_TIMESTAMP);
    put16(b, 8);
    put32(b, now.dwLowDateTime);
    put32(b, now.dwHighDateTime);
    put16(b, AV_EOL);
    put16(b, 0);
    patch_field(b, target_at, target_length, target_offset);
    patch_field(b, info_at, b->Size - info_offset, info_offset);
}

/* the client's last message: who it is and the ticket that proves it to a server on the machine. */
static bool authenticate_message(BUF *b, AUTH_CONTEXT *a, const UCHAR *challenge, ULONG size, const UCHAR *ticket)
{
    if (!header(challenge, size, NTLM_CHALLENGE) || size < 48)
        return false;
    ULONG flags = get32(challenge, 20);
    UCHAR context_id[8];
    memcpy(context_id, challenge + 32, 8);
    WCHAR machine[64], user[128];
    machine_name(machine, RTL_NUMBER_OF(machine));
    bool system;
    if (!SecUserName(user, RTL_NUMBER_OF(user), &system))
        user[0] = 0;
    a->Negotiated = flags;
    put(b, k_signature, 8);
    put32(b, NTLM_AUTHENTICATE);
    ULONG fields_at = b->Size;
    ULONG zero[12] = {};
    put(b, zero, 6 * 8);
    put32(b, flags);
    put_version(b);
    put(b, zero, 16);
    ULONG lm_at = fields_at, nt_at = fields_at + 8, domain_at = fields_at + 16, user_at = fields_at + 24,
          station_at = fields_at + 32, key_at = fields_at + 40;
    patch_field(b, lm_at, 0, b->Size);
    ULONG nt_offset = b->Size;
    put(b, ticket, 16);
    put(b, context_id, 8);
    patch_field(b, nt_at, 24, nt_offset);
    ULONG domain_offset = b->Size;
    put_text(b, machine);
    patch_field(b, domain_at, (ULONG)wcslen(machine) * sizeof(WCHAR), domain_offset);
    ULONG user_offset = b->Size;
    put_text(b, user);
    patch_field(b, user_at, (ULONG)wcslen(user) * sizeof(WCHAR), user_offset);
    ULONG station_offset = b->Size;
    put_text(b, machine);
    patch_field(b, station_at, (ULONG)wcslen(machine) * sizeof(WCHAR), station_offset);
    patch_field(b, key_at, 0, b->Size);
    return !b->Failed;
}

/* the name the token's user goes by here, as DOMAIN\user. */
static PWSTR token_name(HANDLE token)
{
    UCHAR buffer[512];
    DWORD returned;
    if (!GetTokenInformation(token, TOKEN_USER_, buffer, sizeof(buffer), &returned))
        return nullptr;
    WCHAR name[128], domain[128];
    DWORD name_size = RTL_NUMBER_OF(name), domain_size = RTL_NUMBER_OF(domain), use;
    if (!LookupAccountSidW(nullptr, *(PVOID *)buffer, name, &name_size, domain, &domain_size, &use))
        return nullptr;
    PWSTR out = SecHeapAlloc((wcslen(domain) + 1 + wcslen(name) + 1) * sizeof(WCHAR));
    if (out)
        _snwprintf(out, wcslen(domain) + 1 + wcslen(name) + 1, L"%ls\\%ls", domain, name);
    return out;
}

/* checks a client's last message against the manager and takes the token of the ticket it carries. */
static SECURITY_STATUS accept_authenticate(AUTH_CONTEXT *a, const UCHAR *m, ULONG size)
{
    if (!header(m, size, NTLM_AUTHENTICATE) || size < 64)
        return SEC_E_INVALID_TOKEN;
    const UCHAR *lm, *nt, *domain, *user, *station;
    ULONG lm_length, nt_length, domain_length, user_length, station_length;
    if (!field(m, size, 12, &lm, &lm_length) || !field(m, size, 20, &nt, &nt_length) ||
        !field(m, size, 28, &domain, &domain_length) || !field(m, size, 36, &user, &user_length) ||
        !field(m, size, 44, &station, &station_length))
        return SEC_E_INVALID_TOKEN;
    /* the response is the ticket and the exchange it was made for. */
    if (nt_length != 24 || memcmp(nt + 16, a->ContextId, 8))
        return SEC_E_LOGON_DENIED;
    HANDLE token;
    if (SspiRedeemToken(nt, &token))
        return SEC_E_LOGON_DENIED;
    PWSTR name = token_name(token);
    /* the account it says it is must be the one the ticket belongs to. */
    PCWSTR slash = name ? wcschr(name, L'\\') : nullptr;
    if (!name || !slash || (user_length & 1) || !names_match((PCWSTR)user, user_length / 2, slash + 1)) {
        SecHeapFree(name);
        CloseHandle(token);
        return SEC_E_LOGON_DENIED;
    }
    a->Token = token;
    a->Name = name;
    return SEC_E_OK;
}

/* ------------------------------------------------------------------ spnego */

static void put_length(BUF *b, ULONG n)
{
    if (n < 0x80) {
        put8(b, (UCHAR)n);
    } else if (n < 0x100) {
        put8(b, 0x81);
        put8(b, (UCHAR)n);
    } else if (n < 0x10000) {
        put8(b, 0x82);
        put8(b, (UCHAR)(n >> 8));
        put8(b, (UCHAR)n);
    } else {
        put8(b, 0x83);
        put8(b, (UCHAR)(n >> 16));
        put8(b, (UCHAR)(n >> 8));
        put8(b, (UCHAR)n);
    }
}

static void put_tlv(BUF *b, UCHAR tag, const void *data, ULONG n)
{
    put8(b, tag);
    put_length(b, n);
    put(b, data, n);
}

/* the contents of the element at p, with the position after it. false when it runs past the end. */
static bool read_tlv(const UCHAR **p, const UCHAR *end, UCHAR *tag, const UCHAR **value, ULONG *length)
{
    const UCHAR *at = *p;
    if (end - at < 2)
        return false;
    *tag = *at++;
    ULONG n = *at++;
    if (n & 0x80) {
        ULONG count = n & 0x7F;
        if (!count || count > 3 || (ULONG)(end - at) < count)
            return false;
        n = 0;
        while (count--)
            n = n << 8 | *at++;
    }
    if (n > (ULONG)(end - at))
        return false;
    *value = at;
    *length = n;
    *p = at + n;
    return true;
}

static void wrap_init(BUF *out, const UCHAR *token, ULONG n)
{
    BUF mechs = {}, token_part = {}, init = {}, inner = {};
    put_tlv(&mechs, 0x06, k_oid_ntlm, sizeof(k_oid_ntlm));
    BUF list = {};
    put_tlv(&list, 0x30, mechs.Data, mechs.Size);
    put_tlv(&inner, 0xA0, list.Data, list.Size);
    put_tlv(&token_part, 0x04, token, n);
    put_tlv(&inner, 0xA2, token_part.Data, token_part.Size);
    put_tlv(&init, 0x30, inner.Data, inner.Size);
    BUF body = {};
    put_tlv(&body, 0x06, k_oid_spnego, sizeof(k_oid_spnego));
    put_tlv(&body, 0xA0, init.Data, init.Size);
    put_tlv(out, 0x60, body.Data, body.Size);
    SecHeapFree(mechs.Data);
    SecHeapFree(token_part.Data);
    SecHeapFree(init.Data);
    SecHeapFree(inner.Data);
    SecHeapFree(list.Data);
    SecHeapFree(body.Data);
    if (mechs.Failed || list.Failed || inner.Failed || token_part.Failed || init.Failed || body.Failed)
        out->Failed = true;
}

/* a response of the negotiation: its state, the package it chose, and the package's token if there is one. */
static void wrap_response(BUF *out, int state, bool mech, const UCHAR *token, ULONG n)
{
    BUF fields = {}, part = {}, seq = {};
    UCHAR value = (UCHAR)state;
    put_tlv(&part, 0x0A, &value, 1);
    put_tlv(&fields, 0xA0, part.Data, part.Size);
    SecHeapFree(part.Data);
    part = (BUF){};
    if (mech) {
        put_tlv(&part, 0x06, k_oid_ntlm, sizeof(k_oid_ntlm));
        put_tlv(&fields, 0xA1, part.Data, part.Size);
        SecHeapFree(part.Data);
        part = (BUF){};
    }
    if (token) {
        put_tlv(&part, 0x04, token, n);
        put_tlv(&fields, 0xA2, part.Data, part.Size);
    }
    put_tlv(&seq, 0x30, fields.Data, fields.Size);
    put_tlv(out, 0xA1, seq.Data, seq.Size);
    if (fields.Failed || part.Failed || seq.Failed)
        out->Failed = true;
    SecHeapFree(fields.Data);
    SecHeapFree(part.Data);
    SecHeapFree(seq.Data);
}

/* the ntlm token of the first message of a negotiation: from a token that lists ntlm among its packages. */
static SECURITY_STATUS unwrap_init(const UCHAR *m, ULONG size, const UCHAR **token, ULONG *n)
{
    const UCHAR *p = m, *end = m + size, *value;
    UCHAR tag;
    ULONG length;
    if (!read_tlv(&p, end, &tag, &value, &length) || tag != 0x60)
        return SEC_E_INVALID_TOKEN;
    const UCHAR *q = value, *q_end = value + length;
    if (!read_tlv(&q, q_end, &tag, &value, &length) || tag != 0x06 || length != sizeof(k_oid_spnego) ||
        memcmp(value, k_oid_spnego, length))
        return SEC_E_INVALID_TOKEN;
    if (!read_tlv(&q, q_end, &tag, &value, &length) || tag != 0xA0)
        return SEC_E_INVALID_TOKEN;
    const UCHAR *s = value, *s_end = value + length;
    if (!read_tlv(&s, s_end, &tag, &value, &length) || tag != 0x30)
        return SEC_E_INVALID_TOKEN;
    const UCHAR *f = value, *f_end = value + length;
    bool ntlm = false;
    bool first_is_ntlm = false;
    *token = nullptr;
    *n = 0;
    while (f < f_end) {
        const UCHAR *item;
        ULONG item_length;
        if (!read_tlv(&f, f_end, &tag, &item, &item_length))
            return SEC_E_INVALID_TOKEN;
        if (tag == 0xA0) {
            const UCHAR *l = item, *l_end = item + item_length, *oids;
            ULONG oids_length;
            UCHAR t;
            if (!read_tlv(&l, l_end, &t, &oids, &oids_length) || t != 0x30)
                return SEC_E_INVALID_TOKEN;
            const UCHAR *o = oids, *o_end = oids + oids_length;
            bool first = true;
            while (o < o_end) {
                const UCHAR *oid;
                ULONG oid_length;
                if (!read_tlv(&o, o_end, &t, &oid, &oid_length) || t != 0x06)
                    return SEC_E_INVALID_TOKEN;
                if (oid_length == sizeof(k_oid_ntlm) && !memcmp(oid, k_oid_ntlm, oid_length)) {
                    ntlm = true;
                    if (first)
                        first_is_ntlm = true;
                }
                first = false;
            }
        } else if (tag == 0xA2) {
            const UCHAR *l = item, *l_end = item + item_length, *octets;
            ULONG octets_length;
            UCHAR t;
            if (!read_tlv(&l, l_end, &t, &octets, &octets_length) || t != 0x04)
                return SEC_E_INVALID_TOKEN;
            *token = octets;
            *n = octets_length;
        }
    }
    if (!ntlm)
        return SEC_E_NO_AUTHENTICATING_AUTHORITY;
    /* a token made for the package that came first, when that is not ntlm, is no use to ntlm. */
    if (!first_is_ntlm)
        *token = nullptr, *n = 0;
    return *token ? SEC_E_OK : SEC_E_INVALID_TOKEN;
}

/* the fields of a response: its state and its token. */
static SECURITY_STATUS unwrap_response(const UCHAR *m, ULONG size, int *state, const UCHAR **token, ULONG *n)
{
    const UCHAR *p = m, *end = m + size, *value;
    UCHAR tag;
    ULONG length;
    *state = -1;
    *token = nullptr;
    *n = 0;
    if (!read_tlv(&p, end, &tag, &value, &length) || tag != 0xA1)
        return SEC_E_INVALID_TOKEN;
    const UCHAR *s = value, *s_end = value + length;
    if (!read_tlv(&s, s_end, &tag, &value, &length) || tag != 0x30)
        return SEC_E_INVALID_TOKEN;
    const UCHAR *f = value, *f_end = value + length;
    while (f < f_end) {
        const UCHAR *item;
        ULONG item_length;
        if (!read_tlv(&f, f_end, &tag, &item, &item_length))
            return SEC_E_INVALID_TOKEN;
        const UCHAR *l = item, *l_end = item + item_length, *inner;
        ULONG inner_length;
        UCHAR t;
        if (!read_tlv(&l, l_end, &t, &inner, &inner_length))
            return SEC_E_INVALID_TOKEN;
        if (tag == 0xA0 && t == 0x0A && inner_length == 1) {
            *state = inner[0];
        } else if (tag == 0xA2 && t == 0x04) {
            *token = inner;
            *n = inner_length;
        }
    }
    return SEC_E_OK;
}

/* ------------------------------------------------------------------ the context */

static const UCHAR *input_token(PSecBufferDesc input, ULONG *n)
{
    *n = 0;
    if (!input)
        return nullptr;
    for (ULONG i = 0; i < input->cBuffers; i++) {
        SecBuffer *b = &input->pBuffers[i];
        if ((b->BufferType & ~SECBUFFER_ATTRMASK) == SECBUFFER_TOKEN) {
            *n = b->cbBuffer;
            return b->pvBuffer;
        }
    }
    return nullptr;
}

/* the finished message goes to the caller's token buffer, or to memory of its own when it asked for that. */
static SECURITY_STATUS deliver(PSecBufferDesc output, ULONG flags, BUF *message)
{
    if (message->Failed)
        return SEC_E_INSUFFICIENT_MEMORY;
    if (!output)
        return message->Size ? SEC_E_INVALID_TOKEN : SEC_E_OK;
    SecBuffer *token = nullptr;
    for (ULONG i = 0; i < output->cBuffers; i++)
        if ((output->pBuffers[i].BufferType & ~SECBUFFER_ATTRMASK) == SECBUFFER_TOKEN) {
            token = &output->pBuffers[i];
            break;
        }
    if (!token)
        return message->Size ? SEC_E_INVALID_TOKEN : SEC_E_OK;
    if (!message->Size) {
        token->cbBuffer = 0;
        return SEC_E_OK;
    }
    if (flags & ISC_REQ_ALLOCATE_MEMORY) {
        void *copy = LocalAlloc(LMEM_FIXED, message->Size);
        if (!copy)
            return SEC_E_INSUFFICIENT_MEMORY;
        memcpy(copy, message->Data, message->Size);
        token->pvBuffer = copy;
        token->cbBuffer = message->Size;
        token->BufferType = SECBUFFER_TOKEN;
        return SEC_E_OK;
    }
    if (token->cbBuffer < message->Size) {
        token->cbBuffer = message->Size;
        return SEC_E_BUFFER_TOO_SMALL;
    }
    memcpy(token->pvBuffer, message->Data, message->Size);
    token->cbBuffer = message->Size;
    return SEC_E_OK;
}

/* whether the credential asks for an account other than the one that is logged on. */
static bool names_other_account(PCredHandle credential)
{
    WCHAR name[260];
    if (!SecCredentialName(credential, name, RTL_NUMBER_OF(name)))
        return false;
    PCWSTR user = wcschr(name, L'\\');
    user = user ? user + 1 : name;
    WCHAR own[128];
    bool system;
    if (!SecUserName(own, RTL_NUMBER_OF(own), &system))
        return true;
    return _wcsicmp(user, own) != 0;
}

/* the name of the account that is logged on here, as DOMAIN\user. */
static PWSTR token_name_of_self(void)
{
    WCHAR machine[64], user[128];
    machine_name(machine, RTL_NUMBER_OF(machine));
    bool system;
    if (!SecUserName(user, RTL_NUMBER_OF(user), &system))
        return nullptr;
    SIZE_T n = wcslen(machine) + 1 + wcslen(user) + 1;
    PWSTR out = SecHeapAlloc(n * sizeof(WCHAR));
    if (out)
        _snwprintf(out, n, L"%ls\\%ls", machine, user);
    return out;
}

SECURITY_STATUS SecAuthEstablish(const SEC_PACKAGE *package, PCredHandle credential, AUTH_CONTEXT **context, bool server,
                                 PCWSTR target, ULONG flags, PSecBufferDesc input, PSecBufferDesc output,
                                 ULONG *attributes, PTimeStamp expiry)
{
    AUTH_CONTEXT *a = *context;
    if (!a) {
        a = SecHeapAlloc(sizeof(*a));
        if (!a)
            return SEC_E_INSUFFICIENT_MEMORY;
        memset(a, 0, sizeof(*a));
        a->Server = server;
        a->Spnego = package->Kind == SEC_KIND_NEGOTIATE;
        a->Kind = package->Kind;
        *context = a;
    }
    a->Flags = flags;
    ULONG in_size;
    const UCHAR *in = input_token(input, &in_size);
    BUF out = {};
    SECURITY_STATUS status;
    const UCHAR *message = in;
    ULONG message_size = in_size;
    int spnego_state = -1;

    if (!server) {
        if (a->State == 0) {
            if (names_other_account(credential)) {
                status = SEC_E_LOGON_DENIED;
                goto done;
            }
            BUF neg = {};
            negotiate_message(&neg);
            if (a->Spnego)
                wrap_init(&out, neg.Data, neg.Size);
            else
                put(&out, neg.Data, neg.Size);
            if (neg.Failed)
                out.Failed = true;
            SecHeapFree(neg.Data);
            a->State = 1;
            status = SEC_I_CONTINUE_NEEDED;
        } else if (a->State == 1) {
            if (a->Spnego) {
                const UCHAR *token;
                ULONG token_size;
                status = in ? unwrap_response(in, in_size, &spnego_state, &token, &token_size) : SEC_E_INVALID_TOKEN;
                if (status != SEC_E_OK)
                    goto done;
                if (spnego_state == 2) {
                    status = SEC_E_LOGON_DENIED;
                    goto done;
                }
                message = token;
                message_size = token_size;
            }
            if (!message) {
                status = SEC_E_INVALID_TOKEN;
                goto done;
            }
            UCHAR ticket[16];
            if (SspiPublishToken(ticket)) {
                status = SEC_E_NO_CREDENTIALS;
                goto done;
            }
            BUF auth = {};
            if (!authenticate_message(&auth, a, message, message_size, ticket)) {
                SecHeapFree(auth.Data);
                status = SEC_E_INVALID_TOKEN;
                goto done;
            }
            if (a->Spnego)
                wrap_response(&out, 1, false, auth.Data, auth.Size);
            else
                put(&out, auth.Data, auth.Size);
            SecHeapFree(auth.Data);
            a->State = a->Spnego ? 2 : 3;
            status = a->Spnego ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
            if (status == SEC_E_OK)
                a->Name = token_name_of_self();
        } else if (a->State == 2 && a->Spnego) {
            const UCHAR *token;
            ULONG token_size;
            status = in ? unwrap_response(in, in_size, &spnego_state, &token, &token_size) : SEC_E_INVALID_TOKEN;
            if (status != SEC_E_OK)
                goto done;
            if (spnego_state != 0) {
                status = SEC_E_LOGON_DENIED;
                goto done;
            }
            a->State = 3;
            a->Name = token_name_of_self();
            status = SEC_E_OK;
        } else {
            status = SEC_E_OUT_OF_SEQUENCE;
        }
    } else {
        if (a->State == 0) {
            if (!in) {
                status = SEC_E_INVALID_TOKEN;
                goto done;
            }
            a->Spnego = in_size && in[0] == 0x60;
            if (a->Spnego) {
                status = unwrap_init(in, in_size, &message, &message_size);
                if (status != SEC_E_OK)
                    goto done;
            }
            if (!header(message, message_size, NTLM_NEGOTIATE) || message_size < 32) {
                status = SEC_E_INVALID_TOKEN;
                goto done;
            }
            BUF challenge = {};
            challenge_message(&challenge, a, get32(message, 12));
            if (a->Spnego)
                wrap_response(&out, 1, true, challenge.Data, challenge.Size);
            else
                put(&out, challenge.Data, challenge.Size);
            if (challenge.Failed)
                out.Failed = true;
            SecHeapFree(challenge.Data);
            a->State = 1;
            status = SEC_I_CONTINUE_NEEDED;
        } else if (a->State == 1) {
            if (!in) {
                status = SEC_E_INVALID_TOKEN;
                goto done;
            }
            if (a->Spnego) {
                const UCHAR *token;
                ULONG token_size;
                status = unwrap_response(in, in_size, &spnego_state, &token, &token_size);
                if (status != SEC_E_OK)
                    goto done;
                message = token;
                message_size = token_size;
            }
            if (!message) {
                status = SEC_E_INVALID_TOKEN;
                goto done;
            }
            status = accept_authenticate(a, message, message_size);
            if (status != SEC_E_OK)
                goto done;
            if (a->Spnego)
                wrap_response(&out, 0, false, nullptr, 0);
            a->State = 3;
        } else {
            status = SEC_E_OUT_OF_SEQUENCE;
        }
    }
done:
    if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED) {
        SECURITY_STATUS delivered = deliver(output, flags, &out);
        if (delivered != SEC_E_OK) {
            status = delivered;
            /* a buffer that was too small is asked for again with the same input. */
            if (delivered == SEC_E_BUFFER_TOO_SMALL)
                a->State = a->State ? a->State - 1 : 0;
        }
    }
    SecHeapFree(out.Data);
    if (attributes)
        *attributes = (flags & (ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM |
                                ISC_REQ_INTEGRITY)) |
                      ((flags & ISC_REQ_ALLOCATE_MEMORY) ? ISC_RET_ALLOCATED_MEMORY : 0);
    if (expiry)
        expiry->QuadPart = SEC_NEVER_EXPIRES;
    return status;
}

void SecAuthFree(AUTH_CONTEXT *a)
{
    if (!a)
        return;
    if (a->Token)
        CloseHandle(a->Token);
    SecHeapFree(a->Name);
    SecHeapFree(a);
}

static PWSTR copy_local(PCWSTR text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    PWSTR out = LocalAlloc(LMEM_FIXED, bytes);
    if (out)
        memcpy(out, text, bytes);
    return out;
}

static PSTR copy_local_ansi(PCWSTR text)
{
    int n = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    PSTR out = n > 0 ? LocalAlloc(LMEM_FIXED, n) : nullptr;
    if (out)
        WideCharToMultiByte(CP_ACP, 0, text, -1, out, n, nullptr, nullptr);
    return out;
}

SECURITY_STATUS SecAuthQuery(AUTH_CONTEXT *a, const SEC_PACKAGE *package, ULONG attribute, void *buffer, bool ansi)
{
    if (!buffer)
        return SEC_E_INVALID_PARAMETER;
    bool done = a->State == 3;
    switch (attribute) {
    case SECPKG_ATTR_SIZES: {
        SIZES *sizes = buffer;
        sizes->MaxToken = package->MaxToken;
        sizes->MaxSignature = 16;
        sizes->BlockSize = 1;
        sizes->SecurityTrailer = 16;
        return SEC_E_OK;
    }
    case SECPKG_ATTR_NAMES:
    case SECPKG_ATTR_AUTHORITY: {
        if (!done || !a->Name)
            return SEC_E_NO_CREDENTIALS;
        PCWSTR text = a->Name;
        WCHAR machine[64];
        if (attribute == SECPKG_ATTR_AUTHORITY) {
            machine_name(machine, RTL_NUMBER_OF(machine));
            text = machine;
        }
        PVOID copy = ansi ? (PVOID)copy_local_ansi(text) : (PVOID)copy_local(text);
        if (!copy)
            return SEC_E_INSUFFICIENT_MEMORY;
        *(PVOID *)buffer = copy;
        return SEC_E_OK;
    }
    case SECPKG_ATTR_NEGOTIATION_INFO: {
        PSecPkgInfoW info;
        const SEC_PACKAGE *ntlm = SecFindPackageW(L"NTLM");
        SECURITY_STATUS status = ansi ? SecCopyPackageInfoA(ntlm, (PSecPkgInfoA *)&info) : SecCopyPackageInfoW(ntlm, &info);
        if (status != SEC_E_OK)
            return status;
        NEGOTIATION_W *negotiation = buffer;
        negotiation->PackageInfo = info;
        negotiation->NegotiationState = done ? 0 : 1;
        return SEC_E_OK;
    }
    case ATTR_FLAGS:
        ((FLAGS_ *)buffer)->Flags = a->Flags & (ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_CONFIDENTIALITY |
                                                 ISC_REQ_INTEGRITY | ISC_REQ_STREAM);
        return SEC_E_OK;
    default:
        return SEC_E_UNSUPPORTED_FUNCTION;
    }
}

/* the thread speaks as the client until it reverts. */
SECURITY_STATUS SecAuthImpersonate(AUTH_CONTEXT *a)
{
    if (!a->Server || !a->Token || a->State != 3)
        return SEC_E_NO_IMPERSONATION;
    return SetThreadToken(nullptr, a->Token) ? SEC_E_OK : SEC_E_NO_IMPERSONATION;
}

SECURITY_STATUS SecAuthRevert(AUTH_CONTEXT *a)
{
    if (!a->Server || !a->Token)
        return SEC_E_NO_IMPERSONATION;
    return SetThreadToken(nullptr, nullptr) ? SEC_E_OK : SEC_E_NO_IMPERSONATION;
}

/* a copy of the client's token that the caller closes. */
SECURITY_STATUS SecAuthToken(AUTH_CONTEXT *a, void **token)
{
    if (!token)
        return SEC_E_INVALID_PARAMETER;
    *token = nullptr;
    if (!a->Server || !a->Token || a->State != 3)
        return SEC_E_NO_IMPERSONATION;
    HANDLE copy;
    if (!DuplicateHandle(GetCurrentProcess(), a->Token, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return SEC_E_INSUFFICIENT_MEMORY;
    *token = copy;
    return SEC_E_OK;
}
