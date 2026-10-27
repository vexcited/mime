/*
 * rpcss.dll: the RpcSs and DcomLaunch services. mime keeps the class table and the running object table in shared
 * sections of ole32, so both services only report that they run.
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
