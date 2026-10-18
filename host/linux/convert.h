/*
 * what other host programs put on the clipboard or in a drag, and what windows programs expect of the same
 * contents: line ends and encodings of text, device independent bitmaps and png files, the header of "HTML
 * Format", DROPFILES blocks and uri lists. every function returns memory from malloc, null when the input is not
 * of the kind or empty.
 */
#ifndef MIME_LINUX_CONVERT_H
#define MIME_LINUX_CONVERT_H

#include "../mime_host.h"

/* utf-8 text as utf-16le with windows line ends and no terminator. */
uint8_t *mime_conv_text_to_guest(const char *utf8, size_t len, size_t *out_size);
/* utf-16le text, cut at the first nul, as utf-8 with unix line ends and a nul that the length does not count. */
char *mime_conv_text_from_guest(const uint16_t *text, size_t chars, size_t *out_len);

/* a png or bmp file as the bytes of CF_DIB, or CF_DIBV5 when v5, bottom up in 32 bits. */
uint8_t *mime_conv_image_to_dib(const void *file, size_t size, bool v5, size_t *out_size);
/* a png or bmp file as a png file. */
uint8_t *mime_conv_image_to_png(const void *file, size_t size, size_t *out_size);
/* the bytes of CF_DIB or CF_DIBV5 as a png file, or as a bmp file. */
uint8_t *mime_conv_dib_to_png(const void *dib, size_t size, size_t *out_size);
uint8_t *mime_conv_dib_to_bmp(const void *dib, size_t size, size_t *out_size);

/* html in utf-8, or utf-16 with a byte order mark, as "HTML Format". */
uint8_t *mime_conv_html_to_guest(const void *html, size_t size, size_t *out_size);
/* "HTML Format" as the html document in utf-8. */
uint8_t *mime_conv_html_from_guest(const void *data, size_t size, size_t *out_size);

/* the file: uris of a uri list as a DROPFILES block of guest paths; null when no file is on the list. */
uint8_t *mime_conv_uris_to_hdrop(const MimePaths *paths, const char *list, size_t len, size_t *out_size);
/* a DROPFILES block as a uri list with the host paths of the files that have one. */
char *mime_conv_hdrop_to_uris(const MimePaths *paths, const void *data, size_t size, size_t *out_len);
/* the first uri that is not a file as the text of "UniformResourceLocatorW", terminator included. */
uint8_t *mime_conv_uris_to_url(const char *list, size_t len, size_t *out_size);
/* the text of "UniformResourceLocatorW" or "UniformResourceLocator" as a uri list. */
char *mime_conv_url_to_uris(const void *text, size_t size, bool wide, size_t *out_len);

/* the percent decoded path of a file: uri on this machine, false for any other uri. */
bool mime_conv_uri_to_path(const char *uri, size_t len, char *out, size_t size);

#endif
