/*
 * identity of the installation: what windows setup and the first boot
 * experience choose once, computer name, user, machine sid, volume serial.
 */
#ifndef MIME_IDENTITY_H
#define MIME_IDENTITY_H

#include "mime_host.h"

typedef struct MimeIdentity {
    char computer_name[16];
    char user_name[24];
    uint32_t sid[3];
    uint32_t volume_serial;
    uint64_t install_filetime;
    char machine_serial[20]; /* serial of the case, as firmware reports it */
    char machine_uuid[40];   /* smbios uuid, text form */
    char locale[32];         /* name of the locale of the user, empty until chosen. */
} MimeIdentity;

/*
 * the machine the guest presents itself as: a stock windows 11 arm64
 * computer, so that nothing in it can tell the mac, the hypervisor or the
 * apple hardware. fixed strings, the same for every installation, like a
 * hardware model is.
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
