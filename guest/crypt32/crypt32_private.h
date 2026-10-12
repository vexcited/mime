/*
 * crypt32 internal interface shared by its source files.
 */
#ifndef MIME_CRYPT32_PRIVATE_H
#define MIME_CRYPT32_PRIVATE_H

#define MIME_BUILD_CRYPT32
#include <mime/win32/wincrypt.h>
#include <mime/win32/winnls.h>
#include <stdbool.h>

#ifndef MAXDWORD
#define MAXDWORD 0xFFFFFFFF
#endif

FORCEINLINE void *CrAlloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, 0, size);
}

FORCEINLINE void *CrAllocZero(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void CrFree(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, 0, p);
}

FORCEINLINE BOOL CrFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

/* ------------------------------------------------------------------ der reading (asn1.c) */

#define ASN_BOOLEAN 0x01
#define ASN_INTEGER 0x02
#define ASN_BITSTRING 0x03
#define ASN_OCTETSTRING 0x04
#define ASN_NULL 0x05
#define ASN_OID 0x06
#define ASN_ENUMERATED 0x0A
#define ASN_UTF8STRING 0x0C
#define ASN_NUMERICSTRING 0x12
#define ASN_PRINTABLESTRING 0x13
#define ASN_T61STRING 0x14
#define ASN_VIDEOTEXSTRING 0x15
#define ASN_IA5STRING 0x16
#define ASN_UTCTIME 0x17
#define ASN_GENERALTIME 0x18
#define ASN_GRAPHICSTRING 0x19
#define ASN_VISIBLESTRING 0x1A
#define ASN_GENERALSTRING 0x1B
#define ASN_UNIVERSALSTRING 0x1C
#define ASN_BMPSTRING 0x1E
#define ASN_SEQUENCE 0x30
#define ASN_SET 0x31
#define ASN_CONTEXT(n) (0x80 | (n))
#define ASN_CONSTRUCTED_CONTEXT(n) (0xA0 | (n))

/* one tag length value element. */
typedef struct {
    BYTE tag;
    const BYTE *content;
    DWORD length;
    const BYTE *start;
    DWORD total;
} der_item;

/* the unread part of a constructed value. */
typedef struct {
    const BYTE *p;
    DWORD n;
} der_reader;

DWORD DerRead(der_reader *r, der_item *item);
DWORD DerExpect(der_reader *r, BYTE tag, der_item *item);
BOOL DerPeek(const der_reader *r, BYTE tag);
FORCEINLINE void DerOpen(der_reader *r, const der_item *item)
{
    r->p = item->content;
    r->n = item->length;
}
DWORD DerCount(const der_item *item);
DWORD DerParseOid(const der_item *item, char *out, DWORD size);
DWORD DerParseTime(const der_item *item, FILETIME *ft);
DWORD DerParseInt(const der_item *item, int *value);

/* ------------------------------------------------------------------ der writing (asn1.c) */

typedef struct {
    BYTE *data;
    DWORD size;
    DWORD capacity;
    DWORD error;
} der_writer;

void DerWriteInit(der_writer *w);
void DerWriteFree(der_writer *w);
void DerWriteBytes(der_writer *w, const void *data, DWORD size);
/* starts a tag; returns the mark to pass to DerEnd once the content is written. */
DWORD DerBegin(der_writer *w, BYTE tag);
void DerEnd(der_writer *w, DWORD mark);
void DerWriteTlv(der_writer *w, BYTE tag, const void *data, DWORD size);
void DerWriteOid(der_writer *w, LPCSTR oid);
void DerWriteIntLe(der_writer *w, const BYTE *le, DWORD size, BOOL unsigned_value);
void DerWriteInt(der_writer *w, int value);
void DerWriteTime(der_writer *w, const FILETIME *ft);

/* ------------------------------------------------------------------ decoded structure layout (decode.c) */

typedef struct {
    BYTE *base;
    SIZE_T used;
} cr_arena;

void *ArenaAlloc(cr_arena *a, SIZE_T size);
void *ArenaCopy(cr_arena *a, const void *data, SIZE_T size);
LPSTR ArenaString(cr_arena *a, LPCSTR s);

