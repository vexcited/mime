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

#ifdef __aarch64__
/* armv8.2 has no generator in the processor. */
static BOOLEAN have_rdrand(void)
{
    return FALSE;
}

static ULONG64 rdrand(void)
{
    return 0;
}
#else
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
#endif

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

/*
 * The configuration is the registry's: Configuration\Local\<context>\<interface> lists the functions of an interface in
 * the context's order, each function key the providers that serve it and its properties, and Providers\<name> registers
 * an image per mode with the functions it has per interface. Measured with rp.c of scratch/g2/ncrypt of the
 * research repository.
 */
#define CNG_KEY L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Cryptography"
#define CNG_DEFAULT_CONTEXT CNG_KEY L"\\Configuration\\Local\\Default"
#define CNG_PATH 512
#define CNG_NAME 260
#define CNG_REG_SZ 1
#define CNG_REG_DWORD 4
#define CNG_REG_MULTI_SZ 7

static const ULONG g_interfaces[] = {1, 2, 3, 4, 5, 6, 7, 8, 0x10001, 0x10002, 0x10003, 0x10004};

static void path_add(PWCHAR path, PCWSTR part)
{
    SIZE_T at = wcslen(path), n = wcslen(part);
    if (at + n + 2 > CNG_PATH)
        return;
    path[at] = L'\\';
    memcpy(path + at + 1, part, (n + 1) * sizeof(WCHAR));
}

static void path_add_interface(PWCHAR path, ULONG interface)
{
    WCHAR text[9];
    for (int i = 0; i < 8; i++)
        text[i] = L"0123456789ABCDEF"[(interface >> (28 - 4 * i)) & 15];
    text[8] = 0;
    path_add(path, text);
}

/* a multi string value, ended by an empty string, or null. */
static PWCHAR read_multi(PCWSTR key, PCWSTR name)
{
    ULONG type = 0, length = 0;
    NTSTATUS status = CmQueryValue(key, name, &type, nullptr, &length);
    if (status != STATUS_BUFFER_OVERFLOW && !(NT_SUCCESS(status) && !length))
        return nullptr;
    PWCHAR text = ExAllocatePool2(POOL_FLAG_NON_PAGED, length + 2 * sizeof(WCHAR), KI_TAG('K', 's', 'e', 'c'));
    if (!text)
        return nullptr;
    ULONG room = length;
    if (length && !NT_SUCCESS(CmQueryValue(key, name, &type, text, &room))) {
        ExFreePool(text);
        return nullptr;
    }
    if (type != CNG_REG_MULTI_SZ && type != CNG_REG_SZ) {
        ExFreePool(text);
        return nullptr;
    }
    text[room / sizeof(WCHAR)] = 0;
    text[room / sizeof(WCHAR) + 1] = 0;
    return text;
}

static PCWSTR multi_find(PCWSTR list, PCWSTR name)
{
    for (PCWSTR s = list; s && *s; s += wcslen(s) + 1)
        if (!_wcsicmp(s, name))
            return s;
    return nullptr;
}

typedef struct CNG_NAMES {
    PWCHAR Text;
    ULONG Used, Room;
} CNG_NAMES;

static void collect_name(PVOID ctx, PCWSTR name, ULONG chars)
{
    CNG_NAMES *n = ctx;
    if (n->Used + chars + 2 > n->Room)
        return;
    memcpy(n->Text + n->Used, name, chars * sizeof(WCHAR));
    n->Used += chars;
    n->Text[n->Used++] = 0;
    n->Text[n->Used] = 0;
}

/* the names of the registered providers, in the order of their keys. */
static PWCHAR provider_names(void)
{
    CNG_NAMES n = {ExAllocatePool2(POOL_FLAG_NON_PAGED, 0x2000, KI_TAG('K', 's', 'e', 'c')), 0, 0x1000};
    if (!n.Text)
        return nullptr;
    n.Text[0] = n.Text[1] = 0;
    CmEnumerateSubkeys(CNG_KEY L"\\Providers", collect_name, &n);
    return n.Text;
}

/* what a provider registers for an interface in one mode: its image, the flags and the functions. */
typedef struct CNG_IMAGE {
    WCHAR Image[CNG_NAME];
    ULONG Flags;
    PWCHAR Functions;
} CNG_IMAGE;

