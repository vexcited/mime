/*
 * comctl32 entry point.
 */
#include "comctl32_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        SubclassProcessAttach();
    return TRUE;
}
