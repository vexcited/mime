#include "qmgr.h"

#define LANG_ENGLISH_ 0x09
#define ERROR_MR_MID_NOT_FOUND_ 317
#define ERROR_OUTOFMEMORY_ 14
#define FORMAT_FLAGS_MODULE (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE)
#define FORMAT_FLAGS_SYSTEM (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM)

/*
 * the texts of winhttp.dll.mui that transfers meet most, for a winhttp.dll that carries no message table of its own.
 * they are only looked at when FormatMessageW finds nothing in winhttp.dll.
 */
const QM_MESSAGE g_winhttp_messages[] = {
    {12002, L"The operation timed out\r\n"},
    {12005, L"The URL is invalid\r\n"},
    {12006, L"The URL does not use a recognized protocol\r\n"},
    {12007, L"The server name or address could not be resolved\r\n"},
    {12016, L"The requested operation is invalid\r\n"},
    {12017, L"The operation has been canceled\r\n"},
    {12029, L"A connection with the server could not be established\r\n"},
    {12030, L"The connection with the server was terminated abnormally\r\n"},
    {12031, L"The connection with the server was reset\r\n"},
    {12037, L"The date in the certificate is invalid or has expired\r\n"},
    {12038, L"The host name in the certificate is invalid or does not match\r\n"},
    {12045, L"The certificate authority is invalid or incorrect\r\n"},
    {12150, L"The requested header was not found\r\n"},
    {12152, L"The server returned an invalid or unrecognized response\r\n"},
    {12156, L"The HTTP redirect request failed\r\n"},
    {12157, L"An error occurred in the secure channel support\r\n"},
    {12175, L"A security error occurred\r\n"},
};
const DWORD g_winhttp_message_count = ARRAYSIZE(g_winhttp_messages);

static HMODULE g_winhttp;

/* the table is en-US, which a neutral or default language and LANG_ENGLISH ask for. */
static BOOL english(DWORD language)
{
    DWORD primary = language & 0x3FF;
    return !language || !primary || language == 0x409 || (primary == LANG_ENGLISH_ && !(language >> 10 & ~1u));
}

static const WCHAR *find(const QM_MESSAGE *table, DWORD count, DWORD id)
{
    DWORD low = 0, high = count;
    while (low < high) {
        DWORD middle = (low + high) / 2;
        if (table[middle].id == id)
            return table[middle].text;
        if (table[middle].id < id)
            low = middle + 1;
        else
            high = middle;
    }
    return nullptr;
}

