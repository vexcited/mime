/*
 * converts clipboard and drag data between host and Windows formats: text, DIBs, PNG, HTML Format, DROPFILES and URI
 * lists. returned buffers use malloc; empty or invalid input returns null.
 */
#ifndef MIME_LINUX_CONVERT_H
#define MIME_LINUX_CONVERT_H

#include "../mime_host.h"
#include "../../include/mime/abi.h"

/* UTF-8 text as UTF-16LE with windows line ends and no terminator. */
uint8_t *mime_conv_text_to_guest(const char *utf8, size_t len, size_t *out_size);
/* UTF-16LE text, cut at the first NUL, as UTF-8 with unix line ends and a NUL that the length does not count. */
char *mime_conv_text_from_guest(const uint16_t *text, size_t chars, size_t *out_len);

/* a PNG or BMP file as the bytes of CF_DIB, or CF_DIBV5 when v5, bottom up in 32 bits. */
uint8_t *mime_conv_image_to_dib(const void *file, size_t size, bool v5, size_t *out_size);
/* a PNG or BMP file as a PNG file. */
uint8_t *mime_conv_image_to_png(const void *file, size_t size, size_t *out_size);
/* the bytes of CF_DIB or CF_DIBV5 as a PNG file, or as a BMP file. */
uint8_t *mime_conv_dib_to_png(const void *dib, size_t size, size_t *out_size);
uint8_t *mime_conv_dib_to_bmp(const void *dib, size_t size, size_t *out_size);

/* HTML in UTF-8, or UTF-16 with a byte order mark, as "HTML Format". */
uint8_t *mime_conv_html_to_guest(const void *html, size_t size, size_t *out_size);
/* "HTML Format" as the HTML document in utf-8. */
uint8_t *mime_conv_html_from_guest(const void *data, size_t size, size_t *out_size);

/* the file: URIs of a URI list as a DROPFILES block of guest paths; null when no file is on the list. */
uint8_t *mime_conv_uris_to_hdrop(const MimePaths *paths, const char *list, size_t len, size_t *out_size);
/* a DROPFILES block as a URI list with the host paths of the files that have one. */
char *mime_conv_hdrop_to_uris(const MimePaths *paths, const void *data, size_t size, size_t *out_len);
/* the first URI that is not a file as the text of "UniformResourceLocatorW", terminator included. */
uint8_t *mime_conv_uris_to_url(const char *list, size_t len, size_t *out_size);
/* the text of "UniformResourceLocatorW" or "UniformResourceLocator" as a URI list. */
char *mime_conv_url_to_uris(const void *text, size_t size, bool wide, size_t *out_len);

/*
 * mime_conv_kinds maps offered types to MIME_CLIP_* bits. mime_conv_read_type selects an input type or null;
 * mime_conv_to_guest converts its bytes to the guest layout.
 */
uint32_t mime_conv_kinds(char *const *mimes, size_t count);
/* returns supported formats and the marked drop effect for file transfers. */
uint32_t mime_conv_clipboard_kinds(char *const *mimes, size_t count);
const char *mime_conv_read_type(uint32_t kind, char *const *mimes, size_t count);
uint8_t *mime_conv_to_guest(uint32_t kind, const void *raw, size_t size, size_t *out_size);

/*
 * lists at most 16 offered host types in display order. mime_conv_pick_kind selects a guest format or 0;
 * mime_conv_from_guest converts it. file types encode cut or copy using DROPEFFECT_*; mime_conv_needs_effect
 * identifies types requiring it, with 0 when absent.
 */
size_t mime_conv_offer_types(uint32_t kinds, const char **types);
uint32_t mime_conv_pick_kind(const char *mime, uint32_t offered);
uint8_t *mime_conv_from_guest(const char *mime, uint32_t kind, const void *raw, size_t size, uint32_t effect,
                              size_t *out_size);

bool mime_conv_needs_effect(const char *mime);

/* the percent decoded path of a file: URI on this machine, false for any other uri. */
bool mime_conv_uri_to_path(const char *uri, size_t len, char *out, size_t size);

#endif
