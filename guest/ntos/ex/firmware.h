/* the firmware of the machine mime runs on, as the host read it; the reference tables stand in when it could not. */
#ifndef MIME_FIRMWARE_GUEST_H
#define MIME_FIRMWARE_GUEST_H

#include "../ntos.h"

/* acpi tables, and one by index with its signature and length. */
ULONG KiFirmwareAcpiCount(VOID);
const UCHAR *KiFirmwareAcpi(ULONG index, PULONG signature, PULONG size);

/* the raw smbios data windows builds. */
const UCHAR *KiFirmwareSmbios(PULONG size);

#endif
