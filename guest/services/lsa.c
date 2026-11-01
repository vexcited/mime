/*
 * services.exe: the exchange of identities between the two ends of a local authentication.
 *
 * lsass keeps the logon sessions on windows and a server that accepts a client's authentication asks it for the client's
 * token. here the client asks the manager to keep the token it speaks with under a ticket, and the server that gets the
 * ticket in the client's last message asks for the token. a ticket is made of sixteen random bytes, serves once, and
 * the token it stands for goes to the process that asks as a handle of its own.
 */
#include "services.h"

#define SecurityImpersonation_ 2
#define PROCESS_DUP_HANDLE_ 0x0040
#define PROCESS_QUERY_LIMITED_INFORMATION_ 0x1000
#define TOKEN_USER_CLASS 1
#define MAX_TICKETS 256
/* a ticket that nobody redeemed is dropped after this long, in 100 ns units. */
#define TICKET_LIFETIME (10ll * 60 * 10000000)

BOOLEAN WINAPI SystemFunction036(PVOID buffer, ULONG length);

typedef struct TICKET {
    struct TICKET *Next;
    UCHAR Id[16];
    HANDLE Token;
    UINT64 Made;
} TICKET;

static TICKET *g_tickets;
static ULONG g_ticket_count;

static UINT64 now(void)
{
    FILETIME t;
    GetSystemTimeAsFileTime(&t);
    return ((UINT64)t.dwHighDateTime << 32) | t.dwLowDateTime;
}

static void drop(TICKET **link)
{
    TICKET *t = *link;
    *link = t->Next;
    CloseHandle(t->Token);
    SvcFree(t);
    g_ticket_count--;
}

static void expire(void)
{
    UINT64 limit = now() - TICKET_LIFETIME;
    TICKET **link = &g_tickets;
    while (*link) {
        if ((*link)->Made < limit)
            drop(link);
        else
            link = &(*link)->Next;
    }
}

DWORD SspiPublish(HANDLE pipe, UCHAR *id)
{
    if (!ImpersonateNamedPipeClient(pipe))
        return GetLastError();
    HANDLE token = nullptr, copy = nullptr;
    BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &token);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    RevertToSelf();
    if (error)
        return error;
    if (!DuplicateTokenEx(token, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation_, TokenImpersonation, &copy))
        error = GetLastError();
    CloseHandle(token);
    if (error)
        return error;
    expire();
    /* a table that is full gives up its oldest ticket. */
    if (g_ticket_count >= MAX_TICKETS) {
        TICKET **oldest = &g_tickets;
        for (TICKET **link = &g_tickets; *link; link = &(*link)->Next)
            if ((*link)->Made < (*oldest)->Made)
                oldest = link;
        drop(oldest);
    }
    TICKET *t = SvcAllocZero(sizeof(*t));
    if (!t || !SystemFunction036(t->Id, sizeof(t->Id))) {
        SvcFree(t);
        CloseHandle(copy);
        return t ? ERROR_GEN_FAILURE : ERROR_NOT_ENOUGH_MEMORY;
    }
    t->Token = copy;
    t->Made = now();
    t->Next = g_tickets;
    g_tickets = t;
    g_ticket_count++;
    memcpy(id, t->Id, sizeof(t->Id));
    return ERROR_SUCCESS;
}

/*
 * the token may be opened again by the process it goes to, as the token lsass makes can be: the system, the
 * administrators and the account of that process are given all access to it.
 */
static void allow_process(HANDLE process, HANDLE token)
{
    HANDLE own;
    if (!OpenProcessToken(process, TOKEN_QUERY, &own))
        return;
    UCHAR buffer[256];
    DWORD returned;
    WCHAR *sid_text = nullptr;
    if (GetTokenInformation(own, TOKEN_USER_CLASS, buffer, sizeof(buffer), &returned) &&
        ConvertSidToStringSidW(*(PSID *)buffer, &sid_text)) {
        WCHAR sddl[200];
        _snwprintf(sddl, RTL_NUMBER_OF(sddl), L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;%ls)", sid_text);
        PSECURITY_DESCRIPTOR sd;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, &sd, nullptr)) {
            SetKernelObjectSecurity(token, DACL_SECURITY_INFORMATION, sd);
            LocalFree(sd);
        }
        LocalFree(sid_text);
    }
    CloseHandle(own);
}

/* the token of a ticket, as a handle in the process that asks, and the ticket is spent. */
DWORD SspiRedeem(DWORD pid, const UCHAR *id, UINT64 *handle)
{
    expire();
    for (TICKET **link = &g_tickets; *link; link = &(*link)->Next) {
        /* every byte is compared, so the time taken tells nothing about the ticket. */
        UCHAR difference = 0;
        for (int i = 0; i < 16; i++)
            difference |= (*link)->Id[i] ^ id[i];
        if (difference)
            continue;
        HANDLE process = OpenProcess(PROCESS_DUP_HANDLE_ | PROCESS_QUERY_LIMITED_INFORMATION_, FALSE, pid);
        if (!process)
            return GetLastError();
        allow_process(process, (*link)->Token);
        HANDLE remote = nullptr;
        DWORD error = DuplicateHandle(GetCurrentProcess(), (*link)->Token, process, &remote, TOKEN_ALL_ACCESS, FALSE, 0)
                          ? ERROR_SUCCESS
                          : GetLastError();
        CloseHandle(process);
        if (error)
            return error;
        *handle = (UINT64)(ULONG_PTR)remote;
        drop(link);
        return ERROR_SUCCESS;
    }
    return ERROR_NOT_FOUND;
}
