#include "ole32_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        OleProcessAttach();
        break;
    case DLL_THREAD_DETACH:
        OleThreadDetach();
        break;
    }
    return TRUE;
}
