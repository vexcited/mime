/* a pull parser for the xml of component manifests: utf-8, no dtd, entities decoded, prefixes dropped. */
#include "cbs.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

int CbsXmlOpen(CBS_XML *xml, const char *text, size_t length)
{
    memset(xml, 0, sizeof(*xml));
    if (length >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) {
        text += 3;
        length -= 3;
    }
    xml->Cursor = text;
    xml->End = text + length;
    return CBS_OK;
}

void CbsXmlClose(CBS_XML *xml)
{
    CbsFree(xml->Buffer);
    xml->Buffer = nullptr;
    xml->BufferSize = 0;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const char *find(const char *p, const char *end, const char *what)
{
    size_t n = strlen(what);
    for (; p + n <= end; p++)
        if (*p == *what && !memcmp(p, what, n))
            return p;
    return nullptr;
}

static size_t encode_utf8(uint32_t c, char *out)
{
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | c >> 6);
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        out[0] = (char)(0xE0 | c >> 12);
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | c >> 18);
    out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

static int digit_value(char d)
{
    if (d >= '0' && d <= '9')
        return d - '0';
    if (d >= 'a' && d <= 'f')
        return d - 'a' + 10;
    if (d >= 'A' && d <= 'F')
        return d - 'A' + 10;
    return 16;
}

/* the character an entity of n characters stands for, written to out; returns its length or 0 when malformed. */
static size_t entity(const char *e, size_t n, char *out)
{
    if (n == 2 && !memcmp(e, "lt", 2))
        return *out = '<', 1;
    if (n == 2 && !memcmp(e, "gt", 2))
        return *out = '>', 1;
    if (n == 3 && !memcmp(e, "amp", 3))
        return *out = '&', 1;
    if (n == 4 && !memcmp(e, "quot", 4))
        return *out = '"', 1;
    if (n == 4 && !memcmp(e, "apos", 4))
        return *out = '\'', 1;
    if (n < 2 || e[0] != '#')
        return 0;
    uint32_t c = 0;
    if (e[1] == 'x' || e[1] == 'X') {
        if (n < 3)
            return 0;
        for (size_t i = 2; i < n; i++) {
            int v = digit_value(e[i]);
            if (v == 16)
                return 0;
            c = c << 4 | (uint32_t)v;
        }
    } else {
        for (size_t i = 1; i < n; i++) {
            if (e[i] < '0' || e[i] > '9')
                return 0;
            c = c * 10 + (uint32_t)(e[i] - '0');
        }
    }
    if (c == 0 || c > 0x10FFFF)
        return 0;
    return encode_utf8(c, out);
}

/* decodes the entities of the text from p to end into out and terminates it; null when one is malformed. */
static char *decode(const char *p, const char *end, char *out)
{
    while (p < end) {
        if (*p != '&') {
            *out++ = *p++;
            continue;
        }
        const char *semi = p + 1;
        while (semi < end && *semi != ';' && semi - p < 12)
            semi++;
        if (semi >= end || *semi != ';')
            return nullptr;
        size_t n = entity(p + 1, (size_t)(semi - p - 1), out);
        if (!n)
            return nullptr;
        out += n;
        p = semi + 1;
    }
    *out++ = 0;
    return out;
}

static bool grow(CBS_XML *xml, size_t need)
{
    if (xml->BufferSize >= need)
        return true;
    char *buffer = CbsAlloc(need);
    if (!buffer)
        return false;
    CbsFree(xml->Buffer);
    xml->Buffer = buffer;
    xml->BufferSize = need;
    return true;
}

static const char *local_name(const char *name)
{
    const char *colon = name;
    for (const char *p = name; *p; p++)
        if (*p == ':')
            colon = p + 1;
    return colon;
}

