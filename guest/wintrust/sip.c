#include "trust_private.h"

/*
 * the subject interface package of portable executables, which crypt32's CryptSIPLoad finds through the registry under
 * Cryptography\OID\EncodingType 0\CryptSIPDll* and calls here. measured on windows 11 26300.
 */
typedef struct {
    DWORD cbSize;
    GUID *pgSubjectType;
    HANDLE hFile;
    LPCWSTR pwsFileName;
    LPCWSTR pwsDisplayName;
    DWORD dwReserved1, dwIntVersion;
    HCRYPTPROV hProv;
    CRYPT_ALGORITHM_IDENTIFIER DigestAlgorithm;
    DWORD dwFlags, dwEncodingType, dwReserved2, fdwCAPISettings, fdwSecuritySettings, dwIndex, dwUnionChoice;
    PVOID psFlat;
    LPVOID pClientData;
} SIP_SUBJECTINFO;

typedef struct {
    struct {
        LPSTR pszObjId;
        CRYPT_OBJID_BLOB Value;
    } Data;
    CRYPT_ALGORITHM_IDENTIFIER DigestAlgorithm;
    CRYPT_HASH_BLOB Digest;
} SIP_INDIRECT_DATA;

typedef struct {
    DWORD cbSize, dwVersion;
    BOOL isMultiSign;
    DWORD dwFlags;
} SIP_CAP_SET;

#define SIP_CAP_FLAG_SEALING 1

typedef struct {
    DWORD dwLength;
    WORD wRevision, wCertificateType;
    BYTE bCertificate[];
} SIP_CERTIFICATE;

#define WIN_CERT_REVISION_2_0 0x0200
#define WIN_CERT_TYPE_PKCS_SIGNED_DATA 0x0002

DECLSPEC_IMPORT BOOL WINAPI ImageAddCertificate(HANDLE file, SIP_CERTIFICATE *certificate, PDWORD index);
DECLSPEC_IMPORT BOOL WINAPI ImageRemoveCertificate(HANDLE file, DWORD index);

static const GUID g_pe = {0xc689aab8, 0x8e78, 0x11d0, {0x8c, 0x47, 0x00, 0xc0, 0x4f, 0xc2, 0x95, 0xee}};
static const char g_pe_image_data[] = "1.3.6.1.4.1.311.2.1.15";
/* an SpcPeImageData with no flags and a link to an empty file name. */
static const BYTE g_image_data[] = {0x30, 0x09, 0x03, 0x01, 0x00, 0xa0, 0x04, 0xa2, 0x02, 0x80, 0x00};

typedef struct {
    HANDLE file, mapping;
    BOOL opened;
    const BYTE *head;
    WT_SUBJECT subject;
    DWORD table_size;
} SIP_IMAGE;

static WORD word(const BYTE *p)
{
    return p[0] | (WORD)p[1] << 8;
}

static DWORD dword(const BYTE *p)
{
    return word(p) | (DWORD)word(p + 2) << 16;
}

static void image_close(SIP_IMAGE *image)
{
    if (image->head)
        UnmapViewOfFile(image->head);
    if (image->mapping)
        CloseHandle(image->mapping);
    if (image->opened)
        CloseHandle(image->file);
}

