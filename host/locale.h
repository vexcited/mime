/*
 * the locale of the guest's user: what the installation is set to, and what the host's own settings give.
 */
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

/* finds a locale by its name, a language alone giving the main region of it. */
bool mime_locale_find(const char *name, MimeLocale *out);

/* the locale the host's environment names, en-US when it names none that exists. */
void mime_locale_from_host(MimeLocale *out);

/* what a language tag or a name of the C library, such as fr_FR.UTF-8@euro, stands for; the name written as a tag. */
bool mime_locale_tag(const char *text, char *out, unsigned size);

#endif