static BOOLEAN provider_image(PCWSTR provider, PCWSTR mode, ULONG interface, CNG_IMAGE *out)
{
    WCHAR path[CNG_PATH] = CNG_KEY L"\\Providers";
    path_add(path, provider);
    path_add(path, mode);
    ULONG type = 0, length = sizeof(out->Image) - sizeof(WCHAR);
    memset(out, 0, sizeof(*out));
    if (!NT_SUCCESS(CmQueryValue(path, L"Image", &type, out->Image, &length)) || type != CNG_REG_SZ)
        return FALSE;
    path_add_interface(path, interface);
    out->Functions = read_multi(path, L"Functions");
    if (!out->Functions)
        return FALSE;
    length = sizeof(ULONG);
    if (!NT_SUCCESS(CmQueryValue(path, L"Flags", &type, &out->Flags, &length)) || type != CNG_REG_DWORD)
        out->Flags = 0;
    return TRUE;
}

/* the registered name of a provider, given by it or by one of its aliases. */
static PCWSTR provider_by_name(PCWSTR names, PCWSTR asked)
{
    PCWSTR found = multi_find(names, asked);
    for (PCWSTR s = names; !found && s && *s; s += wcslen(s) + 1) {
        WCHAR path[CNG_PATH] = CNG_KEY L"\\Providers";
        path_add(path, s);
        PWCHAR aliases = read_multi(path, L"Aliases");
        if (multi_find(aliases, asked))
            found = s;
        if (aliases)
            ExFreePool(aliases);
    }
    return found;
}

static ULONG put_image(CNG_OUT *o, const CNG_IMAGE *image)
{
    ULONG at = reserve(o, 16, 8);
    put32(o, at + 8, image->Flags);
    put64(o, at, put_string(o, image->Image, TRUE));
    return at;
}

typedef struct CNG_PROPERTIES {
    CNG_OUT *Out;
    ULONG Array, Count, Index;
} CNG_PROPERTIES;

static void count_property(PVOID ctx, PCWSTR name, ULONG chars, ULONG type, const void *data, ULONG length)
{
    ((CNG_PROPERTIES *)ctx)->Count++;
}

static void put_property(PVOID ctx, PCWSTR name, ULONG chars, ULONG type, const void *data, ULONG length)
{
    CNG_PROPERTIES *p = ctx;
    CNG_OUT *o = p->Out;
    if (p->Index >= p->Count || chars >= CNG_NAME)
        return;
    WCHAR text[CNG_NAME];
    memcpy(text, name, chars * sizeof(WCHAR));
    text[chars] = 0;
    ULONG property = reserve(o, 24, 8);
    put64(o, p->Array + p->Index++ * 8, property);
    put64(o, property, put_string(o, text, TRUE));
    put32(o, property + 8, length);
    ULONG value = reserve(o, (length + 7) & ~7u, 8);
    if (value + length <= o->Room)
        memcpy(o->Data + value, data, length);
    put64(o, property + 16, value);
}

/*
 * a CRYPT_PROVIDER_REF and what it points to, in windows' order. a provider asked for by name alone has no function
 * and no properties.
 */
static BOOLEAN put_provider_ref(CNG_OUT *o, ULONG slot, PCWSTR context_key, ULONG interface, PCWSTR function,
                                PCWSTR provider, ULONG mode)
{
    CNG_IMAGE um = {}, km = {};
    BOOLEAN ok = TRUE;
    if (mode & CRYPT_UM)
        ok = provider_image(provider, L"UM", interface, &um) && (!function || multi_find(um.Functions, function));
    if (ok && (mode & CRYPT_KM))
        ok = provider_image(provider, L"KM", interface, &km) && (!function || multi_find(km.Functions, function));
    if (ok) {
        ULONG ref = reserve(o, 56, 8);
        put64(o, slot, ref);
        put32(o, ref, interface);
        put64(o, ref + 8, function ? put_string(o, function, TRUE) : ~0ull);
        put64(o, ref + 16, put_string(o, provider, TRUE));
        CNG_PROPERTIES properties = {o, 0, 0, 0};
        WCHAR path[CNG_PATH];
        if (function) {
            wcscpy(path, context_key);
            path_add_interface(path, interface);
            path_add(path, function);
            path_add(path, L"Properties");
            CmEnumerateValues(path, count_property, &properties);
        }
        put32(o, ref + 24, properties.Count);
        if (properties.Count) {
            properties.Array = reserve(o, properties.Count * 8, 8);
            put64(o, ref + 32, properties.Array);
            CmEnumerateValues(path, put_property, &properties);
        } else {
            put64(o, ref + 32, ~0ull);
        }
        put64(o, ref + 40, mode & CRYPT_UM ? put_image(o, &um) : ~0ull);
        put64(o, ref + 48, mode & CRYPT_KM ? put_image(o, &km) : ~0ull);
    }
    if (um.Functions)
        ExFreePool(um.Functions);
    if (km.Functions)
        ExFreePool(km.Functions);
    return ok;
}

