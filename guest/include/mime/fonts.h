/*
 * fonts. Windows always has its fonts, a machine of Mime may have none of them: they are an optional download. The
 * open font that ships in the ROM stands in when a requested face is missing, under its own name.
 */
#ifndef MIME_FONTS_H
#define MIME_FONTS_H

/* The family of the open font in the ROM, as the font lookups of GDI, GDI+ and DirectWrite end up with it. */
#define MIME_FALLBACK_FONT L"Noto Sans"

#endif
