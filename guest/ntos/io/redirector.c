/*
 * the drivers of the workstation's redirector. Mime has no network to reach with it, so nothing is served here, but the
 * workstation service depends on them: a service that checks that they run finds their objects, as it does the one
 * of Tcpip.
 */
#include "../ntos.h"

static NTSTATUS NTAPI RedirectorDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    return STATUS_SUCCESS;
}

void IopInitializeRedirector(void)
{
    static const PCWSTR drivers[] = {L"\\FileSystem\\Mup",     L"\\FileSystem\\rdbss",  L"\\FileSystem\\mrxsmb",
                                     L"\\FileSystem\\MRxSmb20", L"\\FileSystem\\Bowser", L"\\Driver\\nsiproxy"};
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(drivers); i++) {
        NTSTATUS status = IoCreateDriver(drivers[i], RedirectorDriverEntry, nullptr);
        if (!NT_SUCCESS(status))
            KiLogPrintf("driver object %u of the redirector failed: %08x", (unsigned)i, status);
    }
}
