/*
 * SHCreateLocalServerRunDll: the entry rundll32 calls to run one of the shell's local servers, as the registry says for the
 * classes it holds: `rundll32.exe shell32.dll,SHCreateLocalServerRunDll {clsid}`. Windows has the desktop do this, the
 * classes are served by the process of the shell; Mime has no explorer, so the server process is the shell for the
 * programs that look for one: it registers the class of the shell's windows and answers for the desktop.
 *
 * the class is made by the in process server of the class that backs it, explorerframe's factory for the list of shell
 * windows, and registered as a local server for every client. The call does not return before the process is told to
 * quit, it runs the messages of the thread, which the calls from other processes come in on.
 */
#include "shell32_private.h"
#include <mime/win32/shldisp.h>

typedef struct LOCAL_CLASS {
    const GUID *Clsid;
    /* the class that makes the factory, in the dll the registry names for it. */
    const GUID *Backing;
} LOCAL_CLASS;

static const GUID CLSID_ShellWindowsFactory = {0xD92BD3B9, 0x99A0, 0x4334, {0xA4, 0x97, 0x11, 0xBC, 0xB0, 0x93, 0xE9, 0xD2}};

/* the classes the shell serves that Mime has an implementation of. */
static const LOCAL_CLASS g_classes[] = {
    {&CLSID_ShellWindows, &CLSID_ShellWindowsFactory},
};

/* the class at the start of text, which may go on with other words: the line of a server ends in -Embedding. */
static BOOL parse_class(LPCSTR text, GUID *clsid)
{
    while (*text == ' ')
        text++;
    WCHAR wide[40];
    int i = 0;
    for (; text[i] && text[i] != '}' && i < 38; i++)
        wide[i] = (WCHAR)(UCHAR)text[i];
    if (text[i] != '}')
        return FALSE;
    wide[i++] = L'}';
    wide[i] = 0;
    return SUCCEEDED(CLSIDFromString(wide, clsid));
}

SHSTDAPI void CALLBACK SHCreateLocalServerRunDll(HWND window, HINSTANCE instance, LPSTR command, int show)
{
    GUID clsid;
    if (!command || !parse_class(command, &clsid))
        return;
    const LOCAL_CLASS *served = nullptr;
    for (size_t i = 0; i < sizeof(g_classes) / sizeof(g_classes[0]); i++)
        if (IsEqualCLSID(&clsid, g_classes[i].Clsid))
            served = &g_classes[i];
    if (!served)
        return;
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return;
    IClassFactory *factory;
    DWORD cookie;
    if (SUCCEEDED(CoCreateInstance(served->Backing, nullptr, CLSCTX_INPROC_SERVER, &IID_IClassFactory, (void **)&factory))) {
        if (SUCCEEDED(CoRegisterClassObject(&clsid, (IUnknown *)factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE,
                                            &cookie))) {
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            CoRevokeClassObject(cookie);
        }
        factory->lpVtbl->Release(factory);
    }
    CoUninitialize();
}