/* the caller's handle, or the file it names, with where the fields a signature leaves out are. */
static BOOL image_open(const SIP_SUBJECTINFO *info, SIP_IMAGE *image)
{
    memset(image, 0, sizeof(*image));
    if (!info || !info->pgSubjectType || memcmp(info->pgSubjectType, &g_pe, sizeof(g_pe))) {
        SetLastError(TRUST_E_SUBJECT_FORM_UNKNOWN);
        return FALSE;
    }
    image->file = info->hFile;
    if (!image->file || image->file == INVALID_HANDLE_VALUE) {
        image->file = CreateFileW(info->pwsFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (image->file == INVALID_HANDLE_VALUE)
            return FALSE;
        image->opened = TRUE;
    }
    LARGE_INTEGER size;
    DWORD error = TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (!GetFileSizeEx(image->file, &size) || size.QuadPart > 0x7fffffff || size.QuadPart < 64)
        goto fail;
    image->mapping = CreateFileMappingW(image->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    DWORD head = size.LowPart < 0x100000 ? size.LowPart : 0x100000;
    image->head = image->mapping ? MapViewOfFile(image->mapping, FILE_MAP_READ, 0, 0, head) : nullptr;
    if (!image->head) {
        error = GetLastError();
        goto fail;
    }
    const BYTE *bytes = image->head;
    if (word(bytes) != 0x5a4d)
        goto fail;
    DWORD nt = dword(bytes + 60);
    if (nt > head - 24 || dword(bytes + nt) != 0x4550)
        goto fail;
    DWORD optional = nt + 24, optional_size = word(bytes + nt + 20);
    if (optional_size > head - optional || optional_size < 96)
        goto fail;
    WORD magic = word(bytes + optional);
    DWORD directories = magic == 0x10b ? 96 : magic == 0x20b ? 112 : 0;
    if (!directories || optional_size < directories + 40 || dword(bytes + optional + directories - 4) <= 4)
        goto fail;
    WT_SUBJECT *s = &image->subject;
    s->mapping = image->mapping;
    s->image = bytes;
    s->size = size.LowPart;
    s->checksum = optional + 64;
    s->directory = optional + directories + 32;
    s->certificate = dword(bytes + s->directory);
    image->table_size = dword(bytes + s->directory + 4);
    if (!s->certificate || !image->table_size) {
        s->certificate = s->size;
        image->table_size = 0;
    } else if (s->certificate > s->size || image->table_size > s->size - s->certificate) {
        goto fail;
    }
    return TRUE;
fail:
    image_close(image);
    SetLastError(error);
    return FALSE;
}

static ALG_ID digest_algorithm(const char *oid)
{
    if (!oid)
        return 0;
    if (!strcmp(oid, szOID_NIST_sha256))
        return CALG_SHA_256;
    if (!strcmp(oid, szOID_OIWSEC_sha1))
        return CALG_SHA1;
    if (!strcmp(oid, szOID_NIST_sha384))
        return CALG_SHA_384;
    if (!strcmp(oid, szOID_NIST_sha512))
        return CALG_SHA_512;
    if (!strcmp(oid, szOID_RSA_MD5))
        return CALG_MD5;
    return 0;
}

/* the record of the table with this index, where it starts in the file. */
static BOOL find_record(const SIP_IMAGE *image, DWORD index, DWORD *offset, DWORD *length)
{
    DWORD at = 0;
    for (DWORD i = 0; at + 8 <= image->table_size; i++) {
        BYTE header[8];
        DWORD got;
        LARGE_INTEGER where = {.QuadPart = (LONGLONG)image->subject.certificate + at};
        OVERLAPPED position = {.Offset = where.LowPart, .OffsetHigh = (DWORD)where.HighPart};
        if (!ReadFile(image->file, header, sizeof(header), &got, &position) || got != sizeof(header))
            return FALSE;
        DWORD size = dword(header);
        if (size < 8 || size > image->table_size - at)
            return FALSE;
        if (i == index) {
            if (word(header + 6) != WIN_CERT_TYPE_PKCS_SIGNED_DATA)
                return FALSE;
            *offset = image->subject.certificate + at;
            *length = size;
            return TRUE;
        }
        at += (size + 7) & ~7u;
    }
    return FALSE;
}

/*
 * the signed data of a certificate record. without a buffer the size told is the record's with its eight bytes of
 * header, which is also the room a buffer must have, and the size of what was copied is the one without them.
 */
WINTRUSTAPI BOOL WINAPI CryptSIPGetSignedDataMsg(SIP_SUBJECTINFO *info, DWORD *encoding, DWORD index, DWORD *size, BYTE *data)
{
    SIP_IMAGE image;
    if (!size || !image_open(info, &image))
        return FALSE;
    DWORD offset, length;
    if (!find_record(&image, index, &offset, &length)) {
        image_close(&image);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = TRUE;
    DWORD error = 0;
    if (!data) {
        *size = length;
    } else if (*size < length) {
        *size = length;
        error = ERROR_INSUFFICIENT_BUFFER;
        ok = FALSE;
    } else {
        DWORD got;
        OVERLAPPED position = {.Offset = offset + 8};
        ok = ReadFile(image.file, data, length - 8, &got, &position) && got == length - 8;
        if (ok) {
            *size = length - 8;
            if (encoding)
                *encoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
        } else {
            error = GetLastError();
        }
    }
    image_close(&image);
    if (!ok)
        SetLastError(error);
    return ok;
}

/*
 * what a signature of the file signs: the kind of the data, the algorithm and the digest of the image, in one block the
 * pointers of which point after the structure. a block too small fails with no error told and no size.
 */
WINTRUSTAPI BOOL WINAPI CryptSIPCreateIndirectData(SIP_SUBJECTINFO *info, DWORD *size, SIP_INDIRECT_DATA *out)
{
    SIP_IMAGE image;
    if (!size || !image_open(info, &image))
        return FALSE;
    const char *oid = info->DigestAlgorithm.pszObjId;
    BYTE hash[64];
    DWORD digest = WtImageDigest(digest_algorithm(oid), &image.subject, hash);
    image_close(&image);
    if (!digest) {
        SetLastError((DWORD)NTE_BAD_ALGID);
        return FALSE;
    }
    DWORD oid_size = (DWORD)strlen(oid) + 1;
    DWORD need = sizeof(*out) + sizeof(g_pe_image_data) + sizeof(g_image_data) + oid_size + digest;
    if (!out) {
        *size = need;
        return TRUE;
    }
    if (*size < need)
        return FALSE;
    BYTE *at = (BYTE *)(out + 1);
    memset(out, 0, sizeof(*out));
    out->Data.pszObjId = (LPSTR)at;
    memcpy(at, g_pe_image_data, sizeof(g_pe_image_data));
    at += sizeof(g_pe_image_data);
    out->Data.Value.cbData = sizeof(g_image_data);
    out->Data.Value.pbData = at;
    memcpy(at, g_image_data, sizeof(g_image_data));
    at += sizeof(g_image_data);
    out->DigestAlgorithm.pszObjId = (LPSTR)at;
    memcpy(at, oid, oid_size);
    at += oid_size;
    out->Digest.cbData = digest;
    out->Digest.pbData = at;
    memcpy(at, hash, digest);
    return TRUE;
}

WINTRUSTAPI BOOL WINAPI CryptSIPVerifyIndirectData(SIP_SUBJECTINFO *info, SIP_INDIRECT_DATA *data)
{
    SIP_IMAGE image;
    if (!data || !image_open(info, &image))
        return FALSE;
    BYTE hash[64];
    DWORD digest = WtImageDigest(digest_algorithm(data->DigestAlgorithm.pszObjId), &image.subject, hash);
    image_close(&image);
    if (!digest || digest != data->Digest.cbData || memcmp(hash, data->Digest.pbData, digest)) {
        SetLastError((DWORD)TRUST_E_BAD_DIGEST);
        return FALSE;
    }
    return TRUE;
}

/* the handle to change the file through: the caller's, or the file it names opened for writing. */
static HANDLE writable(const SIP_SUBJECTINFO *info, BOOL *opened)
{
    *opened = FALSE;
    if (!info || !info->pgSubjectType || memcmp(info->pgSubjectType, &g_pe, sizeof(g_pe))) {
        SetLastError(TRUST_E_SUBJECT_FORM_UNKNOWN);
        return INVALID_HANDLE_VALUE;
    }
    if (info->hFile && info->hFile != INVALID_HANDLE_VALUE)
        return info->hFile;
    HANDLE file = CreateFileW(info->pwsFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    *opened = file != INVALID_HANDLE_VALUE;
    return file;
}

WINTRUSTAPI BOOL WINAPI CryptSIPPutSignedDataMsg(SIP_SUBJECTINFO *info, DWORD encoding, DWORD *index, DWORD size, BYTE *data)
{
    BOOL opened;
    HANDLE file = writable(info, &opened);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL ok = FALSE;
    SIP_CERTIFICATE *record = data && size <= 0x1000000 ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 8 + size) : nullptr;
    if (record) {
        record->dwLength = 8 + size;
        record->wRevision = WIN_CERT_REVISION_2_0;
        record->wCertificateType = WIN_CERT_TYPE_PKCS_SIGNED_DATA;
        memcpy(record->bCertificate, data, size);
        ok = ImageAddCertificate(file, record, index);
        HeapFree(GetProcessHeap(), 0, record);
    } else {
        SetLastError(ERROR_INVALID_PARAMETER);
    }
    if (opened)
        CloseHandle(file);
    return ok;
}

WINTRUSTAPI BOOL WINAPI CryptSIPRemoveSignedDataMsg(SIP_SUBJECTINFO *info, DWORD index)
{
    BOOL opened;
    HANDLE file = writable(info, &opened);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL ok = ImageRemoveCertificate(file, index);
    if (opened)
        CloseHandle(file);
    return ok;
}

WINTRUSTAPI BOOL WINAPI CryptSIPGetCaps(SIP_SUBJECTINFO *info, SIP_CAP_SET *caps)
{
    if (!info || !caps || caps->cbSize < sizeof(*caps)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    caps->dwVersion = 3;
    caps->isMultiSign = TRUE;
    caps->dwFlags = SIP_CAP_FLAG_SEALING;
    return TRUE;
}
