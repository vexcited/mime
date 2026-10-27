/*
 * RpcEpMap.dll: the RpcEptMapper service. rpcrt4 keeps mime's endpoint mapper, so the service only reports that it
 * runs.
 */
#include <mime/svcstub.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    svc_stub_run(argc, argv);
}
