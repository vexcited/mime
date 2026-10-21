#include "locale.h"
#include "mime_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nls_hostlocales.h"

bool mime_locale_find(const char *name, MimeLocale *out)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_host_locales); i++) {
        if (strcasecmp(g_host_locales[i].Name, name))
            continue;
        mime_strlcpy(out->name, g_host_locales[i].Specific, sizeof(out->name));
        out->lcid = g_host_locales[i].Lcid;
        out->ansi_code_page = g_host_locales[i].Ansi;
        out->oem_code_page = g_host_locales[i].Oem;
        return true;
    }
    return false;
}

/* the modifiers of the C library that name a script, and the ones of the locales of the other scripts. */
static const char *script_of_modifier(const char *modifier)
{
    static const struct {
        const char *Modifier, *Script;
    } scripts[] = {{"latin", "Latn"}, {"cyrillic", "Cyrl"}, {"devanagari", "Deva"}, {"arabic", "Arab"}};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(scripts); i++)
        if (!strcasecmp(modifier, scripts[i].Modifier))
            return scripts[i].Script;
    return nullptr;
}

bool mime_locale_tag(const char *text, char *out, unsigned size)
{
    char buffer[64];
    mime_strlcpy(buffer, text, sizeof(buffer));
    const char *script = nullptr;
    char *at = strchr(buffer, '@');
    if (at) {
        *at = 0;
        script = script_of_modifier(at + 1);
    }
    char *dot = strchr(buffer, '.');
    if (dot)
        *dot = 0;
    if (!buffer[0] || !strcmp(buffer, "C") || !strcmp(buffer, "POSIX"))
        return false;
    for (char *c = buffer; *c; c++)
        if (*c == '_')
            *c = '-';
    char *dash = strchr(buffer, '-');
    if (script && dash) {
        *dash = 0;
        snprintf(out, size, "%s-%s-%s", buffer, script, dash + 1);
    } else if (script) {
        snprintf(out, size, "%s-%s", buffer, script);
    } else {
        mime_strlcpy(out, buffer, size);
    }
    return true;
}

/* a name, or the language of it when the name has no locale of its own. */
static bool find_loosely(const char *tag, MimeLocale *out)
{
    char name[64];
    mime_strlcpy(name, tag, sizeof(name));
    for (;;) {
        if (mime_locale_find(name, out))
            return true;
        char *dash = strrchr(name, '-');
        if (!dash)
            return false;
        *dash = 0;
    }
}

void mime_locale_from_host(MimeLocale *out)
{
    static const char *const variables[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(variables); i++) {
        const char *value = getenv(variables[i]);
        if (!value || !value[0])
            continue;
        char tag[64];
        /* the first variable that is set decides, even when it names no locale of the guest. */
        if (mime_locale_tag(value, tag, sizeof(tag)) && find_loosely(tag, out))
            return;
        break;
    }
    mime_locale_find("en-US", out);
}
