/*
 * the host's reading of a Windows shortcut (MS-SHLLINK): what the target is, how it is started, how it is described and
 * what its application user model id is. the shell link of the guest is a file of the guest's drive, so the host can
 * read what a program wrote without asking the guest. the format is documented, nothing here is the guest's code.
 */
#include "lnk.h"

#include <stdlib.h>
#include <string.h>

#define LNK_HEADER_SIZE 0x4C
#define FLAG_ID_LIST 0x00000001
#define FLAG_LINK_INFO 0x00000002
#define FLAG_NAME 0x00000004
#define FLAG_RELATIVE_PATH 0x00000008
#define FLAG_WORKING_DIR 0x00000010
#define FLAG_ARGUMENTS 0x00000020
#define FLAG_ICON_LOCATION 0x00000040
#define FLAG_UNICODE 0x00000080

#define BLOCK_ENVIRONMENT 0xA0000001u
#define BLOCK_ICON_ENVIRONMENT 0xA0000007u
#define BLOCK_PROPERTY_STORE 0xA0000009u

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* the text of ANSI bytes, as the code page of the machine that wrote it was a Latin one: bytes above 0x7F become the
 * code points of the same value. */
static void ansi(char *out, size_t size, const uint8_t *p, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < max && p[i] && n + 3 < size; i++) {
        if (p[i] < 0x80) {
            out[n++] = (char)p[i];
        } else {
            out[n++] = (char)(0xC0 | p[i] >> 6);
            out[n++] = (char)(0x80 | (p[i] & 0x3F));
        }
    }
    out[n] = 0;
}

static void wide(char *out, size_t size, const uint8_t *p, size_t chars)
{
    uint16_t buffer[1100];
    if (chars > 1090)
        chars = 1090;
    size_t n = 0;
    while (n < chars && (p[2 * n] || p[2 * n + 1])) {
        buffer[n] = u16(p + 2 * n);
        n++;
    }
    mime_utf16_to_utf8(out, size, buffer, n);
}

/* a counted string of the string data, advancing the offset; false when the file is cut short. */
static bool string_data(const uint8_t *d, size_t size, size_t *at, bool unicode, char *out, size_t out_size)
{
    out[0] = 0;
    if (*at + 2 > size)
        return false;
    size_t count = u16(d + *at);
    *at += 2;
    size_t bytes = count * (unicode ? 2 : 1);
    if (*at + bytes > size)
        return false;
    if (unicode)
        wide(out, out_size, d + *at, count);
    else
        ansi(out, out_size, d + *at, count);
    *at += bytes;
    return true;
}