/* parses the tag from p to end, after its '<', that opens an element. */
static CBS_XML_EVENT start_tag(CBS_XML *xml, const char *p, const char *end)
{
    char *out = xml->Buffer;
    const char *n = p;
    while (p < end && !is_space(*p) && *p != '/')
        p++;
    if (p == n)
        return CBS_XML_ERROR;
    memcpy(out, n, (size_t)(p - n));
    out[p - n] = 0;
    xml->Name = local_name(out);
    out += p - n + 1;
    xml->AttributeCount = 0;
    bool empty = false;
    for (;;) {
        while (p < end && is_space(*p))
            p++;
        if (p >= end)
            break;
        if (*p == '/') {
            empty = true;
            break;
        }
        n = p;
        while (p < end && *p != '=' && !is_space(*p))
            p++;
        const char *name_end = p;
        while (p < end && is_space(*p))
            p++;
        if (p >= end || *p != '=' || name_end == n)
            return CBS_XML_ERROR;
        p++;
        while (p < end && is_space(*p))
            p++;
        if (p >= end || (*p != '"' && *p != '\''))
            return CBS_XML_ERROR;
        char quote = *p++;
        const char *value = p;
        while (p < end && *p != quote)
            p++;
        if (p >= end)
            return CBS_XML_ERROR;
        const char *value_end = p++;
        bool namespace_declaration = name_end - n >= 5 && !memcmp(n, "xmlns", 5) &&
                                     (name_end - n == 5 || n[5] == ':');
        if (namespace_declaration || xml->AttributeCount >= CBS_XML_MAX_ATTRIBUTES)
            continue;
        char *name_out = out;
        memcpy(out, n, (size_t)(name_end - n));
        out[name_end - n] = 0;
        out += name_end - n + 1;
        char *value_out = out;
        out = decode(value, value_end, out);
        if (!out)
            return CBS_XML_ERROR;
        xml->AttributeName[xml->AttributeCount] = local_name(name_out);
        xml->AttributeValue[xml->AttributeCount] = value_out;
        xml->AttributeCount++;
    }
    xml->Depth = ++xml->Open;
    xml->PendingClose = empty;
    return CBS_XML_START;
}

CBS_XML_EVENT CbsXmlNext(CBS_XML *xml)
{
    if (xml->PendingClose) {
        xml->PendingClose = false;
        xml->AttributeCount = 0;
        xml->Depth = xml->Open--;
        return CBS_XML_CLOSE;
    }
    for (;;) {
        const char *p = xml->Cursor, *end = xml->End;
        const char *text = p;
        while (p < end && *p != '<')
            p++;
        bool visible = false;
        for (const char *t = text; t < p && !visible; t++)
            visible = !is_space(*t);
        if (visible && xml->Open) {
            xml->Cursor = p;
            if (!grow(xml, (size_t)(p - text) + 1))
                return CBS_XML_ERROR;
            if (!decode(text, p, xml->Buffer))
                return CBS_XML_ERROR;
            /* surrounding white space does not belong to a value. */
            char *start = xml->Buffer;
            while (is_space(*start))
                start++;
            char *stop = start + strlen(start);
            while (stop > start && is_space(stop[-1]))
                *--stop = 0;
            xml->Text = start;
            xml->Depth = xml->Open;
            xml->AttributeCount = 0;
            return CBS_XML_TEXT;
        }
        if (p >= end) {
            xml->Cursor = p;
            return xml->Open ? CBS_XML_ERROR : CBS_XML_END;
        }
        const char *q;
        if (end - p >= 4 && !memcmp(p, "<!--", 4)) {
            q = find(p + 4, end, "-->");
            if (!q)
                return CBS_XML_ERROR;
            xml->Cursor = q + 3;
            continue;
        }
        if (end - p >= 9 && !memcmp(p, "<![CDATA[", 9)) {
            q = find(p + 9, end, "]]>");
            if (!q)
                return CBS_XML_ERROR;
            xml->Cursor = q + 3;
            continue;
        }
        if (end - p >= 2 && p[1] == '?') {
            q = find(p + 2, end, "?>");
            if (!q)
                return CBS_XML_ERROR;
            xml->Cursor = q + 2;
            continue;
        }
        /* the end of the tag is the first '>' outside of quotes. */
        q = p + 1;
        char quote = 0;
        while (q < end) {
            if (quote) {
                if (*q == quote)
                    quote = 0;
            } else if (*q == '"' || *q == '\'')
                quote = *q;
            else if (*q == '>')
                break;
            q++;
        }
        if (q >= end)
            return CBS_XML_ERROR;
        xml->Cursor = q + 1;
        if (p[1] == '!')
            continue;
        size_t length = (size_t)(q - p);
        if (!grow(xml, length * 2 + 4 * CBS_XML_MAX_ATTRIBUTES + 64))
            return CBS_XML_ERROR;
        if (p[1] == '/') {
            if (!xml->Open)
                return CBS_XML_ERROR;
            const char *n = p + 2;
            const char *e = q;
            while (e > n && is_space(e[-1]))
                e--;
            memcpy(xml->Buffer, n, (size_t)(e - n));
            xml->Buffer[e - n] = 0;
            xml->Name = local_name(xml->Buffer);
            xml->AttributeCount = 0;
            xml->Depth = xml->Open--;
            return CBS_XML_CLOSE;
        }
        return start_tag(xml, p + 1, q);
    }
}

const char *CbsXmlAttribute(const CBS_XML *xml, const char *name)
{
    for (unsigned i = 0; i < xml->AttributeCount; i++)
        if (!strcmp(xml->AttributeName[i], name))
            return xml->AttributeValue[i];
    return nullptr;
}
