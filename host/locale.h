/* guest user locale from installation settings and the host environment. */
#ifndef MIME_LOCALE_H
#define MIME_LOCALE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct MimeLocale {
    char name[32]; /* the name of the locale, such as fr-FR. */
    uint32_t lcid;
    uint16_t ansi_code_page;
    uint16_t oem_code_page;
} MimeLocale;

/* looks up a locale; a language alone selects its main region. */
bool mime_locale_find(const char *name, MimeLocale *out);

/* host environment locale, defaulting to en-US if unsupported. */
void mime_locale_from_host(MimeLocale *out);

/* normalizes language tags and C library names such as fr_FR.UTF-8@euro to a tag. */
bool mime_locale_tag(const char *text, char *out, unsigned size);

#endif
