#ifndef MIME_LIB_TRANSFER_H
#define MIME_LIB_TRANSFER_H

#include <mime/win32/winhttp.h>

#define XFER_TO_END ((UINT64)-1)
#define XFER_MAX_AUTH_TRIES 6

/* a credential the request answers a 401 or 407 with, the scheme as WINHTTP_AUTH_SCHEME_* and the target as WINHTTP_AUTH_TARGET_*. */
typedef struct XFER_CREDENTIAL {
    DWORD target;
    DWORD scheme;
    const WCHAR *user;
    const WCHAR *password;
} XFER_CREDENTIAL;

struct XFER_RESULT;

/* one http request of a url, synchronous, its body read from the source and its answer written through the sink. */
typedef struct XFER_REQUEST {
    const WCHAR *url;
    const WCHAR *verb;
    const WCHAR *headers;
    BOOL range;
    UINT64 offset;
    UINT64 length;
    DWORD security_flags;
    BOOL revocation;
    DWORD redirect_policy;
    BOOL has_redirect_policy;
    const void *client_cert;
    DWORD client_cert_size;
    HANDLE cancel;
    HRESULT (*write)(void *context, UINT64 offset, const void *data, DWORD size);
    void *context;
    /* the request handle while it runs, so that XferAbort can end a call that blocks in winhttp. */
    HINTERNET volatile *active;
    /* Accept-encoding: identity, replacing what winhttp would ask for. */
    BOOL identity;
    BOOL no_redirects;
    DWORD autologon;
    BOOL has_autologon;
    const WINHTTP_PROXY_INFO *proxy;
    const XFER_CREDENTIAL *credentials;
    DWORD credential_count;
    /* a body of body_length bytes, read through read from body_offset on, zeros when there is no read. */
    UINT64 body_length;
    UINT64 body_offset;
    HRESULT (*read)(void *context, UINT64 offset, void *data, DWORD size, DWORD *got);
    /* called once the answer's headers are read, before its body. */
    void (*headers_ready)(void *context, const struct XFER_RESULT *result);
} XFER_REQUEST;

typedef struct XFER_RESULT {
    DWORD status;
    UINT64 total;
    UINT64 received;
    UINT64 range_first;
    UINT64 range_last;
    BOOL has_range;
    BOOL has_length;
    UINT64 length;
    BOOL chunked;
    BOOL accept_ranges;
    BOOL html;
    DWORD retry_after;
    WCHAR *headers;
    WCHAR *final_url;
    WCHAR address[64];
    DWORD auth_tries;
} XFER_RESULT;

HINTERNET XferOpenSession(const WCHAR *agent, DWORD access, DWORD timeout);
HRESULT XferRun(HINTERNET session, const XFER_REQUEST *request, XFER_RESULT *result);
void XferAbort(HINTERNET volatile *active);
void XferFreeResult(XFER_RESULT *result);
HRESULT XferCheckUrl(const WCHAR *url);
WCHAR *XferHeader(const WCHAR *headers, const WCHAR *name);

#endif
