/*
 * windows script files: the jobs of an XML file, each with its scripts, objects and references.
 */
#include "wsh.h"

HRESULT WshRunWsf(PCWSTR path, PCWSTR job, int argc, PWSTR *argv)
{
    WshPrintF(RES_BASE + 21, 3309, L".wsf");
    return WSH_E_REPORTED;
}
