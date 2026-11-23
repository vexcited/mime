#include "ntos.h"
#include <crypto/crypto.h>

#define SIGNING_FLAG_CACHE 0x4
#define CACHE_FLAGS 0x8002
#define LEVEL_UNSIGNED_CACHED 2
#define LEVEL_AUTHENTICODE 4
#define LEVEL_MICROSOFT 8
#define LEVEL_WINDOWS 12
#define CALG_SHA_256_ 0x800c
#define MAX_SIGNATURE (1u << 20)
#define CHUNK (256u << 10)
#define FILE_READ_DATA_ 0x1
#define FILE_WRITE_DATA_ 0x2
#define FILE_APPEND_DATA_ 0x4
#define FILE_WRITE_ATTRIBUTES_ 0x100
#define GENERIC_READ_ 0x80000000
#define GENERIC_WRITE_ 0x40000000
#define GENERIC_ALL_ 0x10000000
#define STATUS_WRITABLE_ STATUS_SHARING_VIOLATION /* what a handle that may write the file gets. */

/*
 * windows keeps the level code integrity found for a file in an extended attribute of ntfs, until the file changes.
 * here the machine keeps it, by the identity of the file and the time it was last written.
 */
typedef struct SIGNING_ENTRY {
    struct SIGNING_ENTRY *Next;
    LARGE_INTEGER FileId, Created, Changed, LastWrite, Size;
    UCHAR Level;
    BOOLEAN AttributesWritable; /* measured: an unsigned file cached through such a handle is told with flag 0x4000. */
    UCHAR Thumbprint[32];
    BOOLEAN HasThumbprint;
} SIGNING_ENTRY;

static SIGNING_ENTRY *g_entries;
static KSPIN_LOCK g_lock;

void *mime_crypto_alloc(size_t size)
{
    void *p = ExAllocatePool2(POOL_FLAG_PAGED, size ? size : 1, 'giSM');
    if (p)
        memset(p, 0, size ? size : 1);
    return p;
}

void mime_crypto_free(void *p)
{
    if (p)
        ExFreePool(p);
}

bool mime_crypto_random(void *out, size_t size)
{
    return false;
}

/* ------------------------------------------------------------------ der */

typedef struct DER {
    const UCHAR *Data;
    SIZE_T Size;
} DER;

typedef struct TLV {
    UCHAR Tag;
    const UCHAR *Start, *Content; /* the whole element and its contents. */
    SIZE_T Length, Total;
} TLV;

static BOOLEAN der_next(DER *d, TLV *t)
{
    if (d->Size < 2)
        return FALSE;
    const UCHAR *p = d->Data;
    SIZE_T head = 2, length = p[1];
    if (length & 0x80) {
        ULONG n = length & 0x7f;
        if (!n || n > 4 || d->Size < 2 + n)
            return FALSE;
        length = 0;
        for (ULONG i = 0; i < n; i++)
            length = length << 8 | p[2 + i];
        head += n;
    }
    if (length > d->Size - head)
        return FALSE;
    t->Tag = p[0];
    t->Start = p;
    t->Content = p + head;
    t->Length = length;
    t->Total = head + length;
    d->Data += t->Total;
    d->Size -= t->Total;
    return TRUE;
}

static DER der_of(const TLV *t)
{
    return (DER){t->Content, t->Length};
}

/* the next element, which must carry this tag. */
static BOOLEAN der_take(DER *d, UCHAR tag, TLV *t)
{
    return der_next(d, t) && t->Tag == tag;
}

static BOOLEAN oid_is(const TLV *t, const UCHAR *oid, SIZE_T size)
{
    return t->Tag == 0x06 && t->Length == size && !memcmp(t->Content, oid, size);
}

static const UCHAR OID_SHA1[] = {0x2b, 0x0e, 0x03, 0x02, 0x1a};
static const UCHAR OID_SHA256[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01};
static const UCHAR OID_SHA384[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02};
static const UCHAR OID_SHA512[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03};
static const UCHAR OID_RSA[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};
static const UCHAR OID_RSA_SHA1[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05};
static const UCHAR OID_RSA_SHA256[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b};
static const UCHAR OID_RSA_SHA384[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c};
static const UCHAR OID_RSA_SHA512[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0d};
static const UCHAR OID_MESSAGE_DIGEST[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x04};
static const UCHAR OID_EKU[] = {0x55, 0x1d, 0x25};
static const UCHAR OID_WINDOWS_COMPONENT[] = {0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x0a, 0x03, 0x06};