/* a string the request carries at an offset, or none for all-ones. */
static BOOLEAN request_string_after(const UCHAR *in, ULONG in_len, ULONG field, ULONG head, PCWSTR *out)
{
    ULONG64 offset;
    memcpy(&offset, in + field, 8);
    *out = nullptr;
    if (offset == ~0ull)
        return TRUE;
    if (offset < head || offset >= in_len || (offset & 1))
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

static BOOLEAN request_string(const UCHAR *in, ULONG in_len, ULONG field, PCWSTR *out)
{
    return request_string_after(in, in_len, field, 48, out);
}

#define CNG_MAX_REFS 512

typedef struct CNG_REFS {
    CNG_OUT *Out;
    ULONG Array, Count;
} CNG_REFS;

static BOOLEAN add_ref(CNG_REFS *r, PCWSTR context_key, ULONG interface, PCWSTR function, PCWSTR provider, ULONG mode)
{
    if (r->Count >= CNG_MAX_REFS)
        return FALSE;
    if (!put_provider_ref(r->Out, r->Array + r->Count * 8, context_key, interface, function, provider, mode))
        return FALSE;
    r->Count++;
    return TRUE;
}

static void no_subkey(PVOID ctx, PCWSTR name, ULONG chars)
{
}

static BOOLEAN function_key(PCWSTR context_key, ULONG interface, PCWSTR function)
{
    WCHAR path[CNG_PATH];
    wcscpy(path, context_key);
    path_add_interface(path, interface);
    path_add(path, function);
    return NT_SUCCESS(CmEnumerateSubkeys(path, no_subkey, nullptr));
}

/*
 * the providers of one function: the ones the context lists for it, in its order, then, when all providers or a named
 * one are asked for, the registered ones that have the function. without the flag the first one answers.
 */
static void resolve_function(CNG_REFS *r, PCWSTR context_key, ULONG interface, PCWSTR function, PCWSTR names,
                             PCWSTR provider, ULONG mode, ULONG flags)
{
    WCHAR path[CNG_PATH];
    wcscpy(path, context_key);
    path_add_interface(path, interface);
    path_add(path, function);
    PWCHAR listed = read_multi(path, L"Providers");
    /* a context that only orders the functions takes their providers from the default one. */
    if (!listed) {
        context_key = CNG_DEFAULT_CONTEXT;
        wcscpy(path, context_key);
        path_add_interface(path, interface);
        path_add(path, function);
        listed = read_multi(path, L"Providers");
    }
    ULONG before = r->Count;
    for (PCWSTR s = listed; s && *s; s += wcslen(s) + 1) {
        if (provider && _wcsicmp(s, provider))
            continue;
        PCWSTR registered = multi_find(names, s);
        if (registered && add_ref(r, context_key, interface, function, registered, mode) &&
            !(flags & CRYPT_ALL_PROVIDERS))
            break;
    }
    if ((flags & CRYPT_ALL_PROVIDERS) || (provider && r->Count == before)) {
        for (PCWSTR s = names; s && *s; s += wcslen(s) + 1) {
            if ((provider && s != provider) || multi_find(listed, s))
                continue;
            if (add_ref(r, context_key, interface, function, s, mode) && !(flags & CRYPT_ALL_PROVIDERS))
                break;
        }
    }
    if (listed)
        ExFreePool(listed);
}

/* BCryptResolveProviders: the providers of a function, of the functions of an interface, or a provider by its name. */
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
    BOOLEAN known = !interface;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_interfaces); i++)
        known = known || g_interfaces[i] == interface;
    if (mode < CRYPT_UM || mode > CRYPT_MM || (flags & ~(ULONG)(CRYPT_ALL_FUNCTIONS | CRYPT_ALL_PROVIDERS)) || !known ||
        (!interface && !function))
        return STATUS_INVALID_PARAMETER;
    if (!context)
        context = L"Default";
    if (provider && !*provider)
        provider = nullptr;
    if (wcslen(context) >= CNG_NAME || (function && wcslen(function) >= CNG_NAME))
        return STATUS_NOT_FOUND;
    WCHAR context_key[CNG_PATH] = CNG_KEY L"\\Configuration\\Local";
    path_add(context_key, context);
    PWCHAR names = provider_names();
    if (!names)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = STATUS_NOT_FOUND;
    if (provider && !(provider = provider_by_name(names, provider)))
        goto done;
    ULONG head = reserve(o, 16, 8);
    CNG_REFS refs = {o, 0, 0};
    /* the array of references comes before them, so a first pass counts and a second one writes. */
    for (int pass = 0; pass < 2; pass++) {
        CNG_OUT counting = {nullptr, 0, 0};
        CNG_REFS r = {pass ? o : &counting, 0, 0};
        if (pass) {
            if (!refs.Count)
                break;
            r.Array = reserve(o, refs.Count * 8, 8);
            put32(o, head, refs.Count);
            put64(o, head + 8, r.Array);
        }
        if (!function && provider) {
            add_ref(&r, context_key, interface, nullptr, provider, mode);
        } else {
            for (ULONG i = 0; i < RTL_NUMBER_OF(g_interfaces); i++) {
                if (interface && g_interfaces[i] != interface)
                    continue;
                WCHAR path[CNG_PATH];
                wcscpy(path, context_key);
                path_add_interface(path, g_interfaces[i]);
                PWCHAR functions = read_multi(path, L"Functions");
                if (function) {
                    /* with a provider named, the answer spells the function as it was asked. */
                    PCWSTR found = multi_find(functions, function);
                    if (found)
                        resolve_function(&r, context_key, g_interfaces[i], provider ? function : found, names, provider,
                                         mode, flags);
                } else {
                    BOOLEAN more = TRUE;
                    for (PCWSTR s = functions; more && s && *s; s += wcslen(s) + 1) {
                        resolve_function(&r, context_key, g_interfaces[i], s, names, provider, mode, flags);
                        more = !r.Count || (flags & CRYPT_ALL_FUNCTIONS);
                    }
                    /* then the functions the context has a key for without ordering them, as their providers register them. */
                    for (PCWSTR p = names; more && *p; p += wcslen(p) + 1) {
                        CNG_IMAGE image;
                        provider_image(p, mode & CRYPT_UM ? L"UM" : L"KM", g_interfaces[i], &image);
                        for (PCWSTR s = image.Functions; more && s && *s; s += wcslen(s) + 1) {
                            if (multi_find(functions, s) || !function_key(context_key, g_interfaces[i], s))
                                continue;
                            resolve_function(&r, context_key, g_interfaces[i], s, names, provider, mode, flags);
                            more = !r.Count || (flags & CRYPT_ALL_FUNCTIONS);
                        }
                        if (image.Functions)
                            ExFreePool(image.Functions);
                    }
                }
                if (functions)
                    ExFreePool(functions);
            }
        }
        refs.Count = r.Count;
    }
    if (refs.Count)
        status = STATUS_SUCCESS;
