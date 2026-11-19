/*
 * \Device\KsecDD and \Device\CNG, the kernel security support and cryptography devices. genuine bcryptprimitives seeds
 * from them, cryptbase's RtlEncryptMemory keys its blocks there, and genuine bcrypt asks KsecDD for the cng
 * configuration: which provider serves an algorithm and from which image. bcrypt cannot load when its first request,
 * the configuration change notification, fails. what each request answers was measured on windows 11, see
 * scratch/dlls/gaps/ksec of the research repository.
 */
#include "../ntos.h"
#include <crypto/crypto.h>

#define IOCTL_KSEC_RNG 0x390004
#define IOCTL_KSEC_RNG_REKEY 0x390008
#define IOCTL_KSEC_ENCRYPT_MEMORY 0x39000E
#define IOCTL_KSEC_DECRYPT_MEMORY 0x390012
#define IOCTL_KSEC_ENCRYPT_MEMORY_CROSS_PROC 0x390016
#define IOCTL_KSEC_DECRYPT_MEMORY_CROSS_PROC 0x39001A
#define IOCTL_KSEC_ENCRYPT_MEMORY_SAME_LOGON 0x39001E
#define IOCTL_KSEC_DECRYPT_MEMORY_SAME_LOGON 0x390022
#define IOCTL_CNG_CONFIGURATION 0x390400

#define FILE_DEVICE_KSEC 0x39
#define CNG_MAGIC 0x1A2B3C4D
#define STATUS_ILLEGAL_FUNCTION ((NTSTATUS)0xC00000AFL)

/* ------------------------------------------------------------ random bytes */

static KSPIN_LOCK g_rng_lock;
static UCHAR g_rng_key[32];
static ULONG64 g_rng_counter;
static BOOLEAN g_rng_seeded;

static BOOLEAN have_rdrand(void)
{
    int regs[4];
    __asm__ volatile("cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(1), "c"(0));
    return (regs[2] >> 30) & 1;
}

static ULONG64 rdrand(void)
{
    ULONG64 value = 0;
    UCHAR ok = 0;
    for (int i = 0; i < 10 && !ok; i++)
        __asm__ volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(ok));
    return value;
}

/* what the guest can draw on: the cpu's generator when it has one, and the timing of the counter. */
static void entropy(MimeHashCtx *h)
{
    ULONG64 sample[8];
    BOOLEAN hw = have_rdrand();
    for (int i = 0; i < 8; i++)
        sample[i] = HalReadCounter() ^ (hw ? rdrand() : 0);
    mime_sha256.update(h, sample, sizeof(sample));
}

/* sha-256 in counter mode under a key drawn at the first request and stirred with fresh entropy at each one. */
static void random_bytes(PUCHAR out, ULONG length)
{
    MimeHashCtx h;
    KIRQL irql;
    KeAcquireSpinLock(&g_rng_lock, &irql);
    mime_sha256.init(&h);
    if (g_rng_seeded)
        mime_sha256.update(&h, g_rng_key, sizeof(g_rng_key));
    entropy(&h);
    mime_sha256.final(&h, g_rng_key);
    g_rng_seeded = TRUE;
    UCHAR key[32];
    memcpy(key, g_rng_key, sizeof(key));
    ULONG64 counter = g_rng_counter;
    g_rng_counter += (length + 31) / 32 + 1;
    KeReleaseSpinLock(&g_rng_lock, irql);
    while (length) {
        UCHAR block[32];
        mime_sha256.init(&h);
        mime_sha256.update(&h, key, sizeof(key));
        mime_sha256.update(&h, &counter, sizeof(counter));
        mime_sha256.final(&h, block);
        counter++;
        ULONG n = length < sizeof(block) ? length : sizeof(block);
        memcpy(out, block, n);
        out += n;
        length -= n;
    }
}

/* ------------------------------------------------------------ RtlEncryptMemory */

static UCHAR g_memory_key[32];

/*
 * blocks of eight bytes, the size RtlEncryptMemory asks for, through a keyed feistel network chained as cbc. the key
 * belongs to the process, to the logon session or to the boot, as the option asks, so only that scope can decrypt.
 */
static void scope_key(ULONG scope, PEPROCESS process, UCHAR key[32])
{
    MimeHashCtx h;
    mime_sha256.init(&h);
    mime_sha256.update(&h, g_memory_key, sizeof(g_memory_key));
    mime_sha256.update(&h, &scope, sizeof(scope));
    if (scope == 0) {
        ULONG64 id[2] = {(ULONG64)(ULONG_PTR)process->UniqueProcessId, process->CreateTime};
        mime_sha256.update(&h, id, sizeof(id));
    } else if (scope == 2) {
        ULONG64 logon = SepTokenAuthenticationId(process);
        mime_sha256.update(&h, &logon, sizeof(logon));
    }
    mime_sha256.final(&h, key);
}