/* the hash of an algorithm identifier, digest or signature. */
static const MimeHashAlg *hash_of(const TLV *algorithm)
{
    DER d = der_of(algorithm);
    TLV oid;
    if (!der_take(&d, 0x06, &oid))
        return nullptr;
    if (oid_is(&oid, OID_SHA1, sizeof(OID_SHA1)) || oid_is(&oid, OID_RSA_SHA1, sizeof(OID_RSA_SHA1)))
        return &mime_sha1;
    if (oid_is(&oid, OID_SHA256, sizeof(OID_SHA256)) || oid_is(&oid, OID_RSA_SHA256, sizeof(OID_RSA_SHA256)))
        return &mime_sha256;
    if (oid_is(&oid, OID_SHA384, sizeof(OID_SHA384)) || oid_is(&oid, OID_RSA_SHA384, sizeof(OID_RSA_SHA384)))
        return &mime_sha384;
    if (oid_is(&oid, OID_SHA512, sizeof(OID_SHA512)) || oid_is(&oid, OID_RSA_SHA512, sizeof(OID_RSA_SHA512)))
        return &mime_sha512;
    return nullptr;
}

/* ------------------------------------------------------------------ certificates */

typedef struct CERT {
    TLV Whole, Tbs, Algorithm, Signature;
    TLV Serial, Issuer, Subject, KeyInfo, Extensions;
} CERT;

static BOOLEAN cert_parse(const UCHAR *data, SIZE_T size, CERT *c)
{
    memset(c, 0, sizeof(*c));
    DER d = {data, size}, outer, tbs;
    if (!der_take(&d, 0x30, &c->Whole))
        return FALSE;
    outer = der_of(&c->Whole);
    if (!der_take(&outer, 0x30, &c->Tbs) || !der_take(&outer, 0x30, &c->Algorithm) ||
        !der_take(&outer, 0x03, &c->Signature))
        return FALSE;
    tbs = der_of(&c->Tbs);
    TLV t, skip;
    if (!der_next(&tbs, &t))
        return FALSE;
    if (t.Tag == 0xa0 && !der_next(&tbs, &t))
        return FALSE;
    c->Serial = t;
    if (c->Serial.Tag != 0x02 || !der_take(&tbs, 0x30, &skip) || !der_take(&tbs, 0x30, &c->Issuer) ||
        !der_take(&tbs, 0x30, &skip) || !der_take(&tbs, 0x30, &c->Subject) || !der_take(&tbs, 0x30, &c->KeyInfo))
        return FALSE;
    while (der_next(&tbs, &t))
        if (t.Tag == 0xa3)
            c->Extensions = t;
    return TRUE;
}

static BOOLEAN same(const TLV *a, const TLV *b)
{
    return a->Total == b->Total && !memcmp(a->Start, b->Start, a->Total);
}

/* the rsa key of a subject public key info. */
static MimeRsa *key_of(const TLV *info)
{
    DER d = der_of(info), alg;
    TLV algorithm, oid, bits, seq, n, e;
    if (!der_take(&d, 0x30, &algorithm) || !der_take(&d, 0x03, &bits) || bits.Length < 2 || bits.Content[0])
        return nullptr;
    alg = der_of(&algorithm);
    if (!der_take(&alg, 0x06, &oid) || !oid_is(&oid, OID_RSA, sizeof(OID_RSA)))
        return nullptr;
    DER key = {bits.Content + 1, bits.Length - 1};
    if (!der_take(&key, 0x30, &seq))
        return nullptr;
    DER parts = der_of(&seq);
    if (!der_take(&parts, 0x02, &n) || !der_take(&parts, 0x02, &e))
        return nullptr;
    MimeRsa *rsa = mime_rsa_new();
    if (!rsa)
        return nullptr;
    if (!mime_bn_from_bytes(&rsa->n, n.Content, n.Length) || !mime_bn_from_bytes(&rsa->e, e.Content, e.Length) ||
        mime_rsa_check(rsa) != MIME_CRYPTO_OK) {
        mime_rsa_free(rsa);
        return nullptr;
    }
    return rsa;
}

