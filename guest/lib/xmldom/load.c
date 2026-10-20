/* documents from bytes and from files: encodings, byte order marks, and file locations. */
#include <xmldom/xmldom.h>

/* ------------------------------------------------------------------ decoding */

static const UINT16 cp1252_high[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

/* utf-8 to utf-16: returns the characters or -1 and the byte offset of the first bad sequence. */
static WCHAR *decode_utf8(const BYTE *p, UINT size, UINT *length, UINT *bad)
{
    WCHAR *out = xd_alloc_raw(((SIZE_T)size + 1) * sizeof(WCHAR));
    if (!out)
        return nullptr;
    UINT n = 0, i = 0;
    while (i < size) {
        UINT c = p[i];
        if (c < 0x80) {
            out[n++] = (WCHAR)c;
            i++;
            continue;
        }
        UINT need, min;
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
            c &= 0x1F;
            min = 0x80;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
            c &= 0x0F;
            min = 0x800;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
            c &= 0x07;
            min = 0x10000;
        } else {
            *bad = i;
            xd_free(out);
            return nullptr;
        }
        if (i + need >= size) {
            *bad = i;
            xd_free(out);
            return nullptr;
        }
        for (UINT k = 1; k <= need; k++) {
            if ((p[i + k] & 0xC0) != 0x80) {
                *bad = i;
                xd_free(out);
                return nullptr;
            }
            c = (c << 6) | (p[i + k] & 0x3F);
        }
        if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            *bad = i;
            xd_free(out);
            return nullptr;
        }
        i += need + 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            out[n++] = (WCHAR)(0xD800 + (c >> 10));
            out[n++] = (WCHAR)(0xDC00 + (c & 0x3FF));
        } else {
            out[n++] = (WCHAR)c;
        }
    }
    out[n] = 0;
    *length = n;
    return out;
}

static WCHAR *decode_utf16(const BYTE *p, UINT size, BOOL big, UINT *length)
{
    UINT n = size / 2;
    WCHAR *out = xd_alloc_raw(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!out)
        return nullptr;
    for (UINT i = 0; i < n; i++)
        out[i] = big ? (WCHAR)(p[2 * i] << 8 | p[2 * i + 1]) : (WCHAR)(p[2 * i] | p[2 * i + 1] << 8);
    out[n] = 0;
    *length = n;
    return out;
}

static WCHAR *decode_page(UINT page, const BYTE *p, UINT size, UINT *length)
{
    WCHAR *out = xd_alloc_raw(((SIZE_T)size + 1) * sizeof(WCHAR));
    if (!out)
        return nullptr;
    if (page == 28591 || page == 20127 || page == 1252) {
        for (UINT i = 0; i < size; i++) {
            UINT c = p[i];
            if (page == 1252 && c >= 0x80 && c <= 0x9F)
                c = cp1252_high[c - 0x80];
            else if (page == 20127 && c >= 0x80)
                c = 0xFFFD;
            out[i] = (WCHAR)c;
        }
        out[size] = 0;
        *length = size;
        return out;
    }
    int n = MultiByteToWideChar(page, 0, (LPCCH)p, (int)size, out, (int)size);
    if (n <= 0 && size) {
        xd_free(out);
        return nullptr;
    }
    out[n] = 0;
    *length = (UINT)n;
    return out;
}

/* the encoding named by the declaration at the start of text, as an offset and a length, or false. */
static BOOL declared_encoding(const WCHAR *w, UINT n, UINT *off, UINT *len)
{
    if (n < 6 || memcmp(w, L"<?xml", 10) || !xd_is_space(w[5]))
        return FALSE;
    UINT i = 5;
    while (i + 1 < n && !(w[i] == '?' && w[i + 1] == '>')) {
        if (i + 8 < n && !memcmp(w + i, L"encoding", 16)) {
            UINT j = i + 8;
            while (j < n && xd_is_space(w[j]))
                j++;
            if (j >= n || w[j] != '=')
                return FALSE;
            j++;
            while (j < n && xd_is_space(w[j]))
                j++;
            if (j >= n || (w[j] != '"' && w[j] != '\''))
                return FALSE;
            WCHAR q = w[j++];
            UINT start = j;
            while (j < n && w[j] != q)
                j++;
            if (j >= n)
                return FALSE;
            *off = start;
            *len = j - start;
            return TRUE;
        }
        i++;
    }
    return FALSE;
}

static BOOL is_utf16_name(const WCHAR *name, UINT len)
{
    WCHAR copy[24];
    if (len >= 24)
        return FALSE;
    memcpy(copy, name, (SIZE_T)len * sizeof(WCHAR));
    copy[len] = 0;
    UINT page = xd_code_page(copy);
    return page == 1200 || page == 1201;
}

static BOOL encoding_error(XDoc *doc, UINT code, const WCHAR *text, UINT n, UINT off)
{
    LONG line, linepos;
    xd_line_info(text, n, off, &line, &linepos);
    xd_err_set(doc, code, line, linepos, (LONG)off, text, n < 200 ? n : 200, nullptr, nullptr);
    return FALSE;
}

