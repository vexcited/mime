/*
 * the firmware of the machine mime runs on, as the host read it. an arm64 host has none, so its guest has the
 * reference machine's.
 */
#ifndef MIME_FIRMWARE_GUEST_H
#define MIME_FIRMWARE_GUEST_H

#include "../ntos.h"

/* acpi tables, and one by index with its signature and length. */
ULONG KiFirmwareAcpiCount(VOID);
const UCHAR *KiFirmwareAcpi(ULONG index, PULONG signature, PULONG size);

#endif