/* a pkcs #1 v1.5 signature by a key over data. */
static BOOLEAN verify(const TLV *key_info, const TLV *algorithm, const UCHAR *data, SIZE_T size, const UCHAR *signature,
                      SIZE_T signature_size, const MimeHashAlg *alg)
{
    if (!alg)
        alg = hash_of(algorithm);
    MimeRsa *rsa = alg ? key_of(key_info) : nullptr;
    if (!rsa)
        return FALSE;
    UCHAR hash[MIME_HASH_MAX_DIGEST];
    mime_hash(alg, data, size, hash);
    BOOLEAN ok = mime_rsa_pkcs1_verify(rsa, alg, hash, alg->digest_size, signature, signature_size) == MIME_CRYPTO_OK;
    mime_rsa_free(rsa);
    return ok;
}

/* a certificate signed by the key of another. */
static BOOLEAN signed_by(const CERT *c, const CERT *issuer)
{
    if (c->Signature.Length < 2 || c->Signature.Content[0])
        return FALSE;
    return verify(&issuer->KeyInfo, &c->Algorithm, c->Tbs.Start, c->Tbs.Total, c->Signature.Content + 1,
                  c->Signature.Length - 1, nullptr);
}

static BOOLEAN has_usage(const CERT *c, const UCHAR *usage, SIZE_T size)
{
    if (!c->Extensions.Start)
        return FALSE;
    DER d = der_of(&c->Extensions), list;
    TLV seq, ext;
    if (!der_take(&d, 0x30, &seq))
        return FALSE;
    list = der_of(&seq);
    while (der_take(&list, 0x30, &ext)) {
        DER e = der_of(&ext), uses;
        TLV oid, t, value, purposes, purpose;
        if (!der_take(&e, 0x06, &oid) || !oid_is(&oid, OID_EKU, sizeof(OID_EKU)) || !der_next(&e, &t))
            continue;
        value = t;
        if (t.Tag == 0x01 && !der_next(&e, &value))
            continue;
        DER inner = der_of(&value);
        if (value.Tag != 0x04 || !der_take(&inner, 0x30, &purposes))
            continue;
        uses = der_of(&purposes);
        while (der_take(&uses, 0x06, &purpose))
            if (oid_is(&purpose, usage, size))
                return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the roots of the machine */

typedef struct KEY_BASIC_INFORMATION_ {
    LARGE_INTEGER LastWriteTime;
    ULONG TitleIndex, NameLength;
    WCHAR Name[64];
} KEY_BASIC_INFORMATION_;

NTSTATUS NTAPI ZwOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSTATUS NTAPI ZwEnumerateKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length, PULONG returned);
NTSTATUS NTAPI ZwClose(HANDLE handle);

#define STORES L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\SystemCertificates\\"
#define CERT_CERT_PROP_ID 32
#define KEY_READ_ 0x20019

/* the roots of microsoft, by the sha-1 names their stores keep them under. */
static const PCWSTR g_microsoft_roots[] = {
    L"8F43288AD272F3103B6FB1428485EA3014C0BCFE", /* microsoft root certificate authority 2011. */
    L"3B1EFD3A66EA28B16697394703A72CA340A05BD5", /* microsoft root certificate authority 2010. */
    L"CDD4EEAE6000AC7F40C3802C171E30148030C072", /* microsoft root certificate authority. */
    L"A43489159A520F0D93D032CCAF37E7FE20A8B419", /* microsoft root authority. */
};

/* the encoded certificate among the serialized elements of a Blob value. */
static BOOLEAN blob_certificate(const UCHAR *blob, ULONG size, const UCHAR **cert, ULONG *cert_size)
{
    for (ULONG at = 0; at + 12 <= size;) {
        ULONG id, length;
        memcpy(&id, blob + at, 4);
        memcpy(&length, blob + at + 8, 4);
        if (length > size - at - 12)
            return FALSE;
        if (id == CERT_CERT_PROP_ID) {
            *cert = blob + at + 12;
            *cert_size = length;
            return TRUE;
        }
        at += 12 + length;
    }
    return FALSE;
}

/* the level of the root that issued the top of a chain: microsoft, another root of the machine, or none. */
static UCHAR root_level(const CERT *top)
{
    static const PCWSTR stores[] = {L"ROOT", L"AuthRoot"};
    UCHAR level = 0;
    UCHAR *blob = ExAllocatePool2(POOL_FLAG_PAGED, 16384, 'giSM');
    if (!blob)
        return 0;
    for (ULONG s = 0; s < RTL_NUMBER_OF(stores) && !level; s++) {
        WCHAR path[160];
        _snwprintf(path, RTL_NUMBER_OF(path), STORES L"%ls\\Certificates", stores[s]);
        UNICODE_STRING name;
        RtlInitUnicodeString(&name, path);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
        HANDLE key;
        if (!NT_SUCCESS(ZwOpenKey(&key, KEY_READ_, &oa)))
            continue;
        KEY_BASIC_INFORMATION_ info;
        ULONG returned;
        for (ULONG i = 0; !level && NT_SUCCESS(ZwEnumerateKey(key, i, 0, &info, sizeof(info) - 2, &returned)); i++) {
            WCHAR thumb[64] = {};
            ULONG chars = info.NameLength / sizeof(WCHAR) < 63 ? info.NameLength / sizeof(WCHAR) : 63;
            memcpy(thumb, info.Name, chars * sizeof(WCHAR));
            WCHAR entry[240];
            _snwprintf(entry, RTL_NUMBER_OF(entry), L"%ls\\%ls", path, thumb);
            ULONG type, length = 16384;
            const UCHAR *der;
            ULONG der_size;
            CERT root;
            if (!NT_SUCCESS(CmQueryValue(entry, L"Blob", &type, blob, &length)) ||
                !blob_certificate(blob, length, &der, &der_size) || !cert_parse(der, der_size, &root) ||
                !same(&root.Subject, &top->Issuer) || !signed_by(top, &root))
                continue;
            level = LEVEL_AUTHENTICODE;
            for (ULONG m = 0; m < RTL_NUMBER_OF(g_microsoft_roots); m++)
                if (!_wcsicmp(thumb, g_microsoft_roots[m]))
                    level = LEVEL_MICROSOFT;
        }
        ZwClose(key);
    }
    ExFreePool(blob);
    return level;
}

/* ------------------------------------------------------------------ the file */

typedef struct IMAGE {
    HANDLE File;
    LONG64 Size;
    ULONG ChecksumAt, DirectoryAt, SignatureAt, SignatureSize;
} IMAGE;

static NTSTATUS read_at(HANDLE file, LONG64 offset, void *buffer, ULONG length)
{
    IO_STATUS_BLOCK io = {};
    LARGE_INTEGER at = {.QuadPart = offset};
    NTSTATUS status = ZwReadFile(file, nullptr, nullptr, nullptr, &io, buffer, length, &at, nullptr);
    if (NT_SUCCESS(status) && io.Information != length)
        status = STATUS_END_OF_FILE;
    return status;
}

/* where the headers of a pe keep the checksum and the certificate table, and where that table is. */
static BOOLEAN image_layout(IMAGE *m)
{
    UCHAR head[1024];
    if (m->Size < (LONG64)sizeof(head) || !NT_SUCCESS(read_at(m->File, 0, head, sizeof(head))) || head[0] != 'M' ||
        head[1] != 'Z')
        return FALSE;
    ULONG pe;
    memcpy(&pe, head + 0x3c, 4);
    if (pe > sizeof(head) - 0xa8 || memcmp(head + pe, "PE\0\0", 4))
        return FALSE;
    ULONG optional = pe + 24;
    USHORT magic;
    memcpy(&magic, head + optional, 2);
    ULONG directories = optional + (magic == 0x20b ? 112 : 96);
    if (directories + 5 * 8 > sizeof(head))
        return FALSE;
    m->ChecksumAt = optional + 64;
    m->DirectoryAt = directories + 4 * 8;
    memcpy(&m->SignatureAt, head + m->DirectoryAt, 4);
    memcpy(&m->SignatureSize, head + m->DirectoryAt + 4, 4);
    return TRUE;
}

/* the authenticode hash: the file but its checksum, its certificate entry and the table itself. */
static BOOLEAN image_hash(const IMAGE *m, const MimeHashAlg *alg, UCHAR *out)
{
    UCHAR *buffer = ExAllocatePool2(POOL_FLAG_PAGED, CHUNK, 'giSM');
    MimeHashCtx *ctx = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(MimeHashCtx), 'giSM');
    BOOLEAN ok = buffer && ctx;
    if (ok)
        alg->init(ctx);
    LONG64 ranges[4][2] = {{0, m->ChecksumAt},
                           {m->ChecksumAt + 4, m->DirectoryAt},
                           {m->DirectoryAt + 8, m->SignatureAt},
                           {(LONG64)m->SignatureAt + m->SignatureSize, m->Size}};
    for (ULONG r = 0; ok && r < 4; r++) {
        for (LONG64 at = ranges[r][0]; ok && at < ranges[r][1];) {
            ULONG n = ranges[r][1] - at < CHUNK ? (ULONG)(ranges[r][1] - at) : CHUNK;
            ok = NT_SUCCESS(read_at(m->File, at, buffer, n));
            if (ok)
                alg->update(ctx, buffer, n);
            at += n;
        }
    }
    if (ok)
        alg->final(ctx, out);
    if (buffer)
        ExFreePool(buffer);
    if (ctx)
        ExFreePool(ctx);
    return ok;
}

