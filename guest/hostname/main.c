/*
 * hostname.exe: prints the name of the computer. the only option, -s, is refused as it always was.
 */
#include "tool.h"

int tool_main(void)
{
    if (tu_argc == 2 && tu_eq(tu_argv[1], L"-s")) {
        tu_eprint(L"sethostname: Use the Network Control Panel Applet to set hostname.\nhostname -s is not supported.\n");
        return 1;
    }
    if (tu_argc != 1) {
        tu_print(L"\nPrints the name of the current host.\n\nhostname\n\n");
        return 1;
    }
    WCHAR name[256];
    DWORD size = TU_ARRAY(name);
    if (!GetComputerNameExW(ComputerNameDnsHostname, name, &size)) {
        tu_eprint(L"hostname: gethostname: error %lu\n", GetLastError());
        return 1;
    }
    tu_print(L"%s\n", name);
    return 0;
}
