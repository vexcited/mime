#include "microcode.h"

#include <stdio.h>
#include <stdlib.h>

/* what linux tells every user of the first processor's microcode: a hex number in a file, 0 when there is none. */
static uint64_t microcode_file(const char *name)
{
    char path[96], text[32] = {};
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/microcode/%s", name);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    if (!fgets(text, sizeof(text), f))
        text[0] = 0;
    fclose(f);
    return strtoull(text, nullptr, 16);
}

uint64_t mime_host_microcode(void)
{
    return microcode_file("version") << 32;
}

uint32_t mime_host_platform(void)
{
    return (uint32_t)microcode_file("processor_flags");
}
