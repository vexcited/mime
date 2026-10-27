/*
 * The catalog of features on demand and the rules that decide what happens to one. The rules know nothing of the
 * system, everything that touches it is reached through FEATURE_OPS, so that they run in the host's tests with
 * fakes. The errors are Win32 error numbers.
 */
#ifndef MIME_FEATURES_H
#define MIME_FEATURES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FEATURE_OK 0
#define FEATURE_ERROR_NOT_SUPPORTED 50
#define FEATURE_ERROR_CANCELLED 1223
#define FEATURE_ERROR_NOT_FOUND 1168
#define FEATURE_ERROR_ACCESS_DISABLED_BY_POLICY 1260
#define FEATURE_ERROR_INSTALL_FAILURE 1603

/* The processors a download is made for. A missing address falls back to the first one. */
typedef enum FEATURE_ARCH {
    FEATURE_ARCH_X64,
    FEATURE_ARCH_ARM64,
    FEATURE_ARCH_X86,
    FEATURE_ARCH_COUNT
} FEATURE_ARCH;

/* What puts a feature on the drive. The platform part keeps the code, the catalog only names it. */
typedef enum FEATURE_HANDLER {
    FEATURE_HANDLER_WEBVIEW2,
    FEATURE_HANDLER_NETFX,
    /*
     * A set of fonts of Microsoft, installed into the fonts folder and registered under the Fonts key of the
     * registry. No entry uses it yet. Only the real files of the real fonts may be installed, never another font
     * under the name of one.
     */
    FEATURE_HANDLER_FONTS,
} FEATURE_HANDLER;

typedef struct FEATURE_DEF {
    const char *Name;        /* The id on the command line. */
    const char *DisplayName;
    const char *Version;
    const char *Vendor;
    const char *Url[FEATURE_ARCH_COUNT];
    const char *Signer;      /* The common name the signing certificate must have. */
    uint64_t MinSize, MaxSize;
    uint32_t DownloadMB;     /* About how much it is, for the question. */
    FEATURE_HANDLER Handler;
    /* The names a program looks for in a font lookup, for a feature of fonts. Null terminated, or null. */
    const char *const *Provides;
} FEATURE_DEF;

size_t FeatureCount(void);
const FEATURE_DEF *FeatureAt(size_t index);
/* The entry of a name, compared without regard to case. Null when the catalog has none. */
const FEATURE_DEF *FeatureFind(const char *name);
/* The feature of fonts that has a font family, for a font lookup by name. Null when none does. */
const FEATURE_DEF *FeatureFindProviding(const char *family);
/* The address of the download for a processor. */
const char *FeatureUrl(const FEATURE_DEF *feature, FEATURE_ARCH arch);
/* Whether a download may come from this address: https, and a host of microsoft.com. */
bool FeatureUrlAllowed(const char *url);
/* Whether a root certificate, by the SHA-1 of its encoding, is one of the roots Microsoft signs code with. */
bool FeatureRootPinned(const uint8_t sha1[20]);
/* Whether the signer named by a certificate is the one the feature expects. */
bool FeatureSignerMatches(const FEATURE_DEF *feature, const char *signer);

/* What the policy says about downloads. */
typedef enum FEATURE_POLICY {
    FEATURE_POLICY_ASK,   /* The user is asked, unless the request is explicit. */
    FEATURE_POLICY_ALLOW, /* Nobody is asked. */
    FEATURE_POLICY_DENY,  /* Nothing is downloaded. */
} FEATURE_POLICY;

typedef enum FEATURE_STATE {
    FEATURE_STATE_NONE,     /* Never turned on or off. */
    FEATURE_STATE_ENABLED,
    FEATURE_STATE_DISABLED, /* Turned off, its files removed. */
} FEATURE_STATE;

/* The steps, for a window that shows them. */
typedef enum FEATURE_STAGE {
    FEATURE_STAGE_COLLECTING,
    FEATURE_STAGE_DOWNLOADING,
    FEATURE_STAGE_VERIFYING,
    FEATURE_STAGE_INSTALLING,
    FEATURE_STAGE_REMOVING,
} FEATURE_STAGE;

typedef struct FEATURE_OPS {
    void *Context;
    FEATURE_POLICY (*Policy)(void *context, const FEATURE_DEF *feature);
    /* FEATURE_OK when the handler of the feature is there to install it, else the error to give. */
    uint32_t (*Available)(void *context, const FEATURE_DEF *feature);
    /* Whether the files are on the drive. */
    bool (*Present)(void *context, const FEATURE_DEF *feature);
    FEATURE_STATE (*GetState)(void *context, const FEATURE_DEF *feature);
    /* Records the state, the error of the last attempt and whether the files were there before the attempt. */
    void (*SetState)(void *context, const FEATURE_DEF *feature, FEATURE_STATE state, uint32_t error, bool from_drive);
    /* Whether the user said no a short while ago, so that a program that asks again is not asked again. */
    bool (*RecentlyDeclined)(void *context, const FEATURE_DEF *feature);
    void (*SetDeclined)(void *context, const FEATURE_DEF *feature);
    /* Asks the user. Returns FEATURE_OK for yes. */
    uint32_t (*Consent)(void *context, const FEATURE_DEF *feature);
    void (*Stage)(void *context, const FEATURE_DEF *feature, FEATURE_STAGE stage);
    /* Brings the file to the machine, from the vendor or from the source the caller named. */
    uint32_t (*Acquire)(void *context, const FEATURE_DEF *feature, void **package);
    /* Checks the signature and the size of the file. */
    uint32_t (*Verify)(void *context, const FEATURE_DEF *feature, void *package);
    uint32_t (*Install)(void *context, const FEATURE_DEF *feature, void *package);
    /* Forgets the file and deletes it. */
    void (*Discard)(void *context, const FEATURE_DEF *feature, void *package);
    uint32_t (*Remove)(void *context, const FEATURE_DEF *feature);
} FEATURE_OPS;

/* Enable flags. */
#define FEATURE_ENABLE_ASK 0x1 /* A program needs the feature, the user decides. */

/*
 * Turns a feature on. The policy first: denied is an error and nothing happens. Files that are on the drive count as
 * on without a download. Otherwise the user is asked when the request says so and the policy leaves it to them, then
 * the file is acquired, verified and installed, and the state recorded. A file that fails the check is discarded
 * before anything is installed.
 */
uint32_t FeatureEnable(const FEATURE_OPS *ops, const FEATURE_DEF *feature, uint32_t flags);

/* Turns a feature off and removes its files. A feature that is not on the drive is only marked. */
uint32_t FeatureDisable(const FEATURE_OPS *ops, const FEATURE_DEF *feature);

#endif
