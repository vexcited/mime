/* the host's own firmware tables, handed to the guest so it sees the machine it runs on. */
#ifndef MIME_FIRMWARE_H
#define MIME_FIRMWARE_H

#include "mime_host.h"

#include "../include/mime/abi.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * reads the acpi tables and the smbios of this machine into one MimeFirmware blob, with the edid of a panel when one
 * has it. false with an error when the tables or the smbios cannot be read. root is needed, the /sys/firmware tables
 * are root only.
 */
bool mime_firmware_build(MimeFirmware **out, size_t *size);

/* the firmware mime up saved for this start, false when there is none. */
bool mime_firmware_load(const MimePaths *paths, MimeFirmware **out, size_t *size);

/* saves the firmware read as root for the machine, false with an error when it cannot be written. */
bool mime_firmware_save(const MimePaths *paths, const MimeFirmware *firmware, size_t size);

#endif