/* the properties of a serialized property storage: only the application user model id and the toast activator are read. */
static void property_storage(const uint8_t *d, size_t size, MimeLink *link)
{
    static const uint8_t app_user_model[16] = {0x55, 0x28, 0x4C, 0x9F, 0x79, 0x9F, 0x39, 0x4B,
                                               0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3};
    size_t at = 0;
    while (at + 4 <= size) {
        uint32_t storage = u32(d + at);
        if (storage < 28 || at + storage > size)
            return;
        const uint8_t *s = d + at;
        if (!memcmp(s + 4, "1SPS", 4)) {
            bool ours = !memcmp(s + 8, app_user_model, 16);
            size_t p = 24;
            while (p + 4 <= storage) {
                uint32_t value = u32(s + p);
                if (value == 0 || p + value > storage)
                    break;
                if (ours && value >= 13) {
                    uint32_t id = u32(s + p + 4);
                    const uint8_t *v = s + p + 9;
                    uint16_t type = u16(v);
                    if (id == 5 && type == 0x1F && value >= 9 + 4 + 4) {
                        uint32_t chars = u32(v + 4);
                        if (chars && 9 + 8 + (size_t)chars * 2 <= value)
                            wide(link->aumid, sizeof(link->aumid), v + 8, chars);
                    } else if (id == 26 && type == 0x48 && value >= 9 + 4 + 16) {
                        const uint8_t *g = v + 4;
                        snprintf(link->toast_clsid, sizeof(link->toast_clsid),
                                 "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", u32(g), u16(g + 4), u16(g + 6),
                                 g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
                    }
                }
                p += value;
            }
        }
        at += storage;
    }
}

bool mime_lnk_parse(const void *data, size_t size, MimeLink *out)
{
    memset(out, 0, sizeof(*out));
    const uint8_t *d = data;
    static const uint8_t clsid[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
    if (size < LNK_HEADER_SIZE || u32(d) != LNK_HEADER_SIZE || memcmp(d + 4, clsid, 16))
        return false;
    uint32_t flags = u32(d + 20);
    out->show_command = (int)u32(d + 60);
    out->icon_index = (int)u32(d + 56);
    size_t at = LNK_HEADER_SIZE;
    if (flags & FLAG_ID_LIST) {
        if (at + 2 > size)
            return false;
        at += 2 + u16(d + at);
    }
    if (flags & FLAG_LINK_INFO) {
        if (at + 4 > size)
            return false;
        uint32_t info_size = u32(d + at);
        if (info_size < 28 || at + info_size > size)
            return false;
        const uint8_t *li = d + at;
        uint32_t header = u32(li + 4), info_flags = u32(li + 8);
        uint32_t local = u32(li + 16), suffix = u32(li + 24);
        uint32_t local_unicode = header >= 0x24 ? u32(li + 28) : 0, suffix_unicode = header >= 0x24 ? u32(li + 32) : 0;
        if (info_flags & 1) {
            char base[1100] = "", tail[1100] = "";
            if (local_unicode && local_unicode < info_size)
                wide(base, sizeof(base), li + local_unicode, (info_size - local_unicode) / 2);
            else if (local && local < info_size)
                ansi(base, sizeof(base), li + local, info_size - local);
            if (suffix_unicode && suffix_unicode < info_size)
                wide(tail, sizeof(tail), li + suffix_unicode, (info_size - suffix_unicode) / 2);
            else if (suffix && suffix < info_size)
                ansi(tail, sizeof(tail), li + suffix, info_size - suffix);
            size_t n = strlen(base);
            snprintf(out->target, sizeof(out->target), "%s%s%s", base,
                     tail[0] && n && base[n - 1] != '\\' ? "\\" : "", tail);
        }
        at += info_size;
    }
    bool unicode = (flags & FLAG_UNICODE) != 0;
    char relative[1100] = "";
    if (flags & FLAG_NAME)
        string_data(d, size, &at, unicode, out->description, sizeof(out->description));
    if (flags & FLAG_RELATIVE_PATH)
        string_data(d, size, &at, unicode, relative, sizeof(relative));
    if (flags & FLAG_WORKING_DIR)
        string_data(d, size, &at, unicode, out->directory, sizeof(out->directory));
    if (flags & FLAG_ARGUMENTS)
        string_data(d, size, &at, unicode, out->arguments, sizeof(out->arguments));
    if (flags & FLAG_ICON_LOCATION)
        string_data(d, size, &at, unicode, out->icon, sizeof(out->icon));
    /* the blocks of extra data. */
    while (at + 8 <= size) {
        uint32_t block = u32(d + at), signature = u32(d + at + 4);
        if (block < 4 || at + block > size)
            break;
        const uint8_t *b = d + at;
        if (signature == BLOCK_ENVIRONMENT && block >= 8 + 260 + 520) {
            char target[1100];
            wide(target, sizeof(target), b + 8 + 260, 260);
            if (!target[0])
                ansi(target, sizeof(target), b + 8, 260);
            if (target[0]) {
                mime_strlcpy(out->target_raw, target, sizeof(out->target_raw));
            }
        } else if (signature == BLOCK_ICON_ENVIRONMENT && block >= 8 + 260 + 520) {
            char icon[1100];
            wide(icon, sizeof(icon), b + 8 + 260, 260);
            if (icon[0])
                mime_strlcpy(out->icon_raw, icon, sizeof(out->icon_raw));
        } else if (signature == BLOCK_PROPERTY_STORE && block > 8) {
            property_storage(b + 8, block - 8, out);
        }
        at += block;
    }
    if (!out->target[0] && out->target_raw[0])
        mime_strlcpy(out->target, out->target_raw, sizeof(out->target));
    if (!out->target[0] && relative[0])
        mime_strlcpy(out->relative, relative, sizeof(out->relative));
    return out->target[0] || out->relative[0];
}
