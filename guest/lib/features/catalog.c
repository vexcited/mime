/* The features Mime can fetch, and the checks on what comes back. Only addresses Microsoft publishes are here. */
#include "features.h"

#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

static const FEATURE_FONT management_files[] = {
    {
        .File = "Microsoft.Management.Infrastructure.dll",
        .Member = "runtimes/win10-x64/lib/net451/Microsoft.Management.Infrastructure.dll",
        .Sha256 = "96b01d1d981c216d417f10fd0c49f6a45299465e70b384202897638c979c7a18",
        .Size = 133280,
    },
};

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
        .InstallMB = 600,
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
    {
        .Name = "PowerShell",
        .DisplayName = "Windows PowerShell 5.1",
        .Version = "5.1.14409.1005",
        .Vendor = "Microsoft Corporation",
        /*
         * The update KB3191564 of the Windows Management Framework 5.1 for Windows 8.1 and Windows Server 2012 R2. It is
         * not a program: Microsoft publishes the hash of the file, which the catalog names, and that is what decides.
         */
        .Url = {[FEATURE_ARCH_X64] = "https://go.microsoft.com/fwlink/?linkid=839516"},
        .Signer = "Microsoft Corporation",
        .MinSize = 15ull << 20,
        .MaxSize = 40ull << 20,
        .DownloadMB = 19,
        .InstallMB = 130,
        .Handler = FEATURE_HANDLER_POWERSHELL,
        .Format = FEATURE_FORMAT_CABINET,
        .Sha256 = "a8d788fa31b02a999cc676fb546fc782e86c2a0acd837976122a1891ceee42c0",
    },
    {
        .Name = "ManagementInfrastructure",
        .DisplayName = "Windows Management Infrastructure for PowerShell",
        .Version = "1.0.0.0",
        .Vendor = "Microsoft Corporation",
        /*
         * The package of NuGet that Microsoft publishes for PowerShell, of which one library is read by range. Windows
         * has this library as a part of the system and the Management Framework does not carry it.
         */
        .Url = {[FEATURE_ARCH_X64] = "https://api.nuget.org/v3-flatcontainer/microsoft.management.infrastructure/1.0.0/microsoft.management.infrastructure.1.0.0.nupkg"},
        .Format = FEATURE_FORMAT_ZIP,
        .MinSize = 0ull, .MaxSize = 1099511627776ull,
        .DownloadMB = 1,
        .InstallMB = 1,
        .Handler = FEATURE_HANDLER_MANAGEMENT,
        .Terms = "This library is Microsoft's. The package names its license at https://github.com/PowerShell/PowerShell-Tests/blob/master/LICENSE.",
        .Fonts = management_files, .FontCount = 1,
    },
    {
        .Name = "VCRuntime.x64",
        .DisplayName = "Microsoft Visual C++ Runtime for x64 programs",
        .Version = "Latest",
        .Vendor = "Microsoft Corporation",
        /*
         * The redistributable of the latest runtime, which holds the arm64x runtime and the x64 one. Windows on arm
         * installs the first, mime runs x64 programs with x64 system dlls and takes the second out of it.
         */
        .Url = {[FEATURE_ARCH_X64] = "https://aka.ms/vc14/vc_redist.x64.exe"},
        .Signer = "Microsoft Corporation",
        .MinSize = 10ull << 20,
        .MaxSize = 100ull << 20,
        .DownloadMB = 19,
        .InstallMB = 15,
        .Handler = FEATURE_HANDLER_VCRUNTIME,
    },
};

/* The fonts, in fontcatalog.c. */
extern const FEATURE_DEF g_font_features[];
extern const size_t g_font_feature_count;

#define BASE_COUNT (sizeof(g_catalog) / sizeof(g_catalog[0]))

size_t FeatureCount(void)
{
    return BASE_COUNT + g_font_feature_count;
}

const FEATURE_DEF *FeatureAt(size_t index)
{
    if (index < BASE_COUNT)
        return &g_catalog[index];
    return index < FeatureCount() ? &g_font_features[index - BASE_COUNT] : nullptr;
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
    for (size_t i = 0; i < FeatureCount(); i++) {
        const FEATURE_DEF *feature = FeatureAt(i);
        if (strlen(feature->Name) == n && equal_nocase(feature->Name, name, n))
            return feature;
    }
    return nullptr;
}

const FEATURE_DEF *FeatureFindProviding(const char *family)
{
    if (!family)
        return nullptr;
    size_t n = strlen(family);
    for (size_t i = 0; i < FeatureCount(); i++) {
        const FEATURE_DEF *feature = FeatureAt(i);
        for (const char *const *name = feature->Provides; name && *name; name++)
            if (strlen(*name) == n && equal_nocase(*name, family, n))
                return feature;
        for (size_t k = 0; k < feature->FontCount; k++)
            for (const char *const *name = feature->Fonts[k].Families; name && *name; name++)
                if (strlen(*name) == n && equal_nocase(*name, family, n))
                    return feature;
    }
    return nullptr;
}

const FEATURE_FONT *FeatureFindFont(const FEATURE_DEF *feature, const char *file)
{
    if (!file)
        return nullptr;
    size_t n = strlen(file);
    for (size_t k = 0; k < feature->FontCount; k++)
        if (strlen(feature->Fonts[k].File) == n && equal_nocase(feature->Fonts[k].File, file, n))
            return &feature->Fonts[k];
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
    /* aka.ms is Microsoft's link shortener, it only redirects. */
    static const char domain[] = ".microsoft.com", shortener[] = "aka.ms";
    if (n == sizeof(shortener) - 1 && equal_nocase(host, shortener, n))
        return true;
    return n > sizeof(domain) - 1 && equal_nocase(host + n - (sizeof(domain) - 1), domain, sizeof(domain) - 1);
}

/* The host of an address, without a port, when the address is plain: a name, no user name or brackets. */
static bool address_host(const char *url, const char *scheme, size_t *start, size_t *length)
{
    size_t scheme_length = strlen(scheme);
    if (!url || strlen(url) <= scheme_length || !equal_nocase(url, scheme, scheme_length))
        return false;
    const char *host = url + scheme_length;
    size_t n = 0;
    while (host[n] && host[n] != '/' && host[n] != ':' && host[n] != '?' && host[n] != '#') {
        if (host[n] == '@' || host[n] == '[' || host[n] == '\\')
            return false;
        n++;
    }
    *start = scheme_length;
    *length = n;
    return n > 0;
}

static bool host_is(const char *url, size_t start, size_t length, const char *domain)
{
    size_t d = strlen(domain);
    /* The domain itself or a name below it. */
    if (length == d)
        return equal_nocase(url + start, domain, d);
    return length > d && url[start + length - d - 1] == '.' && equal_nocase(url + start + length - d, domain, d);
}

bool FeatureUrlAllowedFor(const FEATURE_DEF *feature, const char *url)
{
    if (FeatureUrlAllowed(url))
        return true;
    /* Only what a hash decides may come from anywhere else, and each kind of file from one place. */
    size_t start, length;
    if (feature->Format == FEATURE_FORMAT_ZIP && address_host(url, "https://", &start, &length))
        return host_is(url, start, length, "github.com") || host_is(url, start, length, "githubusercontent.com") ||
               host_is(url, start, length, "api.nuget.org");
    if (feature->Format == FEATURE_FORMAT_WIM && address_host(url, "http://", &start, &length))
        return host_is(url, start, length, "delivery.mp.microsoft.com");
    return false;
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