/* ------------------------------------------------------------------ the signature */

/*
 * the level an authenticode signature gives a file and the sha-256 of its signer's tbs certificate: the hash of the
 * image, the digest of the signed attributes, the signer's signature, the chain to a root of the machine.
 */
static UCHAR signature_level(IMAGE *m, UCHAR *thumbprint)
{
    if (!m->SignatureSize || m->SignatureSize > MAX_SIGNATURE || m->SignatureSize < 8 ||
        (LONG64)m->SignatureAt + m->SignatureSize > m->Size)
        return 0;
    UCHAR *table = ExAllocatePool2(POOL_FLAG_PAGED, m->SignatureSize, 'giSM');
    CERT *certs = ExAllocatePool2(POOL_FLAG_PAGED, 16 * sizeof(CERT), 'giSM');
    UCHAR level = 0;
    if (!table || !certs || !NT_SUCCESS(read_at(m->File, m->SignatureAt, table, m->SignatureSize)))
        goto done;
    ULONG length;
    USHORT revision, type;
    memcpy(&length, table, 4);
    memcpy(&revision, table + 4, 2);
    memcpy(&type, table + 6, 2);
    if (length > m->SignatureSize || length < 8 || type != 2 /* WIN_CERT_TYPE_PKCS_SIGNED_DATA */)
        goto done;
    DER d = {table + 8, length - 8}, x;
    TLV info, oid, explicit, sd, version, algorithms, content, certificates, signers = {}, t;
    if (!der_take(&d, 0x30, &info))
        goto done;
    x = der_of(&info);
    if (!der_take(&x, 0x06, &oid) || !der_take(&x, 0xa0, &explicit))
        goto done;
    x = der_of(&explicit);
    if (!der_take(&x, 0x30, &sd))
        goto done;
    x = der_of(&sd);
    if (!der_take(&x, 0x02, &version) || !der_take(&x, 0x31, &algorithms) || !der_take(&x, 0x30, &content))
        goto done;
    memset(&certificates, 0, sizeof(certificates));
    while (der_next(&x, &t)) {
        if (t.Tag == 0xa0)
            certificates = t;
        else if (t.Tag == 0x31)
            signers = t;
    }
    if (!certificates.Start || signers.Tag != 0x31)
        goto done;

    /* the indirect data: the hash of the image it was signed for. */
    DER c = der_of(&content);
    TLV ctype, wrapped, indirect, attribute, digest_info, digest_alg, digest;
    if (!der_take(&c, 0x06, &ctype) || !der_take(&c, 0xa0, &wrapped))
        goto done;
    c = der_of(&wrapped);
    if (!der_take(&c, 0x30, &indirect))
        goto done;
    c = der_of(&indirect);
    if (!der_take(&c, 0x30, &attribute) || !der_take(&c, 0x30, &digest_info))
        goto done;
    c = der_of(&digest_info);
    if (!der_take(&c, 0x30, &digest_alg) || !der_take(&c, 0x04, &digest))
        goto done;
    const MimeHashAlg *alg = hash_of(&digest_alg);
    UCHAR hash[MIME_HASH_MAX_DIGEST];
    if (!alg || digest.Length != alg->digest_size || !image_hash(m, alg, hash) ||
        memcmp(hash, digest.Content, digest.Length))
        goto done;

    /* the certificates. */
    ULONG count = 0;
    DER list = der_of(&certificates);
    while (count < 16 && der_next(&list, &t))
        if (t.Tag == 0x30 && cert_parse(t.Start, t.Total, &certs[count]))
            count++;

    /* the signer: its certificate, the digest of the signed attributes and their signature. */
    DER s = der_of(&signers);
    TLV signer, sv, issuer_serial, salg, attributes, ealg, signature;
    if (!der_take(&s, 0x30, &signer))
        goto done;
    s = der_of(&signer);
    if (!der_take(&s, 0x02, &sv) || !der_take(&s, 0x30, &issuer_serial) || !der_take(&s, 0x30, &salg) ||
        !der_take(&s, 0xa0, &attributes) || !der_take(&s, 0x30, &ealg) || !der_take(&s, 0x04, &signature))
        goto done;
    DER is = der_of(&issuer_serial);
    TLV issuer, serial;
    if (!der_take(&is, 0x30, &issuer) || !der_take(&is, 0x02, &serial))
        goto done;
    CERT *leaf = nullptr;
    for (ULONG i = 0; i < count && !leaf; i++)
        if (same(&certs[i].Issuer, &issuer) && same(&certs[i].Serial, &serial))
            leaf = &certs[i];
    const MimeHashAlg *salgo = hash_of(&salg);
    if (!leaf || !salgo)
        goto done;
    UCHAR content_hash[MIME_HASH_MAX_DIGEST];
    mime_hash(salgo, indirect.Content, indirect.Length, content_hash);
    BOOLEAN matched = FALSE;
    DER attrs = der_of(&attributes);
    TLV a;
    while (der_take(&attrs, 0x30, &a)) {
        DER ad = der_of(&a), values;
        TLV aoid, set, value;
        if (!der_take(&ad, 0x06, &aoid) || !oid_is(&aoid, OID_MESSAGE_DIGEST, sizeof(OID_MESSAGE_DIGEST)) ||
            !der_take(&ad, 0x31, &set))
            continue;
        values = der_of(&set);
        matched = der_take(&values, 0x04, &value) && value.Length == salgo->digest_size &&
                  !memcmp(value.Content, content_hash, value.Length);
    }
    if (!matched)
        goto done;
    /* the signed attributes are signed as a set, the tag their implicit context tag stands for. */
    UCHAR *set = ExAllocatePool2(POOL_FLAG_PAGED, attributes.Total, 'giSM');
    if (!set)
        goto done;
    memcpy(set, attributes.Start, attributes.Total);
    set[0] = 0x31;
    BOOLEAN good = verify(&leaf->KeyInfo, &ealg, set, attributes.Total, signature.Content, signature.Length, salgo);
    ExFreePool(set);
    if (!good)
        goto done;

    /* the chain within the signature, then the root of the machine above it. */
    CERT *top = leaf;
    for (ULONG depth = 0; depth < count; depth++) {
        CERT *up = nullptr;
        for (ULONG i = 0; i < count && !up; i++)
            if (&certs[i] != top && same(&certs[i].Subject, &top->Issuer) && !same(&certs[i].Subject, &certs[i].Issuer))
                up = &certs[i];
        if (!up)
            break;
        if (!signed_by(top, up))
            goto done;
        top = up;
    }
    level = root_level(top);
    if (level == LEVEL_MICROSOFT && has_usage(leaf, OID_WINDOWS_COMPONENT, sizeof(OID_WINDOWS_COMPONENT)))
        level = LEVEL_WINDOWS;
    if (level)
        mime_hash(&mime_sha256, leaf->Tbs.Start, leaf->Tbs.Total, thumbprint);
done:
    if (table)
        ExFreePool(table);
    if (certs)
        ExFreePool(certs);
    return level;
}