BOOL xd_parse_bytes(XDoc *doc, const BYTE *data, UINT size)
{
    WCHAR *text = nullptr;
    UINT length = 0;
    UINT bad = 0;
    enum { UTF8, UTF16LE, UTF16BE } kind = UTF8;
    BOOL bom = FALSE;
    const BYTE *p = data;
    UINT n = size;
    xd_err_clear(doc);

    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        kind = UTF16LE;
        bom = TRUE;
        p += 2;
        n -= 2;
    } else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        kind = UTF16BE;
        bom = TRUE;
        p += 2;
        n -= 2;
    } else if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        bom = TRUE;
        p += 3;
        n -= 3;
    } else if (n >= 4 && p[0] == 0 && p[1] == '<' && p[2] == 0 && p[3] == '?') {
        kind = UTF16BE;
    } else if (n >= 4 && p[0] == '<' && p[1] == 0 && p[2] == '?' && p[3] == 0) {
        kind = UTF16LE;
    }

    if (kind != UTF8) {
        text = decode_utf16(p, n, kind == UTF16BE, &length);
        if (!text) {
            xd_err_set(doc, (UINT)E_OUTOFMEMORY, 0, 0, 0, nullptr, 0, nullptr, nullptr);
            return FALSE;
        }
        UINT off, len;
        if (declared_encoding(text, length, &off, &len) && !is_utf16_name(text + off, len)) {
            BOOL r = encoding_error(doc, XE_SWITCH_ENCODING, text, length, off);
            xd_free(text);
            xd_clear_children(&doc->node);
            return r;
        }
    } else {
        /* the declaration is ascii whatever the encoding, so it can be read from the bytes. */
        WCHAR head[256];
        UINT hn = n < 255 ? n : 255;
        for (UINT i = 0; i < hn; i++)
            head[i] = p[i];
        UINT off, len;
        UINT page = 65001;
        if (declared_encoding(head, hn, &off, &len)) {
            WCHAR name[40];
            if (len >= 40) {
                return encoding_error(doc, XE_BAD_ENCODING, head, hn, off);
            }
            memcpy(name, head + off, (SIZE_T)len * sizeof(WCHAR));
            name[len] = 0;
            page = xd_code_page(name);
            if (!page)
                return encoding_error(doc, XE_BAD_ENCODING, head, hn, off);
            if ((page == 1200 || page == 1201) || (bom && page != 65001))
                return encoding_error(doc, XE_SWITCH_ENCODING, head, hn, off);
        }
        if (page == 65001)
            text = decode_utf8(p, n, &length, &bad);
        else
            text = decode_page(page, p, n, &length);
        if (!text) {
            if (n && bad < n) {
                /* the character number of the bad sequence. */
                UINT at = 0;
                for (UINT i = 0; i < bad; i++)
                    if ((p[i] & 0xC0) != 0x80)
                        at++;
                LONG line = 1, linepos = 1;
                for (UINT i = 0; i < bad; i++) {
                    if (p[i] == '\n') {
                        line++;
                        linepos = 1;
                    } else if ((p[i] & 0xC0) != 0x80) {
                        linepos++;
                    }
                }
                xd_err_set(doc, XE_TEXT_CHAR, line, linepos, (LONG)at, nullptr, 0, nullptr, nullptr);
                return FALSE;
            }
            xd_err_set(doc, (UINT)E_OUTOFMEMORY, 0, 0, 0, nullptr, 0, nullptr, nullptr);
            return FALSE;
        }
    }
    BOOL ok = xd_parse(doc, text, length, 0);
    xd_free(text);
    return ok;
}

/* ------------------------------------------------------------------ locations */

static int hex_digit(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
        return (c | 0x20) - 'a' + 10;
    return -1;
}

static BOOL has_prefix(const WCHAR *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        WCHAR c = *s;
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c != (UCHAR)*prefix)
            return FALSE;
    }
    return TRUE;
}

