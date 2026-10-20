/* the texts of errors and the record of the last one a document had. */
#include <xmldom/xmldom.h>

typedef struct XMessage {
    UINT id;
    const WCHAR *text;
} XMessage;

static const XMessage messages[] = {
#include "messages.inc"
};

const WCHAR *xd_message(UINT id)
{
    /* the limits are reported with the text of the message about a constraint, naming which. */
    if (id == XE_MAX_DEPTH)
        return L"Operation aborted: MaxElementDepth constraint violated.\r\n";
    if (id == XE_MAX_SIZE)
        return L"Operation aborted: MaxXMLSize constraint violated.\r\n";
    for (UINT i = 0; i < sizeof(messages) / sizeof(messages[0]); i++)
        if (messages[i].id == id)
            return messages[i].text;
    return nullptr;
}

/* a message with its %1 and %2 filled in. */
WCHAR *xd_format(UINT id, const WCHAR *p1, const WCHAR *p2)
{
    const WCHAR *text = xd_message(id);
    if (!text)
        return nullptr;
    XBuf b = {};
    for (; *text; text++) {
        if (text[0] == '%' && (text[1] == '1' || text[1] == '2')) {
            const WCHAR *p = text[1] == '1' ? p1 : p2;
            if (p)
                xb_put(&b, p, xd_wlen(p));
            text++;
        } else {
            xb_putc(&b, *text);
        }
    }
    if (b.failed) {
        xb_free(&b);
        return nullptr;
    }
    if (!b.s)
        return xd_wcsdup(L"", 0);
    return b.s;
}

const WCHAR *xd_system_message(UINT code)
{
    switch (code) {
    case 0x80070002:
        return L"The system cannot find the file specified.\r\n";
    case 0x80070003:
        return L"The system cannot find the path specified.\r\n";
    case 0x80070005:
        return L"Access is denied.\r\n";
    case 0x80070020:
        return L"The process cannot access the file because it is being used by another process.\r\n";
    case 0x80070057:
        return L"The parameter is incorrect.\r\n";
    }
    return xd_message(code);
}

void xd_err_free(XErr *e)
{
    xd_free(e->reason);
    xd_free(e->src);
    xd_free(e->url);
    for (UINT i = 0; i < 3; i++)
        xd_free(e->param[i]);
    memset(e, 0, sizeof(*e));
}

void xd_err_clear(XDoc *doc)
{
    xd_err_free(&doc->err);
}

void xd_err_copy(XErr *to, const XErr *from)
{
    memset(to, 0, sizeof(*to));
    to->code = from->code;
    to->line = from->line;
    to->linepos = from->linepos;
    to->filepos = from->filepos;
    to->params = from->params;
    if (from->reason)
        to->reason = xd_wcsdup(from->reason, (UINT)-1);
    if (from->src)
        to->src = xd_wcsdup(from->src, (UINT)-1);
    if (from->url)
        to->url = xd_wcsdup(from->url, (UINT)-1);
    for (UINT i = 0; i < 3; i++)
        if (from->param[i])
            to->param[i] = xd_wcsdup(from->param[i], (UINT)-1);
}

void xd_err_set(XDoc *doc, UINT code, LONG line, LONG linepos, LONG filepos, const WCHAR *src, UINT srclen,
                const WCHAR *p1, const WCHAR *p2)
{
    xd_err_free(&doc->err);
    XErr *e = &doc->err;
    e->code = (LONG)code;
    e->line = line;
    e->linepos = linepos;
    e->filepos = filepos;
    e->reason = xd_format(code, p1, p2);
    if (src)
        e->src = xd_wcsdup(src, srclen);
    if (doc->url)
        e->url = xd_wcsdup(doc->url, (UINT)-1);
    if (p1) {
        e->param[e->params++] = xd_wcsdup(p1, (UINT)-1);
        if (p2)
            e->param[e->params++] = xd_wcsdup(p2, (UINT)-1);
    }
}

/* a resource that could not be read: the system's words for the code, then which resource it was. */
void xd_err_set_reason(XDoc *doc, UINT code, const WCHAR *reason, const WCHAR *url)
{
    xd_err_free(&doc->err);
    XErr *e = &doc->err;
    e->code = (LONG)code;
    WCHAR *where = xd_format(XE_PROCESSING_RESOURCE, url, nullptr);
    XBuf b = {};
    if (reason)
        xb_put(&b, reason, xd_wlen(reason));
    if (where)
        xb_put(&b, where, xd_wlen(where));
    xd_free(where);
    e->reason = b.failed ? nullptr : b.s;
    if (b.failed)
        xb_free(&b);
    if (url)
        e->url = xd_wcsdup(url, (UINT)-1);
}
