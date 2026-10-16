#ifndef MIME_PE_ICON_H
#define MIME_PE_ICON_H

#include <stddef.h>
#include <stdint.h>

/* returns a malloc-owned ico for a resource id, or the first group if negative. */
bool mime_pe_icon_extract(const void *image, size_t size, int32_t group_id, void **icon, size_t *icon_size);
bool mime_pe_icon_read(const char *path, int32_t group_id, void **icon, size_t *icon_size);

#endif
