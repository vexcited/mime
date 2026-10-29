/*
 * authz.dll: authorization checks against security descriptors. a client context holds an impersonation token, from the
 * token it is made of or from the token of this process for the user it names, and an access check is the system's
 * check with that token. a context for another user cannot be made, as groups of other users are not known here.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

__declspec(dllimport) BOOL WINAPI AccessCheck(PVOID descriptor, HANDLE token, DWORD desired, PVOID mapping, PVOID privileges,
                                              LPDWORD privileges_length, LPDWORD granted, LPBOOL status);
__declspec(dllimport) BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
__declspec(dllimport) BOOL WINAPI DuplicateTokenEx(HANDLE token, DWORD access, PVOID attributes, int level, int type,
                                                   PHANDLE duplicate);
__declspec(dllimport) BOOL WINAPI GetTokenInformation(HANDLE token, int information, PVOID buffer, DWORD length,
                                                      PDWORD needed);
__declspec(dllimport) BOOL WINAPI EqualSid(PVOID a, PVOID b);

#define TOKEN_QUERY_ 0x8
#define TOKEN_IMPERSONATE_ 0x4
#define TOKEN_DUPLICATE_ 0x2
#define SECURITY_IMPERSONATION_LEVEL 2
#define TOKEN_IMPERSONATION_TYPE 2
#define TOKEN_USER_ 1
#define TOKEN_GROUPS_ 2
#define TOKEN_PRIVILEGES_ 3
#define TOKEN_STATISTICS_ 10
#define MAXIMUM_ALLOWED_ 0x02000000

#define RM_MAGIC 0x4D52415A
#define CONTEXT_MAGIC 0x4358415A
#define ERROR_NO_SUCH_USER_ 1317

typedef struct {
    DWORD Magic, Flags;
} RESOURCE_MANAGER;

typedef struct {
    DWORD Magic;
    HANDLE Token;
    LUID Identifier;
    LARGE_INTEGER Expiration;
} CLIENT_CONTEXT;

typedef struct {
    DWORD DesiredAccess;
    PVOID PrincipalSelfSid;
    PVOID ObjectTypeList;
    DWORD ObjectTypeListLength;
    PVOID OptionalArguments;
} ACCESS_REQUEST;

typedef struct {
    DWORD ResultListLength;
    DWORD *GrantedAccessMask;
    DWORD *SaclEvaluationResults;
    DWORD *Error;
} ACCESS_REPLY;

BOOL WINAPI AuthzInitializeResourceManager(DWORD flags, PVOID dynamic_check, PVOID compute_groups, PVOID free_groups,
                                           LPCWSTR name, HANDLE *manager)
{
    if (!manager || (flags & ~7u)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RESOURCE_MANAGER *rm = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*rm));
    if (!rm) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    rm->Magic = RM_MAGIC;
    rm->Flags = flags;
    *manager = rm;
    return TRUE;
}

static RESOURCE_MANAGER *manager_of(HANDLE handle)
{
    RESOURCE_MANAGER *rm = handle;
    return rm && rm->Magic == RM_MAGIC ? rm : nullptr;
}

BOOL WINAPI AuthzFreeResourceManager(HANDLE handle)
{
    RESOURCE_MANAGER *rm = manager_of(handle);
    if (!rm) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    rm->Magic = 0;
    HeapFree(GetProcessHeap(), 0, rm);
    return TRUE;
}

static BOOL make_context(HANDLE token, LUID identifier, PLARGE_INTEGER expiration, HANDLE *out)
{
    HANDLE copy;
    if (!DuplicateTokenEx(token, TOKEN_QUERY_ | TOKEN_IMPERSONATE_ | TOKEN_DUPLICATE_, nullptr, SECURITY_IMPERSONATION_LEVEL,
                          TOKEN_IMPERSONATION_TYPE, &copy))
        return FALSE;
    CLIENT_CONTEXT *context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (!context) {
        CloseHandle(copy);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    context->Magic = CONTEXT_MAGIC;
    context->Token = copy;
    context->Identifier = identifier;
    context->Expiration.QuadPart = expiration ? expiration->QuadPart : 0x7FFFFFFFFFFFFFFFll;
    *out = context;
    return TRUE;
}

BOOL WINAPI AuthzInitializeContextFromToken(DWORD flags, HANDLE token, HANDLE manager, PLARGE_INTEGER expiration,
                                            LUID identifier, PVOID dynamic_args, HANDLE *context)
{
    if (!context || !token || !manager_of(manager) || flags) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return make_context(token, identifier, expiration, context);
}

BOOL WINAPI AuthzInitializeContextFromSid(DWORD flags, PVOID sid, HANDLE manager, PLARGE_INTEGER expiration, LUID identifier,
                                          PVOID dynamic_args, HANDLE *context)
{
    if (!context || !sid || !manager_of(manager)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE process;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY_ | TOKEN_DUPLICATE_, &process))
        return FALSE;
    BYTE buffer[512];
    DWORD size = sizeof(buffer);
    BOOL ok = FALSE;
    if (GetTokenInformation(process, TOKEN_USER_, buffer, size, &size)) {
        /* the groups of the user are the groups of the token of this process, so only this user can be answered for. */
        if (EqualSid(*(PVOID *)buffer, sid))
            ok = make_context(process, identifier, expiration, context);
        else
            SetLastError(ERROR_NO_SUCH_USER_);
    }
    CloseHandle(process);
    return ok;
}