/* ------------------------------------------------------------------ the cache */

/* the identity of a file and the state its level was taken in. */
static NTSTATUS file_state(HANDLE file, SIGNING_ENTRY *e)
{
    IO_STATUS_BLOCK io;
    FILE_BASIC_INFORMATION basic;
    FILE_STANDARD_INFORMATION standard;
    LARGE_INTEGER id;
    NTSTATUS status = ZwQueryInformationFile(file, &io, &id, sizeof(id), FileInternalInformation);
    if (NT_SUCCESS(status))
        status = ZwQueryInformationFile(file, &io, &basic, sizeof(basic), FileBasicInformation);
    if (NT_SUCCESS(status))
        status = ZwQueryInformationFile(file, &io, &standard, sizeof(standard), FileStandardInformation);
    if (NT_SUCCESS(status)) {
        e->FileId = id;
        e->Created = basic.CreationTime;
        e->Changed = basic.ChangeTime;
        e->LastWrite = basic.LastWriteTime;
        e->Size = standard.EndOfFile;
    }
    return status;
}

static NTSTATUS file_handle(HANDLE handle, ACCESS_MASK *granted)
{
    PVOID object;
    NTSTATUS status = ObReferenceHandle(handle, IoFileObjectTypeObject, UserMode, &object, granted);
    if (NT_SUCCESS(status))
        ObDereferenceObject(object);
    return status;
}

