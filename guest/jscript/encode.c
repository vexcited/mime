/*
 * encoded scripts: the blocks "#@~^ length == data checksum == ^#~@" that the script encoder makes, which the engine of the
 * class JScript.Encode turns back into text before it reads it.
 */
#include "js.h"

#include "encoder.inc"

/* six characters of base 64 and "==" are four bytes, the first being the lowest. */
static BOOL decode_base64_dword(const WCHAR *s, SIZE_T available, ULONG *out)
{
    if (available < 8 || s[6] != '=' || s[7] != '=')
        return FALSE;
    unsigned long long bits = 0;
    for (int i = 0; i < 6; i++) {
        if (s[i] > 0x7F || g_base64_values[s[i]] == 0xFF)
            return FALSE;
        bits = (bits << 6) | g_base64_values[s[i]];
    }
    /* 36 bits hold 4 bytes and 4 bits that must not be used. */
    ULONG value = 0;
    for (int i = 0; i < 4; i++)
        value |= (ULONG)(((bits >> (28 - 8 * i)) & 0xFF) << (8 * i));
    *out = value;
    return TRUE;
}

/* the text with its encoded blocks replaced, nullptr when there is none. the length is that of the result. */
WCHAR *script_decode(const WCHAR *text, SIZE_T length, SIZE_T *result_length)
{
    BOOL any = FALSE;
    WCHAR *out = js_alloc((length + 1) * sizeof(WCHAR));
    SIZE_T o = 0;
    for (SIZE_T i = 0; i < length;) {
        const WCHAR *s = text + i;
        SIZE_T left = length - i;
        ULONG count, expected;
        if (left >= 24 && s[0] == '#' && s[1] == '@' && s[2] == '~' && s[3] == '^' && decode_base64_dword(s + 4, left - 4, &count) &&
            count && left >= (SIZE_T)count + 24 && s[count + 20] == '^' && s[count + 21] == '#' && s[count + 22] == '~' &&
            s[count + 23] == '@' && decode_base64_dword(s + 12 + count, left - 12 - count, &expected)) {
            SIZE_T mark = o;
            ULONG sum = 0;
            int position = 0;
            BOOL good = TRUE;
            for (const WCHAR *c = s + 12, *end = s + 12 + count; c < end; c++) {
                WCHAR ch = *c;
                if (ch >= 0x80) {
                    out[o++] = ch;
                    continue;
                }
                int v = g_encoder_alphabet[(3 + g_encoder_pattern[position]) * 128 + ch];
                if (v == -1) {
                    if (++c >= end) {
                        good = FALSE;
                        break;
                    }
                    switch (*c) {
                    case '$': v = '@'; break;
                    case '&': v = '\n'; break;
                    case '#': v = '\r'; break;
                    case '!': v = '<'; break;
                    case '*': v = '>'; break;
                    default: v = 0xFFFF; break;
                    }
                }
                sum += (ULONG)(WCHAR)v;
                out[o++] = (WCHAR)v;
                if (++position >= 64)
                    position = 0;
            }
            if (good && sum == expected) {
                any = TRUE;
                i += count + 24;
                continue;
            }
            o = mark;
        }
        out[o++] = text[i++];
    }
    if (!any) {
        js_free(out);
        return nullptr;
    }
    out[o] = 0;
    *result_length = o;
    return out;
}