static HRESULT answer(const WCHAR *text, WCHAR **out)
{
    *out = QmTaskDup(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT answer_local(WCHAR *text, WCHAR **out)
{
    HRESULT hr = answer(text, out);
    LocalFree(text);
    return hr;
}

/*
 * CJobManager::GetErrorDescription: the message table of qmgr.dll, the one of winhttp.dll for 12000 to 12999 of
 * FACILITY_WIN32, then the system. UPnP codes of 0x80040200 to 0x80040217 and 0x80040500 and 0x80040501 read as
 * BG_E_UPNP_ERROR.
 */
HRESULT QmDescribe(HRESULT code, DWORD language, WCHAR **out)
{
    *out = nullptr;
    DWORD id = (DWORD)code;
    if ((id - 0x80040200u <= 0x17 && (0xFF03FFu >> (id - 0x80040200u) & 1)) || id - 0x80040500u <= 1)
        id = (DWORD)BG_E_UPNP_ERROR;
    if (english(language)) {
        const WCHAR *text = find(g_messages, g_message_count, id);
        if (text)
            return answer(text, out);
    }
    WCHAR *buffer = nullptr;
    if ((id & 0x1FFF0000) == 0x70000 && (id & 0xFFFF) - 12000u <= 999) {
        if (!g_winhttp)
            g_winhttp = LoadLibraryExW(L"winhttp.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (g_winhttp && FormatMessageW(FORMAT_FLAGS_MODULE, g_winhttp, id & 0xFFFF, language, (LPWSTR)&buffer, 0, nullptr))
            return answer_local(buffer, out);
        if (GetLastError() == ERROR_OUTOFMEMORY_)
            return E_OUTOFMEMORY;
        const WCHAR *text = english(language) ? find(g_winhttp_messages, g_winhttp_message_count, id & 0xFFFF) : nullptr;
        if (text)
            return answer(text, out);
    }
    if (FormatMessageW(FORMAT_FLAGS_SYSTEM, nullptr, id, language, (LPWSTR)&buffer, 0, nullptr))
        return answer_local(buffer, out);
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

/* ------------------------------------------------------------------ IBackgroundCopyError */

typedef struct ERROR_EXT {
    IBackgroundCopyError iface;
    LONG refs;
    BG_ERROR_CONTEXT context;
    HRESULT code;
    IBackgroundCopyFile *file;
    WCHAR protocol[8];
} ERROR_EXT;

#define EX(iface) CONTAINING_RECORD(iface, ERROR_EXT, iface)

static HRESULT STDMETHODCALLTYPE e_qi(IBackgroundCopyError *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyError)) {
        *out = iface;
        InterlockedIncrement(&EX(iface)->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE e_addref(IBackgroundCopyError *iface)
{
    return (ULONG)InterlockedIncrement(&EX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE e_release(IBackgroundCopyError *iface)
{
    ERROR_EXT *e = EX(iface);
    LONG refs = InterlockedDecrement(&e->refs);
    if (refs)
        return (ULONG)refs;
    if (e->file)
        QM_RELEASE(e->file);
    LocalFree(e);
    QmObjectRelease();
    return 0;
}

static HRESULT STDMETHODCALLTYPE e_get(IBackgroundCopyError *iface, BG_ERROR_CONTEXT *context, HRESULT *code)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    if (!context || !code)
        hr = E_INVALIDARG;
    else {
        *context = EX(iface)->context;
        *code = EX(iface)->code;
    }
    QmLeave();
    return hr;
}

static HRESULT STDMETHODCALLTYPE e_file(IBackgroundCopyError *iface, IBackgroundCopyFile **file)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    if (!file)
        hr = E_INVALIDARG;
    else if ((*file = EX(iface)->file))
        QM_ADDREF(*file);
    else
        hr = BG_E_FILE_NOT_AVAILABLE;
    QmLeave();
    return hr;
}

static HRESULT STDMETHODCALLTYPE e_description(IBackgroundCopyError *iface, DWORD language, LPWSTR *text)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    hr = text ? QmDescribe(EX(iface)->code, language, text) : E_INVALIDARG;
    QmLeave();
    return hr;
}

/* CJobErrorExternal::GetErrorContextDescriptionInternal: the context as one of the BG_*_ERROR_CONTEXT_* messages. */
static HRESULT STDMETHODCALLTYPE e_context(IBackgroundCopyError *iface, DWORD language, LPWSTR *text)
{
    static const HRESULT codes[] = {
        BG_S_ERROR_CONTEXT_NONE,
        BG_E_ERROR_CONTEXT_UNKNOWN,
        BG_E_ERROR_CONTEXT_GENERAL_QUEUE_MANAGER,
        BG_E_ERROR_CONTEXT_QUEUE_MANAGER_NOTIFICATION,
        BG_E_ERROR_CONTEXT_LOCAL_FILE,
        BG_E_ERROR_CONTEXT_REMOTE_FILE,
        BG_E_ERROR_CONTEXT_GENERAL_TRANSPORT,
        BG_E_ERROR_CONTEXT_REMOTE_APPLICATION,
        BG_E_ERROR_CONTEXT_SERVER_CERTIFICATE_CALLBACK,
    };
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    DWORD context = (DWORD)EX(iface)->context;
    hr = text ? QmDescribe(context < ARRAYSIZE(codes) ? codes[context] : BG_E_ERROR_CONTEXT_UNKNOWN, language, text)
              : E_INVALIDARG;
    QmLeave();
    return hr;
}

static HRESULT STDMETHODCALLTYPE e_protocol(IBackgroundCopyError *iface, LPWSTR *protocol)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    if (!protocol)
        hr = E_INVALIDARG;
    else if (!EX(iface)->file) {
        *protocol = nullptr;
        hr = BG_E_PROTOCOL_NOT_AVAILABLE;
    } else
        hr = answer(EX(iface)->protocol, protocol);
    QmLeave();
    return hr;
}

static const IBackgroundCopyErrorVtbl g_error_vtbl = {e_qi,          e_addref,  e_release, e_get, e_file,
                                                      e_description, e_context, e_protocol};

/* CJobErrorExternal::CJobErrorExternal: a snapshot of the error, taken with the manager lock held. */
HRESULT ErrorCreate(BJOB *job, IBackgroundCopyError **out)
{
    *out = nullptr;
    if (!job->error.present)
        return BG_E_ERROR_INFORMATION_UNAVAILABLE;
    ERROR_EXT *e = LocalAlloc(LMEM_ZEROINIT, sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->iface.lpVtbl = &g_error_vtbl;
    e->refs = 1;
    e->context = job->error.context;
    e->code = job->error.code;
    if (job->error.file >= 0 && (DWORD)job->error.file < job->file_count) {
        BFILE *file = job->files[job->error.file];
        if (FAILED(FileExtFor(file, &e->file)))
            e->file = nullptr;
        int protocol = QmProtocol(file->remote);
        wcscpy(e->protocol, protocol == PROTOCOL_HTTPS ? L"https" : protocol == PROTOCOL_HTTP ? L"http" : L"file");
    }
    QmObjectAdd();
    *out = &e->iface;
    return S_OK;
}