static ULONG round_function(const UCHAR key[32], ULONG round, ULONG half)
{
    MimeHashCtx h;
    UCHAR digest[32];
    mime_sha256.init(&h);
    mime_sha256.update(&h, key, 32);
    mime_sha256.update(&h, &round, sizeof(round));
    mime_sha256.update(&h, &half, sizeof(half));
    mime_sha256.final(&h, digest);
    return *(ULONG *)digest;
}

#define FEISTEL_ROUNDS 8

static ULONG64 block_encrypt(const UCHAR key[32], ULONG64 block)
{
    ULONG left = (ULONG)block, right = (ULONG)(block >> 32);
    for (ULONG r = 0; r < FEISTEL_ROUNDS; r++) {
        ULONG next = left ^ round_function(key, r, right);
        left = right;
        right = next;
    }
    return (ULONG64)left | ((ULONG64)right << 32);
}

static ULONG64 block_decrypt(const UCHAR key[32], ULONG64 block)
{
    ULONG left = (ULONG)block, right = (ULONG)(block >> 32);
    for (ULONG r = FEISTEL_ROUNDS; r-- > 0;) {
        ULONG previous = right ^ round_function(key, r, left);
        right = left;
        left = previous;
    }
    return (ULONG64)left | ((ULONG64)right << 32);
}

static void crypt_memory(PUCHAR data, ULONG length, BOOLEAN encrypt, ULONG scope)
{
    UCHAR key[32];
    scope_key(scope, PsCurrentProcess(), key);
    ULONG64 chain = 0;
    for (ULONG at = 0; at < length; at += 8) {
        ULONG64 block;
        memcpy(&block, data + at, 8);
        ULONG64 result = encrypt ? block_encrypt(key, block ^ chain) : block_decrypt(key, block) ^ chain;
        chain = encrypt ? result : block;
        memcpy(data + at, &result, 8);
    }
}

/* ------------------------------------------------------------ the cng configuration */

#define CRYPT_UM 1
#define CRYPT_KM 2
#define CRYPT_MM 3
#define CRYPT_ALL_FUNCTIONS 1
#define CRYPT_ALL_PROVIDERS 2
#define CNG_INTERFACES 8

typedef struct CNG_FUNCTION {
    ULONG Interface;
    PCWSTR Function;
    BOOLEAN Kernel;  /* cng.sys serves it in kernel mode too */
    ULONG KeyLength; /* the KeyLength property, 0 for none */
} CNG_FUNCTION;

/* the default context of windows 11, as the configuration answers in user and kernel mode, in its order. */
static const CNG_FUNCTION g_functions[] = {
    {1, L"AES", TRUE, 128},
    {1, L"3DES", TRUE, 128},
    {1, L"3DES_112", TRUE, 112},
    {1, L"XTS-AES", TRUE, 0},
    {1, L"DESX", TRUE, 128},
    {1, L"DES", TRUE, 128},
    {1, L"RC2", TRUE, 128},
    {1, L"RC4", TRUE, 128},
    {1, L"CHACHA20_POLY1305", TRUE, 0},
    {2, L"SHA256", TRUE, 0},
    {2, L"SHA384", TRUE, 0},
    {2, L"SHA512", TRUE, 0},
    {2, L"SHA3-256", TRUE, 0},
    {2, L"SHA3-384", TRUE, 0},
    {2, L"SHA3-512", TRUE, 0},
    {2, L"CSHAKE128", TRUE, 0},
    {2, L"CSHAKE256", TRUE, 0},
    {2, L"KMAC128", TRUE, 0},
    {2, L"KMAC256", TRUE, 0},
    {2, L"SHAKE128", TRUE, 0},
    {2, L"SHAKE256", TRUE, 0},
    {2, L"SHA1", TRUE, 0},
    {2, L"MD5", TRUE, 0},
    {2, L"MD4", TRUE, 0},
    {2, L"MD2", TRUE, 0},
    {2, L"AES-GMAC", TRUE, 0},
    {2, L"AES-CMAC", TRUE, 0},
    {3, L"RSA", TRUE, 1024},
    {4, L"DH", TRUE, 1024},
    {4, L"ECDH_P256", TRUE, 256},
    {4, L"ECDH_P384", TRUE, 384},
    {4, L"ECDH_P521", TRUE, 521},
    {4, L"ECDH", TRUE, 0},
    {5, L"RSA_SIGN", TRUE, 1024},
    {5, L"ECDSA_P256", TRUE, 256},
    {5, L"ECDSA_P384", TRUE, 384},
    {5, L"ECDSA_P521", TRUE, 521},
    {5, L"ECDSA", TRUE, 0},
    {5, L"DSA", FALSE, 1024},
    {5, L"ML-DSA", TRUE, 0},
    {5, L"Composite-ML-DSA", TRUE, 0},
    {6, L"RNG", TRUE, 0},
    {6, L"FIPS186DSARNG", TRUE, 0},
    {6, L"DUALECRNG", TRUE, 0},
    {7, L"SP800_108_CTR_HMAC", TRUE, 0},
    {7, L"SP800_56A_CONCAT", TRUE, 0},
    {7, L"PBKDF2", TRUE, 0},
    {7, L"CAPI_KDF", TRUE, 0},
    {7, L"TLS1_1_KDF", TRUE, 0},
    {7, L"TLS1_2_KDF", TRUE, 0},
    {7, L"HKDF", TRUE, 0},
    {8, L"ML-KEM", TRUE, 0},
    {8, L"Composite-ML-KEM", TRUE, 0},
};

