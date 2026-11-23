#include <mime/win32/winbase.h>

typedef void(WINAPI *APPSTATE_CHANGE_ROUTINE)(BOOLEAN quiesced, PVOID context);
typedef void(WINAPI *APPCONSTRAIN_CHANGE_ROUTINE)(BOOLEAN constrained, PVOID context);

typedef struct APPSTATE_REGISTRATION {
    APPSTATE_CHANGE_ROUTINE routine;
    PVOID context;
} APPSTATE_REGISTRATION;

static INIT_ONCE connect_once = INIT_ONCE_STATIC_INIT;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/*
 * windows asks the process state manager about the caller once, over rpc, and the manager refuses a program without
 * package identity. the registration still succeeds and its routine never runs, which is all mime has: no process
 * lifetime manager ever suspends a program here. the first registration leaves the last error at 0 as that rpc
 * exchange does.
 */
static BOOL CALLBACK connect(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

ULONG WINAPI RegisterAppStateChangeNotification(APPSTATE_CHANGE_ROUTINE routine, PVOID context,
                                                APPSTATE_REGISTRATION **registration)
{
    APPSTATE_REGISTRATION *r = HeapAlloc(GetProcessHeap(), 0, sizeof(*r));
    if (!r)
        return ERROR_NOT_ENOUGH_MEMORY;
    r->routine = routine;
    r->context = context;
    InitOnceExecuteOnce(&connect_once, connect, nullptr, nullptr);
    *registration = r;
    return ERROR_SUCCESS;
}

BOOLEAN WINAPI UnregisterAppStateChangeNotification(APPSTATE_REGISTRATION *registration)
{
    return HeapFree(GetProcessHeap(), 0, registration) != FALSE;
}

/* windows 11 answers success and keeps nothing, so a program is never told it is constrained. */
ULONG WINAPI RegisterAppConstrainedChangeNotification(APPCONSTRAIN_CHANGE_ROUTINE routine, PVOID context,
                                                      PVOID *registration)
{
    return ERROR_SUCCESS;
}

void WINAPI UnregisterAppConstrainedChangeNotification(PVOID registration)
{
}
