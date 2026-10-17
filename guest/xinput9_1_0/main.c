/*
 * xinput9_1_0.dll: windows' wrapper over xinput1_4, loaded on first use,
 * whose capabilities are the fixed ones of the first xinput.
 */
#define MIME_BUILD_XINPUT
#include <mime/win32/winbase.h>
#include <mime/win32/objbase.h>
#include <mime/win32/xinput.h>

typedef DWORD(WINAPI *GET_STATE)(DWORD, XINPUT_STATE *);
typedef DWORD(WINAPI *SET_STATE)(DWORD, XINPUT_VIBRATION *);
typedef DWORD(WINAPI *GET_CAPS)(DWORD, DWORD, XINPUT_CAPABILITIES *);

static HMODULE g_xinput;
static GET_STATE g_get_state;
static SET_STATE g_set_state;
static GET_CAPS g_get_caps;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_DETACH && g_xinput)
        FreeLibrary(g_xinput);
    return TRUE;
}

static DWORD load(void)
{
    if (g_xinput)
        return ERROR_SUCCESS;
    HMODULE module = LoadLibraryExW(L"XInput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
        return ERROR_MOD_NOT_FOUND;
    g_get_caps = (GET_CAPS)(void *)GetProcAddress(module, "XInputGetCapabilities");
    g_get_state = (GET_STATE)(void *)GetProcAddress(module, "XInputGetState");
    g_set_state = (SET_STATE)(void *)GetProcAddress(module, "XInputSetState");
    if (!g_get_caps || !g_get_state || !g_set_state) {
        FreeLibrary(module);
        return ERROR_PROC_NOT_FOUND;
    }
    g_xinput = module;
    return ERROR_SUCCESS;
}

XINPUTAPI DWORD WINAPI XInputGetState(DWORD index, XINPUT_STATE *state)
{
    DWORD result = load();
    return result ? result : g_get_state(index, state);
}

XINPUTAPI DWORD WINAPI XInputSetState(DWORD index, XINPUT_VIBRATION *vibration)
{
    DWORD result = load();
    return result ? result : g_set_state(index, vibration);
}

XINPUTAPI DWORD WINAPI XInputGetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES *caps)
{
    DWORD result = load();
    if (result)
        return result;
    result = g_get_caps(index, flags, caps);
    if (result == ERROR_SUCCESS)
        *caps = (XINPUT_CAPABILITIES){XINPUT_DEVTYPE_GAMEPAD, XINPUT_DEVSUBTYPE_GAMEPAD, XINPUT_CAPS_VOICE_SUPPORTED,
                                      {0xF3FF, 1, 1, 1, 1, 1, 1}, {1, 1}};
    return result;
}

/* no headset is ever found: both ids stay empty, the result telling only whether a pad is there. */
XINPUTAPI DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD index, GUID *render, GUID *capture)
{
    if (!render || !capture)
        return ERROR_BAD_ARGUMENTS;
    *render = *capture = GUID_NULL;
    XINPUT_STATE scratch;
    return XInputGetState(index, &scratch);
}