void SeForgetSigningLevel(ULONG64 file_id)
{
    if (!__atomic_load_n(&g_entries, __ATOMIC_ACQUIRE))
        return;
    SIGNING_ENTRY *gone = nullptr;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_lock);
    for (SIGNING_ENTRY **link = &g_entries; *link;) {
        SIGNING_ENTRY *e = *link;
        if ((ULONG64)e->FileId.QuadPart == file_id) {
            *link = e->Next;
            e->Next = gone;
            gone = e;
        } else {
            link = &e->Next;
        }
    }
    KeReleaseSpinLock(&g_lock, irql);
    while (gone) {
        SIGNING_ENTRY *next = gone->Next;
        ExFreePool(gone);
        gone = next;
    }
}

static SIGNING_ENTRY *find_entry(const LARGE_INTEGER *id)
{
    for (SIGNING_ENTRY *e = g_entries; e; e = e->Next)
        if (e->FileId.QuadPart == id->QuadPart)
            return e;
    return nullptr;
}

/*
 * measured on windows 11 26300: a level is set only for the files given, with flag 4 whatever else is set; without it
 * the call is refused, flag 2 and an empty list are wrong parameters, a handle that may write the file is refused. a
 * file signed for microsoft gets level 8 and the sha-256 of the signer's tbs certificate as thumbprint, an unsigned one
 * level 2 and none.
 */
