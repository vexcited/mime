#include "schannel.h"
#include <mime/scm.h>

/*
 * the exchange of identities that the local authentication packages need: lsass, on windows, knows the logon session a
 * client speaks for and gives it to the server that accepts the client's token. here the manager keeps the client's
 * token under a ticket that only the two ends of an exchange see, and gives it to the server as a handle of its own.
 * the advapi32 of windows knows nothing of it, so the package speaks to the manager itself.
 */

DECLSPEC_IMPORT BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                                 PSECURITY_DESCRIPTOR *result,
                                                                                 PULONG size);

#ifndef RPC_S_SERVER_UNAVAILABLE
#define RPC_S_SERVER_UNAVAILABLE 1722L
#endif
#define TICKET_READY_TIMEOUT_MS 180000

static volatile LONG g_ready;

/* a process that starts before the manager creates the event itself: anyone may wait on it, the system owns it. */
static HANDLE open_ready_event(DWORD *error)
{
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, SCM_START_EVENT);
    if (event || GetLastError() != ERROR_FILE_NOT_FOUND) {
        *error = event ? ERROR_SUCCESS : GetLastError();
        return event;
    }
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;0x100000;;;WD)(A;;GA;;;SY)", 1, &sd, nullptr)) {
        *error = GetLastError();
        return nullptr;
    }
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), sd, FALSE};
    event = CreateEventW(&attributes, TRUE, FALSE, SCM_START_EVENT);
    DWORD created = GetLastError();
    LocalFree(sd);
    if (event && created == ERROR_ALREADY_EXISTS) {
        CloseHandle(event);
        event = OpenEventW(SYNCHRONIZE, FALSE, SCM_START_EVENT);
        created = event ? ERROR_SUCCESS : GetLastError();
    }
    *error = event ? ERROR_SUCCESS : created;
    return event;
}

static DWORD wait_until_ready(void)
{
    if (g_ready)
        return ERROR_SUCCESS;
    DWORD error;
    HANDLE event = open_ready_event(&error);
    if (!event)
        return error;
    DWORD waited = WaitForSingleObject(event, TICKET_READY_TIMEOUT_MS);
    CloseHandle(event);
    if (waited == WAIT_TIMEOUT)
        return ERROR_TIMEOUT;
    if (waited != WAIT_OBJECT_0)
        return GetLastError();
    g_ready = 1;
    return ERROR_SUCCESS;
}

static HANDLE connect_pipe(void)
{
    for (int attempt = 0; attempt < 40; attempt++) {
        HANDLE h = CreateFileW(SCM_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            return h;
        if (GetLastError() != ERROR_PIPE_BUSY)
            return nullptr;
        WaitNamedPipeW(SCM_PIPE_NAME, 1000);
    }
    return nullptr;
}

static BOOL pipe_write(HANDLE h, const void *data, DWORD size)
{
    const BYTE *p = data;
    while (size) {
        DWORD done = 0;
        if (!WriteFile(h, p, size, &done, nullptr))
            return FALSE;
        p += done;
        size -= done;
    }
    return TRUE;
}

static BOOL pipe_read(HANDLE h, void *data, DWORD size)
{
    BYTE *p = data;
    while (size) {
        DWORD done = 0;
        if (!ReadFile(h, p, size, &done, nullptr) || !done)
            return FALSE;
        p += done;
        size -= done;
    }
    return TRUE;
}

/* one request and its answer, which the caller frees with SCM_FREE. the manager's answer is a win32 error. */
static DWORD call(SCM_WRITER *w, BYTE **data, SCM_READER *reader)
{
    *data = nullptr;
    DWORD error = wait_until_ready();
    UINT32 size;
    UCHAR *message = ScmWriterFinish(w, &size);
    if (!message)
        error = error ? error : ERROR_NOT_ENOUGH_MEMORY;
    if (error) {
        SCM_FREE(w->Data);
        return error;
    }
    error = RPC_S_SERVER_UNAVAILABLE;
    HANDLE pipe = connect_pipe();
    SCM_HEADER header;
    if (pipe && pipe_write(pipe, message, size) && pipe_read(pipe, &header, sizeof(header)) &&
        header.Magic == SCM_MAGIC && header.Size >= sizeof(header) && header.Size <= SCM_MAX_MESSAGE) {
        BYTE *answer = SCM_ALLOC(header.Size);
        if (!answer) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            memcpy(answer, &header, sizeof(header));
            if (pipe_read(pipe, answer + sizeof(header), header.Size - (DWORD)sizeof(header))) {
                *data = answer;
                ScmReaderInit(reader, answer, header.Size);
                error = header.Code;
            } else {
                SCM_FREE(answer);
            }
        }
    }
    if (pipe)
        CloseHandle(pipe);
    SCM_FREE(w->Data);
    return error;
}

DWORD SecPublishToken(BYTE *ticket)
{
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_SSPI_PUBLISH);
    BYTE *data;
    SCM_READER r;
    DWORD error = call(&w, &data, &r);
    if (!error) {
        UINT32 length;
        const void *bytes = ScmGetBytes(&r, &length);
        if (r.Failed || length != 16)
            error = ERROR_INVALID_DATA;
        else
            memcpy(ticket, bytes, 16);
    }
    if (data)
        SCM_FREE(data);
    return error;
}

DWORD SecRedeemToken(const BYTE *ticket, HANDLE *token)
{
    *token = nullptr;
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_SSPI_REDEEM);
    ScmPutBytes(&w, ticket, 16);
    BYTE *data;
    SCM_READER r;
    DWORD error = call(&w, &data, &r);
    if (!error) {
        UINT64 handle = ScmGetU64(&r);
        if (r.Failed)
            error = ERROR_INVALID_DATA;
        else
            *token = (HANDLE)(ULONG_PTR)handle;
    }
    if (data)
        SCM_FREE(data);
    return error;
}
