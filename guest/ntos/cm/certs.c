/*
 * the host's trusted roots. its distribution's certificate bundle, read through
 * the z: drive at each boot, becomes the machine's AuthRoot store, where windows
 * keeps the roots automatic root update brings:
 * SOFTWARE\Microsoft\SystemCertificates\AuthRoot\Certificates\<sha-1>, whose Blob
 * value holds the hash and certificate as serialized store elements. the keys are
 * made before the journal starts, so they are never saved and every boot sees the
 * bundle as it is now. roots the default hive or the journal already hold are left alone.
 */
#include "cm.h"
#include <crypto/crypto.h>

#define AUTH_ROOT L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\SystemCertificates\\AuthRoot\\Certificates"
#define MAX_BUNDLE (8u << 20)
#define CERT_SHA1_HASH_PROP_ID 3
#define CERT_CERT_PROP_ID 32
#define X509_ASN_ENCODING 1

/* first match wins. */
static const PCWSTR bundles[] = {
    L"\\??\\C:\\ProgramData\\Mime\\roots.pem",                          /* macos, its keychain's roots. */
    L"\\??\\Z:\\etc\\ssl\\certs\\ca-certificates.crt",                 /* debian, ubuntu, arch, gentoo. */
    L"\\??\\Z:\\etc\\pki\\tls\\certs\\ca-bundle.crt",                  /* fedora, rhel. */
    L"\\??\\Z:\\etc\\pki\\ca-trust\\extracted\\pem\\tls-ca-bundle.pem", /* fedora without the link. */
    L"\\??\\Z:\\etc\\ssl\\ca-bundle.pem",                             /* opensuse. */
    L"\\??\\Z:\\etc\\ssl\\cert.pem",                                  /* alpine, macos without the export. */
};

static PUCHAR read_bundle(PULONG size)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(bundles); i++) {
        UNICODE_STRING path;
        RtlInitUnicodeString(&path, bundles[i]);
        PFILE_OBJECT file;
        if (!NT_SUCCESS(IopOpenFileObject(&path, FILE_READ_DATA | SYNCHRONIZE, FILE_NON_DIRECTORY_FILE, &file)))
            continue;
        ULONG64 length = 0;
        PUCHAR data = nullptr;
        if (NT_SUCCESS(IoQueryFileSize(file, &length)) && length && length <= MAX_BUNDLE)
            data = ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)length, KI_TAG('C', 'm', 'C', 'a'));
        ULONG done = 0;
        while (data && done < length) {
            ULONG got = 0;
            if (!NT_SUCCESS(IoReadFileObject(file, data + done, (ULONG)length - done, done, &got)) || !got)
                break;
            done += got;
        }
        ObDereferenceObject(file);
        if (data && done == length) {
            *size = done;
            return data;
        }
        if (data)
            ExFreePool(data);
    }
    return nullptr;
}

static int base64_value(UCHAR c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

/* skips white space, returns the bytes written. */
static ULONG base64_decode(const UCHAR *p, const UCHAR *end, PUCHAR out)
{
    ULONG n = 0, bits = 0, acc = 0;
    for (; p < end && *p != '='; p++) {
        int v = base64_value(*p);
        if (v < 0)
            continue;
        acc = acc << 6 | (ULONG)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[n++] = (UCHAR)(acc >> bits);
        }
    }
    return n;
}

/* the size of the der sequence at the start of data, 0 if it is not one or does not fit. */
static ULONG der_size(const UCHAR *data, ULONG size)
{
    if (size < 2 || data[0] != 0x30)
        return 0;
    ULONG length = data[1], header = 2;
    if (length & 0x80) {
        ULONG count = length & 0x7F;
        if (!count || count > 3 || size < 2 + count)
            return 0;
        length = 0;
        for (ULONG i = 0; i < count; i++)
            length = length << 8 | data[2 + i];
        header += count;
    }
    return header + length <= size ? header + length : 0;
}

static void put_element(PUCHAR out, ULONG id, ULONG length)
{
    ULONG fields[3] = {id, X509_ASN_ENCODING, length};
    memcpy(out, fields, sizeof(fields));
}

/* skipped when its key already holds a certificate. lock held. */
static void add_root(const UCHAR *der, ULONG size)
{
    UCHAR hash[20];
    mime_hash(&mime_sha1, der, size, hash);
    static const WCHAR hex[] = L"0123456789ABCDEF";
    WCHAR path[RTL_NUMBER_OF(AUTH_ROOT) + 48] = AUTH_ROOT L"\\";
    PWCHAR name = path + RTL_NUMBER_OF(AUTH_ROOT);
    for (ULONG i = 0; i < 20; i++) {
        name[2 * i] = hex[hash[i] >> 4];
        name[2 * i + 1] = hex[hash[i] & 15];
    }
    name[40] = 0;
    static const WCHAR blob_name[] = L"Blob";
    CM_NODE *node = CmpOpenPath(path, FALSE);
    if (node && NT_SUCCESS(CmpLoadValues(node)) && CmpFindValue(node, blob_name, RTL_NUMBER_OF(blob_name) - 1))
        return;
    node = node ? node : CmpOpenPath(path, TRUE);
    ULONG length = 12 + sizeof(hash) + 12 + size;
    PUCHAR blob = ExAllocatePool2(POOL_FLAG_PAGED, length, KI_TAG('C', 'm', 'C', 'a'));
    if (!node || !blob) {
        if (blob)
            ExFreePool(blob);
        return;
    }
    put_element(blob, CERT_SHA1_HASH_PROP_ID, sizeof(hash));
    memcpy(blob + 12, hash, sizeof(hash));
    put_element(blob + 12 + sizeof(hash), CERT_CERT_PROP_ID, size);
    memcpy(blob + 24 + sizeof(hash), der, size);
    CmpSetValue(node, blob_name, RTL_NUMBER_OF(blob_name) - 1, REG_BINARY, blob, length);
    ExFreePool(blob);
}

static const UCHAR *find(const UCHAR *p, const UCHAR *end, const char *text)
{
    SIZE_T n = strlen(text);
    for (; p + n <= end; p++)
        if (!memcmp(p, text, n))
            return p;
    return nullptr;
}

void CmpImportHostRoots(void)
{
    ULONG size;
    PUCHAR bundle = read_bundle(&size);
    if (!bundle)
        return;
    static const char begin[] = "-----BEGIN CERTIFICATE-----", end_line[] = "-----END CERTIFICATE-----";
    const UCHAR *p = bundle, *end = bundle + size;
    PUCHAR der = ExAllocatePool2(POOL_FLAG_PAGED, 64 * 1024, KI_TAG('C', 'm', 'C', 'a'));
    ExAcquireFastMutex(&CmpLock);
    while (der && (p = find(p, end, begin))) {
        const UCHAR *body = p + sizeof(begin) - 1, *stop = find(body, end, end_line);
        if (!stop)
            break;
        /* base64 decodes to at most three quarters of its text, which must fit the buffer. */
        if ((ULONG64)(stop - body) * 3 / 4 <= 64 * 1024) {
            ULONG n = base64_decode(body, stop, der);
            ULONG cert = der_size(der, n);
            if (cert)
                add_root(der, cert);
        }
        p = stop + sizeof(end_line) - 1;
    }
    CmpUnlock();
    if (der)
        ExFreePool(der);
    ExFreePool(bundle);
}