NTSTATUS NTAPI NtSetCachedSigningLevel(ULONG flags, UCHAR input_level, PHANDLE sources, ULONG count, HANDLE target)
{
    if (!count || !sources || (flags & 2))
        return STATUS_INVALID_PARAMETER;
    /* measured: without flag 4 it is refused, and the other flags are let through. */
    if (!(flags & SIGNING_FLAG_CACHE))
        return STATUS_ACCESS_DENIED;
    ACCESS_MASK granted = 0;
    NTSTATUS status = file_handle(target, &granted);
    if (!NT_SUCCESS(status))
        return status;
    if (granted & GENERIC_WRITE_)
        granted |= FILE_WRITE_DATA_;
    if (granted & (GENERIC_READ_ | GENERIC_ALL_))
        granted |= FILE_READ_DATA_;
    if (granted & (FILE_WRITE_DATA_ | FILE_APPEND_DATA_ | GENERIC_ALL_))
        return STATUS_WRITABLE_;
    if (!(granted & FILE_READ_DATA_))
        return STATUS_ACCESS_DENIED;
    SIGNING_ENTRY *e = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*e), 'giSM');
    if (!e)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(e, 0, sizeof(*e));
    e->AttributesWritable = (granted & FILE_WRITE_ATTRIBUTES_) != 0;
    PKTHREAD thread = KiCurrentThread();
    KPROCESSOR_MODE mode = thread->PreviousMode;
    thread->PreviousMode = KernelMode;
    status = file_state(target, e);
    if (NT_SUCCESS(status)) {
        IMAGE m = {target, e->Size.QuadPart};
        UCHAR level = image_layout(&m) ? signature_level(&m, e->Thumbprint) : 0;
        e->HasThumbprint = level != 0;
        e->Level = level ? level : LEVEL_UNSIGNED_CACHED;
    }
    thread->PreviousMode = mode;
    if (!NT_SUCCESS(status)) {
        ExFreePool(e);
        return status;
    }
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_lock);
    SIGNING_ENTRY *old = find_entry(&e->FileId);
    if (old) {
        old->Created = e->Created;
        old->Changed = e->Changed;
        old->LastWrite = e->LastWrite;
        old->AttributesWritable = e->AttributesWritable;
        old->Size = e->Size;
        old->Level = e->Level;
        old->HasThumbprint = e->HasThumbprint;
        memcpy(old->Thumbprint, e->Thumbprint, sizeof(e->Thumbprint));
    } else {
        e->Next = g_entries;
        g_entries = e;
        e = nullptr;
    }
    KeReleaseSpinLock(&g_lock, irql);
    if (e)
        ExFreePool(e);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtGetCachedSigningLevel(HANDLE file, PULONG flags, PUCHAR level, PUCHAR thumbprint,
                                       PULONG thumbprint_size, PULONG algorithm)
{
    ACCESS_MASK granted;
    NTSTATUS status = file_handle(file, &granted);
    if (!NT_SUCCESS(status))
        return status;
    SIGNING_ENTRY now = {};
    PKTHREAD thread = KiCurrentThread();
    KPROCESSOR_MODE mode = thread->PreviousMode;
    thread->PreviousMode = KernelMode;
    status = file_state(file, &now);
    thread->PreviousMode = mode;
    if (!NT_SUCCESS(status))
        return status;
    SIGNING_ENTRY found = {};
    BOOLEAN valid = FALSE;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_lock);
    SIGNING_ENTRY *e = find_entry(&now.FileId);
    /* a file written since its level was taken has none. */
    if (e && e->Created.QuadPart == now.Created.QuadPart && e->Changed.QuadPart == now.Changed.QuadPart &&
        e->LastWrite.QuadPart == now.LastWrite.QuadPart &&
        e->Size.QuadPart == now.Size.QuadPart) {
        found = *e;
        valid = TRUE;
    }
    KeReleaseSpinLock(&g_lock, irql);
    if (!valid)
        return STATUS_NOT_FOUND;
    ULONG room = 0, value = CACHE_FLAGS | (!found.HasThumbprint && found.AttributesWritable ? 0x4000 : 0);
    if (thumbprint_size && !NT_SUCCESS(KiCopyFromUser(&room, thumbprint_size, sizeof(room))))
        return STATUS_ACCESS_VIOLATION;
    ULONG size = found.HasThumbprint ? sizeof(found.Thumbprint) : 0, alg = found.HasThumbprint ? CALG_SHA_256_ : 0;
    if (size && thumbprint_size && room < size) {
        KiCopyToUser(thumbprint_size, &size, sizeof(size));
        return STATUS_BUFFER_TOO_SMALL;
    }
    if ((flags && !NT_SUCCESS(KiCopyToUser(flags, &value, sizeof(value)))) ||
        (level && !NT_SUCCESS(KiCopyToUser(level, &found.Level, sizeof(found.Level)))) ||
        (thumbprint_size && !NT_SUCCESS(KiCopyToUser(thumbprint_size, &size, sizeof(size)))) ||
        (algorithm && !NT_SUCCESS(KiCopyToUser(algorithm, &alg, sizeof(alg)))) ||
        (size && thumbprint && !NT_SUCCESS(KiCopyToUser(thumbprint, found.Thumbprint, size))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}
