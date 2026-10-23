/* persistent installation identity: computer name, user, machine SID and volume serial. */
#ifndef MIME_IDENTITY_H
#define MIME_IDENTITY_H

#include "mime_host.h"

typedef struct MimeIdentity {
    char computer_name[16];
    char user_name[24];
    uint32_t sid[3];
    uint32_t volume_serial;
    uint64_t install_filetime;
    char machine_serial[20]; /* serial of the case, as firmware reports it. */
    char machine_uuid[40];   /* SMBIOS UUID, text form. */
    char locale[32];         /* name of the locale of the user, empty until chosen. */
} MimeIdentity;

/*
 * fixed Windows 11 ARM64 hardware identity shared by installations, replacing the host's Mac and Apple hardware
 * strings.
 */
typedef struct MimeHardwareProfile {
    const char *cpu_name;
    uint32_t cpu_mhz;
    uint64_t cpu_midr;
    const char *manufacturer;
    const char *product;
    const char *family;
    const char *bios_vendor;
    const char *bios_version;
    const char *bios_date;
} MimeHardwareProfile;

const MimeHardwareProfile *mime_hardware_profile(void);

/* reads machine.ini, creating it with fresh values the first time. */
bool mime_identity_load(const MimePaths *paths, MimeIdentity *out);

/* sets the locale of the user in machine.ini; false when the name is not a locale of the system. */
bool mime_identity_set_locale(const MimePaths *paths, const char *name);

#endif
