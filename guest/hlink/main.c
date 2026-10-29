/*
 * hlink.dll: hyperlinks. the browse context, the links and their frames are not here. what a program can do with a
 * target is open it as the shell does, and ask whether a file is an internet shortcut.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/shellapi.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

typedef ULONG_PTR ARG;

#define E_INVALIDARG_ ((HRESULT)0x80070057)
#define E_NOTIMPL_ ((HRESULT)0x80004001)
#define E_FAIL_ ((HRESULT)0x80004005)
#define CLASS_E_CLASSNOTAVAILABLE_ ((HRESULT)0x80040111)

#define NOT_IMPLEMENTED_1(name) HRESULT WINAPI name(ARG a) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_2(name) HRESULT WINAPI name(ARG a, ARG b) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_3(name) HRESULT WINAPI name(ARG a, ARG b, ARG c) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_4(name) HRESULT WINAPI name(ARG a, ARG b, ARG c, ARG d) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_5(name) HRESULT WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_6(name) HRESULT WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e, ARG f) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_7(name) HRESULT WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e, ARG f, ARG g) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_8(name) HRESULT WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e, ARG f, ARG g, ARG h) { return E_NOTIMPL_; }

NOT_IMPLEMENTED_8(HlinkCreateFromMoniker)
NOT_IMPLEMENTED_8(HlinkCreateFromString)
NOT_IMPLEMENTED_6(HlinkCreateFromData)
NOT_IMPLEMENTED_3(HlinkCreateBrowseContext)
NOT_IMPLEMENTED_5(HlinkClone)
NOT_IMPLEMENTED_7(HlinkOnNavigate)
NOT_IMPLEMENTED_6(HlinkUpdateStackItem)
NOT_IMPLEMENTED_4(HlinkOnRenameDocument)
NOT_IMPLEMENTED_6(HlinkNavigate)
NOT_IMPLEMENTED_7(HlinkResolveMonikerForData)
NOT_IMPLEMENTED_7(HlinkResolveStringForData)
NOT_IMPLEMENTED_3(OleSaveToStreamEx)
NOT_IMPLEMENTED_5(HlinkParseDisplayName)
NOT_IMPLEMENTED_1(HlinkQueryCreateFromData)
NOT_IMPLEMENTED_2(HlinkSetSpecialReference)
NOT_IMPLEMENTED_2(HlinkGetSpecialReference)
NOT_IMPLEMENTED_6(HlinkCreateShortcut)
NOT_IMPLEMENTED_6(HlinkResolveShortcut)
NOT_IMPLEMENTED_3(HlinkResolveShortcutToString)
NOT_IMPLEMENTED_8(HlinkCreateShortcutFromString)
NOT_IMPLEMENTED_3(HlinkGetValueFromParams)
NOT_IMPLEMENTED_8(HlinkCreateShortcutFromMoniker)
NOT_IMPLEMENTED_3(HlinkResolveShortcutToMoniker)
NOT_IMPLEMENTED_3(HlinkTranslateURL)
NOT_IMPLEMENTED_7(HlinkCreateExtensionServices)
NOT_IMPLEMENTED_3(HlinkPreprocessMoniker)

/* a target is opened by the shell, with its location after it, as the frame of a browser would be asked to. */
HRESULT WINAPI HlinkNavigateToStringReference(LPCWSTR target, LPCWSTR location, PVOID site, DWORD site_data, PVOID frame,
                                              DWORD flags, PVOID context, PVOID callback, PVOID browse_context)
{
    if (!target)
        return E_INVALIDARG_;
    WCHAR full[2048];
    int n = 0;
    for (; target[n] && n < 2000; n++)
        full[n] = target[n];
    if (location && location[0]) {
        full[n++] = L'#';
        for (int i = 0; location[i] && n < 2040; i++)
            full[n++] = location[i];
    }
    full[n] = 0;
    return (ULONG_PTR)ShellExecuteW(nullptr, L"open", full, nullptr, nullptr, SW_SHOWNORMAL) > 32 ? 0 : E_FAIL_;
}

/* an internet shortcut is a file named for it. */
BOOL WINAPI HlinkIsShortcut(LPCWSTR file_name)
{
    if (!file_name)
        return FALSE;
    int n = lstrlenW(file_name);
    if (n < 4 || file_name[n - 4] != L'.')
        return FALSE;
    return CompareStringOrdinal(file_name + n - 4, 4, L".url", 4, TRUE) == CSTR_EQUAL;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return 0;
}

HRESULT WINAPI DllGetClassObject(const GUID *clsid, const GUID *iid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE_;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return 0;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return 0;
}