/* the path of a file: location, or null when the location is some other kind of url. */
WCHAR *xd_location_to_path(const WCHAR *location, BOOL *other)
{
    *other = FALSE;
    if (has_prefix(location, "file:")) {
        const WCHAR *p = location + 5;
        while (*p == '/')
            p++;
        /* "file:///C:/x" has the drive after the slashes, "file://host/share/x" has a server. */
        UINT slashes = (UINT)(p - (location + 5));
        BOOL drive = p[0] && p[1] == ':';
        XBuf out = {};
        if (!drive && slashes == 2) {
            xb_puta(&out, "\\\\");
        } else if (!drive && slashes >= 1) {
            xb_putc(&out, '\\');
        }
        /* percent escapes are utf-8 bytes. */
        BYTE bytes[4096];
        UINT nb = 0;
        for (; *p && *p != '#' && *p != '?'; p++) {
            if (*p == '%' && hex_digit(p[1]) >= 0 && hex_digit(p[2]) >= 0 && nb < sizeof(bytes) - 1) {
                bytes[nb++] = (BYTE)(hex_digit(p[1]) * 16 + hex_digit(p[2]));
                p += 2;
                continue;
            }
            if (nb) {
                UINT len = 0, bad = 0;
                WCHAR *w = decode_utf8(bytes, nb, &len, &bad);
                if (w) {
                    xb_put(&out, w, len);
                    xd_free(w);
                }
                nb = 0;
            }
            xb_putc(&out, *p == '/' ? '\\' : *p);
        }
        if (nb) {
            UINT len = 0, bad = 0;
            WCHAR *w = decode_utf8(bytes, nb, &len, &bad);
            if (w) {
                xb_put(&out, w, len);
                xd_free(w);
            }
        }
        if (out.failed) {
            xb_free(&out);
            return nullptr;
        }
        return out.s ? out.s : xd_wcsdup(L"", 0);
    }
    for (const WCHAR *p = location; *p; p++) {
        if (*p == ':') {
            if (p - location > 1)
                *other = TRUE;
            break;
        }
        if (*p == '\\' || *p == '/')
            break;
    }
    if (*other)
        return nullptr;
    return xd_wcsdup(location, (UINT)-1);
}

/* the url of a path, as the dom reports it. */
static WCHAR *path_to_url(const WCHAR *path)
{
    XBuf out = {};
    xb_puta(&out, "file:///");
    const WCHAR *p = path;
    if (p[0] == '\\' && p[1] == '\\') {
        out.length = 5;
        xb_puta(&out, "//");
        p += 2;
    }
    for (; *p; p++) {
        WCHAR c = *p;
        if (c == '\\')
            xb_putc(&out, '/');
        else if (c == ' ')
            xb_puta(&out, "%20");
        else if (c == '%')
            xb_puta(&out, "%25");
        else if (c == '#')
            xb_puta(&out, "%23");
        else
            xb_putc(&out, c);
    }
    if (out.failed) {
        xb_free(&out);
        return nullptr;
    }
    return out.s;
}

static void file_error(XDoc *doc, DWORD err, const WCHAR *url)
{
    UINT code;
    switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_PATHNAME:
        code = XE_FILE_NOT_FOUND;
        break;
    default:
        code = err ? (0x80070000u | (err & 0xFFFF)) : (UINT)E_FAIL;
    }
    const WCHAR *reason = xd_system_message(code);
    xd_err_set_reason(doc, code, reason, url);
}

BOOL xd_load_file(XDoc *doc, const WCHAR *location)
{
    xd_err_clear(doc);
    xd_clear_children(&doc->node);
    BOOL other;
    WCHAR *path = xd_location_to_path(location, &other);
    xd_free(doc->url);
    doc->url = nullptr;
    if (other) {
        doc->url = xd_wcsdup(location, (UINT)-1);
        /* urls of other kinds, such as http, are not fetched. */
        xd_err_set_reason(doc, 0x800C0005u, xd_message(0x800C0005u), doc->url);
        return FALSE;
    }
    if (!path) {
        xd_err_set(doc, (UINT)E_OUTOFMEMORY, 0, 0, 0, nullptr, 0, nullptr, nullptr);
        return FALSE;
    }
    WCHAR full[MAX_PATH * 4];
    DWORD fl = GetFullPathNameW(path, MAX_PATH * 4, full, nullptr);
    const WCHAR *use = fl && fl < MAX_PATH * 4 ? full : path;
    doc->url = path_to_url(use);
    HANDLE file = CreateFileW(use, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    BOOL ok = FALSE;
    if (file == INVALID_HANDLE_VALUE) {
        file_error(doc, GetLastError(), doc->url);
    } else {
        LARGE_INTEGER size;
        if (!GetFileSizeEx(file, &size) || size.QuadPart > 0x7FFFFFF0) {
            file_error(doc, ERROR_FILE_TOO_LARGE, doc->url);
        } else {
            BYTE *data = xd_alloc_raw((SIZE_T)size.QuadPart + 1);
            if (!data) {
                xd_err_set(doc, (UINT)E_OUTOFMEMORY, 0, 0, 0, nullptr, 0, nullptr, nullptr);
            } else {
                UINT total = 0;
                BOOL read_ok = TRUE;
                while (total < (UINT)size.QuadPart) {
                    DWORD got = 0;
                    if (!ReadFile(file, data + total, (DWORD)((UINT)size.QuadPart - total), &got, nullptr) || !got) {
                        read_ok = got == 0 && GetLastError() == 0;
                        break;
                    }
                    total += got;
                }
                if (read_ok || total) {
                    ok = xd_parse_bytes(doc, data, total);
                } else {
                    file_error(doc, GetLastError(), doc->url);
                }
                xd_free(data);
            }
        }
        CloseHandle(file);
    }
    /* parse errors carry the url of the document. */
    if (!ok && doc->err.code && !doc->err.url && doc->url)
        doc->err.url = xd_wcsdup(doc->url, (UINT)-1);
    xd_free(path);
    return ok;
}
