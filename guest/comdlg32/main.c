/*
 * comdlg32 entry point.
 */
#include "comdlg32_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DlgProcessAttach();
    return TRUE;
}
