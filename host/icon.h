/*
 * converts Windows icons to PNG for desktop icon themes. host/ui/pe_icon.c extracts ICO data from program resources.
 */
#ifndef MIME_ICON_H
#define MIME_ICON_H

#include "mime_host.h"

/*
 * returns malloc-owned PNG data for the largest readable icon image up to 256 pixels. supports PNG and 1, 4, 8, 24
 * or 32-bit bitmaps; returns false if none can be read.
 */
bool mime_ico_to_png(const void *ico, size_t size, void **png, size_t *png_size, uint32_t *side);

/* writes uncompressed PNG from rows of RGBA pixels. */
bool mime_png_encode(const uint8_t *rgba, uint32_t width, uint32_t height, void **png, size_t *png_size);

#endif
