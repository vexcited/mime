#include "regfile.h"

#include <stdbool.h>

#define CHUNK 0x10000u
#define NAME_START 1024u

static void *copy_memory(void *dst, const void *src, size_t n)
{
    return __builtin_memcpy(dst, src, n);
}

static size_t length_of(const MimeRegChar *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* ------------------------------------------------------------------ the character stream */

typedef struct Buffer {
    MimeRegChar *Data;
    size_t Count, Capacity;
} Buffer;

typedef struct Reader {
    const MimeRegImportHost *Host;
    MimeRegImportStatus Status;
    int Mode; /* 0 ANSI, 1 UTF-16, 2 UTF-8 */
    int Eof;
    uint32_t Carry, Position, Length;
    uint32_t Version; /* 0x500, 0x400 or 0x310. */
    uint8_t Raw[CHUNK + 8];
    MimeRegChar Wide[CHUNK + 8];
} Reader;

/* a failure replaces what was reported before, in the order the file is read. */
static void fail(Reader *r, MimeRegImportStatus status)
{
    r->Status = status;
}

static void decode_utf16(Reader *r, uint32_t bytes)
{
    uint32_t units = bytes / 2;
    for (uint32_t i = 0; i < units; i++)
        r->Wide[i] = (MimeRegChar)(r->Raw[2 * i] | (r->Raw[2 * i + 1] << 8));
    r->Length = units;
    r->Carry = bytes & 1;
    if (r->Carry)
        r->Raw[0] = r->Raw[bytes - 1];
}

static void decode_utf8(Reader *r, uint32_t bytes)
{
    uint32_t i = 0, n = 0;
    while (i < bytes) {
        uint8_t c = r->Raw[i];
        uint32_t need = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 99;
        if (need != 0 && need != 99 && i + need >= bytes)
            break; /* the sequence goes on in the next block. */
        uint32_t code;
        if (need == 0) {
            code = c;
            i++;
        } else if (need == 99) {
            code = 0xFFFD;
            i++;
        } else {
            code = c & (0x3F >> need);
            uint32_t k = 1;
            for (; k <= need; k++) {
                uint8_t d = r->Raw[i + k];
                if ((d & 0xC0) != 0x80)
                    break;
                code = (code << 6) | (d & 0x3F);
            }
            if (k <= need) {
                code = 0xFFFD;
                i += k;
            } else {
                static const uint32_t least[4] = {0, 0x80, 0x800, 0x10000};
                i += need + 1;
                if (code < least[need] || code > 0x10FFFF || (code >= 0xD800 && code < 0xE000))
                    code = 0xFFFD;
            }
        }
        if (code >= 0x10000) {
            code -= 0x10000;
            r->Wide[n++] = (MimeRegChar)(0xD800 + (code >> 10));
            r->Wide[n++] = (MimeRegChar)(0xDC00 + (code & 0x3FF));
        } else {
            r->Wide[n++] = (MimeRegChar)code;
        }
    }
    r->Length = n;
    r->Carry = bytes - i;
    for (uint32_t k = 0; k < r->Carry; k++)
        r->Raw[k] = r->Raw[i + k];
}

/* the next block of text; returns 0 at the end of the file or when it cannot be read. */
static bool refill(Reader *r)
{
    const MimeRegImportHost *host = r->Host;
    r->Position = r->Length = 0;
    while (!r->Eof && r->Length == 0) {
        long n = host->Read(host->Context, r->Raw + r->Carry, CHUNK);
        if (n < 0) {
            fail(r, MIME_REG_READ_FAILED);
            r->Eof = 1;
            break;
        }
        if (n == 0) {
            r->Eof = 1;
            break;
        }
        uint32_t bytes = r->Carry + (uint32_t)n;
        if (r->Mode == 1) {
            decode_utf16(r, bytes);
        } else if (r->Mode == 2) {
            decode_utf8(r, bytes);
        } else {
            r->Length = host->AnsiToWide(host->Context, (const char *)r->Raw, bytes, r->Wide, CHUNK);
            r->Carry = 0;
            if (r->Length == 0)
                r->Eof = 1;
        }
    }
    return r->Length != 0;
}

static bool get_char(Reader *r, MimeRegChar *c)
{
    if (r->Position == r->Length && !refill(r))
        return false;
    *c = r->Wide[r->Position++];
    return true;
}

static void unget_char(Reader *r)
{
    r->Position--;
}

/* a character that is the expected one is read, any other stays. */
static bool match(Reader *r, MimeRegChar expected)
{
    MimeRegChar c;
    if (!get_char(r, &c))
        return false;
    if (c == expected)
        return true;
    unget_char(r);
    return false;
}

static bool is_newline(MimeRegChar c)
{
    return c == 10 || c == 13;
}

static bool is_space(MimeRegChar c)
{
    return c == 32 || c == 9;
}

static void skip_whitespace(Reader *r)
{
    MimeRegChar c;
    while (get_char(r, &c)) {
        if (!is_space(c)) {
            unget_char(r);
            return;
        }
    }
}

/* to the end of the line, then over the line ends that follow. a line end read before drops the next line too. */
static void skip_line(Reader *r)
{
    MimeRegChar c;
    while (get_char(r, &c) && !is_newline(c))
        ;
    while (get_char(r, &c)) {
        if (!is_newline(c)) {
            unget_char(r);
            return;
        }
    }
}

/* whitespace and a comment up to a line end, and the empty lines and comments after it. false when text comes first. */
static bool parse_end_of_line(Reader *r)
{
    bool newline = false, clean = true;
    bool comment;
    MimeRegChar c;
again:
    comment = false;
    while (get_char(r, &c)) {
        if (is_space(c))
            continue;
        if (is_newline(c)) {
            newline = true;
            goto again;
        }
        if (c == ';') {
            comment = true;
        } else if (!comment) {
            unget_char(r);
            clean = false;
            break;
        }
    }
    return newline || clean;
}

static bool hex_digit(Reader *r, uint8_t *value)
{
    MimeRegChar c;
    if (!get_char(r, &c))
        return false;
    if (c >= '0' && c <= '9')
        *value = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f')
        *value = (uint8_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
        *value = (uint8_t)(c - 'A' + 10);
    else {
        unget_char(r);
        return false;
    }
    return true;
}

/* up to eight digits, at least one. */
static bool parse_dword(Reader *r, uint32_t *value)
{
    uint32_t v = 0;
    uint8_t d;
    int n = 0;
    while (n < 8 && hex_digit(r, &d)) {
        v = (v << 4) | d;
        n++;
    }
    *value = v;
    return n != 0;
}

/* ------------------------------------------------------------------ growing buffers */

static bool reserve(Reader *r, Buffer *b, size_t units)
{
    if (b->Count + units <= b->Capacity)
        return true;
    size_t capacity = b->Capacity ? b->Capacity : NAME_START;
    while (capacity < b->Count + units)
        capacity *= 2;
    MimeRegChar *data = r->Host->Realloc(r->Host->Context, b->Data, capacity * sizeof(MimeRegChar));
    if (!data)
        return false;
    b->Data = data;
    b->Capacity = capacity;
    return true;
}

static void release(Reader *r, Buffer *b)
{
    if (b->Data)
        r->Host->Realloc(r->Host->Context, b->Data, 0);
    b->Data = nullptr;
    b->Count = b->Capacity = 0;
}

/* the text of a string up to its closing quote, the opening quote read. returns false when the string is broken. */
static bool parse_string(Reader *r, Buffer *out)
{
    MimeRegChar c;
    out->Count = 0;
    while (get_char(r, &c)) {
        if (!reserve(r, out, 1))
            return false;
        if (c == '"') {
            out->Data[out->Count] = 0;
            return true;
        }
        if (c == '\\') {
            if (!get_char(r, &c))
                return false;
            if (c == '"' || c == '\\') {
                /* kept as it is. */
            } else if (c == 'n' && r->Host->MultiLineStrings) {
                c = 10;
            } else if (c == 'r' && r->Host->MultiLineStrings) {
                c = 13;
            } else {
                return false;
            }
        } else if (is_newline(c)) {
            return false;
        }
        out->Data[out->Count++] = c;
    }
    return false;
}

/* the bytes of a list of hex digits separated by commas, which a backslash and a line end continue. */
static bool parse_hex_list(Reader *r, uint8_t **bytes, uint32_t *count)
{
    size_t capacity = 256, used = 0;
    uint8_t *data = r->Host->Realloc(r->Host->Context, nullptr, capacity);
    if (!data)
        return false;
    bool ok = true;
    for (;;) {
        skip_whitespace(r);
        if (match(r, '\\') && !parse_end_of_line(r)) {
            ok = false;
            break;
        }
        skip_whitespace(r);
        uint8_t first, second;
        if (!hex_digit(r, &first))
            break;
        if (hex_digit(r, &second))
            first = (uint8_t)((first << 4) | second);
        if (used == capacity) {
            uint8_t *bigger = r->Host->Realloc(r->Host->Context, data, capacity * 2);
            if (!bigger) {
                ok = false;
                break;
            }
            data = bigger;
            capacity *= 2;
        }
        data[used++] = first;
        skip_whitespace(r);
        if (!match(r, ','))
            break;
    }
    *bytes = data;
    *count = (uint32_t)used;
    return ok;
}

/* ------------------------------------------------------------------ the 5.00 and 4.0 formats */

typedef struct KeyState {
    void *Key;
    bool Open;
} KeyState;

static bool match_text(Reader *r, const char *text)
{
    bool all = true;
    for (; *text; text++)
        if (!match(r, (MimeRegChar)*text))
            all = false;
    return all;
}

/* a value of the open key; the name was read. any failure drops the rest of the line and writes nothing. */
static void parse_value(Reader *r, void *key, const MimeRegChar *name)
{
    const MimeRegImportHost *host = r->Host;
    bool ok = true;
    uint32_t type = 1, size = 0, number = 0;
    uint8_t *bytes = nullptr;
    Buffer text = {};

    skip_whitespace(r);
    if (!match(r, '=')) {
        skip_line(r);
        return;
    }
    skip_whitespace(r);
    if (match(r, '"')) {
        ok = parse_string(r, &text) && parse_end_of_line(r);
        bytes = (uint8_t *)text.Data;
        size = (uint32_t)((text.Count + 1) * sizeof(MimeRegChar));
    } else if (match(r, 'd')) {
        type = 4;
        size = 4;
        ok = match_text(r, "word:");
        if (ok) {
            skip_whitespace(r);
            ok = parse_dword(r, &number) && parse_end_of_line(r);
        }
    } else if (match(r, '-')) {
        if (parse_end_of_line(r))
            host->DeleteValue(host->Context, key, name);
        else
            skip_line(r);
        return;
    } else {
        type = 3;
        ok = match_text(r, "hex");
        if (ok && match(r, '('))
            ok = parse_dword(r, &type) && match(r, ')');
        if (ok)
            ok = match(r, ':') && parse_hex_list(r, &bytes, &size) && parse_end_of_line(r);
    }
    if (ok) {
        uint8_t dword[4];
        const void *data = bytes;
        if (!bytes) {
            for (int i = 0; i < 4; i++)
                dword[i] = (uint8_t)(number >> (8 * i));
            data = dword;
        }
        int ansi = r->Version < 0x500 && (type == 2 || type == 7);
        if (host->SetValue(host->Context, key, name, type, data, size, ansi) != 0)
            fail(r, MIME_REG_VALUE_FAILED);
    }
    if (bytes && bytes != (uint8_t *)text.Data)
        host->Realloc(host->Context, bytes, 0);
    release(r, &text);
    if (!ok)
        skip_line(r);
}

static MimeRegChar upper(MimeRegChar c)
{
    return c >= 'a' && c <= 'z' ? (MimeRegChar)(c - 32) : c;
}

/* the index of a root name, case ignored, or -1. */
static int find_root(const MimeRegChar *name, size_t length)
{
    static const char *const names[MIME_REG_ROOTS] = {"HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE",
                                                      "HKEY_USERS", "HKEY_CURRENT_CONFIG"};
    for (int i = 0; i < MIME_REG_ROOTS; i++) {
        const char *n = names[i];
        size_t k = 0;
        while (k < length && n[k] && upper(name[k]) == (MimeRegChar)n[k])
            k++;
        if (k == length && !n[k])
            return i;
    }
    return -1;
}

/* the line of a key, after its bracket. the key that was open is closed before. */
static void parse_header(Reader *r, KeyState *state)
{
    const MimeRegImportHost *host = r->Host;
    MimeRegChar c;
    bool deleting = false;
    if (!get_char(r, &c))
        return;
    if (c == '-') {
        if (!get_char(r, &c))
            return;
        deleting = true;
    }
    uint32_t limit = host->KeyNameLimit;
    Buffer name = {};
    if (!reserve(r, &name, limit + 1))
        return;
    uint32_t count = 0, last = 0xFFFFFFFFu;
    while (!is_newline(c)) {
        name.Data[count] = c;
        if (c == ']')
            last = count;
        count++;
        if (count == limit) {
            for (;;) {
                if (c == ']')
                    last = 0xFFFFFFFFu;
                if (is_newline(c) || !get_char(r, &c))
                    break;
            }
            break;
        }
        if (!get_char(r, &c))
            break;
    }
    if (last < limit) {
        name.Data[last] = 0;
        size_t length = length_of(name.Data);
        size_t cut = 0;
        while (cut < length && name.Data[cut] != '\\')
            cut++;
        int root = find_root(name.Data, cut);
        const MimeRegChar *rest = cut < length ? name.Data + cut + 1 : nullptr;
        if (root >= 0) {
            if (deleting) {
                if (rest && *rest)
                    host->DeleteKey(host->Context, root, rest);
            } else if (rest || (root != MIME_REG_ROOT_MACHINE && root != MIME_REG_ROOT_USERS)) {
                void *key = nullptr;
                if (host->CreateKey(host->Context, root, rest, &key) == 0) {
                    state->Key = key;
                    state->Open = true;
                } else {
                    fail(r, MIME_REG_KEY_FAILED);
                }
            }
        }
    }
    release(r, &name);
}

static void import_text(Reader *r)
{
    const MimeRegImportHost *host = r->Host;
    KeyState state = {};
    MimeRegChar c;
    for (;;) {
        skip_whitespace(r);
        if (!get_char(r, &c))
            break;
        switch (c) {
        case '"':
            if (!state.Open) {
                skip_line(r);
            } else {
                Buffer name = {};
                if (parse_string(r, &name))
                    parse_value(r, state.Key, name.Data);
                else
                    skip_line(r);
                release(r, &name);
            }
            break;
        case '@':
            if (!state.Open)
                skip_line(r);
            else
                parse_value(r, state.Key, nullptr);
            break;
        case '[':
            if (state.Open) {
                host->CloseKey(host->Context, state.Key);
                state.Open = false;
                state.Key = nullptr;
            }
            parse_header(r, &state);
            break;
        case ';':
            skip_line(r);
            break;
        default:
            if (!is_newline(c))
                skip_line(r);
            break;
        }
    }
    if (state.Open)
        host->CloseKey(host->Context, state.Key);
}

/* ------------------------------------------------------------------ the 3.1 format */

static void import_win31(Reader *r)
{
    const MimeRegImportHost *host = r->Host;
    MimeRegChar c;
    Buffer value = {};
    while (get_char(r, &c)) {
        unget_char(r);
        const char *prefix = match(r, '\\') ? ".classes" : "HKEY_CLASSES_ROOT";
        bool ok = true;
        for (; *prefix && ok; prefix++)
            ok = match(r, (MimeRegChar)*prefix);
        if (!ok || !match(r, '\\')) {
            skip_line(r);
            continue;
        }
        MimeRegChar subkey[0x400];
        uint32_t count = 0;
        bool overflow = false;
        while (get_char(r, &c) && c != 32 && !is_newline(c)) {
            if (count >= 0x3FF) {
                overflow = true;
                break;
            }
            subkey[count++] = c;
        }
        if (overflow) {
            skip_line(r);
            continue;
        }
        subkey[count] = 0;
        skip_whitespace(r);
        if (match(r, '='))
            match(r, ' ');
        value.Count = 0;
        bool alloc = true;
        while (get_char(r, &c)) {
            if (is_newline(c))
                break;
            if (!reserve(r, &value, 1)) {
                alloc = false;
                break;
            }
            value.Data[value.Count++] = c;
        }
        if (alloc && reserve(r, &value, 1)) {
            value.Data[value.Count] = 0;
            if (host->SetClassDefault(host->Context, subkey, value.Data,
                                      (uint32_t)(value.Count * sizeof(MimeRegChar))) != 0)
                fail(r, MIME_REG_VALUE_FAILED);
        } else {
            fail(r, MIME_REG_VALUE_FAILED);
            skip_line(r);
        }
    }
    release(r, &value);
}

/* ------------------------------------------------------------------ the file */

MimeRegImportStatus MimeRegImport(const MimeRegImportHost *host)
{
    Reader *r = host->Realloc(host->Context, nullptr, sizeof(Reader));
    if (!r)
        return MIME_REG_VALUE_FAILED;
    r->Host = host;
    r->Status = MIME_REG_OK;
    r->Eof = 0;
    r->Carry = r->Position = r->Length = 0;
    r->Version = 0;
    r->Mode = 0;

    /* the byte order mark decides the encoding, and goes. */
    long first = host->Read(host->Context, r->Raw, 3);
    uint32_t got = first > 0 ? (uint32_t)first : 0;
    while (got < 3 && first > 0) {
        first = host->Read(host->Context, r->Raw + got, 3 - got);
        if (first > 0)
            got += (uint32_t)first;
    }
    if (first < 0) {
        fail(r, MIME_REG_READ_FAILED);
        goto done;
    }
    uint32_t used = 0;
    if (got >= 2 && r->Raw[0] == 0xFF && r->Raw[1] == 0xFE) {
        r->Mode = 1;
        used = 2;
    } else if (host->Utf8 && got >= 3 && r->Raw[0] == 0xEF && r->Raw[1] == 0xBB && r->Raw[2] == 0xBF) {
        r->Mode = 2;
        used = 3;
    }
    /* what was read after the mark is the start of the text. */
    r->Carry = got - used;
    for (uint32_t i = 0; i < r->Carry; i++)
        r->Raw[i] = r->Raw[used + i];
    if (r->Carry && r->Mode == 0) {
        r->Length = host->AnsiToWide(host->Context, (const char *)r->Raw, r->Carry, r->Wide, CHUNK);
        r->Carry = 0;
    } else if (r->Carry && r->Mode == 1) {
        decode_utf16(r, r->Carry);
    } else if (r->Carry && r->Mode == 2) {
        decode_utf8(r, r->Carry);
    }

    skip_whitespace(r);
    static const char version_header[] = "Windows Registry Editor Version";
    const char *p = version_header;
    while (*p && match(r, (MimeRegChar)*p))
        p++;
    if (!*p) {
        skip_whitespace(r);
        MimeRegChar c;
        uint32_t version = 0;
        for (;;) {
            if (match(r, '.'))
                break;
            if (!get_char(r, &c) || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
                fail(r, MIME_REG_BAD_HEADER);
                goto done;
            }
            version = version * 16 + (c >= '0' && c <= '9' ? (uint32_t)(c - '0') : 0);
        }
        for (int i = 0; i < 2; i++) {
            if (!get_char(r, &c) || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
                fail(r, MIME_REG_BAD_HEADER);
                goto done;
            }
            version = version * 16 + (c >= '0' && c <= '9' ? (uint32_t)(c - '0') : 0);
        }
        if (version != 0x500) {
            fail(r, MIME_REG_WRONG_VERSION);
            goto done;
        }
        r->Version = version;
        skip_whitespace(r);
        import_text(r);
        goto done;
    }
    p = "REGEDIT";
    while (*p && match(r, (MimeRegChar)*p))
        p++;
    if (*p) {
        fail(r, MIME_REG_NOT_A_SCRIPT);
        goto done;
    }
    bool four = match(r, '4');
    skip_whitespace(r);
    MimeRegChar c;
    if (get_char(r, &c) && is_newline(c)) {
        if (four) {
            r->Version = 0x400;
            import_text(r);
        } else {
            r->Version = 0x310;
            import_win31(r);
        }
    }
done:;
    MimeRegImportStatus status = r->Status;
    host->Realloc(host->Context, r, 0);
    return status;
}

/* ------------------------------------------------------------------ the writer */

static const char hex_digits[] = "0123456789abcdef";

int MimeRegWriterFlush(MimeRegWriter *w)
{
    if (w->Count) {
        if (w->Ansi) {
            uint32_t bytes = w->WideToAnsi(w->Context, w->Buffer, w->Count, w->Bytes, sizeof(w->Bytes));
            if (!w->Write(w->Context, w->Bytes, bytes) || (bytes == 0))
                w->Failed = 1;
        } else if (!w->Write(w->Context, w->Buffer, w->Count * 2)) {
            w->Failed = 1;
        }
    }
    w->Count = 0;
    return !w->Failed;
}

static void put_char(MimeRegWriter *w, MimeRegChar c)
{
    if (c == '\n') {
        put_char(w, '\r');
        w->Column = 0;
    } else {
        w->Column++;
    }
    if (w->Count < 0x10000)
        w->Buffer[w->Count++] = c;
    if (w->Count == 0x10000)
        MimeRegWriterFlush(w);
}

static void put_text(MimeRegWriter *w, const char *text)
{
    for (; *text; text++)
        put_char(w, (MimeRegChar)(unsigned char)*text);
}

static void put_wide(MimeRegWriter *w, const MimeRegChar *text)
{
    for (; *text; text++)
        put_char(w, *text);
}

static void put_hex(MimeRegWriter *w, uint32_t value, int digits)
{
    bool started = digits != 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        char d = hex_digits[(value >> shift) & 0xF];
        if (started || d != '0') {
            put_char(w, (MimeRegChar)d);
            started = true;
        }
    }
    if (!started)
        put_char(w, '0');
}

/* a string between quotes, a backslash before a quote and a backslash. */
static void put_quoted(MimeRegWriter *w, const MimeRegChar *text, size_t units)
{
    put_char(w, '"');
    for (size_t i = 0; i < units && text[i]; i++) {
        MimeRegChar c = text[i];
        if (c == '\n' || c == '\r') {
            if (w->MultiLineStrings) {
                put_char(w, '\\');
                c = c == '\n' ? 'n' : 'r';
            }
        } else if (c == '"' || c == '\\') {
            put_char(w, '\\');
        }
        put_char(w, c);
    }
    put_char(w, '"');
}

void MimeRegWriterBegin(MimeRegWriter *w, int ansi)
{
    w->Ansi = ansi;
    w->Count = w->Column = 0;
    w->Failed = 0;
    if (ansi) {
        put_text(w, "REGEDIT4\n\n");
        return;
    }
    static const uint8_t mark[2] = {0xFF, 0xFE};
    if (!w->Write(w->Context, mark, 2))
        w->Failed = 1;
    put_text(w, "Windows Registry Editor Version 5.00\n\n");
}

void MimeRegWriteKey(MimeRegWriter *w, const MimeRegChar *path)
{
    put_char(w, '[');
    put_wide(w, path);
    put_text(w, "]\n");
}

void MimeRegWriteEnd(MimeRegWriter *w)
{
    put_char(w, '\n');
}

void MimeRegWriteValue(MimeRegWriter *w, const MimeRegChar *name, uint32_t type, const void *data, uint32_t size)
{
    const uint8_t *bytes = data;
    if (name && *name)
        put_quoted(w, name, (size_t)-1);
    else
        put_char(w, '@');
    put_char(w, '=');
    if (type == 1) {
        MimeRegChar unit[2];
        size_t units = size / 2;
        /* the units of the string may not be aligned. */
        put_char(w, '"');
        for (size_t i = 0; i < units; i++) {
            copy_memory(unit, bytes + 2 * i, 2);
            if (!unit[0])
                break;
            if (unit[0] == '\n' || unit[0] == '\r') {
                if (w->MultiLineStrings) {
                    put_char(w, '\\');
                    unit[0] = unit[0] == '\n' ? 'n' : 'r';
                }
            } else if (unit[0] == '"' || unit[0] == '\\') {
                put_char(w, '\\');
            }
            put_char(w, unit[0]);
        }
        put_char(w, '"');
    } else if (type == 4 && size == 4) {
        uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
                         ((uint32_t)bytes[3] << 24);
        put_text(w, "dword:");
        put_hex(w, value, 1);
    } else {
        /* the ANSI format keeps the low byte of every unit of a string list, so the file has half the bytes. */
        bool halve = w->Ansi && (type == 2 || type == 7);
        uint32_t count = halve ? size / 2 : size;
        uint32_t step = halve ? 2 : 1;
        put_text(w, "hex");
        if (type != 3) {
            put_char(w, '(');
            put_hex(w, type, 0);
            put_char(w, ')');
        }
        put_char(w, ':');
        for (uint32_t i = 0; i < count; i++) {
            if (i != 0) {
                if (w->Column <= 75)
                    put_char(w, ',');
                else
                    put_text(w, ",\\\n  ");
            }
            uint8_t b = bytes[i * step];
            put_char(w, (MimeRegChar)hex_digits[b >> 4]);
            put_char(w, (MimeRegChar)hex_digits[b & 15]);
        }
    }
    put_char(w, '\n');
}