done:
    ExFreePool(names);
    return status;
}

/* BCryptEnumRegisteredProviders: the names, packed one after the other. */
static NTSTATUS registered_providers(CNG_OUT *o)
{
    PWCHAR names = provider_names();
    if (!names)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG count = 0;
    for (PCWSTR s = names; *s; s += wcslen(s) + 1)
        count++;
    ULONG head = reserve(o, 16, 8);
    put32(o, head, count);
    ULONG array = reserve(o, count * 8, 8);
    put64(o, head + 8, array);
    ULONG i = 0;
    for (PCWSTR s = names; *s; s += wcslen(s) + 1)
        put64(o, array + i++ * 8, put_string(o, s, FALSE));
    ExFreePool(names);
    return STATUS_SUCCESS;
}

/* a list of names: their count, the array of their offsets and the strings one after the other. */
static void put_names(CNG_OUT *o, ULONG head, PCWSTR names)
{
    ULONG count = 0;
    for (PCWSTR s = names; s && *s; s += wcslen(s) + 1)
        count++;
    put32(o, head, count);
    if (!count) {
        put64(o, head + 8, ~0ull);
        return;
    }
    ULONG array = reserve(o, count * 8, 8);
    put64(o, head + 8, array);
    ULONG i = 0;
    for (PCWSTR s = names; *s; s += wcslen(s) + 1)
        put64(o, array + i++ * 8, put_string(o, s, FALSE));
}

static PWCHAR subkey_names(PCWSTR key)
{
    CNG_NAMES n = {ExAllocatePool2(POOL_FLAG_NON_PAGED, 0x2000, KI_TAG('K', 's', 'e', 'c')), 0, 0x1000};
    if (!n.Text)
        return nullptr;
    n.Text[0] = n.Text[1] = 0;
    if (!NT_SUCCESS(CmEnumerateSubkeys(key, collect_name, &n))) {
        ExFreePool(n.Text);
        return nullptr;
    }
    return n.Text;
}

static ULONG interface_id(PCWSTR text)
{
    ULONG id = 0;
    for (; *text; text++) {
        ULONG digit = *text >= L'0' && *text <= L'9' ? *text - L'0' : (*text | 0x20) >= L'a' && (*text | 0x20) <= L'f' ? (*text | 0x20) - L'a' + 10 : 16;
        if (digit > 15)
            return 0;
        id = id << 4 | digit;
    }
    return id;
}

static BOOLEAN known_interface(ULONG interface)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_interfaces); i++)
        if (g_interfaces[i] == interface)
            return TRUE;
    return FALSE;
}