#define PRIMITIVE_PROVIDER L"Microsoft Primitive Provider"

/* the registered providers, as BCryptEnumRegisteredProviders lists them. */
static const PCWSTR g_providers[] = {
    L"Microsoft Key Protection Provider",
    L"Microsoft Passport Key Storage Provider",
    L"Microsoft Platform Crypto Provider",
    L"Microsoft Pluton Cryptographic Provider",
    PRIMITIVE_PROVIDER,
    L"Microsoft Smart Card Key Storage Provider",
    L"Microsoft Software Key Storage Provider",
    L"Microsoft SSL Protocol Provider",
    L"Windows Client Key Protection Provider",
};

/* an answer grows as a self-relative block: offsets from its start, all-ones for nothing. */
typedef struct CNG_OUT {
    PUCHAR Data;
    ULONG Size; /* what the answer needs, also past the room */
    ULONG Room;
} CNG_OUT;

static ULONG reserve(CNG_OUT *o, ULONG size, ULONG align)
{
    ULONG at = (o->Size + align - 1) & ~(align - 1);
    if (at + size <= o->Room)
        memset(o->Data + o->Size, 0, at + size - o->Size);
    o->Size = at + size;
    return at;
}

static void put32(CNG_OUT *o, ULONG at, ULONG value)
{
    if (at + 4 <= o->Room)
        memcpy(o->Data + at, &value, 4);
}

static void put64(CNG_OUT *o, ULONG at, ULONG64 value)
{
    if (at + 8 <= o->Room)
        memcpy(o->Data + at, &value, 8);
}

/* a string, padded to eight bytes when the layout asks for it. */
static ULONG put_string(CNG_OUT *o, PCWSTR text, BOOLEAN padded)
{
    ULONG bytes = (ULONG)(wcslen(text) + 1) * sizeof(WCHAR);
    ULONG at = reserve(o, padded ? (bytes + 7) & ~7u : bytes, padded ? 8 : 2);
    if (at + bytes <= o->Room)
        memcpy(o->Data + at, text, bytes);
    return at;
}

static ULONG put_image(CNG_OUT *o, PCWSTR image)
{
    ULONG at = reserve(o, 16, 8);
    put32(o, at + 8, 1);
    put64(o, at, put_string(o, image, TRUE));
    return at;
}

/* a CRYPT_PROVIDER_REF and what it points to, in windows' order. */
static void put_provider_ref(CNG_OUT *o, ULONG slot, const CNG_FUNCTION *f, ULONG mode)
{
    ULONG ref = reserve(o, 56, 8);
    put64(o, slot, ref);
    put32(o, ref, f->Interface);
    put64(o, ref + 8, put_string(o, f->Function, TRUE));
    put64(o, ref + 16, put_string(o, PRIMITIVE_PROVIDER, TRUE));
    if (f->KeyLength) {
        put32(o, ref + 24, 1);
        ULONG array = reserve(o, 8, 8);
        put64(o, ref + 32, array);
        ULONG property = reserve(o, 24, 8);
        put64(o, array, property);
        put64(o, property, put_string(o, L"KeyLength", TRUE));
        put32(o, property + 8, sizeof(ULONG));
        ULONG value = reserve(o, 8, 8);
        put32(o, value, f->KeyLength);
        put64(o, property + 16, value);
    } else {
        put32(o, ref + 24, 0);
        put64(o, ref + 32, ~0ull);
    }
    put64(o, ref + 40, mode & CRYPT_UM ? put_image(o, L"bcryptprimitives.dll") : ~0ull);
    put64(o, ref + 48, mode & CRYPT_KM ? put_image(o, L"cng.sys") : ~0ull);
}

