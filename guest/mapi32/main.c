/*
 * mapi32.dll: the mail api. no mail system is installed, so no session opens: the extended api cannot log on, the simple
 * api cannot log on or send, and the property and table helpers have nothing to work on. what is here that works
 * without a mail system is the buffers that the api hands out, which are chains that one call frees.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

typedef ULONG_PTR ARG;

#define MAPI_E_NOT_INITIALIZED ((HRESULT)0x80040605)
#define MAPI_E_LOGON_FAILED ((HRESULT)0x80040111)
#define MAPI_E_INVALID_PARAMETER ((HRESULT)0x80070057)
#define MAPI_E_NOT_ENOUGH_MEMORY ((HRESULT)0x8007000E)
#define MAPI_E_NO_SUPPORT ((HRESULT)0x80040102)
#define MAPI_E_NOT_FOUND ((HRESULT)0x8004010F)
#define MAPI_E_FAILURE 2
#define MAPI_E_LOGIN_FAILURE 3

__declspec(dllimport) HRESULT WINAPI CoGetMalloc(DWORD context, void **malloc);

/* ------------------------------------------------------------------ buffers */

/* each block begins with the link to the next block that was allocated with it. */
typedef struct BLOCK {
    struct BLOCK *Next;
    SIZE_T Size;
} BLOCK;

#define BLOCK_HEADER ((sizeof(BLOCK) + 15) & ~(SIZE_T)15)

HRESULT WINAPI MAPIAllocateBuffer(ULONG size, void **buffer)
{
    if (!buffer)
        return MAPI_E_INVALID_PARAMETER;
    *buffer = nullptr;
    BLOCK *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BLOCK_HEADER + size);
    if (!block)
        return MAPI_E_NOT_ENOUGH_MEMORY;
    block->Size = size;
    *buffer = (BYTE *)block + BLOCK_HEADER;
    return 0;
}

/* more is added to the chain of a buffer made before, and freed with it. */
HRESULT WINAPI MAPIAllocateMore(ULONG size, void *object, void **buffer)
{
    if (!buffer)
        return MAPI_E_INVALID_PARAMETER;
    *buffer = nullptr;
    if (!object)
        return MAPIAllocateBuffer(size, buffer);
    BLOCK *head = (BLOCK *)((BYTE *)object - BLOCK_HEADER);
    BLOCK *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BLOCK_HEADER + size);
    if (!block)
        return MAPI_E_NOT_ENOUGH_MEMORY;
    block->Size = size;
    block->Next = head->Next;
    head->Next = block;
    *buffer = (BYTE *)block + BLOCK_HEADER;
    return 0;
}

ULONG WINAPI MAPIFreeBuffer(void *buffer)
{
    if (!buffer)
        return 0;
    BLOCK *block = (BLOCK *)((BYTE *)buffer - BLOCK_HEADER);
    while (block) {
        BLOCK *next = block->Next;
        HeapFree(GetProcessHeap(), 0, block);
        block = next;
    }
    return 0;
}

void *WINAPI MAPIGetDefaultMalloc(void)
{
    void *malloc = nullptr;
    return SUCCEEDED(CoGetMalloc(1, &malloc)) ? malloc : nullptr;
}

/* ------------------------------------------------------------------ the extended api */

HRESULT WINAPI MAPIInitialize(void *init)
{
    return 0;
}

void WINAPI MAPIUninitialize(void)
{
}

HRESULT WINAPI MAPILogonEx(ARG ui, ARG profile, ARG password, ULONG flags, void **session)
{
    if (session)
        *session = nullptr;
    return MAPI_E_LOGON_FAILED;
}

HRESULT WINAPI MAPIAdminProfiles(ULONG flags, void **admin)
{
    if (admin)
        *admin = nullptr;
    return MAPI_E_NOT_INITIALIZED;
}

/* a table or a property object comes from a mail provider, and none can have been given here. */
HRESULT WINAPI HrGetOneProp(void *object, ULONG tag, void **property)
{
    if (property)
        *property = nullptr;
    return object ? MAPI_E_NOT_FOUND : MAPI_E_INVALID_PARAMETER;
}

HRESULT WINAPI HrSetOneProp(void *object, void *property)
{
    return object && property ? MAPI_E_NO_SUPPORT : MAPI_E_INVALID_PARAMETER;
}

HRESULT WINAPI HrQueryAllRows(void *table, void *columns, void *restriction, void *sort, LONG max_rows, void **rows)
{
    if (rows)
        *rows = nullptr;
    return table && rows ? MAPI_E_NO_SUPPORT : MAPI_E_INVALID_PARAMETER;
}

HRESULT WINAPI OpenIMsgOnIStg(ARG session, ARG allocate, ARG more, ARG release, ARG memory, ARG support, ARG storage,
                              ARG callback, ARG caller_data, ARG flags, void **message)
{
    if (message)
        *message = nullptr;
    return storage && message ? MAPI_E_NO_SUPPORT : MAPI_E_INVALID_PARAMETER;
}

/* ------------------------------------------------------------------ the simple api */

ULONG WINAPI MAPILogon(ARG ui, ARG profile, ARG password, ULONG flags, ULONG reserved, ULONG *session)
{
    if (session)
        *session = 0;
    return MAPI_E_LOGIN_FAILURE;
}

ULONG WINAPI MAPILogoff(ARG session, ARG ui, ULONG flags, ULONG reserved)
{
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPISendMail(ARG session, ARG ui, ARG message, ULONG flags, ULONG reserved)
{
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPISendMailW(ARG session, ARG ui, ARG message, ULONG flags, ULONG reserved)
{
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPISendDocuments(ARG ui, ARG delimiter, ARG paths, ARG names, ULONG reserved)
{
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPIResolveName(ARG session, ARG ui, ARG name, ULONG flags, ULONG reserved, void **recipient)
{
    if (recipient)
        *recipient = nullptr;
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPIFindNext(ARG session, ARG ui, ARG type, ARG seed, ULONG flags, ULONG reserved, char *id)
{
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPIReadMail(ARG session, ARG ui, ARG id, ULONG flags, ULONG reserved, void **message)
{
    if (message)
        *message = nullptr;
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPIDeleteMail(ARG session, ARG ui, ARG id, ULONG flags, ULONG reserved)
{
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPISaveMail(ARG session, ARG ui, ARG message, ULONG flags, ULONG reserved, ARG id)
{
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPIAddress(ARG session, ARG ui, ARG caption, ULONG edit_fields, ARG labels, ULONG recipients_count,
                         ARG recipients, ULONG flags, ULONG reserved, ULONG *new_count, void **new_recipients)
{
    if (new_count)
        *new_count = 0;
    if (new_recipients)
        *new_recipients = nullptr;
    return MAPI_E_FAILURE;
}

ULONG WINAPI MAPIDetails(ARG session, ARG ui, ARG recipient, ULONG flags, ULONG reserved)
{
    return MAPI_E_FAILURE;
}