/* decodes a structure into newly allocated heap memory; free it with CrFree. */
void *CrDecodeAlloc(LPCSTR type, const BYTE *data, DWORD size, DWORD flags, DWORD *out_size);
/* encodes into newly allocated heap memory; free it with CrFree. */
BYTE *CrEncodeAlloc(LPCSTR type, const void *info, DWORD flags, DWORD *out_size);
DWORD CrDecodeNameValueString(DWORD value_type, const BYTE *data, DWORD size, WCHAR *out, DWORD capacity);
DWORD CrStringTypeFromTag(BYTE tag);
BYTE CrTagFromStringType(DWORD type);
BOOL CrIsPrintable(const WCHAR *s, DWORD n);

/* ------------------------------------------------------------------ hashes (hash.c) */

#define CR_MAX_HASH 64

DWORD CrHashSize(ALG_ID alg);
BOOL CrHash(ALG_ID alg, const BYTE *data, DWORD size, BYTE *out);
ALG_ID CrHashFromOid(LPCSTR oid);

/* ------------------------------------------------------------------ signatures (rsa.c) */

/* verifies the signature of an encoded signed blob; returns an error code or 0. */
DWORD CrVerifySignedBlob(const BYTE *encoded, DWORD size, const CERT_PUBLIC_KEY_INFO *key);
DWORD CrRsaModulusBits(const CERT_PUBLIC_KEY_INFO *key);

/* ------------------------------------------------------------------ oids (oid.c) */

PCCRYPT_OID_INFO CrFindOid(LPCSTR oid, DWORD group);
PCCRYPT_OID_INFO CrFindOidByName(LPCWSTR name, DWORD group);

/* ------------------------------------------------------------------ names (name.c) */

DWORD CrNameToStr(const CERT_NAME_BLOB *name, DWORD str_type, WCHAR *out, DWORD capacity);
DWORD CrGetNameString(PCCERT_CONTEXT cert, DWORD type, DWORD flags, void *para, WCHAR *out, DWORD capacity);

/* ------------------------------------------------------------------ certificates and stores (cert.c, store.c) */

typedef struct cr_prop {
    struct cr_prop *next;
    DWORD id;
    DWORD size;
    BYTE data[];
} cr_prop;

typedef struct cr_store cr_store;

typedef struct cr_cert {
    CERT_CONTEXT context;
    LONG refs;
    cr_store *store;
    struct cr_cert *next;
    struct cr_cert *prev;
    BOOL deleted;
    cr_prop *props;
} cr_cert;

#define CR_STORE_MAGIC 0x53524543

struct cr_store {
    DWORD magic;
    LONG refs;
    LONG opens;
    DWORD flags;
    BOOL system;
    BOOL collection;
    cr_cert *head;
    cr_cert *tail;
    cr_store **children;
    DWORD *child_flags;
    DWORD *child_priorities;
    DWORD child_count;
    WCHAR *localized_name;
};

void CrInitLock(void);
void CrLock(void);
void CrUnlock(void);
cr_cert *CrCert(PCCERT_CONTEXT context);
cr_store *CrStore(HCERTSTORE store);
cr_cert *CrCreateCert(const BYTE *encoded, DWORD size);
void CrReleaseCert(cr_cert *cert);
void CrReleaseStore(cr_store *store);
BOOL CrCopyProps(cr_cert *dst, const cr_cert *src, BOOL keep_existing);
void CrFreeProps(cr_cert *cert);
BOOL CrCertHash(PCCERT_CONTEXT cert, DWORD prop, BYTE *out, DWORD *size);
BOOL CrSameCert(PCCERT_CONTEXT a, PCCERT_CONTEXT b);
BOOL CrHasPrivateKey(PCCERT_CONTEXT cert);
HCERTSTORE CrOpenMemoryStore(DWORD flags);
BOOL CrAddEncoded(HCERTSTORE store, const BYTE *data, DWORD size);
BOOL CrLoadSerialized(HCERTSTORE store, const BYTE *data, DWORD size);
BOOL CrLoadPkcs7(HCERTSTORE store, const BYTE *data, DWORD size);

/* ------------------------------------------------------------------ chains (chain.c) */

BOOL CrIsSelfIssued(PCCERT_CONTEXT cert);
BOOL CrUsageAllowed(PCCERT_CONTEXT cert, const CERT_USAGE_MATCH *usage);

#endif