/*
 * a CRYPT_IMAGE_REG: the image, then each interface it registers with its flags and functions. all-ones when the mode has
 * no image or the image does not have the interface that was asked for.
 */
static ULONG64 put_image_reg(CNG_OUT *o, PCWSTR provider, PCWSTR mode, ULONG interface)
{
    WCHAR path[CNG_PATH] = CNG_KEY L"\\Providers";
    path_add(path, provider);
    path_add(path, mode);
    WCHAR image[CNG_NAME] = {};
    ULONG type = 0, length = sizeof(image) - sizeof(WCHAR);
    if (!NT_SUCCESS(CmQueryValue(path, L"Image", &type, image, &length)) || type != CNG_REG_SZ)
        return ~0ull;
    PWCHAR keys = subkey_names(path);
    ULONG count = 0;
    for (PCWSTR s = keys; s && *s; s += wcslen(s) + 1) {
        ULONG id = interface_id(s);
        if (known_interface(id) && (!interface || id == interface))
            count++;
    }
    if (interface && !count) {
        if (keys)
            ExFreePool(keys);
        return ~0ull;
    }
    ULONG reg = reserve(o, 24, 8);
    put64(o, reg, put_string(o, image, TRUE));
    put32(o, reg + 8, count);
    ULONG array = count ? reserve(o, count * 8, 8) : 0;
    put64(o, reg + 16, count ? array : ~0ull);
    ULONG index = 0;
    for (PCWSTR s = keys; s && *s; s += wcslen(s) + 1) {
        ULONG id = interface_id(s);
        if (!known_interface(id) || (interface && id != interface))
            continue;
        CNG_IMAGE registered;
        provider_image(provider, mode, id, &registered);
        ULONG entry = reserve(o, 24, 8);
        put64(o, array + index++ * 8, entry);
        put32(o, entry, id);
        put32(o, entry + 4, registered.Flags);
        ULONG functions = 0;
        for (PCWSTR f = registered.Functions; f && *f; f += wcslen(f) + 1)
            functions++;
        put32(o, entry + 8, functions);
        if (functions) {
            ULONG list = reserve(o, functions * 8, 8);
            put64(o, entry + 16, list);
            ULONG k = 0;
            for (PCWSTR f = registered.Functions; *f; f += wcslen(f) + 1)
                put64(o, list + k++ * 8, put_string(o, f, FALSE));
        } else {
            put64(o, entry + 16, ~0ull);
        }
        if (registered.Functions)
            ExFreePool(registered.Functions);
    }
    if (keys)
        ExFreePool(keys);
    return reg;
}

static BOOLEAN has_image(PCWSTR provider, PCWSTR mode)
{
    WCHAR path[CNG_PATH] = CNG_KEY L"\\Providers";
    path_add(path, provider);
    path_add(path, mode);
    WCHAR image[CNG_NAME];
    ULONG type = 0, length = sizeof(image);
    return NT_SUCCESS(CmQueryValue(path, L"Image", &type, image, &length)) && type == CNG_REG_SZ;
}

/*
 * BCryptQueryProviderRegistration: the aliases of a provider and what its images register. mode 4 takes the images there
 * are, the other modes need theirs. measured with cf.c of scratch/g2/lsa of the research repository.
 */
static NTSTATUS registration(const UCHAR *in, ULONG in_len, CNG_OUT *o)
{
    if (in_len < 40)
        return STATUS_INVALID_PARAMETER;
    ULONG mode, interface;
    memcpy(&mode, in + 16, 4);
    memcpy(&interface, in + 20, 4);
    PCWSTR asked;
    if (!request_string_after(in, in_len, 8, 40, &asked) || !asked || !*asked || mode < 1 || mode > 4 ||
        (interface && !known_interface(interface)))
        return STATUS_INVALID_PARAMETER;
    PWCHAR names = provider_names();
    if (!names)
        return STATUS_INSUFFICIENT_RESOURCES;
    PCWSTR provider = provider_by_name(names, asked);
    BOOLEAN um = provider && has_image(provider, L"UM"), km = provider && has_image(provider, L"KM");
    if (!provider || ((mode & CRYPT_UM) && mode != 4 && !um) || ((mode & CRYPT_KM) && !km)) {
        ExFreePool(names);
        return STATUS_NOT_FOUND;
    }
    WCHAR path[CNG_PATH] = CNG_KEY L"\\Providers";
    path_add(path, provider);
    PWCHAR aliases = read_multi(path, L"Aliases");
    ULONG head = reserve(o, 32, 8), images = head + 16;
    put_names(o, head, aliases);
    ULONG64 first = mode != CRYPT_KM && um ? put_image_reg(o, provider, L"UM", interface) : ~0ull;
    ULONG64 second = mode != CRYPT_UM && km ? put_image_reg(o, provider, L"KM", interface) : ~0ull;
    put64(o, images, first);
    put64(o, images + 8, second);
    reserve(o, 0, 8);
    if (aliases)
        ExFreePool(aliases);
    ExFreePool(names);
    return STATUS_SUCCESS;
}

