#ifndef MIME_VMM_PCI_H
#define MIME_VMM_PCI_H

#include "../mime_host.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIME_PCI_MAGIC 0x49435046u /* 'FPCI' */
#define MIME_PCI_CONFIG 4096
#define MIME_PCI_ROOT 0xFFFFu

typedef struct MimeVm MimeVm;
typedef struct MimePciBus MimePciBus;

typedef struct MimePciHostFunction {
    uint8_t bus, devfn;
    uint16_t parent;  /* bus << 8 | devfn of the bridge above it, MIME_PCI_ROOT on a root bus. */
    uint32_t length;  /* bytes of configuration read: 64 as a user, 256 or 4096 as root. */
    uint64_t sizes[7]; /* what each bar and the rom decode, zero where nothing. */
    uint8_t config[MIME_PCI_CONFIG];
} MimePciHostFunction;

typedef struct MimePciCapture {
    uint32_t magic;
    uint32_t count;
    MimePciHostFunction functions[];
} MimePciCapture;

/* linux/pci.c: the functions of pci domain 0 in bus order, false with an error when sysfs lists none. */
bool mime_pci_capture(MimePciCapture **out, size_t *size);

/* what mime up read as root for the machine, which runs as the user. false with an error when it cannot be written. */
bool mime_pci_save(const MimePaths *paths, const MimePciCapture *capture, size_t size);

/*
 * the bus of the machine, from what mime up saved, after the firmware is placed. false with an error when nothing was
 * saved. the guest reads and writes copies of the host's configuration spaces, never the devices themselves.
 */
bool mime_pci_init(MimeVm *vm);
void mime_pci_destroy(MimeVm *vm);

/* one access of a vcpu, false when it is not the bus's. data holds size bytes, read into or written from. */
bool mime_pci_io(MimeVm *vm, uint16_t port, uint32_t size, bool write, uint8_t *data);
bool mime_pci_mmio(MimeVm *vm, uint64_t address, uint32_t size, bool write, uint8_t *data);

#endif
