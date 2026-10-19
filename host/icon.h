/*
 * pictures of programs for the host's desktop: an icon of Windows (.ico, as host/ui/pe_icon.c makes of a program's
 * resources too) as the png that icon themes take.
 */
#ifndef MIME_ICON_H
#define MIME_ICON_H

#include "mime_host.h"

/*
 * the png of the best picture in an icon file: the largest one up to 256 pixels. the bytes are malloc-owned. false when
 * the file has no picture this can read (it reads png pictures, and bitmaps of 1, 4, 8, 24 and 32 bits).
 */
bool mime_ico_to_png(const void *ico, size_t size, void **png, size_t *png_size, uint32_t *side);

/* a png of the pixels, rows of r, g, b, a. stored, not compressed. */
bool mime_png_encode(const uint8_t *rgba, uint32_t width, uint32_t height, void **png, size_t *png_size);

#endif