static BOOLEAN key_exists(PCWSTR path)
{
    return NT_SUCCESS(CmEnumerateSubkeys(path, no_subkey, nullptr));
}

typedef struct CNG_CONFIG_REQUEST {
    ULONG Table, Interface, Position, Length;
    PCWSTR Context, Function, Provider, Property;
    const UCHAR *ContextConfig, *FunctionConfig, *Buffer;
    WCHAR TableKey[CNG_PATH], ContextKey[CNG_PATH], InterfaceKey[CNG_PATH], FunctionKey[CNG_PATH];
} CNG_CONFIG_REQUEST;

static const UCHAR *request_block(const UCHAR *in, ULONG in_len, ULONG field, ULONG size)
{
    ULONG64 offset;
    memcpy(&offset, in + field, 8);
    if (offset == ~0ull || offset < 104 || offset > in_len || size > in_len - offset)
        return nullptr;
    return in + offset;
}

static NTSTATUS config_request(const UCHAR *in, ULONG in_len, CNG_CONFIG_REQUEST *r)
{
    if (in_len < 104)
        return STATUS_INVALID_PARAMETER;
    memset(r, 0, sizeof(*r));
    memcpy(&r->Table, in + 8, 4);
    memcpy(&r->Interface, in + 24, 4);
    memcpy(&r->Position, in + 56, 4);
    memcpy(&r->Length, in + 80, 4);
    if (!request_string(in, in_len, 16, &r->Context) || !request_string(in, in_len, 32, &r->Function) ||
        !request_string(in, in_len, 40, &r->Provider) || !request_string(in, in_len, 48, &r->Property))
        return STATUS_INVALID_PARAMETER;
    if (r->Table != 1 && r->Table != 2)
        return STATUS_INVALID_PARAMETER;
    r->ContextConfig = request_block(in, in_len, 64, 8);
    r->FunctionConfig = request_block(in, in_len, 72, 8);
    r->Buffer = request_block(in, in_len, 88, r->Length);
    if (!r->Context || !*r->Context)
        r->Context = L"Default";
    if (wcslen(r->Context) >= CNG_NAME || (r->Function && wcslen(r->Function) >= CNG_NAME) ||
        (r->Provider && wcslen(r->Provider) >= CNG_NAME) || (r->Property && wcslen(r->Property) >= CNG_NAME))
        return STATUS_INVALID_PARAMETER;
    wcscpy(r->TableKey, r->Table == 1 ? CNG_KEY L"\\Configuration\\Local" : CNG_KEY L"\\Configuration\\Domain");
    wcscpy(r->ContextKey, r->TableKey);
    path_add(r->ContextKey, r->Context);
    wcscpy(r->InterfaceKey, r->ContextKey);
    path_add_interface(r->InterfaceKey, r->Interface);
    wcscpy(r->FunctionKey, r->InterfaceKey);
    if (r->Function)
        path_add(r->FunctionKey, r->Function);
    return STATUS_SUCCESS;
}

static ULONG key_flags(PCWSTR key)
{
    ULONG flags = 0, type = 0, length = sizeof(flags);
    if (!NT_SUCCESS(CmQueryValue(key, L"Flags", &type, &flags, &length)) || type != CNG_REG_DWORD)
        flags = 0;
    return flags;
}

