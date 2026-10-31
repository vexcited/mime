/*
 * the function tables the key storage providers of Windows take from bcryptprimitives, and the conversion of an RSA
 * private blob. The tables hold bcrypt's own entry points, so only what bcrypt does can be given out: the random number
 * generator. The other interfaces refuse every algorithm as Windows refuses one it has no table for.
 */
#include <mime/stubexport.h>

#define STATUS_NOT_SUPPORTED_ ((LONG)0xC00000BB)

typedef LONG(WINAPI *BC_OPEN)(void **, LPCWSTR, LPCWSTR, ULONG);
typedef LONG(WINAPI *BC_CLOSE)(void *, ULONG);
typedef LONG(WINAPI *BC_GET_PROPERTY)(void *, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
typedef LONG(WINAPI *BC_SET_PROPERTY)(void *, LPCWSTR, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *BC_GEN_RANDOM)(void *, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *BC_IMPORT)(void *, void *, LPCWSTR, void **, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *BC_EXPORT)(void *, void *, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
typedef LONG(WINAPI *BC_DESTROY)(void *);

static struct {
    BC_OPEN open;
    BC_CLOSE close;
    BC_GET_PROPERTY get_property;
    BC_SET_PROPERTY set_property;
    BC_GEN_RANDOM generate;
    BC_IMPORT import;
    BC_EXPORT export_key;
    BC_DESTROY destroy;
} g_bcrypt;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI load_bcrypt(INIT_ONCE *once, void *param, void **context)
{
    HMODULE module = LoadLibraryW(L"bcrypt.dll");
    if (!module)
        return TRUE;
    g_bcrypt.open = (BC_OPEN)GetProcAddress(module, "BCryptOpenAlgorithmProvider");
    g_bcrypt.close = (BC_CLOSE)GetProcAddress(module, "BCryptCloseAlgorithmProvider");
    g_bcrypt.get_property = (BC_GET_PROPERTY)GetProcAddress(module, "BCryptGetProperty");
    g_bcrypt.set_property = (BC_SET_PROPERTY)GetProcAddress(module, "BCryptSetProperty");
    g_bcrypt.generate = (BC_GEN_RANDOM)GetProcAddress(module, "BCryptGenRandom");
    g_bcrypt.import = (BC_IMPORT)GetProcAddress(module, "BCryptImportKeyPair");
    g_bcrypt.export_key = (BC_EXPORT)GetProcAddress(module, "BCryptExportKey");
    g_bcrypt.destroy = (BC_DESTROY)GetProcAddress(module, "BCryptDestroyKey");
    return TRUE;
}

static BOOL bcrypt_ready(void)
{
    InitOnceExecuteOnce(&g_once, load_bcrypt, nullptr, nullptr);
    return g_bcrypt.open && g_bcrypt.close && g_bcrypt.get_property && g_bcrypt.set_property && g_bcrypt.generate &&
           g_bcrypt.import && g_bcrypt.export_key && g_bcrypt.destroy;
}

/* ------------------------------------------------------------------ the random number generator */

static LONG WINAPI rng_open(void **handle, LPCWSTR algorithm, ULONG flags)
{
    if (!bcrypt_ready())
        return STATUS_NOT_SUPPORTED_;
    return g_bcrypt.open(handle, algorithm, nullptr, flags);
}

static LONG WINAPI rng_get_property(void *handle, LPCWSTR property, PUCHAR output, ULONG size, ULONG *result, ULONG flags)
{
    return bcrypt_ready() ? g_bcrypt.get_property(handle, property, output, size, result, flags) : STATUS_NOT_SUPPORTED_;
}

static LONG WINAPI rng_set_property(void *handle, LPCWSTR property, PUCHAR input, ULONG size, ULONG flags)
{
    return bcrypt_ready() ? g_bcrypt.set_property(handle, property, input, size, flags) : STATUS_NOT_SUPPORTED_;
}

static LONG WINAPI rng_close(void *handle, ULONG flags)
{
    return bcrypt_ready() ? g_bcrypt.close(handle, flags) : STATUS_NOT_SUPPORTED_;
}

static LONG WINAPI rng_generate(void *handle, PUCHAR buffer, ULONG size, ULONG flags)
{
    return bcrypt_ready() ? g_bcrypt.generate(handle, buffer, size, flags) : STATUS_NOT_SUPPORTED_;
}

/* the table of the generator: its version, then open, get and set property, close and generate. */
static const struct {
    ULONG version;
    LONG(WINAPI *open)(void **, LPCWSTR, ULONG);
    LONG(WINAPI *get_property)(void *, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
    LONG(WINAPI *set_property)(void *, LPCWSTR, PUCHAR, ULONG, ULONG);
    LONG(WINAPI *close)(void *, ULONG);
    LONG(WINAPI *generate)(void *, PUCHAR, ULONG, ULONG);
} g_rng_table = {1, rng_open, rng_get_property, rng_set_property, rng_close, rng_generate};

DECLSPEC_EXPORT LONG WINAPI GetRngInterface(ULONG_PTR provider, const void **table, ULONG flags)
{
    *table = &g_rng_table;
    return 0;
}

/* ------------------------------------------------------------------ the other interfaces */

DECLSPEC_EXPORT LONG WINAPI GetAsymmetricEncryptionInterface(ULONG_PTR provider, LPCWSTR algorithm, const void **table,
                                                             ULONG flags)
{
    return STATUS_NOT_SUPPORTED_;
}

DECLSPEC_EXPORT LONG WINAPI GetCipherInterface(ULONG_PTR provider, LPCWSTR algorithm, const void **table, ULONG flags)
{
    return STATUS_NOT_SUPPORTED_;
}

DECLSPEC_EXPORT LONG WINAPI GetHashInterface(ULONG_PTR provider, LPCWSTR algorithm, const void **table, ULONG flags)
{
    return STATUS_NOT_SUPPORTED_;
}

DECLSPEC_EXPORT LONG WINAPI GetKeyDerivationInterface(ULONG_PTR provider, LPCWSTR algorithm, const void **table,
                                                      ULONG flags)
{
    return STATUS_NOT_SUPPORTED_;
}

DECLSPEC_EXPORT LONG WINAPI GetKeyEncapsulationInterface(ULONG_PTR provider, LPCWSTR algorithm, const void **table,
                                                         ULONG flags)
{
    return STATUS_NOT_SUPPORTED_;
}

DECLSPEC_EXPORT LONG WINAPI GetSecretAgreementInterface(ULONG_PTR provider, LPCWSTR algorithm, const void **table,
                                                        ULONG flags)
{
    return STATUS_NOT_SUPPORTED_;
}

DECLSPEC_EXPORT LONG WINAPI GetSignatureInterface(ULONG_PTR provider, LPCWSTR algorithm, const void **table, ULONG flags)
{
    return STATUS_NOT_SUPPORTED_;
}

/* a private blob read into a key and written again with all of its parts. */
DECLSPEC_EXPORT LONG WINAPI MSCryptConvertRsaPrivateBlobToFullRsaBlob(PUCHAR blob, ULONG blob_size, PUCHAR output,
                                                                      ULONG output_size, ULONG *result)
{
    if (!bcrypt_ready())
        return STATUS_NOT_SUPPORTED_;
    void *algorithm = nullptr, *key = nullptr;
    LONG status = g_bcrypt.open(&algorithm, L"RSA", nullptr, 0);
    if (status < 0)
        return status;
    status = g_bcrypt.import(algorithm, nullptr, L"PRIVATEBLOB", &key, blob, blob_size, 0);
    if (status >= 0)
        status = g_bcrypt.export_key(key, nullptr, L"RSAFULLPRIVATEBLOB", output, output_size, result, 0);
    if (key)
        g_bcrypt.destroy(key);
    g_bcrypt.close(algorithm, 0);
    return status;
}
