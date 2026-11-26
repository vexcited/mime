/* the host's own firmware tables, handed to the guest so it sees the machine it runs on. */
#ifndef MIME_FIRMWARE_H
#define MIME_FIRMWARE_H

#include "mime_host.h"

#include "../include/mime/abi.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * reads the dmi table and the acpi tables of this machine into one MimeFirmware blob, false when neither could be
 * read. root is needed, the /sys/firmware tables are root only.
 */
bool mime_firmware_build(MimeFirmware **out, size_t *size);

/* the firmware mime up saved for this start, false when there is none and the reference tables are served. */
bool mime_firmware_load(const MimePaths *paths, MimeFirmware **out, size_t *size);

/* saves the firmware read as root for the machine; nullptr removes the one of an earlier start. */
void mime_firmware_save(const MimePaths *paths, const MimeFirmware *firmware, size_t size);

#endif