static CLIENT_CONTEXT *context_of(HANDLE handle)
{
    CLIENT_CONTEXT *context = handle;
    return context && context->Magic == CONTEXT_MAGIC ? context : nullptr;
}

BOOL WINAPI AuthzFreeContext(HANDLE handle)
{
    CLIENT_CONTEXT *context = context_of(handle);
    if (!context) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    context->Magic = 0;
    CloseHandle(context->Token);
    HeapFree(GetProcessHeap(), 0, context);
    return TRUE;
}

BOOL WINAPI AuthzFreeHandle(HANDLE handle)
{
    return TRUE;
}

BOOL WINAPI AuthzAccessCheck(DWORD flags, HANDLE handle, ACCESS_REQUEST *request, HANDLE audit, PVOID descriptor,
                             PVOID *optional_descriptors, DWORD optional_count, ACCESS_REPLY *reply, HANDLE *results)
{
    CLIENT_CONTEXT *context = context_of(handle);
    if (!context || !request || !descriptor || !reply || !reply->GrantedAccessMask || !reply->Error ||
        reply->ResultListLength != 1 || (optional_count && !optional_descriptors)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (results)
        *results = nullptr;
    /* generic rights are not mapped here, the request has to name the rights of the object. */
    DWORD generic_mapping[4] = {};
    DWORD privileges[16], privileges_length = sizeof(privileges), granted = 0;
    BOOL status = FALSE;
    if (!AccessCheck(descriptor, context->Token, request->DesiredAccess, generic_mapping, privileges, &privileges_length,
                     &granted, &status)) {
        if (GetLastError() != ERROR_ACCESS_DENIED)
            return FALSE;
        granted = 0;
        status = FALSE;
    }
    reply->GrantedAccessMask[0] = status ? granted : 0;
    reply->Error[0] = status ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    if (reply->SaclEvaluationResults)
        reply->SaclEvaluationResults[0] = 0;
    return TRUE;
}

BOOL WINAPI AuthzGetInformationFromContext(HANDLE handle, int information, DWORD length, PDWORD needed, PVOID buffer)
{
    CLIENT_CONTEXT *context = context_of(handle);
    if (!context || !needed) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    switch (information) {
    case 1:
        return GetTokenInformation(context->Token, TOKEN_USER_, buffer, length, needed);
    case 2:
        return GetTokenInformation(context->Token, TOKEN_GROUPS_, buffer, length, needed);
    case 4:
        return GetTokenInformation(context->Token, TOKEN_PRIVILEGES_, buffer, length, needed);
    case 5:
        *needed = sizeof(LARGE_INTEGER);
        if (length < *needed) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *(LARGE_INTEGER *)buffer = context->Expiration;
        return TRUE;
    case 7:
        *needed = sizeof(LUID);
        if (length < *needed) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *(LUID *)buffer = context->Identifier;
        return TRUE;
    case 10: {
        /* the logon session is the first field after the token id of the statistics. */
        BYTE statistics[64];
        DWORD size = sizeof(statistics);
        *needed = sizeof(LUID);
        if (length < *needed) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        if (!GetTokenInformation(context->Token, TOKEN_STATISTICS_, statistics, size, &size))
            return FALSE;
        memcpy(buffer, statistics + sizeof(LUID), sizeof(LUID));
        return TRUE;
    }
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}
