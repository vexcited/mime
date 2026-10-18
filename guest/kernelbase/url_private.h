/*
 * the url engines shared by url.c and urlparts.c: the scheme table, character classes, a growing
 * string and the parts the older engine breaks a url into.
 */
#ifndef MIME_KERNELBASE_URL_PRIVATE_H
#define MIME_KERNELBASE_URL_PRIVATE_H

#include "shlwapi_kb.h"

#ifndef INTERNET_MAX_URL_LENGTH
#define INTERNET_MAX_URL_LENGTH 2084
#endif

/* scheme flags of the table. */
#define SCHEME_OPAQUE 0x01    /* no hierarchy: mailto, javascript... */
#define SCHEME_SERVER 0x02    /* a server after "//" */
#define SCHEME_NOHISTORY 0x04 /* what URLIS_NOHISTORY reports */
#define SCHEME_SLASHES 0x08   /* backslashes count as slashes */
#define SCHEME_SECURE 0x10    /* https, left out of UrlFixupW's guesses */

typedef struct {
    LPCWSTR name;
    URL_SCHEME type;
    DWORD flags;
} URL_SCHEME_INFO;

extern const URL_SCHEME_INFO KbUrlSchemes[24];

/* the scheme of the length characters at name, case insensitive; 0 and no flags when unknown. */
URL_SCHEME KbUrlSchemeType(LPCWSTR name, SIZE_T length, DWORD *flags);

/* character classes of the 0x20 to 0x7F range. */
#define URLC_ALNUM 0x01
#define URLC_HEX 0x02
#define URLC_SCHEME 0x04
#define URLC_SAFE 0x08
#define URLC_UNRESERVED 0x10
BOOL KbUrlClass(WCHAR c, int mask);
BOOL KbUrlSchemeChar(WCHAR c);
int KbUrlHexValue(WCHAR c);

/* the characters of a scheme ending in a colon, 0 when there is none of two or more. */
SIZE_T KbUrlSchemeLength(LPCWSTR url);

/* a heap string that grows; failures stick. */
typedef struct {
    LPWSTR text;
    SIZE_T length, capacity;
    BOOL failed;
} URL_TEXT;

void KbUrlTextInit(URL_TEXT *t);
void KbUrlTextFree(URL_TEXT *t);
void KbUrlTextAppend(URL_TEXT *t, LPCWSTR s, SIZE_T length);
void KbUrlTextAppendChar(URL_TEXT *t, WCHAR c);
void KbUrlTextSet(URL_TEXT *t, LPCWSTR s);

/* a result copied out the way the url functions size their buffers: E_POINTER and the size with
   the terminator when it does not fit, the length without it when it does. */
HRESULT KbUrlCopyOut(LPCWSTR text, LPWSTR out, DWORD *size);
HRESULT KbUrlCopyOutA(LPCWSTR text, LPSTR out, DWORD *size);

/* percent escapes undone in place, as UrlUnescape with URL_UNESCAPE_INPLACE does. */
HRESULT KbUrlUnescapeInPlace(LPWSTR url, DWORD flags);

/* ------------------------------------------------------------------ the parts of the older engine */

/* flags of the parts. */
#define PART_OPAQUE 0x0001
#define PART_SERVER 0x0002
#define PART_SLASHES 0x0008
#define PART_ROOTED 0x0100
#define PART_DRIVE 0x0200
#define PART_EMPTY 0x0400
#define PART_DIRECTORY 0x1000
#define PART_UNC4 0x10000000

typedef struct {
    LPWSTR text;
    BOOL dead;
} URL_SEGMENT;

typedef struct {
    DWORD flags;
    LPWSTR scheme;
    URL_SCHEME type;
    LPWSTR server;
    URL_SEGMENT *path;
    UINT count;
    URL_SEGMENT *more;
    UINT more_count;
    LPWSTR query;
    LPWSTR fragment;
} URL_PARTS;

/* breaks a writable copy of a url into parts the way BreakUrls does; free with KbUrlPartsFree. */
void KbUrlBreak(LPWSTR url, URL_PARTS *parts);
void KbUrlPartsFree(URL_PARTS *parts);
void KbUrlBuild(URL_PARTS *parts, DWORD flags, URL_TEXT *out);

/* the fragment of a url: for a file one only after a known document extension. */
LPCWSTR KbUrlFindFragment(LPCWSTR url, BOOL file);

/* SHUrlCreateFromPath, SHPathCreateFromUrl and SHUrlEscape. */
HRESULT KbUrlCreateFromPath(LPCWSTR path, URL_TEXT *out, DWORD flags);
HRESULT KbUrlPathFromUrl(LPCWSTR url, URL_TEXT *out, DWORD flags);
HRESULT KbUrlEscape(LPCWSTR url, URL_TEXT *out, DWORD flags);

/* the older engine's canonicalization and combination. */
HRESULT KbUrlParse(LPCWSTR base, LPCWSTR relative, URL_TEXT *out, DWORD flags);

/* a dos path inside a url or of its own: "x:", "\\server", "file://x:" and "file:////server". */
LPCWSTR KbUrlFindDosPath(LPCWSTR url);

#endif
