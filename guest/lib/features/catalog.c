/* The features Mime can fetch, and the checks on what comes back. Only addresses Microsoft publishes are here. */
#include "features.h"

#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

static const FEATURE_DEF g_catalog[] = {
    {
        .Name = "WebView2",
        .DisplayName = "Microsoft Edge WebView2 Runtime",
        .Version = "Evergreen",
        .Vendor = "Microsoft Corporation",
        /* The Evergreen standalone installers, each processor has its own. */
        .Url = {
            [FEATURE_ARCH_X64] = "https://go.microsoft.com/fwlink/?linkid=2124701",
            [FEATURE_ARCH_ARM64] = "https://go.microsoft.com/fwlink/?linkid=2099616",
            [FEATURE_ARCH_X86] = "https://go.microsoft.com/fwlink/?linkid=2099617",
        },
        .Signer = "Microsoft Corporation",
        .MinSize = 20ull << 20,
        .MaxSize = 600ull << 20,
        .DownloadMB = 215,
        .Handler = FEATURE_HANDLER_WEBVIEW2,
    },
    {
        .Name = "NetFx4",
        .DisplayName = ".NET Framework 4.8.1",
        .Version = "4.8.1",
        .Vendor = "Microsoft Corporation",
        /* The offline installer, which holds the files of the Framework for every processor. */
        .Url = {[FEATURE_ARCH_X64] = "https://go.microsoft.com/fwlink/?linkid=2203305"},
        .Signer = "Microsoft Corporation",
        .MinSize = 30ull << 20,
        .MaxSize = 300ull << 20,
        .DownloadMB = 78,
        .Handler = FEATURE_HANDLER_NETFX,
    },
};

size_t FeatureCount(void)
{
    return sizeof(g_catalog) / sizeof(g_catalog[0]);
}

const FEATURE_DEF *FeatureAt(size_t index)
{
    return index < FeatureCount() ? &g_catalog[index] : nullptr;
}

static int fold(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static bool equal_nocase(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (fold((unsigned char)a[i]) != fold((unsigned char)b[i]))
            return false;
    return true;
}

const FEATURE_DEF *FeatureFind(const char *name)
{
    if (!name)
        return nullptr;
    size_t n = strlen(name);
    for (size_t i = 0; i < FeatureCount(); i++)
        if (strlen(g_catalog[i].Name) == n && equal_nocase(g_catalog[i].Name, name, n))
            return &g_catalog[i];
    return nullptr;
}

const FEATURE_DEF *FeatureFindProviding(const char *family)
{
    if (!family)
        return nullptr;
    size_t n = strlen(family);
    for (size_t i = 0; i < FeatureCount(); i++)
        for (const char *const *name = g_catalog[i].Provides; name && *name; name++)
            if (strlen(*name) == n && equal_nocase(*name, family, n))
                return &g_catalog[i];
    return nullptr;
}

const char *FeatureUrl(const FEATURE_DEF *feature, FEATURE_ARCH arch)
{
    if ((unsigned)arch < FEATURE_ARCH_COUNT && feature->Url[arch])
        return feature->Url[arch];
    return feature->Url[FEATURE_ARCH_X64];
}

bool FeatureUrlAllowed(const char *url)
{
    static const char scheme[] = "https://";
    if (!url || strlen(url) < sizeof(scheme) || !equal_nocase(url, scheme, sizeof(scheme) - 1))
        return false;
    const char *host = url + sizeof(scheme) - 1;
    size_t n = 0;
    /* A name, never a user name or an address with brackets, and a port is not part of it. */
    while (host[n] && host[n] != '/' && host[n] != ':' && host[n] != '?' && host[n] != '#') {
        if (host[n] == '@' || host[n] == '[' || host[n] == '\\')
            return false;
        n++;
    }
    static const char domain[] = ".microsoft.com";
    return n > sizeof(domain) - 1 && equal_nocase(host + n - (sizeof(domain) - 1), domain, sizeof(domain) - 1);
}

bool FeatureSignerMatches(const FEATURE_DEF *feature, const char *signer)
{
    return signer && strlen(signer) == strlen(feature->Signer) &&
           equal_nocase(signer, feature->Signer, strlen(signer));
}

/*
 * The roots that Microsoft's code signing chains end in. A package is trusted when its chain ends in one of these,
 * whatever else the machine trusts, so a certificate from some other authority never passes for Microsoft's.
 */
static const uint8_t g_roots[][20] = {
    /* Microsoft Root Certificate Authority 2010. */
    {0x3B, 0x1E, 0xFD, 0x3A, 0x66, 0xEA, 0x28, 0xB1, 0x66, 0x97, 0x39, 0x47, 0x03, 0xA7, 0x2C, 0xA3, 0x40, 0xA0, 0x5B, 0xD5},
    /* Microsoft Root Certificate Authority 2011. */
    {0x8F, 0x43, 0x28, 0x8A, 0xD2, 0x72, 0xF3, 0x10, 0x3B, 0x6F, 0xB1, 0x42, 0x84, 0x85, 0xEA, 0x30, 0x14, 0xC0, 0xBC, 0xFE},
};

bool FeatureRootPinned(const uint8_t sha1[20])
{
    for (size_t i = 0; i < sizeof(g_roots) / sizeof(g_roots[0]); i++)
        if (!memcmp(g_roots[i], sha1, 20))
            return true;
    return false;
}