/* the contexts of a table, a context's flags and functions, and a function's flags, providers and properties. */
static NTSTATUS context_query(ULONG function, const UCHAR *in, ULONG in_len, CNG_OUT *o)
{
    CNG_CONFIG_REQUEST *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('K', 's', 'e', 'c'));
    if (!r)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = config_request(in, in_len, r);
    PWCHAR names = nullptr;
    if (!NT_SUCCESS(status))
        goto done;
    if (function >= 0x10200) {
        status = STATUS_INVALID_PARAMETER;
        if (!known_interface(r->Interface))
            goto done;
        if (function != 0x10202 && (!r->Function || !*r->Function))
            goto done;
        if (function == 0x10401 && (!r->Property || !*r->Property))
            goto done;
    }
    status = STATUS_NOT_FOUND;
    switch (function) {
    case 0x10002:
        names = subkey_names(r->TableKey);
        if (names) {
            put_names(o, reserve(o, 16, 8), names);
            status = STATUS_SUCCESS;
        }
        break;
    case 0x10004:
        if (key_exists(r->ContextKey)) {
            put32(o, reserve(o, 8, 8), key_flags(r->ContextKey));
            status = STATUS_SUCCESS;
        }
        break;
    case 0x10202:
        names = read_multi(r->InterfaceKey, L"Functions");
        if (names) {
            put_names(o, reserve(o, 16, 8), names);
            status = STATUS_SUCCESS;
        }
        break;
    case 0x10204:
        if (key_exists(r->FunctionKey)) {
            put32(o, reserve(o, 8, 8), key_flags(r->FunctionKey));
            status = STATUS_SUCCESS;
        }
        break;
    case 0x10302:
        names = read_multi(r->FunctionKey, L"Providers");
        if (names) {
            put_names(o, reserve(o, 16, 8), names);
            status = STATUS_SUCCESS;
        }
        break;
    case 0x10401: {
        path_add(r->FunctionKey, L"Properties");
        ULONG type = 0, length = 0;
        NTSTATUS query = CmQueryValue(r->FunctionKey, r->Property, &type, nullptr, &length);
        if (query != STATUS_BUFFER_OVERFLOW && !NT_SUCCESS(query))
            break;
        PUCHAR value = ExAllocatePool2(POOL_FLAG_NON_PAGED, length + 8, KI_TAG('K', 's', 'e', 'c'));
        if (!value) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        ULONG room = length;
        if (!length || NT_SUCCESS(CmQueryValue(r->FunctionKey, r->Property, &type, value, &room))) {
            ULONG at = reserve(o, length, 1);
            if (at + length <= o->Room)
                memcpy(o->Data + at, value, length);
            status = STATUS_SUCCESS;
        }
        ExFreePool(value);
        break;
    }
    }
done:
    if (names)
        ExFreePool(names);
    ExFreePool(r);
    return status;
}

/* a multi string value with one name added at the top or the bottom, or taken out. */
static NTSTATUS change_list(PCWSTR key, PCWSTR value, PCWSTR name, BOOLEAN add, BOOLEAN top)
{
    PWCHAR old = read_multi(key, value);
    BOOLEAN listed = multi_find(old, name) != nullptr;
    NTSTATUS status = STATUS_SUCCESS;
    if (add == listed) {
        status = add ? STATUS_SUCCESS : STATUS_NOT_FOUND;
    } else {
        ULONG chars = 0;
        for (PCWSTR s = old; s && *s; s += wcslen(s) + 1)
            chars += (ULONG)wcslen(s) + 1;
        PWCHAR list = ExAllocatePool2(POOL_FLAG_NON_PAGED, (chars + wcslen(name) + 2) * sizeof(WCHAR),
                                      KI_TAG('K', 's', 'e', 'c'));
        if (!list) {
            status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            ULONG at = 0;
            if (add && top) {
                wcscpy(list, name);
                at = (ULONG)wcslen(name) + 1;
            }
            for (PCWSTR s = old; s && *s; s += wcslen(s) + 1) {
                if (!add && !_wcsicmp(s, name))
                    continue;
                wcscpy(list + at, s);
                at += (ULONG)wcslen(s) + 1;
            }
            if (add && !top) {
                wcscpy(list + at, name);
                at += (ULONG)wcslen(name) + 1;
            }
            list[at++] = 0;
            status = CmSetValue(key, value, CNG_REG_MULTI_SZ, list, at * sizeof(WCHAR));
            ExFreePool(list);
        }
    }
    if (old)
        ExFreePool(old);
    return status;
}

/* DELETE, READ_CONTROL and every right of a key. */
#define CNG_CHANGE_ACCESS 0x3001F

/*
 * the requests that change the configuration. registering a provider is not served.
 *
 * cng.sys checks the arguments, then opens a key with access 0x3001F and OBJ_FORCE_ACCESS_CHECK and closes it, and
 * writes as the kernel once that passed: Configuration\Local or Configuration\Domain for the table of the request
 * (CfgReg_Acquire), Providers and then Configuration for a provider (PrvReg_Acquire). The descriptor of the key
 * decides, so the limited token gets STATUS_ACCESS_DENIED.
 */