/* a string the request carries at an offset, or none for all-ones. */
static BOOLEAN request_string(const UCHAR *in, ULONG in_len, ULONG field, PCWSTR *out)
{
    ULONG64 offset;
    memcpy(&offset, in + field, 8);
    *out = nullptr;
    if (offset == ~0ull)
        return TRUE;
    if (offset < 48 || offset >= in_len || (offset & 1))
        return FALSE;
    const WCHAR *text = (const WCHAR *)(in + offset);
    for (ULONG i = 0; offset + (i + 1) * sizeof(WCHAR) <= in_len; i++) {
        if (!text[i]) {
            *out = text;
            return TRUE;
        }
    }
    return FALSE;
}

/* BCryptResolveProviders: the providers of a function, or of every function of an interface. */
static NTSTATUS resolve(const UCHAR *in, ULONG in_len, CNG_OUT *o)
{
    if (in_len < 48)
        return STATUS_INVALID_PARAMETER;
    ULONG interface, mode, flags;
    memcpy(&interface, in + 16, 4);
    memcpy(&mode, in + 40, 4);
    memcpy(&flags, in + 44, 4);
    PCWSTR context, function, provider;
    if (!request_string(in, in_len, 8, &context) || !request_string(in, in_len, 24, &function) ||
        !request_string(in, in_len, 32, &provider))
        return STATUS_INVALID_PARAMETER;
    if (mode < CRYPT_UM || mode > CRYPT_MM || (flags & ~(ULONG)(CRYPT_ALL_FUNCTIONS | CRYPT_ALL_PROVIDERS)) ||
        interface > CNG_INTERFACES || (!interface && !function))
        return STATUS_INVALID_PARAMETER;
    if (context && _wcsicmp(context, L"Default"))
        return STATUS_NOT_FOUND;
    if (provider && _wcsicmp(provider, PRIMITIVE_PROVIDER))
        return STATUS_NOT_FOUND;
    const CNG_FUNCTION *match[RTL_NUMBER_OF(g_functions)];
    ULONG count = 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_functions); i++) {
        const CNG_FUNCTION *f = &g_functions[i];
        if ((interface && f->Interface != interface) || (function && _wcsicmp(function, f->Function)))
            continue;
        if ((mode & CRYPT_KM) && !f->Kernel)
            continue;
        match[count++] = f;
        if (!function && !(flags & CRYPT_ALL_FUNCTIONS))
            break;
    }
    if (!count)
        return STATUS_NOT_FOUND;
    ULONG head = reserve(o, 16, 8);
    put32(o, head, count);
    ULONG array = reserve(o, count * 8, 8);
    put64(o, head + 8, array);
    for (ULONG i = 0; i < count; i++)
        put_provider_ref(o, array + i * 8, match[i], mode);
    return STATUS_SUCCESS;
}

/* BCryptEnumRegisteredProviders: the names, packed one after the other. */
static NTSTATUS registered_providers(CNG_OUT *o)
{
    ULONG count = RTL_NUMBER_OF(g_providers);
    ULONG head = reserve(o, 16, 8);
    put32(o, head, count);
    ULONG array = reserve(o, count * 8, 8);
    put64(o, head + 8, array);
    for (ULONG i = 0; i < count; i++)
        put64(o, array + i * 8, put_string(o, g_providers[i], FALSE));
    return STATUS_SUCCESS;
}

/*
 * a configuration request starts with the magic and the function. a failure travels in the answer as STATUS_BUFFER_OVERFLOW
 * with the status in the first four bytes, and the size an answer needs after STATUS_BUFFER_TOO_SMALL.
 */
