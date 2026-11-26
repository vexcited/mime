#ifndef MIME_HOST_MICROCODE_H
#define MIME_HOST_MICROCODE_H

#include <stdint.h>

/* the microcode revision of the host processor as register 0x8B holds it, the revision in its high half. 0 unknown. */
uint64_t mime_host_microcode(void);
/* the platform flags of an intel host processor, the one bit windows records as well. 0 when the host tells none. */
uint32_t mime_host_platform(void);

#endif