static NTSTATUS context_change(ULONG function, const UCHAR *in, ULONG in_len)
{
    if (function == 0) {
        NTSTATUS access = CmCheckAccess(CNG_KEY L"\\Providers", CNG_CHANGE_ACCESS);
        return NT_SUCCESS(access) ? STATUS_NOT_SUPPORTED : access;
    }
    if (function == 1) {
        PCWSTR asked;
        if (in_len < 40 || !request_string_after(in, in_len, 8, 40, &asked) || !asked || !*asked || wcslen(asked) >= CNG_NAME)
            return STATUS_INVALID_PARAMETER;
        NTSTATUS access = CmCheckAccess(CNG_KEY L"\\Providers", CNG_CHANGE_ACCESS);
        if (!NT_SUCCESS(access))
            return access;
        WCHAR path[CNG_PATH] = CNG_KEY L"\\Providers";
        path_add(path, asked);
        if (!key_exists(path))
            return STATUS_NOT_FOUND;
        access = CmCheckAccess(CNG_KEY L"\\Configuration", CNG_CHANGE_ACCESS);
        return NT_SUCCESS(access) ? CmDeleteKey(path) : access;
    }
    CNG_CONFIG_REQUEST *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('K', 's', 'e', 'c'));
    if (!r)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = config_request(in, in_len, r);
    if (!NT_SUCCESS(status))
        goto done;
    status = STATUS_INVALID_PARAMETER;
    if (function >= 0x10200 && (!known_interface(r->Interface) || !r->Function || !*r->Function))
        goto done;
    if ((function == 0x10300 || function == 0x10301) && (!r->Provider || !*r->Provider))
        goto done;
    if (function == 0x10400 && (!r->Property || !*r->Property))
        goto done;
    status = CmCheckAccess(r->TableKey, CNG_CHANGE_ACCESS);
    if (!NT_SUCCESS(status))
        goto done;
    ULONG flags = 0;
    if (function == 0x10000) {
        if (r->ContextConfig)
            memcpy(&flags, r->ContextConfig, 4);
        status = CmSetValue(r->ContextKey, L"Flags", CNG_REG_DWORD, &flags, sizeof(flags));
        goto done;
    }
    status = STATUS_NOT_FOUND;
    if (!key_exists(r->ContextKey))
        goto done;
    switch (function) {
    case 0x10001:
        status = CmDeleteKey(r->ContextKey);
        break;
    case 0x10003:
        if (r->ContextConfig)
            memcpy(&flags, r->ContextConfig, 4);
        status = CmSetValue(r->ContextKey, L"Flags", CNG_REG_DWORD, &flags, sizeof(flags));
        break;
    case 0x10200:
        status = change_list(r->InterfaceKey, L"Functions", r->Function, TRUE, r->Position == 0);
        break;
    case 0x10201:
        status = change_list(r->InterfaceKey, L"Functions", r->Function, FALSE, FALSE);
        if (NT_SUCCESS(status) && key_exists(r->FunctionKey))
            CmDeleteKey(r->FunctionKey);
        break;
    case 0x10203:
        if (key_exists(r->FunctionKey)) {
            if (r->FunctionConfig)
                memcpy(&flags, r->FunctionConfig, 4);
            status = CmSetValue(r->FunctionKey, L"Flags", CNG_REG_DWORD, &flags, sizeof(flags));
        }
        break;
    case 0x10300:
    case 0x10301: {
        PWCHAR functions = read_multi(r->InterfaceKey, L"Functions");
        if (multi_find(functions, r->Function))
            status = change_list(r->FunctionKey, L"Providers", r->Provider, function == 0x10300, r->Position == 0);
        if (functions)
            ExFreePool(functions);
        break;
    }
    case 0x10400: {
        PWCHAR functions = read_multi(r->InterfaceKey, L"Functions");
        if (multi_find(functions, r->Function)) {
            path_add(r->FunctionKey, L"Properties");
            status = r->Buffer && r->Length ? CmSetValue(r->FunctionKey, r->Property, 3, r->Buffer, r->Length)
                                            : CmDeleteValue(r->FunctionKey, r->Property);
        }
        if (functions)
            ExFreePool(functions);
        break;
    }
    }
done:
    ExFreePool(r);
    return status;
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
    case 0x2:
        status = registration(in, in_len, &o);
        break;
    case 0x10002: case 0x10004: case 0x10202: case 0x10204: case 0x10302: case 0x10401:
        status = context_query(function, in, in_len, &o);
        break;
    case 0x0: case 0x1: case 0x10000: case 0x10001: case 0x10003: case 0x10200: case 0x10201: case 0x10203:
    case 0x10300: case 0x10301: case 0x10400:
        status = context_change(function, in, in_len);
        break;
    default:
        ExFreePool(in);
        return STATUS_ILLEGAL_FUNCTION;
    }
    ExFreePool(in);
    /* windows builds these answers over the request, whose function code shows through the padding of their head. */
    if (NT_SUCCESS(status) && function != 0x20000 && function != 0x10401 && o.Size >= 8)
        put32(&o, 4, function);
    if (NT_SUCCESS(status) && function != 0x20000 && o.Size && out_len < 8)
        return STATUS_BUFFER_TOO_SMALL;
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