static NTSTATUS configuration(PUCHAR buf, ULONG in_len, ULONG out_len, ULONG_PTR *information)
{
    if (in_len < 8 || *(PULONG)buf != CNG_MAGIC)
        return STATUS_INVALID_PARAMETER;
    ULONG function = ((PULONG)buf)[1];
    UCHAR *in = ExAllocatePool2(POOL_FLAG_NON_PAGED, in_len, KI_TAG('K', 's', 'e', 'c'));
    if (!in)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(in, buf, in_len);
    CNG_OUT o = {buf, 0, out_len};
    NTSTATUS status;
    switch (function) {
    case 0x10500: /* BcpRegisterConfigChangeNotify: an event set when the configuration changes, which it never does */
    case 0x10501: /* and its removal */
        status = in_len >= 104 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
        break;
    case 0x20000:
        status = resolve(in, in_len, &o);
        break;
    case 0x3:
        status = registered_providers(&o);
        break;
    case 0x1: case 0x2: case 0x10000: case 0x10001: case 0x10002: case 0x10003: case 0x10004:
    case 0x10200: case 0x10201: case 0x10202: case 0x10203: case 0x10204:
    case 0x10300: case 0x10301: case 0x10302: case 0x10400: case 0x10401:
        /* the registration and the contexts, which this configuration does not change or list. */
        status = STATUS_INVALID_PARAMETER;
        break;
    default:
        ExFreePool(in);
        return STATUS_ILLEGAL_FUNCTION;
    }
    ExFreePool(in);
    if (NT_SUCCESS(status) && o.Size > out_len) {
        if (out_len < 8)
            return STATUS_BUFFER_TOO_SMALL;
        ((PULONG)buf)[0] = (ULONG)STATUS_BUFFER_TOO_SMALL;
        ((PULONG)buf)[1] = o.Size;
        *information = 8;
        return STATUS_BUFFER_OVERFLOW;
    }
    if (!NT_SUCCESS(status)) {
        if (out_len < 4)
            return status;
        ((PULONG)buf)[0] = (ULONG)status;
        *information = 4;
        return STATUS_BUFFER_OVERFLOW;
    }
    *information = o.Size;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------ the devices */

static NTSTATUS NTAPI ksec_request(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    switch (stack->MajorFunction) {
    case IRP_MJ_CREATE:
        status = stack->FileObject->FileName.Length ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_SUCCESS;
        break;
    case IRP_MJ_CLEANUP:
    case IRP_MJ_CLOSE:
        break;
    case IRP_MJ_DEVICE_CONTROL: {
        ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
        ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
        ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
        PUCHAR buf = irp->AssociatedIrp.SystemBuffer;
        switch (code) {
        case IOCTL_KSEC_RNG:
        case IOCTL_KSEC_RNG_REKEY:
            random_bytes(buf, out_len);
            information = out_len;
            break;
        case IOCTL_KSEC_ENCRYPT_MEMORY:
        case IOCTL_KSEC_DECRYPT_MEMORY:
        case IOCTL_KSEC_ENCRYPT_MEMORY_CROSS_PROC:
        case IOCTL_KSEC_DECRYPT_MEMORY_CROSS_PROC:
        case IOCTL_KSEC_ENCRYPT_MEMORY_SAME_LOGON:
        case IOCTL_KSEC_DECRYPT_MEMORY_SAME_LOGON: {
            /* the data comes in, and goes back to the same memory, in whole blocks. */
            if (!in_len || (in_len & 7) || out_len != in_len) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            ULONG op = ((code >> 2) & 0xFFF) - 3;
            crypt_memory(buf, in_len, !(op & 1), op / 2);
            /* a buffered request is copied back by the i/o manager, a direct one goes to the caller's memory. */
            if (irp->UserBuffer != buf)
                status = KiCopyToUser(irp->UserBuffer, buf, in_len);
            if (NT_SUCCESS(status))
                information = in_len;
            break;
        }
        case IOCTL_CNG_CONFIGURATION:
            status = configuration(buf, in_len, out_len, &information);
            break;
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI ksec_entry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = ksec_request;
    driver->MajorFunction[IRP_MJ_CLEANUP] = ksec_request;
    driver->MajorFunction[IRP_MJ_CLOSE] = ksec_request;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ksec_request;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\KsecDD");
    PDEVICE_OBJECT device;
    return IoCreateDevice(driver, 0, &name, FILE_DEVICE_KSEC, 0, FALSE, &device);
}

static NTSTATUS NTAPI cng_entry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = ksec_request;
    driver->MajorFunction[IRP_MJ_CLEANUP] = ksec_request;
    driver->MajorFunction[IRP_MJ_CLOSE] = ksec_request;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ksec_request;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\CNG");
    PDEVICE_OBJECT device;
    return IoCreateDevice(driver, 0, &name, FILE_DEVICE_KSEC, 0, FALSE, &device);
}

void IopInitializeKsec(void)
{
    KeInitializeSpinLock(&g_rng_lock);
    random_bytes(g_memory_key, sizeof(g_memory_key));
    if (!NT_SUCCESS(IoCreateDriver(L"\\Driver\\KSecDD", ksec_entry, nullptr)))
        KiLogPrintf("ksecdd failed to start");
    if (!NT_SUCCESS(IoCreateDriver(L"\\Driver\\CNG", cng_entry, nullptr)))
        KiLogPrintf("cng failed to start");
}
